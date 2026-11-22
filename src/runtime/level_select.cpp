#include "auto_views.hpp"
#include "level_select.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// Window fields
constexpr std::uint32_t kBannerA = 0xac;
constexpr std::uint32_t kBannerB = 0xb0;
constexpr std::uint32_t kBannerX = 0xb4;
constexpr std::uint32_t kPrompts = 0xb8;        // five: select, accept, back, goals, blank
constexpr std::uint32_t kNameWindow = 0xcc;
constexpr std::uint32_t kGlobe = 0xd0;
constexpr std::uint32_t kInfo = 0xd4;
constexpr std::uint32_t kGoals = 0xd8;
constexpr std::uint32_t kMenu = 0xdc;
constexpr std::uint32_t kPercent = 0xe0;
constexpr std::uint32_t kBackground = 0xe4;
constexpr std::uint32_t kFontUnused = 0xe8;     // mainf's slot: built and destroyed, never filled
constexpr std::uint32_t kFontB = 0x234;         // s2font2
constexpr std::uint32_t kFontC = 0x380;         // s2font1
constexpr std::uint32_t kFontD = 0x4cc;         // s2font3
constexpr std::uint32_t kState = 0x618;
constexpr std::uint32_t kParkMenu = 0x61c;
constexpr std::uint32_t kDone = 0x620;
constexpr std::uint32_t kAnswer = 0x624;

constexpr std::uint32_t kPad0 = 0x0056aff8;
constexpr std::uint32_t kPad1 = 0x0056b164;
constexpr std::uint32_t kPlayerSpool = 0x0056db4c;
constexpr std::uint32_t kTwoSkaters = 0x00561c70;       // byte: two-skater mode
constexpr std::uint32_t kLoaderFlag = 0x005614bc;
constexpr std::uint32_t kLoaderBase = 0x005614cc;
constexpr std::uint32_t kEventSelector = 0x00533f38;
constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kFillHeight = 0x00527d54;

constexpr std::uint32_t kMsgNotCareer = 0x0052debc;     // "must be in career mode"
constexpr std::uint32_t kMsgParkLoader = 0x0052df24;    // "park loader already allocated"
constexpr std::uint32_t kFormatPercent = 0x0052df44;    // "%2d%%"

constexpr std::uint32_t kLevelFonts[6] = {0x00525458, 0x0052796c, 0x0052544c, 0x00526acc, 0x005287a4, 0x005254e0};   // mainf, s2font3, s2font2, s2font1, ingame1, s2tricks

// The answer the screen gives for the goals button (the loop starts the level and comes back)
constexpr std::int32_t kAnswerGoals = 0x22b;

}  // namespace

// ---- the screen callbacks and the loop --------------------------------------------------------------------------------------------------------------

// 0x004588c0: the screen table asks each screen for its two callbacks.
void LevelSelect::init_callbacks(std::uint32_t record) {
    set_word_at(m_, record, 0x004588e0);
    set_word_at(m_, record + 0x0c, 0x004588f0);
}

void LevelSelect::exit_callback() {}

// 0x004588f0: `*result = LevelSelect_Main()`.
void LevelSelect::screen_call(std::uint32_t argument, std::uint32_t result) {
    (void)argument;
    set_word_at(m_, result, u(run()));
}

