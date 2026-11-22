#include "auto_views.hpp"
#include "hud_panel.hpp"
#include "frame_pacing.hpp"
#include "host_options.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace opentony::runtime::hud {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t wrap(std::uint32_t v) { return static_cast<std::int32_t>(v); }
std::uint32_t uns(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t mul32(std::int32_t a, std::int32_t b) { return wrap(uns(a) * uns(b)); }
std::uint32_t low16(std::uint32_t v) { return v & 0xffff; }

constexpr std::uint32_t kMsgNullFrame = 0x00536278;    // "NULL pFrame"
constexpr std::uint32_t kMsgNullPoly = 0x00536284;     // "NULL pPol"
constexpr std::uint32_t kMsgNoSprite = 0x00536290;     // "No sprite"
constexpr std::uint32_t kMsgFlashing = 0x005362d4;     // "Flashing whilst something on stack"
constexpr std::uint32_t kMsgPointsOnStack = 0x005362ac;// "Points on stack, and in mLandedTotal"
constexpr std::uint32_t kMsgBadPlayerNum = 0x005363ec; // "bad mPlayerNum"
constexpr std::uint32_t kMsgNoEffect = 0x0053639c;     // "No curr effect"
constexpr std::uint32_t kMsgBadEffect = 0x00536380;    // "Unrecognized text effect"
constexpr std::uint32_t kMsgNullOffsetX = 0x00536334;
constexpr std::uint32_t kMsgNullOffsetY = 0x00536318;
constexpr std::uint32_t kMsgInvalidViewport = 0x00536304;
constexpr std::uint32_t kMsgObsoleteViewport = 0x005363d4;
constexpr std::uint32_t kMsgPlayerOutOfBounds = 0x005363fc;
constexpr std::uint32_t kMsgBadTape = 0x00536440;

// One element of the HUD (the score block, the clock, the switch icon, ...) for the host's layout of it (host_options.hpp, hud_groups): the arena range its draws write while
// this is alive, with where the game put it ((x, y) in the 512 x 240 units of the layout). Nothing is recorded, and nothing is read, unless the host asked for a layout.
class ElementScope {
public:
    ElementScope(RetailMemory& memory, bool on, std::int32_t x, std::int32_t y) : m_(memory), on_(on), x_(x), y_(y), begin_(on ? global_word(memory, kArenaCursor) : 0) {}
    ElementScope(const ElementScope&) = delete;
    ElementScope& operator=(const ElementScope&) = delete;
    ~ElementScope() {
        if (!on_) {
            return;
        }
        const std::uint32_t end = global_word(m_, kArenaCursor);
        if (end > begin_) {
            HostOptions::HudGroup group;
            group.begin = begin_;
            group.end = end;
            group.fx = static_cast<float>(x_) / 512.0f;
            group.fy = static_cast<float>(y_) / 240.0f;
            host_options().hud_groups.push_back(group);
        }
    }

private:
    RetailMemory& m_;
    bool on_;
    std::int32_t x_;
    std::int32_t y_;
    std::uint32_t begin_;
};

}  // namespace

// ---- polygons ---------------------------------------------------------------------------------------------

void Panel::place_frame_quad_wide(std::int32_t x, std::int32_t y, std::uint32_t poly, std::uint32_t frame) {
    check(frame != 0, kMsgNullFrame);
    check(poly != 0, kMsgNullPoly);
    const std::int32_t width = wrap((static_cast<std::uint32_t>(byte_at(m_, frame + 2)) << 9)) / 320;
    const std::int32_t left = wrap(uns(static_cast<std::int32_t>(sbyte_at(m_, frame + 0))) << 9) / 320;
    const std::int32_t x0 = left + x;
    set_half_at(m_, poly + 8, uns(x0));
    const std::int32_t y0 = static_cast<std::int32_t>(sbyte_at(m_, frame + 1)) + y;
    set_half_at(m_, poly + 0xa, uns(y0));
    set_half_at(m_, poly + 0x12, uns(y0));
    const std::uint32_t x0w = half_at(m_, poly + 8);
    const std::uint32_t x1 = x0w + uns(width);
    set_half_at(m_, poly + 0x10, x1);
    set_half_at(m_, poly + 0x18, x0w);
    const std::uint32_t y1 = uns(y0) + byte_at(m_, frame + 3);
    set_half_at(m_, poly + 0x1a, y1);
    set_half_at(m_, poly + 0x20, x1);
    set_half_at(m_, poly + 0x22, y1);
}

void Panel::place_frame_quad(std::int32_t x, std::int32_t y, std::uint32_t poly, std::uint32_t frame) {
    check(frame != 0, kMsgNullFrame);
    check(poly != 0, kMsgNullPoly);
    const std::uint32_t x0 = uns(static_cast<std::int32_t>(sbyte_at(m_, frame + 0))) + uns(x);
    set_half_at(m_, poly + 8, x0);
    const std::uint32_t y0 = uns(static_cast<std::int32_t>(sbyte_at(m_, frame + 1))) + uns(y);
    set_half_at(m_, poly + 0xa, y0);
    set_half_at(m_, poly + 0x12, y0);
    const std::uint32_t x0w = half_at(m_, poly + 8);
    const std::uint32_t x1 = byte_at(m_, frame + 2) + x0w;
    set_half_at(m_, poly + 0x10, x1);
    set_half_at(m_, poly + 0x18, x0w);
    const std::uint32_t y1 = y0 + byte_at(m_, frame + 3);
    set_half_at(m_, poly + 0x1a, y1);
    set_half_at(m_, poly + 0x20, x1);
    set_half_at(m_, poly + 0x22, y1);
}

std::uint32_t Panel::alloc_frame_primitive(std::uint32_t frame) {
    check(frame != 0, kMsgNullFrame);
    const std::uint32_t cursor = global_word(m_, kArenaCursor);
    if (cursor + 0x30 > global_word(m_, kArenaEnd)) {
        return 0;
    }
    set_global_word(m_, kArenaCursor, cursor + 0x30);
    set_word_at(m_, cursor + 0, 0x09000000);
    set_word_at(m_, cursor + 4, 0x2c808080);
    const std::uint32_t record = word_at(m_, frame + 4);
    set_word_at(m_, cursor + 0x14, word_at(m_, record + 4));
    set_word_at(m_, cursor + 0x0c, word_at(m_, record + 0));
    set_word_at(m_, cursor + 0x1c, word_at(m_, record + 8));
    set_half_at(m_, cursor + 0x24, half_at(m_, record + 0xa));
    set_word_at(m_, cursor + 0x2c, record);
    const std::uint32_t width = global_word(m_, kScreenWidthScale);
    const std::uint32_t height = global_word(m_, kScreenHeightScale);
    for (const std::uint32_t off : {0x08u, 0x10u, 0x18u, 0x20u}) {
        set_half_at(m_, cursor + off, (uns(mul32(short_at(m_, cursor + off), wrap(width)))) >> 9);
    }
    for (const std::uint32_t off : {0x0au, 0x12u, 0x1au, 0x22u}) {
        set_half_at(m_, cursor + off, (uns(mul32(short_at(m_, cursor + off), wrap(height)))) / 240);
    }
    set_word_at(m_, cursor + 0x28, 0xbf800000);
    const std::uint32_t heads = word_at(m_, global_word(m_, kRenderState) + 0x84);
    set_word_at(m_, cursor, word_at(m_, heads));
    set_word_at(m_, heads, cursor);
    return cursor;
}

