#include "auto_views.hpp"
#include "career_select.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// Window fields
constexpr std::uint32_t kBackground = 0xac;
constexpr std::uint32_t kFontA = 0xb0;       // mainf: the list's style
constexpr std::uint32_t kFontB = 0x1fc;      // s2font2: the prompt windows' captions
constexpr std::uint32_t kList = 0x348;
constexpr std::uint32_t kStyle = 0x34c;
constexpr std::uint32_t kRows = 0x350;       // 0x60 slots
constexpr std::uint32_t kArrowA = 0x4d0;
constexpr std::uint32_t kArrowB = 0x4d4;
constexpr std::uint32_t kState = 0x4d8;

constexpr std::uint32_t kPad0 = 0x0056aff8;
constexpr std::uint32_t kPad1 = 0x0056b164;
constexpr std::uint32_t kNewCareerFlag = 0x0052cb3d;
constexpr std::uint32_t kPreManager = 0x0056b834;

constexpr std::uint32_t kMsgNoBackground = 0x00525470;
constexpr std::uint32_t kMsgNotSaveMode = 0x0052549c;
constexpr std::uint32_t kMsgNotLoadMode = 0x005254b0;
constexpr std::uint32_t kMsgUnrecognized = 0x005254c4;

constexpr std::uint32_t kCareerFonts[3] = {0x00525458, 0x0052544c, 0x005254e0};   // mainf, s2font2, s2tricks
constexpr std::uint32_t kPreName = 0x005254f0;                                   // "carsel.pre"

// The choices' ids: the rows are Continue career (3), New career (0) and Load career (1); 2 is the save mode no row selects
constexpr std::uint32_t kChoiceNew = 0;
constexpr std::uint32_t kChoiceLoad = 1;
constexpr std::uint32_t kChoiceSave = 2;
constexpr std::uint32_t kChoiceContinue = 3;

}  // namespace

// ---- the screen callbacks and the loop --------------------------------------------------------------------------------------------------------------

// 0x004173d0: the screen table asks each screen for its two callbacks.
void CareerSelect::init_callbacks(std::uint32_t record) {
    set_word_at(m_, record, 0x004173f0);
    set_word_at(m_, record + 0x0c, 0x00417400);
}

void CareerSelect::exit_callback() {}

// 0x00417400: `*result = run()`.
void CareerSelect::screen_call(std::uint32_t argument, std::uint32_t result) {
    (void)argument;
    set_word_at(m_, result, u(run()));
}

// 0x00417f90: the career select screen: the pre file and three fonts, the window, frames until the poll answers 1 or 2 (or the engine asks to leave), then the
// fonts are unloaded. Answers 0, or -1 when the player backed out.
std::int32_t CareerSelect::run() {
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    engine(0x004a90f0, {global_word(m_, kPreManager), kPreName, 0});
    for (std::uint32_t font : kCareerFonts) {
        font_load(font);
    }
    const std::uint32_t memory = operator_new(0x4dc);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = construct(memory);
    }
    engine(0x004a9330, {global_word(m_, kPreManager), kPreName});
    engine(0x00489670, {kPad0});
    engine(0x00489670, {kPad1});
    const std::int32_t result = frame_loop(
        [&] { return static_cast<std::int32_t>(seams_ ? engine_value(0x00417e30, {window}) : poll(window)); },
        [&] {
            if (seams_) {
                engine(0x00417c90, {window});
            } else {
                update(window);
            }
        },
        [&] {
            if (seams_) {
                engine(0x00417bf0, {window, 0, 0});
            } else {
                draw(window, 0, 0);
            }
        });
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (window != 0) {
        if (seams_) {
            engine(0x00417aa0, {window, 1});
        } else {
            scalar_delete(window, 1);
        }
    }
    for (std::uint32_t font : kCareerFonts) {
        font_unload(font);
    }
    return result != 2 ? 0 : -1;
}

// ---- the window ---------------------------------------------------------------------------------------------------------------------------------------

