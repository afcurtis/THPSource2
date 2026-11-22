#include "auto_views.hpp"
#include "front.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kManager = 0x0056c580;              // the skater manager (the this of its methods)
constexpr std::uint32_t kSelectionRecords = 0x0053a514;     // the character table's flags dword (bit 1: a custom skater)
constexpr std::uint32_t kCharacterStride = 0x170;
constexpr std::uint32_t kReplayCount = 0x0056b8d4;          // the replay header: the number of skaters (negative while their selection is swapped out)
constexpr std::uint32_t kReplayCharacter = 0x0056b8d6;      // per skater: the character the replay was recorded with
constexpr std::uint32_t kReplayCostume = 0x0056b8d8;        // per skater: the costume
constexpr std::uint32_t kReplayData = 0x0056b928;           // per skater: 0x100 bytes (the custom skater's info)
constexpr std::uint32_t kCustomInfo = 0x0056a488;           // per custom slot: 0x80 bytes
constexpr std::uint32_t kRegistry = 0x0055faf8;             // per skater: the character slot the skater data below was built for

// the shell's state (the statics at 0x00561c6c..0x00561ce0)
constexpr std::uint32_t kScreen = 0x00561c78;               // which shell screen runs next
constexpr std::uint32_t kMode = 0x00561c74;                 // the game mode the screens pass on
constexpr std::uint32_t kTwoPlayers = 0x00561c70;
constexpr std::uint32_t kLevel = 0x00561c90;
constexpr std::uint32_t kQuit = 0x00561cbd;
constexpr std::uint32_t kEditorTest = 0x00561ca8;           // a park under test from the editor
constexpr std::uint32_t kPark = 0x005614c4;
constexpr std::uint32_t kDatabase = 0x00561cb4;             // the cached texture-set database
constexpr std::uint32_t kVolume = 0x00561c94;
constexpr std::uint32_t kFade = 0x00561c84;
constexpr std::uint32_t kMusic = 0x0052cb38;                // the shell music's handle (-1: none)
constexpr std::uint32_t kPre = 0x0056b834;                  // the .pre file manager
constexpr std::uint32_t kLevelName = 0x006a0548;
constexpr std::uint32_t kStartScreen = 0x00561b9c;

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s8(std::uint32_t v) { return static_cast<std::int8_t>(v & 0xff); }

// where each stack local of FrontEnd_Main lives in the port
constexpr std::uint32_t kLocals = kScratchLine + 0x80;

// the level names the play screen puts in the log (indexed by level; 10 to 12 and above 13 have none)
constexpr std::uint32_t kLevelNames[10] = {0x0052cc14, 0x0052cc0c, 0x0052cc08, 0x0052cc04, 0x0052cbfc, 0x0052cbf8, 0x0052cbf0, 0x0052cbe4, 0x0052cbdc, 0x0052cbd4};

}  // namespace

// 0x004ac3f0: the replay header is cleared: no skaters, no characters, no level or mode
void FrontScreens::clear_gvideo_restart_data() {
    set_global_half(m_, kReplayCount, 0);
    set_global_byte(m_, kReplayCharacter, 0xff);
    set_global_byte(m_, kReplayCharacter + 1, 0xff);
    set_global_half(m_, 0x0056b8da, 0xffff);
    set_global_half(m_, 0x0056b8dc, 0xffff);
}

// 0x00452e40: which of the four unlockable character groups are open (one byte each)
void FrontScreens::get_unlock_flags(std::uint32_t out) {
    check(out != 0, 0x0055fe10);
    set_byte_at(m_, out, engine_value(0x00416fb0, {0x100}));
    set_byte_at(m_, out + 1, engine_value(0x00416fb0, {0x80}));
    set_byte_at(m_, out + 2, engine_value(0x00416fb0, {0x40}));
    set_byte_at(m_, out + 3, engine_value(0x00416fb0, {0x200}));
}

