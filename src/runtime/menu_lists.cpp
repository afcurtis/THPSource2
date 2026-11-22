#include "auto_views.hpp"
#include "menu_toolkit.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kVtRootClass = 0x005184b4;
constexpr std::uint32_t kListItems = 0x14;       // 0x60 child pointers
constexpr std::uint32_t kListCount = 0x194;
constexpr std::uint32_t kListCursor = 0x198;
constexpr std::uint32_t kListX = 0x19c;
constexpr std::uint32_t kListY = 0x1a0;
constexpr std::uint32_t kListW = 0x1a4;
constexpr std::uint32_t kListH = 0x1a8;
constexpr std::uint32_t kListSelectedArg = 0x1ac;
constexpr std::uint32_t kListCentre = 0x1b0;
constexpr std::uint32_t kListCentreV = 0x1b4;
constexpr std::uint32_t kListGap = 0x1b8;

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t sub(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }
std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t sar1(std::int32_t v) { return v >> 1; }
std::int32_t half(std::int32_t v) { return (v - (v >> 31)) >> 1; }

}  // namespace

// ---- MenuList --------------------------------------------------------------------------------------------

void MenuToolkit::list_draw_box(std::uint32_t self) {
    if (menu_lists_list_at(m_, self).field_4() == 0) {
        return;
    }
    s_.note(kSvcSelectionBox, {menu_lists_list_at(m_, self).field_19c(), u(sub(static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1a0()), 2)), menu_lists_list_at(m_, self).field_1a4(), u(add(static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1a8()), 3)),
                               0x3c, 0x3c, 0x3c});
}

void MenuToolkit::list_set_arrows(std::uint32_t self, std::uint32_t left, std::uint32_t right, std::uint32_t margin) {
    menu_lists_list_at(m_, self).set_field_c(static_cast<Word>(right));
    menu_lists_list_at(m_, self).set_field_10(static_cast<Word>(margin));
    menu_lists_list_at(m_, self).set_field_8(static_cast<Word>(left));
    if (left != 0) {
        set_byte_at(m_, left + 0x0a, 2);
        set_byte_at(m_, menu_lists_list_at(m_, self).field_8() + 0x0b, 1);
    }
    if (menu_lists_list_at(m_, self).field_c() != 0) {
        set_byte_at(m_, menu_lists_list_at(m_, self).field_c() + 0x0a, 0);
        set_byte_at(m_, menu_lists_list_at(m_, self).field_c() + 0x0b, 1);
    }
}

std::uint32_t MenuToolkit::list_highlighted_id(std::uint32_t self) {
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * i);
        if (child != 0 && byte_at(m_, child + 0x0e) != 0) {
            return word_at(m_, word_at(m_, self + kListItems + 4 * i) + 8);
        }
    }
    check(false, kMenuMsgNoneHighlighted);
    return 0xffffffffu;
}

// 0x004dd200 (thiscall, no arguments, answers 0): the mouse on a page of the menu. With the pointer in the page's top part (y < 200) and the highlighted item of the list at +0x1fc
// number 3 or 4, the pointer over either scroll arrow (the elements at +0x388 and +0x38c, a 20 by 4 rectangle) presses left (0x8000) or right (0x2000). Then each of the ten
// elements at +0x200, when the pointer is over its rectangle and it is enabled, takes the click: the ones numbered -1 to 1 press 0x40; the others press 0x2000, and those
// numbered 3 and 4 also highlight their item in the list.
std::uint32_t MenuToolkit::page_mouse_hook(std::uint32_t self) {
    constexpr std::uint32_t kMouseX = 0x006a13f0;
    constexpr std::uint32_t kMouseY = 0x006a13f4;
    constexpr std::uint32_t kMouseButton = 0x006a13f8;
    constexpr std::uint32_t kMouseOn = 0x006a13fc;
    constexpr std::uint32_t kMouseOverList = 0x006a13fd;
    constexpr std::uint32_t kMouseHeld = 0x006a13fe;
    (void)kMouseX;
    const auto hit = [&](std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
        s_.note("engine_004dbf90", {x, y, width, height});
        return (static_cast<std::uint32_t>(s_.value("engine_004dbf90")) & 0xff) != 0;
    };
    const auto press = [&](std::uint32_t buttons) { s_.note("engine_004e4190", {buttons}); };
    if (global_byte(m_, kMouseOn) != 0 && global_int(m_, kMouseY) < 0xc8) {
        s_.note("engine_00470ab0", {word_at(m_, self + 0x1fc)});
        const std::int32_t highlighted = s_.value("engine_00470ab0");
        if (highlighted >= 3 && highlighted <= 4) {
            const std::uint32_t y = global_word(m_, kMouseY) - 2;
            if (const std::uint32_t arrow = word_at(m_, self + 0x388); arrow != 0 && hit(word_at(m_, arrow + 0x44) - 10, y, 0x14, 4)) {
                press(0x8000);
            }
            if (const std::uint32_t arrow = word_at(m_, self + 0x38c); arrow != 0 && hit(word_at(m_, arrow + 0x44) - 10, y, 0x14, 4)) {
                press(0x2000);
            }
        }
    }
    for (std::uint32_t k = 0; k < 10; ++k) {
        const std::uint32_t element = word_at(m_, self + 0x200 + 4 * k);
        const std::uint32_t arrow = word_at(m_, self + 0x38c);
        if (element == 0 || global_byte(m_, kMouseOverList) == 0 || arrow == 0) {
            continue;
        }
        if (!hit(word_at(m_, element + 0x1c), word_at(m_, element + 0x20), word_at(m_, arrow + 0x44) + 0x14, word_at(m_, element + 0x18) * 2)) {
            continue;
        }
        const std::uint32_t chosen = word_at(m_, self + 0x200 + 4 * k);
        if (byte_at(m_, chosen + 0xc) == 0 || (global_byte(m_, kMouseHeld) == 0 && global_word(m_, kMouseButton) == 0)) {
            continue;
        }
        const std::uint32_t index = word_at(m_, chosen + 8) + 1;
        if (index <= 2) {
            set_half_at(m_, chosen + 4, 0x40);
        } else if (index == 3) {
            set_half_at(m_, chosen + 4, 0x2000);
        } else {
            set_half_at(m_, chosen + 4, 0x2000);
            if (index <= 5) {
                s_.note("engine_00470b20", {word_at(m_, self + 0x1fc), word_at(m_, word_at(m_, self + 0x200 + 4 * k) + 8)});
            }
        }
    }
    return 0;
}

