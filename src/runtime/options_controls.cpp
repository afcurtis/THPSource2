#include "auto_views.hpp"
#include "options.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// ControlsMenu fields (vtable 0x005194a4)
constexpr std::uint32_t kCPlayer = 0xac;
constexpr std::uint32_t kCRemembered = 0xb0;    // the id that was highlighted when a button was last changed
constexpr std::uint32_t kCList = 0xb8;
constexpr std::uint32_t kCRows = 0xbc;          // 0x60 slots: the title, then the rows
constexpr std::uint32_t kCButtonRows = 0xc0;    // four rows: ids 0, 2, 1, 3
constexpr std::uint32_t kCStyle = 0x23c;
constexpr std::uint32_t kCTitleStyle = 0x240;
constexpr std::uint32_t kCAutokick = 0x244;
constexpr std::uint32_t kCVibration = 0x248;
constexpr std::uint32_t kCButton = 0x24c;       // four rows, by id: 0, 1, 2, 3

// CheatsPage fields (0x288 bytes, vtable 0x005194c4)
constexpr std::uint32_t kKList = 0xac;
constexpr std::uint32_t kKRows = 0xb0;          // 0x60 slots, the seventeen cheats then Back
constexpr std::uint32_t kKStyle = 0x230;
constexpr std::uint32_t kKStyleLocked = 0x234;
constexpr std::uint32_t kKFaces = 0x238;        // one picture for each of the nineteen skaters

constexpr std::uint32_t kControllerTable = 0x0055fc0c;      // four words for each player: the buttons of the four actions
constexpr std::uint32_t kAutokickTable = 0x0055fc2c;
constexpr std::uint32_t kVibrationTable = 0x0055fc34;
constexpr std::uint32_t kSecondPad = 0x0056b2b0;
constexpr std::uint32_t kReturnSaved = 0x00561ce0;
constexpr std::uint32_t kSkaterTable = 0x0053a3f8;
constexpr std::uint32_t kSkaterStride = 0x170;

constexpr std::uint32_t kPadLeft = 0x0056b079;
constexpr std::uint32_t kPadRight = 0x0056b089;
constexpr std::uint32_t kPadConfirm = 0x0056b029;
constexpr std::uint32_t kPadBack = 0x0056aff9;
constexpr std::uint32_t kPadUpA = 0x0056b099;
constexpr std::uint32_t kPadUpB = 0x0056b0a9;

constexpr std::uint32_t kMsgNoStringBuffer = 0x00535ec8;
constexpr std::uint32_t kMsgControlIndex = 0x00535eac;
constexpr std::uint32_t kMsgControlValue = 0x00535e8c;
constexpr std::uint32_t kMsgNoMenuItem = 0x00535d98;
constexpr std::uint32_t kMsgNoItem = 0x00535edc;
constexpr std::uint32_t kMsgEmpty = 0x0055fe10;
constexpr std::uint32_t kMsgState = 0x005345d8;
constexpr std::uint32_t kFormatPair = 0x00525854;           // "%s: %s"
constexpr std::uint32_t kFormatCheat = 0x00535f14;          // "%s: %d %s"
constexpr std::uint32_t kFormatLabel = 0x00535f10;          // "%s:"
constexpr std::uint32_t kPlayerFormat = 0x0052e770;         // pointer variable: "Player %d controls"
constexpr std::uint32_t kControlNames = 0x0052e7b0;         // four pointer variables
constexpr std::uint32_t kValueNames[4] = {0x0052e7c0, 0x0052e7c8, 0x0052e7c4, 0x0052e7cc};   // Spin left, Nollie, Spin right, Switch

// a pad record is 0x16c bytes, the player's own pad
std::uint32_t pad_of(std::uint32_t player) { return player * kPadStride; }

}  // namespace

// ---- the controller page (class at 0x005194a4) --------------------------------------------------------------------------------------------------------

