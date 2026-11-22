#include "auto_views.hpp"
#include "front.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kManager = 0x0056c580;
constexpr std::uint32_t kScreen = 0x00561c78;               // which shell screen runs next (0..0x17)
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
constexpr std::uint32_t kStartScreen = 0x00561b9c;
constexpr std::uint32_t kLevelName = 0x006a0548;
constexpr std::uint32_t kActivePlayer = 0x0056a888;
constexpr std::uint32_t kHold = 0x00561c98;                 // set while the play screen waits (cleared by the level select)
constexpr std::uint32_t kCreateMode = 0x00561cb8;           // which create-a-skater screen to enter

// the shell screens (the log prints their names)
enum Screen : std::uint32_t {
    kMainMenu, kSkaterSelect, kLevelSelect, kEquipmentSelect, kCreateSkater, kLevelEditor, kPlayGame, kContestPlayers, kContestDifficulty, kMultiSelect, kCareerSelect,
    kCreateSelect, kCreateAppearance, kCreateEquipment, kCreateSave, kStatsEditor, kTrickSelect, kOptions, kCredits, kMultiHandicap, kVideoMenu, kNetworkMenu,
    kNetworkMenu2, kControllerMenu,
};

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s8(std::uint32_t v) { return static_cast<std::int8_t>(v & 0xff); }

// where FrontEnd_Main's stack locals live in the port
constexpr std::uint32_t kFlags = kScratchLine + 0x80;       // the four unlock flags the play screen compares against
constexpr std::uint32_t kArgument = kScratchLine + 0xc0;    // the local a screen's argument pointer points to (live)

// the level names the play screen puts in the log (indexed by level 0..9)
constexpr std::uint32_t kLevelNames[10] = {0x0052cc14, 0x0052cc0c, 0x0052cc08, 0x0052cc04, 0x0052cbfc, 0x0052cbf8, 0x0052cbf0, 0x0052cbe4, 0x0052cbdc, 0x0052cbd4};

}  // namespace

// a screen call that answers through a stack local: the answer is the next scripted value in a replay; live, the screen has written it through the result pointer
// (the second argument) and the call itself returns nothing (milestone 9a: the main menu's answer read as 0 and the shell showed the main menu again)
std::uint32_t FrontScreens::screen_call(std::uint32_t address, std::vector<std::uint32_t> args, std::string text) {
    const std::uint32_t result = args.size() > 1 ? args[1] : 0;
    engine(address, std::move(args), std::move(text));
    if (s_.live() && result != 0) {
        return word_at(m_, result);
    }
    return u(s_.value(engine_name(address)));
}

// A screen whose first argument is a pointer to a local holding the value the shell reports: a replay reports the value, live the screen reads the local.
std::uint32_t FrontScreens::local_argument(std::uint32_t value) {
    if (!s_.live()) {
        return value;
    }
    set_global_word(m_, kArgument, value);
    return kArgument;
}

// a fresh texture-set database (the engine's block, then the manager's constructor)
void FrontScreens::new_database() {
    const std::uint32_t memory = operator_new(0x6a10);
    set_global_word(m_, kDatabase, memory != 0 ? engine_value(0x0041db50, {memory}) : 0);
}

// the cached database is dropped (the volume the music plays at goes with it)
void FrontScreens::delete_database() {
    const std::uint32_t database = global_word(m_, kDatabase);
    set_global_word(m_, kVolume, 0);
    if (database != 0) {
        delete_object(database);
    }
    set_global_word(m_, kDatabase, 0);
}

