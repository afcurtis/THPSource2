#include "auto_views.hpp"
#include "goals.hpp"
#include "host_options.hpp"
#include "seam_addresses_generated.hpp"

#include <bit>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// the loading screen's statics (0x00562054 .. 0x00562090)
constexpr std::uint32_t kBarHeight = 0x00562054;
constexpr std::uint32_t kBarX = 0x0056205c;
constexpr std::uint32_t kBarWidth = 0x00562060;
constexpr std::uint32_t kBarY = 0x00562064;
constexpr std::uint32_t kCaption = 0x00562068;        // the lines the screen shows (an array of text pointers ended by an empty text)
constexpr std::uint32_t kGoalSprite = 0x00562070;
constexpr std::uint32_t kBackground = 0x00562074;
constexpr std::uint32_t kBarStart = 0x0056207c;
constexpr std::uint32_t kBarElapsed = 0x00562080;
constexpr std::uint32_t kBarLast = 0x00562084;
constexpr std::uint32_t kBarTotal = 0x00562088;
constexpr std::uint32_t kBarActive = 0x0056208c;
constexpr std::uint32_t kBarColour = 0x00562090;
constexpr std::uint32_t kModeFlag = 0x00562078;
constexpr std::uint32_t kGoalsMode = 0x00562079;
constexpr std::uint32_t kPromptBack = 0x00562058;
constexpr std::uint32_t kPromptPlay = 0x0056206c;
constexpr std::uint32_t kMovieSkip = 0x005618c8;

constexpr std::uint32_t kFontNameMain = 0x00526acc;   // "s2font1.fnt"
constexpr std::uint32_t kFontNameSkater = 0x0052e090; // "s2skata1.fnt"
constexpr std::uint32_t kFontNameGoal = 0x00525310;   // "s2goal.fnt"
constexpr std::uint32_t kPromptFile = 0x005252f0;

// The local Font objects of these screens (stack locals in retail), in a scratch region of their own; live the services are given them, the replay the
// scratch address the probe recorded for any stack pointer (`font_at`).
constexpr std::uint32_t kFontA = 0x7ff00800;
constexpr std::uint32_t kFontB = 0x7ff00950;
constexpr std::uint32_t kFontC = 0x7ff00aa0;

}  // namespace

// ---- the progress bar -----------------------------------------------------------------------------------------------------------------------------

void GoalScreens::loading_begin_progress(std::int32_t length_frames, std::int32_t y, std::int32_t width, std::int32_t height) {
    set_global_word(m_, kBarActive, 1);
    set_global_word(m_, kBarElapsed, 0);
    set_global_word(m_, kBarStart, global_word(m_, goals::kTickCounter));
    set_global_word(m_, kBarHeight, u(height));
    set_global_word(m_, kBarTotal, u(length_frames * 60));
    set_global_word(m_, kBarWidth, u(width));
    const std::int32_t x = 0x100 - width / 2;
    set_global_word(m_, kBarX, u(x));
    set_global_word(m_, kBarY, u(y));
    scratch_line("");
    set_global_half(m_, kScratchLine, u(x));
    set_global_half(m_, kScratchLine + 2, u(y));
    set_global_half(m_, kScratchLine + 4, u(width));
    set_global_half(m_, kScratchLine + 6, u(height));
    std::string rect;
    for (std::uint32_t k = 0; k < 8; ++k) {
        rect.push_back(static_cast<char>(byte_at(m_, kScratchLine + k)));
    }
    engine(0x004e8730, {kScratchLine, 0, 0, 0}, rect);                     // the bar's frame, black
}

void GoalScreens::loading_end_progress() { set_global_word(m_, kBarActive, 0); }

