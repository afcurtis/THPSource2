#include "core_views.hpp"
#include "seam_addresses_generated.hpp"
#include "frame_pacing.hpp"

#include <bit>
#include <cstring>
#include <stdexcept>

// Phase 4.14c: main.cpp (0x00466b60..0x0046ae8f), the game's entry point and main loop: the polygon arena helpers, the display, the frame loop, the level load and teardown, the intro movies and the
// start-up and shut-down sequences. Written from the machine code: the compiler's choices (the unsigned magic-number division by 240 of the screen scale, 16-bit stores, the order of the stack
// arguments) decide what the retail state looks like afterwards. Everything outside the file is a service reported by its address; the stack locals the retail code passes to services live in
// the scratch area of the replay (`pcplatform::kScratchBase`).

namespace opentony::runtime::core {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kScratch = pcplatform::kScratchBase;
constexpr Word kNormalizeVector = kScratch + 0x500;       // the vector handed to Math_NormalizeFixed12 (a stack local in retail)
constexpr Word kNameBuffer = kScratch + 0x520;            // the skater model name / texture name buffers
constexpr Word kStackObject = kScratch + 0x580;           // a stack-built font object
constexpr Word kStackPointer = kScratch + 0x5c0;          // what every other stack pointer argument is recorded as

// the globals main.cpp works on
constexpr Word kPlayer0 = globals::kPlayer0;
constexpr Word kPlayer1 = globals::kPlayer0 + 4;
constexpr Word kSkaterManager = 0x0056c580;               // the singleton the skater manager methods are called on
constexpr Word kArenaCursor = 0x00560fd0;                 // RenderPolygonArenaCursor
constexpr Word kArenaEnd = 0x00568628;
constexpr Word kScreenWidth = 0x029da394;
constexpr Word kScreenHeight = 0x029da398;
constexpr Word kD3dSoftware = 0x029da3a0;
constexpr Word kFadeLevel = 0x0056a924;
constexpr Word kViewportMode = 0x0056a928;
constexpr Word kLevelEventLatch = 0x00568658;
constexpr Word kSimulationTime = 0x0056e320;
constexpr Word kPublicTick = 0x0056e31c;
constexpr Word kGameStatus = 0x0056a8d0;                  // the level loop's exit code
constexpr Word kCurrentLevel = 0x0056a898;
constexpr Word kMessageLine = 0x00568638;                 // (assert bookkeeping; not reproduced)
constexpr Word kReplayPlayers = 0x0056b8d4;               // word: ReplayNumSkaters (negative while a replay is loaded)
constexpr Word kReplayCursor = 0x0056e774;                // ReplayHighlightCursor
constexpr Word kBufferLoaded = 0x0056e5dc;                // ReplayStreamBuffer0 / 1
constexpr Word kBufferStart = 0x0056e5e0;
constexpr Word kFrameTimes = 0x0056868c;                  // three frame durations
constexpr Word kFont = 0x005619e0;                        // the copy of the system font

constexpr Word kMsgNoBruce = 0x0053427c;
constexpr Word kMsgNoFilename = 0x005269a0;
constexpr Word kMsgNoCustomSkater = 0x00527d3c;
constexpr Word kMsgEmpty = 0x0055fe10;

// the test the level-start code applies to a skater: standing with no trick in progress (state 0), or in state 7 (the second word is not read then)
inline bool waits_for_start(RetailMemory& memory, Word player) {
    const RuntimeGameplaySkaterView view = skater_at(memory, player);
    const Word state = view.physics_state();
    return state == 7 || (state == 0 && view.field_2dd4() == 0);
}

// ScreenWidth scaling: (x * width) >> 9 (logical); ScreenHeight scaling: the unsigned magic-number division of (y * height) by 240
inline Word scale_x(Word screen_width, Word value) {
    const auto product = static_cast<std::int32_t>(static_cast<std::int16_t>(value)) * static_cast<std::int32_t>(screen_width);
    return static_cast<Word>(product) >> 9;
}
inline Word scale_y(Word screen_height, Word value) {
    const auto product = static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int16_t>(value)) * static_cast<std::int32_t>(screen_height));
    return static_cast<Word>((static_cast<std::uint64_t>(product) * 0x88888889ull) >> 32) >> 7;
}

}  // namespace

// ---- the small ones ----------------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x00466b70: an allocation of `size` bytes (rounded up to a word) cleared to zero and counted in the class usage
Word CoreKit::class_new(Word size) {
    const Word block = alloc(size, 0, 1, 0);
    const Word rounded = (size + 3) & 0xfffffffcu;
    for (Word k = 0; k < rounded / 4; ++k) {
        set_word_at(m_, block + 4 * k, 0);
    }
    set_global_word(m_, 0x0056a920, global_word(m_, 0x0056a920) + rounded + 4);
    return block;
}

// 0x00466bb0: the usage counter follows what the heap says it released
void CoreKit::class_delete(Word block) {
    const Word before = global_word(m_, 0x0056a970);
    mem_delete(block);
    if (!s_.live()) {
        set_global_word(m_, 0x0056a970, static_cast<Word>(s_.value("mem_delete")));     // (the heap's use counter after the free: scripted)
    }                                                                         // (live, retail's free already updated it, and its EAX is not the counter)
    set_global_word(m_, 0x0056a920, global_word(m_, 0x0056a920) + (global_word(m_, 0x0056a970) - before));
}

// 0x00466be0
void CoreKit::calc_poly_buffer_end() {
    set_global_word(m_, kArenaEnd, render_state(m_).arena_base() + global_word(m_, 0x00561150));
}

// 0x00466de0: the dimming quad of a paused frame, two primitives at the arena cursor, linked into the ordering table
void CoreKit::draw_dim_overlay() {
    if (global_word(m_, kD3dSoftware) != 0) {
        return;
    }
    if (static_cast<std::int32_t>(global_word(m_, kFadeLevel)) < 0x28) {
        // F1.5E: a step per drawn frame; at a one-tick scene half of it, at half a tick (F2A) a quarter (the fade is 10 frames of 30 Hz; the frame clock is stopped while paused, so the
        // scene period gives the length)
        set_global_word(m_, kFadeLevel, global_word(m_, kFadeLevel) + (framepacing::pacing().count_30hz ? static_cast<Word>(2.0 * framepacing::pacing().period_ticks + 0.5) : 4u));
    }
    const Word cursor = global_word(m_, kArenaCursor);
    const Word second = cursor + 0x1c;
    if (second + 0x10 > global_word(m_, kArenaEnd)) {
        return;
    }
    set_global_word(m_, kArenaCursor, second + 0x10);
    const Word width = global_word(m_, kScreenWidth);
    const Word height = global_word(m_, kScreenHeight);
    const RuntimeScreenPrimitiveView quad = primitive_at(m_, cursor);
    quad.set_code(0x2a);
    quad.set_red(global_byte(m_, kFadeLevel));
    quad.set_green(global_byte(m_, kFadeLevel));
    quad.set_blue(global_byte(m_, kFadeLevel));
    quad.set_x0(0);
    quad.set_y0(0);
    quad.set_x1(global_half(m_, 0x00527d50));
    quad.set_y1(0);
    quad.set_x2(0);
    quad.set_x3(global_half(m_, 0x00527d50));
    quad.set_y2(global_half(m_, 0x00527d54));
    quad.set_y3(global_half(m_, 0x00527d54));
    quad.set_x0(0);
    quad.set_y0(0);
    quad.set_x1(static_cast<std::uint16_t>(scale_x(width, quad.x1())));
    quad.set_y1(0);
    quad.set_x2(0);
    quad.set_y2(static_cast<std::uint16_t>(scale_y(height, quad.y2())));
    quad.set_x3(static_cast<std::uint16_t>(scale_x(width, quad.x3())));
    quad.set_y3(static_cast<std::uint16_t>(scale_y(height, quad.y3())));
    quad.set_depth(-1.0f);
    // (word 2 of the ordering table is the head of the bucket the dimming quads join; the second primitive is a 0x10-byte command: link, the texture-page word, the depth, zero)
    const Word table = render_state(m_).ordering_table();
    quad.set_link(word_at(m_, table + 8));
    set_word_at(m_, table + 8, cursor);
    set_word_at(m_, second, 0x01000000);
    set_word_at(m_, second + 4, 0xe1000640u);
    set_word_at(m_, second + 0xc, 0);
    set_word_at(m_, second + 8, 0xbf800000u);
    const Word table2 = render_state(m_).ordering_table();
    set_word_at(m_, second, word_at(m_, table2 + 8));
    set_word_at(m_, table2 + 8, second);
}

