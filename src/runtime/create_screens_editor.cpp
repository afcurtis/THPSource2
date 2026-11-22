#include "auto_views.hpp"
#include "career.hpp"
#include "create_screens.hpp"
#include "seam_addresses_generated.hpp"

#include <string>
#include <tuple>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgNoAnimRegion = 0x00527860;
constexpr std::uint32_t kMsgNoAppearance = 0x005278b4;
constexpr std::uint32_t kMsgUnrecognized = 0x005278d4;
constexpr std::uint32_t kMsgInvalidEditor = 0x00527920;
constexpr std::uint32_t kMsgDatabaseExists = 0x005279a4;
constexpr std::uint32_t kMsgNoCachedDatabase = 0x00527984;
constexpr std::uint32_t kMsgEmpty = 0x0055fe10;
constexpr std::uint32_t kLogUnlocked = 0x005278e8;
constexpr std::uint32_t kLogBoardTextures = 0x0052788c;
constexpr std::uint32_t kLogActivePlayer = 0x00527904;
constexpr std::uint32_t kFormatContinue = 0x005279f8;
constexpr std::uint32_t kFontsCreate[] = {0x00526acc, 0x0052544c, 0x0052796c, 0x00527960, 0x00527950, 0x00527940};
constexpr std::uint32_t kCreatePre = 0x00527978;
constexpr std::uint32_t kAnimsFe = 0x00525ffc;

constexpr std::uint32_t kCustomRecords = 0x0056a488;
constexpr std::uint32_t kCustomStride = 0x80;
constexpr std::uint32_t kCareerStats = 0x38;

// CreateEditor fields
constexpr std::uint32_t kBackground = 0xac;
constexpr std::uint32_t kFontA = 0xb0;
constexpr std::uint32_t kFontB = 0x1fc;
constexpr std::uint32_t kPromptFirst = 0x348;
constexpr std::uint32_t kSkaterObject = 0x360;
constexpr std::uint32_t kAnimObject = 0x364;
constexpr std::uint32_t kAnimRegion = 0x368;
constexpr std::uint32_t kPage = 0x36c;
constexpr std::uint32_t kMode = 0x370;
constexpr std::uint32_t kView = 0x374;
constexpr std::uint32_t kLights = 0x388;
constexpr std::uint32_t kSpoolBusy = 0x3bc;
constexpr std::uint32_t kSpoolBusyBefore = 0x3c0;
constexpr std::uint32_t kStatPage = 0x3c4;
constexpr std::uint32_t kNameText = 0x3c8;
constexpr std::uint32_t kFaceSprite = 0x3cc;
constexpr std::uint32_t kPersonalPage = 0x3d0;
constexpr std::uint32_t kTrickPage = 0x3d4;
constexpr std::uint32_t kAppearancePage = 0x3d8;
constexpr std::uint32_t kBoardByte = 0x3dc;
constexpr std::uint32_t kBuild = 0x3e0;
// PersonalPage fields
constexpr std::uint32_t kPFont = 0xb0;
constexpr std::uint32_t kPList = 0x1fc;
constexpr std::uint32_t kPRows = 0x200;
constexpr std::uint32_t kPStyleA = 0x380;
constexpr std::uint32_t kPStyleB = 0x384;
constexpr std::uint32_t kPArrowLeft = 0x388;
constexpr std::uint32_t kPArrowRight = 0x38c;
constexpr std::uint32_t kPNameEntry = 0x390;
constexpr std::uint32_t kPState = 0x394;
constexpr std::uint32_t kPIcons = 0x398;

constexpr std::uint32_t kScratch = 0x7ff00400;

std::uint32_t record(std::uint32_t slot) { return kCustomRecords + slot * kCustomStride; }

}  // namespace

// ---- the personal-information page (creedit.cpp's share of perwin) ------------------------------------------------------------------