// 0x00458900: the fonts, then rounds of a new window and the frame loop until the window is done. A level (or -1, backed out) ends the screen; the answer 0x22b
// (the goals button) starts the level's goals in career mode and begins another round.
std::int32_t LevelSelect::run() {
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    // a rectangle (width 0x200, height 0x100) in a local, handed to the engine with the camera's field of view
    scratch_line({});
    set_global_half(m_, kScratchLine, 0x200);
    set_global_half(m_, kScratchLine + 2, 0);
    set_global_half(m_, kScratchLine + 4, 0x200);
    set_global_half(m_, kScratchLine + 6, 0x100);
    engine(0x004e8730, {kScratchLine, 0x80, 0, 0});
    set_global_byte(m_, 0x0055ff90, 0);
    for (std::uint32_t font : kLevelFonts) {
        font_load(font);
    }
    engine(0x004c8f30);
    const std::uint32_t out = kScratchLine + 0xf8;      // the answer is a local in retail
    for (;;) {
        set_word_at(m_, out, 0xffffffffu);
        const std::uint32_t memory = operator_new(0x628);
        std::uint32_t window = 0;
        if (memory != 0) {
            window = construct(memory);
        }
        engine(0x00489670, {kPad0});
        engine(0x00489670, {kPad1});
        for (;;) {
            game_frame_tick();
            engine(0x004f5ff0);
            engine(0x004dae00);
            engine(0x00418150);
            bool done;
            if (seams_) {
                // (the seam's two scripted values: what it writes through the pointer, then what it answers)
                s_.note(engine_name(0x00459d90), {window});
                set_word_at(m_, out, static_cast<std::uint32_t>(s_.value(engine_name(0x00459d90))));
                done = (static_cast<std::uint32_t>(s_.value(engine_name(0x00459d90))) & 0xff) != 0;
            } else {
                done = step(window, out);
            }
            if (done) {
                break;
            }
            engine(0x004304b0);
            engine(0x00466be0);
            engine(0x00489a10);
            if (seams_) {
                engine(0x00459650, {window});
                engine(0x004595e0, {window});
            } else {
                update(window);
                draw(window);
            }
            engine(0x004e8740, {0});
            engine(0x004c9300, {1});
            if (global_word(m_, kGlobalSimTick) < global_word(m_, kGlobalLastTick) + 2) {
                do {
                    game_frame_tick();
                } while (global_word(m_, kGlobalSimTick) < global_word(m_, kGlobalLastTick) + 2);
            }
            set_global_word(m_, kGlobalLastTick, global_word(m_, kGlobalSimTick));
            engine(0x00430510);
            engine(0x00466c00);
            if ((engine_value(0x00452d30) & 0xff) != 0) {
                break;
            }
        }
        if (window != 0) {
            if (seams_) {
                engine(0x004594e0, {window, 1});
            } else {
                scalar_delete(window, 1);
            }
        }
        const std::int32_t answer = int_at(m_, out);
        if (answer != kAnswerGoals) {
            if (answer == -1) {
                engine(0x004304b0);
            }
            for (std::uint32_t font : kLevelFonts) {
                font_unload(font);
            }
            set_global_word(m_, kGlobalFrontFlag, saved);
            return answer;
        }
        check(global_word(m_, kLevelGameMode) == 1, kMsgNotCareer);
        set_global_word(m_, kEventSelector, 1);
        set_global_word(m_, kCurrentLevel, global_word(m_, kChosenLevel));
        engine(0x00489490);
        engine(0x0045d830, {1});
        engine(0x0045db40);
    }
}

// ---- the window ---------------------------------------------------------------------------------------------------------------------------------------

std::uint32_t LevelSelect::copy_font(std::uint32_t dest, std::uint32_t name) {
    copy_font_named(dest, name);
    return dest;
}

