#pragma once

#include "frontend_text.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>

namespace opentony::runtime::frontend {

// Phase 4.10a: the text effects (texteffects.cpp, 0x004b94a0..0x004ba780), from the disassembly. A text effect is an
// object bound to a Font (Font +0x40) whose virtual methods Font::draw calls for every glyph: the position and
// colour callbacks may change what a glyph is drawn with, and a per-glyph callback runs after it is drawn.
//
// Base class TextEffect (vtable 0x005197b8; vtable slots times four are the method offsets):
//   +0x00 vtable   +0x04 (byte) runs while the game is paused   +0x08 enabled / bound flag
//   0 deleting destructor (flag)  1 bind (font)  2 unbind (font)  3 init  4 update  5 position callback (&x, &y)
//   6 colour callback (&r, &g, &b)  7 finished?  8 per-glyph callback (a, b, c, index)
// FallingTextEffect (vtable 0x005197dc, 0x124 bytes): drips blood. +0x0c: 32 glyph records of 8 bytes (+0 countdown,
//   +2 +4 +6 words), +0x10c floor (in pixels), +0x110 glyph cursor, +0x114 font scale to apply, +0x118 glyphs moved this
//   frame, +0x11c blood flag, +0x120 frame count.
// ZoomTextEffect (vtable 0x00519800): scales the font through seven states. +0x0c state, +0x10 frame counter in the
//   state, +0x14 the font scale last applied, +0x18 the scale the zoom started from.
constexpr std::uint32_t kEffVtBase = 0x005197b8;
constexpr std::uint32_t kEffVtFalling = 0x005197dc;
constexpr std::uint32_t kEffVtZoom = 0x00519800;

constexpr std::uint32_t kEffRunsPaused = 0x04;
constexpr std::uint32_t kEffEnabled = 0x08;

constexpr std::uint32_t kFallSlots = 0x0c;
constexpr std::uint32_t kFallSlotStride = 8;
constexpr std::uint32_t kFallSlotCount = 32;
constexpr std::uint32_t kFallFloor = 0x10c;
constexpr std::uint32_t kFallCursor = 0x110;
constexpr std::uint32_t kFallScale = 0x114;
constexpr std::uint32_t kFallMoved = 0x118;
constexpr std::uint32_t kFallBlood = 0x11c;
constexpr std::uint32_t kFallFrame = 0x120;

constexpr std::uint32_t kZoomState = 0x0c;
constexpr std::uint32_t kZoomCounter = 0x10;
constexpr std::uint32_t kZoomApplied = 0x14;
constexpr std::uint32_t kZoomStart = 0x18;

constexpr std::uint32_t kScreenHeight = 0x00527d54;
constexpr std::uint32_t kPausedA = 0x0056a8e0;
constexpr std::uint32_t kPausedB = 0x00561c04;
constexpr std::uint32_t kBloodSprite = 0x0056db6c;
constexpr std::uint32_t kClipFlag = 0x029da3a0;
constexpr std::uint32_t kGameLevelKind = 0x00533f3c;    // 1 = the first style of level (a fixed target scale)
constexpr std::uint32_t kGameMode = 0x00533f38;
constexpr std::uint32_t kClipHeightA = 0x00533ff2;
constexpr std::uint32_t kClipHeightB = 0x00534022;

constexpr const char* kRndService = "rnd";                      // Rnd(limit) -> value (scripted)
constexpr const char* kClipRect = "clip_rect";                  // the engine's clip rectangle (x, y, width, height, 0)
constexpr const char* kBloodDraw = "blood_draw";                // the blood sprite's draw method (sprite, x, y, z)
constexpr const char* kNewService = "operator_new";             // (size) -> address (scripted)
constexpr const char* kDeleteService = "operator_delete";       // (object)
constexpr const char* kSpriteNamed = "sprite_construct_named";  // (memory, name, 1, 0x20, 0x20) -> sprite (scripted); name is the text
constexpr const char* kSpriteReady = "sprite_init";             // (sprite)
constexpr const char* kSpriteDestroy = "virtual_delete";        // (sprite, 1)

constexpr std::uint32_t kMsgNoFontToBind = 0x00540af8;
constexpr std::uint32_t kMsgAlreadyBound = 0x00540ac4;
constexpr std::uint32_t kMsgNotAllowedToUnbind = 0x00540b28;
constexpr std::uint32_t kMsgBadPointer = 0x00540b4c;
constexpr std::uint32_t kMsgEffectEnabled = 0x00540ba4;
constexpr std::uint32_t kMsgNoBloodSprite = 0x00540b94;
constexpr std::uint32_t kMsgBadWidth = 0x00540b80;
constexpr std::uint32_t kMsgBadHeight = 0x00540b6c;
constexpr std::uint32_t kMsgSpriteNotEmpty = 0x00536214;
constexpr std::uint32_t kMsgSpriteMissing = 0x00536260;
constexpr std::uint32_t kMsgBadZoomStep = 0x00534c7c;
constexpr std::uint32_t kMsgBadZoomState = 0x00540bc8;
constexpr std::uint32_t kMsgBadColour = 0x00540be8;

class TextEffects final {
public:
    TextEffects(RetailMemory& memory, RetailServices& services, FontTools& fonts) : m_(memory), s_(services), f_(fonts) {}

