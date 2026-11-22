// opentony_spike.exe (Phase 5.4a): natives in a process that has no retail image.
//
//   opentony_spike.exe <THawk2.exe | thawk2_static.img> <opentony_native.dll> <session_N.img | -> <expected.tsv> [session_N.heap]
//
//   OT_SPIKE_CALL=0x00483ef0,...   after the getters, call these retail addresses (no arguments) through the resolver:
//                                  natives by address, seams by their implementation; whatever is neither stops the process
//
// 1. Reads the retail executable as a FILE (never loads it), or the static-data image made from it (8b: `tony hooks static-data`), and
//    refuses one that is not the expected build.
// 2. Reserves the retail image's address range (0x00400000 .. +SizeOfImage), commits it and copies in `.rdata`
//    and the initialised part of `.data`. `.text` stays zero: there is no retail code here.
// 3. With an image argument, lays a session image (OPENTONY_SESSION_DUMP: the writable section at 0x00524000 as
//    the retail process had it at the main menu) over `.data`.
// 4. Loads the Kit DLL the way opentony.dll does and calls the Kit hooks listed in expected.tsv
//    (`Name<TAB>hex value`, argument-less cdecl getters), comparing each result with the value retail
//    returned in the process the image came from (OPENTONY_SESSION_ORACLE).
//
// 5.4a continued: the Kits are attached with the standalone resolver (`ot_kit_attach_standalone`), so a native that reports a call to
// another retail function reaches the native of that function; see PHASE_5_SUMMARY.md, "5.4a result" and "5.4a continued".
#include "../kit_abi.h"
#include "../oracle_script.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "guest_arena.hpp"

namespace arena = opentony::kitdll::arena;

namespace {

#if UINTPTR_MAX == 0xffffffffu
// zero-filled, read/write, in its own last section (no file bytes)
#pragma clang section bss = ".retail"
// Covers the retail image (to 0x029de000) and, since 7a, the whole area the retail process's memory zones and heaps lived in (to 0x0a100000; the main-menu image of 7b, part 3, has a zone ending at 0x0a050000):
// those regions were once placed with VirtualAlloc, which fails when Wine has mapped a file or a heap there (7a's environment padding moved
// the retail regions into such mappings: error 487 for 02da0000 and 02ff0000). The loader reserves a section before anything else lands.
alignas(4096) std::uint8_t g_retail_reserve[0x9d00000];
#pragma clang section bss = ""
#else
// 11b (docs/PORT64_AUDIT.md 5.2, 5.4 item 5): the guest memory of a 64-bit process is one reservation of 4 GiB (and a guard) made at start, below
std::uintptr_t g_arena_base = 0;
#endif

constexpr std::uint32_t kExpectedTimestamp = 0x3a54c1f6u;  // re/config/native_hooks.yml builds.canonical
constexpr std::uint32_t kExpectedSizeOfImage = 0x025de000u;
constexpr std::uint32_t kExpectedImageBase = 0x00400000u;

std::vector<std::uint8_t> read_file(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// the DLL's guest heap, for the blocks this program hands the game (a host buffer is never a guest address): the exports of guest_arena.cpp
std::uint32_t(__cdecl* g_guest_alloc)(std::uint32_t) = nullptr;
void(__cdecl* g_guest_free)(std::uint32_t) = nullptr;

// a zeroed block of the guest heap, freed with the object
class Guest {
public:
    explicit Guest(std::size_t size) : address_(g_guest_alloc(static_cast<std::uint32_t>(size))) {
        if (address_ == 0) {
            std::fprintf(stderr, "the guest heap cannot hold a block of %zu bytes\n", size);
            ExitProcess(10);
        }
        std::memset(data(), 0, size);
    }
    Guest(const void* bytes, std::size_t size) : Guest(size) { std::memcpy(data(), bytes, size); }
    explicit Guest(const char* text) : Guest(text, std::strlen(text) + 1) {}
    ~Guest() { g_guest_free(address_); }
    Guest(const Guest&) = delete;
    Guest& operator=(const Guest&) = delete;
    [[nodiscard]] std::uint32_t address() const { return address_; }
    [[nodiscard]] std::uint8_t* data() const { return arena::host<std::uint8_t>(address_); }
    [[nodiscard]] std::uint32_t word(std::size_t offset = 0) const {
        std::uint32_t value;
        std::memcpy(&value, data() + offset, 4);
        return value;
    }
    void set_word(std::uint32_t value, std::size_t offset = 0) const { std::memcpy(data() + offset, &value, 4); }

private:
    std::uint32_t address_;
};

void log_line(const char* line) {
    std::printf("kit: %s\n", line);
    std::fflush(stdout);  // a fault ends the process: the last line must already be out
}

// A fault in a native ends the run with the address and the module, not in Wine's debugger
LONG WINAPI crash_filter(EXCEPTION_POINTERS* info) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(info->ExceptionRecord->ExceptionAddress), &module)) {
        GetModuleFileNameA(module, name, sizeof name);
    }
    const auto* record = info->ExceptionRecord;
    std::printf("CRASH %08lx at %p (%s +%lx), %s %p\n", record->ExceptionCode, record->ExceptionAddress, name,
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(record->ExceptionAddress) - reinterpret_cast<std::uintptr_t>(module)),
                record->NumberParameters >= 2 && record->ExceptionInformation[0] == 1 ? "write to" : "access of",
                record->NumberParameters >= 2 ? reinterpret_cast<void*>(record->ExceptionInformation[1]) : nullptr);