void MenuToolkit::list_highlight(std::uint32_t self, std::uint32_t id) {
    vcall_item("item_highlight", word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198()), 3, {0});
    menu_lists_list_at(m_, self).set_field_198(0);
    for (;;) {
        const std::uint32_t at = menu_lists_list_at(m_, self).field_198();
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * at);
        if (child != 0 && word_at(m_, child + 8) == id) {
            break;
        }
        menu_lists_list_at(m_, self).set_field_198(static_cast<Word>(at + 1));
        if (!(static_cast<std::int32_t>(at + 1) < 0x60)) {
            break;
        }
    }
    check(menu_lists_list_at(m_, self).field_198() != 0x60, kMenuMsgItemDoesNotExist);
    vcall_item("item_highlight", word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198()), 3, {1});
}

void MenuToolkit::list_highlight_first(std::uint32_t self) {
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * i);
        if (child != 0 && byte_at(m_, child + 0x0c) != 0) {
            list_highlight(self, word_at(m_, word_at(m_, self + kListItems + 4 * i) + 8));
            return;
        }
    }
    check(menu_lists_list_at(m_, self).field_14() != 0, kMenuMsgFirstItemMissing);
    list_highlight(self, word_at(m_, menu_lists_list_at(m_, self).field_14() + 8));
}

std::uint32_t MenuToolkit::list_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
                                          std::uint32_t centre, std::uint32_t centre_v, std::uint32_t gap, std::uint32_t arg) {
    menu_lists_list_at(m_, self).set_field_0(static_cast<Word>(kMenuVtList));
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kListItems + 4 * i, 0);
    }
    menu_lists_list_at(m_, self).set_field_194(0);
    menu_lists_list_at(m_, self).set_field_19c(static_cast<Word>(u(x)));
    menu_lists_list_at(m_, self).set_field_1a0(static_cast<Word>(u(y)));
    menu_lists_list_at(m_, self).set_field_1a4(static_cast<Word>(u(width)));
    menu_lists_list_at(m_, self).set_field_1a8(static_cast<Word>(u(height)));
    menu_lists_list_at(m_, self).set_field_1b0(static_cast<Word>(centre));
    menu_lists_list_at(m_, self).set_field_c(0);
    menu_lists_list_at(m_, self).set_field_8(0);
    menu_lists_list_at(m_, self).set_field_10(0);
    menu_lists_list_at(m_, self).set_field_198(0);
    menu_lists_list_at(m_, self).set_field_4(0);
    menu_lists_list_at(m_, self).set_field_1b4(static_cast<Word>(centre_v));
    menu_lists_list_at(m_, self).set_field_1ac(static_cast<Word>(arg));
    menu_lists_list_at(m_, self).set_field_1b8(static_cast<Word>(gap));
    return self;
}

void MenuToolkit::list_destroy(std::uint32_t self) { set_word_at(m_, self, kVtRootClass); }

