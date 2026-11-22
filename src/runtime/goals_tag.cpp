#include "auto_views.hpp"
#include "goals.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

constexpr std::uint32_t kItSkater = 0x0056db5c;          // the skater who is "it" in the tag game
constexpr std::uint32_t kRunner = 0x0056db60;            // the one who runs away
constexpr std::uint32_t kVersusState = 0x0056db64;
constexpr std::uint32_t kPlayerOne = 0x0056a858;
constexpr std::uint32_t kPlayerTwo = 0x0056a85c;
constexpr std::uint32_t kScreenW = 0x029da394;
constexpr std::uint32_t kScreenH = 0x029da398;
constexpr std::uint32_t kDrawCursor = 0x00560fd0;        // the draw list's next free packet and its end
constexpr std::uint32_t kDrawEnd = 0x00568628;
constexpr std::uint32_t kFillHeight = 0x00527d54;
constexpr std::uint32_t kViewportMode = 0x0056a928;

// x / 4096 as retail rounds it (toward zero)
std::int32_t q12(std::int32_t v) { return (v + ((v >> 31) & 0xfff)) >> 12; }

// a ground-plane x as drawn: the scale of the screen's aspect (16 / 10) is folded into the rotation
std::int32_t wide(std::int32_t v) { return (q12(v) << 4) / 10; }

}  // namespace

// 0x004b84c0: the two banners are taken down and put up again at the skaters' new roles
void GoalScreens::tag_show_banners() {
    engine(0x004736c0, {global_word(m_, 0x0052e470)}, m_.cstring(global_word(m_, 0x0052e470), 0x100));
    engine(0x004736c0, {global_word(m_, 0x0052e474)}, m_.cstring(global_word(m_, 0x0052e474), 0x100));
    engine(0x004b83b0, {global_word(m_, kItSkater), global_word(m_, 0x0052e470)}, m_.cstring(global_word(m_, 0x0052e470), 0x100));
    engine(0x004b83b0, {global_word(m_, kRunner), global_word(m_, 0x0052e474)}, m_.cstring(global_word(m_, 0x0052e474), 0x100));
}

void GoalScreens::tag_swap_roles() {
    const std::uint32_t it = global_word(m_, kItSkater);
    const std::uint32_t runner = global_word(m_, kRunner);
    set_global_word(m_, kRunner, it);
    set_global_word(m_, kItSkater, runner);
    set_word_at(m_, runner + 0x16c, 0);
    set_word_at(m_, global_word(m_, kRunner) + 0x16c, 0);
    engine(0x0046ce60, {global_word(m_, kItSkater)});
    engine(0x0046ce60, {global_word(m_, kRunner)});
    tag_show_banners();
    engine(0x004de7c0, {global_word(m_, kItSkater), global_word(m_, kRunner), 1});
}

void GoalScreens::tag_check_caught() {
    const std::uint32_t it = global_word(m_, kItSkater);
    if (word_at(m_, it + 0x2f64) != 0) {
        return;
    }
    // Vector3_Subtract(out, Player + 8, the other + 8): the three words come back as scripted values
    scratch_line("");
    engine(0x004cabf0, {kScratchLine, global_word(m_, kPlayerOne) + 8, global_word(m_, kPlayerTwo) + 8});
    for (std::uint32_t k = 0; k < 3; ++k) {
        set_word_at(m_, kScratchLine + 4 * k, u(s_.value(engine_name(0x004cabf0))));
    }
    std::int32_t score = int_at(m_, global_word(m_, kItSkater) + 0x16c);
    if (score > 0x7d0) {
        score = 0x7d0;
    }
    std::string vector;
    for (std::uint32_t k = 0; k < 12; ++k) {
        vector.push_back(static_cast<char>(byte_at(m_, kScratchLine + k)));
    }
    const std::int32_t length = s(engine_value(0x004ca8f0, {kScratchLine}, vector));       // Math_Vector3Length
    const std::int32_t threshold = score / 20 + 0x96;
    if (length < threshold) {
        engine(0x004904d0, {global_word(m_, kRunner), 0, 0});                                       // CBruce__Bail
        tag_swap_roles();
    }
}

