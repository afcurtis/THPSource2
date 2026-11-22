#include "auto_views.hpp"
#include "front.hpp"
#include "host_options.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

constexpr std::uint32_t kSkater = 0x0056a960;               // the skater object whose statistics the end screens show (the HUD's)
constexpr std::uint32_t kPlayerLevelFlag = 0x005614c4;      // set in the park editor's levels (the high-score table of level 13)
constexpr std::uint32_t kHighScores = 0x00569ec0;           // eight entries of 8 bytes per level, 0x40 per level
constexpr std::uint32_t kLevelRecords = 0x0053c1f8;         // the level table (0x1ac bytes per level; the name first)
constexpr std::uint32_t kNameRecords = 0x0053a3f8;          // the character table (0x170 bytes per character; a name pointer first)
constexpr std::uint32_t kSlideA = 0x00561918;               // the five columns that slide in from the left
constexpr std::uint32_t kSlideB = 0x00561710;               // and from the right
constexpr std::uint32_t kLineBuffer = 0x00561b30;
constexpr std::uint32_t kLineBuffer2 = 0x005616fc;
constexpr std::uint32_t kLineBuffer3 = 0x005619cc;
constexpr std::uint32_t kComboBuffer = 0x005619a4;
constexpr std::uint32_t kTitleBuffer = 0x005618d8;
constexpr std::uint32_t kTimeBuffer = 0x005616c0;           // the sound levels menu's three lines
constexpr std::uint32_t kMusicBuffer = 0x0056175c;
constexpr std::uint32_t kTrackBuffer = 0x00561734;
constexpr std::uint32_t kHorseWord = 0x0056a478;            // the word the horse game plays for (its length is the word before it)
constexpr std::uint32_t kHorseLength = 0x0056a474;
constexpr std::uint32_t kHorseCounts = 0x0056174c;          // the letters each player has (two dwords)
constexpr std::uint32_t kHorseTurn = 0x00561774;
constexpr std::uint32_t kHorseState = 0x00561998;
constexpr std::uint32_t kHorseRestart = 0x00561938;
constexpr std::uint32_t kHorseSlide = 0x00561b2c;
constexpr std::uint32_t kHorseCapture = 0x00561be8;
constexpr std::uint32_t kHorseTarget = 0x00568660;
constexpr std::uint32_t kHorseMatch = 0x00568680;
constexpr std::uint32_t kHorseWho = 0x0056a854;
constexpr std::uint32_t kHorseRestartIndex = 0x0056867c;
constexpr std::uint32_t kHorseSaved = 0x00568a60;
constexpr std::uint32_t kHorseSavedB = 0x00568a64;
constexpr std::uint32_t kHorseRestarts = 0x0056e208;
constexpr std::uint32_t kNameEntry = 0x00561bf8;
constexpr std::uint32_t kScoreAnchor = 0x0052bf34;          // the high-score row being edited (-1 for none)
constexpr std::uint32_t kScoreCursor = 0x00561bf4;
constexpr std::uint32_t kScoreFlash = 0x0056bb3c;
constexpr std::uint32_t kFrameCounter = 0x0056a8d4;
constexpr std::uint32_t kWaitTicks = 0x005616e0;
constexpr std::uint32_t kEndFlag = 0x00561bf0;
constexpr std::uint32_t kSoundVolumeSfx = 0x005618c4;
constexpr std::uint32_t kSoundVolumeMusic = 0x00561994;
constexpr std::uint32_t kCompetition = 0x0055fe28;

constexpr std::uint32_t kFmtNumber = 0x00525764;            // "%d"
constexpr std::uint32_t kFmtString = 0x005256dc;            // "%s"
constexpr std::uint32_t kFmtTricks = 0x0052c73c;            // "%ld"
constexpr std::uint32_t kFmtLabel = 0x0052a56c;             // "%s: %d"
constexpr std::uint32_t kFmtLabelDashes = 0x0052c6bc;       // "%s: ---"
constexpr std::uint32_t kFmtPlayer = 0x00525898;            // "%s %d"
constexpr std::uint32_t kCursorText = 0x0052c740;           // the blinking cursor under a high-score letter

