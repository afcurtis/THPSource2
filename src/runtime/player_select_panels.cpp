#include "auto_views.hpp"
#include "player_select.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// Assertion messages
constexpr std::uint32_t kMsgTooManyLines = 0x00526a50;
constexpr std::uint32_t kMsgNoDots = 0x00537290;           // "Could not delete mpDots"
constexpr std::uint32_t kMsgInvalidListCount = 0x005372a8;
constexpr std::uint32_t kMsgNoItem = 0x005372bc;

// BioWindow fields
constexpr std::uint32_t kBioExtent = 0xac;      // the text's height beyond the frame (the scroll range)
constexpr std::uint32_t kBioLines = 0xb0;       // sixteen text elements
constexpr std::uint32_t kBioCount = 0xf0;
constexpr std::uint32_t kBioFont = 0xf4;        // s2bio
constexpr std::uint32_t kBioCharacter = 0x240;  // the character the lines were built for

constexpr std::uint32_t kSoftRenderer = 0x029da3a0;   // D3DSoftwareRenderer: the fills are clipped in the other order
constexpr std::uint32_t kFillHeight = 0x00527d54;
constexpr std::uint32_t kPadCurrentGlobal = 0x0056a888;
constexpr std::uint32_t kPanelPadStride = 0x16c;
constexpr std::uint32_t kPadScrollUp = 0x0056b0a8;
constexpr std::uint32_t kPadScrollDown = 0x0056b098;
constexpr std::uint32_t kPadUpEdge = 0x0056b099;
constexpr std::uint32_t kPadUpEdgeB = 0x0056b0a9;
constexpr std::uint32_t kPanelConfirm = 0x0056b029;

// PageDots / PlayerHeader / PlayerMenu fields
constexpr std::uint32_t kDotsSprite = 0xac;
constexpr std::uint32_t kHeaderFont = 0xac;
constexpr std::uint32_t kMenuList = 0xac;
constexpr std::uint32_t kMenuRows = 0xb0;       // 0x60 row pointers
constexpr std::uint32_t kMenuProps = 0x230;
constexpr std::uint32_t kMenuChoice = 0x234;
constexpr std::uint32_t kMenuArrowA = 0x238;
constexpr std::uint32_t kMenuArrowB = 0x23c;

constexpr std::uint32_t kGlobalReplayState = 0x00561c74;
constexpr std::uint32_t kTextPlayer = 0x0052e624;       // pointer variable: "Player"
constexpr std::uint32_t kTextChoose = 0x0052e620;       // "Choose skater"
constexpr std::uint32_t kTextCustomAppearance = 0x0052e600;
constexpr std::uint32_t kTextCostume = 0x0052e614;
constexpr std::uint32_t kMenuLabels[] = {0x0052e5fc, 0x0052e600, 0x0052e604, 0x0052e608, 0x0052e60c, 0x0052e610};   // Continue, Appearance, Buy stats, Buy tricks, Edit tricks, Skate shop

}  // namespace

// ---- the bio window ------------------------------------------------------------------------------------------------------------------------

// 0x004a3830: the scroll bar, the frame, sixteen empty line slots and the bio font (white, mapping 2).
std::uint32_t PlayerSelect::bio_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    font_construct(self + kBioFont);
    player_select_panels_bio_at(m_, self).set_field_0(static_cast<Word>(kVtBioWindow));
    player_select_panels_bio_at(m_, self).set_field_240(0xffffffff);
    auto sprite = [&](std::uint32_t file, std::int32_t at_x, std::int32_t at_y) {
        const std::uint32_t memory = operator_new(0x2c);
        const std::uint32_t element = memory != 0 ? w_.sprite_construct(memory, file, at_x, at_y, -1, -1, 0, 0) : 0u;
        w_.window_add(self, element);
    };
    sprite(0x005371e0, 2, 0x10);
    sprite(0x005371d4, 0, 0);
    player_select_panels_bio_at(m_, self).set_field_ac(0);
    player_select_panels_bio_at(m_, self).set_field_f0(0);
    for (std::uint32_t i = 0; i < 16; ++i) {
        set_word_at(m_, self + kBioLines + 4 * i, 0);
    }
    copy_font_named(self + kBioFont, 0x005371c8);
    set_word_at(m_, self + kBioFont + 4, 0);
    set_word_at(m_, self + kBioFont + 8, 3);
    f_.set_rgb(self + kBioFont, 0xff, 0xff, 0xff);
    set_byte_at(m_, self + kBioFont + 0x21, 0);
    set_word_at(m_, self + kBioFont + 0xc, 1);
    set_word_at(m_, self + kBioFont + 0x10, 0);
    set_word_at(m_, self + kBioFont + 0x30, 0x800);
    f_.set_mapping(self + kBioFont, 2);
    return self;
}