// 0x00452ff0: the shell: the screen loop that runs everything before and between the levels. `kScreen` names the screen to show; each screen reports how it was left and this
// picks the next one. The loop ends when the window is closed (the quit flag) or the main menu asks to quit.
void FrontScreens::frontend_main() {
    // (0x00452fe0, a function that returns at once)
    scratch_line({});
    set_global_word(m_, kScreen, kMainMenu);
    set_global_word(m_, kMode, 2);
    set_global_word(m_, kLevel, u(static_cast<std::int32_t>(global_word(m_, 0x0056a8a8)) - 1));
    set_global_byte(m_, kTwoPlayers, 0);
    set_global_byte(m_, kEditorTest, 0);
    set_global_byte(m_, kPark, 0);
    set_global_word(m_, 0x0056a880, 1);
    engine(0x004301b0);
    set_global_word(m_, 0x005614bc, 0);
    engine(0x004326a0);
    s_.note(kSvcSprintf, {0x0053d7b4, global_word(m_, 0x0052eb20)}, m_.cstring(global_word(m_, 0x0052eb20), 0x40));
    engine(0x004ad020, {0x0052ce64}, m_.cstring(0x0052ce64, 0x20));
    engine(0x004301f0, {0x9c40, 1, 0});
    new_database();
    set_global_word(m_, kFade, 0);
    set_global_word(m_, kVolume, 0x3fff);
    start_shell_music();
    engine(0x004a8ee0);
    engine(0x004b50e0);
    engine(0x004236d0);
    sync_skater_slots();
    engine(0x004dbd90);
    if (global_byte(m_, kQuit) != 0) {
        return;
    }
    auto pre_load = [&](std::uint32_t name) { engine(0x004a90f0, {global_word(m_, kPre), name, 0}, m_.cstring(name, 0x20)); };
    auto pre_unload = [&](std::uint32_t name) { engine(0x004a9330, {global_word(m_, kPre), name}, m_.cstring(name, 0x20)); };
    auto go = [&](std::uint32_t screen) { set_global_word(m_, kScreen, screen); };
    auto pack_new_free = [&]() { return (engine_value(0x004de150) & 0xff) != 0; };
    // the shell screens' music comes back after the screens that stop it (the editor, the credits)
    auto restart_music = [&]() {
        const std::uint32_t handle = global_word(m_, kMusic);
        set_global_word(m_, kVolume, 0x3fff);
        if (handle != 0xffffffffu) {
            engine(0x004ad9f0, {handle});
            set_global_word(m_, kMusic, 0xffffffffu);
        }
        start_shell_music();
    };

    do {
        if (global_word(m_, kActivePlayer) != 0) {
            const std::uint32_t shown = global_word(m_, kScreen);
            check(shown == kSkaterSelect || shown == kEquipmentSelect || shown == kCreateAppearance, 0x0052ccbc);
        }
        if (quit_combo()) {
            quit_combo_reset();
            go(kMainMenu);
        }
        engine(0x00417060);
        const std::uint32_t screen = global_word(m_, kScreen);
        set_global_word(m_, front::kStatus, 0);
        s_.note(kSvcPrintfLog, {0x0052cca0});
        switch (screen) {
        case kMainMenu: {
            engine(0x004b1890);
            engine(0x004b1150, {kManager});
            const std::uint32_t index = screen_call(0x0046aee0, {0, kScratchLine}) - 0x20;
            if (index > 0xdf) {
                break;
            }
            // the main menu's answers 0x20.. map to twelve outcomes (the byte table at 0x00454014)
            std::uint32_t outcome = 11;
            if (index < 4) {
                outcome = index;
            } else if (index >= 6 && index <= 11) {
                outcome = index - 2;
            } else if (index == 0xdf) {
                outcome = 10;
            }
            switch (outcome) {
            case 0:
            case 6:
                set_global_word(m_, kMode, outcome == 0 ? 2 : 3);
                set_global_byte(m_, kTwoPlayers, 0);
                go(kSkaterSelect);
                break;
            case 1:
                set_global_word(m_, kMode, 1);
                set_global_byte(m_, kTwoPlayers, 0);
                go(kCareerSelect);
                break;
            case 2:
                set_global_byte(m_, kTwoPlayers, 0);
                go(kLevelEditor);
                break;
            case 3:
                go(kCreateSelect);
                break;
            case 4:
                go(kLevelSelect);
                break;
            case 5:
                set_global_byte(m_, kTwoPlayers, 1);
                go(kNetworkMenu);
                break;
            case 7:
                go(kEquipmentSelect);
                break;
            case 8:
                // the demo: the recorded game plays back with the selection it was recorded with
                engine(0x0047b2d0, {0x0052be20}, m_.cstring(0x0052be20, 0x20));
                set_global_byte(m_, kTwoPlayers, global_short(m_, 0x0056b8d4) == 2 ? 1 : 0);
                set_global_word(m_, 0x00561c88, engine_value(0x004b18a0, {0}));
                set_global_word(m_, 0x00561c8c, engine_value(0x004b18a0, {1}));
                engine(0x004b18d0, {u(s8(global_byte(m_, 0x0056b8d6))), 0});
                engine(0x004b18d0, {u(s8(global_byte(m_, 0x0056b8d7))), 1});
                set_global_word(m_, kLevel, u(static_cast<std::int16_t>(global_half(m_, 0x0056b8da))));
                set_global_word(m_, kMode, u(static_cast<std::int16_t>(global_half(m_, 0x0056b8dc))));
                set_global_byte(m_, 0x00561990, 1);
                set_global_byte(m_, kPark, 0);
                set_global_word(m_, kHold, 0);
                go(kPlayGame);
                break;
            case 9:
                go(kOptions);
                break;
            case 10:
                s_.note(kSvcPrintfLog, {0x0052cba4});
                engine(0x004dbe60);
                return;
            default:
                break;
            }
            break;
        }
        case kCareerSelect:
            go(screen_call(0x00417400, {0, kScratchLine}) != 0xffffffffu ? 1 : 0);
            break;
        case kSkaterSelect: {
            engine(0x004304b0);
            engine(0x004c8f10, {0xec, 1});
            const std::uint32_t mode = global_word(m_, kMode);
            go(kLevelSelect);
            const std::uint32_t selection = screen_call(0x004a2010, {local_argument(mode), kScratchLine});
            check(global_word(m_, kDatabase) != 0, 0x0052cc74);
            if (global_word(m_, kDatabase) == 0) {
                new_database();
            }
            if (selection != 0xffffffffu) {
                if (global_byte(m_, kTwoPlayers) != 0 && pack_new_free() && global_word(m_, kScreen) != kEquipmentSelect && global_word(m_, kScreen) != kCreateAppearance) {
                    go(kPlayGame);
                }
            } else if (global_byte(m_, kTwoPlayers) != 0) {
                go(kMultiSelect);
                go(pack_new_free() ? kNetworkMenu + 1 : kNetworkMenu);
            } else {
                go(global_word(m_, kMode) == 1 ? kCareerSelect : kMainMenu);
            }
            sync_skater_slots();
            break;
        }
        case kLevelSelect: {
            set_global_word(m_, kHold, 0);
            pre_load(0x0052cc64);
            const std::uint32_t chosen = screen_call(0x004588f0, {0, kScratchLine});
            pre_unload(0x0052cc64);
            if (chosen != 0xffffffffu) {
                set_global_word(m_, kLevel, chosen);
                set_global_byte(m_, kPark, chosen == 0xd ? 1 : 0);
                go(kPlayGame);
                if (global_byte(m_, kTwoPlayers) != 0 && pack_new_free()) {
                    go(kNetworkMenu + 1);
                }
            } else {
                const std::uint32_t mode = global_word(m_, kMode);
                if (mode == 1 || mode == 2 || mode == 3 || mode == 10 || global_byte(m_, kTwoPlayers) == 0 || !pack_new_free()) {
                    go(kSkaterSelect);
                } else {
                    go(kNetworkMenu);
                }
            }
            break;
        }
        case kEquipmentSelect:
        case kCreateEquipment: {
            const std::uint32_t mode_in = screen == kCreateEquipment ? 1 : global_word(m_, kMode);
            pre_load(0x0052b6ac);
            pre_load(0x0052b6a0);
            std::string argument;
            for (std::uint32_t word : {mode_in, global_word(m_, kActivePlayer)}) {
                for (int byte = 0; byte < 4; ++byte) {
                    argument.push_back(static_cast<char>((word >> (8 * byte)) & 0xff));
                }
            }
            if (s_.live()) {   // the argument record the pointer names (a replay has it as the call's text)
                set_global_word(m_, kScratchLine, mode_in);
                set_global_word(m_, kScratchLine + 4, global_word(m_, kActivePlayer));
            }
            const std::uint32_t player = screen_call(0x00444590, {kScratchLine, kScratchLine}, argument);
            if (global_word(m_, kScreen) != kCreateEquipment) {
                go(kSkaterSelect);
            } else if (player == 0xffffffffu) {
                set_global_word(m_, kCreateMode, 3);
                go(kCreateSkater);
            } else {
                go(kCreateSave);
            }
            break;
        }
        case kCreateSelect:
            pre_load(0x0052cc58);
            if (screen_call(0x0042cd50, {0, kScratchLine}) == 0xffffffffu) {
                pre_unload(0x0052cc58);
                go(kMainMenu);
            } else {
                pre_unload(0x0052cc58);
                set_global_word(m_, kCreateMode, 0);
                go(kCreateSkater);
            }
            break;
        case kCreateSkater:
        case kCreateAppearance: {
            const bool appearance = screen == kCreateAppearance;
            const std::uint32_t request = appearance ? 1 : global_word(m_, kCreateMode);
            set_global_word(m_, 0x0056a878, appearance ? 1 : 0);
            const std::uint32_t result = screen_call(0x004271a0, {local_argument(request), kScratchLine});
            if (global_word(m_, kDatabase) == 0) {
                new_database();
            }
            if (appearance) {
                go(kSkaterSelect);
            } else {
                go(result != 0xffffffffu ? kCreateEquipment : kCreateSelect);
                sync_skater_slots();
            }
            break;
        }
        case kCreateSave:
            go(screen_call(0x0042be70, {0, kScratchLine}) != 0xffffffffu ? static_cast<std::uint32_t>(kMainMenu) : static_cast<std::uint32_t>(kCreateEquipment));
            break;
        case kStatsEditor:
            pre_load(0x0052cc48);
            screen_call(0x004b5ba0, {0, kScratchLine});
            pre_unload(0x0052cc48);
            go(kSkaterSelect);
            break;
        case kTrickSelect:
            pre_load(0x0052cc38);
            screen_call(0x004bf860, {0, kScratchLine});
            pre_unload(0x0052cc38);
            go(kSkaterSelect);
            break;
        case kLevelEditor: {
            delete_database();
            engine(0x004301b0);
            engine(0x004302c0, {0x18a88, 1, 1, 1});
            set_global_byte(m_, 0x00561ca9, 1);
            engine(0x0045d830, {2});
            check(global_word(m_, 0x005614bc) != 0, 0x0052cc1c);
            if (global_byte(m_, 0x00561450) != 0) {
                engine(0x00432980, {global_word(m_, 0x0052eb24), 0}, m_.cstring(global_word(m_, 0x0052eb24), 0x40));
            }
            set_global_word(m_, 0x00561c9c, 1);
            engine(seam::hal::WINMAIN_SwitchResolution, {2});
            engine(0x00432840);
            engine(0x00432860, {0, 0});
            engine(0x00432850);
            engine(seam::hal::WINMAIN_SwitchResolution, {2});
            set_global_word(m_, 0x00561c9c, 0);
            engine(0x004301b0);
            engine(0x004302c0, {0x9c40, 1, 1, 0});
            new_database();
            restart_music();
            if (global_byte(m_, kEditorTest) != 0) {
                set_global_byte(m_, kPark, 1);
                set_global_word(m_, kMode, global_byte(m_, kTwoPlayers) != 0 ? 8 : 2);
                set_global_word(m_, kLevel, 0xd);
                set_global_word(m_, kHold, 0);
                go(kPlayGame);
            } else {
                set_global_byte(m_, kPark, 0);
                go(kMainMenu);
            }
            break;
        }
        case kPlayGame: {
            set_global_word(m_, kVolume, 0);
            set_global_word(m_, kStartScreen, 0);
            const std::uint32_t database = global_word(m_, kDatabase);
            if (database != 0) {
                delete_object(database);
            }
            set_global_word(m_, kDatabase, 0);
            get_unlock_flags(kFlags);
            const std::uint32_t level = global_word(m_, kLevel);
            set_global_word(m_, front::kCurrentLevel, level);
            set_global_word(m_, kLevel, engine_value(0x004de300, {level}));
            engine(0x004dbe60);
            // the level's name goes in the log
            const std::uint32_t current = global_word(m_, front::kCurrentLevel);
            if (current != 13) {
                const std::uint32_t source = current < 10 ? kLevelNames[current] : 0x0055fe10;
                std::vector<std::uint8_t> bytes;
                for (char c : m_.cstring(source, 0x20)) {
                    bytes.push_back(static_cast<std::uint8_t>(c));
                }
                bytes.push_back(0);
                m_.write(kLevelName, bytes);
            }
            s_.note(kSvcPrintfLog, {0x0052aaf0});
            engine(seam::hal::CD_Recheck);
            launch_game_level(global_word(m_, kLevel), global_word(m_, kMode));
            set_global_byte(m_, kLevelName, 0);
            if (global_byte(m_, 0x00561990) != 0) {
                engine(0x004b18d0, {global_word(m_, 0x00561c88), 0});
                engine(0x004b18d0, {global_word(m_, 0x00561c8c), 1});
                set_global_byte(m_, 0x00561990, 0);
            }
            clear_gvideo_restart_data();
            if (global_byte(m_, kEditorTest) != 0) {
                go(kLevelEditor);
            } else {
                s_.note(kSvcPrintfLog, {0x0052cbc0});
                const std::uint32_t start = global_word(m_, kStartScreen);
                if (start == 1) {
                    go(kLevelSelect);
                } else if (start == 2) {
                    set_global_word(m_, 0x00561ca4, 1);
                    go(kSkaterSelect);
                } else if (start == 6) {
                    set_global_word(m_, kHold, 1);
                    go(kPlayGame);
                } else if (start == 7) {
                    set_global_word(m_, kStartScreen, 0);
                    go(kCredits);
                } else {
                    reselect_after_unlock_change(kFlags);
                    go(kMainMenu);
                }
            }
            if (global_byte(m_, kTwoPlayers) != 0 && pack_new_free()) {
                go(kNetworkMenu + 1);
            }
            break;
        }
        case kMultiSelect: {
            set_global_word(m_, 0x00561c6c, global_word(m_, kMode));
            const std::uint32_t result = screen_call(0x0047ba60, {0, kScratchLine});
            set_global_word(m_, kMode, global_word(m_, 0x00561c6c));
            go(result != 0xffffffffu ? 1 : 0);
            break;
        }
        case kMultiHandicap:
            go(screen_call(0x00454eb0, {0, kScratchLine}) != 0xffffffffu ? 2 : 1);
            break;
        case kOptions: {
            engine(0x004304b0);
            engine(0x004c8f10, {0xec, 1});
            screen_call(0x00481200, {0, kScratchLine});
            // the options screen leaves its requests in memory: the controller menu, the video menu, the credits and the video-test screen (live, the screen
            // has written them; a replay scripts them)
            if (!s_.live()) {
                set_global_byte(m_, 0x005601cc, u(s_.value(engine_name(0x00481200))));
                set_global_byte(m_, 0x005601cd, u(s_.value(engine_name(0x00481200))));
                set_global_word(m_, 0x00561cdc, u(s_.value(engine_name(0x00481200))));
                set_global_word(m_, 0x00561ce0, u(s_.value(engine_name(0x00481200))));
            }
            go(global_byte(m_, 0x005601cc) != 0 ? 0x17 : (global_byte(m_, 0x005601cd) != 0 ? 0x14 : 0));
            if (replay_playing()) {
                set_global_word(m_, kHold, 0);
                go(kPlayGame);
            }
            if (global_word(m_, 0x00561cdc) != 0) {
                go(kCredits);
            }
            if (global_word(m_, 0x00561ce0) != 0) {
                go(kOptions);
            }
            break;
        }
        case kCredits: {
            set_global_word(m_, kVolume, 0);
            const std::uint32_t track = global_word(m_, 0x00561cdc) - 1;
            const std::uint32_t database = global_word(m_, kDatabase);
            if (database != 0) {
                delete_object(database);
            }
            set_global_word(m_, kDatabase, 0);
            engine(0x004301b0);
            engine(0x004302c0, {0xea60, 1, 1, 1});
            // (Credits_Run takes the address of a local that holds the track; a replay recorded the track itself)
            if (s_.live()) {
                set_global_word(m_, kArgument, track);
                engine(0x00426430, {kArgument, 0});
            } else {
                engine(0x00426430, {track, 0});
            }
            engine(0x004302c0, {0x9c40, 1, 0, 0});
            new_database();
            restart_music();
            if (global_word(m_, 0x00561ca0) != 0) {
                set_global_word(m_, 0x00561ca0, 0);
                go(kMainMenu);
            } else {
                go(kOptions);
            }
            break;
        }
        case kVideoMenu:
            engine(0x004ce030);
            go(kOptions);
            break;
        case kNetworkMenu: {
            engine(0x0047cbb0, {0});
            const std::uint32_t result = u(s_.value(engine_name(0x0047cbb0)));
            go(result != 0xffffffffu ? result : 0);
            break;
        }
        case kNetworkMenu2: {
            engine(0x0047cbb0, {1});
            const std::uint32_t result = u(s_.value(engine_name(0x0047cbb0)));
            go(result == 0xffffffffu ? 1 : result);
            break;
        }
        case kControllerMenu:
            engine(0x0041c2d0);
            go(kOptions);
            break;
        default:
            break;
        }
    } while (global_byte(m_, kQuit) == 0);
}

}  // namespace opentony::runtime::frontend
