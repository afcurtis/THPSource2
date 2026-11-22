#include "mmu_views.hpp"
#include "seam_addresses_generated.hpp"

#include <algorithm>

// Phase 4.14b: the rest of the card layer that is not the screen's own code: the flags and the registration the screen reads, the set-up that builds the manager for each kind of save or load, the
// replay card helpers, the string helpers (0x00479f20..0x0047ba00), and the libcard emulation over files (0x004e8d40..0x004e9130).

namespace opentony::runtime::mmu {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kMsgLeak = 0x00526874;              // "Possible memory leak"
constexpr Word kMsgOverflow = 0x005353c4;          // "File overflows block size"
constexpr Word kMsgPlayers = 0x005355e4;           // "Invalid player count"
constexpr Word kMsgNoHeader = 0x00535594;          // "player 1 header does not exist"
constexpr Word kMsgNoSaveBuffer = 0x00535628;      // "No buffer to save"
constexpr Word kMsgNoLoadBuffer = 0x0053563c;      // "No buffer to load"
constexpr Word kMsgParkAlign = 0x005355fc;         // "data size must be a multiple of 128"
constexpr Word kMsgScreenUpdate = 0x00535650;
constexpr Word kMsgNoScreen = 0x00534590;          // "No screen"
constexpr Word kMsgFinalize = 0x005357e0;
constexpr Word kMsgInvalidKey = 0x00535694;        // "Invalid key press"
constexpr Word kMsgNotFound = 0x0053584c;          // "file not found in Card_LoadReplay"
constexpr Word kMsgNoLoad = 0x00535448;
constexpr Word kMsgLoadOffset = 0x005353f0;
constexpr Word kMsgNotComplete = 0x005353e0;
constexpr Word kMsgLoadFailed = 0x00535878;
constexpr Word kMsgNoReturnBuffer = 0x00535884;    // "No return buffer specified"
constexpr Word kMsgUnrecognizedType = 0x005358a0;

constexpr Word kQuitComboReset = 0x00452dc0;
constexpr Word kCareerPrepareSave = 0x004166f0;
constexpr Word kCareerApplyLoadedSave = 0x004165d0;
constexpr Word kMenuConstruct = 0x00476b00;        // the screen's constructor
constexpr Word kReplayWriteHeader = 0x004cb810;
constexpr Word kPCinit = seam::hal::PCinit;
constexpr Word kPCremove = 0x004e7500;
constexpr Word kBackendOpen = 0x004e75b0;
constexpr Word kPCcreate = 0x004e7900;
constexpr Word kBackendSeek = 0x004e79f0;
constexpr Word kBackendRead = 0x004e7ad0;
constexpr Word kPCwrite = 0x004e7b60;
constexpr Word kBackendClose = 0x004e7bc0;
constexpr Word kOpenResource = 0x00449030;
constexpr Word kLoadResource = 0x00449230;
constexpr Word kSyncResource = 0x00449660;
constexpr Word kCardNothingA = 0x004e8c90;
constexpr Word kCardNothingB = 0x004e8cb0;
constexpr Word kCardNothingC = 0x004e8cc0;
constexpr Word kCardNothingD = 0x004e8ca0;

constexpr Word kActivePlayer = 0x0056a888;
constexpr Word kInputActions = 0x0056aff9;         // byte per player at a 0x16c stride
constexpr Word kReplayBuffer0 = 0x0056e5dc;
constexpr Word kReplayBuffer1 = 0x0056e454;
constexpr Word kReplayNumSkaters = 0x0056b8d4;     // short
constexpr Word kReplaySkaters = 0x0056b8d6;        // two bytes
constexpr Word kReplayCostumes = 0x0056b8d8;       // two bytes
constexpr Word kReplayLevel = 0x0056b8da;          // short
constexpr Word kReplayGame = 0x0056b8dc;           // short
constexpr Word kReplaySkaterData = 0x0056b928;     // 0x100 bytes per skater
constexpr Word kReplayFrames = 0x0056e5f8;
constexpr Word kReplayHighlightA = 0x0056e62c;     // 8 bytes per range
constexpr Word kReplayHighlightB = 0x0056e630;
constexpr Word kPrefixTable[3] = {0x0052e35c, 0x0052e360, 0x0052e358};     // career, replay, park

// the log lines of the finalise step (formats; the log stub records only the format)
constexpr Word kLogBanner = 0x005357c0;
constexpr Word kLogLines[9] = {0x005357a0, 0x00535784, 0x00535764, 0x00535748, 0x00535728, 0x0053570c, 0x005356f0, 0x005356d0, 0x005356a8};
constexpr Word kLogHighlights = 0x005356a8;
constexpr Word kLogTwoPlayers = 0x00535820;

// a transfer is recorded (and scripted) for its first 0x400 bytes only: the replay streams are 0x7fe00 bytes
constexpr Word kTransferLimit = 0x400;

inline bool multiple_of(Word value, Word modulus) { return static_cast<std::int32_t>(value) % static_cast<std::int32_t>(modulus) == 0; }

}  // namespace

