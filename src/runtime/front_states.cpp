#include "auto_views.hpp"
#include "front.hpp"
#include "score_hooks.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kInputActionStates = 0x0056aff8;     // pad 1's button states; pad 2's follow 0x16c bytes later
constexpr std::uint32_t kPad2States = 0x0056b164;
constexpr std::uint32_t kLevelEventLatch = 0x00568658;
constexpr std::uint32_t kSkater = 0x0056a960;
constexpr std::uint32_t kHorseWord = 0x0056a478;
constexpr std::uint32_t kNameEntry = 0x00561bf8;
constexpr std::uint32_t kFontMess = 0x0056a9a0;
constexpr std::uint32_t kTextEffect = 0x005617a0;
constexpr std::uint32_t kEffectScale = 0x0056a9d0;
constexpr std::uint32_t kLevelTable = 0x0053c240;
constexpr std::uint32_t kHighScores = 0x00569ec0;
constexpr std::uint32_t kWaitTicks = 0x005616e0;

// the pad bytes FUN_0044dda0 reads and clears
constexpr std::uint32_t kButtonA = 0x0056b0d9;
constexpr std::uint32_t kButtonB = 0x0056b245;
constexpr std::uint32_t kAcceptPressed = 0x005618d4;
constexpr std::uint32_t kAcceptPressedB = 0x005618d0;

}  // namespace

// 0x0044dda0: collect this frame's presses and clear them. `select` is a select press on pad 1 or 2 (unless the second pad is being waited for), `accept` is that or a
// confirm edge, `back` a back edge, `any` any of those or a dozen other buttons
FrontScreens::Buttons FrontScreens::poll_buttons() {
    Buttons out;
    if (global_word(m_, 0x0056a900) != 0 && global_word(m_, 0x0056a8e0) != 0) {
        out.select = 0;
    } else {
        out.select = global_byte(m_, kButtonA) != 0 || global_byte(m_, kButtonB) != 0 ? 1 : 0;
        set_global_word(m_, kAcceptPressed, global_byte(m_, kButtonA));
        set_global_word(m_, kAcceptPressedB, global_word(m_, kAcceptPressed) == 0 ? global_byte(m_, kButtonB) : 0);
        set_global_byte(m_, kButtonA, 0);
        set_global_byte(m_, kButtonB, 0);
    }
    out.accept = global_byte(m_, 0x0056b029) != 0 || global_byte(m_, 0x0056b195) != 0 || out.select != 0 ? 1 : 0;
    set_global_byte(m_, 0x0056b029, 0);
    set_global_byte(m_, 0x0056b195, 0);
    out.back = global_byte(m_, 0x0056aff9) != 0 || global_byte(m_, 0x0056b165) != 0 ? 1 : 0;
    set_global_byte(m_, 0x0056aff9, 0);
    set_global_byte(m_, 0x0056b165, 0);
    out.any = out.accept | out.back;
    for (std::uint32_t at : {0x0056b009u, 0x0056b019u, 0x0056b0e9u, 0x0056b039u, 0x0056b049u, 0x0056b059u, 0x0056b069u, 0x0056b175u, 0x0056b185u, 0x0056b255u, 0x0056b1a5u,
                             0x0056b1b5u, 0x0056b1c5u, 0x0056b1d5u}) {
        if (byte_at(m_, at) != 0) {
            set_byte_at(m_, at, 0);
            out.any = 1;
        }
    }
    if (out.accept != 0) {
        out.back = 0;
    }
    return out;
}

void FrontScreens::poll_buttons_to(std::uint32_t accept, std::uint32_t back, std::uint32_t any, std::uint32_t select) {
    const Buttons out = poll_buttons();
    set_word_at(m_, select, out.select);
    set_word_at(m_, accept, out.accept);
    set_word_at(m_, back, out.back);
    set_word_at(m_, any, out.any);
}

// 0x0044dfa0: put up the pause menu's yes/no question (the sound is paused, the pads and the shake are cleared)
void FrontScreens::common_switch_on_menu_code() {
    s_.note(kSvcPrintfLog, {0x0052c78c});
    check(global_word(m_, front::kStatus) != 0x13, 0x0052c768);
    set_global_word(m_, 0x00561bcc, 0);
    set_global_word(m_, 0x00561ba0, 0);
    set_global_word(m_, 0x005616e4, 0);
    const std::uint32_t skater = global_word(m_, kSkater);
    const std::uint32_t other = word_at(m_, skater + 0x20);
    engine(0x004cbf30, {skater});
    engine(0x004cbf30, {other});
    set_status(0x98d, 0x13);
    engine(0x00489670, {kInputActionStates});
    engine(0x00489670, {kPad2States});
    set_global_word(m_, 0x00561c04, 1);
    check(global_word(m_, front::kMenuYesNo) != 0, 0x0052c754);
    set_menu_line(global_word(m_, front::kMenuYesNo), str(0x0052e414));
    mess_delete_all();
    engine(0x00489850, {0, 0});
    engine(0x00489850, {0, 1});
    engine(0x004ad9c0);
    play(2, 0x1000);
}