// 0x004288a0: the page: a font, two arrow sprites, five icons beside the rows, the name entry keyboard, then the list.
std::uint32_t CreateScreens::personal_construct(std::uint32_t self, std::int32_t x, std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    font_construct(self + kPFont);
    create_screens_editor_personal_at(m_, self).set_field_0(0x005187c8);
    create_screens_editor_personal_at(m_, self).set_field_394(0);
    copy_font_named(self + kPFont, 0x00526acc);
    set_word_at(m_, self + kPFont + 4, 1);
    set_word_at(m_, self + kPFont + 8, 3);
    f_.set_rgb(self + kPFont, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kPFont + 0x21, 0);
    set_word_at(m_, self + kPFont + 0xc, 0);
    set_word_at(m_, self + kPFont + 0x10, 0);
    set_word_at(m_, self + kPFont + 0x30, 0x1000);
    auto arrow = [&](std::uint32_t slot, std::uint32_t file, std::uint32_t mode) {
        const std::uint32_t memory = operator_new(0x5c);
        std::uint32_t sprite = 0;
        if (memory != 0) {
            s_.note(kSpriteConstruct, {memory, file, 1, 0, 0}, m_.cstring(file));
            sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, sprite);
        set_byte_at(m_, sprite + 0x0a, mode);
        set_byte_at(m_, sprite + 0x0b, 1);
        s_.note(kSpriteInit, {sprite});
    };
    arrow(kPArrowLeft, 0x00525908, 2);
    arrow(kPArrowRight, 0x005258f8, 0);
    const struct {
        std::uint32_t file;
        std::int32_t y;
    } icons[] = {{0x005279ec, 0x57}, {0x005279e0, 0x57}, {0x005279d4, 0x57}, {0x005279c8, 0x6f}, {0x005279bc, 0x6f}};
    for (std::uint32_t i = 0; i < 5; ++i) {
        const std::uint32_t memory = operator_new(0x2c);
        std::uint32_t icon = 0;
        if (memory != 0) {
            icon = w_.sprite_construct(memory, icons[i].file, 0xca, icons[i].y, -1, -1, 1, 0);
        }
        set_word_at(m_, self + kPIcons + 4 * i, icon);
        w_.window_add(self, icon);
    }
    const std::uint32_t memory = operator_new(0x374);
    std::uint32_t entry = 0;
    if (memory != 0) {
        entry = menu_.name_entry_construct(memory, self + kPFont, 0xf, 0x100, 0x50, 0x100, 0x96, 1, 1, 1, 1, 1);
    }
    create_screens_editor_personal_at(m_, self).set_field_390(static_cast<Word>(entry));
    menu_.name_entry_set_name(entry, record(static_cast<std::uint32_t>(sk_.current_custom_slot())));
    menu_.name_entry_set_prompt(entry, global_word(m_, 0x0052e710), 0x100, 0x3c);
    personal_build_list(self);
    return self;
}

std::uint32_t CreateScreens::personal_delete(std::uint32_t self, std::uint32_t flag) {
    personal_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00428ca0: the two styles (s2menu1 right aligned, s2skata2 centred, both orange) and the rows of the list: name, hometown, age, style, stance,
// weight (each of the style and stance rows is followed by a blank text the page fills in) and the continue row.
void CreateScreens::personal_build_list(std::uint32_t self) {
    std::uint32_t memory = operator_new(0x350);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, 0x2a, 0x2f, 0xef, 0x96, 0, 3, 6, 1);
    }
    create_screens_editor_personal_at(m_, self).set_field_1fc(static_cast<Word>(list));
    auto style = [&](std::uint32_t slot, std::uint32_t font_name, std::uint32_t justification) {
        const std::uint32_t block = operator_new(0x17c);
        std::uint32_t props = 0;
        if (block != 0) {
            props = menu_.properties_construct(block);
        }
        set_word_at(m_, self + slot, props);
        menu_.copy_font_fields(props + 4, find_font(font_name));
        set_word_at(m_, props + 8, justification);
        set_word_at(m_, props + 0xc, 3);
        f_.set_rgb(props + 4, 0xff, 0x60, 0x20);
        set_byte_at(m_, props + 0x25, 0);
        set_word_at(m_, props + 0x10, 0);
        set_word_at(m_, props + 0x14, 0);
        set_word_at(m_, props + 0x34, 0x1000);
        set_word_at(m_, props + 0x150, 0x1324);
        set_word_at(m_, props + 0x154, 0x1000);
    };
    style(kPStyleA, 0x00527960, 2);
    style(kPStyleB, 0x00527950, 1);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kPRows + 4 * i, 0);
    }
    // sprintf(buffer, "         %s", the continue text)
    std::string continue_text = m_.cstring(kFormatContinue, 0x40);
    const std::size_t at = continue_text.find("%s");
    if (at != std::string::npos) {
        continue_text.replace(at, 2, m_.cstring(global_word(m_, 0x0052e968), 0x100));
    }
    const std::uint32_t props = create_screens_editor_personal_at(m_, self).field_380();
    auto text_row = [&](std::uint32_t slot, std::uint32_t id, std::uint32_t text, bool hidden) {
        const std::uint32_t block = operator_new(0x54);
        std::uint32_t item = 0;
        if (block != 0) {
            item = menu_.text_construct(block, id, text, props);
        }
        set_word_at(m_, self + slot, item);
        if (hidden) {
            set_byte_at(m_, item + 0xc, 0);
        }
    };
    auto row = [&](std::uint32_t slot, std::uint32_t id, std::uint32_t text, std::uint32_t left, std::uint32_t right) {
        const std::uint32_t block = operator_new(0x100);
        std::uint32_t item = 0;
        if (block != 0) {
            item = menu_.row_construct(block, id, text, props, 2, left, right);
        }
        set_word_at(m_, self + slot, item);
    };
    row(0x200, 0, global_word(m_, 0x0052e6b8), 0, 0);
    row(0x204, 1, global_word(m_, 0x0052e6bc), 0, 0);
    {
        const std::uint32_t block = operator_new(0x114);
        std::uint32_t item = 0;
        if (block != 0) {
            item = helpers_.number_construct(block, 2, global_word(m_, 0x0052e6c0), sbyte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x20), 5,
                                             0x37, 0xa0, create_screens_editor_personal_at(m_, self).field_380(), create_screens_editor_personal_at(m_, self).field_384(), create_screens_editor_personal_at(m_, self).field_388(),
                                             create_screens_editor_personal_at(m_, self).field_38c());
        }
        create_screens_editor_personal_at(m_, self).set_field_208(static_cast<Word>(item));
    }
    text_row(0x20c, 3, global_word(m_, 0x0052e6c4), false);
    text_row(0x210, 0xfffffffe, kEmptyString, true);
    text_row(0x214, 4, global_word(m_, 0x0052e6c8), false);
    text_row(0x218, 0xfffffffe, kEmptyString, true);
    row(0x21c, 5, global_word(m_, 0x0052e6cc), create_screens_editor_personal_at(m_, self).field_388(), create_screens_editor_personal_at(m_, self).field_38c());
    {
        const std::uint32_t block = operator_new(0x54);
        std::uint32_t item = 0;
        if (block != 0) {
            item = menu_.text_construct_from(block, 0xffffffff, continue_text, props);
        }
        create_screens_editor_personal_at(m_, self).set_field_224(static_cast<Word>(item));
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t item = word_at(m_, self + kPRows + 4 * i);
        if (item != 0) {
            vcall_list("list_v6", create_screens_editor_personal_at(m_, self).field_1fc(), 6, {item});
        }
    }
    vcall_list("list_v2", create_screens_editor_personal_at(m_, self).field_1fc(), 2);
}

