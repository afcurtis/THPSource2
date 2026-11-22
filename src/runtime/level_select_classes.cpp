#include "auto_views.hpp"
#include "level_select.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// Banner fields
constexpr std::uint32_t kBFace = 0xac;
constexpr std::uint32_t kBLineA = 0xb0;
constexpr std::uint32_t kBLineB = 0xb4;
constexpr std::uint32_t kBFont = 0xb8;
constexpr std::uint32_t kBBufferA = 0x204;
constexpr std::uint32_t kBBufferB = 0x218;

// Globe fields
constexpr std::uint32_t kGRecord = 0x2c;
constexpr std::uint32_t kGRegion = 0x78;
constexpr std::uint32_t kGCrosshair = 0x7c;
constexpr std::uint32_t kGLatitude = 0x80;
constexpr std::uint32_t kGLatitudeTarget = 0x84;
constexpr std::uint32_t kGLongitude = 0x88;
constexpr std::uint32_t kGLongitudeTarget = 0x8c;
constexpr std::uint32_t kGView = 0x90;

// Name window fields
constexpr std::uint32_t kNName = 0xac;
constexpr std::uint32_t kNLocation = 0xb0;
constexpr std::uint32_t kNFont = 0xb4;
constexpr std::uint32_t kNSlide = 0x200;
constexpr std::uint32_t kNShown = 0x204;
constexpr std::uint32_t kNWanted = 0x208;
constexpr std::uint32_t kNState = 0x20c;

// Mode menu fields
constexpr std::uint32_t kMList = 0xac;
constexpr std::uint32_t kMRows = 0xb0;          // 0x60 slots
constexpr std::uint32_t kMStyle = 0x230;
constexpr std::uint32_t kMResult = 0x234;
constexpr std::uint32_t kMFlag = 0x238;
constexpr std::uint32_t kMPad = 0x23c;

constexpr std::uint32_t kFont1 = 0x00526acc;        // "s2font1.fnt"
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;
constexpr std::uint32_t kRegionNames = 0x0056d428;  // PsxRegionNamesBySlot: 0x44 bytes a slot
constexpr std::uint32_t kFlagByte = 0x00561450;
constexpr std::uint32_t kPad0Confirm = 0x0056b029;
constexpr std::uint32_t kPad1Confirm = 0x0056b195;

}  // namespace

// ---- a player's banner ------------------------------------------------------------------------------------------------------------------------------

// 0x00459db0: a banner for one player: a font, the skater's name cut into two lines, his face and, for the second player, a versus mark. The window base takes the
// pieces (the name's lines are texts over buffers in the banner itself).
std::uint32_t LevelSelect::banner_construct(std::uint32_t self, std::int32_t player, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    font_construct(self + kBFont);
    level_select_classes_banner_at(m_, self).set_field_0(static_cast<Word>(kVtLevelBanner));
    copy_font_named(self + kBFont, kFont1);
    set_word_at(m_, self + kBFont + 0xc, 0);
    set_word_at(m_, self + kBFont + 4, 2);
    set_word_at(m_, self + kBFont + 8, 2);
    set_word_at(m_, self + kBFont + 0x10, 0);
    set_word_at(m_, self + kBFont + 0x14, 0x80);
    set_word_at(m_, self + kBFont + 0x18, 0x80);
    set_word_at(m_, self + kBFont + 0x1c, 0x80);
    set_byte_at(m_, self + kBFont + 0x21, 0);
    set_word_at(m_, self + kBFont + 0x30, 0xe00);
    (void)engine_value(0x004b18a0, {player != 0 ? 1u : 0u});
    scratch_line({});
    sk_.format_name(player, kScratchLine);
    const std::int32_t used = copy_name_word(self + kBBufferA, kScratchLine, false);
    copy_name_word(self + kBBufferB, kScratchLine + u(used), true);
    std::uint32_t memory = operator_new(0x2c);
    std::uint32_t face = 0;
    if (memory != 0) {
        face = w_.sprite_construct(memory, sk_.face_bitmap(player), 0, -0x20, -1, -1, 0, 0);
    }
    level_select_classes_banner_at(m_, self).set_field_ac(static_cast<Word>(face));
    memory = operator_new(0x30);
    std::uint32_t line = 0;
    if (memory != 0) {
        line = w_.text_construct(memory, self + kBBufferA, self + kBFont, -2, 4, -2, -0x14);
    }
    level_select_classes_banner_at(m_, self).set_field_b0(static_cast<Word>(line));
    memory = operator_new(0x30);
    line = 0;
    if (memory != 0) {
        line = w_.text_construct(memory, self + kBBufferB, self + kBFont, -2, 0xe, -2, -0x1e);
    }
    level_select_classes_banner_at(m_, self).set_field_b4(static_cast<Word>(line));
    if (player != 0) {
        memory = operator_new(0x2c);
        std::uint32_t versus = 0;
        if (memory != 0) {
            versus = w_.sprite_construct(memory, 0x0052df4c, -0x4c, -0x2d, -1, -1, 0, 0);
        }
        w_.window_add(self, versus);
    }
    w_.window_add(self, level_select_classes_banner_at(m_, self).field_ac());
    w_.window_add(self, level_select_classes_banner_at(m_, self).field_b4());
    w_.window_add(self, level_select_classes_banner_at(m_, self).field_b0());
    return self;
}

