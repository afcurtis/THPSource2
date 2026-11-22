#include "auto_views.hpp"
#include "trick_screens.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgNoAnimRegion = 0x00527860;
constexpr std::uint32_t kMsgNoCustomSkater = 0x00539da0;

// TrickScreen fields
constexpr std::uint32_t kBackground = 0xac;
constexpr std::uint32_t kFont = 0xb0;
constexpr std::uint32_t kFace = 0x1fc;
constexpr std::uint32_t kMenu = 0x200;
constexpr std::uint32_t kAnimRegion = 0x204;
constexpr std::uint32_t kModelRegion = 0x208;
constexpr std::uint32_t kSkaterObject = 0x20c;
constexpr std::uint32_t kView = 0x210;
constexpr std::uint32_t kLights = 0x224;

// The scratch region the screens' stack locals (the skater's name) live in during a replay (outside every region a replay compares).
constexpr std::uint32_t kScratch = 0x7ff00400;

}  // namespace

// 0x004bf860: a screen callback: run the trick-list screen and hand back its answer.
void TrickScreens::screen_call(std::uint32_t unused, std::uint32_t result) {
    (void)unused;
    set_word_at(m_, result, u(run()));
}

// 0x004bf870: the screen window: its view and lights, the preview skater, the font, the trick menu, the active skater's name, the face picture and the
// background.
std::uint32_t TrickScreens::screen_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFont);
    trick_screens_screen_screen_at(m_, self).set_field_0(static_cast<Word>(kVtTrickScreen));
    init_view(self + kView);
    helpers_.light_defaults(self + kLights);
    screen_setup_scene(self);
    screen_load_font(self);
    m_.ensure_region(kScratch, 0x100);
    sk_.format_name(static_cast<int>(global_word(m_, kPadCurrent)), kScratch);
    f_.ellipsize(self + kFont, kScratch, 0x100);
    std::uint32_t memory = operator_new(0x2158);
    std::uint32_t menu = 0;
    if (memory != 0) {
        menu = menu_construct(memory, 0, 0);
    }
    trick_screens_screen_screen_at(m_, self).set_field_200(static_cast<Word>(menu));
    w_.window_add(self, menu);
    memory = operator_new(0x30);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = w_.text_construct(memory, kScratch, self + kFont, 0xaa, 0x23, -1, -1);
    }
    w_.window_add(self, text);
    memory = operator_new(0x2c);
    std::uint32_t face = 0;
    if (memory != 0) {
        face = w_.sprite_construct(memory, 0x00527850, 0x20, 0x2a, static_cast<std::int32_t>(0xffffff00), -1, 0, 0);
    }
    w_.window_add(self, face);
    trick_screens_screen_screen_at(m_, self).set_field_1fc(static_cast<Word>(face));
    trick_screens_screen_screen_at(m_, self).set_field_ac(static_cast<Word>(background_sprite()));
    return self;
}

