#include "auto_views.hpp"
#include "front.hpp"
#include "host_options.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kDemoFont = 0x005619e0;
constexpr std::uint32_t kDemoBlink = 0x0052bf3c;
constexpr std::uint32_t kCashBuffer = 0x00561bd0;
constexpr std::uint32_t kHorseWord = 0x0056a478;
constexpr std::uint32_t kHorseRestarts = 0x0056e208;
constexpr std::uint32_t kHorseRestartNames = 0x0056e1b8;
constexpr std::uint32_t kHorseRestartIndex = 0x0056867c;
constexpr std::uint32_t kMsgNoHorseRestarts = 0x0052c624;
constexpr std::uint32_t kFmtCash = 0x0052c610;             // "%s $%d"
constexpr std::uint32_t kFmtCashThousands = 0x0052c618;    // "%s $%d,%03d"

// the pointer cells of the menu line names
constexpr std::uint32_t kYes = 0x0052e410;
constexpr std::uint32_t kNo = 0x0052e414;
constexpr std::uint32_t kUpdateSave = 0x0052e5e0;
constexpr std::uint32_t kRestartCompetition = 0x0052e554;
constexpr std::uint32_t kRetry = 0x0052e550;
constexpr std::uint32_t kLevelGoals = 0x0052e510;
constexpr std::uint32_t kLevelSelect = 0x0052e558;
constexpr std::uint32_t kBackToEditor = 0x0052e55c;
constexpr std::uint32_t kSpendCash = 0x0052e560;
constexpr std::uint32_t kWatchReplay = 0x0052e564;
constexpr std::uint32_t kSaveCareer = 0x0052e54c;
constexpr std::uint32_t kQuit = 0x0052e970;
constexpr std::uint32_t kMainMenu = 0x0052e568;
constexpr std::uint32_t kPlayReplay = 0x0052e534;
constexpr std::uint32_t kHighlights = 0x0052e538;
constexpr std::uint32_t kSaveReplay = 0x0052e53c;
constexpr std::uint32_t kReplayBack = 0x0052e548;
constexpr std::uint32_t kReturnToCompetition = 0x0052e5b0;
constexpr std::uint32_t kNextHeat = 0x0052e5a8;
constexpr std::uint32_t kEndCompetition = 0x0052e5ac;
constexpr std::uint32_t kNextPlayer = 0x0052e5f0;
constexpr std::uint32_t kOneMinute = 0x0052e500;
constexpr std::uint32_t kTwoMinutes = 0x0052e504;
constexpr std::uint32_t kFiveMinutes = 0x0052e508;
constexpr std::uint32_t kTenMinutes = 0x0052e50c;
constexpr std::uint32_t kSoundBack = 0x0052e5d8;
constexpr std::uint32_t kTempFormat = 0x00527c68;          // "TEMP" (the sound levels menu's second line is looked up by this text)
constexpr std::uint32_t kTrackText = 0x0052c6b4;           // "TRACK"

constexpr std::uint32_t kSfxLevelLine = 0x005616c0;
constexpr std::uint32_t kMusicLevelLine = 0x0056175c;
constexpr std::uint32_t kSkipTrackLine = 0x00561734;

}  // namespace

bool FrontScreens::replay_level_flag() const {
    // 0x00419420: the competition flag, or a contest level played in career mode
    if (global_byte(m_, 0x00560080) != 0) {
        return true;
    }
    const std::int32_t level = global_int(m_, 0x00561c90);
    return byte_at(m_, 0x0053c240 + u(level) * 0x1ac) != 0 && global_int(m_, front::kGameMode) == 1;
}

void FrontScreens::demo_font_construct() { font_construct(kDemoFont); }

// 0x0044b1f0: a status change is logged (the old and new names are the log's arguments, which the probe's printf stub does not keep) and flagged for the update
void FrontScreens::set_status(std::uint32_t, std::uint32_t status) {
    if (status != global_word(m_, front::kStatus)) {
        s_.note(kSvcPrintfLog, {0x0052c600});
    }
    set_global_word(m_, front::kStatus, status);
    set_global_word(m_, front::kGotNewStatus, 1);
}

