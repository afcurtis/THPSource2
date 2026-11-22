// The host's side of the seams that are not the clock, the window or the files (milestone 8c, Phase 5.3c): the C runtime's allocation-free corners (atexit, exit, the vector
// destructor iterator, isspace, stat, the package stream, the error print), upstream zlib in place of the retail executable's zlib 1.1.2, the platform calls of WINMAIN
// (settings, file search, display modes, the disc check, the renderer and resolution switches), the force-feedback and device-shutdown calls of the input layer, the
// DirectPlay answers of a single-player program, and the functions that data tables hold and no code calls (re/config/table_seams.yml).
//
// Each implementation stands in for the retail function's contract as the natives use it, and where the function's disassembly decided a detail (FindFiles' slot rule, the
// settings file, what CD_Recheck does) the comment says so. docs/HOOKS.md, "Standalone".
#include "kit_bridge.hpp"
#include "standalone_options.hpp"
#include "standalone_scores.hpp"
#include "standalone_userdata.hpp"
#include "seam_addresses_generated.hpp"

#include <windows.h>
#include <zlib.h>

#include <direct.h>
#include <io.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "retail_services.hpp"
#include "guest_arena.hpp"
#include "guest_format.hpp"

// mod_overlay.cpp (src/hooks) logs through this; the standalone's log is stderr
void ot_log(const char* format, ...) {
    std::va_list list;
    va_start(list, format);
    std::fputs("game: ", stderr);
    std::vfprintf(stderr, format, list);
    std::fputc('\n', stderr);
    va_end(list);
}

