#include "auto_views.hpp"
#include "menu_toolkit.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kVtRootClass = 0x005184b4;

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t sub(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }
std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t half(std::int32_t v) { return (v - (v >> 31)) >> 1; }

// NameEntry layout
constexpr std::uint32_t kNeCell = 0xe4;          // 0x40 cell pointers
constexpr std::uint32_t kNeGrid = 0xe0;
constexpr std::uint32_t kNeName = 0xb0;          // the name typed so far (0x20 bytes)
constexpr std::uint32_t kNeCursor = 0xd0;
constexpr std::uint32_t kNeLimit = 0xdc;         // the name length limit plus one
constexpr std::uint32_t kNeProps = 0x1e4;
constexpr std::uint32_t kNeFont = 0x1e8;
constexpr std::uint32_t kNeMinimum = 0x1ec;
constexpr std::uint32_t kNePromptWindows = 0x1f0;   // three prompt windows
constexpr std::uint32_t kNeOwnFont = 0x1fc;
constexpr std::uint32_t kNePrompt = 0x348;
constexpr std::uint32_t kNeBlink = 0x370;

}  // namespace

// ---- NameEntry -------------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::name_entry_construct(std::uint32_t self, std::uint32_t font, std::int32_t limit, std::uint32_t x, std::uint32_t y,
                                                std::uint32_t grid_x, std::uint32_t grid_y, std::uint32_t minimum, std::uint32_t with_space,
                                                std::uint32_t with_digits, std::uint32_t wide, std::uint32_t with_prompts) {
    w_.window_construct(self, 0, 0, -1, -1);
    s_.note(kSvcFontConstruct, {self + kNeOwnFont});
    menu_screens_name_at(m_, self).set_field_d4(static_cast<Word>(x));
    menu_screens_name_at(m_, self).set_field_d8(static_cast<Word>(y));
    menu_screens_name_at(m_, self).set_field_0(static_cast<Word>(kMenuVtNameEntry));
    menu_screens_name_at(m_, self).set_field_ac(0x14);
    check(font != 0, kMenuMsgNoFont);
    menu_screens_name_at(m_, self).set_field_dc(static_cast<Word>(u(add(limit, 1))));
    menu_screens_name_at(m_, self).set_field_1e8(static_cast<Word>(font));
    menu_screens_name_at(m_, self).set_field_1ec(static_cast<Word>(minimum));
    check(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_dc()) <= 0x20, kMenuMsgTooManyChars);
    copy_string(self + kNeName, 0x00534dd4);
    menu_screens_name_at(m_, self).set_field_d0(0);
    for (std::uint32_t i = 0; i < 0x40; ++i) {
        set_word_at(m_, self + kNeCell + 4 * i, 0);
    }
    const std::uint32_t props_memory = operator_new(0x17c);
    std::uint32_t props = 0;
    if (props_memory != 0) {
        props = properties_construct(props_memory);
    }
    menu_screens_name_at(m_, self).set_field_1e4(static_cast<Word>(props));
    copy_font_fields(props + 4, font);
    set_word_at(m_, props + 8, 0);
    set_word_at(m_, props + 0x34, word_at(m_, font + 0x30));
    {
        const std::int32_t scale5 = mul(int_at(m_, font + 0x30), 5);
        set_word_at(m_, props + 0x150, u((add(scale5, (scale5 >> 31) & 3)) >> 2));
    }
    set_word_at(m_, props + 0x154, word_at(m_, font + 0x30));
    std::int32_t columns = 8;
    std::int32_t gap_x = 0x28;
    std::int32_t gap_y = 8;
    if ((wide & 0xff) != 0) {
        columns = 9;
        gap_x = 0x26;
        gap_y = 6;
    }
    const std::int32_t cells = mul(columns, 5);
    const std::uint32_t grid_memory = operator_new(0x38);
    std::uint32_t grid = 0;
    if (grid_memory != 0) {
        grid = grid_construct(grid_memory, u(static_cast<std::int32_t>(grid_x)), u(static_cast<std::int32_t>(grid_y)), columns, 5, u(gap_x), u(gap_y), 1);
    }
    menu_screens_name_at(m_, self).set_field_e0(static_cast<Word>(grid));
    std::int32_t index = 0;
    // one cell: the label is built on the stack in the original, so the port passes it as a value
    auto add_cell = [&](std::uint32_t id, const std::string& label, bool checked, bool advance) {
        if (checked) {
            check(index < 0x40 && index < cells, kMenuMsgTooManyEntries);
        }
        const std::uint32_t memory = operator_new(0x54);
        std::uint32_t item = 0;
        if (memory != 0) {
            item = text_construct_from(memory, id, label, menu_screens_name_at(m_, self).field_1e4());
        }
        set_word_at(m_, self + kNeCell + 4 * u(index), item);
        if (advance) {
            ++index;
        }
        grid_add(menu_screens_name_at(m_, self).field_e0(), item);
    };
    auto character = [](std::uint32_t c) { return c == 0 ? std::string() : std::string(1, static_cast<char>(c)); };
    for (std::int32_t i = 0; i < 0x1a; ++i) {
        add_cell(u(i + 0x41), character(static_cast<std::uint32_t>(i + 0x41)), true, true);
    }
    if ((with_digits & 0xff) != 0) {
        for (std::int32_t d = 0; d < 10; ++d) {
            add_cell(u(d + 0x30), character(static_cast<std::uint32_t>(d + 0x30)), true, true);
        }
    }
    if ((wide & 0xff) != 0) {
        // five punctuation characters read from the constant at 0x00534db0 (four bytes and a word), unchecked
        std::uint8_t chars[5];
        for (std::uint32_t k = 0; k < 5; ++k) {
            chars[k] = byte_at(m_, 0x00534db0 + k);
        }
        for (std::uint32_t k = 0; k < 5; ++k) {
            const std::uint32_t memory = operator_new(0x54);
            std::uint32_t item = 0;
            if (memory != 0) {
                item = text_construct_from(memory, u(static_cast<std::int32_t>(static_cast<std::int8_t>(chars[k]))), character(chars[k]),
                                           menu_screens_name_at(m_, self).field_1e4());
            }
            set_word_at(m_, self + kNeCell + 4 * u(index + static_cast<std::int32_t>(k)), item);
            grid_add(menu_screens_name_at(m_, self).field_e0(), item);
        }
        index += 5;
    }
    if ((with_space & 0xff) != 0) {
        add_cell(0xfffffffdu, m_.cstring(0x00534dac, 0x20), true, true);
    }
    add_cell(0xfffffffeu, m_.cstring(0x0052f65c, 0x20), true, true);
    add_cell(0xffffffffu, m_.cstring(0x0052f674, 0x20), true, false);
    grid_layout(menu_screens_name_at(m_, self).field_e0());
    name_entry_set_name(self, 0x00534dd4);
    name_entry_set_prompt(self, kEmptyText, menu_screens_name_at(m_, self).field_d4(), u(sub(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d8()), 0xf)));
    menu_screens_name_at(m_, self).set_field_370(0);
    menu_screens_name_at(m_, self).set_field_1f0(0);
    set_word_at(m_, self + kNePromptWindows + 4, 0);
    set_word_at(m_, self + kNePromptWindows + 8, 0);
    if ((with_prompts & 0xff) != 0) {
        s_.note(kSvcFontFind, {0x0052544c}, m_.cstring(0x0052544c));
        const std::uint32_t found = static_cast<std::uint32_t>(s_.value(kSvcFontFind));
        copy_font_fields(self + kNeOwnFont, found);
        const std::uint32_t own = self + kNeOwnFont;
        set_word_at(m_, own + 4, 0);
        set_word_at(m_, own + 8, 3);
        f_.set_rgb(own, 0x80, 0x80, 0x80);
        set_byte_at(m_, own + 0x21, 0);
        set_word_at(m_, own + 0x0c, 0);
        set_word_at(m_, own + 0x30, 0x800);
        struct Prompt {
            std::uint32_t file;
            std::uint32_t x;
            std::uint32_t slot;
        };
        for (const Prompt& p : {Prompt{0x00527840, 0xffffff6au, kNePromptWindows}, Prompt{0x00534d9c, 0x190, kNePromptWindows + 4},
                                Prompt{0x00525400, 0x32, kNePromptWindows + 8}}) {
            const std::uint32_t memory = operator_new(0xb0);
            std::uint32_t window = 0;
            if (memory != 0) {
                s_.note(kSvcPromptConstruct, {memory, p.file, 0x005252f0, own, p.x, 0xdc, 0xffffffffu, global_word(m_, kScreenFillHeight) + 0x14}, m_.cstring(p.file));
                window = static_cast<std::uint32_t>(s_.value(kSvcPromptConstruct));
            }
            set_word_at(m_, self + p.slot, window);
            w_.window_add(self, window);
        }
    }
    return self;
}

