#include "auto_views.hpp"
#include "create_screens.hpp"
#include "seam_addresses_generated.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgNoAnimRegion = 0x00527860;
constexpr std::uint32_t kMsgNoCustomSkater = 0x00539da0;
constexpr std::uint32_t kMsgOnlyPlayerZero = 0x00539dd4;
constexpr std::uint32_t kMsgEmpty = 0x0055fe10;

// StatEdit fields
constexpr std::uint32_t kBackground = 0xac;
constexpr std::uint32_t kFontA = 0xb0;
constexpr std::uint32_t kFontB = 0x1fc;
constexpr std::uint32_t kAnimRegion = 0x348;
constexpr std::uint32_t kModelRegion = 0x34c;
constexpr std::uint32_t kSkaterObject = 0x350;
constexpr std::uint32_t kView = 0x354;
constexpr std::uint32_t kLights = 0x368;
constexpr std::uint32_t kPage = 0x39c;
// StatPage fields
constexpr std::uint32_t kPageFontA = 0xac;
constexpr std::uint32_t kPageFontB = 0x1f8;
constexpr std::uint32_t kPageFontC = 0x344;
constexpr std::uint32_t kPageList = 0x490;
constexpr std::uint32_t kPageRows = 0x494;
constexpr std::uint32_t kPageStyle = 0x614;
constexpr std::uint32_t kPageDots = 0x618;
constexpr std::uint32_t kPagePoints = 0x61c;
constexpr std::uint32_t kPageDialog = 0x620;
constexpr std::uint32_t kPageDialogResult = 0x624;
constexpr std::uint32_t kPageArrowA = 0x628;
constexpr std::uint32_t kPageArrowB = 0x62c;

// The scratch region the screens' stack locals (the skater's name) live in during a replay (outside every region a replay compares).
constexpr std::uint32_t kScratch = 0x7ff00400;

// The two part manifests' name arrays and counts (stack locals of the retail function; live they are in pc_platform's scratch window, clear of every Kit's area).
constexpr std::uint32_t kManifestNamesA = 0x7ff0a000;
constexpr std::uint32_t kManifestCountA = 0x7ff0a200;
constexpr std::uint32_t kManifestNamesB = 0x7ff0a400;
constexpr std::uint32_t kManifestCountB = 0x7ff0a600;

}  // namespace

// ---- the stat window (statwin.cpp) -----------------------------------------------------------------------------------------------------

// 0x004b5bb0: three fonts (s2menu1, s2font3 and s2font1 copied in), the dots and arrow sprites, the points block, then the list of sliders.
std::uint32_t CreateScreens::stat_page_construct(std::uint32_t self, std::int32_t x, std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    font_construct(self + kPageFontA);
    font_construct(self + kPageFontB);
    font_construct(self + kPageFontC);
    create_screens_stat_stat_at(m_, self).set_field_0(static_cast<Word>(kVtStatPage));
    copy_font_named(self + kPageFontA, 0x00527960);
    set_word_at(m_, self + kPageFontA + 4, 0);
    set_word_at(m_, self + kPageFontA + 8, 3);
    f_.set_rgb(self + kPageFontA, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kPageFontA + 0x21, 0);
    set_word_at(m_, self + kPageFontA + 0xc, 0);
    set_word_at(m_, self + kPageFontA + 0x10, 0);
    set_word_at(m_, self + kPageFontA + 0x30, 0x1000);
    copy_font_named(self + kPageFontB, 0x0052796c);
    set_word_at(m_, self + kPageFontB + 4, 2);
    set_word_at(m_, self + kPageFontB + 8, 3);
    f_.set_rgb(self + kPageFontB, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kPageFontB + 0x21, 0);
    set_word_at(m_, self + kPageFontB + 0xc, 0);
    copy_font_named(self + kPageFontC, 0x00526acc);
    set_word_at(m_, self + kPageFontC + 4, 1);
    set_word_at(m_, self + kPageFontC + 8, 3);
    f_.set_rgb(self + kPageFontC, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kPageFontC + 0x21, 0);
    set_word_at(m_, self + kPageFontC + 0xc, 0);
    set_word_at(m_, self + kPageFontC + 0x10, 0);
    set_word_at(m_, self + kPageFontC + 0x30, 0x1000);
    auto sprite = [&](std::uint32_t slot, std::uint32_t file, bool has_mode, std::uint32_t mode) {
        const std::uint32_t memory = operator_new(0x5c);
        std::uint32_t object = 0;
        if (memory != 0) {
            s_.note(kSpriteConstruct, {memory, file, 1, 0, 0}, m_.cstring(file));
            object = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, object);
        if (has_mode) {
            set_byte_at(m_, object + 0x0a, mode);
            set_byte_at(m_, object + 0x0b, 1);
        }
        s_.note(kSpriteInit, {object});
    };
    sprite(kPageDots, 0x0052d84c, false, 0);
    sprite(kPageArrowA, 0x00525908, true, 2);
    sprite(kPageArrowB, 0x005258f8, true, 0);
    const std::uint32_t memory = operator_new(0x14c);
    std::uint32_t points = 0;
    if (memory != 0) {
        points = engine_value(0x004b6d90, {memory, self + kPageFontA, self + kPageFontB, 0x12d, 0xa1, 0xffffffffu, 0xffffffffu});
    }
    create_screens_stat_stat_at(m_, self).set_field_61c(static_cast<Word>(points));
    w_.window_add(self, points);
    create_screens_stat_stat_at(m_, self).set_field_620(0);
    create_screens_stat_stat_at(m_, self).set_field_624(0);
    engine(0x004b6560, {self});
    return self;
}

