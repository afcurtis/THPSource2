#include "auto_views.hpp"
#include "menu_toolkit.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kVtRootClass = 0x005184b4;   // the root class's vtable: every destructor ends by storing it

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t sub(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }
std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
// `sar eax, 1`
std::int32_t sar1(std::int32_t v) { return v >> 1; }
// `cdq; sub eax, edx; sar eax, 1`: halve, rounding toward zero
std::int32_t half(std::int32_t v) { return (v - (v >> 31)) >> 1; }

}  // namespace

// ---- helpers --------------------------------------------------------------------------------------------

void MenuToolkit::copy_string(std::uint32_t dest, std::uint32_t source) {
    const std::string value = m_.cstring(source, 0x1000);
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    m_.write(dest, bytes);
}

void MenuToolkit::copy_font_fields(std::uint32_t d, std::uint32_t s) {
    // dwords +4..+0x1c, bytes +0x20 +0x21, dwords +0x24..+0x44, bytes +0x48..+0x4a, then the 0x100-byte character table at +0x4b
    for (std::uint32_t off = 4; off <= 0x1c; off += 4) {
        set_word_at(m_, d + off, word_at(m_, s + off));
    }
    set_byte_at(m_, d + 0x20, byte_at(m_, s + 0x20));
    set_byte_at(m_, d + 0x21, byte_at(m_, s + 0x21));
    for (std::uint32_t off = 0x24; off <= 0x44; off += 4) {
        set_word_at(m_, d + off, word_at(m_, s + off));
    }
    set_byte_at(m_, d + 0x48, byte_at(m_, s + 0x48));
    set_byte_at(m_, d + 0x49, byte_at(m_, s + 0x49));
    set_byte_at(m_, d + 0x4a, byte_at(m_, s + 0x4a));
    for (std::uint32_t off = 0x4b; off < 0x14b; ++off) {
        set_byte_at(m_, d + off, byte_at(m_, s + off));
    }
}

std::int32_t MenuToolkit::pulse_scale(std::uint32_t props, std::int32_t step) {
    // B + ((A - B) * step) / 4, where A is the normal and B the small scale; `cdq; and edx, 3; add eax, edx; sar eax, 2` divides
    // the product by four rounding toward zero.
    const std::int32_t a = int_at(m_, props + 0x150);
    const std::int32_t b = int_at(m_, props + 0x154);
    const std::int32_t product = mul(sub(a, b), step);
    return add((add(product, (product >> 31) & 3)) >> 2, b);
}

// ---- MenuControl and friends -----------------------------------------------------------------------------

std::uint32_t MenuToolkit::control_construct(std::uint32_t self, std::uint32_t id) {
    menu_toolkit_control_at(m_, self).set_field_8(static_cast<Word>(id));
    menu_toolkit_control_at(m_, self).set_field_0(static_cast<Word>(kMenuVtControl));
    menu_toolkit_control_at(m_, self).set_field_24(0);
    menu_toolkit_control_at(m_, self).set_field_28(0);
    menu_toolkit_control_at(m_, self).set_field_4(0);
    return self;
}

void MenuToolkit::control_destroy(std::uint32_t self) { set_word_at(m_, self, kVtRootClass); }

