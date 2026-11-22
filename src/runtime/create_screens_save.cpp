#include "auto_views.hpp"
#include "create_screens.hpp"
#include "seam_addresses_generated.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgNoBackground = 0x00525470;

// CreateSave fields
constexpr std::uint32_t kBackground = 0xac;
constexpr std::uint32_t kFontA = 0xb0;
constexpr std::uint32_t kFontB = 0x1fc;
constexpr std::uint32_t kList = 0x348;
constexpr std::uint32_t kStyleA = 0x34c;
constexpr std::uint32_t kStyleB = 0x350;
constexpr std::uint32_t kTextA = 0x354;
constexpr std::uint32_t kTextB = 0x358;
constexpr std::uint32_t kArrowA = 0x35c;
constexpr std::uint32_t kArrowB = 0x360;
constexpr std::uint32_t kState = 0x364;
constexpr std::uint32_t kBox = 0x368;
constexpr std::uint32_t kPrompts = 0x36c;

}  // namespace

// 0x0042be40: the screen table asks each screen for its two callbacks.
void CreateScreens::save_init_callbacks(std::uint32_t record) {
    set_word_at(m_, record, 0x0042be60);
    set_word_at(m_, record + 0x0c, 0x0042be70);
}

void CreateScreens::save_exit_callback() {}

void CreateScreens::save_screen_call(std::uint32_t unused, std::uint32_t result) {
    (void)unused;
    set_word_at(m_, result, u(save_run()));
}

// 0x0042be80: the save-skater window. A background, two fonts, a two-item list (save into the career file / continue without saving), two arrows,
// the warning box shown for the second choice and three prompt windows.
std::uint32_t CreateScreens::save_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontA);
    font_construct(self + kFontB);
    create_screens_save_save_at(m_, self).set_field_0(static_cast<Word>(kVtCreateSave));
    create_screens_save_save_at(m_, self).set_field_364(0);
    create_screens_save_save_at(m_, self).set_field_ac(static_cast<Word>(background_sprite()));
    copy_font_named(self + kFontA, 0x00526acc);
    set_word_at(m_, self + kFontA + 4, 1);
    set_word_at(m_, self + kFontA + 8, 3);
    f_.set_rgb(self + kFontA, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontA + 0x21, 0);
    set_word_at(m_, self + kFontA + 0xc, 0);
    set_word_at(m_, self + kFontA + 0x10, 0);
    set_word_at(m_, self + kFontA + 0x30, 0x800);
    copy_font_named(self + kFontB, 0x0052544c);
    set_word_at(m_, self + kFontB + 0x14, 0x80);
    set_word_at(m_, self + kFontB + 0x18, 0x80);
    set_word_at(m_, self + kFontB + 0x1c, 0x80);
    set_word_at(m_, self + kFontB + 4, 0);
    set_word_at(m_, self + kFontB + 8, 3);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x800);
    create_screens_save_save_at(m_, self).set_field_354(0);
    create_screens_save_save_at(m_, self).set_field_358(0);
    std::uint32_t memory = operator_new(0x1bc);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.list_construct(memory, 0, 0, 0x200, 0xf0, 1, 1, 3, 0);
    }
    create_screens_save_save_at(m_, self).set_field_348(static_cast<Word>(list));
    // the style of the items: mainf at orange 0x800... first style, then a copy of it with the highlight colour changed
    memory = operator_new(0x17c);
    std::uint32_t style = 0;
    if (memory != 0) {
        style = menu_.properties_construct(memory);
    }
    create_screens_save_save_at(m_, self).set_field_34c(static_cast<Word>(style));
    menu_.copy_font_fields(style + 4, find_font(0x00525458));
    set_word_at(m_, style + 8, 1);
    set_word_at(m_, style + 0xc, 3);
    f_.set_rgb(style + 4, 0x80, 0x80, 0x80);
    set_word_at(m_, style + 0x10, 0);
    set_word_at(m_, style + 0x14, 0);
    set_byte_at(m_, style + 0x25, 0);
    set_word_at(m_, style + 0x34, 0xc00);
    set_word_at(m_, style + 0x150, 0xc00);
    set_word_at(m_, style + 0x154, 0xc00);
    memory = operator_new(0x17c);
    std::uint32_t second = 0;
    if (memory != 0) {
        second = menu_.properties_construct(memory);
    }
    create_screens_save_save_at(m_, self).set_field_350(static_cast<Word>(second));
    const std::uint32_t source = create_screens_save_save_at(m_, self).field_34c();
    menu_.copy_font_fields(second + 4, source + 4);
    for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
        set_word_at(m_, second + offset, word_at(m_, source + offset));
    }
    set_word_at(m_, second + 0x158, 0x91);
    set_word_at(m_, second + 0x15c, 0x12);
    set_word_at(m_, second + 0x160, 0xf);
    memory = operator_new(0x54);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = menu_.text_construct(memory, 0, global_word(m_, 0x0052e71c), create_screens_save_save_at(m_, self).field_34c());
    }
    create_screens_save_save_at(m_, self).set_field_354(static_cast<Word>(text));
    memory = operator_new(0x54);
    text = 0;
    if (memory != 0) {
        text = menu_.text_construct(memory, 0xff, global_word(m_, 0x0052e354), create_screens_save_save_at(m_, self).field_34c());
    }
    create_screens_save_save_at(m_, self).set_field_358(static_cast<Word>(text));
    for (std::uint32_t slot : {kTextA, kTextB}) {
        if (word_at(m_, self + slot) != 0) {
            vcall_list("list_v6", create_screens_save_save_at(m_, self).field_348(), 6, {word_at(m_, self + slot)});
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
    vcall_list("list_v1", create_screens_save_save_at(m_, self).field_348(), 1, {create_screens_save_save_at(m_, self).field_35c(), create_screens_save_save_at(m_, self).field_360(), 5});
    vcall_list("list_v2", create_screens_save_save_at(m_, self).field_348(), 2);
    memory = operator_new(0x428);
    std::uint32_t box = 0;
    if (memory != 0) {
        box = confirm_construct(memory, self + kFontA, global_word(m_, 0x0052e724), 0x100, 0x78);
    }
    create_screens_save_save_at(m_, self).set_field_368(static_cast<Word>(box));
    const struct {
        std::uint32_t file;
        std::int32_t x;
    } prompts[] = {{0x0052541c, 200}, {0x0052540c, 400}, {0x00525400, 0x32}};
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::uint32_t window = prompt(prompts[i].file, 0x005252f0, self + kFontB, prompts[i].x, 0xdc);
        set_word_at(m_, self + kPrompts + 4 * i, window);
        w_.window_add(self, window);
    }
    return self;
}