void MenuToolkit::name_entry_destroy(std::uint32_t self) {
    menu_screens_name_at(m_, self).set_field_0(static_cast<Word>(kMenuVtNameEntry));
    const std::uint32_t props = menu_screens_name_at(m_, self).field_1e4();
    check(props != 0, kMenuMsgNoPropertiesToDelete);
    if (menu_screens_name_at(m_, self).field_1e4() != 0) {
        vcall_item(kVirtualDelete, menu_screens_name_at(m_, self).field_1e4(), 0, {1});
    }
    for (std::uint32_t i = 0; i < 0x40; ++i) {
        const std::uint32_t cell = word_at(m_, self + kNeCell + 4 * i);
        if (cell != 0) {
            vcall_item(kVirtualDelete, cell, 0, {1});
        }
    }
    check(menu_screens_name_at(m_, self).field_e0() != 0, kMenuMsgNoGridToDelete);
    if (menu_screens_name_at(m_, self).field_e0() != 0) {
        vcall_item(kVirtualDelete, menu_screens_name_at(m_, self).field_e0(), 0, {1});
    }
    s_.note(kSvcFontDestroy, {self + kNeOwnFont});
    w_.window_destroy(self);
}

std::uint32_t MenuToolkit::name_entry_delete(std::uint32_t self, std::uint32_t flag) {
    name_entry_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

std::uint32_t MenuToolkit::name_entry_name(std::uint32_t self) const { return self + kNeName; }

void MenuToolkit::name_entry_set_name(std::uint32_t self, std::uint32_t name) {
    check(name != 0, kMenuMsgNoName);
    const std::string value = m_.cstring(name, 0x1000);
    const std::uint32_t length = static_cast<std::uint32_t>(value.size());
    check(length <= 0x20 && length <= menu_screens_name_at(m_, self).field_dc(), kMenuMsgNameTooLongFmt);
    copy_string(self + kNeName, name);
    grid_highlight(menu_screens_name_at(m_, self).field_e0(), 0xffffffffu);
    menu_screens_name_at(m_, self).set_field_d0(static_cast<Word>(length));
    if (static_cast<std::int32_t>(length) >= sub(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_dc()), 1)) {
        menu_screens_name_at(m_, self).set_field_d0(static_cast<Word>(u(add(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_dc()), -2))));
    }
    if (static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d0()) < 0) {
        menu_screens_name_at(m_, self).set_field_d0(0);
    }
}

