// opentony_native.dll (Phase 5.0): the Kits of src/runtime/, run inside the retail process.
//
// opentony.dll loads this DLL before it installs the hooks and calls `ot_kit_attach`, which
//   - maps the scratch area (pcplatform::kScratchBase) at its fixed address: the Kits keep there the
//     records retail keeps in stack locals;
//   - makes one RetailMemory live (the process's own memory) and one RetailServices live (each
//     reported call becomes a real call of the retail function, through its patched entry, so a hooked
//     callee runs native when its hook is active, exactly as when retail calls it);
//   - creates every Kit once over them (the generated `make_kits`);
//   - returns the generated adapters, one per Kit hook.
#include "../kit_abi.h"
#include "guest_view.hpp"
#include "guest_arena.hpp"
#include "kit_bridge.hpp"
#include "pc_platform.hpp"
#include "seam_addresses_generated.hpp"
#ifdef OPENTONY_STANDALONE
#include "standalone_thunks_generated.hpp"
#include "standalone_options.hpp"
#include "standalone_music.hpp"
#endif

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace opentony::kitdll {
namespace {

// 11d (docs/PORT64_AUDIT.md D3): a production DLL holds the arena backend only; the sparse region backend is the fixtures'
// (OPENTONY_GUEST_MEMORY_FLAT, set on opentony_kits_runtime in CMakeLists.txt; `tony hooks build` also reads the DLL's symbols).
static_assert(OPENTONY_GUEST_MEMORY_FLAT && !runtime::SparseRetailMemory<RetailMemory>,
              "the production DLLs must be built with the arena-only RetailMemory");

const OtKitHost* g_host = nullptr;
RetailMemory g_memory;
RetailServices g_services;

void log_line(const std::string& line) {
    if (g_host != nullptr && g_host->log != nullptr) {
        g_host->log(line.c_str());
    }
}

// True when the printf format at `address` converts more arguments than `given` (`%%` converts none).
bool format_wants_more(std::uint32_t address, std::size_t given) {
    const char* text = arena::host<const char>(address);
    std::size_t wanted = 0;
    for (std::size_t k = 0; k < 512 && text[k] != '\0'; ++k) {
        if (text[k] == '%') {
            if (text[k + 1] == '%') {
                ++k;
            } else {
                ++wanted;
            }
        }
    }
    return wanted > given;
}

// ---- live memory -----------------------------------------------------------------------------------------

// guest addresses, as RetailMemory's probe is given them (11b: the host bytes are at the arena's base plus the address)
bool mapped(std::uint32_t address, std::size_t size, bool write) {
    const std::uint64_t end = arena::base() + static_cast<std::uint64_t>(address) + size;
    std::uint64_t at = arena::base() + address;
    // 12 s1: a loop that asks word by word (the fog table, a cleared buffer) made one VirtualQuery system call per word, which is cheap under Wine and took minutes on Windows.
    // Memory that was found readable and writable stays so: nothing in the program decommits or re-protects the arena or the scratch buffer, so the last such span answers
    // the questions that fall inside it.
#ifdef OPENTONY_STANDALONE   // not in the hooked build: retail's own code frees memory
    thread_local std::uint64_t good_begin = 0, good_end = 0;
    if (at >= good_begin && end <= good_end) {
        return true;
    }
#endif
    while (at < end) {
        MEMORY_BASIC_INFORMATION info;
        // `at` is a host address, not a guest word (the walk is over host pages)
        if (VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), &info, sizeof info) != sizeof info ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (write && (info.Protect & writable) == 0) {
            return false;
        }
        const std::uint64_t region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
#ifdef OPENTONY_STANDALONE
        if ((info.Protect & PAGE_READWRITE) == PAGE_READWRITE) {
            good_begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            good_end = region_end;
        }
#endif
        at = region_end;
    }
    return true;
}

// The replays' scratch window (the stack locals of the retail functions at fixed addresses, which Wine
// does not let a process map): live, it is a buffer anywhere, aliased by the live memory.
bool map_scratch() {
    constexpr std::uint32_t kBase = runtime::pcplatform::kScratchBase & ~0xffffu;
    constexpr std::size_t kBytes = 0x10000;
    static_assert(runtime::pcplatform::kScratchBase + runtime::pcplatform::kScratchBytes <= kBase + kBytes);
#ifdef OPENTONY_STANDALONE
    static_assert(kBase == arena::kScratchWindow && kBytes == arena::kScratchWindowSize);
    // 11b (docs/PORT64_AUDIT.md D7): the 64-bit arena covers the window's own guest addresses; it is committed there and needs no alias
    if (!arena::kFlat) {
        if (!arena::commit(kBase, kBytes)) {
            char line[96];
            std::snprintf(line, sizeof line, "kits: cannot commit the scratch window in the arena (%lu)", GetLastError());
            log_line(line);
            return false;
        }
        log_line("kits: scratch window committed in the guest arena");
        return true;
    }
#endif
    void* at = VirtualAlloc(nullptr, kBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (at == nullptr) {
        char line[96];
        std::snprintf(line, sizeof line, "kits: cannot allocate the scratch window (%lu)", GetLastError());
        log_line(line);
        return false;
    }
    g_memory.alias_live(kBase, kBytes, static_cast<std::uint8_t*>(at));
    char line[96];
    std::snprintf(line, sizeof line, "kits: scratch window %08X..%08X lives at %p", kBase, static_cast<unsigned>(kBase + kBytes), at);
    log_line(line);
    return true;
}

// ---- live services -----------------------------------------------------------------------------------------

template <std::size_t>
using Word = std::uint32_t;

// What a call goes to: a retail function at its address (the hooked build) or a native adapter / host function by its host address (the
// standalone). 11b: a host function pointer is 64 bits on the 64-bit build, so it is never carried in LiveCall's 32-bit `address`. On
// x86-64 the three conventions are one (the Microsoft x64 convention); the adapters are declared with the same parameter lists as these
// pointer types, so a call matches either way.
struct CallTarget {
    std::uintptr_t function;
    Convention convention;
    std::uint8_t stack_args;
};

CallTarget retail_target(const LiveCall& call) { return {static_cast<std::uintptr_t>(call.address), call.convention, call.stack_args}; }

template <std::size_t... I>
std::uint32_t call_cdecl(std::uintptr_t function, const std::uint32_t* a, std::index_sequence<I...>) {
    using F = std::uint32_t(__cdecl*)(Word<I>...);
    return reinterpret_cast<F>(function)(a[I]...);
}
template <std::size_t... I>
std::uint32_t call_stdcall(std::uintptr_t function, const std::uint32_t* a, std::index_sequence<I...>) {
    using F = std::uint32_t(__stdcall*)(Word<I>...);
    return reinterpret_cast<F>(function)(a[I]...);
}
// thiscall: ECX, then callee-popped stack arguments; __fastcall with an EDX it ignores is the same contract
template <std::size_t... I>
std::uint32_t call_thiscall(std::uintptr_t function, std::uint32_t self, const std::uint32_t* a, std::index_sequence<I...>) {
    using F = std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, Word<I>...);
    return reinterpret_cast<F>(function)(self, 0, a[I]...);
}

constexpr std::size_t kMaxStackArgs = 16;

template <std::size_t N>
std::uint32_t call_n(const CallTarget& target, std::uint32_t self, const std::uint32_t* a) {
    switch (target.convention) {
    case Convention::Cdecl:
        return call_cdecl(target.function, a, std::make_index_sequence<N>{});
    case Convention::Stdcall:
        return call_stdcall(target.function, a, std::make_index_sequence<N>{});
    case Convention::Thiscall:
        return call_thiscall(target.function, self, a, std::make_index_sequence<N>{});
    }
    return 0;
}

template <std::size_t... N>
std::uint32_t call_any(const CallTarget& target, std::uint32_t self, const std::uint32_t* a, std::index_sequence<N...>) {
    using Caller = std::uint32_t (*)(const CallTarget&, std::uint32_t, const std::uint32_t*);
    static constexpr Caller callers[] = {&call_n<N>...};
    return callers[target.stack_args](target, self, a);
}

const LiveCall* find_call(const std::string& service) {
    for (std::size_t k = 0; k < kLiveCallCount; ++k) {
        if (service == kLiveCalls[k].service) {
            return &kLiveCalls[k];
        }
    }
    return nullptr;
}

class LiveHost final : public runtime::ServiceHost {
public:
    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string&) override {
        const LiveCall* target = find_call(name);
        // a virtual call (pc_platform.hpp `virtual_call`): the address is the vtable slot's, the object comes first, and the method pops its own arguments
        LiveCall virtual_target{};
        if (target == nullptr && name.compare(0, 6, "vcall_") == 0 && !args.empty()) {
            virtual_target.service = "vcall";
            virtual_target.address = static_cast<std::uint32_t>(std::strtoul(name.c_str() + 6, nullptr, 16));
            virtual_target.convention = Convention::Thiscall;
            virtual_target.stack_args = static_cast<std::uint8_t>(args.size() - 1);
            target = &virtual_target;
        }
        if (target == nullptr) {
            throw runtime::ServiceFault("service '" + name + "' has no live call (re/config/native_hooks.yml services.live_calls)");
        }
        const bool self = target->convention == Convention::Thiscall;
        if (target->stack_args > kMaxStackArgs) {
            throw runtime::ServiceFault("service '" + name + "' has more stack arguments than the live host passes");
        }
        const std::size_t reported = static_cast<std::size_t>(target->stack_args) + (self ? 1u : 0u) - target->lead;
        if (target->variadic ? args.size() < reported || args.size() + target->lead > kMaxStackArgs :args.size() != reported) {
            throw runtime::ServiceFault("service '" + name + "' reported with " + std::to_string(args.size()) + " arguments, the retail function takes " +
                                        (target->variadic ? "at least " : "") + std::to_string(reported));
        }
        if (target->variadic && !args.empty() && format_wants_more(g_memory.live_address(args[0]), args.size() - 1)) {
            return 0;  // debug text only: the Kits report the format and not the numbers behind it, retail's printf would read stack garbage (a %s would fault)
        }
        LiveCall variable = *target;
        if (target->variadic) {
            variable.stack_args = static_cast<std::uint8_t>(args.size() + target->lead);
            target = &variable;
        }
        // an argument that points into the scratch window is passed as where that memory really is
        std::uint32_t stack[kMaxStackArgs] = {};
        std::size_t next = 0;
        for (std::size_t k = 0; k < target->lead; ++k) {
            stack[next++] = target->lead_value;
        }
        for (std::size_t k = self ? 1 : 0; k < args.size(); ++k) {
            stack[next++] = g_memory.live_address(args[k]);
        }
        // the callee runs retail code (or another hook's native, which sets the control word it needs)
        // under the caller's x87 control word, as when retail called it
        const std::uint16_t ours = x87_control();
        x87_load(t_caller_x87);
        const std::uint32_t result =
            call_any(retail_target(*target), self ? g_memory.live_address(args[0]) : 0, stack, std::make_index_sequence<kMaxStackArgs + 1>{});
        x87_load(ours);
        return result;
    }
};

