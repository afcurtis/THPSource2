#include "auto_views.hpp"
#include "level_editor.hpp"
#include "seam_addresses_generated.hpp"

#include <cstdio>

namespace opentony::runtime::leveleditor {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kMenuVtable = 0x00518850;
constexpr std::uint32_t kMenuBaseVtable = 0x005184b4;
constexpr std::uint32_t kMenuFont = 0x005287a4;            // "ingame1.fnt"
constexpr std::uint32_t kDeleteIds = 0x00528714;
constexpr std::uint32_t kDeleteTexts = 0x0052e404;
constexpr std::uint32_t kFmtParkFile = 0x005287dc;         // "park%d.prk"
constexpr std::uint32_t kFmtLabel = 0x00528810;
constexpr std::uint32_t kLabelFile = 0x00528814;           // "CDParks.txt"
constexpr std::uint32_t kLogLoading = 0x005287cc;
constexpr std::uint32_t kLogLoadingName = 0x005287e8;
constexpr std::uint32_t kLogSize = 0x005287c0;
constexpr std::uint32_t kLogSaving = 0x005287b0;
constexpr std::uint32_t kParks = 0x32;
}  // namespace

// ---- helpers -------------------------------------------------------------------------------------------------------------------

std::uint32_t LevelEditor::alloc(std::uint32_t size, std::uint32_t heap, std::uint32_t one, std::uint32_t last) {
    s_.note("alloc", {size, heap, one, last});
    return static_cast<std::uint32_t>(s_.value("alloc"));
}

void LevelEditor::free_block(std::uint32_t block) { s_.note("mem_delete", {block}); }

void LevelEditor::log(std::uint32_t format) { s_.note("printf_log", {format}); }

namespace {
std::string service_name(std::uint32_t address) {
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "engine_%08x", address);
    return buffer;
}
}  // namespace

void LevelEditor::engine(std::uint32_t address, std::vector<std::uint32_t> args, std::string text) {
    if (dispatch_) {
        (void)dispatch_(address, args);
        return;
    }
    s_.note(service_name(address), std::move(args), std::move(text));
}

std::uint32_t LevelEditor::engine_value(std::uint32_t address, std::vector<std::uint32_t> args, std::string text) {
    if (dispatch_) {
        return dispatch_(address, args);
    }
    s_.note(service_name(address), std::move(args), std::move(text));
    return static_cast<std::uint32_t>(s_.value(service_name(address)));
}

// a virtual call on an engine object: the slot of its class's vtable is the service (the object comes first).
std::uint32_t LevelEditor::virtual_call(std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args, std::string text) {
    const std::uint32_t target = word_at(m_, word_at(m_, object) + 4 * slot);
    args.insert(args.begin(), object);
    return engine_value(target, std::move(args), std::move(text));
}

std::uint32_t LevelEditor::operator_new(std::uint32_t size) {
    s_.note("operator_new", {size});
    return static_cast<std::uint32_t>(s_.value("operator_new"));
}

// FileIO_LoadResource then FileIO_SyncResource: the file is read into the buffer (the replay's script holds the bytes the retail run found, a word at a time).
void LevelEditor::load_resource(std::uint32_t buffer, std::uint32_t size) {
    engine(0x00449230, {buffer});
    if (replay()) {
        for (std::uint32_t at = 0; at < size; at += 4) {
            const std::uint32_t word = static_cast<std::uint32_t>(s_.value("engine_00449230"));
            for (std::uint32_t k = 0; k < 4 && at + k < size; ++k) {
                set_byte_at(m_, buffer + at + k, (word >> (8 * k)) & 0xff);
            }
        }
    }
    engine(0x00449660);
}

// 0x005002a1: sprintf for the formats this file uses: %s takes the string the next argument points at, %d the next argument as a number.
void LevelEditor::sprintf_to(std::uint32_t destination, std::uint32_t format, std::uint32_t argument) { sprintf_list(destination, format, {argument}); }

void LevelEditor::sprintf_list(std::uint32_t destination, std::uint32_t format, const std::vector<std::uint32_t>& arguments) {
    const std::string pattern = m_.cstring(format, 0x80);
    std::string out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%' || i + 1 >= pattern.size()) {
            out.push_back(pattern[i]);
            continue;
        }
        ++i;
        if (pattern[i] == 's') {
            out += m_.cstring(arguments.at(next++), 0x100);
        } else {
            out += std::to_string(static_cast<std::int32_t>(arguments.at(next++)));       // (%d: the only other conversion the formats of this file use)
        }
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        set_byte_at(m_, destination + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(out[i]));
    }
    set_byte_at(m_, destination + static_cast<std::uint32_t>(out.size()), 0);
}