void MenuToolkit::name_entry_set_prompt(std::uint32_t self, std::uint32_t text, std::uint32_t x, std::uint32_t y) {
    check(text != 0, kMenuMsgNoName);
    check(m_.cstring(text, 0x1000).size() <= 0x20, kMenuMsgNameTooLong);
    copy_string(self + kNePrompt, text);
    menu_screens_name_at(m_, self).set_field_368(static_cast<Word>(x));
    menu_screens_name_at(m_, self).set_field_36c(static_cast<Word>(y));
}

void MenuToolkit::name_entry_draw(std::uint32_t self, std::uint32_t x, std::uint32_t y) {
    const std::int32_t blink_phase = add(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_370()), 1);
    menu_screens_name_at(m_, self).set_field_370(static_cast<Word>(u(blink_phase)));
    if (blink_phase > 0x14) {
        menu_screens_name_at(m_, self).set_field_370(0);
    }
    const bool blink = static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_370()) > 0xa;
    const std::uint32_t font = menu_screens_name_at(m_, self).field_1e8();
    // the font draws through its vtable; the strings of the character cells live on the stack, so only the text is recorded
    s_.note(kFontDrawService, {font, menu_screens_name_at(m_, self).field_368(), menu_screens_name_at(m_, self).field_36c()}, m_.cstring(self + kNePrompt, 0x40));
    const std::int32_t spacing = static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_ac());
    const std::int32_t span = mul(sub(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_dc()), 2), spacing);
    const std::int32_t count = sub(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_dc()), 1);
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint8_t c = byte_at(m_, self + kNeName + u(i));
        if (i <= static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d0())) {
            const std::int32_t cx = sub(add(mul(spacing, i), static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d4())), half(span));
            s_.note(kFontDrawService, {font, u(cx), menu_screens_name_at(m_, self).field_d8()}, c == 0 ? std::string() : std::string(1, static_cast<char>(c)));
        }
        if (i != static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d0()) || blink) {
            const std::int32_t cx = sub(add(mul(spacing, i), static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d4())), half(span));
            s_.note(kFontDrawService, {font, u(cx), u(add(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d8()), 0xd))}, "-");
        }
    }
    grid_draw(menu_screens_name_at(m_, self).field_e0());
    w_.window_update(self);
    w_.window_draw(self, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
}