// 0x00452ec0: after a screen the unlock state may have changed (a cheat): if a group opened or closed since `old` was taken, the skater selection moves off a character that is
// no longer there. Only the first group that changed is handled.
void FrontScreens::reselect_after_unlock_change(std::uint32_t old) {
    check(old != 0, 0x0055fe10);
    scratch_line({});
    const std::uint32_t fresh = kLocals + 0x40;
    get_unlock_flags(fresh);
    auto each_character = [&](std::uint32_t bits) {
        for (std::uint32_t i = 0; i < 20; ++i) {
            if ((word_at(m_, kSelectionRecords + i * kCharacterStride) & bits) != 0) {
                engine(0x004b0e60, {kManager, 0, i});
            }
        }
    };
    if (byte_at(m_, fresh) != byte_at(m_, old)) {
        each_character(0x100);
    } else if (byte_at(m_, fresh + 1) != byte_at(m_, old + 1)) {
        each_character(0x80);
    } else if (byte_at(m_, fresh + 2) != byte_at(m_, old + 2)) {
        each_character(0x40);
    } else if (byte_at(m_, fresh + 3) != byte_at(m_, old + 3)) {
        engine(0x004b0e60, {kManager, 0, 0});
        engine(0x004b0fe0, {kManager, 0, 2, 1});
    }
}

// 0x00454130: a replay starts: each skater of the header takes the replay's character and costume (and custom skater info), and the header keeps the player's own selection
void FrontScreens::replay_swap_in() {
    const std::int16_t count = global_short(m_, kReplayCount);
    check(count >= 1 && count <= 2, 0x0052cea0);
    for (std::int32_t i = 0; i < global_short(m_, kReplayCount); ++i) {
        const std::uint32_t data = kReplayData + 0x100 * u(i);
        engine(0x004b0bc0, {kManager, u(i)});
        const std::uint32_t previous_character = engine_value(0x004b0c30, {kManager});
        const std::int32_t character = s8(byte_at(m_, kReplayCharacter + u(i)));
        const std::int32_t costume = s8(byte_at(m_, kReplayCostume + u(i)));
        engine(0x004b0d40, {kManager, u(character)});
        const std::uint32_t previous_costume = engine_value(0x004b0c40, {kManager});
        engine(0x004b0fe0, {kManager, u(i), u(costume), 0});
        if ((byte_at(m_, kSelectionRecords + u(character) * kCharacterStride) & 2) != 0) {
            const std::uint32_t slot = engine_value(0x004b1810, {u(character)});
            engine(0x0046f3e0, {kCustomInfo + (slot << 7), data, 0x80});
            check(word_at(m_, kCustomInfo + (slot << 7) + 0x64) != word_at(m_, kCustomInfo + (slot << 7) + 0x68), 0x0052ce70);
        }
        set_byte_at(m_, kReplayCharacter + u(i), previous_character);
        set_byte_at(m_, kReplayCostume + u(i), previous_costume);
    }
}

// 0x00454300: the replay ends: the selection the header kept comes back (the count is negative while it is swapped out)
void FrontScreens::replay_swap_out() {
    const std::int32_t count = -static_cast<std::int32_t>(global_short(m_, kReplayCount));
    check(count >= 1 && count <= 2, 0x0052cea0);
    if (-static_cast<std::int32_t>(global_short(m_, kReplayCount)) <= 0) {
        return;
    }
    std::int32_t i = 0;
    do {
        const std::uint32_t data = kReplayData + 0x100 * u(i);
        engine(0x004b0bc0, {kManager, u(i)});
        const std::uint32_t previous_character = engine_value(0x004b0c30, {kManager});
        const std::uint32_t replay_character = u(s8(byte_at(m_, kReplayCharacter + u(i))));
        const std::uint32_t replay_costume = u(s8(byte_at(m_, kReplayCostume + u(i))));
        engine(0x004b0d40, {kManager, previous_character});
        const std::uint32_t previous_costume = engine_value(0x004b0c40, {kManager});
        engine(0x004b0fe0, {kManager, u(i), replay_costume, 0});
        if ((byte_at(m_, kSelectionRecords + previous_character * kCharacterStride) & 2) != 0) {
            const std::uint32_t slot = engine_value(0x004b1810, {previous_character});
            engine(0x0046f3e0, {kCustomInfo + (slot << 7), data, 0x80});
        }
        set_byte_at(m_, kReplayCharacter + u(i), previous_character);
        set_byte_at(m_, kReplayCostume + u(i), previous_costume);
        engine(0x004b0d40, {kManager, replay_character});
        ++i;
    } while (i < -static_cast<std::int32_t>(global_short(m_, kReplayCount)));
}