std::uint32_t Panel::draw_flat_poly(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, std::uint32_t r, std::uint32_t g,
                                    std::uint32_t b, std::int32_t bucket) {
    const std::uint32_t cursor = global_word(m_, kArenaCursor);
    if (cursor + 0x1c > global_word(m_, kArenaEnd)) {
        return 0;
    }
    set_global_word(m_, kArenaCursor, cursor + 0x1c);
    set_byte_at(m_, cursor + 4, r);
    set_byte_at(m_, cursor + 5, g);
    set_byte_at(m_, cursor + 6, b);
    set_byte_at(m_, cursor + 7, 0x28);
    const std::uint32_t x1 = uns(w) + uns(x);
    const std::uint32_t y1 = uns(h) + uns(y);
    const std::int32_t xs0 = static_cast<std::int16_t>(uns(x));
    const std::int32_t ys0 = static_cast<std::int16_t>(uns(y));
    const std::int32_t xs1 = static_cast<std::int16_t>(x1);
    const std::int32_t ys1 = static_cast<std::int16_t>(y1);
    const std::int32_t width = global_int(m_, kScreenWidthScale);
    const std::int32_t height = global_int(m_, kScreenHeightScale);
    const auto sx = [&](std::int32_t v) { return uns(mul32(v, width)) >> 9; };
    const auto sy = [&](std::int32_t v) { return uns(mul32(v, height)) / 240; };
    set_half_at(m_, cursor + 0x08, sx(xs0));
    set_half_at(m_, cursor + 0x0a, sy(ys0));
    set_half_at(m_, cursor + 0x0c, sx(xs1));
    set_half_at(m_, cursor + 0x0e, sy(ys0));
    set_half_at(m_, cursor + 0x10, sx(xs0));
    set_half_at(m_, cursor + 0x12, sy(ys1));
    set_half_at(m_, cursor + 0x14, sx(xs1));
    set_half_at(m_, cursor + 0x16, sy(ys1));
    set_word_at(m_, cursor + 0x18, 0xbf800000);
    const std::uint32_t heads = word_at(m_, global_word(m_, kRenderState) + 0x84);
    set_word_at(m_, cursor, word_at(m_, heads + uns(bucket) * 8));
    set_word_at(m_, heads + uns(bucket) * 8, cursor);
    return cursor;
}

std::uint32_t Panel::draw_gouraud_poly(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, const std::uint32_t colours[6],
                                       std::int32_t bucket) {
    const std::uint32_t cursor = global_word(m_, kArenaCursor);
    if (cursor + 0x28 > global_word(m_, kArenaEnd)) {
        return 0;
    }
    set_global_word(m_, kArenaCursor, cursor + 0x28);
    // colours: the second corner pair's rgb (p5, p6, p7) and the first corner pair's (p8, p9, p10)
    set_byte_at(m_, cursor + 0x0c, colours[0]);
    set_byte_at(m_, cursor + 0x04, colours[3]);
    set_byte_at(m_, cursor + 0x14, colours[3]);
    set_byte_at(m_, cursor + 0x1c, colours[0]);
    set_byte_at(m_, cursor + 0x0d, colours[1]);
    set_byte_at(m_, cursor + 0x1d, colours[1]);
    set_byte_at(m_, cursor + 0x05, colours[4]);
    set_byte_at(m_, cursor + 0x15, colours[4]);
    set_byte_at(m_, cursor + 0x06, colours[5]);
    set_byte_at(m_, cursor + 0x16, colours[5]);
    set_byte_at(m_, cursor + 0x0e, colours[2]);
    set_byte_at(m_, cursor + 0x1e, colours[2]);
    set_byte_at(m_, cursor + 0x07, 0x38);
    const std::uint32_t x1 = uns(w) + uns(x);
    const std::uint32_t y1 = uns(h) + uns(y);
    const std::int32_t xs0 = static_cast<std::int16_t>(uns(x));
    const std::int32_t ys0 = static_cast<std::int16_t>(uns(y));
    const std::int32_t xs1 = static_cast<std::int16_t>(x1);
    const std::int32_t ys1 = static_cast<std::int16_t>(y1);
    const std::int32_t width = global_int(m_, kScreenWidthScale);
    const std::int32_t height = global_int(m_, kScreenHeightScale);
    const auto sx = [&](std::int32_t v) { return uns(mul32(v, width)) >> 9; };
    const auto sy = [&](std::int32_t v) { return uns(mul32(v, height)) / 240; };
    set_half_at(m_, cursor + 0x08, sx(xs0));
    set_half_at(m_, cursor + 0x0a, sy(ys0));
    set_half_at(m_, cursor + 0x10, sx(xs1));
    set_half_at(m_, cursor + 0x12, sy(ys0));
    set_half_at(m_, cursor + 0x18, sx(xs0));
    set_half_at(m_, cursor + 0x1a, sy(ys1));
    set_half_at(m_, cursor + 0x20, sx(xs1));
    set_half_at(m_, cursor + 0x22, sy(ys1));
    set_word_at(m_, cursor + 0x24, 0xbf800000);
    const std::uint32_t heads = word_at(m_, global_word(m_, kRenderState) + 0x84);
    set_word_at(m_, cursor, word_at(m_, heads + uns(bucket) * 8));
    set_word_at(m_, heads + uns(bucket) * 8, cursor);
    return cursor;
}

// ---- the tapes, the colours -------------------------------------------------------------------------------

void Panel::display_tapes() {
    const std::uint32_t skater = global_word(m_, kPlayerOne);
    if (word_at(m_, skater + 0x20) != 0) {
        return;
    }
    std::int32_t timer = get(kTapeTimerA);
    if (timer != 0) {
        std::int32_t slide = get(kTapeSlideA);
        if (slide > 0x1e2 && timer > 10) {
            slide -= 4;
            put(kTapeSlideA, slide);
        }
        if (timer < 10) {
            put(kTapeSlideA, 0x20a - timer * 4);
        }
        timer -= static_cast<std::int32_t>(framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }));   // F1.5E: a count of 30 Hz frames
        put(kTapeTimerA, timer);
        const std::uint32_t time = global_word(m_, kSimulationTime);
        s_.note(kSvcMessRgb, {(time / 20 & 0xffff0000u) | ((time % 20) * 12), 100, 100, 0});
        s_.note(kSvcMessJustify, {1});
        s_.note(kSvcMessScale, {0xff});
        static constexpr std::uint32_t kLetterY[5] = {0x50, 0x69, 0x82, 0x9b, 0xb4};
        for (std::uint32_t i = 0; i < 5; ++i) {
            if (byte_at(m_, skater + 0x2994 + i) != 0) {
                const std::uint32_t text = kSkateLetters + 4 * i;
                s_.note(kSvcMessDraw, {global_word(m_, kTapeSlideA), kLetterY[i], text, 0}, m_.cstring(text));
            }
        }
    }
    std::int32_t second = get(kTapeTimerB);
    if (second == 0) {
        return;
    }
    std::int32_t slide = get(kTapeSlideB);
    if (slide > 0x1e1 && second > 10) {
        slide -= 4;
        put(kTapeSlideB, slide);
    }
    if (second <= 10) {
        put(kTapeSlideB, 0x1ff - second * 4);
    }
    put(kTapeTimerB, second - static_cast<std::int32_t>(framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); })));
}

