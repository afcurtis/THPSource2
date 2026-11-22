#include "auto_views.hpp"
#include "goals.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// VideoWindow fields (see options_gaps.cpp for the constructor)
constexpr std::uint32_t kVLevel = 0xac;
constexpr std::uint32_t kVNext = 0xb0;
constexpr std::uint32_t kVState = 0xb4;
constexpr std::uint32_t kVCounter = 0xb8;
constexpr std::uint32_t kVTexX = 0xbc;
constexpr std::uint32_t kVTexY = 0xc0;
constexpr std::uint32_t kVSizeX = 0xc4;
constexpr std::uint32_t kVSizeY = 0xc8;
constexpr std::uint32_t kVPacket = 0xcc;
constexpr std::uint32_t kVSprites = 0xd0;
constexpr std::uint32_t kVSlotA = 0x140;
constexpr std::uint32_t kVSlotB = 0x144;
constexpr std::uint32_t kVReady = 0x148;
constexpr std::uint32_t kVtVideoWindow = 0x00518b30;

constexpr std::uint32_t kScreenW = 0x029da394;
constexpr std::uint32_t kScreenH = 0x029da398;
constexpr std::uint32_t kSpool = 0x0056db4c;
constexpr std::uint32_t kMovieA = 0x00561cf4;
constexpr std::uint32_t kMovieB = 0x00561dba;
constexpr std::uint32_t kMovieC = 0x00561ce8;
constexpr std::uint32_t kVtLevelPic = 0x00518b50;

// LevelPic fields
constexpr std::uint32_t kLMode = 0x28;
constexpr std::uint32_t kLFont = 0x2c;
constexpr std::uint32_t kLLockValid = 0x178;
constexpr std::uint32_t kLLine1 = 0x179;
constexpr std::uint32_t kLLine2 = 0x199;
constexpr std::uint32_t kLPlaceX = 0x1bc;      // x of the two lines (+0x1bc, +0x1c0), y of the two lines (+0x1c4, +0x1c8)
constexpr std::uint32_t kLWidth = 0x1cc;
constexpr std::uint32_t kLHeight = 0x1d0;
constexpr std::uint32_t kLState = 0x1d4;
constexpr std::uint32_t kLCurrent = 0x1d8;
constexpr std::uint32_t kLPrevious = 0x1dc;
constexpr std::uint32_t kLWanted = 0x1e0;
constexpr std::uint32_t kLTimer = 0x1e4;
constexpr std::uint32_t kLPictures = 0x1e8;
constexpr std::uint32_t kLSpooled = 0x224;
constexpr std::uint32_t kLHalfW = 0x25c;
constexpr std::uint32_t kLHalfH = 0x260;
constexpr std::uint32_t kLSlide = 0x264;
constexpr std::uint32_t kLSlideSpeed = 0x268;
constexpr std::uint32_t kLSlideBase = 0x26c;
constexpr std::uint32_t kLArrowA = 0x270;
constexpr std::uint32_t kLArrowB = 0x274;
constexpr std::uint32_t kLCounter = 0x278;

}  // namespace

// ---- the level information window (videowin.cpp) -------------------------------------------------------------------------------------------------