// 0x00487490: a list with a title ("Player n controls"), the four buttons (each shows the action it is set to), the autokick and vibration steppers, Reset to defaults and
// Back. The values come from the controller table of the player.
std::uint32_t OptionsScreen::controls_construct(std::uint32_t self, std::uint32_t player, std::uint32_t font, std::uint32_t arrow_a, std::uint32_t arrow_b) {
    w_.window_construct(self, 0, 0, -1, -1);
    options_controls_controls_at(m_, self).set_field_0(static_cast<Word>(kVtControlsMenu));
    options_controls_controls_at(m_, self).set_field_ac(static_cast<Word>(player));
    std::uint32_t memory = operator_new(0x1bc);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.list_construct(memory, 0xc, 0x37, 0x1f4, 0x82, 1, 1, 3, 0);
    }
    options_controls_controls_at(m_, self).set_field_b8(static_cast<Word>(list));
    memory = operator_new(0x17c);
    const std::uint32_t style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    options_controls_controls_at(m_, self).set_field_23c(static_cast<Word>(style));
    menu_.copy_font_fields(style + 4, font);
    set_word_at(m_, style + 0x18, 0x80);
    set_word_at(m_, style + 0x1c, 0x80);
    set_word_at(m_, style + 0x20, 0x80);
    set_word_at(m_, style + 0x34, 0xc00);
    set_word_at(m_, style + 8, 1);
    set_word_at(m_, style + 0x150, 0xc00);
    set_word_at(m_, style + 0x154, 0xc00);
    memory = operator_new(0x17c);
    const std::uint32_t title_style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    options_controls_controls_at(m_, self).set_field_240(static_cast<Word>(title_style));
    menu_.copy_font_fields(title_style + 4, style + 4);
    for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
        set_word_at(m_, title_style + offset, word_at(m_, style + offset));
    }
    set_word_at(m_, title_style + 0x158, 0x91);
    set_word_at(m_, title_style + 0x15c, 0x12);
    set_word_at(m_, title_style + 0x160, 0xf);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kCRows + 4 * i, 0);
    }
    const std::string title = retail_format(m_.cstring(global_word(m_, kPlayerFormat), 0x40), {}, {static_cast<std::int32_t>(player + 1)});
    scratch_line(title);
    memory = operator_new(0x58);
    std::uint32_t row = 0;
    if (memory != 0) {
        row = menu_.text_scaled_construct(memory, 0, kScratchLine, title_style, 1);
    }
    options_controls_controls_at(m_, self).set_field_bc(static_cast<Word>(row));
    vcall_list("list_v6", options_controls_controls_at(m_, self).field_b8(), 6, {row});
    auto text = [&](std::uint32_t slot, std::uint32_t copy, std::uint32_t id, std::uint32_t text_pointer) {
        const std::uint32_t block = operator_new(0x54);
        std::uint32_t made = 0;
        if (block != 0) {
            made = menu_.text_construct(block, id, text_pointer, options_controls_controls_at(m_, self).field_23c());
        }
        set_word_at(m_, self + slot, made);
        if (copy != 0) {
            set_word_at(m_, self + copy, made);
        }
        vcall_list("list_v6", options_controls_controls_at(m_, self).field_b8(), 6, {made});
    };
    auto stepper = [&](std::uint32_t slot, std::uint32_t copy, std::uint32_t id, std::uint32_t label_variable) {
        scratch_text(0, {});
        set_global_word(m_, kScratchLine, word_at(m_, label_variable));
        set_global_word(m_, kScratchLine + 4, global_word(m_, 0x0052e52c));
        set_global_word(m_, kScratchLine + 8, global_word(m_, 0x0052e530));
        const std::uint32_t block = operator_new(0x104);
        std::uint32_t made = 0;
        if (block != 0) {
            made = engine_value(0x004189c0, {block, id, options_controls_controls_at(m_, self).field_23c(), kScratchLine, 3, 1});
        }
        set_word_at(m_, self + slot, made);
        set_word_at(m_, self + copy, made);
        vcall_list("list_v6", options_controls_controls_at(m_, self).field_b8(), 6, {made});
    };
    text(kCButtonRows, kCButton, 0, 0x00527c68);
    text(kCButtonRows + 4, kCButton + 8, 2, 0x00527c68);
    text(kCButtonRows + 8, kCButton + 4, 1, 0x00527c68);
    text(kCButtonRows + 0xc, kCButton + 0xc, 3, 0x00527c68);
    stepper(0xd0, kCAutokick, 4, 0x0052e7d4);
    stepper(0xd4, kCVibration, 7, 0x0052e7d0);
    {
        const std::uint32_t block = operator_new(0x54);
        std::uint32_t made = 0;
        if (block != 0) {
            made = menu_.text_construct(block, 8, global_word(m_, 0x0052e7d8), options_controls_controls_at(m_, self).field_23c());
        }
        options_controls_controls_at(m_, self).set_field_d8(static_cast<Word>(made));
        vcall_list("list_v6", options_controls_controls_at(m_, self).field_b8(), 6, {made});
    }
    memory = operator_new(0x54);
    std::uint32_t back = 0;
    if (memory != 0) {
        back = menu_.text_construct(memory, 9, global_word(m_, 0x0052e79c), options_controls_controls_at(m_, self).field_23c());
    }
    options_controls_controls_at(m_, self).set_field_dc(static_cast<Word>(back));
    vcall_list("list_v6", options_controls_controls_at(m_, self).field_b8(), 6, {back});
    vcall_list("list_v1", options_controls_controls_at(m_, self).field_b8(), 1, {arrow_a, arrow_b, 5});
    vcall_list("list_v2", options_controls_controls_at(m_, self).field_b8(), 2);
    for (std::uint32_t k = 0; k < 4; ++k) {
        set_word_at(m_, word_at(m_, self + kCButton + 4 * k) + 0x10, word_at(m_, kControllerTable + 16 * player + 4 * k));
    }
    set_word_at(m_, options_controls_controls_at(m_, self).field_244() + 0x10, word_at(m_, kAutokickTable + 4 * player) != 0 ? 1u : 2u);
    set_word_at(m_, options_controls_controls_at(m_, self).field_248() + 0x10, word_at(m_, kVibrationTable + 4 * player) != 0 ? 1u : 2u);
    return self;
}