// 0x0044e0f0: the level is over: choose the screen that follows (a message, the statistics, the end menu) and enter the high score when the run earned a place
void FrontScreens::switch_on_end_level() {
    s_.note(kSvcPrintfLog, {0x0052c7a8});
    if (replay_level_flag()) {
        const std::uint32_t record = word_at(m_, global_word(m_, front::kPlayer) + 0x29b0);
        if (word_at(m_, record + 0x504) == 0x17) {
            set_word_at(m_, record + 0x504, 1);
        }
        if (global_word(m_, front::kStatus) == 0x13) {
            return;
        }
        common_switch_on_menu_code();
        return;
    }
    set_global_word(m_, kLevelEventLatch, 0);
    const std::uint32_t status = global_word(m_, front::kStatus);
    if (status == 0xb || status == 0xd || status == 0x32 || status == 0x35 || status == 0x36 || status == 0x37 || status == 0x34) {
        return;
    }
    const std::uint32_t skater = global_word(m_, kSkater);
    engine(0x004cbf30, {skater});
    engine(0x004cbf30, {word_at(m_, skater + 0x20)});
    const std::uint32_t player = global_word(m_, front::kPlayer);
    const std::uint32_t mode = global_word(m_, front::kGameMode);
    if (word_at(m_, player + 0x31e4) != 0 && global_word(m_, front::kPlayerCount) == 1) {
        set_status(0x9dd, 0xb);
    } else if ((engine_value(0x00452a80, {0xc}) & 0xff) == 0) {
        const std::uint32_t config = 0x00568a6c + word_at(m_, player + 0x2cc0) * 0x104;
        if (mode == 1) {
            std::uint32_t flags = global_word(m_, 0x0056a68c);
            if ((flags & 1) == 0 && int_at(m_, config + 8) >= 500 && int_at(m_, config + 8) == int_at(m_, config + 4)) {
                set_global_word(m_, 0x0056a68c, flags | 1);
                set_status(0xa01, 0x32);
            } else if ((flags & 2) == 0 && int_at(m_, config + 8) >= 2000 && int_at(m_, config + 8) == int_at(m_, config + 4)) {
                set_global_word(m_, 0x0056a68c, flags | 2);
                set_status(0xa09, 0x32);
            } else {
                set_status(0xa0d, 0xd);
            }
        } else if (mode == 2 || mode == 4 || (mode == 9 && global_word(m_, 0x0056db64) != 2)) {
            set_status(0xa0f, 0xe);
        } else {
            set_status(0xa0d, 0xd);
        }
    }
    const std::int32_t level = global_int(m_, front::kCurrentLevel);
    bool enter_score = false;
    if (mode == 1) {
        if (byte_at(m_, kLevelTable + u(level) * 0x1ac) != 0) {
            switch_on_end_heat();
            reset_score_slide();
            return;
        }
        enter_score = true;
    } else if (mode == 3) {
        enter_score = true;
    }
    if (enter_score) {
        // the run's score is entered in the level's table of eight, at the first place it beats (the lower places move down)
        set_global_word(m_, 0x0052bf34, 0xffffffffu);
        set_global_word(m_, 0x00561bf4, 0);
        for (std::uint32_t place = 0; place < 8; ++place) {
            const std::uint32_t table = global_byte(m_, 0x005614c4) == 0 ? u(level) : 0xd;
            const std::uint32_t row = kHighScores + (place + table * 8) * 8;
            if (word_at(m_, player + 0x16c) < word_at(m_, row)) {
                continue;
            }
            if (place < 7) {
                for (std::uint32_t k = 7; k > place; --k) {
                    const std::uint32_t to = kHighScores + (k + table * 8) * 8;
                    const std::uint32_t from = to - 8;
                    set_word_at(m_, to, word_at(m_, from));
                    set_byte_at(m_, to - 1, byte_at(m_, from - 1));
                    set_byte_at(m_, to - 4, byte_at(m_, from - 4));
                    set_byte_at(m_, to - 3, byte_at(m_, from - 3));
                    set_byte_at(m_, to - 2, byte_at(m_, from - 2));
                }
            }
            set_word_at(m_, row, word_at(m_, player + 0x16c));
            set_byte_at(m_, row - 1, byte_at(m_, global_word(m_, front::kPlayer) + 0x2cc0));
            set_byte_at(m_, row - 4, global_byte(m_, 0x0056a408));
            set_byte_at(m_, row - 3, global_byte(m_, 0x0056a409));
            set_global_word(m_, 0x0052bf34, place);
            set_byte_at(m_, row - 2, global_byte(m_, 0x0056a40a));
            break;
        }
        // host side (score_hooks.hpp): the score file hears of every finished run, placed or not; null (retail, every gate) reads nothing more
        if (const auto hook = scorehooks::hooks().run_end; hook != nullptr) {
            scorehooks::RunEnd run;
            run.mode = mode;
            run.level = level;
            run.table = global_byte(m_, 0x005614c4) == 0 ? u(level) : 0xd;
            run.place = global_int(m_, 0x0052bf34);
            run.score = word_at(m_, player + 0x16c);
            run.skater = word_at(m_, player + 0x2cc0);
            hook(run);
        }
    }
    if (global_word(m_, front::kGameMode) == 7) {
        engine(0x004b9790, {kTextEffect});
        engine(0x004b9780, {kTextEffect, 0});
        set_global_byte(m_, 0x005617a4, 0);
        set_global_word(m_, 0x00561b2c, 0);
        set_global_word(m_, 0x00568650, 0x2d);
        set_global_word(m_, 0x00561be8, 0);
    }
    switch_on_end_heat();
    reset_score_slide();
}

