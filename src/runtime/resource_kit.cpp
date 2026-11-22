#include "auto_views.hpp"
#include "resource_kit.hpp"
#include "seam_addresses_generated.hpp"

#include <cstdio>
#include <utility>

namespace opentony::runtime::resourcekit {

using namespace opentony::runtime::autoviews;

namespace {

// the retail functions this layer reaches (reported by address; the resolver answers with the native or the seam)
constexpr std::uint32_t kNew = 0x00466b70;                  // CClass_OpNew(size)
constexpr std::uint32_t kLoadHed = 0x00448c60;              // FileIO_LoadHed()
constexpr std::uint32_t kContinue = 0x004494b0;             // FileIO_Continue()
constexpr std::uint32_t kProgress = 0x0045d690;             // LoadingScreen_UpdateProgress()
constexpr std::uint32_t kAlloc = 0x0046f490;                // FileIO_AllocLoadedBuffer(size, heap, 1, 0)
constexpr std::uint32_t kMemCopy = 0x0046f2e0;              // Mem_Copy(destination, source, size)
constexpr std::uint32_t kMemDelete = 0x0046f4d0;            // Mem_Delete(block)
constexpr std::uint32_t kStopMovie = 0x004e7140;            // PCMOVIE_StopFMV()
constexpr std::uint32_t kBackendOpen = 0x004e75b0;          // FileIO_BackendOpen(path, mode, 0): a handle
constexpr std::uint32_t kBackendSeek = 0x004e79f0;          // FileIO_BackendSeek(handle, offset, origin)
constexpr std::uint32_t kBackendClose = 0x004e7bc0;         // FileIO_BackendClose(handle)
constexpr std::uint32_t kSetMapping = 0x0044abb0;           // Font_SetMapping(font, mapping)
constexpr std::uint32_t kFreeGlyphs = 0x0044ad30;           // Font_FreeGlyphs(font)
constexpr std::uint32_t kFreeSlot = 0x0048a490;             // GetFree16Slot()
constexpr std::uint32_t kPaletteStub = 0x004e8770;          // PsxStub_ReturnZero (the palette upload of the PlayStation build)
constexpr std::uint32_t kConvertPalette = 0x004da780;       // Palette_ConvertInPlace(palette, count)
constexpr std::uint32_t kCreateRecord = 0x004d98d0;         // D3DTex_CreateExternalRecord(pixels, width, height, palette, count, 0)
constexpr std::uint32_t kConstructImage = 0x00456d10;       // FontGlyph_ConstructImage(image, pixels, width, height, 0, 0, 4, palette id)
constexpr std::uint32_t kPcInit = seam::hal::PCinit;        // PCinit(): 0 when the install and the disc were found
constexpr std::uint32_t kSetUnusedFlag = 0x00448f40;        // FileIO_SetUnusedFlag(value)

constexpr std::uint32_t kLoadingFlag = 0x005615d0;          // FileIO_Init clears it
constexpr std::uint32_t kConfiguredPath = 0x0052b9e8;       // the configured path: its directory part ends at a '?'

// assertion messages (the format strings the retail code names)
constexpr std::uint32_t kMsgCantOpen = 0x0052bb4c;          // "Can't call FileIO_Open for %s, %s ..."
constexpr std::uint32_t kMsgSeekFailed = 0x0052bb0c;        // "PClseek failed for %s, size %d, e..."
constexpr std::uint32_t kMsgNoFileOpen = 0x0052bc20;        // "FileIO_Load called without a file open"
constexpr std::uint32_t kMsgUnaligned = 0x0052bbf4;         // "Destination pointer not lword aligned"
constexpr std::uint32_t kMsgSpoolerLoad = 0x0052bbc4;       // "Can't call FileIO_Load, spooler is ..."
constexpr std::uint32_t kMsgNewLoad = 0x0052bb8c;           // "Tried to start a new load before ..."
constexpr std::uint32_t kMsgSpoolerSync = 0x0052bc88;       // "Can't call FileIO_Sync, spooler is ..."
constexpr std::uint32_t kMsgSubDirTotal = 0x0052ba04;       // "Too many characters when using sub directory"
constexpr std::uint32_t kMsgSubDirLength = 0x0052bcec;      // "Too many characters in sub directory name"
constexpr std::uint32_t kMsgSubDirEnd = 0x0052bcb4;         // "Last character in subdir name ne ..."
constexpr std::uint32_t kMsgDirTooLong = 0x0052bae8;        // FileIO_Init: the directory and a name do not fit FileName
constexpr std::uint32_t kMsgPcInitFailed = 0x0052bad8;      // FileIO_Init: PCinit failed
constexpr std::uint32_t kMsgPreCreated = 0x00537438;        // "PRE Manager already created"
constexpr std::uint32_t kMsgPreSlots = 0x005374a4;          // "out of PREManager slots"
constexpr std::uint32_t kMsgPreNotLoaded = 0x005374e8;      // "PRE file is not loaded"
constexpr std::uint32_t kMsgFontSize = 0x0052bddc;          // "unlikely font file size"
constexpr std::uint32_t kMsgFontMissing = 0x0052bdcc;       // "File not found"
constexpr std::uint32_t kMsgFontMemory = 0x0052bdbc;        // "Out of memory"
constexpr std::uint32_t kMsgFontSlots = 0x0052bdf4;         // "out of font slots"
constexpr std::uint32_t kMsgFontNotLoaded = 0x0052be08;     // "Font %s is not loaded"

constexpr std::uint32_t kBmpNeedle = 0x0052bb48;            // "bmp": a name that contains it is never taken from a PRE
constexpr std::uint32_t kLogName = 0x0052bb40;              // "--- %s"
constexpr std::uint32_t kLogSize = 0x0052bb38;              // "(%d)"
constexpr std::uint32_t kLogLoad = 0x0052bbf0;              // the format of the load line
constexpr std::uint32_t kLogMemory = 0x00537488;            // "Memory error loading %s"
constexpr std::uint32_t kSlotFormat = 0x005256dc;           // the format that writes a PRE slot's name ("%s")

constexpr std::uint32_t kEmbeddedNameLimit = 0x10;          // PRE_FindEmbeddedResource copies the name into a 16-byte local
constexpr std::uint32_t kFileNameLimit = 0x32;              // FileName has 50 characters
constexpr std::uint32_t kSubDirLimit = 0x10;

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 0x20) : c; }

}  // namespace