std::uint32_t OptionsScreen::controls_delete(std::uint32_t self, std::uint32_t flag) {
    controls_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00487c30: unless the saved data list was just left the changed buttons and steppers go back to the controller table; then the list, the rows and the two styles go.
void OptionsScreen::controls_destroy(std::uint32_t self) {
    options_controls_controls_at(m_, self).set_field_0(static_cast<Word>(kVtControlsMenu));
    check(global_word(m_, kReturnSaved) == 0, kMsgEmpty);
    if (global_word(m_, kReturnSaved) == 0) {
        const std::uint32_t player = options_controls_controls_at(m_, self).field_ac();
        for (std::uint32_t k = 0; k < 4; ++k) {
            set_word_at(m_, kControllerTable + 16 * player + 4 * k, word_at(m_, word_at(m_, self + kCButton + 4 * k) + 0x10));
        }
        set_word_at(m_, kAutokickTable + 4 * player, word_at(m_, options_controls_controls_at(m_, self).field_244() + 0x10) == 1 ? 1u : 0u);
        set_word_at(m_, kVibrationTable + 4 * player, word_at(m_, options_controls_controls_at(m_, self).field_248() + 0x10) == 1 ? 1u : 0u);
    }
    if (options_controls_controls_at(m_, self).field_b8() != 0) {
        delete_object(options_controls_controls_at(m_, self).field_b8());
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kCRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kCRows + 4 * i));
        }
    }
    for (std::uint32_t slot : {kCStyle, kCTitleStyle}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    w_.window_destroy(self);
}

void OptionsScreen::controls_update(std::uint32_t self) { w_.window_update(self); }

// 0x00487f60: "<name of the control>: <what it does>" into a buffer (the name from one of four strings, what it does from one of four: spin left, nollie, spin right, switch).
// An unknown control leaves the buffer as it was; an unknown value stops after the name.
void OptionsScreen::controls_text(std::uint32_t buffer, std::uint32_t control, std::uint32_t value) {
    check(buffer != 0, kMsgNoStringBuffer);
    auto write = [&](const std::string& text) {
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        m_.write(buffer, bytes);
    };
    if (control <= 3) {
        write(m_.cstring(word_at(m_, kControlNames + 4 * control), 0x80));
    } else {
        check(false, kMsgControlIndex);
    }
    if (value > 3) {
        check(false, kMsgControlValue);
        return;
    }
    write(retail_format(m_.cstring(kFormatPair), {m_.cstring(buffer, 0x100), m_.cstring(word_at(m_, kValueNames[value]), 0x40)}, {}));
}

// 0x00487dd0: each button's text says what it does; the vibration stepper is available only when the pad can vibrate; a move on the pad makes a sound (not on Reset or Back);
// the list follows the pad; then the window.
void OptionsScreen::controls_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t player = options_controls_controls_at(m_, self).field_ac();
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t row = word_at(m_, self + kCButton + 4 * k);
        scratch_line({});
        controls_text(kScratchLine, k, word_at(m_, row + 0x10));
        vcall_item("item_v8", row, 8, {kScratchLine});
    }
    const std::uint32_t capable = engine_value(0x00489690, {player});
    set_byte_at(m_, options_controls_controls_at(m_, self).field_248() + 0xc, capable != 0 ? 1 : 0);
    s_.note(kSvcPadEdges, {player, 0, 6});
    const std::int32_t edge = static_cast<std::int32_t>(s_.value(kSvcPadEdges));
    const bool sideways = edge == 2 || edge == 3;
    const bool vertical = edge == 0 || edge == 1;
    const std::uint32_t chosen = menu_.list_highlighted_id(options_controls_controls_at(m_, self).field_b8());
    const bool plain_row = chosen != 9 && chosen != 8;
    if ((vertical && plain_row) || sideways) {
        sfx(0x11f);
    }
    const std::uint32_t list = options_controls_controls_at(m_, self).field_b8();
    if (list != 0) {
        vcall_list("list_v2", list, 2);
        vcall_list("list_v3", list, 3);
        vcall_list("list_v5", list, 5, {u(edge)});
        vcall_list("list_v4", list, 4);
    }
    w_.window_draw(self, x, y);
}