std::uint32_t PlayerSelect::bio_delete(std::uint32_t self, std::uint32_t flag) {
    bio_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void PlayerSelect::bio_destroy(std::uint32_t self) {
    player_select_panels_bio_at(m_, self).set_field_0(static_cast<Word>(kVtBioWindow));
    for (std::uint32_t i = 0; i < 16; ++i) {
        if (word_at(m_, self + kBioLines + 4 * i) != 0) {
            delete_object(word_at(m_, self + kBioLines + 4 * i));
        }
        set_word_at(m_, self + kBioLines + 4 * i, 0);
    }
    font_destroy(self + kBioFont);
    w_.window_destroy(self);
}

// 0x004a3af0: the lines are drawn with the frame's rectangle filled around them (the software renderer fills it first, the others after).
void PlayerSelect::bio_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (word_at(m_, self + kElemHidden) != 0) {
        return;
    }
    auto frame = [&]() {
        s_.note(kSvcFillRect, {u(int_at(m_, self + kElemX) + 0x17), u(int_at(m_, self + kElemY) + 0xa), 0xa5, 0x43, 0});
    };
    auto screen = [&]() { s_.note(kSvcFillRect, {0, 0, 0x200, global_word(m_, kFillHeight), 0}); };
    const bool software = global_word(m_, kSoftRenderer) != 0;
    if (software) {
        frame();
    } else {
        screen();
    }
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(player_select_panels_bio_at(m_, self).field_f0()); ++i) {
        vcall_item(kElementDraw, word_at(m_, self + kBioLines + 4 * static_cast<std::uint32_t>(i)), 2,
                   {u(int_at(m_, self + kElemX) + x), u(int_at(m_, self + kElemY) + y)});
    }
    if (software) {
        screen();
    } else {
        frame();
    }
    w_.window_draw(self, x, y);
}

