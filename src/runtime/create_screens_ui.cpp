#include "auto_views.hpp"
#include "create_screens.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t sub(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }
std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }
// Division by two the way the compiler writes it for a signed value: add the sign bit, then shift.
std::int32_t half(std::int32_t v) { return add(v, static_cast<std::int32_t>(static_cast<std::uint32_t>(v) >> 31)) >> 1; }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgNoText = 0x00526a28;
constexpr std::uint32_t kMsgNoFont = 0x0052576c;

// TextBox fields
constexpr std::uint32_t kBackdrop = 0x04;
constexpr std::uint32_t kBoxX = 0x08;
constexpr std::uint32_t kBoxY = 0x0c;
constexpr std::uint32_t kBoxFont = 0x10;
constexpr std::uint32_t kBoxLines = 0x14;
constexpr std::uint32_t kBoxCount = 0x414;
// ConfirmBox fields
constexpr std::uint32_t kList = 0x418;
constexpr std::uint32_t kYes = 0x41c;
constexpr std::uint32_t kNo = 0x420;
constexpr std::uint32_t kStyle = 0x424;

}  // namespace

// 0x00425a40: a box that shows `text` in `font` centred on (x, y); the lines are laid out at once (create_spool.hpp).
std::uint32_t CreateScreens::box_construct(std::uint32_t self, std::uint32_t font, std::uint32_t text, std::int32_t x, std::int32_t y) {
    create_screens_ui_box_at(m_, self).set_field_0(static_cast<Word>(kVtTextBox));
    create_screens_ui_box_at(m_, self).set_field_414(0);
    check(text != 0, kMsgNoText);
    check(font != 0, kMsgNoFont);
    create_screens_ui_box_at(m_, self).set_field_8(static_cast<Word>(u(x)));
    create_screens_ui_box_at(m_, self).set_field_10(static_cast<Word>(font));
    create_screens_ui_box_at(m_, self).set_field_c(static_cast<Word>(u(y)));
    // retail measures each candidate line with Font_TextWidth on the box's font
    spool_.layout(self, text, [&](const std::string& line) { return f_.text_width_of(font, line); });
    create_screens_ui_box_at(m_, self).set_field_4(0);
    return self;
}

std::uint32_t CreateScreens::box_delete(std::uint32_t self, std::uint32_t flag) {
    box_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void CreateScreens::box_destroy(std::uint32_t self) { set_word_at(m_, self, kVtRoot); }

// 0x00425b70: the lines go one below the other, `step` (one and a half text heights) apart and centred on the box's y; with the backdrop flag a dark
// rectangle (two pixels bigger than the widest line each way) is drawn behind them.
void CreateScreens::box_draw(std::uint32_t self) {
    const std::uint32_t font = create_screens_ui_box_at(m_, self).field_10();
    const std::uint32_t lines = self + kBoxLines;
    const std::int32_t step = half(mul(f_.text_extent(font, lines), 3));
    const std::int32_t count = static_cast<std::int32_t>(create_screens_ui_box_at(m_, self).field_414());
    std::int32_t y = sub(static_cast<std::int32_t>(create_screens_ui_box_at(m_, self).field_c()), half(mul(sub(count, 1), step)));
    const std::int32_t top = sub(y, f_.text_top(font, lines));
    std::int32_t widest = 0;
    {
        std::uint32_t line = lines;
        for (std::int32_t i = 0; i < count; ++i) {
            s_.note(kFontDrawService, {font, create_screens_ui_box_at(m_, self).field_8(), u(y), line, 0}, m_.cstring(line, 0x100));
            y = add(y, step);
            line += 0x80;
        }
    }
    if (create_screens_ui_box_at(m_, self).field_4() != 0) {
        std::uint32_t line = lines;
        for (std::int32_t i = 0; i < count; ++i) {
            if (widest < f_.text_width(font, line)) {
                widest = f_.text_width(font, line);
            }
            line += 0x80;
        }
        const std::int32_t extent = f_.text_extent(font, lines);
        s_.note(kSvcSelectionBox, {u(sub(sub(static_cast<std::int32_t>(create_screens_ui_box_at(m_, self).field_8()), half(widest)), 4)), u(add(top, -4)), u(add(widest, 8)),
                                   u(add(sub(mul(count, step), half(extent)), 8)), 0x20, 0x20, 0x20});
    }
}

// 0x00425fa0: a text box with a two-item list (the second item is the default) under it.
std::uint32_t CreateScreens::confirm_construct(std::uint32_t self, std::uint32_t font, std::uint32_t text, std::int32_t x, std::int32_t y) {
    box_construct(self, font, text, x, y);
    create_screens_ui_confirm_at(m_, self).set_field_0(static_cast<Word>(kVtConfirmBox));
    std::uint32_t memory = operator_new(0x17c);
    std::uint32_t props = 0;
    if (memory != 0) {
        props = menu_.properties_construct(memory);
    }
    create_screens_ui_confirm_at(m_, self).set_field_424(static_cast<Word>(props));
    menu_.copy_font_fields(props + 4, font);
    set_word_at(m_, props + 0x34, 0x1000);
    set_word_at(m_, props + 0x150, 0x1324);
    set_word_at(m_, props + 0x154, 0x1000);
    memory = operator_new(0x1bc);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.list_construct(memory, 0, u(add(y, 0x14)), 0x200, u(add(y, 0x3c)), 1, 0, 3, 1);
    }
    create_screens_ui_confirm_at(m_, self).set_field_418(static_cast<Word>(list));
    memory = operator_new(0x54);
    std::uint32_t yes = 0;
    if (memory != 0) {
        yes = menu_.text_construct(memory, 1, global_word(m_, 0x0052e410), create_screens_ui_confirm_at(m_, self).field_424());
    }
    create_screens_ui_confirm_at(m_, self).set_field_41c(static_cast<Word>(yes));
    memory = operator_new(0x54);
    std::uint32_t no = 0;
    if (memory != 0) {
        no = menu_.text_construct(memory, 0, global_word(m_, 0x0052e414), create_screens_ui_confirm_at(m_, self).field_424());
    }
    create_screens_ui_confirm_at(m_, self).set_field_420(static_cast<Word>(no));
    vcall_list("list_v6", create_screens_ui_confirm_at(m_, self).field_418(), 6, {create_screens_ui_confirm_at(m_, self).field_41c()});
    vcall_list("list_v6", create_screens_ui_confirm_at(m_, self).field_418(), 6, {create_screens_ui_confirm_at(m_, self).field_420()});
    vcall_list("list_v2", create_screens_ui_confirm_at(m_, self).field_418(), 2);
    confirm_highlight(self, 0);
    return self;
}