std::uint32_t LevelSelect::banner_delete(std::uint32_t self, std::uint32_t flag) {
    banner_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void LevelSelect::banner_destroy(std::uint32_t self) {
    level_select_classes_banner_at(m_, self).set_field_0(static_cast<Word>(kVtLevelBanner));
    font_destroy(self + kBFont);
    w_.window_destroy(self);
}

// 0x0045a120: copy one word of a name: skip leading spaces, take at most nine characters, stopping at a space (the last one seen wins) or after a hyphen; an empty
// word is a single space; with `ellipsis` and more text left the copy ends in "...". Answers how many source bytes it went through (the spaces included).
std::int32_t LevelSelect::copy_name_word(std::uint32_t dest, std::uint32_t source, bool ellipsis) {
    std::uint32_t at = source;
    while (byte_at(m_, at) == ' ') {
        ++at;
    }
    std::int32_t length = 0;
    std::int32_t space = 0;
    for (;;) {
        const std::uint8_t c = byte_at(m_, at + u(length));
        if (c == ' ') {
            space = length;
        }
        if (length >= 9 || c == 0) {
            break;
        }
        if (c == '-') {
            ++length;
            break;
        }
        ++length;
    }
    if (space != 0) {
        length = space;
    }
    for (std::int32_t i = 0; i < length; ++i) {
        set_byte_at(m_, dest, byte_at(m_, at));
        ++dest;
        ++at;
    }
    if (length == 0) {
        set_byte_at(m_, dest, ' ');
        ++dest;
    }
    if (ellipsis && byte_at(m_, at) != 0) {
        set_byte_at(m_, dest, '.');
        set_byte_at(m_, dest + 1, '.');
        set_byte_at(m_, dest + 2, '.');
        dest += 3;
    }
    set_byte_at(m_, dest, 0);
    return static_cast<std::int32_t>(at - source);
}

// ---- the globe ---------------------------------------------------------------------------------------------------------------------------------------

// 0x0045a1b0: a screen element with a PSX object record for the globe's model, queued from the spooler, its view scaled to the screen, a camera and fog, and a
// crosshair sprite; pointed at the chosen level.
std::uint32_t LevelSelect::globe_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    w_.element_construct(self, x, y, start_x, start_y);
    engine(0x0047fdb0, {self + kGRecord});
    level_select_classes_globe_at(m_, self).set_field_0(static_cast<Word>(kVtLevelGlobe));
    const std::uint32_t region = engine_value(0x004b37a0, {0x0052df6c, 0, 0, 1}, m_.cstring(0x0052df6c));
    set_word_at(m_, self + kGRegion, region);
    engine(0x004b3df0);
    const std::uint32_t width = global_word(m_, kScreenWidth);
    const std::uint32_t height = global_word(m_, kScreenHeight);
    set_half_at(m_, self + kGView + 4, 0xab);
    level_select_classes_globe_at(m_, self).set_field_90(0x156);
    set_half_at(m_, self + kGView + 2, 0x78);
    set_half_at(m_, self + kGView + 6, 0);
    set_half_at(m_, self + kGView + 4, (width * 0xab) >> 9);
    level_select_classes_globe_at(m_, self).set_field_90(static_cast<std::uint16_t>((width * 0x156) >> 9));
    set_half_at(m_, self + kGView + 2, static_cast<std::uint32_t>((static_cast<std::uint64_t>(0x88888889ull) * (height * 0x78)) >> 39));
    set_half_at(m_, self + kGView + 8, 10);
    set_half_at(m_, self + kGView + 10, 30000);
    set_half_at(m_, self + kGView + 12, 0xd55);
    // the object record
    const std::uint32_t record = self + kGRecord;
    set_half_at(m_, record + 4, 0);
    set_half_at(m_, record + 6, 0);
    set_word_at(m_, record + 8, 0);
    set_word_at(m_, record + 0xc, 0);
    set_word_at(m_, record + 0x10, 0xfffb0000u);
    set_half_at(m_, record + 0x14, 0);
    set_half_at(m_, record + 0x16, 0);
    set_half_at(m_, record + 0x18, 0);
    set_byte_at(m_, record + 0x1c, 0);
    set_byte_at(m_, record + 0x1d, 0);
    set_byte_at(m_, record + 0x1e, 0);
    set_half_at(m_, record + 0x1a, 0);
    set_word_at(m_, record + 0x24, 0xff00);
    set_byte_at(m_, record + 0x1f, byte_at(m_, self + kGRegion));
    set_word_at(m_, record + 0x20, 0);
    engine(0x004f5430, {0x0055f7fc});
    set_global_word(m_, 0x0055f7cc, 0);
    set_global_word(m_, 0x0055f7d0, 0);
    set_global_word(m_, 0x0055f7d4, 0xfffffed4u);
    engine(0x004e8460, {0x0055f7fc, 0x0055f7cc});
    engine(0x00464710, {10, 0x1770, 0x800});
    engine(0x004645f0);
    const std::uint32_t memory = operator_new(0x5c);
    std::uint32_t sprite = 0;
    if (memory != 0) {
        s_.note(kSpriteConstruct, {memory, 0x0052df5c, 1, 0, 0}, m_.cstring(0x0052df5c));
        sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
    }
    level_select_classes_globe_at(m_, self).set_field_7c(static_cast<Word>(sprite));
    s_.note(kSpriteInit, {sprite});
    set_byte_at(m_, sprite + 0xa, 1);
    set_byte_at(m_, sprite + 0xb, 1);
    const std::uint32_t row = level_row(static_cast<std::int32_t>(global_word(m_, kChosenLevel)));
    level_select_classes_globe_at(m_, self).set_field_88(static_cast<Word>(word_at(m_, row + 0x70)));
    level_select_classes_globe_at(m_, self).set_field_80(static_cast<Word>(word_at(m_, row + 0x74)));
    globe_set_level(self, static_cast<std::int32_t>(global_word(m_, kChosenLevel)));
    return self;
}

