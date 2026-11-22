#include "auto_views.hpp"
#include "skater_windows.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// StatWindow fields
constexpr std::uint32_t kHidden = 0x14;
constexpr std::uint32_t kList = 0x490;
constexpr std::uint32_t kSliders = 0x494;      // 0x60 pointers to the sliders' text parts (the dots sit 0x18 before)
constexpr std::uint32_t kHeading = 0x4bc;
constexpr std::uint32_t kProps = 0x614;
constexpr std::uint32_t kSprite = 0x618;
constexpr std::uint32_t kPoints = 0x61c;
constexpr std::uint32_t kDialog = 0x620;
constexpr std::uint32_t kConfirm = 0x624;
constexpr std::uint32_t kArrowLeft = 0x628;
constexpr std::uint32_t kArrowRight = 0x62c;
constexpr std::uint32_t kDialogFont = 0x344;
constexpr std::uint32_t kStatOffset = 0x38;      // the ten stat bytes of a career record
constexpr std::uint32_t kTable = 0x00539e10;     // pointers to ten-byte preset arrays
constexpr std::uint32_t kCharacterRows = 0x0053a514;

}  // namespace

// 0x4b6180 .. a formatted line of text with %d and %s only, the way sprintf writes it (the arguments in the order they are used, numbers and
// strings counted separately).
std::string SkaterWindows::format(const std::string& fmt, const std::vector<std::string>& strings, const std::vector<std::int32_t>& numbers) {
    std::string out;
    std::size_t next_string = 0;
    std::size_t next_number = 0;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == 'd') {
            out += std::to_string(numbers.at(next_number++));
            ++i;
        } else if (fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == 's') {
            out += strings.at(next_string++);
            ++i;
        } else {
            out.push_back(fmt[i]);
        }
    }
    return out;
}

// 0x004b6180: poll the stat page: a confirmation dialog when there is one, otherwise the pad. The result is 0 (stay), 1 (done, the stats are
// kept) or 2 (leave); while a dialog is up the page returns what the dialog said.
std::uint32_t SkaterWindows::stat_poll(std::uint32_t self) {
    if (skater_windows_stats_stat_at(m_, self).field_620() != 0) {
        const std::uint32_t edge = pad_edges(0, 0, 6);
        const std::uint32_t answer = engine_value(0x00426310, {skater_windows_stats_stat_at(m_, self).field_620(), edge, 0xffffffff});
        if (answer == 1) {
            stat_apply(self);
            if (skater_windows_stats_stat_at(m_, self).field_620() != 0) {
                s_.note(kVirtualDelete, {skater_windows_stats_stat_at(m_, self).field_620(), 1});
            }
            skater_windows_stats_stat_at(m_, self).set_field_620(0);
            if (sk_.custom_with_flag(sk_.current_character())) {
                set_byte_at(m_, 0x0056a4aa + custom_slot() * 0x80, 0);
            }
            return skater_windows_stats_stat_at(m_, self).field_624();
        }
        if (answer == 2) {
            if (skater_windows_stats_stat_at(m_, self).field_620() != 0) {
                s_.note(kVirtualDelete, {skater_windows_stats_stat_at(m_, self).field_620(), 1});
            }
            skater_windows_stats_stat_at(m_, self).set_field_620(0);
        }
        return 0;
    }
    auto open_dialog = [&](std::uint32_t result) {
        const std::uint32_t memory = operator_new(0x428);
        std::uint32_t dialog = 0;
        if (memory != 0) {
            const std::uint32_t text = points_text(skater_windows_stats_stat_at(m_, self).field_61c());
            dialog = engine_value(0x00425fa0, {memory, self + kDialogFont, text, 0x100, 0x78}, m_.cstring(text, 0x100));
        }
        skater_windows_stats_stat_at(m_, self).set_field_620(static_cast<Word>(dialog));
        skater_windows_stats_stat_at(m_, self).set_field_624(static_cast<Word>(result));
    };
    if (global_byte(m_, kPadConfirm) != 0) {
        sfx(0x91);
        set_global_byte(m_, kPadConfirm, 0);
        if (static_cast<std::int32_t>(menu_.list_highlighted_id(skater_windows_stats_stat_at(m_, self).field_490())) == -1) {
            check(skater_windows_stats_stat_at(m_, self).field_620() == 0, kStatMsgDialog);
            if (!points_changed(skater_windows_stats_stat_at(m_, self).field_61c()) && !sk_.custom_with_flag(sk_.current_character())) {
                set_byte_at(m_, 0x0056a4aa + custom_slot() * 0x80, 0);
                return 1;
            }
            open_dialog(1);
        } else {
            vcall_list("list_v5", skater_windows_stats_stat_at(m_, self).field_490(), 5, {3});
        }
    }
    if (global_byte(m_, kPadBack) != 0) {
        sfx(0x93);
        set_global_byte(m_, kPadBack, 0);
        check(skater_windows_stats_stat_at(m_, self).field_620() == 0, kStatMsgDialog);
        if (points_changed(skater_windows_stats_stat_at(m_, self).field_61c()) && !sk_.custom_with_flag(sk_.current_character())) {
            open_dialog(2);
            return 0;
        }
        return 2;
    }
    return 0;
}