std::uint32_t CreateScreens::stat_page_delete(std::uint32_t self, std::uint32_t flag) {
    stat_page_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void CreateScreens::stat_page_destroy(std::uint32_t self) {
    create_screens_stat_stat_at(m_, self).set_field_0(static_cast<Word>(kVtStatPage));
    if (create_screens_stat_stat_at(m_, self).field_490() != 0) {
        vcall_list("virtual_delete", create_screens_stat_stat_at(m_, self).field_490(), 0, {1});
    }
    if (create_screens_stat_stat_at(m_, self).field_614() != 0) {
        delete_object(create_screens_stat_stat_at(m_, self).field_614());
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t row = word_at(m_, self + kPageRows + 4 * i);
        if (row != 0) {
            delete_object(row);
        }
    }
    for (std::uint32_t slot : {kPageDialog, kPageDots, kPageArrowA, kPageArrowB}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    font_destroy(self + kPageFontC);
    font_destroy(self + kPageFontB);
    font_destroy(self + kPageFontA);
    w_.window_destroy(self);
}

// ---- the stat editor (statedit.cpp) ----------------------------------------------------------------------------------------------------

// 0x004b5ba0: a screen callback: run the stat editor and hand back its answer.
void CreateScreens::stat_screen_call(std::uint32_t unused, std::uint32_t result) {
    (void)unused;
    set_word_at(m_, result, u(stat_run()));
}

// 0x004b7430: the stat editor window: its scene and fonts, the stat window, the skater's name and the face picture, the four prompt windows
// (select, change, accept, back) and the background.
std::uint32_t CreateScreens::stat_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontA);
    font_construct(self + kFontB);
    create_screens_stat_stat_at(m_, self).set_field_0(static_cast<Word>(kVtStatEdit));
    init_view(self + kView);
    helpers_.light_defaults(self + kLights);
    stat_setup_scene(self);
    stat_load_fonts(self);
    std::uint32_t memory = operator_new(0x630);
    std::uint32_t page = 0;
    if (memory != 0) {
        page = stat_page_construct(memory, 0, 0);
    }
    create_screens_stat_stat_at(m_, self).set_field_39c(static_cast<Word>(page));
    w_.window_add(self, page);
    m_.ensure_region(kScratch, 0x100);
    sk_.format_name(static_cast<int>(global_word(m_, kPadCurrent)), kScratch);
    f_.ellipsize(self + kFontA, kScratch, 0x100);
    memory = operator_new(0x30);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = w_.text_construct(memory, kScratch, self + kFontA, 0xaa, 0x23, -1, -1);
    }
    w_.window_add(self, text);
    memory = operator_new(0x2c);
    std::uint32_t face = 0;
    if (memory != 0) {
        face = w_.sprite_construct(memory, 0x00527850, 0x20, 0x2a, static_cast<std::int32_t>(0xffffff00), -1, 0, 0);
    }
    w_.window_add(self, face);
    w_.window_add(self, prompt(0x0052541c, global_word(m_, 0x0052e3f0), self + kFontB, 0x78, 0xe0));
    w_.window_add(self, prompt(0x005258d8, global_word(m_, 0x0052e3fc), self + kFontB, 0x104, 0xe0));
    w_.window_add(self, prompt(0x0052540c, global_word(m_, 0x0052e3f4), self + kFontB, 0x190, 0xe0));
    w_.window_add(self, prompt(0x00525400, global_word(m_, 0x0052e3f8), self + kFontB, 0x1e, 0xe0));
    create_screens_stat_stat_at(m_, self).set_field_ac(static_cast<Word>(background_sprite()));
    return self;
}