void CreateScreens::personal_destroy(std::uint32_t self) {
    create_screens_editor_personal_at(m_, self).set_field_0(0x005187c8);
    for (std::uint32_t slot : {kPList, kPStyleA, kPStyleB}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t item = word_at(m_, self + kPRows + 4 * i);
        if (item != 0) {
            delete_object(item);
        }
    }
    for (std::uint32_t slot : {kPNameEntry, kPArrowLeft, kPArrowRight}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    font_destroy(self + kPFont);
    w_.window_destroy(self);
}

// ---- the editor window ----------------------------------------------------------------------------------------------------------------

// 0x004271a0: a screen callback: run the editor on the page in `*page_pointer` and hand back its answer.
void CreateScreens::editor_screen_call(std::uint32_t page_pointer, std::uint32_t result) {
    set_word_at(m_, result, u(editor_run(word_at(m_, page_pointer), global_word(m_, kGlobalEditorMode))));
}

// 0x004271c0: the editor: its pages (personal, stat, trick list and appearance, each added to the window), the skater object and its colours, the
// face sprite, the camera view, the scene and fonts, the name text, six prompt windows and the background.
std::uint32_t CreateScreens::editor_construct(std::uint32_t self, std::uint32_t page, std::uint32_t mode) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontA);
    font_construct(self + kFontB);
    create_screens_editor_editor_at(m_, self).set_field_0(static_cast<Word>(kVtCreateEditor));
    engine(0x00423bd0);
    create_screens_editor_editor_at(m_, self).set_field_368(static_cast<Word>(engine_value(0x004b46a0, {kAnimsFe, 0}, m_.cstring(kAnimsFe, 0x40))));
    std::uint32_t memory = operator_new(0x3ac);
    std::uint32_t object = 0;
    if (memory != 0) {
        object = personal_construct(memory, 0, 0);
    }
    create_screens_editor_editor_at(m_, self).set_field_3d0(static_cast<Word>(object));
    w_.window_add(self, object);
    memory = operator_new(0x630);
    object = 0;
    if (memory != 0) {
        object = stat_page_construct(memory, 0, 0);
    }
    create_screens_editor_editor_at(m_, self).set_field_3c4(static_cast<Word>(object));
    w_.window_add(self, object);
    memory = operator_new(0x2158);
    object = 0;
    if (memory != 0) {
        object = engine_value(0x004c0430, {memory, 0, 0});
    }
    create_screens_editor_editor_at(m_, self).set_field_3d4(static_cast<Word>(object));
    w_.window_add(self, object);
    const std::uint32_t slot = static_cast<std::uint32_t>(sk_.current_custom_slot());
    memory = operator_new(0x2c);
    std::uint32_t holder = 0;
    if (memory != 0) {
        holder = engine_value(0x0042f880, {memory});
    }
    engine(0x0042f950, {holder, static_cast<std::uint32_t>(sk_.current_custom_slot()), 0, slot});
    if (holder != 0) {
        delete_object(holder);
    }
    memory = operator_new(0x31c);
    std::uint32_t skater = 0;
    if (memory != 0) {
        skater = engine_value(0x0041ff50, {memory, slot, 0, 0});
    }
    create_screens_editor_editor_at(m_, self).set_field_360(static_cast<Word>(skater));
    set_word_at(m_, skater + 4, 1);
    engine(0x00422690, {skater, static_cast<std::uint32_t>(sk_.current_custom_slot())});
    memory = operator_new(0x268);
    object = 0;
    if (memory != 0) {
        object = engine_value(0x00429e00, {memory, create_screens_editor_editor_at(m_, self).field_360(), 0, 0});
    }
    create_screens_editor_editor_at(m_, self).set_field_3d8(static_cast<Word>(object));
    w_.window_add(self, object);
    memory = operator_new(0x2c);
    object = 0;
    if (memory != 0) {
        object = w_.sprite_construct(memory, 0x00527850, 0x20, 0x2a, static_cast<std::int32_t>(0xffffff00), -1, 0, 0);
    }
    create_screens_editor_editor_at(m_, self).set_field_3cc(static_cast<Word>(object));
    w_.window_add(self, object);
    init_view(self + kView);
    helpers_.light_defaults(self + kLights);
    create_screens_editor_editor_at(m_, self).set_field_370(static_cast<Word>(mode));
    create_screens_editor_editor_at(m_, self).set_field_36c(static_cast<Word>(page));
    editor_setup_scene(self);
    editor_load_fonts(self);
    memory = operator_new(0x30);
    object = 0;
    if (memory != 0) {
        object = w_.text_construct(memory, record(static_cast<std::uint32_t>(sk_.current_custom_slot())), self + kFontA, 0xaa, 0x23, -1, -1);
    }
    create_screens_editor_editor_at(m_, self).set_field_3c8(static_cast<Word>(object));
    w_.window_add(self, object);
    const struct {
        std::uint32_t file;
        std::int32_t x;
    } prompts[] = {{0x0052541c, 0xc8}, {0x005258d8, 0x67}, {0x00527840, -204}, {0x00527830, 0x134}, {0x0052540c, 0x1a4}, {0x00525400, 0x21}};
    for (std::uint32_t i = 0; i < 6; ++i) {
        const std::uint32_t window = prompt(prompts[i].file, 0x005252f0, self + kFontB, prompts[i].x, 0xe0);
        set_word_at(m_, self + kPromptFirst + 4 * i, window);
        w_.window_add(self, window);
    }
    create_screens_editor_editor_at(m_, self).set_field_ac(static_cast<Word>(background_sprite()));
    create_screens_editor_editor_at(m_, self).set_field_3dc(static_cast<Word>(byte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x23)));
    create_screens_editor_editor_at(m_, self).set_field_3e0(static_cast<Word>(u(sbyte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x24))));
    return self;
}