#if UINTPTR_MAX != 0xffffffffu
    // 64-bit: a fault inside the arena names the guest address the game (or a native) used
    if (record->NumberParameters >= 2 && record->ExceptionInformation[1] >= g_arena_base && record->ExceptionInformation[1] - g_arena_base <= 0xffffffffull) {
        std::printf("  guest address %08x (arena at %p)\n", static_cast<unsigned>(record->ExceptionInformation[1] - g_arena_base), reinterpret_cast<void*>(g_arena_base));
    }
#endif
    // 9a: the words of the stack that point into the standalone's code, nearest first (the callers, by `llvm-nm --numeric-sort` on the DLL)
    HMODULE standalone = GetModuleHandleA("opentony_standalone.dll");
    if (standalone != nullptr && info->ContextRecord != nullptr) {
        const auto base = reinterpret_cast<std::uintptr_t>(standalone);
#if UINTPTR_MAX == 0xffffffffu
        const auto* stack = reinterpret_cast<const std::uintptr_t*>(info->ContextRecord->Esp);
        constexpr std::uintptr_t kModuleSpan = 0x440000, kLinkBase = 0x10000000;
#else
        const auto* stack = reinterpret_cast<const std::uintptr_t*>(info->ContextRecord->Rsp);
        constexpr std::uintptr_t kModuleSpan = 0x1000000, kLinkBase = 0;   // an offset into the DLL (`llvm-nm --numeric-sort` on it names the function)
#endif
        int printed = 0;
        for (int i = 0; i < 4096 && printed < 40; ++i) {
            if (IsBadReadPtr(stack + i, sizeof(std::uintptr_t))) {
                break;
            }
            if (stack[i] > base + 0x1000 && stack[i] < base + kModuleSpan) {
                std::printf("  caller 0x%08lx\n", static_cast<unsigned long>(kLinkBase + (stack[i] - base)));
                ++printed;
            }
        }
    }
    std::fflush(stdout);
    ExitProcess(0xc0);
}

// OT_HANG_DUMP=SECONDS: after that long the main thread is stopped and reported like a crash (where it is, and the standalone's functions on its stack): a
// run that never finishes on a machine with no debugger (12 s1: the first run on Windows) says where it is stuck.
HANDLE g_main_thread = nullptr;
DWORD WINAPI hang_watchdog(LPVOID seconds) {
    Sleep(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(seconds)) * 1000);
    SuspendThread(g_main_thread);
    CONTEXT context = {};
    context.ContextFlags = CONTEXT_CONTROL;
    GetThreadContext(g_main_thread, &context);
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = 0x00004854;
#if UINTPTR_MAX == 0xffffffffu
    record.ExceptionAddress = reinterpret_cast<PVOID>(context.Eip);
#else
    record.ExceptionAddress = reinterpret_cast<PVOID>(context.Rip);
#endif
    // every module on the stack, nearest first (the standalone's own frames are listed by the crash report that follows)
    {
#if UINTPTR_MAX == 0xffffffffu
        const auto* stack = reinterpret_cast<const std::uintptr_t*>(context.Esp);
#else
        const auto* stack = reinterpret_cast<const std::uintptr_t*>(context.Rsp);
#endif
        int printed = 0;
        for (int i = 0; i < 4096 && printed < 30; ++i) {
            if (IsBadReadPtr(stack + i, sizeof(std::uintptr_t))) {
                break;
            }
            MEMORY_BASIC_INFORMATION info;
            if (VirtualQuery(reinterpret_cast<const void*>(stack[i]), &info, sizeof info) == sizeof info && info.Type == MEM_IMAGE && info.AllocationBase != nullptr) {
                char name[MAX_PATH] = "?";
                GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase), name, sizeof name);
                std::printf("  frame %s +%lx\n", name, static_cast<unsigned long>(stack[i] - reinterpret_cast<std::uintptr_t>(info.AllocationBase)));
                ++printed;
            }
        }
    }
    EXCEPTION_POINTERS pointers = {&record, &context};
    std::printf("HANG: the main thread did not finish in %lu s\n", static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(seconds)));
    return static_cast<DWORD>(crash_filter(&pointers));
}

}  // namespace

#if UINTPTR_MAX != 0xffffffffu
// the program's side of guest_arena.hpp on 64-bit: the base it reserved (the DLL keeps its own copy, handed over in OtKitHost::arena_base)
namespace opentony::kitdll::arena {
std::uintptr_t base() noexcept { return g_arena_base; }
}  // namespace opentony::kitdll::arena
#endif

