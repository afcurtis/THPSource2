// opentony.exe: the program a player starts (Phase 5, milestone 12). It does the two one-time reads of the player's own files and then starts the standalone.
// It lives in <install>\Build; the install folder's layout is src/launcher/layout.hpp.
//
//   1. THawk2.exe -> <install>\Data\Unpacked\thawk2_static.img  the static data (tables, strings, constants) at their retail addresses; the same bytes
//      `tony hooks static-data` writes (tony/static_data.py). The Activision Value re-release's are the natives' own; the original 2000 release's (its
//      unprotected THawk2.exe) are rebuilt at those addresses (original_2000_static.inc) and checked by their SHA-256. After this the executable is not needed.
//   2. ALL.PKR    -> <install>\Data\Unpacked\data\...           the package's files, copied out of the archive (src/assets/pkr_asset.cpp); the same bytes
//      `tony assets extract-pkr` writes.
//   3. opentony_spike.exe (the standalone: linked at the retail base, hence a program of its own) is started in the game folder (<install>\Data\Game, or the
//      installed game that --game-dir names) with the window, the input, the sound and the real-time clock switched on. Its settings file, saves, parks, mods
//      and logs are redirected to <install>\Config, Saves, Mods and Logs.
//
// Both steps are repeated only when their input changed (size and time of the file). Settings the player has already made in the environment win.
//
//   opentony [--root INSTALL] [--game-dir DIR] [--prepare] [--force] [--movies]
#include "assets/pkr_asset.hpp"
#include "launcher/layout.hpp"
#include "launcher/original_2000_static.inc"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// re/config/native_hooks.yml builds.canonical (tests/test_launcher.py keeps these equal)
constexpr const char* kCanonicalSha256 = "f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c";
constexpr std::uint32_t kImageBase = 0x00400000u;
constexpr std::uint32_t kTimestamp = 0x3a54c1f6u;
constexpr std::uint32_t kSizeOfImage = 0x025de000u;
constexpr std::uint32_t kStaticVersion = 1;
// re/xref/config.yml variants.original_2000.protected_disc_executable: the original disc's own THawk2.exe, named when it is refused
constexpr const char* kProtectedDiscSha256 = "bca39d172fff978c270a065903a394260de45f171f67ad00d9a094f66b35a9d9";

[[noreturn]] void fail(const std::string& message, int code = 1) {
    std::fprintf(stderr, "opentony: %s\n", message.c_str());
    std::exit(code);
}

std::vector<std::uint8_t> read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail("cannot read " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_all(const fs::path& path, const void* bytes, std::size_t size) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    if (!out) {
        fail("cannot write " + path.string());
    }
}

std::string sha256_hex(const std::vector<std::uint8_t>& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::uint8_t digest[32] = {};
    const bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
                    BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
                    BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) == 0 &&
                    BCryptFinishHash(hash, digest, sizeof digest, 0) == 0;
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    if (!ok) {
        fail("the system's SHA-256 is not available");
    }
    char text[65];
    for (int k = 0; k < 32; ++k) {
        std::snprintf(text + 2 * k, 3, "%02x", digest[k]);
    }
    return text;
}

// A file of the game directory by name, whatever its case (a copied install may have any).
fs::path find_in(const fs::path& directory, const char* name) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        std::string candidate = entry.path().filename().string();
        std::string wanted = name;
        auto lower = [](std::string& s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); };
        lower(candidate);
        lower(wanted);
        if (candidate == wanted && entry.is_regular_file(ec)) {
            return entry.path();
        }
    }
    return {};
}