namespace opentony::kitdll {
namespace {

using Args = std::vector<std::uint32_t>;
using Implementation = std::uint32_t (*)(const Args&, const std::string&);

constexpr std::uint32_t kFailed = 0xffffffffu;

// the C runtime functions a stream user reaches that are not registered seams (src/runtime/core_pkr.cpp names them by address)
constexpr std::uint32_t kStreamClose = 0x00500e7b;   // fclose
constexpr std::uint32_t kStreamRead = 0x00502358;    // fread(destination, size, count, stream)
constexpr std::uint32_t kStreamWrite = 0x0050195d;   // fwrite(source, size, count, stream)
constexpr std::uint32_t kRemove = 0x00501fa0;        // remove(path): 0, or -1 for a file that could not be deleted
constexpr std::uint32_t kUncompress = 0x004f9d50;    // zlib uncompress(destination, &destination length, source, source length)

constexpr std::uint32_t kTimerState = 0x006a05a0;    // TimerState: the multimedia timer's id (0: none)
constexpr std::uint32_t kPackageManager = 0x004e9780;   // PKR2_Preload(name)

template <typename T>
T& word(std::uint32_t address) {
    return *arena::host<T>(address);
}

const char* text_at(std::uint32_t address) { return arena::host<const char>(address); }

std::string host_path(std::string path) {
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    return user_path(path);   // a save, a park or TH2_OPT.CFG of the working directory lives in Saves or Config (standalone_userdata.hpp)
}

// ---- the C runtime ----------------------------------------------------------------------------------------------------------

std::vector<std::uint32_t> g_exit_functions;

// atexit(function): 0 when registered. The functions are retail addresses (natives or seams), run in reverse order by exit().
std::uint32_t crt_atexit(const Args& args, const std::string&) {
    if (args.empty() || args[0] == 0) {
        return kFailed;
    }
    g_exit_functions.push_back(args[0]);
    return 0;
}

// exit(code) (0x00501439): the registered functions in reverse order, the C streams flushed, the process ended. A function that registers another while exit runs is run too.
std::uint32_t crt_exit(const Args& args, const std::string&) {
    while (!g_exit_functions.empty()) {
        const std::uint32_t function = g_exit_functions.back();
        g_exit_functions.pop_back();
        call_native_or_seam(function, {});
    }
    std::fflush(nullptr);
    scores_shutdown();   // the score file (standalone_scores.cpp): a run held for its initials entry
    ExitProcess(args.empty() ? 0 : args[0]);
    return 0;
}

// FUN_0050030c, the vector destructor iterator (array, element size, count, destructor): the destructor, a thiscall, runs on the elements from the last to the first (the
// loop at 0x00500343 subtracts the size before each call). The constructor iterator is in standalone_seams.cpp.
std::uint32_t crt_vector_destruct(const Args& args, const std::string&) {
    if (args.size() < 4 || args[3] == 0) {
        return 0;
    }
    for (std::uint32_t k = args[2]; k-- > 0;) {
        call_native_or_seam(args[3], {args[0] + k * args[1]});
    }
    return 0;
}

// FUN_005009d1 is isspace as the single-threaded C locale answers it: the ctype table's blank flag (8) of the character, 0 outside 0..255
std::uint32_t crt_isspace(const Args& args, const std::string&) {
    const std::uint32_t c = args.empty() ? 0 : args[0];
    return c <= 0xff && std::isspace(static_cast<int>(c)) ? 8u : 0u;
}

// stat(path, buffer) (0x005005d3): the 32-bit struct _stat of the MSVC 6 runtime, which the UCRT's _stat32 has byte for byte (st_dev 0, st_ino 4, st_mode 6, st_nlink 8,
// st_uid 10, st_gid 12, st_rdev 16, st_size 20, the three times from 24: 36 bytes). 0, or -1 for a path that is not there.
std::uint32_t crt_stat(const Args& args, const std::string&) {
    if (args.size() < 2) {
        return kFailed;
    }
    static_assert(sizeof(struct _stat32) == 36, "the MSVC 6 struct _stat is 36 bytes");
    struct _stat32 status {};
    if (_stat32(host_path(text_at(args[0])).c_str(), &status) != 0) {
        return kFailed;
    }
    std::memcpy(arena::host<void>(args[1]), &status, sizeof status);
    return 0;
}

// _findfirst(pattern, data) / _findnext(handle, data) / _findclose(handle) (0x0050246f / 0x0050253c / 0x00502604; milestone 9: the memory card's directory scan at
// start-up): the MSVC 6 runtime's struct _finddata_t, which the UCRT's _finddata32_t has byte for byte (attrib 0, the three times from 4, size 0x10, name 0x14,
// 0x118 bytes). The handle is the UCRT's; -1 when nothing matches, and 0 / -1 from the others.
// The program holds a find handle as a 32-bit number: here an index into a table of the UCRT's handles (intptr_t, 64 bits wide on x86-64), plus one, so that 0
// and -1 are never handles (docs/PORT64_AUDIT.md D2).
constexpr std::size_t kFinds = 8;
std::intptr_t g_finds[kFinds];   // 0: free

std::intptr_t* find_slot(std::uint32_t handle) {
    return handle >= 1 && handle <= kFinds && g_finds[handle - 1] != 0 ? &g_finds[handle - 1] : nullptr;
}

std::uint32_t crt_findfirst(const Args& args, const std::string&) {
    if (args.size() < 2) {
        return kFailed;
    }
    static_assert(sizeof(_finddata32_t) == 0x118, "the MSVC 6 struct _finddata_t is 0x118 bytes");
    const std::intptr_t found = _findfirst32(host_path(text_at(args[0])).c_str(), arena::host<_finddata32_t>(args[1]));
    if (found == -1) {
        return kFailed;
    }
    for (std::size_t k = 0; k < kFinds; ++k) {
        if (g_finds[k] == 0) {
            g_finds[k] = found;
            return static_cast<std::uint32_t>(k + 1);
        }
    }
    _findclose(found);   // eight scans open at once: retail's start-up has one
    throw runtime::ServiceFault("_findfirst: the table of open scans is full");
}

std::uint32_t crt_findnext(const Args& args, const std::string&) {
    if (args.size() < 2) {
        return kFailed;
    }
    const std::intptr_t* slot = find_slot(args[0]);
    return slot == nullptr ? kFailed : static_cast<std::uint32_t>(_findnext32(*slot, arena::host<_finddata32_t>(args[1])));
}

std::uint32_t crt_findclose(const Args& args, const std::string&) {
    std::intptr_t* slot = args.empty() ? nullptr : find_slot(args[0]);
    if (slot == nullptr) {
        return kFailed;
    }
    const int status = _findclose(*slot);
    *slot = 0;
    return static_cast<std::uint32_t>(status);
}

// printf-style text from the C runtime's error print (0x00502688): the format and the argument words the caller pushed (docs/PORT64_AUDIT.md 5.4 item 4: the text is
// made by the guest formatter, so no host printf sees a guest word)
std::uint32_t crt_log_error(const Args& args, const std::string&) {
    if (args.empty()) {
        return 0;
    }
    std::size_t next = 1;
    std::string line = runtime::guest::format(
        text_at(args[0]), {[&args, &next]() -> std::uint32_t {
                               if (next >= args.size()) {
                                   throw runtime::ServiceFault("error print: the format converts more arguments than the caller pushed");
                               }
                               return args[next++];
                           },
                           [](std::uint32_t address) { return arena::readable_text(address); }});
    // _vsnprintf into the old 1024-byte line: -1 when the text did not fit
    const std::uint32_t length = line.size() > 1023 ? 0xffffffffu : static_cast<std::uint32_t>(line.size());
    if (line.size() > 1023) {
        line.resize(1023);
    }
    std::fprintf(stderr, "game: %s", line.c_str());
    if (line.empty() || line.back() != '\n') {
        std::fputc('\n', stderr);
    }
    return length;
}

// ---- the package stream -------------------------------------------------------------------------------------------------------
//
// PKR2_OpenPackageStream (0x005013f9) is fopen(name, mode) and its reader and closer are the CRT's fread and fclose (CoreKit::pkr_preload and the package reads). The stream
// is a host FILE; the number the program holds is an index into this table, never a pointer into the process.

constexpr std::size_t kStreams = 16;
constexpr std::uint32_t kStreamBase = 0x7f000000;
std::FILE* g_streams[kStreams];

std::uint32_t stream_number(std::size_t slot) { return kStreamBase + 0x40u * static_cast<std::uint32_t>(slot + 1); }

std::FILE* stream_of(std::uint32_t number) {
    if (number < kStreamBase + 0x40u || (number - kStreamBase) % 0x40u != 0) {
        return nullptr;
    }
    const std::size_t slot = (number - kStreamBase) / 0x40u - 1;
    return slot < kStreams ? g_streams[slot] : nullptr;
}

// where the package is: the path the program built (its game directory and "all.pkr"), then OPENTONY_PKR, then the name in the working directory
std::FILE* open_package(const std::string& name, const char* mode) {
    std::vector<std::string> candidates = {host_path(name)};
    // the other places are for reading the package; a file opened to be written or updated (a save, a custom park) is only ever the path the program built, so a missing
    // save can never turn into the package opened for writing
    if (std::strpbrk(mode, "wa+") != nullptr) {
        if (user_file_locked(candidates.front())) {
            return nullptr;   // a quick start never writes a save or a park
        }
        std::FILE* file = std::fopen(candidates.front().c_str(), mode);
        if (std::getenv("OT_FILES_LOG") != nullptr) {
            std::fprintf(stderr, "files: write %s (%s) -> %s\n", candidates.front().c_str(), mode, file != nullptr ? "ok" : "FAILED");
        }
        return file;
    }
    if (const char* override_path = std::getenv("OPENTONY_PKR"); override_path != nullptr && *override_path != '\0') {
        candidates.emplace_back(override_path);
    }
    const std::size_t slash = candidates.front().find_last_of('/');
    candidates.push_back(slash == std::string::npos ? candidates.front() : candidates.front().substr(slash + 1));
    for (const std::string& path : candidates) {
        if (std::FILE* file = std::fopen(path.c_str(), mode)) {
            if (std::getenv("OT_FILES_LOG") != nullptr) {
                std::fprintf(stderr, "files: package %s -> ok\n", path.c_str());
            }
            return file;
        }
    }
    if (std::getenv("OT_FILES_LOG") != nullptr) {
        std::fprintf(stderr, "files: package %s -> NOT FOUND\n", name.c_str());
    }
    return nullptr;
}

std::uint32_t crt_open_package(const Args& args, const std::string&) {
    if (args.size() < 2) {
        return 0;
    }
    for (std::size_t slot = 0; slot < kStreams; ++slot) {
        if (g_streams[slot] == nullptr) {
            g_streams[slot] = open_package(text_at(args[0]), text_at(args[1]));
            return g_streams[slot] != nullptr ? stream_number(slot) : 0;
        }
    }
    return 0;
}

// fread(destination, size, count, stream): the complete items read
std::uint32_t crt_fread(const Args& args, const std::string&) {
    std::FILE* file = args.size() >= 4 ? stream_of(args[3]) : nullptr;
    if (file == nullptr) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::fread(arena::host<void>(args[0]), args[1], args[2], file));
}

// fwrite(source, size, count, stream): the complete items written (the saves: the memory card's file backend, a custom park)
std::uint32_t crt_fwrite(const Args& args, const std::string&) {
    std::FILE* file = args.size() >= 4 ? stream_of(args[3]) : nullptr;
    if (file == nullptr) {
        return 0;
    }
    const auto written = std::fwrite(arena::host<const void>(args[0]), args[1], args[2], file);
    if (std::getenv("OT_FILES_LOG") != nullptr) {
        std::fprintf(stderr, "files: fwrite %u x %u -> %u\n", static_cast<unsigned>(args[1]), static_cast<unsigned>(args[2]), static_cast<unsigned>(written));
    }
    return static_cast<std::uint32_t>(written);
}

// remove(path): the file deleted (a save or a custom park about to be written again)
std::uint32_t crt_remove(const Args& args, const std::string&) {
    if (args.empty()) {
        return kFailed;
    }
    const std::string path = host_path(text_at(args[0]));
    if (user_file_locked(path)) {
        return kFailed;
    }
    const bool removed = std::remove(path.c_str()) == 0;
    if (std::getenv("OT_FILES_LOG") != nullptr) {
        std::fprintf(stderr, "files: remove %s -> %s\n", path.c_str(), removed ? "ok" : "NOT FOUND");
    }
    return removed ? 0u : kFailed;
}

std::uint32_t crt_fclose(const Args& args, const std::string&) {
    std::FILE* file = args.empty() ? nullptr : stream_of(args[0]);
    if (file == nullptr) {
        return kFailed;
    }
    g_streams[(args[0] - kStreamBase) / 0x40u - 1] = nullptr;
    if (std::getenv("OT_FILES_LOG") != nullptr) {
        std::fprintf(stderr, "files: fclose\n");
    }
    return static_cast<std::uint32_t>(std::fclose(file));
}

// ---- zlib ---------------------------------------------------------------------------------------------------------------------
//
// compress / uncompress (destination, &destination length, source, source length) -> status, as upstream zlib answers them: 0 Z_OK, -4 Z_MEM_ERROR, -5 Z_BUF_ERROR, -3
// Z_DATA_ERROR. The destination length is a 32-bit word the call reads and then holds the bytes written. CoreKit's wrappers (core_pkr.cpp) choose their messages from those codes.

std::uint32_t zlib_compress_seam(const Args& args, const std::string&) {
    if (args.size() < 4) {
        return static_cast<std::uint32_t>(Z_STREAM_ERROR);
    }
    uLongf length = word<std::uint32_t>(args[1]);
    const int status = compress(arena::host<Bytef>(args[0]), &length, arena::host<const Bytef>(args[2]), args[3]);
    word<std::uint32_t>(args[1]) = static_cast<std::uint32_t>(length);
    return static_cast<std::uint32_t>(status);
}

std::uint32_t zlib_uncompress_seam(const Args& args, const std::string&) {
    if (args.size() < 4) {
        return static_cast<std::uint32_t>(Z_STREAM_ERROR);
    }
    uLongf length = word<std::uint32_t>(args[1]);
    const int status = uncompress(arena::host<Bytef>(args[0]), &length, arena::host<const Bytef>(args[2]), args[3]);
    word<std::uint32_t>(args[1]) = static_cast<std::uint32_t>(length);
    return static_cast<std::uint32_t>(status);
}

// ---- WINMAIN: settings, file search, display ----------------------------------------------------------------------------------

// The settings file (retail's is the INI whose path WINMAIN keeps at 0x029d8378): OPENTONY_SETTINGS, else THawk2.ini in the working directory. GetPrivateProfileString
// resolves a bare name against the Windows directory, so the path is made absolute.
const std::string& settings_path() {
    static const std::string path = [] {
        const char* configured = std::getenv("OPENTONY_SETTINGS");
        char full[MAX_PATH];
        const DWORD length = GetFullPathNameA(configured != nullptr && *configured != '\0' ? configured : "THawk2.ini", sizeof full, full, nullptr);
        return std::string(full, length < sizeof full ? length : 0);
    }();
    return path;
}

// WINMAIN_LoadSetting(section, key, default, out): GetPrivateProfileString into `out` (0x100 bytes), the default when the file has none
std::uint32_t winmain_load_setting(const Args& args, const std::string&) {
    if (args.size() < 4) {
        return 0;
    }
    return GetPrivateProfileStringA(text_at(args[0]), text_at(args[1]), text_at(args[2]), arena::host<char>(args[3]), 0x100, settings_path().c_str());
}

// WINMAIN_SaveSetting(section, key, value): WritePrivateProfileString, then the flush call retail makes (all-null arguments)
std::uint32_t winmain_save_setting(const Args& args, const std::string&) {
    if (args.size() < 3) {
        return 0;
    }
    WritePrivateProfileStringA(text_at(args[0]), text_at(args[1]), text_at(args[2]), settings_path().c_str());
    WritePrivateProfileStringA(nullptr, nullptr, nullptr, settings_path().c_str());
    return 0;
}

// WINMAIN_FindFiles(pattern, names, count, buffer, include_hidden) (0x004f6560, from its disassembly): FindFirstFile / FindNextFile over the pattern. Each hit is copied
// to the next 0x104-byte slot of `buffer` and its address stored in `names` (when that is given); the slot is kept when the name does not start with '!' (unless the last
// argument is set) and is shorter than 31 characters, else the next hit overwrites it. The loop ends at `count` kept names. The answer is the number kept plus one, so a
// search that finds nothing, or fails, answers 1.
std::uint32_t winmain_find_files(const Args& args, const std::string&) {
    if (args.size() < 5) {
        return 1;
    }
    const std::uint32_t count = args[2];
    WIN32_FIND_DATAA found;
    const HANDLE handle = FindFirstFileA(host_path(text_at(args[0])).c_str(), &found);
    if (handle == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "game: Invalid File Handle. Get Last Error reports %lu\n", GetLastError());
        return 1;
    }
    std::uint32_t kept = 0;
    for (;;) {
        char* slot = arena::host<char>(args[3] + 0x104u * kept);
        if (args[1] != 0) {
            word<std::uint32_t>(args[1] + 4u * kept) = args[3] + 0x104u * kept;
            std::strcpy(slot, found.cFileName);
        }
        if ((args[4] & 0xff) != 0 || found.cFileName[0] != '!') {
            if (std::strlen(found.cFileName) < 0x1f) {
                ++kept;
            }
        }
        if (!FindNextFileA(handle, &found) || kept >= count) {
            break;
        }
    }
    FindClose(handle);
    return kept + 1;
}

constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;
constexpr std::uint32_t kModeList = 0x029d8478;   // {count, then DDSURFACEDESC2 records of 0x7c bytes} that the enumeration callback (0x004f79e0) fills, at most 64

// WINMAIN_EnumDisplayModes (0x004f7a70): retail asks DirectDraw and the callback keeps the 16-bit RGB modes up to 1024 x 768. The standalone has one mode, the game's own
// 640 x 480 at 16 bits (the HAL scales it to the window), recorded as the callback records it: the DDSURFACEDESC2 with its size (+0), height (+8), width (+0xc), pixel
// format size (+0x48), RGB flag (+0x4c) and bit count (+0x54).
std::uint32_t winmain_enum_display_modes(const Args&, const std::string&) {
    word<std::uint32_t>(kModeList) = 1;
    std::uint8_t* entry = arena::host<std::uint8_t>(kModeList + 4);
    std::memset(entry, 0, 0x7c);
    *reinterpret_cast<std::uint32_t*>(entry + 0x00) = 0x7c;
    *reinterpret_cast<std::uint32_t*>(entry + 0x08) = 480;   // (the listed mode stays retail's 640 x 480 whatever the display: the game matches it by name)
    *reinterpret_cast<std::uint32_t*>(entry + 0x0c) = 640;
    *reinterpret_cast<std::uint32_t*>(entry + 0x48) = 0x20;
    *reinterpret_cast<std::uint32_t*>(entry + 0x4c) = 0x40;
    *reinterpret_cast<std::uint32_t*>(entry + 0x54) = 16;
    std::fputs("game: Got Video Mode: 640x480x16bpp\n", stderr);
    return 0;
}

