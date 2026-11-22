#include "auto_views.hpp"
#include "multi_select.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// Window fields
constexpr std::uint32_t kBackground = 0xac;
constexpr std::uint32_t kFont = 0xb0;        // s2font2: the prompt windows' captions
constexpr std::uint32_t kList = 0x1fc;
constexpr std::uint32_t kStyle = 0x200;      // the rows' style (mainf)
constexpr std::uint32_t kTitleStyle = 0x204; // the title's: the same with its own colour
constexpr std::uint32_t kRows = 0x208;       // ten slots
constexpr std::uint32_t kArrowA = 0x230;
constexpr std::uint32_t kArrowB = 0x234;

constexpr std::uint32_t kPad0 = 0x0056aff8;
constexpr std::uint32_t kPad1 = 0x0056b164;
constexpr std::uint32_t kPreManager = 0x0056b834;

constexpr std::uint32_t kMsgNoBackground = 0x00525470;
constexpr std::uint32_t kMainFont = 0x00525458;      // "mainf.fnt"
constexpr std::uint32_t kS2Font2 = 0x0052544c;       // "s2font2.fnt"
constexpr std::uint32_t kPreName = 0x005358c4;       // "multisel.pre"

// the rows: the id the screen answers with, and the pointer variable of the text
struct Row {
    std::uint32_t id;
    std::uint32_t text;
};
constexpr Row kGames[5] = {{5, 0x0052e74c}, {8, 0x0052e750}, {7, 0x0052e754}, {9, 0x0052e758}, {4, 0x0052e75c}};   // Trick attack, Graffiti, Horse, Tag, Free Skate

}  // namespace

// ---- the screen callbacks and the loop --------------------------------------------------------------------------------------------------------------

// 0x0047ba30: the screen table asks each screen for its two callbacks.
void MultiSelect::init_callbacks(std::uint32_t record) {
    set_word_at(m_, record, 0x0047ba50);
    set_word_at(m_, record + 0x0c, 0x0047ba60);
}

void MultiSelect::exit_callback() {}

// 0x0047ba60: `*result = run()`.
void MultiSelect::screen_call(std::uint32_t argument, std::uint32_t result) {
    (void)argument;
    set_word_at(m_, result, u(run()));
}

// 0x0047c560: the multi-player select screen: two fonts, the window, frames until the poll answers 1 (a game chosen: its id goes to 0x00561c6c) or 2 (back: 2 goes
// there) or the engine asks to leave. Answers 0, or -1 when the player backed out. (A frame waits for the clock with a bare spin on it, which a forced call cannot
// satisfy: the replays end the loop on the first frame.)
std::int32_t MultiSelect::run() {
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    font_load(kMainFont);
    font_load(kS2Font2);
    const std::uint32_t memory = operator_new(0x238);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = construct(memory);
    }
    engine(0x00489670, {kPad0});
    engine(0x00489670, {kPad1});
    std::int32_t step = 0;
    for (;;) {
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x00418150);
        set_global_word(m_, kGlobalLastTick, global_word(m_, kGlobalSimTick));
        engine(0x004304b0);
        engine(0x00489a10);
        step = static_cast<std::int32_t>(seams_ ? engine_value(0x0047c4a0) : poll());
        if (step == 1) {
            set_global_word(m_, kMultiAnswer, seams_ ? engine_value(0x0047c550, {window}) : selected_id(window));
            break;
        }
        if (step == 2) {
            set_global_word(m_, kMultiAnswer, 2);
            break;
        }
        if ((engine_value(0x00452d30) & 0xff) != 0) {
            break;
        }
        if (seams_) {
            engine(0x0047c460, {window});
            engine(0x0047c3e0, {window, 0, 0});
        } else {
            update(window);
            draw(window, 0, 0);
        }
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        s_.note("clock_wait", {});
        set_global_word(m_, kGlobalSimTick, static_cast<std::uint32_t>(s_.value("clock_wait")));
        engine(0x00430510);
        engine(0x00466c00);
    }
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (window != 0) {
        if (seams_) {
            engine(0x0047c290, {window, 1});
        } else {
            scalar_delete(window, 1);
        }
    }
    font_unload(kMainFont);
    font_unload(kS2Font2);
    return step != 2 ? 0 : -1;
}