// ---- the screen's flags and the registration ---------------------------------------------------------------------------------------------------------------------------------------

bool MmuKit::flag_8(Word self) { return (screen_at(m_, self).button_flags() & 8) != 0; }      // 0x00479f20
bool MmuKit::flag_4(Word self) { return (screen_at(m_, self).button_flags() & 4) != 0; }      // 0x00479f40
bool MmuKit::screen_exists() { return global_word(m_, globals::kManager) != 0; }              // 0x0047ac40
void MmuKit::clear_loaded_flag() { set_global_byte(m_, globals::kCardLoadedFlag, 0); }        // 0x0047b8d0

// 0x00479f90: what the screen answers each frame: 0 while it waits, the key that ended it, or 2 once for the first press of a held button
MmuKit::Word MmuKit::screen_result(Word self) {
    const Word state = screen_at(m_, self).display_state();
    if (state == 5) {
        return 0;
    }
    const Word action_at = kInputActions + global_word(m_, kActivePlayer) * 0x16c;
    if (state == 3 || state == 2 || state == 4 || byte_at(m_, action_at) == 0) {
        return screen_at(m_, self).result() == 0 ? 0 : screen_at(m_, self).result();
    }
    s_.note("sfx", {0xb0, 0x2000, 0});
    set_byte_at(m_, action_at, 0);
    return 2;
}

// 0x0047a030
void MmuKit::register_save_buffer(Word screen, Word buffer, Word size) { add_save_buffer(screen + manager::kInScreen, buffer, size); }

// 0x0047a060 / 0x0047a080: the library's start and stop (empty in the PC build)
void MmuKit::init() {
    engine(kCardNothingA, {1});
    engine(kCardNothingB);
}

void MmuKit::shutdown() {
    engine(kCardNothingC);
    engine(kCardNothingD);
}

// 0x0047a090: ask the library for its state (the answer is dropped; the call only lets it advance)
void MmuKit::refresh_card_state() { card_get_state(0, kScratchA, kScratchB); }

// ---- the set-up for each kind of save and load ---------------------------------------------------------------------------------------------------------------------------------


// the manager object every set-up builds: `operator new(0xdd0)`, then the screen's constructor (type, argument, loading flag); 0 when the allocation fails
MmuKit::Word MmuKit::create_manager(Word type, Word argument, Word flag) {
    s_.note("operator_new", {manager::kObjectSize});
    const Word object = static_cast<Word>(s_.value("operator_new"));
    if (object == 0) {
        return 0;
    }
    return engine_value(kMenuConstruct, {object, type, argument, flag});
}

// 0x0047a0b0: a career save: the career image is the one buffer
void MmuKit::setup_career_save(Word return_a, Word return_b, Word argument) {
    engine(kCareerPrepareSave);
    const Word blocks = validate_action_type(1, 1);
    check(0x1fffu < (blocks << 13), kMsgOverflow);
    check(global_word(m_, globals::kManager) == 0, kMsgLeak);
    set_global_word(m_, globals::kManager, create_manager(1, argument, 1));
    register_save_buffer(global_word(m_, globals::kManager), globals::kCareerSaveImage, 0x1e00);
    set_global_word(m_, globals::kReturnScreen, return_a);
    set_global_word(m_, globals::kReturnScreenLoaded, return_b);
}

