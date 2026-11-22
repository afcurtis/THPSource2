#include "auto_views.hpp"
#include "frontend_effects.hpp"

#include <string>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kEffVtRoot = 0x005184b4;   // the root class: every destructor ends here

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

std::int32_t mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

}  // namespace

std::uint32_t TextEffects::rnd(std::uint32_t limit) {
    s_.note(kRndService, {limit});
    return static_cast<std::uint32_t>(s_.value(kRndService));
}

// ---- TextEffect (base) -----------------------------------------------------------------------------------

std::uint32_t TextEffects::effect_construct(std::uint32_t self) {
    frontend_effects_effect_at(m_, self).set_field_0(static_cast<Word>(kEffVtBase));
    set_word_at(m_, self + kEffEnabled, 0);
    set_byte_at(m_, self + kEffRunsPaused, 1);
    return self;
}

void TextEffects::effect_destroy(std::uint32_t self) { set_word_at(m_, self, kEffVtRoot); }

std::uint32_t TextEffects::effect_delete(std::uint32_t self, std::uint32_t flag) {
    effect_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kDeleteService, {self});
    }
    return self;
}

void TextEffects::effect_bind(std::uint32_t self, std::uint32_t font) {
    if (word_at(m_, self + kEffEnabled) == 0) {
        return;
    }
    check(font != 0, kMsgNoFontToBind);
    check(f_.effect(font) == 0, kMsgAlreadyBound);
    f_.set_effect(font, self);
}

void TextEffects::effect_unbind(std::uint32_t self, std::uint32_t font) {
    if (word_at(m_, self + kEffEnabled) == 0) {
        return;
    }
    check(f_.effect(font) == self, kMsgNotAllowedToUnbind);
    f_.set_effect(font, 0);
}

void TextEffects::effect_position_default(std::uint32_t self, std::uint32_t x, std::uint32_t y) {
    if (word_at(m_, self + kEffEnabled) != 0) {
        check(x != 0 && y != 0, kMsgBadPointer);
    }
}

void TextEffects::effect_colour_default(std::uint32_t self, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    if (word_at(m_, self + kEffEnabled) != 0) {
        check(r != 0 && g != 0 && b != 0, kMsgBadPointer);
    }
}

void TextEffects::effect_disable(std::uint32_t self) { set_word_at(m_, self + kEffEnabled, 0); }

// ---- FallingTextEffect -----------------------------------------------------------------------------------

std::uint32_t TextEffects::falling_construct(std::uint32_t self) {
    effect_construct(self);
    frontend_effects_falling_at(m_, self).set_field_0(static_cast<Word>(kEffVtFalling));
    set_word_at(m_, self + kFallBlood, 0);
    return self;
}

void TextEffects::falling_destroy(std::uint32_t self) {
    frontend_effects_falling_at(m_, self).set_field_0(static_cast<Word>(kEffVtFalling));
    effect_destroy(self);
}

std::uint32_t TextEffects::falling_delete(std::uint32_t self, std::uint32_t flag) {
    falling_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kDeleteService, {self});
    }
    return self;
}

void TextEffects::falling_set_blood(std::uint32_t self, std::uint32_t blood) { set_word_at(m_, self + kFallBlood, blood); }

void TextEffects::falling_init(std::uint32_t self) {
    set_word_at(m_, self + kEffEnabled, 1);
    for (std::uint32_t i = 0; i < kFallSlotCount; ++i) {
        const std::uint32_t slot = self + kFallSlots + i * kFallSlotStride;
        set_half_at(m_, slot, rnd(0x1e) + 0xf);
        set_half_at(m_, slot + 2, rnd(0x30));
        set_half_at(m_, slot + 4, 0);
        set_half_at(m_, slot + 6, (rnd(0x100) - 0x80u) & 0xfff);
    }
    set_word_at(m_, self + kFallFloor, global_word(m_, kScreenHeight) + 0x1e);
    const std::uint32_t pick = rnd(2);
    set_word_at(m_, self + kFallFrame, 0);
    set_word_at(m_, self + kFallBlood, pick == 0 ? 1 : 0);
}

void TextEffects::falling_update(std::uint32_t self) {
    if (word_at(m_, self + kEffEnabled) == 0) {
        return;
    }
    if (byte_at(m_, self + kEffRunsPaused) == 0 || (global_word(m_, kPausedA) == 0 && global_word(m_, kPausedB) == 0)) {
        for (std::uint32_t i = 0; i < kFallSlotCount; ++i) {
            const std::uint32_t at = self + kFallSlots + i * kFallSlotStride;
            set_half_at(m_, at, half_at(m_, at) - 1u);
        }
        set_word_at(m_, self + kFallFrame, word_at(m_, self + kFallFrame) + 1);
    }
}