std::uint32_t CreateScreens::editor_delete(std::uint32_t self, std::uint32_t flag) {
    editor_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00427800: release the animation region, the animated skater, write the colours back, delete the background and the editor skater.
void CreateScreens::editor_destroy(std::uint32_t self) {
    create_screens_editor_editor_at(m_, self).set_field_0(static_cast<Word>(kVtCreateEditor));
    check(static_cast<std::int32_t>(create_screens_editor_editor_at(m_, self).field_368()) != -1, kMsgNoAnimRegion);
    engine(0x004b32f0, {create_screens_editor_editor_at(m_, self).field_368(), 1, 0});
    if (create_screens_editor_editor_at(m_, self).field_364() != 0) {
        delete_object(create_screens_editor_editor_at(m_, self).field_364());
    }
    engine(0x00422640, {create_screens_editor_editor_at(m_, self).field_360(), static_cast<std::uint32_t>(sk_.current_custom_slot())});
    const std::uint32_t sprite = create_screens_editor_editor_at(m_, self).field_ac();
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    if (create_screens_editor_editor_at(m_, self).field_360() != 0) {
        delete_object(create_screens_editor_editor_at(m_, self).field_360());
    }
    font_destroy(self + kFontB);
    font_destroy(self + kFontA);
    w_.window_destroy(self);
}

// 0x00427920: the scene: the animation clock and fog, the skater's texture groups from the appearance page, the animated skater object and the
// board textures applied to the face.
void CreateScreens::editor_setup_scene(std::uint32_t self) {
    check(create_screens_editor_editor_at(m_, self).field_3d8() != 0, kMsgNoAppearance);
    set_global_word(m_, 0x0056865c, 0x100);
    engine(0x004f5430, {0x0055f7fc});
    set_global_word(m_, 0x0055f7cc, 0);
    set_global_word(m_, 0x0055f7d0, 0xffffff74);
    set_global_word(m_, 0x0055f7d4, 0xffffff7e);
    engine(0x004e8460, {0x0055f7fc, 0x0055f7cc});
    engine(0x00464710, {10, 6000, 0x800});
    engine(0x004645f0);
    const std::uint32_t skater = create_screens_editor_editor_at(m_, self).field_360();
    engine(0x00420c10, {skater, static_cast<std::uint32_t>(sk_.current_custom_slot())});
    std::uint32_t page = create_screens_editor_editor_at(m_, self).field_3d8();
    engine(0x004208a0, {skater, 0, word_at(m_, page + 0xb4), word_at(m_, page + 0xb0)});
    page = create_screens_editor_editor_at(m_, self).field_3d8();
    engine(0x00422d80, {skater, word_at(m_, page + 0xbc), word_at(m_, page + 0xc4), word_at(m_, page + 0xc8), word_at(m_, page + 0xb0)});
    page = create_screens_editor_editor_at(m_, self).field_3d8();
    engine(0x004208a0, {skater, 6, word_at(m_, page + 0xcc), word_at(m_, page + 0xb0)});
    const std::uint32_t storage = engine_value(0x0047fd30, {0x168});
    std::uint32_t object = 0;
    if (storage != 0) {
        object = engine_value(0x00480610, {storage});
    }
    create_screens_editor_editor_at(m_, self).set_field_364(static_cast<Word>(object));
    set_word_at(m_, object + 0x40, self + kLights);
    set_word_at(m_, object + 8, 0);
    set_word_at(m_, object + 0xc, 0);
    set_half_at(m_, object + 0x16, 0x3c);
    const std::uint32_t region_name = engine_value(0x004214d0, {skater});
    engine_named(0x0047fe30, {create_screens_editor_editor_at(m_, self).field_364()}, m_.cstring(region_name, 0x100), region_name);
    const std::uint32_t region = engine_value(0x004203c0, {skater});
    engine(0x00452390, {region, create_screens_editor_editor_at(m_, self).field_368(), 1});
    engine(0x00421460, {skater, create_screens_editor_editor_at(m_, self).field_364()});
    object = create_screens_editor_editor_at(m_, self).field_364();
    set_word_at(m_, object + 0x108, 0x10000);
    engine(0x00480890, {object, 0, 1});
    engine(0x00422e30, {skater, create_screens_editor_editor_at(m_, self).field_364()});
    if (s_.live()) {
        // the board's checksums go to a list and a count that are locals of the retail function (here pc_platform's scratch window)
        constexpr std::uint32_t kBoardList = 0x7ff0c800;
        constexpr std::uint32_t kBoardCount = 0x7ff0c900;
        set_global_word(m_, kBoardCount, 0);
        engine(0x00420420, {skater, kBoardList, kBoardCount});
    } else {
        engine(0x00420420, {skater});
    }
    const std::uint32_t material = word_at(m_, create_screens_editor_editor_at(m_, self).field_360() + 0x6c);
    check(material != 0, kMsgEmpty);
    if (material != 0) {
        s_.note(kSvcPrintfLog, {kLogBoardTextures});
        engine(0x00418480, {material, u(sk_.current_character()), global_word(m_, kPadCurrent)});
    }
}

// 0x00427be0: the editor's two fonts: s2font1 (centred, scale 1.0) and s2font2 (left).
void CreateScreens::editor_load_fonts(std::uint32_t self) {
    copy_font_named(self + kFontA, 0x00526acc);
    set_word_at(m_, self + kFontA + 4, 1);
    set_word_at(m_, self + kFontA + 8, 3);
    f_.set_rgb(self + kFontA, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontA + 0x21, 0);
    set_word_at(m_, self + kFontA + 0xc, 0);
    set_word_at(m_, self + kFontA + 0x10, 0);
    set_word_at(m_, self + kFontA + 0x30, 0x1000);
    copy_font_named(self + kFontB, 0x0052544c);
    set_word_at(m_, self + kFontB + 4, 0);
    set_word_at(m_, self + kFontB + 8, 3);
    f_.set_rgb(self + kFontB, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0);
}

// 0x00427dc0: go to a page: the face sprite slides in from the left, every page returns to its first item.
void CreateScreens::editor_set_page(std::uint32_t self, std::int32_t page) {
    if (page == 2) {
        editor_change_build(self);
    }
    const std::uint32_t face = create_screens_editor_editor_at(m_, self).field_3cc();
    set_word_at(m_, face + 8, 0xffffff00);
    if (word_at(m_, face) == kVtSprite) {
        w_.element_snap_x(face);
    } else {
        s_.note(kElementSnapX, {face});
    }
    set_word_at(m_, face + 8, 0x20);
    create_screens_editor_editor_at(m_, self).set_field_36c(static_cast<Word>(u(page)));
    engine(0x00429cd0, {create_screens_editor_editor_at(m_, self).field_3d0()});
    engine(0x004b6bf0, {create_screens_editor_editor_at(m_, self).field_3c4()});
    engine(0x004c29d0, {create_screens_editor_editor_at(m_, self).field_3d4()});
    appearance_highlight_first(create_screens_editor_editor_at(m_, self).field_3d8());
    engine(0x0042b4d0, {create_screens_editor_editor_at(m_, self).field_3d8()});
}

// 0x0042b4c0 (appwin.cpp): the appearance page's list goes to its first item (a tail jump into MenuList_HighlightFirst).
void CreateScreens::appearance_highlight_first(std::uint32_t page) { menu_.list_highlight_first(word_at(m_, page + 0xd0)); }

// 0x00427e40: one frame: the animation hooks; the pad's spin buttons turn the skater (not on the trick page, not while typing a name); the
// texture spool's busy flag; the name text; the pages shown and the buttons that go with the page; the board byte; the body scale; the window.
void CreateScreens::editor_update_frame(std::uint32_t self) {
    engine(0x00480fa0, {create_screens_editor_editor_at(m_, self).field_364()});
    const std::uint32_t player = global_word(m_, kPadCurrent);
    const std::uint32_t page_index = create_screens_editor_editor_at(m_, self).field_36c();
    const std::uint32_t personal = create_screens_editor_editor_at(m_, self).field_3d0();
    auto typing = [&]() { return (engine_value(0x00429ce0, {personal}) & 0xff) != 0; };
    if (page_index != 3 && (page_index != 0 || !typing())) {
        const std::uint32_t object = create_screens_editor_editor_at(m_, self).field_364();
        if (byte_at(m_, kPadSpinLeft + player * 0x16c) != 0) {
            set_half_at(m_, object + 0x16, half_at(m_, object + 0x16) + 0x3c);
        }
        if (byte_at(m_, kPadSpinRight + player * 0x16c) != 0) {
            set_half_at(m_, object + 0x16, half_at(m_, object + 0x16) + 0xffc4);
        }
    }
    create_screens_editor_editor_at(m_, self).set_field_3c0(static_cast<Word>(create_screens_editor_editor_at(m_, self).field_3bc()));
    const std::uint32_t idle = engine_value(0x00422b10);
    create_screens_editor_editor_at(m_, self).set_field_3bc(static_cast<Word>(idle == 0 ? 1 : 0));
    if (idle != 0 && create_screens_editor_editor_at(m_, self).field_3c0() != 0) {
        engine(0x0042b4d0, {create_screens_editor_editor_at(m_, self).field_3d8()});
    }
    // the skater's name for the name text (sprintf with the name as the format, then shortened to fit)
    m_.ensure_region(kScratch, 0x100);
    {
        const std::string name = m_.cstring(record(static_cast<std::uint32_t>(sk_.current_custom_slot())), 0x40);
        std::vector<std::uint8_t> bytes(name.begin(), name.end());
        bytes.push_back(0);
        m_.write(kScratch, bytes);
    }
    f_.ellipsize(self + kFontA, kScratch, 0x100);
    w_.text_set_string(create_screens_editor_editor_at(m_, self).field_3c8(), kScratch);
    set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3c4() + 0x14, 1);
    set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3d0() + 0x14, 1);
    set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3d4() + 0x14, 1);
    set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3d8() + 0x14, 1);
    set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3cc() + 0x14, 0);
    set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3c8() + 0x14, 0);
    editor_show_buttons(self, 1);
    switch (create_screens_editor_editor_at(m_, self).field_36c()) {
    case 0: {
        set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3d0() + 0x14, 0);
        set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3cc() + 0x14, engine_value(0x00429ce0, {personal}) & 0xff);
        set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3c8() + 0x14, engine_value(0x00429ce0, {personal}) & 0xff);
        editor_show_buttons(self, (engine_value(0x00429ce0, {personal}) & 0xff) == 0 ? 1 : 0);
        break;
    }
    case 1:
        set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3d8() + 0x14, 0);
        break;
    case 2:
        set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3c4() + 0x14, 0);
        break;
    case 3:
        set_word_at(m_, create_screens_editor_editor_at(m_, self).field_3d4() + 0x14, 0);
        editor_show_buttons(self, 0);
        break;
    default:
        check(false, kMsgUnrecognized);
        break;
    }
    if (create_screens_editor_editor_at(m_, self).field_3dc() != byte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x23)) {
        engine(0x00421410, {create_screens_editor_editor_at(m_, self).field_360()});
        create_screens_editor_editor_at(m_, self).set_field_3dc(static_cast<Word>(byte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x23)));
    }
    if (word_at(m_, create_screens_editor_editor_at(m_, self).field_360() + 8) != 0) {
        engine(0x00422e30, {create_screens_editor_editor_at(m_, self).field_360(), create_screens_editor_editor_at(m_, self).field_364()});
    }
    w_.window_update(self);
    engine(0x00421460, {create_screens_editor_editor_at(m_, self).field_360(), create_screens_editor_editor_at(m_, self).field_364()});
}

