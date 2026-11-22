#include "auto_views.hpp"
#include "front.hpp"
#include "host_options.hpp"
#include "score_hooks.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

constexpr std::uint32_t kAnimClock = 0x005685f4;
constexpr std::uint32_t kWaitTicks = 0x005616e0;
constexpr std::uint32_t kLevelTable = 0x0053c240;
constexpr std::uint32_t kLevelRecords = 0x0053c1f8;
constexpr std::uint32_t kCursor = 0x0056e774;               // ReplayHighlightCursor
constexpr std::uint32_t kReplayLevel = 0x00561c90;
constexpr std::uint32_t kTextEffect = 0x005617a0;
constexpr std::uint32_t kCompetition = 0x0055fe28;
constexpr std::uint32_t kTextEffectFinished = 0x004b9de0;
constexpr std::uint32_t kNameEntry = 0x00561bf8;
constexpr std::uint32_t kHighScores = 0x00569ebc;


constexpr std::uint32_t kContinue = 0x0052e968;
constexpr std::uint32_t kQuit = 0x0052e970;
constexpr std::uint32_t kYes = 0x0052e410;
constexpr std::uint32_t kNo = 0x0052e414;
constexpr std::uint32_t kSoundBackCell = 0x0052e5d8;

}  // namespace

// The statuses from 0xb on: the goals and unlock screens (0xb, 0x31..0x37), the save and quit questions (0xc, 0x11), the statistics and end menus (0xd, 0xe), the replay menus
// (0xf, 0x10, 0x1a), the save wait (0x12), the competition screens (0x14..0x19, 0x24), the horse game screens (0x2a, 0x2b), the high-score entry (0x2e), the sound levels (0x2f) and
// the time select (0x33).
void FrontScreens::update_late(const Buttons& b, bool& leave, std::uint32_t& sound) {
    (void)sound;
    const std::uint32_t status = global_word(m_, front::kStatus);
    auto choice = [&](std::uint32_t variable, std::uint32_t cell) { return menu_choice(variable, cell); };
    auto ready = [&]() { return ((global_int(m_, front::kAnimScale) * 10) >> 8) < global_int(m_, kWaitTicks); };
    auto more_wait = [&]() { set_global_word(m_, kWaitTicks, global_word(m_, kWaitTicks) + 1); };
    auto update_menu = [&](std::uint32_t variable) { cmenu_update(word_at(m_, variable)); };
    auto wait_for_pad_release = [&]() {
        while (global_byte(m_, 0x0056b028) != 0 || global_byte(m_, 0x0056b194) != 0 || global_byte(m_, 0x0056b0d8) != 0 || global_byte(m_, 0x0056b244) != 0) {
            game_frame_tick();
            input_update_action_states();
        }
    };
    auto highlights_choice = [&](std::uint32_t variable) {
        // "play replay" and "highlights" start the replay (the front end ends); both set the cursor the replay reads
        if (choice(variable, 0x0052e534)) {
            set_global_word(m_, kCursor, 0xffffffffu);
        } else if (choice(variable, 0x0052e538)) {
            set_global_word(m_, kCursor, 1);
        } else {
            return false;
        }
        set_global_word(m_, 0x0056a8d0, 7);
        leave = true;
        engine(0x004cb6e0);
        return true;
    };

    switch (status) {
    case 0xb: {
        // the level goals screen after a level
        if (global_word(m_, front::kGotNewStatus) != 0) {
            set_global_word(m_, front::kGotNewStatus, 0);
            set_global_word(m_, 0x005616dc, global_word(m_, front::kSimTick));
            set_global_word(m_, 0x00561b60, 0);
            set_global_word(m_, 0x00561b78, 0);
            set_global_word(m_, 0x00561b64, 0);
            set_global_byte(m_, 0x0056b029, 0);
            set_global_word(m_, 0x00561754, 0xffffffffu);
            set_global_half(m_, 0x00561b68, 0);
        }
        const std::uint32_t level = global_word(m_, front::kCurrentLevel);
        sprintf_to(0x00561b44, retail_format(m_.cstring(str(0x0052e430), 0x40), {m_.cstring(kLevelRecords + level * 0x1ac, 0x40)}, {}));
        check(m_.cstring(0x00561b44, 0x40).size() < 0x19, 0x0052c8c8);
        mess_justify(0);
        mess_scale(0xff);
        mess_rgb(0x80, 0x80, 0);
        mess_text(0x100, 0x19, 0x00561b44);
        const std::uint32_t config = 0x00568a6c + word_at(m_, global_word(m_, front::kPlayer) + 0x2cc0) * 0x104;
        std::int32_t total = int_at(m_, config + 4);
        std::int32_t delay = 0x14;
        std::int32_t y = 0x2d;
        std::uint32_t text = 0x0056193c;
        std::uint32_t k = 0;
        for (; delay < 0xdc; ++k, delay += 0x14, y += 0xf, text += 8) {
            std::int32_t t = delay - s(global_word(m_, front::kSimTick) - global_word(m_, 0x005616dc));
            std::int32_t reward = int_at(m_, 0x0053c2bc + level * 0x1ac + k * 0x18);
            const bool just = engine_value(0x004140c0, {k}) != 0;
            bool draw = true;
            if (just) {
                if (t >= 0) {
                    total -= reward;
                    reward = 0;
                    draw = t < 0x14;
                } else {
                    if (t <= -0x78) {
                        if (global_int(m_, 0x00561754) != -1) {
                            engine(0x004ad9f0, {global_word(m_, 0x00561754)});
                            play(0x12);
                            set_global_word(m_, 0x00561754, 0xffffffffu);
                        }
                    } else {
                        const std::int32_t x = reward * t;
                        const std::int32_t high = static_cast<std::int32_t>((static_cast<std::int64_t>(x) * 0x77777777LL) >> 32);
                        std::int32_t q = high - x;
                        const std::int32_t next = (q >> 6) - (q >> 31);
                        total += next - reward;
                        reward = next;
                        if (global_int(m_, 0x00561754) == -1) {
                            s_.note(kSvcSfx, {0x17f, 0x2000, 0});
                            set_global_word(m_, 0x00561754, u(s_.value(kSvcSfx, -1)));
                        }
                    }
                    t = 0;
                }
            } else {
                draw = t < 0x14;
            }
            if (!draw) {
                continue;
            }
            mess_rgb(0x60, 0x20, 0x20);
            mess_scale(0xc0);
            if (engine_value(0x00414030, {k}) != 0) {
                if (engine_value(0x004140c0, {k}) != 0) {
                    if (byte_at(m_, 0x00561b60 + k) == 0) {
                        play(0xbc);
                        set_byte_at(m_, 0x00561b60 + k, 1);
                    }
                    const std::int32_t d = 10 - t;
                    const std::int32_t distance = d < 0 ? -d : d;
                    mess_scale(u(((10 - distance) * 0x28) / 10 * 4 + 0xc0));
                    const std::int32_t phase = s(global_word(m_, front::kSimTick) % 0x14);
                    const std::int32_t e = 10 - phase;
                    const std::int32_t v = 10 - (e < 0 ? -e : e);
                    mess_rgb(u(0x80 - v * 6), 0x80, u(v * 3 + 0x80));
                } else {
                    mess_rgb(0x40, 0x40, 0x40);
                }
            }
            mess_justify(1);
            mess_text(0x1e, y, word_at(m_, 0x0053c2c0 + level * 0x1ac + k * 0x18));
            std::uint32_t shown;
            if (engine_value(0x004140c0, {k}) == 0 && engine_value(0x00414030, {k}) != 0) {
                sprintf_to(text, m_.cstring(str(0x0052e444), 0x40));
            } else {
                sprintf_to(text, retail_format(m_.cstring(0x005252c4, 0x10), {}, {reward}));
            }
            shown = text;
            mess_justify(2);
            mess_text(0x1e2, y, shown);
        }
        mess_rgb(0x80, 0x80, 0x80);
        mess_scale(0xff);
        mess_justify(1);
        const std::int32_t total_y = static_cast<std::int32_t>(k) * 15 + 0x35;
        mess_text(0x1e, total_y, str(0x0052e434));
        sprintf_to(0x00561b90, retail_format(m_.cstring(0x005252c4, 0x10), {}, {total}));
        mess_justify(2);
        mess_text(0x1e2, total_y, 0x00561b90);
        std::uint32_t accept = b.accept;
        if (global_word(m_, front::kSimTick) - global_word(m_, 0x005616dc) > 0x140) {
            hint_text(str(kContinue), 0);
            set_global_word(m_, 0x005616dc, global_word(m_, front::kSimTick) - 0x141);
        } else if (accept != 0) {
            accept = 0;
            set_global_word(m_, 0x005616dc, global_word(m_, front::kSimTick) - 0x141);
        }
        if (global_int(m_, kWaitTicks) < 0x1f) {
            accept = 0;
            set_global_word(m_, kWaitTicks, global_word(m_, kWaitTicks) + 1);
        } else if (accept != 0) {
            play(5);
            if ((engine_value(0x00452a80, {0xc}) & 0xff) == 0) {
                set_status(0x131, 0xc);
                engine(0x004dc020, {1});
            }
            find_line(front::kMenuYesNo, kYes);
        }
        engine(0x004dc0c0);
        break;
    }
    case 0xc:
        if (host_options().quick_play) {
            set_status(0xf71, 0xd);   // "Save Career?" after an unlock message: the quick start never saves, so its No
            break;
        }
        update_menu(front::kMenuYesNo);
        if (b.accept != 0) {
            play(5);
            engine(0x004498a0, {0x20, 1});
            if (!choice(front::kMenuYesNo, kYes)) {
                set_status(0xf71, 0xd);
            } else {
                engine(0x0047a0b0, {0xc, 0xd, 0});
                set_status(0xf6e, 0x12);
            }
        }
        if (b.back != 0) {
            play(2);
            set_status(0xf76, 0xb);
        }
        break;
    case 0xd: {
        // the statistics (and the horse game's results): continue goes on to the high scores, the save or the end menu
        set_global_word(m_, kWaitTicks, u(((global_int(m_, front::kAnimScale) * 10) >> 8) + 1));
        std::uint32_t accept = b.accept;
        const bool horse = global_word(m_, front::kGameMode) == 7;
        if (horse) {
            set_global_word(m_, 0x00568814, 0);
            set_global_word(m_, 0x00568650, global_word(m_, 0x00568650) - 1);
            const std::uint32_t state = global_word(m_, 0x00561998);
            if (global_int(m_, 0x00568650) < 1 && state != 2) {
                accept = 1;
            }
            if (state == 2 && global_word(m_, 0x00561be8) != 0 && engine_value(kTextEffectFinished, {kTextEffect}) != 0) {
                set_global_word(m_, kWaitTicks, 0);
                set_status(0xfee, 0xe);
                engine(0x004dc020, {1});
            }
        }
        if (accept == 0) {
            break;
        }
        set_global_word(m_, 0x00561b70, 0);
        play(5);
        set_global_word(m_, 0x00561730, 0);
        set_global_byte(m_, 0x00561be4, 0);
        if (horse) {
            if (global_word(m_, 0x00561998) == 2) {
                set_global_word(m_, 0x00561be8, 1);
                set_global_word(m_, 0x0056867c, 0);
                const std::uint32_t name = global_word(m_, 0x0056e1b8);
                engine(0x004c4c50, {name}, m_.cstring(name, 0x40));
            } else {
                set_global_word(m_, 0x00568660, global_word(m_, 0x00561758));
                set_global_word(m_, 0x00568680, global_word(m_, 0x00561998));
                set_global_word(m_, 0x00568a60, global_word(m_, 0x0056174c));
                set_global_word(m_, 0x0056a854, global_word(m_, 0x00561774));
                set_global_word(m_, 0x0056867c, global_word(m_, 0x00561938));
                set_global_word(m_, 0x00568a64, global_word(m_, 0x00561750));
                const std::uint32_t name = word_at(m_, 0x0056e1b8 + 4 * global_word(m_, 0x00561938));
                engine(0x004c4c50, {name}, m_.cstring(name, 0x40));
                leave = true;
                set_global_word(m_, 0x0056a8d0, 7);
            }
            break;
        }
        if (global_int(m_, 0x0052bf34) != -1) {
            set_status(0x101b, 0x2e);
            reset_score_slide();
            break;
        }
        if ((engine_value(0x004de150) & 0xff) == 0) {
            set_status(0x102e, 0xe);
            engine(0x004dc020, {1});
            break;
        }
        leave = true;
        set_global_word(m_, 0x0056a8d0, 8);
        restart_comp_frig();
        play(5);
        break;
    }
    case 0xe: {
        // the end-of-run menu
        if (global_byte(m_, 0x00561bf0) != 0) {
            set_global_byte(m_, 0x00561bf0, 0);
            set_status(0xdc2, 8);
            break;
        }
        if (!ready()) {
            more_wait();
            break;
        }
        update_menu(front::kMenuEnd);
        if (b.accept == 0) {
            break;
        }
        play(5);
        engine(0x004498a0, {0x10, 1});
        bool quit = choice(front::kMenuEnd, kQuit);
        if (quit && host_options().quick_play) {
            find_line(front::kMenuYesNo, kNo);   // the quick start's Quit: "Are you sure?", then back to the launcher (case 0x11)
            set_status(0xdf1, 0x11);
            break;
        }
        bool editor = false;
        bool main = false;
        if (!quit) {
            editor = choice(front::kMenuEnd, 0x0052e55c);
            if (!editor) {
                main = choice(front::kMenuEnd, 0x0052e568);
            }
        }
        if (!quit && !editor && !main) {
            if (choice(front::kMenuEnd, replay_level_flag() ? 0x0052e554 : 0x0052e550)) {
                leave = true;
                set_global_word(m_, 0x0056a8d0, 7);
                engine(0x004195e0);
                restart_comp_frig();
            } else if (choice(front::kMenuEnd, 0x0052e564)) {
                select_play_replay();
                set_status(0xdfe, 0xf);
            } else if (choice(front::kMenuEnd, 0x0052e54c)) {
                find_line(front::kMenuYesNo, kYes);
                set_status(0xe04, 5);
            } else if (choice(front::kMenuEnd, 0x0052e5e0)) {
                find_line(front::kMenuYesNo, kYes);
                set_status(0xe09, 5);
            } else if (choice(front::kMenuEnd, 0x0052e558)) {
                set_global_word(m_, 0x00561b9c, 1);
                set_status(0xe0f, 8);
            } else if (choice(front::kMenuEnd, 0x0052e560)) {
                set_global_word(m_, 0x00561b9c, 2);
                set_status(0xe15, 8);
            } else if (choice(front::kMenuEnd, 0x0052e510)) {
                set_global_word(m_, 0x00561b9c, 6);
                set_status(0xe1a, 8);
            }
            break;
        }
        if (!quit) {
            if (!main) {
                if (choice(front::kMenuEnd, 0x0052e55c)) {
                    set_global_byte(m_, 0x00561ca8, 1);
                }
            } else {
                set_global_byte(m_, 0x00561ca8, 0);
            }
        } else if (!choice(front::kMenuEnd, 0x0052e568)) {
            if (choice(front::kMenuEnd, 0x0052e55c)) {
                set_global_byte(m_, 0x00561ca8, 1);
            }
        } else {
            set_global_byte(m_, 0x00561ca8, 0);
        }
        if ((engine_value(0x004de150) & 0xff) != 0 && choice(front::kMenuEnd, kQuit)) {
            leave = true;
            set_global_word(m_, 0x0056a8d0, 8);
            restart_comp_frig();
            play(5);
            break;
        }
        if (byte_at(m_, kLevelTable + global_word(m_, front::kCurrentLevel) * 0x1ac) == 0 && cmenu_is_entry_on(global_word(m_, front::kMenuEnd), str(0x0052e54c)) && global_byte(m_, 0x0056acf8) == 0) {
            set_status(0xde6, 6);
            find_line(front::kMenuYesNo, kYes);
            engine(0x004dc020, {1});
        } else {
            find_line(front::kMenuYesNo, kNo);
            set_status(0xdf1, 0x11);
        }
        break;
    }
    case 0xf:
    case 0x10: {
        const std::uint32_t variable = status == 0xf ? front::kMenuReplayFull : front::kMenuReplayShort;
        if (status == 0x10 && global_byte(m_, 0x00561990) != 0) {
            engine(0x004cbc90);
            set_status(0xeb8, 8);
        }
        if (!ready()) {
            more_wait();
            break;
        }
        update_menu(variable);
        if (b.accept != 0) {
            set_global_word(m_, kCursor, 0);
            play(5);
            engine(0x004498a0, {0x20, 1});
            if (choice(variable, 0x0052e548)) {
                engine(0x004cbc90);
                set_status(status == 0xf ? 0xe34 : 0xec8, status == 0xf ? 0xe : 8);
            } else if (!highlights_choice(variable) && status == 0xf) {
                if (choice(variable, 0x0052e53c)) {
                    engine(0x0047a220, {0xf, 0xe, global_word(m_, front::kPlayerCount), 0});
                    set_status(0xe49, 0x12);
                } else if (choice(variable, 0x0052e540)) {
                    engine(0x0047b1a0, {0x0052c910}, m_.cstring(0x0052c910, 0x20));
                    set_status(0xe4e, 8);
                }
            }
            wait_for_pad_release();
        }
        if (b.back != 0) {
            play(2);
            if (status == 0x10) {
                engine(0x004cbc90);
            }
            set_status(status == 0xf ? 0xe5e : 0xee8, status == 0xf ? 0xe : 8);
        }
        break;
    }
    case 0x11: {
        update_menu(front::kMenuYesNo);
        const bool early = global_byte(m_, 0x0055ff1c) < 3 && replay_level_flag();
        if (b.accept != 0) {
            if (!choice(front::kMenuYesNo, kYes)) {
                play(2);
                if (!(global_byte(m_, 0x0055ff1c) < 3 && replay_level_flag())) {
                    set_status(0xdad, 0xe);
                } else {
                    set_status(0xda9, 0x19);
                    find_line(front::kMenuHeat, 0x0052e5a8);
                }
            } else if (host_options().quick_play) {
                host_options().quit_to_launcher = true;   // the host ends the program at its next poll
                play(5);
            } else {
                set_status(0xda0, 8);
                play(5);
            }
        }
        (void)early;
        if (b.back == 0) {
            break;
        }
        play(2);
        if (!(global_byte(m_, 0x0055ff1c) < 3 && replay_level_flag())) {
            set_status(0xdba, 0xe);
            break;
        }
        set_status(0xdb6, 0x19);
        find_line(front::kMenuHeat, 0x0052e5a8);
        break;
    }
    case 0x12: {
        // waiting for a save to finish
        const std::uint32_t result = engine_value(0x0047ac60);
        if (result == 0) {
            engine(0x0047ace0);
            if ((engine_value(0x0047ad50) & 0xff) != 0 && (engine_value(0x0047adb0) & 0xff) != 0) {
                hint_one();
            } else if ((engine_value(0x0047ad50) & 0xff) != 0) {
                hint_text(str(0x0052e97c), 1);
            } else if ((engine_value(0x0047adb0) & 0xff) != 0) {
                hint_text(str(0x0052e990), 0);
            }
            break;
        }
        const std::uint32_t screen = engine_value(0x0047ae10, {result});
        if (global_byte(m_, 0x005614c4) != 0) {
            check(global_word(m_, 0x00561bec) != 0, 0x0052c980);
            s_.note(kMemDeleteService, {global_word(m_, 0x00561bec)});
            set_global_word(m_, 0x00561bec, 0);
        }
        if (screen == 0xd) {
            engine(0x004dc020, {0});
        }
        set_status(0xb84, screen);
        break;
    }
    case 0x13:
        set_status(0x115a, 0x14);
        more_wait();
        break;
    case 0x14:
        engine(0x0041b540, {kCompetition});
        if (!ready()) {
            more_wait();
            break;
        }
        if (b.accept == 0) {
            break;
        }
        if (global_byte(m_, 0x0055fede) != 0) {
            play(5);
            set_status(0x110b, 0x16);
            set_global_word(m_, kCursor, 0xffffffffu);
            set_global_word(m_, 0x0056a8d0, 7);
            leave = true;
            engine(0x004cb6e0);
            break;
        }
        set_global_word(m_, 0x0055fed0, 1000);
        play(0x34);
        break;
    case 0x16: {
        if (global_word(m_, 0x0055fed4) == 0) {
            set_global_byte(m_, 0x0055fedf, 0);
            for (std::uint32_t k = 0; k < 8; ++k) {
                engine(0x00419cc0, {kCompetition, k});
            }
            set_global_byte(m_, 0x0055fedd, 0);
        }
        set_global_word(m_, 0x0055fed4, global_word(m_, 0x0055fed4) + 1);
        if (b.accept == 0) {
            break;
        }
        if (global_byte(m_, 0x0055fedf) != 0) {
            engine(0x004cbf30, {global_word(m_, 0x0056a960)});
            set_global_word(m_, 0x00561c04, 1);
            engine(0x004abc80);
            engine(0x004ad9e0);
            if ((engine_value(0x00419fb0, {kCompetition}) & 0xff) == 0) {
                set_global_word(m_, 0x0055fed8, 0);
                set_global_byte(m_, 0x0055ff1c, global_byte(m_, 0x0055ff1c) + 1);
                set_global_word(m_, 0x0055fed4, 0);
                set_global_word(m_, 0x0055fed0, 0);
                set_global_byte(m_, 0x0055fedc, 0);
                set_global_byte(m_, 0x0055fede, 0);
                set_global_byte(m_, 0x0055fedf, 0);
                if (global_byte(m_, 0x0055ff1c) < 3) {
                    find_line(front::kMenuHeat, 0x0052e5a8);
                    if ((engine_value(0x00452a80, {0x19}) & 0xff) == 0) {
                        set_status(0x10e9, 0x19);
                    }
                } else {
                    set_global_word(m_, 0x00561b74, 0);
                    if ((engine_value(0x00452a80, {0x17}) & 0xff) == 0) {
                        set_status(0x10f5, 0x17);
                    }
                }
            } else {
                set_status(0x10e2, 0x18);
                find_line(front::kMenuPlayer, 0x0052e5f0);
            }
            break;
        }
        set_global_word(m_, 0x0055fed4, 1000);
        play(0x34);
        break;
    }
    case 0x17:
        scratch_line("");
        set_global_word(m_, kScratchLine, b.accept);
        engine(0x0041a610, {kCompetition, front::kStatus, kScratchLine});
        break;
    case 0x18:
    case 0x19: {
        const std::uint32_t variable = status == 0x18 ? front::kMenuPlayer : front::kMenuHeat;
        const std::uint32_t next = status == 0x18 ? 0x0052e5f0 : 0x0052e5a8;
        if (!ready()) {
            more_wait();
            break;
        }
        update_menu(variable);
        if (b.accept == 0) {
            break;
        }
        play(5);
        engine(0x004498a0, {0x20, 1});
        if (choice(variable, kQuit)) {
            find_line(front::kMenuYesNo, kNo);
            set_status(status == 0x18 ? 0x1072 : 0x104c, 0x11);
            break;
        }
        if (!choice(variable, next)) {
            if (!choice(variable, 0x0052e5ac)) {
                if (choice(variable, 0x0052e564)) {
                    select_play_replay();
                    set_status(status == 0x18 ? 0x1087 : 0x105c, 0x1a);
                }
            } else {
                set_global_word(m_, 0x00561b74, 0);
                set_status(status == 0x18 ? 0x1082 : 0x1057, 0x17);
            }
            break;
        }
        leave = true;
        set_global_word(m_, 0x0056a8d0, 7);
        break;
    }
    case 0x1a:
        if (!ready()) {
            more_wait();
            break;
        }
        update_menu(front::kMenuReplayComp);
        if (b.accept != 0) {
            set_global_word(m_, kCursor, 0);
            play(5);
            engine(0x004498a0, {0x20, 1});
            if (choice(front::kMenuReplayComp, 0x0052e5b0)) {
                engine(0x004cbc90);
                set_status(0xeff, 0x19);
                find_line(front::kMenuHeat, 0x0052e5a8);
            } else if (!highlights_choice(front::kMenuReplayComp)) {
                if (choice(front::kMenuReplayComp, 0x0052e53c)) {
                    engine(0x0047a220, {0x1a, 0x1a, global_word(m_, front::kPlayerCount), 0});
                    set_status(0xf14, 0x12);
                } else if (choice(front::kMenuReplayComp, 0x0052e540)) {
                    engine(0x0047b1a0, {0x0052c910}, m_.cstring(0x0052c910, 0x20));
                    set_status(0xf19, 8);
                }
            }
        }
        if (b.back == 0) {
            break;
        }
        play(2);
        set_status(0xf21, 0x19);
        find_line(front::kMenuHeat, 0x0052e5a8);
        break;
    case 0x24:
        scratch_line("");
        set_global_word(m_, kScratchLine, b.accept);
        engine(0x0041a040, {kCompetition, front::kStatus, kScratchLine});
        break;
    case 0x2a:
        set_global_word(m_, 0x00568814, 1);
        set_word_at(m_, global_word(m_, front::kPlayer) + 0x2e30, global_word(m_, kAnimClock) + 1);
        if (b.accept != 0) {
            leave = true;
        }
        break;
    case 0x2b: {
        set_global_word(m_, 0x00568814, 0);
        const std::int32_t stick = s(engine_value(0x004ac370));
        std::uint32_t input = engine_value(0x0046f650);
        if (stick == -1) {
            input = 2;
        } else if (stick == 1) {
            input = 3;
        }
        if (b.accept != 0) {
            set_global_byte(m_, 0x0056b029, 1);
        }
        if (engine_value(0x00472cc0, {global_word(m_, kNameEntry), input}) == 1) {
            engine(0x004dc020, {0});
            const std::uint32_t name = engine_value(0x00472920, {global_word(m_, kNameEntry)});
            const std::string text = m_.cstring(name, 0x40);
            sprintf_to(0x0056a478, text);
            set_global_word(m_, 0x0055fcc8, static_cast<std::uint32_t>(m_.cstring(0x0056a478, 0x40).size()));
            set_global_word(m_, 0x0056a8c0, 1);
            set_global_word(m_, 0x00568630, 0);
            set_global_word(m_, 0x0056a474, global_word(m_, 0x0055fcc8));
            sprintf_to(0x0055fccc, m_.cstring(0x0056a478, 0x40));
        }
        break;
    }
    case 0x2e: {
        set_global_word(m_, kWaitTicks, u(((global_int(m_, front::kAnimScale) * 10) >> 8) + 1));
        const std::int32_t anchor = global_int(m_, 0x0052bf34);
        const std::uint32_t level = global_byte(m_, 0x005614c4) == 0 ? global_word(m_, front::kCurrentLevel) : 0xd;
        if (anchor != -1) {
            const std::int32_t stick = s(engine_value(0x004ac370));
            const std::uint32_t row = (u(anchor) + level * 8) * 8;
            auto letter = [&]() { return kHighScores + row + global_word(m_, 0x00561bf4); };
            if (stick == -1) {
                set_global_word(m_, 0x0056bb3c, 0x10);
                play(0x11f);
                const std::int8_t c = static_cast<std::int8_t>(byte_at(m_, letter()));
                if (c == ' ' || c == 0) {
                    set_byte_at(m_, letter(), 'A');
                } else {
                    set_byte_at(m_, letter(), u(c + 1));
                    if (static_cast<std::int8_t>(c + 1) > 'Z') {
                        set_byte_at(m_, letter(), ' ');
                    }
                }
            } else if (stick == 1) {
                set_global_word(m_, 0x0056bb3c, 0x10);
                play(0x11f);
                const std::int8_t c = static_cast<std::int8_t>(byte_at(m_, letter()));
                if (c == ' ' || c == 0) {
                    set_byte_at(m_, letter(), 'Z');
                } else {
                    set_byte_at(m_, letter(), u(c - 1));
                    if (static_cast<std::int8_t>(c - 1) < 'A') {
                        set_byte_at(m_, letter(), ' ');
                    }
                }
            }
            if (global_byte(m_, 0x0056b079) != 0 || global_byte(m_, 0x0056b1e5) != 0) {
                set_global_word(m_, 0x0056bb3c, 0x10);
                play(5);
                set_global_word(m_, 0x00561bf4, global_word(m_, 0x00561bf4) - 1);
                set_global_byte(m_, 0x0056b079, 0);
                set_global_byte(m_, 0x0056b1e5, 0);
                if (global_int(m_, 0x00561bf4) < 0) {
                    set_global_word(m_, 0x00561bf4, 2);
                }
            }
            if (global_byte(m_, 0x0056b089) != 0 || global_byte(m_, 0x0056b1f5) != 0) {
                set_global_word(m_, 0x0056bb3c, 0x10);
                play(5);
                set_global_word(m_, 0x00561bf4, global_word(m_, 0x00561bf4) + 1);
                set_global_byte(m_, 0x0056b089, 0);
                set_global_byte(m_, 0x0056b1f5, 0);
                if (global_int(m_, 0x00561bf4) > 2) {
                    set_global_word(m_, 0x00561bf4, 0);
                }
            }
        }
        if (b.accept != 0) {
            play(0x12);
            const std::uint32_t row = (u(global_int(m_, 0x0052bf34)) + level * 8) * 8;
            for (std::uint32_t k = 0; k < 3; ++k) {
                set_byte_at(m_, 0x0056a408 + k, byte_at(m_, kHighScores + k + row));
                set_byte_at(m_, 0x0055fc5c + k, byte_at(m_, kHighScores + k + row));
            }
            // host side (score_hooks.hpp): the letters chosen reach the score file; null (retail, every gate) reads nothing more
            if (const auto hook = scorehooks::hooks().entry_accepted; hook != nullptr) {
                hook(level, global_int(m_, 0x0052bf34));
            }
            set_global_word(m_, 0x0052bf34, 0xffffffffu);
            set_status(0xfc9, 0xe);
            engine(0x004dc020, {1});
        }
        break;
    }
    case 0x2f: {
        update_menu(front::kMenuSound);
        const bool on_sfx = cmenu_choice_is(global_word(m_, front::kMenuSound), 0x005616c0);
        const bool on_music = cmenu_choice_is(global_word(m_, front::kMenuSound), 0x0056175c);
        const bool on_track = cmenu_choice_is(global_word(m_, front::kMenuSound), 0x00561734);
        (void)on_sfx;
        set_global_word(m_, 0x0052be48, (!on_music && !on_track) ? 0 : u(global_int(m_, 0x0055fb40) * 0x15));
        engine(0x004ab9e0);
        auto step = [&](int direction, std::uint32_t pad_a, std::uint32_t pad_b) {
            if (byte_at(m_, pad_a) == 0 && byte_at(m_, pad_b) == 0) {
                return;
            }
            set_byte_at(m_, pad_a, 0);
            set_byte_at(m_, pad_b, 0);
            if (cmenu_choice_is(global_word(m_, front::kMenuSound), 0x005616c0)) {
                if (direction < 0 ? global_int(m_, 0x005618c4) > 0 : global_int(m_, 0x005618c4) < 10) {
                    set_global_word(m_, 0x005618c4, u(global_int(m_, 0x005618c4) + direction));
                }
                set_global_word(m_, 0x0055fb3c, u((global_int(m_, 0x005618c4) * 6) / 5));
                set_global_word(m_, 0x0052be44, u(global_int(m_, 0x0055fb3c) * 0x555));
                set_global_word(m_, 0x0056a2e8, global_word(m_, 0x0055fb3c));
                play(5);
            } else if (cmenu_choice_is(global_word(m_, front::kMenuSound), 0x0056175c)) {
                if (direction < 0 ? global_int(m_, 0x00561994) > 0 : global_int(m_, 0x00561994) < 10) {
                    set_global_word(m_, 0x00561994, u(global_int(m_, 0x00561994) + direction));
                }
                set_global_word(m_, 0x0055fb40, u((global_int(m_, 0x00561994) * 6) / 5));
                set_global_word(m_, 0x0056a2ec, global_word(m_, 0x0055fb40));
            } else if (cmenu_choice_is(global_word(m_, front::kMenuSound), 0x00561734)) {
                engine(0x004abcb0, {u(direction)});
            }
        };
        step(-1, 0x0056b079, 0x0056b1e5);
        step(1, 0x0056b089, 0x0056b1f5);
        const bool stale = (!cmenu_choice_is(global_word(m_, front::kMenuSound), 0x0056175c) && !cmenu_choice_is(global_word(m_, front::kMenuSound), 0x00561734)) || global_word(m_, 0x0052be48) == 0;
        engine(0x004abe20, {stale ? 1u : 0u});
        bool back = b.back != 0;
        if (b.accept == 0) {
            if (!back) {
                break;
            }
        } else if (cmenu_choice_is(global_word(m_, front::kMenuSound), 0x00561734)) {
            engine(0x004abcb0, {1});
            if (!back) {
                break;
            }
        } else if (!cmenu_choice_is(global_word(m_, front::kMenuSound), str(kSoundBackCell))) {
            if (!back) {
                break;
            }
        } else {
            back = true;
        }
        play(2);
        set_status(0x11f4, 1);
        engine(0x004abe20, {1});
        set_global_word(m_, 0x0052be48, u(global_int(m_, 0x0056a2ec) * 0x15));
        engine(0x004ab9e0);
        engine(0x004abf80, {0x005616f0});
        break;
    }
    default:
        update_screens(b, leave, status);
        break;
    }
}

}  // namespace opentony::runtime::frontend