std::uint32_t TrickScreens::screen_delete(std::uint32_t self, std::uint32_t flag) {
    screen_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004bfab0: release the animation region, the skater object, the background and the skater's regions (a custom skater is freed as an object).
void TrickScreens::screen_destroy(std::uint32_t self) {
    trick_screens_screen_screen_at(m_, self).set_field_0(static_cast<Word>(kVtTrickScreen));
    check(static_cast<std::int32_t>(trick_screens_screen_screen_at(m_, self).field_204()) != -1, kMsgNoAnimRegion);
    engine(0x004b32f0, {trick_screens_screen_screen_at(m_, self).field_204(), 1, 0});
    if (trick_screens_screen_screen_at(m_, self).field_20c() != 0) {
        delete_object(trick_screens_screen_screen_at(m_, self).field_20c());
    }
    const std::uint32_t sprite = trick_screens_screen_screen_at(m_, self).field_ac();
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    const std::int32_t character = sk_.current_character();
    if ((word_at(m_, skater::row(character) + skater::kRowFlags) & 2) == 0) {
        engine(0x004b32f0, {trick_screens_screen_screen_at(m_, self).field_208(), 1, 0});
        engine(0x004b32f0, {trick_screens_screen_screen_at(m_, self).field_204(), 1, 0});
    } else {
        check(global_word(m_, kGlobalCustomSkater) != 0, kMsgNoCustomSkater);
        engine(0x00422ec0, {global_word(m_, kGlobalCustomSkater)});
        set_global_word(m_, kGlobalCustomSkater, 0);
    }
    font_destroy(self + kFont);
    w_.window_destroy(self);
}

// 0x004bfc30: the font is s2font1, centred at the top (scale 1.0).
void TrickScreens::screen_load_font(std::uint32_t self) {
    copy_font_named(self + kFont, 0x00526acc);
    set_word_at(m_, self + kFont + 4, 1);
    set_word_at(m_, self + kFont + 8, 3);
    f_.set_rgb(self + kFont, 0x80, 0x80, 0x80);
    set_word_at(m_, self + kFont + 0x30, 0x1000);
    set_byte_at(m_, self + kFont + 0x21, 0);
    set_word_at(m_, self + kFont + 0xc, 0);
    set_word_at(m_, self + kFont + 0x10, 0);
}

// 0x004bfd30: the stat editor's scene (create_screens_stat.cpp) with this screen's fields.
void TrickScreens::screen_setup_scene(std::uint32_t self) {
    screens_.preview_scene(self + kAnimRegion, self + kModelRegion, self + kSkaterObject, self + kLights);
}

// 0x004c0120: set the view (full width, half-width centre), render the skater, draw the window and the background.
void TrickScreens::screen_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t view = self + kView;
    set_half_at(m_, view + 4, 0x100);
    set_half_at(m_, view + 6, 0);
    set_half_at(m_, view, 0x200);
    set_half_at(m_, view + 2, 0xf0);
    set_half_at(m_, view + 0xc, 0x6aa);
    set_half_at(m_, view + 4, (global_word(m_, kGlobalScreenWidth) & 0xffffff) >> 1);
    set_half_at(m_, view, global_word(m_, kGlobalScreenWidth) & 0x7fffff);
    set_half_at(m_, view + 6, 0);
    set_half_at(m_, view + 2, (global_word(m_, kGlobalScreenHeight) * 0xf0u) / 0xf0u);
    engine(0x0045e8e0, {0x0055f7c8, view, word_at(m_, global_word(m_, kGlobalRenderer) + 0x88)});
    engine(0x00461bf0, {trick_screens_screen_screen_at(m_, self).field_20c()});
    engine(0x0045f530, {trick_screens_screen_screen_at(m_, self).field_20c()});
    engine(0x0045f3f0);
    w_.window_draw(self, u(static_cast<std::int32_t>(trick_screens_screen_screen_at(m_, self).field_20()) + x), u(static_cast<std::int32_t>(trick_screens_screen_screen_at(m_, self).field_24()) + y));
    draw_background(trick_screens_screen_screen_at(m_, self).field_ac());
}

// 0x004c0200: one frame: the animation hooks, the custom-skater list is trimmed, the window updates.
void TrickScreens::screen_update_frame(std::uint32_t self) {
    engine(0x00480fa0, {trick_screens_screen_screen_at(m_, self).field_20c()});
    if (global_word(m_, kGlobalCustomSkater) != 0) {
        engine(0x00423b50, {1, kGlobalCustomSkater});
    }
    w_.window_update(self);
}

// 0x004c0230: a tail jump into the trick menu's update with the screen's menu.
std::uint32_t TrickScreens::screen_step(std::uint32_t self) { return menu_update(word_at(m_, self + kMenu)); }

// 0x004c0240: the trick-list screen: the six fonts, the window, frames until the menu answers 1 (continue) or 2 (back), then the unload.
std::int32_t TrickScreens::run() {
    static const std::uint32_t kFonts[] = {0x00526acc, 0x0052544c, 0x0052796c, 0x00527960, 0x00527940, 0x00527950};
    engine(0x00423bd0);
    sk_.set_active_slot(static_cast<int>(global_word(m_, kPadCurrent)));
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    for (std::uint32_t font : kFonts) {
        font_load(font);
    }
    const std::uint32_t memory = operator_new(0x258);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = screen_construct(memory);
    }
    engine(0x00489670, {0x0056aff8});
    engine(0x00489670, {0x0056b164});
    engine(0x004c8f30);
    const std::int32_t result = frame_loop(
        [&] { return static_cast<std::int32_t>(seams_ ? engine_value(0x004c0230, {window}) : screen_step(window)); },
        [&] {
            if (seams_) {
                engine(0x004c0200, {window});
            } else {
                screen_update_frame(window);
            }
        },
        [&] {
            if (seams_) {
                engine(0x004c0120, {window, 0, 0});
            } else {
                screen_draw(window, 0, 0);
            }
        });
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (window != 0) {
        if (seams_) {
            engine(0x004bfa90, {window, 1});
        } else {
            screen_delete(window, 1);
        }
    }
    for (std::uint32_t font : kFonts) {
        font_unload(font);
    }
    engine(0x0046f480);
    engine(0x00423bd0);
    return result != 2 ? 0 : -1;
}

}  // namespace opentony::runtime::frontend
