#include "skater_views.hpp"
#include "trick_database.hpp"

// Phase 4.15a: the tricks.bin loader and the trick script table parser of physics.cpp (0x00492a90..0x00492e90).

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t sext16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }

constexpr Word kScriptCursor = pcplatform::kScratchBase + 0x690;   // the local that holds the stream pointer parse_stream hands TrickScript_ReadWord
constexpr std::size_t kCommandWindow = 0x100;                      // the bytes of a command the size routine reads (the longest is a name)

}  // namespace

// 0x004bf6c0 (cdecl): the address of the command after the one at `command`. The widths are trick_command_size's (trick_database.cpp: the table of the routine's jump table, validated
// against every script of TRICKS.BIN), over the bytes at the command. Retail logs "Script Error" and goes on one byte for an unknown command; none of the file's scripts has one.
Word SkaterKit::trick_script_command_size(Word command) {
    std::size_t size = kCommandWindow;
    while (size > 1 && !m_.contains(command, size)) {
        --size;
    }
    const std::vector<std::uint8_t> window = m_.bytes(command, size);
    std::vector<std::string> notes;
    const std::size_t next = trick_command_size(window, 0, &notes);
    if (!notes.empty()) {
        throw TrickDatabaseFault("TrickScript_CommandSize: " + notes.front());
    }
    return command + static_cast<Word>(next);
}

// 0x004be3c0 (cdecl): the word at the stream pointer a local holds; the pointer moves on by two
Word SkaterKit::trick_script_read_word(Word cursor) {
    const Word at = word_at(m_, cursor);
    const Word value = half_at(m_, at);
    set_word_at(m_, cursor, at + 2);
    return value;
}

// 0x00492a90: loads tricks.bin once (a second call does nothing and answers 0). The file starts with seven shorts, each the offset from the start of the file of a table the game keeps a pointer to.
Word SkaterKit::tricks_load_binary() {
    if (global_word(m_, globals::kTricksBinary) != 0) {
        return 0;
    }
    log_line(0x00536c58);
    const Word base = engine_value(kOpenLoad, {0x00536c4c, 1});
    set_global_word(m_, globals::kTricksBinary, base);
    static constexpr Word kTables[7] = {globals::kBinaryHeader0, globals::kGrindScriptOffsets, globals::kStateScriptOffsets, globals::kPlayerOneTable, globals::kPlayerTwoTable, globals::kBinaryTable5,
                                        globals::kBinaryTable6};
    for (Word k = 0; k < 7; ++k) {
        set_word_at(m_, kTables[k], u(sext16(half_at(m_, base + 2 * k))) + base);
    }
    return 1;
}

// 0x00492b20
void SkaterKit::unload_gtricks() {
    if (global_word(m_, globals::kTricksBinary) == 0) {
        return;
    }
    log_line(0x00536c6c);
    engine(kMemDelete, {global_word(m_, globals::kTricksBinary)});
    set_global_word(m_, globals::kTricksBinary, 0);
    set_global_word(m_, globals::kBinaryTable5, 0);
}

// 0x00492b60: the extra animations start again behind the fixed list: the cursor is one past the fixed list's end mark and the new end mark is written there
void SkaterKit::reset_extra_animations() {
    Word at = globals::kAnimationList;
    if (half_at(m_, at) != 0xffff) {
        do {
            at += 2;
            set_global_word(m_, globals::kExtraCursor, at);
        } while (half_at(m_, at) != 0xffff);
    }
    at += 2;
    set_global_word(m_, globals::kExtraCursor, at);
    set_half_at(m_, at, 0xffff);
    set_global_word(m_, globals::kExtraCount, 0);
}

// 0x00492ba0: add an animation to the extra list (at most 300; an assertion says so)
void SkaterKit::register_animation(Word animation) {
    constexpr Word kMsgAnimation = 0x00536c98;
    constexpr Word kMsgTooMany = 0x00536c80;
    const std::int32_t number = sext16(animation);
    check(number >= 0 && number <= 500, kMsgAnimation);
    const std::int32_t count = global_int(m_, globals::kExtraCount) + 1;
    set_global_word(m_, globals::kExtraCount, u(count));
    check(count < 300, kMsgTooMany);
    Word at = global_word(m_, globals::kExtraCursor);
    set_half_at(m_, at, animation);
    at = global_word(m_, globals::kExtraCursor) + 2;
    set_global_word(m_, globals::kExtraCursor, at);
    set_half_at(m_, at, 0xffff);
}

// 0x00492c70: is an animation in the fixed list or in the extra list that follows it
Word SkaterKit::animation_in_lists(Word animation) {
    constexpr Word kMsgAnimation = 0x00536c98;
    const std::int16_t wanted = static_cast<std::int16_t>(animation);
    check(wanted >= 0 && wanted <= 500, kMsgAnimation);
    Word at = globals::kAnimationList;
    for (int list = 0; list < 2; ++list) {
        while (half_at(m_, at) != 0xffff) {
            if (static_cast<std::int16_t>(half_at(m_, at)) == wanted) {
                return 1;
            }
            at += 2;
        }
        at += 2;
    }
    return 0;
}

// 0x00492d10: walks a script stream; every "animation" command (1) registers the animation it names, the command sizes come from the interpreter's size routine; command 7 ends the stream
void SkaterKit::parse_stream(Word stream) {
    Word command = stream;
    while (byte_at(m_, command) != 7) {
        if (byte_at(m_, command) == 1) {
            // (the reader gets the address of a local that holds the stream pointer: the replay records what the local holds, live the local is in the scratch window)
            if (s_.live()) {
                set_global_word(m_, kScriptCursor, command + 1);
            }
            register_animation(engine_value(kScriptReadWord, {s_.live() ? kScriptCursor : command + 1}));
        }
        command = engine_value(kScriptCommandSize, {command});
    }
}

// 0x00492d50: a table of records (a button count of 1..4, that many buttons, the offset of a stream, one more word), ended by a count of 0; each record's stream is parsed
void SkaterKit::parse_table(Word table) {
    constexpr Word kMsgButtons = 0x00536cac;
    std::uint16_t count = half_at(m_, table);
    Word at = table;
    while (count != 0) {
        const std::int16_t buttons = static_cast<std::int16_t>(count);
        check(buttons > 0 && buttons < 5, kMsgButtons);
        at += 2;
        const Word offset = u(sext16(half_at(m_, at + 2 * static_cast<Word>(buttons))));
        at += 2 * static_cast<Word>(buttons);
        parse_stream(offset + global_word(m_, globals::kTricksBinary));
        count = half_at(m_, at + 4);
        at += 4;
    }
}

// 0x00492de0: the tables of the trick scripts: load the file, make the extra list empty, let the runtime build the player tables, parse player one's table (and player two's when two play); the file
// is dropped again when this call loaded it
void SkaterKit::trick_script_initialize() {
    constexpr Word kMsgSkater = 0x00536cd8;
    const Word loaded = tricks_load_binary();
    bool valid = false;
    if (s(engine_value(kSelectManagerSlot, {0})) >= 0) {
        valid = s(engine_value(kSelectManagerSlot, {0})) <= 0x14;
    }
    engine_value(kSelectManagerSlot, {0});
    check(valid, kMsgSkater);
    reset_extra_animations();
    engine(kBuildPlayerTables);
    parse_table(global_word(m_, globals::kPlayerOneTable));
    if (global_word(m_, globals::kNumberOfPlayers) == 2) {
        parse_table(global_word(m_, globals::kPlayerTwoTable));
    }
    if (loaded != 0) {
        unload_gtricks();
    }
}

}  // namespace opentony::runtime::skaterkit