// 0x00454710: the shell's music starts at the volume the shell keeps (full volume scaled by the fade)
void FrontScreens::start_shell_music() {
    // (the compiler's divide by 255: the high word of the signed product with 0x80808081, plus the dividend, shifted, sign-corrected, then halved)
    const std::int32_t scaled = static_cast<std::int32_t>(global_word(m_, 0x0052be48) * global_word(m_, kFade));
    std::int32_t quotient = static_cast<std::int32_t>((static_cast<std::int64_t>(static_cast<std::int32_t>(0x80808081)) * scaled) >> 32);
    quotient = static_cast<std::int32_t>(static_cast<std::uint32_t>(quotient) + static_cast<std::uint32_t>(scaled)) >> 7;
    quotient += static_cast<std::int32_t>(static_cast<std::uint32_t>(quotient) >> 31);
    quotient >>= 1;
    const std::uint32_t saved = global_word(m_, 0x0052be44);
    set_global_word(m_, 0x0052be44, 0x3fff);
    s_.note(kSvcSfx, {0x9d, u(quotient), 0});
    const std::uint32_t handle = u(s_.value(kSvcSfx));
    set_global_word(m_, 0x0052be44, saved);
    set_global_word(m_, kMusic, handle);
}

// 0x00454760: the shell's skater data follows the character slot the manager has selected for each of the two skaters
void FrontScreens::sync_skater_slots() {
    for (std::uint32_t i = 0; i < 2; ++i) {
        const std::uint32_t slot = engine_value(0x004b0c60, {kManager, i});
        if (byte_at(m_, kRegistry + i) != (slot & 0xff)) {
            set_byte_at(m_, kRegistry + i, slot);
            const std::uint32_t a = byte_at(m_, 0x0055fb0e + slot);
            set_byte_at(m_, 0x0055fb38 + i, a);
            set_byte_at(m_, 0x0056a688 + i, a);
            const std::uint32_t b = byte_at(m_, 0x0055fafa + slot);
            set_byte_at(m_, 0x0055fb36 + i, b);
            const std::uint32_t c = byte_at(m_, 0x0055fb22 + slot);
            set_byte_at(m_, 0x0056a2dc + i, b);
            set_byte_at(m_, 0x0055fb3a + i, c);
            set_byte_at(m_, 0x0056a2e0 + i, c);
        } else {
            const std::uint32_t a = byte_at(m_, 0x0055fb38 + i);
            const std::uint32_t b = byte_at(m_, 0x0055fb36 + i);
            const std::uint32_t c = byte_at(m_, 0x0055fb3a + i);
            set_byte_at(m_, 0x0056a688 + i, a);
            set_byte_at(m_, 0x0056a2dc + i, b);
            set_byte_at(m_, 0x0056a2e0 + i, c);
        }
    }
}

// 0x004547f0: the picture file of a texture-set deck (set = the character, deck 0..7; a custom skater has twelve more at 0x0053c078)
std::uint32_t FrontScreens::texture_set_entry(std::uint32_t set, std::uint32_t deck) {
    check(static_cast<std::int32_t>(set) >= 0 && static_cast<std::int32_t>(set) < 20, 0x0052cf24);
    const std::uint32_t row = set * kCharacterStride;
    if ((byte_at(m_, kSelectionRecords + row) & 2) == 0) {
        check(static_cast<std::int32_t>(deck) >= 0 && static_cast<std::int32_t>(deck) < 8, 0x0052cf10);
        return 0x0053a3fc + (deck << 5) + row;
    }
    check(static_cast<std::int32_t>(deck) >= 0 && static_cast<std::int32_t>(deck) < 20, 0x0052cf10);
    if (static_cast<std::int32_t>(deck) < 8) {
        return 0x0053a3fc + (deck << 5) + row;
    }
    return 0x0053c078 + ((deck - 8) << 5);
}