// 0x00458b90: four fonts, the background, five prompt windows, then the other classes (every one is built by its own constructor, a seam here): the medal window
// (career), the banners, the globe, the level's name, the information windows and the mode menu.
std::uint32_t LevelSelect::construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontUnused);
    font_construct(self + kFontB);
    font_construct(self + kFontC);
    font_construct(self + kFontD);
    set_word_at(m_, self, kVtLevelSelect);
    engine(0x004b52b0, {global_word(m_, kPlayerSpool)});
    set_word_at(m_, self + kBackground, background_sprite(0x0052df18));
    (void)find_font(kLevelFonts[0]);                    // (mainf is looked up and not used)
    // s2font3: the title font
    copy_font(self + kFontD, kLevelFonts[1]);
    set_word_at(m_, self + kFontD + 4, 0);
    set_word_at(m_, self + kFontD + 8, 3);
    set_word_at(m_, self + kFontD + 0x14, 0x80);
    set_word_at(m_, self + kFontD + 0x18, 0x80);
    set_word_at(m_, self + kFontD + 0x1c, 0x80);
    set_byte_at(m_, self + kFontD + 0x21, 0);
    set_word_at(m_, self + kFontD + 0xc, 0);
    set_word_at(m_, self + kFontD + 0x10, 0);
    set_word_at(m_, self + kFontD + 0x30, 0x800);
    // s2font2: the prompt windows' captions
    copy_font(self + kFontB, kLevelFonts[2]);
    set_word_at(m_, self + kFontB + 4, 0);
    set_word_at(m_, self + kFontB + 8, 3);
    set_word_at(m_, self + kFontB + 0x14, 0x80);
    set_word_at(m_, self + kFontB + 0x18, 0x80);
    set_word_at(m_, self + kFontB + 0x1c, 0x80);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0);
    set_word_at(m_, self + kFontB + 0x10, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x1000);
    // s2font1: the map's labels
    copy_font(self + kFontC, kLevelFonts[3]);
    set_word_at(m_, self + kFontC + 4, 0);
    set_word_at(m_, self + kFontC + 8, 3);
    f_.set_rgb(self + kFontC, 0x80, 0x80, 0x80);
    set_byte_at(m_, self + kFontC + 0x21, 0);
    set_word_at(m_, self + kFontC + 0xc, 0xffffffffu);
    set_word_at(m_, self + kFontC + 0x10, 0);
    set_word_at(m_, self + kFontC + 0x30, 0x800);
    // the prompt windows: select, accept, back, goals (below the screen until a goal is wanted) and blank
    set_word_at(m_, self + kPrompts, prompt(0x005258d8, 0x005252f0, self + kFontB, 0x8c, 0xdc));
    set_word_at(m_, self + kPrompts + 4, prompt(0x0052540c, 0x005252f0, self + kFontB, 0x190, 0xdc));
    set_word_at(m_, self + kPrompts + 8, prompt(0x00525400, 0x005252f0, self + kFontB, 0x32, 0xdc));
    set_word_at(m_, self + kPrompts + 12, prompt(0x0052df08, 0x005252f0, self + kFontB, 0x122, 0x3e7));
    std::uint32_t block = operator_new(0xb0);
    std::uint32_t blank = 0;
    if (block != 0) {
        s_.note(kSvcPromptConstruct, {block, 0x00527840, 0x005252f0, self + kFontB, 0xffffffb9u, 0x3e7, 0xffffffffu, 0xffffffffu}, m_.cstring(0x00527840));
        blank = static_cast<std::uint32_t>(s_.value(kSvcPromptConstruct));
    }
    set_word_at(m_, self + kPrompts + 16, blank);
    const std::uint32_t mode = global_word(m_, kLevelGameMode);
    if (mode != 1) {
        for (std::uint32_t i = 0; i < 5; ++i) {
            vcall_item(kElementSnapX, word_at(m_, self + kPrompts + 4 * i), 5);
        }
    }
    if (mode == 1) {
        block = operator_new(0xb8);
        std::uint32_t medal = 0;
        if (block != 0) {
            medal = engine_value(0x004a1670, {block, self + kFontD, 0xf4, 0x85, 0x200, 0x85});
        }
        w_.window_add(self, medal);
    }
    set_word_at(m_, self + kBannerX, 0x1ab);
    auto banner = [&](std::int32_t player, std::int32_t y) {
        const std::uint32_t memory = operator_new(0x22c);
        std::uint32_t object = 0;
        if (memory != 0) {
            object = engine_value(0x00459db0, {memory, u(player), 0x1ab, u(y), 0x280, 0xffffffffu});
        }
        return object;
    };
    if ((engine_value(0x004de150) & 0xff) != 0) {
        set_word_at(m_, self + kBannerA, 0);
        set_word_at(m_, self + kBannerB, 0);
    } else if (mode == 1) {
        const std::uint32_t first = banner(0, 0x77);
        set_word_at(m_, self + kBannerA, first);
        set_word_at(m_, self + kBannerB, 0);
        w_.window_add(self, first);
    } else if (global_byte(m_, kTwoSkaters) == 0) {
        const std::uint32_t first = banner(0, 0xb4);
        set_word_at(m_, self + kBannerA, first);
        w_.window_add(self, first);
        set_word_at(m_, self + kBannerB, 0);
    } else {
        const std::uint32_t first = banner(0, 0x76);
        set_word_at(m_, self + kBannerA, first);
        w_.window_add(self, first);
        const std::uint32_t second = banner(1, 0xb4);
        set_word_at(m_, self + kBannerB, second);
        w_.window_add(self, second);
    }
    // the globe, the level's name, the information windows and the mode menu
    auto add_object = [&](std::uint32_t size, std::uint32_t constructor, std::vector<std::uint32_t> args, std::uint32_t slot) {
        const std::uint32_t memory = operator_new(size);
        std::uint32_t object = 0;
        if (memory != 0) {
            args.insert(args.begin(), memory);
            object = engine_value(constructor, std::move(args));
        }
        set_word_at(m_, self + slot, object);
        w_.window_add(self, object);
    };
    add_object(0xa4, 0x0045a1b0, {0x16f, 0x46, 0xffffffffu, 0xffffffffu}, kGlobe);
    add_object(0x210, 0x0045a6e0, {0x38, 0x23, 0xffffffffu, 0xffffffffu}, kNameWindow);
    add_object(0x14c, 0x0045b0a0, {global_word(m_, kChosenLevel), 1, 0x3d, 0x7e, 0xffffff80u, 0x7e}, kInfo);
    add_object(0x27c, 0x0045b950, {global_word(m_, kChosenLevel), mode, 0x41, 0x2e, 0xffffffffu, 0xffffffffu}, kGoals);
    add_object(0x240, 0x0045abc0, {self + kFontC, 0x100, 0x78}, kMenu);
    set_word_at(m_, word_at(m_, self + kMenu) + 0x14, 1);
    if (mode == 1) {
        block = operator_new(0xb0);
        std::uint32_t percent = 0;
        if (block != 0) {
            s_.note(kSvcPromptConstruct, {block, 0x0052def4, 0x0052df04, self + kFontB, 0x4b, 0x6a, 0xffffffffu, 0xffffffffu}, m_.cstring(0x0052def4));
            percent = static_cast<std::uint32_t>(s_.value(kSvcPromptConstruct));
        }
        set_word_at(m_, self + kPercent, percent);
        w_.window_add(self, percent);
    }
    set_word_at(m_, self + kState, 0);
    set_byte_at(m_, self + kDone, 0);
    set_word_at(m_, self + kParkMenu, 0);
    change_level(self, 0);
    engine(0x004b5580, {global_word(m_, kPlayerSpool)});
    return self;
}