std::uint32_t MenuToolkit::list_delete(std::uint32_t self, std::uint32_t flag) {
    list_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::list_layout(std::uint32_t self) {
    const std::int32_t count = static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_194());
    std::int32_t total = 0;
    std::int32_t widest = 0;
    {
        const std::int32_t gap = static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1b8());
        for (std::int32_t i = 0; i < count; ++i) {
            const std::uint32_t child = word_at(m_, self + kListItems + 4 * u(i));
            const std::int32_t w = int_at(m_, child + 0x14);
            if (w > widest) {
                widest = w;
            }
            total = add(total, add(int_at(m_, child + 0x18), gap));
        }
    }
    std::int32_t y = 0;
    if (static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1b4()) == 1) {
        y = add(sar1(sub(static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1a8()), total)), static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1a0()));
        menu_lists_list_at(m_, self).set_field_1a0(static_cast<Word>(u(y)));
    } else {
        y = static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1a0());
    }
    menu_lists_list_at(m_, self).set_field_1a8(static_cast<Word>(u(total)));
    if (static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1b0()) == 1) {
        const std::int32_t shift = sub(sar1(static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1a4())), sar1(widest));
        menu_lists_list_at(m_, self).set_field_19c(static_cast<Word>(u(add(static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_19c()), shift))));
    }
    menu_lists_list_at(m_, self).set_field_1a4(static_cast<Word>(u(widest)));
    const std::uint32_t arrow = menu_lists_list_at(m_, self).field_8();
    std::int32_t margin = static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_10());
    if (arrow != 0) {
        margin = add(margin, mul(static_cast<std::int16_t>(half_at(m_, arrow + 0x40)), 2));
    }
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_194()); ++i) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * u(i));
        set_word_at(m_, child + 0x2c, menu_lists_list_at(m_, self).field_1b0());
        if (static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1b0()) != 1) {
            vcall_item("item_set_position", child, 1, {u(add(static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_19c()), margin)), u(y)});
        } else {
            const std::int32_t x = add(sub(sar1(static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1a4())), sar1(int_at(m_, child + 0x14))), static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_19c()));
            vcall_item("item_set_position", child, 1, {u(x), u(y)});
        }
        const std::uint32_t placed = word_at(m_, self + kListItems + 4 * u(i));
        const std::int32_t advance = add(int_at(m_, placed + 0x18), static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_1b8()));
        set_word_at(m_, placed + 0x28, menu_lists_list_at(m_, self).field_1ac());
        y = add(y, advance);
    }
    // make sure the highlighted child can be selected: step forward (wrapping once) to the next that can
    std::int32_t laps = 0;
    std::uint32_t at = menu_lists_list_at(m_, self).field_198();
    if (byte_at(m_, word_at(m_, self + kListItems + 4 * at) + 0x0c) == 0) {
        for (;;) {
            at = menu_lists_list_at(m_, self).field_198() + 1;
            menu_lists_list_at(m_, self).set_field_198(static_cast<Word>(at));
            if (at == menu_lists_list_at(m_, self).field_194()) {
                ++laps;
                menu_lists_list_at(m_, self).set_field_198(0);
                if (laps == 2) {
                    break;
                }
            }
            const std::uint32_t child = word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198());
            if (byte_at(m_, child + 0x0c) != 0) {
                break;
            }
        }
    }
    vcall_item("item_highlight", word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198()), 3, {1});
}

void MenuToolkit::list_draw(std::uint32_t self) {
    const std::uint32_t left = menu_lists_list_at(m_, self).field_8();
    if (left != 0) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198());
        const std::int32_t y = add(sar1(int_at(m_, child + 0x18)), int_at(m_, child + 0x20));
        const std::int32_t x = sub(int_at(m_, child + 0x1c), static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_10()));
        s_.note(kSpriteDrawService, {left, u(x), u(y), 0});
    }
    const std::uint32_t right = menu_lists_list_at(m_, self).field_c();
    if (right != 0) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198());
        const std::int32_t y = add(sar1(int_at(m_, child + 0x18)), int_at(m_, child + 0x20));
        const std::int32_t x = add(add(int_at(m_, child + 0x1c), int_at(m_, child + 0x14)), static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_10()));
        s_.note(kSpriteDrawService, {right, u(x), u(y), 0});
    }
    for (std::int32_t i = 0x5f; i >= 0; --i) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * u(i));
        if (child != 0) {
            vcall_item("item_draw", child, 2);
        }
    }
    list_draw_box(self);
}

void MenuToolkit::list_snap_all(std::uint32_t self) {
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_194()); ++i) {
        vcall_item("item_snap", word_at(m_, self + kListItems + 4 * u(i)), 6);
    }
}

void MenuToolkit::list_navigate(std::uint32_t self, std::uint32_t direction) {
    if (direction == 0 || direction == 1) {
        vcall_item("item_navigate", word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198()), 5, {direction});
    }
    vcall_item("item_highlight", word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198()), 3, {0});
    const std::uint32_t old = menu_lists_list_at(m_, self).field_198();
    if (direction == 2) {
        std::int32_t at = static_cast<std::int32_t>(old);
        for (;;) {
            --at;
            menu_lists_list_at(m_, self).set_field_198(static_cast<Word>(u(at)));
            if (at < 0) {
                at = sub(static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_194()), 1);
                menu_lists_list_at(m_, self).set_field_198(static_cast<Word>(u(at)));
            }
            if (byte_at(m_, word_at(m_, self + kListItems + 4 * u(at)) + 0x0c) != 0) {
                break;
            }
        }
    } else if (direction == 3) {
        std::int32_t at = static_cast<std::int32_t>(old);
        for (;;) {
            ++at;
            menu_lists_list_at(m_, self).set_field_198(static_cast<Word>(u(at)));
            if (!(at < static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_194()))) {
                at = 0;
                menu_lists_list_at(m_, self).set_field_198(0);
            }
            if (byte_at(m_, word_at(m_, self + kListItems + 4 * u(at)) + 0x0c) != 0) {
                break;
            }
        }
    }
    vcall_item("item_highlight", word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198()), 3, {1});
    if (menu_lists_list_at(m_, self).field_198() != old) {
        set_word_at(m_, word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_198()) + 0x24, 4);
    }
    vcall_list("list_v7", self, 7);
}