// strcpy: the string and its terminator
void LevelEditor::copy_string(std::uint32_t destination, std::uint32_t source) {
    std::uint32_t at = 0;
    for (;; ++at) {
        const std::uint8_t c = byte_at(m_, source + at);
        set_byte_at(m_, destination + at, c);
        if (c == 0) {
            break;
        }
    }
}

// the bytes at an address as the text of a service call (a routine handed a small structure by pointer)
std::string LevelEditor::bytes_text(std::uint32_t at, std::uint32_t size) {
    const auto bytes = m_.bytes(at, size);
    return std::string(bytes.begin(), bytes.end());
}

// a service that writes `size` bytes through the pointer argument `destination`: the replay's script holds them, a word at a time
void LevelEditor::engine_fill(std::uint32_t address, std::vector<std::uint32_t> args, std::string text, std::uint32_t destination, std::uint32_t size) {
    engine(address, std::move(args), std::move(text));
    if (replay()) {
        for (std::uint32_t at = 0; at < size; at += 4) {
            const std::uint32_t word = static_cast<std::uint32_t>(s_.value(service_name(address)));
            for (std::uint32_t k = 0; k < 4 && at + k < size; ++k) {
                set_byte_at(m_, destination + at + k, (word >> (8 * k)) & 0xff);
            }
        }
    }
}

std::uint32_t LevelEditor::engine_out(std::uint32_t address, std::vector<std::uint32_t> args, std::uint32_t out, bool answers, std::string text) {
    if (dispatch_) {
        return dispatch_(address, args);
    }
    engine(address, std::move(args), std::move(text));
    const std::string name = service_name(address);
    if (out != 0 && replay()) {   // (live, the routine stored it through the pointer and the one value is its answer)
        set_word_at(m_, out, static_cast<std::uint32_t>(s_.value(name)));
    }
    return answers ? static_cast<std::uint32_t>(s_.value(name)) : 0;
}

std::uint32_t LevelEditor::engine_fill_value(std::uint32_t address, std::vector<std::uint32_t> args, std::string text, std::uint32_t destination, std::uint32_t size) {
    if (dispatch_) {
        return dispatch_(address, args);
    }
    engine_fill(address, std::move(args), std::move(text), destination, size);
    return static_cast<std::uint32_t>(s_.value(service_name(address)));
}

// 0x0046f2e0: Mem_Copy (both addresses word aligned or the assertion logs).
void LevelEditor::mem_copy(std::uint32_t destination, std::uint32_t source, std::uint32_t size) {
    s_.check((destination & 3) == 0, kMsgMemCopy);
    s_.check((source & 3) == 0, kMsgMemCopy);
    if (static_cast<std::int32_t>(size) > 0) {
        m_.copy(destination, source, size);
    }
}

// ---- the maps ------------------------------------------------------------------------------------------------------------------

// 0x004326a0: the editor's cell rows (a table of 60 rows and the block of cells) and the ten item records are taken once.
void LevelEditor::alloc_maps() {
    s_.check(global_word(m_, kMapState) == 0, kMsgMapInMemory);
    if (global_word(m_, kRowsSource) == 0) {
        set_global_word(m_, kRowsSource, alloc(0xf0, 1, 1, 0));
        const std::uint32_t block = alloc(0x3840, 1, 1, 0);
        set_word_at(m_, global_word(m_, kRowsSource), block);
    }
    if (global_word(m_, kItemsSource) == 0) {
        set_global_word(m_, kItemsSource, alloc(0x1b8, 1, 1, 0));
    }
    set_global_word(m_, kMapState, 1);
    set_park_name(0, 1);
}

// 0x004327a0
void LevelEditor::free_maps() {
    s_.check(global_word(m_, kMapState) != 0, kMsgMapFreed);
    if (global_word(m_, kRowsSource) != 0) {
        free_block(word_at(m_, global_word(m_, kRowsSource)));
        free_block(global_word(m_, kRowsSource));
    }
    set_global_word(m_, kRowsSource, 0);
    if (global_word(m_, kItemsSource) != 0) {
        free_block(global_word(m_, kItemsSource));
    }
    set_global_word(m_, kItemsSource, 0);
    set_global_word(m_, kMapState, 0);
}

