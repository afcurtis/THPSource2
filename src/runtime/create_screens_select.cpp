#include "auto_views.hpp"
#include "create_screens.hpp"
#include "seam_addresses_generated.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
// Division by two the way the compiler writes it for a signed value: add the sign bit, then shift.
std::int32_t half(std::int32_t v) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) + (static_cast<std::uint32_t>(v) >> 31)) >> 1;
}

constexpr std::uint32_t kMsgNoBackground = 0x00525470;
constexpr std::uint32_t kMsgNoDialog = 0x00527ca4;
constexpr std::uint32_t kMsgNotLoadMode = 0x005254b0;
constexpr std::uint32_t kMsgNotSaveMode = 0x0052549c;
constexpr std::uint32_t kMsgBadState = 0x00527c8c;
constexpr std::uint32_t kMsgEmpty = 0x0055fe10;

// CreateSelect fields (0x7e4 bytes, vtable 0x00518828)
constexpr std::uint32_t kBackground = 0xac;
constexpr std::uint32_t kFontA = 0xb0;      // mainf: the list rows' font and the style's
constexpr std::uint32_t kFontB = 0x1fc;     // s2font2: the prompt windows' captions
constexpr std::uint32_t kFontC = 0x348;     // s2font1: the warning lines and the dialogs
constexpr std::uint32_t kListA = 0x494;     // the list of the four custom skater slots
constexpr std::uint32_t kRowsA = 0x498;     // its 0x60 row pointers
constexpr std::uint32_t kListB = 0x618;     // the list of the skaters to load
constexpr std::uint32_t kRowsB = 0x61c;     // its 0x60 row pointers
constexpr std::uint32_t kStyleA = 0x79c;    // the rows' style
constexpr std::uint32_t kStyleB = 0x7a0;    // the titles' style
constexpr std::uint32_t kStyleC = 0x7a4;
constexpr std::uint32_t kArrowA = 0x7a8;
constexpr std::uint32_t kArrowB = 0x7ac;
constexpr std::uint32_t kState = 0x7b0;
constexpr std::uint32_t kNextState = 0x7b4;
constexpr std::uint32_t kDialog = 0x7b8;
constexpr std::uint32_t kWarnings = 0x7bc;  // four text elements
constexpr std::uint32_t kImage = 0x7cc;     // the working copy of the save image
constexpr std::uint32_t kPrompts = 0x7d0;   // select, delete, load, accept, back

constexpr std::uint32_t kCustomRecords = 0x0056a488;
constexpr std::uint32_t kCareerImage = 0x00568a68;
constexpr std::uint32_t kCachedDatabase = 0x00561cb4;
constexpr std::uint32_t kPadUpEdge = 0x0056b099;
constexpr std::uint32_t kPadUpEdge2 = 0x0056b0a9;
constexpr std::uint32_t kPadSquareEdge = 0x0056b009;
constexpr std::uint32_t kPadTriangleEdge = 0x0056b019;

constexpr std::uint32_t kFormatSkaterLine = 0x00527cc8;   // "%s %d: %s"
constexpr std::uint32_t kFormatEmptyLine = 0x00527cbc;    // "%s %d: <%s>"
constexpr std::uint32_t kFormatPlain = 0x005256dc;        // "%s"
constexpr std::uint32_t kFormatEmptyPlain = 0x00527cb4;   // "<%s>"
constexpr std::uint32_t kFormatPair = 0x00527cd4;         // "%d %d\n"
constexpr std::uint32_t kPtrSkater = 0x0052e740;          // the text "Skater"
constexpr std::uint32_t kPtrEmptySlot = 0x0052e744;       // "--EMPTY--"
constexpr std::uint32_t kPtrBracketed = 0x0052e738;       // "[%s]."

// The scratch region the screens' stack locals (a formatted line) live in during a replay (outside every region a replay compares).
constexpr std::uint32_t kScratch = 0x7ff00400;

// sprintf for the formats the screen uses: %s takes the next string, %d the next number.
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

// The formatted line goes into the screen's stack buffer.
void CreateScreens::select_write_line(const std::string& text) {
    m_.ensure_region(kScratch, 0x100);
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(kScratch, bytes);
}