// 0x004b8610: a triangle packet with the corners scaled from the 512 x 240 screen to the real one
void GoalScreens::tag_push_triangle(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2, std::int32_t x3, std::int32_t y3, std::int32_t key) {
    const std::uint32_t at = global_word(m_, kDrawCursor);
    if (at + 0x18 > global_word(m_, kDrawEnd)) {
        return;
    }
    set_global_word(m_, kDrawCursor, at + 0x18);
    set_word_at(m_, at + 4, u(key) + 0x20000000u);
    set_word_at(m_, at, 0x4000000);
    auto scale_x = [&](std::int32_t v) { return (u(static_cast<std::int16_t>(v)) * global_word(m_, kScreenW)) >> 9; };
    auto scale_y = [&](std::int32_t v) {
        const std::uint32_t product = u(static_cast<std::int16_t>(v)) * global_word(m_, kScreenH);
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(product) * 0x88888889ull) >> 32) >> 7;
    };
    set_half_at(m_, at + 0x08, scale_x(x1));
    set_half_at(m_, at + 0x0a, scale_y(y1));
    set_half_at(m_, at + 0x0c, scale_x(x2));
    set_half_at(m_, at + 0x0e, scale_y(y2));
    set_half_at(m_, at + 0x10, scale_x(x3));
    set_word_at(m_, at + 0x14, 0xbf800000);
    set_half_at(m_, at + 0x12, scale_y(y3));
    const std::uint32_t head = word_at(m_, global_word(m_, goals::kRendererPointer) + 0x84);
    set_word_at(m_, at, word_at(m_, head));
    set_word_at(m_, head, at);
}

void GoalScreens::tag_rotated_line(std::int32_t cx, std::int32_t cy, std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2, std::uint32_t angle, std::uint32_t colour) {
    const std::int32_t sine = s(engine_value(0x004f39b0, {angle}));
    const std::int32_t cosine = s(engine_value(0x004f3980, {angle}));
    auto mul = [](std::int32_t a, std::int32_t b) { return s(u(a) * u(b)); };
    const std::int32_t d = s(u(mul(sine, x2)) + u(mul(cosine, y2)));
    const std::int32_t c = s(u(mul(cosine, x2)) - u(mul(sine, y2)));
    const std::int32_t b = s(u(mul(sine, x1)) + u(mul(cosine, y1)));
    const std::int32_t a = s(u(mul(cosine, x1)) - u(mul(sine, y1)));
    engine(0x0048ab20, {u(wide(a) + cx), u(q12(b) + cy), u(wide(c) + cx), u(q12(d) + cy), colour});     // Panel_Line
}

void GoalScreens::tag_rotated_triangle(std::int32_t cx, std::int32_t cy, std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2, std::int32_t x3, std::int32_t y3,
                                       std::uint32_t angle, std::uint32_t colour) {
    const std::int32_t sine = s(engine_value(0x004f39b0, {angle}));
    const std::int32_t cosine = s(engine_value(0x004f3980, {angle}));
    auto mul = [](std::int32_t a, std::int32_t b) { return s(u(a) * u(b)); };
    const std::int32_t f = s(u(mul(sine, x3)) + u(mul(cosine, y3)));
    const std::int32_t e = s(u(mul(cosine, x3)) - u(mul(sine, y3)));
    const std::int32_t d = s(u(mul(sine, x2)) + u(mul(cosine, y2)));
    const std::int32_t c = s(u(mul(cosine, x2)) - u(mul(sine, y2)));
    const std::int32_t b = s(u(mul(sine, x1)) + u(mul(cosine, y1)));
    const std::int32_t a = s(u(mul(cosine, x1)) - u(mul(sine, y1)));
    tag_push_triangle(wide(a) + cx, q12(b) + cy, wide(c) + cx, q12(d) + cy, wide(e) + cx, q12(f) + cy, s(colour));
}

// 0x004b8930: an arrow: a shaft and a head, both rotated about the same point
void GoalScreens::tag_draw_arrow(std::int32_t cx, std::int32_t cy, std::int32_t shaft, std::int32_t length, std::int32_t half_width, std::uint32_t angle, std::uint32_t colour) {
    tag_rotated_line(cx, cy, 0, 0, 0, -shaft, angle, colour);
    tag_rotated_triangle(cx, cy, half_width, length - shaft, -half_width, length - shaft, 0, -shaft, angle, colour);
}