void ResourceKit::service(std::uint32_t address, std::vector<std::uint32_t> args) {
    char name[24];
    std::snprintf(name, sizeof name, "engine_%08x", address);
    s_.note(name, std::move(args));
}

std::uint32_t ResourceKit::service_value(std::uint32_t address) {
    char name[24];
    std::snprintf(name, sizeof name, "engine_%08x", address);
    return static_cast<std::uint32_t>(s_.value(name));
}

std::uint32_t ResourceKit::call(std::uint32_t address, std::vector<std::uint32_t> args) {
    service(address, std::move(args));
    return service_value(address);
}

std::uint32_t ResourceKit::virtual_call(std::uint32_t target, std::vector<std::uint32_t> args) {
    char name[24];
    std::snprintf(name, sizeof name, "vcall_%08x", target);
    s_.note(name, std::move(args));
    return static_cast<std::uint32_t>(s_.value(name));
}

void ResourceKit::put_string(std::uint32_t destination, const std::string& text) {
    m_.write(destination, {reinterpret_cast<const std::uint8_t*>(text.c_str()), text.size() + 1});
}

// ---- fileio.cpp -------------------------------------------------------------------------------------------------------------

// 0x004b82b0: the first place the needle occurs in the haystack, ignoring the case of A..Z; 0 for none (and for an empty needle).
std::uint32_t ResourceKit::find_no_case(std::uint32_t haystack, std::uint32_t needle) {
    const std::string hay = m_.cstring(haystack);
    const std::string pin = m_.cstring(needle);
    if (pin.empty()) {
        return 0;
    }
    for (std::size_t at = 0; at < hay.size(); ++at) {
        std::size_t k = 0;
        while (k < pin.size() && at + k < hay.size() && lower(hay[at + k]) == lower(pin[k])) {
            ++k;
        }
        if (k == pin.size()) {
            return haystack + static_cast<std::uint32_t>(at);
        }
    }
    return 0;
}

