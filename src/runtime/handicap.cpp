#include "auto_views.hpp"
#include "handicap.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// Panel fields
constexpr std::uint32_t kPFontA = 0xac;       // s2font1: the name and the player's title
constexpr std::uint32_t kPFontB = 0x1f8;      // s2font2: the lines under the dots
constexpr std::uint32_t kDotsSpriteA = 0x344;
constexpr std::uint32_t kDotsSpriteB = 0x348;
constexpr std::uint32_t kDotsA = 0x34c;
constexpr std::uint32_t kDotsB = 0x350;
constexpr std::uint32_t kFace = 0x354;
constexpr std::uint32_t kFaceSprite = 0x358;
constexpr std::uint32_t kReady = 0x35c;
constexpr std::uint32_t kPlayerSlot = 0x360;

// Window fields
constexpr std::uint32_t kBackground = 0xac;
constexpr std::uint32_t kFontA = 0xb0;        // s2font1: the title
constexpr std::uint32_t kFontB = 0x1fc;       // s2font2: the prompt windows' captions
constexpr std::uint32_t kPanelA = 0x348;
constexpr std::uint32_t kPanelB = 0x34c;

constexpr std::uint32_t kPad0 = 0x0056aff8;
constexpr std::uint32_t kPad1 = 0x0056b164;
constexpr std::uint32_t kPadLeftEdge = 0x0056b079;
constexpr std::uint32_t kPadRightEdge = 0x0056b089;

constexpr std::uint32_t kFont1 = 0x00526acc;        // "s2font1.fnt"
constexpr std::uint32_t kFont2 = 0x0052544c;        // "s2font2.fnt"
constexpr std::uint32_t kDotsFile = 0x0052d84c;     // "s2dots.bmp"
constexpr std::uint32_t kMsgNoBackground = 0x00525470;
constexpr std::uint32_t kFormatLogZero = 0x0052d8bc;    // "Handicap[0]: %d\n"
constexpr std::uint32_t kFormatLogOne = 0x0052d8a8;     // "Handicap[1]: %d\n"

// What a panel says about a handicap and the strings it reads (pointer variables)
constexpr std::uint32_t kTextNoChange = 0x0052e338;
constexpr std::uint32_t kTextToStats = 0x0052e33c;
constexpr std::uint32_t kTextFromStats = 0x0052e340;
constexpr std::uint32_t kTextAdd = 0x0052e344;
constexpr std::uint32_t kTextSubtract = 0x0052e348;
constexpr std::uint32_t kTextWaitingFor = 0x0052e34c;
constexpr std::uint32_t kTextOtherPlayer = 0x0052e350;

std::uint32_t pad_at(std::uint32_t base, std::uint32_t player) { return base + player * kPadStride; }

}  // namespace

// ---- the screen callbacks and the loop --------------------------------------------------------------------------------------------------------------

// 0x00454e80: the screen table asks each screen for its two callbacks.
void HandicapScreen::init_callbacks(std::uint32_t record) {
    set_word_at(m_, record, 0x00454ea0);
    set_word_at(m_, record + 0x0c, 0x00454eb0);
}

void HandicapScreen::exit_callback() {}

// 0x00454eb0: `*result = run()`.
void HandicapScreen::screen_call(std::uint32_t argument, std::uint32_t result) {
    (void)argument;
    set_word_at(m_, result, u(run()));
}

// 0x00455f00: the handicap screen: two fonts, the window, frames until the window answers 1 (both players ready) or 2 (one backed out) or the engine asks to
// leave; then the window and fonts go, the leak check runs and both handicaps are logged. Answers 0, or -1 when someone backed out. (A frame waits for the
// clock with a bare spin on it, which a forced call cannot satisfy: the replays end the loop on the first frame.)
std::int32_t HandicapScreen::run() {
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    font_load(kFont1);
    font_load(kFont2);
    const std::uint32_t memory = operator_new(0x350);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = window_construct(memory);
    }
    engine(0x00489670, {kPad0});
    engine(0x00489670, {kPad1});
    engine(0x004c8f30);
    std::int32_t step = 0;
    for (;;) {
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x00418150);
        set_global_word(m_, kGlobalLastTick, global_word(m_, kGlobalSimTick));
        engine(0x004304b0);
        engine(0x00489a10);
        step = static_cast<std::int32_t>(seams_ ? engine_value(0x00455e90, {window}) : window_step(window));
        if (step == 1 || step == 2) {
            break;
        }
        if ((engine_value(0x00452d30) & 0xff) != 0) {
            break;
        }
        if (seams_) {
            engine(0x004ac730, {window});
            engine(0x00455e10, {window, 0, 0});
        } else {
            w_.window_update(window);
            window_draw(window, 0, 0);
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
            engine(0x00455d20, {window, 1});
        } else {
            window_delete(window, 1);
        }
    }
    font_unload(kFont1);
    font_unload(kFont2);
    engine(0x0046f480);
    s_.note(kSvcPrintfLog, {kFormatLogZero, global_word(m_, kHandicapValues)});
    s_.note(kSvcPrintfLog, {kFormatLogOne, global_word(m_, kHandicapValues + 4)});
    return step != 2 ? 0 : -1;
}