// 0x00488410: a value is kept between `low` and `high` by wrapping round.
void OptionsScreen::controls_clamp(std::uint32_t row, std::int32_t low, std::int32_t high) {
    check(row != 0, kMsgNoMenuItem);
    const std::int32_t value = int_at(m_, row + 0x10);
    if (value < low) {
        set_word_at(m_, row + 0x10, u(high));
    } else if (value > high) {
        set_word_at(m_, row + 0x10, u(low));
    }
}

// 0x00488480: no other button has the same value.
bool OptionsScreen::controls_unique(std::uint32_t self, std::uint32_t row) {
    const std::uint32_t value = word_at(m_, row + 0x10);
    std::int32_t same = 0;
    for (std::uint32_t k = 0; k < 4; ++k) {
        if (word_at(m_, word_at(m_, self + kCButtonRows + 4 * k) + 0x10) == value) {
            ++same;
        }
    }
    return same <= 1;
}

// 0x004884c0: the other buttons are moved on until none shares the value of the one that was just changed.
void OptionsScreen::controls_fix(std::uint32_t self) {
    const std::uint32_t remembered = options_controls_controls_at(m_, self).field_b0();
    std::uint32_t found = 0;
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t row = word_at(m_, self + kCButtonRows + 4 * k);
        if (word_at(m_, row + 8) == remembered) {
            found = row;
        }
    }
    check(found != 0, kMsgNoItem);
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t row = word_at(m_, self + kCButtonRows + 4 * k);
        if (row == found) {
            continue;
        }
        if (!controls_unique(self, row)) {
            do {
                set_word_at(m_, row + 0x10, word_at(m_, row + 0x10) + 1);
                controls_clamp(row, 0, 3);
            } while (!controls_unique(self, row));
        }
    }
}

// 0x00488160: left and right on the player's own pad change the highlighted button or stepper; any move or confirm fixes duplicates; confirm on Reset puts the defaults back
// (answers 0), on Back answers 1; back on the pad answers 2 (the second player's page also when no second pad is there).
std::uint32_t OptionsScreen::controls_step(std::uint32_t self) {
    const std::uint32_t player = options_controls_controls_at(m_, self).field_ac();
    const std::uint32_t pad = pad_of(player);
    if (byte_at(m_, kPadLeft + pad) != 0 || byte_at(m_, kPadRight + pad) != 0) {
        const std::int32_t direction = byte_at(m_, kPadLeft + pad) != 0 ? -1 : 1;
        const std::uint32_t list = options_controls_controls_at(m_, self).field_b8();
        const std::uint32_t id = menu_.list_highlighted_id(list);
        std::uint32_t row = 0;
        if (id == 0) {
            row = options_controls_controls_at(m_, self).field_24c();
        } else if (menu_.list_highlighted_id(list) == 1) {
            row = word_at(m_, self + kCButton + 4);
        } else if (menu_.list_highlighted_id(list) == 2) {
            row = word_at(m_, self + kCButton + 8);
        } else if (menu_.list_highlighted_id(list) == 3) {
            row = word_at(m_, self + kCButton + 0xc);
        }
        if (row != 0) {
            set_word_at(m_, row + 0x10, word_at(m_, row + 0x10) + u(direction));
            controls_clamp(row, 0, 3);
            options_controls_controls_at(m_, self).set_field_b0(static_cast<Word>(menu_.list_highlighted_id(list)));
        }
    }
    if (byte_at(m_, kPadUpA + pad) != 0 || byte_at(m_, kPadUpB + pad) != 0 || byte_at(m_, kPadConfirm + pad) != 0 || byte_at(m_, kPadBack + pad) != 0) {
        controls_fix(self);
    }
    if (byte_at(m_, kPadConfirm + pad) != 0) {
        set_global_byte(m_, kPadConfirm, 0);
        set_global_byte(m_, kPadConfirm + kPadStride, 0);
        const std::uint32_t chosen = menu_.list_highlighted_id(options_controls_controls_at(m_, self).field_b8());
        if (chosen == 8) {
            sfx(0x91);
            engine(0x00413f30, {options_controls_controls_at(m_, self).field_ac()});
            const std::uint32_t current = options_controls_controls_at(m_, self).field_ac();
            for (std::uint32_t k = 0; k < 4; ++k) {
                set_word_at(m_, word_at(m_, self + kCButton + 4 * k) + 0x10, word_at(m_, kControllerTable + 16 * current + 4 * k));
            }
            set_word_at(m_, options_controls_controls_at(m_, self).field_244() + 0x10, word_at(m_, kAutokickTable + 4 * current) != 0 ? 1u : 2u);
            set_word_at(m_, options_controls_controls_at(m_, self).field_248() + 0x10, word_at(m_, kVibrationTable + 4 * current) != 0 ? 1u : 2u);
            return 0;
        }
        if (menu_.list_highlighted_id(options_controls_controls_at(m_, self).field_b8()) == 9) {
            sfx(0x91);
            return 1;
        }
    }
    const std::uint32_t current = options_controls_controls_at(m_, self).field_ac();
    if (byte_at(m_, kPadBack + pad_of(current)) != 0) {
        sfx(0x93);
        set_global_byte(m_, kPadBack, 0);
        set_global_byte(m_, kPadBack + kPadStride, 0);
        return 2;
    }
    if (current == 1 && global_word(m_, kSecondPad) == 0) {
        return 2;
    }
    return 0;
}