constexpr std::uint32_t kMsgNoNameControl = 0x0052c744;
constexpr std::uint32_t kMsgZeroScore = 0x0052c710;
constexpr std::uint32_t kMsgEndAllGold = 0x0052c6e0;
constexpr std::uint32_t kMsgEndPlayerMovie = 0x0052c6c4;
constexpr std::uint32_t kMsgEndBailMovie = 0x0052c6f8;

// the pointer cells of the screens' texts
constexpr std::uint32_t kTxtContinue = 0x0052e968;
constexpr std::uint32_t kTxtSelect = 0x0052e980;


}  // namespace

// a button hint the retail build left empty: nothing is drawn (0x0044c6e0, 0x0044c6f0 and 0x0044c700 return at once)

// 0x0044dd80: the PAUSED bitmap: the flag the pad code reads, then the picture's draw method (slot 3) at the top of the screen
void FrontScreens::draw_paused_bitmap() {
    set_global_word(m_, 0x00537b3c, 1);
    s_.note(kSpriteDrawService, {global_word(m_, 0x0056b70c), 0x100, 0x28, 0});
}

// 0x0044c710: one player's column of the tag/versus results: the name, WINNER/LOSER (pulsing until the player's pad press), and six statistics
void FrontScreens::print_go_back(std::uint32_t player, std::int32_t x, std::int32_t y, std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t steals,
                                 std::uint32_t losses, std::uint32_t streak, std::uint32_t total, std::int32_t result) {
    std::uint32_t pulse = (global_word(m_, front::kSimTick) << 1) % 0x3c;
    if (pulse > 0x1e) {
        pulse = 0x3c - pulse;
    }
    mess_justify(0);
    const std::string title = retail_format(m_.cstring(kFmtPlayer, 0x10), {m_.cstring(str(0x0052eaf4), 0x40)}, {s(word_at(m_, player + 0x2cc4)) + 1});
    mess_scale(0xe0);
    mess_rgb(r, g, b);
    mess_line(x + 0x62, y - 0x46, title);
    if (result == 1 || result == 0) {
        mess_rgb(pulse + r, g, (b - pulse + 0x1e));
        mess_scale(pulse * 2 + 0x100);
        const bool flagged = (engine_value(0x004de010, {player}) & 0xff) != 0;
        if (flagged) {
            mess_scale(0xe0);
            mess_rgb(r, g, b);
        }
        mess_text(x + 0x62, y - 0x16, str(result == 1 ? 0x0052e29c : 0x0052e2a0));
    }
    mess_scale(0xe0);
    mess_rgb(r, g, b);
    mess_justify(1);
    static const std::uint32_t labels[6] = {0x0052e490, 0x0052e494, 0x0052e498, 0x0052e49c, 0x0052e4a0, 0x0052e4a4};
    static const std::int32_t rows[6] = {0, 0x11, 0x33, 0x44, 0x55, 0x66};
    for (int k = 0; k < 6; ++k) {
        mess_text(x + 0x14, y + rows[k], str(labels[k]));
    }
    mess_justify(2);
    const std::uint32_t values[6] = {word_at(m_, player + 0x2cdc), word_at(m_, player + 0x2ce0), total, steals, losses, streak};
    for (int k = 0; k < 6; ++k) {
        mess_line(x + 0x9c, y + rows[k], retail_format("%d", {}, {s(values[k])}));
    }
}