LiveHost g_live_host;

#ifdef OPENTONY_STANDALONE
// ---- the standalone resolver (Phase 5.4a) -------------------------------------------------------------------
//
// No retail code is in the process: a retail function a Kit reports (`engine_<address>`, a `vcall_` through a vtable
// slot, a named service) is a native, found by address in the generated kNatives table, or a seam, found in
// standalone_seams.cpp. A call that is neither stops the process with the address, which is how the work still to be
// done shows itself.

const NativeEntry* find_native(std::uint32_t address) {
    const NativeEntry* end = kNatives + kNativeCount;
    const NativeEntry* at = std::lower_bound(kNatives, end, address, [](const NativeEntry& entry, std::uint32_t value) { return entry.address < value; });
    return at != end && at->address == address ? at : nullptr;
}

bool parse_address(const std::string& name, std::uint32_t& address) {
    for (const char* prefix : {"engine_", "vcall_"}) {
        const std::size_t length = std::strlen(prefix);
        if (name.compare(0, length, prefix) == 0 && name.size() == length + 8) {
            address = static_cast<std::uint32_t>(std::strtoul(name.c_str() + length, nullptr, 16));
            return true;
        }
    }
    return false;
}

struct ResolverStats {
    std::uint64_t natives = 0;
    std::uint64_t seams = 0;
};
ResolverStats g_stats;