std::uint32_t GoalScreens::video_delete(std::uint32_t self, std::uint32_t flag) {
    video_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void GoalScreens::video_destroy(std::uint32_t self) {
    goals_windows_video_at(m_, self).set_field_0(static_cast<Word>(kVtVideoWindow));
    engine(0x004e8740, {0});
    for (std::uint32_t k = 0; k < 14; ++k) {
        const std::uint32_t picture = word_at(m_, self + kVSprites + 4 * k);
        if (picture != 0) {
            vcall_item(kVirtualDelete, picture, 0, {1});
        }
    }
    s_.note(kMemDeleteService, {goals_windows_video_at(m_, self).field_cc()});
    engine(0x00489450, {half_at(m_, self + kVTexX), half_at(m_, self + kVTexY)});
    const std::int32_t level = static_cast<std::int32_t>(goals_windows_video_at(m_, self).field_ac());
    if (level <= 9 && level != 0xd) {
        engine(0x00454e60);                                                 // PCMOVIE_StopFMV
    }
    w_.window_destroy(self);
}

// 0x0045b470: the movie's rectangle goes to the movie player in screen pixels; the picture draws itself otherwise; a window that has been told to start its movie does so
void GoalScreens::video_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t px = goals_windows_video_at(m_, self).field_140() + goals_windows_video_at(m_, self).field_20() + u(x);
    const std::uint32_t py = goals_windows_video_at(m_, self).field_144() + goals_windows_video_at(m_, self).field_24() + u(y);
    const std::int32_t state = static_cast<std::int32_t>(goals_windows_video_at(m_, self).field_b4());
    const std::int32_t level = static_cast<std::int32_t>(goals_windows_video_at(m_, self).field_ac());
    if ((state == 3 || state == 4) && level <= 9 && level != 0xd) {
        const std::uint32_t r = goals_windows_video_at(m_, self).field_cc();
        const std::uint32_t wx = goals_windows_video_at(m_, self).field_c4(), wy = goals_windows_video_at(m_, self).field_c8();
        set_half_at(m_, r + 0x08, px);
        set_half_at(m_, r + 0x0a, py);
        set_half_at(m_, r + 0x10, wx + px);
        set_half_at(m_, r + 0x12, py);
        set_half_at(m_, r + 0x18, px);
        set_half_at(m_, r + 0x1a, wy + py);
        set_half_at(m_, r + 0x20, wx + px);
        set_half_at(m_, r + 0x22, wy + py);
        for (const std::uint32_t at : {0x08u, 0x10u, 0x18u, 0x20u}) {                     // x: times the screen width over 512
            set_half_at(m_, r + at, (u(short_at(m_, r + at)) * global_word(m_, kScreenW)) >> 9);
        }
        for (const std::uint32_t at : {0x0au, 0x12u, 0x1au, 0x22u}) {                     // y: times the screen height over 240
            const std::uint32_t product = u(short_at(m_, r + at)) * global_word(m_, kScreenH);
            set_half_at(m_, r + at, static_cast<std::uint32_t>((static_cast<std::uint64_t>(product) * 0x88888889ull) >> 32) >> 7);
        }
        engine(0x004e6f00, {r});                                            // PCMOVIE_DrawWindow
    } else {
        s_.note(kSpriteDrawService, {word_at(m_, self + kVSprites + 4 * u(level)), px, py, 0});
    }
    if (static_cast<std::int32_t>(goals_windows_video_at(m_, self).field_b4()) == 2 && goals_windows_video_at(m_, self).field_b8() == 0) {
        const std::uint32_t renderer = global_word(m_, kGlobalRenderer);
        scratch_line("");
        set_global_half(m_, kScratchLine, px);
        set_global_half(m_, kScratchLine + 2, half_at(m_, renderer + 0x90) + py);
        set_global_half(m_, kScratchLine + 4, goals_windows_video_at(m_, self).field_c4());
        set_global_half(m_, kScratchLine + 6, goals_windows_video_at(m_, self).field_c8());
        std::string rect;
        for (std::uint32_t k = 0; k < 8; ++k) {
            rect.push_back(static_cast<char>(byte_at(m_, kScratchLine + k)));
        }
        engine(0x004e8760, {kScratchLine, word_at(m_, self + kVTexX), word_at(m_, self + kVTexY)}, rect);
        const std::uint8_t movie = byte_at(m_, career::level(level) + 0x6c);
        if (movie != 0 && goals_windows_video_at(m_, self).field_148() == 0 && level <= 9 && level != 0xd) {
            engine(0x00454d30, {u(static_cast<std::int8_t>(movie))});
            set_global_half(m_, kMovieA, half_at(m_, self + kVTexX));
            set_global_half(m_, kMovieB, half_at(m_, self + kVTexX) + goals_windows_video_at(m_, self).field_c4());
            set_global_half(m_, kMovieC, half_at(m_, self + kVTexY));
            engine(0x00454e50, {1});
        }
        goals_windows_video_at(m_, self).set_field_148(0);
        goals_windows_video_at(m_, self).set_field_b4(3);
    }
    w_.window_draw(self, x, y);
}