// 0x00468350: one textured quad (the colour / code word `colour`) at the arena cursor, its eight coordinates scaled to the screen, linked into the ordering table
void CoreKit::add_quad(Word x0, Word y0, Word x1, Word y1, Word x2, Word y2, Word x3, Word y3, Word colour) {
    const Word cursor = global_word(m_, kArenaCursor);
    const Word next = cursor + 0x1c;
    // (the vertices are stored in the order 0, 1, 3, 2 of the arguments: the third argument pair goes to the fourth vertex)
    const RuntimeScreenPrimitiveView quad = primitive_at(m_, cursor);
    quad.set_y1(static_cast<std::uint16_t>(y1));
    quad.set_x2(static_cast<std::uint16_t>(x3));
    quad.set_x0(static_cast<std::uint16_t>(x0));
    quad.set_colour_word(colour);
    quad.set_y0(static_cast<std::uint16_t>(y0));
    quad.set_x1(static_cast<std::uint16_t>(x1 + 1));
    quad.set_x3(static_cast<std::uint16_t>(x2 + 1));
    quad.set_y3(static_cast<std::uint16_t>(y2 + 1));
    quad.set_y2(static_cast<std::uint16_t>(y3 + 1));
    quad.set_code(0x28);
    const Word width = global_word(m_, kScreenWidth);
    const Word height = global_word(m_, kScreenHeight);
    quad.set_x0(static_cast<std::uint16_t>(scale_x(width, x0)));
    quad.set_y0(static_cast<std::uint16_t>(scale_y(height, y0)));
    quad.set_x1(static_cast<std::uint16_t>(scale_x(width, x1 + 1)));
    quad.set_y1(static_cast<std::uint16_t>(scale_y(height, y1)));
    quad.set_x2(static_cast<std::uint16_t>(scale_x(width, x3)));
    quad.set_y2(static_cast<std::uint16_t>(scale_y(height, y3 + 1)));
    quad.set_x3(static_cast<std::uint16_t>(scale_x(width, x2 + 1)));
    quad.set_y3(static_cast<std::uint16_t>(scale_y(height, y2 + 1)));
    const RuntimeRenderStateView state = render_state(m_);
    if (state.viewport_table() == state.ordering_table()) {
        quad.set_depth(-1.0f);
    } else {
        // (fild of the viewport's depth word, stored as a float)
        quad.set_depth(static_cast<float>(half_at(m_, global_word(m_, 0x00563a38) + 8)));
    }
    const Word head = render_state(m_).viewport_table();
    quad.set_link(word_at(m_, head));
    set_word_at(m_, head, cursor);
    set_global_word(m_, kArenaCursor, next);
}

// 0x004684f0: the rectangle (x, y, width, height) as a quad
void CoreKit::add_rect(Word x, Word y, Word width, Word height) {
    add_quad(x, y, width + x, y, width + x, height + y, x, height + y, 0x40000000);
}

// 0x00467610: mirror every primitive of the ordering table left to right (the screen-flip cheat): each coordinate x becomes the screen width minus x
void CoreKit::flip_primitives() {
    const Word size = engine_value(0x0042fc60);
    Word primitive = render_state(m_).ordering_table() + size * 8 - 8;
    const Word width = global_word(m_, kScreenWidth);
    const auto precision = math_.precision();
    auto flip16 = [&](Word at) { set_half_at(m_, at, static_cast<Word>(static_cast<std::uint16_t>(width) - half_at(m_, at))); };
    while (primitive != 0) {
        const RuntimeD3DPolygonCommandHeaderView command(m_, primitive);
        if (command.enabled_or_payload_word() != 0) {
            switch (command.opcode_and_format()) {
            case 0x20:
                flip16(primitive + 8);
                flip16(primitive + 0xc);
                flip16(primitive + 0x10);
                break;
            case 0x24:
            case 0x30:
                flip16(primitive + 8);
                flip16(primitive + 0x10);
                flip16(primitive + 0x18);
                break;
            case 0x28:
                flip16(primitive + 8);
                flip16(primitive + 0xc);
                flip16(primitive + 0x10);
                flip16(primitive + 0x14);
                break;
            case 0x2c:
            case 0x38:
                flip16(primitive + 8);
                flip16(primitive + 0x10);
                flip16(primitive + 0x18);
                flip16(primitive + 0x20);
                break;
            case 0x34:
                flip16(primitive + 8);
                flip16(primitive + 0x14);
                flip16(primitive + 0x20);
                break;
            case 0x3c:
                flip16(primitive + 8);
                flip16(primitive + 0x14);
                flip16(primitive + 0x20);
                flip16(primitive + 0x2c);
                break;
            case 0x40:
                flip16(primitive + 8);
                flip16(primitive + 0xc);
                break;
            case 0x50:
                flip16(primitive + 8);
                flip16(primitive + 0x10);
                break;
            case 0x68:
                flip16(primitive + 8);
                break;
            case 0xb0: {
                const std::int32_t count = int_at(m_, primitive + 0x14);
                for (std::int32_t k = 0; k < count; ++k) {
                    // (the vertices are 0x14 bytes apart when the flag word at +0x10 is zero, 0x1c bytes apart otherwise)
                    const Word at = primitive + 0x18 + static_cast<Word>(k) * (word_at(m_, primitive + 0x10) == 0 ? 0x14 : 0x1c);
                    float value;
                    const Word bits = word_at(m_, at);
                    std::memcpy(&value, &bits, sizeof value);
                    const long double difference = mathkit::x87::round_to(precision, static_cast<long double>(width) - static_cast<long double>(value));
                    const auto result = static_cast<float>(difference);
                    Word out;
                    std::memcpy(&out, &result, sizeof out);
                    set_word_at(m_, at, out);
                }
                break;
            }
            default:
                break;
            }
        }
        primitive = command.next().address;
    }
}

// ---- the frame clocks -------------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x00468b00
void CoreKit::fps_init() {
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kFrameTimes + 4 * k, 2);
    }
    set_global_word(m_, 0x00568604, global_word(m_, kSimulationTime) - 2);
    if (framepacing::pacing().sub_tick) {   // F2A: the host-side frame lengths start again with it
        framepacing::sub_tick_restart(read_scalar<double>(m_, 0x006a0598));
    }
}

// 0x00468b30: the frame durations (three of them), the animation scale and the animation clock
void CoreKit::animation_clock_update() {
    const Word previous = global_word(m_, 0x00568604);
    const bool paused = global_word(m_, 0x00561c04) != 0;
    // F2A (120 fps): the frame is shorter than a tick, so its length is the change of the game accumulator (a double, in ticks), kept host-side with the last three; the scale and the
    // step come from it (64 and 0x400 on every frame at 120, where whole ticks give 42 / 85 and 0 / 0x800). Everything on whole ticks below stays retail's. Off: nothing read.
    const bool sub_tick = framepacing::pacing().sub_tick;
    const double sub_length = sub_tick ? framepacing::frame_length(read_scalar<double>(m_, 0x006a0598), paused) : 0.0;
    if (!paused) {
        const Word index = global_word(m_, 0x0056a934);
        set_word_at(m_, kFrameTimes + 4 * index, global_word(m_, kSimulationTime) - previous);
        set_global_word(m_, 0x0056a934, index + 1);
        if (index + 1 == 3) {
            set_global_word(m_, 0x0056a934, 0);
        }
    }
    set_global_word(m_, 0x0056a93c, sub_tick ? framepacing::sub_tick_step(sub_length) : (global_word(m_, kSimulationTime) - previous) << 11);
    const std::int32_t sum_ticks = static_cast<std::int32_t>(global_word(m_, 0x0056a944) + (global_word(m_, kSimulationTime) - previous));
    set_global_word(m_, 0x0056a944, static_cast<Word>(sum_ticks) & 1u);
    set_global_word(m_, 0x0056a940, static_cast<Word>(sum_ticks / 2));
    set_global_word(m_, 0x00568604, global_word(m_, kSimulationTime));
    std::int32_t total = static_cast<std::int32_t>(global_word(m_, kFrameTimes) + word_at(m_, kFrameTimes + 4) + word_at(m_, kFrameTimes + 8));
    if (total == 0) {
        total = 1;
    }
    set_global_word(m_, 0x00568808, static_cast<Word>(0xb4 / total));
    if (total > 0xf) {
        total = 0xf;
    }
    if (!paused) {
        std::int32_t scale = static_cast<std::int32_t>((static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<Word>(total) << 8)) * 0x2aaaaaab) >> 32);
        scale += static_cast<std::int32_t>(static_cast<Word>(scale) >> 31);
        if (sub_tick) {
            scale = static_cast<std::int32_t>(framepacing::sub_tick_scale());
        }
        set_global_word(m_, 0x0056865c, static_cast<Word>(scale));
        const Word player = global_word(m_, kPlayer0);
        if (skater_at(m_, player).physics_state() != 0 || skater_at(m_, player).field_2dd4() != 0) {
            if ((engine_value(0x00416980, {0xa}) & 0xff) != 0) {
                auto quarter = [](std::int32_t v) { return (v + ((v >> 31) & 3)) >> 2; };
                scale = quarter(static_cast<std::int32_t>(global_word(m_, 0x0056865c)));
                const std::int32_t other = quarter(static_cast<std::int32_t>(global_word(m_, 0x0056a93c)));
                set_global_word(m_, 0x0056865c, static_cast<Word>(scale));
                set_global_word(m_, 0x0056a93c, static_cast<Word>(other));
            } else {
                scale = static_cast<std::int32_t>(global_word(m_, 0x0056865c));
            }
        }
        if (global_word(m_, 0x0056a948) != 0) {
            scale = static_cast<std::int32_t>((static_cast<std::int64_t>(scale * 125) * 0x51eb851f) >> 32) >> 5;
            scale += static_cast<std::int32_t>(static_cast<Word>(scale) >> 31);
            set_global_word(m_, 0x0056865c, static_cast<Word>(scale));
        }
        set_global_word(m_, 0x00568804, static_cast<Word>((scale * scale) >> 8));
        Word accumulator = global_word(m_, 0x00568810);
        if (global_byte(m_, 0x006a3d26) == 0) {
            accumulator += static_cast<Word>(scale) * 2;
            set_global_word(m_, 0x00568810, accumulator);
        }
        if (global_word(m_, globals::kGameMode) == 9) {
            const Word record = global_word(m_, 0x0056db60);
            const std::int32_t difference = (static_cast<std::int32_t>(accumulator) >> 8) - static_cast<std::int32_t>(global_word(m_, 0x005685f4));
            skater_at(m_, record).set_field_2cc8(skater_at(m_, record).field_2cc8() + static_cast<Word>(difference));
            accumulator = global_word(m_, 0x00568810);
        }
        set_global_word(m_, 0x005685f4, static_cast<Word>(static_cast<std::int32_t>(accumulator) >> 8));
    }
    if (global_word(m_, 0x0056a868) != 0) {
        return;          // (a jump to an empty function)
    }
}