// 0x0047a220: a replay save: one or two 0x7fe00-byte streams, each with its header written first
void MmuKit::setup_replay_save(Word return_a, Word return_b, std::int32_t players, Word argument) {
    check(players == 1 || players == 2, kMsgPlayers);
    validate_action_type(2, 1);
    check(global_word(m_, globals::kManager) == 0, kMsgLeak);
    set_global_word(m_, globals::kManager, create_manager(2, argument, 1));
    const Word first = global_word(m_, kReplayBuffer0);
    check(first != 0, kMsgNoHeader);
    engine(kReplayWriteHeader, {first, 0});
    register_save_buffer(global_word(m_, globals::kManager), first, 0x7fe00);
    const Word second = global_word(m_, kReplayBuffer1);
    if (second != 0) {
        engine(kReplayWriteHeader, {second, 1});
        register_save_buffer(global_word(m_, globals::kManager), second, 0x7fe00);
    }
    set_global_word(m_, globals::kReturnScreen, return_a);
    set_global_word(m_, globals::kReturnScreenLoaded, return_b);
}

// 0x0047a480: a custom park save: the caller's buffer, rounded up to a multiple of 128 bytes
void MmuKit::setup_park_save(Word buffer, Word size, Word argument, std::uint8_t flag) {
    check(buffer != 0, kMsgNoSaveBuffer);
    set_global_word(m_, globals::kParkBuffer, buffer);
    set_global_word(m_, globals::kParkBytes, size);
    set_global_word(m_, globals::kScreenMode, flag == 0 ? 2 : 1);
    const Word blocks = validate_action_type(3, 1);
    check(global_word(m_, globals::kManager) == 0, kMsgLeak);
    set_global_word(m_, globals::kManager, create_manager(3, argument, 1));
    check(multiple_of(size, 128), kMsgParkAlign);
    const Word rounded = (size + 0x7f) & 0xffffff80u;
    check(rounded + 0x200u <= (blocks << 13), kMsgOverflow);
    register_save_buffer(global_word(m_, globals::kManager), buffer, rounded);
    set_global_word(m_, globals::kReturnScreen, 5);
    set_global_word(m_, globals::kReturnScreenLoaded, 0xe);
}

// 0x0047a6c0: a career load: a buffer the layer allocates
void MmuKit::setup_career_load(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
    set_global_byte(m_, globals::kCareerLoadFlag, b);
    set_global_byte(m_, globals::kCardSkipFlag, c);
    const Word blocks = validate_action_type(1, 0);
    check(global_word(m_, globals::kManager) == 0, kMsgLeak);
    set_global_word(m_, globals::kManager, create_manager(1, a, 0));
    check(global_word(m_, globals::kCareerLoadBuffer) == 0, kMsgLeak);
    s_.note("alloc", {0x1e00, 0, 1, 0});
    set_global_word(m_, globals::kCareerLoadBuffer, static_cast<Word>(s_.value("alloc")));
    check(0x1fffu < (blocks << 13), kMsgOverflow);
    register_save_buffer(global_word(m_, globals::kManager), global_word(m_, globals::kCareerLoadBuffer), 0x1e00);
}

// 0x0047a8a0: a replay load: a 0x280-byte header buffer the layer allocates
void MmuKit::setup_replay_load(Word argument) {
    check(global_word(m_, globals::kManager) == 0, kMsgLeak);
    set_global_word(m_, globals::kManager, create_manager(2, argument, 0));
    check(global_word(m_, globals::kReplayLoadBuffer) == 0, kMsgLeak);
    s_.note("alloc", {0x280, 0, 1, 0});
    set_global_word(m_, globals::kReplayLoadBuffer, static_cast<Word>(s_.value("alloc")));
    register_save_buffer(global_word(m_, globals::kManager), global_word(m_, globals::kReplayLoadBuffer), 0x280);
}