// 0x0044e3f0: the end screens' columns start off the screen (one set slides in from each side)
void FrontScreens::reset_score_slide() {
    std::int32_t right = 0x2dc;
    std::int32_t left = -0xdc;
    for (std::uint32_t off = 0; right > 0x1ec; off += 4) {
        set_word_at(m_, 0x00561710 + off, u(right));
        set_word_at(m_, 0x00561918 + off, u(left));
        right -= 0x1e;
        left += 0x1e;
    }
}

// 0x0044e420: a menu is put up: pads cleared, the yes/no menu on "no", the messages cleared and the sound paused
void FrontScreens::switch_on_end_heat() {
    engine(0x00489670, {kInputActionStates});
    engine(0x00489670, {kPad2States});
    set_global_word(m_, 0x00561c04, 1);
    check(global_word(m_, front::kMenuYesNo) != 0, 0x0052c754);
    set_menu_line(global_word(m_, front::kMenuYesNo), str(0x0052e414));
    mess_delete_all();
    engine(0x00489850, {0, 0});
    engine(0x00489850, {0, 1});
    engine(0x004ad9c0);
    play(2, 0x1000);
    set_global_word(m_, kWaitTicks, 0);
}

void FrontScreens::switch_on_ending_screen() {
    check(global_word(m_, front::kStatus) != 0xe, 0x0052c7dc);
    set_status(0xa4a, 0xe);
    switch_on_end_heat();
}

void FrontScreens::switch_on_level_menu() {
    check(global_word(m_, front::kStatus) != 0x2a, 0x0052c804);
    set_status(0xa52, 0x2a);
    if (global_word(m_, kNameEntry) != 0) {
        delete_object(global_word(m_, kNameEntry));
        set_global_word(m_, kNameEntry, 0);
    }
    switch_on_end_heat();
}

void FrontScreens::switch_on_time_select() {
    if (global_byte(m_, 0x006a3b21) != 0 && (engine_value(0x004de040) & 0xff) == 0) {
        return;
    }
    check(global_word(m_, front::kStatus) != 0x33, 0x0052c82c);
    set_status(0xa68, 0x33);
    switch_on_end_heat();
}

// 0x0044e630: a message of the horse game that grows into place (the counter runs up to 0x200), flashes red, and then falls apart in the falling-text effect
void FrontScreens::horse_message(std::uint32_t first, std::uint32_t second, std::uint32_t counter, std::uint32_t sound) {
    horse_message(first, second, counter, sound, m_.cstring(first, 0x100), m_.cstring(second, 0x100));
}