std::uint32_t LevelSelect::globe_delete(std::uint32_t self, std::uint32_t flag) {
    globe_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x0045a400: the model's PSX region is cleared and its name slot emptied, the crosshair deleted, then the object record and the element base.
void LevelSelect::globe_destroy(std::uint32_t self) {
    level_select_classes_globe_at(m_, self).set_field_0(static_cast<Word>(kVtLevelGlobe));
    const std::uint32_t region = word_at(m_, self + kGRegion);
    engine(0x004b32f0, {region, 1, 0});
    set_byte_at(m_, kRegionNames + 0x44 * region, 0);
    if (level_select_classes_globe_at(m_, self).field_7c() != 0) {
        delete_object(level_select_classes_globe_at(m_, self).field_7c());
    }
    engine(0x0047fe20, {self + kGRecord});
    w_.element_destroy(self);
}

// 0x0045a490: the globe turns toward its target longitude (the short way round the 4096-step circle: a target of -1 latitude means it free-runs 0x40 ahead), a
// quarter of the distance a frame and snapping when within three, and tilts toward its target latitude the same way; then the element updates.
void LevelSelect::globe_update(std::uint32_t self) {
    const std::uint32_t latitude_target = level_select_classes_globe_at(m_, self).field_84();
    level_select_classes_globe_at(m_, self).set_field_42(static_cast<std::uint16_t>(level_select_classes_globe_at(m_, self).field_88()));
    if (latitude_target == 0xffffffffu) {
        level_select_classes_globe_at(m_, self).set_field_8c(static_cast<Word>(level_select_classes_globe_at(m_, self).field_88() + 0x40));
    }
    std::int32_t target = static_cast<std::int32_t>(level_select_classes_globe_at(m_, self).field_8c());
    std::int32_t now = static_cast<std::int32_t>(level_select_classes_globe_at(m_, self).field_88());
    std::int32_t difference = target - now;
    if (difference > 0x800) {
        target -= 0x1000;
        difference -= 0x1000;
        level_select_classes_globe_at(m_, self).set_field_8c(static_cast<Word>(u(target)));
    } else if (difference < -0x800) {
        target += 0x1000;
        difference += 0x1000;
        level_select_classes_globe_at(m_, self).set_field_8c(static_cast<Word>(u(target)));
    }
    if (difference >= -3 && difference <= 3) {
        level_select_classes_globe_at(m_, self).set_field_88(static_cast<Word>(level_select_classes_globe_at(m_, self).field_8c()));
    }
    std::int32_t moved = static_cast<std::int32_t>(level_select_classes_globe_at(m_, self).field_88()) + (difference >> 2);
    level_select_classes_globe_at(m_, self).set_field_88(static_cast<Word>(u(moved)));
    if (moved < 0) {
        moved += 0x1000;
        level_select_classes_globe_at(m_, self).set_field_88(static_cast<Word>(u(moved)));
        level_select_classes_globe_at(m_, self).set_field_8c(static_cast<Word>(level_select_classes_globe_at(m_, self).field_8c() + 0x1000));
    }
    moved = static_cast<std::int32_t>(level_select_classes_globe_at(m_, self).field_88());
    if (moved >= 0x1000) {
        moved -= 0x1000;
        level_select_classes_globe_at(m_, self).set_field_88(static_cast<Word>(u(moved)));
        level_select_classes_globe_at(m_, self).set_field_8c(static_cast<Word>(level_select_classes_globe_at(m_, self).field_8c() - 0x1000));
    }
    const std::int32_t lat_difference = static_cast<std::int32_t>(latitude_target) - static_cast<std::int32_t>(level_select_classes_globe_at(m_, self).field_80());
    if (lat_difference >= -3 && lat_difference <= 3) {
        level_select_classes_globe_at(m_, self).set_field_80(static_cast<Word>(latitude_target));
    }
    level_select_classes_globe_at(m_, self).set_field_80(static_cast<Word>(u(static_cast<std::int32_t>(level_select_classes_globe_at(m_, self).field_80()) + (lat_difference >> 2))));
    w_.element_update(self);
}

// 0x0045a5a0: the globe's view is the rectangle (x - 0x41 .. x + 0x41, y - 0x28 .. y + 0x28) round the element's place, scaled to the screen; the model renders in it
// and, unless the globe free-runs, the crosshair is drawn over the chosen level.
void LevelSelect::globe_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint16_t cx = static_cast<std::uint16_t>(static_cast<std::uint32_t>(x) + half_at(m_, self + 0x20));
    const std::uint16_t cy = static_cast<std::uint16_t>(half_at(m_, self + 0x24) + static_cast<std::uint32_t>(y));
    const std::uint32_t left = static_cast<std::uint16_t>(cx - 0x41);
    const std::uint32_t right = static_cast<std::uint16_t>(cx + 0x41);
    const std::uint32_t top = static_cast<std::uint16_t>(cy - 0x28);
    const std::uint32_t bottom = static_cast<std::uint16_t>(cy + 0x28);
    const std::uint32_t width = global_word(m_, kScreenWidth);
    const std::uint32_t height = global_word(m_, kScreenHeight);
    set_half_at(m_, self + kGView + 4, (left * width) >> 9);
    level_select_classes_globe_at(m_, self).set_field_90(static_cast<std::uint16_t>((right * width) >> 9));
    set_half_at(m_, self + kGView + 6, static_cast<std::uint32_t>((static_cast<std::uint64_t>(0x88888889ull) * (top * height)) >> 39));
    set_half_at(m_, self + kGView + 2, static_cast<std::uint32_t>((static_cast<std::uint64_t>(0x88888889ull) * (bottom * height)) >> 39));
    engine(0x0045e8e0, {0x0055f7c8, self + kGView, word_at(m_, global_word(m_, kGlobalRenderer) + 0x88)});
    engine(0x0045f530, {self + kGRecord});
    engine(0x0045f3f0);
    if (level_select_classes_globe_at(m_, self).field_84() != 0xffffffffu) {
        vcall_item(kSpriteDrawService, level_select_classes_globe_at(m_, self).field_7c(), 3,
                   {u(x) + word_at(m_, self + 0x20), level_select_classes_globe_at(m_, self).field_80() + word_at(m_, self + 0x24) + u(y), 0});
    }
}