// 0x0045b750: switch to the level asked for through a short fade, playing the movie once the new picture is up
void GoalScreens::video_update(std::uint32_t self) {
    switch (goals_windows_video_at(m_, self).field_b4()) {
    case 0:
        if (goals_windows_video_at(m_, self).field_b8() != 0) {
            goals_windows_video_at(m_, self).set_field_b8(static_cast<Word>(goals_windows_video_at(m_, self).field_b8() - 1));
        }
        goals_windows_video_at(m_, self).set_field_b4(1);
        break;
    case 1:
        goals_windows_video_at(m_, self).set_field_b4(2);
        goals_windows_video_at(m_, self).set_field_b8(0x1e);
        break;
    case 2:
        goals_windows_video_at(m_, self).set_field_b8(static_cast<Word>(goals_windows_video_at(m_, self).field_b8() - 1));
        break;
    case 3: {
        const std::int32_t level = static_cast<std::int32_t>(goals_windows_video_at(m_, self).field_ac());
        if (level <= 9 && level != 0xd) {
            engine(0x00454e70, {1});
        }
        if (static_cast<std::int32_t>(goals_windows_video_at(m_, self).field_b0()) != -1) {
            goals_windows_video_at(m_, self).set_field_b4(4);
        }
        break;
    }
    case 4: {
        const std::int32_t level = static_cast<std::int32_t>(goals_windows_video_at(m_, self).field_ac());
        if (byte_at(m_, career::level(level) + 0x6c) != 0 && level <= 9 && level != 0xd) {
            engine(0x00454e60);
        }
        engine(0x00457150, {word_at(m_, self + kVSprites + 4 * u(level))});
        engine(0x00457130, {word_at(m_, self + kVSprites + 4 * goals_windows_video_at(m_, self).field_b0())});
        const std::uint32_t next = goals_windows_video_at(m_, self).field_b0();
        goals_windows_video_at(m_, self).set_field_b0(0xffffffffu);
        goals_windows_video_at(m_, self).set_field_ac(static_cast<Word>(next));
        goals_windows_video_at(m_, self).set_field_b4(1);
        break;
    }
    default:
        break;
    }
    w_.window_update(self);
}

void GoalScreens::video_set_next(std::uint32_t self, std::int32_t level) { set_word_at(m_, self + kVNext, u(level)); }

// 0x0045b8a0: whether a level's picture may be shown: the level, a custom park and the first levels have their own rules, the rest need every earlier level open
bool GoalScreens::video_level_available(std::int32_t level, std::int32_t mode) {
    if (level == 0xd) {
        return mode != 1;
    }
    if (level >= 0xa) {
        if (mode != 3 && mode != 2) {
            return false;
        }
        return career_.level_open(level, mode);
    }
    const std::int32_t slot = sk_.select_slot(-1);
    if ((byte_at(m_, skater::kRows + skater::kRowFlags + u(slot) * skater::kRowStride) & 0x80) != 0 && level >= 8) {
        return career_.level_open(level, mode);
    }
    for (std::int32_t i = 0; i <= level; ++i) {
        if (!career_.level_open(i, mode)) {
            return false;
        }
    }
    return true;
}

// ---- the level picture window (goalwin.cpp) ---------------------------------------------------------------------------------------------------------