// 0x00488590: whether the highlighted row is one that can change (neither Reset nor Back).
bool OptionsScreen::controls_selectable(std::uint32_t self) {
    if (menu_.list_highlighted_id(options_controls_controls_at(m_, self).field_b8()) == 8) {
        return false;
    }
    return menu_.list_highlighted_id(options_controls_controls_at(m_, self).field_b8()) != 9;
}

// ---- the cheats page (class at 0x005194c4) ---------------------------------------------------------------------------------------------------------------

namespace {

// the weight cheat's value is a percentage of the normal weight squared, in pounds
std::int32_t weight_pounds(std::uint32_t value) {
    const std::int32_t scaled = static_cast<std::int32_t>(value * value * 180u);
    const std::int64_t q = (static_cast<std::int64_t>(scaled) * 0x68db8badLL) >> 32;
    std::int32_t d = static_cast<std::int32_t>(q) >> 12;
    d += static_cast<std::int32_t>(static_cast<std::uint32_t>(d) >> 31);
    return d;
}

}  // namespace

// 0x004885d0: a scroll list with a row for each of the seventeen cheats (locked ones show "???" and cannot be chosen; a plain cheat is a text, a switch a stepper, the weight a
// number with its unit), Back, and a picture for each skater (greyed when the skater's game has not been cleared).
std::uint32_t OptionsScreen::cheats_construct(std::uint32_t self, std::uint32_t font, std::uint32_t arrow_a, std::uint32_t arrow_b) {
    w_.window_construct(self, 0, 0, -1, -1);
    options_controls_cheats_at(m_, self).set_field_0(static_cast<Word>(kVtCheatsPage));
    std::uint32_t memory = operator_new(0x350);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, 0, 0x2a, 0x200, 0x50, 1, 0x64, 2, 1);
    }
    options_controls_cheats_at(m_, self).set_field_ac(static_cast<Word>(list));
    memory = operator_new(0x17c);
    const std::uint32_t style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    options_controls_cheats_at(m_, self).set_field_230(static_cast<Word>(style));
    menu_.copy_font_fields(style + 4, font);
    set_word_at(m_, style + 0x18, 0x80);
    set_word_at(m_, style + 0x1c, 0x80);
    set_word_at(m_, style + 0x20, 0x80);
    set_word_at(m_, style + 0x34, 0xc00);
    set_word_at(m_, style + 8, 1);
    set_word_at(m_, style + 0x150, 0x800);
    set_word_at(m_, style + 0x154, 0x800);
    memory = operator_new(0x17c);
    const std::uint32_t locked_style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    options_controls_cheats_at(m_, self).set_field_234(static_cast<Word>(locked_style));
    menu_.copy_font_fields(locked_style + 4, style + 4);
    for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
        set_word_at(m_, locked_style + offset, word_at(m_, style + offset));
    }
    set_word_at(m_, locked_style + 0x158, 0x60);
    set_word_at(m_, locked_style + 0x164, 0x40);
    set_word_at(m_, locked_style + 0x15c, 0x40);
    set_word_at(m_, locked_style + 0x168, 0x40);
    set_word_at(m_, locked_style + 0x160, 0x10);
    set_word_at(m_, locked_style + 0x16c, 0x40);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kKRows + 4 * i, 0);
    }
    std::uint32_t count = 0;
    for (std::uint32_t cheat = 0; cheat < 0x11; ++cheat, ++count) {
        const std::uint32_t slot = self + kKRows + 4 * cheat;
        if (engine_value(0x00416940, {cheat}) == 0) {
            memory = operator_new(0x54);
            std::uint32_t row = 0;
            if (memory != 0) {
                row = menu_.text_construct(memory, cheat, 0x00535f20, options_controls_cheats_at(m_, self).field_234());
                set_word_at(m_, slot, row);
                set_byte_at(m_, row + 0xc, 0);
            } else {
                set_word_at(m_, slot, 0);
            }
        } else {
            const std::uint32_t type = engine_value(0x00416f50, {cheat});
            if (type == 0) {
                memory = operator_new(0x54);
                std::uint32_t row = 0;
                if (memory != 0) {
                    row = menu_.text_construct(memory, cheat, engine_value(0x00416e30, {cheat}), options_controls_cheats_at(m_, self).field_230());
                }
                set_word_at(m_, slot, row);
            } else if (type == 1) {
                const std::uint32_t name = engine_value(0x00416e30, {cheat});
                const std::string label = retail_format(m_.cstring(kFormatLabel), {m_.cstring(name, 0x80)}, {});
                const std::uint32_t label_at = scratch_text(0x80, label);
                scratch_text(0, {});
                set_global_word(m_, kScratchLine, label_at);
                set_global_word(m_, kScratchLine + 4, global_word(m_, 0x0052e52c));
                set_global_word(m_, kScratchLine + 8, global_word(m_, 0x0052e530));
                memory = operator_new(0x104);
                std::uint32_t row = 0;
                if (memory != 0) {
                    row = engine_value(0x004189c0, {memory, cheat, options_controls_cheats_at(m_, self).field_230(), kScratchLine, 3, 1});
                }
                set_word_at(m_, slot, row);
            } else if (type == 2) {
                const std::uint32_t value = engine_value(0x00416980, {cheat}) & 0xff;
                const std::uint32_t name = engine_value(0x00416e30, {cheat});
                const std::string text = retail_format(m_.cstring(kFormatCheat), {m_.cstring(name, 0x80), m_.cstring(global_word(m_, 0x0052e70c), 0x20)}, {weight_pounds(value)});
                const std::uint32_t text_at = scratch_text(0, text);
                memory = operator_new(0x54);
                std::uint32_t row = 0;
                if (memory != 0) {
                    row = menu_.text_construct(memory, cheat, text_at, options_controls_cheats_at(m_, self).field_230());
                }
                set_word_at(m_, slot, row);
                set_word_at(m_, row + 0x10, value);
            } else {
                check(false, kMsgEmpty);
            }
        }
        vcall_list("list_v6", options_controls_cheats_at(m_, self).field_ac(), 6, {word_at(m_, slot)});
    }
    memory = operator_new(0x54);
    std::uint32_t back = 0;
    if (memory != 0) {
        back = menu_.text_construct(memory, 0xffffffffu, global_word(m_, 0x0052e79c), options_controls_cheats_at(m_, self).field_230());
    }
    set_word_at(m_, self + kKRows + 4 * count, back);
    vcall_list("list_v6", options_controls_cheats_at(m_, self).field_ac(), 6, {back});
    vcall_list("list_v1", options_controls_cheats_at(m_, self).field_ac(), 1, {arrow_a, arrow_b, 5});
    vcall_list("list_v2", options_controls_cheats_at(m_, self).field_ac(), 2);
    menu_.list_highlight_first(options_controls_cheats_at(m_, self).field_ac());
    // a picture for each of the nineteen skaters
    std::uint32_t row_at = 0x0053a510;
    for (std::uint32_t k = 0; k < 20; ++k, row_at += kSkaterStride) {
        const std::uint32_t slot = self + kKFaces + 4 * k;
        bool cleared = false;
        std::uint32_t sprite = 0;
        auto face = [&](std::uint32_t file) {
            const std::uint32_t block = operator_new(0x5c);
            std::uint32_t made = 0;
            if (block != 0) {
                s_.note(kSpriteConstruct, {block, file, 0x40, 0x40, 0}, m_.cstring(file));
                made = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
            }
            return made;
        };
        if ((byte_at(m_, row_at + 4) & 2) != 0) {
            sprite = face(0x00535f04);
            set_word_at(m_, slot, sprite);
            for (std::uint32_t character = 0; character < 4; ++character) {
                if ((engine_value(0x00416740, {engine_value(0x004b1980, {character})}) & 0xff) != 0) {
                    cleared = true;
                }
            }
        } else if (word_at(m_, row_at) == 0) {
            sprite = face(0x00535ef4);
            set_word_at(m_, slot, sprite);
            cleared = false;
        } else {
            sprite = face(word_at(m_, row_at - 8));
            set_word_at(m_, slot, sprite);
            cleared = (engine_value(0x00416740, {k}) & 0xff) != 0;
        }
        s_.note(kSpriteInit, {word_at(m_, slot)});
        if (!cleared) {
            engine(0x00457810, {word_at(m_, slot)});
        }
        set_word_at(m_, word_at(m_, slot) + 0xc, 0xc00);
    }
    return self;
}