std::uint32_t MenuToolkit::control_delete(std::uint32_t self, std::uint32_t flag) {
    control_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::control_set_highlighted(std::uint32_t self, std::uint32_t value) { set_byte_at(m_, self + 0x0e, value); }
void MenuToolkit::control_set_value(std::uint32_t self, std::uint32_t value) { set_word_at(m_, self + 0x10, value); }
// printf_log("...%d", highlighted): the probe's stub records the format string only (the log call's arguments vary in count)
void MenuToolkit::control_log_navigate(std::uint32_t self) {
    (void)menu_toolkit_control_at(m_, self).field_e();
    s_.note(kSvcPrintfLog, {0x00534c20});
}

std::uint32_t MenuToolkit::control_b_construct(std::uint32_t self, std::uint32_t id) {
    control_construct(self, id);
    menu_toolkit_control_at(m_, self).set_field_0(static_cast<Word>(kMenuVtControlB));
    return self;
}

std::uint32_t MenuToolkit::control_b_delete(std::uint32_t self, std::uint32_t flag) {
    control_destroy(self);   // 0x00418b40 jumps to 0x0046f6c0
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

std::uint32_t MenuToolkit::control_id(std::uint32_t self) const { return word_at(m_, self + 0x08); }

// ---- MenuProperties --------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::properties_construct(std::uint32_t self) {
    menu_toolkit_properties_at(m_, self).set_field_0(static_cast<Word>(kVtRootClass));
    s_.note(kSvcFontConstruct, {self + 4});
    menu_toolkit_properties_at(m_, self).set_field_164(0x7a);
    menu_toolkit_properties_at(m_, self).set_field_168(0x7a);
    menu_toolkit_properties_at(m_, self).set_field_16c(0x7a);
    menu_toolkit_properties_at(m_, self).set_field_170(0x40);
    menu_toolkit_properties_at(m_, self).set_field_174(0x40);
    menu_toolkit_properties_at(m_, self).set_field_178(0x40);
    menu_toolkit_properties_at(m_, self).set_field_0(static_cast<Word>(kMenuVtProperties));
    menu_toolkit_properties_at(m_, self).set_field_150(0x1324);
    menu_toolkit_properties_at(m_, self).set_field_154(0x1000);
    menu_toolkit_properties_at(m_, self).set_field_158(0xb6);
    menu_toolkit_properties_at(m_, self).set_field_15c(0x84);
    menu_toolkit_properties_at(m_, self).set_field_160(0x24);
    return self;
}

void MenuToolkit::properties_destroy(std::uint32_t self) {
    s_.note(kSvcFontDestroy, {self + 4});
    menu_toolkit_properties_at(m_, self).set_field_0(static_cast<Word>(kVtRootClass));
}

std::uint32_t MenuToolkit::properties_delete(std::uint32_t self, std::uint32_t flag) {
    properties_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// ---- MenuText --------------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::text_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props) {
    return text_construct_from(self, id, m_.cstring(text, 0x1000), props);
}

std::uint32_t MenuToolkit::text_construct_from(std::uint32_t self, std::uint32_t id, const std::string& text, std::uint32_t props) {
    control_b_construct(self, id);
    menu_toolkit_text_at(m_, self).set_field_0(static_cast<Word>(kMenuVtText));
    menu_toolkit_text_at(m_, self).set_field_8(static_cast<Word>(id));
    menu_toolkit_text_at(m_, self).set_field_50(static_cast<Word>(props));
    menu_toolkit_text_at(m_, self).set_field_c(1);
    menu_toolkit_text_at(m_, self).set_field_d(1);
    set_word_at(m_, props + 0x34, word_at(m_, props + 0x150));
    text_set_text_from(self, text);
    return self;
}

void MenuToolkit::text_destroy(std::uint32_t self) {
    menu_toolkit_text_at(m_, self).set_field_0(static_cast<Word>(kMenuVtText));
    control_destroy(self);
}

std::uint32_t MenuToolkit::text_delete(std::uint32_t self, std::uint32_t flag) {
    text_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::item_set_position(std::uint32_t self, std::int32_t x, std::int32_t y) {
    set_word_at(m_, self + 0x1c, u(x));
    set_word_at(m_, self + 0x20, u(y));
}

void MenuToolkit::text_set_text(std::uint32_t self, std::uint32_t text) { text_set_text_from(self, m_.cstring(text, 0x1000)); }

void MenuToolkit::text_set_text_from(std::uint32_t self, const std::string& value) {
    check(value.size() < 0x20, kMenuMsgStringTooLongForMenu);
    write_string(self + 0x30, value);
    const std::uint32_t props = menu_toolkit_text_at(m_, self).field_50();
    set_word_at(m_, props + 0x34, word_at(m_, props + 0x150));
    check(props != 0, kMenuMsgNoProperties);
    menu_toolkit_text_at(m_, self).set_field_14(static_cast<Word>(u(f_.text_width(props + 4, self + 0x30))));
    menu_toolkit_text_at(m_, self).set_field_18(static_cast<Word>(u(f_.text_extent(props + 4, 0x00525768))));
}

// The font, colour and scale an item draws with, by its state. `props` is the style it draws with.
void MenuToolkit::prepare_item_font(std::uint32_t self, std::uint32_t props, bool set_justification) {
    if (set_justification) {
        set_word_at(m_, props + 0x08, word_at(m_, self + 0x2c));
    }
    set_word_at(m_, props + 0x0c, 1);
    set_word_at(m_, props + 0x34, word_at(m_, props + 0x154));
    auto colour = [&](std::uint32_t from) {
        set_word_at(m_, props + 0x18, word_at(m_, props + from));
        set_word_at(m_, props + 0x1c, word_at(m_, props + from + 4));
        set_word_at(m_, props + 0x20, word_at(m_, props + from + 8));
    };
    if (byte_at(m_, self + 0x0c) == 0) {
        colour(0x170);
    } else if (byte_at(m_, self + 0x0e) == 0) {
        colour(0x164);
    } else {
        if (word_at(m_, self + 0x28) == 0) {
            set_word_at(m_, props + 0x34, word_at(m_, props + 0x150));
        } else {
            const std::int32_t step = int_at(m_, self + 0x24);
            check(step >= 0 && step <= 4, kMenuMsgScaleOutOfRange);
            set_word_at(m_, props + 0x34, u(pulse_scale(props, step)));
        }
        colour(0x158);
    }
}

void MenuToolkit::text_draw(std::uint32_t self) {
    const std::uint32_t just = menu_toolkit_text_at(m_, self).field_2c();
    check(just == 0 || just == 1, kMenuMsgJustification);
    const std::uint32_t props = menu_toolkit_text_at(m_, self).field_50();
    prepare_item_font(self, props, true);
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (int_at(m_, props + 0x08) == 1) {
        y = add(sar1(static_cast<std::int32_t>(menu_toolkit_text_at(m_, self).field_18())), menu_toolkit_text_at(m_, self).field_20());
        x = add(sar1(static_cast<std::int32_t>(menu_toolkit_text_at(m_, self).field_14())), menu_toolkit_text_at(m_, self).field_1c());
    } else {
        y = add(sar1(static_cast<std::int32_t>(menu_toolkit_text_at(m_, self).field_18())), menu_toolkit_text_at(m_, self).field_20());
        x = menu_toolkit_text_at(m_, self).field_1c();
    }
    s_.note(kFontDrawService, {props + 4, u(x), u(y), self + 0x30, 0}, m_.cstring(self + 0x30, 0x40));
    const std::int32_t timer = static_cast<std::int32_t>(menu_toolkit_text_at(m_, self).field_24());
    if (timer > 0) {
        menu_toolkit_text_at(m_, self).set_field_24(static_cast<Word>(u(timer - 1)));
    }
}

// ---- MenuRow ---------------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::row_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props, std::int32_t columns,
                                         std::uint32_t arrow_left, std::uint32_t arrow_right) {
    text_construct(self, id, text, props);
    menu_toolkit_row_at(m_, self).set_field_0(static_cast<Word>(kMenuVtRow));
    check(columns > 0 && columns < 4, kMenuMsgBadColumnCount);
    menu_toolkit_row_at(m_, self).set_field_e4(static_cast<Word>(u(columns)));
    row_set_column(self, text, 0, 0, 0);
    menu_toolkit_row_at(m_, self).set_field_d4(0);
    menu_toolkit_row_at(m_, self).set_field_e8(static_cast<Word>(props));
    for (std::int32_t column = 1; column < static_cast<std::int32_t>(menu_toolkit_row_at(m_, self).field_e4()); ++column) {
        row_set_column(self, kEmptyText, column, 0, props);
    }
    menu_toolkit_row_at(m_, self).set_field_f8(static_cast<Word>(arrow_left));
    menu_toolkit_row_at(m_, self).set_field_fc(static_cast<Word>(arrow_right));
    return self;
}

void MenuToolkit::row_destroy(std::uint32_t self) {
    menu_toolkit_row_at(m_, self).set_field_0(static_cast<Word>(kMenuVtRow));
    text_destroy(self);   // 0x0046f930
}

std::uint32_t MenuToolkit::row_delete(std::uint32_t self, std::uint32_t flag) {
    row_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::row_set_column(std::uint32_t self, std::uint32_t text, std::int32_t column, std::uint32_t offset, std::uint32_t props) {
    row_set_column_from(self, m_.cstring(text, 0x1000), column, offset, props);
}

// The same with the text given as a value (a window formats a number into a local buffer and passes it).
void MenuToolkit::row_set_column_from(std::uint32_t self, const std::string& text, std::int32_t column, std::uint32_t offset,
                                      std::uint32_t props) {
    check(column >= 0 && column < static_cast<std::int32_t>(menu_toolkit_row_at(m_, self).field_e4()), kMenuMsgInvalidColumn);
    check(text.size() < 0x20, kMenuMsgStringTooLong);
    write_string(self + 0x54 + (u(column) << 5), text);
    set_word_at(m_, self + 0xd4 + 4 * u(column), offset);
    set_word_at(m_, self + 0xe8 + 4 * u(column), props);
}

void MenuToolkit::row_draw(std::uint32_t self) {
    const std::uint32_t just = menu_toolkit_row_at(m_, self).field_2c();
    check(just == 0 || just == 1, kMenuMsgJustification);
    const std::int32_t columns = static_cast<std::int32_t>(menu_toolkit_row_at(m_, self).field_e4());
    for (std::int32_t i = 0; i < columns; ++i) {
        std::uint32_t props = menu_toolkit_row_at(m_, self).field_50();
        const std::uint32_t own = word_at(m_, self + 0xe8 + 4 * u(i));
        if (own != 0) {
            props = own;
        }
        // this style's colours and scale, by the row's state (the justification belongs to the row's own style, set below)
        set_word_at(m_, props + 0x0c, 1);
        set_word_at(m_, props + 0x34, word_at(m_, props + 0x154));
        auto colour = [&](std::uint32_t from) {
            set_word_at(m_, props + 0x18, word_at(m_, props + from));
            set_word_at(m_, props + 0x1c, word_at(m_, props + from + 4));
            set_word_at(m_, props + 0x20, word_at(m_, props + from + 8));
        };
        if (menu_toolkit_row_at(m_, self).field_c() == 0) {
            colour(0x170);
        } else if (menu_toolkit_row_at(m_, self).field_e() == 0) {
            colour(0x164);
        } else {
            if (menu_toolkit_row_at(m_, self).field_28() == 0) {
                set_word_at(m_, props + 0x34, word_at(m_, props + 0x150));
            } else {
                const std::int32_t step = static_cast<std::int32_t>(menu_toolkit_row_at(m_, self).field_24());
                check(step >= 0 && step <= 4, kMenuMsgScaleOutOfRange);
                set_word_at(m_, props + 0x34, u(pulse_scale(props, step)));
            }
            colour(0x158);
        }
        const std::uint32_t text = self + 0x54 + (u(i) << 5);
        const std::int32_t offset = int_at(m_, self + 0xd4 + 4 * u(i));
        const std::uint32_t row_props = menu_toolkit_row_at(m_, self).field_50();
        const std::int32_t y_mid = add(sar1(menu_toolkit_row_at(m_, self).field_18()), menu_toolkit_row_at(m_, self).field_20());
        if (int_at(m_, row_props + 0x08) == 1) {
            const std::int32_t width = f_.text_width(props + 4, text);
            const std::int32_t x = add(add(sar1(width), menu_toolkit_row_at(m_, self).field_1c()), offset);
            s_.note(kFontDrawService, {props + 4, u(x), u(y_mid), text, 0}, m_.cstring(text, 0x40));
        } else {
            const std::int32_t x = add(menu_toolkit_row_at(m_, self).field_1c(), offset);
            s_.note(kFontDrawService, {props + 4, u(x), u(y_mid), text, 0}, m_.cstring(text, 0x40));
        }
    }
    const std::int32_t timer = static_cast<std::int32_t>(menu_toolkit_row_at(m_, self).field_24());
    if (timer > 0) {
        menu_toolkit_row_at(m_, self).set_field_24(static_cast<Word>(u(timer - 1)));
    }
    if (menu_toolkit_row_at(m_, self).field_e() == 0) {
        return;
    }
    if (global_word(m_, kSimClock) % 60 <= 30) {
        return;
    }
    std::uint32_t arrow_props = menu_toolkit_row_at(m_, self).field_50();
    if (menu_toolkit_row_at(m_, self).field_ec() != 0) {
        arrow_props = menu_toolkit_row_at(m_, self).field_ec();
    }
    const std::uint32_t left = menu_toolkit_row_at(m_, self).field_f8();
    if (left != 0) {
        const bool centred = int_at(m_, arrow_props + 0x08) == 1;
        const std::int32_t y = add(half(menu_toolkit_row_at(m_, self).field_18()), menu_toolkit_row_at(m_, self).field_20());
        std::int32_t x = 0;
        if (centred) {
            const std::int32_t width = f_.text_width(arrow_props + 4, self + 0x74);
            x = sub(sub(add(menu_toolkit_row_at(m_, self).field_d8(), menu_toolkit_row_at(m_, self).field_1c()), sar1(width)), 5);
        } else {
            x = sub(add(menu_toolkit_row_at(m_, self).field_d8(), menu_toolkit_row_at(m_, self).field_1c()), 5);
        }
        s_.note(kSpriteDrawService, {left, u(x), u(y), 0});
    }
    const std::uint32_t right = menu_toolkit_row_at(m_, self).field_fc();
    if (right != 0) {
        const bool centred = int_at(m_, arrow_props + 0x08) == 1;
        const std::int32_t y = add(half(menu_toolkit_row_at(m_, self).field_18()), menu_toolkit_row_at(m_, self).field_20());
        const std::int32_t width = f_.text_width(arrow_props + 4, self + 0x74);
        const std::int32_t reach = centred ? sar1(width) : width;
        const std::int32_t x = add(add(reach, menu_toolkit_row_at(m_, self).field_d8()), add(menu_toolkit_row_at(m_, self).field_1c(), 5));
        s_.note(kSpriteDrawService, {right, u(x), u(y), 0});
    }
}

// ---- MenuDots --------------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::dots_construct(std::uint32_t self, std::uint32_t sprite, std::uint32_t value, std::uint32_t maximum) {
    menu_toolkit_dots_at(m_, self).set_field_4(static_cast<Word>(value));
    menu_toolkit_dots_at(m_, self).set_field_8(static_cast<Word>(value));
    menu_toolkit_dots_at(m_, self).set_field_c(static_cast<Word>(maximum));
    menu_toolkit_dots_at(m_, self).set_field_0(static_cast<Word>(kMenuVtDots));
    check(sprite != 0, kMenuMsgNoDotsSprite);
    menu_toolkit_dots_at(m_, self).set_field_10(static_cast<Word>(sprite));
    set_byte_at(m_, sprite + 5, 1);
    menu_toolkit_dots_at(m_, self).set_field_14(0x1e);
    return self;
}

void MenuToolkit::dots_destroy(std::uint32_t self) { set_word_at(m_, self, kVtRootClass); }

std::uint32_t MenuToolkit::dots_delete(std::uint32_t self, std::uint32_t flag) {
    dots_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::dots_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t sprite = menu_toolkit_dots_at(m_, self).field_10();
    check(sprite != 0, kMenuMsgNoDotsSprite);
    const std::uint32_t height = global_word(m_, kScreenFillHeight);
    const bool software = global_word(m_, kSoftwareRenderer) != 0;
    fill_rect(0, 0, 0x200, height);
    if (software) {
        fill_rect(x, 0, mul(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_8()), 12), height);
    }
    set_byte_at(m_, sprite + 9, 0x80);
    set_byte_at(m_, sprite + 8, 0x80);
    set_byte_at(m_, sprite + 7, 0x80);
    s_.note(kSpriteDrawService, {sprite, u(x), u(y), 0});
    if (software) {
        fill_rect(add(x, mul(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_8()), 12)), 0, mul(sub(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_4()), static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_8())), 12), height);
    } else {
        fill_rect(x, 0, mul(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_8()), 12), height);
    }
    // the blink: the dots dim and brighten over the timer's thirty frames
    std::int32_t phase = static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_14());
    if (phase > 0xf) {
        phase = 0x1e - phase;
    }
    check(phase >= 0 && phase <= 0xf, kMenuMsgScaleOutOfRange);
    const std::int32_t shifted = phase << 6;
    // `imul` by 0x88888889 (signed), the high word plus the operand, shifted right by three and corrected toward zero: shifted / 15
    const std::int64_t product = static_cast<std::int64_t>(shifted) * static_cast<std::int32_t>(0x88888889u);
    std::int32_t scaled = add(static_cast<std::int32_t>(product >> 32), shifted) >> 3;
    scaled = add(scaled, static_cast<std::int32_t>(static_cast<std::uint32_t>(scaled) >> 31));
    const std::uint32_t colour = (static_cast<std::uint32_t>(scaled) + 0x40) & 0xff;
    set_byte_at(m_, sprite + 9, colour);
    set_byte_at(m_, sprite + 8, byte_at(m_, sprite + 9));
    set_byte_at(m_, sprite + 7, byte_at(m_, sprite + 8));
    s_.note(kSpriteDrawService, {sprite, u(x), u(y), 0});
    if (software) {
        fill_rect(add(x, mul(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_4()), 12)), 0, mul(sub(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_c()), static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_4())), 12), height);
    } else {
        fill_rect(add(x, mul(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_8()), 12)), 0, mul(sub(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_4()), static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_8())), 12), height);
    }
    set_byte_at(m_, sprite + 9, 0x20);
    set_byte_at(m_, sprite + 8, 0x20);
    set_byte_at(m_, sprite + 7, 0x20);
    s_.note(kSpriteDrawService, {sprite, u(x), u(y), 0});
    fill_rect(add(x, mul(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_4()), 12)), 0, mul(sub(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_c()), static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_4())), 12), height);
    const std::int32_t timer = sub(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_14()), 1);
    menu_toolkit_dots_at(m_, self).set_field_14(static_cast<Word>(u(timer)));
    if (timer <= 0) {
        menu_toolkit_dots_at(m_, self).set_field_14(0x1e);
    }
    fill_rect(0, 0, 0x200, height);
}

