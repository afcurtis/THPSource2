#include "auto_views.hpp"
#include "front.hpp"
#include "host_options.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

constexpr std::uint32_t kSkater = 0x0056a960;
constexpr std::uint32_t kAnimClock = 0x005685f4;
constexpr std::uint32_t kLevelTable = 0x0053c240;          // byte per level (0x1ac apart): the level is a contest level
constexpr std::uint32_t kLevelRecords = 0x0053c1f8;        // the level table: name first, 0x1ac bytes per level
constexpr std::uint32_t kWaitTicks = 0x005616e0;
constexpr std::uint32_t kLevelEventLatch = 0x00568658;
constexpr std::uint32_t kLevelEventSecondary = 0x00568818;
constexpr std::uint32_t kNameEntry = 0x00561bf8;
constexpr std::uint32_t kInputActionStates = 0x0056aff8;
constexpr std::uint32_t kPad2States = 0x0056b164;
constexpr std::uint32_t kHighScores = 0x00569ec0;
constexpr std::uint32_t kScoreAnchor = 0x0052bf34;
constexpr std::uint32_t kScoreCursor = 0x00561bf4;
constexpr std::uint32_t kScoreFlash = 0x0056bb3c;
constexpr std::uint32_t kNumCheatRestarts = 0x0056e1b4;
constexpr std::uint32_t kCheatRestartNames = 0x0056e164;
constexpr std::uint32_t kCompetition = 0x0055fe28;

// the pointer cells of the line names
constexpr std::uint32_t kYes = 0x0052e410;
constexpr std::uint32_t kNo = 0x0052e414;
constexpr std::uint32_t kContinue = 0x0052e968;
constexpr std::uint32_t kRestartCompetition = 0x0052e554;
constexpr std::uint32_t kRetry = 0x0052e550;
constexpr std::uint32_t kSkipToRestart = 0x0052e98c;
constexpr std::uint32_t kSoundLevels = 0x0052e5c8;
constexpr std::uint32_t kEndRun = 0x0052e518;
constexpr std::uint32_t kEndGame = 0x0052e514;
constexpr std::uint32_t kQuit = 0x0052e970;
constexpr std::uint32_t kSaveCareer = 0x0052e54c;
constexpr std::uint32_t kUpdateSave = 0x0052e5e0;
constexpr std::uint32_t kLevelSelect = 0x0052e558;
constexpr std::uint32_t kSpendCash = 0x0052e560;
constexpr std::uint32_t kLevelGoals = 0x0052e510;
constexpr std::uint32_t kBackToEditor = 0x0052e55c;
constexpr std::uint32_t kMainMenu = 0x0052e568;
constexpr std::uint32_t kWatchReplay = 0x0052e564;
constexpr std::uint32_t kPlayReplay = 0x0052e534;
constexpr std::uint32_t kHighlights = 0x0052e538;
constexpr std::uint32_t kSaveReplay = 0x0052e53c;
constexpr std::uint32_t kSaveVideo = 0x0052e540;
constexpr std::uint32_t kReplayBack = 0x0052e548;
constexpr std::uint32_t kReturnToCompetition = 0x0052e5b0;
constexpr std::uint32_t kNextHeat = 0x0052e5a8;
constexpr std::uint32_t kNextPlayer = 0x0052e5f0;
constexpr std::uint32_t kEndCompetition = 0x0052e5ac;
constexpr std::uint32_t kOneMinute = 0x0052e500;
constexpr std::uint32_t kTwoMinutes = 0x0052e504;
constexpr std::uint32_t kFiveMinutes = 0x0052e508;
constexpr std::uint32_t kTenMinutes = 0x0052e50c;
constexpr std::uint32_t kSoundBack = 0x0052e5d8;

// the sound levels menu's buffers (their current text is the line's name)
constexpr std::uint32_t kSfxLine = 0x005616c0;
constexpr std::uint32_t kMusicLine = 0x0056175c;
constexpr std::uint32_t kTrackLine = 0x00561734;

}  // namespace