std::string hex_address(std::uint32_t address) {
    char text[16];
    std::snprintf(text, sizeof text, "%08x", address);
    return text;
}

std::uint32_t resolve_and_call(std::uint32_t address, std::vector<std::uint32_t> args, const std::string& text, const std::string& what, bool virtual_call = false);

// Call the native or the seam at `address` with the arguments a Kit reported (a thiscall's object first).
std::uint32_t resolve_and_call_raw(std::uint32_t address, std::vector<std::uint32_t> args, const std::string& text, const std::string& what,
                                   bool virtual_call) {
    // 9: OT_RESOLVER_WATCH=<hex address>,... logs every call to those addresses with its arguments and the native that made it (a measurement mode)
    static const std::vector<std::uint32_t> watched = [] {
        std::vector<std::uint32_t> list;
        for (const char* at = std::getenv("OT_RESOLVER_WATCH"); at != nullptr && *at != '\0';) {
            char* next = nullptr;
            list.push_back(static_cast<std::uint32_t>(std::strtoul(at, &next, 16)));
            at = *next == ',' ? next + 1 : next;
        }
        return list;
    }();
    if (std::find(watched.begin(), watched.end(), address) != watched.end()) {
        std::string line = "watch: " + hex_address(address) + "(";
        for (std::size_t k = 0; k < args.size(); ++k) {
            line += (k != 0 ? ", " : "") + hex_address(args[k]);
        }
        log_line(line + ")" + (t_native != nullptr ? std::string(" in ") + t_native : std::string()) + " [poll " + std::to_string(platform_input_polls()) + "]");
        // OT_WATCH_DUMP=<hex offset>:<hex length>: also the object's words (its first argument) at that offset, one line per call
        static const char* dump = std::getenv("OT_WATCH_DUMP");
        if (dump != nullptr && *dump != '\0' && !args.empty()) {
            char* next = nullptr;
            const std::uint32_t offset = static_cast<std::uint32_t>(std::strtoul(dump, &next, 16));
            const std::uint32_t length = *next == ':' ? static_cast<std::uint32_t>(std::strtoul(next + 1, nullptr, 16)) : 0x40u;
            const auto* words = arena::host<const std::uint32_t>(g_memory.live_address(args[0]) + offset);
            std::string dumped = "dump: " + hex_address(address) + " +" + hex_address(offset) + ":";
            for (std::uint32_t k = 0; k < length / 4; ++k) {
                dumped += " " + hex_address(words[k]);
            }
            log_line(dumped + " [poll " + std::to_string(platform_input_polls()) + "]");
        }
    }
    std::uint32_t answered = 0;
    if (session_overrides(address, args.size() > 1 && (args[1] & 0xff) != 0, answered)) {
        return answered;
    }
    if (const NativeEntry* native = find_native(address)) {
        // a method that ignores `this` and returns or does nothing has `ret` and no arguments in the code, so its row says cdecl: the
        // Kit's report of it carries the object. The same for a stub the caller gives arguments (PCInput_NothingMotor takes none and is
        // called with two; 9: PsxStub_ReturnArg takes one and is called with five): a cdecl callee reads the arguments it has and
        // ignores the rest of what the caller pushed, and the caller pops it
        if (native->convention == Convention::Cdecl && args.size() > native->stack_args) {
            args.resize(native->stack_args);
        }
        // a virtual call hands the object over in ECX whatever the target is: a target that is not a thiscall (a method that
        // ignores `this` and returns a constant has no way to show it) takes its stack arguments only
        if (virtual_call && native->convention != Convention::Thiscall && !args.empty()) {
            args.erase(args.begin());
        }
        // 9: a method whose native ignores `this` has a stdcall row (both pop their stack arguments; Skater_GetResourceSlot), and a Kit that calls it as the
        // method it is reports the object first: one argument more than the row's, the first of them the object, which ECX would have carried
        if (native->convention == Convention::Stdcall && args.size() == static_cast<std::size_t>(native->stack_args) + 1) {
            args.erase(args.begin());
        }
        const bool self = native->convention == Convention::Thiscall;
        const std::size_t reported = static_cast<std::size_t>(native->stack_args) + (self ? 1u : 0u);
        if (args.size() != reported) {
            throw runtime::ServiceFault(what + ": reported with " + std::to_string(args.size()) + " arguments, the native " + native->name + " (0x" +
                                        hex_address(address) + ") takes " + std::to_string(reported));
        }
        const CallTarget target{reinterpret_cast<std::uintptr_t>(native->function), native->convention, native->stack_args};
        std::uint32_t stack[kMaxStackArgs] = {};
        for (std::size_t k = self ? 1 : 0; k < args.size(); ++k) {
            stack[k - (self ? 1 : 0)] = g_memory.live_address(args[k]);
        }
        ++g_stats.natives;
        // the callee is an adapter that sets the control word it needs and reads the caller's from t_caller_x87
        const std::uint16_t ours = x87_control();
        x87_load(t_caller_x87);
        const std::uint32_t result = call_any(target, self ? g_memory.live_address(args[0]) : 0, stack, std::make_index_sequence<kMaxStackArgs + 1>{});
        x87_load(ours);
        return result;
    }
    std::uint32_t result = 0;
    // a seam is host code: an argument that points into the Kits' scratch window is where that memory really is
    for (std::uint32_t& argument : args) {
        argument = g_memory.live_address(argument);
    }
    if (call_seam(address, args, text, result)) {
        ++g_stats.seams;
        return result;
    }
    // 9: a jump thunk (standalone_thunks_generated.hpp) is its target, called with the same arguments
    const Thunk* thunk = std::lower_bound(std::begin(kThunks), std::end(kThunks), address, [](const Thunk& entry, std::uint32_t value) { return entry.address < value; });
    if (thunk != std::end(kThunks) && thunk->address == address) {
        return resolve_and_call(thunk->target, std::move(args), text, what, virtual_call);
    }
    throw runtime::ServiceFault(what + ": 0x" + hex_address(address) + " is neither a native nor a seam with an implementation");
}