// 0x0044ca60: the front end's draw: whatever the status calls for (a menu, a statistics table, the high scores, a message)
void FrontScreens::display() {
    if (global_word(m_, 0x0056a8f0) != 0) {
        engine(0x0048b620, {0x13, 0xbe, 0xc2, 0x2a, 0, 0, 0, 0});
    }
    if (global_word(m_, front::kStatus) != 0) {
        print_demo();
    }
    mess_justify(0);
    mess_scale(0x100);
    engine(0x004df030);
    auto set_menu_y = [&](std::uint32_t menu, std::uint32_t y) { set_word_at(m_, word_at(m_, menu) + 0x10, y); };
    auto ready = [&]() { return s(global_word(m_, kWaitTicks)) > ((global_int(m_, front::kAnimScale) * 10) >> 8); };
    auto question = [&](std::uint32_t text, std::uint32_t y, std::uint32_t menu_y) {
        mess_rgb(0x96, 100, 0x19);
        mess_text(0x100, static_cast<std::int32_t>(y), text);
        set_menu_y(front::kMenuYesNo, menu_y);
        cmenu_display(global_word(m_, front::kMenuYesNo));
        hint_text(str(kTxtSelect), 0);
    };
    switch (global_word(m_, front::kStatus)) {
    case 1:
        cmenu_display(global_word(m_, front::kMenuPaused));
        return;
    case 3:
        draw_paused_bitmap();
        hint_one();
        mess_rgb(0x96, 100, 0x19);
        mess_text(0x100, 0x5f, str(0x0052e99c));
        set_menu_y(front::kMenuYesNo, 0x87);
        cmenu_display(global_word(m_, front::kMenuYesNo));
        return;
    case 4:
        draw_paused_bitmap();
        mess_rgb(0xb4, 10, 10);
        if ((global_int(m_, kFrameCounter) / 10 & 1) != 0) {
            mess_text(0x100, 0xb8, str(0x0052e974));
        }
        return;
    case 5:
        mess_rgb(0x96, 100, 0x19);
        mess_text(0x100, 0x67, str(0x0052e96c));
        set_menu_y(front::kMenuYesNo, 0x8f);
        cmenu_display(global_word(m_, front::kMenuYesNo));
        hint_text(str(kTxtSelect), 0);
        return;
    case 6:
    case 7:
    case 0xc:
        question(str(0x0052e524), 0x50, 0x82);
        return;
    case 0x11:
        question(str(0x0052e59c), 0x50, 0x82);
        return;
    case 0xd:
        display_statistics();
        return;
    case 0xe:
        if (global_byte(m_, kEndFlag) == 0 && ready()) {
            cmenu_display(global_word(m_, front::kMenuEnd));
            hint_text(str(kTxtSelect), 0);
        }
        return;
    case 0xf:
        if (ready()) {
            cmenu_entry_off(global_word(m_, front::kMenuReplayFull), str(0x0052e538));
            cmenu_centre_y(global_word(m_, front::kMenuReplayFull));
            cmenu_display(global_word(m_, front::kMenuReplayFull));
            hint_one();
        }
        return;
    case 0x10:
        if (ready()) {
            cmenu_entry_off(global_word(m_, front::kMenuReplayShort), str(0x0052e538));
            cmenu_centre_y(global_word(m_, front::kMenuReplayShort));
            cmenu_display(global_word(m_, front::kMenuReplayShort));
            hint_one();
        }
        return;
    case 0x14:
    case 0x1e:
        engine(0x0041b590, {kCompetition});
        return;
    case 0x16:
    case 0x1f:
    case 0x20:
        engine(0x0041af40, {kCompetition});
        return;
    case 0x17:
    case 0x23:
        engine(0x0041a780, {kCompetition});
        return;
    case 0x18:
        if (ready()) {
            cmenu_display(global_word(m_, front::kMenuPlayer));
            hint_text(str(kTxtSelect), 0);
        }
        return;
    case 0x19:
        if (ready()) {
            cmenu_display(global_word(m_, front::kMenuHeat));
            hint_text(str(kTxtSelect), 0);
        }
        return;
    case 0x1a:
        if (ready()) {
            cmenu_entry_off(global_word(m_, front::kMenuReplayComp), str(0x0052e538));
            cmenu_centre_y(global_word(m_, front::kMenuReplayComp));
            cmenu_display(global_word(m_, front::kMenuReplayComp));
            hint_one();
        }
        return;
    case 0x24:
        engine(0x0041a270, {kCompetition});
        return;
    case 0x25:
        check(false, kMsgEndAllGold);
        return;
    case 0x26:
        check(false, kMsgEndPlayerMovie);
        return;
    case 0x27:
        check(false, kMsgEndBailMovie);
        return;
    case 0x2a:
        display_horse_title();
        return;
    case 0x2b:
        mess_justify(0);
        mess_rgb(0x7a, 0x7a, 0x7a);
        check(global_word(m_, kNameEntry) != 0, kMsgNoNameControl);
        vcall_item("name_entry_draw", global_word(m_, kNameEntry), 2, {0, 0});
        hint_text(str(0x0052e990), 0);
        return;
    case 0x2e:
        display_high_scores();
        return;
    case 0x2f:
        sprintf_to(kTimeBuffer, retail_format(m_.cstring(kFmtLabel, 0x10), {m_.cstring(str(0x0052e5cc), 0x40)}, {global_int(m_, kSoundVolumeSfx)}));
        sprintf_to(kMusicBuffer, retail_format(m_.cstring(kFmtLabel, 0x10), {m_.cstring(str(0x0052e5d0), 0x40)}, {global_int(m_, kSoundVolumeMusic)}));
        {
            const std::int32_t tracks = s(engine_value(0x004ac010));
            if (tracks < 1 || global_int(m_, 0x0056a2ec) == 0 || host_options().custom_soundtrack) {
                sprintf_to(kTrackBuffer, retail_format(m_.cstring(kFmtLabelDashes, 0x10), {m_.cstring(str(0x0052e5d4), 0x40)}, {}));
            } else {
                const std::int32_t track = s(engine_value(0x004ac010));
                sprintf_to(kTrackBuffer, retail_format(m_.cstring(kFmtLabel, 0x10), {m_.cstring(str(0x0052e5d4), 0x40)}, {track}));
            }
        }
        cmenu_display(global_word(m_, front::kMenuSound));
        hint_text(str(kTxtSelect), 0);
        return;
    case 0x33:
        mess_scale(0x180);
        mess_rgb(0x96, 100, 0x19);
        mess_text(0x100, 0x28, str(0x0052e4a8));
        if (ready()) {
            cmenu_centre_y(global_word(m_, front::kMenuTime));
            cmenu_display(global_word(m_, front::kMenuTime));
            hint_one();
        }
        return;
    default:
        return;
    }
}

