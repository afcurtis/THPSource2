#include "auto_views.hpp"
#include "trick_screens.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgEmpty = 0x0055fe10;
constexpr std::uint32_t kMsgBadItem = 0x00541874;
constexpr std::uint32_t kMsgButtonRange = 0x0054188c;
constexpr std::uint32_t kMsgBadState = 0x005345d8;
constexpr std::uint32_t kMsgPossibleLeak = 0x005265b8;
constexpr std::uint32_t kMsgNoRecord = 0x00541928;
constexpr std::uint32_t kMsgNoFont = 0x0052576c;
constexpr std::uint32_t kMsgNoFont2 = 0x00539d70;

// TrickMenu fields (see trick_screens_menu.cpp)
constexpr std::uint32_t kCategoryList = 0x874;
constexpr std::uint32_t kComboList = 0x9f8;
constexpr std::uint32_t kComboRows = 0x9fc;
constexpr std::uint32_t kPageList = 0xb7c;
constexpr std::uint32_t kPageRows = 0xb80;
constexpr std::uint32_t kStyleTricks = 0xd0c;
constexpr std::uint32_t kStyleEmpty = 0xd14;
constexpr std::uint32_t kDialog = 0xd18;
constexpr std::uint32_t kPanel = 0xd28;
constexpr std::uint32_t kManager = 0xd44;
constexpr std::uint32_t kFontF = 0x728;
constexpr std::uint32_t kState = 0x2150;
constexpr std::uint32_t kBlock = 0x2154;

constexpr std::uint32_t kPadSquareEdge = 0x0056b009;
constexpr std::uint32_t kPadTriangleEdge = 0x0056b039;   // the pad's fourth edge (it only logs)
constexpr std::uint32_t kSpecialTable = 0x00540e30;

constexpr std::uint32_t kScratch = 0x7ff00400;

// the panel
constexpr std::uint32_t kPanelPoints = 0xac;
constexpr std::uint32_t kPanelPointsLabel = 0xb0;
constexpr std::uint32_t kPanelCount = 0xb4;
constexpr std::uint32_t kPanelCountLabel = 0xb8;
constexpr std::uint32_t kPanelGlyphs = 0xbc;
constexpr std::uint32_t kPanelGlyphsLabel = 0xc0;
constexpr std::uint32_t kPanelTimers = 0xc4;
constexpr std::uint32_t kPanelCounts = 0xd0;

std::string format_text(const std::string& fmt, const std::vector<std::string>& strings, const std::vector<std::int32_t>& numbers) {
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

}  // namespace