// ---- a player's panel ---------------------------------------------------------------------------------------------------------------------------------

// 0x00454ec0: two fonts, two dots sprites and the two rows of dots they draw, the skater's face, and the title and name lines.
std::uint32_t HandicapScreen::panel_construct(std::uint32_t self, std::int32_t player, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    font_construct(self + kPFontA);
    font_construct(self + kPFontB);
    handicap_panel_at(m_, self).set_field_0(static_cast<Word>(kVtHandicapPlayer));
    handicap_panel_at(m_, self).set_field_35c(0);
    handicap_panel_at(m_, self).set_field_360(static_cast<Word>(u(player)));
    // the first font: s2font1, grey, no outline
    copy_font_named(self + kPFontA, kFont1);
    set_word_at(m_, self + kPFontA + 0x14, 0x80);
    set_word_at(m_, self + kPFontA + 0x18, 0x80);
    set_word_at(m_, self + kPFontA + 0x1c, 0x80);
    set_word_at(m_, self + kPFontA + 4, 1);
    set_word_at(m_, self + kPFontA + 8, 3);
    set_byte_at(m_, self + kPFontA + 0x21, 0);
    set_word_at(m_, self + kPFontA + 0xc, 0);
    set_word_at(m_, self + kPFontA + 0x30, 0x800);
    // the second: s2font2
    copy_font_named(self + kPFontB, kFont2);
    set_word_at(m_, self + kPFontB + 4, 1);
    set_word_at(m_, self + kPFontB + 8, 3);
    set_word_at(m_, self + kPFontB + 0x14, 0x80);
    set_word_at(m_, self + kPFontB + 0x18, 0x80);
    set_word_at(m_, self + kPFontB + 0x1c, 0x80);
    set_byte_at(m_, self + kPFontB + 0x21, 0);
    set_word_at(m_, self + kPFontB + 0xc, 0);
    set_word_at(m_, self + kPFontB + 0x30, 0x800);
    // the dots sprites (the second is marked) and the dots they draw
    auto dots_sprite = [&](std::uint32_t slot, bool marked) {
        const std::uint32_t block = operator_new(0x5c);
        std::uint32_t sprite = 0;
        if (block != 0) {
            s_.note(kSpriteConstruct, {block, kDotsFile, 1, 0, 0}, m_.cstring(kDotsFile));
            sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, sprite);
        if (marked) {
            engine(0x00457810, {sprite});
        }
        s_.note(kSpriteInit, {sprite});
    };
    dots_sprite(kDotsSpriteA, false);
    dots_sprite(kDotsSpriteB, true);
    auto dots = [&](std::uint32_t slot, std::uint32_t sprite_slot) {
        const std::uint32_t block = operator_new(0x18);
        std::uint32_t row = 0;
        if (block != 0) {
            row = menu_.dots_construct(block, word_at(m_, self + sprite_slot), word_at(m_, kHandicapValues + 4 * u(player)), 10);
        }
        set_word_at(m_, self + slot, row);
    };
    dots(kDotsA, kDotsSpriteA);
    dots(kDotsB, kDotsSpriteB);
    // the skater's face: an element in the window, and the same picture as a sprite drawn by hand
    std::uint32_t block = operator_new(0x2c);
    std::uint32_t face = 0;
    if (block != 0) {
        face = w_.sprite_construct(block, sk_.face_bitmap(player), 0, 0x50, -1, -1, 1, 1);
    }
    handicap_panel_at(m_, self).set_field_354(static_cast<Word>(face));
    w_.window_add(self, face);
    block = operator_new(0x5c);
    std::uint32_t sprite = 0;
    if (block != 0) {
        const std::uint32_t file = sk_.face_bitmap(player);
        s_.note(kSpriteConstruct, {block, file, 1, 0, 0}, m_.cstring(file));
        sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
    }
    handicap_panel_at(m_, self).set_field_358(static_cast<Word>(sprite));
    if (sprite != 0) {
        set_byte_at(m_, sprite + 0xa, 1);
        set_byte_at(m_, sprite + 0xb, 1);
    }
    engine(0x00457810, {sprite});
    s_.note(kSpriteInit, {sprite});
    // the name, formatted into a local buffer and trimmed to fit
    scratch_line({});
    sk_.format_name(player, kScratchLine);
    f_.ellipsize(self + kPFontA, kScratchLine, 0xf0);
    auto text = [&](std::uint32_t string, std::int32_t text_y, std::int32_t text_x) {
        const std::uint32_t memory = operator_new(0x30);
        std::uint32_t element = 0;
        if (memory != 0) {
            element = w_.text_construct(memory, string, self + kPFontA, 0, text_y, text_x, -1);
        }
        w_.window_add(self, element);
        return element;
    };
    const std::int32_t side = player == 0 ? -0x80 : 0x80;
    text(word_at(m_, player == 0 ? 0x0052e488 : 0x0052e48c), 0x76, side);
    const std::uint32_t name = text(kScratchLine, 0x84, side);
    if (name != 0) {
        unspecified_.emplace_back(name + 0x28, 4);    // (the element keeps the address of the local buffer)
    }
    return self;
}