// A virtual call on a dialog or a help box: the confirm box and the text box run their own methods; any other object is reported.
void CreateScreens::dialog_call(std::uint32_t dialog, std::uint32_t slot, std::vector<std::uint32_t> args) {
    if (word_at(m_, dialog) == kVtTextBox) {
        if (slot == 0) {
            box_delete(dialog, args.empty() ? 0 : args[0]);
            return;
        }
        if (slot == 1) {
            box_draw(dialog);
            return;
        }
    }
    if (word_at(m_, dialog) == kVtConfirmBox) {
        switch (slot) {
        case 0:
            confirm_delete(dialog, args.empty() ? 0 : args[0]);
            return;
        case 1:
            confirm_draw(dialog);
            return;
        case 2:
            confirm_update(dialog);
            return;
        default:
            break;
        }
    }
    // (a fake object of a probe has the slot 1 and 2 methods of a window child)
    static const char* const kNames[] = {"virtual_delete", "element_update", "element_draw"};
    std::vector<std::uint32_t> call;
    call.push_back(dialog);
    call.insert(call.end(), args.begin(), args.end());
    s_.note(slot < 3 ? kNames[slot] : "dialog_vcall", std::move(call));
}

// 0x0042cd20: the screen table asks each screen for its two callbacks.
void CreateScreens::select_init_callbacks(std::uint32_t record) {
    set_word_at(m_, record, 0x0042cd40);
    set_word_at(m_, record + 0x0c, 0x0042cd50);
}

void CreateScreens::select_exit_callback() {}

void CreateScreens::select_screen_call(std::uint32_t unused, std::uint32_t result) {
    (void)unused;
    set_word_at(m_, result, u(select_run()));
}

