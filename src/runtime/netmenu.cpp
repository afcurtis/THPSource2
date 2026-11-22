#include "auto_views.hpp"
#include "netmenu.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

using namespace netmenu;

namespace {


constexpr std::uint32_t kEmpty = 0x005252f0;               // ""
constexpr std::uint32_t kMainFont = 0x00525458;            // "mainf.fnt"
constexpr std::uint32_t kNetState = 0x0056ad10;            // the argument NetMenu_Main was called with
constexpr std::uint32_t kDone = 0x0056ad20;                // byte: the screen ends
constexpr std::uint32_t kBack = 0x0056ad19;                // byte: back out of the screen
constexpr std::uint32_t kResult = 0x0056ad1c;
constexpr std::uint32_t kFrontFlag = 0x0056114c;
constexpr std::uint32_t kLastTick = 0x00568624;
constexpr std::uint32_t kNetActive = 0x006a3b21;
constexpr std::uint32_t kNetHost = 0x006a3b22;
constexpr std::uint32_t kNetLost = 0x006a3b30;
constexpr std::uint32_t kNetPlayers = 0x006a18b0;
constexpr std::uint32_t kNetFlag = 0x006a3c11;
constexpr std::uint32_t kGameMode = 0x00533f38;
constexpr std::uint32_t kPre = 0x0056b834;

// the five tables of item descriptions the lists are built from (24 bytes each: +0 kind, +8 the text)
struct Table {
    std::uint32_t kind_at;           // address of the first entry's kind
    std::uint32_t count;
    std::uint32_t list;              // the window's list field
    std::uint32_t items;             // the window's first item field
    std::uint32_t x, y, width, height;
    std::uint32_t texture_table;     // the table FUN_0047e210 reads (0 for none)
};

}  // namespace

// ---- construction helpers ---------------------------------------------------------------------------------------------------------------------------------------

void NetMenu::new_font(std::uint32_t font_struct) { font_construct(font_struct); }

// the first font is loaded (Font_Load returns the font), the others found
void NetMenu::font_from_load(std::uint32_t font_struct) {
    s_.note(kSvcFontLoad, {kMainFont}, m_.cstring(kMainFont, 0x40));
    menu_.copy_font_fields(font_struct, static_cast<std::uint32_t>(s_.value(kSvcFontLoad)));
}

void NetMenu::font_from_find(std::uint32_t font_struct) { copy_font_named(font_struct, kMainFont); }

// a menu style (0x17c bytes) copied from a font, then edited by the caller
std::uint32_t NetMenu::make_style(std::uint32_t source_font) {
    const std::uint32_t memory = operator_new(0x17c);
    const std::uint32_t style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    menu_.copy_font_fields(style + 4, source_font);
    return style;
}

std::uint32_t NetMenu::make_item(std::uint32_t id, std::uint32_t text, std::uint32_t style, bool hidden) {
    const std::uint32_t memory = operator_new(0x54);
    const std::uint32_t item = memory != 0 ? menu_.text_construct(memory, id, text, style) : 0u;
    if (hidden) {
        set_byte_at(m_, item + 0xc, 0);
    }
    return item;
}

std::uint32_t NetMenu::make_edit(std::uint32_t id, std::uint32_t text, std::uint32_t style) {
    const std::uint32_t memory = operator_new(0x134);
    return memory != 0 ? engine_value(0x004dc100, {memory, id, text, style, 2}) : 0u;
}

std::uint32_t NetMenu::make_list(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, std::uint32_t centre) {
    const std::uint32_t memory = operator_new(0x1bc);
    return memory != 0 ? menu_.list_construct(memory, x, y, width, height, centre, 0, 3, 0) : 0u;
}

// ---- the window --------------------------------------------------------------------------------------------------------------------------------------------------

