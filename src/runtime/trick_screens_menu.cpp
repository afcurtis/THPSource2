#include "auto_views.hpp"
#include "trick_screens.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgEmpty = 0x0055fe10;
constexpr std::uint32_t kMsgNoCombo = 0x005417d4;

// TrickMenu fields
constexpr std::uint32_t kFontA = 0xac;         // s2menu1: the panel's labels
constexpr std::uint32_t kFontB = 0x1f8;        // s2menu1: the header texts
constexpr std::uint32_t kFontC = 0x344;        // s2font3: the panel's counters
constexpr std::uint32_t kFontD = 0x490;        // s2font2: the prompt windows
constexpr std::uint32_t kFontE = 0x5dc;        // ltisymbol: the button glyphs
constexpr std::uint32_t kFontF = 0x728;        // s2font1: the dialogs
constexpr std::uint32_t kCategoryList = 0x874;
constexpr std::uint32_t kCategoryRows = 0x878;
constexpr std::uint32_t kComboList = 0x9f8;
constexpr std::uint32_t kComboRows = 0x9fc;
constexpr std::uint32_t kPageList = 0xb7c;
constexpr std::uint32_t kPageRows = 0xb80;
constexpr std::uint32_t kStyleLabels = 0xd00;   // s2font1, left, scale 1.0
constexpr std::uint32_t kStyleTitle = 0xd04;    // a copy with another highlight colour
constexpr std::uint32_t kStyleGlyphs = 0xd08;   // ltisymbol
constexpr std::uint32_t kStyleTricks = 0xd0c;   // s2skata2
constexpr std::uint32_t kStyleDim = 0xd10;      // a copy of it
constexpr std::uint32_t kStyleEmpty = 0xd14;    // a copy of it, darker
constexpr std::uint32_t kDialog = 0xd18;
constexpr std::uint32_t kWarning = 0xd1c;
constexpr std::uint32_t kArrowA = 0xd20;
constexpr std::uint32_t kArrowB = 0xd24;
constexpr std::uint32_t kPanel = 0xd28;
constexpr std::uint32_t kHeaderA = 0xd2c;
constexpr std::uint32_t kHeaderB = 0xd30;
constexpr std::uint32_t kPrompts = 0xd34;       // select, delete, accept, back
constexpr std::uint32_t kManager = 0xd44;
constexpr std::uint32_t kState = 0x2150;
constexpr std::uint32_t kBlock = 0x2154;

constexpr std::uint32_t kScratch = 0x7ff00400;

constexpr std::uint32_t kComboTable = 0x00540cb8;    // the ordinary combinations (12-byte records)
constexpr std::uint32_t kSpecialTable = 0x00540e30;  // the special combinations

}  // namespace

// ---- the trick menu: construction and destruction -----------------------------------------------------------------------------------------