// 0x00448d00: the name goes after the base directory in FileName.
void ResourceKit::add_dir_to_file_name(std::uint32_t name) {
    put_string(kFileName + global_word(m_, kDirLength), m_.cstring(name));
}

// 0x00448d30: the sub directory and the name go after the base directory (they replace what AddDirToFileName wrote there).
void ResourceKit::add_sub_dir_to_file_name(std::uint32_t name) {
    const std::string sub = m_.cstring(kSubDirName);
    s_.check(sub.size() + 1 + 10 + global_word(m_, kDirLength) < kFileNameLimit, kMsgSubDirTotal);
    put_string(kFileName + global_word(m_, kDirLength), sub + m_.cstring(name));
}

// 0x00449700
void ResourceKit::set_sub_dir(std::uint32_t sub_dir) {
    const std::string text = m_.cstring(sub_dir);
    s_.check(text.size() < kSubDirLimit, kMsgSubDirLength);
    s_.check(text.empty() || text.back() == '\\', kMsgSubDirEnd);
    put_string(kSubDirName, text);
}

// 0x00449030: find the file (a PRE-embedded copy for a short name that is not a bitmap, else the backend), remember it for FileIO_LoadResource and answer its size.
std::uint32_t ResourceKit::open_resource(std::uint32_t name) {
    set_global_word(m_, kReadDone, 0);
    service(kLoadHed);
    const std::string text = m_.cstring(name);
    s_.check(global_byte(m_, kPlayerSpoolBusy) == 0, kMsgCantOpen);
    put_string(kNameCopy, text);
    set_global_word(m_, kLoadFromPre, 0);
    const std::uint32_t manager = global_word(m_, kPreManager);
    if (manager != 0 && text.size() < kEmbeddedNameLimit && find_no_case(name, kBmpNeedle) == 0) {
        const std::uint32_t found = find_embedded(manager, name, kPreSize);
        set_global_word(m_, kPreData, found);
        if (found != 0) {
            set_global_word(m_, kLoadFromPre, 1);
            set_global_word(m_, kReadPending, 1);
            set_global_word(m_, kFileSize, global_word(m_, kPreSize));
            if (global_byte(m_, kVerbose) != 0) {
                service(seam::hal::printf_log, {kLogName, name});
                service(seam::hal::printf_log, {kLogSize, global_word(m_, kFileSize)});
            }
            return global_word(m_, kPreSize);
        }
    }
    set_global_word(m_, kLastName, name);
    add_dir_to_file_name(name);
    add_sub_dir_to_file_name(name);
    const std::uint32_t handle = call(kBackendOpen, {kFileName, 0, 0});
    set_global_half(m_, kReadHandle, handle);
    const std::uint32_t sign_extended = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(handle)));
    const std::uint32_t size = call(kBackendSeek, {sign_extended, 0, 2});
    set_global_word(m_, kReadSize, size);
    set_global_word(m_, kFileSize, size);
    const std::uint32_t rewind = call(kBackendSeek, {sign_extended, 0, 0});
    s_.check(rewind == 0, kMsgSeekFailed);
    set_global_word(m_, kReadPending, 1);
    return size;
}

// 0x00449230: start reading the opened file into `destination` (a copy at once for an embedded one).
void ResourceKit::load_resource(std::uint32_t destination) {
    const std::uint32_t pending = global_word(m_, kReadPending);
    s_.check(pending != 0, kMsgNoFileOpen);
    s_.check((destination & 3) == 0, kMsgUnaligned);
    if (global_word(m_, kPreManager) != 0 && global_word(m_, kLoadFromPre) != 0) {
        service(kMemCopy, {destination, global_word(m_, kPreData), global_word(m_, kPreSize)});
        set_global_word(m_, kReadPending, 0);
        return;
    }
    if (global_byte(m_, kVerbose) != 0) {
        service(seam::hal::printf_log, {kLogLoad, global_word(m_, kLastName)});
        service(seam::hal::printf_log, {kLogSize, global_word(m_, kReadSize)});
    }
    s_.check(global_byte(m_, kPlayerSpoolBusy) == 0, kMsgSpoolerLoad);
    service(kStopMovie);
    s_.check(global_word(m_, kLoading) == 0, kMsgNewLoad);
    set_global_word(m_, kReadBuffer, destination);
    set_global_word(m_, kLoading, 4);
    set_global_word(m_, kReadMark, 1);
    set_global_word(m_, kReadDone, 0);
    service(kContinue);
}