// 0x0045a6b0: the globe's targets for a level, from the level table.
void LevelSelect::globe_set_level(std::uint32_t self, std::int32_t level) {
    const std::uint32_t row = level_row(level);
    level_select_classes_globe_at(m_, self).set_field_8c(static_cast<Word>(word_at(m_, row + 0x70)));
    level_select_classes_globe_at(m_, self).set_field_84(static_cast<Word>(word_at(m_, row + 0x74)));
}

// ---- the level name window ---------------------------------------------------------------------------------------------------------------------------

// 0x0045a6e0: a font and two flagged texts (built by the engine's text-flag class, a seam here) over the level's name and its location.
std::uint32_t LevelSelect::name_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    font_construct(self + kNFont);
    level_select_classes_name_at(m_, self).set_field_0(static_cast<Word>(kVtLevelName));
    copy_font_named(self + kNFont, kFont1);
    set_word_at(m_, self + kNFont + 4, 0);
    set_word_at(m_, self + kNFont + 8, 3);
    set_word_at(m_, self + kNFont + 0x14, 0x80);
    set_word_at(m_, self + kNFont + 0x18, 0x80);
    set_word_at(m_, self + kNFont + 0x1c, 0x80);
    set_byte_at(m_, self + kNFont + 0x21, 0);
    set_word_at(m_, self + kNFont + 0xc, 0);
    set_word_at(m_, self + kNFont + 0x10, 0);
    set_word_at(m_, self + kNFont + 0x30, 0x1000);
    level_select_classes_name_at(m_, self).set_field_200(0x14);
    const std::int32_t level = static_cast<std::int32_t>(global_word(m_, kChosenLevel));
    std::uint32_t memory = operator_new(0x34);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = engine_value(0x004a1ed0, {memory, level_row(level), self + kNFont, 1, 0xfffffefau, 0, 0xffffffffu, 0xffffffffu});
    }
    level_select_classes_name_at(m_, self).set_field_ac(static_cast<Word>(text));
    memory = operator_new(0x34);
    text = 0;
    if (memory != 0) {
        text = engine_value(0x004a1ed0, {memory, word_at(m_, level_row(level) + 0x40), self + kNFont, 0, 0x20a, 0, 0xffffffffu, 0xffffffffu});
    }
    level_select_classes_name_at(m_, self).set_field_b0(static_cast<Word>(text));
    w_.window_add(self, level_select_classes_name_at(m_, self).field_ac());
    w_.window_add(self, level_select_classes_name_at(m_, self).field_b0());
    name_set_level(self, static_cast<std::int32_t>(global_word(m_, kChosenLevel)));
    return self;
}

