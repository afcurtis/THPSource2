#pragma once

// Phase 5.0: what the generated Kit hook adapters (kit_hooks_generated.cpp) share with the live host
// (kit_dll.cpp) in opentony_native.dll. See docs/HOOKS.md, "Kit hooks".

#include "math_kit.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace opentony::kitdll {

using runtime::RetailMemory;
using runtime::RetailServices;
using Precision = runtime::mathkit::x87::Precision;

// the argument conversions of the replay adapter tables (retail_calls_replay.hpp), so a table row's
// call expression reads the same in both places
inline int S(std::uint32_t v) { return static_cast<std::int32_t>(v); }
inline std::uint32_t U(bool v) { return v ? 1u : 0u; }
inline std::uint32_t U(int v) { return static_cast<std::uint32_t>(v); }
inline std::uint32_t U(std::uint32_t v) { return v; }

// How a retail function takes its arguments. `Thiscall` is ECX plus callee-popped stack arguments
// (VC6's member functions and its __fastcall functions of one register argument are the same thing).
enum class Convention : std::uint8_t { Cdecl, Stdcall, Thiscall };

// A retail function the Kits reach through a service name (`engine_<address>`, `assert`): the live
// host calls it. `lead` constant arguments come before the reported ones (the assertion routine's
// condition, which the Kits report only when it failed).
struct LiveCall {
    const char* service;
    std::uint32_t address;
    Convention convention;
    std::uint8_t stack_args;
    std::uint8_t lead;
    std::uint32_t lead_value;
    // a cdecl function with a variable argument list (`printf_log`): `stack_args` is the least the Kits
    // report, and the call passes as many as they reported
    bool variadic;
};

// One native by retail address: the entry point of its generated adapter, with the retail function's own
// calling convention. kNatives holds every native that has an adapter (the Kit hooks and the rows of
// re/config/standalone_natives.yml), sorted by address: the standalone resolver searches it.
struct NativeEntry {
    std::uint32_t address;
    const char* name;
    Convention convention;
    std::uint8_t stack_args;
    void* function;
};