std::uint32_t CreateScreens::stat_delete(std::uint32_t self, std::uint32_t flag) {
    stat_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004b77d0: release the animation region, the skater object, the background and the skater's regions (a custom skater is freed as an object).
void CreateScreens::stat_destroy(std::uint32_t self) {
    create_screens_stat_stat_at(m_, self).set_field_0(static_cast<Word>(kVtStatEdit));
    check(static_cast<std::int32_t>(create_screens_stat_stat_at(m_, self).field_348()) != -1, kMsgNoAnimRegion);
    engine(0x004b32f0, {create_screens_stat_stat_at(m_, self).field_348(), 1, 0});
    if (create_screens_stat_stat_at(m_, self).field_350() != 0) {
        delete_object(create_screens_stat_stat_at(m_, self).field_350());
    }
    const std::uint32_t sprite = create_screens_stat_stat_at(m_, self).field_ac();
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    const std::int32_t character = sk_.current_character();
    if ((word_at(m_, skater::row(character) + skater::kRowFlags) & 2) == 0) {
        engine(0x004b32f0, {create_screens_stat_stat_at(m_, self).field_34c(), 1, 0});
        engine(0x004b32f0, {create_screens_stat_stat_at(m_, self).field_348(), 1, 0});
    } else {
        check(global_word(m_, kGlobalCustomSkater) != 0, kMsgNoCustomSkater);
        engine(0x00422ec0, {global_word(m_, kGlobalCustomSkater)});
        set_global_word(m_, kGlobalCustomSkater, 0);
    }
    font_destroy(self + kFontB);
    font_destroy(self + kFontA);
    w_.window_destroy(self);
}

// 0x004b7960: the stat editor's two fonts: s2font1 (centred top, scale 0.5) and s2font2 (left, scale 0.5).
void CreateScreens::stat_load_fonts(std::uint32_t self) {
    copy_font_named(self + kFontA, 0x00526acc);
    set_word_at(m_, self + kFontA + 4, 1);
    set_word_at(m_, self + kFontA + 8, 3);
    f_.set_rgb(self + kFontA, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontA + 0x21, 0);
    set_word_at(m_, self + kFontA + 0xc, 0);
    set_word_at(m_, self + kFontA + 0x10, 0);
    set_word_at(m_, self + kFontA + 0x30, 0x800);
    copy_font_named(self + kFontB, 0x0052544c);
    set_word_at(m_, self + kFontB + 4, 0);
    set_word_at(m_, self + kFontB + 8, 3);
    f_.set_rgb(self + kFontB, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x800);
}

// 0x004b7b50: build the skater the editor shows: a character from its model files (merged with the shared animation parts) or a custom skater;
// both then take the face bitmap of the character's busiest material. The trick screen's scene (0x004bfd30) is the same code with the screen's own
// fields: `anim_at`, `model_at` and `object_at` are the addresses of the screen's animation region, model region and skater object words, `lights` its light rig.
void CreateScreens::stat_setup_scene(std::uint32_t self) { preview_scene(self + kAnimRegion, self + kModelRegion, self + kSkaterObject, self + kLights); }

void CreateScreens::preview_scene(std::uint32_t anim_at, std::uint32_t model_at, std::uint32_t object_at, std::uint32_t lights) {
    const std::uint32_t anims = engine_value(0x004b46a0, {0x00525ffc, 0}, m_.cstring(0x00525ffc, 0x40));
    set_word_at(m_, anim_at, anims);
    set_global_word(m_, 0x0056865c, 0x100);
    engine(0x004f5430, {0x0055f7fc});
    set_global_word(m_, 0x0055f7cc, 0);
    set_global_word(m_, 0x0055f7d0, 0xffffffbf);
    set_global_word(m_, 0x0055f7d4, 0xfffffee0);
    engine(0x004e8460, {0x0055f7fc, 0x0055f7cc});
    engine(0x00464710, {10, 6000, 0x800});
    engine(0x004645f0);
    const std::uint32_t storage = engine_value(0x0047fd30, {0x168});
    std::uint32_t object = 0;
    if (storage != 0) {
        object = engine_value(0x00480610, {storage});
    }
    set_word_at(m_, object_at, object);
    set_word_at(m_, object + 0x40, lights);
    set_word_at(m_, object + 8, 0);
    set_word_at(m_, object + 0xc, 0);
    set_word_at(m_, object + 0x10, 0);
    std::int32_t character = sk_.current_character();
    std::uint32_t material = 0;
    if ((word_at(m_, skater::row(character) + skater::kRowFlags) & 2) != 0) {
        const std::int32_t index = sk_.custom_index(character);
        const std::uint32_t memory = operator_new(0x2c);
        std::uint32_t holder = 0;
        if (memory != 0) {
            holder = engine_value(0x0042f880, {memory});
        }
        engine(0x0042f950, {holder, u(sk_.current_custom_slot()), 0, u(index)});
        if (holder != 0) {
            delete_object(holder);
        }
        const std::int32_t index2 = sk_.custom_index(sk_.current_character());
        const std::uint32_t custom = engine_value(0x00423800, {u(index2), u(index), 1, 0, global_word(m_, kGlobalCachedDatabase), 0});
        set_global_word(m_, kGlobalCustomSkater, custom);
        set_word_at(m_, model_at, engine_value(0x004203c0, {custom}));
        engine(0x00423be0, {global_word(m_, kGlobalCustomSkater), word_at(m_, object_at)});
        engine(0x00452390, {word_at(m_, model_at), word_at(m_, anim_at), 1});
        material = word_at(m_, global_word(m_, kGlobalCustomSkater) + 0x6c);
    } else {
        const std::uint32_t name = sk_.select_model_name(character, sk_.current_costume());
        set_word_at(m_, model_at, engine_value(0x004b43e0, {name, global_word(m_, kPadCurrent)}, m_.cstring(name, 0x40)));
        // the model's name with the player's digit in front is the PSX region the object takes
        std::string region = m_.cstring(name, 0x1f);
        if (region.empty()) {
            region.push_back('\0');
        }
        region[0] = static_cast<char>(static_cast<std::uint8_t>(global_word(m_, kPadCurrent)) + 0x30);
        engine_named(0x0047fe30, {word_at(m_, object_at)}, region);
        // the two part manifests (the shared animation parts, then the model's), each with the count the loader wrote
        const PartManifest manifest_a = load_part_manifest(0x0052c9f8, kManifestNamesA, kManifestCountA);
        const PartManifest manifest_b = load_part_manifest(name, kManifestNamesB, kManifestCountB);
        if (s_.live()) {
            engine(0x00480d90, {word_at(m_, object_at), kManifestNamesA, manifest_a.count, kManifestNamesB, manifest_b.count});
        } else {
            engine(0x00480d90, {word_at(m_, object_at), manifest_a.count, manifest_b.count});
        }
        s_.note(kMemDeleteService, {manifest_a.block});
        s_.note(kMemDeleteService, {manifest_b.block});
        engine(0x00452390, {word_at(m_, model_at), word_at(m_, anim_at), 0});
        material = engine_value(0x00418360, {u(sk_.current_character()), word_at(m_, object_at)});
    }
    check(material != 0, kMsgEmpty);
    if (material != 0) {
        engine(0x00418480, {material, u(sk_.current_character()), global_word(m_, kPadCurrent)});
    }
    object = word_at(m_, object_at);
    set_word_at(m_, object + 0x108, 0x10000);
    engine(0x00480890, {object, 0, 1});
    character = sk_.current_character();
    object = word_at(m_, object_at);
    std::uint32_t flags = word_at(m_, object + 0xd8);
    if ((word_at(m_, skater::row(character) + skater::kRowFlags) & 2) == 0 && byte_at(m_, skater::row(character) + skater::kRowSlotFlag) == 0) {
        flags |= 2;
    } else {
        flags &= ~2u;
    }
    set_word_at(m_, object + 0xd8, flags);
}

// 0x004b7f40: set the view (full width, half-width centre), render the skater, draw the window and the background.
void CreateScreens::stat_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t view = self + kView;
    set_half_at(m_, view + 4, 0x100);
    set_half_at(m_, view + 6, 0);
    set_half_at(m_, view, 0x200);
    set_half_at(m_, view + 2, 0xf0);
    set_half_at(m_, view + 4, (global_word(m_, kGlobalScreenWidth) & 0xffffff) >> 1);
    set_half_at(m_, view, global_word(m_, kGlobalScreenWidth) & 0x7fffff);
    set_half_at(m_, view + 6, 0);
    set_half_at(m_, view + 0xc, 0x6aa);
    set_half_at(m_, view + 2, (global_word(m_, kGlobalScreenHeight) * 0xf0u) / 0xf0u);
    engine(0x0045e8e0, {0x0055f7c8, view, word_at(m_, global_word(m_, kGlobalRenderer) + 0x88)});
    engine(0x00461bf0, {create_screens_stat_stat_at(m_, self).field_350()});
    engine(0x0045f530, {create_screens_stat_stat_at(m_, self).field_350()});
    engine(0x0045f3f0);
    w_.window_draw(self, u(static_cast<std::int32_t>(create_screens_stat_stat_at(m_, self).field_20()) + x), u(static_cast<std::int32_t>(create_screens_stat_stat_at(m_, self).field_24()) + y));
    draw_background(create_screens_stat_stat_at(m_, self).field_ac());
}