std::uint32_t GoalScreens::levelpic_construct(std::uint32_t self, std::int32_t level, std::uint32_t mode, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                              std::int32_t start_y) {
    w_.element_construct(self, x, y, start_x, start_y);
    font_construct(self + kLFont);
    goals_windows_levelpic_at(m_, self).set_field_0(static_cast<Word>(kVtLevelPic));
    goals_windows_levelpic_at(m_, self).set_field_28(static_cast<Word>(mode));
    copy_font_named(self + kLFont, 0x0052544c);                            // "s2font2.fnt"
    set_word_at(m_, self + kLFont + 4, 1);
    set_word_at(m_, self + kLFont + 8, 1);
    goals_windows_levelpic_at(m_, self).set_field_4d(0);
    goals_windows_levelpic_at(m_, self).set_field_40(0x80);
    goals_windows_levelpic_at(m_, self).set_field_44(0x80);
    goals_windows_levelpic_at(m_, self).set_field_48(0x80);
    goals_windows_levelpic_at(m_, self).set_field_38(0);
    goals_windows_levelpic_at(m_, self).set_field_3c(0);
    goals_windows_levelpic_at(m_, self).set_field_5c(0x1000);
    const std::uint32_t name = 0x0052dfe8;                                 // "s2levl11.bmp"
    const std::uint32_t resource = engine_value(0x00449030, {name}, m_.cstring(name));
    check(resource != 0, 0x0052dfbc);                                      // "couldn't load"
    s_.note(kAllocBuffer, {resource, 1, 1, 0});
    const std::uint32_t buffer = u(s_.value(kAllocBuffer));
    check(buffer != 0, 0x00526b30);                                        // "couldn't allocate memory"
    engine(0x00449230, {buffer});                                          // FileIO_LoadResource
    engine(0x00449660);                                                    // FileIO_SyncResource
    for (std::uint32_t k = 0; k < 14; ++k) {
        const std::uint32_t memory = operator_new(0x5c);
        std::uint32_t picture = 0;
        if (memory != 0) {
            picture = engine_value(0x00457540, {memory, buffer, 0, 0x40, 0x40});
        }
        set_word_at(m_, self + kLPictures + 4 * k, picture);
        if (!video_level_available(static_cast<std::int32_t>(k), static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_28()))) {
            engine(0x00457810, {picture});
        }
        set_byte_at(m_, picture + 0xa, 1);
        set_byte_at(m_, picture + 0xb, 1);
    }
    s_.note(kMemDeleteService, {buffer});
    goals_windows_levelpic_at(m_, self).set_field_1d8(static_cast<Word>(u(level)));
    const std::uint32_t shown = word_at(m_, self + kLPictures + 4 * u(level));
    engine(0x00457130, {shown});
    goals_windows_levelpic_at(m_, self).set_field_1cc(static_cast<Word>(engine_value(0x00457820, {shown})));
    goals_windows_levelpic_at(m_, self).set_field_1d0(static_cast<Word>(engine_value(0x00457830, {shown})));
    goals_windows_levelpic_at(m_, self).set_field_25c(static_cast<Word>(u(static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_1cc()) >> 1)));
    goals_windows_levelpic_at(m_, self).set_field_260(static_cast<Word>(u(static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_1d0()) >> 1)));
    goals_windows_levelpic_at(m_, self).set_field_1d4(0);
    goals_windows_levelpic_at(m_, self).set_field_1e0(0xffffffffu);
    goals_windows_levelpic_at(m_, self).set_field_1e4(0);
    for (int k = 0; k < 2; ++k) {
        const std::uint32_t file = k == 0 ? 0x00527c10u : 0x00527c20u;     // "s2arrow8.bmp" / "s2arrow9.bmp"
        const std::uint32_t memory = operator_new(0x5c);
        std::uint32_t arrow = 0;
        if (memory != 0) {
            s_.note(kSpriteConstruct, {memory, file, 1, 0, 0}, m_.cstring(file));
            arrow = u(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + (k == 0 ? kLArrowA : kLArrowB), arrow);
        set_byte_at(m_, arrow + 0xa, k == 0 ? 2 : 0);
        set_byte_at(m_, arrow + 0xb, 1);
    }
    goals_windows_levelpic_at(m_, self).set_field_278(0);
    // the order the pictures are spooled in: the level shown first (PlayerSpool_RingOrder fills the array: a replay scripts its 14 words; live it is a
    // stack local of the retail function, here in pc_platform's scratch window, clear of every Kit's area)
    constexpr std::uint32_t kLiveOrder = 0x7ff0e400;
    engine(0x004b51b0, {s_.live() ? kLiveOrder : kScratchLine, 14, u(level)});
    std::uint32_t order[14];
    for (std::uint32_t k = 0; k < 14; ++k) {
        order[k] = s_.live() ? word_at(m_, kLiveOrder + 4 * k) : u(s_.value(engine_name(0x004b51b0)));
    }
    for (std::uint32_t k = 0; k < 14; ++k) {
        const std::uint32_t entry = order[k];
        const std::string file = m_.cstring(word_at(m_, career::level(static_cast<int>(entry)) + 0x78), 0x100);
        scratch_line(file);
        const std::uint32_t handle = engine_value(0x004b5370, {global_word(m_, kSpool), kScratchLine, 0, 1, 0xffffffffu}, file);
        set_word_at(m_, self + kLSpooled + 4 * entry, handle);
    }
    levelpic_lock_text(self);
    return self;
}