std::uint32_t scenario_live_address(std::uint32_t address) { return g_memory.live_address(address); }

// 10f: a Warehouse scenario (standalone_scenario.cpp) sees every call before and after it: the retail capture's seams, in the order a recorded run reaches them
std::uint32_t resolve_and_call(std::uint32_t address, std::vector<std::uint32_t> args, const std::string& text, const std::string& what, bool virtual_call) {
    const MusicLevelScope music_level(address);   // the custom soundtrack replaces songs only while a level runs (standalone_music.cpp)
    if (!scenario_active()) {
        if (is_options_reset(address)) {
            const std::uint32_t result = resolve_and_call_raw(address, std::move(args), text, what, virtual_call);
            options_after_options_reset();   // retail's defaults and a save bring their own levels and trick tips: the launcher's go back
            return result;
        }
        if (!options_watch_call(address)) {
            return resolve_and_call_raw(address, std::move(args), text, what, virtual_call);
        }
        // the player's quick start (standalone_options.cpp) sees the calls the same way until its level is launched (after it, a speed run's or a competition's)
        std::uint32_t quick_answer = 0;
        if (options_before_call(address, args, t_native, scenario_live_address, quick_answer)) {
            return quick_answer;
        }
        const std::uint32_t quick_result = resolve_and_call_raw(address, args, text, what, virtual_call);
        options_after_call(address, args, scenario_live_address);
        return quick_result;
    }
    std::uint32_t answered = 0;
    if (scenario_before_call(address, args, t_native, scenario_live_address, answered)) {
        return answered;
    }
    const std::uint32_t result = resolve_and_call_raw(address, args, text, what, virtual_call);
    scenario_after_call(address, args, scenario_live_address);
    return result;
}