// 0x004c21a0: the page's tricks: for every record of the manager that this character may have and whose class fits the page, one row (the trick's name and
// its price, or "Available" when it is bought or free), sorted by cost; on the manual and grind pages the other class's tricks follow. A blank "Done" row ends
// the list, the first row is highlighted, and the row of the trick bound to the highlighted combination is highlighted instead.
void TrickScreens::build_page_list(std::uint32_t self) {
    if (word_at(m_, self + kPageList) != 0) {
        vcall_list("virtual_delete", word_at(m_, self + kPageList), 0, {1});
        set_word_at(m_, self + kPageList, 0);
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t row = word_at(m_, self + kPageRows + 4 * i);
        if (row != 0) {
            delete_object(row);
            set_word_at(m_, self + kPageRows + 4 * i, 0);
        }
    }
    std::uint32_t memory = operator_new(0x350);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, 0x2a, 0x4d, 0xe0, 0x73, 0, 3, 6, 1);
    }
    set_word_at(m_, self + kPageList, list);
    set_word_at(m_, list + 0x1b4, 1);
    std::int32_t count = 0;
    const std::int32_t total = int_at(m_, self + kManager + 0x1404);
    const std::uint32_t mask = category_mask(static_cast<std::int32_t>(menu_.list_highlighted_id(word_at(m_, self + kCategoryList))));
    for (std::int32_t n = 0; n < total; ++n) {
        const std::uint32_t record = record_at(self + kManager, n);
        const std::uint32_t flags = half_at(m_, record + 0x24);
        if (!requirements_met(sk_.current_character(), flags)) {
            continue;
        }
        bool take = false;
        if ((mask & 0x8000) != 0) {
            bool found = false;
            if ((flags & 0x8000) != 0) {
                for (std::uint32_t i = 0; i < 0x60; ++i) {
                    const std::uint32_t widget = word_at(m_, self + kComboRows + 4 * i);
                    if (widget != 0) {
                        const std::int32_t id = int_at(m_, widget + 8);
                        if (static_cast<std::int32_t>(menu_.list_highlighted_id(word_at(m_, self + kComboList))) == id) {
                            const std::uint32_t table = kSpecialTable + u(id) * 12;
                            found = half_at(m_, table + 2 * half_at(m_, table)) == 0xc;
                        }
                    }
                }
                take = found == ((flags & 0x4800) != 0);
            }
        } else {
            std::uint32_t m;
            if ((mask & 0x4800) == 0) {
                m = (mask & 0x300) == 0 ? (mask & flags) : (flags & 0x300);
            } else {
                m = mask & flags;
            }
            take = m != 0 && (flags & 0x8000) == 0;
        }
        if (!take) {
            continue;
        }
        memory = operator_new(0x100);
        std::uint32_t row = 0;
        if (memory != 0) {
            row = menu_.row_construct(memory, u(static_cast<std::int16_t>(half_at(m_, record + 0x20))), record, word_at(m_, self + kStyleTricks), 2, 0, 0);
        }
        set_word_at(m_, self + kPageRows + 4 * u(count), row);
        std::string price = format_text(m_.cstring(0x005252c4, 0x10), {}, {half_at(m_, record + 0x22)});
        const std::int32_t index = index_by_source(self + kManager, half_at(m_, record + 0x20));
        const bool purchased = (engine_value(0x00416070, {character_record(sk_.current_character()), u(index)}) & 0xff) != 0;
        if (purchased || half_at(m_, record + 0x22) == 0) {
            price = m_.cstring(global_word(m_, 0x0052e7f0), 0x20);
        }
        set_column(row, price, 1, 0xa0, word_at(m_, self + kStyleTricks));
        ++count;
    }
    sort_by_cost(self, count);
    if ((mask & 0x300) == 0) {
        for (std::int32_t i = 0; i < count; ++i) {
            vcall_list("list_v6", word_at(m_, self + kPageList), 6, {word_at(m_, self + kPageRows + 4 * u(i))});
        }
    } else {
        std::uint32_t first;
        std::uint32_t second;
        if ((mask & 0x200) != 0) {
            first = 0x200;
            second = 0x100;
        } else if ((mask & 0x100) != 0) {
            first = 0x100;
            second = 0x200;
        } else {
            check(false, kMsgEmpty);
            first = 0;
            second = 0;
        }
        for (std::uint32_t wanted : {first, second}) {
            for (std::int32_t i = 0; i < count; ++i) {
                const std::uint32_t row = word_at(m_, self + kPageRows + 4 * u(i));
                const std::uint32_t record = find_by_source(self + kManager, half_at(m_, row + 8));
                if ((wanted & half_at(m_, record + 0x24)) != 0) {
                    vcall_list("list_v6", word_at(m_, self + kPageList), 6, {row});
                }
            }
        }
    }
    const std::uint32_t done = scratch_text(done_text());
    memory = operator_new(0x100);
    std::uint32_t row = 0;
    if (memory != 0) {
        row = menu_.row_construct(memory, 0xffffffff, done, word_at(m_, self + kStyleTricks), 2, 0, 0);
    }
    set_word_at(m_, self + kPageRows + 4 * u(count), row);
    vcall_list("list_v6", word_at(m_, self + kPageList), 6, {row});
    ++count;
    vcall_list("list_v2", word_at(m_, self + kPageList), 2);
    menu_.list_highlight_first(word_at(m_, self + kPageList));
    const std::uint32_t assigned = assigned_record(self);
    if (assigned != 0) {
        for (std::int32_t i = 0; i < count; ++i) {
            if (int_at(m_, word_at(m_, self + kPageRows + 4 * u(i)) + 8) == static_cast<std::int16_t>(half_at(m_, assigned + 0x20))) {
                menu_.list_highlight(word_at(m_, self + kPageList), word_at(m_, word_at(m_, self + kPageRows + 4 * u(i)) + 8));
                break;
            }
        }
    }
}