void MenuToolkit::dots_set_range(std::uint32_t self, std::uint32_t minimum, std::int32_t value) {
    menu_toolkit_dots_at(m_, self).set_field_8(static_cast<Word>(minimum));
    check(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_8()) >= 0 && static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_8()) <= static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_c()), kMenuMsgMinOutOfRange);
    menu_toolkit_dots_at(m_, self).set_field_4(static_cast<Word>(u(value)));
    if (value < static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_8())) {
        menu_toolkit_dots_at(m_, self).set_field_4(static_cast<Word>(menu_toolkit_dots_at(m_, self).field_8()));
    }
    check(static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_4()) >= 0 && static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_4()) <= static_cast<std::int32_t>(menu_toolkit_dots_at(m_, self).field_c()), kMenuMsgValueOutOfRange);
}

// ---- MenuSlider ------------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::slider_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props, std::uint32_t sprite,
                                            std::uint32_t value, std::uint32_t arrow_left, std::uint32_t arrow_right) {
    dots_construct(self, sprite, value, 10);
    text_construct(self + 0x18, id, text, props);
    menu_toolkit_slider_at(m_, self).set_field_18(static_cast<Word>(kMenuVtSliderDots));
    menu_toolkit_slider_at(m_, self).set_field_6c(static_cast<Word>(arrow_left));
    menu_toolkit_slider_at(m_, self).set_field_0(static_cast<Word>(kMenuVtSlider));
    menu_toolkit_slider_at(m_, self).set_field_70(static_cast<Word>(arrow_right));
    return self;
}