// OT_RESOLVER_LENIENT=1: a call that resolves to nothing is logged once (by what) and answers 0 instead of stopping the process, so
// one run lists everything a stretch of game code still needs. A measurement mode: the answers after the first gap are not the game's.
bool lenient() {
    static const bool on = std::getenv("OT_RESOLVER_LENIENT") != nullptr;
    return on;
}

class StandaloneHost final : public runtime::ServiceHost {
public:
    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string& text) override {
        // OT_RESOLVER_TRACE=1: each service name the first time it is asked for, with the native it was asked from (a measurement mode)
        static const bool trace = std::getenv("OT_RESOLVER_TRACE") != nullptr;
        if (trace && traced_.insert(name).second) {
            log_line("resolve: " + name + (t_native != nullptr ? std::string(" (in ") + t_native + ")" : std::string()));
        }
        try {
            return resolve(name, args, text);
        } catch (const runtime::ServiceFault& fault) {
            if (!lenient()) {
                throw;
            }
            if (seen_.insert(fault.what()).second) {
                std::string where = t_native != nullptr ? std::string(" (in ") + t_native : std::string(" (");
                if (name.compare(0, 6, "vcall_") == 0 && !args.empty()) {
                    // which object asked: its address and its vtable
                    char object[48];
                    std::snprintf(object, sizeof object, "%sobject %08x, vtable %08x", t_native != nullptr ? ", " : "", args[0],
                                  runtime::guest::read_scalar<std::uint32_t>(g_memory, args[0]));
                    where += object;
                }
                log_line(std::string("unresolved: ") + fault.what() + (where == " (" ? std::string() : where + ")"));
            }
            return 0;
        }
    }