// the title of the horse game's score-setting screen (status 0x2a)
void FrontScreens::display_horse_title() {
    mess_scale(0x100);
    const bool first = global_word(m_, kHorseWho) == 0;
    if (first) {
        mess_rgb(0x50, 0x50, 0x80);
    } else {
        mess_rgb(0x80, 0x50, 0x50);
    }
    if (global_word(m_, kHorseMatch) == 0) {
        mess_text(0x100, 0x78, str(first ? 0x0052e4ac : 0x0052e4b0));
    } else {
        mess_text(0x100, 0x78, str(first ? 0x0052e4b4 : 0x0052e4b8));
        mess_line(0x100, 0x8c, retail_format("%d", {}, {global_int(m_, kHorseTarget)}));
    }
    horse_display_words(kHorseSaved);
    engine(0x004dc0c0);
}

// status 0xd: the statistics (one player) or the game over screen (two players, the horse game, the tag game)
void FrontScreens::display_statistics() {
    engine(0x004dc0c0);
    mess_scale(0x100);
    set_global_word(m_, 0x00568618, 0);
    const std::uint32_t mode = global_word(m_, front::kGameMode);
    if (global_word(m_, front::kPlayerCount) == 1) {
        mess_rgb(0xb6, 0x84, 0x24);
        mess_text(0x100, 0x28, str(0x0052e574));
    } else {
        mess_rgb(0xb6, 0x84, 0x24);
        mess_text(0x100, 0x28, str(mode == 7 ? 0x0052e4bc : 0x0052e4c0));
    }
    mess_rgb(0x7a, 0x7a, 0x7a);
    mess_scale(0x100);
    if (global_word(m_, front::kPlayerCount) == 1) {
        mess_justify(1);
        for (std::uint32_t k = 0; k < 5; ++k) {
            std::int32_t v = int_at(m_, kSlideA + 4 * k) + 0x1e;
            set_word_at(m_, kSlideA + 4 * k, u(v));
            if (v > 0x5a) {
                set_word_at(m_, kSlideA + 4 * k, 0x5a);
            }
        }
        const std::int32_t level = global_byte(m_, kPlayerLevelFlag) == 0 ? global_int(m_, front::kCurrentLevel) : 0xd;
        mess_text(global_int(m_, kSlideA), 0x3c, str(0x0052e578));
        mess_text(global_int(m_, kSlideA + 4), 0x4e, str(0x0052e57c));
        mess_text(global_int(m_, kSlideA + 12), 0x60, str(0x0052e584));
        mess_justify(0);
        const std::uint32_t skater = global_word(m_, kSkater);
        mess_justify(2);
        for (std::uint32_t k = 0; k < 5; ++k) {
            const std::int32_t v = int_at(m_, kSlideB + 4 * k) - 0x1e;
            set_word_at(m_, kSlideB + 4 * k, u(v));
            if (v < 0x19a) {
                set_word_at(m_, kSlideB + 4 * k, 0x19a);
            }
        }
        sprintf_to(kLineBuffer, retail_format("%d", {}, {int_at(m_, skater + 0x16c)}));
        mess_text(global_int(m_, kSlideB), 0x3c, kLineBuffer);
        sprintf_to(kLineBuffer2, retail_format("%d", {}, {int_at(m_, kHighScores + u(level) * 0x40)}));
        mess_text(global_int(m_, kSlideB + 4), 0x4e, kLineBuffer2);
        sprintf_to(kLineBuffer3, retail_format(m_.cstring(kFmtTricks, 0x10), {}, {static_cast<std::int32_t>(half_at(m_, skater + 0x172))}));
        mess_text(global_int(m_, kSlideB + 12), 0x60, kLineBuffer3);
        if (global_int(m_, kSlideA) == 0x5a) {
            mess_justify(0);
            if (word_at(m_, skater + 0x178) == 0) {
                mess_scale(0xdc);
                mess_text(0x100, 0x73, str(0x0052e58c));
                mess_scale(0x100);
                mess_text(0x100, 0x8c, str(0x0052e590));
            } else {
                const std::uint32_t count = half_at(m_, skater + 0x176);
                const std::string format = m_.cstring(str(count == 1 ? 0x0052e598 : 0x0052e594), 0x80);
                sprintf_to(kComboBuffer, retail_format(format, {}, {static_cast<std::int32_t>(count), int_at(m_, skater + 0x178)}));
                mess_scale(0xdc);
                mess_text(0x100, 0x73, kComboBuffer);
                mess_scale(0xbc);
                std::int32_t y = 0x8a;
                std::uint32_t at = skater + 0x17c;
                do {
                    mess_text(0x100, y, at);
                    y += 0xe;
                    at += 100;
                } while (y < 0xb4);
            }
        }
    } else {
        mess_scale(0x100);
        const std::uint32_t first = global_word(m_, front::kPlayer);
        const std::uint32_t second = global_word(m_, front::kPlayer2);
        std::int32_t a = int_at(m_, first + 0x16c);
        std::int32_t b = second != 0 ? int_at(m_, second + 0x16c) : 0;
        if (mode == 7) {
            display_horse_game();
        } else if (mode == 8) {
            std::int32_t result_a = 0;
            std::int32_t result_b = 0;
            const std::uint32_t player_one = global_word(m_, front::kPlayer);
            if (int_at(m_, second + 0x2cdc) == int_at(m_, player_one + 0x2cdc)) {
                mess_text(0x100, 0x40, str(0x0052e4c4));
                result_a = result_b = -1;
            } else if (int_at(m_, second + 0x2cdc) > int_at(m_, player_one + 0x2cdc)) {
                result_b = 1;
            } else {
                result_a = 1;
            }
            print_go_back(global_word(m_, front::kPlayer), 0x1e, 0x56, 0x20, 0x20, 0x80, global_word(m_, 0x0056b7dc), global_word(m_, 0x0056b79c), global_word(m_, 0x0056b7b0), global_word(m_, 0x0056b7a4), result_a);
            print_go_back(global_word(m_, front::kPlayer2), 0x11e, 0x56, 0x80, 0x20, 0x20, global_word(m_, 0x0056b7e0), global_word(m_, 0x0056b798), global_word(m_, 0x0056b7b4), global_word(m_, 0x0056b7a0), result_b);
        } else if (mode == 9) {
            engine(0x004b8ef0);
        } else {
            sprintf_to(kLineBuffer, retail_format(m_.cstring(str(0x0052e4f4), 0x40), {}, {a}));
            sprintf_to(kLineBuffer2, retail_format(m_.cstring(str(0x0052e4f8), 0x40), {}, {b}));
            mess_text(0x100, 0x4b, kLineBuffer);
            mess_text(0x100, 0x63, kLineBuffer2);
            if (a > b) {
                mess_text(0x100, 0x7b, str(0x0052e4cc));
            } else if (a < b) {
                mess_text(0x100, 0x7b, str(0x0052e4c8));
            } else {
                mess_text(0x100, 0x7b, str(0x0052e4e4));
            }
        }
    }
    if (mode != 7) {
        hint_text(str(kTxtContinue), 0);
    }
}