void GoalScreens::loading_update_progress() {
    if (global_word(m_, kMovieSkip) != 0 || global_word(m_, kBarActive) == 0) {
        return;
    }
    std::uint32_t elapsed = global_word(m_, goals::kTickCounter) - global_word(m_, kBarStart);
    const std::uint32_t total = global_word(m_, kBarTotal);
    set_global_word(m_, kBarElapsed, elapsed);
    if (elapsed > total) {
        elapsed = total;
        set_global_word(m_, kBarElapsed, elapsed);
    }
    scratch_line("");
    set_global_half(m_, kScratchLine, global_half(m_, kBarX));
    set_global_half(m_, kScratchLine + 2, global_half(m_, kBarY));
    set_global_half(m_, kScratchLine + 6, global_half(m_, kBarHeight));
    std::uint32_t colour;
    std::uint32_t red;
    std::uint32_t last = 0;
    if (total != 0 && elapsed <= total) {
        set_global_half(m_, kScratchLine + 4, static_cast<std::uint32_t>((static_cast<std::uint64_t>(global_word(m_, kBarWidth)) * elapsed) / total));
        red = 0x96;
        colour = (elapsed & 0xffffff00u) | global_byte(m_, kBarColour);
    } else {
        set_global_half(m_, kScratchLine + 4, global_half(m_, kBarWidth));
        red = 0;
        colour = 0;
        last = 0xff;
    }
    std::string rect;
    for (std::uint32_t k = 0; k < 8; ++k) {
        rect.push_back(static_cast<char>(byte_at(m_, kScratchLine + k)));
    }
    engine(0x004e8730, {kScratchLine, red, colour, last}, rect);
    set_global_word(m_, kBarColour, 0);
}

void GoalScreens::loading_tick(std::uint32_t flag) {
    loading_update_progress();
    const std::uint32_t elapsed = global_word(m_, kBarElapsed);
    if ((flag & 0xff) != 0) {
        const std::uint32_t last = global_word(m_, kBarLast);
        if (elapsed > last) {
            set_global_word(m_, kBarTotal, global_word(m_, kBarTotal) + (elapsed - last) - 2);
        }
    }
    set_global_word(m_, kBarLast, elapsed);
}

// 0x0045d7a0: the displayed frame is copied into the drawing buffer through a temporary copy
void GoalScreens::loading_copy_frame() {
    const std::uint32_t x = global_half(m_, kBarX), y = global_half(m_, kBarY) + 0x100, w = global_half(m_, kBarWidth), h = global_half(m_, kBarHeight);
    const std::int32_t size = static_cast<std::int32_t>(global_short(m_, kBarHeight)) * static_cast<std::int32_t>(global_short(m_, kBarWidth)) * 2;
    s_.note(kAllocBuffer, {u(size), 0, 1, 0});
    const std::uint32_t buffer = u(s_.value(kAllocBuffer));
    engine(0x004e8740, {0});
    auto rect = [&](std::uint32_t top) {
        scratch_line("");
        set_global_half(m_, kScratchLine, x);
        set_global_half(m_, kScratchLine + 2, top);
        set_global_half(m_, kScratchLine + 4, w);
        set_global_half(m_, kScratchLine + 6, h);
        std::string bytes;
        for (std::uint32_t k = 0; k < 8; ++k) {
            bytes.push_back(static_cast<char>(byte_at(m_, kScratchLine + k)));
        }
        return bytes;
    };
    engine(0x004e8750, {kScratchLine, buffer}, rect(y));
    engine(0x004e8740, {0});
    engine(0x004e8770, {kScratchLine, buffer}, rect((y + 0xff00) & 0xffff));
    engine(0x004e8740, {0});
    s_.note(kMemDeleteService, {buffer});
}

// ---- the loading picture -----------------------------------------------------------------------------------------------------------------------------

namespace {

void scratch_font_region(RetailMemory& m) {
    m.ensure_region(kFontA, 0x400);
}

}  // namespace