void MenuToolkit::list_add(std::uint32_t self, std::uint32_t item) {
    check(static_cast<std::int32_t>(menu_lists_list_at(m_, self).field_194()) < 0x60, kMenuMsgOutOfMenuSlots);
    set_word_at(m_, self + kListItems + 4 * menu_lists_list_at(m_, self).field_194(), item);
    menu_lists_list_at(m_, self).set_field_194(static_cast<Word>(menu_lists_list_at(m_, self).field_194() + 1));
    if (half_at(m_, item + 4) == 0) {
        set_half_at(m_, item + 4, 0x40);
    }
}

// ---- ScrollList ------------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::scroll_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
                                            std::uint32_t centre, std::uint32_t gap, std::uint32_t pad, std::uint32_t arg) {
    menu_lists_scroll_at(m_, self).set_field_0(static_cast<Word>(kMenuVtScrollList));
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kListItems + 4 * i, 0);
    }
    menu_lists_scroll_at(m_, self).set_field_1b0(static_cast<Word>(centre));
    menu_lists_scroll_at(m_, self).set_field_19c(static_cast<Word>(u(x)));
    menu_lists_scroll_at(m_, self).set_field_1a0(static_cast<Word>(u(y)));
    menu_lists_scroll_at(m_, self).set_field_1a4(static_cast<Word>(u(width)));
    menu_lists_scroll_at(m_, self).set_field_194(0);
    menu_lists_scroll_at(m_, self).set_field_1a8(static_cast<Word>(u(height)));
    menu_lists_scroll_at(m_, self).set_field_344(static_cast<Word>(u(height)));
    menu_lists_scroll_at(m_, self).set_field_348(static_cast<Word>(pad));
    menu_lists_scroll_at(m_, self).set_field_33c(static_cast<Word>(gap));
    menu_lists_scroll_at(m_, self).set_field_c(0);
    menu_lists_scroll_at(m_, self).set_field_8(0);
    menu_lists_scroll_at(m_, self).set_field_10(0);
    menu_lists_scroll_at(m_, self).set_field_340(0);
    menu_lists_scroll_at(m_, self).set_field_198(0);
    menu_lists_scroll_at(m_, self).set_field_1b4(0);
    menu_lists_scroll_at(m_, self).set_field_4(0);
    menu_lists_scroll_at(m_, self).set_field_1b8(0);
    menu_lists_scroll_at(m_, self).set_field_34c(0);
    menu_lists_scroll_at(m_, self).set_field_1ac(static_cast<Word>(arg));
    menu_lists_scroll_at(m_, self).set_field_1b9(1);
    return self;
}

void MenuToolkit::scroll_destroy(std::uint32_t self) { set_word_at(m_, self, kVtRootClass); }