// 0x0047aa50: a custom park load: the caller's buffer
void MmuKit::setup_park_load(Word buffer, Word size) {
    check(buffer != 0, kMsgNoLoadBuffer);
    const Word blocks = validate_action_type(3, 0);
    check(global_word(m_, globals::kManager) == 0, kMsgLeak);
    set_global_word(m_, globals::kManager, create_manager(3, 1, 0));
    check(multiple_of(size, 128), kMsgParkAlign);
    const Word rounded = (size + 0x7f) & 0xffffff80u;
    check(rounded + 0x200u <= (blocks << 13), kMsgOverflow);
    register_save_buffer(global_word(m_, globals::kManager), buffer, rounded);
}

// 0x0047ac60 / 0x0047ace0 / 0x0047ad50 / 0x0047adb0: the screen's per-frame entry points
MmuKit::Word MmuKit::update_screen() {
    check(global_word(m_, globals::kManager) != 0, kMsgScreenUpdate);
    const Word result = screen_result(global_word(m_, globals::kManager));
    virtual_call(global_word(m_, globals::kManager), 1);
    return result;
}

void MmuKit::draw_screen() {
    check(global_word(m_, globals::kManager) != 0, kMsgNoScreen);
    virtual_call(global_word(m_, globals::kManager), 2, {0, 0});
}

bool MmuKit::screen_flag_8() {
    check(global_word(m_, globals::kManager) != 0, kMsgNoScreen);
    return flag_8(global_word(m_, globals::kManager));
}

bool MmuKit::screen_flag_4() {
    check(global_word(m_, globals::kManager) != 0, kMsgNoScreen);
    return flag_4(global_word(m_, globals::kManager));
}

// 0x0047ae10: end the screen: delete it, hand a loaded career or replay to the game, free the load buffers; answers the screen to go to
MmuKit::Word MmuKit::finalize_screen(std::int32_t key) {
    check(global_word(m_, globals::kManager) != 0, kMsgFinalize);
    if (global_word(m_, globals::kManager) != 0) {
        virtual_call(global_word(m_, globals::kManager), 0, {1});
    }
    set_global_word(m_, globals::kManager, 0);
    set_global_word(m_, globals::kScreenMode, 0);
    const Word career = global_word(m_, globals::kCareerLoadBuffer);
    if (career != 0) {
        if (key == 1) {
            s_.note("mem_copy", {globals::kCareerSaveImage, career, globals::kCareerImageBytes});
            if (global_byte(m_, globals::kCareerLoadFlag) != 0) {
                engine(kCareerApplyLoadedSave);
            }
        }
        s_.note("mem_delete", {career});
        set_global_word(m_, globals::kCareerLoadBuffer, 0);
    }
    const Word replay = global_word(m_, globals::kReplayLoadBuffer);
    if (replay != 0) {
        if (key == 1) {
            const RuntimeReplayHeaderFieldsView header(m_, replay);
            set_global_half(m_, kReplayNumSkaters, header.num_skaters());
            for (Word c = 0; c < 2; ++c) {
                set_global_byte(m_, kReplaySkaters + c, c == 0 ? header.skater() : header.skater2());
                set_global_byte(m_, kReplayCostumes + c, c == 0 ? header.costume() : header.costume2());
                m_.copy(kReplaySkaterData + c * 0x100, replay + 0x80 + c * 0x100, 0x100);
            }
            set_global_half(m_, kReplayLevel, header.level());
            set_global_half(m_, kReplayGame, header.ggame());
            set_global_word(m_, kReplayFrames, header.num_frames());
            for (Word c = 0; c < 5; ++c) {
                const Word second = static_cast<Word>(header.highlight_ranges(c).end());
                set_global_word(m_, kReplayHighlightA + c * 8, static_cast<Word>(header.highlight_ranges(c).start()));
                set_global_word(m_, kReplayHighlightB + c * 8, second);
            }
            log_line(kLogBanner);
            for (std::size_t k = 0; k < 8; ++k) {
                log_line(kLogLines[k]);
            }
            for (Word c = 0; c < 5; ++c) {
                log_line(kLogHighlights);
            }
        }
        s_.note("mem_delete", {replay});
        set_global_word(m_, globals::kReplayLoadBuffer, 0);
    }
    nothing();
    check(key == 2 || key == 1, kMsgInvalidKey);
    set_global_byte(m_, globals::kCareerLoadFlag, 1);
    set_global_byte(m_, globals::kCardSkipFlag, 0);
    return key == 1 ? global_word(m_, globals::kReturnScreenLoaded) : global_word(m_, globals::kReturnScreen);
}