std::uint32_t GoalScreens::levelpic_delete(std::uint32_t self, std::uint32_t flag) {
    levelpic_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void GoalScreens::levelpic_destroy(std::uint32_t self) {
    goals_windows_levelpic_at(m_, self).set_field_0(static_cast<Word>(kVtLevelPic));
    for (std::uint32_t k = 0; k < 14; ++k) {
        const std::uint32_t picture = word_at(m_, self + kLPictures + 4 * k);
        if (picture != 0) {
            vcall_item(kVirtualDelete, picture, 0, {1});
        }
    }
    engine(0x004b52b0, {global_word(m_, kSpool)});                                  // PlayerSpool_Reset
    for (const std::uint32_t slot : {kLArrowA, kLArrowB}) {
        const std::uint32_t arrow = word_at(m_, self + slot);
        if (arrow != 0) {
            vcall_item(kVirtualDelete, arrow, 0, {1});
        }
    }
    font_destroy(self + kLFont);
    w_.element_destroy(self);
}

void GoalScreens::levelpic_update(std::uint32_t self) {
    const std::uint32_t spool = global_word(m_, kSpool);
    for (std::uint32_t k = 0; k < 14; ++k) {
        const std::uint32_t handle = word_at(m_, self + kLSpooled + 4 * k);
        if ((engine_value(0x004b58a0, {spool, handle}) & 0xff) == 0) {
            continue;
        }
        const std::uint32_t data = engine_value(0x004b5990, {spool, handle});
        check(data != 0, 0x0052dff8);                                      // "no file data found"
        const std::uint32_t picture = word_at(m_, self + kLPictures + 4 * k);
        vcall_item("picture_load", picture, 2, {data});                    // slot 2 of the picture: its bitmap from the spooled data
        if (!video_level_available(static_cast<std::int32_t>(k), static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_28()))) {
            engine(0x00457810, {picture});
        }
        s_.note(kMemDeleteService, {data});
    }
    switch (goals_windows_levelpic_at(m_, self).field_1d4()) {
    case 0: {
        const std::uint32_t wanted = goals_windows_levelpic_at(m_, self).field_1e0();
        if (wanted != 0xffffffffu) {
            const std::uint32_t current = goals_windows_levelpic_at(m_, self).field_1d8();
            goals_windows_levelpic_at(m_, self).set_field_1d8(static_cast<Word>(wanted));
            goals_windows_levelpic_at(m_, self).set_field_1dc(static_cast<Word>(current));
            goals_windows_levelpic_at(m_, self).set_field_1e0(0xffffffffu);
            engine(0x00457130, {word_at(m_, self + kLPictures + 4 * wanted)});
            goals_windows_levelpic_at(m_, self).set_field_264(0);
            goals_windows_levelpic_at(m_, self).set_field_268(static_cast<Word>(goals_windows_levelpic_at(m_, self).field_26c()));
            goals_windows_levelpic_at(m_, self).set_field_1d4(1);
        }
        break;
    }
    case 1: {
        const std::uint32_t outgoing = word_at(m_, self + kLPictures + 4 * goals_windows_levelpic_at(m_, self).field_1dc());
        const std::int32_t scale = int_at(m_, outgoing + 0xc) + 0x190;
        set_word_at(m_, outgoing + 0xc, u(scale));
        const std::int32_t width = static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_1cc());
        const std::int32_t limit = (static_cast<std::int32_t>(u(scale) * u(width)) >> 13) + (width >> 1);
        const std::int32_t speed = static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_268());
        goals_windows_levelpic_at(m_, self).set_field_264(static_cast<Word>(goals_windows_levelpic_at(m_, self).field_264() + u(speed)));
        const std::int32_t half = speed >> 1;
        goals_windows_levelpic_at(m_, self).set_field_268(static_cast<Word>(u(speed > 0 ? half + speed + 1 : half + speed - 1)));
        const std::int32_t position = static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_264());
        if (position > limit || position < -limit) {
            goals_windows_levelpic_at(m_, self).set_field_1e4(2);
            goals_windows_levelpic_at(m_, self).set_field_1d4(2);
        }
        break;
    }
    case 2:
        if (goals_windows_levelpic_at(m_, self).field_1e4() != 0) {
            goals_windows_levelpic_at(m_, self).set_field_1e4(static_cast<Word>(goals_windows_levelpic_at(m_, self).field_1e4() - 1));
            if (goals_windows_levelpic_at(m_, self).field_1e4() == 0) {
                const std::uint32_t outgoing = word_at(m_, self + kLPictures + 4 * goals_windows_levelpic_at(m_, self).field_1dc());
                set_word_at(m_, outgoing + 0xc, 0x1000);
                engine(0x00457150, {outgoing});
                goals_windows_levelpic_at(m_, self).set_field_1d4(0);
                levelpic_lock_text(self);
            }
        }
        break;
    default:
        break;
    }
    if (goals_windows_levelpic_at(m_, self).field_278() == 0) {
        goals_windows_levelpic_at(m_, self).set_field_278(0x28);
    }
    goals_windows_levelpic_at(m_, self).set_field_278(static_cast<Word>(goals_windows_levelpic_at(m_, self).field_278() - 1));
    w_.element_update(self);
}

