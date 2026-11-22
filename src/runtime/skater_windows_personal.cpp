#include "auto_views.hpp"
#include "skater_windows.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// PersonalWindow fields
constexpr std::uint32_t kHidden = 0x14;
constexpr std::uint32_t kList = 0x1fc;
constexpr std::uint32_t kNameRow = 0x200;
constexpr std::uint32_t kHometownRow = 0x204;
constexpr std::uint32_t kAgeRow = 0x208;
constexpr std::uint32_t kWeightRow = 0x21c;
constexpr std::uint32_t kFont = 0x384;
constexpr std::uint32_t kArrowLeft = 0x388;
constexpr std::uint32_t kArrowRight = 0x38c;
constexpr std::uint32_t kNameEntry = 0x390;
constexpr std::uint32_t kState = 0x394;
constexpr std::uint32_t kMarkers = 0x398;
constexpr std::uint32_t kMatched = 0xac;

}  // namespace

bool SkaterWindows::names_equal(std::uint32_t a, std::uint32_t b, std::uint32_t limit) const {
    if (a == 0) {
        return b == 0;
    }
    if (b == 0) {
        return false;
    }
    auto lower = [](std::uint8_t c) { return static_cast<std::uint8_t>((c >= 'A' && c <= 'Z') ? c + 0x20 : c); };
    for (;;) {
        const std::uint8_t x = lower(byte_at(m_, a++));
        const std::uint8_t y = lower(byte_at(m_, b++));
        if (x != y) {
            return false;
        }
        if (x == 0 || --limit == 0) {
            return true;
        }
    }
}

void SkaterWindows::copy_text(std::uint32_t dest, const std::string& value) {
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    m_.write(dest, bytes);
}

// The page's rows take their text through the row's virtual method 9 (set column): a row of the toolkit runs it natively, any other object (a fake
// row of a probe) is reported as the service "item_set_column" with the text.
void SkaterWindows::set_column_at(std::uint32_t row, const std::string& text, std::int32_t column, std::uint32_t offset, std::uint32_t props) {
    const std::uint32_t vt = word_at(m_, row);
    if (vt == kMenuVtRow || vt == kVtNumberRow) {
        menu_.row_set_column_from(row, text, column, offset, props);
        return;
    }
    s_.note("item_set_column", {row, static_cast<std::uint32_t>(column), offset, props}, text);
}