// the horse game's results: the score the first player set, the match, the letters
void FrontScreens::display_horse_game() {
    set_global_word(m_, kHorseState, global_word(m_, kHorseMatch));
    set_global_word(m_, 0x00561758, global_word(m_, kHorseTarget));
    set_global_word(m_, kHorseRestart, global_word(m_, kHorseRestartIndex));
    set_global_word(m_, kHorseTurn, global_word(m_, kHorseWho));
    set_global_word(m_, kHorseCounts, global_word(m_, kHorseSaved));
    set_global_word(m_, kHorseCounts + 4, global_word(m_, kHorseSavedB));
    if (global_word(m_, kHorseWho) == 0) {
        mess_rgb(0x50, 0x50, 0x80);
    } else {
        mess_rgb(0x80, 0x50, 0x50);
    }
    const std::uint32_t player = global_word(m_, front::kPlayer);
    auto flip_turn = [&]() { set_global_word(m_, kHorseTurn, global_word(m_, kHorseTurn) ^ 1); };
    if (global_word(m_, kHorseMatch) == 0) {
        const std::uint32_t score = word_at(m_, player + 0x2a8);
        set_global_word(m_, kHorseTarget, score);
        if (score == 0) {
            mess_text(0x100, 0x50, str(0x0052e4d0));
        } else {
            mess_line(0x100, 0x50, retail_format(m_.cstring(str(0x0052e4e8), 0x40), {}, {static_cast<std::int32_t>(score)}));
            set_global_word(m_, kHorseState, global_word(m_, kHorseState) ^ 1);
        }
        flip_turn();
    } else {
        check(global_word(m_, kHorseTarget) != 0, kMsgZeroScore);
        mess_line(0x100, 0x50, retail_format(m_.cstring(str(0x0052e4ec), 0x40), {}, {static_cast<std::int32_t>(global_word(m_, kHorseTarget))}));
        const std::uint32_t scored = word_at(m_, player + 0x2a8);
        mess_line(0x100, 0x68, retail_format(m_.cstring(str(0x0052e4f0), 0x40), {}, {static_cast<std::int32_t>(scored)}));
        if (scored == global_word(m_, kHorseTarget)) {
            mess_text(0x100, 0x80, str(0x0052e4d4));
            flip_turn();
        } else if (scored > global_word(m_, kHorseTarget)) {
            mess_text(0x100, 0x80, str(0x0052e4e0));
            set_global_word(m_, 0x00561758, scored);
            flip_turn();
        } else {
            const std::uint32_t who = global_word(m_, kHorseWho);
            const std::uint32_t letters = word_at(m_, kHorseCounts + 4 * who) + 1;
            set_word_at(m_, kHorseCounts + 4 * who, letters);
            if (letters == global_word(m_, kHorseLength)) {
                const std::uint8_t first = global_byte(m_, kHorseWord);
                const bool vowel = first == 'A' || first == 'E' || first == 'I' || first == 'O' || first == 'U';
                const std::string line = m_.cstring(str(vowel ? 0x0052e4d8 : 0x0052e4dc), 0x40);
                scratch_line(line);
                horse_message(kScratchLine, kHorseWord, kHorseSlide, 0x182, line, m_.cstring(kHorseWord, 0x40));
                hint_text(str(kTxtContinue), 0);
                set_global_word(m_, kHorseState, 2);
            } else {
                const std::string letter(1, static_cast<char>(byte_at(m_, kHorseWord - 1 + letters)));
                horse_message(str(0x0052e5dc), kScratchLine, kHorseSlide, 0xbd, m_.cstring(str(0x0052e5dc), 0x80), letter);
                set_global_word(m_, kHorseRestart, global_word(m_, kHorseRestart) + 1);
                set_global_word(m_, kHorseCapture, 1);
                if (global_word(m_, kHorseRestart) == global_word(m_, kHorseRestarts)) {
                    set_global_word(m_, kHorseRestart, 0);
                }
                flip_turn();
                set_global_word(m_, kHorseState, global_word(m_, kHorseState) ^ 1);
            }
            horse_display_words(kHorseCounts);
            return;
        }
    }
    horse_display_words(kHorseCounts);
}