int main(int argc, char** argv) {
    if (argc != 5 && argc != 6) {
        std::fprintf(stderr, "usage: opentony_spike <THawk2.exe> <opentony_native.dll> <session image | -> <expected.tsv>\n");
        return 64;
    }
    SetUnhandledExceptionFilter(&crash_filter);
    if (const char* hang = std::getenv("OT_HANG_DUMP")) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        CreateThread(nullptr, 0, &hang_watchdog, reinterpret_cast<LPVOID>(static_cast<std::uintptr_t>(std::atoi(hang))), 0, nullptr);
    }
    const std::vector<std::uint8_t> exe = read_file(argv[1]);
    if (exe.size() < sizeof(IMAGE_DOS_HEADER)) {
        std::fprintf(stderr, "rejected: %s is not readable\n", argv[1]);
        return 2;
    }
    // 8b: either the static-data image `tony hooks static-data` writes from the player's executable (`OTSD`, tony/static_data.py), or the executable itself
    struct Part {
        std::uint32_t rva;
        const std::uint8_t* bytes;
        std::size_t size;
        char name[9];
        std::uint32_t virtual_size;
        std::uint32_t raw_size;
        bool code;
    };
    std::vector<Part> parts;
    const bool packed = exe.size() >= 4 && std::memcmp(exe.data(), "OTSD", 4) == 0;
    if (packed) {
        std::uint32_t head[5] = {};
        if (exe.size() < 4 + sizeof head) {
            std::fprintf(stderr, "rejected: %s is a truncated static-data image\n", argv[1]);
            return 2;
        }
        std::memcpy(head, exe.data() + 4, sizeof head);  // version, image base, timestamp, size of image, section count
        if (head[0] != 1 || head[1] != kExpectedImageBase || head[2] != kExpectedTimestamp || head[3] != kExpectedSizeOfImage) {
            std::fprintf(stderr, "rejected: %s is not the retail build the natives target (timestamp %08x, size of image %08x)\n", argv[1],
                         static_cast<unsigned>(head[2]), static_cast<unsigned>(head[3]));
            return 2;
        }
        std::size_t table = 4 + sizeof head;
        std::size_t at = table + static_cast<std::size_t>(head[4]) * 8;
        for (std::uint32_t i = 0; i < head[4]; ++i, table += 8) {
            std::uint32_t entry[2] = {};
            if (table + 8 > exe.size()) {
                std::fprintf(stderr, "rejected: %s is a truncated static-data image\n", argv[1]);
                return 2;
            }
            std::memcpy(entry, exe.data() + table, 8);
            if (at + entry[1] > exe.size() || entry[0] < kExpectedImageBase || entry[0] - kExpectedImageBase + entry[1] > kExpectedSizeOfImage) {
                std::fprintf(stderr, "rejected: %s has a section outside the retail image\n", argv[1]);
                return 2;
            }
            parts.push_back({entry[0] - kExpectedImageBase, exe.data() + at, entry[1], {}, entry[1], entry[1], false});
            at += entry[1];
        }
    } else {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
            static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > exe.size()) {
            std::fprintf(stderr, "rejected: %s is not a PE file\n", argv[1]);
            return 2;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(exe.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->FileHeader.TimeDateStamp != kExpectedTimestamp || nt->OptionalHeader.SizeOfImage != kExpectedSizeOfImage ||
            nt->OptionalHeader.ImageBase != kExpectedImageBase) {
            std::fprintf(stderr, "rejected: %s is not the retail build the natives target (timestamp %08x, size of image %08x)\n",
                         argv[1], static_cast<unsigned>(nt->FileHeader.TimeDateStamp),
                         static_cast<unsigned>(nt->OptionalHeader.SizeOfImage));
            return 2;
        }
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            Part part{section->VirtualAddress, nullptr, 0, {}, section->Misc.VirtualSize, section->SizeOfRawData,
                      (section->Characteristics & IMAGE_SCN_CNT_CODE) != 0};
            std::memcpy(part.name, section->Name, 8);
            part.size = std::min<std::size_t>(section->SizeOfRawData, section->Misc.VirtualSize);
            if (!part.code && section->PointerToRawData + part.size <= exe.size()) {
                part.bytes = exe.data() + section->PointerToRawData;
            }
            parts.push_back(part);
        }
    }

    // the retail address range: this executable is linked at 0x00400000 and ends in a zero-filled section that covers it,
    // so the loader reserves it before anything else can land there (a VirtualAlloc at 0x00400000 from an executable
    // linked elsewhere fails under Wine: its own heaps and system mappings are in that range)
#if UINTPTR_MAX == 0xffffffffu
    const auto reserve_begin = reinterpret_cast<std::uintptr_t>(g_retail_reserve);
    const auto reserve_end = reserve_begin + sizeof g_retail_reserve;
#else
    // 11b: one reservation of 4 GiB and a guard, at a 4 GiB-aligned address where one is free (the low 32 bits of a host address are then the guest address:
    // a crash line and a debugger read it directly), else anywhere; the image's span and the zone block's are committed, the heap commits itself on demand
    for (std::uintptr_t k = 1; k < 0x7000 && g_arena_base == 0; ++k) {
        g_arena_base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(reinterpret_cast<void*>(k << 32), arena::kArenaBytes, MEM_RESERVE, PAGE_NOACCESS));
    }
    if (g_arena_base == 0) {
        g_arena_base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, arena::kArenaBytes, MEM_RESERVE, PAGE_NOACCESS));
    }
    if (g_arena_base == 0 || VirtualAlloc(arena::host<void>(arena::kImageBase), arena::kZonesEnd - arena::kImageBase, MEM_COMMIT, PAGE_READWRITE) == nullptr) {
        std::fprintf(stderr, "cannot reserve and commit the guest arena (error %lu)\n", GetLastError());
        return 3;
    }
    const std::uint64_t reserve_begin = arena::kImageBase;   // guest addresses: the span committed above
    const std::uint64_t reserve_end = arena::kZonesEnd;