// 0x004293b0: poll the page. The result is 0 to stay, 1 to leave the page by its "next" row (the list's -1) and 2 to go back.
std::uint32_t SkaterWindows::personal_poll(std::uint32_t self) {
    skater_windows_personal_personal_at(m_, self).set_field_ac(0);
    if (skater_windows_personal_personal_at(m_, self).field_394() == 1) {
        // typing a name: the keyboard answers 1 for the enter key and 2 for cancel
        const std::uint32_t edge = pad_edges(0, 0, 6);
        const std::uint32_t answer = menu_.name_entry_handle_input(skater_windows_personal_personal_at(m_, self).field_390(), edge);
        if (answer == 1) {
            const std::int32_t id = static_cast<std::int32_t>(menu_.list_highlighted_id(skater_windows_personal_personal_at(m_, self).field_1fc()));
            if (id == 0) {
                const std::uint32_t name = menu_.name_entry_name(skater_windows_personal_personal_at(m_, self).field_390());
                copy_text(record(custom_slot()), m_.cstring(name, 0x100));
                skater_windows_personal_personal_at(m_, self).set_field_ac(static_cast<std::uint8_t>(personal_apply_preset_name(self) ? 1 : 0));
                skater_windows_personal_personal_at(m_, self).set_field_394(0);
                return 0;
            }
            if (id == 1) {
                const std::uint32_t name = menu_.name_entry_name(skater_windows_personal_personal_at(m_, self).field_390());
                copy_text(record(custom_slot()) + 0x10, m_.cstring(name, 0x100));
                skater_windows_personal_personal_at(m_, self).set_field_394(0);
                return 0;
            }
            check(false, kPersonalMsgNoEntry);
            skater_windows_personal_personal_at(m_, self).set_field_394(0);
            return 0;
        }
        if (answer == 2) {
            skater_windows_personal_personal_at(m_, self).set_field_394(0);
        }
        return 0;
    }
    check(skater_windows_personal_personal_at(m_, self).field_394() == 0, kPersonalMsgState);
    if (global_byte(m_, kPadConfirm) != 0) {
        sfx(0x91);
        set_global_byte(m_, kPadConfirm, 0);
        if (static_cast<std::int32_t>(menu_.list_highlighted_id(skater_windows_personal_personal_at(m_, self).field_1fc())) == -1) {
            return 1;
        }
        if (menu_.list_highlighted_id(skater_windows_personal_personal_at(m_, self).field_1fc()) == 0) {
            skater_windows_personal_personal_at(m_, self).set_field_394(1);
            const std::uint32_t slot = custom_slot();
            menu_.name_entry_set_name(skater_windows_personal_personal_at(m_, self).field_390(), record(slot));
            menu_.name_entry_set_prompt(skater_windows_personal_personal_at(m_, self).field_390(), global_word(m_, 0x0052e710), 0x100, 0x3c);
            return 0;
        }
        if (menu_.list_highlighted_id(skater_windows_personal_personal_at(m_, self).field_1fc()) == 1) {
            skater_windows_personal_personal_at(m_, self).set_field_394(1);
            const std::uint32_t slot = custom_slot();
            menu_.name_entry_set_name(skater_windows_personal_personal_at(m_, self).field_390(), record(slot) + 0x10);
            menu_.name_entry_set_prompt(skater_windows_personal_personal_at(m_, self).field_390(), global_word(m_, 0x0052e714), 0x100, 0x3c);
            return 0;
        }
        vcall_list("list_v5", skater_windows_personal_personal_at(m_, self).field_1fc(), 5, {3});
    }
    if (global_byte(m_, kPadBack) != 0) {
        sfx(0x93);
        set_global_byte(m_, kPadBack, 0);
        return 2;
    }
    return 0;
}

// 0x00429680: one frame: keep the age in the record, then (unless typing) move the list and, on left / right, step the value of the row the list
// stands on (stance, style or height).
void SkaterWindows::personal_update(std::uint32_t self) {
    if (skater_windows_personal_personal_at(m_, self).field_14() != 0) {
        return;
    }
    const std::uint32_t age = h_.number_value(skater_windows_personal_personal_at(m_, self).field_208());
    set_byte_at(m_, record(custom_slot()) + 0x20, age);
    if (skater_windows_personal_personal_at(m_, self).field_394() == 1) {
        return;
    }
    check(skater_windows_personal_personal_at(m_, self).field_394() == 0, kPersonalMsgState);
    const std::uint32_t edge = pad_edges(0, 0, 6);
    const std::uint32_t list = skater_windows_personal_personal_at(m_, self).field_1fc();
    vcall_list("list_v5", list, 5, {edge});
    vcall_list("list_v4", list, 4);
    if (edge == 2 || edge == 3) {
        sfx(0x90);
    }
    if (edge == 0 || edge == 1) {
        const std::int32_t step = edge == 0 ? -1 : 1;
        const std::int32_t id = static_cast<std::int32_t>(menu_.list_highlighted_id(skater_windows_personal_personal_at(m_, self).field_1fc()));
        bool play = false;
        switch (id) {
        case 2:
            play = true;
            break;
        case 3: {   // stance: 0..2, wrapping
            set_byte_at(m_, record(custom_slot()) + 0x24, u(add(sbyte_at(m_, record(custom_slot()) + 0x24), step)));
            if (sbyte_at(m_, record(custom_slot()) + 0x24) < 0) {
                set_byte_at(m_, record(custom_slot()) + 0x24, 2);
            }
            if (sbyte_at(m_, record(custom_slot()) + 0x24) >= 3) {
                set_byte_at(m_, record(custom_slot()) + 0x24, 0);
            }
            play = true;
            break;
        }
        case 4: {   // style: 0..1, wrapping
            set_byte_at(m_, record(custom_slot()) + 0x25, u(add(sbyte_at(m_, record(custom_slot()) + 0x25), step)));
            if (sbyte_at(m_, record(custom_slot()) + 0x25) < 0) {
                set_byte_at(m_, record(custom_slot()) + 0x25, 1);
            }
            if (sbyte_at(m_, record(custom_slot()) + 0x25) >= 2) {
                set_byte_at(m_, record(custom_slot()) + 0x25, 0);
            }
            play = true;
            break;
        }
        case 5: {   // height: steps of five between 0x46 and 0x91 (unsigned bytes)
            const std::uint32_t at = record(custom_slot()) + 0x23;
            set_byte_at(m_, at, byte_at(m_, at) + u(static_cast<std::int8_t>(step * 5)));
            if (byte_at(m_, record(custom_slot()) + 0x23) <= 0x46) {
                set_byte_at(m_, record(custom_slot()) + 0x23, 0x46);
            } else if (byte_at(m_, record(custom_slot()) + 0x23) >= 0x91) {
                set_byte_at(m_, record(custom_slot()) + 0x23, 0x91);
            }
            play = true;
            break;
        }
        default:
            break;
        }
        if (play) {
            sfx(0x90);
        }
    }
    w_.window_update(self);
}

