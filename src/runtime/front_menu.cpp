#include "auto_views.hpp"
#include "front.hpp"
#include "frame_pacing.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// the assertion messages (front.cpp)
constexpr std::uint32_t kMsgResetCalled = 0x0052c674;
constexpr std::uint32_t kMsgEntryNotFound = 0x0052c684;
constexpr std::uint32_t kMsgTooManyEntries = 0x0052c694;

// the pad bytes CMenu__Update reads: up on the pad and on the stick, down on both (pad 1; the second pad's are at +0x16c)
constexpr std::uint32_t kMenuUp = 0x0056b098;
constexpr std::uint32_t kMenuDown = 0x0056b0a8;
constexpr std::uint32_t kMenuStickUp = 0x0056b204;
constexpr std::uint32_t kMenuStickDown = 0x0056b214;
constexpr std::uint32_t kRepeat = 0x00561c10;

constexpr std::uint32_t kEntryStride = 0x18;
constexpr std::uint32_t kNameOffset = 0x18;       // entry e's name pointer is at self + 0x18 * (e + 1)

std::uint32_t entry(std::uint32_t self, std::uint32_t e) { return self + kEntryStride * e; }

}  // namespace

bool FrontScreens::names_equal(std::uint32_t a, std::uint32_t b, std::uint32_t limit) const {
    // 0x004c9f50: equal ignoring case, at most `limit` characters; the letters fold on signed bytes, so bytes above 0x7f do not
    if (a == 0) {
        return b == 0;
    }
    if (b == 0) {
        return false;
    }
    auto fold = [](std::uint8_t c) { return static_cast<std::uint8_t>((static_cast<std::int8_t>(c) >= 0x41 && static_cast<std::int8_t>(c) <= 0x5a) ? c + 0x20 : c); };
    for (;;) {
        const std::uint8_t x = fold(byte_at(m_, a++));
        const std::uint8_t y = fold(byte_at(m_, b++));
        if (x != y) {
            return false;
        }
        if (x == 0 || --limit == 0) {
            return true;
        }
    }
}

void FrontScreens::sprintf_to(std::uint32_t buffer, const std::string& text) {
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(buffer, bytes);
}

// 0x0044b640: the menu object: position, justification, spacing, the default colours and the two scales of every entry; the first line is current
std::uint32_t FrontScreens::cmenu_new(std::uint32_t self, std::int32_t x, std::int32_t y, std::uint32_t justify, std::uint32_t scale_a, std::uint32_t scale_b, std::int32_t spacing) {
    front_menu_cmenu_at(m_, self).set_field_c(static_cast<Word>(u(x)));
    front_menu_cmenu_at(m_, self).set_field_10(static_cast<Word>(u(y)));
    front_menu_cmenu_at(m_, self).set_field_4(static_cast<std::uint8_t>(justify));
    front_menu_cmenu_at(m_, self).set_field_14(static_cast<Word>(u(spacing)));
    front_menu_cmenu_at(m_, self).set_field_0(static_cast<Word>(front::kVtCMenu));
    for (std::uint32_t e = 0; e < front::kMenuEntries; ++e) {
        const std::uint32_t at = entry(self, e);
        set_byte_at(m_, at + 0x27, 0xb6);
        set_byte_at(m_, at + 0x28, 0x84);
        set_byte_at(m_, at + 0x29, 0x24);
        set_byte_at(m_, at + 0x2a, 0x7a);
        set_byte_at(m_, at + 0x2b, 0x7a);
        set_byte_at(m_, at + 0x2c, 0x7a);
        set_byte_at(m_, at + 0x2d, 0x80);
        set_half_at(m_, at + 0x20, scale_a);
        set_half_at(m_, at + 0x22, scale_b);
    }
    cmenu_set_line(self, 0);
    return self;
}