// ---- the pad ------------------------------------------------------------------------------------------------------------------------------------

// 0x004c2ed0: state 0 (the pages): the list follows the pad; confirm opens a page's combinations (ids 6..10) or ends the screen ("Continue", id 11); back
// leaves.
std::uint32_t TrickScreens::update_categories(std::uint32_t self) {
    std::uint32_t pad = global_word(m_, kPadCurrent);
    if (byte_at(m_, kPadUp + pad * kPadStride) != 0 || byte_at(m_, kPadUp2 + pad * kPadStride) != 0) {
        sfx(0x90);
        pad = global_word(m_, kPadCurrent);
    }
    const std::uint32_t list = trick_screens_input_update_at(m_, self).field_874();
    const std::uint32_t edge = pad_edges_value(6);
    vcall_list("list_v5", list, 5, {edge});
    vcall_list("list_v4", list, 4);
    vcall_list("list_v3", list, 3);
    pad = global_word(m_, kPadCurrent);
    if (byte_at(m_, kPadConfirmEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadConfirmEdge + pad * kPadStride, 0);
        sfx(0x91);
        const std::int32_t id = static_cast<std::int32_t>(menu_.list_highlighted_id(list));
        if (id >= 6 && id <= 10) {
            rebuild_combo_list(self);
            trick_screens_input_update_at(m_, self).set_field_2150(1);
        } else if (id == 0xb) {
            return 1;
        } else {
            check(false, kMsgBadItem);
            (void)menu_.list_highlighted_id(trick_screens_input_update_at(m_, self).field_874());
        }
        pad = global_word(m_, kPadCurrent);
    }
    if (byte_at(m_, kPadBackEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadBackEdge + pad * kPadStride, 0);
        sfx(0x91);
        return 2;
    }
    return 0;
}

// 0x004c3050: state 1 (the combinations): every combination's row shows the trick bound to it (or "Empty"); the list follows the pad; confirm opens the
// trick list of the combination (the blank row goes back), square clears the binding (sound 0x93, or 0x92 when there is none), back returns to the pages.
std::uint32_t TrickScreens::update_combos(std::uint32_t self) {
    std::uint32_t pad = global_word(m_, kPadCurrent);
    if (byte_at(m_, kPadUp + pad * kPadStride) != 0 || byte_at(m_, kPadUp2 + pad * kPadStride) != 0) {
        sfx(0x90);
    }
    const std::uint32_t mask = category_mask(static_cast<std::int32_t>(menu_.list_highlighted_id(trick_screens_input_update_at(m_, self).field_874())));
    const std::uint32_t block = trick_screens_input_update_at(m_, self).field_2154();
    const std::string empty = m_.cstring(global_word(m_, 0x0052e7f4), 0x20);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t widget = word_at(m_, self + kComboRows + 4 * i);
        if (widget == 0) {
            continue;
        }
        const std::int32_t id = int_at(m_, widget + 8);
        if (id == -1) {
            continue;
        }
        if ((mask & 0x8000) != 0) {
            set_column(widget, empty, 1, 0x98, trick_screens_input_update_at(m_, self).field_d14());
            for (std::uint32_t slot = 0; slot < 5; ++slot) {
                if (static_cast<std::int32_t>(i) == static_cast<std::int8_t>(byte_at(m_, block + 0x30 + slot)) && byte_at(m_, block + 0x2b + slot) != 0xff) {
                    const std::uint32_t record = record_at(self + kManager, static_cast<std::int8_t>(byte_at(m_, block + 0x2b + slot)));
                    set_column(widget, m_.cstring(record, 0x20), 1, 0x98, trick_screens_input_update_at(m_, self).field_d0c());
                }
            }
        } else {
            check(id >= 0 && id < 0x2b, kMsgButtonRange);
            const std::uint8_t bound = byte_at(m_, block + u(id));
            if (bound != 0xff) {
                const std::uint32_t record = record_at(self + kManager, static_cast<std::int8_t>(bound));
                set_column(widget, m_.cstring(record, 0x20), 1, 0x98, trick_screens_input_update_at(m_, self).field_d0c());
            } else {
                set_column(widget, empty, 1, 0x98, trick_screens_input_update_at(m_, self).field_d14());
            }
        }
    }
    const std::uint32_t list = trick_screens_input_update_at(m_, self).field_9f8();
    const std::uint32_t edge = pad_edges_value(3);
    vcall_list("list_v5", list, 5, {edge});
    vcall_list("list_v4", list, 4);
    vcall_list("list_v3", list, 3);
    pad = global_word(m_, kPadCurrent);
    if (byte_at(m_, kPadConfirmEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadConfirmEdge + pad * kPadStride, 0);
        sfx(0x91);
        if (static_cast<std::int32_t>(menu_.list_highlighted_id(list)) == -1) {
            trick_screens_input_update_at(m_, self).set_field_2150(0);
        } else {
            trick_screens_input_update_at(m_, self).set_field_2150(2);
            build_page_list(self);
        }
        pad = global_word(m_, kPadCurrent);
    }
    if (byte_at(m_, kPadSquareEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadSquareEdge + pad * kPadStride, 0);
        if (static_cast<std::int32_t>(menu_.list_highlighted_id(list)) != -1 && assigned_record(self) != 0) {
            sfx(0x93);
            unbind_selected(self);
        } else {
            sfx(0x92);
        }
        pad = global_word(m_, kPadCurrent);
    }
    if (byte_at(m_, kPadBackEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadBackEdge + pad * kPadStride, 0);
        sfx(0x91);
        trick_screens_input_update_at(m_, self).set_field_2150(0);
    }
    return 0;
}