// 0x0042cd60: the select window. A buffer holding a copy of the career save image, the background, three fonts, three styles (rows, titles, a third
// with its own colours), two arrow sprites, two lists (the four slots to save into, the skaters to load from) of a title and three rows and a "back"
// row each, the four warning lines and the five prompt windows.
std::uint32_t CreateScreens::select_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontA);
    font_construct(self + kFontB);
    font_construct(self + kFontC);
    create_screens_select_select_at(m_, self).set_field_0(static_cast<Word>(kVtCreateSelect));
    s_.note(kAllocBuffer, {0x1dc4, 0, 1, 0});
    const std::uint32_t image = static_cast<std::uint32_t>(s_.value(kAllocBuffer));
    create_screens_select_select_at(m_, self).set_field_7cc(static_cast<Word>(image));
    s_.note("mem_copy", {image, kCareerImage, 0x1dc4});
    create_screens_select_select_at(m_, self).set_field_7b4(0);
    create_screens_select_select_at(m_, self).set_field_7b0(0);
    create_screens_select_select_at(m_, self).set_field_ac(static_cast<Word>(background_sprite()));
    // the three fonts
    copy_font_named(self + kFontA, 0x00525458);
    set_word_at(m_, self + kFontA + 4, 1);
    set_word_at(m_, self + kFontA + 8, 3);
    f_.set_rgb(self + kFontA, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontA + 0x21, 0);
    set_word_at(m_, self + kFontA + 0xc, 0);
    set_word_at(m_, self + kFontA + 0x10, 0);
    set_word_at(m_, self + kFontA + 0x30, 0xc00);
    copy_font_named(self + kFontB, 0x0052544c);
    set_word_at(m_, self + kFontB + 4, 0);
    set_word_at(m_, self + kFontB + 8, 3);
    set_word_at(m_, self + kFontB + 0x14, 0x80);
    set_word_at(m_, self + kFontB + 0x18, 0x80);
    set_word_at(m_, self + kFontB + 0x1c, 0x80);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x800);
    copy_font_named(self + kFontC, 0x00526acc);
    set_word_at(m_, self + kFontC + 4, 1);
    set_word_at(m_, self + kFontC + 8, 3);
    f_.set_rgb(self + kFontC, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontC + 0x21, 0);
    set_word_at(m_, self + kFontC + 0xc, 0);
    set_word_at(m_, self + kFontC + 0x10, 0);
    set_word_at(m_, self + kFontC + 0x30, 0x1000);
    // the styles
    auto new_properties = [&]() {
        const std::uint32_t memory = operator_new(0x17c);
        return memory != 0 ? menu_.properties_construct(memory) : 0u;
    };
    std::uint32_t style_a = new_properties();
    create_screens_select_select_at(m_, self).set_field_79c(static_cast<Word>(style_a));
    menu_.copy_font_fields(style_a + 4, self + kFontA);
    f_.set_rgb(style_a + 4, 0x80, 0x80, 0x80);
    set_word_at(m_, style_a + 0x34, 0xc00);
    set_word_at(m_, style_a + 0x150, 0xc00);
    set_word_at(m_, style_a + 0x154, 0xc00);
    auto copy_style = [&](std::uint32_t slot) {
        const std::uint32_t style = new_properties();
        set_word_at(m_, self + slot, style);
        const std::uint32_t source = create_screens_select_select_at(m_, self).field_79c();
        menu_.copy_font_fields(style + 4, source + 4);
        for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
            set_word_at(m_, style + offset, word_at(m_, source + offset));
        }
        return style;
    };
    std::uint32_t style_b = copy_style(kStyleB);
    set_word_at(m_, style_b + 0x158, 0x91);
    set_word_at(m_, style_b + 0x15c, 0x12);
    set_word_at(m_, style_b + 0x160, 0xf);
    std::uint32_t style_c = copy_style(kStyleC);
    set_word_at(m_, style_c + 0x158, 0x60);
    set_word_at(m_, style_c + 0x164, 0x40);
    set_word_at(m_, style_c + 0x15c, 0x40);
    set_word_at(m_, style_c + 0x168, 0x40);
    set_word_at(m_, style_c + 0x160, 0x10);
    set_word_at(m_, style_c + 0x16c, 0x40);
    // the arrows
    auto arrow = [&](std::uint32_t slot, std::uint32_t file) {
        const std::uint32_t block = operator_new(0x5c);
        std::uint32_t sprite = 0;
        if (block != 0) {
            s_.note(kSpriteConstruct, {block, file, 1, 0, 0}, m_.cstring(file));
            sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, sprite);
        s_.note(kSpriteInit, {sprite});
    };
    arrow(kArrowA, 0x00527c20);
    arrow(kArrowB, 0x00527c10);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kRowsA + 4 * i, 0);
        set_word_at(m_, self + kRowsB + 4 * i, 0);
    }
    // a list: a scaled title, three rows (the skater slots) and the "back" row; the rows go into the list in order
    auto build_list = [&](std::uint32_t list_slot, std::uint32_t rows, std::uint32_t title) {
        std::uint32_t memory = operator_new(0x1bc);
        std::uint32_t list = 0;
        if (memory != 0) {
            list = menu_.list_construct(memory, 0, 0, 0x200, global_word(m_, kScreenFillHeight), 1, 1, 3, 0);
        }
        set_word_at(m_, self + list_slot, list);
        memory = operator_new(0x58);
        std::uint32_t text = 0;
        if (memory != 0) {
            text = menu_.text_scaled_construct(memory, 0xffffffff, word_at(m_, title), create_screens_select_select_at(m_, self).field_7a0(), 1);
        }
        set_word_at(m_, self + rows, text);
        for (std::uint32_t i = 0; i < 3; ++i) {
            memory = operator_new(0x54);
            text = 0;
            if (memory != 0) {
                text = menu_.text_construct(memory, i, 0x00527c68, create_screens_select_select_at(m_, self).field_79c());
            }
            set_word_at(m_, self + rows + 4 * (1 + i), text);
        }
        memory = operator_new(0x54);
        text = 0;
        if (memory != 0) {
            text = menu_.text_construct(memory, 0xfffffffd, global_word(m_, 0x0052e5f8), create_screens_select_select_at(m_, self).field_79c());
        }
        set_word_at(m_, self + rows + 4 * 4, text);
        for (std::uint32_t i = 0; i < 0x60; ++i) {
            if (word_at(m_, self + rows + 4 * i) != 0) {
                vcall_list("list_v6", word_at(m_, self + list_slot), 6, {word_at(m_, self + rows + 4 * i)});
            }
        }
        vcall_list("list_v1", word_at(m_, self + list_slot), 1, {create_screens_select_select_at(m_, self).field_7a8(), create_screens_select_select_at(m_, self).field_7ac(), 5});
        vcall_list("list_v2", word_at(m_, self + list_slot), 2);
    };
    build_list(kListA, kRowsA, 0x0052e5f4);
    build_list(kListB, kRowsB, 0x0052e73c);
    // the four warning lines
    const std::uint32_t warnings[4] = {0x0052e72c, 0x0052e730, 0x0052e734, 0x0052e738};
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint32_t memory = operator_new(0x30);
        std::uint32_t text = 0;
        if (memory != 0) {
            text = w_.text_construct(memory, word_at(m_, warnings[i]), self + kFontC, 0x100, 0x50 + 0xf * i, -1, -1);
        }
        set_word_at(m_, self + kWarnings + 4 * i, text);
        w_.window_add(self, text);
    }
    // the prompt windows, built in this order: back, delete, load, select, accept
    const struct {
        std::uint32_t file;
        std::int32_t x;
        std::uint32_t slot;
    } prompts[] = {{0x00525400, 0x32, 4}, {0x00527c58, 0x78, 1}, {0x00527c4c, 0xe2, 2}, {0x0052541c, 300, 0}, {0x0052540c, 400, 3}};
    for (const auto& p : prompts) {
        const std::uint32_t window = prompt(p.file, 0x005252f0, self + kFontB, p.x, 0xdc);
        set_word_at(m_, self + kPrompts + 4 * p.slot, window);
        w_.window_add(self, window);
    }
    set_half_at(m_, word_at(m_, self + kPrompts + 4 * 2) + 4, 0x20);
    set_half_at(m_, word_at(m_, self + kPrompts + 4 * 1) + 4, 0x80);
    create_screens_select_select_at(m_, self).set_field_7b8(0);
    return self;
}