void LevelEditor::nothing_a() {}
void LevelEditor::nothing_b() {}

// 0x00432860: the editor's command table: 0 runs the editor, 1 throws the loaded park away, 2 unloads its region, 3 saves it into the buffer `argument` names.
void LevelEditor::command(std::uint32_t what, std::uint32_t argument) {
    switch (what) {
    case 0:
        run();
        break;
    case 1:
        discard_park();
        break;
    case 2:
        unload_psx();
        break;
    case 3:
        save_park(argument);
        break;
    default:
        break;
    }
}

// 0x004328c0: the objects of the base pieces (a model with bit 1 of its second flag byte) take two colour bytes up or down by 0x7f as the shade changes.
void LevelEditor::set_shade(std::uint32_t shade, std::uint32_t slot) {
    const std::uint8_t wanted = static_cast<std::uint8_t>(shade);
    if (global_byte(m_, kFlashOn) == 0 || global_byte(m_, kShadeCache) == wanted) {
        return;
    }
    set_global_byte(m_, kShadeCache, wanted);
    const std::uint8_t delta = wanted != 0 ? 0x81 : 0x7f;
    const std::uint32_t objects = word_at(m_, kObjectTables + slot * 0x44);
    const std::uint32_t raw = word_at(m_, kObjectTables + 8 + slot * 0x44);
    const std::uint32_t models = raw + 0x10 + global_word(m_, kSlotRow) * 0x24;
    const std::int32_t count = global_int(m_, kObjectCount);
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t record = objects + 0x1c + static_cast<std::uint32_t>(i) * 0x4c;
        const std::uint32_t model = word_at(m_, models + 4 * half_at(m_, record - 2));
        if ((byte_at(m_, model + 1) & 2) != 0) {
            set_byte_at(m_, record, byte_at(m_, record) + delta);
            set_byte_at(m_, record + 2, byte_at(m_, record + 2) + delta);
        }
    }
}

// 0x00432960
std::uint32_t LevelEditor::alloc_buffer(std::uint32_t size) { return alloc(size, 1, 1, 0); }

// 0x00432980: the name the editor shows for the park: a label, or "empty park"; a reset clears the changed flag.
void LevelEditor::set_park_name(std::uint32_t label, std::uint32_t reset) {
    if (label != 0) {
        set_global_byte(m_, kParkIsNew, 0);
        sprintf_to(kParkName, kNameFormat, label);
    } else {
        set_global_byte(m_, kParkIsNew, 1);
        sprintf_to(kParkName, global_word(m_, kParkNamePointer), 0);
    }
    if ((reset & 0xff) != 0) {
        set_global_byte(m_, kNameDirty, 0);
    }
}

// ---- the park menu (object: +0 vtable, +4 the delete menu, +8..+0x14 its texts, +0x18 the label of the park, +0x58 step, +0x5c mode, +0x60 saving, +0x64 result,
//      +0x68 player, +0x6c buffer, +0x70 its size, +0x74 the list of parks, +0x78 the labels' file, +0x7c the 50 labels) ------------