// 0x0045d010: show the picture `file` with the lines of text in `lines` (an array of text pointers ended by an empty text) for two frames; `mode` picks the prompt windows
void GoalScreens::loading_show(std::uint32_t file, std::uint32_t lines, std::uint32_t mode) {
    scratch_font_region(m_);
    font_construct(font_at(kFontA));
    font_construct(font_at(kFontB));
    set_global_byte(m_, kGoalsMode, 0);
    set_global_word(m_, kCaption, lines);
    const bool loaded_main = (engine_value(0x0044af90, {kFontNameMain}, m_.cstring(kFontNameMain, 0x40)) & 0xff) != 0;
    if (!loaded_main) {
        font_load(kFontNameMain);
    }
    copy_font_named(kFontA, kFontNameMain);
    set_global_word(m_, kFontA + 4, 1);
    set_global_byte(m_, kFontA + 0x21, 1);
    set_global_word(m_, kFontA + 0x10, 1);
    set_global_word(m_, kFontA + 0x24, 2);
    set_global_word(m_, kFontA + 0x28, 2);
    set_global_word(m_, kFontA + 8, 0);
    set_global_word(m_, kFontA + 0xc, 3);
    set_global_word(m_, kFontA + 0x30, 0x800);
    set_global_word(m_, kFontA + 0x14, 0x80);
    set_global_word(m_, kFontA + 0x18, 0x80);
    set_global_word(m_, kFontA + 0x1c, 0x96);
    const bool loaded_skater = (engine_value(0x0044af90, {kFontNameSkater}, m_.cstring(kFontNameSkater, 0x40)) & 0xff) != 0;
    if (!loaded_skater) {
        font_load(kFontNameSkater);
    }
    copy_font_named(kFontB, kFontNameSkater);
    set_global_word(m_, kFontB + 4, 0);
    set_global_word(m_, kFontB + 8, 0);
    set_global_byte(m_, kFontB + 0x21, 1);
    set_global_word(m_, kFontB + 0xc, 3);
    set_global_word(m_, kFontB + 0x10, 1);
    set_global_word(m_, kFontB + 0x30, 0x880);
    set_global_word(m_, kFontB + 0x24, 2);
    set_global_word(m_, kFontB + 0x28, 1);
    set_global_word(m_, kFontB + 0x14, 0xaf);
    set_global_word(m_, kFontB + 0x18, 0xaf);
    set_global_word(m_, kFontB + 0x1c, 0xaf);
    set_global_word(m_, kBackground, background_sprite(file));
    switch (mode & 0xff) {
    case 1:
        set_global_word(m_, kPromptBack, prompt(0x005252d8, kPromptFile, font_at(kFontA), 0xea, 0xdc));
        break;
    case 2:
        set_global_word(m_, kPromptPlay, prompt(0x005252e4, kPromptFile, font_at(kFontA), 0xea, 0xdc));
        break;
    default:
        set_global_word(m_, kPromptPlay, prompt(0x005252e4, kPromptFile, font_at(kFontA), 0xa4, 0xdc));
        set_global_word(m_, kPromptBack, prompt(0x005252d8, kPromptFile, font_at(kFontA), 0x132, 0xdc));
        break;
    }
    for (int frame = 0; frame < 2; ++frame) {
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x004304b0);
        engine(0x00466be0);
        std::int32_t y = 0x3c;
        for (std::uint32_t at = global_word(m_, kCaption); byte_at(m_, word_at(m_, at)) != 0; at += 4) {
            const std::uint32_t text = word_at(m_, at);
            if (y == 0x3c) {
                engine(0x0044a070, {font_at(kFontA), 0x100, 0x3c, text, 0}, m_.cstring(text, 0x100));
                y = 0x5a;
            } else {
                engine(0x0044a070, {font_at(kFontB), 0x1a, u(y), text, 0}, m_.cstring(text, 0x100));
                y += 0xf;
            }
        }
        draw_background(global_word(m_, kBackground));
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        engine(0x00430510);
    }
    if (!loaded_main) {
        font_unload(kFontNameMain);
    }
    if (!loaded_skater) {
        font_unload(kFontNameSkater);
    }
    font_destroy(font_at(kFontB));
    font_destroy(font_at(kFontA));
}