private:
    std::uint32_t resolve(const std::string& name, const std::vector<std::uint32_t>& given, const std::string& text) {
        std::vector<std::uint32_t> args = given;
        // 10g s17: the HUD names the string of a font call by its text and not by a pointer (the retail trace recorded the font, x and y only: "the string pointer is often a stack
        // buffer"): the font's Draw (this, x, y, string, 0) and Font_TextExtent (this, string) get the text in the scratch window, as the Kits that keep it in memory pass it
        constexpr std::uint32_t kHostText = 0x7ff0f800;      // (0x7ff0f000..0x7ff0f1d0 is Camera_ApplyEffects' frame)
        if ((name == "font_draw" && args.size() == 3) || ((name == "font_text_extent" || name == "font_text_width") && args.size() == 1)) {
            std::string line = text.substr(0, 0xff);
            line.push_back('\0');
            g_memory.write(kHostText, {reinterpret_cast<const std::uint8_t*>(line.data()), line.size()});
            args.push_back(kHostText);
            if (name == "font_draw") {
                args.push_back(0);
            }
        }
        std::uint32_t address = 0;
        std::vector<std::uint32_t> full = args;
        std::uint32_t answer = 0;
        // the Bink imports and the Win32 calls of the natives (standalone_movie.cpp): host code, so an argument into the Kits' scratch window is where that memory
        // really is, as for a seam
        std::vector<std::uint32_t> host_args = args;
        for (std::uint32_t& argument : host_args) {
            argument = g_memory.live_address(argument);
        }
        if (call_movie_service(name, host_args, answer)) {
            return answer;
        }
        if (call_surface_service(g_memory, name, args, answer)) {  // the DirectDraw surfaces of the texture layer (standalone_surfaces.cpp)
            return answer;
        }
        if (parse_address(name, address)) {
            return resolve_and_call(address, args, text, "service '" + name + "'", name.compare(0, 6, "vcall_") == 0);
        }
        // Services the window base and the font report by name that are virtual calls on their first argument: a font's Draw (slot 1: x, y, text,
        // argument; Font_Draw 0x0044a070) and a sprite's draw (slot 3: x, y, argument; the window's sprites and each glyph of Font_Draw). The probe names
        // them after stub sites (font_draw is an empty function), so their kServices address is not the call.
        // 9: the window base's element methods, which the Kits report by name (frontend_window.hpp: the method offsets 0, 4, 8, 0x14, 0x18, 0x1c), are the same
        // slots of the object's vtable whatever its class (5.4a, "Measured", item 5). 9a: a font's glyph sprite is deleted through slot 0 (Font_FreeGlyphs)
        static const std::pair<const char*, std::uint32_t> kVirtualServices[] = {
            {"font_draw", 1},      {"sprite_draw", 3},    {"blood_draw", 3},     {"font_sprite_draw", 3}, {"font_sprite_delete", 0}, {"virtual_delete", 0},
            {"element_update", 1},
            {"element_draw", 2},   {"element_snap_x", 5}, {"element_snap_y", 6},   {"element_hook", 7},
            // 10g s17: the HUD's text effects (the base class's vtable 0x005197b8: slot 1 TextEffect_Bind 0x004b94f0, slot 2 TextEffect_Unbind 0x004b95a0; init, update and
            // finished are the derived classes' own methods at +0xc, +0x10, +0x1c: hud_panel.hpp)
            {"effect_bind", 1},    {"effect_unbind", 2},  {"effect_init", 3},      {"effect_update", 4},      {"effect_finished", 7},
            // 10g s23: the font's per-glyph effect methods (frontend_text.hpp: +0x14 position, +0x18 colour, +0x20 glyph done)
            {"font_effect_position", 5}, {"font_effect_colour", 6}, {"font_effect_glyph", 8},
        };
        for (const auto& [service, slot] : kVirtualServices) {
            if (name == service && !args.empty()) {
                const std::uint32_t target = runtime::guest::read_scalar<std::uint32_t>(g_memory, runtime::guest::read_scalar<std::uint32_t>(g_memory, args[0]) + 4 * slot);
                static const bool trace = std::getenv("OT_RESOLVER_TRACE") != nullptr;
                if (trace && traced_.insert(name + "@" + hex_address(target)).second) {
                    log_line("resolve: " + name + " -> 0x" + hex_address(target) + " (vtable 0x" + hex_address(runtime::guest::read_scalar<std::uint32_t>(g_memory, args[0])) + ")");
                }
                return resolve_and_call(target, args, text, "service '" + name + "'", true);
            }
        }
        if (const LiveCall* named = find_call(name)) {
            address = named->address;
            full.insert(full.begin(), named->lead, named->lead_value);
        } else {
            const ServiceAddress* service = std::find_if(kServices, kServices + kServiceCount, [&](const ServiceAddress& item) { return name == item.name; });
            if (service == kServices + kServiceCount) {
                throw runtime::ServiceFault("service '" + name + "' has no address (not in services.live_calls, no probe stub names it)");
            }
            address = service->address;
        }
        return resolve_and_call(address, std::move(full), text, "service '" + name + "'");
    }

    std::set<std::string> seen_;
    std::set<std::string> traced_;
};