void Panel::graffiti_color(std::uint32_t skater) {
    if (get(kGameMode) == 8) {
        const std::uint32_t font = skater + 0x418;
        if (get(skater + 0x2cc4) == 0) {
            s_.note(kSvcMessRgb, {0x50, 0x50, 0x80, 0});
            vcall(kSvcFontRgb, font, {0x50, 0x50, 0x80});
        } else {
            s_.note(kSvcMessRgb, {0x80, 0x50, 0x50, 0});
            vcall(kSvcFontRgb, font, {0x80, 0x50, 0x50});
        }
    }
}

// ---- the score and the icons ------------------------------------------------------------------------------

void Panel::display_score(std::uint32_t skater, std::uint32_t x, std::uint32_t y) {
    const bool playing = video_playing();
    const bool show = !playing;
    s_.note(kSvcCheat, {6});
    if ((s_.value(kSvcCheat) & 0xff) != 0) {
        set_word_at(m_, skater + 0x2f70, 0x2ee000);
        set_word_at(m_, skater + 0x2f6c, 0x2ee000);
    }
    if (get(kGameMode) == 7) {
        s_.note(kSvcCheat, {6});
        if ((s_.value(kSvcCheat) & 0xff) == 0) {
            set_word_at(m_, skater + 0x2f70, 0);
            set_word_at(m_, skater + 0x2f6c, 0xffffffffu);
        }
        s_.note(kSvcHorseWords, {kHorseWords});
        return;
    }
    const std::uint32_t x16 = low16(x);
    const std::uint32_t y16 = low16(y);
    const std::uint32_t time = global_word(m_, kSimulationTime);
    if (show) {
        if (get(skater + 0x2f6c) != -1 && !net_remote_player(skater)) {
            const std::int32_t special = get(skater + 0x2f70);
            if ((uns(special) & 0xfffff000u) != 0) {
                const std::uint32_t r = ((time % 40) * 6) & 0xff;
                const std::int32_t width = mul32(special >> 12, 120) / 750;
                const std::uint32_t colours[6] = {r, r, 0x32, r, r, 0x32};
                (void)draw_gouraud_poly(wrap(x16) - 1, wrap(y16) + 10, width, 3, colours, 0);
            } else if ((word_at(m_, skater + 0x2f6c) & 0xfffff000u) != 0) {
                const std::int32_t width = mul32(get(skater + 0x2f6c) >> 12, 120) / 3000;
                const std::uint32_t colours[6] = {0, 0x96, 0, 0, 0x96, 0x32};
                (void)draw_gouraud_poly(wrap(x16) - 1, wrap(y16) + 10, width, 3, colours, 0);
            }
        }
        set_global_word(m_, 0x00537b3c, 1);
        std::uint32_t c7 = 0x80, c8 = 0x80, c9 = 0x80;
        if (get(kGameMode) == 8) {
            if (get(skater + 0x2cc4) == 0) {
                c7 = 0x50;
                c8 = 0x50;
                c9 = 0x80;
            } else {
                c7 = 0x80;
                c8 = 0x50;
                c9 = 0x50;
            }
        }
        const std::uint32_t sprite = global_word(m_, kSpriteDisplay);
        set_byte_at(m_, sprite + 7, c7);
        set_byte_at(m_, sprite + 8, c8);
        set_byte_at(m_, sprite + 9, c9);
        vcall(kSvcSpriteDraw, sprite, {x16, y16 - 0x14, 0});
        if (!net_remote_player(skater)) {
            const std::uint32_t back = global_word(m_, kSpriteScoreBack);
            set_byte_at(m_, back + 7, c7);
            set_byte_at(m_, back + 8, c8);
            set_byte_at(m_, back + 9, c9);
            vcall(kSvcSpriteDraw, back, {x16 - 4, y16, 0});
        }
    }
    // the points of the last landing flow into the score a step at a time
    if (get(kGameMode) != 7) {
        const std::uint32_t landed = word_at(m_, skater + 0x2a8);
        if (landed != 0 && get(skater + 0x29ac) == 0) {
            if (get(kGameMode) == 8) {
                set_word_at(m_, skater + 0x2a8, 0);
            } else {
                std::uint32_t step;
                if (landed > 0x186a0) {
                    step = 0x9c4;
                } else if (landed > 0x2710) {
                    step = 1000;
                } else if (landed > 0x3e8) {
                    step = 100;
                } else if (landed > 0x19) {
                    step = 0x19;
                } else {
                    step = landed;
                }
                set_word_at(m_, skater + 0x16c, word_at(m_, skater + 0x16c) + step);
                set_word_at(m_, skater + 0x2a8, landed - step);
                if (word_at(m_, skater + 0x16c) > 0x70000000u) {
                    set_word_at(m_, skater + 0x16c, 0);
                }
            }
        }
    }
    if (show) {
        if (get(kGameMode) == 1 && byte_at(m_, kLevelBytes + uns(get(kCurrentLevel)) * kLevelGoalStride) == 0) {
            s_.note(kSvcCheckScore);
        }
        const std::uint32_t font = skater + 0x564;
        vcall(kSvcFontRgb, font, {0x80, 0x80, 0x80});
        std::int32_t value = get(skater + 0x16c);
        if (get(kGameMode) == 8) {
            value = get(skater + 0x2cdc);
            graffiti_color(skater);
        }
        draw_text(font, x16 + 0x7d, y16 - 4, std::to_string(value));
    }
}

void Panel::draw_switch_icon(std::uint32_t skater, std::uint32_t x, std::uint32_t y) {
    if (video_playing()) {
        return;
    }
    const std::int32_t state = get(skater + kSkState);
    if (state != 1 && state != 2) {
        const std::int32_t threshold = framepacing::clock_threshold(get(kAnimationScale), 15);
        if (get(kAnimationClock) - get(skater + 0x2d98) > threshold) {
            const bool switched = (((word_at(m_, skater + 0xd8) >> 1) & 1) ^ byte_at(m_, skater + 0x163)) != 0;
            put(skater + 0x314c, switched ? 1 : 0);
        }
    }
    const std::uint32_t x16 = low16(x);
    const std::uint32_t y16 = low16(y);
    if (get(skater + 0x31f4) == 0) {
        if (get(skater + 0x314c) == 0) {
            return;
        }
        check(global_word(m_, kSpriteSwitch) != 0, kMsgNoSprite);
        vcall(kSvcSpriteDraw, global_word(m_, kSpriteSwitch), {x16, y16, 0});
        return;
    }
    if (get(skater + 0x314c) != 0) {
        check(global_word(m_, kSpriteIcon9) != 0, kMsgNoSprite);
        vcall(kSvcSpriteDraw, global_word(m_, kSpriteIcon9), {x16, y16, 0});
        put(skater + 0x314c, 2);
        return;
    }
    check(global_word(m_, kSpriteIcon8) != 0, kMsgNoSprite);
    vcall(kSvcSpriteDraw, global_word(m_, kSpriteIcon8), {x16, y16, 0});
}