// 0x00449660: finish the load that was started.
void ResourceKit::sync_resource() {
    if (global_word(m_, kLoadFromPre) != 0) {
        set_global_word(m_, kLoading, 0);
        set_global_word(m_, kLoadFromPre, 0);
        return;
    }
    s_.check(global_byte(m_, kPlayerSpoolBusy) == 0, kMsgSpoolerSync);
    while (global_word(m_, kLoading) != 0) {
        service(kContinue);
        service(kProgress);
    }
    service(kContinue);
}

// ---- PRE.cpp ----------------------------------------------------------------------------------------------------------------

// 0x004a9000: the manager's sixteen buffers are empty and their names are the blank name.
std::uint32_t ResourceKit::initialize_manager(std::uint32_t self) {
    set_word_at(m_, self, kPreVtable);
    const std::string blank = m_.cstring(kPreSlotName);
    for (std::uint32_t slot = 0; slot < kPreSlots; ++slot) {
        set_word_at(m_, self + 4 + 4 * slot, 0);
        put_string(self + 0x44 + 0x10 * slot, blank);
    }
    return self;
}

// 0x004a8ee0
void ResourceKit::create_manager() {
    s_.check(global_word(m_, kPreManager) == 0, kMsgPreCreated);
    const std::uint32_t object = call(kNew, {0x144});
    set_global_word(m_, kPreManager, object != 0 ? initialize_manager(object) : 0);
}

// The records of a PRE buffer: a count, then for each a NUL-terminated name padded to four bytes, a size word and the payload padded to four bytes.
std::uint32_t ResourceKit::find_entry_text(std::uint32_t buffer, const std::string& name, std::uint32_t size_out) {
    const std::int32_t count = int_at(m_, buffer);
    std::uint32_t at = buffer + 4;
    for (std::int32_t entry = 0; entry < count; ++entry) {
        const std::string found = m_.cstring(at);
        const std::uint32_t data = at + ((static_cast<std::uint32_t>(found.size()) + 1 + 3) & 0xfffffffcu);
        if (found == name) {
            set_word_at(m_, size_out, word_at(m_, data));
            return data + 4;
        }
        at = data + 4 + ((word_at(m_, data) + 3) & 0xfffffffcu);
    }
    return 0;
}

// 0x004a9480
std::uint32_t ResourceKit::find_entry_in_buffer(std::uint32_t buffer, std::uint32_t name, std::uint32_t size_out) {
    return find_entry_text(buffer, m_.cstring(name), size_out);
}

// 0x004a9410: the name, lower-cased, in the sixteen buffers in turn.
std::uint32_t ResourceKit::find_embedded(std::uint32_t self, std::uint32_t name, std::uint32_t size_out) {
    std::string folded = m_.cstring(name);
    for (char& c : folded) {
        c = lower(c);
    }
    for (std::uint32_t slot = 0; slot < kPreSlots; ++slot) {
        const std::uint32_t buffer = word_at(m_, self + 4 + 4 * slot);
        if (buffer != 0) {
            const std::uint32_t found = find_entry_text(buffer, folded, size_out);
            if (found != 0) {
                return found;
            }
        }
    }
    return 0;
}

