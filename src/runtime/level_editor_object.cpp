#include "auto_views.hpp"
#include "level_editor.hpp"

#include <cstring>

namespace opentony::runtime::leveleditor {

using namespace opentony::runtime::autoviews;

// The editor object (0x1dc bytes, also a Font: its own font object is at +0x38): +0 the vtable, +4 and +8 the theme and the version a load or a new park asks for, +0xc, +0xd and
// +0xe the requests to rebuild the park (a loaded one, a new one, another theme), +0x10 and +0x14 what the buffer parser reads out, +0x18 the load buffer, +0x1c the save buffer,
// +0x2e the "save first" flag, +0x30 the menu on the screen, +0x34 the name entry, +0x38 the font object, +0x184 the gap bitmap, +0x188 the state of the menus, +0x18c the state
// to come back to, +0x190 the park menu, +0x194 the mode (0 piece, 1 item, 2 and 3 a menu, 4 adjusting a gap, 5 the pad is out), +0x198 the mode to come back to, +0x19c the item
// being placed, +0x1a0 and +0x1a4 the item and its end under the cursor, +0x1a8 the interface, +0x1ac the park (the generator object), +0x1b0 the region's object records,
// +0x1b4 the camera, +0x1b8 the cursor, +0x1bc the view (four words and more), +0x1d0 a value the view keeps, +0x1d4 the region slot, +0x1d8 the pad's repeat delay.

namespace {
constexpr std::uint32_t kVtable = 0x00518908;
constexpr std::uint32_t kScaleBias = 0x0051890c;           // 2.0f
constexpr std::uint32_t kFontIngame = 0x005287a4;          // "ingame1.fnt"
constexpr std::uint32_t kFontTricks = 0x005254e0;          // "s2tricks.fnt"
constexpr std::uint32_t kFontS2Font2 = 0x0052544c;         // "s2font2.fnt"
constexpr std::uint32_t kFontPark = 0x0052a688;            // "s2park1.fnt"
constexpr std::uint32_t kGapBitmap = 0x0052a524;           // "s2gap.bmp"
constexpr std::uint32_t kArrowsBitmap = 0x0052a678;        // "ltiEdArrows.bmp"
constexpr std::uint32_t kLogWelcome = 0x0052a530;
constexpr std::uint32_t kLogRegion = 0x0052a4e0;
constexpr std::uint32_t kMsgNoItemList = 0x0052a4f0;
constexpr std::uint32_t kMsgGapId = 0x0052a55c;
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;
constexpr std::uint32_t kInputActions = 0x0056aff8;
constexpr std::uint32_t kBudgetTable = 0x005188b8;
constexpr std::uint32_t kPointValues = 0x005286bc;         // the 16 values a gap can be worth
constexpr std::uint32_t kGapTypes = 0x005286fc;            // the four gap type numbers
constexpr std::uint32_t kGapTypeNames = 0x0052e8a0;        // pointers to "Air gap", "Rail gap", "Manual gap", "Wallride gap"
constexpr std::uint32_t kPointsTitle = 0x0052e8b4;
constexpr std::uint32_t kPointsLabel = 0x0052e8b8;
constexpr std::uint32_t kTypeFormat = 0x0052e8bc;          // "Current type: %s"
constexpr std::uint32_t kFmtValue = 0x0052a56c;            // "%s: %d"
constexpr std::uint32_t kStrGapSelect = 0x0052e8f4;
constexpr std::uint32_t kDefaultViewport = 0x00533fa8;
constexpr std::uint32_t kPushback = 0x00564368;            // two words
constexpr std::uint32_t kViewportEdges = 0x00563a38;
constexpr std::uint32_t kFarClip = 0x00563b38;
constexpr std::uint32_t kPolygonShade = 0x00564384;
constexpr std::uint32_t kRenderMode = 0x00562108;
constexpr std::uint32_t kDepthBias = 0x005620ac;
constexpr std::uint32_t kViewTable = 0x0055f7c8;
constexpr std::uint32_t kRegionSlot = 0x005613b8;          // the region the park was last built in
constexpr std::uint32_t kUnsavedPark = 0x0052eb24;         // a pointer to the name "unsaved park"
constexpr std::uint32_t kVersionDims = 0x005283a4;         // per version: the park's width and depth in cells
}  // namespace

// ---- helpers --------------------------------------------------------------------------------------------------------------------

// The font record Font_Load or Font_Find answers with, copied into a Font object: every field from +4 to +0x14a except the two bytes at +0x22.
void LevelEditor::copy_font(std::uint32_t destination, std::uint32_t source) {
    for (std::uint32_t at = 4; at <= 0x1c; at += 4) {
        set_word_at(m_, destination + at, word_at(m_, source + at));
    }
    set_byte_at(m_, destination + 0x20, byte_at(m_, source + 0x20));
    set_byte_at(m_, destination + 0x21, byte_at(m_, source + 0x21));
    for (std::uint32_t at = 0x24; at <= 0x44; at += 4) {
        set_word_at(m_, destination + at, word_at(m_, source + at));
    }
    for (std::uint32_t at = 0x48; at <= 0x4a; ++at) {
        set_byte_at(m_, destination + at, byte_at(m_, source + at));
    }
    for (std::uint32_t at = 0; at < 0x100; ++at) {
        set_byte_at(m_, destination + 0x4b + at, byte_at(m_, source + 0x4b + at));
    }
}

// 0x00434710: the editor's modes and requests start out clear.
void LevelEditor::reset_state(std::uint32_t self) {
    set_word_at(m_, self + 0x19c, 0xffffffff);
    set_word_at(m_, self + 0x188, 0);
    set_word_at(m_, self + 0x30, 0);
    set_word_at(m_, self + 0x34, 0);
    set_byte_at(m_, self + 0xc, 0);
    set_byte_at(m_, self + 0xd, 0);
    set_byte_at(m_, self + 0xe, 0);
    set_byte_at(m_, self + 0x2e, 0);
    set_word_at(m_, self + 0x194, 0);
    set_word_at(m_, self + 0x190, 0);
    set_word_at(m_, self + 0x18, 0);
    set_word_at(m_, self + 0x1c, 0);
}

// 0x00435f60: the pieces the memory can still hold: what is free less what the park's theme and version need (and the allowance retail keeps), zero when only just short.
std::int32_t LevelEditor::pieces_left() {
    const std::uint32_t index = global_word(m_, kTheme) * 5 + global_word(m_, kVersion);
    const std::int32_t need = int_at(m_, kBudgetTable + 4 * index);
    const std::int32_t free_bytes = static_cast<std::int32_t>(engine_value(0x0046f2c0, {1}));
    std::int32_t left = static_cast<std::int32_t>(static_cast<std::uint32_t>(free_bytes) + static_cast<std::uint32_t>(-0x6338 - need));
    if (left < 0 && left > -0x1518) {
        left = 0;
    }
    return left;
}

// 0x00435f20: whether the park can take another piece: out of memory, nodes or zone room puts the "full" flag up and the interface into its warning.
void LevelEditor::refresh_budget(std::uint32_t self) {
    set_global_byte(m_, kBlink, 0);
    if (pieces_left() > 0 && static_cast<std::int32_t>(engine_value(0x00440170)) > 0 && static_cast<std::int32_t>(engine_value(0x0043f710)) > 0) {
        return;
    }
    set_global_byte(m_, kBlink, 1);
    set_word_at(m_, word_at(m_, self + 0x1a8) + 0x434, 4);
}

// ---- the editor object ----------------------------------------------------------------------------------------------------------

// 0x004334d0: the editor: its font, the gap bitmap, the interface, the camera and the cursor are made, the park is built, and the cursor starts on the first piece.
std::uint32_t LevelEditor::editor_construct(std::uint32_t self) {
    const std::uint32_t font = self + 0x38;
    engine(0x00449ca0, {font});
    level_editor_object_editor_at(m_, self).set_field_0(static_cast<Word>(kVtable));
    level_editor_object_editor_at(m_, self).set_field_4(0xffffffff);
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    log(kLogWelcome);
    reset_state(self);
    for (const std::uint32_t name : {kFontIngame, kFontTricks, kFontS2Font2}) {
        (void)engine_value(0x0044ada0, {name}, m_.cstring(name, 0x20));
    }
    const std::uint32_t record = engine_value(0x0044b000, {kFontIngame}, m_.cstring(kFontIngame, 0x20));
    copy_font(font, record);
    level_editor_object_editor_at(m_, self).set_field_3c(1);
    level_editor_object_editor_at(m_, self).set_field_40(3);
    engine(0x0044ab70, {font, 0x80, 0x80, 0x80});
    level_editor_object_editor_at(m_, self).set_field_44(2);
    level_editor_object_editor_at(m_, self).set_field_48(2);
    level_editor_object_editor_at(m_, self).set_field_68(0x1000);
    level_editor_object_editor_at(m_, self).set_field_59(1);
    level_editor_object_editor_at(m_, self).set_field_5c(2);
    level_editor_object_editor_at(m_, self).set_field_60(2);
    std::uint32_t block = operator_new(0x5c);
    std::uint32_t element = block != 0 ? engine_value(0x00457420, {block, kGapBitmap, 1, 0, 0}, m_.cstring(kGapBitmap, 0x20)) : 0;
    level_editor_object_editor_at(m_, self).set_field_184(static_cast<Word>(element));
    engine(0x00457120, {element});
    set_byte_at(m_, level_editor_object_editor_at(m_, self).field_184() + 0xa, 1);
    set_byte_at(m_, level_editor_object_editor_at(m_, self).field_184() + 0xb, 1);
    block = engine_value(0x004436a0, {0x458});
    level_editor_object_editor_at(m_, self).set_field_1a8(static_cast<Word>(block != 0 ? ui_construct(block) : 0));
    block = engine_value(0x004436a0, {0x4c});
    level_editor_object_editor_at(m_, self).set_field_1b4(static_cast<Word>(block != 0 ? engine_value(0x00437270, {block}) : 0));
    block = engine_value(0x004436a0, {0x15f0});
    level_editor_object_editor_at(m_, self).set_field_1b8(static_cast<Word>(block != 0 ? engine_value(0x004377d0, {block}) : 0));
    engine(0x00433280, {self + 0x1ac, 0});
    if (replay()) {
        level_editor_object_editor_at(m_, self).set_field_1ac(static_cast<Word>(static_cast<std::uint32_t>(s_.value("engine_00433280"))));       // (the loader leaves the park it made in the object)
    }
    const std::uint32_t ui = level_editor_object_editor_at(m_, self).field_1a8();
    level_editor_object_editor_at(m_, self).set_field_1d4(static_cast<Word>(global_word(m_, kRegionSlot)));
    engine(0x00436fa0, {ui, 0});
    engine(0x004371f0, {ui, global_word(m_, kTheme), global_word(m_, kVersion)});
    engine(0x004378d0, {level_editor_object_editor_at(m_, self).field_1b8(), level_editor_object_editor_at(m_, self).field_1ac()});
    engine(0x00437220, {ui});
    set_global_byte(m_, kBlink, 0);
    level_editor_object_editor_at(m_, self).set_field_1b0(static_cast<Word>(word_at(m_, kObjectTables + level_editor_object_editor_at(m_, self).field_1d4() * 0x44)));
    s_.check(level_editor_object_editor_at(m_, self).field_1b0() != 0, kMsgNoItemList);
    log(kLogRegion);
    const std::uint32_t piece = engine_value(0x004371d0, {ui});
    engine(0x00437950, {level_editor_object_editor_at(m_, self).field_1b8(), piece});
    (void)engine_fill_value(0x00437720, {level_editor_object_editor_at(m_, self).field_1b4(), kScratchLook}, {}, kScratchLook, 6);
    engine(0x00439110, {level_editor_object_editor_at(m_, self).field_1b8(), global_word(m_, kScratchLook), global_half(m_, kScratchLook + 4)});
    const std::uint16_t hud_left = global_half(m_, kHudLeft);
    level_editor_object_editor_at(m_, self).set_field_1bc(0x200);
    level_editor_object_editor_at(m_, self).set_field_1be(static_cast<std::uint16_t>(hud_left));
    level_editor_object_editor_at(m_, self).set_field_1c2(0);
    level_editor_object_editor_at(m_, self).set_field_1c0(0);
    level_editor_object_editor_at(m_, self).set_field_1bc(static_cast<std::uint16_t>(static_cast<std::uint16_t>(global_word(m_, kScreenWidth) & 0x7fffff)));
    level_editor_object_editor_at(m_, self).set_field_1c4(10);
    level_editor_object_editor_at(m_, self).set_field_1be(static_cast<std::uint16_t>(static_cast<std::uint16_t>(static_cast<std::uint32_t>(hud_left) * global_word(m_, kScreenHeight) / 240)));
    level_editor_object_editor_at(m_, self).set_field_1c6(30000);
    level_editor_object_editor_at(m_, self).set_field_1c8(0xd55);
    level_editor_object_editor_at(m_, self).set_field_1d0(static_cast<Word>(engine_value(0x004e8960, {0xffffffff})));
    engine(0x00489670, {kInputActions});
    level_editor_object_editor_at(m_, self).set_field_1d8(0);
    return self;
}

// 0x004338e0: the editor taken apart: camera, cursor, the PSX region, the park, the interface, the bitmap, the fonts, the menus and the two buffers.
void LevelEditor::editor_destruct(std::uint32_t self) {
    level_editor_object_editor_at(m_, self).set_field_0(static_cast<Word>(kVtable));
    const std::uint32_t camera = level_editor_object_editor_at(m_, self).field_1b4();
    if (camera != 0) {
        engine(0x004372c0, {camera});
        engine(0x004436d0, {camera});
    }
    const std::uint32_t cursor = level_editor_object_editor_at(m_, self).field_1b8();
    if (cursor != 0) {
        engine(0x00437860, {cursor});
        engine(0x004436d0, {cursor});
    }
    engine(0x0043bf80, {1});
    const std::uint32_t park = level_editor_object_editor_at(m_, self).field_1ac();
    if (park != 0) {
        engine(0x00439900, {park});
        engine(0x004436d0, {park});
    }
    const std::uint32_t ui = level_editor_object_editor_at(m_, self).field_1a8();
    if (ui != 0) {
        engine(0x00436520, {ui});
        engine(0x004436d0, {ui});
    }
    if (level_editor_object_editor_at(m_, self).field_184() != 0) {
        (void)virtual_call(level_editor_object_editor_at(m_, self).field_184(), 0, {1});
    }
    for (const std::uint32_t name : {kFontIngame, kFontTricks, kFontS2Font2}) {
        engine(0x0044b0a0, {name}, m_.cstring(name, 0x20));
    }
    set_global_half(m_, kPushback, 0);
    set_global_half(m_, kPushback + 2, 0);
    set_global_word(m_, kViewportEdges, kDefaultViewport);
    for (const std::uint32_t offset : {0x30u, 0x34u, 0x190u}) {
        if (word_at(m_, self + offset) != 0) {
            (void)virtual_call(word_at(m_, self + offset), 0, {1});
        }
    }
    if (level_editor_object_editor_at(m_, self).field_18() != 0) {
        free_block(level_editor_object_editor_at(m_, self).field_18());
    }
    if (level_editor_object_editor_at(m_, self).field_1c() != 0) {
        free_block(level_editor_object_editor_at(m_, self).field_1c());
    }
    engine(0x0044a060, {self + 0x38});
}

// 0x00433a40: draw the interface, the menu on the screen, the prompts of the gap menus, then the park (the piece objects, the cursor, the base objects).
void LevelEditor::editor_draw(std::uint32_t self) {
    engine(0x004365e0, {level_editor_object_editor_at(m_, self).field_1a8()});
    if (level_editor_object_editor_at(m_, self).field_188() == 2 && level_editor_object_editor_at(m_, self).field_2e() == 0) {
        engine(0x0047ace0);
    }
    if (level_editor_object_editor_at(m_, self).field_190() != 0) {
        engine(0x00432c10, {level_editor_object_editor_at(m_, self).field_190()});
    }
    if (level_editor_object_editor_at(m_, self).field_30() != 0) {
        (void)virtual_call(word_at(m_, level_editor_object_editor_at(m_, self).field_30() + 4), 3);
    }
    const std::uint32_t font = self + 0x38;
    const std::uint32_t state = level_editor_object_editor_at(m_, self).field_188();
    const std::uint32_t item = global_word(m_, kItems) + level_editor_object_editor_at(m_, self).field_1a0() * 0x2c;
    if (state == 0xf) {
        const std::uint32_t title = global_word(m_, kPointsTitle);
        (void)virtual_call(font, 1, {0x100, 0x26, title, 0}, m_.cstring(title, 0x80));
        const std::uint32_t value = word_at(m_, kPointValues + 4 * word_at(m_, item + 0x28));
        sprintf_list(kScratchLine, kFmtValue, {global_word(m_, kPointsLabel), value});
        (void)virtual_call(font, 1, {0x100, 0x35, kScratchLine, 0}, m_.cstring(kScratchLine, 0x40));
    }
    if (level_editor_object_editor_at(m_, self).field_188() == 0x10) {
        std::uint32_t found = 0;
        while (found < 4 && word_at(m_, kGapTypes + 4 * found) != byte_at(m_, item + 0xc)) {
            ++found;
        }
        s_.check(found < 4, kMsgGapId);
        const std::uint32_t name = found < 4 ? word_at(m_, kGapTypeNames + 4 * found) : kScratchEmpty;
        sprintf_list(kScratchLine, global_word(m_, kTypeFormat), {name});
        (void)virtual_call(font, 1, {0x100, 0x4b, kScratchLine, 0}, m_.cstring(kScratchLine, 0x40));
    }
    if (level_editor_object_editor_at(m_, self).field_194() == 3 && (level_editor_object_editor_at(m_, self).field_188() == 0xd || level_editor_object_editor_at(m_, self).field_188() == 0x10)) {
        (void)virtual_call(level_editor_object_editor_at(m_, self).field_184(), 3, {0x100, 0x30, 0});
    }
    engine(0x0045e8e0, {kViewTable, self + 0x1bc, word_at(m_, global_word(m_, kRenderer) + 0x88)});
    set_global_word(m_, kPolygonShade, 0x005050b4);
    set_global_half(m_, kPushback, 0x400);
    set_global_half(m_, kPushback + 2, 0x400);
    set_global_word(m_, kRenderMode, 1);
    const float scaled = cursor_scaled(level_editor_object_editor_at(m_, self).field_1b8());
    set_global_word(m_, kFarClip, 0x44fa0000);
    const std::uint32_t bias_bits = global_word(m_, kScaleBias);
    float bias;
    std::memcpy(&bias, &bias_bits, sizeof bias);
    const float depth = scaled - bias;
    std::uint32_t depth_bits;
    std::memcpy(&depth_bits, &depth, sizeof depth_bits);
    set_global_word(m_, kDepthBias, depth_bits);
    engine(0x0045f530, {level_editor_object_editor_at(m_, self).field_1b0()});
    set_global_word(m_, kRenderMode, 0);
    engine(0x004385b0, {level_editor_object_editor_at(m_, self).field_1b8()});
    set_global_word(m_, kPolygonShade, 0x00ff7070);
    const std::uint32_t objects = word_at(m_, level_editor_object_editor_at(m_, self).field_1ac() + 0x23a0);
    if (objects != 0) {
        engine(0x0045f530, {objects});
    }
}

// 0x00433cb0: one frame of the editor.  The menus and the pad first; then the requests to rebuild the park (a loaded park, a new one, another theme) are served, and the
// camera, the cursor and the interface move on.  Answers 1 when the editor is done (the pad is out of the menus' reach and the quit state is reached).
std::uint32_t LevelEditor::editor_update(std::uint32_t self) {
    if (level_editor_object_editor_at(m_, self).field_1d8() != 0) {
        level_editor_object_editor_at(m_, self).set_field_1d8(static_cast<Word>(level_editor_object_editor_at(m_, self).field_1d8() - 1));
    }
    update_menu(self);
    if (level_editor_object_editor_at(m_, self).field_188() == 5) {
        return 1;
    }
    const std::uint32_t ui = level_editor_object_editor_at(m_, self).field_1a8();
    const std::uint32_t park = level_editor_object_editor_at(m_, self).field_1ac();
    const std::uint32_t cursor = level_editor_object_editor_at(m_, self).field_1b8();
    auto rebuild = [&]() {
        engine(0x00437220, {ui});
        (void)engine_out(0x0043d400, {park, self + 0x1d4, 1}, self + 0x1d4, false);
    };
    auto table = [&]() { level_editor_object_editor_at(m_, self).set_field_1b0(static_cast<Word>(word_at(m_, kObjectTables + level_editor_object_editor_at(m_, self).field_1d4() * 0x44))); };
    auto home = [&]() {
        engine(0x00437cd0, {cursor, static_cast<std::uint32_t>(int_at(m_, kVersionDims + 8 * level_editor_object_editor_at(m_, self).field_8()) >> 1), 0,
                            static_cast<std::uint32_t>(int_at(m_, kVersionDims + 4 + 8 * level_editor_object_editor_at(m_, self).field_8()) >> 1)});
    };
    if (level_editor_object_editor_at(m_, self).field_c() != 0) {
        level_editor_object_editor_at(m_, self).set_field_c(0);
        level_editor_object_editor_at(m_, self).set_field_19c(0xffffffff);
        engine(0x00437cd0, {cursor, 0, 0, 0});
        engine(0x0043a280, {park, level_editor_object_editor_at(m_, self).field_4(), level_editor_object_editor_at(m_, self).field_8(), 0});
        rebuild();
        refresh_budget(self);
        table();
        s_.check(level_editor_object_editor_at(m_, self).field_1b0() != 0, kMsgNoItemList);
        engine(0x004371f0, {ui, 0xffffffff, level_editor_object_editor_at(m_, self).field_8()});
        engine(0x00436da0, {ui, 0});
        engine(0x00437950, {cursor, engine_value(0x004371d0, {ui})});
        home();
        level_editor_object_editor_at(m_, self).set_field_4(0xffffffff);
    }
    if (level_editor_object_editor_at(m_, self).field_d() != 0) {
        level_editor_object_editor_at(m_, self).set_field_d(0);
        level_editor_object_editor_at(m_, self).set_field_19c(0xffffffff);
        engine(0x004371f0, {ui, 0xffffffff, level_editor_object_editor_at(m_, self).field_8()});
        engine(0x00436da0, {ui, 0});
        engine(0x00437950, {cursor, engine_value(0x004371d0, {ui})});
        engine(0x00437cd0, {cursor, 0, 0, 0});
        engine(0x0043a280, {park, 0xffffffff, level_editor_object_editor_at(m_, self).field_8(), 1});
        rebuild();
        refresh_budget(self);
        table();
        s_.check(level_editor_object_editor_at(m_, self).field_1b0() != 0, kMsgNoItemList);
        engine(0x00432980, {global_word(m_, kUnsavedPark), 1}, m_.cstring(global_word(m_, kUnsavedPark), 0x40));
        home();
    }
    if (level_editor_object_editor_at(m_, self).field_e() != 0) {
        level_editor_object_editor_at(m_, self).set_field_e(0);
        engine(0x0043a280, {park, level_editor_object_editor_at(m_, self).field_4(), 0xffffffff, 0});
        engine(0x00437220, {ui});
        (void)engine_out(0x0043d400, {park, self + 0x1d4, 1}, self + 0x1d4, false);
        table();
        s_.check(level_editor_object_editor_at(m_, self).field_1b0() != 0, kMsgNoItemList);
        engine(0x00437cd0, {cursor, word_at(m_, cursor + 0x270), word_at(m_, cursor + 0x274), word_at(m_, cursor + 0x278)});
        level_editor_object_editor_at(m_, self).set_field_4(0xffffffff);
    }
    engine(0x0043f6c0, {park});
    if ((engine_fill_value(0x00437720, {level_editor_object_editor_at(m_, self).field_1b4(), kScratchLook}, {}, kScratchLook, 6) & 0xff) != 0) {
        engine(0x00439110, {cursor, global_word(m_, kScratchLook), global_half(m_, kScratchLook + 4)});
    }
    const std::uint32_t piece = engine_value(0x004371d0, {ui});
    engine(0x0043ce60, {park, piece == global_word(m_, kBaseType) ? 1u : 0u});
    engine(0x00438410, {cursor});
    engine(0x004372d0, {level_editor_object_editor_at(m_, self).field_1b4(), word_at(m_, cursor + 0x2ac), 0, word_at(m_, cursor + 0x2b0)});
    engine(0x00437310, {level_editor_object_editor_at(m_, self).field_1b4()});
    engine(0x004369d0, {ui});
    return 0;
}

// 0x00435fa0: the interface: two Font objects (the second a copy of the first), the piece library's counts, the arrow bitmaps, the panel's windows and the help text, in the
// layout the editor starts in.
std::uint32_t LevelEditor::ui_construct(std::uint32_t self) {
    const std::uint32_t font = self + 0x184;
    const std::uint32_t font2 = self + 0x2d0;
    engine(0x00449ca0, {font});
    engine(0x00449ca0, {font2});
    std::uint32_t count = 0;
    if (global_byte(m_, kPieceNames) != 0xff) {
        std::uint32_t at = kPieceNames;
        do {
            at += kPieceBytes;
            ++count;
        } while (byte_at(m_, at) != 0xff);
    }
    set_word_at(m_, self + 0x174, count);
    count = 0;
    if (global_byte(m_, kCategoryNames) != 0xff) {
        std::uint32_t at = kCategoryNames;
        do {
            at += kCategoryBytes;
            ++count;
        } while (byte_at(m_, at) != 0xff);
    }
    set_word_at(m_, self + 0x178, count);
    const std::uint32_t record = engine_value(0x0044ada0, {kFontPark}, m_.cstring(kFontPark, 0x20));
    copy_font(font, record);
    set_word_at(m_, self + 0x188, 0);
    set_word_at(m_, self + 0x18c, 0);
    set_word_at(m_, self + 0x1a8, 2);
    set_word_at(m_, self + 0x1ac, 2);
    set_word_at(m_, self + 0x190, 1);
    copy_font(font2, font);
    set_word_at(m_, self + 0x2d8, 2);
    set_word_at(m_, self + 8, 0x2b);
    set_word_at(m_, self + 0xc, 0x1c);
    set_word_at(m_, self, 0xffffffd5);
    auto bitmap = [&](std::uint32_t offset, std::uint8_t flag) {
        const std::uint32_t block = operator_new(0x5c);
        const std::uint32_t element = block != 0 ? engine_value(0x00457420, {block, kArrowsBitmap, 1, 0, 0}, m_.cstring(kArrowsBitmap, 0x20)) : 0;
        set_word_at(m_, self + offset, element);
        set_byte_at(m_, element + 0xa, flag);
        set_byte_at(m_, word_at(m_, self + offset) + 0xb, 1);
        return engine_value(0x00457820, {word_at(m_, self + offset)});
    };
    set_word_at(m_, self + 0x138, bitmap(0x134, 2) + 0xe);
    set_word_at(m_, self + 0x140, 0x1f0 - bitmap(0x13c, 0));
    set_word_at(m_, self + 0x164, 0);
    set_word_at(m_, self + 0x16c, 0);
    set_word_at(m_, self + 0x170, 0xffffffff);
    auto window = [&](std::uint32_t offset, std::uint32_t name, std::uint32_t x, std::uint32_t y, std::uint32_t height) {
        const std::uint32_t block = operator_new(0xb0);
        set_word_at(m_, self + offset, block != 0 ? engine_value(0x004a1b40, {block, name, 0x005252f0, font2, x, y, 0xffffffff, height}, m_.cstring(name, 0x20)) : 0);
    };
    window(0x144, 0x0052a66c, 0x32, 0xe7, 0x10e);
    window(0x148, 0x0052a65c, 0x78, 0xe7, 0x10e);
    window(0x150, 0x0052a64c, 0xe6, 0xe7, 0x10e);
    window(0x14c, 0x0052a63c, 0x163, 0xe7, 0x10e);
    window(0x154, 0x005258d8, 0x2c, 0x1e, 0xffffffec);
    window(0x158, 0x00527840, 0xfffffefa, 0x1e, 0xffffffec);
    window(0x15c, 0x00527840, 0xffffffc4, 0xe7, 0x10e);
    const std::uint32_t block = operator_new(0x30);
    const std::uint32_t help = global_word(m_, kStrGapSelect);
    set_word_at(m_, self + 0x160, block != 0 ? engine_value(0x004ac840, {block, help, font2, 0x2c, 0x2a, 0xffffffff, 0xffffffec}, m_.cstring(help, 0x40)) : 0);
    engine(0x00436fd0, {self, 1});
    set_word_at(m_, self + 0x444, 0x208);
    set_word_at(m_, self + 0x448, 0x208);
    set_word_at(m_, self + 0x41c, global_word(m_, kHudTop) + 0x28);
    set_word_at(m_, self + 0x424, 0);
    set_word_at(m_, self + 0x430, 0);
    set_word_at(m_, self + 0x434, 0);
    set_byte_at(m_, self + 0x438, 0);
    set_word_at(m_, self + 0x440, 2);
    set_word_at(m_, self + 0x43c, 0);
    set_word_at(m_, self + 0x454, 0xffffffff);
    return self;
}

}  // namespace opentony::runtime::leveleditor