void Panel::draw_balance_icon(std::uint32_t skater, std::uint32_t x, std::uint32_t y) {
    if (get(skater + kSkState) != 1 || get(skater + 0x30c4) != 0) {
        return;
    }
    s_.note(kSvcCheat, {0xe});
    if ((s_.value(kSvcCheat) & 0xff) != 0 || get(0x0056b7ec) != 0) {
        return;
    }
    if (((get(skater + 0x2f40) + get(skater + 0x2f38)) >> 12) <= get(skater + 0x2f3c)) {
        return;
    }
    check(global_word(m_, kSpriteIcon10) != 0, kMsgNoSprite);
    const std::uint32_t state = global_word(m_, kRenderState);
    put(state + 0x84, get(state + 0x84) + 8);
    vcall(kSvcSpriteDraw, global_word(m_, kSpriteIcon10), {low16(x), low16(y), 0});
    const std::uint32_t after = global_word(m_, kRenderState);
    put(after + 0x84, get(after + 0x84) - 8);
}

// draw `text` through `effect` bound to `font`
void Panel::draw_with_effect(std::uint32_t effect, std::uint32_t font, std::uint32_t x, std::uint32_t y, const std::string& text,
                             std::uint32_t skater) {
    s_.note(kSvcClipClear, {effect});
    vcall(kSvcEffectBind, effect, {font});
    set_word_at(m_, skater + 0x448, 0x1000);
    draw_text(font, low16(x), low16(y), text);
    vcall(kSvcEffectUnbind, effect, {font});
    s_.note(kSvcClipClearFor, {half_at(m_, skater + 0x170) + uns(get(kViewportMode)) * 2});
}

void Panel::display_trick_points(std::uint32_t skater, std::uint32_t x, std::uint32_t y) {
    const bool draw = !video_playing();
    std::int32_t mult = 0;
    std::int32_t points = 0;
    if (get(kCareerFlag) == 0) {
        return;
    }
    const std::uint32_t panel = skater + kPanelInSkater;
    if (get(skater + kSkStackCountMirror) > 0) {
        check(get(skater + 0x29ac) == 0, kMsgFlashing);
        const std::int32_t count = get(skater + kSkStackCountMirror);
        std::int32_t base;
        if (count * 2 < 0xf) {
            base = get(kStackMultTable + uns(count) * 8);
        } else {
            base = wrap(uns(get(kStackMultBase)) + uns(count) * 2 - 0xe);
        }
        mult = base / 2;
        if (get(skater + kSkState) == 2 && sum_entry_values(panel) == 1) {
            mult -= 1;
        }
        points = compute_stack_sum(panel);
    }
    if (word_at(m_, skater + kSkLandedTotal) != 0) {
        check(points == 0, kMsgPointsOnStack);
        points = get(skater + kSkLandedTotal);
    }
    const bool held = get(skater + 0x2c4) != 0 || get(skater + 0x7f0) == wrap(skater + 0x6b0);
    std::int32_t shown;
    if (points == 0) {
        if (!held) {
            goto tail;
        }
        shown = get(skater + 0x2bc);
    } else {
        shown = held ? get(skater + 0x2bc) : points;
    }
    {
        const std::uint32_t font = skater + 0x418;
        const std::uint32_t effect = skater + 0x6b0;
        vcall(kSvcFontRgb, font, {0x80, 0x80, 0x80});
        graffiti_color(skater);
        put(skater + 0x41c, mult > 2 ? 2 : 1);
        if (draw) {
            draw_with_effect(effect, font, x, y, std::to_string(shown), skater);
        }
        if (get(skater + 0x29ac) > 0) {
            put(skater + 0x29ac, get(skater + 0x29ac) - 1);
        }
        if (mult > 2) {
            const std::string times = (mult & 1) == 0 ? " x" + std::to_string(mult >> 1) : " x" + std::to_string(mult >> 1) + ".5";
            vcall(kSvcFontRgb, font, {0x80, 0x80, 0x80});
            graffiti_color(skater);
            put(skater + 0x41c, 0);
            if (draw) {
                draw_with_effect(effect, font, x, y, times, skater);
            }
        }
        if (points != 0) {
            set_word_at(m_, skater + 0x2bc, (uns(mult) * uns(points)) >> 1);
        }
    }
tail:
    if (get(skater + kSkStackCountMirror) != 0 && get(skater + kSkLandQuality) > get(skater + 0x2988)) {
        show_stack_text(panel);
        put(skater + 0x2988, get(skater + kSkLandQuality));
    }
}

void Panel::display_holding(std::uint32_t skater, std::uint32_t x, std::uint32_t y) {
    const std::int32_t mode = get(kGameMode);
    if (mode == 2 || mode == 4) {
        return;
    }
    if (mode == 7 && get(skater + 0x2cc8) == 0) {
        return;
    }
    if (video_playing()) {
        return;
    }
    std::int32_t ticks = get(skater + 0x2cc8) - get(kAnimationClock);
    if (global_byte(m_, kCountdownMode) != 0) {
        ticks = get(skater + 0x2cc8);
    }
    std::int32_t minutes = ticks / 3600;
    std::int32_t seconds = (ticks - minutes * 3600) / 60;
    if (ticks < 0) {
        seconds = 0;
        minutes = 0;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%d:%02d", minutes, seconds);
    const std::string text = buffer;
    const std::uint32_t font = skater + 0x2cc;
    vcall(kSvcFontRgb, font, {0x80, 0x30, 0x30});
    set_word_at(m_, skater + 0x2fc, 0x1000);
    const std::uint32_t time = global_word(m_, kSimulationTime);
    bool skip_to_common = false;
    if (mode == 9) {
        if (get(kVersusState) != 2 || skater == global_word(m_, kVersusIt)) {
            skip_to_common = true;
        } else {
            vcall(kSvcFontRgb, font, {0x40, 0x40, 0x40});
            set_word_at(m_, skater + 0x2fc, 0xc00);
        }
    }
    if (!skip_to_common && get(kGameMode) == 7) {
        if (!(minutes == 0 && seconds <= 3 && time % 30 < 15)) {
            draw_text(font, low16(x), low16(y), text);
        }
        return;
    }
    // (host_options.hpp speed_run: the clock counts the run's time up, so its first ten seconds are not the last ones: no flash, and no beep below)
    const bool speed_run = host_options().speed_run;
    if (speed_run || !(minutes == 0 && seconds <= 10 && time % 60 < 30)) {
        draw_text(font, low16(x), low16(y), text);
    }
    if (speed_run) {
        return;
    }
    if (get(kGameMode) == 9) {
        if (skater != global_word(m_, kVersusIt)) {
            return;
        }
    } else if (get(skater + 0x2cc4) != 0) {
        return;
    }
    if (ticks > 0 && ticks < 600) {
        if (ticks >= get(kHoldingFlash)) {
            return;
        }
        if (framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }) == 0) {
            return;   // F1.5E: the flash and its beep step once per 30 Hz frame
        }
        s_.note(kSvcSfx, {0x16, 0x2000, 0});
        std::int32_t step = get(kHoldingFlash) / 10;
        if (step < 10) {
            step = 10;
        }
        put(kHoldingFlash, get(kHoldingFlash) - step);
        return;
    }
    put(kHoldingFlash, 600);
}