// 0x004b8020: one frame: the animation hooks, the pad's two spin buttons turn the skater, the custom-skater list is trimmed, the window updates.
void CreateScreens::stat_update_skater(std::uint32_t self) {
    const std::uint32_t object = create_screens_stat_stat_at(m_, self).field_350();
    engine(0x00480fa0, {object});
    if (global_byte(m_, kPadSpinLeft) != 0) {
        set_half_at(m_, object + 0x16, half_at(m_, object + 0x16) + 0x3c);
    }
    if (global_byte(m_, kPadSpinRight) != 0) {
        set_half_at(m_, object + 0x16, half_at(m_, object + 0x16) + 0xffc4);
    }
    if (global_word(m_, kGlobalCustomSkater) != 0) {
        engine(0x00423b50, {1, kGlobalCustomSkater});
    }
    w_.window_update(self);
}

// 0x004b8080: a tail jump into StatPage_Poll with the editor's page.
std::uint32_t CreateScreens::stat_step(std::uint32_t self) { return engine_value(0x004b6180, {word_at(m_, self + kPage)}); }

// 0x004b8090: the stat editor screen: load the fonts, build the window, run frames until the poll answers 1 (accept) or 2 (back), unload.
std::int32_t CreateScreens::stat_run() {
    engine(0x00423bd0);
    check(global_word(m_, kPadCurrent) == 0, kMsgOnlyPlayerZero);
    sk_.set_active_slot(static_cast<int>(global_word(m_, kPadCurrent)));
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    for (std::uint32_t font : {0x00526acc, 0x0052544c, 0x0052796c, 0x00527960, 0x00527950}) {
        font_load(font);
    }
    const std::uint32_t memory = operator_new(0x3a0);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = stat_construct(memory);
    }
    engine(0x00489670, {0x0056aff8});
    engine(0x00489670, {0x0056b164});
    engine(0x004c8f30);
    const std::int32_t result = frame_loop(
        [&] { return static_cast<std::int32_t>(seams_ ? engine_value(0x004b8080, {window}) : stat_step(window)); },
        [&] {
            if (seams_) {
                engine(0x004b8020, {window});
            } else {
                stat_update_skater(window);
            }
        },
        [&] {
            if (seams_) {
                engine(0x004b7f40, {window, 0, 0});
            } else {
                stat_draw(window, 0, 0);
            }
        });
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (window != 0) {
        if (seams_) {
            engine(0x004b77b0, {window, 1});
        } else {
            stat_delete(window, 1);
        }
    }
    for (std::uint32_t font : {0x00526acc, 0x0052544c, 0x0052796c, 0x00527960, 0x00527950}) {
        font_unload(font);
    }
    engine(0x0046f480);
    engine(0x00423bd0);
    return result != 2 ? 0 : -1;
}

}  // namespace opentony::runtime::frontend