// 0x0044b240: the career cash in the corner while the end-of-level menu of a career run is up
void FrontScreens::print_demo() {
    if (global_word(m_, front::kGameMode) != 1 || global_word(m_, front::kStatus) != 0xe) {
        return;
    }
    const std::uint32_t config = 0x00568a6c + word_at(m_, global_word(m_, front::kPlayer) + 0x2cc0) * 0x104;     // TrickConfig_FindByCharacter
    std::int32_t cash = int_at(m_, config + 8);
    mess_justify(2);
    mess_scale(0xc0);
    mess_rgb(0x80, 0x80, 0x20);
    const std::string label = m_.cstring(str(0x0052e924), 0x40);
    if (cash < 0 || cash > 999999) {
        cash = 999999;
    }
    if (cash < 1000) {
        sprintf_to(kCashBuffer, retail_format(m_.cstring(kFmtCash, 0x10), {label}, {cash}));
    } else {
        sprintf_to(kCashBuffer, retail_format(m_.cstring(kFmtCashThousands, 0x10), {label}, {cash / 1000, cash % 1000}));
    }
    mess_text(0x1e0, 0x20, kCashBuffer);
    mess_justify(0);
}

// 0x0044b320: "demo mode" blinks in the corner (once for each player's half in two-player games)
void FrontScreens::demo_mode_blink() {
    std::int32_t blink = global_int(m_, kDemoBlink) - 1;
    set_global_word(m_, kDemoBlink, u(blink));
    if (blink == 0) {
        set_global_word(m_, kDemoBlink, 0x30);
    } else if (blink <= 0xc) {
        return;
    }
    const std::uint32_t text = str(0x0052e484);
    if (global_word(m_, front::kPlayerCount) != 2) {
        engine(0x0044a070, {kDemoFont, 0x100, 0x14, text, 0}, m_.cstring(text, 0x40));
        return;
    }
    engine(0x0044a070, {kDemoFont, 0x80, 0x14, text, 0}, m_.cstring(text, 0x40));
    engine(0x0044a070, {kDemoFont, 0x180, 0x14, text, 0}, m_.cstring(text, 0x40));
}