bool Panel::viewport_offsets(std::uint32_t index, std::uint32_t& x, std::uint32_t& y) {
    const std::int32_t half = get(kScreenHeight) / 2;
    x = 0;
    y = 0;
    switch (index) {
        case 0:
        case 1: return true;
        case 2: x = 0x80; return true;
        case 3: x = 0xff80; return true;
        case 4: y = uns(half - 0x3c) & 0xffff; return true;
        case 5: y = 0xffc4; return true;
        case 6: x = 0x80; y = uns(half - 0x3c) & 0xffff; return true;
        case 7: x = 0xff80; y = 0xffc4; return true;
        default: return false;
    }
}

void Panel::viewport_offset(std::uint32_t index, std::uint32_t x_out, std::uint32_t y_out) {
    check(x_out != 0, kMsgNullOffsetX);
    check(y_out != 0, kMsgNullOffsetY);
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    if (!viewport_offsets(index, x, y)) {
        check(false, kMsgInvalidViewport);
        return;
    }
    set_half_at(m_, x_out, x);
    set_half_at(m_, y_out, y);
}

void Panel::display_level_message(std::uint32_t skater, std::uint32_t x, std::uint32_t y) {
    if (get(0x0056a8e0) != 0 || get(kPausedHud) != 0 || get(skater + 0x2c8) == 0) {
        return;
    }
    // the offsets the viewport wants go through two 16-bit locals on the stack
    const std::uint32_t index = half_at(m_, skater + kSkPlayerNum) + uns(get(kViewportMode)) * 2;
    std::uint32_t off_x = 0;
    std::uint32_t off_y = 0;
    if (!viewport_offsets(index, off_x, off_y)) {
        check(false, kMsgInvalidViewport);
    }
    const std::int32_t offset_x = static_cast<std::int16_t>(off_x);
    const std::int32_t offset_y = static_cast<std::int16_t>(off_y);
    const std::uint32_t effect = skater + 0x7f4;
    const std::uint32_t list = word_at(m_, kLevelMessages + uns(get(kCurrentLevel)) * kLevelGoalStride);
    const std::uint32_t text = word_at(m_, list + uns(get(skater + 0x2c8)) * 4 - 4);
    vcall(kSvcEffectUpdate, effect);
    s_.note(kSvcClipClear, {effect});
    const std::uint32_t font = skater + 0x418;
    vcall(kSvcEffectBind, effect, {font});
    set_word_at(m_, skater + 0x448, (get(kNumPlayers) > 1 && get(kGameMode) != 7) ? 0xc00 : 0x1000);
    draw_text(font, uns(offset_x) + low16(x), uns(offset_y) + low16(y), text == 0 ? std::string() : m_.cstring(text));
    vcall(kSvcEffectUnbind, effect, {font});
    s_.note(kSvcClipClearFor, {half_at(m_, skater + 0x170) + uns(get(kViewportMode)) * 2});
    vcall(kSvcEffectFinished, effect);
    if (s_.value(kSvcEffectFinished) != 0) {
        put(skater + 0x2c8, 0);
    }
}

void Panel::replay_instructions() {
    if (get(kReplayState) != 2 || global_byte(m_, kReplayVideoRestart) != 0) {
        return;
    }
    const std::int32_t status = get(kGameStatus);
    if (status == 0x14 || status == 0x16 || status == 0x15 || status == 0xf) {
        return;
    }
    std::int32_t limit = get(kReplayHintLimit);
    if (limit == -1) {
        limit = framepacing::clock_threshold(get(kAnimationScale), 90);
        put(kReplayHintLimit, limit);
    }
    const std::int32_t step = get(kReplayStep);
    if (step == 0) {
        if (global_word(m_, kReplayFrames) - global_word(m_, kReplayFrame) >= uns(limit)) {
            return;
        }
    } else {
        if (step <= 0 || step > 1) {
            return;
        }
    }
    s_.note(kSvcMessRgb, {0x80, 0x80, 0x80, 0});
    s_.note(kSvcMessScale, {0xbc});
    const std::int32_t line = get(kScreenHeight) - 0x14;
    s_.note(kSvcMessJustify, {0});
    const std::uint32_t first = global_word(m_, kReplayHintLine1);
    s_.note(kSvcMessDraw, {0x100, uns(line), first, 0}, m_.cstring(first));
    const std::uint32_t second = global_word(m_, kReplayHintLine2);
    s_.note(kSvcMessDraw, {0x100, uns(line - 10), second, 0}, m_.cstring(second));
}

// 0x0048cd80 (this = panel; skater, x, y): the stack text, a line at a time from the newest, through whichever of the
// two text effects is current.
void Panel::display_trick_names(std::uint32_t panel, std::uint32_t skater, std::uint32_t x, std::uint32_t y) {
    if (video_playing() || get(kCareerFlag) == 0) {
        return;
    }
    const std::uint32_t font = panel + 0x2b0;
    const std::uint32_t falling = panel + 0x548;
    const std::uint32_t zoom = panel + 0x66c;
    vcall(kSvcFontRgb, font, {0x80, 0x80, 0x80});
    graffiti_color(skater);
    put(panel + 0x2b4, 1);
    s_.note(kSvcClipClear, {falling});
    const std::uint32_t current = word_at(m_, panel + 0x688);
    if (current == zoom) {
        vcall(kSvcEffectBind, zoom, {font});
    } else if (current == falling) {
        vcall(kSvcEffectBind, falling, {font});
    } else {
        check(current == 0, kMsgBadEffect);
        return;
    }
    const std::int32_t spacing = (get(kNumPlayers) > 1 && get(kGameMode) != 7) ? 3 : 5;
    std::int32_t line_y = wrap(y);
    std::uint32_t line = panel + 0x27bc;
    for (int i = 0; i < 2; ++i) {
        if (byte_at(m_, line) != 0) {
            const std::string text = m_.cstring(line);
            draw_text(font, low16(x), low16(uns(line_y)), text);
            s_.note(kSvcTextExtent, {font}, text);
            line_y -= s_.value(kSvcTextExtent) + spacing;
        }
        line -= 0x64;
    }
    vcall(kSvcBloodUpdate, falling);
    draw_text(font, low16(x), low16(uns(line_y)), m_.cstring(panel + 0x26f4));
    check(word_at(m_, panel + 0x688) != 0, kMsgNoEffect);
    const std::uint32_t active = word_at(m_, panel + 0x688);
    vcall(kSvcEffectUnbind, active, {font});
    if (word_at(m_, panel + 0x688) == zoom) {
        vcall(kSvcEffectFinished, zoom);
        if (s_.value(kSvcEffectFinished) != 0 && get(panel + kZoomPending) != 0) {
            vcall(kSvcEffectInit, falling);
            if (get(kNumPlayers) > 1 && get(kViewportMode) != 0) {
                vcall(kSvcSetBlood, falling, {0});
            }
            put(panel + kZoomPending, 0);
            vcall(kSvcSetScale, falling, {word_at(m_, panel + 0x2e0)});
            put(panel + 0x688, wrap(falling));
        }
    } else if (word_at(m_, panel + 0x688) == falling) {
        vcall(kSvcEffectFinished, falling);
        if (s_.value(kSvcEffectFinished) != 0) {
            put(panel + 0x688, 0);
        }
        s_.note(kSvcClipClearFor, {half_at(m_, skater + kSkPlayerNum) + uns(get(kViewportMode)) * 2});
    }
}