// 0x004b6490: draw the dialog (its own update runs here too), the list, then the window's children.
void SkaterWindows::stat_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (skater_windows_stats_stat_at(m_, self).field_14() != 0) {
        return;
    }
    if (skater_windows_stats_stat_at(m_, self).field_620() != 0) {
        s_.note("dialog_v1", {skater_windows_stats_stat_at(m_, self).field_620()});
    }
    vcall_list("list_v3", skater_windows_stats_stat_at(m_, self).field_490(), 3);
    w_.window_draw(self, add(skater_windows_stats_stat_at(m_, self).field_20(), x), add(skater_windows_stats_stat_at(m_, self).field_24(), y));
}

// 0x004b64e0: one frame without a dialog: a sound for up / down, the list follows the pad, the points block refreshes.
void SkaterWindows::stat_update(std::uint32_t self) {
    if (skater_windows_stats_stat_at(m_, self).field_14() != 0) {
        return;
    }
    if (skater_windows_stats_stat_at(m_, self).field_620() == 0) {
        if (global_byte(m_, kPadUpDown) != 0 || global_byte(m_, kPadUpDown2) != 0) {
            sfx(0x90);
        }
        const std::uint32_t list = skater_windows_stats_stat_at(m_, self).field_490();
        const std::uint32_t edge = pad_edges(0, 0, 6);
        vcall_list("list_v5", list, 5, {edge});
        vcall_list("list_v4", skater_windows_stats_stat_at(m_, self).field_490(), 4);
        vcall_item("points_update", skater_windows_stats_stat_at(m_, self).field_61c(), 4);
    }
    w_.window_update(self);
}

// 0x004b6560: the page's list of ten stat sliders (one per stat of the current character's record) and the heading text above them.
void SkaterWindows::stat_build(std::uint32_t self) {
    std::uint32_t memory = operator_new(0x350);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, 0x2a, 0x2f, 0xef, 0x96, 0, 3, 6, 1);
    }
    skater_windows_stats_stat_at(m_, self).set_field_490(static_cast<Word>(list));
    set_word_at(m_, list + 0x1b4, 1);
    memory = operator_new(0x17c);
    std::uint32_t props = 0;
    if (memory != 0) {
        props = menu_.properties_construct(memory);
    }
    skater_windows_stats_stat_at(m_, self).set_field_614(static_cast<Word>(props));
    menu_.copy_font_fields(props + 4, self + 0xac);
    set_word_at(m_, props + 8, 2);
    set_word_at(m_, props + 0x0c, 3);
    f_.set_rgb(props + 4, 0xff, 0x60, 0x20);
    set_byte_at(m_, props + 0x25, 0);
    set_word_at(m_, props + 0x10, 0);
    set_word_at(m_, props + 0x14, 0);
    set_word_at(m_, props + 0x150, 0x1324);
    set_word_at(m_, props + 0x154, 0x1000);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kSliders + 4 * i, 0);
    }
    const std::uint32_t config = career::record(sk_.current_character());
    for (std::uint32_t k = 0; k < 10; ++k) {
        const std::uint32_t block = menu_.operator_new_thunk(0x74);
        std::uint32_t slider = 0;
        if (block != 0) {
            slider = menu_.slider_construct(block, k, word_at(m_, 0x0052e80c + 4 * k), skater_windows_stats_stat_at(m_, self).field_614(), skater_windows_stats_stat_at(m_, self).field_618(),
                                            u(sbyte_at(m_, config + kStatOffset + k)), skater_windows_stats_stat_at(m_, self).field_628(), skater_windows_stats_stat_at(m_, self).field_62c());
        }
        set_word_at(m_, self + kSliders + 4 * k, slider != 0 ? slider + 0x18 : 0);
    }
    const std::string heading = "         " + m_.cstring(global_word(m_, 0x0052e968), 0x100);
    memory = operator_new(0x54);
    std::uint32_t item = 0;
    if (memory != 0) {
        item = menu_.text_construct_from(memory, 0xffffffff, heading, skater_windows_stats_stat_at(m_, self).field_614());
    }
    skater_windows_stats_stat_at(m_, self).set_field_4bc(static_cast<Word>(item));
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t child = word_at(m_, self + kSliders + 4 * i);
        if (child != 0) {
            vcall_list("list_v6", skater_windows_stats_stat_at(m_, self).field_490(), 6, {child});
        }
    }
    vcall_list("list_v2", skater_windows_stats_stat_at(m_, self).field_490(), 2);
    std::int32_t y = 0x2f;
    for (std::uint32_t i = 0; y < 0x502; ++i, y += 0xd) {
        const std::uint32_t child = word_at(m_, self + kSliders + 4 * i);
        if (child == 0) {
            continue;
        }
        if (int_at(m_, child + 8) == -1) {
            vcall_item("item_set_position", child, 1, {u(add(int_at(m_, child + 0x1c), 0x3c)), u(add(y, 0xd))});
        } else {
            vcall_item("item_set_position", child, 1, {word_at(m_, child + 0x1c), u(y)});
        }
    }
}