std::uint32_t CreateScreens::select_delete(std::uint32_t self, std::uint32_t flag) {
    select_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void CreateScreens::select_destroy(std::uint32_t self) {
    create_screens_select_select_at(m_, self).set_field_0(static_cast<Word>(kVtCreateSelect));
    s_.note(kMemDeleteService, {create_screens_select_select_at(m_, self).field_7cc()});
    check(create_screens_select_select_at(m_, self).field_ac() != 0, kMsgNoBackground);
    const std::uint32_t sprite = create_screens_select_select_at(m_, self).field_ac();
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    for (std::uint32_t slot : {kListA, kListB}) {
        if (word_at(m_, self + slot) != 0) {
            vcall_list("virtual_delete", word_at(m_, self + slot), 0, {1});
        }
    }
    for (std::uint32_t slot : {kStyleA, kStyleB, kStyleC, kArrowA, kArrowB}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kRowsA + 4 * i) != 0) {
            delete_object(word_at(m_, self + kRowsA + 4 * i));
        }
        if (word_at(m_, self + kRowsB + 4 * i) != 0) {
            delete_object(word_at(m_, self + kRowsB + 4 * i));
        }
    }
    if (create_screens_select_select_at(m_, self).field_7b8() != 0) {
        dialog_call(create_screens_select_select_at(m_, self).field_7b8(), 0, {1});
    }
    font_destroy(self + kFontC);
    font_destroy(self + kFontB);
    font_destroy(self + kFontA);
    w_.window_destroy(self);
}