// ---- the input and the frame ends ----------------------------------------------------------------------------------------------------------------------------------------------

Word CoreKit::game_input_update() {
    engine(0x00489cd0);
    engine(0x00489a10);
    engine(0x00489e70);
    if (global_word(m_, 0x00568a58) != 0) {
        engine(0x004895c0, {0});
    }
    if (global_word(m_, 0x00568a5c) != 0) {
        engine(0x004895c0, {1});
    }
    return 0;
}

// 0x00469d30: while the level waits for the start, the pad of a skater that is not skating is cleared (the low byte of its first pad word is kept)
void CoreKit::check_skaters_stop() {
    if (global_word(m_, kLevelEventLatch) != 1 || global_word(m_, 0x00561c04) != 0) {
        return;
    }
    auto standing = [&](Word player) {
        const Word state = skater_at(m_, player).physics_state();
        return (state == 0 || state == 7) && skater_at(m_, player).field_2dd4() == 0;
    };
    if (standing(global_word(m_, kPlayer0))) {
        const std::uint8_t keep = global_byte(m_, 0x0056b018);
        engine(0x004895c0, {0});
        set_global_byte(m_, 0x0056b018, keep);
        set_global_byte(m_, 0x0056b0a8, 1);
    }
    if (global_word(m_, globals::kNumberOfPlayers) == 2 && global_word(m_, kPlayer1) != 0 && standing(global_word(m_, kPlayer1))) {
        const std::uint8_t keep = global_byte(m_, 0x0056b184);
        engine(0x004895c0, {1});
        set_global_byte(m_, 0x0056b184, keep);
        set_global_byte(m_, 0x0056b214, 1);
    }
}

// 0x00469a30: the level-start countdown (the level-event latch is 1 until it runs out). Once both skaters wait for the start, a skater in state 7 with no animation (or the end of the 0x5d / 0x5f pair)
// is given its start animation; the countdown at 0x00568818 steps down (a pad button shortens a competition's to one frame) and, when it runs out, the pending score of each skater moves into its
// total (unless the mode is 7), the voices are stopped, the latch is cleared and the level-event completion runs.
void CoreKit::skater_update_animation_objects() {
    if (!waits_for_start(m_, global_word(m_, kPlayer0)) || (global_word(m_, kPlayer1) != 0 && !waits_for_start(m_, global_word(m_, kPlayer1)))) {
        return;
    }
    for (Word slot = kPlayer0; slot < kPlayer0 + 8; slot += 4) {
        const Word player = word_at(m_, slot);
        if (player == 0 || skater_at(m_, player).physics_state() != 7 || global_word(m_, kLevelEventLatch) != 1) {
            continue;
        }
        const Word animation = skater_at(m_, player).field_f6();
        if (animation == 0) {
            log_line(0x005343cc);
            engine(0x00480730, {word_at(m_, slot), 0x5d, 0, 0xffffffffu, 0xffffffffu});
        } else if ((animation == 0x5d || animation == 0x5f) && skater_at(m_, player).field_107() != 0) {
            log_line(0x005343b8);
            engine(0x00480730, {word_at(m_, slot), 0x5f, 0, 0xffffffffu, 0xffffffffu});
        }
    }
    const bool competition = global_word(m_, globals::kGameMode) == 7;
    if (global_word(m_, kLevelEventLatch) != 1) {
        return;
    }
    Word count = global_word(m_, 0x00568818);
    Word countdown_step = framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); });   // F1.5E: the countdown is in 30 Hz frames
    if (competition && global_int(m_, 0x00568818) < 0x3c && (global_byte(m_, 0x0056b028) != 0 || global_byte(m_, 0x0056b194) != 0)) {
        count = 1;
        countdown_step = 1;   // a pad button ends it now
    }
    count -= countdown_step;
    set_global_word(m_, 0x00568818, count);
    if (static_cast<std::int32_t>(count) > 0) {
        return;
    }
    engine(0x004cbf30, {global_word(m_, kPlayer0)});
    engine(0x004cbf30, {global_word(m_, kPlayer1)});
    const Word first = global_word(m_, kPlayer0);
    const Word second = global_word(m_, kPlayer1);
    if (global_word(m_, globals::kGameMode) != 7 && (skater_at(m_, first).landed_total_score() != 0 || (second != 0 && skater_at(m_, second).landed_total_score() != 0))) {
        set_global_word(m_, 0x00568610, 0);
        set_global_word(m_, 0x00568818, 1);
        const Word pending = static_cast<Word>(skater_at(m_, first).landed_total_score());
        if (pending != 0 && (global_byte(m_, 0x0056b028) != 0 || global_byte(m_, 0x0056b018) != 0 || global_byte(m_, 0x0056b008) != 0 || global_byte(m_, 0x0056aff8) != 0)) {
            skater_at(m_, first).set_event_committed_score_total(static_cast<std::int32_t>(static_cast<Word>(skater_at(m_, first).event_committed_score_total()) + pending));
            skater_at(m_, first).set_landed_total_score(0);
        }
        if (second == 0 || skater_at(m_, second).landed_total_score() == 0) {
            return;
        }
        if (global_byte(m_, 0x0056b194) != 0 || global_byte(m_, 0x0056b184) != 0 || global_byte(m_, 0x0056b174) != 0 || global_byte(m_, 0x0056b164) != 0) {
            skater_at(m_, second).set_event_committed_score_total(static_cast<std::int32_t>(static_cast<Word>(skater_at(m_, second).event_committed_score_total()) + static_cast<Word>(skater_at(m_, second).landed_total_score())));
            skater_at(m_, second).set_landed_total_score(0);
        }
        return;
    }
    // the voices of each skater (a sound slot of -1 is free); the second slot of the first skater is read again after the first stop, as retail does
    auto stop_voices = [&](Word slot_address, bool first_skater) {
        Word player = word_at(m_, slot_address);
        if (first_skater && skater_at(m_, player).voice_3090() != 0xffffffffu) {
            engine(0x004ad9f0, {skater_at(m_, player).voice_308c()});
            skater_at(m_, word_at(m_, slot_address)).set_voice_3090(0xffffffffu);
        }
        player = word_at(m_, slot_address);
        if (skater_at(m_, player).voice_3090() != 0xffffffffu) {
            engine(0x004ad9f0, {skater_at(m_, player).voice_3090()});
            skater_at(m_, word_at(m_, slot_address)).set_voice_3090(0xffffffffu);
        }
        player = word_at(m_, slot_address);
        if (first_skater || player != 0) {
            const Word voice = skater_at(m_, player).voice_3094();
            if (voice != 0xffffffffu) {
                engine(0x004ad9f0, {voice});
                skater_at(m_, word_at(m_, slot_address)).set_voice_3094(0xffffffffu);
            }
        }
    };
    stop_voices(kPlayer0, true);
    if (global_word(m_, kPlayer1) != 0) {
        stop_voices(kPlayer1, false);
    }
    set_global_word(m_, kLevelEventLatch, 0);
    engine(0x0044e0f0);
}