// 0x0045d830: choose the picture and the lines from the game mode, the level and the editor, show them and start the bar
void GoalScreens::loading_start(std::uint32_t mode_flag) {
    set_global_byte(m_, kModeFlag, mode_flag);
    set_global_word(m_, kMovieSkip, 0);
    set_global_word(m_, 0x0052e088, mode_flag & 0xff);
    const std::int32_t mode = global_int(m_, career::kGameMode);
    if (mode == 1) {
        s_.note("printf_log", {0x0052e164});
    } else if (mode == 2) {
        s_.note("printf_log", {0x0052e148});
    } else if (mode == 3) {
        s_.note("printf_log", {0x0052e120});
    } else if (mode > 3) {
        s_.note("printf_log", {0x0052e0fc});
    }
    const std::int32_t mode_again = global_int(m_, career::kGameMode);
    auto bar = [&](std::int32_t seconds) { loading_begin_progress(seconds, 0xd2, 0xc8, 3); };
    if (global_byte(m_, 0x00561ca9) != 0) {                                         // the park editor
        loading_show(0x0052e0ec, 0x0052e188, 2);
        bar(0xa);
        return;
    }
    if (global_byte(m_, 0x00561990) != 0 || global_short(m_, 0x0056b8d4) != 0) {
        loading_show(0x0052e0a0, 0x0052e08c, 0);                          // "loadingt.bmp"
        const std::int32_t value = global_short(m_, 0x0056b8d4);
        bar((value < 0 ? -value : value) == 1 ? 0xc : 0x11);
        return;
    }
    const std::int32_t level = global_int(m_, career::kCurrentLevel);
    const bool contest = byte_at(m_, career::level(level) + career::kLvContest) != 0;
    if (mode_again == 1 && !contest) {
        if (level == 8) {
            loading_show(0x0052e0e0, 0x0052e08c, 0);                      // "s2bgd12.bmp"
            bar(0xd);
            return;
        }
        if (level == 9) {
            loading_show(0x0052e0d4, 0x0052e08c, 0);                      // "s2bgd11.bmp"
            bar(0xd);
            return;
        }
        std::uint32_t flag = mode_flag;
        if (global_word(m_, 0x00561c98) != 0) {
            flag = 2;
        }
        engine(0x00415b30, {flag & 0xff});                                // Career_LoadingScreen
        bar(0xd);
        return;
    }
    if (global_byte(m_, 0x00561ca8) != 0) {
        loading_show(0x0052e0ec, 0x0052e1ac, mode_flag);
        bar(0x10);
        return;
    }
    if (contest && mode_again == 1) {
        if (level == 2) {
            loading_show(0x0052e0b0, 0x0052e08c, mode_flag);              // "s2bgd7.bmp"
        } else if (level == 5) {
            loading_show(0x0052e0bc, 0x0052e08c, mode_flag);              // "s2bgd8.bmp"
        } else if (level == 7) {
            loading_show(0x0052e0c8, 0x0052e08c, mode_flag);              // "s2bgd9.bmp"
        } else if (host_options().judge_level >= 0) {
            // the quick start's Competition Session on a level that is not one of the three contests (host_options.hpp judge_level): retail shows nothing here and
            // leaves the lines unset, which the goals screen then reads at 0; the plain loading picture with the empty lines, as the replays have
            loading_show(0x0052e0ec, 0x0052e08c, mode_flag);              // "loadingg.bmp"
        }
        bar(0xd);
        return;
    }
    static const std::uint32_t captions[9] = {0x0052e1cc, 0x0052e1e8, 0x0052e1cc, 0x0052e204, 0, 0x0052e224, 0x0052e244, 0x0052e264, 0x0052e288};
    if (mode_again >= 2 && mode_again <= 10 && captions[mode_again - 2] != 0) {
        loading_show(0x0052e0ec, captions[mode_again - 2], 0);
    }
    bar(global_byte(m_, 0x005614c4) != 0 ? 0x11 : 0xd);
}

// ---- the goals checklist screen ------------------------------------------------------------------------------------------------------------------------

// FUN_004e02d0: the pad scan; the two buttons the loop waits to be released come back as scripted values (live, the scan wrote them itself)
void GoalScreens::pad_scan() {
    engine(0x004e02d0);
    if (s_.live()) {
        return;
    }
    set_global_byte(m_, 0x0056b028, u(s_.value(engine_name(0x004e02d0))));
    set_global_byte(m_, 0x0056b194, u(s_.value(engine_name(0x004e02d0))));
}