// "size:last write" of a file: the stamp that says an input did not change
std::string stamp_of(const fs::path& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    const auto time = fs::last_write_time(path, ec).time_since_epoch().count();
    return std::to_string(size) + ":" + std::to_string(static_cast<long long>(time));
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Part {
    std::uint32_t va;
    const std::uint8_t* bytes;
    std::uint32_t size;
};

// The sections of a PE file that are not code, with their initialised bytes (the smaller of the raw and the virtual size)
std::vector<Part> data_sections(const std::vector<std::uint8_t>& exe, const IMAGE_NT_HEADERS32* nt) {
    std::vector<Part> parts;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned k = 0; k < nt->FileHeader.NumberOfSections; ++k, ++section) {
        if ((section->Characteristics & IMAGE_SCN_CNT_CODE) != 0) {
            continue;
        }
        const std::uint32_t size = std::min(section->SizeOfRawData, section->Misc.VirtualSize);
        if (static_cast<std::size_t>(section->PointerToRawData) + size > exe.size()) {
            continue;
        }
        parts.push_back({kImageBase + section->VirtualAddress, exe.data() + section->PointerToRawData, size});
    }
    return parts;
}

// "OTSD", version, image base, timestamp, size of image, section count, a (va, size) table, and the bytes of every part, in order
std::vector<std::uint8_t> static_image_of(const std::vector<Part>& parts) {
    std::vector<std::uint8_t> out;
    auto put32 = [&](std::uint32_t value) {
        for (int k = 0; k < 4; ++k) {
            out.push_back(static_cast<std::uint8_t>(value >> (8 * k)));
        }
    };
    out.insert(out.end(), {'O', 'T', 'S', 'D'});
    put32(kStaticVersion);
    put32(kImageBase);
    put32(kTimestamp);
    put32(kSizeOfImage);
    put32(static_cast<std::uint32_t>(parts.size()));
    for (const Part& part : parts) {
        put32(part.va);
        put32(part.size);
    }
    for (const Part& part : parts) {
        out.insert(out.end(), part.bytes, part.bytes + part.size);
    }
    return out;
}

// The original 2000 release's data, rebuilt at the canonical addresses by the recipe tony/static_variant.py applies (re/xref/original_2000_static.yml;
// original_2000_static.inc is its copy): copies, the address words of the copies moved by the breakpoints, zeros, then the literal bytes.
std::vector<std::uint8_t> rebuild_original_2000(const std::vector<std::uint8_t>& exe, const IMAGE_NT_HEADERS32* nt) {
    namespace recipe = opentony::original_2000;
    const std::vector<Part> sources = data_sections(exe, nt);
    const std::uint32_t image_end = kImageBase + nt->OptionalHeader.SizeOfImage;
    std::vector<std::vector<std::uint8_t>> sections;
    for (const auto& range : recipe::kSections) {
        sections.emplace_back(range.size, 0);
    }
    auto target = [&](std::uint32_t va, std::uint32_t size) -> std::uint8_t* {
        for (std::size_t k = 0; k < sections.size(); ++k) {
            const auto& range = recipe::kSections[k];
            if (va >= range.va && static_cast<std::uint64_t>(va) + size <= static_cast<std::uint64_t>(range.va) + range.size) {
                return sections[k].data() + (va - range.va);
            }
        }
        fail("the original 2000 recipe writes outside the canonical sections", 2);
    };
    auto source = [&](std::uint32_t va, std::uint32_t size) -> const std::uint8_t* {
        for (const Part& part : sources) {
            if (va >= part.va && static_cast<std::uint64_t>(va) + size <= static_cast<std::uint64_t>(part.va) + part.size) {
                return part.bytes + (va - part.va);
            }
        }
        fail("the original 2000 recipe reads outside THawk2.exe's data", 2);
    };
    auto translate = [](std::uint32_t value) {
        const auto* end = std::end(recipe::kBreakpoints);
        const auto* next = std::upper_bound(std::begin(recipe::kBreakpoints), end, value,
                                            [](std::uint32_t v, const recipe::Breakpoint& b) { return v < b.variant; });
        return next == std::begin(recipe::kBreakpoints) ? value : value + static_cast<std::uint32_t>((next - 1)->delta);
    };
    for (const auto& copy : recipe::kCopies) {
        std::uint8_t* to = target(copy.canonical, copy.size);
        std::memcpy(to, source(copy.variant, copy.size), copy.size);
        for (std::uint32_t o = (4 - copy.variant % 4) % 4; o + 4 <= copy.size; o += 4) {
            std::uint32_t word = 0;
            std::memcpy(&word, to + o, 4);
            if (word >= kImageBase && word < image_end) {
                word = translate(word);
                std::memcpy(to + o, &word, 4);
            }
        }
    }
    for (const auto& zero : recipe::kZeros) {
        if (zero.size != 0) {
            std::memset(target(zero.va, zero.size), 0, zero.size);
        }
    }
    for (const auto& literal : recipe::kLiterals) {
        std::memcpy(target(literal.canonical, literal.size), recipe::kLiteralBytes + literal.offset, literal.size);
    }
    std::vector<Part> parts;
    for (std::size_t k = 0; k < sections.size(); ++k) {
        parts.push_back({recipe::kSections[k].va, sections[k].data(), recipe::kSections[k].size});
    }
    std::vector<std::uint8_t> image = static_image_of(parts);
    if (sha256_hex(image) != recipe::kImageSha256) {
        fail("the data rebuilt from the original 2000 release's THawk2.exe is not the expected image (sha256 " + sha256_hex(image) + ")", 2);
    }
    return image;
}