void Panel::obsolete_viewport_mode() { check(false, 0x005363ac); }

// 0x0048d090: every skater HUD in the layout of its viewport.
void Panel::display() {
    if (global_word(m_, kHudEnabled) == 0) {
        return;
    }
    // the host's "HUD off": everything below still runs (timers, messages, versus state); the packets it writes to the arena are remembered, and the ordering table's walk
    // (DrawKit::walk) takes them without drawing them
    struct PacketMute {
        RetailMemory& m;
        std::uint32_t begin;
        explicit PacketMute(RetailMemory& memory) : m(memory), begin(global_word(memory, kArenaCursor)) {}
        ~PacketMute() {
            const std::uint32_t end = global_word(m, kArenaCursor);
            if (host_options().hide_hud && end > begin) {
                host_options().hidden_packets.emplace_back(begin, end);
            }
        }
    } packet_mute(m_);
    std::uint32_t skater = global_word(m_, kPlayerOne);
    // the host's layout of the HUD (host_options.hpp): each element below is recorded as the arena range it writes and where the game put it. Only the layouts that use the
    // picture's whole width (one player, or one above the other) are: side by side, each player's HUD belongs to half of it, and stays in the 4:3 middle.
    const std::uint32_t layout_view = uns(get(kViewportMode));
    const bool lay = host_options().hud_layout() && (layout_view == 0 || layout_view == 2);
    const auto element = [&](std::int32_t x, std::int32_t y, const auto& draw) {
        ElementScope scope(m_, lay, x, y);
        draw();
    };
    element(0x100, 0xe8, [&] { replay_instructions(); });
    s_.note(kSvcVersusUpdate);
    if (skater == 0) {
        return;
    }
    while (true) {
        vcall(kSvcEffectUpdate, skater + 0x6b0);
        vcall(kSvcEffectUpdate, skater + 0x7d4);
        element(0x100, 0x50, [&] { display_level_message(skater, 0x100, 0x50); });
        element(0x1f0, 0x82, [&] { display_tapes(); });
        if (global_word(m_, kPausedHud) != 0) {
            if (get(kGameStatus) == 1) {
                put(kTapeTimerA, 0x1e);
                put(kTapeTimerB, 0x1e);
            }
            return;
        }
        const std::uint32_t player = half_at(m_, skater + kSkPlayerNum);
        check((player & 0xfffe) == 0, kMsgBadPlayerNum);
        const std::uint32_t panel = skater + kPanelInSkater;
        const std::int32_t mode = get(kGameMode);
        const std::int32_t h2 = get(kScreenHeight) / 2;
        switch (player + uns(get(kViewportMode)) * 2) {
            case 0:
            case 1:
                if (!net_remote_player(skater)) {
                    s_.note(kSvcTrickTips);
                    element(0x1ae, 5, [&] { draw_switch_icon(skater, 0x1ae, 5); });
                    element(200, 0x50, [&] { draw_balance_icon(skater, 200, 0x50); });
                    element(0x100, 0xe0, [&] { display_trick_points(skater, 0x100, 0xe0); });
                    element(0x100, 0xcd, [&] { display_trick_names(panel, skater, 0x100, 0xcd); });
                    element((mode == 9 ? 0xdc : 0x100), 0x30, [&] { display_holding(skater, mode == 9 ? 0xdc : 0x100, 0x30); });
                } else if (mode == 9) {
                    element(0x138, 0x30, [&] { display_holding(skater, 0x138, 0x30); });
                }
                if (net_remote_player(skater)) {
                    element(0x166, 0x2d, [&] { display_score(skater, 0x166, 0x2d); });
                } else {
                    element(0x19, 0x2d, [&] { display_score(skater, 0x19, 0x2d); });
                }
                break;
            case 2:
                draw_balance_icon(skater, 0x150, 0x50);
                display_trick_points(skater, 0x180, 0xe0);
                display_trick_names(panel, skater, 0x180, 0xcd);
                draw_switch_icon(skater, 0x1bb, 0x42);
                display_score(skater, 0x166, 0x2d);
                if (mode == 9 && get(kVersusState) == 2) {
                    display_holding(skater, 0x132, 0x30);
                }
                break;
            case 3:
                display_trick_points(skater, 0x80, 0xe0);
                draw_balance_icon(skater, 0x50, 0x50);
                display_trick_names(panel, skater, 0x80, 0xcd);
                draw_switch_icon(skater, 0x14, 0x42);
                display_score(skater, 0x19, 0x2d);
                if (mode == 9 && get(kVersusState) == 2) {
                    display_holding(skater, 0xc9, 0x30);
                } else {
                    display_holding(skater, 0x100, 0x30);
                }
                break;
            case 4:
                element(0x100, 0xcd, [&] { display_trick_names(panel, skater, 0x100, 0xcd); });
                element(0x1bb, (get(kScreenHeight) - 0x23), [&] { draw_switch_icon(skater, 0x1bb, uns(get(kScreenHeight) - 0x23)); });
                element(200, (h2 + 0x28), [&] { draw_balance_icon(skater, 200, uns(h2 + 0x28)); });
                element(0x100, 0xe0, [&] { display_trick_points(skater, 0x100, 0xe0); });
                element(0x19, (h2 + 0x19), [&] { display_score(skater, 0x19, uns(h2 + 0x19)); });
                element(0x1c4, (h2 + 0x19), [&] { display_holding(skater, 0x1c4, uns(h2 + 0x19)); });
                break;
            case 5:
                element(0x100, (0xcd - h2), [&] { display_trick_names(panel, skater, 0x100, uns(0xcd - h2)); });
                element(0x1bb, (0xd7 - h2), [&] { draw_switch_icon(skater, 0x1bb, uns(0xd7 - h2)); });
                element(0x100, (0xe0 - h2), [&] { display_trick_points(skater, 0x100, uns(0xe0 - h2)); });
                element(200, 0x28, [&] { draw_balance_icon(skater, 200, 0x28); });
                element(0x19, 0x23, [&] { display_score(skater, 0x19, 0x23); });
                element(0x1c4, 0x23, [&] { display_holding(skater, 0x1c4, 0x23); });
                break;
            case 6:
                display_trick_points(skater, 0x80, 0xea);
                draw_balance_icon(skater, 0x150, uns(h2 + 0x28));
                display_trick_names(panel, skater, 0x80, 0xd7);
                draw_switch_icon(skater, 0x37, uns(h2 + 0x1e));
                display_score(skater, 0x70, uns(h2 + 0x1e));
                display_holding(skater, 0x37, uns(h2 + 0x19));
                (void)draw_flat_poly(0x100, 0, 0x100, h2, 0x1e, 0x1e, 0x1e, 1);
                break;
            case 7:
                display_trick_points(skater, 0x180, uns(0xef - h2));
                draw_balance_icon(skater, 0x50, 0x28);
                display_trick_names(panel, skater, 0x180, uns(0xda - h2));
                draw_switch_icon(skater, 0x1c9, 0x23);
                display_score(skater, 0x114, 0x23);
                display_holding(skater, 0x1c9, 0x1e);
                (void)draw_flat_poly(0, h2, 0x101, 2, 0x1e, 0x1e, 0x1e, 1);
                (void)draw_flat_poly(0, h2, 0x101, h2, 0x14, 0x14, 0x14, 1);
                (void)draw_flat_poly(0x100, h2 - 1, 0x200, 2, 0x1e, 0x1e, 0x1e, 1);
                break;
            default:
                check(false, kMsgObsoleteViewport);
                break;
        }
        skater = word_at(m_, skater + 0x20);
        if (skater == 0) {
            return;
        }
    }
}