// WINMAIN_SwitchResolution(mode): retail resets the DirectDraw display; the standalone renders 640 x 480 whatever the window, so both modes (1: the table's entry, 2: 640 x 480)
// leave the game's screen words at that size
std::uint32_t winmain_switch_resolution(const Args&, const std::string&) {
    word<std::uint32_t>(kScreenWidth) = static_cast<std::uint32_t>(display().width);   // 640 x 480 unless the program made a wider display (standalone_options.hpp)
    word<std::uint32_t>(kScreenHeight) = static_cast<std::uint32_t>(display().height);
    return 0;
}

// WINMAIN_SwitchRenderer(mode, choice): there is one renderer (the HAL's); the request is logged as retail logs it and nothing changes
std::uint32_t winmain_switch_renderer(const Args& args, const std::string&) {
    std::fprintf(stderr, "game: \t\tD3D RENDERER: From %i to %i\r\n", args.empty() ? 0 : static_cast<int>(args[0] & 0xff), args.size() < 2 ? 0 : static_cast<int>(args[1]));
    return 0;
}

// WINMAIN_Sleep(milliseconds): the program's clock moves and the multimedia timer's tick that falls due is delivered; the answer is SimulationPublicTick, which is what
// begin_scene and the frame limiters read back (standalone_seams.cpp)
std::uint32_t winmain_sleep(const Args&, const std::string&) { return standalone_sleep_tick(); }