// 0x00469de0: one frame of play, between the pad update and the network update. The level-event, crowd, video and skater animation updates come first, then the object lists run (or, while the world is
// frozen by the pause or the debug camera, only the camera list does and the objects with an animation selected get their virtual update), the level-start countdown moves each camera's word at +0x5b4,
// the speech and trick tips update, the level-event latches of the competition modes fire once, and a dying player record ends the level. The globals are read where retail reads them, after the callees
// that may change them (the pause flag after the camera list, the game mode after the latch calls).
void CoreKit::game_gameplay_update() {
    constexpr Word kCarVtable = 0x005184e0;
    constexpr Word kPlatformVtable = 0x005194f8;
    constexpr Word kPowerupVtable = 0x00519684;
    // the objects drawn between their 30 Hz states (count_30hz only): the cars of the baddy list, the platforms of the game list (the Bullring's helicopter, moving and thrown props) and the
    // power-ups (cash, the letters, a goal's items: their spin and bob, and the flight of a collected one) that M3D_RenderObjects draws from their own position and angle words (not a
    // skater-style model: +4 bit 1 clear). The last two are drawn from their words as they are, so both take the platform's way
    const auto for_each_smoothed = [&](auto&& visit) {
        for (Word car = global_word(m_, globals::kBaddyList); car != 0; car = next_object(m_, car)) {
            if (word_at(m_, car) == kCarVtable) {
                visit(car, false);
            }
        }
        for (Word object = global_word(m_, globals::kGameList); object != 0; object = next_object(m_, object)) {
            if (word_at(m_, object) == kPlatformVtable && (byte_at(m_, object + 4) & 2) == 0) {
                visit(object, true);
            }
        }
        for (Word object = global_word(m_, globals::kPowerUpList); object != 0; object = next_object(m_, object)) {
            if (word_at(m_, object) == kPowerupVtable && (byte_at(m_, object + 4) & 2) == 0) {
                visit(object, true);
            }
        }
    };
    const auto read_snap = [&](Word object, bool platform) {
        framepacing::MotionSnap snap;
        for (Word k = 0; k < 3; ++k) snap.pos[k] = word_at(m_, object + 8 + 4 * k);
        snap.pitch = half_at(m_, object + 0x14);
        snap.yaw = half_at(m_, object + 0x16);
        snap.roll = platform ? half_at(m_, object + 0x18) : 0;
        snap.platform = platform;
        return snap;
    };
    const auto write_snap = [&](Word object, const framepacing::MotionSnap& snap) {
        for (Word k = 0; k < 3; ++k) set_word_at(m_, object + 8 + 4 * k, snap.pos[k]);
        set_half_at(m_, object + 0x14, snap.pitch);
        set_half_at(m_, object + 0x16, snap.yaw);
        if (snap.platform) {
            set_half_at(m_, object + 0x18, snap.roll);
        }
    };
    const auto same_snap = [](const framepacing::MotionSnap& a, const framepacing::MotionSnap& b) {
        return a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] && a.pos[2] == b.pos[2] && a.pitch == b.pitch && a.yaw == b.yaw && a.roll == b.roll && a.platform == b.platform;
    };
    if (framepacing::pacing().count_30hz && !framepacing::motion_state().real.empty()) {   // restore: the real state, in place of the point drawn last frame (only objects still in their list, still at that point)
        auto& motion = framepacing::motion_state();
        for_each_smoothed([&](Word object, bool platform) {
            const auto found = motion.real.find(object);
            const auto drawn = motion.drawn.find(object);
            if (found == motion.real.end() || drawn == motion.drawn.end() || found->second.platform != platform || !same_snap(read_snap(object, platform), drawn->second)) {
                return;
            }
            write_snap(object, found->second);
        });
        motion.real.clear();
        motion.drawn.clear();
    }
    engine(0x00469a30);
    engine(0x0042f430);
    calc_poly_buffer_end();
    engine(0x004cdab0);
    check_skaters_stop();
    if ((global_word(m_, globals::kNumberOfPlayers) == 1 || (global_word(m_, globals::kGameMode) == 7 && global_word(m_, 0x00568814) != 0)) && global_word(m_, 0x0056a900) == 0) {
        const std::uint8_t keep = global_byte(m_, 0x0056b254);     // (ebx holds the byte across the clear of the second pad)
        engine(0x004895c0, {1});
        set_global_byte(m_, 0x0056b254, keep);
    }
    set_global_word(m_, 0x0056a8d4, global_word(m_, 0x0056a8d4) + framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }));   // F1.5D: a frame counter in 30 Hz frames
    if (global_word(m_, 0x00561c04) != 0 || global_word(m_, 0x0056a8e0) != 0) {
        if (global_word(m_, kLevelEventLatch) == 1) {
            engine(0x00480ff0, {0x0055fa38});
        }
        Word object = global_word(m_, globals::kPowerUpList);
        while (object != 0) {
            const Word next = next_object(m_, object);
            if (half_at(m_, object + 0xf6) != 0) {
                virtual_call(object, 3);
            }
            object = next;
        }
    } else {
        engine(0x004498f0);
        clear_explosion_stack();
        // F1.5F: the world objects and effects update on 30 Hz frames only (see framepacing::runs_30hz_frame); with the flag off `gate` is retail's straight line
        const bool gate = framepacing::pacing().count_30hz;
        const bool object_frame = !gate || global_word(m_, 0x0056a940) != 0;
        if (gate) {
            framepacing::gate_time().scale += global_word(m_, 0x0056865c);
            framepacing::gate_time().step += global_word(m_, 0x0056a93c);
        }
        if (gate && object_frame) {   // cars and platforms: where each stands before the update, to draw between it and where it stands after
            auto& before = framepacing::motion_state().before;
            before.clear();
            for_each_smoothed([&](Word object, bool platform) { before[object] = read_snap(object, platform); });
        }
        if (object_frame) {
            engine(0x00480fd0, {global_word(m_, globals::kBaddyList)});
            engine(0x00480fd0, {global_word(m_, globals::kGameList)});
        }
        engine(0x00480ff0, {globals::kPlayerRecord});
        if (!s_.live()) set_global_word(m_, 0x00561c04, static_cast<Word>(s_.value(pcplatform::engine_name(0x00480ff0))));         // (the camera list's update can freeze the world: scripted at its call site)
        if (global_word(m_, 0x00561c04) == 0) {
            const Word frame_scale = gate && object_frame ? global_word(m_, 0x0056865c) : 0;
            const Word frame_step = gate && object_frame ? global_word(m_, 0x0056a93c) : 0;
            if (gate && object_frame) {   // the objects see the time of every frame since they last ran
                set_global_word(m_, 0x0056865c, framepacing::gate_time().scale);
                set_global_word(m_, 0x0056a93c, framepacing::gate_time().step);
                framepacing::motion_state().ran_scale = framepacing::gate_time().scale;
                framepacing::gate_time() = {};
            }
            if (object_frame) {
                engine(0x00407270);
                engine(0x00480ff0, {globals::kPowerUpList});
                engine(0x004bdd00);
                engine(0x00481110, {globals::kGameList});
                engine(0x00481110, {0x0055f6b0});
                engine(0x00481150);
                engine(0x00480ff0, {globals::kBaddyList});
            }
            if (gate && object_frame) {
                set_global_word(m_, 0x0056865c, frame_scale);
                set_global_word(m_, 0x0056a93c, frame_step);
            }
            if (gate) {
                // cars and platforms: the renderer reads an object's position when it draws, so the position drawn is put in the object's own words and the real state is put back at the start of the
                // next frame (the restore above). A frame the gate ran draws where the object stood before the update, one that it skipped the point between that and where it stands now, by the share of
                // the step passed: smooth, a 30 Hz frame behind. A car's transform is rebuilt from it; a platform is drawn from the words themselves.
                const Word num = object_frame ? 0 : framepacing::gate_time().scale;
                const Word den = framepacing::motion_state().ran_scale;
                for_each_smoothed([&](Word object, bool platform) {
                    auto& motion = framepacing::motion_state();
                    const auto found = motion.before.find(object);
                    if (found == motion.before.end() || found->second.platform != platform || den == 0) {
                        return;
                    }
                    const framepacing::MotionSnap& was = found->second;
                    const framepacing::MotionSnap now = read_snap(object, platform);
                    const Word share = num < den ? num : den;
                    framepacing::MotionSnap drawn = now;
                    for (Word k = 0; k < 3; ++k) drawn.pos[k] = framepacing::blend_position(was.pos[k], now.pos[k], share, den);
                    drawn.pitch = framepacing::blend_angle(was.pitch, now.pitch, share, den);
                    drawn.yaw = framepacing::blend_angle(was.yaw, now.yaw, share, den);
                    if (platform) {
                        drawn.roll = framepacing::blend_angle(was.roll, now.roll, share, den);
                    }
                    motion.real[object] = now;
                    write_snap(object, drawn);
                    motion.drawn[object] = read_snap(object, platform);
                    if (!platform) {
                        engine(0x00461bf0, {object});
                    }
                });
            }
            engine(0x00403bd0);
            // the countdown of the level start: a camera whose skater stands (state 0 with no trick, or 7) takes the step word
            if (global_word(m_, globals::kGameMode) != 7 && global_word(m_, kLevelEventLatch) == 1 && global_int(m_, 0x00568818) < 0x28 && waits_for_start(m_, global_word(m_, kPlayer0)) &&
                (global_word(m_, kPlayer1) == 0 || waits_for_start(m_, global_word(m_, kPlayer1)))) {
                const Word intro_step = framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); });   // F1.5E: the step word is per 30 Hz frame
                const Word camera = skater_at(m_, global_word(m_, kPlayer0)).camera().address;
                camera_at(m_, camera).set_field_5b4(static_cast<std::int32_t>(static_cast<Word>(camera_at(m_, camera).field_5b4()) + global_word(m_, 0x00568610) * intro_step));
                if (global_word(m_, kPlayer1) != 0) {
                    const Word second = skater_at(m_, global_word(m_, kPlayer1)).camera().address;
                    camera_at(m_, second).set_field_5b4(static_cast<std::int32_t>(static_cast<Word>(camera_at(m_, second).field_5b4()) + global_word(m_, 0x00568610) * intro_step));
                }
            }
        }
    }
    engine(0x004af630);
    engine(0x0048b2b0);
    if (global_word(m_, globals::kGameMode) == 7 && global_word(m_, 0x0056a854) == 1) {
        // the three pad-history buffers (0x5b dwords each: the current, the previous and a spare) are rotated twice: retail's two sequences leave the first two as they were and the spare holding the previous
        auto copy = [&](Word to, Word from) {
            for (Word k = 0; k < 0x5b; ++k) {
                set_word_at(m_, to + 4 * k, word_at(m_, from + 4 * k));
            }
        };
        for (int pass = 0; pass < 2; ++pass) {
            copy(0x00568698, 0x0056aff8);
            copy(0x0056aff8, 0x0056b164);
            copy(0x0056b164, 0x00568698);
        }
    }
    engine(0x004c4620);
    {
        // the message layer (Mess_Update: the gap names, the letters, "NEW TRICK" and the other banners) ages each message one tick a call and its programs are written in
        // 30 Hz frames: at 60 and 120 it runs once per 30 Hz frame this frame is worth, so a message holds, pops and blinks as long in seconds (retail: one call)
        const Word mess_steps = framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); });
        for (Word k = 0; k < mess_steps; ++k) {
            engine(0x00473760);
        }
    }
    engine(0x004b39b0);
    engine(0x004494b0);
    Word mode = global_word(m_, globals::kGameMode);
    bool settled = false;
    if (mode == 7) {
        if (global_word(m_, 0x0056a8c0) == 0) {
            set_global_word(m_, 0x00568630, 1);
            set_global_word(m_, 0x0056a8c0, 1);
            engine(0x0044e7e0);
        } else if (global_word(m_, 0x00568630) == 0) {
            set_global_word(m_, 0x00568630, 1);
            engine(0x0044e540);
        } else {
            settled = true;
        }
        mode = global_word(m_, globals::kGameMode);
    }
    if (!settled && (mode == 5 || mode == 8 || mode == 9) && global_word(m_, 0x00568618) == 0 && global_word(m_, 0x00561bfc) == 0 && (engine_value(0x004cde70) & 0xff) == 0) {
        set_global_word(m_, 0x00568618, 1);
        engine(0x0044e5c0);
    }
    if (engine_value(0x004805f0, {global_word(m_, globals::kPlayerRecord)}) != 0) {
        set_global_word(m_, kGameStatus, 2);
    }
}