// The static-data image, byte for byte what tony/static_data.py writes. From the Activision Value re-release: the bytes of every section that is not code. From
// the original 2000 release (its unprotected THawk2.exe): the same image rebuilt by the recipe, with the CD check's key zeroed.
std::vector<std::uint8_t> build_static_image(const fs::path& exe_path) {
    const std::vector<std::uint8_t> exe = read_all(exe_path);
    const std::string found = sha256_hex(exe);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe.data());
    if (exe.size() < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > exe.size()) {
        fail(exe_path.string() + " is not a PE file", 2);
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(exe.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.ImageBase != kImageBase) {
        fail(exe_path.string() + " is not a THawk2.exe of Tony Hawk's Pro Skater 2", 2);
    }
    if (found == opentony::original_2000::kVariantSha256) {
        return rebuild_original_2000(exe, nt);
    }
    if (found == kProtectedDiscSha256) {
        fail(exe_path.string() + " is the copy-protected THawk2.exe of the original 2000 disc (SafeDisc 2: its data is encrypted). Use the original release's "
                                 "unprotected THawk2.exe (sha256 " + std::string(opentony::original_2000::kVariantSha256) +
                 ") or the Activision Value re-release's.",
             2);
    }
    if (found != kCanonicalSha256 || nt->FileHeader.TimeDateStamp != kTimestamp || nt->OptionalHeader.SizeOfImage != kSizeOfImage) {
        fail(exe_path.string() + " is not a build the natives can use: the Activision Value re-release's THawk2.exe (sha256 " + kCanonicalSha256 +
                 ") or the original 2000 release's unprotected one (sha256 " + opentony::original_2000::kVariantSha256 + "); it has sha256 " + found + ".",
             2);
    }
    return static_image_of(data_sections(exe, nt));
}