std::uint32_t MenuToolkit::scroll_delete(std::uint32_t self, std::uint32_t flag) {
    scroll_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::scroll_layout(std::uint32_t self) {
    std::int32_t margin = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_10());
    const std::uint32_t arrow = menu_lists_scroll_at(m_, self).field_8();
    if (arrow != 0) {
        margin = add(margin, mul(static_cast<std::int16_t>(half_at(m_, arrow + 0x40)), 2));
    }
    std::int32_t y = sub(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a0()), static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_340()));
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_194()); ++i) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * u(i));
        set_word_at(m_, child + 0x2c, menu_lists_scroll_at(m_, self).field_1b0());
        if (static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1b0()) != 1) {
            vcall_item("item_set_position", child, 1, {u(add(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_19c()), margin)), u(y)});
        } else {
            const std::int32_t x = add(sub(sar1(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a4())), sar1(int_at(m_, child + 0x14))), static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_19c()));
            vcall_item("item_set_position", child, 1, {u(x), u(y)});
        }
        const std::uint32_t placed = word_at(m_, self + kListItems + 4 * u(i));
        const std::int32_t advance = add(int_at(m_, placed + 0x18), static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_348()));
        set_word_at(m_, placed + 0x28, menu_lists_scroll_at(m_, self).field_1ac());
        y = add(y, advance);
    }
    // the highlighted child must be selectable: step once (wrapping) when it is not
    if (static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_194()) > 0) {
        const std::uint32_t at = menu_lists_scroll_at(m_, self).field_198();
        if (byte_at(m_, word_at(m_, self + kListItems + 4 * at) + 0x0c) == 0) {
            const std::uint32_t next = at + 1;
            menu_lists_scroll_at(m_, self).set_field_198(static_cast<Word>(next));
            if (next == menu_lists_scroll_at(m_, self).field_194()) {
                menu_lists_scroll_at(m_, self).set_field_198(0);
            }
        }
    }
    vcall_item("item_highlight", word_at(m_, self + kListItems + 4 * menu_lists_scroll_at(m_, self).field_198()), 3, {1});
    if (menu_lists_scroll_at(m_, self).field_34c() != 0) {
        return;
    }
    // once: shrink the visible height to the children that fit, and the width to the widest child
    std::int32_t used = 0;
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * i);
        if (child != 0) {
            used = add(used, add(int_at(m_, child + 0x18), static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_348())));
            if (!(used < static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a8()))) {
                break;
            }
        }
    }
    used = sub(used, static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_348()));
    menu_lists_scroll_at(m_, self).set_field_1a8(static_cast<Word>(u(used)));
    menu_lists_scroll_at(m_, self).set_field_344(static_cast<Word>(u(used)));
    std::int32_t widest = 0;
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * i);
        if (child != 0) {
            const std::int32_t w = int_at(m_, child + 0x14);
            if (w > widest) {
                widest = w;
            }
        }
    }
    if (menu_lists_scroll_at(m_, self).field_1b8() != 0) {
        if (static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1b0()) == 1) {
            const std::int32_t shift = sub(sar1(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a4())), sar1(widest));
            menu_lists_scroll_at(m_, self).set_field_19c(static_cast<Word>(u(add(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_19c()), shift))));
        }
        menu_lists_scroll_at(m_, self).set_field_1a4(static_cast<Word>(u(widest)));
    }
    menu_lists_scroll_at(m_, self).set_field_34c(1);
}

void MenuToolkit::scroll_draw(std::uint32_t self) {
    const std::uint32_t current = word_at(m_, self + kListItems + 4 * menu_lists_scroll_at(m_, self).field_198());
    const std::int32_t top = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a0());
    std::int32_t y = add(sar1(int_at(m_, current + 0x18)), int_at(m_, current + 0x20));
    if (!(y >= top)) {
        y = top;
    } else {
        const std::int32_t bottom = add(top, static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a8()));
        if (!(y < bottom)) {
            y = sub(bottom, 1);
        }
    }
    if (menu_lists_scroll_at(m_, self).field_8() != 0) {
        const std::int32_t x = sub(int_at(m_, current + 0x1c), static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_10()));
        s_.note(kSpriteDrawService, {menu_lists_scroll_at(m_, self).field_8(), u(x), u(y), 0});
    }
    if (menu_lists_scroll_at(m_, self).field_c() != 0) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * menu_lists_scroll_at(m_, self).field_198());
        const std::int32_t x = add(add(int_at(m_, child + 0x1c), int_at(m_, child + 0x14)), static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_10()));
        s_.note(kSpriteDrawService, {menu_lists_scroll_at(m_, self).field_c(), u(x), u(y), 0});
    }
    const bool software = global_word(m_, kSoftwareRenderer) != 0;
    const std::uint32_t height = global_word(m_, kScreenFillHeight);
    if (!software) {
        fill_rect(0, 0, 0x200, height);
    }
    for (std::int32_t i = 0x5f; i >= 0; --i) {
        const std::uint32_t child = word_at(m_, self + kListItems + 4 * u(i));
        if (child == 0) {
            continue;
        }
        const std::int32_t h = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a8());
        const std::int32_t rect_y = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a0());
        const std::int32_t child_y = int_at(m_, child + 0x20);
        if (child_y > add(h, rect_y) || child_y < rect_y) {
            continue;
        }
        const bool trim = menu_lists_scroll_at(m_, self).field_1b9() != 0;
        const std::int32_t fill_y = trim ? sub(rect_y, 1) : rect_y;
        const std::int32_t fill_h = trim ? add(h, 2) : h;
        if (software) {
            fill_rect(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_19c()), fill_y, static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a4()), u(fill_h));
        }
        vcall_item("item_draw", child, 2);
        if (menu_lists_scroll_at(m_, self).field_1b4() != 0) {
            s_.note(kSvcFrameBox, {word_at(m_, child + 0x1c), u(add(add(int_at(m_, child + 0x20), int_at(m_, child + 0x18)), 2)), menu_lists_scroll_at(m_, self).field_1a4(), 1, 0x40, 0x40,
                                   0x80});
        }
        if (!software) {
            fill_rect(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_19c()), fill_y, static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a4()), u(fill_h));
        }
    }
    if (software) {
        fill_rect(0, 0, 0x200, height);
    }
    list_draw_box(self);
}