// 0x00429910: draw the page: while typing the keyboard, otherwise the rows (with the name and hometown shortened to fit), the marker of the
// stance and style, the weight the height gives, the row separators and the arrows beside the stance and style rows.
void SkaterWindows::personal_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (skater_windows_personal_personal_at(m_, self).field_14() != 0) {
        return;
    }
    if (skater_windows_personal_personal_at(m_, self).field_394() == 1) {
        vcall_list("name_entry_draw", skater_windows_personal_personal_at(m_, self).field_390(), 2, {0, 0});
        return;
    }
    const std::uint32_t font = skater_windows_personal_personal_at(m_, self).field_384();
    std::string text = m_.cstring(record(custom_slot()), 0x100);
    f_.ellipsize_text(font + 4, text, 0xa5);
    set_column(skater_windows_personal_personal_at(m_, self).field_200(), text, 0xa0, font);
    text = m_.cstring(record(custom_slot()) + 0x10, 0x100);
    f_.ellipsize_text(font + 4, text, 0xa5);
    set_column(skater_windows_personal_personal_at(m_, self).field_204(), text, 0xa0, font);
    for (std::uint32_t i = 0; i < 5; ++i) {
        set_word_at(m_, word_at(m_, self + kMarkers + 4 * i) + 0x14, 1);
    }
    switch (sbyte_at(m_, record(custom_slot()) + 0x24)) {
    case 0:
        set_word_at(m_, word_at(m_, self + kMarkers + 4) + 0x14, 0);
        break;
    case 1:
        set_word_at(m_, skater_windows_personal_personal_at(m_, self).field_398() + 0x14, 0);
        break;
    case 2:
        set_word_at(m_, word_at(m_, self + kMarkers + 8) + 0x14, 0);
        break;
    default:
        check(false, kPersonalMsgBadStyle);
        break;
    }
    switch (sbyte_at(m_, record(custom_slot()) + 0x25)) {
    case 0:
        set_word_at(m_, word_at(m_, self + kMarkers + 0xc) + 0x14, 0);
        break;
    case 1:
        set_word_at(m_, word_at(m_, self + kMarkers + 0x10) + 0x14, 0);
        break;
    default:
        check(false, kPersonalMsgBadStance);
        break;
    }
    // weight = height^2 * 180 / 10000 (retail multiplies by the reciprocal 0x68db8bad and shifts)
    const std::int32_t height = byte_at(m_, record(custom_slot()) + 0x23);
    const std::int32_t scaled = mul(mul(mul(height, height), 5), 36);
    const std::int32_t high = static_cast<std::int32_t>((static_cast<std::int64_t>(0x68db8bad) * static_cast<std::int64_t>(scaled)) >> 32);
    std::int32_t weight = high >> 12;
    weight = add(weight, static_cast<std::int32_t>(static_cast<std::uint32_t>(weight) >> 31));
    if (word_at(m_, record(custom_slot()) + 0x54) == 1) {
        weight >>= 1;
    }
    set_column(skater_windows_personal_personal_at(m_, self).field_21c(), std::to_string(weight) + " " + m_.cstring(global_word(m_, 0x0052e70c), 0x100), 0xa0, font);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t child = word_at(m_, self + kNameRow + 4 * i);
        if (child == 0) {
            continue;
        }
        if (i != 3 && i != 5) {
            s_.note(kSvcFrameBox, {word_at(m_, child + 0x1c), u(add(add(int_at(m_, child + 0x20), int_at(m_, child + 0x18)), 2)), 0xe0, 1, 0x40, 0x40, 0x80});
        }
        if (global_word(m_, kSimClock) % 0x3c > 0x1e) {
            if (int_at(m_, word_at(m_, self + kNameRow + 4 * i) + 8) == 3 && menu_.list_highlighted_id(skater_windows_personal_personal_at(m_, self).field_1fc()) == 3) {
                s_.note(kSpriteDrawService, {skater_windows_personal_personal_at(m_, self).field_388(), 0xa2, 0x64, 0});
                s_.note(kSpriteDrawService, {skater_windows_personal_personal_at(m_, self).field_38c(), 0xf2, 0x64, 0});
            } else if (int_at(m_, word_at(m_, self + kNameRow + 4 * i) + 8) == 4 && menu_.list_highlighted_id(skater_windows_personal_personal_at(m_, self).field_1fc()) == 4) {
                s_.note(kSpriteDrawService, {skater_windows_personal_personal_at(m_, self).field_388(), 0xa2, 0x80, 0});
                s_.note(kSpriteDrawService, {skater_windows_personal_personal_at(m_, self).field_38c(), 0xf2, 0x80, 0});
            }
        }
    }
    vcall_list("list_v3", skater_windows_personal_personal_at(m_, self).field_1fc(), 3);
    w_.window_draw(self, add(skater_windows_personal_personal_at(m_, self).field_20(), x), add(skater_windows_personal_personal_at(m_, self).field_24(), y));
}