std::uint32_t LevelSelect::name_delete(std::uint32_t self, std::uint32_t flag) {
    name_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void LevelSelect::name_destroy(std::uint32_t self) {
    level_select_classes_name_at(m_, self).set_field_0(static_cast<Word>(kVtLevelName));
    font_destroy(self + kNFont);
    w_.window_destroy(self);
}

// 0x0045a9a0: ask for a level's name: the two texts have to slide out first (state 2); the name sits to the left of the screen's edge by the wider of the two names,
// the location to the right of it.
void LevelSelect::name_set_level(std::uint32_t self, std::int32_t level) {
    level_select_classes_name_at(m_, self).set_field_208(static_cast<Word>(u(level)));
    level_select_classes_name_at(m_, self).set_field_20c(2);
    const std::int32_t shown_width = f_.text_width(self + kNFont, level_row(static_cast<std::int32_t>(level_select_classes_name_at(m_, self).field_204())));
    const std::int32_t wanted_width = f_.text_width(self + kNFont, level_row(static_cast<std::int32_t>(level_select_classes_name_at(m_, self).field_208())));
    const std::int32_t wider = shown_width > wanted_width ? shown_width : wanted_width;
    set_word_at(m_, level_select_classes_name_at(m_, self).field_ac() + 8, u((-10 - level_select_classes_name_at(m_, self).field_8()) - wider));
    set_word_at(m_, level_select_classes_name_at(m_, self).field_b0() + 8, u(0x20a - level_select_classes_name_at(m_, self).field_8()));
}

// 0x0045aa40: the window updates and so do the two texts; a new level's strings go in once the old ones have slid out (state 0), and the slide waits for both texts
// to report arrival (state 2).
void LevelSelect::name_update(std::uint32_t self) {
    w_.window_update(self);
    vcall_item(kElementUpdate, level_select_classes_name_at(m_, self).field_ac(), 1);
    vcall_item(kElementUpdate, level_select_classes_name_at(m_, self).field_b0(), 1);
    const std::uint32_t state = level_select_classes_name_at(m_, self).field_20c();
    if (state == 0) {
        const std::int32_t width = f_.text_width(self + kNFont, level_row(static_cast<std::int32_t>(level_select_classes_name_at(m_, self).field_208())));
        level_select_classes_name_at(m_, self).set_field_204(static_cast<Word>(level_select_classes_name_at(m_, self).field_208()));
        set_word_at(m_, level_select_classes_name_at(m_, self).field_ac() + 8, 0);
        set_word_at(m_, level_select_classes_name_at(m_, self).field_b0() + 8, level_select_classes_name_at(m_, self).field_200() + u(width));
        w_.text_set_string(level_select_classes_name_at(m_, self).field_ac(), level_row(static_cast<std::int32_t>(level_select_classes_name_at(m_, self).field_204())));
        w_.text_set_string(level_select_classes_name_at(m_, self).field_b0(), word_at(m_, level_row(static_cast<std::int32_t>(level_select_classes_name_at(m_, self).field_204())) + 0x40));
        level_select_classes_name_at(m_, self).set_field_20c(1);
    } else if (state == 2) {
        if (byte_at(m_, level_select_classes_name_at(m_, self).field_b0() + 0x1c) != 0 && byte_at(m_, level_select_classes_name_at(m_, self).field_ac() + 0x1c) != 0) {
            level_select_classes_name_at(m_, self).set_field_20c(0);
        }
    }
}

// 0x0045ab50: the shown level's strings at once, the location placed after the name's width.
void LevelSelect::name_refresh(std::uint32_t self) {
    w_.text_set_string(level_select_classes_name_at(m_, self).field_ac(), level_row(static_cast<std::int32_t>(level_select_classes_name_at(m_, self).field_204())));
    w_.text_set_string(level_select_classes_name_at(m_, self).field_b0(), word_at(m_, level_row(static_cast<std::int32_t>(level_select_classes_name_at(m_, self).field_204())) + 0x40));
    vcall_item("element_width", level_select_classes_name_at(m_, self).field_b0(), 3);
    const std::uint32_t width = static_cast<std::uint32_t>(s_.value("element_width"));
    set_word_at(m_, level_select_classes_name_at(m_, self).field_b0() + 8, width + level_select_classes_name_at(m_, self).field_200());
}

// ---- the career's mode menu --------------------------------------------------------------------------------------------------------------------------

// 0x0045abc0: a scroll list, a style copied from the given font, and four rows: Play (1), the one at id 2, Saved Parks (3) and Pre-made parks (4).
std::uint32_t LevelSelect::menu_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    level_select_classes_menu_at(m_, self).set_field_0(static_cast<Word>(kVtModeMenu));
    std::uint32_t memory = operator_new(0x350);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, x, y, 0x100, 100, 1, 3, 2, 1);
    }
    level_select_classes_menu_at(m_, self).set_field_ac(static_cast<Word>(list));
    memory = operator_new(0x17c);
    const std::uint32_t style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    level_select_classes_menu_at(m_, self).set_field_230(static_cast<Word>(style));
    menu_.copy_font_fields(style + 4, font);
    set_word_at(m_, style + 0x18, 0x80);
    set_word_at(m_, style + 0x1c, 0x80);
    set_word_at(m_, style + 0x20, 0x80);
    set_word_at(m_, style + 0x34, 0x1000);
    set_word_at(m_, style + 0x150, 0x1400);
    set_word_at(m_, style + 0x154, 0x1000);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kMRows + 4 * i, 0);
    }
    const struct {
        std::uint32_t id;
        std::uint32_t text;
    } rows[4] = {{1, 0x0052e938}, {2, 0x0052e93c}, {3, 0x0052e940}, {4, 0x0052e850}};
    for (std::uint32_t i = 0; i < 4; ++i) {
        memory = operator_new(0x54);
        std::uint32_t row = 0;
        if (memory != 0) {
            row = menu_.text_construct(memory, rows[i].id, word_at(m_, rows[i].text), style);
        }
        set_word_at(m_, self + kMRows + 4 * i, row);
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kMRows + 4 * i) != 0) {
            vcall_list("list_v6", level_select_classes_menu_at(m_, self).field_ac(), 6, {word_at(m_, self + kMRows + 4 * i)});
        }
    }
    vcall_list("list_v2", level_select_classes_menu_at(m_, self).field_ac(), 2);
    level_select_classes_menu_at(m_, self).set_field_234(0);
    level_select_classes_menu_at(m_, self).set_field_238(0);
    return self;
}