// ---- the window ---------------------------------------------------------------------------------------------------------------------------------------

// 0x0047ba70: the window base, a font, the pre file, the background, the list (a style copied from mainf and a second with its own colour for the title, the title and
// five rows, two arrows) and three prompt windows; the pre file is unloaded again at the end.
std::uint32_t MultiSelect::construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFont);
    set_word_at(m_, self, kVtMultiSelect);
    engine(0x004a90f0, {global_word(m_, kPreManager), kPreName, 0});
    set_word_at(m_, self + kBackground, background_sprite());
    copy_font_named(self + kFont, kS2Font2);
    set_word_at(m_, self + kFont + 4, 0);
    set_word_at(m_, self + kFont + 0x14, 0x80);
    set_word_at(m_, self + kFont + 0x18, 0x80);
    set_word_at(m_, self + kFont + 0x1c, 0x80);
    set_byte_at(m_, self + kFont + 0x21, 0);
    set_word_at(m_, self + kFont + 0xc, 0);
    set_word_at(m_, self + kFont + 8, 3);
    set_word_at(m_, self + kFont + 0x30, 0x800);
    for (std::uint32_t i = 0; i < 10; ++i) {
        set_word_at(m_, self + kRows + 4 * i, 0);
    }
    std::uint32_t memory = operator_new(0x1bc);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.list_construct(memory, 0, 0, 0x200, 0xf0, 1, 1, 3, 0);
    }
    set_word_at(m_, self + kList, list);
    // the rows' style: mainf, centred, grey
    memory = operator_new(0x17c);
    const std::uint32_t style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    set_word_at(m_, self + kStyle, style);
    copy_font_named(style + 4, kMainFont);
    set_word_at(m_, style + 8, 1);
    set_word_at(m_, style + 0xc, 3);
    f_.set_rgb(style + 4, 0x80, 0x80, 0x80);
    set_word_at(m_, style + 0x10, 0);
    set_word_at(m_, style + 0x14, 0);
    set_byte_at(m_, style + 0x25, 0);
    set_word_at(m_, style + 0x34, 0x600);
    set_word_at(m_, style + 0x150, 0x600);
    set_word_at(m_, style + 0x154, 0x600);
    // the title's style: a copy with its own colour
    memory = operator_new(0x17c);
    const std::uint32_t title_style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    set_word_at(m_, self + kTitleStyle, title_style);
    menu_.copy_font_fields(title_style + 4, style + 4);
    for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
        set_word_at(m_, title_style + offset, word_at(m_, style + offset));
    }
    set_word_at(m_, title_style + 0x158, 0x91);
    set_word_at(m_, title_style + 0x15c, 0x12);
    set_word_at(m_, title_style + 0x160, 0xf);
    // the title and the five games
    memory = operator_new(0x58);
    std::uint32_t title = 0;
    if (memory != 0) {
        title = menu_.text_scaled_construct(memory, 0xffffffffu, global_word(m_, 0x0052e748), title_style, 1);
    }
    set_word_at(m_, self + kRows, title);
    for (std::uint32_t i = 0; i < 5; ++i) {
        memory = operator_new(0x54);
        std::uint32_t row = 0;
        if (memory != 0) {
            row = menu_.text_construct(memory, kGames[i].id, word_at(m_, kGames[i].text), style);
        }
        set_word_at(m_, self + kRows + 4 * (i + 1), row);
    }
    for (std::uint32_t i = 0; i < 10; ++i) {
        if (word_at(m_, self + kRows + 4 * i) != 0) {
            vcall_list("list_v6", word_at(m_, self + kList), 6, {word_at(m_, self + kRows + 4 * i)});
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
        s_.note(kSpriteInit, {sprite});
    };
    arrow(kArrowA, 0x00527c20);
    arrow(kArrowB, 0x00527c10);
    vcall_list("list_v1", word_at(m_, self + kList), 1, {word_at(m_, self + kArrowA), word_at(m_, self + kArrowB), 5});
    vcall_list("list_v2", word_at(m_, self + kList), 2);
    // the prompt windows: select, accept, back
    const struct {
        std::uint32_t file;
        std::uint32_t caption;
        std::int32_t x;
    } prompts[] = {{0x005358b4, 0x0052e3f0, 0x76}, {0x0052d868, 0x0052e3f4, 0xda}, {0x0052d858, 0x0052e3f8, 0x147}};
    for (const auto& p : prompts) {
        w_.window_add(self, prompt(p.file, word_at(m_, p.caption), self + kFont, p.x, 0xdc));
    }
    engine(0x004a9330, {global_word(m_, kPreManager), kPreName});
    return self;
}