std::uint32_t HandicapScreen::panel_delete(std::uint32_t self, std::uint32_t flag) {
    panel_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00455430: the two dots sprites, the two dots and the hand-drawn face are deleted (the face element belongs to the window), then the fonts and the window base.
void HandicapScreen::panel_destroy(std::uint32_t self) {
    handicap_panel_at(m_, self).set_field_0(static_cast<Word>(kVtHandicapPlayer));
    for (std::uint32_t slot : {kDotsSpriteA, kDotsSpriteB, kDotsA, kDotsB, kFaceSprite}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    font_destroy(self + kPFontB);
    font_destroy(self + kPFontA);
    w_.window_destroy(self);
}

// 0x004554f0: confirm marks the player ready (sound 0x91); back (sound 0x93) takes it back, or, if it was not ready, leaves the screen. Answers 2 (leave), 1 (ready)
// or 0.
std::uint32_t HandicapScreen::panel_poll(std::uint32_t self) {
    const std::uint32_t player = handicap_panel_at(m_, self).field_360();
    if (byte_at(m_, pad_at(kPadConfirmEdge, player)) != 0) {
        set_byte_at(m_, pad_at(kPadConfirmEdge, player), 0);
        handicap_panel_at(m_, self).set_field_35c(1);
        sfx(0x91);
    }
    if (byte_at(m_, pad_at(kPadBackEdge, handicap_panel_at(m_, self).field_360())) != 0) {
        set_byte_at(m_, pad_at(kPadBackEdge, handicap_panel_at(m_, self).field_360()), 0);
        sfx(0x93);
        if (handicap_panel_at(m_, self).field_35c() == 0) {
            return 2;
        }
        handicap_panel_at(m_, self).set_field_35c(0);
    }
    return handicap_panel_at(m_, self).field_35c() != 0 ? 1 : 0;
}

// 0x00455590: while the player is not ready, left and right edges move the handicap by one (sound 0x90) and it is limited to -5..5; both rows of dots follow.
void HandicapScreen::panel_update(std::uint32_t self) {
    const std::uint32_t player = handicap_panel_at(m_, self).field_360();
    const std::uint32_t value = kHandicapValues + 4 * player;
    if (handicap_panel_at(m_, self).field_35c() == 0) {
        if (byte_at(m_, pad_at(kPadLeftEdge, player)) != 0) {
            sfx(0x90);
            set_byte_at(m_, pad_at(kPadLeftEdge, handicap_panel_at(m_, self).field_360()), 0);
            set_word_at(m_, value, word_at(m_, value) - 1);
        }
        if (byte_at(m_, pad_at(kPadRightEdge, handicap_panel_at(m_, self).field_360())) != 0) {
            sfx(0x90);
            set_byte_at(m_, pad_at(kPadRightEdge, handicap_panel_at(m_, self).field_360()), 0);
            set_word_at(m_, value, word_at(m_, value) + 1);
        }
        engine(0x004ca8d0, {value, 0xfffffffbu, 5});
    }
    const std::uint32_t shown = word_at(m_, kHandicapValues + 4 * handicap_panel_at(m_, self).field_360()) + 5;
    menu_.dots_set_range(handicap_panel_at(m_, self).field_34c(), shown, static_cast<std::int32_t>(shown));
    menu_.dots_set_range(handicap_panel_at(m_, self).field_350(), word_at(m_, kHandicapValues + 4 * handicap_panel_at(m_, self).field_360()) + 5,
                         static_cast<std::int32_t>(word_at(m_, kHandicapValues + 4 * handicap_panel_at(m_, self).field_360()) + 5));
    w_.window_update(self);
}

// 0x00455690: the dots (the lit row once the player is ready, with the hand-drawn face), then the lines under them: what the handicap does to the skater's stats, and
// while the player is ready and the other is not a blinking "Waiting for / Other player".
void HandicapScreen::panel_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t player = handicap_panel_at(m_, self).field_360();
    const std::int32_t column = static_cast<std::int32_t>(player << 8);
    const bool ready = handicap_panel_at(m_, self).field_35c() != 0;
    if (!ready) {
        menu_.dots_draw(handicap_panel_at(m_, self).field_34c(), column + 0x44, 0x8a);
        set_word_at(m_, handicap_panel_at(m_, self).field_354() + 0x14, 0);
    } else {
        menu_.dots_draw(handicap_panel_at(m_, self).field_350(), column + 0x44, 0x8a);
        set_word_at(m_, handicap_panel_at(m_, self).field_354() + 0x14, 1);
        vcall_item("sprite_draw", handicap_panel_at(m_, self).field_358(), 3, {handicap_panel_at(m_, self).field_20(), handicap_panel_at(m_, self).field_24() + 0x50, 0});
    }
    const std::int32_t handicap = int_at(m_, kHandicapValues + 4 * player);
    std::string first;
    std::string second;
    if (handicap == 0) {
        first = m_.cstring(global_word(m_, kTextNoChange), 0x40);
        second = m_.cstring(global_word(m_, kTextToStats), 0x40);
    } else if (handicap < 0) {
        first = retail_format(m_.cstring(global_word(m_, kTextSubtract), 0x40), {}, {-handicap});
        second = m_.cstring(global_word(m_, kTextFromStats), 0x40);
    } else {
        first = retail_format(m_.cstring(global_word(m_, kTextAdd), 0x40), {}, {handicap});
        second = m_.cstring(global_word(m_, kTextToStats), 0x40);
    }
    // (the two lines under the dots are in a local buffer; the blinking message is the game's own text)
    auto draw_line = [&](std::int32_t line_y, const std::string& line, std::uint32_t pointer) {
        s_.note(kFontDrawService, {self + kPFontB, u(column + 0x80), u(line_y), pointer, 0}, line);
    };
    if (ready) {
        if (global_word(m_, kSimClock) % 0x3c > 0x1e) {
            f_.set_rgb(self + kPFontB, 0x60, 0x12, 0xf);
            draw_line(0xb6, m_.cstring(global_word(m_, kTextWaitingFor), 0x40), global_word(m_, kTextWaitingFor));
            draw_line(0xc1, m_.cstring(global_word(m_, kTextOtherPlayer), 0x40), global_word(m_, kTextOtherPlayer));
        }
        f_.set_rgb(self + kPFontB, 0x40, 0x40, 0x40);
        f_.set_rgb(self + kPFontA, 0x40, 0x40, 0x40);
    } else {
        f_.set_rgb(self + kPFontB, 0x80, 0x80, 0x80);
        f_.set_rgb(self + kPFontA, 0x80, 0x80, 0x80);
    }
    draw_line(0xa0, first, kScratchLine);
    draw_line(0xab, second, kScratchLine);
    w_.window_draw(self, x, y);
}

// ---- the window ---------------------------------------------------------------------------------------------------------------------------------------

// 0x004558c0: the background, two fonts, three prompt windows (change, accept, back), a panel for each player and the title.
std::uint32_t HandicapScreen::window_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontA);
    font_construct(self + kFontB);
    handicap_window_at(m_, self).set_field_0(static_cast<Word>(kVtHandicapWindow));
    handicap_window_at(m_, self).set_field_ac(static_cast<Word>(background_sprite()));
    copy_font_named(self + kFontA, kFont1);
    set_word_at(m_, self + kFontA + 4, 1);
    set_word_at(m_, self + kFontA + 8, 3);
    set_word_at(m_, self + kFontA + 0x14, 0x91);
    set_word_at(m_, self + kFontA + 0x18, 0x12);
    set_word_at(m_, self + kFontA + 0x1c, 0xf);
    set_byte_at(m_, self + kFontA + 0x21, 0);
    set_word_at(m_, self + kFontA + 0xc, 0);
    set_word_at(m_, self + kFontA + 0x30, 0x800);
    copy_font_named(self + kFontB, kFont2);
    set_word_at(m_, self + kFontB + 4, 0);
    set_word_at(m_, self + kFontB + 8, 3);
    set_word_at(m_, self + kFontB + 0x14, 0x80);
    set_word_at(m_, self + kFontB + 0x18, 0x80);
    set_word_at(m_, self + kFontB + 0x1c, 0x80);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x800);
    // the prompt windows: change, accept, back (each with its caption from a pointer variable)
    const struct {
        std::uint32_t file;
        std::uint32_t caption;
        std::int32_t x;
    } prompts[] = {{0x0052d878, 0x0052e3fc, 0x6e}, {0x0052d868, 0x0052e3f4, 0xe2}, {0x0052d858, 0x0052e3f8, 0x14f}};
    for (const auto& p : prompts) {
        w_.window_add(self, prompt(p.file, word_at(m_, p.caption), self + kFontB, p.x, 0xdc));
    }
    // a panel for each player
    for (std::int32_t player = 0; player < 2; ++player) {
        const std::uint32_t memory = operator_new(0x364);
        std::uint32_t panel = 0;
        if (memory != 0) {
            panel = panel_construct(memory, player, player == 0 ? 0x80 : 0x180, 0, -1, -1);
        }
        set_word_at(m_, self + (player == 0 ? kPanelA : kPanelB), panel);
        w_.window_add(self, panel);
    }
    const std::uint32_t memory = operator_new(0x30);
    std::uint32_t title = 0;
    if (memory != 0) {
        title = w_.text_construct(memory, global_word(m_, 0x0052e334), self + kFontA, 0x100, 0x50, -1, 0);
    }
    w_.window_add(self, title);
    return self;
}