// 0x004329d0
std::uint32_t LevelEditor::menu_construct(std::uint32_t self, std::uint32_t mode, std::uint32_t buffer, std::uint32_t size, std::uint32_t player) {
    level_editor_global_menu_at(m_, self).set_field_70(static_cast<Word>(size));
    level_editor_global_menu_at(m_, self).set_field_0(static_cast<Word>(kMenuVtable));
    level_editor_global_menu_at(m_, self).set_field_6c(static_cast<Word>(buffer));
    set_word_at(m_, self + 0x68, player);
    level_editor_global_menu_at(m_, self).set_field_5c(static_cast<Word>((mode == 0 || mode == 2) ? 0 : 1));
    level_editor_global_menu_at(m_, self).set_field_60(static_cast<std::uint8_t>(mode == 2 ? 1 : 0));
    if (level_editor_global_menu_at(m_, self).field_5c() == 1) {
        level_editor_global_menu_at(m_, self).set_field_5c(4);
    }
    // (a mode 1 is made 4 above, so the retail switch's case 1 can never run)
    const std::uint32_t kind = level_editor_global_menu_at(m_, self).field_5c();
    if (kind == 0) {
        load_labels(self);
        for (std::uint32_t i = 0; i < kParks; ++i) {
            set_word_at(m_, kScratchList + 4 * i, i);
        }
        const std::uint32_t block = operator_new(0x290);
        const std::uint32_t wrapper = block != 0 ? engine_value(0x00472ef0, {block, self + 0x7c, kScratchList, kParks, kMenuFont, 1}) : 0;
        level_editor_global_menu_at(m_, self).set_field_74(static_cast<Word>(wrapper));
        set_byte_at(m_, wrapper + 0xc, byte_at(m_, self + 0x68));
        set_byte_at(m_, level_editor_global_menu_at(m_, self).field_74() + 0xd, 1);
    } else if (kind == 4) {
        engine(0x004dc3a0, {self, kScratchList, kParks});
        const std::uint32_t block = operator_new(0x290);
        const std::uint32_t wrapper = block != 0 ? engine_value(0x00472ef0, {block, self + 0x7c, kScratchList, kParks, kMenuFont, 1}) : 0;
        level_editor_global_menu_at(m_, self).set_field_74(static_cast<Word>(wrapper));
        set_byte_at(m_, wrapper + 0xc, byte_at(m_, self + 0x68));
        level_editor_global_menu_at(m_, self).set_field_58(0);
    }
    return self;
}

// 0x00432b60: the scalar deleting destructor
std::uint32_t LevelEditor::menu_delete(std::uint32_t self, std::uint32_t flags) {
    menu_destroy(self);
    if ((flags & 1) != 0) {
        engine(0x00466bb0, {self});
    }
    return self;
}

// 0x00432b80
void LevelEditor::menu_destroy(std::uint32_t self) {
    level_editor_global_menu_at(m_, self).set_field_0(static_cast<Word>(kMenuVtable));
    switch (level_editor_global_menu_at(m_, self).field_5c()) {
    case 1:
        if ((engine_value(0x0047ac40) & 0xff) != 0) {
            engine(0x0047ae10, {2});
        }
        break;
    case 0:
    case 4:
        free_block(level_editor_global_menu_at(m_, self).field_78());
        if (level_editor_global_menu_at(m_, self).field_74() != 0) {
            (void)virtual_call(level_editor_global_menu_at(m_, self).field_74(), 0, {1});
        }
        break;
    default:
        break;
    }
    level_editor_global_menu_at(m_, self).set_field_0(static_cast<Word>(kMenuBaseVtable));
}

// 0x00432c10: a step of the list the mode is showing (a tail call: the result is the list's).
std::uint32_t LevelEditor::menu_update(std::uint32_t self) {
    const std::uint32_t mode = level_editor_global_menu_at(m_, self).field_5c();
    if (mode == 0) {
        return virtual_call(word_at(m_, level_editor_global_menu_at(m_, self).field_74() + 4), 3);
    }
    if (mode == 1) {
        return engine_value(0x0047ace0);
    }
    if (mode == 4) {
        const std::int32_t step = static_cast<std::int32_t>(level_editor_global_menu_at(m_, self).field_58());
        if (step >= 0 && step < 2) {
            return virtual_call(word_at(m_, level_editor_global_menu_at(m_, self).field_74() + 4), 3);
        }
        if (step == 2) {
            return virtual_call(word_at(m_, level_editor_global_menu_at(m_, self).field_4() + 4), 3);
        }
    }
    return 0;
}