// PCinit (0x004e73f0, from its disassembly): the ten-slot file table and the device words cleared, the working directory read, "EXE RAN FROM" logged, the game directory
// (0x029da3c4: OPENTONY_GAME_DIR, else the working directory) and "\all.pkr" made into the package name at 0x006a72bc, and the package's tables loaded (PKR2_Preload). Not done:
// the install-drive search of 0x004f6730 and the settings read of 0x004f6920, which find the CD and the install directory.
constexpr std::uint32_t kGameDirectory = 0x029da3c4;
constexpr std::uint32_t kWorkingDirectory = 0x006a71bc;
constexpr std::uint32_t kPackageName = 0x006a72bc;

std::uint32_t pcinit_seam(const Args&, const std::string&) {
    for (const std::uint32_t base : {0x006a6d44u, 0x006a716cu, 0x006a7194u, 0x006a6d1cu}) {
        std::memset(arena::host<void>(base), 0, 40);
    }
    word<std::uint32_t>(0x006a6d10) = 0;
    word<std::uint32_t>(0x006a6d14) = 0;
    word<std::uint16_t>(0x006a6d18) = 0;
    char directory[256];
    if (_getcwd(directory, sizeof directory) == nullptr) {
        directory[0] = '\0';
    }
    std::strcpy(arena::host<char>(kWorkingDirectory), directory);
    std::fprintf(stderr, "game: EXE RAN FROM: %s\r\n", directory);
    const char* game = std::getenv("OPENTONY_GAME_DIR");
    std::string root = game != nullptr && *game != '\0' ? game : directory;
    std::strncpy(arena::host<char>(kGameDirectory), root.c_str(), 255);
    std::string package = root + "\\all.pkr";
    std::strncpy(arena::host<char>(kPackageName), package.c_str(), 255);
    call_native_or_seam(kPackageManager, {kPackageName});
    return 0;
}