std::uint32_t OptionsScreen::cheats_delete(std::uint32_t self, std::uint32_t flag) {
    cheats_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00488d70: the list, the rows, the two styles, the pictures and the window base.
void OptionsScreen::cheats_destroy(std::uint32_t self) {
    options_controls_cheats_at(m_, self).set_field_0(static_cast<Word>(kVtCheatsPage));
    if (options_controls_cheats_at(m_, self).field_ac() != 0) {
        delete_object(options_controls_cheats_at(m_, self).field_ac());
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kKRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kKRows + 4 * i));
        }
    }
    for (std::uint32_t slot : {kKStyle, kKStyleLocked}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t i = 0; i < 0x14; ++i) {
        if (word_at(m_, self + kKFaces + 4 * i) != 0) {
            delete_object(word_at(m_, self + kKFaces + 4 * i));
        }
    }
    w_.window_destroy(self);
}

// 0x00488e30: a page that is hidden (the main list is showing) does not update.
void OptionsScreen::cheats_update(std::uint32_t self) {
    if (options_controls_cheats_at(m_, self).field_14() == 0) {
        w_.window_update(self);
    }
}

// 0x00488e40: left and right change the weight cheat by ten (50 .. 150 percent) and turn the switches (two cheats exclude each other); the list follows the pad (either pad);
// the window; the skaters' pictures, the first eight in one row and the rest in another.
void OptionsScreen::cheats_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (options_controls_cheats_at(m_, self).field_14() != 0) {
        return;
    }
    nav_sound();
    const bool sideways = global_byte(m_, kPadLeft) != 0 || global_byte(m_, kPadLeft + kPadStride) != 0 || global_byte(m_, kPadRight) != 0 || global_byte(m_, kPadRight + kPadStride) != 0;
    if (sideways) {
        sfx(0x11f);
        const std::int32_t delta = (global_byte(m_, kPadLeft) != 0 || global_byte(m_, kPadLeft + kPadStride) != 0) ? -10 : 10;
        const std::uint32_t list = options_controls_cheats_at(m_, self).field_ac();
        if (menu_.list_highlighted_id(list) == 8) {
            for (std::uint32_t i = 0; i < 0x60; ++i) {
                const std::uint32_t row = word_at(m_, self + kKRows + 4 * i);
                if (row != 0 && word_at(m_, row + 8) == menu_.list_highlighted_id(list)) {
                    set_word_at(m_, row + 0x10, word_at(m_, row + 0x10) + u(delta));
                    if (int_at(m_, row + 0x10) < 0x32) {
                        set_word_at(m_, row + 0x10, 0x32);
                    }
                    if (int_at(m_, row + 0x10) > 0x96) {
                        set_word_at(m_, row + 0x10, 0x96);
                    }
                    const std::string text = retail_format(m_.cstring(kFormatCheat), {m_.cstring(global_word(m_, 0x0052e3b0), 0x20), m_.cstring(global_word(m_, 0x0052e70c), 0x20)},
                                                           {weight_pounds(word_at(m_, row + 0x10))});
                    vcall_item("item_v8", row, 8, {scratch_text(0, text)});
                }
            }
        }
        if (menu_.list_highlighted_id(list) == 0xb || menu_.list_highlighted_id(list) == 4) {
            const std::uint32_t id = menu_.list_highlighted_id(list);
            const std::uint32_t partner = menu_.list_highlighted_id(list) == 0xb ? 4u : 0xbu;
            std::int32_t partner_index = -1;
            for (std::int32_t i = 0; i < 0x60; ++i) {
                const std::uint32_t row = word_at(m_, self + kKRows + 4 * u(i));
                if (row != 0 && word_at(m_, row + 8) == partner) {
                    partner_index = i;
                }
            }
            check(partner_index != -1, kMsgEmpty);
            for (std::uint32_t i = 0; i < 0x60; ++i) {
                const std::uint32_t row = word_at(m_, self + kKRows + 4 * i);
                if (row != 0 && word_at(m_, row + 8) == id && word_at(m_, row + 0x10) == 2) {
                    set_word_at(m_, word_at(m_, self + kKRows + 4 * u(partner_index)) + 0x10, 2);
                }
            }
        }
    }
    const std::uint32_t list = options_controls_cheats_at(m_, self).field_ac();
    if (list != 0) {
        vcall_list("list_v2", list, 2);
        vcall_list("list_v3", list, 3);
        s_.note(kSvcPadEdges, {0, 0, 0xa});
        std::int32_t edge = static_cast<std::int32_t>(s_.value(kSvcPadEdges));
        if (edge == -1) {
            s_.note(kSvcPadEdges, {1, 0, 0xa});
            edge = static_cast<std::int32_t>(s_.value(kSvcPadEdges));
        }
        vcall_list("list_v5", list, 5, {u(edge)});
        vcall_list("list_v4", list, 4);
    }
    w_.window_draw(self, x, y);
    std::int32_t face_x = 0x40;
    std::int32_t face_x2 = -0x158;
    std::uint32_t row_at = 0x0053a514;
    for (std::uint32_t k = 0; k < 20; ++k, row_at += kSkaterStride) {
        if ((byte_at(m_, row_at) & 2) != 0) {
            if (k != engine_value(0x004b1980, {0})) {
                continue;
            }
        }
        const std::uint32_t sprite = word_at(m_, self + kKFaces + 4 * k);
        if (row_at < 0x0053b094) {
            vcall_item("sprite_draw", sprite, 3, {u(face_x), 0x80, 0});
        } else {
            vcall_item("sprite_draw", sprite, 3, {u(face_x2), 0xa5, 0});
        }
        face_x += 0x30;
        face_x2 += 0x30;
    }
}

