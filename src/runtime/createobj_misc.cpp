#include "auto_views.hpp"
#include "createobj_kit.hpp"
#include "trick_database.hpp"
#include "trick_tips.hpp"

namespace opentony::runtime::createobj {

using namespace opentony::runtime::autoviews;

using Word = CreateObjKit::Word;

namespace {

constexpr Word kOutX = kScratch + 0x20;          // the two words Panel_ViewportOffset fills (locals of the caller)
constexpr Word kOutY = kScratch + 0x24;
constexpr Word kViewportMode = 0x0056a928;
constexpr Word kVersusPlayers = 0x0056db5c;      // the pair of players the versus events refer to
constexpr Word kVersusSelector = 0x0056db64;
constexpr Word kPlayer = 0x0056a858;
constexpr Word kPlayerTwo = 0x0056a85c;
constexpr Word kCareerRecords = 0x00568a6c;      // skatermanager: 0x104 bytes a character
constexpr Word kTrickTableOne = 0x00568a50;      // PlayerOneTrickScriptTable
constexpr Word kTrickTableTwo = 0x00568a4c;
constexpr Word kPlayerCount = 0x00533f3c;        // GNumberOfPlayers
constexpr Word kSingletonAddress = 0x0056db98;

constexpr Word kIsRemotePlayer = 0x004de010;     // NetGame_IsRemotePlayer(player)
constexpr Word kViewportOffset = 0x0048c760;     // Panel_ViewportOffset(mode, &x, &y)
constexpr Word kAddMessage = 0x004736f0;         // Mess_AddMessage(message, format, 0, x, y)
constexpr Word kMessageFormat = 0x00534f68;
constexpr Word kTricksInitialize = 0x004bb4f0;   // Tricks_InitializeManager() on the storage
constexpr Word kBuildTable = 0x004bcf00;         // TrickTable_BuildForPlayer(player, table) on the manager
constexpr Word kUnloadTricks = 0x00492b20;       // UnloadGTricks()
constexpr Word kLoadTricks = 0x00492a90;         // Tricks_LoadBinary(): 1 when it loaded TRICKS.BIN just now
constexpr Word kTricksImage = 0x0056a894;        // the loaded TRICKS.BIN
constexpr std::size_t kTricksImageSize = 33168;
constexpr Word kSkaterManager = 0x0056c580;      // the skater manager object every slot query is a method of
constexpr Word kSection0 = 0x00568688;           // TRICKS.BIN section 0: one s16 key stream offset a character
constexpr Word kSection6 = 0x0056a850;           // section 6: the alternate key stream
constexpr Word kAlternateBlock = 0x0056a690;     // the alternate binding block
constexpr Word kAssertionLine = 0x00568638;      // the assertion macro's line and file name (0x0054108c: the source file)
constexpr Word kAssertionFile = 0x0056881c;
constexpr Word kSourceFile = 0x0054108c;
constexpr Word kMsgNoTable = 0x00541454;
constexpr Word kMsgTableSize = 0x00541420;

}  // namespace

// ---- the record bank --------------------------------------------------------------------------------------------------------------------------------------

// 0x004b0b50 (fastcall): eight empty triples and twenty empty words
Word CreateObjKit::record_bank_construct(Word self) {
    set_word_at(m_, self, kVtRecordBank);
    for (Word i = 0; i < 8; ++i) {
        set_word_at(m_, self + 4 + 12 * i, 0);
        set_word_at(m_, self + 8 + 12 * i, 0);
        set_word_at(m_, self + 12 + 12 * i, 0);
    }
    for (Word i = 0; i < 0x14; ++i) {
        set_word_at(m_, self + 0x68 + 4 * i, 0);
    }
    set_word_at(m_, self + 0x64, 0);
    return self;
}

// 0x004b0bb0 (fastcall)
void CreateObjKit::record_bank_destruct(Word self) { set_word_at(m_, self, kVtBase); }

// 0x004b0b90 (thiscall, one argument)
Word CreateObjKit::record_bank_scalar_delete(Word self, Word flags) {
    record_bank_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// ---- the singleton at 0x0056db98 --------------------------------------------------------------------------------------------------------------------------

// 0x004c43f0 (fastcall)
Word CreateObjKit::singleton_construct(Word self) {
    set_word_at(m_, self, kVtSingleton);
    return self;
}

// 0x004c43b0: the global object is built
Word CreateObjKit::singleton_construct_global() { return singleton_construct(kSingletonAddress); }

// 0x004c43e0 (fastcall)
void CreateObjKit::singleton_destruct(Word self) { set_word_at(m_, self, kVtBase); }

// 0x004c4400 (thiscall, one argument)
Word CreateObjKit::singleton_scalar_delete(Word self, Word flags) {
    singleton_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// ---- strings and small helpers ----------------------------------------------------------------------------------------------------------------------------

// 0x004b8340 (cdecl): the letters A..Z of the string become lower case
void CreateObjKit::string_to_lower(Word text) {
    for (Word at = text; byte_at(m_, at) != 0; ++at) {
        std::uint8_t c = byte_at(m_, at);
        if (static_cast<std::int8_t>(c) > '@' && static_cast<std::int8_t>(c) < '[') {
            c = static_cast<std::uint8_t>(c + ' ');
        }
        set_byte_at(m_, at, c);
    }
}

// 0x004b8360 (cdecl): a space or a tab
Word CreateObjKit::is_blank(Word text) { return byte_at(m_, text) == ' ' || byte_at(m_, text) == '\t' ? 1u : 0u; }

// 0x004b8380 (cdecl): the versus events refer to the two players
void CreateObjKit::select_versus_players() {
    set_global_word(m_, kVersusPlayers, global_word(m_, kPlayer));
    set_global_word(m_, kVersusPlayers + 4, global_word(m_, kPlayerTwo));
    set_global_word(m_, kVersusSelector, 0);
}

// 0x004b83b0 (cdecl, two arguments): a message at the player's place in the viewport, unless the player is on the other machine of a network game
void CreateObjKit::add_viewport_message(Word player, Word message) {
    if ((engine_value(kIsRemotePlayer, {player}) & 0xff) != 0) {
        return;
    }
    engine(kViewportOffset, {half_at(m_, player + 0x170) + global_word(m_, kViewportMode) * 2, kOutX, kOutY});
    if (!s_.live()) {
        set_global_word(m_, kOutX, static_cast<Word>(s_.value("engine_0048c760")));
        set_global_word(m_, kOutY, static_cast<Word>(s_.value("engine_0048c760")));
    }
    engine(kAddMessage, {message, kMessageFormat, 0, global_word(m_, kOutX), global_word(m_, kOutY)});
}

// ---- the tricks -------------------------------------------------------------------------------------------------------------------------------------------

// 0x00416050 (cdecl): the career record of a character
Word CreateObjKit::find_trick_config(Word character) { return kCareerRecords + character * 0x104; }

// 0x004bb4f0 (thiscall; milestone 9: the cold boot's CustomSkaters_InitAll is its first caller in a standalone run): the trick database of TRICKS.BIN in the
// manager. Its vtable, no record yet (+0x1404), whether it loaded the file just now (+0x1408: Tricks_LoadBinary's answer, so the destructor unloads only what it
// loaded), then every trick of the GAllTricks section added or merged as TrickDatabase::build does it (Phase 4's parse of 0x004bb4f0 and TrickDatabase_AddOrMerge,
// trick_database.cpp), kept as AddOrMerge keeps a record: at +4 + 0x28 * index the name copied with its NUL, the script offset (+0x20), the cost (+0x22), the
// class flags (+0x24) and the descriptor (+0x26); then the count. Retail lowercases each name inside the loaded file, which build() does on its copy: the copy
// is written back. The checks of the section's shape are build()'s faults (retail asserts and goes on).
Word CreateObjKit::trick_manager_construct(Word self) {
    createobj_misc_trick_at(m_, self).set_field_0(static_cast<Word>(kVtTrickManager));
    createobj_misc_trick_at(m_, self).set_field_1404(0);
    createobj_misc_trick_at(m_, self).set_field_1408(static_cast<Word>(engine_value(kLoadTricks)));
    const Word binary = global_word(m_, kTricksImage);
    std::vector<std::uint8_t> image = m_.bytes(binary, kTricksImageSize);
    const TrickDatabase db = TrickDatabase::build(image);
    m_.write(binary, image);
    Word record = self + 4;
    for (const TrickRecord& trick : db.records()) {
        m_.write(record, {reinterpret_cast<const std::uint8_t*>(trick.name.c_str()), trick.name.size() + 1});
        set_half_at(m_, record + 0x20, trick.source);
        set_half_at(m_, record + 0x22, trick.cost);
        set_half_at(m_, record + 0x24, trick.flags);
        set_half_at(m_, record + 0x26, trick.descriptor);
        record += kTrickRecordSize;
    }
    createobj_misc_trick_at(m_, self).set_field_1404(static_cast<Word>(static_cast<Word>(db.count())));
    return self;
}

// 0x004bb790 (fastcall): the manager unloads the trick scripts it loaded
void CreateObjKit::trick_manager_destruct(Word self) {
    createobj_misc_trick_at(m_, self).set_field_0(static_cast<Word>(kVtTrickManager));
    if (createobj_misc_trick_at(m_, self).field_1408() != 0) {
        engine(kUnloadTricks);
    }
    createobj_misc_trick_at(m_, self).set_field_0(static_cast<Word>(kVtBase));
}

// 0x004bb770 (thiscall, one argument)
Word CreateObjKit::trick_manager_scalar_delete(Word self, Word flags) {
    trick_manager_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004c4570 (cdecl): the level start clears the tip monitor's counters (trick_tips_reset, trick_tips.cpp: the words and the flag byte the routine stores, replayed against retail's before
// and after images of the monitor object at 0x0056db98), here over the object in memory
void CreateObjKit::tips_reset() {
    TrickTipState state;
    state.object.bytes() = m_.bytes(kTipMonitorAddress, kTipMonitorSize);
    trick_tips_reset(state);
    m_.write(kTipMonitorAddress, state.object.bytes());
}

// 0x004bcf00 (thiscall on the manager, two arguments): the trick script table of a player, written at `table`. The key stream and the binding block are the loaded
// skater's (the slot's record when the slot holds a skater loaded from a file: the mapped index of set 0 and the loaded block) or the character's own (the career record's
// block at +0xcc), and the alternate pair for character 0 in costume 2. The table is trick_table_build_for_player (trick_database.cpp: the four passes of 0x004bcb70 /
// 0x004bcc90 and the specials of 0x004bcdd0, each a function of the key stream, the block and the database) over the loaded file; the database is built again from it as
// Career::initialize_trick_config does. Retail asserts when a key stream or the table is malformed and goes on; the model's faults and notes are stopped here.
// The retail function ends by checking the size of the table against 0x400 bytes; its operands are the wrong way round (start minus end), so the check holds for any table.
void CreateObjKit::trick_table_build(Word self, Word player, Word table) {
    (void)self;
    set_global_word(m_, kAssertionLine, 0x4bf);
    copy_string(kAssertionFile, kSourceFile);
    s_.check(table != 0, kMsgNoTable);
    const Word image = global_word(m_, kTricksImage);
    const auto stream_of = [&](Word index) { return image + u(static_cast<std::int16_t>(half_at(m_, global_word(m_, kSection0) + index * 2))); };
    Word stream = 0;
    Word block = 0;
    if (method(0x004b1430, kSkaterManager, {player}) != 0) {
        const Word index = engine_value(0x004b1980, {0});
        block = method(0x004b1570, kSkaterManager, {player});
        stream = stream_of(index);
    } else {
        const Word character = method(0x004b0c60, kSkaterManager, {player});
        block = engine_value(0x00416050, {character}) + 0xcc;
        stream = stream_of(character);
        if (character == 0 && method(0x004b0cd0, kSkaterManager, {player}) == 2) {
            stream = global_word(m_, kSection6);
            block = kAlternateBlock;
        }
    }
    std::vector<std::uint8_t> file = m_.bytes(image, kTricksImageSize);
    const TrickDatabase db = TrickDatabase::build(file);
    const std::size_t notes = db.diagnostics().size();
    const std::vector<std::uint16_t> words = trick_table_build_for_player(db, file, stream - image, m_.bytes(block, kBindingBlockSize));
    if (db.diagnostics().size() != notes) {
        throw TrickDatabaseFault("TrickTable_BuildForPlayer: " + db.diagnostics()[notes]);
    }
    for (std::size_t i = 0; i < words.size(); ++i) {
        set_half_at(m_, table + u(static_cast<std::int32_t>(2 * i)), words[i]);
    }
    set_global_word(m_, kAssertionLine, 0x537);
    copy_string(kAssertionFile, kSourceFile);
    const Word end = table + u(static_cast<std::int32_t>(2 * (words.size() - 1)));
    Word size = table - end - 2;
    size = (size & ~0xffu) | (size & 0xfeu);
    s_.check(s(size) < 0x400, kMsgTableSize);
}

// 0x004bd1e0 (cdecl): the trick script tables of the players (the second in a two-player game), built with a manager that exists only for the call
void CreateObjKit::build_player_tables() {
    const Word block = operator_new(0x140c);
    const Word manager = block != 0 ? method(kTricksInitialize, block) : 0;
    method(kBuildTable, manager, {0, global_word(m_, kTrickTableOne)});
    if (global_word(m_, kPlayerCount) == 2) {
        method(kBuildTable, manager, {1, global_word(m_, kTrickTableTwo)});
    }
    if (manager != 0) {
        delete_object(manager);
    }
}

}  // namespace opentony::runtime::createobj
