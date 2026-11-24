// The file backend of a standalone program (Phase 5.4a, 7b): FileIO_BackendOpen / Seek / Read / Close, the four functions the file layer
// (src/runtime/resource_kit.*) reaches for the bytes of a file. Retail opens a resource in ALL.PKR through PKR2 or reads a loose file through
// the C runtime into a ten-slot table (0x006a6d10..0x006a7194) that nothing else reads, so the table is host state here.
//
// Where the bytes come from: a file of a mod (the loose-file overlay of 5.2, in front), else the directory OPENTONY_DATA_DIR (the extracted contents of ALL.PKR,
// `tony assets`), a loose file below it. The package itself is read through the stream seams of standalone_host.cpp and upstream zlib (8c).
#include "hook_runtime.h"
#include "kit_bridge.hpp"
#include "mod_overlay.h"
#include "standalone_options.hpp"
#include "standalone_userdata.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include "win32_posix.h"
#endif

#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "guest_arena.hpp"

namespace opentony::kitdll {
namespace {

using Args = std::vector<std::uint32_t>;

constexpr std::size_t kSlots = 10;  // FileIO_FindFreeBackendSlot scans ten
constexpr std::uint32_t kFailed = 0xffffffffu;

constexpr std::uint32_t kBackendOpen = 0x004e75b0;
constexpr std::uint32_t kBackendSeek = 0x004e79f0;
constexpr std::uint32_t kBackendRead = 0x004e7ad0;
constexpr std::uint32_t kBackendClose = 0x004e7bc0;
constexpr std::uint32_t kPCremove = 0x004e7500;   // remove(install folder + name)
constexpr std::uint32_t kPCcreate = 0x004e7900;   // fopen(install folder + name, "a+b"): a slot
constexpr std::uint32_t kPCwrite = 0x004e7b60;    // fwrite(data, 1, size) to a slot
constexpr std::uint32_t kInstallDir = 0x006a71bc; // the folder the game was started from (a string)

struct Slot {
    bool used = false;
    std::string name;
    std::vector<std::uint8_t> bytes;
    std::int64_t position = 0;
    std::FILE* file = nullptr;   // a save or a custom park: a real stream, as the retail slots of state 2 are (modes 1 and 2 of the open, PCcreate)
};
Slot g_slots[kSlots];

bool log_files() {
    static const bool on = std::getenv("OT_FILES_LOG") != nullptr;
    return on;
}

std::string data_root() {
    const char* root = std::getenv("OPENTONY_DATA_DIR");
    return root != nullptr && *root != '\0' ? root : ".";
}

const char* text_at(std::uint32_t address) { return arena::host<const char>(address); }

// The loose-file overlay (src/hooks/mod_overlay.cpp, the code of the hooked build): mods are looked for as docs/HOOKS.md says (OPENTONY_MODS, [mods] path in opentony.ini,
// <game dir>\Mods), the game directory being OPENTONY_GAME_DIR or the working directory. Configured at the first open.
void configure_overlay() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    std::string directory = std::getenv("OPENTONY_GAME_DIR") != nullptr && *std::getenv("OPENTONY_GAME_DIR") != '\0' ? std::getenv("OPENTONY_GAME_DIR") : ".";
    if (directory.back() != '\\' && directory.back() != '/') {
        directory += '\\';
    }
    const std::string ini = directory + "opentony.ini";
    ot_overlay_configure(directory.c_str(), GetFileAttributesA(ini.c_str()) != INVALID_FILE_ATTRIBUTES ? ini.c_str() : nullptr);
}

bool valid(std::uint32_t handle) { return handle < kSlots && g_slots[handle].used; }

std::size_t free_slot_index() {
    std::size_t slot = 0;
    while (slot < kSlots && g_slots[slot].used) {
        ++slot;
    }
    return slot;
}

// a name the game hands the file calls of the memory card: below the folder it was started from (retail builds install folder + "\\" + name), slashes the host's
std::string install_path(const char* name) {
    std::string path = name;
    const bool absolute = path.find(':') != std::string::npos || (!path.empty() && (path[0] == '/' || path[0] == '\\'));
    if (!absolute) {
        path = std::string(text_at(kInstallDir)) + "/" + path;
    }
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    return user_path(path);   // a save, a park or TH2_OPT.CFG of the working directory lives in Saves or Config (standalone_userdata.hpp)
}

std::uint32_t open_stream(const std::string& path, const char* mode) {
    const std::size_t index = free_slot_index();
    if (index == kSlots) {
        return kFailed;
    }
    std::FILE* file = host_fopen(path, mode);
    if (log_files()) {
        std::fprintf(stderr, "files: %s %s (%s) -> %s\n", mode[0] == 'r' && mode[1] != '+' ? "read" : "write", path.c_str(), mode, file != nullptr ? "ok" : "FAILED");
    }
    if (file == nullptr) {
        return kFailed;
    }
    Slot& slot = g_slots[index];
    slot = Slot{};
    slot.used = true;
    slot.name = path;
    slot.file = file;
    return static_cast<std::uint32_t>(index);
}

// (path, mode, ...): the handle, or -1 with no free slot or no such file
std::uint32_t backend_open(const Args& args) {
    if (args.empty()) {
        return kFailed;
    }
    // modes 1 and 2 are the memory card's and the custom parks' (retail: fopen of the install folder and the name, "a+b" and "rb"); mode 0 is a resource
    if (args.size() >= 2 && (args[1] == 1 || args[1] == 2)) {
        const std::string path = install_path(text_at(args[0]));
        return open_stream(path, args[1] == 1 && !user_file_locked(path) ? "a+b" : "rb");   // a quick start reads a save, never creates or updates one
    }
    std::size_t free_slot = free_slot_index();
    if (free_slot == kSlots) {
        return kFailed;
    }
    std::string path = text_at(args[0]);
    if (path.size() >= 2 && path[1] == ':') {
        path.erase(0, 2);  // the drive of _splitpath
    }
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    // The player's "PSX textures" option: the PC port's replacement bitmaps (NEWTEX\<level>\<checksum>.BMP, NEWTEX\<checksum>.BMP) are not there, so every texture
    // is the one of the level's own PlayStation files. (The game's own switch, D3DConfiguredTextureRootEnabled, is passed over while a level loads: the loads run in
    // texture mode 1, D3DTex_OpenImage 0x004d8df0.)
    if (game_options().psx_textures && path.size() > 7 && _strnicmp(path.c_str() + (path[0] == '/' ? 1 : 0), "NEWTEX/", 7) == 0) {
        if (log_files()) {
            std::fprintf(stderr, "files: open %s -> left out (PSX textures)\n", path.c_str());
        }
        return kFailed;
    }
    // the mod overlay of 5.2 answers before the data does (docs/HOOKS.md, "Mods"): the same keys, the same folders, the same trace as the hooked build
    configure_overlay();
    char key[OT_OVERLAY_KEY_MAX];
    const OtOverlayEntry* replacement = ot_overlay_lookup(path.c_str(), key, sizeof key);
    std::string full = data_root() + "/" + path;
    std::FILE* file = nullptr;
    if (replacement != nullptr) {
        file = host_fopen(replacement->path, "rb");
        if (file != nullptr) {
            full = replacement->path;
            if (ot_overlay_trace() != 0) {
                ot_log("mods: replaced %s <- %s", key, replacement->path);
            }
        } else {
            ot_log("mods: cannot read %s for %s; the game's own file is used", replacement->path, key);
        }
    } else if (ot_overlay_trace() >= 2 && key[0] != 0) {
        ot_log("mods: open %s (not replaced)", key);
    }
    if (file == nullptr) {
        file = host_fopen(full, "rb");
    }
    if (log_files()) {
        // (errno 22 with a name that looks right is a control byte in it: the log prints it as nothing)
        std::fprintf(stderr, "files: open %s -> %s (errno %d)\n", full.c_str(), file != nullptr ? "ok" : "NOT FOUND", file != nullptr ? 0 : errno);
    }
    if (file == nullptr) {
        return kFailed;
    }
    Slot& slot = g_slots[free_slot];
    slot = Slot{};
    std::fseek(file, 0, SEEK_END);
    slot.bytes.resize(static_cast<std::size_t>(std::ftell(file)));
    std::fseek(file, 0, SEEK_SET);
    if (!slot.bytes.empty() && std::fread(slot.bytes.data(), 1, slot.bytes.size(), file) != slot.bytes.size()) {
        std::fclose(file);
        return kFailed;
    }
    std::fclose(file);
    slot.used = true;
    slot.name = path;
    return static_cast<std::uint32_t>(free_slot);
}

// (handle, offset, origin): the new position. Origin 2 counts from the end of the file, as a stream's seek does.
std::uint32_t backend_seek(const Args& args) {
    if (args.size() < 3 || !valid(args[0])) {
        return kFailed;
    }
    Slot& slot = g_slots[args[0]];
    if (slot.file != nullptr) {
        // (the stream's own seek; a stream opened to append still writes at its end)
        if (std::fseek(slot.file, static_cast<long>(static_cast<std::int32_t>(args[1])), static_cast<int>(args[2])) != 0) {
            return kFailed;
        }
        return static_cast<std::uint32_t>(std::ftell(slot.file));
    }
    const std::int64_t offset = static_cast<std::int32_t>(args[1]);
    switch (args[2]) {
    case 0:
        slot.position = offset;
        break;
    case 1:
        slot.position += offset;
        break;
    case 2:
        slot.position = static_cast<std::int64_t>(slot.bytes.size()) + offset;
        break;
    default:
        break;
    }
    return static_cast<std::uint32_t>(slot.position);
}

// (handle, buffer, size): `size` is what retail answers (and advances by), whatever is left of the file
std::uint32_t backend_read(const Args& args) {
    if (args.size() < 3 || !valid(args[0])) {
        return kFailed;
    }
    Slot& slot = g_slots[args[0]];
    if (slot.file != nullptr) {
        return static_cast<std::uint32_t>(std::fread(arena::host<void>(args[1]), 1, args[2], slot.file));
    }
    const std::size_t available = slot.position >= 0 && static_cast<std::size_t>(slot.position) < slot.bytes.size()
                                      ? slot.bytes.size() - static_cast<std::size_t>(slot.position)
                                      : 0;
    const std::size_t count = args[2] < available ? args[2] : available;
    if (count != 0) {
        std::memcpy(arena::host<void>(args[1]), slot.bytes.data() + slot.position, count);
    }
    slot.position += args[2];
    return args[2];
}

std::uint32_t backend_close(const Args& args) {
    if (args.empty() || !valid(args[0])) {
        return kFailed;
    }
    if (g_slots[args[0]].file != nullptr) {
        std::fclose(g_slots[args[0]].file);
        if (log_files()) {
            std::fprintf(stderr, "files: close %s\n", g_slots[args[0]].name.c_str());
        }
    }
    g_slots[args[0]] = Slot{};
    return 0;
}

// PCcreate(name): a new file opened to write ("a+b"): its slot, or -1
std::uint32_t backend_create(const Args& args) {
    if (args.empty()) {
        return kFailed;
    }
    const std::string path = install_path(text_at(args[0]));
    return user_file_locked(path) ? kFailed : open_stream(path, "a+b");
}

// PCwrite(handle, data, size): the bytes written, or -1 for a handle that is not an open file; the stream is flushed at once (the program may end without closing it)
std::uint32_t backend_write(const Args& args) {
    if (args.size() < 3 || !valid(args[0]) || g_slots[args[0]].file == nullptr || user_file_locked(g_slots[args[0]].name)) {
        return kFailed;
    }
    const std::size_t written = std::fwrite(arena::host<const void>(args[1]), 1, args[2], g_slots[args[0]].file);
    std::fflush(g_slots[args[0]].file);
    if (log_files()) {
        std::fprintf(stderr, "files: write %u of %u bytes\n", static_cast<unsigned>(written), static_cast<unsigned>(args[2]));
    }
    return static_cast<std::uint32_t>(written);
}

// PCremove(name): the file deleted (0, or -1 when it was not there)
std::uint32_t backend_remove(const Args& args) {
    if (args.empty()) {
        return kFailed;
    }
    const std::string path = install_path(text_at(args[0]));
    if (user_file_locked(path)) {
        return kFailed;
    }
    const bool removed = std::remove(path.c_str()) == 0;
    if (log_files()) {
        std::fprintf(stderr, "files: remove %s -> %s\n", path.c_str(), removed ? "ok" : "NOT FOUND");
    }
    return removed ? 0u : kFailed;
}

}  // namespace

bool has_file_seam(std::uint32_t address) {
    return address == kBackendOpen || address == kBackendSeek || address == kBackendRead || address == kBackendClose || address == kPCremove || address == kPCcreate ||
           address == kPCwrite;
}

bool call_file_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string&, std::uint32_t& result) {
    switch (address) {
    case kBackendOpen:
        result = backend_open(args);
        return true;
    case kBackendSeek:
        result = backend_seek(args);
        return true;
    case kBackendRead:
        result = backend_read(args);
        return true;
    case kBackendClose:
        result = backend_close(args);
        return true;
    case kPCcreate:
        result = backend_create(args);
        return true;
    case kPCwrite:
        result = backend_write(args);
        return true;
    case kPCremove:
        result = backend_remove(args);
        return true;
    default:
        return false;
    }
}

}  // namespace opentony::kitdll