// 0x004280d0: when the build changed, the stat offsets of a custom skater move to the new build's base stats and spare points.
void CreateScreens::editor_change_build(std::uint32_t self) {
    const std::uint32_t slot = static_cast<std::uint32_t>(sk_.current_custom_slot());
    if (static_cast<std::int32_t>(create_screens_editor_editor_at(m_, self).field_3e0()) != sbyte_at(m_, record(slot) + 0x24)) {
        if (byte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x22) != 0) {
            const std::uint32_t current = static_cast<std::uint32_t>(sk_.current_custom_slot());
            const std::int32_t character = sk_.map_enabled_index(static_cast<int>(current));
            const std::uint32_t stats = career::record(character) + kCareerStats;
            const std::uint32_t offsets = record(current) + 0x2c;
            for (std::uint32_t i = 0; i < 10; ++i) {
                set_byte_at(m_, stats + i, byte_at(m_, stats + i) - byte_at(m_, offsets + 4 * i));
            }
            engine(0x004b6c00, {create_screens_editor_editor_at(m_, self).field_3c4(), create_screens_editor_editor_at(m_, self).field_3e0()});
            for (std::uint32_t i = 0; i < 10; ++i) {
                set_byte_at(m_, stats + i, byte_at(m_, stats + i) + byte_at(m_, offsets + 4 * i));
                while (sbyte_at(m_, stats + i) > 10) {
                    set_byte_at(m_, stats + i, byte_at(m_, stats + i) - 1);
                    set_word_at(m_, offsets + 4 * i, word_at(m_, offsets + 4 * i) - 1);
                    set_byte_at(m_, record(current) + 0x21, byte_at(m_, record(current) + 0x21) + 1);
                }
            }
        }
        create_screens_editor_editor_at(m_, self).set_field_3e0(static_cast<Word>(u(sbyte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x24))));
    }
}