std::uint32_t MenuToolkit::name_entry_handle_input(std::uint32_t self, std::uint32_t direction) {
    if (direction == 0 || direction == 1 || direction == 2 || direction == 3) {
        sfx(0x11f);
    }
    grid_navigate(menu_screens_name_at(m_, self).field_e0(), direction);
    if (global_byte(m_, kPadPressB) != 0) {
        set_global_byte(m_, kPadPressB, 0);
        const std::uint32_t id = grid_highlighted_id(menu_screens_name_at(m_, self).field_e0());
        bool accept = false;
        if (id == 0xfffffffeu) {
            accept = static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d0()) != 0;
        } else if (id == 0xffffffffu) {
            const std::int32_t cursor = static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d0());
            accept = false;
            if (cursor > 0) {
                for (std::int32_t i = 0; i < cursor; ++i) {
                    if (byte_at(m_, self + kNeName + u(i)) != 0x20) {
                        accept = cursor >= static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_1ec());
                        break;
                    }
                }
            }
        } else {
            accept = static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d0()) < sub(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_dc()), 1);
        }
        if (!accept) {
            sfx(0xbd);
            return 0;
        }
        sfx(0xb4);
        const std::uint32_t second = grid_highlighted_id(menu_screens_name_at(m_, self).field_e0());
        if (second == 0xfffffffeu) {
            std::int32_t cursor = static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d0());
            if (cursor > 0) {
                --cursor;
                menu_screens_name_at(m_, self).set_field_d0(static_cast<Word>(u(cursor)));
            }
            set_byte_at(m_, self + kNeName + menu_screens_name_at(m_, self).field_d0(), 0);
        } else if (second == 0xffffffffu) {
            // trailing blanks are trimmed through a pointer that, in the original, is the cursor field itself (a slip: the
            // byte compared is the low byte of the cursor), so the loop only runs for a cursor of 0x20
            std::uint32_t at = self + kNeCursor;
            while (byte_at(m_, at) == 0x20) {
                set_byte_at(m_, at, 0);
                --at;
                if (static_cast<std::int32_t>(at - self - kNeName) < 0) {
                    break;
                }
            }
            s_.note(kSvcPrintfLog, {0x00534e84});
            return 1;
        } else {
            check(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_d0()) < sub(static_cast<std::int32_t>(menu_screens_name_at(m_, self).field_dc()), 1), kMenuMsgBadIndex);
            const std::uint32_t letter = grid_highlighted_id(menu_screens_name_at(m_, self).field_e0());
            set_byte_at(m_, self + kNeName + menu_screens_name_at(m_, self).field_d0(), letter);
            if (grid_highlighted_id(menu_screens_name_at(m_, self).field_e0()) == 0xfffffffdu) {
                set_byte_at(m_, self + kNeName + menu_screens_name_at(m_, self).field_d0(), 0x20);
            }
            const std::uint32_t next = menu_screens_name_at(m_, self).field_d0() + 1;
            menu_screens_name_at(m_, self).set_field_d0(static_cast<Word>(next));
            set_byte_at(m_, self + kNeName + next, 0);
        }
    }
    if (global_byte(m_, kPadPressA) != 0) {
        sfx(0xb0);
        set_global_byte(m_, kPadPressA, 0);
        return 2;
    }
    return 0;
}