// 0x004c33f0: state 2 (the tricks): the list follows the pad; confirm on the blank row goes back, on a trick binds it when it is bought, answers that the
// points are too few (message 0, sound 0x92) or asks whether to spend them (state 3); back returns to the combinations.
std::uint32_t TrickScreens::update_tricks(std::uint32_t self) {
    std::uint32_t pad = global_word(m_, kPadCurrent);
    if (byte_at(m_, kPadUp + pad * kPadStride) != 0 || byte_at(m_, kPadUp2 + pad * kPadStride) != 0) {
        sfx(0x90);
        pad = global_word(m_, kPadCurrent);
    }
    const std::uint32_t list = trick_screens_input_update_at(m_, self).field_b7c();
    const std::uint32_t edge = pad_edges_value(3);
    vcall_list("list_v5", list, 5, {edge});
    vcall_list("list_v4", list, 4);
    vcall_list("list_v3", list, 3);
    pad = global_word(m_, kPadCurrent);
    if (byte_at(m_, kPadTriangleEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadTriangleEdge + pad * kPadStride, 0);
        s_.note(kSvcPrintfLog, {0x005418cc});
        (void)menu_.list_highlighted_id(trick_screens_input_update_at(m_, self).field_9f8());
        s_.note(kSvcPrintfLog, {0x005418c0});
        (void)menu_.list_highlighted_id(list);
        pad = global_word(m_, kPadCurrent);
    }
    if (byte_at(m_, kPadConfirmEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadConfirmEdge + pad * kPadStride, 0);
        sfx(0x91);
        if (static_cast<std::int32_t>(menu_.list_highlighted_id(list)) == -1) {
            trick_screens_input_update_at(m_, self).set_field_2150(1);
        } else {
            const std::uint32_t record = find_by_source(self + kManager, menu_.list_highlighted_id(list));
            const std::uint32_t config = character_record(sk_.current_character());
            const std::int32_t index = index_by_source(self + kManager, half_at(m_, record + 0x20));
            if ((engine_value(0x00416070, {config, u(index)}) & 0xff) != 0) {
                bind_selected(self, index);
                trick_screens_input_update_at(m_, self).set_field_2150(1);
            } else if (int_at(m_, config + 8) < static_cast<std::int32_t>(half_at(m_, record + 0x22))) {
                panel_show_message(trick_screens_input_update_at(m_, self).field_d28(), 0, 0);
                sfx(0x92);
                return 0;
            } else {
                check(trick_screens_input_update_at(m_, self).field_d18() == 0, kMsgPossibleLeak);
                const std::string text = format_text(m_.cstring(global_word(m_, 0x0052e7fc), 0x40), {m_.cstring(record, 0x20)}, {half_at(m_, record + 0x22)});
                const std::uint32_t buffer = scratch_text(text);
                const std::uint32_t block = operator_new(0x428);
                std::uint32_t dialog = 0;
                if (block != 0) {
                    dialog = screens_.confirm_construct(block, self + kFontF, buffer, 0x100, 0x78);
                }
                trick_screens_input_update_at(m_, self).set_field_d18(static_cast<Word>(dialog));
                trick_screens_input_update_at(m_, self).set_field_2150(3);
            }
        }
        pad = global_word(m_, kPadCurrent);
    }
    if (byte_at(m_, kPadBackEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadBackEdge + pad * kPadStride, 0);
        sfx(0x91);
        trick_screens_input_update_at(m_, self).set_field_2150(1);
    }
    return 0;
}