// 0x004544a0: launch a level: the replay (if one is playing) overrides the level and mode, the players and the pad are reset, the game runs (LaunchGame returns when the
// level ends) and the shell's own state is rebuilt
void FrontScreens::launch_game_level(std::uint32_t level, std::uint32_t mode) {
    set_global_word(m_, 0x005361d0, 0xffffffff);
    bool replay = false;
    s_.note(kSvcPrintfLog, {0x0052cee8});
    if (replay_playing()) {
        check(global_byte(m_, kPark) == 0, 0x0052cec4);
        replay_swap_in();
        level = u(static_cast<std::int16_t>(global_half(m_, 0x0056b8da)));
        mode = u(static_cast<std::int16_t>(global_half(m_, 0x0056b8dc)));
        replay = true;
    }
    engine(0x004a8f90);
    engine(0x004b5190);
    set_global_byte(m_, 0x0056aff9, 0);
    set_global_byte(m_, 0x0056b165, 0);
    set_global_byte(m_, 0x0056aff8, 0);
    set_global_byte(m_, 0x0056b164, 0);
    set_global_word(m_, 0x005618cc, 0);
    set_global_word(m_, front::kGameMode, mode);
    set_global_word(m_, front::kPlayerCount, (mode == 1 || mode == 2 || mode == 3 || mode == 10) ? 1 : 2);
    if (global_byte(m_, kPark) != 0) {
        set_global_word(m_, 0x0056dd58, 0x005283d0);
    } else {
        const std::int32_t current = static_cast<std::int32_t>(global_word(m_, front::kCurrentLevel));
        if (current >= 10 && current != 13) {
            set_global_word(m_, 0x0056dd58, 0x00546110);
        } else {
            set_global_word(m_, 0x0056dd58, 0x00541990);
            engine(0x004327a0);
        }
    }
    set_global_word(m_, 0x0056a880, 0);
    if (replay_level_flag()) {
        engine(0x004195e0);
    }
    engine(0x0046a8d0, {level});
    // (the level leaves the shell's next-screen request: a replay scripts it; live, the front end wrote it in memory while the level ran ("Go there now", Level Select, ...) and retail
    // never stores the launcher's return value, so a live run must not overwrite it: it sent every one of those requests to the main menu)
    if (!s_.live()) {
        set_global_word(m_, kStartScreen, u(s_.value(engine_name(0x0046a8d0))));
    }
    set_global_word(m_, 0x0056a880, 1);
    if (global_byte(m_, kPark) != 0) {
        engine(0x004b32f0, {global_word(m_, 0x005613bc), 0, 0});
    }
    engine(0x004586f0, {0, 0x10});
    engine(0x004301b0);
    if (global_word(m_, 0x005614bc) == 0) {
        engine(0x004326a0);
        s_.note(kSvcSprintf, {0x0053d7b4, global_word(m_, 0x0052eb20)}, m_.cstring(global_word(m_, 0x0052eb20), 0x40));
    }
    engine(0x004a8ee0);
    engine(0x004b50e0);
    engine(0x004ad020, {0x0052ce64}, m_.cstring(0x0052ce64, 0x20));
    if (global_byte(m_, kEditorTest) != 0) {
        engine(0x004302c0, {0x18a88, 1, 1, 1});
    } else {
        engine(0x004302c0, {0x9c40, 1, 0, 0});
    }
    const std::uint32_t memory = operator_new(0x6a10);
    set_global_word(m_, kDatabase, memory != 0 ? engine_value(0x0041db50, {memory}) : 0);
    set_global_word(m_, kFade, 0);
    set_global_word(m_, kVolume, 0x3fff);
    if (global_word(m_, kMusic) != 0xffffffffu) {
        engine(0x004ad9f0, {global_word(m_, kMusic)});
        set_global_word(m_, kMusic, 0xffffffffu);
    }
    start_shell_music();
    if (replay) {
        replay_swap_out();
    }
}

}  // namespace opentony::runtime::frontend