    // ---- TextEffect (base) ----
    std::uint32_t effect_construct(std::uint32_t self);                          // 0x004b94a0
    std::uint32_t effect_delete(std::uint32_t self, std::uint32_t flag);         // 0x004b94c0
    void effect_destroy(std::uint32_t self);                                     // 0x004b94e0
    void effect_bind(std::uint32_t self, std::uint32_t font);                    // 0x004b94f0
    void effect_unbind(std::uint32_t self, std::uint32_t font);                  // 0x004b95a0
    void effect_position_default(std::uint32_t self, std::uint32_t x, std::uint32_t y);                      // 0x004b9610
    void effect_colour_default(std::uint32_t self, std::uint32_t r, std::uint32_t g, std::uint32_t b);      // 0x004b9690
    void effect_disable(std::uint32_t self);                                     // 0x004b9720

    // ---- FallingTextEffect ----
    std::uint32_t falling_construct(std::uint32_t self);                         // 0x004b9730
    std::uint32_t falling_delete(std::uint32_t self, std::uint32_t flag);        // 0x004b9750
    void falling_destroy(std::uint32_t self);                                    // 0x004b9770
    void falling_set_blood(std::uint32_t self, std::uint32_t blood);             // 0x004b9780
    void falling_init(std::uint32_t self);                                       // 0x004b9790
    void falling_update(std::uint32_t self);                                     // 0x004b9820
    void falling_glyph_done(std::uint32_t self, std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t index);   // 0x004b9860
    void clear_clip();                                                           // 0x004b9ac0
    void clear_clip_for(std::uint32_t mode);                                     // 0x004b9ae0
    void falling_position(std::uint32_t self, std::uint32_t x, std::uint32_t y);                             // 0x004b9b30
    void falling_colour(std::uint32_t self, std::uint32_t r, std::uint32_t g, std::uint32_t b);             // 0x004b9c80
    void falling_set_scale(std::uint32_t self, std::uint32_t scale);             // 0x004b9d50
    void falling_bind(std::uint32_t self, std::uint32_t font);                   // 0x004b9d60
    std::uint32_t falling_finished(std::uint32_t self);                          // 0x004b9de0
    void blood_sprite_update(std::uint32_t self);                                // 0x004b9e00
    void blood_sprite_create();                                                  // 0x004b9ef0
    void blood_sprite_destroy();                                                 // 0x004b9fb0

    // ---- ZoomTextEffect ----
    std::uint32_t zoom_construct(std::uint32_t self);                            // 0x004ba020
    std::uint32_t zoom_delete(std::uint32_t self, std::uint32_t flag);           // 0x004ba040
    void zoom_destroy(std::uint32_t self);                                       // 0x004ba060
    void zoom_bind(std::uint32_t self, std::uint32_t font);                      // 0x004ba070
    void zoom_init(std::uint32_t self);                                          // 0x004ba430
    void zoom_update(std::uint32_t self);                                        // 0x004ba450
    void zoom_colour(std::uint32_t self, std::uint32_t r, std::uint32_t g, std::uint32_t b);                // 0x004ba480
    std::uint32_t zoom_finished(std::uint32_t self);                             // 0x004ba760
    void zoom_start(std::uint32_t self);                                         // 0x004ba780

private:
    void check(bool condition, std::uint32_t message) { s_.check(condition, message); }
    [[nodiscard]] std::uint32_t rnd(std::uint32_t limit);

    RetailMemory& m_;
    RetailServices& s_;
    FontTools& f_;
};

}  // namespace opentony::runtime::frontend