// 0x004c3a80: state 3 (spend the points?): the dialog follows the pad; yes binds the trick after the points are spent (a special trick with every special
// slot used gets the warning instead, state 4), no goes back to the tricks.
std::uint32_t TrickScreens::update_buy_dialog(std::uint32_t self) {
    std::uint32_t pad = global_word(m_, kPadCurrent);
    if (byte_at(m_, kPadUp + pad * kPadStride) != 0 || byte_at(m_, kPadUp2 + pad * kPadStride) != 0) {
        sfx(0x90);
        pad = global_word(m_, kPadCurrent);
    }
    const std::uint32_t edge = pad_edges_value(6);
    const std::uint32_t answer = screens_.confirm_poll(trick_screens_input_update_at(m_, self).field_d18(), static_cast<std::int32_t>(edge), -1);
    screens_.dialog_call(trick_screens_input_update_at(m_, self).field_d18(), 2);
    screens_.dialog_call(trick_screens_input_update_at(m_, self).field_d18(), 1);
    if (answer == 1) {
        if (trick_screens_input_update_at(m_, self).field_d18() != 0) {
            screens_.dialog_call(trick_screens_input_update_at(m_, self).field_d18(), 0, {1});
        }
        trick_screens_input_update_at(m_, self).set_field_d18(0);
        trick_screens_input_update_at(m_, self).set_field_2150(1);
        const std::uint32_t record = find_by_source(self + kManager, menu_.list_highlighted_id(trick_screens_input_update_at(m_, self).field_b7c()));
        const std::uint32_t config = character_record(sk_.current_character());
        const std::int32_t index = index_by_source(self + kManager, half_at(m_, record + 0x20));
        if ((byte_at(m_, record + 0x25) & 0x80) != 0 && specials_full(self)) {
            sfx(0x92);
            panel_show_message(trick_screens_input_update_at(m_, self).field_d28(), 1, 0);
            trick_screens_input_update_at(m_, self).set_field_2150(4);
            return 0;
        }
        if ((engine_value(0x00416140, {config, u(index), half_at(m_, record + 0x22)}) & 0xff) != 0) {
            bind_selected(self, index);
        }
        const std::uint32_t id = menu_.list_highlighted_id(trick_screens_input_update_at(m_, self).field_9f8());
        std::uint32_t i = 0;
        for (; i < 0x60; ++i) {
            if (word_at(m_, word_at(m_, self + kComboRows + 4 * i) + 8) == id) {
                break;
            }
        }
        if (i == 0x60) {
            sfx(0x91);
        } else {
            set_column(word_at(m_, self + kComboRows + 4 * i), m_.cstring(record, 0x20), 1, 0x98, trick_screens_input_update_at(m_, self).field_d0c());
            sfx(0x91);
        }
    } else if (answer == 2) {
        if (trick_screens_input_update_at(m_, self).field_d18() != 0) {
            screens_.dialog_call(trick_screens_input_update_at(m_, self).field_d18(), 0, {1});
        }
        trick_screens_input_update_at(m_, self).set_field_d18(0);
        trick_screens_input_update_at(m_, self).set_field_2150(2);
        sfx(0x93);
    }
    pad = global_word(m_, kPadCurrent);
    if (byte_at(m_, kPadBackEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadBackEdge + pad * kPadStride, 0);
        if (trick_screens_input_update_at(m_, self).field_d18() != 0) {
            screens_.dialog_call(trick_screens_input_update_at(m_, self).field_d18(), 0, {1});
        }
        trick_screens_input_update_at(m_, self).set_field_d18(0);
        trick_screens_input_update_at(m_, self).set_field_2150(2);
    }
    return 0;
}