// ---- MenuWrapper -----------------------------------------------------------------------------------------

std::uint32_t MenuToolkit::wrapper_construct(std::uint32_t self, std::uint32_t texts, std::uint32_t ids, std::int32_t count, std::uint32_t font_name,
                                             std::uint32_t scrolling) {
    menu_screens_wrapper_at(m_, self).set_field_0(static_cast<Word>(kMenuVtWrapperSub));
    properties_construct(self + 0x10);
    menu_screens_wrapper_at(m_, self).set_field_0(static_cast<Word>(kMenuVtWrapper));
    std::uint32_t list = 0;
    if ((scrolling & 0xff) == 0) {
        const std::uint32_t memory = operator_new(0x1bc);
        if (memory != 0) {
            list = list_construct(memory, 0, 0, 0x200, 0x100, 1, 1, 4, 0);
        }
        menu_screens_wrapper_at(m_, self).set_field_4(static_cast<Word>(list));
        set_byte_at(m_, list + 4, 1);
    } else {
        const std::uint32_t memory = operator_new(0x350);
        if (memory != 0) {
            list = scroll_construct(memory, 0x50, 0x3c, 0x160, 0x78, 1, 4, 0, 0);
        }
        menu_screens_wrapper_at(m_, self).set_field_4(static_cast<Word>(list));
        set_byte_at(m_, list + 4, 1);
        set_byte_at(m_, menu_screens_wrapper_at(m_, self).field_4() + 0x1b8, 1);
    }
    s_.note(kSvcFontFind, {font_name}, m_.cstring(font_name));
    const std::uint32_t found = static_cast<std::uint32_t>(s_.value(kSvcFontFind));
    copy_font_fields(self + 0x14, found);
    if (m_.cstring(font_name, 0x100) == m_.cstring(0x005287a4, 0x100)) {
        menu_screens_wrapper_at(m_, self).set_field_160(0xdac);
        menu_screens_wrapper_at(m_, self).set_field_164(3000);
    } else {
        menu_screens_wrapper_at(m_, self).set_field_160(0xe5b);
        menu_screens_wrapper_at(m_, self).set_field_164(0xc00);
    }
    menu_screens_wrapper_at(m_, self).set_field_38(2);
    menu_screens_wrapper_at(m_, self).set_field_3c(2);
    check(count <= 0x40, kMenuMsgTooManyItems);
    menu_screens_wrapper_at(m_, self).set_field_28c(static_cast<Word>(u(count)));
    for (std::int32_t i = 0; i < count; ++i) {
        const std::int32_t id = int_at(m_, ids + 4 * u(i));
        std::uint32_t item = 0;
        if (id < 0) {
            const std::uint32_t memory = operator_new(0x58);
            if (memory != 0) {
                item = text_scaled_construct(memory, u(id), word_at(m_, texts + 4 * u(i)), self + 0x10, id == -1 ? 1u : 0u);
            }
        } else {
            const std::uint32_t memory = operator_new(0x54);
            if (memory != 0) {
                item = text_construct(memory, u(id), word_at(m_, texts + 4 * u(i)), self + 0x10);
            }
        }
        set_word_at(m_, self + 0x18c + 4 * u(i), item);
        vcall_list("list_v6", menu_screens_wrapper_at(m_, self).field_4(), 6, {item});
    }
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(menu_screens_wrapper_at(m_, self).field_28c()); ++i) {
        if (int_at(m_, ids + 4 * u(i)) >= 0) {
            list_highlight(menu_screens_wrapper_at(m_, self).field_4(), word_at(m_, ids + 4 * u(i)));
            break;
        }
    }
    menu_screens_wrapper_at(m_, self).set_field_c(0);
    menu_screens_wrapper_at(m_, self).set_field_d(0);
    vcall_list("list_v2", menu_screens_wrapper_at(m_, self).field_4(), 2);
    return self;
}