// CD_Recheck (0x004f6510): retail asks 0x004bb240(0) whether the disc is in the drive and, when it is not, shows a message box and quits. The standalone's data is on disk.
std::uint32_t cd_recheck(const Args&, const std::string&) { return 0; }

// D3D_Shutdown(full) (0x004d0f80): releases the device and the surfaces. The HAL's video belongs to the program, which ends it; the game's own teardown has nothing to do.
std::uint32_t d3d_shutdown(const Args&, const std::string&) { return 0; }

// D3DTimer_Shutdown (0x004dab80): the multimedia timer is killed (timeKillEvent) and its id word cleared, which also ends the tick delivery of Game_FrameTick
std::uint32_t d3d_timer_shutdown(const Args&, const std::string&) {
    word<std::uint32_t>(kTimerState) = 0;
    return 0;
}

// FUN_004f2700 (0x004f2700) is the CPUID test for MMX (EDX bit 23); the machines the program runs on all have it
std::uint32_t has_mmx(const Args&, const std::string&) { return 1; }

// FUN_004f23d0 (0x004f23d0): the software renderer's teardown. The draw path has no software renderer (DrawKit stops the program if D3DSoftwareRenderer is set).
std::uint32_t soft_renderer_shutdown(const Args&, const std::string&) { return 0; }

// ---- input devices ------------------------------------------------------------------------------------------------------------