void MenuToolkit::slider_destroy(std::uint32_t self) {
    menu_toolkit_slider_at(m_, self).set_field_0(static_cast<Word>(kMenuVtSlider));
    menu_toolkit_slider_at(m_, self).set_field_18(static_cast<Word>(kMenuVtSliderDots));
    text_destroy(self + 0x18);   // 0x0046f930: the text sub-object's destructor
    menu_toolkit_slider_at(m_, self).set_field_0(static_cast<Word>(kVtRootClass));
}

std::uint32_t MenuToolkit::slider_delete(std::uint32_t self, std::uint32_t flag) {
    slider_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete_thunk(self);
    }
    return self;
}

std::uint32_t MenuToolkit::operator_new_thunk(std::uint32_t size) { return operator_new(size); }
void MenuToolkit::operator_delete_thunk(std::uint32_t block) { operator_delete(block); }

void MenuToolkit::slider_draw(std::uint32_t sub_object) {
    const std::uint32_t dots = sub_object - 0x18;
    dots_draw(dots, add(int_at(m_, sub_object + 0x1c), 0x64), add(int_at(m_, sub_object + 0x20), 2));
    text_draw(sub_object);
    const std::int32_t x0 = add(int_at(m_, sub_object + 0x1c), 0x64);
    if (byte_at(m_, sub_object + 0x0e) == 0 || int_at(m_, sub_object - 4) <= 0xf) {
        // (not highlighted, or in the dim half of the blink) only the timer moves on
    } else {
        const std::uint32_t left = word_at(m_, sub_object + 0x54);
        if (left != 0) {
            const std::int32_t y = add(half(int_at(m_, sub_object + 0x18)), int_at(m_, sub_object + 0x20));
            s_.note(kSpriteDrawService, {left, u(sub(x0, 6)), u(y), 0});
        }
        const std::uint32_t right = word_at(m_, sub_object + 0x58);
        if (right != 0) {
            const std::int32_t y = add(half(int_at(m_, sub_object + 0x18)), int_at(m_, sub_object + 0x20));
            const std::int32_t x = add(add(x0, mul(int_at(m_, sub_object - 0x0c), 12)), 6);
            s_.note(kSpriteDrawService, {right, u(x), u(y), 0});
        }
    }
    const std::int32_t timer = sub(int_at(m_, sub_object - 4), 1);
    set_word_at(m_, sub_object - 4, u(timer));
    if (timer <= 0) {
        set_word_at(m_, sub_object - 4, 0x1e);
    }
    fill_rect(0, 0, 0x200, global_word(m_, kScreenFillHeight));
}