// 0x004a9110: load a file into the first free slot and name the slot after it.
void ResourceKit::load_into_slot(std::uint32_t self, std::uint32_t name, std::uint32_t heap) {
    std::uint32_t slot = 0;
    while (slot < kPreSlots && word_at(m_, self + 4 + 4 * slot) != 0) {
        ++slot;
    }
    s_.check(slot < kPreSlots, kMsgPreSlots);
    const std::uint32_t size = open_resource(name);
    const std::uint32_t buffer = call(kAlloc, {size, heap, 1, 0});
    set_word_at(m_, self + 4 + 4 * slot, buffer);
    if (buffer == 0) {
        service(seam::hal::printf_log, {kLogMemory, name});
    } else {
        load_resource(buffer);
    }
    service(seam::crt::CRT_sprintf, {self + 0x44 + 0x10 * slot, kSlotFormat, name});
}

// 0x004a90f0
void ResourceKit::load(std::uint32_t self, std::uint32_t name, std::uint32_t heap) {
    load_into_slot(self, name, heap);
    sync_resource();
}

// 0x00449460 (from the disassembly): the name with the base and sub directory opened and closed again through the backend; 1 when it opened
// 0x00448f50 (from the disassembly; milestone 9's cold boot is its first caller here): the loading flag (0x005615d0) cleared, the configured path's
// directory part (the text 0x0052b9e8 points at, up to its '?') copied to the start of FileName with its length in kDirLength (no terminator: the
// name is appended at that length), an assertion that the length and an 8.3 name fit, PCinit (which must answer 0, an assertion), and
// FileIO_SetUnusedFlag(4).
void ResourceKit::file_init() {
    set_global_word(m_, kLoadingFlag, 0);
    set_global_word(m_, kDirLength, 0);
    const std::uint32_t path = global_word(m_, kConfiguredPath);
    std::uint32_t length = 0;
    while (byte_at(m_, path + length) != '?') {
        set_byte_at(m_, kFileName + length, byte_at(m_, path + length));
        set_global_word(m_, kDirLength, ++length);
    }
    s_.check(length + 11 < kFileNameLimit, kMsgDirTooLong);
    s_.check(call(kPcInit, {}) == 0, kMsgPcInitFailed);
    service(kSetUnusedFlag, {4});
}

std::uint32_t ResourceKit::file_exists(std::uint32_t name) {
    add_dir_to_file_name(name);
    if (global_byte(m_, kSubDirName) != 0) {
        add_sub_dir_to_file_name(name);
    }
    const std::uint32_t handle = call(kBackendOpen, {kFileName, 0, 0});
    if (handle != 0xffffffffu) {
        service(kBackendClose, {handle});
    }
    return handle != 0xffffffffu ? 1 : 0;
}

// 0x004a9330 (from the disassembly): the first slot whose name equals the given one (compared as strcmp does) gets the blank name back and its buffer freed; a name that
// is not loaded asserts.
void ResourceKit::unload(std::uint32_t self, std::uint32_t name) {
    const std::string wanted = m_.cstring(name);
    std::uint32_t slot = 0;
    while (slot < kPreSlots && m_.cstring(self + 0x44 + 0x10 * slot) != wanted) {
        ++slot;
    }
    if (slot < kPreSlots) {
        put_string(self + 0x44 + 0x10 * slot, m_.cstring(kPreSlotName));
        const std::uint32_t buffer = word_at(m_, self + 4 + 4 * slot);
        if (buffer != 0) {
            service(kMemDelete, {buffer});
        }
        set_word_at(m_, self + 4 + 4 * slot, 0);
    }
    s_.check(slot < kPreSlots, kMsgPreNotLoaded);
}

// ---- FontTools.cpp ----------------------------------------------------------------------------------------------------------

// The slot whose name is `name` (eight when none is).
std::uint32_t ResourceKit::font_slot_of(std::uint32_t name) {
    const std::string wanted = m_.cstring(name);
    std::uint32_t slot = 0;
    while (slot < kFontSlots && m_.cstring(kFontNames + 0x10 * slot) != wanted) {
        ++slot;
    }
    return slot;
}

// 0x0044b000: the font object of a loaded font (a name that is not loaded asserts and reads the word after the table, as retail does).
std::uint32_t ResourceKit::font_find(std::uint32_t name) {
    const std::uint32_t slot = font_slot_of(name);
    s_.check(slot < kFontSlots, kMsgFontNotLoaded);
    return word_at(m_, kFontTable + 4 * slot);
}