// 0x004c3ca0: state 4 (the specials-full warning): confirm goes back to the combinations.
std::uint32_t TrickScreens::update_warning(std::uint32_t self) {
    const std::uint32_t pad = global_word(m_, kPadCurrent);
    if (byte_at(m_, kPadConfirmEdge + pad * kPadStride) != 0) {
        set_byte_at(m_, kPadConfirmEdge + pad * kPadStride, 0);
        sfx(0x91);
        trick_screens_input_update_at(m_, self).set_field_2150(1);
    }
    return 0;
}

// 0x004c3d30: one frame of input for the state the menu is in.
std::uint32_t TrickScreens::menu_update(std::uint32_t self) {
    switch (word_at(m_, self + kState)) {
    case 0:
        return update_categories(self);
    case 1:
        return update_combos(self);
    case 2:
        return update_tricks(self);
    case 3:
        return update_buy_dialog(self);
    case 4:
        return update_warning(self);
    default:
        check(false, kMsgBadState);
        return 0;
    }
}

// ---- the panel window ------------------------------------------------------------------------------------------------------------------------

// 0x004c3dd0: the cash counter and its label, "Specials used" and its "n of 5", the "Already assigned to" line with the combination's glyphs, and the backdrop.
std::uint32_t TrickScreens::panel_construct(std::uint32_t self, std::uint32_t font, std::uint32_t font2, std::uint32_t glyph_font, std::int32_t x,
                                            std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    trick_screens_input_panel_at(m_, self).set_field_0(static_cast<Word>(kVtTrickPanel));
    check(font != 0, kMsgNoFont);
    check(font2 != 0, kMsgNoFont2);
    auto text = [&](std::uint32_t slot, std::uint32_t string, std::uint32_t at_font, std::int32_t px, std::int32_t py) {
        const std::uint32_t memory = operator_new(0x30);
        std::uint32_t element = 0;
        if (memory != 0) {
            element = w_.text_construct(memory, string, at_font, px, py, -1, -1);
        }
        set_word_at(m_, self + slot, element);
        w_.window_add(self, element);
    };
    text(kPanelPointsLabel, global_word(m_, 0x0052e800), font, 0xd, 0x17);
    std::uint32_t memory = operator_new(0xbc);
    std::uint32_t points = 0;
    if (memory != 0) {
        points = engine_value(0x004a1440, {memory, 0, font2, 0xb6, 0x17});
    }
    trick_screens_input_panel_at(m_, self).set_field_ac(static_cast<Word>(points));
    w_.window_add(self, points);
    text(kPanelCountLabel, global_word(m_, 0x0052e308), font, 0xd, 0x23);
    text(kPanelCount, global_word(m_, 0x0052e30c), font2, 0xb6, 0x23);
    text(kPanelGlyphsLabel, 0x00541914, font, 0x14, 0x17);
    text(kPanelGlyphs, 0x0054190c, glyph_font, 100, 0x23);
    memory = operator_new(0x2c);
    std::uint32_t backdrop = 0;
    if (memory != 0) {
        backdrop = w_.sprite_construct(memory, 0x0052b780, 0, 0, -1, -1, 0, 0);
    }
    w_.window_add(self, backdrop);
    for (std::uint32_t i = 0; i < 3; ++i) {
        set_word_at(m_, self + kPanelCounts + 4 * i, 0);
        set_word_at(m_, self + kPanelTimers + 4 * i, 0);
    }
    return self;
}