void Panel::set_player_number(std::uint32_t player, std::uint32_t skater) {
    check((player & 0xffff) < 2, kMsgPlayerOutOfBounds);
    set_half_at(m_, skater + kSkPlayerNum, skater == global_word(m_, kPlayerOne) ? 0 : 1);
}

void Panel::reset_trick_state(std::uint32_t skater) {
    put(skater + 0x2988, 0);
    if (get(skater + 0x2dd4) == 0 && get(skater + kSkState) != 4 && get(skater + kSkState) != 5) {
        put(skater + kSkGapRecord, 0);
        put(skater + kSkGoalGapRecord, 0);
    }
    s_.note(kSvcHighlightStart, {skater});
}

std::int32_t Panel::skate_letter_collected(std::int32_t letter) {
    put(kTapeTimerA, 0x28);
    const std::uint32_t skater = global_word(m_, kPlayerOne);
    if (letter >= 0 && letter <= 4) {
        set_byte_at(m_, skater + 0x2994 + uns(letter), 1);
    }
    for (std::uint32_t i = 0; i < 5; ++i) {
        if (byte_at(m_, skater + 0x2994 + i) != 1) {
            return 0;
        }
    }
    s_.note(kSvcGiveGoalType, {1});
    return 1;
}

void Panel::tape_collected(std::int32_t index) {
    check(index >= 0 && index <= 4, kMsgBadTape);
    put(kTapeTimerB, 0x28);
    s_.note(kSvcGiveGoal, {uns(index)});
}

void Panel::collectible_counted() {
    if (get(kGameMode) != 1) {
        return;
    }
    s_.note(kSvcGoalIndex, {4});
    const std::int32_t index = s_.value(kSvcGoalIndex);
    if (index == -1) {
        return;
    }
    const std::uint32_t goal = uns(mul32(index, 0x18)) + uns(get(kCurrentLevel)) * kLevelGoalStride;
    const std::int32_t total = get(0x0053c2b4 + goal);
    const std::uint32_t name = word_at(m_, 0x0053c2b8 + goal);
    const std::uint32_t skater = global_word(m_, kPlayerOne);
    put(skater + 0x299c, get(skater + 0x299c) + 1);
    const std::int32_t counted = get(skater + 0x299c);
    if (counted == total) {
        put(skater + 0x299c, 99);
        s_.note(kSvcGiveGoalType, {4});
        return;
    }
    const std::string text = std::to_string(counted) + " OF " + std::to_string(total) + " " + m_.cstring(name);
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(kCollectBuffer, bytes);
    s_.note(kSvcMessDeleteByText, {kCollectBuffer}, text);
    s_.note(kSvcMessAdd, {kCollectBuffer, 0x00534ec8, 1, 0, 0}, text);
}

void Panel::show_steals_message(std::uint32_t panel) {
    const std::uint32_t skater = word_at(m_, panel + kOwner);
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    const std::uint32_t index = half_at(m_, skater + kSkPlayerNum) + uns(get(kViewportMode)) * 2;
    if (!viewport_offsets(index, x, y)) {
        check(false, kMsgInvalidViewport);
    }
    const std::uint32_t text = word_at(m_, get(skater + 0x2cc4) != 0 ? kStealsRed : kStealsBlue);
    s_.note(kSvcMessAdd, {text, 0x00534f68, 0, x, y}, m_.cstring(text));
}

void Panel::message_out_of_bounds(std::uint32_t panel) {
    const std::int32_t level = get(kCurrentLevel);
    std::uint32_t list = word_at(m_, kLevelMessages + uns(level) * kLevelGoalStride);
    std::int32_t count = 0;
    while (word_at(m_, list) != 0) {
        list += 4;
        ++count;
    }
    if (get(kNumPlayers) > 1 && get(kGameMode) != 7) {
        if (level == 7 || level == 9) {
            count = 3;
        }
        if (global_byte(m_, 0x005614c4) != 0) {
            count = 4;
        }
    }
    s_.note(kSvcRnd, {uns(count)});
    put(panel + 0x160, s_.value(kSvcRnd) + 1);
    const std::uint32_t effect = panel + 0x68c;
    vcall(kSvcEffectInit, effect);
    vcall(kSvcSetBlood, effect, {0});
}

// ---- loading and releasing -----------------------------------------------------------------------------------

namespace {

// copy a loaded font object into an embedded one the way the HUD does: every field but the vtable (+0) and the padding
// words after the colour bytes, then the 0x100-byte glyph map
void copy_font_fields(RetailMemory& m, std::uint32_t dest, std::uint32_t src) {
    for (const std::uint32_t off : {0x04u, 0x08u, 0x0cu, 0x10u, 0x14u, 0x18u, 0x1cu}) {
        guest::write_scalar<std::uint32_t>(m, dest + off, static_cast<std::uint32_t>(guest::read_scalar<std::uint32_t>(m, src + off)));
    }
    guest::write_scalar<std::uint8_t>(m, dest + 0x20, static_cast<std::uint8_t>(guest::read_scalar<std::uint8_t>(m, src + 0x20)));
    guest::write_scalar<std::uint8_t>(m, dest + 0x21, static_cast<std::uint8_t>(guest::read_scalar<std::uint8_t>(m, src + 0x21)));
    for (const std::uint32_t off : {0x24u, 0x28u, 0x2cu, 0x30u, 0x34u, 0x38u, 0x3cu, 0x40u, 0x44u}) {
        guest::write_scalar<std::uint32_t>(m, dest + off, static_cast<std::uint32_t>(guest::read_scalar<std::uint32_t>(m, src + off)));
    }
    guest::write_scalar<std::uint8_t>(m, dest + 0x48, static_cast<std::uint8_t>(guest::read_scalar<std::uint8_t>(m, src + 0x48)));
    guest::write_scalar<std::uint8_t>(m, dest + 0x49, static_cast<std::uint8_t>(guest::read_scalar<std::uint8_t>(m, src + 0x49)));
    guest::write_scalar<std::uint8_t>(m, dest + 0x4a, static_cast<std::uint8_t>(guest::read_scalar<std::uint8_t>(m, src + 0x4a)));
    for (std::uint32_t i = 0; i < 0x100; ++i) {
        guest::write_scalar<std::uint8_t>(m, dest + 0x4b + i, static_cast<std::uint8_t>(guest::read_scalar<std::uint8_t>(m, src + 0x4b + i)));
    }
}

}  // namespace