// 0x004891a0: confirm on Back answers 1 (0x91), confirm on a cheat makes the sound 0x92 and answers 0; back on the pad answers 2.
std::uint32_t OptionsScreen::cheats_step(std::uint32_t self) {
    if (global_byte(m_, kPadConfirm) == 0 && global_byte(m_, kPadConfirm + kPadStride) == 0) {
        if (global_byte(m_, kPadBack) == 0 && global_byte(m_, kPadBack + kPadStride) == 0) {
            return 0;
        }
        sfx(0x93);
        set_global_byte(m_, kPadBack, 0);
        set_global_byte(m_, kPadBack + kPadStride, 0);
        return 2;
    }
    set_global_byte(m_, kPadConfirm, 0);
    set_global_byte(m_, kPadConfirm + kPadStride, 0);
    if (menu_.list_highlighted_id(options_controls_cheats_at(m_, self).field_ac()) == 0xffffffffu) {
        sfx(0x91);
        return 1;
    }
    sfx(0x92);
    return 0;
}

// 0x00489240: every row shows the cheat's value (the weight's number, the others on or off).
void OptionsScreen::cheats_refresh(std::uint32_t self) {
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t row = word_at(m_, self + kKRows + 4 * i);
        if (row == 0) {
            continue;
        }
        const std::uint32_t id = word_at(m_, row + 8);
        if (id == 0xffffffffu) {
            continue;
        }
        if (id == 8) {
            set_word_at(m_, word_at(m_, self + kKRows + 4 * i) + 0x10, engine_value(0x00416980, {id}) & 0xff);
        } else {
            set_word_at(m_, word_at(m_, self + kKRows + 4 * i) + 0x10, (engine_value(0x00416980, {id}) & 0xff) != 0 ? 1u : 2u);
        }
    }
}