std::uint32_t HandicapScreen::window_delete(std::uint32_t self, std::uint32_t flag) {
    window_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00455d40: the background (its sprite object, then its block), the two fonts, the window base (which deletes the children).
void HandicapScreen::window_destroy(std::uint32_t self) {
    handicap_window_at(m_, self).set_field_0(static_cast<Word>(kVtHandicapWindow));
    check(handicap_window_at(m_, self).field_ac() != 0, kMsgNoBackground);
    const std::uint32_t sprite = handicap_window_at(m_, self).field_ac();
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    font_destroy(self + kFontB);
    font_destroy(self + kFontA);
    w_.window_destroy(self);
}

// 0x00455e10: the window, then the background.
void HandicapScreen::window_draw(std::uint32_t self, std::uint32_t x, std::uint32_t y) {
    w_.window_draw(self, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
    check(handicap_window_at(m_, self).field_ac() != 0, kMsgNoBackground);
    engine(0x004d7990, {handicap_window_at(m_, self).field_ac(), word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
}

// 0x00455e90: with a second pad, both panels are polled: 2 when either answers 2, 1 when both answer 1, else 0. With one pad the screen has nothing to ask: 2.
std::uint32_t HandicapScreen::window_step(std::uint32_t self) {
    if (global_word(m_, kSecondPad) == 0) {
        return 2;
    }
    const std::uint32_t first = panel_poll(handicap_window_at(m_, self).field_348());
    const std::uint32_t second = panel_poll(handicap_window_at(m_, self).field_34c());
    if (first == 2 || second == 2) {
        return 2;
    }
    return (first == 1 && second == 1) ? 1 : 0;
}

}  // namespace opentony::runtime::frontend