// 0x00417410: the window base, two fonts, the background, the list (a style copied from mainf, three text rows, two arrows) and three prompt windows.
std::uint32_t CareerSelect::construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontA);
    font_construct(self + kFontB);
    set_word_at(m_, self, kVtCareerSelect);
    set_word_at(m_, self + kState, 0xffffffffu);
    set_word_at(m_, self + kBackground, background_sprite());
    // the first font: mainf, justification 1, colour 0x80 grey, the character scale 0xc00
    copy_font_named(self + kFontA, kCareerFonts[0]);
    set_word_at(m_, self + kFontA + 4, 1);
    set_word_at(m_, self + kFontA + 8, 3);
    set_word_at(m_, self + kFontA + 0x14, 0x40);
    set_word_at(m_, self + kFontA + 0x18, 0x80);
    set_word_at(m_, self + kFontA + 0x1c, 0x80);
    set_byte_at(m_, self + kFontA + 0x21, 0);
    set_word_at(m_, self + kFontA + 0xc, 0);
    set_word_at(m_, self + kFontA + 0x10, 0);
    set_word_at(m_, self + kFontA + 0x30, 0xc00);
    // the second: s2font2
    copy_font_named(self + kFontB, kCareerFonts[1]);
    set_word_at(m_, self + kFontB + 4, 0);
    set_word_at(m_, self + kFontB + 8, 3);
    set_word_at(m_, self + kFontB + 0x14, 0x80);
    set_word_at(m_, self + kFontB + 0x18, 0x80);
    set_word_at(m_, self + kFontB + 0x1c, 0x80);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x800);
    // the list and its style
    std::uint32_t memory = operator_new(0x1bc);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.list_construct(memory, 0, 4, 0x200, 0xf0, 1, 1, 3, 0);
    }
    set_word_at(m_, self + kList, list);
    memory = operator_new(0x17c);
    const std::uint32_t style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    set_word_at(m_, self + kStyle, style);
    menu_.copy_font_fields(style + 4, self + kFontA);
    set_word_at(m_, style + 0x18, 0x80);
    set_word_at(m_, style + 0x1c, 0x80);
    set_word_at(m_, style + 0x20, 0x80);
    set_word_at(m_, style + 0x34, 0xc00);
    set_word_at(m_, style + 0x150, 0xc00);
    set_word_at(m_, style + 0x154, 0xc00);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kRows + 4 * i, 0);
    }
    // the rows: Continue career (3), New career (0), Load career (1); while the flag byte at 0x0052cb3d is set the first row's byte at +0xc is cleared (greyed)
    auto row = [&](std::uint32_t slot, std::uint32_t id, std::uint32_t text_variable) {
        const std::uint32_t block = operator_new(0x54);
        std::uint32_t text = 0;
        if (block != 0) {
            text = menu_.text_construct(block, id, word_at(m_, text_variable), style);
        }
        set_word_at(m_, self + slot, text);
        return text;
    };
    const std::uint32_t continue_row = row(kRows, kChoiceContinue, 0x0052e6ac);
    if (global_byte(m_, kNewCareerFlag) != 0 && continue_row != 0) {
        set_byte_at(m_, continue_row + 0xc, 0);
    }
    row(kRows + 4, kChoiceNew, 0x0052e6b0);
    row(kRows + 8, kChoiceLoad, 0x0052e6b4);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kRows + 4 * i) != 0) {
            vcall_list("list_v6", word_at(m_, self + kList), 6, {word_at(m_, self + kRows + 4 * i)});
        }
    }
    // the two arrows
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
    arrow(kArrowA, 0x0052543c);
    arrow(kArrowB, 0x0052542c);
    vcall_list("list_v1", word_at(m_, self + kList), 1, {word_at(m_, self + kArrowA), word_at(m_, self + kArrowB), 5});
    vcall_list("list_v2", word_at(m_, self + kList), 2);
    // the prompt windows: select, accept, back
    const struct {
        std::uint32_t file;
        std::int32_t x;
    } prompts[] = {{0x0052541c, 0xe6}, {0x0052540c, 0x190}, {0x00525400, 0x32}};
    for (const auto& p : prompts) {
        w_.window_add(self, prompt(p.file, 0x005252f0, self + kFontB, p.x, 0xdc));
    }
    return self;
}