// 0x0044b6f0: the scalar deleting destructor: the base class vtable, then the block is freed when bit 0 of the flag is set
std::uint32_t FrontScreens::cmenu_destruct(std::uint32_t self, std::uint32_t flag) {
    cmenu_delete(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void FrontScreens::cmenu_delete(std::uint32_t self) { set_word_at(m_, self, kVtRoot); }

// 0x0044b720: the first line's y puts the shown entries in the middle of the 240-line screen
void FrontScreens::cmenu_centre_y(std::uint32_t self) {
    std::int32_t heights = 0;
    std::int32_t shown = 0;
    for (std::uint32_t e = 0; e < front_menu_cmenu_at(m_, self).field_9(); ++e) {
        const std::uint32_t at = entry(self, e);
        if (byte_at(m_, at + 0x25) != 0) {
            heights += byte_at(m_, at + 0x24);
            ++shown;
        }
    }
    const std::int32_t whole = 0xf0 - (shown - 1) * static_cast<std::int32_t>(front_menu_cmenu_at(m_, self).field_14()) - heights;
    front_menu_cmenu_at(m_, self).set_field_10(static_cast<Word>(u(whole / 2)));
}

// 0x0044b770: make `line` current: its entry grows to the selected scale, the others shrink to the normal one (at once)
void FrontScreens::cmenu_set_line(std::uint32_t self, std::uint32_t line) {
    front_menu_cmenu_at(m_, self).set_field_6(static_cast<std::uint8_t>(line));
    for (std::uint32_t e = 0; e < front::kMenuEntries; ++e) {
        const std::uint32_t at = entry(self, e);
        const std::uint32_t scale = e == front_menu_cmenu_at(m_, self).field_6() ? half_at(m_, at + 0x20) : half_at(m_, at + 0x22);
        set_half_at(m_, at + 0x1e, scale);
        set_half_at(m_, at + 0x1c, scale);
    }
}

// 0x0044b7b0: reset (an assertion that always fires, then the first line)
void FrontScreens::cmenu_reset(std::uint32_t self) {
    check(false, kMsgResetCalled);
    cmenu_set_line(self, 0);
}

std::uint32_t FrontScreens::cmenu_find_entry(std::uint32_t self, std::uint32_t name) {
    for (std::uint32_t e = 0; e < front_menu_cmenu_at(m_, self).field_9(); ++e) {
        if (names_equal(name, word_at(m_, self + kNameOffset + kEntryStride * e), 0xffffffffu)) {
            return e;
        }
    }
    check(false, kMsgEntryNotFound);
    return 0;
}

// 0x0044b890: a new entry is shown and can be chosen, with no extra height
void FrontScreens::cmenu_add_entry(std::uint32_t self, std::uint32_t name) {
    const std::uint32_t count = front_menu_cmenu_at(m_, self).field_9();
    check(count < front::kMenuEntries, kMsgTooManyEntries);
    set_word_at(m_, self + kNameOffset + kEntryStride * count, name);
    set_byte_at(m_, entry(self, count) + 0x25, 1);
    set_byte_at(m_, entry(self, count) + 0x26, 1);
    set_byte_at(m_, entry(self, count) + 0x24, 0);
    front_menu_cmenu_at(m_, self).set_field_9(static_cast<std::uint8_t>(count + 1));
}

void FrontScreens::cmenu_entry_on(std::uint32_t self, std::uint32_t name) { set_byte_at(m_, entry(self, cmenu_find_entry(self, name)) + 0x25, 1); }
void FrontScreens::cmenu_entry_off(std::uint32_t self, std::uint32_t name) { set_byte_at(m_, entry(self, cmenu_find_entry(self, name)) + 0x25, 0); }
bool FrontScreens::cmenu_is_entry_on(std::uint32_t self, std::uint32_t name) { return byte_at(m_, entry(self, cmenu_find_entry(self, name)) + 0x25) == 1; }
void FrontScreens::cmenu_enable(std::uint32_t self, std::uint32_t name) { set_byte_at(m_, entry(self, cmenu_find_entry(self, name)) + 0x26, 1); }
void FrontScreens::cmenu_disable(std::uint32_t self, std::uint32_t name) { set_byte_at(m_, entry(self, cmenu_find_entry(self, name)) + 0x26, 0); }
bool FrontScreens::cmenu_is_shown(std::uint32_t self, std::uint32_t name) { return byte_at(m_, entry(self, cmenu_find_entry(self, name)) + 0x25) != 0; }
void FrontScreens::cmenu_set_height(std::uint32_t self, std::uint32_t name, std::uint32_t height) { set_byte_at(m_, entry(self, cmenu_find_entry(self, name)) + 0x24, height); }

// 0x0044ba00: is the current line's entry the one with this name
bool FrontScreens::cmenu_choice_is(std::uint32_t self, std::uint32_t name) {
    return names_equal(name, word_at(m_, self + kNameOffset + kEntryStride * front_menu_cmenu_at(m_, self).field_6()), 0xffffffffu);
}

// 0x0044ba20: where an entry is drawn: x of the menu, y of the first line plus the heights (and spacings) of the shown entries before it
void FrontScreens::cmenu_position_of(std::uint32_t self, std::uint32_t name, std::uint32_t out_x, std::uint32_t out_y) {
    set_word_at(m_, out_x, front_menu_cmenu_at(m_, self).field_c());
    set_word_at(m_, out_y, front_menu_cmenu_at(m_, self).field_10());
    for (std::uint32_t e = 0; e < front_menu_cmenu_at(m_, self).field_9(); ++e) {
        const std::uint32_t at = entry(self, e);
        if (byte_at(m_, at + 0x25) == 0) {
            continue;
        }
        set_word_at(m_, out_y, word_at(m_, out_y) + byte_at(m_, at + 0x24));
        if (names_equal(name, word_at(m_, at + kNameOffset), 0xffffffffu)) {
            return;
        }
        set_word_at(m_, out_y, word_at(m_, out_y) + front_menu_cmenu_at(m_, self).field_14());
    }
}

// 0x0044ba90: draw the shown entries: the current one pulses between its selected colour and a shifted one, the others fade toward a grey of their own
void FrontScreens::cmenu_display(std::uint32_t self) {
    mess_justify(front_menu_cmenu_at(m_, self).field_4());
    std::uint32_t y = front_menu_cmenu_at(m_, self).field_10();
    for (std::uint32_t e = 0; e < front_menu_cmenu_at(m_, self).field_9(); ++e) {
        const std::uint32_t at = entry(self, e);
        y += byte_at(m_, at + 0x24);
        if (byte_at(m_, at + 0x25) == 0) {
            continue;
        }
        const std::int16_t scale = short_at(m_, at + 0x1c);
        if (scale > 0) {
            mess_scale(u(scale));
            if (e == front_menu_cmenu_at(m_, self).field_6()) {
                const std::uint32_t phase = global_word(m_, front::kSimTick) << 1;
                std::uint32_t quotient = phase / 0x3c;
                std::uint32_t pulse = phase % 0x3c;
                if (pulse > 0x1e) {
                    quotient = 0x3c - pulse;
                    pulse = quotient;
                }
                // the green argument keeps the upper bits of the quotient (the register is only partly overwritten)
                const std::uint32_t g = (quotient & 0xffffff00u) | ((byte_at(m_, at + 0x28) - pulse) & 0xffu);
                mess_rgb((byte_at(m_, at + 0x27) + pulse) & 0xffu, g, byte_at(m_, at + 0x29));
                set_byte_at(m_, at + 0x2d, 0);
            } else {
                const std::uint32_t fade = byte_at(m_, at + 0x26) != 0 ? 0x80 : 0x40;
                set_byte_at(m_, at + 0x2d, fade);
                mess_rgb(u((byte_at(m_, at + 0x2a) * static_cast<std::int32_t>(fade)) >> 7), u((byte_at(m_, at + 0x2b) * static_cast<std::int32_t>(fade)) >> 7),
                         u((byte_at(m_, at + 0x2c) * static_cast<std::int32_t>(fade)) >> 7));
                const std::uint32_t next = (byte_at(m_, at + 0x2d) + 0x3cu) & 0xffu;
                set_byte_at(m_, at + 0x2d, next > 0x80 ? 0x80 : next);
            }
            mess_sort_mode(scale <= short_at(m_, at + 0x22) ? 1 : 0);
            mess_text(static_cast<std::int32_t>(front_menu_cmenu_at(m_, self).field_c()), static_cast<std::int32_t>(y), word_at(m_, at + kNameOffset));
        }
        y += front_menu_cmenu_at(m_, self).field_14();
    }
}

// 0x0044bbc0: step the menu: skip hidden lines, move the cursor on the pad (with a repeat after the first sixteen frames), then slide every scale toward its target
void FrontScreens::cmenu_update(std::uint32_t self) {
    engine(0x004dd520, s_.live() ? std::vector<std::uint32_t>{self} : std::vector<std::uint32_t>{});
    auto word_copy = [&](std::uint32_t at, std::uint32_t to, std::uint32_t from) { set_half_at(m_, at + to, half_at(m_, at + from)); };
    auto current = [&]() { return entry(self, front_menu_cmenu_at(m_, self).field_6()); };
    while (byte_at(m_, current() + 0x25) == 0) {
        word_copy(current(), 0x1e, 0x22);
        word_copy(current(), 0x1c, 0x22);
        front_menu_cmenu_at(m_, self).set_field_6(static_cast<std::uint8_t>(front_menu_cmenu_at(m_, self).field_6() + 1));
        if (front_menu_cmenu_at(m_, self).field_6() >= front_menu_cmenu_at(m_, self).field_9()) {
            front_menu_cmenu_at(m_, self).set_field_6(0);
        }
        word_copy(current(), 0x1e, 0x20);
        word_copy(current(), 0x1c, 0x22);
    }
    const std::uint8_t up = global_byte(m_, kMenuUp);
    const std::uint8_t stick_up = global_byte(m_, kMenuStickUp);
    const bool any = up != 0 || global_byte(m_, kMenuDown) != 0 || stick_up != 0 || global_byte(m_, kMenuStickDown) != 0;
    if (!any) {
        set_global_word(m_, kRepeat, 0);
    } else {
        // after the first sixteen counts the move repeats every fourth count; a count is a call (retail), or a 30 Hz frame of time at 60 and 120 (framepacing::hold_steps:
        // the pause menu of a level is called once a scene). The count goes up after the test, as retail's does after the move
        const std::uint32_t steps = framepacing::hold_steps(framepacing::menu_hold_clock(), global_int(m_, kRepeat) == 0, [&] { return global_word(m_, front::kSimTick); });
        bool act = false;
        for (std::uint32_t step = 0; step < steps; ++step) {
            const std::int32_t repeat = global_int(m_, kRepeat);
            act = act || repeat == 0 || (repeat > 0xf && (repeat & 3) == 0);
            set_global_word(m_, kRepeat, global_word(m_, kRepeat) + 1);
        }
        if (act) {
            if (up != 0 || stick_up != 0) {
                word_copy(current(), 0x1e, 0x22);
                do {
                    front_menu_cmenu_at(m_, self).set_field_6(static_cast<std::uint8_t>(front_menu_cmenu_at(m_, self).field_6() == 0 ? front_menu_cmenu_at(m_, self).field_9() - 1 : front_menu_cmenu_at(m_, self).field_6() - 1));
                } while (byte_at(m_, current() + 0x25) == 0 || byte_at(m_, current() + 0x26) == 0);
                word_copy(current(), 0x1e, 0x20);
                s_.note(kSvcSfx, {6, 0x2000, 0});
            }
            if (global_byte(m_, kMenuDown) != 0 || global_byte(m_, kMenuStickDown) != 0) {
                word_copy(current(), 0x1e, 0x22);
                do {
                    front_menu_cmenu_at(m_, self).set_field_6(static_cast<std::uint8_t>(front_menu_cmenu_at(m_, self).field_6() + 1));
                    if (front_menu_cmenu_at(m_, self).field_6() >= front_menu_cmenu_at(m_, self).field_9()) {
                        front_menu_cmenu_at(m_, self).set_field_6(0);
                    }
                } while (byte_at(m_, current() + 0x25) == 0 || byte_at(m_, current() + 0x26) == 0);
                word_copy(current(), 0x1e, 0x20);
                s_.note(kSvcSfx, {6, 0x2000, 0});
            }
        }
    }
    for (std::uint32_t e = 0; e < front_menu_cmenu_at(m_, self).field_9(); ++e) {
        const std::uint32_t at = entry(self, e);
        std::int16_t now = short_at(m_, at + 0x1c);
        const std::int16_t target = short_at(m_, at + 0x1e);
        if (now < target) {
            now = static_cast<std::int16_t>(now + 0x28);
            set_half_at(m_, at + 0x1c, static_cast<std::uint16_t>(now));
            if (now > target) {
                set_half_at(m_, at + 0x1c, static_cast<std::uint16_t>(target));
            }
        }
        now = short_at(m_, at + 0x1c);
        if (now > target) {
            now = static_cast<std::int16_t>(now - 0x28);
            set_half_at(m_, at + 0x1c, static_cast<std::uint16_t>(now));
            if (now < target) {
                set_half_at(m_, at + 0x1c, static_cast<std::uint16_t>(target));
            }
        }
    }
}

}  // namespace opentony::runtime::frontend