void SkaterWindows::personal_highlight_first(std::uint32_t self) { menu_.list_highlight_first(word_at(m_, self + kList)); }

bool SkaterWindows::personal_is_typing(std::uint32_t self) const { return word_at(m_, self + kState) == 1; }

// 0x00429cf0: when the name just typed is one of the preset names (case-insensitively, whole name), take the preset's age.
bool SkaterWindows::personal_apply_preset_name(std::uint32_t self) {
    if (global_word(m_, kPresetsEnabled) == 0 || global_byte(m_, kPresetNameTable) == 0) {
        return false;
    }
    std::uint32_t row = kPresetNameTable;
    for (;;) {
        const bool equal = names_equal(record(custom_slot()), row, 0xffffffff);
        if (equal) {
            break;
        }
        const std::uint8_t next = byte_at(m_, row + 0x80);
        row += 0x80;
        if (next == 0) {
            return false;
        }
    }
    check(m_.cstring(row, 0x100).size() < 0x10, kPersonalMsgName);
    check(m_.cstring(row + 0x10, 0x100).size() < 0x10, kPersonalMsgHometown);
    h_.number_set_value(skater_windows_personal_personal_at(m_, self).field_208(), sbyte_at(m_, row + 0x20));
    return true;
}

}  // namespace opentony::runtime::frontend