// 0x0046a0f0
void CoreKit::game_display() {
    animation_clock_update();
    display();
    if (global_word(m_, 0x0056a868) != 0) {
        engine(0x0048b620, {0x12, 0x12, 0x6e, global_word(m_, 0x0056a938) + 3, 0, 0, 0, 0});
    }
    if (global_byte(m_, 0x00561990) != 0) {
        engine(0x0044b320);
    }
    const Word level = global_word(m_, kCurrentLevel);
    Word argument = 0;
    if (word_at(m_, level * 0x1ac + 0x0053c290) != 0) {
        const Word mode = global_word(m_, globals::kGameMode);
        if (global_word(m_, globals::kNumberOfPlayers) == 1 || mode == 7 || mode == 10) {
            argument = global_word(m_, 0x00563a4c);
        }
    }
    engine(0x00454e70, {argument});
    const std::int32_t mode = global_int(m_, globals::kGameMode);
    if (mode < 5 || mode == 10) {
        engine(0x004abe50);
    }
    set_global_word(m_, 0x00563a4c, 0);
}

// 0x0046a1a0: the end of a frame: a guard against the polygon arena overflowing, then the render
void CoreKit::main_loop_end() {
    if (global_word(m_, kPublicTick) == global_word(m_, 0x00568624)) {
        pause();
    }
    check((global_word(m_, kArenaCursor) & 0x7fffffffu) <= ((global_word(m_, kArenaEnd) + 0x64) & 0x7fffffffu), 0x005343e8);
    engine(0x0042ffc0);
    if (global_word(m_, kPublicTick) == global_word(m_, 0x00568624) + 1) {
        pause();
    }
    set_global_word(m_, 0x00568624, global_word(m_, kPublicTick));
}

// ---- the per-level objects --------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x00468cf0: the state reset at the start of a level
void CoreKit::play_away_init() {
    const Word storage = engine_value(0x0047fd30, {0x54});
    set_global_word(m_, 0x00568614, storage == 0 ? 0 : engine_value(0x004564b0, {storage, 0, 0}));
    const Word players = global_word(m_, globals::kNumberOfPlayers);
    check(players == 1 || players == 2, 0x00534264);
    engine(0x004b8380);
    set_global_byte(m_, 0x0056acf8, 0);
    set_global_word(m_, 0x00568a58, 0);
    set_global_word(m_, 0x00568a5c, 0);
    set_global_word(m_, kLevelEventLatch, 0);
    // (the +0xc word of each of the nine screen rectangles)
    static const std::uint16_t rect_fields[kViewportRectCount] = {0xd55, 0x8e3, 0x8e3, 0xd55, 0xd55, 0x8e3, 0x8e3, 0x8e3, 0x8e3};
    for (Word index = 0; index < kViewportRectCount; ++index) {
        viewport_rect(m_, index).set_field_0c(rect_fields[index]);
    }
    set_global_word(m_, 0x00568810, 0);
    set_global_word(m_, 0x005685f4, 0);
    set_global_word(m_, kPublicTick, 0);
    set_global_word(m_, kSimulationTime, 0);
    set_global_word(m_, 0x006a0590, 0);
    set_global_word(m_, 0x006a0594, 0);
    set_global_word(m_, 0x006a0598, 0);
    set_global_word(m_, 0x006a059c, 0);
    set_global_word(m_, 0x0056e840, 0);
    set_global_word(m_, 0x00568604, global_word(m_, kSimulationTime));
    set_global_word(m_, 0x0056a8d4, 0);
    set_global_word(m_, 0x0056a8d8, 0);
    engine(0x00489610);
    set_global_word(m_, kGameStatus, 0);
    initial_rand(0x12a6cc58);
    engine(0x004629f0);
    engine(0x00492a90);
    engine(0x004bd1e0);
}

// 0x004674d0: load a skater texture bitmap and hand it to the renderer
void CoreKit::load_skater_bitmap(Word key, Word name) {
    const Word material = engine_value(0x004b2030, {key});
    if (material == 0) {
        return;
    }
    const Word size = engine_value(0x00449030, {name}, string_at(name));
    const Word buffer = alloc(size, 1, 1, 0);
    engine(0x00449230, {buffer});
    if (!s_.live()) set_half_at(m_, buffer + 0x1c, static_cast<Word>(s_.value(pcplatform::engine_name(0x00449230))));
    engine(0x00449660);
    const Word bits = half_at(m_, buffer + 0x1c);
    Word flag = 0;
    if ((byte_at(m_, word_at(m_, material + 0x1c) + 3) & 2) != 0) {
        check(bits == 8, 0x0053424c);
    } else {
        check(bits == 4, 0x00534238);
    }
    if ((engine_value(0x00416980, {0xd}) & 0xff) != 0 && global_byte(m_, 0x0056db40) != 0) {
        flag = 1;
    }
    engine(0x004d8c60, {word_at(m_, material + 0x14), buffer, material, name, 1, flag}, string_at(name));
    mem_delete(buffer);
}