std::uint32_t LevelSelect::scalar_delete(std::uint32_t self, std::uint32_t flag) {
    destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00459500: the five prompts, the background (its sprite object, then its block), the park menu, the four fonts and the window base (which deletes the rest).
void LevelSelect::destroy(std::uint32_t self) {
    set_word_at(m_, self, kVtLevelSelect);
    for (std::uint32_t i = 0; i < 5; ++i) {
        if (word_at(m_, self + kPrompts + 4 * i) != 0) {
            delete_object(word_at(m_, self + kPrompts + 4 * i));
        }
    }
    const std::uint32_t sprite = word_at(m_, self + kBackground);
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    if (word_at(m_, self + kParkMenu) != 0) {
        delete_object(word_at(m_, self + kParkMenu));
    }
    font_destroy(self + kFontD);
    font_destroy(self + kFontC);
    font_destroy(self + kFontB);
    font_destroy(self + kFontUnused);
    w_.window_destroy(self);
}

// 0x004595e0: the prompts draw (unless the park menu, in the middle of loading, is up), then the park menu or the window and the background.
void LevelSelect::draw(std::uint32_t self) {
    const std::uint32_t park = word_at(m_, self + kParkMenu);
    if (park == 0 || int_at(m_, park + 0x5c) != 1) {
        for (std::uint32_t i = 0; i < 5; ++i) {
            vcall_item(kElementDraw, word_at(m_, self + kPrompts + 4 * i), 2, {0, 0});
        }
    }
    if (park != 0) {
        engine(0x00432c10, {park});
        return;
    }
    w_.window_draw(self, 0, 0);
    engine(0x004d7990, {word_at(m_, self + kBackground), word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
}

// 0x00459650: the screen's state machine. State 0 chooses the level (left and right move the globe, confirm picks it, back leaves, the goals button asks for the
// level's goals in career mode); in the career confirming a level opens state 1, the mode menu (Play, back, Saved Parks, Pre-made parks), whose park rows build the
// park menu and enter state 2 (the park menu runs on its own until it answers). The prompts are updated last.
void LevelSelect::update(std::uint32_t self) {
    engine(0x004b55c0, {global_word(m_, kPlayerSpool)});
    if (global_byte(m_, kTwoSkaters) == 0) {
        engine(0x00489670, {kPad1});
    }
    bool back = false;
    auto clear_pair = [&](std::uint32_t a, std::uint32_t b) {
        set_byte_at(m_, a, 0);
        set_byte_at(m_, b, 0);
    };
    auto banners_to = [&](std::uint32_t x) {
        if (word_at(m_, self + kBannerA) != 0) {
            set_word_at(m_, word_at(m_, self + kBannerA) + 8, x);
        }
        if (word_at(m_, self + kBannerB) != 0) {
            set_word_at(m_, word_at(m_, self + kBannerB) + 8, x);
        }
    };
    // the park menu: a Saved Parks menu (kind 1) or a Pre-made parks one (kind 0), handed the pad that confirmed
    auto park_menu = [&](std::uint32_t kind, std::uint32_t pad) {
        check(word_at(m_, self + kParkMenu) == 0, kMsgParkLoader);
        const std::uint32_t memory = operator_new(0x144);
        std::uint32_t menu = 0;
        if (memory != 0) {
            menu = engine_value(0x004329d0, {memory, kind, word_at(m_, global_word(m_, kLoaderBase)), 0, pad});
        }
        set_word_at(m_, self + kParkMenu, menu);
    };
    const std::uint32_t out = kScratchLine + 0xf0;      // the mode menu's second answer is a local in retail
    const std::uint32_t state = word_at(m_, self + kState);
    if (state == 0) {
        if (global_byte(m_, 0x0056b079) != 0 || global_byte(m_, 0x0056b1e5) != 0) {
            sfx(0xea);
            clear_pair(0x0056b079, 0x0056b1e5);
            change_level(self, -1);
        }
        if (global_byte(m_, 0x0056b089) != 0 || global_byte(m_, 0x0056b1f5) != 0) {
            sfx(0xea);
            clear_pair(0x0056b089, 0x0056b1f5);
            change_level(self, 1);
        }
        if (global_byte(m_, 0x0056b029) != 0 || global_byte(m_, 0x0056b0d9) != 0 || global_byte(m_, 0x0056b195) != 0 || global_byte(m_, 0x0056b245) != 0) {
            const std::uint32_t level = global_word(m_, kChosenLevel);
            set_global_byte(m_, 0x0056b029, 0);
            set_global_byte(m_, 0x0056b195, 0);
            set_global_byte(m_, 0x0056b0d9, 0);
            set_global_byte(m_, 0x0056b245, 0);
            if (level == 0xd) {
                if ((engine_value(0x0045b8a0, {0xd, global_word(m_, kLevelGameMode)}) & 0xff) != 0) {
                    banners_to(0x2bc);
                    set_word_at(m_, word_at(m_, self + kMenu) + 0x14, 0);
                    set_word_at(m_, self + kState, 1);
                } else {
                    sfx(0x92);
                }
            } else if ((engine_value(0x0045b8a0, {level, global_word(m_, kLevelGameMode)}) & 0xff) != 0) {
                sfx(0x91);
                set_word_at(m_, self + kAnswer, global_word(m_, kChosenLevel));
                set_byte_at(m_, self + kDone, 1);
            } else {
                sfx(0x92);
            }
        }
        if (global_byte(m_, 0x0056aff9) != 0 || global_byte(m_, 0x0056b165) != 0) {
            sfx(0x93);
            clear_pair(0x0056aff9, 0x0056b165);
            set_word_at(m_, self + kAnswer, 0xffffffffu);
            set_byte_at(m_, self + kDone, 1);
        }
        if (global_byte(m_, 0x0056b019) != 0) {
            if (global_word(m_, kLevelGameMode) != 1) {
                // (the prompts only)
                for (std::uint32_t i = 0; i < 5; ++i) {
                    vcall_item(kElementUpdate, word_at(m_, self + kPrompts + 4 * i), 1);
                }
                w_.window_update(self);
                return;
            }
            const std::uint32_t level = global_word(m_, kChosenLevel);
            set_global_byte(m_, 0x0056b019, 0);
            if (level != 8 && level != 9 && level != 0xd) {
                sfx(0x91);
                set_word_at(m_, self + kAnswer, u(kAnswerGoals));
                set_byte_at(m_, self + kDone, 1);
            }
        }
    } else if (state == 1) {
        if (global_byte(m_, 0x0056aff9) != 0 || global_byte(m_, 0x0056b165) != 0) {
            sfx(0x93);
            clear_pair(0x0056aff9, 0x0056b165);
            back = true;
        }
        // (the mode menu's answer is a seam with two scripted values: the pad that confirmed, written through the pointer, then the chosen row)
        scratch_line({});
        const bool live = s_.live();   // live: the native writes the pad word through the pointer (the second argument) and returns the row
        s_.note(engine_name(0x0045b080), live ? std::vector<std::uint32_t>{word_at(m_, self + kMenu), out} : std::vector<std::uint32_t>{word_at(m_, self + kMenu)});
        if (!live) {
            set_word_at(m_, out, static_cast<std::uint32_t>(s_.value(engine_name(0x0045b080))));
        }
        const std::uint32_t result = static_cast<std::uint32_t>(s_.value(engine_name(0x0045b080)));
        bool to_choosing = false;
        switch (result) {
        case 1:
            engine(0x004de180, {1, 0x0053d7b4});
            set_word_at(m_, self + kAnswer, global_word(m_, kChosenLevel));
            set_byte_at(m_, self + kDone, 1);
            break;
        case 2:
            to_choosing = true;
            break;
        case 3:
        case 4: {
            set_word_at(m_, self + kState, 2);
            engine(0x004b5300, {global_word(m_, kPlayerSpool)});
            engine(0x00454e60);
            park_menu(result == 3 ? 1 : 0, word_at(m_, out));
            if (result == 4) {
                set_word_at(m_, word_at(m_, self + kPrompts) + 0xc, global_word(m_, kFillHeight) + 0x14);
                set_word_at(m_, word_at(m_, self + kPrompts + 16) + 0xc, 0xdc);
            }
            break;
        }
        default:
            break;
        }
        if (to_choosing || back) {
            banners_to(word_at(m_, self + kBannerX));
            set_word_at(m_, word_at(m_, self + kMenu) + 0x14, 1);
            set_word_at(m_, self + kState, 0);
        }
    } else if (state == 2) {
        const std::uint32_t answer = engine_value(0x00432c60, {word_at(m_, self + kParkMenu)});
        if (answer == 1 || answer == 2) {
            if (answer == 1) {
                engine(0x0045ab50, {word_at(m_, self + kNameWindow)});
                set_global_word(m_, kLoaderFlag, 3);
            }
            const std::uint32_t park = word_at(m_, self + kParkMenu);
            set_word_at(m_, self + kState, 1);
            if (park != 0) {
                delete_object(park);
            }
            set_word_at(m_, self + kParkMenu, 0);
            engine(0x004b5350, {global_word(m_, kPlayerSpool)});
            set_word_at(m_, word_at(m_, self + kPrompts) + 0xc, 0xdc);
            set_word_at(m_, word_at(m_, self + kPrompts + 16) + 0xc, global_word(m_, kFillHeight) + 0x14);
            engine(0x0045b890, {word_at(m_, self + kInfo), global_word(m_, kChosenLevel)});
        }
    }
    // the goals button's prompt shows on the park levels (8 to 12) and the custom park option
    if (global_word(m_, kLevelGameMode) == 1) {
        const std::uint32_t level = global_word(m_, kChosenLevel);
        const bool park_level = (level >= 8 && level <= 0xc) || level == 0xd;
        set_word_at(m_, word_at(m_, self + kPercent) + 0x14, park_level ? 1 : 0);
    }
    for (std::uint32_t i = 0; i < 5; ++i) {
        vcall_item(kElementUpdate, word_at(m_, self + kPrompts + 4 * i), 1);
    }
    w_.window_update(self);
}

// 0x00459be0: move the chosen level one step (zero means forward on the park levels), wrapping 0..13, past the park levels that are not open; then refresh the globe,
// the name, the information windows and, in the career, the goal percentage (tapes found, or the goal medals' bits).
void LevelSelect::change_level(std::uint32_t self, std::int32_t step) {
    bool park_step = false;
    for (;;) {
        std::int32_t level = static_cast<std::int32_t>(global_word(m_, kChosenLevel)) + step;
        set_global_word(m_, kChosenLevel, u(level));
        if (level > 0xd) {
            park_step = false;
            set_global_word(m_, kChosenLevel, 0);
            break;
        }
        if (level < 0) {
            set_global_word(m_, kChosenLevel, 0xd);
            park_step = false;
            break;
        }
        if (level != 8 && level != 9 && (level < 10 || level > 0xc)) {
            park_step = false;
            break;
        }
        park_step = true;
        if (step == 0) {
            step = 1;
        }
        if ((engine_value(0x0045b8a0, {u(level), global_word(m_, kLevelGameMode)}) & 0xff) != 0) {
            break;
        }
    }
    const std::int32_t level = static_cast<std::int32_t>(global_word(m_, kChosenLevel));
    if (global_word(m_, kLevelGameMode) == 1) {
        if (park_step || level == 0xd) {
            set_word_at(m_, word_at(m_, self + kPrompts + 12) + 0xc, global_word(m_, kFillHeight) + 0x14);
        } else {
            set_word_at(m_, word_at(m_, self + kPrompts + 12) + 0xc, 0xdc);
        }
    }
    engine(0x0045a9a0, {word_at(m_, self + kNameWindow), u(level)});
    engine(0x0045a6b0, {word_at(m_, self + kGlobe), u(level)});
    engine(0x0045b890, {word_at(m_, self + kInfo), u(level)});
    engine(0x0045c360, {word_at(m_, self + kGoals), u(level), step < 0 ? 1u : 0u});
    const std::uint32_t slot = engine_value(0x004b18a0, {0});
    const std::uint32_t config = engine_value(0x00416050, {slot});
    std::int32_t percent = 0;
    if (byte_at(m_, level_row(level) + 0x48) == 0) {
        percent = static_cast<std::int32_t>(engine_value(0x00414130, {config, u(level)})) * 10;
    } else {
        const std::uint32_t bits = half_at(m_, config + 0xc + 2 * u(level));
        if ((bits & 0x400) != 0) {
            percent = 0x5a;
        } else if ((bits & 0x800) != 0) {
            percent = 0x3c;
        } else if ((bits & 0x1000) != 0) {
            percent = 0x1e;
        }
        if ((bits & 0x8000) != 0) {
            percent += 10;
        }
    }
    if (global_word(m_, kLevelGameMode) == 1) {
        const std::uint32_t line = scratch_line(retail_format(m_.cstring(kFormatPercent, 0x10), {}, {percent}));
        engine(0x004a1eb0, {word_at(m_, self + kPercent), line}, m_.cstring(line, 0x20));
    }
}

// 0x00459d90: the screen's answer, and whether it is done.
bool LevelSelect::step(std::uint32_t self, std::uint32_t out) {
    set_word_at(m_, out, word_at(m_, self + kAnswer));
    return byte_at(m_, self + kDone) != 0;
}

}  // namespace opentony::runtime::frontend