void FrontScreens::horse_message(std::uint32_t first, std::uint32_t second, std::uint32_t counter, std::uint32_t sound, const std::string& first_text, const std::string& second_text) {
    if (int_at(m_, counter) >= 0x200) {
        if (global_byte(m_, 0x00561be4) != 0) {
            return;
        }
        if (global_word(m_, 0x00561be8) != 0) {
            engine(0x004b9820, {kTextEffect});
            set_global_word(m_, kEffectScale, 0x2000);
            engine(0x004b9d50, {kTextEffect, 0x2000});
            engine(0x004b9d60, {kTextEffect, kFontMess});
            engine(0x0044a070, {kFontMess, 0x100, 0xbc, second, 0}, second_text);
            engine(0x004b95a0, {kTextEffect, kFontMess});
            set_global_word(m_, kEffectScale, 0x1800);
            engine(0x004b9d50, {kTextEffect, 0x1800});
            engine(0x004b9d60, {kTextEffect, kFontMess});
            engine(0x0044a070, {kFontMess, 0x100, 0x98, first, 0}, first_text);
            engine(0x004b95a0, {kTextEffect, kFontMess});
            if (engine_value(0x004b9de0, {kTextEffect}) == 0) {
                set_global_word(m_, 0x00568650, 0x2d);
                return;
            }
            set_global_byte(m_, 0x00561be4, 1);
            return;
        }
    }
    const std::int32_t grown = int_at(m_, counter) + 0x20;
    set_word_at(m_, counter, u(grown));
    if (grown >= 0x200) {
        set_word_at(m_, counter, 0x200);
        if (global_word(m_, 0x00561730) == 0) {
            play(sound);
            set_global_word(m_, 0x00561730, 1);
        }
        mess_rgb(0xff, 0x32, 0x32);
    }
    std::int32_t scale = int_at(m_, counter);
    if (scale >= 0x180) {
        scale = 0x180;
    }
    mess_scale(u(scale));
    engine(0x004739e0, {0x100, 0x98, first, 0}, first_text);
    scale = int_at(m_, counter);
    if (scale >= 0x200) {
        scale = 0x200;
    }
    mess_scale(u(scale));
    engine(0x004739e0, {0x100, 0xbc, second, 0}, second_text);
    set_global_word(m_, 0x00568650, 0x2d);
}

// 0x0044e7e0: the horse game's word entry
void FrontScreens::print_horse_letters() {
    check(global_word(m_, front::kStatus) != 0x2b, 0x0052c854);
    set_status(0xac4, 0x2b);
    engine(0x004dc020, {1});
    check(global_word(m_, kNameEntry) == 0, 0x00526874);
    mess_justify(0);
    const std::uint32_t memory = operator_new(0x374);
    std::uint32_t entry = 0;
    if (memory != 0) {
        entry = engine_value(0x00471e30, {memory, kFontMess, 10, 0x100, 0x50, 0x100, u(global_int(m_, 0x00527d54) / 2 + 0x1e), 3, 0, 0, 0, 0});
    }
    set_global_word(m_, kNameEntry, entry);
    set_word_at(m_, entry + 0xac, 0x1e);
    engine(0x00472930, {entry, kHorseWord}, m_.cstring(kHorseWord, 0x40));
    engine(0x00472a70, {entry, str(0x0052e4fc), 0x100, 0x3c}, m_.cstring(str(0x0052e4fc), 0x40));
    switch_on_end_heat();
}

// 0x0044e950: the replay menus start on "play replay"; the screen waits a moment before it takes input
void FrontScreens::select_play_replay() {
    for (std::uint32_t menu : {front::kMenuReplayFull, front::kMenuReplayShort, front::kMenuReplayComp}) {
        set_menu_line(word_at(m_, menu), str(0x0052e534));
    }
    set_global_word(m_, kWaitTicks, u(((global_int(m_, front::kAnimScale) * 5 * 2) >> 8) + 1));
}

// 0x0044e9c0 (and its tail at 0x0044e9f0): the replay-or-quit screen after a level
void FrontScreens::switch_on_video_screen() {
    engine(0x004dc020, {1});
    std::uint32_t target = 0xf;
    if (global_half(m_, 0x0056b8d4) != 0) {
        target = 0x10;
    } else if (replay_level_flag() && global_byte(m_, 0x0055ff1c) < 3) {
        target = 0x1a;
    }
    check(global_word(m_, front::kStatus) != target, 0x0052c87c);
    set_status(0xb02, target);
    switch_on_end_heat();
    select_play_replay();
}

// 0x0044ea70: in a contest level the restart point of the replay is the level's own (only level 5 has one)
void FrontScreens::replay_restart() {
    const std::int32_t level = global_int(m_, front::kCurrentLevel);
    if (byte_at(m_, kLevelTable + u(level) * 0x1ac) == 0) {
        return;
    }
    if (level == 5) {
        engine(0x004c4c50, {0x0052c8a8}, m_.cstring(0x0052c8a8, 0x40));
        engine(0x004c4e30);
    }
}

}  // namespace opentony::runtime::frontend