std::uint32_t CreateScreens::save_delete(std::uint32_t self, std::uint32_t flag) {
    save_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void CreateScreens::save_destroy(std::uint32_t self) {
    create_screens_save_save_at(m_, self).set_field_0(static_cast<Word>(kVtCreateSave));
    check(create_screens_save_save_at(m_, self).field_ac() != 0, kMsgNoBackground);
    const std::uint32_t sprite = create_screens_save_save_at(m_, self).field_ac();
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    if (create_screens_save_save_at(m_, self).field_348() != 0) {
        vcall_list("virtual_delete", create_screens_save_save_at(m_, self).field_348(), 0, {1});
    }
    for (std::uint32_t slot : {kStyleA, kStyleB, kArrowA, kArrowB, kTextA, kTextB}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    if (create_screens_save_save_at(m_, self).field_368() != 0) {
        confirm_delete(create_screens_save_save_at(m_, self).field_368(), 1);
    }
    font_destroy(self + kFontB);
    font_destroy(self + kFontA);
    w_.window_destroy(self);
}

// 0x0042c860: draw what the state shows (the list, the warning box or the memory-card screen), the window, then the background.
void CreateScreens::save_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    switch (create_screens_save_save_at(m_, self).field_364()) {
    case 0:
        save_draw_list(self);
        break;
    case 1:
        save_draw_box(self);
        break;
    case 2:
        save_mmu_thunk();
        break;
    default:
        break;
    }
    w_.window_draw(self, x, y);
    check(create_screens_save_save_at(m_, self).field_ac() != 0, kMsgNoBackground);
    engine(0x004d7990, {create_screens_save_save_at(m_, self).field_ac(), word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
}

// 0x0042c910: the prompt windows are shown only on the card screen (state 2), then the window's children update.
void CreateScreens::save_update(std::uint32_t self) {
    for (std::uint32_t i = 0; i < 3; ++i) {
        set_word_at(m_, word_at(m_, self + kPrompts + 4 * i) + 0x14, create_screens_save_save_at(m_, self).field_364() == 2 ? 1 : 0);
    }
    w_.window_update(self);
}

void CreateScreens::save_draw_list(std::uint32_t self) { vcall_list("list_v3", word_at(m_, self + kList), 3); }

void CreateScreens::save_draw_box(std::uint32_t self) { confirm_draw(word_at(m_, self + kBox)); }

void CreateScreens::save_mmu_thunk() { engine(0x0047ace0); }

// 0x0042c940: state 0 is the list (confirm on the first item starts the card save, on the second asks for a confirmation, back leaves), state 1
// the warning box (yes leaves the screen, no goes back to the list), state 2 the memory-card screen.
std::uint32_t CreateScreens::save_step(std::uint32_t self) {
    switch (create_screens_save_save_at(m_, self).field_364()) {
    case 0: {
        if (global_byte(m_, kPadUp) != 0 || global_byte(m_, kPadUp2) != 0) {
            sfx(0x90);
        }
        s_.note(kSvcPadEdges, {0, 0, 6});
        const std::uint32_t edge = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
        vcall_list("list_v5", create_screens_save_save_at(m_, self).field_348(), 5, {edge});
        vcall_list("list_v4", create_screens_save_save_at(m_, self).field_348(), 4);
        if (global_byte(m_, kPadConfirmEdge) != 0) {
            sfx(0x91);
            set_global_byte(m_, kPadConfirmEdge, 0);
            const std::uint32_t id = menu_.list_highlighted_id(create_screens_save_save_at(m_, self).field_348());
            if (id == 0) {
                engine(0x0047a0b0, {0x12, 0x12, 1});
                create_screens_save_save_at(m_, self).set_field_364(2);
            } else if (id == 0xff) {
                confirm_highlight(create_screens_save_save_at(m_, self).field_368(), 0);
                create_screens_save_save_at(m_, self).set_field_364(1);
            }
        }
        if (global_byte(m_, kPadBackEdge) != 0) {
            sfx(0x93);
            set_global_byte(m_, kPadBackEdge, 0);
            return 2;
        }
        return 0;
    }
    case 1: {
        if (global_byte(m_, kPadUp) != 0 || global_byte(m_, kPadUp2) != 0) {
            sfx(0x90);
        }
        s_.note(kSvcPadEdges, {0, 0, 6});
        const std::int32_t edge = static_cast<std::int32_t>(s_.value(kSvcPadEdges));
        const std::uint32_t answer = confirm_poll(create_screens_save_save_at(m_, self).field_368(), edge, -1);
        confirm_update(create_screens_save_save_at(m_, self).field_368());
        if (answer == 1) {
            return 1;
        }
        if (answer == 2) {
            create_screens_save_save_at(m_, self).set_field_364(0);
        }
        return 0;
    }
    case 2: {
        const std::uint32_t answer = engine_value(0x0047ac60);
        if (answer == 1 || answer == 2) {
            engine(0x0047ae10, {answer});
            engine(0x00489670, {0x0056aff8});
            engine(0x00489670, {0x0056b164});
            if (answer == 1) {
                return 1;
            }
            create_screens_save_save_at(m_, self).set_field_364(0);
        }
        return 0;
    }
    default:
        return 0;
    }
}

// 0x0042cb80: the save-skater screen: the four fonts, the window, frames until the step answers 1 or 2, then the unload.
std::int32_t CreateScreens::save_run() {
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    for (std::uint32_t font : {0x00525458, 0x0052544c, 0x00526acc, 0x005254e0}) {
        font_load(font);
    }
    const std::uint32_t memory = operator_new(0x378);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = save_construct(memory);
    }
    engine(0x00489670, {0x0056aff8});
    engine(0x00489670, {0x0056b164});
    const std::int32_t result = frame_loop(
        [&] { return static_cast<std::int32_t>(seams_ ? engine_value(0x0042c940, {window}) : save_step(window)); },
        [&] {
            if (seams_) {
                engine(0x0042c910, {window});
            } else {
                save_update(window);
            }
        },
        [&] {
            if (seams_) {
                engine(0x0042c860, {window, 0, 0});
            } else {
                save_draw(window, 0, 0);
            }
        });
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (window != 0) {
        if (seams_) {
            engine(0x0042c6f0, {window, 1});
        } else {
            save_delete(window, 1);
        }
    }
    for (std::uint32_t font : {0x00525458, 0x0052544c, 0x00526acc, 0x005254e0}) {
        font_unload(font);
    }
    return result != 2 ? 0 : -1;
}

}  // namespace opentony::runtime::frontend