// 0x0042df60: the four warning lines start hidden and the five prompts shown; each state then shows what it needs. The confirm dialog (states 4 and
// 5, and 1 with the skater's name) is drawn by its own method; the memory-card states (2 and 6) draw the card screen instead of the window.
void CreateScreens::select_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    for (std::uint32_t i = 0; i < 4; ++i) {
        set_word_at(m_, word_at(m_, self + kWarnings + 4 * i) + 0x14, 1);
    }
    for (std::uint32_t i = 0; i < 5; ++i) {
        set_word_at(m_, word_at(m_, self + kPrompts + 4 * i) + 0x14, 0);
    }
    const std::uint32_t delete_prompt = word_at(m_, self + kPrompts + 4 * 1);
    const std::uint32_t load_prompt = word_at(m_, self + kPrompts + 4 * 2);
    switch (create_screens_select_select_at(m_, self).field_7b0()) {
    case 1: {
        for (std::uint32_t i = 0; i < 4; ++i) {
            set_word_at(m_, word_at(m_, self + kWarnings + 4 * i) + 0x14, 0);
        }
        const std::uint32_t id = menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494());
        select_write_line(format_text(m_.cstring(global_word(m_, kPtrBracketed), 0x100), {m_.cstring(kCustomRecords + id * 0x80, 0x80)}, {}));
        w_.text_set_string(word_at(m_, self + kWarnings + 4 * 3), kScratch);
        if (create_screens_select_select_at(m_, self).field_7b8() != 0) {
            dialog_call(create_screens_select_select_at(m_, self).field_7b8(), 1);
        }
        set_word_at(m_, delete_prompt + 0x14, 1);
        set_word_at(m_, load_prompt + 0x14, 1);
        w_.window_draw(self, x, y);
        break;
    }
    case 0:
        w_.window_draw(self, x, y);
        break;
    case 2:
    case 6:
        engine(0x0047ace0);
        break;
    case 3:
        set_word_at(m_, delete_prompt + 0x14, 1);
        set_word_at(m_, load_prompt + 0x14, 1);
        w_.window_draw(self, x, y);
        break;
    case 4:
    case 5:
        if (create_screens_select_select_at(m_, self).field_7b8() != 0) {
            dialog_call(create_screens_select_select_at(m_, self).field_7b8(), 1);
        }
        set_word_at(m_, delete_prompt + 0x14, 1);
        set_word_at(m_, load_prompt + 0x14, 1);
        w_.window_draw(self, x, y);
        break;
    default:
        check(false, kMsgBadState);
        break;
    }
    check(create_screens_select_select_at(m_, self).field_ac() != 0, kMsgNoBackground);
    engine(0x004d7990, {create_screens_select_select_at(m_, self).field_ac(), word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
}

// 0x0042e180: a state change clears the pad edges; then per state: the lists' rows get the skaters' names ("Skater 2: Name", or the empty slot's
// text in brackets) and the lists move with the pad, a dialog answers the pad (yes goes on to the card screen or the next state), the card states
// wait for the card.
void CreateScreens::select_update_frame(std::uint32_t self) {
    if (create_screens_select_select_at(m_, self).field_7b0() != create_screens_select_select_at(m_, self).field_7b4()) {
        engine(0x00489670, {0x0056aff8});
        engine(0x00489670, {0x0056b164});
        create_screens_select_select_at(m_, self).set_field_7b0(static_cast<Word>(create_screens_select_select_at(m_, self).field_7b4()));
    }
    auto run_list = [&](std::uint32_t list_slot) {
        const std::uint32_t list = word_at(m_, self + list_slot);
        vcall_list("list_v2", list, 2);
        vcall_list("list_v3", list, 3);
        s_.note(kSvcPadEdges, {0, 0, 6});
        const std::uint32_t edge = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
        vcall_list("list_v5", list, 5, {edge});
        vcall_list("list_v4", list, 4);
        w_.window_update(self);
    };
    auto answer_dialog = [&]() {
        check(create_screens_select_select_at(m_, self).field_7b8() != 0, kMsgNoDialog);
        s_.note(kSvcPadEdges, {0, 0, 6});
        const std::int32_t edge = static_cast<std::int32_t>(s_.value(kSvcPadEdges));
        const std::uint32_t answer = confirm_poll(create_screens_select_select_at(m_, self).field_7b8(), edge, -1);
        dialog_call(create_screens_select_select_at(m_, self).field_7b8(), 2);
        return answer;
    };
    auto drop_dialog = [&]() {
        if (create_screens_select_select_at(m_, self).field_7b8() != 0) {
            dialog_call(create_screens_select_select_at(m_, self).field_7b8(), 0, {1});
        }
        create_screens_select_select_at(m_, self).set_field_7b8(0);
    };
    switch (create_screens_select_select_at(m_, self).field_7b0()) {
    case 0:
    case 3: {
        const bool first = create_screens_select_select_at(m_, self).field_7b0() == 0;
        const std::uint32_t rows = first ? kRowsA : kRowsB;
        for (std::uint32_t i = 0; i < 0x60; ++i) {
            const std::uint32_t row = word_at(m_, self + rows + 4 * i);
            if (row == 0) {
                continue;
            }
            const std::int32_t id = int_at(m_, row + 8);
            if (id == -1 || id == -2 || id == -3) {
                continue;
            }
            const bool used = byte_at(m_, kCustomRecords + 0x26 + u(id) * 0x80) != 0;
            std::string line;
            if (first) {
                const std::string skater = m_.cstring(global_word(m_, kPtrSkater), 0x40);
                line = used ? format_text(m_.cstring(kFormatSkaterLine, 0x20), {skater, m_.cstring(kCustomRecords + u(id) * 0x80, 0x80)}, {id + 1})
                            : format_text(m_.cstring(kFormatEmptyLine, 0x20), {skater, m_.cstring(global_word(m_, kPtrEmptySlot), 0x40)}, {id + 1});
            } else {
                line = used ? format_text(m_.cstring(kFormatPlain, 0x20), {m_.cstring(kCustomRecords + u(id) * 0x80, 0x80)}, {})
                            : format_text(m_.cstring(kFormatEmptyPlain, 0x20), {m_.cstring(global_word(m_, kPtrEmptySlot), 0x40)}, {});
            }
            select_write_line(line);
            if (first) {
                f_.ellipsize(create_screens_select_select_at(m_, self).field_79c() + 4, kScratch, 0x1e0);
            }
            vcall_item("item_set_text", row, 8, {kScratch});
        }
        run_list(first ? kListA : kListB);
        return;
    }
    case 1: {
        const std::uint32_t answer = answer_dialog();
        if (answer == 1) {
            drop_dialog();
            engine(0x0047a6c0, {1, 0, 1});
            create_screens_select_select_at(m_, self).set_field_7b4(2);
            w_.window_update(self);
            return;
        }
        if (answer == 2) {
            drop_dialog();
            create_screens_select_select_at(m_, self).set_field_7b4(0);
        }
        w_.window_update(self);
        return;
    }
    case 2:
    case 6:
        return;
    case 4:
    case 5: {
        const bool deleting = create_screens_select_select_at(m_, self).field_7b0() == 4;
        const std::uint32_t answer = answer_dialog();
        if (answer != 1 && answer != 2) {
            w_.window_update(self);
            return;
        }
        drop_dialog();
        if (answer == 1) {
            if (deleting) {
                const std::uint32_t memory = operator_new(0x140c);
                std::uint32_t manager = 0;
                if (memory != 0) {
                    manager = engine_value(0x004bb4f0, {memory});
                }
                check(global_word(m_, kCachedDatabase) != 0, kMsgEmpty);
                engine(0x00422f20, {menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494()), global_word(m_, kCachedDatabase), manager});
                if (manager != 0) {
                    delete_object(manager);
                }
                check(create_screens_select_select_at(m_, self).field_7b8() == 0, kMsgEmpty);
                const std::uint32_t block = operator_new(0x428);
                std::uint32_t dialog = 0;
                if (block != 0) {
                    dialog = confirm_construct(block, self + kFontC, global_word(m_, 0x0052e39c), 0x100, half(global_int(m_, kScreenFillHeight)));
                }
                create_screens_select_select_at(m_, self).set_field_7b8(static_cast<Word>(dialog));
                sk_.select_character_checked(0, 0);
                s_.note("mem_copy", {create_screens_select_select_at(m_, self).field_7cc(), kCareerImage, 0x1dc4});
                create_screens_select_select_at(m_, self).set_field_7b4(5);
                w_.window_update(self);
                return;
            }
            engine(0x0047a0b0, {0x12, 0x12, 1});
            create_screens_select_select_at(m_, self).set_field_7b4(6);
            w_.window_update(self);
            return;
        }
        create_screens_select_select_at(m_, self).set_field_7b4(0);
        w_.window_update(self);
        return;
    }
    default:
        check(false, kMsgBadState);
        return;
    }
}