#endif
    auto* base = arena::host<std::uint8_t>(kExpectedImageBase);
    if (reserve_begin > 0x00524000u || reserve_end < kExpectedImageBase + kExpectedSizeOfImage) {
        std::fprintf(stderr, "the zero-filled section %08lx..%08lx does not cover the retail data %08x..%08x\n",
                     static_cast<unsigned long>(reserve_begin), static_cast<unsigned long>(reserve_end), 0x00524000u,
                     kExpectedImageBase + kExpectedSizeOfImage);
        return 3;
    }
    std::printf("this process's own image is %p..; the retail range %08x..%08x is inside its zero-filled section / committed arena span %08lx..%08lx\n",
                static_cast<void*>(GetModuleHandleA(nullptr)), kExpectedImageBase, kExpectedImageBase + kExpectedSizeOfImage,
                static_cast<unsigned long>(reserve_begin), static_cast<unsigned long>(reserve_end));
    std::size_t copied = 0;
    for (const Part& part : parts) {
        if (!packed) {
            std::printf("section %-6s %08x +%08x raw %08x%s\n", part.name, kExpectedImageBase + part.rva, static_cast<unsigned>(part.virtual_size),
                        static_cast<unsigned>(part.raw_size), part.code ? "  (code: not loaded)" : "");
        }
        if (part.bytes == nullptr) {
            continue;
        }
        std::memcpy(base + part.rva, part.bytes, part.size);
        copied += part.size;
    }
    std::printf("regions: %zu bytes of initialised data from the %s; .text is zero (first words %08x %08x)\n", copied, packed ? "static-data image" : "executable file",
                *reinterpret_cast<const std::uint32_t*>(base + 0x1000), *reinterpret_cast<const std::uint32_t*>(base + 0x1004));

    if (std::strcmp(argv[3], "-") != 0) {
        const std::vector<std::uint8_t> image = read_file(argv[3]);
        constexpr std::uint32_t kDataBase = 0x00524000u;
        if (image.empty() || kDataBase - kExpectedImageBase + image.size() > kExpectedSizeOfImage) {
            std::fprintf(stderr, "session image %s does not fit .data\n", argv[3]);
            return 4;
        }
        std::memcpy(base + (kDataBase - kExpectedImageBase), image.data(), image.size());
        std::printf("session image: %zu bytes laid over .data at %08x\n", image.size(), kDataBase);
    }

    // the large private regions of the retail process (the game's memory zones), at the addresses they had
    if (argc == 6) {
        const std::vector<std::uint8_t> heap = read_file(argv[5]);
        std::size_t at = 0;
        unsigned placed = 0;
        unsigned refused = 0;
        while (at + 8 <= heap.size()) {
            std::uint32_t where = 0;
            std::uint32_t size = 0;
            std::memcpy(&where, heap.data() + at, 4);
            std::memcpy(&size, heap.data() + at + 4, 4);
            at += 8;
            if (at + size > heap.size()) {
                break;
            }
            void* got = arena::host<void>(where);
            if (where >= reserve_begin && static_cast<std::uint64_t>(where) + size <= reserve_end) {
                // inside the zero-filled section: it is already there, read/write
            } else {
                got = VirtualAlloc(got, size, arena::kFlat ? (MEM_RESERVE | MEM_COMMIT) : MEM_COMMIT, PAGE_READWRITE);   // 64-bit: inside the reservation
            }
            if (got != arena::host<void>(where)) {
                std::printf("heap region %08x +%08x: cannot be placed at its address (error %lu)\n", where, size, GetLastError());
                MEMORY_BASIC_INFORMATION occupant{};
                const auto first = reinterpret_cast<std::uintptr_t>(arena::host<void>(where));
                for (std::uintptr_t probe = first; probe < first + size;) {  // what is in the way
                    if (VirtualQuery(reinterpret_cast<void*>(probe), &occupant, sizeof occupant) == 0) break;
                    if (occupant.State != MEM_FREE) {
                        char module[MAX_PATH] = "";
                        GetModuleFileNameA(static_cast<HMODULE>(occupant.AllocationBase), module, sizeof module);
                        std::printf("    in the way: %p +%08lx state %lx type %lx base %p %s\n", occupant.BaseAddress,
                                    static_cast<unsigned long>(occupant.RegionSize), occupant.State, occupant.Type, occupant.AllocationBase, module);
                        break;
                    }
                    probe = reinterpret_cast<std::uintptr_t>(occupant.BaseAddress) + occupant.RegionSize;
                }
                ++refused;
            } else {
                std::memcpy(got, heap.data() + at, size);
                std::printf("heap region %08x +%08x placed\n", where, size);
                ++placed;
            }
            at += size;
        }
        std::printf("heap regions: %u placed, %u refused\n", placed, refused);
    }

    // the Kits, over this memory, the way opentony.dll attaches them
    const HMODULE kits = LoadLibraryA(argv[2]);
    if (kits == nullptr) {
        std::fprintf(stderr, "cannot load %s (error %lu)\n", argv[2], GetLastError());
        return 5;
    }
    const auto attach = reinterpret_cast<OtKitAttach>(GetProcAddress(kits, OT_KIT_ATTACH_STANDALONE));
    const auto call_native = reinterpret_cast<std::uint32_t(__cdecl*)(std::uint32_t, const std::uint32_t*, std::uint32_t)>(
        GetProcAddress(kits, OT_STANDALONE_CALL));
    if (attach == nullptr || call_native == nullptr) {
        std::fprintf(stderr, "%s has no %s / %s\n", argv[2], OT_KIT_ATTACH_STANDALONE, OT_STANDALONE_CALL);
        return 5;
    }
    OtKitHost host{};
    host.version = OT_KIT_ABI_VERSION;
    host.build = "canonical";
    host.arena_base = arena::base();   // 0 in a 32-bit process
    host.log = &log_line;
    g_guest_alloc = reinterpret_cast<std::uint32_t(__cdecl*)(std::uint32_t)>(GetProcAddress(kits, "ot_standalone_guest_alloc"));
    g_guest_free = reinterpret_cast<void(__cdecl*)(std::uint32_t)>(GetProcAddress(kits, "ot_standalone_guest_free"));
    if (g_guest_alloc == nullptr || g_guest_free == nullptr) {
        std::fprintf(stderr, "%s has no ot_standalone_guest_alloc / _free\n", argv[2]);
        return 5;
    }
    std::uint32_t count = 0;
    const OtKitEntry* entries = attach(&host, &count);
    if (entries == nullptr) {
        std::fprintf(stderr, "the Kits declined to attach\n");
        return 5;
    }
    std::printf("kits: %u Kit hooks have an entry point\n", count);

    std::ifstream expected(argv[4]);
    std::string name;
    std::string value;
    unsigned checked = 0;
    unsigned mismatched = 0;
    unsigned missing = 0;
    unsigned low_byte_only = 0;
    while (expected >> name >> value) {
        const OtKitEntry* entry = nullptr;
        for (std::uint32_t k = 0; k < count; ++k) {
            if (name == entries[k].name) {
                entry = &entries[k];
                break;
            }
        }
        if (entry == nullptr) {
            std::printf("MISSING  %-34s no Kit entry\n", name.c_str());
            ++missing;
            continue;
        }
        const std::uint32_t want = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 16));
        std::fprintf(stderr, "calling %s\n", name.c_str());
        const std::uint32_t got = reinterpret_cast<std::uint32_t(__cdecl*)()>(entry->function)();
        ++checked;
        // A function that returns a bool leaves the rest of EAX as it was; the callers read AL. Say so, do not hide it.
        const bool same_low_byte = got <= 1u && (got & 0xffu) == (want & 0xffu);
        const char* verdict = got == want ? "MATCH" : same_low_byte ? "MATCH-AL" : "MISMATCH";
        if (got != want && !same_low_byte) {
            ++mismatched;
        }
        if (got != want && same_low_byte) {
            ++low_byte_only;
        }
        std::printf("%-8s %-34s native %08x  retail %08x\n", verdict, name.c_str(), static_cast<unsigned>(got), static_cast<unsigned>(want));
    }
    std::printf("RESULT %u checked, %u mismatched, %u missing, %u equal in AL only (bool results)\n", checked, mismatched, missing,
                low_byte_only);
    // 7c: OT_SPIKE_DUMP=<path>:<ticks>: the image's memory (the same span as the session image) after that many Game_FrameTick calls
    if (const char* dump = std::getenv("OT_SPIKE_DUMP")) {
        const std::string text = dump;
        const auto colon = text.rfind(':');
        const auto set_dump = reinterpret_cast<void(__cdecl*)(const char*, std::uint32_t, std::uint32_t, std::uint32_t)>(GetProcAddress(kits, "ot_standalone_set_dump"));
        if (colon == std::string::npos || set_dump == nullptr || std::strcmp(argv[3], "-") == 0) {
            std::fprintf(stderr, "OT_SPIKE_DUMP needs <path>:<ticks>, a session image and a DLL that exports ot_standalone_set_dump\n");
            return 9;
        }
        static std::string path;
        path = text.substr(0, colon);
        set_dump(path.c_str(), static_cast<std::uint32_t>(std::strtoul(text.c_str() + colon + 1, nullptr, 10)), 0x00524000u,
                 static_cast<std::uint32_t>(read_file(argv[3]).size()));
    }
    // 7c: the video the draw path draws into (standalone_video.cpp in opentony_spike_video.dll, beside this executable; OT_VIDEO_DIR). 8a: the same DLL is the window, the
    // input devices and the sound (OT_VIDEO_SDL, OT_VIDEO_SHOW)
    if (std::getenv("OT_VIDEO_DIR") != nullptr || std::getenv("OT_VIDEO_SDL") != nullptr || std::getenv("OT_VIDEO_SHOW") != nullptr || std::getenv("OT_TEXTURE_DUMP") != nullptr) {
        char path[MAX_PATH];
        const DWORD length = GetModuleFileNameA(nullptr, path, sizeof path);
        std::string video = std::string(path, length);
        video = video.substr(0, video.find_last_of("\\/") + 1) + "opentony_spike_video.dll";
        const HMODULE module = LoadLibraryA(video.c_str());
        const auto start = module != nullptr ? reinterpret_cast<bool(__cdecl*)(HMODULE)>(GetProcAddress(module, "standalone_video_start")) : nullptr;
        if (start == nullptr || !start(kits)) {
            std::fprintf(stderr, "video: %s could not start (error %lu)\n", video.c_str(), GetLastError());
            return 8;
        }
    }
    // 7c: `restore` in OT_SPIKE_CALL makes again what a session image cannot carry. Retail allocates its texture records with the C runtime's malloc, and a session
    // dump holds .data and the game's memory zones, not that heap: the textures of every font and of the two mouse cursors the image has loaded read as zeros (no
    // surface), so their sprites would never draw. The fonts are loaded again by name into their own slots and the cursors reloaded, through the natives; the old
    // objects stay where they are, unused. The front end read those fonts from the copies inside mainmenu.pre (the loose MAINF.FNT and S2FONT2.FNT have other glyph
    // widths), so that file is in the image's PRE manager while they load and is unloaded after, as the front end does. A cold boot (milestone 9) has no image and
    // needs none of this.
    auto restore = [&] {
        constexpr std::uint32_t kFontTable = 0x0056161c, kFontNames = 0x0056163c, kFontLoad = 0x0044ada0;
        constexpr std::uint32_t kCursorSprites = 0x006a1424, kLoadCursors = 0x004dbd90;
        constexpr std::uint32_t kPreManager = 0x0056b834, kPreLoad = 0x004a90f0, kPreUnload = 0x004a9330;
        auto word = [](std::uint32_t address) -> volatile std::uint32_t& { return *arena::host<volatile std::uint32_t>(address); };
        const std::uint32_t manager = word(kPreManager);
        const Guest pre_name("mainmenu.pre");
        const std::uint32_t name_address = pre_name.address();
        if (manager != 0) {
            const std::uint32_t load[3] = {manager, name_address, 0};
            call_native(kPreLoad, load, 3);
        }
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            if (word(kFontTable + 4 * slot) != 0) {
                const std::uint32_t name = kFontNames + 0x10 * slot;
                std::printf("restore: font %s (slot %u)\n", arena::host<const char>(name), static_cast<unsigned>(slot));
                word(kFontTable + 4 * slot) = 0;
                call_native(kFontLoad, &name, 1);
            }
        }
        if (word(kCursorSprites) != 0 || word(kCursorSprites + 4) != 0) {
            std::printf("restore: mouse cursors\n");
            word(kCursorSprites) = 0;
            word(kCursorSprites + 4) = 0;
            call_native(kLoadCursors, nullptr, 0);
        }
        if (manager != 0) {
            const std::uint32_t unload[2] = {manager, name_address};
            call_native(kPreUnload, unload, 2);
        }
        std::fflush(stdout);
    };
    // 8c: `hostcheck` calls the host seams of standalone_host.cpp with buffers of the spike's own and prints one "hostcheck: NAME ok" or "hostcheck: NAME FAIL ..." line
    // each (tests/test_standalone_seams.py reads them): upstream zlib both ways and its error codes, isspace, stat, the package stream, the file search, the settings file,
    // DirectPlay's empty answers and the display-mode list
    auto host_check = [&] {
        int failures = 0;
        auto report = [&](const char* name, bool ok, const char* detail = "") {
            std::printf("hostcheck: %s %s%s%s\n", name, ok ? "ok" : "FAIL", detail[0] != '\0' ? " " : "", detail);
            failures += ok ? 0 : 1;
        };
        auto call = [&](std::uint32_t address, std::initializer_list<std::uint32_t> arguments) {
            std::vector<std::uint32_t> list(arguments);
            return call_native(address, list.data(), static_cast<std::uint32_t>(list.size()));
        };
        constexpr std::uint32_t kCompress = 0x004f9d30, kUncompress = 0x004f9d50, kIsSpace = 0x005009d1, kStat = 0x005005d3, kOpen = 0x005013f9, kRead = 0x00502358,
                                kClose = 0x00500e7b, kFind = 0x004f6560, kSave = 0x004f6cd0, kLoad = 0x004f6ca0, kPoll = 0x004db1c0, kConnect = 0x004dbc50,
                                kEnum = 0x004f7a70, kSwitch = 0x004f6e60, kCd = 0x004f6510, kMmx = 0x004f2700;
        // zlib: 4,096 compressible bytes
        std::vector<std::uint8_t> input(4096);
        for (std::size_t k = 0; k < input.size(); ++k) {
            input[k] = static_cast<std::uint8_t>("OpenTony "[k % 9] + (k / 512));
        }
        const Guest g_input(input.data(), input.size()), g_packed(8192), g_unpacked(4096), g_packed_size(4), g_unpacked_size(4);
        g_packed_size.set_word(8192);
        std::uint32_t status = call(kCompress, {g_packed.address(), g_packed_size.address(), g_input.address(), static_cast<std::uint32_t>(input.size())});
        const std::uint32_t packed_size = g_packed_size.word();
        report("zlib-compress", status == 0 && packed_size > 0 && packed_size < 400 && g_packed.data()[0] == 0x78, ("size " + std::to_string(packed_size)).c_str());
        g_unpacked_size.set_word(4096);
        status = call(kUncompress, {g_unpacked.address(), g_unpacked_size.address(), g_packed.address(), packed_size});
        report("zlib-uncompress", status == 0 && g_unpacked_size.word() == input.size() && std::memcmp(g_unpacked.data(), input.data(), input.size()) == 0);
        const Guest g_small(4), g_tiny(4);
        g_small.set_word(4);
        status = call(kCompress, {g_tiny.address(), g_small.address(), g_input.address(), static_cast<std::uint32_t>(input.size())});
        report("zlib-buffer-error", static_cast<std::int32_t>(status) == -5);
        const Guest g_damaged(g_packed.data(), 8192);
        g_damaged.data()[10] ^= 0xff;
        g_damaged.data()[11] ^= 0xff;
        g_unpacked_size.set_word(4096);
        status = call(kUncompress, {g_unpacked.address(), g_unpacked_size.address(), g_damaged.address(), packed_size});
        report("zlib-data-error", static_cast<std::int32_t>(status) == -3 || static_cast<std::int32_t>(status) == -5, std::to_string(static_cast<std::int32_t>(status)).c_str());
        // the C runtime
        report("isspace", call(kIsSpace, {' '}) == 8 && call(kIsSpace, {'\n'}) == 8 && call(kIsSpace, {'a'}) == 0 && call(kIsSpace, {0xffffffffu}) == 0);
        const Guest g_record(36);
        const char* file = "re/config/zlib.yml";
        const Guest g_file(file), g_rb("rb");
        const bool found = call(kStat, {g_file.address(), g_record.address()}) == 0;
        const std::uint32_t size = g_record.word(20);
        std::uint16_t mode = 0;
        std::memcpy(&mode, g_record.data() + 6, 2);
        std::ifstream sized(file, std::ios::binary | std::ios::ate);
        report("stat", found && size != 0 && size == static_cast<std::uint32_t>(sized.tellg()) && (mode & 0xf000) == 0x8000, ("size " + std::to_string(size)).c_str());
        const Guest g_nothing("re/config/no such file");
        report("stat-missing", call(kStat, {g_nothing.address(), g_record.address()}) == 0xffffffffu);
        const std::uint32_t stream = call(kOpen, {g_file.address(), g_rb.address()});
        const Guest g_head(16);
        const bool read = stream != 0 && call(kRead, {g_head.address(), 1, 8, stream}) == 8;
        const Guest g_nopackage("no/such/package");
        report("package-stream", read && std::memcmp(g_head.data(), "# Pinned", 8) == 0 && call(kClose, {stream}) == 0 && call(kOpen, {g_nopackage.address(), g_rb.address()}) == 0);
        // WINMAIN: the file search keeps the names that fit and answers kept + 1
        const Guest g_names(12), g_names_buffer(3 * 0x104), g_pattern("re/config/*.yml"), g_pattern_none("re/config/*.nothing");
        const std::uint32_t kept = call(kFind, {g_pattern.address(), g_names.address(), 3, g_names_buffer.address(), 0});
        const char* first = arena::host<const char>(g_names.word(0));
        report("find-files", kept == 4 && g_names.word(0) == g_names_buffer.address() && g_names.word(4) == g_names_buffer.address() + 0x104 && std::strstr(first, ".yml") != nullptr, first);
        report("find-files-none", call(kFind, {g_pattern_none.address(), g_names.address(), 3, g_names_buffer.address(), 0}) == 1);
        // the settings file (OPENTONY_SETTINGS names it): a value written is read back, a missing one answers the default
        const Guest g_value(0x100), g_check("Check"), g_key("Key"), g_hello("hello"), g_fallback("fallback"), g_absent("Absent");
        call(kSave, {g_check.address(), g_key.address(), g_hello.address()});
        call(kLoad, {g_check.address(), g_key.address(), g_fallback.address(), g_value.address()});
        const bool saved = std::strcmp(reinterpret_cast<const char*>(g_value.data()), "hello") == 0;
        call(kLoad, {g_check.address(), g_absent.address(), g_fallback.address(), g_value.address()});
        report("settings", saved && std::strcmp(reinterpret_cast<const char*>(g_value.data()), "fallback") == 0);
        // single-player DirectPlay: no message, a refused connection
        report("directplay", call(kPoll, {0}) == 0 && call(kConnect, {0, 0, 0}) == 0xffffffffu);
        // the display: one mode of 640 x 480 at 16 bits in the list, the screen words at that size
        const auto read32 = [](std::uint32_t address) { return *arena::host<volatile std::uint32_t>(address); };
        call(kEnum, {});
        call(kSwitch, {2});
        report("display", read32(0x029d8478) == 1 && read32(0x029d8478 + 4 + 0x0c) == 640 && read32(0x029d8478 + 4 + 0x08) == 480 && read32(0x029d8478 + 4 + 0x54) == 16 &&
                              read32(0x029da394) == 640 && read32(0x029da398) == 480);
        report("platform-answers", call(kMmx, {}) == 1 && call(kCd, {}) == 0);
        std::printf("hostcheck: %d failures\n", failures);
        std::fflush(stdout);
    };
    // whole-function calls through the resolver (a screen's entry point): what they reach is natives and seams, never retail code
    bool booted = false;
    if (const char* list = std::getenv("OT_SPIKE_CALL")) {
        for (const char* at = list; *at != '\0';) {
            if (std::strncmp(at, "hostcheck", 9) == 0) {
                host_check();
                at += at[9] == ',' ? 10 : 9;
                continue;
            }
            if (std::strncmp(at, "restore", 7) == 0) {
                restore();
                at += at[7] == ',' ? 8 : 7;
                continue;
            }
            // 9: `boot` is the cold boot (standalone_boot.cpp): with no session image (`-`), the C++ initializers, Game_Startup's work and main; the run ends
            // in the frame budget (OT_SPIKE_FRAMES) or the session recorder's limit (OPENTONY_SESSION_TRACE)
            if (std::strncmp(at, "boot", 4) == 0) {
                const auto boot = reinterpret_cast<std::uint32_t(__cdecl*)()>(GetProcAddress(kits, "ot_standalone_boot"));
                if (boot == nullptr) {
                    std::fprintf(stderr, "%s has no ot_standalone_boot\n", argv[2]);
                    return 5;
                }
                std::printf("boot: main returned %08x\n", static_cast<unsigned>(boot()));
                std::fflush(stdout);
                booted = true;   // the game ended by itself (the main menu's Exit): a normal end, whatever main answered
                at += at[4] == ',' ? 5 : 4;
                continue;
            }
            // 8a: `poke:0x006a6964=0` writes a word of the image (a test sets the game's state it needs, as a probe script does in the retail process)
            if (std::strncmp(at, "poke:", 5) == 0) {
                char* next = nullptr;
                const auto where = static_cast<std::uint32_t>(std::strtoul(at + 5, &next, 16));
                const auto value = static_cast<std::uint32_t>(std::strtoul(*next == '=' ? next + 1 : next, &next, 0));
                *arena::host<volatile std::uint32_t>(where) = value;
                std::printf("poke %08x = %08x\n", static_cast<unsigned>(where), static_cast<unsigned>(value));
                at = *next == ',' ? next + 1 : next;
                continue;
            }
            // 8c: `peek:0x006a7678` prints a word of the image and `peekstr:0x006a7578` the text at an address, so a test reads what a call left in the game's memory
            // (`peekp:` follows the pointer stored there first)
            if (std::strncmp(at, "peek:", 5) == 0 || std::strncmp(at, "peekstr:", 8) == 0 || std::strncmp(at, "peekp:", 6) == 0) {
                const bool text = at[4] == 's';
                const bool pointer = at[4] == 'p';
                char* next = nullptr;
                auto where = static_cast<std::uint32_t>(std::strtoul(at + (text ? 8 : pointer ? 6 : 5), &next, 16));
                if (pointer) {
                    const std::uint32_t target = *arena::host<volatile std::uint32_t>(where);
                    std::printf("peekp %08x -> %08x = %08x\n", static_cast<unsigned>(where), static_cast<unsigned>(target),
                                static_cast<unsigned>(*arena::host<volatile std::uint32_t>(target)));
                } else if (text) {
                    std::printf("peekstr %08x = %s\n", static_cast<unsigned>(where), arena::host<const char>(where));
                } else {
                    std::printf("peek %08x = %08x\n", static_cast<unsigned>(where), static_cast<unsigned>(*arena::host<volatile std::uint32_t>(where)));
                }
                at = *next == ',' ? next + 1 : next;
                continue;
            }
            char* end = nullptr;
            const std::uint32_t address = static_cast<std::uint32_t>(std::strtoul(at, &end, 16));
            // 8a: `0x004f2e20(5,100,100)` passes arguments (decimal, or 0x hex; negative allowed)
            std::vector<std::uint32_t> arguments;
            if (*end == '(') {
                ++end;
                while (*end != ')' && *end != '\0') {
                    char* next = nullptr;
                    arguments.push_back(static_cast<std::uint32_t>(std::strtol(end, &next, 0)));
                    if (next == end) {
                        break;   // not a number
                    }
                    end = *next == ',' ? next + 1 : next;
                }
                if (*end == ')') {
                    ++end;
                }
            }
            std::printf("calling %08x through the resolver\n", static_cast<unsigned>(address));
            std::fflush(stdout);
            const std::uint32_t result = call_native(address, arguments.data(), static_cast<std::uint32_t>(arguments.size()));
            std::printf("returned %08x\n", static_cast<unsigned>(result));
            at = *end == ',' ? end + 1 : end;
        }
    }
    // 7b, part 3: the function-level oracle (src/hooks/oracle_script.h). The same script the hooked retail process runs (OPENTONY_SESSION_SCRIPT) runs here against
    // the natives, and the two outputs are compared by tests/test_texture_oracle.py.
    if (const char* script = std::getenv("OT_ORACLE_SCRIPT")) {
        const auto read_surface = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::uint32_t*, std::uint32_t*, std::uint32_t*, unsigned char*, std::uint32_t)>(
            GetProcAddress(kits, "ot_standalone_surface"));
        const char* output = std::getenv("OT_ORACLE_OUT");
        if (read_surface == nullptr || output == nullptr) {
            std::fprintf(stderr, "OT_ORACLE_SCRIPT needs OT_ORACLE_OUT and a DLL that exports ot_standalone_surface\n");
            return 6;
        }
        struct Context {
            decltype(call_native) call;
            decltype(read_surface) surface;
        } context{call_native, read_surface};
        OtOracleBackend backend{};
        backend.context = &context;
        backend.call = [](void* c, std::uint32_t address, int, const std::uint32_t* args, std::uint32_t count) -> std::uint32_t {
            return static_cast<Context*>(c)->call(address, args, count);
        };
        backend.tick = [](void* c) {
            const std::uint32_t arguments[5] = {0, 0, 0x006a05a0, 0, 0};   // SimulationTimeTimerCallback(id, message, TimerState, 0, 0)
            static_cast<Context*>(c)->call(0x004dace0, arguments, 5);
        };
        backend.surface = [](void* c, std::uint32_t handle, std::uint32_t* width, std::uint32_t* height, std::uint32_t* bpp, unsigned char* pixels,
                             std::uint32_t capacity) -> int { return static_cast<Context*>(c)->surface(handle, width, height, bpp, pixels, capacity); };
        const int failed = ot_oracle_run(script, output, &backend);
        std::printf("oracle script %s: %s (line %d)\n", script, failed == 0 ? "ran" : "failed", failed);
        std::fflush(stdout);
        return failed == 0 ? 0 : 7;
    }
    // a cold-boot run checks nothing ("RESULT 0 checked"): when main returned, the game ended by itself, which is exit 0, not a crash
    return booted || (mismatched == 0 && missing == 0 && checked != 0) ? 0 : 1;
}