void MenuToolkit::scroll_follow(std::uint32_t self) {
    vcall_list("list_v7", self, 7);
    const std::uint32_t cursor = menu_lists_scroll_at(m_, self).field_198();
    std::int32_t top = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_340());
    std::int32_t bottom = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_344());
    const std::int32_t offset = int_at(m_, self + 0x1bc + 4 * cursor);
    const std::int32_t height = int_at(m_, word_at(m_, self + kListItems + 4 * cursor) + 0x18);
    if (top > offset) {
        std::int32_t step = sar1(sub(top, offset));
        if (step == 0) {
            step = 1;
        }
        top = sub(top, step);
        bottom = sub(bottom, step);
        menu_lists_scroll_at(m_, self).set_field_340(static_cast<Word>(u(top)));
        menu_lists_scroll_at(m_, self).set_field_344(static_cast<Word>(u(bottom)));
        if (top < offset) {
            menu_lists_scroll_at(m_, self).set_field_340(static_cast<Word>(u(offset)));
            menu_lists_scroll_at(m_, self).set_field_344(static_cast<Word>(u(add(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a8()), offset))));
        }
    } else if (bottom < add(offset, height)) {
        std::int32_t step = sar1(sub(add(offset, height), bottom));
        if (step == 0) {
            step = 1;
        }
        top = add(top, step);
        bottom = add(bottom, step);
        menu_lists_scroll_at(m_, self).set_field_340(static_cast<Word>(u(top)));
        menu_lists_scroll_at(m_, self).set_field_344(static_cast<Word>(u(bottom)));
        const std::int32_t limit = add(offset, height);
        if (bottom > limit) {
            menu_lists_scroll_at(m_, self).set_field_344(static_cast<Word>(u(limit)));
            menu_lists_scroll_at(m_, self).set_field_340(static_cast<Word>(u(sub(limit, static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1a8())))));
        }
    }
    vcall_list("list_v2", self, 2);
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_194()); ++i) {
        vcall_item("item_snap", word_at(m_, self + kListItems + 4 * u(i)), 6);
    }
}

void MenuToolkit::scroll_navigate(std::uint32_t self, std::uint32_t direction) {
    vcall_item("item_highlight", word_at(m_, self + kListItems + 4 * menu_lists_scroll_at(m_, self).field_198()), 3, {0});
    const std::uint32_t old = menu_lists_scroll_at(m_, self).field_198();
    if (direction == 0 || direction == 1) {
        vcall_item("item_navigate", word_at(m_, self + kListItems + 4 * old), 5, {direction});
    }
    if (direction == 2) {
        const std::int32_t span = sub(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_344()), static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_340()));
        std::int32_t at = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_198());
        for (;;) {
            --at;
            if (at < 0) {
                at = sub(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_194()), 1);
                const std::int32_t bottom = add(int_at(m_, word_at(m_, self + kListItems + 4 * u(at)) + 0x18), int_at(m_, self + 0x1bc + 4 * u(at)));
                menu_lists_scroll_at(m_, self).set_field_344(static_cast<Word>(u(bottom)));
                menu_lists_scroll_at(m_, self).set_field_340(static_cast<Word>(u(sub(bottom, span))));
            }
            if (byte_at(m_, word_at(m_, self + kListItems + 4 * u(at)) + 0x0c) != 0) {
                break;
            }
        }
        menu_lists_scroll_at(m_, self).set_field_198(static_cast<Word>(u(at)));
    } else if (direction == 3) {
        std::int32_t at = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_198());
        const std::int32_t count = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_194());
        for (;;) {
            ++at;
            const std::int32_t span = sub(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_344()), static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_340()));
            if (!(at < count)) {
                const std::int32_t first = static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_1bc());
                at = 0;
                menu_lists_scroll_at(m_, self).set_field_340(static_cast<Word>(u(first)));
                menu_lists_scroll_at(m_, self).set_field_344(static_cast<Word>(u(add(first, span))));
            }
            if (byte_at(m_, word_at(m_, self + kListItems + 4 * u(at)) + 0x0c) != 0) {
                break;
            }
        }
        menu_lists_scroll_at(m_, self).set_field_198(static_cast<Word>(u(at)));
    }
    vcall_item("item_highlight", word_at(m_, self + kListItems + 4 * menu_lists_scroll_at(m_, self).field_198()), 3, {1});
    if (menu_lists_scroll_at(m_, self).field_198() != old) {
        set_word_at(m_, word_at(m_, self + kListItems + 4 * menu_lists_scroll_at(m_, self).field_198()) + 0x24, 4);
    }
}

void MenuToolkit::scroll_add(std::uint32_t self, std::uint32_t item) {
    check(static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_194()) < 0x60, kMenuMsgOutOfMenuSlots);
    const std::uint32_t count = menu_lists_scroll_at(m_, self).field_194();
    set_word_at(m_, self + kListItems + 4 * count, item);
    if (count == 0) {
        menu_lists_scroll_at(m_, self).set_field_1bc(0);
    } else {
        const std::uint32_t previous = word_at(m_, self + 0x10 + 4 * count);
        const std::int32_t offset = add(add(int_at(m_, self + 0x1b8 + 4 * count), int_at(m_, previous + 0x18)), static_cast<std::int32_t>(menu_lists_scroll_at(m_, self).field_348()));
        set_word_at(m_, self + 0x1bc + 4 * count, u(offset));
    }
    menu_lists_scroll_at(m_, self).set_field_194(static_cast<Word>(menu_lists_scroll_at(m_, self).field_194() + 1));
    set_half_at(m_, item + 4, 0x40);
}