std::string CoreKit::string_at(Word address) const {
    std::string text;
    for (Word at = address; byte_at(m_, at) != 0; ++at) {
        text.push_back(static_cast<char>(byte_at(m_, at)));
    }
    return text;
}

// 0x00468e90: build a skater's model and load its texture: a custom skater from the editor's record, else the model files named by the character (the animation set merged with the character's parts)
void CoreKit::setup_appearance(Word slot, Word skater, Word name) {
    check(skater != 0, kMsgNoBruce);
    check(name != 0, kMsgNoFilename);
    const Word record = engine_value(0x004b1430, {kSkaterManager, slot});
    if (record != 0) {
        engine(0x004b14a0, {kSkaterManager, slot});
        const Word index = engine_value(0x004b1980, {slot, slot});
        // retail pushes the slot ahead of the index and leaves it on the stack for this call: (character, slot)
        engine(0x004b18d0, s_.live() ? std::vector<Word>{index, slot} : std::vector<Word>{index});
        check(word_at(m_, 0x00568648 + slot * 4) != 0, kMsgNoCustomSkater);
        engine(0x00423be0, {word_at(m_, 0x00568648 + slot * 4), skater});
    } else {
        const Word character = skater_at(m_, skater).skater_resource().address;
        if ((byte_at(m_, 0x0053a514 + character * 0x170) & 2) != 0) {
            check(word_at(m_, 0x00568648 + slot * 4) != 0, kMsgNoCustomSkater);
            engine(0x00423be0, {word_at(m_, 0x00568648 + slot * 4), skater});
        } else {
            std::string path = string_at(name);
            path[0] = static_cast<char>('0' + (slot & 0xff));
            set_string(kNameBuffer, path);
            engine(0x0046d940, {skater, s_.live() ? kNameBuffer : kStackPointer}, path);
            // the two part manifests (the animation set's, then the model's): a replay scripts each call's count and block; live each fills a 32-entry name
            // array and a count, stack locals of the retail function (here pc_platform's scratch window, clear of every Kit's area), and answers the block
            constexpr Word kLiveNamesA = 0x7ff0e800;
            constexpr Word kLiveNamesB = 0x7ff0e880;
            constexpr Word kLiveCountA = 0x7ff0e900;
            constexpr Word kLiveCountB = 0x7ff0e904;
            const Word names = 0x0052c9f8;                         // "sk2anim"
            const auto manifest = pcplatform::engine_name(0x004b47a0);
            const auto load_manifest = [&](Word file, Word names_at, Word count_at, Word& count, Word& block) {
                if (s_.live()) {
                    set_word_at(m_, count_at, 0);
                    block = engine_value(0x004b47a0, {file, names_at, 0, count_at, 0x20});
                    count = word_at(m_, count_at);
                    return;
                }
                engine(0x004b47a0, {file, kStackPointer, 0, kStackPointer, 0x20});
                count = static_cast<Word>(s_.value(manifest));
                block = static_cast<Word>(s_.value(manifest));
            };
            Word count_a = 0;
            Word block_a = 0;
            Word count_b = 0;
            Word block_b = 0;
            load_manifest(names, kLiveNamesA, kLiveCountA, count_a, block_a);
            load_manifest(name, kLiveNamesB, kLiveCountB, count_b, block_b);
            if (s_.live()) {
                engine(0x00480d90, {skater, kLiveNamesA, count_a, kLiveNamesB, count_b});
            } else {
                engine(0x00480d90, {skater, kStackPointer, count_a, kStackPointer, count_b});
            }
            mem_delete(block_a);
            mem_delete(block_b);
        }
    }
    skater_at(m_, skater).set_appearance_resource_10(GuestPtr<void>{engine_value(0x0048fbe0, {skater, 0x10})});
    skater_at(m_, skater).set_appearance_resource_11(GuestPtr<void>{engine_value(0x0048fbe0, {skater, 0x11})});
    skater_at(m_, skater).set_appearance_resource_12(GuestPtr<void>{engine_value(0x0048fbe0, {skater, 0x12})});
    const Word material = engine_value(0x004b4e70, {skater});
    check(material != 0, kMsgEmpty);
    Word set = static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int8_t>(byte_at(m_, 0x0056a688 + slot))));
    Word variant = static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int8_t>(byte_at(m_, 0x0056a2dc + slot))));
    if (global_half(m_, kReplayPlayers) != 0) {
        set = word_at(m_, 0x0056b8f4 + slot * 4);
        variant = word_at(m_, 0x0056b8ec + slot * 4);
    }
    if (engine_value(0x004b1430, {kSkaterManager, slot}) != 0) {
        set = engine_value(0x004b1980, {0});
        variant = 0;
    }
    const Word entry = engine_value(0x004547f0, {set, variant});
    // sprintf into a local, then byte 2 becomes 'g': the probe scripts the 8 bytes sprintf leaves
    // (live the buffer is the name buffer itself, where sprintf writes; a replay records the stack pointer and takes the bytes from the script)
    engine(seam::crt::CRT_sprintf, {s_.live() ? kNameBuffer : kStackPointer, 0x005256dc, word_at(m_, entry)});
    take_filled(kNameBuffer, 8, pcplatform::engine_name(seam::crt::CRT_sprintf));
    set_byte_at(m_, kNameBuffer + 2, 0x67);
    load_skater_bitmap(material, kNameBuffer);
    if (engine_value(0x004b1430, {kSkaterManager, slot}) != 0) {
        engine(0x004b14a0, {kSkaterManager, slot});
    }
}

// 0x004691e0: create the player objects (one, or two in a split-screen versus game) with their cameras and appearance
void CoreKit::create_skater_objects() {
    engine(0x004c4570);
    set_global_word(m_, kPlayer0, 0);
    set_global_word(m_, kPlayer1, 0);
    const Word mode = global_word(m_, globals::kGameMode);
    Word count = 2;
    if (global_word(m_, globals::kNumberOfPlayers) != 2 || mode == 7 || mode == 10) {
        count = 1;
    }
    for (Word i = 0; i < count; ++i) {
        const Word storage = engine_value(0x0047fd30, {0x3538});
        Word skater = 0;
        if (storage != 0) {
            skater = engine_value(0x0046c720, {storage});
        }
        set_word_at(m_, kPlayer0 + 4 * i, skater);
        skater_at(m_, skater).set_player_index(static_cast<std::int32_t>(i));
        skater_at(m_, skater).set_skater_resource(GuestPtr<void>{engine_value(0x004b0c60, {kSkaterManager, 0})});
        if (kPlayer0 + 4 * i > kPlayer0 || (global_word(m_, globals::kGameMode) == 7 && global_word(m_, 0x0056a854) != 0)) {
            skater_at(m_, skater).set_skater_resource(GuestPtr<void>{engine_value(0x004b0c60, {kSkaterManager, 1})});
        }
        engine(0x004ddf30, {skater});
        const Word camera_storage = engine_value(0x0047fd30, {0x674});
        Word camera = 0;
        if (camera_storage != 0) {
            camera = engine_value(0x0040b650, {camera_storage, skater, 0});
        }
        camera_at(m_, camera).set_camera_mode(1);
        skater_at(m_, skater).set_camera(GuestPtr<CCamera>{camera});
        const Word index = global_word(m_, globals::kGameMode) == 7 ? global_word(m_, 0x0056a854) : i;
        const Word costume = engine_value(0x004b0cd0, {kSkaterManager, index});
        const Word model = engine_value(0x004b1700, {skater_at(m_, skater).skater_resource().address, costume});
        setup_appearance(index, skater, model);
        const Word selectable = engine_value(0x004b1630, {kSkaterManager, index}) & 0xff;
        skater_at(m_, skater).set_selectable(static_cast<std::uint8_t>(selectable));
        if (selectable != 0) {
            skater_at(m_, skater).set_field_d8(skater_at(m_, skater).field_d8() | 2);
        }
        engine(0x0048f1d0, {skater});
    }
    const Word second = global_word(m_, kPlayer1);
    if (global_word(m_, globals::kNumberOfPlayers) == 2) {
        if (second == 0) {
            return;
        }
        skater_at(m_, second).set_peer_skater(GuestPtr<RuntimeGameplaySkater>{global_word(m_, kPlayer0)});
        skater_at(m_, global_word(m_, kPlayer0)).set_peer_skater(GuestPtr<RuntimeGameplaySkater>{second});
    }
    if (global_word(m_, globals::kNumberOfPlayers) == 1) {
        if ((engine_value(0x00419420) & 0xff) != 0 || global_word(m_, kCurrentLevel) == 8) {
            if (global_half(m_, kReplayPlayers) == 0 && global_word(m_, kReplayCursor) == 0) {
                engine(0x00410bb0, {skater_at(m_, global_word(m_, kPlayer0)).camera().address});
            }
        }
    }
}