// PCInput_Shutdown (0x004e4a60) releases the DirectInput devices; the HAL's input service is the program's. PCInput_ActuatorOn / Off (0x004e4c40 / 0x004e4cf0) start and stop
// a force-feedback effect on a port whose device has one (0x006a3f80 / 0x006a695d): there is none, and the HAL has no rumble yet.
std::uint32_t input_nothing(const Args&, const std::string&) { return 0; }

// PCInput_ActuatorOn(port, duration, motor, strength) and PCInput_ActuatorOff on the HAL's pad (standalone_options.cpp); a program without a pad does nothing
std::uint32_t input_actuator_on(const Args& args, const std::string&) {
    if (args.size() >= 4) {
        options_rumble(args[0], args[1], args[2], args[3]);
    }
    return 0;
}
std::uint32_t input_actuator_off(const Args&, const std::string&) {
    options_rumble_stop();
    return 0;
}

// FUN_004ebfe0 (this): Release the COM object at +4 (vtable slot 2, the object pushed) and clear the pointer. The streams and the SDX wrappers call it from their destructors.
std::uint32_t release_member(const Args& args, const std::string&) {
    if (args.empty()) {
        return 0;
    }
    const std::uint32_t object = word<std::uint32_t>(args[0] + 4);
    if (object != 0) {
        const std::uint32_t table = word<std::uint32_t>(object);
        call_native_or_seam(word<std::uint32_t>(table + 8), {object});
    }
    word<std::uint32_t>(args[0] + 4) = 0;
    return 0;
}

// ---- DirectPlay ---------------------------------------------------------------------------------------------------------------
//
// Online and LAN play are dropped (scope.yml, category directplay) and the netgame flag (0x006a3b21) stays clear, so these run only when a menu screen asks the network for
// something. A single-player program has no session, no player and no message: every question gets the empty answer, every command is accepted and does nothing.
//   NetGame_PollMessages -> 0 messages; NetGame_NextMessage -> no message; NetGame_Latency -> 0 ticks; NetGame_SendPacket -> DP_OK; FUN_004dba20 (find a session record) -> none;
//   FUN_004dbc50 (connect) -> a negative HRESULT, which NetGame::connect turns into "not joined"; the rest are commands and initialisers.
std::uint32_t net_nothing(const Args&, const std::string&) { return 0; }
std::uint32_t net_connect_refused(const Args&, const std::string&) { return kFailed; }

// ---- functions that tables hold and no code calls (re/config/table_seams.yml) --------------------------------------------------

// The scalar deleting destructors of the SDX input classes sit in vtables of objects only the DirectInput layer creates; none exists here, so the call answers `this`.
std::uint32_t sdx_scalar_delete(const Args& args, const std::string&) { return args.empty() ? 0 : args[0]; }

// zlib 1.1.2's deflate strategies (deflate_stored / deflate_fast, 0x004fa990 / 0x004fac60) sit in its configuration table, and the C++ exception runtime's unwind funclets and
// handlers sit in the .rdata unwind maps. They are internals of code the host replaces whole (upstream zlib, the toolchain's exception runtime): nothing reaches them but the
// code they belong to. A call is a bug in the program, not a missing feature, and stops it with the reason.
std::uint32_t replaced_internal(const Args&, const std::string& text) {
    throw runtime::ServiceFault("a function of the retail C runtime or zlib that only the replaced code reaches was called" + (text.empty() ? std::string() : ": " + text));
}

struct HostSeam {
    std::uint32_t address;
    Implementation implementation;
};

