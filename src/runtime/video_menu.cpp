#include "auto_views.hpp"
#include "options.hpp"
#include "seam_addresses_generated.hpp"

#include <cstring>

// Phase 4.15b: the video options screen of vidmenu.cpp (0x004cdf60 .. 0x004ce2a0 and 0x004cea50), the part of it the options screen's loop does not own: the start-up tables of
// its two option lists, the loop that runs it, the gamma call and the constructor that builds its window (a background picture, four sprite halves, two arrows, two fonts,
// three prompt windows and a four-row list). Its destructor (0x004cea70) is OptionsScreen::video_menu_destroy; the window's own methods (0x004cebb0 set an option, 0x004cec70
// draw, 0x004ced80 the mode names, 0x004cefb0 the pad poll) belong to PcSystem and are services here.

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

// the window's fields
constexpr Word kDone = 0xe4;               // byte: the window is finished
constexpr Word kPie = 0xb0;                // an engine object (destroyed by 0x00454970)
constexpr Word kMainFont = 0xe8;           // the Font of the rows
constexpr Word kSmallFont = 0x23c;         // the Font of the prompt captions
constexpr Word kBackground = 0x238;
constexpr Word kList = 0x388;
constexpr Word kRows = 0x38c;              // four rows
constexpr Word kArrowLeft = 0x3b4;
constexpr Word kArrowRight = 0x3b8;
constexpr Word kStyle = 0x3b0;             // the rows' MenuProperties
constexpr Word kVtVideoMenu = 0x00519914;
constexpr Word kSize = 0x3cc;

// the screen's option records: three words each (the text, ...), starting at 0x0056e7c4
constexpr Word kOptionRecords = 0x0056e7c4;
constexpr Word kOptionRecordsEnd = 0x0056e7f4;

constexpr Word kGamma = 0x0056e7d8;
constexpr Word kMenuFlag = 0x0056e7b8;
constexpr Word kSoftware = 0x00548568;
constexpr Word kRenderer = 0x0056e7c0;
constexpr Word kRendererChoice = 0x0056e7e4;

constexpr Word kPad0 = 0x0056aff8;
constexpr Word kPad1 = 0x0056b164;

// where the port keeps the stack local the pad poll writes its answer to
constexpr Word kChoice = kScratchLine;

float float_at(RetailMemory& m, Word address) {
    const Word bits = guest::read_scalar<std::uint32_t>(m, address);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

}  // namespace

// 0x004cdf70 (jump thunk 0x004cdf60): the current mode and its column from the defaults, the gamma step cleared.
void OptionsScreen::video_menu_init_state() {
    const Word mode = global_word(m_, 0x0052ea20);
    const Word column = global_word(m_, 0x0052e498);
    set_global_word(m_, 0x0056e7ac, mode);
    set_global_word(m_, 0x0056e7b0, column);
    set_global_word(m_, 0x0056e7b4, 0);
}

// 0x004cdfb0 (jump thunk 0x004cdfa0): the option records (renderer, resolution, gamma, back) from the defaults: the text, the value, and where the value is kept.
void OptionsScreen::video_menu_init_options() {
    const Word renderer_text = global_word(m_, 0x0052ea2c);
    const Word resolution_text = global_word(m_, 0x0052ea30);
    const Word gamma_text = global_word(m_, 0x0052ea34);
    set_global_word(m_, 0x0056e7c4, renderer_text);
    set_global_word(m_, 0x0056e7c8, 0x00548564);
    set_global_word(m_, 0x0056e7cc, 0);
    set_global_word(m_, 0x0056e7d8, 0);
    set_global_word(m_, 0x0056e7e4, 0);
    const Word back_text = global_word(m_, 0x0052eb10);
    set_global_word(m_, 0x0056e7d0, resolution_text);
    set_global_word(m_, 0x0056e7d4, 0x0056e788);
    set_global_word(m_, 0x0056e7dc, gamma_text);
    set_global_word(m_, 0x0056e7e0, 0x00548570);
    set_global_word(m_, 0x0056e7e8, back_text);
    set_global_word(m_, 0x0056e7ec, 0x0056e7ac);
}

// 0x004ce020: `*out = run()` (the first argument is not used).
void OptionsScreen::video_menu_run_into(Word out) { set_word_at(m_, out, static_cast<Word>(video_menu_run())); }

// 0x004ce260: the gamma step as a float (step * scale + offset, at the game's 53-bit precision, rounded to a float by the store) handed to the renderer.
void OptionsScreen::video_menu_apply_gamma() {
    const double product = static_cast<double>(global_int(m_, kGamma)) * static_cast<double>(float_at(m_, 0x00519910));
    const float level = static_cast<float>(product + static_cast<double>(float_at(m_, 0x0051890c)));
    Word bits;
    std::memcpy(&bits, &level, sizeof bits);
    engine(0x004d4120, {bits});
}

// 0x004ce290
Word OptionsScreen::video_menu_is_done(Word self) { return byte_at(m_, self + kDone) != 0 ? 1u : 0u; }