void SkaterWindows::stat_highlight_first(std::uint32_t self) { menu_.list_highlight_first(word_at(m_, self + kList)); }

// 0x004b6c00: after the stance changed from `previous`, restate every slider: the dots show the stat and how far the points left could take it.
void SkaterWindows::stat_reset_sliders(std::uint32_t self, std::int32_t previous) {
    const std::uint32_t slot = custom_slot();
    const std::int32_t character = sk_.map_enabled_index(static_cast<int>(slot));
    check(byte_at(m_, 0x0056a4aa + slot * 0x80) != 0, 0x00539d58);
    const std::uint32_t config = career::record(character);
    const std::uint32_t from = word_at(m_, kTable + 4 * u(previous));
    const std::uint32_t to = word_at(m_, kTable + 4 * u(static_cast<std::int32_t>(sbyte_at(m_, 0x0056a4ac + (slot << 7)))));
    for (std::uint32_t i = 0; i < 10; ++i) {
        set_byte_at(m_, config + kStatOffset + i, byte_at(m_, config + kStatOffset + i) - byte_at(m_, from + i));
        set_byte_at(m_, config + kStatOffset + i, byte_at(m_, config + kStatOffset + i) + byte_at(m_, to + i));
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t child = word_at(m_, self + kSliders + 4 * i);
        if (child == 0) {
            continue;
        }
        const std::int32_t id = int_at(m_, child + 8);
        if (id < 0 || id >= 10) {
            continue;
        }
        const std::int32_t stat = sbyte_at(m_, config + kStatOffset + u(id));
        std::int32_t extra = int_at(m_, 0x0056a4b4 + (slot * 0x20 + u(id)) * 4);
        if (add(stat, extra) > 9) {
            extra = 10 - stat;
        }
        menu_.dots_set_range(child - 0x18, u(stat), add(extra, stat));
    }
}

// 0x004b6d20: apply the stats of the current custom skater to the sliders (the dots) and to the points block.
void SkaterWindows::stat_apply(std::uint32_t self) {
    const std::int32_t character = sk_.map_enabled_index(static_cast<int>(custom_slot()));
    const std::uint32_t config = career::record(character);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t child = word_at(m_, self + kSliders + 4 * i);
        if (child == 0) {
            continue;
        }
        const std::int32_t id = int_at(m_, child + 8);
        if (id < 0 || id >= 10) {
            continue;
        }
        const std::uint32_t stat = u(sbyte_at(m_, config + kStatOffset + u(id)));
        menu_.dots_set_range(child - 0x18, stat, static_cast<std::int32_t>(stat));
    }
    points_set_character(skater_windows_stats_stat_at(m_, self).field_61c(), character);
}

// ---- the points block ---------------------------------------------------------------------------------------------