// 0x00432c60: one step of the load and save state machine.  Mode 0 is the parks on the disc (50 files), 1 the memory card, 4 the list of saved parks; the result at +0x64
// is 0 (waiting), 1 (done) or 2 (the choice was not a park).
std::uint32_t LevelEditor::menu_process(std::uint32_t self) {
    const std::uint32_t player = global_word(m_, kActivePlayer);
    const std::uint32_t mode = level_editor_global_menu_at(m_, self).field_5c();
    const std::uint32_t list = level_editor_global_menu_at(m_, self).field_74();
    auto finish = [&](std::uint32_t object, std::uint32_t result) {
        level_editor_global_menu_at(m_, self).set_field_64(static_cast<Word>(result));
        (void)virtual_call(object, 1);
        return level_editor_global_menu_at(m_, self).field_64();
    };
    if (mode == 0) {
        if ((virtual_call(list, 2) & 0xff) != 0) {
            const std::int32_t choice = static_cast<std::int32_t>(virtual_call(list, 3));
            if (choice >= 0 && choice < static_cast<std::int32_t>(kParks)) {
                sprintf_to(kScratchFile, kFmtParkFile, static_cast<std::uint32_t>(choice));
                const std::string name = m_.cstring(kScratchFile, 0x10);
                if (level_editor_global_menu_at(m_, self).field_60() == 0) {
                    log(kLogLoading);
                    const std::uint32_t size = engine_value(0x00449030, {kScratchFile}, name);
                    log(kLogSize);
                    load_resource(level_editor_global_menu_at(m_, self).field_6c(), size);
                    engine(0x004de180, {0, kScratchFile}, name);
                } else {
                    log(kLogSaving);
                    engine(seam::hal::PCinit);
                    const std::uint32_t handle = engine_value(0x004e7900, {kScratchFile, 0}, name) & 0xffff;
                    engine(0x004e79f0, {handle, 0, 0});
                    engine(0x004e7b60, {handle, level_editor_global_menu_at(m_, self).field_6c(), level_editor_global_menu_at(m_, self).field_70()});
                    engine(0x004e7bc0, {handle});
                }
                set_park_name(word_at(m_, self + 0x7c + 4 * static_cast<std::uint32_t>(choice)), 1);
                return finish(list, 1);
            }
            return finish(list, 2);
        }
        return finish(list, 0);
    }
    if (mode == 1) {
        set_global_word(m_, kActivePlayer, word_at(m_, self + 0x68));
        const std::uint32_t result = engine_value(0x0047ac60);
        level_editor_global_menu_at(m_, self).set_field_64(static_cast<Word>(result));
        if (result == 1) {
            engine(0x0047b850, {kParkName});
            set_park_name(kParkName, 0);
        }
        set_global_word(m_, kActivePlayer, player);
        return level_editor_global_menu_at(m_, self).field_64();
    }
    if (mode != 4) {
        return level_editor_global_menu_at(m_, self).field_64();
    }
    const std::uint32_t step = level_editor_global_menu_at(m_, self).field_58();
    if (step == 0) {
        if ((virtual_call(list, 2) & 0xff) != 0) {
            const std::int32_t choice = static_cast<std::int32_t>(virtual_call(list, 3));
            if (choice >= 0 && choice < static_cast<std::int32_t>(kParks)) {
                const std::uint32_t label = word_at(m_, self + 0x7c + 4 * static_cast<std::uint32_t>(choice));
                engine(0x004f6140, {label, 1, 0, word_at(m_, global_word(m_, kRowsSource))}, m_.cstring(label, 0x40));
                m_.copy(level_editor_global_menu_at(m_, self).field_6c(), word_at(m_, global_word(m_, kRowsSource)), 0x780 * 4);
                set_park_name(label, 1);
                engine(0x004de180, {1, label}, m_.cstring(label, 0x40));
                return finish(list, 1);
            }
            return finish(list, 2);
        }
        return finish(list, 0);
    }
    if (step == 1) {
        if ((virtual_call(list, 2) & 0xff) != 0) {
            const std::int32_t choice = static_cast<std::int32_t>(virtual_call(list, 3));
            if (choice >= 0 && choice < static_cast<std::int32_t>(kParks)) {
                menu_set_mode(self, 2);
                (void)virtual_call(list, 1);
                return level_editor_global_menu_at(m_, self).field_64();
            }
            return finish(list, 2);
        }
        return finish(list, 0);
    }
    if (step == 2) {
        const std::uint32_t confirm = level_editor_global_menu_at(m_, self).field_4();
        if ((virtual_call(confirm, 2) & 0xff) != 0) {
            if (virtual_call(confirm, 3) != 0) {
                const std::uint32_t choice = virtual_call(list, 3);
                const std::uint32_t label = word_at(m_, self + 0x7c + 4 * choice);
                engine(0x004f6400, {label, 0}, m_.cstring(label, 0x40));
                return finish(confirm, 1);
            }
            level_editor_global_menu_at(m_, self).set_field_58(0);
            level_editor_global_menu_at(m_, self).set_field_64(0);
        }
        (void)virtual_call(confirm, 1);
        return level_editor_global_menu_at(m_, self).field_64();
    }
    return level_editor_global_menu_at(m_, self).field_64();
}