// 0x004892a0: every row's value is stored in the career.
void OptionsScreen::cheats_store(std::uint32_t self) {
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t row = word_at(m_, self + kKRows + 4 * i);
        if (row == 0) {
            continue;
        }
        const std::uint32_t id = word_at(m_, row + 8);
        if (id == 0xffffffffu) {
            continue;
        }
        if (id == 8) {
            engine(0x00416a00, {id, byte_at(m_, row + 0x10), 0});
            continue;
        }
        const std::uint32_t value = word_at(m_, row + 0x10);
        if (value == 1) {
            engine(0x00416a00, {id, 1, 0});
        } else if (value == 2) {
            engine(0x00416a00, {id, 0, 0});
        } else {
            check(false, kMsgState);
        }
    }
}

// 0x00489350: whether the highlighted cheat is one that can be switched (its type is not zero).
bool OptionsScreen::cheats_switchable(std::uint32_t self) {
    if (menu_.list_highlighted_id(options_controls_cheats_at(m_, self).field_ac()) == 0xffffffffu) {
        return false;
    }
    return engine_value(0x00416f50, {menu_.list_highlighted_id(options_controls_cheats_at(m_, self).field_ac())}) != 0;
}

// 0x00489390: whether Back is highlighted.
bool OptionsScreen::cheats_on_back(std::uint32_t self) { return menu_.list_highlighted_id(word_at(m_, self + kKList)) == 0xffffffffu; }

}  // namespace opentony::runtime::frontend