// The package's files at <directory>\<archive path>, written to a sibling directory first and renamed, so an interrupted run leaves nothing half done.
void extract_package(const fs::path& pkr, const fs::path& directory) {
    std::printf("opentony: unpacking %s (once; a few hundred megabytes) ...\n", pkr.string().c_str());
    std::fflush(stdout);
    opentony::assets::PkrArchive archive = [&] {
        try {
            return opentony::assets::PkrArchive::load(pkr.string());
        } catch (const std::exception& error) {
            fail(pkr.string() + ": " + error.what(), 2);
        }
    }();
    const fs::path temporary = directory.string() + ".part";
    std::error_code ec;
    fs::remove_all(temporary, ec);
    for (std::size_t k = 0; k < archive.entries().size(); ++k) {
        const std::string path = archive.entries()[k].archive_path();
        if (path.find('\\') != std::string::npos || path.find("..") != std::string::npos || (!path.empty() && (path[0] == '/' || path.find(':') != std::string::npos))) {
            fail("unsafe path in the package: " + path, 2);
        }
        const auto payload = archive.stored_payload(k);
        write_all(temporary / path, payload.data(), payload.size());
    }
    fs::remove_all(directory, ec);
    // a virus scanner or an indexer may hold a file of the folder just written for a moment, and Windows then refuses to rename the folder: try again for a while
    for (int attempt = 0; attempt < 40; ++attempt) {
        fs::rename(temporary, directory, ec);
        if (!ec) {
            break;
        }
        Sleep(500);
    }
    if (ec) {
        fail("cannot move " + temporary.string() + " to " + directory.string() + ": " + ec.message());
    }
}

void set_default(const char* name, const std::string& value) {
    if (GetEnvironmentVariableA(name, nullptr, 0) == 0) {
        SetEnvironmentVariableA(name, value.c_str());
    }
}

// The game's own pad bindings leave Pause and Camera unbound (PAUSE=-1, CAMERA=-1 in [JOYPAD]), so a pad could not pause. Once, when the settings file exists and has
// them unbound, Start becomes Pause (button 9) and Back (Select) the camera (8); the buttons are numbered as standalone_platform.cpp's write_joystick does. A later
// choice in the Options menu is kept: [Launcher] PadPauseBound=1 says this was done.
void bind_pad_pause(const fs::path& ini) {
    std::error_code ec;
    if (!fs::is_regular_file(ini, ec)) {
        return;
    }
    const std::string path = ini.string();
    if (GetPrivateProfileIntA("Launcher", "PadPauseBound", 0, path.c_str()) != 0) {
        return;
    }
    const struct { const char* key; int button; } bindings[] = {{"PAUSE", 9}, {"CAMERA", 8}};
    for (const auto& binding : bindings) {
        if (static_cast<int>(GetPrivateProfileIntA("JOYPAD", binding.key, -1, path.c_str())) == -1) {
            WritePrivateProfileStringA("JOYPAD", binding.key, std::to_string(binding.button).c_str(), path.c_str());
        }
    }
    WritePrivateProfileStringA("Launcher", "PadPauseBound", "1", path.c_str());
}