void GoalScreens::goals_run() {
    scratch_font_region(m_);
    font_construct(font_at(kFontA));
    font_construct(font_at(kFontB));
    font_construct(font_at(kFontC));
    bool loaded_main = false;
    bool loaded_skater = false;
    const bool checklist = global_byte(m_, kGoalsMode) != 0;
    if (checklist) {
        font_load(kFontNameGoal);
        copy_font_named(kFontC, kFontNameGoal);
        set_global_word(m_, kFontC + 4, 1);
        set_global_word(m_, kFontC + 8, 3);
        set_global_word(m_, kFontC + 0x14, 0x80);
        set_global_word(m_, kFontC + 0x18, 0x80);
        set_global_word(m_, kFontC + 0x1c, 0x80);
        set_global_byte(m_, kFontC + 0x21, 0);
        set_global_word(m_, kFontC + 0xc, 0);
        set_global_word(m_, kFontC + 0x10, 0);
        set_global_word(m_, kFontC + 0x30, 0x800);
        engine(0x0044abb0, {font_at(kFontC), 2});                              // Font_SetMapping
    } else {
        loaded_main = (engine_value(0x0044af90, {kFontNameMain}, m_.cstring(kFontNameMain, 0x40)) & 0xff) != 0;
        if (!loaded_main) {
            font_load(kFontNameMain);
        }
        copy_font_named(kFontA, kFontNameMain);
        set_global_word(m_, kFontA + 4, 1);
        set_global_byte(m_, kFontA + 0x21, 1);
        set_global_word(m_, kFontA + 0x10, 1);
        set_global_word(m_, kFontA + 0x24, 2);
        set_global_word(m_, kFontA + 0x28, 2);
        set_global_word(m_, kFontA + 8, 0);
        set_global_word(m_, kFontA + 0xc, 3);
        set_global_word(m_, kFontA + 0x30, 0x800);
        set_global_word(m_, kFontA + 0x14, 0x80);
        set_global_word(m_, kFontA + 0x18, 0x80);
        set_global_word(m_, kFontA + 0x1c, 0x96);
        loaded_skater = (engine_value(0x0044af90, {kFontNameSkater}, m_.cstring(kFontNameSkater, 0x40)) & 0xff) != 0;
        if (!loaded_skater) {
            font_load(kFontNameSkater);
        }
        copy_font_named(kFontB, kFontNameSkater);
        set_global_word(m_, kFontB + 4, 0);
        set_global_word(m_, kFontB + 8, 0);
        set_global_byte(m_, kFontB + 0x21, 1);
        set_global_word(m_, kFontB + 0xc, 3);
        set_global_word(m_, kFontB + 0x10, 1);
        set_global_word(m_, kFontB + 0x30, 0x880);
        set_global_word(m_, kFontB + 0x24, 2);
        set_global_word(m_, kFontB + 0x28, 1);
        set_global_word(m_, kFontB + 0x14, 0xaf);
        set_global_word(m_, kFontB + 0x18, 0xaf);
        set_global_word(m_, kFontB + 0x1c, 0xaf);
    }
    loading_end_progress();
    auto destroy_prompts = [&] {
        for (const std::uint32_t slot : {kPromptPlay, kPromptBack}) {
            const std::uint32_t window = word_at(m_, slot);
            if (window != 0) {
                vcall_item(kVirtualDelete, window, 0, {1});
                set_word_at(m_, slot, 0);
            }
        }
    };
    auto destroy_sprite = [&](std::uint32_t slot) {
        const std::uint32_t sprite = word_at(m_, slot);
        if (sprite != 0) {
            engine(seam::hal::D3DSprite_Destroy, {sprite});                                   // D3DSprite_Destroy
            s_.note("crt_delete", {sprite});
        }
    };
    auto release_screen = [&](bool back) {
        destroy_prompts();
        destroy_sprite(kBackground);
        if (checklist) {
            destroy_sprite(kGoalSprite);
            font_unload(kFontNameGoal);
            if (!back) {
                set_global_byte(m_, kGoalsMode, 0);
            }
        } else {
            if (!loaded_main) {
                font_unload(kFontNameMain);
            }
            if (!loaded_skater) {
                font_unload(kFontNameSkater);
            }
        }
        if (back) {
            set_global_byte(m_, kGoalsMode, 0);
            set_global_word(m_, kCaption, 0);
            engine(0x004c8f10, {0x34, 2});                                  // Utils_StartFade
        }
        font_destroy(font_at(kFontC));
        font_destroy(font_at(kFontB));
        font_destroy(font_at(kFontA));
    };
    auto wait_release = [&] {
        while (global_byte(m_, 0x0056b028) != 0 || global_byte(m_, 0x0056b194) != 0) {
            game_frame_tick();
            engine(0x00489a10);
            pad_scan();
        }
    };
    auto finish_back = [&] {
        wait_release();
        engine(0x00489670, {0x0056aff8});
        engine(0x00489670, {0x0056b164});
        engine(0x0047fc20, {0});
        engine(0x004dbe60);
        release_screen(true);
    };
    if (global_word(m_, 0x0056a8e8) != 0 || global_short(m_, 0x0056b8d4) != 0 || global_word(m_, 0x0056a89c) != 0) {
        finish_back();
        return;
    }
    engine(0x0047fb20);
    engine(0x004dbd90);
    host_options().goals_screen_up = true;
    for (;;) {                                                              // (the loop jumps back to the frame tick, past these two calls)
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x004304b0);
        engine(0x00466be0);
        engine(0x0047fc00);
        if (global_byte(m_, kGoalsMode) != 0) {
            const std::int32_t level = global_int(m_, career::kCurrentLevel);
            set_global_word(m_, kFontC + 4, 1);
            const std::string title = retail_format(m_.cstring(0x005252c8), {m_.cstring(career::level(level), 0x100)}, {});
            scratch_line(title);
            engine(0x0044a070, {font_at(kFontC), 0x100, 0x2d, kScratchLine, 0}, title);
            std::int32_t y = 0x3c;
            for (std::int32_t goal = 0; y < 0xdc; ++goal, y += 0x10) {
                if (career_.got(goal)) {
                    const float vertical = static_cast<float>(y - 10);
                    engine(0x004d7990, {global_word(m_, kGoalSprite), word_at(m_, global_word(m_, goals::kRendererPointer) + 0x84), 0x42e60000u, std::bit_cast<std::uint32_t>(vertical)});
                }
                set_global_word(m_, kFontC + 4, 0);
                const std::uint32_t row = career::level(level) + career::kLvGoals + career::kGoalStride * u(goal);
                std::uint32_t text = word_at(m_, row + 0x14);
                if (text == 0) {
                    text = word_at(m_, row + 0x10);
                }
                engine(0x0044a070, {font_at(kFontC), 0x82, u(y), text, 0}, m_.cstring(text, 0x100));
                const std::string reward = retail_format(m_.cstring(0x005252c4), {}, {int_at(m_, row + career::kGoalReward)});
                set_global_word(m_, kFontC + 4, 2);
                scratch_line(reward);
                engine(0x0044a070, {font_at(kFontC), 0x1a6, u(y), kScratchLine, 0}, reward);
            }
            set_global_word(m_, kFontC + 4, 1);
        } else {
            std::int32_t y = 0x3c;
            for (std::uint32_t at = global_word(m_, kCaption); byte_at(m_, word_at(m_, at)) != 0; at += 4) {
                if (engine_value(0x004de150) & 0xff) {
                    break;
                }
                const std::uint32_t text = word_at(m_, at);
                if (y == 0x3c) {
                    engine(0x0044a070, {font_at(kFontA), 0x100, 0x3c, text, 0}, m_.cstring(text, 0x100));
                    y = 0x5a;
                } else {
                    engine(0x0044a070, {font_at(kFontB), 0x1a, u(y), text, 0}, m_.cstring(text, 0x100));
                    y += 0xf;
                }
            }
        }
        engine(0x00489a10);
        for (const std::uint32_t slot : {kPromptPlay, kPromptBack}) {
            const std::uint32_t window = word_at(m_, slot);
            if (window != 0) {
                vcall_item("element_update", window, 1);
                vcall_item("element_draw", window, 2, {0, 0});
            }
        }
        draw_background(global_word(m_, kBackground));
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        engine(0x00430510);
        engine(0x00489a10);
        loading_copy_frame();
        engine(0x004305b0);
        if (global_byte(m_, 0x006a1417) != 0) {
            set_global_byte(m_, 0x0056aff9, 1);
            set_global_byte(m_, 0x006a1417, 0);
        }
        pad_scan();
        bool confirmed = false;
        if (global_byte(m_, kModeFlag) != 1 && (engine_value(0x0047fac0) & 0xff) != 0) {
            if (global_byte(m_, 0x0056b029) != 0) {
                confirmed = true;
            } else if (global_byte(m_, 0x0056b195) != 0 && global_int(m_, 0x00533f3c) == 2) {
                confirmed = true;
            }
        }
        if (confirmed) {   // (the screen's Play: the level starts, whatever finish_back's name says)
            sfx(0xb4);
            host_options().goals_screen_up = false;
            finish_back();
            return;
        }
        bool leave = (engine_value(0x0047fad0) & 0xff) != 0;
        if (!leave) {
            const std::uint32_t arg = (global_byte(m_, kModeFlag) != 2 && global_word(m_, 0x00561c98) == 0) ? 0u : 1u;
            leave = engine_value(0x00452430, {arg}) != 0;
        }
        if (leave && host_options().quick_play) {
            // the screen's Back, in a quick start, which has no shell to go back to: the way back to the launcher (the host ends the program at its next poll, as the pause
            // menu's Quit does)
            sfx(0xb0);
            host_options().quit_to_launcher = true;
            leave = false;
        }
        if (leave) {
            sfx(0xb0);
            engine(0x0047fc20, {1});
            set_global_word(m_, 0x0056114c, 0);
            engine(0x0042fc70);
            host_options().goals_screen_up = false;
            release_screen(false);
            return;
        }
        engine(0x004c9300, {1});
        engine(0x00489670, {0x0056aff8});
        engine(0x00489670, {0x0056b164});
    }
}

}  // namespace opentony::runtime::frontend