// 0x0044eac0: the front end's per-frame update: the status picks what is polled and where the buttons lead
void FrontScreens::update() {
    bool leave = false;                       // the frame ends the front end (local_58)
    Buttons b;
    std::uint32_t sound = 0xffffffffu;        // the sound a menu choice plays (ebx)
    {
        const std::uint32_t status = global_word(m_, front::kStatus);
        if ((global_byte(m_, 0x0056b038) == 0 || status != 1) && status != 0x12) {
            b = poll_buttons();
        }
    }
    // both pads held (the two start buttons, or the pair on the second pad) for two seconds restarts the game
    if (!((global_word(m_, 0x0052bf38) != 0 && global_byte(m_, 0x0056b0e8) != 0 && global_byte(m_, 0x0056b0d8) != 0) || (global_byte(m_, 0x0056b254) != 0 && global_byte(m_, 0x0056b244) != 0))) {
        set_global_word(m_, 0x00561c48, 0);
    } else {
        if (global_word(m_, 0x00561c48) == 0) {
            set_global_word(m_, 0x00561c48, 1);
            set_global_word(m_, 0x00561c4c, global_word(m_, front::kSimTick));
        }
        if (global_word(m_, front::kSimTick) - global_word(m_, 0x00561c4c) > 0x78) {
            set_global_word(m_, 0x00561c48, 0);
            leave = true;
            set_global_word(m_, 0x0056a8d0, 8);
            restart_comp_frig();
            set_global_byte(m_, 0x00561cbc, 1);        // FUN_00452dd0
        }
    }
    set_global_word(m_, 0x0052bf38, 1);

    auto menu = [&](std::uint32_t variable) { return word_at(m_, variable); };
    auto choice = [&](std::uint32_t variable, std::uint32_t cell) { return menu_choice(variable, cell); };
    auto competition_name = [&]() { return competition_line(); };
    auto skater_flags = [&]() {          // the player objects the statistics are folded into
        for (std::uint32_t slot : {front::kPlayer, front::kPlayer2}) {
            const std::uint32_t player = word_at(m_, slot);
            if (player != 0) {
                set_word_at(m_, player + 0x16c, word_at(m_, player + 0x16c) + word_at(m_, player + 0x2a8));
                set_word_at(m_, player + 0x2a8, 0);
            }
        }
    };

    {   // a host request outlives only the frame it is answered in: the level was not in play (a menu, a load), so the player no longer wants it
        const std::uint32_t status = global_word(m_, front::kStatus);
        int& request = host_options().request;
        if (request != 0 && status != 0 && !(status == 1 && request == 3)) {
            request = 0;
        }
    }
    switch (global_word(m_, front::kStatus)) {
    case 0: {
        // gameplay: a pause (start, or a lost controller) puts up the pause menu
        const std::uint32_t skater = global_word(m_, kSkater);
        const std::uint32_t second = word_at(m_, skater + 0x20);
        if (global_word(m_, 0x0056e5d8) == 2 && b.accept != 0) {
            set_global_word(m_, 0x0056e698, 0);
            set_global_word(m_, 0x0056a8e0, 0);
            engine(0x004cbf30, {skater});
            engine(0x004cbf30, {second});
            engine(0x0044e9c0);
            break;
        }
        const bool by_service = (engine_value(0x004de0a0, {1, b.select}) & 0xff) != 0;
        int& host_request = host_options().request;
        const bool by_host = host_request == 1 || host_request == 2;
        if (host_request == 1) {
            host_request = 0;
        } else if (host_request == 2) {
            host_request = 3;   // the pause menu's first frame takes the Retry choice
        }
        if (by_host && b.select == 0) {
            set_global_word(m_, 0x005618d4, 1);   // the pause is the first pad's Start (poll_buttons' latch): the menu is its own, and the check below has a pad to name
            set_global_word(m_, 0x005618d0, 0);
        }
        const bool pause = by_host || by_service || b.select != 0 || global_word(m_, 0x0056b144) == 0 ||
                           (global_word(m_, front::kPlayerCount) == 2 && global_word(m_, 0x0056b2b0) == 0 && global_byte(m_, 0x00561990) == 0 && global_word(m_, kLevelEventLatch) != 1);
        if (!pause) {
            break;
        }
        set_global_word(m_, 0x00561c04, 1);
        engine(0x004dc020, {1});
        set_word_at(m_, global_word(m_, front::kPlayer) + 0x2e30, global_word(m_, kAnimClock) + 1);
        if (global_word(m_, front::kPlayer2) != 0) {
            set_word_at(m_, global_word(m_, front::kPlayer2) + 0x2e30, global_word(m_, kAnimClock) + 1);
        }
        set_word_at(m_, global_word(m_, front::kPlayer) + 0x2e34, global_word(m_, kAnimClock) + 1);
        if (global_word(m_, front::kPlayer2) != 0) {
            set_word_at(m_, global_word(m_, front::kPlayer2) + 0x2e34, global_word(m_, kAnimClock) + 1);
        }
        mess_delete_all();
        engine(0x00489670, {kInputActionStates});
        engine(0x00489670, {kPad2States});
        set_global_word(m_, 0x00561c08, 0);
        engine(0x004ad9c0);
        play(2, 0x1000);
        engine(0x00489850, {0, 0});
        engine(0x00489850, {0, 1});
        check(global_word(m_, front::kMenuPaused) == 0, 0x0052c964);
        const std::uint32_t paused = make_menu(front::kMenuPaused, 0x14f);
        cmenu_add_entry(paused, str(kContinue));
        cmenu_add_entry(paused, competition_name());
        cmenu_add_entry(paused, str(kSkipToRestart));
        if (global_word(m_, 0x0056a8ec) == 0 || global_word(m_, front::kPlayerCount) == 2) {
            cmenu_entry_off(paused, str(kSkipToRestart));
        }
        if (global_int(m_, front::kCurrentLevel) > 9 && global_int(m_, front::kCurrentLevel) != 0xd) {
            cmenu_entry_off(paused, str(kSkipToRestart));
        }
        cmenu_add_entry(paused, str(kSoundLevels));
        cmenu_add_entry(paused, str(global_word(m_, front::kGameMode) == 7 ? kEndGame : kEndRun));
        if (host_options().quick_play) {
            cmenu_add_entry(paused, str(kQuit));   // the quick start's way back to the launcher (case 3); retail's pause menu has no Quit line
        }
        if (global_byte(m_, 0x006a3b21) != 0) {
            cmenu_entry_off(paused, competition_name());
            cmenu_entry_off(paused, str(kSoundLevels));
            if ((global_word(m_, 0x006a18bc) & 0x2000) != 0) {
                cmenu_entry_off(paused, str(kContinue));
            }
        }
        cmenu_centre_y(paused);
        set_word_at(m_, paused + 0x10, word_at(m_, paused + 0x10) + 0xd);
        if (global_word(m_, 0x0056b144) == 0 || (global_word(m_, front::kPlayerCount) != 1 && global_word(m_, 0x0056b2b0) == 0)) {
            set_status(0xc35, 4);
        } else {
            set_status(0xc0c, 1);
            if (global_byte(m_, 0x006a3b21) != 0) {
                engine(0x004df6d0);
            }
            check(global_word(m_, 0x005618d4) != 0 || global_word(m_, 0x005618d0) != 0, 0x0052c944);
            if (global_word(m_, 0x005618d4) == 0 || global_word(m_, 0x005618d0) == 0) {
                if (global_word(m_, front::kGameMode) != 7) {
                    set_global_word(m_, 0x00568a58, global_word(m_, 0x005618d0));
                    set_global_word(m_, 0x00568a5c, global_word(m_, 0x005618d4));
                }
            } else {
                set_global_word(m_, 0x00568a58, 0);
                set_global_word(m_, 0x00568a5c, 0);
            }
        }
        break;
    }
    case 1: {
        // the pause menu
        draw_paused_bitmap();
        if (global_word(m_, 0x0056a8f0) == 0) {
            hint_one();
        } else {
            hint_none();
        }
        check(global_word(m_, front::kMenuPaused) != 0, 0x0052c934);
        if (global_word(m_, 0x00561c08) != 0) {
            set_global_word(m_, 0x00561c0c, global_word(m_, 0x00561c0c) + 0x2b2);
            set_global_word(m_, 0x00561c08, global_word(m_, 0x00561c08) - 2);
            if (global_int(m_, 0x00561c08) < 0) {
                set_global_word(m_, 0x00561c08, 0);
                set_global_word(m_, 0x00561c0c, 0);
            }
        }
        {
            const std::int32_t cosine = s(engine_value(0x004f3980, {global_word(m_, 0x00561c0c)}));
            const std::int32_t product = cosine * global_int(m_, 0x00561c08);
            set_word_at(m_, menu(front::kMenuPaused) + 0xc, u(((product + ((product >> 31) & 0xfff)) >> 12) + 0x100));
        }
        if (global_word(m_, 0x00561c04) == 0) {
            leave = true;
        }
        if (global_word(m_, 0x0056b144) == 0 || (global_word(m_, front::kPlayerCount) == 2 && global_word(m_, 0x0056b2b0) == 0)) {
            set_status(0xc74, 4);
        }
        if (global_byte(m_, 0x0056b038) != 0) {
            if (engine_value(0x004af6b0, {1}) != 0) {
                set_global_word(m_, 0x00561c08, 0x20);
                set_global_word(m_, 0x00561c0c, 0);
            }
            engine(0x00489670, {kInputActionStates});
            engine(0x00489670, {kPad2States});
            break;
        }
        set_global_word(m_, 0x0056c574, 0);
        set_global_word(m_, 0x0056c578, 0);
        check(global_word(m_, front::kMenuPaused) != 0, 0x0052c934);
        if (global_word(m_, 0x0056a8ec) != 0 && global_word(m_, front::kPlayerCount) == 1) {
            cmenu_entry_on(menu(front::kMenuPaused), str(kSkipToRestart));
        } else {
            cmenu_entry_off(menu(front::kMenuPaused), str(kSkipToRestart));
        }
        if (global_int(m_, front::kCurrentLevel) > 9 && global_int(m_, front::kCurrentLevel) != 0xd) {
            cmenu_entry_off(menu(front::kMenuPaused), str(kSkipToRestart));
        }
        if (global_byte(m_, 0x006a3b21) != 0) {
            cmenu_entry_off(menu(front::kMenuPaused), str(kSkipToRestart));
        }
        cmenu_update(menu(front::kMenuPaused));
        if (host_options().request == 3) {   // the host's retry: the pause menu's Retry choice
            host_options().request = 0;
            restart_comp_frig();
            leave = true;
            set_global_word(m_, 0x0056a8d0, 7);
            engine(0x004195e0);
        }
        if (b.accept != 0) {
            if (choice(front::kMenuPaused, kEndRun)) {
                sound = 5;
                engine(0x004defc0);
                engine(0x00466c10);
                skater_flags();
                engine(0x0044e0f0);
                set_global_word(m_, kLevelEventLatch, 1);
                set_global_word(m_, kLevelEventSecondary, 1);
                for (std::uint32_t slot : {front::kPlayer, front::kPlayer2}) {
                    if (word_at(m_, slot) != 0) {
                        set_word_at(m_, word_at(m_, slot) + 0x30b8, 0);
                        set_word_at(m_, word_at(m_, slot) + 0x2dd4, 0);
                    }
                    if (word_at(m_, slot) != 0) {
                        set_word_at(m_, word_at(m_, slot) + 0x16c, word_at(m_, word_at(m_, slot) + 0x16c) + word_at(m_, word_at(m_, slot) + 0x2a8));
                        set_word_at(m_, word_at(m_, slot) + 0x2a8, 0);
                    }
                }
            }
            if (choice(front::kMenuPaused, kContinue)) {
                sound = 5;
                leave = true;
            }
            if (choice(front::kMenuPaused, replay_level_flag() ? kRestartCompetition : kRetry)) {
                restart_comp_frig();
                leave = true;
                set_global_word(m_, 0x0056a8d0, 7);
                engine(0x004195e0);
            }
            if (choice(front::kMenuPaused, kSkipToRestart)) {
                sound = 5;
                set_status(0xceb, 2);
            }
            if (choice(front::kMenuPaused, kQuit)) {
                if (!host_options().quick_play && (engine_value(0x004de150) & 0xff) != 0) {
                    leave = true;
                    set_global_word(m_, 0x0056a8d0, 8);
                    restart_comp_frig();
                    play(5);
                    break;
                }
                find_line(front::kMenuYesNo, kNo);
                sound = 5;
                set_status(0xcfd, 3);
            }
            if (choice(front::kMenuPaused, kSoundLevels)) {
                sound = 5;
                set_status(0xd04, 0x2f);
                engine(0x004abf10, {0x005616f0});
            }
            if (choice(front::kMenuPaused, kEndGame)) {
                sound = 5;
                set_global_word(m_, kWaitTicks, 0);
                set_global_word(m_, 0x00568814, 0);
                set_status(0xd10, 0xe);
            }
        }
        if (b.back != 0) {
            sound = 5;
            leave = true;
        }
        play(sound, 0x1000);
        break;
    }
    case 2: {
        // the cheat restart list (skip to restart)
        if (global_word(m_, front::kMenuRestart) == 0) {
            const std::uint32_t created = make_menu(front::kMenuRestart, 0x14f);
            set_word_at(m_, created + 0x14, 0x12);
            check(global_word(m_, kNumCheatRestarts) != 0, 0x0052c920);
            for (std::uint32_t k = 0; s(k) < global_int(m_, kNumCheatRestarts); ++k) {
                const std::uint32_t name = word_at(m_, kCheatRestartNames + 4 * k);
                std::string prefix;
                for (std::uint32_t c = 0; c < 3; ++c) {
                    prefix.push_back(static_cast<char>(byte_at(m_, name + c)));
                }
                scratch_text(0x40, prefix);
                if (!names_equal(kScratchLine + 0x40, 0x0052c91c, 0xffffffffu)) {
                    cmenu_add_entry(created, name);
                }
            }
            cmenu_centre_y(created);
        }
        cmenu_update(menu(front::kMenuRestart));
        if (b.accept != 0) {
            const std::uint32_t restart = menu(front::kMenuRestart);
            const std::uint32_t name = word_at(m_, restart + 0x18 * (byte_at(m_, restart + 6) + 1));
            engine(0x004c4c50, {name}, m_.cstring(name, 0x40));
            if (global_word(m_, front::kMenuRestart) != 0) {
                virtual_delete_menu(global_word(m_, front::kMenuRestart));
            }
            set_global_word(m_, front::kMenuRestart, 0);
            leave = true;
            set_global_word(m_, 0x0056a8d0, 7);
            play(5);
        }
        if (b.back != 0) {
            if (global_word(m_, front::kMenuRestart) != 0) {
                virtual_delete_menu(global_word(m_, front::kMenuRestart));
            }
            set_global_word(m_, front::kMenuRestart, 0);
            set_status(0xd42, 1);
            play(2);
        }
        if (global_word(m_, front::kMenuRestart) != 0) {
            cmenu_display(menu(front::kMenuRestart));
        }
        break;
    }
    case 3:
        // quit? (yes/no)
        cmenu_update(menu(front::kMenuYesNo));
        if (b.accept != 0) {
            if (!choice(front::kMenuYesNo, kYes)) {
                set_status(0xd57, 1);
                play(2);
            } else if (host_options().quick_play) {
                host_options().quit_to_launcher = true;   // the host ends the program at its next poll
                play(5);
            } else {
                leave = true;
                set_global_word(m_, 0x0056a8d0, 8);
                restart_comp_frig();
                play(5);
            }
        }
        if (b.back != 0) {
            play(2);
            set_status(0xd5e, 1);
        }
        break;
    case 4:
        // the controller was pulled
        if (global_word(m_, 0x0056b144) != 0 && (global_word(m_, front::kPlayerCount) == 1 || global_word(m_, 0x0056b2b0) != 0)) {
            set_status(0xd65, 1);
        }
        break;
    case 5:
        // save game? (yes/no)
        cmenu_update(menu(front::kMenuYesNo));
        if (b.accept != 0) {
            play(5);
            engine(0x004498a0, {0x20, 1});
            if (!choice(front::kMenuYesNo, kYes)) {
                set_status(0x117e, 0xe);
            } else if (global_byte(m_, 0x005614c4) == 0) {
                engine(0x0047a0b0, {5, 0xe, 0});
                set_status(0x117a, 0x12);
            } else {
                engine(0x00448c60);
                engine(0x00432840);
                engine(0x00432860, {3, kScratchLine});
                // the service stores two words through the pointer (retail reads them from the local after the call): live they are in memory, a replay scripts them
                const std::uint32_t first = s_.live() ? word_at(m_, kScratchLine) : u(s_.value(engine_name(0x00432860)));
                const std::uint32_t second = s_.live() ? word_at(m_, kScratchLine + 4) : u(s_.value(engine_name(0x00432860)));
                engine(0x00432850);
                engine(0x00448c90);
                set_global_word(m_, 0x00561bec, first);
                engine(0x0047a480, {first, second, 0, 1});
                set_status(0x1175, 0x12);
            }
        }
        if (b.back != 0) {
            play(2);
            set_status(0x1183, 0xe);
        }
        break;
    case 6:
        cmenu_update(menu(front::kMenuYesNo));
        if (b.accept != 0) {
            if (!choice(front::kMenuYesNo, kYes)) {
                set_status(0xd8e, 8);
                play(2);
            } else {
                play(5);
                engine(0x0047a0b0, {6, 8, 0});
                set_status(0xd8a, 0x12);
            }
        }
        if (b.back != 0) {
            play(2);
            set_status(0xd95, 0xe);
        }
        break;
    case 7:
        if (host_options().quick_play) {
            set_status(0x10b7, 0xe);   // "Save Career?" after an unlock message: the quick start never saves, so its No
            break;
        }
        cmenu_update(menu(front::kMenuYesNo));
        if (b.accept != 0) {
            play(5);
            engine(0x004498a0, {0x20, 1});
            if (!choice(front::kMenuYesNo, kYes)) {
                set_status(0x10b7, 0xe);
            } else {
                engine(0x0047a0b0, {7, 0xe, 0});
                set_status(0x10b4, 0x12);
            }
        }
        if (b.back != 0) {
            play(2);
            if (global_word(m_, front::kGameMode) == 1 && !replay_level_flag()) {
                set_status(0x10be, 0xb);
            } else {
                set_status(0x10c2, 0xe);
            }
        }
        break;
    case 8:
        engine(0x004498f0);
        if (engine_value(0x00449800) != 0) {
            set_global_word(m_, 0x0056a8d0, 8);
            restart_comp_frig();
            leave = true;
        }
        break;
    case 9:
    case 10:
        engine(0x004498f0);
        if (engine_value(0x00449800) != 0) {
            leave = true;
            set_global_word(m_, 0x0056a8d0, u((global_word(m_, front::kStatus) == 9 ? 1 : 0) + 3));
        }
        break;
    default:
        update_late(b, leave, sound);
        break;
    }
    // the end of the frame: leaving the front end resumes the game
    if (((global_byte(m_, 0x006a3b21) == 0 || (global_word(m_, 0x006a18bc) & 0x2000) == 0) && leave) || (engine_value(0x004de0a0, {0, 0}) & 0xff) != 0) {
        set_global_word(m_, 0x00561c04, 0);
        engine(0x004dc020, {0});
        set_global_word(m_, 0x00568a58, 0);
        set_global_word(m_, 0x00568a5c, 0);
        if (global_word(m_, front::kStatus) != 0x16) {
            set_status(0x1219, 0);
        }
        if (global_word(m_, front::kMenuPaused) != 0) {
            virtual_delete_menu(global_word(m_, front::kMenuPaused));
        }
        set_global_word(m_, front::kMenuPaused, 0);
        const std::uint32_t next = global_word(m_, 0x0056a8d0);
        if (next != 8 && next != 4 && next != 3) {
            engine(0x004ad9d0);
        }
        set_word_at(m_, global_word(m_, front::kPlayer) + 0x2e30, global_word(m_, kAnimClock) + 1);
        if (global_word(m_, front::kPlayer2) != 0) {
            set_word_at(m_, global_word(m_, front::kPlayer2) + 0x2e30, global_word(m_, kAnimClock) + 1);
        }
    }
    (void)skater_flags;
}

}  // namespace opentony::runtime::frontend