StandaloneHost g_standalone_host;
#endif  // OPENTONY_STANDALONE

}  // namespace

#ifdef OPENTONY_STANDALONE
std::uint32_t call_native_or_seam(std::uint32_t address, const std::vector<std::uint32_t>& args) {
    return resolve_and_call(address, args, {}, "call from a seam");
}

#endif  // OPENTONY_STANDALONE

[[noreturn]] void fatal(const char* hook, const char* what) noexcept {
    try {
        log_line(std::string("kits: FATAL in native ") + hook + ": " + what + "; stopping (a native that has started cannot hand over to retail)");
    } catch (...) {
    }
    TerminateProcess(GetCurrentProcess(), 0xe5);
    for (;;) {
    }
}

}  // namespace opentony::kitdll

extern "C" __declspec(dllexport) const OtKitEntry* __cdecl ot_kit_attach(const OtKitHost* host, std::uint32_t* count);
#ifdef OPENTONY_STANDALONE
extern "C" __declspec(dllexport) const OtKitEntry* __cdecl ot_kit_attach_standalone(const OtKitHost* host, std::uint32_t* count);
extern "C" __declspec(dllexport) std::uint32_t __cdecl ot_standalone_call(std::uint32_t address, const std::uint32_t* args, std::uint32_t count);
extern "C" __declspec(dllexport) int __cdecl ot_standalone_surface(std::uint32_t handle, std::uint32_t* width, std::uint32_t* height, std::uint32_t* bytes_per_pixel,
                                                                    unsigned char* pixels, std::uint32_t capacity);
extern "C" __declspec(dllexport) std::uint32_t __cdecl ot_standalone_surface_list(std::uint32_t* handles, std::uint32_t* versions, std::uint32_t* caps, std::uint32_t capacity);
extern "C" __declspec(dllexport) int __cdecl ot_standalone_surface_texels(std::uint32_t handle, std::uint32_t* width, std::uint32_t* height, std::uint32_t* hal_format,
                                                                           unsigned char* pixels, std::uint32_t capacity);
#endif  // OPENTONY_STANDALONE

namespace opentony::kitdll {
extern const OtKitEntry kEntries[];
extern const std::size_t kEntryCount;
}  // namespace opentony::kitdll

extern "C" __declspec(dllexport) const OtKitEntry* __cdecl ot_kit_attach(const OtKitHost* host, std::uint32_t* count) {
    using namespace opentony::kitdll;
    *count = 0;
    if (host == nullptr || host->version != OT_KIT_ABI_VERSION) {
        return nullptr;
    }
    g_host = host;
    if (std::string(host->build) != "canonical") {
        log_line(std::string("kits: the Kits use the canonical build's addresses; none for build ") + host->build);
        return nullptr;
    }
    if (!map_scratch()) {
        return nullptr;
    }
    try {
        g_memory.make_live(&mapped, host->check_every_access != 0);
        g_services.attach(&g_live_host);
        make_kits(g_memory, g_services);
    } catch (const std::exception& error) {
        log_line(std::string("kits: cannot create the Kits: ") + error.what());
        return nullptr;
    }
    char line[96];
    std::snprintf(line, sizeof line, "kits: %u Kit hooks, %u live calls%s", static_cast<unsigned>(kEntryCount), static_cast<unsigned>(kLiveCallCount),
                  host->check_every_access ? "; every live access checked" : "");
    log_line(line);
    *count = static_cast<std::uint32_t>(kEntryCount);
    return kEntries;
}