// 0x0042e7d0: load mode: the card screen answers 1 (a skater was chosen: on to the list of skaters) or 2 (back to the slots).
std::uint32_t CreateScreens::select_update_load(std::uint32_t self) {
    check(create_screens_select_select_at(m_, self).field_7b0() == 2, kMsgNotLoadMode);
    const std::uint32_t answer = engine_value(0x0047ac60);
    if (answer == 1) {
        engine(0x0047ae10, {1});
        create_screens_select_select_at(m_, self).set_field_7b4(3);
        return 0;
    }
    if (answer == 2) {
        engine(0x0047ae10, {2});
        create_screens_select_select_at(m_, self).set_field_7b4(0);
    }
    return 0;
}

// 0x0042e860: save mode: the card screen answers 1 (back to the slots) or 2 (the delete went through: the message box).
std::uint32_t CreateScreens::select_update_save(std::uint32_t self) {
    check(create_screens_select_select_at(m_, self).field_7b0() == 6, kMsgNotSaveMode);
    const std::uint32_t answer = engine_value(0x0047ac60);
    if (answer == 1) {
        engine(0x0047ae10, {1});
        create_screens_select_select_at(m_, self).set_field_7b4(0);
        return 0;
    }
    if (answer == 2) {
        engine(0x0047ae10, {2});
        check(create_screens_select_select_at(m_, self).field_7b8() == 0, kMsgEmpty);
        const std::uint32_t block = operator_new(0x428);
        std::uint32_t dialog = 0;
        if (block != 0) {
            dialog = confirm_construct(block, self + kFontC, global_word(m_, 0x0052e39c), 0x100, half(global_int(m_, kScreenFillHeight)));
        }
        create_screens_select_select_at(m_, self).set_field_7b8(static_cast<Word>(dialog));
        create_screens_select_select_at(m_, self).set_field_7b4(5);
    }
    return 0;
}