// 0x0047ce60: the lobby window: the fonts, the picture, the two side panels, the arrows, three styles, the prompts, and the lists the state machine switches between
std::uint32_t NetMenu::construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    engine(0x00454930, {self + kPie});
    new_font(self + kFontA);
    new_font(self + kFontB);
    new_font(self + kFontC);
    new_font(self + kFontD);
    set_word_at(m_, self, kVtNetMenu);
    set_word_at(m_, self + kBackground, background_sprite(0x00535a48));
    set_word_at(m_, self + kState, 0);
    set_global_byte(m_, kDone, 0);
    set_global_byte(m_, kBack, 0);
    set_byte_at(m_, self + kStatus, 0);
    auto side = [&](std::uint32_t slot, std::uint32_t file, std::int32_t x, std::int32_t start_x) {
        const std::uint32_t memory = operator_new(0x2c);
        const std::uint32_t element = memory != 0 ? w_.sprite_construct(memory, file, x, 100, start_x, 100, 0, 0) : 0u;
        set_word_at(m_, self + slot, element);
        w_.window_add(self, element);
    };
    side(kWindowLeft, 0x00535a34, 0x50, -500);
    side(kWindowRight, 0x00535a20, 0x14f, -245);
    enable_arrows(self);
    auto arrow = [&](std::uint32_t slot, std::uint32_t file, std::uint32_t a) {
        const std::uint32_t memory = operator_new(0x5c);
        std::uint32_t sprite = 0;
        if (memory != 0) {
            s_.note(kSpriteConstruct, {memory, file, 1, 0, 0}, m_.cstring(file));
            sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, sprite);
        set_byte_at(m_, sprite + 10, a);
        set_byte_at(m_, sprite + 11, 1);
    };
    arrow(kArrowA, 0x00525908, 2);
    arrow(kArrowB, 0x005258f8, 0);
    // the fonts: A is loaded, the others found; each is edited for its use
    font_from_load(self + kFontA);
    set_word_at(m_, self + kFontA + 4, 1);
    set_word_at(m_, self + kFontA + 8, 0);
    set_word_at(m_, self + kFontA + 0x30, 0x800);
    set_word_at(m_, self + kFontA + 0x14, 0x40);
    set_word_at(m_, self + kFontA + 0x18, 0x40);
    set_word_at(m_, self + kFontA + 0x1c, 0xff);
    set_byte_at(m_, self + 0x138, 0);
    font_from_find(self + kFontB);
    set_word_at(m_, self + kFontB + 4, 0);
    set_word_at(m_, self + kFontB + 8, 1);
    f_.set_rgb(self + kFontB, 0xff, 0x4d, 0x28);
    set_byte_at(m_, self + kFontB + 0x21, 1);
    set_word_at(m_, self + kFontB + 0xc, 0);
    set_word_at(m_, self + kFontB + 0x10, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x6a4);
    font_from_find(self + kFontC);
    set_word_at(m_, self + kFontC + 4, 0);
    set_word_at(m_, self + kFontC + 8, 1);
    f_.set_rgb(self + kFontC, 0xff, 99, 0x28);
    set_byte_at(m_, self + kFontC + 0x21, 0);
    set_word_at(m_, self + kFontC + 0xc, 0);
    set_word_at(m_, self + kFontC + 0x10, 0);
    set_word_at(m_, self + kFontC + 0x30, 0x800);
    font_from_find(self + kFontD);
    set_word_at(m_, self + kFontD + 4, 0);
    set_word_at(m_, self + kFontD + 8, 1);
    f_.set_rgb(self + kFontD, 200, 200, 200);
    set_byte_at(m_, self + kFontD + 0x21, 0);
    set_word_at(m_, self + kFontD + 0xc, 0);
    set_word_at(m_, self + kFontD + 0x10, 0);
    set_word_at(m_, self + kFontD + 0x30, 0x800);
    // the three styles
    const std::uint32_t a = make_style(self + kFontA);
    set_word_at(m_, self + kStyleA, a);
    set_word_at(m_, a + 8, 0);
    set_word_at(m_, a + 0xc, 0);
    set_word_at(m_, a + 0x18, 0xff);
    set_word_at(m_, a + 0x1c, 0x60);
    set_word_at(m_, a + 0x20, 0x20);
    set_byte_at(m_, a + 0x25, 0);
    set_word_at(m_, a + 0x10, 0);
    set_word_at(m_, a + 0x164, 0x20);
    set_word_at(m_, a + 0x168, 0x60);
    set_word_at(m_, a + 0x16c, 0xff);
    set_word_at(m_, a + 0x158, 0xff);
    set_word_at(m_, a + 0x15c, 99);
    set_word_at(m_, a + 0x160, 0x28);
    set_word_at(m_, a + 0x154, 0x800);
    set_word_at(m_, a + 0x150, 0x960);
    const std::uint32_t b = make_style(self + kFontA);
    set_word_at(m_, self + kStyleB, b);
    set_word_at(m_, b + 8, 0);
    set_word_at(m_, b + 0xc, 0);
    set_word_at(m_, b + 0x170, 0xff);
    set_word_at(m_, b + 0x174, 0xff);
    set_word_at(m_, b + 0x178, 0xff);
    set_word_at(m_, b + 0x18, 0xff);
    set_word_at(m_, b + 0x1c, 0x60);
    set_word_at(m_, b + 0x20, 0x20);
    set_byte_at(m_, b + 0x25, 0);
    set_word_at(m_, b + 0x10, 0);
    set_word_at(m_, b + 0x164, 0x20);
    set_word_at(m_, b + 0x168, 0x60);
    set_word_at(m_, b + 0x16c, 0xff);
    set_word_at(m_, b + 0x158, 0xff);
    set_word_at(m_, b + 0x15c, 0x60);
    set_word_at(m_, b + 0x160, 0x20);
    set_word_at(m_, b + 0x150, 3000);
    set_word_at(m_, b + 0x154, 3000);
    const std::uint32_t c = make_style(self + kFontA);
    set_word_at(m_, self + kStyleC, c);
    set_word_at(m_, c + 8, 0);
    set_word_at(m_, c + 0xc, 0);
    set_word_at(m_, c + 0x18, 0xff);
    set_word_at(m_, c + 0x1c, 0x60);
    set_word_at(m_, c + 0x20, 0x20);
    set_byte_at(m_, c + 0x25, 0);
    set_word_at(m_, c + 0x10, 0);
    set_word_at(m_, c + 0x164, 0x20);
    set_word_at(m_, c + 0x168, 0x60);
    set_word_at(m_, c + 0x16c, 0xff);
    set_word_at(m_, c + 0x158, 0xff);
    set_word_at(m_, c + 0x15c, 0x60);
    set_word_at(m_, c + 0x160, 0x20);
    set_word_at(m_, c + 0x154, 0x800);
    set_word_at(m_, c + 0x150, 0x960);
    // the prompts: back, main menu, accept
    w_.window_add(self, prompt(0x00525400, kEmpty, self + kFontB, 0x32, 0xdc));
    const std::uint32_t menu_prompt = prompt(0x00535a10, kEmpty, self + kFontB, 0xcf, 0xdc);
    set_word_at(m_, self + kMainMenuPrompt, menu_prompt);
    set_half_at(m_, menu_prompt + 4, 0);
    w_.window_add(self, menu_prompt);
    w_.window_add(self, prompt(0x0052540c, kEmpty, self + kFontB, 400, 0xdc));
    // the lists: each is built from a table of 24-byte descriptions (kind at +0, text at +8): kind 0 a hidden label, 2 a shown label, 5 an edit box, the others hidden blanks
    auto build = [&](std::uint32_t list_slot, std::uint32_t item_slot, std::uint32_t kind_at, std::uint32_t count, std::int32_t x, std::int32_t y, std::int32_t width,
                     std::int32_t height, std::uint32_t shown_style, std::uint32_t label_style, bool edits) {
        const std::uint32_t list = make_list(x, y, width, height);
        set_word_at(m_, self + list_slot, list);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t kind = word_at(m_, kind_at + 24 * i);
            const std::uint32_t text = word_at(m_, kind_at + 8 + 24 * i);
            std::uint32_t item;
            if (kind == 0) {
                item = make_item(i, text, word_at(m_, self + label_style), true);
            } else if (kind == 2) {
                item = make_item(i, text, word_at(m_, self + shown_style), false);
            } else if (edits && kind == 5) {
                item = make_edit(i, text, word_at(m_, self + kStyleA));
            } else {
                item = make_item(i, kEmpty, word_at(m_, self + kStyleA), true);
            }
            set_word_at(m_, self + item_slot + 4 * i, item);
            add_to_list(list, item);
        }
        vcall_list("list_v2", list, 2);
    };
    // the main list: kind 2 uses the first style, labels the second
    build(kMainList, kMainItems, 0x0056ad30, 11, 0, 0x2e, 0x200, 0x50, kStyleA, kStyleB, false);
    // the form list: kind 2 uses the third style
    build(kFormList, kFormItems, 0x0056ae38, 9, 0, 0x2e, 0x200, 0x50, kStyleC, kStyleB, true);
    engine(0x004dc370, {word_at(m_, self + kFormItems + 4), 0x006a3b71, 0x10, 0xb4});
    engine(0x004dc370, {word_at(m_, self + kFormItems + 8), 0x006a3bb1, 0x10, 0xb4});
    set_byte_at(m_, word_at(m_, self + kFormItems + 8) + 0x130, 1);
    set_item_textures(self + kFormItems, 0x0056ae30, 9);
    build(kSettingsList, kSettingsItems, 0x00535908, 3, 0, 0x8c, 0x200, 0x50, kStyleA, kStyleB, true);
    set_item_textures(self + kSettingsItems, 0x00535900, 3);
    engine(0x004dc370, {word_at(m_, self + kSettingsItems), self + kTyped, 0x1c, 0x3c});
    // the chat lines start empty
    for (std::uint32_t i = 0; i < 7; ++i) {
        write_string(self + kChatLines + 0x38 * i, m_.cstring(kEmpty, 0x10));
    }
    if (global_byte(m_, kNetHost) == 0 && (engine_value(0x004de040) & 0xff) == 0) {
        item_text(word_at(m_, self + kSettingsItems + 4), kEmpty);
        show_item(word_at(m_, self + kSettingsItems + 4), false);
    }
    show_item(word_at(m_, self + kSettingsItems + 4), false);
    show_item(word_at(m_, self + kSettingsItems + 8), false);
    build(kGameList, kGameItems, 0x00535968, 3, 0, 0x2e, 0x200, 0x50, kStyleC, kStyleB, true);
    set_item_textures(self + kGameItems, 0x00535960, 3);
    engine(0x004dc370, {word_at(m_, self + kGameItems), 0x006a3b31, 0x10, 0xb4});
    set_byte_at(m_, word_at(m_, self + kGameItems) + 0x131, 1);
    engine(0x004dc370, {word_at(m_, self + kGameItems + 4), 0x006a3bb1, 0x10, 0xb4});
    set_byte_at(m_, word_at(m_, self + kGameItems + 4) + 0x130, 1);
    // the found-games list: four blank rows
    const std::uint32_t found = make_list(0x60, 0x74, 0x1f0, 100, 0);
    set_word_at(m_, self + kFoundList, found);
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint32_t item = make_item(i, kEmpty, word_at(m_, self + kStyleA), true);
        set_word_at(m_, self + kFoundItems + 4 * i, item);
        add_to_list(found, item);
    }
    refresh_games(self);
    vcall_list("list_v2", found, 2);
    engine(0x004e02d0);
    if (global_word(m_, kNetState) == 0 || global_byte(m_, kNetLost) != 0) {
        set_word_at(m_, self + kState, 1);
        set_global_word(m_, kGameMode, 5);
    } else if (global_byte(m_, kNetActive) == 0 || static_cast<std::int32_t>(global_word(m_, kNetPlayers)) < 1) {
        set_word_at(m_, self + kState, (global_byte(m_, kNetHost) != 0 ? 5u : 0u) + 1);
    } else {
        set_word_at(m_, self + kState, 0xc);
    }
    set_global_byte(m_, kNetFlag, 1);
    set_word_at(m_, self + kTick, global_word(m_, kGlobalSimTick) + 2);
    return self;
}

}  // namespace opentony::runtime::frontend