// 0x004a3bd0: scroll with the pad; when the character changed the lines are rebuilt from the character's bio (one text element per non-empty line,
// ten pixels apart) and the scroll bar is placed from the position of the first line.
void PlayerSelect::bio_update(std::uint32_t self) {
    const std::uint32_t pad = global_word(m_, kPadCurrentGlobal) * kPanelPadStride;
    if (byte_at(m_, pad + kPadScrollUp) != 0) {
        bio_scroll_up(self);
    } else if (byte_at(m_, pad + kPadScrollDown) != 0) {
        bio_scroll_down(self);
    }
    if (static_cast<std::int32_t>(player_select_panels_bio_at(m_, self).field_240()) != sk_.current_character()) {
        for (std::uint32_t i = 0; i < 16; ++i) {
            if (word_at(m_, self + kBioLines + 4 * i) != 0) {
                delete_object(word_at(m_, self + kBioLines + 4 * i));
            }
            set_word_at(m_, self + kBioLines + 4 * i, 0);
        }
        std::uint32_t entry = word_at(m_, skater::row(sk_.current_character()) + 0x154);
        std::int32_t y = 0x14;
        player_select_panels_bio_at(m_, self).set_field_f0(0);
        while (word_at(m_, entry) != 0) {
            const std::uint32_t text = word_at(m_, entry);
            if (byte_at(m_, text) != 0) {
                check(static_cast<std::int32_t>(player_select_panels_bio_at(m_, self).field_f0()) < 16, kMsgTooManyLines);
                const std::uint32_t memory = operator_new(0x30);
                std::uint32_t element = 0;
                if (memory != 0) {
                    element = w_.text_construct(memory, text, self + kBioFont, 0x17, y, -1, -1);
                }
                set_word_at(m_, self + kBioLines + 4 * player_select_panels_bio_at(m_, self).field_f0(), element);
                player_select_panels_bio_at(m_, self).set_field_f0(static_cast<Word>(player_select_panels_bio_at(m_, self).field_f0() + 1));
            }
            y += 10;
            entry += 4;
        }
        const std::int32_t extent = y - 0x57;
        player_select_panels_bio_at(m_, self).set_field_ac(static_cast<Word>(u(extent)));
        set_word_at(m_, word_at(m_, self + kWinChildren) + kElemHidden, extent > 0 ? 0 : 1);
        player_select_panels_bio_at(m_, self).set_field_240(static_cast<Word>(u(sk_.current_character())));
    }
    const std::int32_t first_y = int_at(m_, player_select_panels_bio_at(m_, self).field_b0() + kElemTargetY);
    const std::int32_t offset = 0x14 - first_y;
    const std::int32_t scaled = (offset * 0x31) / (static_cast<std::int32_t>(player_select_panels_bio_at(m_, self).field_ac()) + 5);
    set_word_at(m_, word_at(m_, self + kWinChildren) + kElemTargetY, u(scaled + 0x10));
    s_.note(kElementHook, {self});
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(player_select_panels_bio_at(m_, self).field_f0()); ++i) {
        vcall_item(kElementUpdate, word_at(m_, self + kBioLines + 4 * static_cast<std::uint32_t>(i)), 1);
    }
    w_.window_update(self);
}

// 0x004dcf00 (thiscall, no arguments; no function of its own in the symbol table): the pointer inside the scroll bar's rectangle (x 0x43, y 0x8a, 0x10 by 0x96) marks the
// mouse as used and, with its button down, presses up (0x1000) above the thumb and down (0x4000) below it. The rectangle is tested again after the press, the answer unused.
void PlayerSelect::bio_mouse_hook(std::uint32_t self) {
    constexpr std::uint32_t kMouseY = 0x006a13f4;
    constexpr std::uint32_t kMouseButton = 0x006a13f8;
    constexpr std::uint32_t kMouseUsed = 0x006a13ff;
    constexpr std::uint32_t kHitRect = seam::net::NetMouse_HitRect;   // (x, y, width, height)
    constexpr std::uint32_t kPressButtons = 0x004e4190;   // PCInput_SetPendingButtons(mask)
    std::int32_t thumb = static_cast<std::int32_t>(word_at(m_, player_select_panels_bio_at(m_, self).field_28() + 0xc)) + 0x82;
    if ((engine_value(kHitRect, {0x43, 0x8a, 0x10, 0x96}) & 0xff) != 0) {
        set_global_byte(m_, kMouseUsed, 1);
        if (global_word(m_, kMouseButton) != 0) {
            const auto y = global_int(m_, kMouseY);
            if (y < thumb) {
                engine(kPressButtons, {0x1000});
            } else if (y > thumb + 6) {
                engine(kPressButtons, {0x4000});
            }
        }
    }
    (void)engine_value(kHitRect, {0x43, 0x8a, 0x10, 0x96});
}

// 0x004a3e00: every line up one pixel, unless the last one is already near the top.
void PlayerSelect::bio_scroll_up(std::uint32_t self) {
    const std::uint32_t last = word_at(m_, self + kBioExtent + 4 * player_select_panels_bio_at(m_, self).field_f0());
    if (last == 0 || int_at(m_, last + kElemTargetY) > 0x48) {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(player_select_panels_bio_at(m_, self).field_f0()); ++i) {
            const std::uint32_t line = word_at(m_, self + kBioLines + 4 * static_cast<std::uint32_t>(i));
            set_word_at(m_, line + kElemTargetY, word_at(m_, line + kElemTargetY) - 1);
        }
    }
}