std::uint32_t LevelSelect::menu_delete(std::uint32_t self, std::uint32_t flag) {
    menu_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void LevelSelect::menu_destroy(std::uint32_t self) {
    level_select_classes_menu_at(m_, self).set_field_0(static_cast<Word>(kVtModeMenu));
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kMRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kMRows + 4 * i));
        }
    }
    if (level_select_classes_menu_at(m_, self).field_230() != 0) {
        delete_object(level_select_classes_menu_at(m_, self).field_230());
    }
    if (level_select_classes_menu_at(m_, self).field_ac() != 0) {
        vcall_list("virtual_delete", level_select_classes_menu_at(m_, self).field_ac(), 0, {1});
    }
    w_.window_destroy(self);
}

void LevelSelect::menu_highlight_play(std::uint32_t self) { menu_.list_highlight(word_at(m_, self + kMList), 1); }

// 0x0045af70: unless hidden, the menu reads the pad and then draws. A change of the flag byte at 0x00561450 makes the Play row selectable only while it is clear and,
// when set, highlights the row of id 2. The list moves (and a move makes a sound); confirm on either pad (the second wins) records the pad and the highlighted id.
void LevelSelect::menu_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (level_select_classes_menu_at(m_, self).field_14() != 0) {
        return;
    }
    const std::uint8_t flag = global_byte(m_, kFlagByte);
    if (level_select_classes_menu_at(m_, self).field_238() != flag) {
        level_select_classes_menu_at(m_, self).set_field_238(static_cast<std::uint8_t>(flag));
        set_byte_at(m_, level_select_classes_menu_at(m_, self).field_b0() + 0xc, flag == 0 ? 1 : 0);
        if (level_select_classes_menu_at(m_, self).field_238() != 0) {
            menu_.list_highlight(level_select_classes_menu_at(m_, self).field_ac(), 2);
        }
    }
    const std::uint32_t list = level_select_classes_menu_at(m_, self).field_ac();
    vcall_list("list_v3", list, 3);
    s_.note(kSvcPadEdges, {0xff, 0, 6});
    const std::uint32_t edge = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
    if (edge != 0xffffffffu) {
        sfx(0x11f);
    }
    vcall_list("list_v5", list, 5, {edge});
    vcall_list("list_v4", list, 4);
    level_select_classes_menu_at(m_, self).set_field_234(0);
    bool chosen = false;
    if (global_byte(m_, kPad0Confirm) != 0) {
        set_global_byte(m_, kPad0Confirm, 0);
        level_select_classes_menu_at(m_, self).set_field_23c(0);
        chosen = true;
    }
    if (global_byte(m_, kPad1Confirm) != 0) {
        set_global_byte(m_, kPad1Confirm, 0);
        level_select_classes_menu_at(m_, self).set_field_23c(1);
        chosen = true;
    }
    if (chosen) {
        sfx(0x91);
        level_select_classes_menu_at(m_, self).set_field_234(static_cast<Word>(menu_.list_highlighted_id(level_select_classes_menu_at(m_, self).field_ac())));
    }
    w_.window_draw(self, x, y);
}

// 0x0045b080: which pad confirmed, and the chosen row's id.
std::uint32_t LevelSelect::menu_result(std::uint32_t self, std::uint32_t out) {
    set_word_at(m_, out, level_select_classes_menu_at(m_, self).field_23c());
    return level_select_classes_menu_at(m_, self).field_234();
}

}  // namespace opentony::runtime::frontend