// ---- the replay card helpers -------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x0047b1a0: write one or two replay streams to a file, headers first
void MmuKit::replay_save_buffers(Word name) {
    const Word first = global_word(m_, kReplayBuffer0);
    check(first != 0, kMsgNoHeader);
    engine(kReplayWriteHeader, {first, 0});
    const Word second = global_word(m_, kReplayBuffer1);
    if (second != 0) {
        engine(kReplayWriteHeader, {second, 1});
    }
    engine(kPCinit);
    const Word handle = engine_value(kPCcreate, {name, 0}, m_.cstring(name, 0x100)) & 0xffff;
    engine_value(kBackendSeek, {handle, 0, 0});
    backend_write(handle, first, 0x7fe00);
    if (second != 0) {
        log_line(kLogTwoPlayers);
        backend_write(handle, second, 0x7fe00);
    }
    engine(kBackendClose, {handle});
}

// 0x0047b2d0: read a replay's header fields from its file
std::int32_t MmuKit::replay_load_header(Word name) {
    const std::int32_t size = static_cast<std::int32_t>(engine_value(kOpenResource, {name}));
    check(0 < size, kMsgNotFound);
    s_.note("alloc", {static_cast<Word>(size), 1, 1, 0});
    const Word buffer = static_cast<Word>(s_.value("alloc"));
    if (buffer != 0) {
        engine(kLoadResource, {buffer});
        engine(kSyncResource);
    }
    const RuntimeReplayHeaderFieldsView header(m_, buffer);
    set_global_half(m_, kReplayNumSkaters, header.num_skaters());
    set_global_byte(m_, kReplaySkaters, header.skater());
    set_global_byte(m_, kReplaySkaters + 1, header.skater2());
    set_global_half(m_, kReplayLevel, header.level());
    set_global_half(m_, kReplayGame, header.ggame());
    set_global_word(m_, kReplayFrames, header.num_frames());
    for (Word c = 0; c < 5; ++c) {
        const Word second = static_cast<Word>(header.highlight_ranges(c).end());
        set_global_word(m_, kReplayHighlightA + c * 8, static_cast<Word>(header.highlight_ranges(c).start()));
        set_global_word(m_, kReplayHighlightB + c * 8, second);
    }
    s_.note("mem_delete", {buffer});
    return static_cast<std::int16_t>(header.num_skaters());
}

// 0x0047b400: read a replay's streams (after the 0x200-byte header) and check the card's answer
bool MmuKit::replay_load_stream(Word buffer_a, Word buffer_b, Word name) {
    refresh_card_state();
    Word buffer = buffer_a;
    check(buffer_a != 0, kMsgNoLoad);
    Word offset = 0x200;
    if (!read_file(0, name, buffer, offset, 0x80000)) {
        return false;
    }
    check(multiple_of(offset, 128), kMsgLoadOffset);
    card_get_state(0, kScratchA, kScratchB);
    if (global_int(m_, kScratchA) != 3 || global_word(m_, kScratchB) != 0) {
        return false;
    }
    if (buffer_b != 0) {
        offset = 0x80000;
        buffer = buffer_b;
        if (!read_file(0, name, buffer, offset, 0x7fe00)) {
            return false;
        }
        check(multiple_of(offset, 128), kMsgLoadOffset);
        card_get_state(0, kScratchA, kScratchB);
        if (global_int(m_, kScratchA) != 3 || global_word(m_, kScratchB) != 0) {
            return false;
        }
        check(global_word(m_, kScratchB) == 0, kMsgLoadFailed);
    }
    return global_word(m_, kScratchB) == 0;
}