// 0x004281e0: the camera, the animated skater at the origin, the view and its render calls, the window and the background.
void CreateScreens::editor_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t object = create_screens_editor_editor_at(m_, self).field_364();
    set_word_at(m_, object + 8, 0);
    set_word_at(m_, object + 0xc, 0);
    set_word_at(m_, object + 0x10, 0);
    engine(0x0042ae30, {create_screens_editor_editor_at(m_, self).field_3d8(), 0x0055f7c8, self + kView});
    engine(0x004f5430, {0x0055f7fc});
    engine(0x004f5390, {0x0055f7f4, 0x0055f7fc});
    engine(0x00464710, {10, 6000, 0x800});
    engine(0x0045e8e0, {0x0055f7c8, self + kView, word_at(m_, global_word(m_, kGlobalRenderer) + 0x88)});
    engine(0x00461bf0, {create_screens_editor_editor_at(m_, self).field_364()});
    engine(0x0045f530, {create_screens_editor_editor_at(m_, self).field_364()});
    engine(0x0045f3f0);
    w_.window_draw(self, u(add(create_screens_editor_editor_at(m_, self).field_20(), x)), u(add(create_screens_editor_editor_at(m_, self).field_24(), y)));
    draw_background(create_screens_editor_editor_at(m_, self).field_ac());
}