void MenuToolkit::wrapper_destroy_root(std::uint32_t self) { set_word_at(m_, self, kVtRootClass); }

std::uint32_t MenuToolkit::wrapper_sub_delete(std::uint32_t self, std::uint32_t flag) {
    wrapper_destroy_root(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::wrapper_destroy(std::uint32_t self) {
    menu_screens_wrapper_at(m_, self).set_field_0(static_cast<Word>(kMenuVtWrapper));
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(menu_screens_wrapper_at(m_, self).field_28c()); ++i) {
        const std::uint32_t item = word_at(m_, self + 0x18c + 4 * u(i));
        if (item != 0) {
            vcall_item(kVirtualDelete, item, 0, {1});
        }
    }
    if (menu_screens_wrapper_at(m_, self).field_4() != 0) {
        vcall_list(kVirtualDelete, menu_screens_wrapper_at(m_, self).field_4(), 0, {1});
    }
    s_.note(kSvcFontDestroy, {self + 0x14});
    menu_screens_wrapper_at(m_, self).set_field_10(static_cast<Word>(kVtRootClass));
    menu_screens_wrapper_at(m_, self).set_field_0(static_cast<Word>(kVtRootClass));
}

std::uint32_t MenuToolkit::wrapper_delete(std::uint32_t self, std::uint32_t flag) {
    wrapper_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void MenuToolkit::wrapper_test_complete(std::uint32_t self) {
    s_.note(kSvcPadEdges, {menu_screens_wrapper_at(m_, self).field_c(), menu_screens_wrapper_at(m_, self).field_d(), 6});
    const std::uint32_t edge = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
    if (edge == 2 || edge == 3) {
        sfx(0x90);
    }
    vcall_list("list_v5", menu_screens_wrapper_at(m_, self).field_4(), 5, {edge});
    vcall_list("list_v4", menu_screens_wrapper_at(m_, self).field_4(), 4);
}

std::uint32_t MenuToolkit::wrapper_poll(std::uint32_t self) {
    const std::uint32_t pad = static_cast<std::uint32_t>(menu_screens_wrapper_at(m_, self).field_c()) * kPadStride;
    if (byte_at(m_, kPadPressC + pad) == 0 && byte_at(m_, kPadPressB + pad) == 0) {
        if (byte_at(m_, kPadPressA + pad) == 0) {
            return 0;
        }
        set_byte_at(m_, kPadPressA + pad, 0);
        menu_screens_wrapper_at(m_, self).set_field_8(0xffffffffu);
        sfx(0x93);
        return 1;
    }
    set_byte_at(m_, kPadPressC + pad, 0);
    set_byte_at(m_, kPadPressB + static_cast<std::uint32_t>(menu_screens_wrapper_at(m_, self).field_c()) * kPadStride, 0);
    menu_screens_wrapper_at(m_, self).set_field_8(0xffffffffu);
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(menu_screens_wrapper_at(m_, self).field_28c()); ++i) {
        if (byte_at(m_, word_at(m_, self + 0x18c + 4 * u(i)) + 0x0e) != 0) {
            menu_screens_wrapper_at(m_, self).set_field_8(static_cast<Word>(word_at(m_, word_at(m_, self + 0x18c + 4 * u(i)) + 8)));
            break;
        }
    }
    sfx(0x91);
    return 1;
}

void MenuToolkit::nothing() {}
std::uint32_t MenuToolkit::false_result() { return 0; }

// ---- statics ---------------------------------------------------------------------------------------------

void MenuToolkit::mess_font_construct() { s_.note(kSvcFontConstruct, {0x0056a9a0}); }
void MenuToolkit::mess_font_atexit() { s_.note(kSvcAtexit, {0x004734a0}); }
void MenuToolkit::mess_font_destroy() { s_.note(kSvcFontDestroy, {0x0056a9a0}); }
void MenuToolkit::statics_init() {
    mess_font_construct();
    mess_font_atexit();
}

}  // namespace opentony::runtime::frontend