// 0x0047b6e0: read a whole replay file and copy its two streams out
MmuKit::Word MmuKit::replay_load_buffers(Word buffer_a, Word buffer_b, Word name) {
    const std::int32_t size = static_cast<std::int32_t>(engine_value(kOpenResource, {name}));
    check(0 < size, kMsgNotFound);
    s_.note("alloc", {static_cast<Word>(size), 1, 1, 0});
    const Word buffer = static_cast<Word>(s_.value("alloc"));
    if (buffer != 0) {
        engine(kLoadResource, {buffer});
        engine(kSyncResource);
    }
    s_.note("mem_copy", {buffer_a, buffer, 0x7fe00});
    if (buffer_b != 0) {
        s_.note("mem_copy", {buffer_b, buffer + 0x7fe00, 0x7fe00});
    }
    s_.note("mem_delete", {buffer});
    return 0;
}

// 0x0047b7d0 / 0x0047b850: hand out the name of the last file loaded / of the last park
void MmuKit::copy_loaded_file_name(Word out) {
    check(out != 0, kMsgNoReturnBuffer);
    copy_string(globals::kLoadedFileName, out);
}

void MmuKit::copy_park_name(Word out) {
    check(out != 0, kMsgNoReturnBuffer);
    copy_string(globals::kParkName, out);
}

// 0x0047b8e0: the label of a file type, the prefix of its file names
void MmuKit::type_label(Word out, std::int32_t type) {
    if (type >= 1 && type <= 3) {
        copy_string(word_at(m_, kPrefixTable[type - 1]), out);
        return;
    }
    check(false, kMsgUnrecognizedType);
}

// 0x0047b9d0 / 0x0047ba00: strcpy(to, from)
void MmuKit::copy_string(Word from, Word to) {
    const std::string text = str(from, 0x400);
    set_string(to, text);
}

// ---- the card backend ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// strncpy: the count's worth of bytes, zero past the end of the source
void MmuKit::strncpy_zero(Word to, Word from, Word count) {
    bool ended = false;
    for (Word k = 0; k < count; ++k) {
        Word c = 0;
        if (!ended) {
            c = byte_at(m_, from + k);
            ended = c == 0;
        }
        set_byte_at(m_, to + k, c);
    }
}

// 0x004e8d40: list the files that match `pattern` in the card directory into `records` (from `first`, never past `last`); the count of records goes to `count_out`
MmuKit::Word MmuKit::scan_directory(Word, Word pattern, Word records, Word count_out, std::int32_t first, std::int32_t last) {
    Word at = records + static_cast<Word>(first) * 0x94;
    for (Word k = 0; k < (static_cast<Word>(last - first) * 0x25 & 0x3fffffffu); ++k) {
        set_word_at(m_, at + 4 * k, 0);
    }
    std::int32_t remaining = last - 1;
    Word cursor = records + 0x88;
    while (remaining > 0) {
        set_word_at(m_, cursor, cursor + 12);
        cursor += 0x94;
        --remaining;
    }
    set_word_at(m_, records - 0xc + static_cast<Word>(last) * 0x94, 0);
    std::string path = str(globals::kCardDirectory) + str(globals::kPathSeparator) + str(pattern);
    set_string(kScratchPath, path);
    const std::int32_t handle = static_cast<std::int32_t>(crt_findfirst(kScratchPath, kScratchFind));
    if (handle != -1) {
        strncpy_zero(at, kScratchFind + 0x14, 0x80);
        set_word_at(m_, at + 0x84, 0x2000);
        ++first;
        if (first != last) {
            at = records + static_cast<Word>(first) * 0x94;
            do {
                if (crt_findnext(static_cast<Word>(handle), kScratchFind) != 0) {
                    break;
                }
                strncpy_zero(at, kScratchFind + 0x14, 0x80);
                set_word_at(m_, at + 0x84, 0x2000);
                ++first;
                at += 0x94;
            } while (first != last);
        }
        set_word_at(m_, count_out, static_cast<Word>(first));
        crt_findclose(static_cast<Word>(handle));
    }
    return 0;
}

// 0x004e8f00: create (empty) a file
MmuKit::Word MmuKit::create_file(Word, Word name) {
    const Word handle = engine_value(kPCcreate, {name, 0}, m_.cstring(name, 0x100));
    engine(kBackendClose, {handle});
    return 0;
}