// standalone_seams.cpp: the out-of-scope retail functions (re/symbols/seams.yml) a standalone program implements itself. True when
// the seam at `address` has an implementation, with its result in `result`.
bool call_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string& text, std::uint32_t& result);
// standalone_seams.cpp: whether the seam at `address` has an implementation of the standalone (any of the files below, or the table of standalone_seams.cpp itself); a native
// is not a seam (kNatives answers for it). The report of OT_SEAMS_REPORT and tests/test_standalone_seams.py ask this of every seam of seam_addresses_generated.hpp.
bool seam_implemented(std::uint32_t address);
// standalone_seams.cpp: the lockstep clock the seams share, in microseconds (a game frame is 1/60 s; a wait that is not over moves it on)
std::uint64_t standalone_clock_micros();
void standalone_advance_clock(std::uint64_t micros);
// standalone_seams.cpp (8c): WINMAIN_Sleep's step: the clock moves one game tick, the multimedia timer's due tick is delivered, and the answer is SimulationPublicTick
std::uint32_t standalone_sleep_tick();
// standalone_host.cpp (8c): the C runtime's corners, upstream zlib, WINMAIN's settings / file search / display modes, DirectPlay's single-player answers and the functions that
// data tables hold (re/config/table_seams.yml)
bool call_host_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string& text, std::uint32_t& result);
bool has_host_seam(std::uint32_t address);
// the x87 precision control the game has set through _controlfp, in the unit's encoding (0 = 24 bits, 2 = 53, 3 = 64)
unsigned standalone_precision();
// standalone_platform.cpp (8a): the window and the input devices of the program. PCInput_Poll / ReadControllers (the devices, then PCInput_BuildActionMask), the chord test
// SDXKeyboard_KeyIsDown, GetCursorPos on the 640 x 480 display, the message pump of Game_FrameTick, and whether the clock follows the wall clock.
std::uint32_t platform_poll_controllers();
std::uint32_t platform_key_is_down(std::uint32_t keyboard, std::uint32_t chord);
void platform_cursor(std::uint32_t point);
void platform_set_cursor(std::int32_t x, std::int32_t y);   // PCInput_SetCursorPos(x, y): the pointer put at a point of the 640 x 480 display
void platform_pump_window();
void platform_drain_messages();   // the thread's Windows messages (retail's pump); a no-op with no window
bool platform_realtime();
std::uint32_t platform_input_polls();
// standalone_audio.cpp (8a): the voices of the sound front (Audio_PlaySound ... Audio_FreeSoundBank) and the sound bank load on the HAL's audio service
bool call_audio_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string& text, std::uint32_t& result);
bool has_audio_seam(std::uint32_t address);
// standalone_movie.cpp (5.3b): the Bink seams (SetupFullScreenMovie ... CloseBinkMovie, and _beginthread for the music thread) and the
// Bink imports the natives report by name (`win32_BinkWait` ...). True when the address or the name is one of theirs.
bool call_movie_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string& text, std::uint32_t& result);
bool has_movie_seam(std::uint32_t address);
std::uint32_t movie_overlay_texture();   // the HAL texture of the open in-game clip (the level select's video box), 0 when there is none
bool call_movie_service(const std::string& name, const std::vector<std::uint32_t>& args, std::uint32_t& result);
// standalone_files.cpp (7b): FileIO_BackendOpen / Seek / Read / Close over a directory (OPENTONY_DATA_DIR)
bool call_file_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string& text, std::uint32_t& result);
bool has_file_seam(std::uint32_t address);
// standalone_surfaces.cpp (7b): the DirectDraw surfaces of the texture layer, as the named services `ddraw_create_surface` ... `ddraw_surface_pixel_format`
// (src/runtime/surface_store.hpp). True when `name` is one of them. The addresses in `args` are retail addresses in `memory`.
bool call_surface_service(RetailMemory& memory, const std::string& name, const std::vector<std::uint32_t>& args, std::uint32_t& result);
// The pixels of a surface of the store, rows packed (width * bytes per pixel), for the function-level oracle (src/hooks/oracle_script.h); false when there is no such surface.
bool read_surface(RetailMemory& memory, std::uint32_t handle, std::uint32_t& width, std::uint32_t& height, std::uint32_t& bytes_per_pixel,
                  std::vector<std::uint8_t>& pixels);
// The texture dump's walk of the store (standalone_texture_dump.cpp): the number of surfaces, with the first `capacity` handles, versions and caps; and one surface's rows packed
// exactly as HalTextureCache uploads them, with the HAL pixel format (0 when the surface has no HAL name) and its caps.
std::uint32_t list_surfaces(RetailMemory& memory, std::uint32_t* handles, std::uint32_t* versions, std::uint32_t* caps, std::uint32_t capacity);
bool read_surface_texels(RetailMemory& memory, std::uint32_t handle, std::uint32_t& width, std::uint32_t& height, std::uint32_t& hal_format, std::vector<std::uint8_t>& packed);
// standalone_session.cpp (9): the session recorder of a standalone run (OPENTONY_SESSION_*, the hooked probe's variables). After each delivery of the
// timer callback, after each keyboard device update, and the calls it answers itself (the startup movies, when skipped); the resolver asks it first.
void session_on_delivery();
void session_after_poll();
bool session_overrides(std::uint32_t address, bool music, std::uint32_t& result);   // `music`: StartBinkMovie's second argument
// the scenario recorder's first gameplay frame: the movie skip ends (retail's capture DLL restores the two movie entries there)
void session_end_movie_skip();
void session_stop();   // the end of a run that has what it came for: closes the trace, writes session.done, exits 0
// standalone_scenario.cpp (10f): a Warehouse scenario (OT_SCENARIO) in the standalone: the retail capture's frontend bootstrap, input injection and physics-frame recorder.
// `before` may change the arguments or answer the call itself (true, the answer in `result`); `live` maps a Kit address to where the memory really is
bool scenario_active();
bool scenario_before_call(std::uint32_t address, std::vector<std::uint32_t>& args, const char* caller, std::uint32_t (*live)(std::uint32_t), std::uint32_t& result);
void scenario_after_call(std::uint32_t address, const std::vector<std::uint32_t>& args, std::uint32_t (*live)(std::uint32_t));
void scenario_after_keyboard();
void scenario_before_bindings();
void scenario_after_bindings();
// standalone_boot.cpp (9): the cold boot: the C++ initializers, Game_Startup's work, then main; whether it is the program's start
void standalone_initialize();
std::uint32_t standalone_boot();
std::uint32_t standalone_zone_block();   // where the cold boot placed the game's memory zones (standalone_seams.cpp; OT_ZONE_BLOCK)
bool standalone_booting();
// kit_dll.cpp (standalone): call a native or a seam by its retail address, as a Kit's engine(...) call does
std::uint32_t call_native_or_seam(std::uint32_t address, const std::vector<std::uint32_t>& args);