void MenuToolkit::slider_adjust(std::uint32_t sub_object, std::uint32_t direction) {
    const std::uint32_t value_at = sub_object - 0x14;
    if (direction == 0) {
        if (int_at(m_, sub_object - 0x14) > int_at(m_, sub_object - 0x10)) {
            sfx(0x90);
            s_.note(kSvcOptionDecrease, {word_at(m_, sub_object + 0x08)});
            set_word_at(m_, value_at, u(sub(int_at(m_, value_at), 1)));
        } else {
            sfx(0x92);
        }
    } else if (direction == 1) {
        s_.note(kSvcOptionLimit);
        const std::uint32_t limit = static_cast<std::uint32_t>(s_.value(kSvcOptionLimit));
        s_.note(kSvcOptionCanIncrease, {word_at(m_, sub_object + 0x08), limit});
        const std::uint32_t can = static_cast<std::uint32_t>(s_.value(kSvcOptionCanIncrease)) & 0xff;
        if (can != 0) {
            sfx(0x90);
            set_word_at(m_, value_at, u(add(int_at(m_, value_at), 1)));
        } else {
            sfx(0x92);
        }
    }
    const std::int32_t value = int_at(m_, value_at);
    check(value >= int_at(m_, sub_object - 0x10) && value <= int_at(m_, sub_object - 0x0c), kMenuMsgValueOutOfRange);
}