// 0x004b8990: an arrow that shows which way the other skater lies, relative to where `skater` is heading
void GoalScreens::tag_pointer_to_skater(std::uint32_t skater, std::uint32_t other, std::int32_t cx, std::int32_t cy) {
    auto subtract = [&](std::uint32_t a, std::uint32_t b, std::int32_t out[3]) {
        scratch_line("");
        engine(0x004cabf0, {kScratchLine, a, b});
        for (int k = 0; k < 3; ++k) {
            out[k] = s(u(s_.value(engine_name(0x004cabf0))));
        }
    };
    auto normalize = [&](std::int32_t x, std::int32_t z, std::int32_t out[3]) {
        scratch_line("");
        set_global_word(m_, kScratchLine, u(x));
        set_global_word(m_, kScratchLine + 4, 0);
        set_global_word(m_, kScratchLine + 8, u(z));
        std::string vector;
        for (std::uint32_t k = 0; k < 12; ++k) {
            vector.push_back(static_cast<char>(byte_at(m_, kScratchLine + k)));
        }
        engine(0x00465f60, {kScratchLine}, vector);
        for (int k = 0; k < 3; ++k) {
            out[k] = s(u(s_.value(engine_name(0x00465f60))));
        }
    };
    std::int32_t difference[3], first[3], second[3];
    subtract(skater + 8, word_at(m_, skater + 0x29b0) + 8, difference);
    normalize(difference[0], difference[2], first);
    subtract(other + 8, skater + 8, difference);
    normalize(difference[0], difference[2], second);
    const std::int32_t angle_second = s(engine_value(0x004e8560, {u(second[2]), u(second[0])}));       // orig_ratan2(z, x)
    const std::int32_t angle_first = s(engine_value(0x004e8560, {u(first[2]), u(first[0])}));
    const std::uint32_t angle = (((0x400u - u(angle_second)) & 0xfff) - ((0x400u - u(angle_first)) & 0xfff)) & 0xfff;
    tag_draw_arrow(cx, cy, 0x14, 7, 2, angle, 0x208020);
    tag_draw_arrow(cx + 1, cy + 1, 0x14, 8, 3, angle, 0);
}

// 0x004b8a90: the pointers' places on the screen: the middle of each half in the split-screen modes
void GoalScreens::tag_draw_pointers() {
    std::int32_t y1 = 0x50, y2 = 0x50;
    std::int32_t x1 = 0x80, x2 = 0x180;
    const std::int32_t mode = global_int(m_, kViewportMode);
    if (mode == 2 || mode == 3) {
        if (mode == 2) {
            x1 = 0x100;
            x2 = 0x100;
        }
        const std::int32_t height = global_int(m_, kFillHeight);
        y1 = ((height + ((height >> 31) & 3)) >> 2) - 0x1e;
        y2 = ((height * 3 + (((height * 3) >> 31) & 3)) >> 2) - 0x1e;
    }
    const std::uint32_t player = global_word(m_, kPlayerOne);
    const std::uint32_t other = global_word(m_, kPlayerTwo);
    if (global_word(m_, kItSkater) != player) {
        tag_pointer_to_skater(other, player, x2, y2);
    } else {
        tag_pointer_to_skater(player, other, x1, y1);
    }
}