// 0x004a3e40: every line down one pixel, unless the first one is already at the top.
void PlayerSelect::bio_scroll_down(std::uint32_t self) {
    const std::uint32_t first = player_select_panels_bio_at(m_, self).field_b0();
    if (first == 0 || int_at(m_, first + kElemTargetY) < 0x14) {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(player_select_panels_bio_at(m_, self).field_f0()); ++i) {
            const std::uint32_t line = word_at(m_, self + kBioLines + 4 * static_cast<std::uint32_t>(i));
            set_word_at(m_, line + kElemTargetY, word_at(m_, line + kElemTargetY) + 1);
        }
    }
}

// ---- the page dots -------------------------------------------------------------------------------------------------------------------------

// 0x004a5450: one sprite (s2dots.bmp).
std::uint32_t PlayerSelect::dots_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    player_select_panels_dots_at(m_, self).set_field_0(static_cast<Word>(kVtPageDots));
    const std::uint32_t memory = operator_new(0x2c);
    std::uint32_t sprite = 0;
    if (memory != 0) {
        sprite = w_.sprite_construct(memory, 0x0052d84c, 0, 0, -1, -1, 0, 0);
    }
    player_select_panels_dots_at(m_, self).set_field_ac(static_cast<Word>(sprite));
    return self;
}

std::uint32_t PlayerSelect::dots_delete(std::uint32_t self, std::uint32_t flag) {
    dots_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void PlayerSelect::dots_destroy(std::uint32_t self) {
    player_select_panels_dots_at(m_, self).set_field_0(static_cast<Word>(kVtPageDots));
    check(player_select_panels_dots_at(m_, self).field_ac() != 0, kMsgNoDots);
    if (player_select_panels_dots_at(m_, self).field_ac() != 0) {
        delete_object(player_select_panels_dots_at(m_, self).field_ac());
    }
    w_.window_destroy(self);
}

// 0x004a5690: a tail jump into the sprite's update unless the dots are hidden.
void PlayerSelect::dots_update(std::uint32_t self) {
    if (word_at(m_, self + kElemHidden) == 0) {
        vcall_item(kElementUpdate, player_select_panels_dots_at(m_, self).field_ac(), 1);
    }
}

// 0x004a55c0: ten rows of dots, each clipped to the width (twelve pixels a point) of the character's stat of that row.
void PlayerSelect::dots_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (word_at(m_, self + kElemHidden) != 0) {
        return;
    }
    const bool software = global_word(m_, kSoftRenderer) != 0;
    for (std::int32_t row = 0; row < 10; ++row) {
        const std::uint32_t record = career::record(sk_.current_character());
        const std::int32_t width = sbyte_at(m_, record + 0x38 + static_cast<std::uint32_t>(row)) * 12;
        if (software) {
            s_.note(kSvcFillRect, {word_at(m_, self + kElemX), 0, u(width), global_word(m_, kFillHeight), 0});
        } else {
            s_.note(kSvcFillRect, {0, 0, 0x200, global_word(m_, kFillHeight), 0});
        }
        vcall_item(kElementDraw, player_select_panels_dots_at(m_, self).field_ac(), 2, {u(x + int_at(m_, self + kElemX)), u(int_at(m_, self + kElemY) + row * 8 + y)});
        if (software) {
            s_.note(kSvcFillRect, {0, 0, 0x200, global_word(m_, kFillHeight), 0});
        } else {
            s_.note(kSvcFillRect, {word_at(m_, self + kElemX), 0, u(width), global_word(m_, kFillHeight), 0});
        }
    }
}

// ---- the header ----------------------------------------------------------------------------------------------------------------------------