std::uint32_t TrickScreens::panel_delete(std::uint32_t self, std::uint32_t flag) {
    panel_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void TrickScreens::panel_destroy(std::uint32_t self) {
    trick_screens_input_panel_at(m_, self).set_field_0(static_cast<Word>(kVtTrickPanel));
    w_.window_destroy(self);
}

// 0x004c40e0: the three timers count down (a message with turns left starts over); while message 2 is on, the "Already assigned to" line shows with the
// combination's glyphs and everything else hides; otherwise the cash counter and the specials count show for a while after they changed.
void TrickScreens::panel_update_frame(std::uint32_t self) {
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::uint32_t timer = self + kPanelTimers + 4 * i;
        const std::int32_t value = int_at(m_, timer) - 1;
        set_word_at(m_, timer, u(value));
        if (value < 0) {
            if (int_at(m_, timer + 12) < 1) {
                set_word_at(m_, timer, 0);
            } else {
                set_word_at(m_, timer + 12, u(int_at(m_, timer + 12) - 1));
                set_word_at(m_, timer, 0x14);
            }
        }
    }
    if (word_at(m_, self + kPanelCounts + 8) != 0) {
        for (std::uint32_t slot : {kPanelPoints, kPanelPointsLabel, kPanelCount, kPanelCountLabel}) {
            set_word_at(m_, word_at(m_, self + slot) + 0x14, 1);
        }
        set_word_at(m_, trick_screens_input_panel_at(m_, self).field_bc() + 0x14, int_at(m_, self + kPanelTimers + 8) > 10 ? 1 : 0);
        set_word_at(m_, trick_screens_input_panel_at(m_, self).field_c0() + 0x14, word_at(m_, trick_screens_input_panel_at(m_, self).field_bc() + 0x14));
        w_.window_update(self);
        return;
    }
    set_word_at(m_, trick_screens_input_panel_at(m_, self).field_c0() + 0x14, 1);
    set_word_at(m_, trick_screens_input_panel_at(m_, self).field_bc() + 0x14, 1);
    if (trick_screens_input_panel_at(m_, self).field_ac() != 0) {
        const std::uint32_t config = character_record(sk_.current_character());
        engine(0x004a1540, {trick_screens_input_panel_at(m_, self).field_ac(), word_at(m_, config + 8)});
        set_word_at(m_, trick_screens_input_panel_at(m_, self).field_ac() + 0x14, trick_screens_input_panel_at(m_, self).field_c4() > 10 ? 1 : 0);
        set_word_at(m_, trick_screens_input_panel_at(m_, self).field_b0() + 0x14, word_at(m_, trick_screens_input_panel_at(m_, self).field_ac() + 0x14));
    }
    if (trick_screens_input_panel_at(m_, self).field_b4() != 0) {
        const std::uint32_t block = current_block();
        check(trick_screens_input_panel_at(m_, self).field_b8() != 0, kMsgNoRecord);
        set_word_at(m_, trick_screens_input_panel_at(m_, self).field_b4() + 0x14, int_at(m_, self + kPanelTimers + 4) > 10 ? 1 : 0);
        set_word_at(m_, trick_screens_input_panel_at(m_, self).field_b8() + 0x14, word_at(m_, trick_screens_input_panel_at(m_, self).field_b4() + 0x14));
        std::int32_t used = 0;
        for (std::uint32_t i = 0; i < 5; ++i) {
            if (byte_at(m_, block + 0x2b + i) != 0xff) {
                ++used;
            }
        }
        w_.text_set_string(trick_screens_input_panel_at(m_, self).field_b4(), scratch_text(format_text(m_.cstring(global_word(m_, 0x0052e30c), 0x20), {}, {used, 5})));
    }
    w_.window_update(self);
}

// 0x004c4300: message `index` (0 the points are too few, 1 the specials are full, 2 the trick is on another combination) is shown for a while; message 2
// carries the glyphs of that combination.
void TrickScreens::panel_show_message(std::uint32_t self, std::int32_t index, std::uint32_t text) {
    check(index >= 0 && index < 3, kMsgEmpty);
    set_word_at(m_, self + kPanelCounts + 4 * u(index), 2);
    set_word_at(m_, self + kPanelTimers + 4 * u(index), 0x14);
    if (text != 0) {
        w_.text_set_string(trick_screens_input_panel_at(m_, self).field_bc(), text);
    }
    if (index == 2) {
        set_word_at(m_, self + kPanelCounts + 8, 4);
    }
}

}  // namespace opentony::runtime::frontend