// 0x0042e9c0: the pad: the sound for a move, then per state. State 0 (the slots): confirm picks the slot (or leaves for the back row), square asks
// whether to delete an occupied one, triangle loads into it (an empty slot goes to the card at once, an occupied one asks first), back leaves.
// State 3 (the skaters to load): confirm copies the chosen skater into the slot. 2 and 6 are the card states.
std::uint32_t CreateScreens::select_update(std::uint32_t self) {
    if (global_byte(m_, kPadUpEdge) != 0 || global_byte(m_, kPadUpEdge2) != 0) {
        sfx(0x90);
    }
    auto used_slot = [&](std::uint32_t id) { return byte_at(m_, kCustomRecords + 0x26 + id * 0x80) != 0; };
    auto name_of = [&](std::uint32_t id) { return m_.cstring(kCustomRecords + id * 0x80, 0x80); };
    auto open_dialog = [&](const std::string& text, std::int32_t y) {
        select_write_line(text);
        const std::uint32_t block = operator_new(0x428);
        std::uint32_t dialog = 0;
        if (block != 0) {
            dialog = confirm_construct(block, self + kFontC, kScratch, 0x100, y);
        }
        create_screens_select_select_at(m_, self).set_field_7b8(static_cast<Word>(dialog));
    };
    switch (create_screens_select_select_at(m_, self).field_7b0()) {
    case 0: {
        if (global_byte(m_, kPadConfirmEdge) != 0) {
            sfx(0x91);
            set_global_byte(m_, kPadConfirmEdge, 0);
            if (static_cast<std::int32_t>(menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494())) == -3) {
                return 2;
            }
            sk_.set_custom_slot(0, static_cast<int>(menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494())));
            return 1;
        }
        if (global_byte(m_, kPadSquareEdge) != 0) {
            set_global_byte(m_, kPadSquareEdge, 0);
            if (static_cast<std::int32_t>(menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494())) == -3) {
                sfx(0x92);
                return 0;
            }
            const std::uint32_t id = menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494());
            if (!used_slot(id)) {
                sfx(0x92);
                return 0;
            }
            sfx(0x91);
            create_screens_select_select_at(m_, self).set_field_7b4(4);
            check(create_screens_select_select_at(m_, self).field_7b8() == 0, kMsgNoDialog);
            open_dialog(format_text(m_.cstring(global_word(m_, 0x0052e728), 0x100), {name_of(menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494()))}, {}),
                        half(global_int(m_, kScreenFillHeight)));
            return 0;
        }
        if (global_byte(m_, kPadTriangleEdge) != 0) {
            set_global_byte(m_, kPadTriangleEdge, 0);
            if (static_cast<std::int32_t>(menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494())) == -3) {
                sfx(0x92);
                return 0;
            }
            sfx(0x91);
            const std::uint32_t id = menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494());
            if (!used_slot(id)) {
                engine(0x0047a6c0, {1, 0, 1});
                create_screens_select_select_at(m_, self).set_field_7b4(2);
                return 0;
            }
            create_screens_select_select_at(m_, self).set_field_7b4(1);
            check(create_screens_select_select_at(m_, self).field_7b8() == 0, kMsgNoDialog);
            open_dialog(format_text(m_.cstring(global_word(m_, 0x0052e720), 0x100), {name_of(menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494()))}, {}),
                        half(global_int(m_, kScreenFillHeight)) + 0x14);
            return 0;
        }
        if (global_byte(m_, kPadBackEdge) != 0) {
            sfx(0x93);
            set_global_byte(m_, kPadBackEdge, 0);
            return 2;
        }
        return 0;
    }
    case 2:
        return select_update_load(self);
    case 3: {
        if (global_byte(m_, kPadConfirmEdge) != 0) {
            set_global_byte(m_, kPadConfirmEdge, 0);
            if (static_cast<std::int32_t>(menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_618())) == -3) {
                sfx(0x91);
                engine(0x0047a6c0, {1, 0, 1});
                engine(0x0042ef30, {self});
                create_screens_select_select_at(m_, self).set_field_7b4(2);
            } else {
                const std::uint32_t first = menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494());
                const std::uint32_t second = menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_618());
                (void)second;
                (void)first;
                s_.note(kSvcPrintfLog, {kFormatPair});     // (the probe's stub takes the format only)
                const std::uint32_t id = menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_618());
                if (!used_slot(id)) {
                    sfx(0x92);
                } else {
                    sfx(0x91);
                    const std::uint32_t to = menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494());
                    const std::uint32_t from = menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_618());
                    engine(0x0042ef50, {self, from, to});
                    open_dialog(format_text(m_.cstring(global_word(m_, 0x0052e3a0), 0x100), {name_of(menu_.list_highlighted_id(create_screens_select_select_at(m_, self).field_494()))}, {}),
                                half(global_int(m_, kScreenFillHeight)));
                    create_screens_select_select_at(m_, self).set_field_7b4(5);
                }
            }
        }
        if (global_byte(m_, kPadBackEdge) != 0) {
            sfx(0x93);
            set_global_byte(m_, kPadBackEdge, 0);
            engine(0x0047a6c0, {1, 0, 1});
            engine(0x0042ef30, {self});
            create_screens_select_select_at(m_, self).set_field_7b4(2);
        }
        return 0;
    }
    case 6:
        return select_update_save(self);
    case 1:
    case 4:
    case 5:
        return 0;
    default:
        check(false, kMsgBadState);
        return 0;
    }
}