// 0x0044b0a0
void ResourceKit::font_unload(std::uint32_t name) {
    const std::uint32_t slot = font_slot_of(name);
    s_.check(slot < kFontSlots, kMsgFontNotLoaded);
    if (slot < kFontSlots) {
        const std::uint32_t font = word_at(m_, kFontTable + 4 * slot);
        service(kFreeGlyphs, {font});
        const std::uint32_t object = word_at(m_, kFontTable + 4 * slot);
        if (object != 0) {
            (void)virtual_call(word_at(m_, word_at(m_, object)), {object, 1});
        }
        set_word_at(m_, kFontTable + 4 * slot, 0);
    }
}

// 0x0044ada0: read a .fnt file and make the font of it.
std::uint32_t ResourceKit::font_load(std::uint32_t name) {
    const std::uint32_t size = open_resource(name);
    s_.check(0x28 < static_cast<std::int32_t>(size), kMsgFontSize);
    s_.check(size != 0, kMsgFontMissing);
    const std::uint32_t buffer = call(kAlloc, {size, 0, 1, 0});
    s_.check(buffer != 0, kMsgFontMemory);
    load_resource(buffer);
    sync_resource();
    const std::uint32_t font = font_create(buffer, name);
    service(kMemDelete, {buffer});
    return font;
}

// 0x0044aea0: parse the file into a new font object and enter it in the first free slot of the table under its name.
std::uint32_t ResourceKit::font_create(std::uint32_t data, std::uint32_t name) {
    std::uint32_t slot = 0;
    while (slot < kFontSlots && word_at(m_, kFontTable + 4 * slot) != 0) {
        ++slot;
    }
    s_.check(slot < kFontSlots, kMsgFontSlots);
    const std::uint32_t object = call(kNew, {0x14c});
    const std::uint32_t font = object != 0 ? font_parse(object, data) : 0;
    // a full table writes past it, as retail does (the word after the table is the first name)
    set_word_at(m_, kFontTable + 4 * slot, font);
    put_string(kFontNames + 0x10 * slot, m_.cstring(name));
    return font;
}