// 0x0045c050: the two lines that say what a locked level needs
void GoalScreens::levelpic_lock_text(std::uint32_t self) {
    const std::int32_t level = static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_1d8());
    if (video_level_available(level, static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_28()))) {
        goals_windows_levelpic_at(m_, self).set_field_178(0);
        return;
    }
    auto put = [&](std::uint32_t at, const std::string& text) {
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        m_.write(at, bytes);
    };
    auto text_at = [&](std::uint32_t variable) { return m_.cstring(word_at(m_, variable), 0x100); };
    std::string line1, line2;
    if (static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_28()) != 1) {
        line1 = text_at(0x0052e95c);                                       // "play career mode"
        line2 = text_at(0x0052e958);                                       // "to unlock level"
    } else {
        const std::int32_t needs = career_.level_needs(level);
        if (level == 0xd) {
            line1 = text_at(0x0052e944);                                   // "Can't Play Custom Park"
            line2 = text_at(0x0052e948);                                   // "in career mode"
        } else if (level >= 0xa) {
            line1 = text_at(0x0052ea14);                                   // "Can't Play THPS 1 Levels"
            line2 = text_at(0x0052e948);
        } else {
            if (needs >= 0) {
                line1 = retail_format(text_at(0x0052e94c), {}, {needs});   // "need %d dollars"
            } else if (needs == -1) {
                line1 = text_at(0x0052e950);                               // "need 1 medal"
            } else {
                line1 = retail_format(text_at(0x0052e954), {}, {-needs});  // "need %d medals"
            }
            line2 = text_at(0x0052e958);
        }
    }
    put(self + kLLine1, line1);
    put(self + kLLine2, line2);
    const std::int32_t half = static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_1cc()) >> 1;
    goals_windows_levelpic_at(m_, self).set_field_1bc(static_cast<Word>(u(half)));
    set_word_at(m_, self + kLPlaceX + 4, u(half));
    const std::int32_t extent1 = f_.text_extent(self + kLFont, self + kLLine1);
    set_word_at(m_, self + kLPlaceX + 8, u((static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_1d0()) >> 1) - (extent1 >> 1) - 2));
    const std::int32_t extent2 = f_.text_extent(self + kLFont, self + kLLine2);
    set_word_at(m_, self + kLPlaceX + 0xc, u((extent2 >> 1) + (static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_1d0()) >> 1) + 2));
    goals_windows_levelpic_at(m_, self).set_field_178(1);
}