// ---- the replay ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x00469400
Word CoreKit::replay_apply_loaded_header() {
    if ((engine_value(0x00452de0) & 0xff) != 0) {
        return engine_value(0x004cdc20);
    }
    if (global_byte(m_, 0x00561990) == 0) {
        return 0;
    }
    const Word second = global_word(m_, kBufferLoaded);
    const Word first = global_word(m_, 0x0056e454);
    const Word status = engine_value(0x0047b6e0, {second, first, 0x0052be20});
    std::int8_t letter = static_cast<std::int8_t>(global_byte(m_, 0x0052be24) + 1);
    set_global_byte(m_, 0x0052be24, static_cast<Word>(static_cast<std::uint8_t>(letter)));
    if (letter > 0x4a) {
        set_global_byte(m_, 0x0052be24, 0x41);
    }
    if (status != 0) {
        check(false, 0x005342a8);
        set_global_word(m_, kGameStatus, 9);
        return 0xffffffffu;
    }
    for (Word k = 0; k < 0xa0; ++k) {
        set_word_at(m_, 0x0056b8a8 + 4 * k, word_at(m_, second + 4 * k));
    }
    set_global_word(m_, 0x0056b8f4, static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int8_t>(global_byte(m_, 0x0056b8d6)))));
    const std::int32_t players = global_short(m_, kReplayPlayers);
    check(players == 1 || players == 2, 0x00534288);
    set_global_word(m_, 0x0056e5f8, word_at(m_, second));
    for (Word at = 0x0056e62c, src = second + 4; at < 0x0056e654; at += 8, src += 8) {
        set_word_at(m_, at, word_at(m_, src));
        set_word_at(m_, at + 4, word_at(m_, src + 4));
    }
    if (global_half(m_, kReplayPlayers) == 2) {
        const Word stream = global_word(m_, 0x0056e454);
        set_global_word(m_, 0x0056e470, word_at(m_, stream));
        for (Word at = 0x0056e4a4, src = stream + 4; at < 0x0056e4cc; at += 8, src += 8) {
            set_word_at(m_, at, word_at(m_, src));
            set_word_at(m_, at + 4, word_at(m_, src + 4));
        }
    }
    set_global_word(m_, kReplayCursor, 0xffffffffu);
    set_global_half(m_, kReplayPlayers, 0u - global_half(m_, kReplayPlayers));
    return 0;
}

// 0x004695a0: the masks of the video the level records (which goals the career has, the money mask of the level)
void CoreKit::prepare_video_masks() {
    if (global_word(m_, kReplayCursor) != 0) {
        const Word start = global_word(m_, kBufferStart);
        if (start != 0) {
            set_global_word(m_, 0x0056e5d8, 2);
            set_global_byte(m_, 0x00568a24, byte_at(m_, start + 0x38));
            set_global_byte(m_, 0x0056a838, byte_at(m_, start + 0x39));
            set_global_word(m_, 0x00568608, word_at(m_, start + 0x3c));
            set_global_word(m_, 0x0056860c, word_at(m_, start + 0x40));
            log_line(0x00534318);
            return;
        }
        log_line(0x005342c8);
        return;
    }
    if (global_word(m_, globals::kGameMode) == 1) {
        set_global_byte(m_, 0x00568a24, 0);
        if (engine_value(0x00414650, {6}) != 0) {
            set_global_byte(m_, 0x00568a24, global_byte(m_, 0x00568a24) | 1);
        }
        if (engine_value(0x00414650, {1}) != 0) {
            set_global_byte(m_, 0x00568a24, global_byte(m_, 0x00568a24) | 2);
        }
        if (engine_value(0x00414650, {3}) != 0) {
            set_global_byte(m_, 0x00568a24, global_byte(m_, 0x00568a24) | 4);
        }
    } else {
        set_global_byte(m_, 0x00568a24, 7);
    }
    const Word character = engine_value(0x004b0c30, {kSkaterManager});
    const Word config = engine_value(0x00416050, {character});
    const Word level = global_word(m_, kCurrentLevel);
    set_global_byte(m_, 0x0056a838, byte_at(m_, level + config + 0x26));
    check(static_cast<std::int32_t>(level) < 0xd, 0x00525084);
    set_global_word(m_, 0x00568608, word_at(m_, config + level * 8 + 0x44));
    set_global_word(m_, 0x0056860c, word_at(m_, config + level * 8 + 0x48));
    log_line(0x00534344);
}

Word CoreKit::prepare_video_masks_wrapper() {
    prepare_video_masks();
    return 0;
}

// 0x00469700: set the player objects' replay streams up (recording from the start, or playing a highlight)
void CoreKit::configure_replay_streams() {
    const std::int32_t cursor = global_int(m_, kReplayCursor);
    if (cursor == 0) {
        check(global_word(m_, kPlayer0) != 0, 0x00534384);
        engine(0x004cbce0, {global_word(m_, kPlayer0), 1, 1, 0x7fffffff});
        if (global_word(m_, kPlayer1) != 0) {
            engine(0x004cbce0, {global_word(m_, kPlayer1), 1, 1, 0x7fffffff});
        }
        check(global_word(m_, kBufferLoaded) != 0, 0x0053436c);
        check(global_word(m_, kBufferStart) != 0, 0x0053436c);
        return;
    }
    if (cursor > 0) {
        set_global_word(m_, 0x0056e628, static_cast<Word>(cursor));
        set_global_word(m_, 0x0056e4a0, static_cast<Word>(cursor));
        engine(0x004cbce0, {global_word(m_, kPlayer0), 2, static_cast<Word>(short_at(m_, 0x0056e624 + cursor * 8)), static_cast<Word>(short_at(m_, 0x0056e626 + cursor * 8))});
        if (global_word(m_, kPlayer1) != 0) {
            const std::int32_t again = global_int(m_, kReplayCursor);
            engine(0x004cbce0, {global_word(m_, kPlayer1), 2, static_cast<Word>(short_at(m_, 0x0056e624 + again * 8)), static_cast<Word>(short_at(m_, 0x0056e626 + again * 8))});
        }
        engine(0x004cbc40);
    } else {
        set_global_word(m_, kReplayCursor, 0);
        set_global_word(m_, 0x0056e628, 0);
        set_global_word(m_, 0x0056e4a0, 0);
        engine(0x004cbce0, {global_word(m_, kPlayer0), 2, 1, 0x7fffffff});
        if (global_word(m_, kPlayer1) != 0) {
            engine(0x004cbce0, {global_word(m_, kPlayer1), 2, 1, 0x7fffffff});
        }
    }
    set_global_word(m_, kLevelEventLatch, 0);
    engine(0x004497e0);
    skater_at(m_, global_word(m_, kPlayer0)).set_field_2cc8(global_word(m_, kSimulationTime) + 0x34bde);
    if (global_word(m_, kPlayer1) != 0) {
        skater_at(m_, global_word(m_, kPlayer1)).set_field_2cc8(global_word(m_, kSimulationTime) + 0x34bde);
    }
}

// ---- the level ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x00469910
void CoreKit::game_init() {
    play_away_init();
    pause();
    fps_init();
    engine(0x0042f2f0);
    engine(0x0042f300);
    set_global_word(m_, kGameStatus, 0);
    configure_replay_streams();
    // (the three words of the vector at +0x30f4 times 20, one at a time: each store re-reads the player slot)
    const auto warmup = [&](Word index) {
        const Word player = global_word(m_, kPlayer0);
        set_component(skater_at(m_, player).velocity(), index, component(skater_at(m_, player).vector_30f4(), index) * 20);
    };
    warmup(0);
    warmup(1);
    warmup(2);
    for (int k = 0; k < 10; ++k) {
        engine(0x00480ff0, {0x0055fa38});
    }
    set_vector(skater_at(m_, global_word(m_, kPlayer0)).velocity(), 0, 0, 0);
    set_global_word(m_, 0x0056e310, 0);
    if (global_word(m_, 0x0056db8c) != 0) {
        engine(0x004be350);
    }
}