// 0x0048d990 (this = panel; skater): build the panel: its fonts (time, score and tricks), the three text effects and the
// stack, ready for the skater.
void Panel::construct(std::uint32_t panel, std::uint32_t skater) {
    set_word_at(m_, panel + kOwner, skater);
    set_word_at(m_, panel + kScore, 0);
    set_word_at(m_, panel + kStackShown, 0);
    set_word_at(m_, panel + 0x160, 0);
    const std::uint32_t time_font = panel + 0x164;
    const std::uint32_t tricks_font = panel + 0x2b0;
    const std::uint32_t score_font = panel + 0x3fc;
    vcall(kSvcFontConstruct, time_font);
    vcall(kSvcFontConstruct, tricks_font);
    vcall(kSvcFontConstruct, score_font);
    vcall(kSvcFallingConstruct, panel + 0x548);
    vcall(kSvcZoomConstruct, panel + 0x66c);
    vcall(kSvcFallingConstruct, panel + 0x68c);
    put(panel + kStackCount, 0);
    put(panel + kStackClock, 0);
    put(panel + kStackTimer, 0);
    put(panel + 0x282c, 0);
    set_byte_at(m_, panel + 0x2830, 0);
    put(panel + 0x2834, 0);
    const auto find = [&](std::uint32_t name) {
        s_.note(kSvcFontFind, {name}, m_.cstring(name));
        return static_cast<std::uint32_t>(s_.value(kSvcFontFind));
    };
    // the time font: small, dark red
    std::uint32_t found = find(kNameTimeFont);
    copy_font_fields(m_, time_font, found);
    put(panel + 0x168, 1);
    put(panel + 0x16c, 3);
    vcall(kSvcFontRgb, time_font, {0x80, 0x30, 0x30});
    set_byte_at(m_, panel + 0x185, 1);
    put(panel + 0x170, 4);
    put(panel + 0x174, 0);
    put(panel + 0x194, 0x1000);
    vcall(kSvcFontMapping, time_font, {1});
    put(panel + 0x190, 1);
    // the score font
    found = find(kNameScoreFont);
    copy_font_fields(m_, score_font, found);
    put(panel + 0x400, 2);
    put(panel + 0x404, 3);
    vcall(kSvcFontRgb, score_font, {0x80, 0x80, 0x80});
    set_byte_at(m_, panel + 0x41d, 1);
    put(panel + 0x408, 4);
    put(panel + 0x40c, 0);
    put(panel + 0x42c, 0x1000);
    vcall(kSvcFontMapping, score_font, {1});
    const std::int32_t score_size = (get(kNumPlayers) == 1 || get(kGameMode) == 7) ? 2 : 1;
    put(panel + 0x420, score_size);
    put(panel + 0x424, (get(kNumPlayers) == 1 || get(kGameMode) == 7) ? 2 : 1);
    // the tricks font
    found = find(kNameTricksFont);
    copy_font_fields(m_, tricks_font, found);
    put(panel + 0x2b4, 1);
    put(panel + 0x2b8, 3);
    vcall(kSvcFontRgb, tricks_font, {0x80, 0x80, 0x80});
    put(panel + 0x2bc, 0);
    put(panel + 0x2c0, 0);
    put(panel + 0x688, 0);
    set_byte_at(m_, panel + 0x2d1, 0);
    put(panel + 0x2e0, 0x1000);
}

// 0x0048ad50 (param): load the HUD fonts, keep a copy of the tricks font for the messages and create the four sprites.
void Panel::load_runtime_sprites(std::uint32_t param) {
    if (get(0x0056a89c) != 0) {
        s_.note(kSvcPrepare, {param});
    }
    for (const std::uint32_t name : {kNameTimeFont, kNameScoreFont, kNameTricksFont}) {
        s_.note(kSvcFontLoad, {name}, m_.cstring(name));
    }
    s_.note(kSvcMessLoadFont, {0x005287a4, 2, 2, 0xffffffffu}, m_.cstring(0x005287a4));
    set_global_word(m_, 0x0056b738, static_cast<std::uint32_t>(s_.value(kSvcMessLoadFont)));
    s_.note(kSvcFontFind, {kNameTricksFont}, m_.cstring(kNameTricksFont));
    const std::uint32_t found = static_cast<std::uint32_t>(s_.value(kSvcFontFind));
    const std::uint32_t copy = 0x005619e0;
    copy_font_fields(m_, copy, found);
    set_global_word(m_, 0x00561a04, 2);
    set_global_word(m_, 0x00561a08, 2);
    set_global_word(m_, 0x005619e4, 1);
    set_global_word(m_, 0x005619e8, 0);
    set_global_byte(m_, 0x00561a01, 1);
    set_global_word(m_, 0x005619ec, 3);
    set_global_word(m_, 0x005619f0, 1);
    set_global_word(m_, 0x00561a10, 0x1000);
    struct Slot {
        std::uint32_t pointer;
        std::uint32_t name;
    };
    const Slot slots[4] = {{kSpriteSwitch, 0x00536204}, {kSpriteIcon8, 0x005361f8}, {kSpriteIcon9, 0x005361ec}, {kSpriteIcon10, 0x005361dc}};
    for (const Slot& slot : slots) {
        check(word_at(m_, slot.pointer) == 0, kMsgSpriteNotEmpty);
        s_.note(kSvcOperatorNew, {0x5c});
        const std::uint32_t memory = static_cast<std::uint32_t>(s_.value(kSvcOperatorNew));
        std::uint32_t sprite = 0;
        if (memory != 0) {
            s_.note(kSvcSpriteConstruct, {memory, slot.name, 0x40, 0x40, 0}, m_.cstring(slot.name));
            sprite = static_cast<std::uint32_t>(s_.value(kSvcSpriteConstruct));
        }
        set_word_at(m_, slot.pointer, sprite);
        s_.note(kSvcSpriteInit, {sprite});
    }
    s_.note(kSvcCreateBloodSprite);
}

// 0x0048b100: release what the loader made.
void Panel::uninit() {
    for (const std::uint32_t name : {kNameTimeFont, kNameScoreFont, kNameTricksFont}) {
        s_.note(kSvcFontUnload, {name}, m_.cstring(name));
    }
    s_.note(kSvcMessSetFont, {global_word(m_, 0x0056b738)});
    s_.note(kSvcMessUnloadFont);
    for (const std::uint32_t pointer : {kSpriteSwitch, kSpriteIcon8, kSpriteIcon9, kSpriteIcon10}) {
        const std::uint32_t sprite = word_at(m_, pointer);
        check(sprite != 0, kMsgSpriteMissing);
        if (sprite != 0) {
            vcall(kSvcSpriteDelete, sprite, {1});
        }
        set_word_at(m_, pointer, 0);
    }
    s_.note(kSvcDestroyBloodSprite);
}

}  // namespace opentony::runtime::hud