// ---- MenuGrid --------------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::grid_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t columns, std::int32_t rows,
                                          std::uint32_t gap_x, std::uint32_t gap_y, std::uint32_t arg) {
    menu_lists_grid_at(m_, self).set_field_4(static_cast<Word>(u(columns)));
    const std::int32_t cells = mul(columns, rows);
    menu_lists_grid_at(m_, self).set_field_8(static_cast<Word>(u(rows)));
    menu_lists_grid_at(m_, self).set_field_1c(static_cast<Word>(u(x)));
    menu_lists_grid_at(m_, self).set_field_20(static_cast<Word>(u(y)));
    menu_lists_grid_at(m_, self).set_field_0(static_cast<Word>(kMenuVtGrid));
    menu_lists_grid_at(m_, self).set_field_18(0);
    menu_lists_grid_at(m_, self).set_field_24(static_cast<Word>(gap_x));
    menu_lists_grid_at(m_, self).set_field_28(static_cast<Word>(gap_y));
    menu_lists_grid_at(m_, self).set_field_10(0);
    menu_lists_grid_at(m_, self).set_field_14(0);
    s_.note(kAllocBuffer, {u(mul(cells, 4)), 0, 1, 0});
    menu_lists_grid_at(m_, self).set_field_c(static_cast<Word>(static_cast<std::uint32_t>(s_.value(kAllocBuffer))));
    for (std::int32_t i = 0; i < cells; ++i) {
        set_word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(i), 0);
    }
    menu_lists_grid_at(m_, self).set_field_2c(1);
    menu_lists_grid_at(m_, self).set_field_30(1);
    menu_lists_grid_at(m_, self).set_field_34(static_cast<Word>(arg));
    return self;
}

void MenuToolkit::grid_destroy(std::uint32_t self) {
    menu_lists_grid_at(m_, self).set_field_0(static_cast<Word>(kMenuVtGrid));
    s_.note(kMemDeleteService, {menu_lists_grid_at(m_, self).field_c()});
    menu_lists_grid_at(m_, self).set_field_0(static_cast<Word>(kVtRootClass));
}

std::uint32_t MenuToolkit::grid_delete(std::uint32_t self, std::uint32_t flag) {
    grid_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::grid_layout(std::uint32_t self) {
    const std::int32_t columns = static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4());
    const std::int32_t rows = static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8());
    const std::uint32_t cells = menu_lists_grid_at(m_, self).field_c();
    std::int32_t total_h = 0;
    for (std::int32_t r = 0; r < rows; ++r) {
        const std::uint32_t first = word_at(m_, cells + 4 * u(mul(r, columns)));
        if (first != 0) {
            total_h = add(total_h, add(int_at(m_, first + 0x18), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_28())));
        }
    }
    total_h = sub(total_h, static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_28()));
    std::int32_t total_w = 0;
    for (std::int32_t c = 0; c < columns; ++c) {
        const std::uint32_t first = word_at(m_, cells + 4 * u(c));
        if (first != 0) {
            total_w = add(total_w, add(int_at(m_, first + 0x14), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_24())));
        }
    }
    total_w = sub(total_w, static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_24()));
    std::int32_t y = static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_20());
    if (static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_30()) == 1) {
        y = sub(y, sar1(total_h));
    }
    std::int32_t start_x = static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_1c());
    if (static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_2c()) == 1) {
        start_x = sub(start_x, sar1(total_w));
    }
    for (std::int32_t r = 0; r < static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8()); ++r) {
        std::int32_t x = start_x;
        for (std::int32_t c = 0; c < static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4()); ++c) {
            const std::uint32_t cell = word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(add(mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4()), r), c)));
            if (cell == 0) {
                continue;
            }
            const std::uint32_t column_ref = word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(c));
            if (int_at(m_, cell + 0x2c) != 1) {
                const std::int32_t cx = add(sub(half(int_at(m_, column_ref + 0x14)), half(int_at(m_, cell + 0x14))), x);
                vcall_item("item_set_position", cell, 1, {u(cx), u(y)});
            } else {
                const std::int32_t cx = sub(x, sar1(int_at(m_, cell + 0x14)));
                vcall_item("item_set_position", cell, 1, {u(cx), u(y)});
            }
            x = add(x, add(int_at(m_, word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(c)) + 0x14), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_24())));
            set_word_at(m_, word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(add(mul(r, static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4())), c))) + 0x28, menu_lists_grid_at(m_, self).field_34());
        }
        const std::uint32_t first = word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4()), r)));
        if (first != 0) {
            y = add(y, add(int_at(m_, first + 0x18), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_28())));
        }
    }
    std::int32_t index = add(mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_14()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4())), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_10()));
    std::int32_t laps = 0;
    while (byte_at(m_, word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(index)) + 0x0c) == 0) {
        index = add(index, 1);
        if (!(index < mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8())))) {
            check(laps == 0, kMenuMsgNoSelectable);
            ++laps;
            index = 0;
        }
    }
    vcall_item("item_highlight", word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(index)), 3, {1});
}