void TextEffects::falling_glyph_done(std::uint32_t self, std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t index) {
    check(global_word(m_, kBloodSprite) != 0, kMsgNoBloodSprite);
    if (word_at(m_, self + kEffEnabled) == 0 || word_at(m_, self + kFallBlood) != 1) {
        return;
    }
    const std::int32_t x = a >> 12;
    const std::int32_t y = b >> 12;
    if (x > 0x200 || x < -16) {
        return;
    }
    std::int32_t h = static_cast<std::int32_t>(static_cast<std::uint32_t>(c) - static_cast<std::uint32_t>(b)) >> 12;
    std::int32_t w = 0x10;
    if (x + 0x10 > 0x200) {
        w = 0x200 - x;
    }
    const std::int32_t height = global_int(m_, kScreenHeight);
    if (h + y > height) {
        h = height - y;
    }
    check(w + x <= 0x200, kMsgBadWidth);
    check(h + y <= global_int(m_, kScreenHeight), kMsgBadHeight);
    const std::uint32_t slot = self + (static_cast<std::uint32_t>(index) & 0x1f) * kFallSlotStride;
    const std::int32_t dx = short_at(m_, slot + 0xe);
    const std::int32_t dy = short_at(m_, slot + 0x10);
    const std::int32_t dz = short_at(m_, slot + 0x12);
    const std::uint32_t sprite = global_word(m_, kBloodSprite);
    const bool clipped = global_word(m_, kClipFlag) != 0;
    auto clip_to_glyph = [&] { s_.note(kClipRect, {u(x), u(y), u(w), u(h), 0}); };
    auto clip_to_screen = [&] { s_.note(kClipRect, {0, 0, 0x200, u(global_int(m_, kScreenHeight)), 0}); };
    auto draw = [&] {
        set_word_at(m_, sprite + 0xc, 0x2000);
        s_.note(kBloodDraw, {sprite, u(x - dx), u(y - dy), u(dz)});
    };
    if (clipped) {
        clip_to_glyph();
        draw();
        clip_to_screen();
    } else {
        clip_to_screen();
        draw();
        clip_to_glyph();
    }
}

void TextEffects::clear_clip() { s_.note(kClipRect, {0, 0, 0x200, global_word(m_, kScreenHeight), 0}); }

void TextEffects::clear_clip_for(std::uint32_t mode) {
    if (mode == 5) {
        s_.note(kClipRect, {0, 0, 0x200, global_half(m_, kClipHeightA), 0});
    } else if (mode == 7) {
        s_.note(kClipRect, {0, 0, 0x200, global_half(m_, kClipHeightB), 0});
    }
}

void TextEffects::falling_position(std::uint32_t self, std::uint32_t x, std::uint32_t y) {
    if (word_at(m_, self + kEffEnabled) == 0) {
        return;
    }
    check(word_at(m_, self + kEffEnabled) != 0, kMsgEffectEnabled);
    check(x != 0 && y != 0, kMsgBadPointer);
    const std::uint32_t slot = word_at(m_, self + kFallCursor) & 0x1f;
    const std::int32_t counter = short_at(m_, self + kFallSlots + slot * kFallSlotStride);
    std::int32_t drop = -counter;
    if (word_at(m_, self + kFallBlood) == 1) {
        drop = static_cast<std::int32_t>(static_cast<std::uint32_t>(drop) << 13);
    } else {
        drop = static_cast<std::int32_t>(static_cast<std::uint32_t>(mul(drop, drop)) << 12);
    }
    if (counter >= 0) {
        set_word_at(m_, self + kFallMoved, word_at(m_, self + kFallMoved) + 1);
        set_word_at(m_, self + kFallCursor, word_at(m_, self + kFallCursor) + 1);
        return;
    }
    const std::int32_t moved = static_cast<std::int32_t>(word_at(m_, y) + static_cast<std::uint32_t>(drop));
    const std::int32_t floor = static_cast<std::int32_t>(word_at(m_, self + kFallFloor) << 12);
    if (moved < floor) {
        set_word_at(m_, y, static_cast<std::uint32_t>(moved));
        set_word_at(m_, self + kFallMoved, word_at(m_, self + kFallMoved) + 1);
        set_word_at(m_, self + kFallCursor, word_at(m_, self + kFallCursor) + 1);
    } else {
        set_word_at(m_, y, static_cast<std::uint32_t>(floor));
        set_word_at(m_, self + kFallCursor, word_at(m_, self + kFallCursor) + 1);
    }
}