// 0x004ce030: the video options screen: the front-end flag cleared and the renderer state refreshed, the window built, frames until the window is done (the pad's answer is noted, the
// window updated and drawn, the clock waited for), then the window gone; an answer other than -1 applies the choice (the renderer is switched), -1 puts the old settings back.
std::int32_t OptionsScreen::video_menu_run() {
    const Word saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    engine(0x004cf030);
    set_global_byte(m_, kMenuFlag, 1);
    if ((engine_value(seam::hal::FUN_004f2700) & 0xff) == 0) {
        set_global_word(m_, kSoftware, 0);
        set_global_word(m_, kRenderer, 0);
    }
    const Word memory = operator_new(kSize);
    Word menu = 0;
    if (memory != 0) {
        menu = seams_ ? engine_value(0x004ce2a0, {memory}) : video_menu_construct(memory);
    }
    engine(0x00489670, {kPad0});
    engine(0x00489670, {kPad1});
    m_.ensure_region(kChoice, 0x100);          // (the loop's stack local)
    set_global_word(m_, kChoice, 0xffffffffu);
    bool pressed = false;
    for (;;) {
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x00418150);
        engine(0x004304b0);
        engine(0x00466be0);
        engine(0x00489a10);
        if (pressed) {
            engine(0x00489670, {kPad0});
            engine(0x00489670, {kPad1});
        }
        // the pad poll stores -1, 0 or +1 in the local and answers whether a button was pressed (the stub's poke is scripted first, then its answer)
        // (live, the poll stored the choice itself and the one value is the answer; a replay scripts the store as a value first)
        engine(0x004cefb0, {menu, kChoice});
        if (!s_.live()) {
            set_global_word(m_, kChoice, static_cast<Word>(s_.value(engine_name(0x004cefb0))));
        }
        if ((static_cast<Word>(s_.value(engine_name(0x004cefb0))) & 0xff) != 0) {
            pressed = true;
            engine(0x004ce280, {menu, global_word(m_, kChoice)});
        }
        // the window's update decides whether the screen is finished: it sets the done flag (the stub's write is scripted, as the clock's is)
        vcall_item("element_update", menu, 1);
        set_byte_at(m_, menu + kDone, static_cast<Word>(s_.value("element_update")) & 0xff);
        if (video_menu_is_done(menu) != 0) {
            break;
        }
        engine(0x004cec70, {menu});
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        if (global_word(m_, kGlobalSimTick) < global_word(m_, kGlobalLastTick) + 2) {
            do {
                game_frame_tick();
            } while (global_word(m_, kGlobalSimTick) < global_word(m_, kGlobalLastTick) + 2);
        }
        set_global_word(m_, kGlobalLastTick, global_word(m_, kGlobalSimTick));
        engine(0x00430510);
    }
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (menu != 0) {
        delete_object(menu);
    }
    const std::int32_t choice = global_int(m_, kChoice);
    if (choice != -1) {
        s_.note(kSvcPrintfLog, {0x00548618});
        engine(0x004cf300);
        engine(0x004dbe60);
        engine(seam::hal::WINMAIN_SwitchRenderer, {global_word(m_, kRenderer) == 0 ? 4u : 8u, global_word(m_, kRendererChoice)});
        engine(0x004dbd90);
        return choice;
    }
    s_.note(kSvcPrintfLog, {0x005485f4});
    engine(0x004cf030);
    return choice;
}