// 0x00432f60: load the park a file name says (from the disc: the file into the buffer and its label; from the card: the card's file).
std::uint32_t LevelEditor::menu_load_by_name(std::uint32_t self, std::uint32_t name) {
    s_.check(level_editor_global_menu_at(m_, self).field_60() == 0, kMsgLoadSaving);
    const std::uint32_t mode = level_editor_global_menu_at(m_, self).field_5c();
    if (mode == 0) {
        log(kLogLoadingName);
        const std::string text = m_.cstring(name, 0x40);
        const std::uint32_t size = engine_value(0x00449030, {name}, text);
        log(kLogSize);
        load_resource(level_editor_global_menu_at(m_, self).field_6c(), size);
        int index = 0;
        std::sscanf(text.c_str(), "park%d.prk", &index);
        set_park_name(word_at(m_, self + 0x7c + 4 * static_cast<std::uint32_t>(index)), 1);
    } else if (mode == 1) {
        const std::uint32_t card = global_word(m_, kMmuManager) + 0x1f8;
        const std::int32_t found = static_cast<std::int32_t>(engine_value(0x004746d0, {card, name}, m_.cstring(name, 0x40)));
        if (found < 0) {
            return 0;
        }
        const std::uint32_t player = global_word(m_, kActivePlayer);
        set_global_word(m_, kActivePlayer, word_at(m_, self + 0x68));
        engine(0x00476200, {card, static_cast<std::uint32_t>(found), word_at(m_, global_word(m_, kMmuManager) + 0xda8)});
        engine(0x0047b850, {kParkName});
        set_park_name(kParkName, 0);
        set_global_word(m_, kActivePlayer, player);
    }
    return 1;
}

// 0x00433090: the step the park menu is in; step 2 builds the "delete this park?" menu (the park's label, the title and the yes and no texts).
void LevelEditor::menu_set_mode(std::uint32_t self, std::uint32_t mode) {
    level_editor_global_menu_at(m_, self).set_field_58(static_cast<Word>(mode));
    const std::uint32_t choice = virtual_call(level_editor_global_menu_at(m_, self).field_74(), 3);
    sprintf_to(self + 0x18, kFmtLabel, word_at(m_, self + 0x7c + 4 * choice));
    if (level_editor_global_menu_at(m_, self).field_58() == 2) {
        level_editor_global_menu_at(m_, self).set_field_c(static_cast<Word>(self + 0x18));
        level_editor_global_menu_at(m_, self).set_field_8(static_cast<Word>(global_word(m_, kDeleteTexts)));
        level_editor_global_menu_at(m_, self).set_field_10(static_cast<Word>(global_word(m_, kDeleteTexts + 0xc)));
        level_editor_global_menu_at(m_, self).set_field_14(static_cast<Word>(global_word(m_, kDeleteTexts + 0x10)));
        if (level_editor_global_menu_at(m_, self).field_4() != 0) {
            (void)virtual_call(level_editor_global_menu_at(m_, self).field_4(), 0, {1});
        }
        const std::uint32_t block = operator_new(0x290);
        const std::uint32_t wrapper = block != 0 ? engine_value(0x00472ef0, {block, self + 8, kDeleteIds, 4, kMenuFont, 0}) : 0;
        level_editor_global_menu_at(m_, self).set_field_4(static_cast<Word>(wrapper));
        set_byte_at(m_, wrapper + 0xc, byte_at(m_, self + 0x68));
    }
}

// 0x00433160: the labels of the 50 parks on the disc: the file "CDParks.txt", each label between a '$' and the end of its line.
void LevelEditor::load_labels(std::uint32_t self) {
    const std::uint32_t size = engine_value(0x00449030, {kLabelFile}, m_.cstring(kLabelFile, 0x20));
    const std::uint32_t buffer = alloc(size, 0, 1, 0);
    set_word_at(m_, self + 0x78, buffer);
    load_resource(buffer, size);
    std::uint32_t at = word_at(m_, self + 0x78);
    for (std::uint32_t i = 0; i < kParks; ++i) {
        while (byte_at(m_, at) != '$') {
            ++at;
        }
        ++at;
        set_word_at(m_, self + 0x7c + 4 * i, at);
        while (byte_at(m_, at) != '\r') {
            ++at;
        }
        set_byte_at(m_, at, 0);
    }
}

}  // namespace opentony::runtime::leveleditor