// 0x004282d0: poll the page the editor is on (0 personal, 1 appearance, 2 stats, 3 tricks); a poll answer of 1 goes on to the next page (the last
// page finishes the editor with 1), 2 goes back (the first page leaves with 2). With the mode flag set the poll's answer is returned as it is.
std::uint32_t CreateScreens::editor_update(std::uint32_t self) {
    std::uint32_t answer = 0;
    const std::uint32_t page = create_screens_editor_editor_at(m_, self).field_36c();
    std::uint32_t back = page;
    std::uint32_t forward = page;
    switch (page) {
    case 0: {
        answer = engine_value(0x004293b0, {create_screens_editor_editor_at(m_, self).field_3d0()});
        if (byte_at(m_, create_screens_editor_editor_at(m_, self).field_3d0() + 0xac) != 0) {
            s_.note(kSvcPrintfLog, {kLogUnlocked});
            engine(0x0042ba00, {create_screens_editor_editor_at(m_, self).field_3d8()});
            set_byte_at(m_, create_screens_editor_editor_at(m_, self).field_3d0() + 0xac, 0);
        }
        back = 0;
        forward = 1;
        break;
    }
    case 1:
        answer = engine_value(0x0042a9b0, {create_screens_editor_editor_at(m_, self).field_3d8()});
        back = 0;
        forward = 2;
        break;
    case 2:
        answer = engine_value(0x004b6180, {create_screens_editor_editor_at(m_, self).field_3c4()});
        back = 1;
        forward = 3;
        break;
    case 3:
        answer = engine_value(0x004c3d30, {create_screens_editor_editor_at(m_, self).field_3d4()});
        back = 2;
        forward = 3;
        break;
    default:
        break;
    }
    engine(0x0042b880, {create_screens_editor_editor_at(m_, self).field_3d8()});
    if (create_screens_editor_editor_at(m_, self).field_370() != 0) {
        return answer;
    }
    if (answer != 1) {
        if (answer == 2) {
            if (back == create_screens_editor_editor_at(m_, self).field_36c()) {
                return 2;
            }
            editor_set_page(self, static_cast<std::int32_t>(back));
        }
        return 0;
    }
    if (forward != create_screens_editor_editor_at(m_, self).field_36c()) {
        editor_set_page(self, static_cast<std::int32_t>(forward));
        return 0;
    }
    return answer;
}