// 0x004ce2a0: the window: the window base, the engine object and two fonts, the background picture, four sprite halves, two arrows, the rows' and the captions' fonts, three prompt windows,
// the rows' style, the mode names, the list with its four rows (renderer, resolution, gamma, back) and the rows' places.
Word OptionsScreen::video_menu_construct(Word self) {
    w_.window_construct(self, 0, 0, -1, -1);
    engine(0x00454930, {self + kPie});
    font_construct(self + kMainFont);
    font_construct(self + kSmallFont);
    set_word_at(m_, self, kVtVideoMenu);
    set_word_at(m_, self + kBackground, background_sprite(0x00548688));
    // the four halves of the picture
    const struct {
        Word name;
        Word x;
        Word y;
        Word x2;
        Word y2;
        Word slot;
    } halves[] = {{0x00548674, 0x48, 0x32, 0xfffffe0c, 0x32, 0x3bc},
                  {0x00548660, 0x115, 0x32, 0xfffffed9, 0x32, 0x3c0},
                  {0x0054864c, 0x48, 0x71, 0xfffffe0c, 0x71, 0x3c4},
                  {0x00548638, 0x115, 0x71, 0xfffffed9, 0x71, 0x3c8}};
    for (const auto& half : halves) {
        const Word block = operator_new(0x2c);
        Word element = 0;
        if (block != 0) {
            element = engine_value(0x004acbe0, {block, half.name, half.x, half.y, half.x2, half.y2, 0, 0}, m_.cstring(half.name));
        }
        set_word_at(m_, self + half.slot, element);
        w_.window_add(self, element);
    }
    // the two arrows (+0xa is the direction, +0xb is set on both)
    auto arrow = [&](Word slot, Word file, Word direction) {
        const Word block = operator_new(0x5c);
        Word made = 0;
        if (block != 0) {
            s_.note(kSpriteConstruct, {block, file, 1, 0, 0}, m_.cstring(file));
            made = static_cast<Word>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, made);
        set_byte_at(m_, made + 0xa, direction);
        set_byte_at(m_, word_at(m_, self + slot) + 0xb, 1);
    };
    arrow(kArrowLeft, 0x00525908, 2);
    arrow(kArrowRight, 0x005258f8, 0);
    // the rows' font: mainf, scaled up and bright
    s_.note(kSvcFontLoad, {0x00525458}, m_.cstring(0x00525458, 0x40));
    Word record = static_cast<Word>(s_.value(kSvcFontLoad));
    menu_.copy_font_fields(self + kMainFont, record);
    set_word_at(m_, self + 0xec, 1);
    set_word_at(m_, self + 0xf0, 1);
    set_word_at(m_, self + 0x118, 0x800);
    set_word_at(m_, self + 0xfc, 0x40);
    set_word_at(m_, self + 0x100, 0x40);
    set_word_at(m_, self + 0x104, 0xff);
    set_byte_at(m_, self + kDone, 0);
    // the captions' font: s2font2, grey
    s_.note(kSvcFontLoad, {0x0052544c}, m_.cstring(0x0052544c, 0x40));
    record = static_cast<Word>(s_.value(kSvcFontLoad));
    menu_.copy_font_fields(self + kSmallFont, record);
    set_word_at(m_, self + 0x240, 0);
    set_word_at(m_, self + 0x244, 3);
    f_.set_rgb(self + kSmallFont, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + 0x25d, 0);
    set_word_at(m_, self + 0x248, 0);
    set_word_at(m_, self + 0x24c, 0);
    set_word_at(m_, self + 0x26c, 0x1000);
    // the prompt windows: back, select, accept
    const struct {
        Word file;
        std::int32_t x;
    } prompts[] = {{0x00525400, 0x32}, {0x00535ca0, 0xcf}, {0x00535c90, 0x190}};
    for (const auto& entry : prompts) {
        w_.window_add(self, prompt(entry.file, 0x005252f0, self + kSmallFont, entry.x, 0xdc));
    }
    // the rows' style: the main font with its colour and the scale of the list
    const Word style_block = operator_new(0x17c);
    const Word style = style_block != 0 ? menu_.properties_construct(style_block) : 0u;
    set_word_at(m_, self + kStyle, style);
    menu_.copy_font_fields(style + 4, self + kMainFont);
    set_word_at(m_, style + 8, 0);
    set_word_at(m_, style + 0xc, 3);
    set_word_at(m_, style + 0x18, 0xff);
    set_word_at(m_, style + 0x1c, 0x60);
    set_word_at(m_, style + 0x20, 0x20);
    set_byte_at(m_, style + 0x25, 0);
    set_word_at(m_, style + 0x10, 0);
    set_word_at(m_, style + 0x150, 0x898);
    set_word_at(m_, style + 0x154, 0x800);
    engine(0x004ced80, {self, 1});
    // the list and its four rows
    const Word list_block = operator_new(0x1bc);
    Word list = 0;
    if (list_block != 0) {
        list = menu_.list_construct(list_block, 0x56, 0x42, 0x1f4, 0xf0, 0, 0, 3, 0);
    }
    set_word_at(m_, self + kList, list);
    Word index = 0;
    for (Word address = kOptionRecords; address < kOptionRecordsEnd; address += 0xc, ++index) {
        const Word row_block = operator_new(0x100);
        Word row = 0;
        if (row_block != 0) {
            row = menu_.row_construct(row_block, index, word_at(m_, address), word_at(m_, self + kStyle), 2, word_at(m_, self + kArrowLeft), word_at(m_, self + kArrowRight));
        }
        set_word_at(m_, self + kRows + 4 * index, row);
        if (row != 0) {
            vcall_list("list_v6", word_at(m_, self + kList), 6, {row});
            set_half_at(m_, word_at(m_, self + kRows + 4 * index) + 4, 0x2000);
        }
        engine(0x004cebb0, {self, index, 0, 0xffff});
    }
    vcall_list("list_v2", word_at(m_, self + kList), 2);
    set_word_at(m_, word_at(m_, self + kList) + 0x1a8, 100);
    index = 0;
    for (Word y = 0x69; y < 0x99; y += 0xc, ++index) {
        if (y >= 0x81) {
            vcall_item("item_set_position", word_at(m_, self + kRows + 4 * index), 1, {0x56, y});
        }
    }
    engine(0x004ced80, {self, 1});
    return self;
}

// 0x004cea50: the window's destructor, then its block when the flag says so; answers the window.
Word OptionsScreen::video_menu_delete(Word self, Word flag) {
    video_menu_destroy(self);
    if ((flag & 1u) != 0) {
        operator_delete(self);
    }
    return self;
}

}  // namespace opentony::runtime::frontend