std::uint32_t SkaterWindows::points_construct(std::uint32_t self, std::uint32_t font, std::uint32_t font2, std::int32_t x, std::int32_t y,
                                              std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    skater_windows_stats_points_at(m_, self).set_field_0(static_cast<Word>(kStatPointsVt));
    check(font != 0, kMsgNoFontName);
    check(font2 != 0, kStatMsgNoFont);
    std::uint32_t elements[8] = {};
    skater_windows_stats_points_at(m_, self).set_field_ac(0);
    skater_windows_stats_points_at(m_, self).set_field_b0(0);
    skater_windows_stats_points_at(m_, self).set_field_b8(0);
    auto text_element = [&](std::uint32_t text, std::uint32_t use_font, std::int32_t ex, std::int32_t ey) {
        const std::uint32_t memory = operator_new(0x30);
        return memory == 0 ? 0u : w_.text_construct(memory, text, use_font, ex, ey, -1, -1);
    };
    auto counter = [&](std::int32_t ey) {
        const std::uint32_t memory = operator_new(0xbc);
        return memory == 0 ? 0u : engine_value(0x004a1440, {memory, 0, font2, 0xb6, u(ey)});
    };
    elements[2] = text_element(global_word(m_, 0x0052e800), font, 0xd, 0x17);
    elements[3] = counter(0x17);
    skater_windows_stats_points_at(m_, self).set_field_ac(static_cast<Word>(elements[3]));
    elements[4] = text_element(global_word(m_, 0x0052e804), font, 0xd, 0x23);
    skater_windows_stats_points_at(m_, self).set_field_b4(static_cast<Word>(elements[4]));
    elements[5] = counter(0x23);
    skater_windows_stats_points_at(m_, self).set_field_b0(static_cast<Word>(elements[5]));
    elements[6] = text_element(0x00527c68, font2, 0xb6, 0x23);
    skater_windows_stats_points_at(m_, self).set_field_b8(static_cast<Word>(elements[6]));
    {
        const std::uint32_t memory = operator_new(0x2c);
        elements[7] = memory == 0 ? 0u : w_.sprite_construct(memory, 0x0052b780, 0, 0, -1, -1, 0, 0);
    }
    for (std::uint32_t element : elements) {
        if (element != 0) {
            w_.window_add(self, element);
        }
    }
    points_set_character(self, sk_.current_character());
    return self;
}