// a named service (`file_open`, `sprite_construct`) and the retail function it stands for
struct ServiceAddress {
    const char* name;
    std::uint32_t address;
};

// generated
struct Kits;
Kits* make_kits(RetailMemory& memory, RetailServices& services);
extern const LiveCall kLiveCalls[];
extern const std::size_t kLiveCallCount;
extern const ServiceAddress kServices[];
extern const std::size_t kServiceCount;
extern const NativeEntry kNatives[];
extern const std::size_t kNativeCount;

// kit_dll.cpp: report a fault in a native (an exception out of a Kit) and stop the process; a native
// that has started has already written memory and called services, so retail cannot take over.
[[noreturn]] void fatal(const char* hook, const char* what) noexcept;

// The x87 unit. The caller's control word decides the precision the Kits emulate (the game runs
// the render thread at 24 bits); the Kits' own long double arithmetic needs the full 64 bits.
inline std::uint16_t x87_control() {
    std::uint16_t word;
    __asm__ volatile("fnstcw %0" : "=m"(word));
    return word;
}
inline void x87_load(std::uint16_t word) { __asm__ volatile("fldcw %0" : : "m"(word)); }

// the control word of the retail code that called the innermost native on this thread: the live host
// restores it around a service call, so the callee runs as when retail called it
inline thread_local std::uint16_t t_caller_x87 = 0x027f;

class X87Scope {
public:
    X87Scope() : saved_(x87_control()), outer_(t_caller_x87) {
        t_caller_x87 = saved_;
        x87_load(0x037f);
    }
    ~X87Scope() {
        t_caller_x87 = outer_;
        x87_load(saved_);
    }
    X87Scope(const X87Scope&) = delete;
    X87Scope& operator=(const X87Scope&) = delete;
    // precision control 00 is 24 bits; 10 (53) and 11 (64) round like the 53 the replays see
    [[nodiscard]] Precision precision() const { return (saved_ & 0x0300) == 0 ? Precision::k24 : Precision::k53; }

private:
    std::uint16_t saved_;
    std::uint16_t outer_;
};

template <typename Kit>
void set_kit_precision(Kit& kit, Precision precision) {
    if constexpr (requires { kit.set_precision(precision); }) {
        kit.set_precision(precision);
    } else if constexpr (requires { kit.math().set_precision(precision); }) {
        kit.math().set_precision(precision);
    }
}

// the innermost native running on this thread (the standalone's lenient log names it)
inline thread_local const char* t_native = nullptr;

class NativeScope {
public:
    explicit NativeScope(const char* hook) : outer_(t_native) { t_native = hook; }
    ~NativeScope() { t_native = outer_; }
    NativeScope(const NativeScope&) = delete;
    NativeScope& operator=(const NativeScope&) = delete;

private:
    const char* outer_;
};

// Every adapter body runs here: no exception leaves a native for the retail caller.
template <typename Body>
std::uint32_t run(const char* hook, Body&& body) noexcept {
    X87Scope x87;
    NativeScope native(hook);
    try {
        return body(x87.precision());
    } catch (const std::exception& error) {
        fatal(hook, error.what());
    } catch (...) {
        fatal(hook, "unknown exception");
    }
}

}  // namespace opentony::kitdll