const HostSeam kHostSeams[] = {
    // the C runtime
    {seam::crt::CRT_atexit, &crt_atexit},
    {seam::crt::FUN_0050030c, &crt_vector_destruct},
    {seam::crt::CRT_stat, &crt_stat},
    {0x0050246f, &crt_findfirst},
    {0x0050253c, &crt_findnext},
    {0x00502604, &crt_findclose},
    {seam::crt::FUN_005009d1, &crt_isspace},
    {seam::crt::PKR2_OpenPackageStream, &crt_open_package},
    {seam::crt::CRT_exit, &crt_exit},
    {seam::crt::CRT_log_error, &crt_log_error},
    {kStreamClose, &crt_fclose},
    {kStreamRead, &crt_fread},
    {kStreamWrite, &crt_fwrite},
    {kRemove, &crt_remove},
    // zlib
    {seam::zlib::zlib_compress, &zlib_compress_seam},
    {kUncompress, &zlib_uncompress_seam},
    // WINMAIN and the display
    {seam::hal::WINMAIN_LoadSetting, &winmain_load_setting},
    {seam::hal::WINMAIN_SaveSetting, &winmain_save_setting},
    {seam::hal::WINMAIN_FindFiles, &winmain_find_files},
    {seam::hal::WINMAIN_EnumDisplayModes, &winmain_enum_display_modes},
    {seam::hal::WINMAIN_SwitchResolution, &winmain_switch_resolution},
    {seam::hal::WINMAIN_SwitchRenderer, &winmain_switch_renderer},
    {seam::hal::WINMAIN_Sleep, &winmain_sleep},
    {seam::hal::PCinit, &pcinit_seam},
    {seam::hal::CD_Recheck, &cd_recheck},
    {seam::hal::D3D_Shutdown, &d3d_shutdown},
    {seam::hal::D3DTimer_Shutdown, &d3d_timer_shutdown},
    {seam::hal::FUN_004f2700, &has_mmx},
    {seam::hal::FUN_004f23d0, &soft_renderer_shutdown},
    // input
    {seam::hal::PCInput_Shutdown, &input_nothing},
    {seam::hal::PCInput_ActuatorOn, &input_actuator_on},
    {seam::hal::PCInput_ActuatorOff, &input_actuator_off},
    {seam::hal::FUN_004ebfe0, &release_member},
    // DirectPlay
    {seam::net::FUN_004db1a0, &net_nothing},
    {seam::net::NetGame_PollMessages, &net_nothing},
    {seam::net::NetGame_NextMessage, &net_nothing},
    {seam::net::NetGame_Latency, &net_nothing},
    {seam::net::NetGame_SendPacket, &net_nothing},
    {seam::net::FUN_004dba20, &net_nothing},
    {seam::net::FUN_004dbc50, &net_connect_refused},
    {seam::net::FUN_004dbcc0, &net_nothing},
    {seam::net::NetGame_SessionStart, &net_nothing},
    {seam::net::FUN_004ddbf0, &net_nothing},
    {seam::net::FUN_004ddd60, &net_nothing},
    {seam::net::FUN_004de6d0, &net_nothing},     // (the remote skater's frame: only a network game has one)
    {seam::net::FUN_004de790, &net_nothing},     // (the hit message: sent only while the network game byte 0x006a3b21 is set)
    {seam::net::FUN_004dea30, &net_nothing},
    {seam::net::FUN_004dec50, &net_nothing},
    {seam::net::NetGame_LinkLost, &net_nothing},
    {seam::net::FUN_004df170, &net_nothing},
    {seam::net::FUN_004df420, &net_nothing},
    {seam::net::NetGame_EventSend, &net_nothing},
    {seam::net::FUN_004e0120, &net_nothing},
    {seam::net::NetGame_SendLevel, &net_nothing},
    {seam::net::NetGame_SendSkater, &net_nothing},
    {seam::net::NetGame_HandleMessage, &net_nothing},
    // held by tables
    {0x004ec4c0, &sdx_scalar_delete},
    {0x004ec720, &sdx_scalar_delete},
    {0x004ecca0, &sdx_scalar_delete},
    {0x004ed490, &sdx_scalar_delete},
    {0x004fa990, &replaced_internal},
    {0x004fac60, &replaced_internal},
    {0x00500182, &replaced_internal},
    {0x00500199, &replaced_internal},
    {0x00500374, &replaced_internal},
    {0x0050046a, &replaced_internal},
    {0x0050144a, &replaced_internal},
    {0x00502d0e, &replaced_internal},
    {0x00503b54, &replaced_internal},
    {0x00506162, &replaced_internal},
    {0x0050a808, &replaced_internal},
    {0x0050b5a4, &replaced_internal},
    {0x0050e02e, &replaced_internal},
};

}  // namespace

bool has_host_seam(std::uint32_t address) {
    for (const HostSeam& seam : kHostSeams) {
        if (seam.address == address) {
            return true;
        }
    }
    return false;
}

bool call_host_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string& text, std::uint32_t& result) {
    for (const HostSeam& seam : kHostSeams) {
        if (seam.address == address) {
            result = seam.implementation(args, text);
            return true;
        }
    }
    return false;
}

}  // namespace opentony::kitdll