// 0x004c0430: the menu window: six fonts, the trick manager, the character's binding block, the two arrows, the panel, the two header texts, the category
// list, the four prompt windows and the warning text box.
std::uint32_t TrickScreens::menu_construct(std::uint32_t self, std::int32_t x, std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    for (std::uint32_t font : {kFontA, kFontB, kFontC, kFontD, kFontE, kFontF}) {
        font_construct(self + font);
    }
    engine(0x004bb4f0, {self + kManager});
    trick_screens_menu_menu_at(m_, self).set_field_0(static_cast<Word>(kVtTrickMenu));
    if ((engine_value(0x00416940, {0}) & 0xff) != 0) {
        engine(0x004bbd70, {self + kManager, 0, 1});
    }
    if (sk_.current_character() == 0 && sk_.current_costume() == 2) {
        trick_screens_menu_menu_at(m_, self).set_field_2154(0x0056a690);
    } else {
        trick_screens_menu_menu_at(m_, self).set_field_2154(static_cast<Word>(character_record(sk_.current_character()) + 0xcc));
    }
    // the fonts: (object, file name, +4, +8, the colour call, +0xc, +0x10, +0x2c / +0x30)
    copy_font_named(self + kFontA, 0x00527960);
    set_word_at(m_, self + kFontA + 4, 0);
    set_word_at(m_, self + kFontA + 8, 3);
    f_.set_rgb(self + kFontA, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontA + 0x21, 0);
    set_word_at(m_, self + kFontA + 0xc, 0);
    set_word_at(m_, self + kFontA + 0x10, 0);
    set_word_at(m_, self + kFontA + 0x30, 0x1000);
    copy_font_named(self + kFontB, 0x00527960);
    set_word_at(m_, self + kFontB + 4, 1);
    set_word_at(m_, self + kFontB + 8, 3);
    f_.set_rgb(self + kFontB, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0);
    set_word_at(m_, self + kFontB + 0x10, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x1000);
    copy_font_named(self + kFontC, 0x0052796c);
    set_word_at(m_, self + kFontC + 4, 2);
    set_word_at(m_, self + kFontC + 8, 3);
    f_.set_rgb(self + kFontC, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontC + 0x21, 0);
    set_word_at(m_, self + kFontC + 0xc, 0);
    copy_font_named(self + kFontD, 0x0052544c);
    set_word_at(m_, self + kFontD + 4, 0);
    set_word_at(m_, self + kFontD + 8, 3);
    f_.set_rgb(self + kFontD, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontD + 0x21, 0);
    set_word_at(m_, self + kFontD + 0xc, 0);
    copy_font_named(self + kFontE, 0x00527940);
    set_word_at(m_, self + kFontE + 4, 1);
    set_word_at(m_, self + kFontE + 8, 3);
    f_.set_rgb(self + kFontE, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontE + 0x21, 0);
    set_word_at(m_, self + kFontE + 0xc, 6);
    set_word_at(m_, self + kFontE + 0x2c, 1);
    copy_font_named(self + kFontF, 0x00526acc);
    set_word_at(m_, self + kFontF + 4, 1);
    set_word_at(m_, self + kFontF + 8, 3);
    f_.set_rgb(self + kFontF, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontF + 0x21, 0);
    set_word_at(m_, self + kFontF + 0xc, 0);
    set_word_at(m_, self + kFontF + 0x10, 0);
    set_word_at(m_, self + kFontF + 0x30, 0x1000);
    // the arrows
    auto arrow = [&](std::uint32_t slot, std::uint32_t file, std::uint32_t mode) {
        const std::uint32_t block = operator_new(0x5c);
        std::uint32_t sprite = 0;
        if (block != 0) {
            s_.note(kSpriteConstruct, {block, file, 1, 0, 0}, m_.cstring(file));
            sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, sprite);
        set_byte_at(m_, sprite + 0x0a, mode);
        set_byte_at(m_, sprite + 0x0b, 1);
        s_.note(kSpriteInit, {sprite});
    };
    arrow(kArrowA, 0x00527c20, 2);
    arrow(kArrowB, 0x00527c10, 0);
    // the panel
    std::uint32_t memory = operator_new(0xdc);
    std::uint32_t panel = 0;
    if (memory != 0) {
        panel = panel_construct(memory, self + kFontA, self + kFontC, self + kFontE, 0x12d, 0xa1);
    }
    trick_screens_menu_menu_at(m_, self).set_field_d28(static_cast<Word>(panel));
    w_.window_add(self, panel);
    trick_screens_menu_menu_at(m_, self).set_field_d18(0);
    auto header = [&](std::uint32_t slot, std::uint32_t font, std::int32_t y_position) {
        const std::uint32_t block = operator_new(0x30);
        std::uint32_t text = 0;
        if (block != 0) {
            text = w_.text_construct(block, global_word(m_, 0x0052e300), font, 0x9a, y_position, -1, -1);
        }
        set_word_at(m_, self + slot, text);
        w_.window_add(self, text);
    };
    header(kHeaderA, self + kFontB, 0x3a);
    header(kHeaderB, self + kFontE, 0x44);
    build_category_list(self);
    const struct {
        std::uint32_t file;
        std::int32_t x;
    } prompts[] = {{0x005417c4, 100}, {0x00527c58, 200}, {0x0052540c, 400}, {0x00525400, 0x32}};
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint32_t window = prompt(prompts[i].file, 0x005252f0, self + kFontD, prompts[i].x, 0xdc);
        set_word_at(m_, self + kPrompts + 4 * i, window);
        w_.window_add(self, window);
        if (i == 1) {
            set_half_at(m_, window + 4, 0x80);
        }
    }
    memory = operator_new(0x418);
    std::uint32_t warning = 0;
    if (memory != 0) {
        warning = screens_.box_construct(memory, self + kFontF, global_word(m_, 0x0052e2f8), 0x100, 0x78);
    }
    trick_screens_menu_menu_at(m_, self).set_field_d1c(static_cast<Word>(warning));
    trick_screens_menu_menu_at(m_, self).set_field_2150(0);
    return self;
}