void TextEffects::falling_colour(std::uint32_t self, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    if (word_at(m_, self + kEffEnabled) == 0) {
        return;
    }
    check(word_at(m_, self + kEffEnabled) != 0, kMsgEffectEnabled);
    check(r != 0 && g != 0 && b != 0, kMsgBadPointer);
    set_byte_at(m_, r, 0xff);
    set_byte_at(m_, g, 0x32);
    set_byte_at(m_, b, 0x32);
}

void TextEffects::falling_set_scale(std::uint32_t self, std::uint32_t scale) { set_word_at(m_, self + kFallScale, scale); }

void TextEffects::falling_bind(std::uint32_t self, std::uint32_t font) {
    effect_bind(self, font);
    if (word_at(m_, self + kEffEnabled) != 0) {
        check(font != 0, kMsgNoFontToBind);
        set_word_at(m_, font + kFontScale, word_at(m_, self + kFallScale));
        set_word_at(m_, self + kFallMoved, 0);
        set_word_at(m_, self + kFallCursor, 0);
    }
}

std::uint32_t TextEffects::falling_finished(std::uint32_t self) {
    const std::uint32_t any = word_at(m_, self + kFallMoved) != 0 ? 1 : 0;
    set_word_at(m_, self + kEffEnabled, any);
    return any == 0 ? 1 : 0;
}

void TextEffects::blood_sprite_update(std::uint32_t self) {
    if (word_at(m_, self + kEffEnabled) == 0 || word_at(m_, self + kFallBlood) != 1) {
        return;
    }
    check(global_word(m_, kBloodSprite) != 0, kMsgNoBloodSprite);
    const std::uint32_t sprite = global_word(m_, kBloodSprite);
    set_byte_at(m_, sprite + 4, 1);
    set_byte_at(m_, sprite + 6, 1);
    const std::int32_t frame = int_at(m_, self + kFallFrame);
    if (frame > 0x32) {
        std::int32_t fade = mul(0x52 - frame, 3);
        if (fade < 0) {
            fade = 0;
        }
        set_byte_at(m_, sprite + 7, u(fade));
        set_byte_at(m_, sprite + 8, u(fade));
        set_byte_at(m_, sprite + 9, u(fade));
    } else {
        set_byte_at(m_, sprite + 7, 0x60);
        set_byte_at(m_, sprite + 8, 0x60);
        set_byte_at(m_, sprite + 9, 0x60);
    }
}

void TextEffects::blood_sprite_create() {
    check(global_word(m_, kBloodSprite) == 0, kMsgSpriteNotEmpty);
    s_.note(kNewService, {0x60});
    const std::uint32_t memory = static_cast<std::uint32_t>(s_.value(kNewService));
    std::uint32_t sprite = 0;
    if (memory != 0) {
        s_.note(kSpriteNamed, {memory, 0x00540bbc, 1, 0x20, 0x20}, m_.cstring(0x00540bbc));
        sprite = static_cast<std::uint32_t>(s_.value(kSpriteNamed));
    }
    set_global_word(m_, kBloodSprite, sprite);
    s_.note(kSpriteReady, {sprite});
    set_byte_at(m_, global_word(m_, kBloodSprite) + 5, 1);
}

void TextEffects::blood_sprite_destroy() {
    check(global_word(m_, kBloodSprite) != 0, kMsgSpriteMissing);
    if (global_word(m_, kBloodSprite) != 0) {
        s_.note(kSpriteDestroy, {global_word(m_, kBloodSprite), 1});
    }
    set_global_word(m_, kBloodSprite, 0);
}

// ---- ZoomTextEffect --------------------------------------------------------------------------------------

std::uint32_t TextEffects::zoom_construct(std::uint32_t self) {
    effect_construct(self);
    frontend_effects_zoom_at(m_, self).set_field_0(static_cast<Word>(kEffVtZoom));
    set_word_at(m_, self + kZoomState, 0);
    return self;
}

void TextEffects::zoom_destroy(std::uint32_t self) {
    frontend_effects_zoom_at(m_, self).set_field_0(static_cast<Word>(kEffVtZoom));
    effect_destroy(self);
}

std::uint32_t TextEffects::zoom_delete(std::uint32_t self, std::uint32_t flag) {
    zoom_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kDeleteService, {self});
    }
    return self;
}