// 0x004a56b0: a dummy text and "Choose skater" (the first is rewritten with the player's number at once).
std::uint32_t PlayerSelect::header_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                             std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    player_select_panels_header_at(m_, self).set_field_0(static_cast<Word>(kVtPlayerHeader));
    player_select_panels_header_at(m_, self).set_field_ac(static_cast<Word>(font));
    std::uint32_t memory = operator_new(0x30);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = w_.text_construct(memory, 0x00525b08, player_select_panels_header_at(m_, self).field_ac(), 0x40, 6, -1, -1);
    }
    w_.window_add(self, text);
    memory = operator_new(0x30);
    text = 0;
    if (memory != 0) {
        text = w_.text_construct(memory, global_word(m_, kTextChoose), player_select_panels_header_at(m_, self).field_ac(), 0x1a, 0x16, -1, -1);
    }
    w_.window_add(self, text);
    header_update(self);
    return self;
}

std::uint32_t PlayerSelect::header_delete(std::uint32_t self, std::uint32_t flag) {
    header_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void PlayerSelect::header_destroy(std::uint32_t self) {
    player_select_panels_header_at(m_, self).set_field_0(static_cast<Word>(kVtPlayerHeader));
    w_.window_destroy(self);
}

// 0x004a57e0: "Player n" for the active player, then the window's update.
void PlayerSelect::header_update(std::uint32_t self) {
    const std::int32_t player = global_int(m_, kPadCurrentGlobal);
    check(player >= 0 && player < 8, kMsgInvalidListCount);
    const std::string line = retail_format("%s %d", {m_.cstring(global_word(m_, kTextPlayer), 0x40)}, {player + 1});
    w_.text_set_string(word_at(m_, self + kWinChildren), scratch_line(line));
    w_.window_update(self);
}

// ---- the main menu window ------------------------------------------------------------------------------------------------------------------

// 0x004a5890: a list (gap 2 in the career, 3 otherwise), its style (the font copy, grey), the rows (Continue, Appearance, Buy stats and Buy tricks in the
// career or Edit tricks outside the multi-player kind, Skate shop) and the two arrow sprites.
std::uint32_t PlayerSelect::menu_construct(std::uint32_t self, std::uint32_t two_player, std::uint32_t font, std::int32_t x, std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    player_select_panels_menu_at(m_, self).set_field_0(static_cast<Word>(kVtPlayerMenu));
    const bool career = global_word(m_, kGlobalReplayState) == 1;
    std::uint32_t memory = operator_new(0x1bc);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.list_construct(memory, x, y, 0x100, 0x78, 1, 1, career ? 2 : 3, 0);
    }
    player_select_panels_menu_at(m_, self).set_field_ac(static_cast<Word>(list));
    memory = operator_new(0x17c);
    const std::uint32_t props = memory != 0 ? menu_.properties_construct(memory) : 0u;
    player_select_panels_menu_at(m_, self).set_field_230(static_cast<Word>(props));
    menu_.copy_font_fields(props + 4, font);
    set_word_at(m_, props + 0x18, 0x80);
    set_word_at(m_, props + 0x1c, 0x80);
    set_word_at(m_, props + 0x20, 0x80);
    set_word_at(m_, props + 0x34, 0x800);
    set_word_at(m_, props + 0x150, 0xa00);
    set_word_at(m_, props + 0x154, 0x800);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kMenuRows + 4 * i, 0);
    }
    auto row = [&](std::uint32_t slot, std::uint32_t id, std::uint32_t label) {
        const std::uint32_t block = operator_new(0x54);
        const std::uint32_t item = block != 0 ? menu_.text_construct(block, id, word_at(m_, label), player_select_panels_menu_at(m_, self).field_230()) : 0u;
        set_word_at(m_, self + slot, item);
    };
    row(kMenuRows, 1, kMenuLabels[0]);
    row(kMenuRows + 4, 2, kMenuLabels[1]);
    if (career) {
        row(kMenuRows + 8, 3, kMenuLabels[2]);
        row(kMenuRows + 12, 4, kMenuLabels[3]);
    } else if (two_player == 0) {
        row(kMenuRows + 12, 4, kMenuLabels[4]);
    }
    row(kMenuRows + 16, 5, kMenuLabels[5]);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kMenuRows + 4 * i) != 0) {
            vcall_list("list_add", player_select_panels_menu_at(m_, self).field_ac(), 6, {word_at(m_, self + kMenuRows + 4 * i)});
        }
    }
    auto arrow = [&](std::uint32_t slot, std::uint32_t file) {
        const std::uint32_t block = operator_new(0x5c);
        std::uint32_t sprite = 0;
        if (block != 0) {
            s_.note(kSpriteConstruct, {block, file, 1, 0, 0}, m_.cstring(file));
            sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, sprite);
    };
    arrow(kMenuArrowA, 0x00527c20);
    arrow(kMenuArrowB, 0x00527c10);
    vcall_list("list_arrows", player_select_panels_menu_at(m_, self).field_ac(), 1, {player_select_panels_menu_at(m_, self).field_238(), player_select_panels_menu_at(m_, self).field_23c(), 5});
    vcall_list("list_v2", player_select_panels_menu_at(m_, self).field_ac(), 2);
    player_select_panels_menu_at(m_, self).set_field_234(0);
    return self;
}