std::uint32_t CreateScreens::confirm_delete(std::uint32_t self, std::uint32_t flag) {
    confirm_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void CreateScreens::confirm_destroy(std::uint32_t self) {
    create_screens_ui_confirm_at(m_, self).set_field_0(static_cast<Word>(kVtConfirmBox));
    for (std::uint32_t slot : {kYes, kNo}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    if (create_screens_ui_confirm_at(m_, self).field_418() != 0) {
        vcall_list("virtual_delete", create_screens_ui_confirm_at(m_, self).field_418(), 0, {1});
    }
    if (create_screens_ui_confirm_at(m_, self).field_424() != 0) {
        delete_object(create_screens_ui_confirm_at(m_, self).field_424());
    }
    box_destroy(self);
}

void CreateScreens::confirm_highlight(std::uint32_t self, std::uint32_t id) { menu_.list_highlight(word_at(m_, self + kList), id & 0xff); }

void CreateScreens::confirm_draw(std::uint32_t self) {
    vcall_list("list_v3", create_screens_ui_confirm_at(m_, self).field_418(), 3);
    box_draw(self);
    s_.note(kSvcSelectionBox, {0, 0, 0x200, global_word(m_, kScreenFillHeight), 0x20, 0x20, 0x20});
}

void CreateScreens::confirm_update(std::uint32_t self) { vcall_list("list_v4", word_at(m_, self + kList), 4); }

// 0x00426310: the list follows the pad edge (a sound for up and down); then the pad(s) named by `pad` (-1: both) are looked at in turn: confirm
// answers 1 on the first item and 2 on the other, back answers 2.
std::uint32_t CreateScreens::confirm_poll(std::uint32_t self, std::int32_t edge, std::int32_t pad) {
    if (edge == 2 || edge == 3) {
        sfx(0x11f);
    }
    vcall_list("list_v5", create_screens_ui_confirm_at(m_, self).field_418(), 5, {u(edge)});
    std::int32_t first = pad;
    std::int32_t last = pad;
    if (pad == -1) {
        first = 0;
        last = 1;
    }
    for (std::int32_t p = first; p <= last; ++p) {
        const std::uint32_t base = kPadBackEdge + u(mul(p, 0x16c));
        if (byte_at(m_, base + 0x30) != 0) {
            sfx(0xb4);
            set_byte_at(m_, kPadConfirmEdge + u(mul(p, 0x16c)), 0);
            return menu_.list_highlighted_id(create_screens_ui_confirm_at(m_, self).field_418()) == 0 ? 2 : 1;
        }
        if (byte_at(m_, base) != 0) {
            sfx(0xb0);
            set_byte_at(m_, base, 0);
            return 2;
        }
    }
    return 0;
}

}  // namespace opentony::runtime::frontend