// 0x004b8b40: the tag game's per-frame state
void GoalScreens::tag_versus_update() {
    if (global_int(m_, career::kGameMode) != 9 || global_word(m_, 0x00561bfc) == 0x33) {
        return;
    }
    const std::uint32_t player = global_word(m_, kPlayerOne);
    const std::uint32_t other = global_word(m_, kPlayerTwo);
    const std::uint32_t state = global_word(m_, kVersusState);
    if (state == 0) {
        if (global_int(m_, 0x006a3a90) > 0x11 && global_int(m_, 0x006a18b0) > 0x10) {
            engine(0x004b83b0, {player, global_word(m_, 0x0052e478)}, m_.cstring(global_word(m_, 0x0052e478), 0x100));
            engine(0x004b83b0, {other, global_word(m_, 0x0052e478)}, m_.cstring(global_word(m_, 0x0052e478), 0x100));
            set_global_word(m_, kVersusState, 1);
        }
    } else if (state == 1) {
        set_word_at(m_, global_word(m_, kRunner) + 0x2cc8, word_at(m_, global_word(m_, kItSkater) + 0x2cc8));
        if ((engine_value(0x004de040) & 0xff) != 0) {
            const std::int32_t mine = int_at(m_, player + 0x2a8) + int_at(m_, player + 0x16c);
            const std::int32_t theirs = int_at(m_, other + 0x2a8) + int_at(m_, other + 0x16c);
            if (global_int(m_, 0x005685f4) > 0x258) {
                set_global_word(m_, kVersusState, 2);
                if (theirs < mine) {
                    set_global_word(m_, kItSkater, other);
                    set_global_word(m_, kRunner, player);
                } else if (mine < theirs) {
                    set_global_word(m_, kItSkater, player);
                    set_global_word(m_, kRunner, other);
                } else {
                    s_.note("rnd", {0x100});
                    if ((u(s_.value("rnd")) & 1) != 0) {
                        set_global_word(m_, kItSkater, other);
                        set_global_word(m_, kRunner, player);
                    } else {
                        set_global_word(m_, kItSkater, player);
                        set_global_word(m_, kRunner, other);
                    }
                }
                tag_show_banners();
            }
        }
    }
}

// 0x004b8ca0: one player's column of the score box: the banner (flashing between two colours), the name and the three counts
void GoalScreens::tag_player_box(std::uint32_t skater, std::int32_t x, std::int32_t y, std::uint32_t r, std::uint32_t g, std::uint32_t b, std::int32_t wins,
                                 std::int32_t losses, std::int32_t streak, std::int32_t unused, std::int32_t state) {
    (void)unused;
    std::uint32_t flash = (global_word(m_, goals::kTickCounter) << 1) % 0x3c;
    if (flash > 0x1e) {
        flash = 0x3c - flash;
    }
    mess_justify(0);
    if (state == 1 || state == 0) {
        mess_rgb((flash + r) & 0xff, g, ((b & 0xff) - flash + 0x1e) & 0xff);
        mess_scale(flash * 2 + 0x100);
        std::uint32_t text = word_at(m_, state == 1 ? 0x0052e29c : 0x0052e2a0);
        if ((engine_value(0x004de010, {skater}) & 0xff) != 0) {
            mess_scale(0x100);
            mess_rgb(r, g, b);
        }
        mess_text(x + 0x62, y + 0x1e, text);
    }
    mess_rgb(r, g, b);
    mess_scale(0x100);
    mess_justify(1);
    mess_line(x + 0x14, y + 0x37, retail_format("%s %d", {m_.cstring(global_word(m_, 0x0052eaf4), 0x100)}, {int_at(m_, skater + 0x2cc4) + 1}));
    mess_text(x + 0x14, y + 0x50, global_word(m_, 0x0052e49c));
    mess_text(x + 0x14, y + 0x64, global_word(m_, 0x0052e4a0));
    mess_text(x + 0x14, y + 0x78, global_word(m_, 0x0052e4a4));
    mess_justify(2);
    mess_line(x + 0xa4, y + 0x50, retail_format("%d", {}, {wins}));
    mess_line(x + 0xa4, y + 0x64, retail_format("%d", {}, {losses}));
    mess_line(x + 0xa4, y + 0x78, retail_format("%d", {}, {streak}));
}

void GoalScreens::tag_draw_boxes() {
    const bool first_side = global_byte(m_, 0x006a3d48) == 0;
    mess_scale(0xe0);
    tag_player_box(global_word(m_, kPlayerOne), 0x1e, 0x28, 0x80, 0x80, 0x80, global_int(m_, 0x0056b7dc), global_int(m_, 0x0056b79c), global_int(m_, 0x0056b7b0), global_int(m_, 0x0056b7a4), first_side ? 1 : 0);
    tag_player_box(global_word(m_, kPlayerTwo), 0x11e, 0x28, 0x80, 0x80, 0x80, global_int(m_, 0x0056b7e0), global_int(m_, 0x0056b798), global_int(m_, 0x0056b7b4), global_int(m_, 0x0056b7a0), first_side ? 0 : 1);
}

bool GoalScreens::tag_flag_one() { return global_byte(m_, 0x0056db69) != 0; }

bool GoalScreens::tag_flag_two() { return global_byte(m_, 0x0056db68) != 0; }

}  // namespace opentony::runtime::frontend