// 0x004e8f20: remove a file (5: it could not be)
MmuKit::Word MmuKit::remove_file(Word, Word name) {
    const Word answer = engine_value(kPCremove, {name}, m_.cstring(name, 0x100));
    return answer != 0 ? 5 : 0;
}

// 0x004e8f40: open a file for the card calls that follow (0: opened)
MmuKit::Word MmuKit::open_file(Word, Word name, Word mode) {
    const Word handle = engine_value(kBackendOpen, {name, mode, 0}, m_.cstring(name, 0x100));
    set_global_word(m_, globals::kBackendHandle, handle);
    return (handle != 0xffffffffu ? 0xfffffffbu : 0u) + 5;
}

void MmuKit::close_file() {
    engine(kBackendClose, {global_word(m_, globals::kBackendHandle)});
    set_global_word(m_, globals::kBackendHandle, 0xffffffffu);
}

MmuKit::Word MmuKit::backend_read(Word handle, Word buffer, Word size) {
    engine(kBackendRead, {handle, buffer, size});
    const std::string name = pcplatform::engine_name(kBackendRead);
    // (live, the read filled the buffer itself and the one value is its answer; a replay scripts the words first)
    for (Word k = 0; !s_.live() && k < (std::min(size, kTransferLimit) + 3) / 4; ++k) {
        set_word_at(m_, buffer + 4 * k, static_cast<Word>(s_.value(name)));
    }
    return static_cast<Word>(s_.value(name));
}

MmuKit::Word MmuKit::backend_write(Word handle, Word buffer, Word size) {
    std::string bytes;
    for (Word k = 0; k < std::min(size, kTransferLimit); ++k) {
        bytes.push_back(static_cast<char>(byte_at(m_, buffer + k)));
    }
    return engine_value(kPCwrite, {handle, buffer, size}, bytes);
}

// 0x004e8f90: read `size` bytes (a multiple of 128) at `offset` of the open file
MmuKit::Word MmuKit::read_open_file(Word buffer, std::int32_t offset, Word size) {
    if (!multiple_of(size, 128)) {
        return 0;
    }
    const Word handle = global_word(m_, globals::kBackendHandle);
    if (static_cast<std::int32_t>(engine_value(kBackendSeek, {handle, static_cast<Word>(offset), 0})) != offset) {
        return 0;
    }
    if (backend_read(handle, buffer, size) == 0) {
        return 0;
    }
    set_global_byte(m_, globals::kCardFlag, 0);
    set_global_word(m_, globals::kCardState, 5);
    return 1;
}

// 0x004e9000: write `size` bytes at `offset` of the open file
MmuKit::Word MmuKit::write_open_file(Word buffer, std::int32_t offset, Word size) {
    if (!multiple_of(size, 128)) {
        return 0;
    }
    const Word handle = global_word(m_, globals::kBackendHandle);
    if (static_cast<std::int32_t>(engine_value(kBackendSeek, {handle, static_cast<Word>(offset), 0})) != offset) {
        return 0;
    }
    if (backend_write(handle, buffer, size) != size) {
        return 0;
    }
    set_global_byte(m_, globals::kCardFlag, 0);
    set_global_word(m_, globals::kCardState, 6);
    return 1;
}

// 0x004e9070 / 0x004e90d0: open, transfer, close, and leave the card "done" (state 3 after a read, 4 after a write)
bool MmuKit::read_file(Word, Word name, Word buffer, std::int32_t offset, Word size) {
    bool ok = false;
    if (open_file(0, name, 2) == 0) {
        ok = read_open_file(buffer, offset, size) != 0;
        close_file();
    }
    set_global_byte(m_, globals::kCardFlag, 0);
    set_global_word(m_, globals::kCardState, 3);
    return ok;
}

bool MmuKit::write_file(Word, Word name, Word buffer, std::int32_t offset, Word size) {
    bool ok = false;
    if (open_file(0, name, 1) == 0) {
        ok = write_open_file(buffer, offset, size) != 0;
        close_file();
    }
    set_global_byte(m_, globals::kCardFlag, 0);
    set_global_word(m_, globals::kCardState, 4);
    return ok;
}

}  // namespace opentony::runtime::mmu