std::uint32_t TrickScreens::menu_delete(std::uint32_t self, std::uint32_t flag) {
    menu_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004c0eb0: the lists, the styles, the rows (one of each of the three kinds at a time), the dialog, the arrows and the warning box; then the trick manager
// and the fonts.
void TrickScreens::menu_destroy(std::uint32_t self) {
    trick_screens_menu_menu_at(m_, self).set_field_0(static_cast<Word>(kVtTrickMenu));
    for (std::uint32_t slot : {kCategoryList, kComboList, kPageList}) {
        if (word_at(m_, self + slot) != 0) {
            vcall_list("virtual_delete", word_at(m_, self + slot), 0, {1});
        }
    }
    for (std::uint32_t slot : {kStyleLabels, kStyleTitle, kStyleGlyphs, kStyleTricks, kStyleDim, kStyleEmpty}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        for (std::uint32_t rows : {kCategoryRows, kComboRows, kPageRows}) {
            if (word_at(m_, self + rows + 4 * i) != 0) {
                delete_object(word_at(m_, self + rows + 4 * i));
            }
        }
    }
    if (trick_screens_menu_menu_at(m_, self).field_d18() != 0) {
        screens_.dialog_call(trick_screens_menu_menu_at(m_, self).field_d18(), 0, {1});
    }
    for (std::uint32_t slot : {kArrowA, kArrowB}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    if (trick_screens_menu_menu_at(m_, self).field_d1c() != 0) {
        screens_.dialog_call(trick_screens_menu_menu_at(m_, self).field_d1c(), 0, {1});
    }
    engine(0x004bb790, {self + kManager});          // TrickManager_Destructor
    for (std::uint32_t font : {kFontF, kFontE, kFontD, kFontC, kFontB, kFontA}) {
        font_destroy(self + font);
    }
    w_.window_destroy(self);
}

// 0x004c1080: a hidden menu draws nothing. Otherwise: the two header texts start hidden and the four prompt windows sit at the bottom; each state
// shows what it needs, and the dialog states darken the screen.
void TrickScreens::menu_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (trick_screens_menu_menu_at(m_, self).field_14() != 0) {
        return;
    }
    const std::uint32_t header_a = trick_screens_menu_menu_at(m_, self).field_d2c();
    const std::uint32_t header_b = trick_screens_menu_menu_at(m_, self).field_d30();
    set_word_at(m_, header_a + 0x14, 1);
    set_word_at(m_, header_b + 0x14, 1);
    for (std::uint32_t i = 0; i < 4; ++i) {
        set_word_at(m_, word_at(m_, self + kPrompts + 4 * i) + 0xc, global_word(m_, kScreenFillHeight) + 0x14);
    }
    auto prompts_at_bottom = [&](std::initializer_list<std::uint32_t> which) {
        for (std::uint32_t i : which) {
            set_word_at(m_, word_at(m_, self + kPrompts + 4 * i) + 0xc, 0xdc);
        }
    };
    switch (trick_screens_menu_menu_at(m_, self).field_2150()) {
    case 0:
        prompts_at_bottom({0, 2, 3});
        break;
    case 1:
        w_.text_set_string(header_a, global_word(m_, 0x0052e300));
        prompts_at_bottom({0, 1, 2, 3});
        set_word_at(m_, header_a + 0x14, 0);
        s_.note(kSvcFrameBox, {0x2a, 0x3e, 0xe0, 1, 0x40, 0x40, 0x80});
        break;
    case 2: {
        w_.text_set_string(header_a, global_word(m_, 0x0052e304));
        prompts_at_bottom({0, 2, 3});
        set_word_at(m_, header_a + 0x14, 0);
        const std::uint32_t id = menu_.list_highlighted_id(trick_screens_menu_menu_at(m_, self).field_9f8());
        check(id != 0xffffffffu, kMsgNoCombo);
        const std::uint32_t mask = category_mask(static_cast<std::int32_t>(menu_.list_highlighted_id(trick_screens_menu_menu_at(m_, self).field_874())));
        const std::uint32_t table = (mask & 0x8000) != 0 ? kSpecialTable : kComboTable;
        button_glyphs(table + id * 12, scratch());
        w_.text_set_string(header_b, kScratch);
        set_word_at(m_, header_b + 0x14, 0);
        s_.note(kSvcFrameBox, {0x2a, 0x48, 0xe0, 1, 0x40, 0x40, 0x80});
        break;
    }
    case 3:
        prompts_at_bottom({0, 2, 3});
        s_.note(kSvcSelectionBox, {0, 0, 0x200, global_word(m_, kScreenFillHeight), 0x20, 0x20, 0x20});
        break;
    case 4:
        prompts_at_bottom({2});
        s_.note(kSvcSelectionBox, {0, 0, 0x200, global_word(m_, kScreenFillHeight), 0x20, 0x20, 0x20});
        screens_.dialog_call(trick_screens_menu_menu_at(m_, self).field_d1c(), 1);
        break;
    default:
        break;
    }
    w_.window_draw(self, u(static_cast<std::int32_t>(trick_screens_menu_menu_at(m_, self).field_20()) + x), u(static_cast<std::int32_t>(trick_screens_menu_menu_at(m_, self).field_24()) + y));
}

// 0x004c13c0: a hidden menu does not update.
void TrickScreens::menu_update_frame(std::uint32_t self) {
    if (trick_screens_menu_menu_at(m_, self).field_14() == 0) {
        w_.window_update(self);
    }
}

// 0x004c13d0: the category list (a scroll list with a title and the four pages and "Continue") and the six styles the menu's rows use.
void TrickScreens::build_category_list(std::uint32_t self) {
    std::uint32_t memory = operator_new(0x350);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, 0x2a, 0x4d, 0xef, 0x93, 1, 3, 3, 1);
    }
    set_word_at(m_, self + kCategoryList, list);
    set_word_at(m_, self + kComboList, 0);
    set_word_at(m_, self + kPageList, 0);
    auto new_properties = [&]() {
        const std::uint32_t block = operator_new(0x17c);
        return block != 0 ? menu_.properties_construct(block) : 0u;
    };
    auto copy_words = [&](std::uint32_t dest, std::uint32_t source) {
        for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
            set_word_at(m_, dest + offset, word_at(m_, source + offset));
        }
    };
    // 0xd00: s2font1, left-aligned
    std::uint32_t style = new_properties();
    set_word_at(m_, self + kStyleLabels, style);
    menu_.copy_font_fields(style + 4, find_font(0x00526acc));
    set_word_at(m_, style + 8, 0);
    set_word_at(m_, style + 0xc, 3);
    f_.set_rgb(style + 4, 0x80, 0x80, 0x80);
    set_byte_at(m_, style + 0x25, 0);
    set_word_at(m_, style + 0x10, 0);
    set_word_at(m_, style + 0x14, 0);
    set_word_at(m_, style + 0x34, 0x1000);
    set_word_at(m_, style + 0x150, 0x1324);
    set_word_at(m_, style + 0x154, 0x1000);
    // 0xd04: a copy of it with the highlight colour changed
    std::uint32_t source = style;
    style = new_properties();
    set_word_at(m_, self + kStyleTitle, style);
    menu_.copy_font_fields(style + 4, source + 4);
    copy_words(style, source);
    set_word_at(m_, style + 0x158, 0x91);
    set_word_at(m_, style + 0x15c, 0x12);
    set_word_at(m_, style + 0x160, 0xf);
    // 0xd08: the button symbols
    style = new_properties();
    set_word_at(m_, self + kStyleGlyphs, style);
    menu_.copy_font_fields(style + 4, find_font(0x00527940));
    set_word_at(m_, style + 8, 0);
    set_word_at(m_, style + 0xc, 3);
    f_.set_rgb(style + 4, 0xff, 0x60, 0x20);
    set_byte_at(m_, style + 0x25, 0);
    set_word_at(m_, style + 0x10, 6);
    set_word_at(m_, style + 0x14, 0);
    set_word_at(m_, style + 0x34, 0x1000);
    set_word_at(m_, style + 0x30, 1);
    set_word_at(m_, style + 0x150, 0x1324);
    set_word_at(m_, style + 0x154, 0x1000);
    // 0xd0c: the trick names (s2skata2)
    style = new_properties();
    set_word_at(m_, self + kStyleTricks, style);
    menu_.copy_font_fields(style + 4, find_font(0x00527950));
    set_word_at(m_, style + 8, 1);
    set_word_at(m_, style + 0xc, 3);
    f_.set_rgb(style + 4, 0xff, 0x60, 0x20);
    set_byte_at(m_, style + 0x25, 0);
    set_word_at(m_, style + 0x10, 0);
    set_word_at(m_, style + 0x14, 0);
    set_word_at(m_, style + 0x158, 0xc0);
    set_word_at(m_, style + 0x164, 0x80);
    set_word_at(m_, style + 0x15c, 0xc0);
    set_word_at(m_, style + 0x168, 0x80);
    set_word_at(m_, style + 0x160, 0xc0);
    set_word_at(m_, style + 0x16c, 0x80);
    set_word_at(m_, style + 0x34, 0x1000);
    set_word_at(m_, style + 0x150, 0x1324);
    set_word_at(m_, style + 0x154, 0x1000);
    // 0xd10 and 0xd14: copies of it (the second one darker)
    source = style;
    style = new_properties();
    set_word_at(m_, self + kStyleDim, style);
    menu_.copy_font_fields(style + 4, source + 4);
    copy_words(style, source);
    set_word_at(m_, style + 8, 1);
    set_word_at(m_, style + 0x158, 0xc0);
    set_word_at(m_, style + 0x164, 0x80);
    set_word_at(m_, style + 0x15c, 0xc0);
    set_word_at(m_, style + 0x168, 0x80);
    set_word_at(m_, style + 0x160, 0xc0);
    set_word_at(m_, style + 0x16c, 0x80);
    set_word_at(m_, style + 0x150, 0x1324);
    set_word_at(m_, style + 0x154, 0x1000);
    style = new_properties();
    set_word_at(m_, self + kStyleEmpty, style);
    menu_.copy_font_fields(style + 4, source + 4);
    copy_words(style, source);
    set_word_at(m_, style + 0x158, 0x60);
    set_word_at(m_, style + 0x164, 0x40);
    set_word_at(m_, style + 0x15c, 0x40);
    set_word_at(m_, style + 0x168, 0x40);
    set_word_at(m_, style + 0x160, 0x10);
    set_word_at(m_, style + 0x16c, 0x40);
    set_word_at(m_, style + 0x150, 0x1324);
    set_word_at(m_, style + 0x154, 0x1000);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kCategoryRows + 4 * i, 0);
        set_word_at(m_, self + kComboRows + 4 * i, 0);
        set_word_at(m_, self + kPageRows + 4 * i, 0);
    }
    // the rows: a scaled title, then the pages and "Continue" (ids 6, 7, 9, 10 and 11)
    memory = operator_new(0x58);
    std::uint32_t title = 0;
    if (memory != 0) {
        title = menu_.text_scaled_construct(memory, 5, global_word(m_, 0x0052e2fc), word_at(m_, self + kStyleTitle), 1);
    }
    set_word_at(m_, self + kCategoryRows, title);
    const struct {
        std::uint32_t id;
        std::uint32_t text;
    } rows[] = {{6, 0x0052e7dc}, {7, 0x0052e7e0}, {9, 0x0052e7e8}, {10, 0x0052e7ec}, {0xb, 0x0052e968}};
    for (std::uint32_t i = 0; i < 5; ++i) {
        memory = operator_new(0x54);
        std::uint32_t row = 0;
        if (memory != 0) {
            row = menu_.text_construct(memory, rows[i].id, word_at(m_, rows[i].text), word_at(m_, self + kStyleLabels));
        }
        set_word_at(m_, self + kCategoryRows + 4 * (1 + i), row);
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kCategoryRows + 4 * i) != 0) {
            vcall_list("list_v6", word_at(m_, self + kCategoryList), 6, {word_at(m_, self + kCategoryRows + 4 * i)});
        }
    }
    vcall_list("list_v1", word_at(m_, self + kCategoryList), 1, {word_at(m_, self + kArrowA), word_at(m_, self + kArrowB), 5});
    vcall_list("list_v2", word_at(m_, self + kCategoryList), 2);
}

}  // namespace opentony::runtime::frontend
