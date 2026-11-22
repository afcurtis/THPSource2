#include "auto_views.hpp"
#include "front.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

constexpr std::uint32_t kLevelRecords = 0x0053c1f8;
constexpr std::uint32_t kLevelCount = 0x00561b6c;           // which level the unlock screen announced (10 or 11 for the special levels)
constexpr std::uint32_t kPulse = 0x004ca890;
constexpr std::uint32_t kWaitTicks = 0x005616e0;
constexpr std::uint32_t kYes = 0x0052e410;
constexpr std::uint32_t kNo = 0x0052e414;

}  // namespace

// The unlock and congratulation screens (0x31, 0x32, 0x34..0x37) and the time select (0x33), the last of Front_Update's statuses.
void FrontScreens::update_screens(const Buttons& b, bool& leave, std::uint32_t status) {
    auto pulse = [&](std::int32_t t) { return s(engine_value(kPulse, {u(t), 0x14, 0x28})); };
    std::uint32_t accept = b.accept;
    switch (status) {
    case 0x31:
    case 0x36:
    case 0x37: {
        // "Congratulations - you opened a new level": level 3 and 6 are the special levels (the stored number 10 and 11), the others come from the career
        if (global_word(m_, front::kGotNewStatus) != 0) {
            set_global_word(m_, front::kGotNewStatus, 0);
            set_global_word(m_, 0x00561b88, global_word(m_, front::kSimTick));
            play(0x17c);
            find_line(front::kMenuYesNo, kNo);
        }
        std::int32_t level;
        if (status == 0x31) {
            const std::uint32_t stored = global_word(m_, kLevelCount);
            if (stored == 10) {
                level = 3;
            } else if (stored == 0xb) {
                level = 6;
            } else {
                level = s(engine_value(0x00415a90, {1, (stored & 0xffffff00u) | (replay_level_flag() ? 1u : 0u)}));
            }
        } else {
            level = (status != 0x36) + 8;
        }
        const std::int32_t t = s(global_word(m_, front::kSimTick) - global_word(m_, 0x00561b88));
        if (t > 0) {
            mess_justify(0);
            mess_scale(u(pulse(t) + 0x180));
            mess_rgb(0x80, 0x80, 0x80);
            mess_text(0x100, 0x28, str(0x0052e438));
        }
        mess_scale(0x100);
        if (t > 0x14) {
            mess_scale(u(pulse(t - 0x14) + 0x100));
            mess_text(0x100, 0x41, str(0x0052e43c));
        }
        if (t > 0x28) {
            sprintf_to(0x00561ba4, retail_format(m_.cstring(0x0052c8c0, 0x10), {m_.cstring(kLevelRecords + u(level) * 0x1ac, 0x40), m_.cstring(word_at(m_, 0x0053c238 + u(level) * 0x1ac), 0x40)}, {}));
            mess_rgb(0x80, 0x80, 0);
            mess_scale(u(pulse(t - 0x28) + 0x100));
            mess_text(0x100, 0x5a, 0x00561ba4);
        }
        std::int32_t lines;
        if (level < 10) {
            engine(0x004dc020, {1});
            lines = 3;
            if (t > 0x3c) {
                mess_rgb(0x80, 0x80, 0x80);
                mess_scale(u(pulse(t - 0x3c) + 0x100));
                mess_text(0x100, 0x73, str(0x0052e440));
                set_word_at(m_, global_word(m_, front::kMenuYesNo) + 0x10, 0x9b);
                cmenu_update(global_word(m_, front::kMenuYesNo));
                cmenu_display(global_word(m_, front::kMenuYesNo));
            }
        } else {
            engine(0x004dc020, {0});
            if (t > 0x3c) {
                mess_rgb(0x80, 0x80, 0x80);
                mess_scale(u(pulse(t - 0x3c) + 0x100));
                mess_text(0x100, 0x8c, str(0x0052eb18));
            }
            lines = 5;
            if (t > 0x50) {
                mess_rgb(0x80, 0x80, 0x80);
                mess_scale(u(pulse(t - 0x50) + 0x100));
                mess_text(0x100, 0xa5, str(0x0052eb1c));
            }
        }
        if (t < lines * 0x14) {
            if (accept != 0) {
                set_global_word(m_, 0x00561b88, global_word(m_, front::kSimTick) - u(lines * 0x14));
            }
            accept = 0;
            if (b.back == 0) {
                break;
            }
        } else {
            if (level > 9) {
                engine(0x004dc0c0);
            }
            if (accept == 0 && b.back == 0) {
                break;
            }
        }
        play(5);
        set_global_word(m_, kLevelCount, 0);
        if (level > 9) {
            if (!replay_level_flag()) {
                if ((engine_value(0x00452a80, {0xc}) & 0xff) == 0) {
                    set_status(0x20f, 0xc);
                    engine(0x004dc020, {1});
                }
            } else if ((engine_value(0x00452a80, {7}) & 0xff) == 0) {
                if (level == 10 || level == 0xb) {
                    const std::uint32_t config = 0x00568a6c + word_at(m_, global_word(m_, front::kPlayer) + 0x2cc0) * 0x104;
                    const std::uint32_t flags = half_at(m_, config + 0xc + global_word(m_, front::kCurrentLevel) * 2);
                    if ((flags & 0x1000) == 0 && (flags & 0x800) == 0) {
                        set_global_word(m_, front::kGotNewStatus, 1);
                        set_global_word(m_, kLevelCount, u(level));
                    } else {
                        set_status(0x200, 7);
                        engine(0x004dc020, {1});
                    }
                } else {
                    set_status(0x206, 7);
                    engine(0x004dc020, {1});
                }
            }
            break;
        }
        if (b.back == 0 && menu_choice(front::kMenuYesNo, kYes)) {
            set_global_word(m_, 0x00561c90, u(level));
            set_global_word(m_, 0x00561ce4, u(level));
            set_global_word(m_, 0x00561b9c, 1);
            set_status(0x21f, 8);
            break;
        }
        if (!replay_level_flag()) {
            if ((engine_value(0x00452a80, {0xc}) & 0xff) == 0) {
                set_status(0x236, 0xc);
                engine(0x004dc020, {1});
            }
        } else if ((engine_value(0x00452a80, {7}) & 0xff) == 0) {
            set_status(0x229, 7);
            engine(0x004dc020, {1});
        }
        find_line(front::kMenuYesNo, kYes);
        break;
    }
    case 0x32: {
        // "Remember: you can spend your earnings on new stats, tricks and decks"
        if (global_word(m_, front::kGotNewStatus) != 0) {
            set_global_word(m_, front::kGotNewStatus, 0);
            set_global_word(m_, 0x0056198c, global_word(m_, front::kSimTick));
        }
        engine(0x00415a90, {1, 0});
        const std::int32_t t = s(global_word(m_, front::kSimTick) - global_word(m_, 0x0056198c));
        const bool second = (global_word(m_, 0x0056a68c) & 2) != 0;
        if (t > 0) {
            mess_justify(0);
            mess_scale(u(pulse(t) + 0x180));
            mess_rgb(0x80, 0x80, 0x80);
            mess_text(0x100, 0x28, str(second ? 0x0052e45c : 0x0052e448));
        }
        std::int32_t k = 1;
        mess_scale(0x100);
        if (t > 0x14) {
            mess_scale(u(pulse(t - 0x14) + 0x100));
            std::uint32_t third;
            if (!second) {
                mess_text(0x100, 0x41, str(0x0052e44c));
                mess_text(0x100, 0x5a, str(0x0052e450));
                third = str(0x0052e454);
            } else {
                mess_text(0x100, 0x41, str(0x0052e460));
                mess_text(0x100, 0x5a, str(0x0052e464));
                third = str(0x0052e468);
            }
            k = 3;
            mess_text(0x100, 0x73, third);
        }
        ++k;
        const std::int32_t reveal = k * 0x14 + 0x32;
        if (reveal < t) {
            mess_rgb(0x80, 0x80, 0x80);
            mess_scale(u(pulse(t - k * 0x14) + 0x100));
            mess_text(0x100, k * 0x19 + 0x28, str(second ? 0x0052e46c : 0x0052e458));
            set_word_at(m_, global_word(m_, front::kMenuYesNo) + 0x10, u(k * 0x19 + 0x50));
            cmenu_update(global_word(m_, front::kMenuYesNo));
            cmenu_display(global_word(m_, front::kMenuYesNo));
        }
        if (t < reveal) {
            if (accept != 0) {
                set_global_word(m_, 0x0056198c, global_word(m_, front::kSimTick) - u(k * 0x14));
            }
            accept = 0;
            if (b.back == 0) {
                break;
            }
        } else {
            hint_text(str(0x0052e968), 0);
            if (accept == 0 && b.back == 0) {
                break;
            }
        }
        play(5);
        if (menu_choice(front::kMenuYesNo, kYes)) {
            set_global_word(m_, 0x00561b9c, 2);
            set_status(0x330, 8);
            break;
        }
        if (!replay_level_flag()) {
            set_status(0x337, 0xc);
        } else {
            set_status(0x335, 7);
        }
        engine(0x004dc020, {1});
        find_line(front::kMenuYesNo, kYes);
        break;
    }
    case 0x33: {
        // the time select of a versus game
        if (!(((global_int(m_, front::kAnimScale) * 10) >> 8) < global_int(m_, kWaitTicks))) {
            set_global_word(m_, kWaitTicks, global_word(m_, kWaitTicks) + 1);
            break;
        }
        cmenu_update(global_word(m_, front::kMenuTime));
        if (accept == 0) {
            if (b.back != 0) {
                play(2);
                set_global_word(m_, 0x00561b9c, 1);
                set_status(0xea4, 8);
            }
        } else {
            play(5);
            std::uint32_t frames;
            if (menu_choice(front::kMenuTime, 0x0052e500)) {
                frames = 0xe2e;
            } else if (menu_choice(front::kMenuTime, 0x0052e504)) {
                frames = 0x1c3e;
            } else if (menu_choice(front::kMenuTime, 0x0052e508)) {
                frames = 0x466e;
            } else if (menu_choice(front::kMenuTime, 0x0052e50c)) {
                frames = 0x8cbe;
            } else {
                frames = b.any;
            }
            set_word_at(m_, global_word(m_, front::kPlayer) + 0x2cc8, frames);
            set_word_at(m_, global_word(m_, front::kPlayer2) + 0x2cc8, frames);
            set_global_word(m_, 0x005685fc, frames);
            if (global_byte(m_, 0x006a3b21) == 0) {
                cmenu_entry_on(global_word(m_, front::kMenuEnd), str(0x0052e564));
            }
            if (frames > 0x1c3e || global_word(m_, front::kGameMode) == 9) {
                cmenu_entry_off(global_word(m_, front::kMenuEnd), str(0x0052e564));
            }
            leave = true;
            engine(0x004de570, {0xf});
        }
        break;
    }
    case 0x34:
    case 0x35: {
        // the cheat or character unlocked: a list of lines that pulse in one after the other
        set_global_word(m_, 0x0055fd74, 1);
        if (global_word(m_, front::kGotNewStatus) != 0) {
            set_global_word(m_, front::kGotNewStatus, 0);
            set_global_word(m_, 0x005616e8, global_word(m_, front::kSimTick));
            if (status == 0x34) {
                set_global_word(m_, 0x00561b80, 0);
                if (engine_value(0x00416de0) == 0) {
                    set_global_word(m_, 0x00561b80, 1);
                }
                const std::uint32_t cheat = engine_value(0x00416cd0);
                set_global_word(m_, 0x00561b84, cheat);
                const std::uint32_t character = engine_value(0x004b0c30, {0x0056c580});
                engine(0x004168b0, {global_word(m_, 0x00561b84), character});
                play(0x17c);
            }
        }
        const std::int32_t t = s(global_word(m_, front::kSimTick) - global_word(m_, 0x005616e8));
        std::uint32_t lines[10] = {};
        std::int32_t count;
        std::int32_t highlight = -1;
        if (status == 0x35) {
            for (std::uint32_t k = 0; k < 5; ++k) {
                lines[k] = str(0x0052e2a4 + 4 * k);
            }
            count = 5;
        } else {
            lines[0] = str(0x0052e2b8);
            lines[1] = str(0x0052e2bc);
            count = 2;
            if (global_word(m_, 0x00561b80) != 0) {
                lines[2] = str(0x0052e2c0);
                count = 3;
            }
            const std::uint32_t name = engine_value(0x00416e30, {global_word(m_, 0x00561b84)});
            sprintf_to(0x00561778, retail_format("%s %s", {m_.cstring(name, 0x40), m_.cstring(str(0x0052e2c4), 0x40)}, {}));
            const std::uint32_t cheat = global_word(m_, 0x00561b84);
            highlight = count;
            lines[count] = cheat == 0 ? str(0x0052e2c8) : cheat == 1 ? str(0x0052e2cc) : cheat == 2 ? str(0x0052e2d0) : 0x00561778;
            lines[count + 1] = str(0x0052e2d4);
            lines[count + 2] = str(0x0052e2d8);
            lines[count + 3] = str(0x0052e2dc);
            count += 4;
        }
        for (std::int32_t k = 0; k < count; ++k) {
            if (t > k * 0x14) {
                mess_justify(0);
                mess_scale(u(pulse(t - k * 0x14) + (k == 0 ? 0x180 : 0x100)));
                mess_rgb(0x80, 0x80, 0x80);
                if (highlight == k) {
                    mess_rgb(0x80, 0x80, 0);
                }
                mess_text(0x100, k * 0x19 + 0x28, lines[k]);
                mess_rgb(0x80, 0x80, 0x80);
            }
        }
        if (t < count * 0x14) {
            if (accept != 0) {
                set_global_word(m_, 0x005616e8, global_word(m_, front::kSimTick) - u(count * 0x14));
            }
            accept = 0;
            if (b.back == 0) {
                break;
            }
        } else {
            engine(0x004dc0c0);
            if (accept == 0 && b.back == 0) {
                break;
            }
        }
        play(5);
        engine(0x00452d00);
        break;
    }
    default:
        break;
    }
}

}  // namespace opentony::runtime::frontend