// 0x0044b3a0: show one full-screen picture for two frames
void FrontScreens::display_screen(std::uint32_t file) {
    const std::uint32_t memory = crt_new(0x1c);
    std::uint32_t sprite = 0;
    if (memory != 0) {
        s_.note(kSvcFileOpen, {file, 1}, m_.cstring(file));
        const std::uint32_t handle = static_cast<std::uint32_t>(s_.value(kSvcFileOpen));
        sprite = engine_value(seam::hal::D3DSprite_Create, {memory, handle, file, 1});
    }
    for (int frame = 0; frame < 2; ++frame) {
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x004304b0);
        engine(0x00466be0);
        engine(0x004d7990, {sprite, word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        engine(0x00430510);
    }
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
}

// 0x0044b470: the competition's restart bookkeeping is cleared; in the horse game the first restart point is the next start
void FrontScreens::restart_comp_frig() {
    set_global_byte(m_, 0x00561be4, 0);
    set_global_word(m_, 0x00561730, 0);
    set_global_word(m_, 0x0056a854, 0);
    set_global_word(m_, 0x00568680, 0);
    set_global_word(m_, 0x00568660, 0);
    set_global_word(m_, 0x00568630, 0);
    set_global_word(m_, kHorseRestartIndex, 0);
    set_global_word(m_, 0x00568a60, 0);
    set_global_word(m_, 0x00568a64, 0);
    set_global_word(m_, 0x00568a20, 0);
    set_global_word(m_, 0x0056a8c0, 0);
    set_global_word(m_, 0x00568814, 1);
    if (global_word(m_, front::kGameMode) == 7) {
        check(global_word(m_, kHorseRestarts) != 0, kMsgNoHorseRestarts);
        const std::uint32_t name = word_at(m_, kHorseRestartNames + 4 * global_word(m_, kHorseRestartIndex));
        engine(0x004c4c50, {name}, m_.cstring(name, 0x40));
    }
}

// 0x0044b570: the two players' HORSE words: the letters each has (dashes for the rest)
void FrontScreens::horse_display_words(std::uint32_t counts) {
    mess_scale(0x100);
    mess_justify(0);
    for (std::uint32_t player = 0; player < 2; ++player) {
        std::int32_t letters = int_at(m_, counts + 4 * player);
        std::string word;
        for (std::uint32_t at = kHorseWord; byte_at(m_, at) != 0; ++at) {
            if (letters == 0) {
                word.push_back('-');
            } else {
                --letters;
                word.push_back(static_cast<char>(byte_at(m_, at)));
            }
        }
        if (player == 0) {
            mess_rgb(0x50, 0x50, 0x80);
            mess_text(0x56, 0x20, str(0x0052e488));
        } else {
            mess_rgb(0x80, 0x50, 0x50);
            mess_text(0x1aa, 0x20, str(0x0052e48c));
        }
        mess_line(static_cast<std::int32_t>(player * 0x154 + 0x56), 0x30, word);
    }
}

// 0x0044bde0: blank the frame buffer: two fills (the whole screen, then a one-line strip)
void FrontScreens::clear_screen() {
    set_global_word(m_, 0x0056e310, 0);
    auto rect = [](std::uint16_t x, std::uint16_t y, std::uint16_t w, std::uint16_t h) {
        std::string bytes;
        for (std::uint16_t word : {x, y, w, h}) {
            bytes.push_back(static_cast<char>(word & 0xff));
            bytes.push_back(static_cast<char>(word >> 8));
        }
        return bytes;
    };
    engine(0x004e8730, {kScratchLine, 0, 0, 0}, rect(0, 0, 0x200, 0x1ff));
    engine(0x004e8740, {0});
    engine(0x004e8730, {kScratchLine, 0, 0, 0}, rect(0, 0x1ff, 0x200, 1));
    engine(0x004e8740, {0});
}

// 0x0044be50: build the front end's menus (and the sound levels menu's lines)
void FrontScreens::init() {
    auto make = [&](std::uint32_t variable, std::uint32_t scale_a) { return make_menu(variable, scale_a); };
    auto add = [&](std::uint32_t menu, std::uint32_t cell) { cmenu_add_entry(menu, str(cell)); };
    const auto competition_name = [&]() { return str(replay_level_flag() ? kRestartCompetition : kRetry); };

    std::uint32_t menu = make(front::kMenuYesNo, 0x190);
    add(menu, kYes);
    add(menu, kNo);

    menu = make(front::kMenuEnd, 0x14f);
    add(menu, kUpdateSave);
    cmenu_add_entry(menu, competition_name());
    for (std::uint32_t cell : {kLevelGoals, kLevelSelect, kBackToEditor, kSpendCash, kWatchReplay, kSaveCareer, kQuit, kMainMenu}) {
        add(menu, cell);
    }
    if (global_word(m_, front::kGameMode) != 1) {
        cmenu_entry_off(menu, str(kSpendCash));
        cmenu_entry_off(menu, str(kSaveCareer));
        cmenu_entry_off(menu, str(kLevelGoals));
    } else if (replay_level_flag() || global_int(m_, front::kCurrentLevel) > 7) {
        cmenu_entry_off(menu, str(kLevelGoals));
    }
    if (global_word(m_, front::kGameMode) != 3) {
        cmenu_entry_off(menu, str(kUpdateSave));
    }
    const std::uint32_t mode = global_word(m_, front::kGameMode);
    if (mode == 2 || mode == 7 || mode == 4 || mode == 9) {
        cmenu_entry_off(menu, str(kWatchReplay));
    }
    if (global_byte(m_, 0x00561ca8) != 0) {
        cmenu_entry_off(menu, str(kLevelSelect));
        cmenu_entry_off(menu, str(kQuit));
    } else {
        cmenu_entry_off(menu, str(kBackToEditor));
        cmenu_entry_off(menu, str(kMainMenu));
    }
    cmenu_centre_y(menu);
    cmenu_set_line(menu, cmenu_find_entry(menu, competition_name()));
    if (global_byte(m_, 0x006a3b21) != 0) {
        cmenu_entry_off(menu, competition_name());
        for (std::uint32_t cell : {kLevelSelect, kBackToEditor, kSpendCash, kWatchReplay, kSaveCareer}) {
            cmenu_entry_off(menu, str(cell));
        }
    }
    if (host_options().quick_play) {
        // the quick start: shown but greyed, never chosen (the cursor passes over them); its run stays in the level and never writes the save file. Spend Some Cash
        // (a career's menu: the speed run, the competition) leaves the level for the shell's screens, as Level Select does
        for (std::uint32_t cell : {kLevelSelect, kUpdateSave, kSaveCareer, kSpendCash}) {
            cmenu_disable(menu, str(cell));
        }
    }

    menu = make(front::kMenuReplayFull, 0x14f);
    add(menu, kPlayReplay);
    add(menu, kHighlights);
    if (global_byte(m_, 0x005614c4) == 0) {
        add(menu, kSaveReplay);
        if (host_options().quick_play) {
            cmenu_disable(menu, str(kSaveReplay));
        }
    }
    add(menu, kReplayBack);
    cmenu_centre_y(menu);

    menu = make(front::kMenuReplayShort, 0x14f);
    add(menu, kPlayReplay);
    add(menu, kHighlights);
    add(menu, kReplayBack);
    cmenu_centre_y(menu);

    menu = make(front::kMenuReplayComp, 0x14f);
    add(menu, kPlayReplay);
    add(menu, kHighlights);
    if (global_byte(m_, 0x005614c4) == 0) {
        add(menu, kSaveReplay);
        if (host_options().quick_play) {
            cmenu_disable(menu, str(kSaveReplay));
        }
    }
    add(menu, kReturnToCompetition);
    cmenu_centre_y(menu);

    menu = make(front::kMenuHeat, 0x14f);
    for (std::uint32_t cell : {kNextHeat, kWatchReplay, kEndCompetition, kQuit}) {
        add(menu, cell);
    }
    cmenu_centre_y(menu);

    menu = make(front::kMenuPlayer, 0x14f);
    for (std::uint32_t cell : {kNextPlayer, kWatchReplay, kEndCompetition, kQuit}) {
        add(menu, cell);
    }
    cmenu_centre_y(menu);

    menu = make(front::kMenuTime, 0x14f);
    for (std::uint32_t cell : {kOneMinute, kTwoMinutes, kFiveMinutes, kTenMinutes}) {
        add(menu, cell);
    }
    cmenu_centre_y(menu);
    cmenu_set_line(menu, cmenu_find_entry(menu, str(kTwoMinutes)));

    menu = make(front::kMenuSound, 0x14f);
    set_global_word(m_, 0x005618c4, u(((global_int(m_, 0x0056a2e8) * 5 + 5) * 2) / 12));
    cmenu_add_entry(menu, kSfxLevelLine);
    set_global_word(m_, 0x00561994, u(((global_int(m_, 0x0056a2ec) * 5 + 5) * 2) / 12));
    sprintf_to(kMusicLevelLine, m_.cstring(kTempFormat, 0x10));
    cmenu_add_entry(menu, kMusicLevelLine);
    sprintf_to(kSkipTrackLine, m_.cstring(kTrackText, 0x10));
    cmenu_add_entry(menu, kSkipTrackLine);
    add(menu, kSoundBack);
    if (global_word(m_, front::kGameMode) == 7) {
        cmenu_disable(menu, kTempFormat);
        cmenu_disable(menu, kTrackText);
    }
    cmenu_centre_y(menu);
    set_global_word(m_, 0x00561b70, 0);
    set_global_byte(m_, 0x0056acf8, 0);
}

std::uint32_t FrontScreens::make_menu(std::uint32_t variable, std::uint32_t scale_a) {
    const std::uint32_t memory = operator_new(0x180);
    const std::uint32_t created = memory != 0 ? cmenu_new(memory, 0x100, 0, 0, scale_a, 0x100, 0x16) : 0;
    set_word_at(m_, variable, created);
    return created;
}

void FrontScreens::virtual_delete_menu(std::uint32_t menu) {
    if (word_at(m_, menu) == front::kVtCMenu) {
        cmenu_destruct(menu, 1);
    } else {
        s_.note(kVirtualDelete, {menu, 1});
    }
}

// 0x0044c650: delete the menu a variable holds and clear the variable
void FrontScreens::delete_menu(std::uint32_t variable) {
    if (word_at(m_, variable) != 0) {
        virtual_delete_menu(word_at(m_, variable));
        set_word_at(m_, variable, 0);
    }
}

void FrontScreens::delete_menus() {
    for (std::uint32_t variable : {front::kMenuYesNo, front::kMenuTime, front::kMenuEnd, front::kMenuReplayFull, front::kMenuReplayShort, front::kMenuReplayComp,
                                   front::kMenuHeat, front::kMenuPlayer, front::kMenuSound}) {
        delete_menu(variable);
    }
}

}  // namespace opentony::runtime::frontend