// 0x004283f0: show (0) or hide (1) the first six of the window's prompts.
void CreateScreens::editor_show_buttons(std::uint32_t self, std::uint32_t hidden) {
    for (std::uint32_t i = 0; i < 6; ++i) {
        set_word_at(m_, word_at(m_, self + kPromptFirst + 4 * i) + 0x14, (hidden & 0xff) == 0 ? 1 : 0);
    }
}

// 0x00428420: the editor screen: the active player's character is switched to the custom skater; create.pre and the fonts are loaded; the editor
// is built and run until the update answers 1 (done) or 2 (back); then everything is unloaded. Without the mode flag a finished editor sets the
// skater up for play (stats and trick configuration) and a backed-out one is saved or marked valid.
std::int32_t CreateScreens::editor_run(std::uint32_t page, std::uint32_t mode) {
    sk_.set_active_slot(static_cast<int>(global_word(m_, kPadCurrent)));
    (void)sk_.current_character();
    const std::int32_t mapped = sk_.map_enabled_index(sk_.current_custom_slot());
    sk_.select_character(static_cast<int>(global_word(m_, kPadCurrent)), mapped);
    check(global_word(m_, kGlobalDatabase) == 0, kMsgDatabaseExists);
    check(global_word(m_, kGlobalCachedDatabase) != 0, kMsgNoCachedDatabase);
    set_global_word(m_, kGlobalDatabase, global_word(m_, kGlobalCachedDatabase));
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    engine(0x004a90f0, {global_word(m_, 0x0056b834), kCreatePre, 0});
    for (std::uint32_t font : kFontsCreate) {
        font_load(font);
    }
    const std::uint32_t memory = operator_new(0x3e4);
    std::uint32_t editor = 0;
    if (memory != 0) {
        editor = editor_construct(memory, page, mode);
    }
    check(editor != 0, kMsgInvalidEditor);
    engine(0x004a9330, {global_word(m_, 0x0056b834), kCreatePre});
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    engine(0x00489670, {0x0056aff8});
    engine(0x00489670, {0x0056b164});
    engine(0x004c8f30);
    const std::int32_t result = frame_loop(
        [&] { return static_cast<std::int32_t>(seams_ ? engine_value(0x004282d0, {editor}) : editor_update(editor)); },
        [&] {
            if (seams_) {
                engine(0x00427e40, {editor});
            } else {
                editor_update_frame(editor);
            }
        },
        [&] {
            if (seams_) {
                engine(0x004281e0, {editor, 0, 0});
            } else {
                editor_draw(editor, 0, 0);
            }
        });
    engine(0x00423bd0);
    if (editor != 0) {
        if (seams_) {
            engine(0x004277e0, {editor, 1});
        } else {
            editor_delete(editor, 1);
        }
    }
    set_global_word(m_, kGlobalFrontFlag, saved);
    for (std::uint32_t font : kFontsCreate) {
        font_unload(font);
    }
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    engine(0x0046f480);
    set_global_word(m_, kGlobalDatabase, 0);
    if (mode == 0) {
        if (result == 2) {
            const std::uint32_t slot = static_cast<std::uint32_t>(sk_.current_custom_slot());
            if (byte_at(m_, record(slot) + 0x26) == 0) {
                set_byte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x22, 1);
                for (std::uint32_t i = 0; i < 10; ++i) {
                    set_word_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x2c + 4 * i, 0);
                }
                set_byte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x21, 5);
                const std::uint32_t block = operator_new(0x140c);
                std::uint32_t manager = 0;
                if (block != 0) {
                    manager = engine_value(0x004bb4f0, {block});
                }
                engine(0x004170d0, {u(sk_.current_character()), manager});
                if (manager != 0) {
                    delete_object(manager);
                }
                sk_.select_character_checked(0, static_cast<int>(global_word(m_, kPadCurrent)));
            }
        } else {
            const std::uint32_t slot = static_cast<std::uint32_t>(sk_.current_custom_slot());
            if (byte_at(m_, record(slot) + 0x26) == 0) {
                engine(0x004171e0, {static_cast<std::uint32_t>(sk_.current_custom_slot())});
            }
            set_byte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x22, 0);
            set_byte_at(m_, record(static_cast<std::uint32_t>(sk_.current_custom_slot())) + 0x26, 1);
        }
    }
    (void)sk_.character_of_slot(1);
    (void)sk_.character_of_slot(0);
    s_.note(kSvcPrintfLog, {kLogActivePlayer});
    return result != 2 ? 0 : -1;
}

}  // namespace opentony::runtime::frontend