std::uint32_t SkaterWindows::points_delete(std::uint32_t self, std::uint32_t flag) {
    points_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004b7060: take over the ten stat bytes and the points of a character's record, then refresh through the virtual method 4.
void SkaterWindows::points_set_character(std::uint32_t self, std::int32_t character) {
    const std::uint32_t config = career::record(character);
    for (std::uint32_t i = 0; i < 10; ++i) {
        set_byte_at(m_, self + 0xbc + i, byte_at(m_, config + kStatOffset + i));
    }
    skater_windows_stats_points_at(m_, self).set_field_c8(static_cast<Word>(word_at(m_, config + 8)));
    vcall_item("points_update", self, 4);
}

void SkaterWindows::points_destroy(std::uint32_t self) {
    skater_windows_stats_points_at(m_, self).set_field_0(static_cast<Word>(kStatPointsVt));
    w_.window_destroy(self);
}

// 0x004b70b0: refresh the two counters: the current points, the cost of the next one (custom skaters show their spare points instead).
void SkaterWindows::points_update(std::uint32_t self) {
    if (skater_windows_stats_points_at(m_, self).field_ac() != 0) {
        const std::uint32_t config = career::record(sk_.current_character());
        engine(0x004a1540, {skater_windows_stats_points_at(m_, self).field_ac(), word_at(m_, config + 8)});
    }
    if (sk_.custom_with_flag(sk_.current_character())) {
        set_word_at(m_, skater_windows_stats_points_at(m_, self).field_b0() + 0x14, 1);
        set_word_at(m_, skater_windows_stats_points_at(m_, self).field_b8() + 0x14, 0);
    } else {
        set_word_at(m_, skater_windows_stats_points_at(m_, self).field_b0() + 0x14, 0);
        set_word_at(m_, skater_windows_stats_points_at(m_, self).field_b8() + 0x14, 1);
    }
    if (word_at(m_, skater_windows_stats_points_at(m_, self).field_b0() + 0x14) == 0) {
        w_.text_set_string(skater_windows_stats_points_at(m_, self).field_b4(), global_word(m_, 0x0052e804));
        engine(0x004a1540, {skater_windows_stats_points_at(m_, self).field_b0(), static_cast<std::uint32_t>(s_.query(kSvcOptionLimit))});   // (0x00415820: a service in the probe)
    }
    if (word_at(m_, skater_windows_stats_points_at(m_, self).field_b8() + 0x14) == 0) {
        const std::uint32_t index = u(sk_.custom_index(sk_.current_character()));
        w_.text_set_string(skater_windows_stats_points_at(m_, self).field_b4(), global_word(m_, 0x0052e38c));
        w_.text_set_string_from(skater_windows_stats_points_at(m_, self).field_b8(), std::to_string(sbyte_at(m_, 0x0056a4a9 + (index << 7))));
    }
}

// 0x004b71c0: move the stats of the block and of the current character's record from the preset array of `previous` to the one of the
// character's own stance.
void SkaterWindows::points_adjust(std::uint32_t self, std::int32_t previous) {
    const std::int32_t character = sk_.current_character();
    const std::uint32_t config = career::record(character);
    const std::uint32_t from = word_at(m_, kTable + 4 * u(previous));
    std::uint32_t to = 0;
    if ((byte_at(m_, kCharacterRows + u(character) * 0x170) & 2) == 0) {
        to = word_at(m_, kTable + 4 * byte_at(m_, 0x0053a506 + u(character) * 0x170));
    } else {
        to = word_at(m_, kTable + 4 * u(static_cast<std::int32_t>(sbyte_at(m_, 0x0056a4ac + (custom_slot() << 7)))));
    }
    for (std::uint32_t i = 0; i < 10; ++i) {
        set_byte_at(m_, config + kStatOffset + i, byte_at(m_, config + kStatOffset + i) - byte_at(m_, from + i));
        set_byte_at(m_, self + 0xbc + i, byte_at(m_, self + 0xbc + i) - byte_at(m_, from + i));
        set_byte_at(m_, config + kStatOffset + i, byte_at(m_, config + kStatOffset + i) + byte_at(m_, to + i));
        set_byte_at(m_, self + 0xbc + i, byte_at(m_, self + 0xbc + i) + byte_at(m_, to + i));
    }
}

// 0x004b7290: have points been spent since the block was loaded (the bytes of the block against the record's)
bool SkaterWindows::points_changed(std::uint32_t self) {
    const std::uint32_t config = career::record(sk_.current_character());
    std::int32_t own = 0;
    std::int32_t record_sum = 0;
    for (std::uint32_t i = 0; i < 10; ++i) {
        own += sbyte_at(m_, self + 0xbc + i);
        record_sum += sbyte_at(m_, config + kStatOffset + i);
    }
    check(record_sum >= own, kStatMsgPointsDecreased);
    return record_sum != own;
}

// 0x004b7320: the confirmation text for leaving the page, written to the block's buffer at +0xcc and returned.
std::uint32_t SkaterWindows::points_text(std::uint32_t self) {
    const std::uint32_t config = career::record(sk_.current_character());
    std::int32_t own = 0;
    std::int32_t record_sum = 0;
    for (std::uint32_t i = 0; i < 10; ++i) {
        own += sbyte_at(m_, self + 0xbc + i);
        record_sum += sbyte_at(m_, config + kStatOffset + i);
    }
    const std::uint32_t buffer = self + 0xcc;
    auto plural = [](std::int32_t n) { return std::string(n == 1 ? "" : "s"); };
    std::string text;
    if (sk_.custom_with_flag(sk_.current_character())) {
        const std::int32_t spent = record_sum - own;
        const std::int32_t left = 5 - spent;
        if (spent <= 0) {
            text = m_.cstring(global_word(m_, 0x0052e390), 0x100);
        } else {
            text = format(m_.cstring(global_word(m_, 0x0052e394), 0x100), {plural(spent)}, {spent});
        }
        if (left != 0) {
            text = format(m_.cstring(global_word(m_, 0x0052e398), 0x100), {text, plural(left)}, {left});
        }
    } else {
        const std::int32_t spent = record_sum - own;
        text = format(m_.cstring(global_word(m_, 0x0052e808), 0x100), {plural(spent)},
                      {static_cast<std::int32_t>(skater_windows_stats_points_at(m_, self).field_c8() - word_at(m_, config + 8)), spent});
    }
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(buffer, bytes);
    return buffer;
}

}  // namespace opentony::runtime::frontend