std::string with_separator(fs::path path) {
    std::string text = path.make_preferred().string();
    if (!text.empty() && text.back() != '\\') {
        text += '\\';
    }
    return text;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path game;
    fs::path root;
    bool prepare_only = false;
    bool force = false;
    bool movies = false;
    for (int k = 1; k < argc; ++k) {
        const std::string argument = argv[k];
        if (argument == "--game-dir" && k + 1 < argc) {
            game = argv[++k];
        } else if (argument == "--root" && k + 1 < argc) {
            root = argv[++k];
        } else if (argument == "--prepare") {
            prepare_only = true;
        } else if (argument == "--force") {
            force = true;
        } else if (argument == "--movies") {
            movies = true;
        } else {
            std::printf("usage: opentony [--root INSTALL] [--game-dir DIR] [--prepare] [--force] [--movies]\n"
                        "  INSTALL is the install folder (default: the folder above the Build folder opentony.exe is in).\n"
                        "  DIR holds the Activision Value re-release's THawk2.exe and ALL.PKR (default: INSTALL\\Data\\Game).\n"
                        "  --prepare  only write the static data and unpack the package, do not start the game\n"
                        "  --force    do both again\n"
                        "  --movies   play the startup movies (about two and a half minutes)\n");
            return argument == "--help" || argument == "-h" ? 0 : 64;
        }
    }
    char self[MAX_PATH];
    const DWORD length = GetModuleFileNameA(nullptr, self, sizeof self);
    const fs::path bin = fs::path(std::string(self, length)).parent_path();
    std::error_code ec;
    const opentony::layout::Layout where = root.empty() ? opentony::layout::from_program_directory(bin) : opentony::layout::Layout{fs::absolute(root, ec)};
    if (game.empty()) {
        game = where.game();
    }
    game = fs::absolute(game, ec);
    for (const fs::path& folder : {where.config(), where.saves(), where.mods(), where.logs()}) {
        fs::create_directories(folder, ec);
    }
    const fs::path state = where.unpacked();
    const fs::path image = where.static_image();
    const fs::path data = where.package_data();

    const fs::path pkr = find_in(game, "ALL.PKR");
    const fs::path exe = find_in(game, "THawk2.exe");
    const bool image_ready = fs::is_regular_file(image, ec) && !force && (exe.empty() || read_text(state / "static.stamp") == stamp_of(exe));
    if (!image_ready) {
        if (exe.empty()) {
            fail("THawk2.exe not found in " + game.string() + " (the first run reads the game's static data from it; --game-dir DIR names the install)", 2);
        }
        std::printf("opentony: reading the static data of %s ...\n", exe.string().c_str());
        const std::vector<std::uint8_t> blob = build_static_image(exe);
        write_all(image, blob.data(), blob.size());
        const std::string stamp = stamp_of(exe);
        write_all(state / "static.stamp", stamp.data(), stamp.size());
    }
    if (pkr.empty()) {
        fail("ALL.PKR not found in " + game.string() + " (--game-dir DIR names the install)", 2);
    }
    if (force || !fs::is_directory(data, ec) || read_text(state / "data.stamp") != stamp_of(pkr)) {
        extract_package(pkr, data);
        const std::string stamp = stamp_of(pkr);
        write_all(state / "data.stamp", stamp.data(), stamp.size());
    }
    write_all(state / "empty.tsv", "", 0);
    if (prepare_only) {
        std::printf("opentony: ready (%s)\n", state.string().c_str());
        return 0;
    }

    const fs::path spike = bin / "opentony_spike.exe";
    const fs::path standalone = bin / "opentony_standalone.dll";
    if (!fs::is_regular_file(spike, ec) || !fs::is_regular_file(standalone, ec)) {
        fail("opentony_spike.exe and opentony_standalone.dll must be in " + bin.string(), 2);
    }
    set_default("OPENTONY_GAME_DIR", with_separator(game));
    set_default("OPENTONY_DATA_DIR", fs::path(data).make_preferred().string());
    set_default("OPENTONY_PKR", pkr.string());
    set_default("OPENTONY_MEDIA_DIR", with_separator(game));
    set_default("OPENTONY_FFMPEG_DIR", with_separator(bin));
    // where the game keeps what is the player's: the settings (the launcher's file, which also holds the game's own sections), saves and parks, mods, TH2_OPT.CFG
    // (the standalone redirects a save, a park or the options file it would have written beside the game to these folders: standalone_userdata.hpp)
    set_default("OPENTONY_SETTINGS", where.ini().string());
    set_default("OPENTONY_SAVES_DIR", where.saves().string());
    set_default("OPENTONY_CONFIG_DIR", where.config().string());
    set_default("OPENTONY_MODS", where.mods().string());
    // debug mode (the launcher's checkbox, OPENTONY_DEBUG): every run is kept and listed, notes.txt gets its lines, and the game dumps its textures and logs the pack census.
    // Off, only the active session is logged: its folder, the run before it, and the newest few crashed ones (their zips are made by the launcher)
    const char* debug_env = std::getenv("OPENTONY_DEBUG");
    const bool debug = debug_env != nullptr && *debug_env != '\0' && std::string(debug_env) != "0";
    if (debug) {
        set_default("OT_TEXTURE_DUMP", fs::path(where.texture_dump()).make_preferred().string());   // every texture the game draws, <key>.png + index.tsv + seen.tsv (the upscaler reads it)
        set_default("OT_TEXTURE_PACK_LOG", "1");   // run.txt: which textures a pack replaced and which it missed
    }
    // every run gets its own folder Logs/runs/<date_time> (run.txt, and whatever else the game writes: opentony.log, session.trace, dumps)
    const ULONGLONG began = GetTickCount64();
    SYSTEMTIME started;
    GetLocalTime(&started);
    char run_name[64];
    std::snprintf(run_name, sizeof run_name, "%04d-%02d-%02d_%02d-%02d-%02d", started.wYear, started.wMonth, started.wDay, started.wHour, started.wMinute, started.wSecond);
    const fs::path runs = where.runs();
    const fs::path run_dir = runs / run_name;
    fs::create_directories(run_dir, ec);
    set_default("OPENTONY_LOG_DIR", fs::path(run_dir).make_preferred().string());
    // Logs\notes.txt: the launcher adds a line when a run starts and when it ends, so what the player types under it is tied to that run's folder
    const fs::path notes = where.notes();
    const auto note = [&notes, debug](const std::string& text) {
        if (debug) {
            std::ofstream(notes, std::ios::app) << text << '\n';
        }
    };
    if (debug && !fs::exists(notes, ec)) {
        note("Type what you did and saw under the latest line (any time, even while the game runs).\nThe launcher adds the run lines; AI reads this together with Logs\\sessions.txt.\n");
    }
    note(std::string("[") + run_name + "] run started");
    set_default("OPENTONY_INPUT_LOG", (run_dir / "input.txt").string());   // every key, pad button and window event with its time (docs/PLAYING.md)
    // the heartbeat: the game counts its event pumps in this mapping, and the loop below notices when the count stops (a hang)
    const std::string heartbeat_name = "OpenTonyHeartbeat-" + std::to_string(GetCurrentProcessId());
    HANDLE heartbeat_map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 8, heartbeat_name.c_str());
    volatile LONG* heartbeat = heartbeat_map != nullptr ? static_cast<volatile LONG*>(MapViewOfFile(heartbeat_map, FILE_MAP_ALL_ACCESS, 0, 0, 8)) : nullptr;
    if (heartbeat != nullptr) {
        SetEnvironmentVariableA("OT_HEARTBEAT", heartbeat_name.c_str());
    }
    set_default("OT_VIDEO_SDL", with_separator(bin));
    set_default("OT_VIDEO_SHOW", "1");
    set_default("OT_WINDOW_SCALE", "auto");   // the largest whole multiple of 640 x 480 the desktop holds (4x on a 4K screen); set a number to choose
    set_default("OT_PACING", "realtime");
    set_default("OT_SPIKE_CALL", "boot");
    set_default("OPENTONY_PLAY_MUSIC", "1");   // the skip below is for the movies only: the music files still play
    if (!movies) {
        set_default("OPENTONY_SESSION_SKIP_MOVIES", "1");   // the startup movies return at once until the first level frame
    }
    set_default("OT_DISABLE_CONTROLS_MENU", "1");   // the launcher has the controls; the game's Player and Display controls screens are greyed
    bind_pad_pause(where.ini());

    std::string command = "\"" + spike.string() + "\" \"" + image.string() + "\" \"" + standalone.string() + "\" - \"" + (state / "empty.tsv").string() + "\"";
    // the game's output goes to opentony\log\run.txt (a player's window closes with the program); a stop is then reported in a message box
    const fs::path run_log = run_dir / "run.txt";
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, nullptr, TRUE};
    HANDLE log = CreateFileW(run_log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOA startup = {};
    startup.cb = sizeof startup;
    if (log != INVALID_HANDLE_VALUE) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = log;
        startup.hStdError = log;
    }
    PROCESS_INFORMATION process = {};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, game.string().c_str(), &startup, &process)) {
        fail("cannot start " + spike.string() + " (error " + std::to_string(GetLastError()) + ")");
    }
    // a hang (the window answers but no frame is pumped for kHangSeconds) leaves watchdog.txt and a minidump (hang.dmp: every thread's stack) in the run folder;
    // the game is left running, since a long load can recover
    constexpr ULONGLONG kHangSeconds = 60;
    bool hang_suspected = false;
    LONG last_beat = heartbeat != nullptr ? *heartbeat : 0;
    ULONGLONG last_beat_at = GetTickCount64();
    while (WaitForSingleObject(process.hProcess, 1000) == WAIT_TIMEOUT) {
        if (heartbeat == nullptr) {
            continue;
        }
        const LONG beat = *heartbeat;
        if (beat != last_beat) {
            last_beat = beat;
            last_beat_at = GetTickCount64();
        } else if (!hang_suspected && GetTickCount64() - last_beat_at > kHangSeconds * 1000) {
            hang_suspected = true;
            std::ofstream(run_dir / "watchdog.txt") << "HANG SUSPECTED: the game pumped no events for " << kHangSeconds << " s, " << (GetTickCount64() - began) / 1000
                << " s after it started (pump count " << beat << "). hang.dmp holds every thread's stack.\n";
            if (HMODULE dbghelp = LoadLibraryA("dbghelp.dll")) {
                using Writer = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
                if (auto write = reinterpret_cast<Writer>(GetProcAddress(dbghelp, "MiniDumpWriteDump"))) {
                    HANDLE file = CreateFileW((run_dir / "hang.dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (file != INVALID_HANDLE_VALUE) {
                        write(process.hProcess, process.dwProcessId, file, 0x1 | 0x40 | 0x1000, nullptr, nullptr, nullptr);   // with data segments, indirectly referenced memory, thread info
                        CloseHandle(file);
                    }
                }
            }
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (log != INVALID_HANDLE_VALUE) {
        CloseHandle(log);
    }
    const bool crashed = code != 0;
    // the reason: the program's own FATAL line, else the last line that is not a log-format note
    std::string reason;
    {
        std::ifstream in(run_log);
        for (std::string line; std::getline(in, line);) {
            // CRASH and HANG: lines start with them (standalone_spike.cpp); anywhere else they are a song's file name or a game's text ("HALFPIPE HANGTIME")
            if (line.find("FATAL") != std::string::npos || line.rfind("CRASH ", 0) == 0 || line.rfind("HANG:", 0) == 0) {
                reason = line;
                break;
            }
            if (!line.empty() && line.find("log format") == std::string::npos) {
                reason = line;
            }
        }
    }
    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "%04d-%02d-%02d %02d:%02d:%02d", started.wYear, started.wMonth, started.wDay, started.wHour, started.wMinute, started.wSecond);
    char exit_hex[32];
    std::snprintf(exit_hex, sizeof exit_hex, "%08X", static_cast<unsigned>(code));
    // the run folder is renamed to say how the run ended, so a listing of Logs\runs is already the index
    const std::string final_name = std::string(run_name) + (crashed ? std::string("_CRASH-") + exit_hex : hang_suspected ? std::string("_HANG-SUSPECTED-then-quit") : std::string("_NORMAL-QUIT"));
    fs::path final_dir = runs / final_name;
    fs::rename(run_dir, final_dir, ec);
    if (ec) {
        final_dir = run_dir;
    }
    const fs::path final_log = final_dir / "run.txt";
    // which build ran: the date and size of the three program files (a stale test folder shows up here)
    std::string build_stamp;
    {
        std::ofstream build_file(final_dir / "build.txt");
        for (const fs::path& file : {bin / "opentony.exe", spike, standalone}) {
            WIN32_FILE_ATTRIBUTE_DATA info = {};
            SYSTEMTIME when = {};
            if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info)) {
                FILETIME local;
                FileTimeToLocalFileTime(&info.ftLastWriteTime, &local);
                FileTimeToSystemTime(&local, &when);
            }
            char line[512];
            std::snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d  %llu bytes  %s\n", when.wYear, when.wMonth, when.wDay, when.wHour, when.wMinute,
                (static_cast<unsigned long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow, file.filename().string().c_str());
            build_file << line;
            if (file == standalone) {
                build_stamp = std::string(line, 16);
            }
        }
    }
    const unsigned long long seconds = (GetTickCount64() - began) / 1000;
    const std::string verdict = crashed ? "CRASH / abnormal stop, exit code 0x" + std::string(exit_hex)
        : hang_suspected ? "HANG SUSPECTED (no frame for 60 s; see watchdog.txt, hang.dmp), then quit with exit code 0" : "NORMAL QUIT (exit code 0, not a crash)";
    const std::string summary = std::string(stamp) + " | " + verdict + " | played " + std::to_string(seconds) + "s | folder runs\\" + final_dir.filename().string() + " | build " + build_stamp + " | " + (crashed ? reason.substr(0, 300) : "no error");
    note(std::string("[") + run_name + "] run ended: " + verdict + ", played " + std::to_string(seconds) + "s -> Logs\\runs\\" + final_dir.filename().string() + "\n");
    // result.txt in the folder, and one line per run in sessions.txt (never overwritten)
    std::ofstream(final_dir / "result.txt") << summary << '\n';
    std::ofstream(where.sessions(), debug ? std::ios::app : std::ios::trunc) << summary << '\n';   // off: just this session's line, which is what the launcher reads
    if (crashed && debug) {
        // crashes.txt is never overwritten either: the full output of every stopped run, appended
        std::ofstream history(where.crashes(), std::ios::app);
        history << "==== " << stamp << "  exit code " << static_cast<std::uint32_t>(code) << "  folder runs\\" << final_dir.filename().string() << " ====\n";
        std::ifstream whole(final_log);
        for (std::string line; std::getline(whole, line);) {
            if (line.find("(log format at") == std::string::npos) {
                history << line << '\n';
            }
        }
        history << '\n';
    }
    {   // debug: keep the newest 200 crashed folders and the newest 100 normal ones (OPENTONY_KEEP_LOGS=0 keeps no normal ones); off: the newest 3 crashed and this session
        std::vector<fs::path> crash_dirs, normal_dirs;
        for (const auto& entry : fs::directory_iterator(runs, ec)) {
            (entry.path().filename().string().find("_NORMAL-QUIT") == std::string::npos ? crash_dirs : normal_dirs).push_back(entry.path());
        }
        std::sort(crash_dirs.begin(), crash_dirs.end());
        std::sort(normal_dirs.begin(), normal_dirs.end());
        const char* keep_env = std::getenv("OPENTONY_KEEP_LOGS");
        const size_t keep_normal = !debug ? 2 : (keep_env != nullptr && std::string(keep_env) == "0") ? 0 : 100;   // off: this session and the one before it (docs/LAUNCHER.md)
        const size_t keep_crashed = debug ? 200 : 3;
        for (size_t i = 0; i + keep_crashed < crash_dirs.size(); ++i) {
            fs::remove_all(crash_dirs[i], ec);
        }
        for (size_t i = 0; i + keep_normal < normal_dirs.size(); ++i) {
            fs::remove_all(normal_dirs[i], ec);
        }
    }
    if (crashed) {
        const std::string text = "THPSource2 stopped (exit code " + std::to_string(code) + ").\n\n" + reason.substr(0, 400) + "\n\nThe full output is in:\n" + final_log.string() + "\n\nEvery run is listed in:\n" + where.sessions().string();
        std::fprintf(stderr, "opentony: %s\n", text.c_str());
        MessageBoxA(nullptr, text.c_str(), "THPSource2", MB_OK | MB_ICONERROR);
    }
    return static_cast<int>(code);
}