void MenuToolkit::grid_draw(std::uint32_t self) {
    for (std::int32_t i = sub(mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4())), 1); i >= 0; --i) {
        const std::uint32_t cell = word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(i));
        if (cell != 0) {
            vcall_item("item_draw", cell, 2);
        }
    }
    s_.note(kSvcSelectionBox, {0, 0, 0x200, global_word(m_, kScreenFillHeight), 8, 8, 8});
}

void MenuToolkit::grid_snap_all(std::uint32_t self) {
    for (std::int32_t i = sub(mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4())), 1); i >= 0; --i) {
        const std::uint32_t cell = word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(i));
        if (cell != 0) {
            vcall_item("item_snap", cell, 6);
        }
    }
}

void MenuToolkit::grid_navigate(std::uint32_t self, std::uint32_t direction) {
    const std::int32_t previous = add(mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_14())), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_10()));
    vcall_item("item_highlight", word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(previous)), 3, {0});
    vcall_list("grid_v1", self, 1);
    const std::int32_t columns = static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4());
    std::int32_t index = 0;
    for (;;) {
        switch (direction) {
        case 0:
            menu_lists_grid_at(m_, self).set_field_10(static_cast<Word>(u(sub(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_10()), 1))));
            if (static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_10()) < 0) {
                menu_lists_grid_at(m_, self).set_field_10(static_cast<Word>(u(sub(columns, 1))));
            }
            break;
        case 1:
            menu_lists_grid_at(m_, self).set_field_10(static_cast<Word>(u(add(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_10()), 1))));
            if (!(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_10()) < columns)) {
                menu_lists_grid_at(m_, self).set_field_10(0);
            }
            break;
        case 2:
            menu_lists_grid_at(m_, self).set_field_14(static_cast<Word>(u(sub(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_14()), 1))));
            if (static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_14()) < 0) {
                menu_lists_grid_at(m_, self).set_field_14(static_cast<Word>(u(sub(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8()), 1))));
            }
            break;
        case 3:
            menu_lists_grid_at(m_, self).set_field_14(static_cast<Word>(u(add(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_14()), 1))));
            if (!(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_14()) < static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8()))) {
                menu_lists_grid_at(m_, self).set_field_14(0);
            }
            break;
        default:
            break;
        }
        index = add(mul(columns, static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_14())), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_10()));
        if (word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(index)) != 0) {
            break;
        }
    }
    const std::uint32_t cell = word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(index));
    vcall_item("item_highlight", cell, 3, {1});
    if (index != previous) {
        set_word_at(m_, word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(index)) + 0x24, 4);
    }
}

void MenuToolkit::grid_add(std::uint32_t self, std::uint32_t item) {
    check(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_18()) < mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4())), kMenuMsgOutOfMenuSlots);
    set_word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * menu_lists_grid_at(m_, self).field_18(), item);
    menu_lists_grid_at(m_, self).set_field_18(static_cast<Word>(menu_lists_grid_at(m_, self).field_18() + 1));
}

std::uint32_t MenuToolkit::grid_highlighted_id(std::uint32_t self) {
    const std::int32_t cells = mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4()));
    for (std::int32_t i = 0; i < cells; ++i) {
        const std::uint32_t cell = word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(i));
        if (cell != 0 && byte_at(m_, cell + 0x0e) != 0) {
            return word_at(m_, word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(i)) + 8);
        }
    }
    check(false, kMenuMsgNoneHighlighted);
    return 0xffffffffu;
}

void MenuToolkit::grid_highlight(std::uint32_t self, std::uint32_t id) {
    const std::int32_t current = add(mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_14())), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_10()));
    vcall_item("item_highlight", word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(current)), 3, {0});
    const std::int32_t cells = mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4()));
    std::int32_t at = 0;
    for (; at < cells; ++at) {
        const std::uint32_t cell = word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(at));
        if (cell != 0 && word_at(m_, cell + 8) == id) {
            break;
        }
    }
    check(at != mul(static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_8()), static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4())), kMenuMsgItemDoesNotExist);
    const std::int32_t columns = static_cast<std::int32_t>(menu_lists_grid_at(m_, self).field_4());
    menu_lists_grid_at(m_, self).set_field_10(static_cast<Word>(u(at % columns)));
    menu_lists_grid_at(m_, self).set_field_14(static_cast<Word>(u(at / columns)));
    vcall_item("item_highlight", word_at(m_, menu_lists_grid_at(m_, self).field_c() + 4 * u(at)), 3, {1});
}

}  // namespace opentony::runtime::frontend