std::uint32_t PlayerSelect::menu_delete(std::uint32_t self, std::uint32_t flag) {
    menu_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void PlayerSelect::menu_destroy(std::uint32_t self) {
    player_select_panels_menu_at(m_, self).set_field_0(static_cast<Word>(kVtPlayerMenu));
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kMenuRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kMenuRows + 4 * i));
        }
    }
    for (std::uint32_t slot : {kMenuProps, kMenuList, kMenuArrowA, kMenuArrowB}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    w_.window_destroy(self);
}

// 0x004a5d80: the second row's text (the custom skater's, or the costume's letter), the pad moves the list, a confirm records the highlighted row.
void PlayerSelect::menu_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (word_at(m_, self + kElemHidden) != 0) {
        return;
    }
    check(word_at(m_, self + kMenuRows + 4) != 0, kMsgNoItem);
    std::string line;
    if ((word_at(m_, skater::row(sk_.current_character()) + skater::kRowFlags) & 2) != 0) {
        line = m_.cstring(global_word(m_, kTextCustomAppearance), 0x40);
    } else {
        const std::string letter(1, static_cast<char>(sk_.current_costume() + 0x41));
        line = retail_format("%s %s", {m_.cstring(global_word(m_, kTextCostume), 0x40), letter}, {});
    }
    vcall_item("item_set_text", word_at(m_, self + kMenuRows + 4), 8, {scratch_line(line)});
    const std::uint32_t pad = global_word(m_, kPadCurrentGlobal) * kPanelPadStride;
    if (byte_at(m_, pad + kPadUpEdge) != 0 || byte_at(m_, pad + kPadUpEdgeB) != 0) {
        sfx(0x90);
    }
    const std::uint32_t list = player_select_panels_menu_at(m_, self).field_ac();
    vcall_list("list_v2", list, 2);
    vcall_list("list_v3", list, 3);
    s_.note(kSvcPadEdges, {global_word(m_, kPadCurrentGlobal), 0, 6});
    const std::uint32_t edges = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
    vcall_list("list_v5", list, 5, {edges});
    vcall_list("list_v4", list, 4);
    player_select_panels_menu_at(m_, self).set_field_234(0);
    if (byte_at(m_, pad + kPanelConfirm) != 0) {
        sfx(0x91);
        set_byte_at(m_, pad + kPanelConfirm, 0);
        player_select_panels_menu_at(m_, self).set_field_234(static_cast<Word>(menu_.list_highlighted_id(player_select_panels_menu_at(m_, self).field_ac())));
    }
    w_.window_draw(self, x, y);
}

}  // namespace opentony::runtime::frontend