// 0x0042f100: the skater select screen: the four fonts, the window, frames until the update answers 1 or 2 (2 leaves without the fade), then the
// pads are cleared and the fonts unloaded.
std::int32_t CreateScreens::select_run() {
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    for (std::uint32_t font : {0x00525458, 0x0052544c, 0x00526acc, 0x005254e0}) {
        font_load(font);
    }
    const std::uint32_t memory = operator_new(0x7e4);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = select_construct(memory);
    }
    engine(0x00489670, {0x0056aff8});
    engine(0x00489670, {0x0056b164});
    const std::int32_t result = frame_loop(
        [&] { return static_cast<std::int32_t>(seams_ ? engine_value(0x0042e9c0, {window}) : select_update(window)); },
        [&] {
            if (seams_) {
                engine(0x0042e180, {window});
            } else {
                select_update_frame(window);
            }
        },
        [&] {
            if (seams_) {
                engine(0x0042df60, {window, 0, 0});
            } else {
                select_draw(window, 0, 0);
            }
        });
    if (result != 2) {
        engine(0x004304b0);
        engine(0x004c8f10, {0xec, 1});
    }
    engine(0x00489670, {0x0056aff8});
    engine(0x00489670, {0x0056b164});
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (window != 0) {
        if (seams_) {
            engine(0x0042dda0, {window, 1});
        } else {
            select_delete(window, 1);
        }
    }
    for (std::uint32_t font : {0x00525458, 0x0052544c, 0x00526acc, 0x005254e0}) {
        font_unload(font);
    }
    engine(0x0046f480);
    return result != 2 ? 0 : -1;
}

}  // namespace opentony::runtime::frontend