// 0x0046a250: after the level (or at a level change) the frame is flushed and the movies, the unlocks and the trick data are dealt with
void CoreKit::level_teardown() {
    if (global_word(m_, 0x0056a94c) == 0) {
        return;
    }
    pause();
    engine(0x0042fed0);
    pause();
    engine(0x004301b0);
    engine(0x0044bde0);
    const Word level = global_word(m_, kCurrentLevel);
    if (global_word(m_, globals::kGameMode) == 1) {
        engine(0x00492b20);
        if (level == 2 || level == 5 || level == 7) {
            engine(0x004b32f0, {global_word(m_, 0x00537c80), 1, 0});
        }
    }
    engine(0x00454d10, {global_word(m_, 0x0056a94c)});
    if (global_word(m_, globals::kGameMode) == 1) {
        engine(0x004302c0, {0, 0, 1, 0});
        load_head();
        const Word lvl = global_word(m_, kCurrentLevel);
        if (lvl == 7) {
            engine(0x004b37a0, {0x00534418, 1, 0, 1});
        } else if (lvl == 2) {
            engine(0x004b37a0, {0x00534410, 1, 0, 1});
        } else if (lvl == 5) {
            engine(0x004b37a0, {0x00534408, 1, 0, 1});
        }
        engine(0x004b3df0);
        engine(0x00492a90);
        engine(0x004301b0);
        unload_head();
    }
    engine(0x004497e0);
    engine(0x004302c0, {0, 0, 1, 0});
    Word status = global_word(m_, 0x00534110);
    set_global_word(m_, 0x0056a94c, 0);
    if (status != 0x30) {
        if (status != 7 || (engine_value(0x00452a80, {7}) & 0xff) == 0) {
            status = global_word(m_, 0x00534110);
            set_global_word(m_, 0x00561bfc, status);
        }
        set_global_word(m_, 0x00534110, 0x30);
    }
}

// 0x0046a3a0: one level, from the set-up to the tear-down: the frame loop runs until the exit code is set
void CoreKit::game_level_loop() {
    game_init();
    for (Word slot = kPlayer0; slot < kPlayer0 + 8; slot += 4) {
        if (word_at(m_, slot) != 0) {
            engine(0x00480730, {word_at(m_, slot), 0x5e, 0, 0xffffffffu, 0xffffffffu});
        }
    }
    unload_head();
    set_global_word(m_, 0x00568624, global_word(m_, kPublicTick));
    skater_at(m_, global_word(m_, kPlayer0)).set_field_2e30(global_word(m_, 0x005685f4) + 1);
    if (global_word(m_, kPlayer1) != 0) {
        skater_at(m_, global_word(m_, kPlayer1)).set_field_2e30(global_word(m_, 0x005685f4) + 1);
    }
    engine(seam::hal::D3D_ClearBuffers);
    engine(0x004e3f80, {0});
    engine(0x004dbd90);
    set_global_byte(m_, 0x006a1400, 0);
    engine(0x004deb00);
    const std::int32_t mode = global_int(m_, globals::kGameMode);
    if (mode < 5 || mode == 10) {
        engine(0x004abcb0, {1});
    } else {
        engine(0x004abc80);
    }
    while (global_word(m_, kGameStatus) == 0) {
        engine(seam::hal::Game_FrameTick);
        engine(0x004f5ff0);
        engine(0x004dae00);
        game_input_update();
        engine(0x00469de0);
        engine(0x004e02d0);
        if (!s_.live()) set_global_word(m_, kGameStatus, static_cast<Word>(s_.value(pcplatform::engine_name(0x004e02d0))));         // (the network update can end the level: scripted)
        if (global_word(m_, kGameStatus) != 0) {
            break;
        }
        engine(0x0042fed0);
        game_display();
        main_loop_end();
        level_teardown();
    }
    engine(0x004e7370);
    engine(0x004dbe60);
    engine(0x004deb50);
    engine(0x004e3f80, {1});
    const Word object = global_word(m_, 0x00568614);
    if (object != 0) {
        virtual_call(object, 0, {1});
    }
    engine(0x00492b20);
    engine(0x004b3df0);
    set_global_half(m_, 0x00564368, 1);
    engine(0x0042fed0);
    if (global_word(m_, 0x00561bfc) != 0x16) {
        engine(0x004302c0, {0, 0, 1, 0});
    }
    engine(0x0042f370);
}

// ---- the start-up screens --------------------------------------------------------------------------------------------------------------------------------------------------------

// the three movie-skip flags the intro tests: a movie that was skipped by the pad sets them
void CoreKit::skip_flags_clear() {
    set_global_byte(m_, 0x0056b0d8, 0);
    set_global_byte(m_, 0x0056b038, 0);
    set_global_byte(m_, 0x0056b048, 0);
}

// 0x0046a500
void CoreKit::intro_movies() {
    engine(0x00489a10);
    skip_flags_clear();
    auto skipped = [&]() { return global_byte(m_, 0x0056b0d8) != 0 || (global_byte(m_, 0x0056b038) != 0 && global_byte(m_, 0x0056b048) != 0); };
    auto play = [&](Word movie) {
        engine(0x00454d10, {movie});
        const auto name = pcplatform::engine_name(0x00454d10);
        // (the probe's call sites of GameFmv_Play here script the three skip flags the movie left)
        if (!s_.live()) {
            set_global_byte(m_, 0x0056b0d8, static_cast<Word>(s_.value(name)));
            set_global_byte(m_, 0x0056b038, static_cast<Word>(s_.value(name)));
            set_global_byte(m_, 0x0056b048, static_cast<Word>(s_.value(name)));
        }
    };
    play(0xffffffffu);
    if (!skipped()) {
        play(0xfffffffeu);
    }
    engine(0x004e5ec0, {0x00534420});
    if (!skipped()) {
        play(0xfffffffdu);
    }
}

// 0x0046a580: the logo: show the screen and wait up to two ticks for a button
void CoreKit::logo_screen() {
    engine(0x0044b3a0, {0x0053442c});
    auto poll = [&]() {
        engine(0x00489a10);
        const auto name = pcplatform::engine_name(0x00489a10);
        for (const Word flag : {0x0056b0d9u, 0x0056b029u, 0x0056b245u, 0x0056b195u}) {
            if (!s_.live()) set_byte_at(m_, flag, static_cast<Word>(s_.value(name)));
        }
    };
    poll();
    for (const Word flag : {0x0056b0d9u, 0x0056b029u, 0x0056b245u, 0x0056b195u}) {
        set_byte_at(m_, flag, 0);
    }
    for (int k = 0; k < 2; ++k) {
        pause();
        poll();
        if (global_byte(m_, 0x0056b0d9) != 0 || global_byte(m_, 0x0056b029) != 0 || global_byte(m_, 0x0056b245) != 0 || global_byte(m_, 0x0056b195) != 0) {
            break;
        }
    }
    for (const Word flag : {0x0056b0d9u, 0x0056b029u, 0x0056b245u, 0x0056b195u}) {
        set_byte_at(m_, flag, 0);
    }
}

// 0x0046a600: two frames with a line of text drawn through a font object built on the stack from a copy of the system font
void CoreKit::message_screen(Word font_copy, Word text) {
    set_global_word(m_, 0x0056114c, 0x80000000u);
    engine(0x0042fc70);
    engine(seam::hal::D3D_ClearBuffers);
    (void)font_copy;
    for (int k = 0; k < 2; ++k) {
        engine(seam::hal::Game_FrameTick);
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x004304b0);
        engine(0x0044a070, {kStackObject, 0x100, 0x69, text, 0});
        engine(0x00430510);
    }
    engine(0x0044a060, {kStackObject});
}

// 0x0046a770
Word CoreKit::main_entry() {
    engine(0x004ac0b0);          // (an empty function)
    engine(0x00413f70);
    engine(0x00458760);
    engine(0x004586f0, {0, 0x10});
    engine(0x004302c0, {0, 0, 1, 0});
    engine(0x0046f460);
    engine(0x0046a500);
    engine(0x0046a580);
    engine(0x00452ff0);
    engine(0x0044ada0, {0x005254e0});
    const Word font = engine_value(0x0044b000, {0x005254e0});
    copy_system_font(font);
    engine(0x0046a600, {kFont, global_word(m_, 0x0052eb14)});
    engine(0x0044b0a0, {0x005254e0});
    engine(0x0047a080);
    engine(0x00458840);
    return 0;
}

// the copy of the loaded font's header into the system font global (main and LaunchGame both do it)
void CoreKit::copy_system_font(Word font) {
    for (Word off = 4; off <= 0x1c; off += 4) {
        set_word_at(m_, kFont + off, word_at(m_, font + off));
    }
    set_byte_at(m_, kFont + 0x20, byte_at(m_, font + 0x20));
    set_byte_at(m_, kFont + 0x21, byte_at(m_, font + 0x21));
    for (Word off = 0x24; off <= 0x44; off += 4) {
        set_word_at(m_, kFont + off, word_at(m_, font + off));
    }
    set_byte_at(m_, kFont + 0x48, byte_at(m_, font + 0x48));
    set_byte_at(m_, kFont + 0x49, byte_at(m_, font + 0x49));
    set_byte_at(m_, kFont + 0x4a, byte_at(m_, font + 0x4a));
    for (Word k = 0; k < 0x100; ++k) {
        set_byte_at(m_, kFont + 0x4b + k, byte_at(m_, font + 0x4b + k));
    }
}

}  // namespace opentony::runtime::core