std::uint32_t MultiSelect::scalar_delete(std::uint32_t self, std::uint32_t flag) {
    destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x0047c2b0: the background (its sprite object, then its block), the list, the two styles, the two arrows, the ten rows, the font and the window base.
void MultiSelect::destroy(std::uint32_t self) {
    set_word_at(m_, self, kVtMultiSelect);
    check(word_at(m_, self + kBackground) != 0, kMsgNoBackground);
    const std::uint32_t sprite = word_at(m_, self + kBackground);
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    if (word_at(m_, self + kList) != 0) {
        vcall_list("virtual_delete", word_at(m_, self + kList), 0, {1});
    }
    for (std::uint32_t slot : {kStyle, kTitleStyle, kArrowA, kArrowB}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t i = 0; i < 10; ++i) {
        if (word_at(m_, self + kRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kRows + 4 * i));
        }
    }
    font_destroy(self + kFont);
    w_.window_destroy(self);
}

// 0x0047c3e0: the window, then the background.
void MultiSelect::draw(std::uint32_t self, std::uint32_t x, std::uint32_t y) {
    w_.window_draw(self, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
    check(word_at(m_, self + kBackground) != 0, kMsgNoBackground);
    engine(0x004d7990, {word_at(m_, self + kBackground), word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
}

// 0x0047c460: the list moves with the pad.
void MultiSelect::update(std::uint32_t self) {
    const std::uint32_t list = word_at(m_, self + kList);
    vcall_list("list_v3", list, 3);
    const std::uint32_t edge = engine_value(0x0046f650);
    vcall_list("list_v5", list, 5, {edge});
    vcall_list("list_v4", list, 4);
    w_.window_update(self);
}

// 0x0047c4a0: a move on either pad makes a sound (0x90); confirm on either pad answers 1 (0x91), back on either answers 2 (0x93); else 0. The edges are cleared.
std::uint32_t MultiSelect::poll() {
    if (global_byte(m_, kPadUp) != 0 || global_byte(m_, kPadUp + kPadStride) != 0 || global_byte(m_, kPadUp2) != 0 || global_byte(m_, kPadUp2 + kPadStride) != 0) {
        sfx(0x90);
    }
    if (global_byte(m_, kPadConfirmEdge) == 0 && global_byte(m_, kPadConfirmEdge + kPadStride) == 0) {
        if (global_byte(m_, kPadBackEdge) == 0 && global_byte(m_, kPadBackEdge + kPadStride) == 0) {
            return 0;
        }
        sfx(0x93);
        set_global_byte(m_, kPadBackEdge, 0);
        set_global_byte(m_, kPadBackEdge + kPadStride, 0);
        return 2;
    }
    sfx(0x91);
    set_global_byte(m_, kPadConfirmEdge, 0);
    set_global_byte(m_, kPadConfirmEdge + kPadStride, 0);
    return 1;
}

// 0x0047c550: the id of the highlighted game.
std::uint32_t MultiSelect::selected_id(std::uint32_t self) { return menu_.list_highlighted_id(word_at(m_, self + kList)); }

}  // namespace opentony::runtime::frontend