// 0x00449cd0: the font object from a file: a glyph count, a 16-byte record per glyph (packed size, height, ... width), a 16-entry palette and the 4-bit pixels.
std::uint32_t ResourceKit::font_parse(std::uint32_t self, std::uint32_t data) {
    set_word_at(m_, self + 0x14, 0x80);
    set_word_at(m_, self + 0x18, 0x80);
    set_word_at(m_, self + 0x1c, 0x80);
    set_word_at(m_, self + 0x0c, 4);
    set_word_at(m_, self + 0x10, 4);
    set_word_at(m_, self + 0x24, 4);
    set_word_at(m_, self + 0x28, 4);
    set_word_at(m_, self, kFontVtable);
    set_word_at(m_, self + 0x04, 0);
    set_word_at(m_, self + 0x08, 0);
    set_byte_at(m_, self + 0x20, 0);
    set_byte_at(m_, self + 0x21, 1);
    set_word_at(m_, self + 0x2c, 0);
    set_word_at(m_, self + 0x30, 0x1000);
    set_word_at(m_, self + 0x40, 0);
    const std::uint32_t count = word_at(m_, data);
    set_word_at(m_, self + 0x38, count);
    service(kSetMapping, {self, 0});
    const std::uint32_t table = call(kAlloc, {count * 8 + 8, 0, 1, 0});
    set_word_at(m_, self + 0x34, table);

    const std::uint32_t palette_x = global_word(m_, kPalX);
    const std::uint32_t palette_y = call(kFreeSlot) + global_word(m_, kPalY);
    const std::uint32_t palette = call(kAlloc, {0x20, 0, 1, 0});
    for (std::uint32_t k = 0; k < 16; ++k) {
        set_half_at(m_, palette + 2 * k, half_at(m_, data + 4 + (k + count * 8) * 2));
    }
    service(kPaletteStub);   // the upload rectangle {palette_x, palette_y, 16, 1} of the PlayStation build, which this build discards
    service(kConvertPalette, {palette, 0x10});
    const std::uint32_t id = (static_cast<std::uint32_t>(static_cast<std::int32_t>(palette_x) >> 4) & 0x3f) | palette_y * 0x40;
    set_word_at(m_, self + 0x3c, id);

    auto finish = [&](std::uint32_t glyph) {
        // Image_Nothing457130 and Image_Nothing457120 follow the construction and do nothing
        set_byte_at(m_, glyph + 10, 0);
        set_byte_at(m_, glyph + 11, 0);
        set_byte_at(m_, glyph + 5, 1);
    };
    std::uint32_t pixels = data + count * 16 + 36;
    std::uint32_t entry = data + 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t at = table + i * 8;
        set_byte_at(m_, at + 4, word_at(m_, entry + 12));
        set_byte_at(m_, at + 5, word_at(m_, entry + 4));
        set_byte_at(m_, at + 6, word_at(m_, entry + 8));
        const std::uint32_t packed = word_at(m_, entry);
        const std::uint32_t width = byte_at(m_, at + 4);
        const std::uint32_t height = byte_at(m_, at + 5);
        set_global_word(m_, kLastClut, call(kCreateRecord, {pixels, width, height, palette, 0x10, 0}));
        const std::uint32_t object = call(kNew, {0x5c});
        const std::uint32_t image_id = (table & 0xffff0000u) | (word_at(m_, self + 0x3c) & 0xffffu);
        const std::uint32_t glyph = object != 0 ? call(kConstructImage, {object, pixels, width, height, 0, 0, 4, image_id}) : 0;
        set_word_at(m_, at, glyph);
        finish(glyph);
        const std::uint32_t product = height * packed;
        pixels += ((product & 1) != 0 ? 2 : 0) + product * 2;
        entry += 16;
    }

    // the glyph after the last one, from the first record's fields: a blank picture
    const std::uint32_t last = table + count * 8;
    set_byte_at(m_, last + 4, byte_at(m_, data + 16));
    set_byte_at(m_, last + 5, byte_at(m_, data + 8));
    set_byte_at(m_, last + 6, byte_at(m_, data + 12));
    const std::uint32_t cells = byte_at(m_, last + 5) * word_at(m_, data + 4);
    const std::uint32_t bytes = cells * 2;
    const std::uint32_t blank = call(kAlloc, {bytes, 0, 1, 0});
    if (static_cast<std::int32_t>(bytes) > 0) {
        m_.fill(blank, 0x12, bytes);
    }
    set_global_word(m_, kLastClut, call(kCreateRecord, {blank, byte_at(m_, last + 4), byte_at(m_, last + 5), palette, 0x10, 0}));
    const std::uint32_t object = call(kNew, {0x5c});
    const std::uint32_t image_id = (count & 0xffff0000u) | (word_at(m_, self + 0x3c) & 0xffffu);
    const std::uint32_t glyph = object != 0 ? call(kConstructImage, {object, blank, byte_at(m_, last + 4), byte_at(m_, last + 5), 0, 0, 4, image_id}) : 0;
    set_word_at(m_, last, glyph);
    service(kMemDelete, {blank});
    finish(glyph);
    service(kMemDelete, {palette});
    return self;
}

// ---- D3D texture records ----------------------------------------------------------------------------------------------------

// 0x004d9880: a zeroed record on the doubly linked list of every texture record.
std::uint32_t ResourceKit::alloc_record() {
    const std::uint32_t record = call(seam::crt::CRT_operator_new, {kRecordSize});
    m_.fill(record, 0, kRecordSize);
    if (global_word(m_, kRecordHead) == 0) {
        set_global_word(m_, kRecordHead, record);
    }
    const std::uint32_t tail = global_word(m_, kRecordTail);
    if (tail != 0) {
        set_word_at(m_, tail + 0x24, record);
        set_word_at(m_, record + 0x28, tail);
    }
    set_global_word(m_, kRecordTail, record);
    set_global_word(m_, kRecordCount, global_word(m_, kRecordCount) + 1);
    return record;
}

}  // namespace opentony::runtime::resourcekit