std::uint32_t CareerSelect::scalar_delete(std::uint32_t self, std::uint32_t flag) {
    destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00417ac0: the background (its sprite object, then its block), the list, the style, the two arrows, the rows, the two fonts and the window base.
void CareerSelect::destroy(std::uint32_t self) {
    set_word_at(m_, self, kVtCareerSelect);
    check(word_at(m_, self + kBackground) != 0, kMsgNoBackground);
    const std::uint32_t sprite = word_at(m_, self + kBackground);
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    if (word_at(m_, self + kList) != 0) {
        vcall_list("virtual_delete", word_at(m_, self + kList), 0, {1});
    }
    for (std::uint32_t slot : {kStyle, kArrowA, kArrowB}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kRows + 4 * i));
        }
    }
    font_destroy(self + kFontB);
    font_destroy(self + kFontA);
    w_.window_destroy(self);
}

// 0x00417bf0: the card screens draw instead of the window while a card step runs (state 1 or 2); the background follows.
void CareerSelect::draw(std::uint32_t self, std::uint32_t x, std::uint32_t y) {
    const std::int32_t state = int_at(m_, self + kState);
    if (state == 2 || state == 1) {
        engine(0x0047ace0);
    } else {
        w_.window_draw(self, x, y);
    }
    check(word_at(m_, self + kBackground) != 0, kMsgNoBackground);
    engine(0x004d7990, {word_at(m_, self + kBackground), word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
}

// 0x00417c90: while choosing, the list moves with the pad.
void CareerSelect::update(std::uint32_t self) {
    if (static_cast<std::int32_t>(career_select_update_at(m_, self).field_4d8()) != -1) {
        return;
    }
    const std::uint32_t list = career_select_update_at(m_, self).field_348();
    vcall_list("list_v3", list, 3);
    s_.note(kSvcPadEdges, {0, 0, 6});
    const std::uint32_t edge = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
    vcall_list("list_v5", list, 5, {edge});
    vcall_list("list_v4", list, 4);
    w_.window_update(self);
}

// 0x00417ce0: save mode: the card layer answers 1 (saved) or 2 (cancelled); either ends the step and clears both pads' triggers.
std::uint32_t CareerSelect::update_save(std::uint32_t self) {
    check(static_cast<std::int32_t>(career_select_update_at(m_, self).field_4d8()) == 2, kMsgNotSaveMode);
    const std::uint32_t answer = engine_value(0x0047ac60);
    if (answer == 1 || answer == 2) {
        engine(0x0047ae10, {answer});
        career_select_update_at(m_, self).set_field_4d8(0xffffffffu);
        engine(0x00489670, {kPad0});
        engine(0x00489670, {kPad1});
    }
    return 0;
}

// 0x00417d70: load mode: 1 (a career was loaded: the screen is done) or 2 (cancelled).
std::uint32_t CareerSelect::update_load(std::uint32_t self) {
    check(static_cast<std::int32_t>(career_select_update_at(m_, self).field_4d8()) == 1, kMsgNotLoadMode);
    const std::uint32_t answer = engine_value(0x0047ac60);
    if (answer == 1 || answer == 2) {
        engine(0x0047ae10, {answer});
        career_select_update_at(m_, self).set_field_4d8(0xffffffffu);
        engine(0x00489670, {kPad0});
        engine(0x00489670, {kPad1});
        return answer == 1 ? 1 : 0;
    }
    return 0;
}

// 0x00417e30: the screen's per-frame input. A card step answers for itself; the state it is in when it is neither (the retail jump falls out with the state) comes
// back unchanged. While choosing: a move makes a sound, confirm picks (new career resets the save image and answers 1, load and save open the card, continue
// answers 1) and back answers 2.
std::uint32_t CareerSelect::poll(std::uint32_t self) {
    const std::int32_t state = int_at(m_, self + kState);
    if (state != -1) {
        if (state == 1) {
            return update_load(self);
        }
        if (state == 2) {
            return update_save(self);
        }
        return u(state);
    }
    if (global_byte(m_, kPadUp) != 0 || global_byte(m_, kPadUp2) != 0) {
        sfx(0x90);
    }
    if (global_byte(m_, kPadConfirmEdge) == 0) {
        if (global_byte(m_, kPadBackEdge) != 0) {
            sfx(0x93);
            set_global_byte(m_, kPadBackEdge, 0);
            return 2;
        }
        return 0;
    }
    sfx(0x91);
    set_global_byte(m_, kPadConfirmEdge, 0);
    const std::uint32_t id = menu_.list_highlighted_id(word_at(m_, self + kList));
    switch (id) {
    case kChoiceNew:
        set_global_byte(m_, kNewCareerFlag, 0);
        engine(0x004138f0);
        return 1;
    case kChoiceLoad:
        engine(0x0047a6c0, {1, 1, 0});
        set_word_at(m_, self + kState, 1);
        return 0;
    case kChoiceSave:
        engine(0x0047a0b0, {0x12, 0x12, 1});
        set_word_at(m_, self + kState, 2);
        return 0;
    case kChoiceContinue:
        return 1;
    default:
        check(false, kMsgUnrecognized);
        return 0;
    }
}

// ---- the front end's shared helpers ---------------------------------------------------------------------------------------------------------------------

// 0x00418140: the address of a 256-byte table.
std::uint32_t CareerSelect::key_table() const { return kFrontKeyTable; }

// 0x00418150: the front end's menu sound. While a voice is playing its pan moves toward the target by 0x88 a frame; when it has arrived (and the target is zero)
// the voice is stopped. The pan is scaled by a word and divided by 510 on its way to the audio layer.
void CareerSelect::update_menu_sound() {
    const std::uint32_t voice = global_word(m_, kFrontVoice);
    if (voice == 0xffffffffu) {
        return;
    }
    std::int32_t current = global_int(m_, kFrontPanCurrent);
    const std::int32_t target = global_int(m_, kFrontPanTarget);
    if (current == target) {
        if (target == 0) {
            engine(0x004ad9f0, {voice});
            set_global_word(m_, kFrontVoice, 0xffffffffu);
            return;
        }
    } else if (current < target) {
        current = static_cast<std::int32_t>(u(current) + 0x88);
        set_global_word(m_, kFrontPanCurrent, u(current));
        if (current > target) {
            current = target;
            set_global_word(m_, kFrontPanCurrent, u(current));
        }
    } else {
        current = static_cast<std::int32_t>(u(current) - 0x88);
        set_global_word(m_, kFrontPanCurrent, u(current));
        if (current < target) {
            current = target;
            set_global_word(m_, kFrontPanCurrent, u(current));
        }
    }
    const std::uint32_t saved = global_word(m_, kFrontSoundPan);
    const std::int32_t product = static_cast<std::int32_t>(global_word(m_, kFrontSoundScale) * u(current));
    // (the compiler's division by 255: the high word of the signed product with 0x80808081, plus the value, shifted by seven; then a sign fix and one more shift)
    const std::int64_t wide = static_cast<std::int64_t>(static_cast<std::int32_t>(0x80808081u)) * static_cast<std::int64_t>(product);
    std::int32_t quotient = static_cast<std::int32_t>(wide >> 32);
    quotient = static_cast<std::int32_t>(u(quotient) + u(product)) >> 7;
    quotient = static_cast<std::int32_t>(u(quotient) + (u(quotient) >> 31));
    quotient >>= 1;
    set_global_word(m_, kFrontSoundPan, 0x3fff);
    engine(0x004ad950, {voice, u(quotient), u(quotient)});
    set_global_word(m_, kFrontSoundPan, saved);
}

}  // namespace opentony::runtime::frontend