void GoalScreens::levelpic_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (goals_windows_levelpic_at(m_, self).field_178() != 0) {
        for (std::uint32_t k = 0; k < 2; ++k) {
            const std::uint32_t text = self + kLLine1 + 0x20 * k;
            s_.note(kFontDrawService, {self + kLFont, goals_windows_levelpic_at(m_, self).field_20() + u(x) + word_at(m_, self + kLPlaceX + 4 * k), goals_windows_levelpic_at(m_, self).field_24() + u(y) + word_at(m_, self + kLPlaceX + 8 + 4 * k), text, 0},
                    m_.cstring(text, 0x100));
        }
    }
    // The clip rectangle goes round the pictures (the software renderer clips to the window) and is taken off again, the other way round.
    const bool software = global_word(m_, kSoftwareRenderer) != 0;
    auto clip_to_window = [&] { s_.note(kSvcFillRect, {goals_windows_levelpic_at(m_, self).field_20() + u(x), goals_windows_levelpic_at(m_, self).field_24() + u(y), goals_windows_levelpic_at(m_, self).field_1cc(), goals_windows_levelpic_at(m_, self).field_1d0(), 0}); };
    auto clip_to_screen = [&] { s_.note(kSvcFillRect, {0, 0, 0x200, 0x100, 0}); };
    if (software) {
        clip_to_window();
    } else {
        clip_to_screen();
    }
    // each picture draws through slot 3 of its class (the bitmap image's: Image_SlicedDraw 0x00457160, x, y, far end); the probe reported the call as the sprite quad
    // it makes, engine_004d7990
    const std::string draw = engine_name(0x004d7990);
    if (goals_windows_levelpic_at(m_, self).field_1d4() == 1) {
        vcall_item(draw.c_str(), word_at(m_, self + kLPictures + 4 * goals_windows_levelpic_at(m_, self).field_1dc()), 3,
                   {goals_windows_levelpic_at(m_, self).field_264() + goals_windows_levelpic_at(m_, self).field_25c() + goals_windows_levelpic_at(m_, self).field_20() + u(x), goals_windows_levelpic_at(m_, self).field_260() + goals_windows_levelpic_at(m_, self).field_24() + u(y), 0});
    }
    vcall_item(draw.c_str(), word_at(m_, self + kLPictures + 4 * goals_windows_levelpic_at(m_, self).field_1d8()), 3,
               {goals_windows_levelpic_at(m_, self).field_25c() + goals_windows_levelpic_at(m_, self).field_20() + u(x), goals_windows_levelpic_at(m_, self).field_260() + goals_windows_levelpic_at(m_, self).field_24() + u(y), 0});
    if (software) {
        clip_to_screen();
    } else {
        clip_to_window();
    }
}

void GoalScreens::levelpic_select(std::uint32_t self, std::int32_t level, std::int32_t speed) {
    if (level != static_cast<std::int32_t>(goals_windows_levelpic_at(m_, self).field_1d8())) {
        goals_windows_levelpic_at(m_, self).set_field_1e0(static_cast<Word>(u(level)));
        goals_windows_levelpic_at(m_, self).set_field_26c(static_cast<Word>(u(speed * 2 - 1)));
    }
}

}  // namespace opentony::runtime::frontend