#ifdef OPENTONY_STANDALONE
// Phase 5.4a: the same Kits over the same live memory, but a retail function a Kit reports is resolved by address among the
// natives and the seams instead of being called in a retail image. The program that calls this owns the memory (it has
// laid the data out at the retail addresses) and the thread.
extern "C" __declspec(dllexport) const OtKitEntry* __cdecl ot_kit_attach_standalone(const OtKitHost* host, std::uint32_t* count) {
    using namespace opentony::kitdll;
    *count = 0;
    if (host == nullptr || host->version != OT_KIT_ABI_VERSION) {
        return nullptr;
    }
    g_host = host;
    if (std::string(host->build) != "canonical") {
        return nullptr;
    }
    try {
        arena::attach(static_cast<std::uintptr_t>(host->arena_base));   // 11b: before anything touches guest memory
    } catch (const std::exception& error) {
        log_line(std::string("kits: ") + error.what());
        return nullptr;
    }
    if (!map_scratch()) {
        return nullptr;
    }
    try {
        g_memory.make_live(&mapped, host->check_every_access != 0, arena::base());
        g_services.attach(&g_standalone_host);
        make_kits(g_memory, g_services);
    } catch (const std::exception& error) {
        log_line(std::string("kits: cannot create the Kits: ") + error.what());
        return nullptr;
    }
    char line[96];
    std::snprintf(line, sizeof line, "kits: standalone: %u natives by address, %u Kit hooks", static_cast<unsigned>(kNativeCount),
                  static_cast<unsigned>(kEntryCount));
    log_line(line);
    // OT_SEAMS_REPORT=1 (8c): which of the registered seams (re/symbols/seams.yml) the program answers, by a native (kNatives) or by an implementation (seam_implemented).
    // tests/test_standalone_seams.py runs it; "seams: N of M answered" with no "unanswered" line below it is the milestone's done-when.
    if (std::getenv("OT_SEAMS_REPORT") != nullptr) {
        std::size_t answered = 0;
        std::vector<std::string> missing;
        for (const opentony::seam::SeamEntry& entry : opentony::seam::kSeams) {
            if (find_native(entry.address) != nullptr || seam_implemented(entry.address)) {
                ++answered;
            } else {
                missing.push_back(std::string(entry.id) + " (0x" + hex_address(entry.address) + ")");
            }
        }
        std::snprintf(line, sizeof line, "seams: %u of %u answered", static_cast<unsigned>(answered), static_cast<unsigned>(opentony::seam::kSeamCount));
        log_line(line);
        for (const std::string& name : missing) {
            log_line("unanswered: " + name);
        }
    }
    *count = static_cast<std::uint32_t>(kEntryCount);
    return kEntries;
}

// Call the native (or seam) at a retail address with the arguments of its retail signature, the object of a thiscall first. A native
// that faults has started already: the process stops, as in a hooked build.
extern "C" __declspec(dllexport) std::uint32_t __cdecl ot_standalone_call(std::uint32_t address, const std::uint32_t* args, std::uint32_t count) {
    using namespace opentony::kitdll;
    try {
        return resolve_and_call(address, std::vector<std::uint32_t>(args, args + count), {}, "call from the program");
    } catch (const std::exception& error) {
        fatal(("0x" + hex_address(address)).c_str(), error.what());
    }
}

// The pixels of a surface of the texture layer's store, rows packed: what the function-level oracle (src/hooks/oracle_script.h) dumps for `surf`.
extern "C" __declspec(dllexport) int __cdecl ot_standalone_surface(std::uint32_t handle, std::uint32_t* width, std::uint32_t* height, std::uint32_t* bytes_per_pixel,
                                                                    unsigned char* pixels, std::uint32_t capacity) {
    std::vector<std::uint8_t> bytes;
    if (!opentony::kitdll::read_surface(opentony::kitdll::g_memory, handle, *width, *height, *bytes_per_pixel, bytes) || bytes.size() > capacity) {
        return 0;
    }
    std::memcpy(pixels, bytes.data(), bytes.size());
    return 1;
}

// The texture dump's walk of the store (standalone_texture_dump.cpp): the surfaces' count, handles, versions and caps; and one surface as the HAL gets it (rows packed, HAL format).
extern "C" __declspec(dllexport) std::uint32_t __cdecl ot_standalone_surface_list(std::uint32_t* handles, std::uint32_t* versions, std::uint32_t* caps, std::uint32_t capacity) {
    return opentony::kitdll::list_surfaces(opentony::kitdll::g_memory, handles, versions, caps, capacity);
}

extern "C" __declspec(dllexport) int __cdecl ot_standalone_surface_texels(std::uint32_t handle, std::uint32_t* width, std::uint32_t* height, std::uint32_t* hal_format,
                                                                           unsigned char* pixels, std::uint32_t capacity) {
    std::vector<std::uint8_t> bytes;
    if (!opentony::kitdll::read_surface_texels(opentony::kitdll::g_memory, handle, *width, *height, *hal_format, bytes) || bytes.size() > capacity) {
        return 0;
    }
    std::memcpy(pixels, bytes.data(), bytes.size());
    return 1;
}

#endif  // OPENTONY_STANDALONE