void TextEffects::zoom_bind(std::uint32_t self, std::uint32_t font) {
    effect_bind(self, font);
    if (word_at(m_, self + kEffEnabled) == 0) {
        return;
    }
    // the scale the zoom aims for: a fixed one on the first kind of level, otherwise by game mode
    const std::int32_t kind = global_int(m_, kGameLevelKind);
    const std::int32_t held = (kind > 1 && global_int(m_, kGameMode) != 7) ? 0x940 : 0xbc0;
    const std::int32_t target = kind == 1 ? 0x1000 : held;
    auto scale = [&](std::int32_t value) { set_word_at(m_, font + kFontScale, u(value)); };
    auto finish_step = [&](std::int32_t end, std::uint32_t next) {
        if (int_at(m_, self + kZoomCounter) == end) {
            set_word_at(m_, self + kZoomCounter, 0);
            set_word_at(m_, self + kZoomState, next);
        }
    };
    const std::int32_t counter = int_at(m_, self + kZoomCounter);
    switch (word_at(m_, self + kZoomState)) {
        case 0:
            check(false, kMsgBadZoomState);
            break;
        case 1:
            check(counter >= 0 && counter <= 5, kMsgBadZoomStep);
            scale(0x3c0 + mul(target - 0x3c0, counter) / 5);
            finish_step(5, 2);
            break;
        case 2:
            check(counter >= 0 && counter <= 5, kMsgBadZoomStep);
            scale(target + mul(held - target, counter) / 5);
            finish_step(5, 3);
            break;
        case 3:
            scale(held);
            finish_step(0x1e, 4);
            break;
        case 4:
            check(counter >= 0 && counter <= 10, kMsgBadZoomStep);
            scale(held - mul(counter, held) / 10);
            finish_step(10, 7);
            break;
        case 5: {
            const std::int32_t start = int_at(m_, self + kZoomStart);
            check(counter >= 0 && counter <= 10, kMsgBadZoomStep);
            scale(start + mul(held - start, counter) / 10);
            finish_step(10, 6);
            break;
        }
        case 7:
            scale(0);
            break;
        default:  // 6 and anything above: nothing
            break;
    }
    set_word_at(m_, self + kZoomApplied, word_at(m_, font + kFontScale));
}

void TextEffects::zoom_init(std::uint32_t self) {
    set_word_at(m_, self + kZoomCounter, 0);
    set_word_at(m_, self + kEffEnabled, 1);
    set_word_at(m_, self + kZoomState, 1);
}

void TextEffects::zoom_update(std::uint32_t self) {
    if (word_at(m_, self + kEffEnabled) == 0) {
        return;
    }
    if (byte_at(m_, self + kEffRunsPaused) == 0 || (global_word(m_, kPausedA) == 0 && global_word(m_, kPausedB) == 0)) {
        set_word_at(m_, self + kZoomCounter, word_at(m_, self + kZoomCounter) + 1);
    }
}

void TextEffects::zoom_colour(std::uint32_t self, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    if (word_at(m_, self + kEffEnabled) == 0) {
        return;
    }
    check(word_at(m_, self + kEffEnabled) != 0, kMsgEffectEnabled);
    check(r != 0 && g != 0 && b != 0, kMsgBadPointer);
    const std::uint32_t state = word_at(m_, self + kZoomState);
    if (state != 5 && state != 6) {
        return;
    }
    if (state == 6) {
        set_word_at(m_, self + kZoomCounter, 10);
    }
    std::int32_t counter = int_at(m_, self + kZoomCounter);
    check(counter >= 0 && counter <= 10, kMsgBadZoomStep);
    const std::int32_t red = 0x80 + mul(counter, 127) / 10;
    counter = int_at(m_, self + kZoomCounter);
    check(counter >= 0 && counter <= 10, kMsgBadZoomStep);
    const std::int32_t other = 0x80 - mul(counter, 78) / 10;
    check(red >= 0 && red <= 0xff, kMsgBadColour);
    check(other >= 0 && other <= 0xff, kMsgBadColour);
    check(other >= 0 && other <= 0xff, kMsgBadColour);
    set_byte_at(m_, r, u(red));
    set_byte_at(m_, g, u(other));
    set_byte_at(m_, b, u(other));
}

std::uint32_t TextEffects::zoom_finished(std::uint32_t self) {
    if (word_at(m_, self + kZoomState) == 6) {
        set_word_at(m_, self + kEffEnabled, 0);
    }
    return word_at(m_, self + kEffEnabled) == 0 ? 1 : 0;
}

void TextEffects::zoom_start(std::uint32_t self) {
    set_word_at(m_, self + kZoomStart, word_at(m_, self + kZoomApplied));
    set_word_at(m_, self + kZoomState, 5);
    set_word_at(m_, self + kZoomCounter, 0);
}

}  // namespace opentony::runtime::frontend