// status 0x2e: the high-score table; the row being edited pulses its current letter and blinks a cursor under it
void FrontScreens::display_high_scores() {
    mess_scale(0x100);
    const std::uint32_t level = global_byte(m_, kPlayerLevelFlag) == 0 ? global_word(m_, front::kCurrentLevel) : 0xd;
    sprintf_to(kTitleBuffer, retail_format(m_.cstring(str(0x0052e5bc), 0x40), {m_.cstring(kLevelRecords + level * 0x1ac, 0x40)}, {}));
    mess_rgb(0xb6, 0x84, 0x24);
    mess_text(0x100, 0x28, kTitleBuffer);
    mess_rgb(0x7a, 0x7a, 0x7a);
    std::uint32_t row = kHighScores + level * 0x40 - 3;       // (the first letter is at row - 1: three letters, the character at +2, the score at +3)
    std::int32_t y = 0x3b;
    const std::int32_t cursor_row = global_int(m_, kScoreAnchor);
    for (std::int32_t i = 0;; ++i) {
        const std::uint32_t slot_a = kSlideA + 4 * u(i);
        const std::uint32_t slot_b = kSlideB + 4 * u(i);
        std::int32_t v = int_at(m_, slot_a) + 0x1e;
        set_word_at(m_, slot_a, u(v));
        if (v > 0xb4) {
            set_word_at(m_, slot_a, 0xb4);
        }
        v = int_at(m_, slot_b) - 0x1e;
        set_word_at(m_, slot_b, u(v));
        if (v < 0x19c) {
            set_word_at(m_, slot_b, 0x19c);
        }
        if (i == cursor_row) {
            mess_rgb(0xb6, 0x84, 0x24);
        } else {
            mess_rgb(0x7a, 0x7a, 0x7a);
        }
        if (global_int(m_, kScoreFlash) > 0) {
            set_global_word(m_, kScoreFlash, u(global_int(m_, kScoreFlash) - 1));
        }
        const std::int32_t flash = global_int(m_, kScoreFlash) * 6 + 0x100;
        for (std::uint32_t letter = 0; letter < 3; ++letter) {
            set_global_byte(m_, kTitleBuffer, byte_at(m_, row - 1 + letter));
            if (letter == 0) {
                set_global_byte(m_, kTitleBuffer + 1, 0);
            }
            mess_justify(0);
            mess_scale(u(i == cursor_row && global_int(m_, kScoreCursor) == static_cast<std::int32_t>(letter) ? flash : 0x100));
            static const std::int32_t offsets[3] = {-0x5c, -0x42, -0x28};
            mess_text(int_at(m_, slot_a) + offsets[letter], y, kTitleBuffer);
            mess_scale(0x100);
        }
        if (i == cursor_row && ((global_int(m_, kFrameCounter) / 10) & 1) != 0) {
            sprintf_to(kTitleBuffer, m_.cstring(kCursorText, 0x10));
            mess_justify(0);
            mess_text(int_at(m_, slot_a) - 0x5c + global_int(m_, kScoreCursor) * 0x1a, y + 8, kTitleBuffer);
        }
        const std::int32_t character = sbyte_at(m_, row + 2);
        sprintf_to(kTitleBuffer, retail_format(m_.cstring(kFmtString, 0x10), {m_.cstring(word_at(m_, kNameRecords + u(character) * 0x170), 0x40)}, {}));
        mess_justify(1);
        mess_text(int_at(m_, slot_a), y, kTitleBuffer);
        sprintf_to(kTitleBuffer, retail_format("%d", {}, {int_at(m_, row + 3)}));
        const std::int32_t length = static_cast<std::int32_t>(m_.cstring(kTitleBuffer, 0x40).size());
        for (std::int32_t k = length - 1; k >= 0; --k) {
            const std::string digit(1, static_cast<char>(byte_at(m_, kTitleBuffer + u(k))));
            mess_justify(0);
            mess_line(int_at(m_, slot_b) + 0x1e + (k - length) * 0x12, y, digit);
        }
        y += 0x13;
        row += 8;
        if (y >= 0xd3) {
            break;
        }
    }
    engine(0x004dc0c0);
    hint_text(str(0x0052e984), 0);
}

}  // namespace opentony::runtime::frontend