std::uint32_t MenuToolkit::slider_sub_delete_thunk(std::uint32_t sub_object, std::uint32_t flag) { return slider_delete(sub_object - 0x18, flag); }
void MenuToolkit::slider_sub_thunk_b(std::uint32_t sub_object) { s_.note(kSvcSliderRefresh, {sub_object - 0x18}); }

// ---- MenuTextScaled --------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::text_scaled_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props, std::uint32_t grow) {
    text_construct(self, id, text, props);
    const std::int32_t height = static_cast<std::int32_t>(menu_toolkit_text_at(m_, self).field_18());
    menu_toolkit_text_at(m_, self).set_field_0(static_cast<Word>(kMenuVtTextScaled));
    menu_toolkit_text_at(m_, self).set_field_c(0);
    menu_toolkit_text_at(m_, self).set_field_54(static_cast<Word>(u(height)));
    if ((grow & 0xff) != 0) {
        menu_toolkit_text_at(m_, self).set_field_18(static_cast<Word>(u(add(sar1(height), height))));
    }
    return self;
}

void MenuToolkit::text_scaled_destroy(std::uint32_t self) { text_destroy(self); }

std::uint32_t MenuToolkit::text_scaled_delete(std::uint32_t self, std::uint32_t flag) {
    text_scaled_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::text_scaled_draw(std::uint32_t self) {
    const std::uint32_t just = menu_toolkit_text_at(m_, self).field_2c();
    check(just == 0 || just == 1, kMenuMsgJustification);
    const std::uint32_t props = menu_toolkit_text_at(m_, self).field_50();
    set_word_at(m_, props + 0x08, menu_toolkit_text_at(m_, self).field_2c());
    set_word_at(m_, props + 0x0c, 1);
    set_word_at(m_, props + 0x34, word_at(m_, props + 0x154));
    set_word_at(m_, props + 0x18, word_at(m_, props + 0x158));
    set_word_at(m_, props + 0x1c, word_at(m_, props + 0x15c));
    set_word_at(m_, props + 0x20, word_at(m_, props + 0x160));
    const std::int32_t y = add(sar1(static_cast<std::int32_t>(menu_toolkit_text_at(m_, self).field_54())), menu_toolkit_text_at(m_, self).field_20());
    std::int32_t x = menu_toolkit_text_at(m_, self).field_1c();
    if (int_at(m_, props + 0x08) == 1) {
        x = add(sar1(static_cast<std::int32_t>(menu_toolkit_text_at(m_, self).field_14())), x);
    }
    s_.note(kFontDrawService, {props + 4, u(x), u(y), self + 0x30, 0}, m_.cstring(self + 0x30, 0x40));
}

}  // namespace opentony::runtime::frontend
