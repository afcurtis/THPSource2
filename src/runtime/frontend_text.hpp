#pragma once

#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>

namespace opentony::runtime {

// Phase 4.10a: the frontend's text layer, over the original's memory layout (the 4.8 / 4.9 pattern).
//
// fonttools.cpp (frontend_text.cpp): the Font object the game draws every string through. Its layout,
// from the disassembly of 0x0044a070..0x0044b0a0 (all offsets from the object, 0x14c bytes):
//   +0x00 vtable        +0x04 horizontal alignment (0 left, 1 centre, 2 right)
//   +0x08 vertical alignment (0 baseline-top, 1 middle, 2 bottom, 3 top)    +0x0c extra advance per glyph
//   +0x14, +0x18, +0x1c colour (dwords)      +0x21 shadow flag (byte)       +0x24, +0x28 shadow offset
//   +0x2c monospace flag    +0x30 scale (0x1000 = 1.0)    +0x34 glyph table    +0x38 glyph count
//   +0x3c CLUT slot (-1 = none)    +0x40 effect object (the draw callbacks)    +0x44 character mapping (0 / 1 / 2)
//   +0x48..+0x4a saved colour (bytes)    +0x4b character to glyph index table (256 bytes, 0xff = no glyph)
// A glyph record is 8 bytes: +0 sprite object, +4 advance, +5 bottom row, +6 top row (bytes).
// The glyph sprite is the engine's (a virtual draw method at vtable +0xc and a deleting destructor at +0): the
// port reports each call as a service and edits the sprite's fields in place exactly as retail does
// (+4 shadow flag, +6 mode, +7 +8 +9 colour bytes, +0xc scale).
// The effect object (+0x40) is a text effect (texteffects.cpp); Font::draw calls three of its virtual methods
// (+0x14 position of a glyph, +0x18 colour of a glyph, +0x20 after a glyph) and the first two may change what
// they are handed by pointer: the port asks the service for the new values by name, in the order the original's
// callee writes them (x then y; red, green, blue).
namespace frontend {

constexpr std::uint32_t kFontHAlign = 0x04;
constexpr std::uint32_t kFontVAlign = 0x08;
constexpr std::uint32_t kFontSpacing = 0x0c;
constexpr std::uint32_t kFontRed = 0x14;
constexpr std::uint32_t kFontGreen = 0x18;
constexpr std::uint32_t kFontBlue = 0x1c;
constexpr std::uint32_t kFontShadow = 0x21;
constexpr std::uint32_t kFontShadowX = 0x24;
constexpr std::uint32_t kFontShadowY = 0x28;
constexpr std::uint32_t kFontMono = 0x2c;
constexpr std::uint32_t kFontScale = 0x30;
constexpr std::uint32_t kFontGlyphs = 0x34;
constexpr std::uint32_t kFontGlyphCount = 0x38;
constexpr std::uint32_t kFontClut = 0x3c;
constexpr std::uint32_t kFontEffect = 0x40;
constexpr std::uint32_t kFontMapping = 0x44;
constexpr std::uint32_t kFontSaved = 0x48;
constexpr std::uint32_t kFontMap = 0x4b;
constexpr std::uint32_t kFontSize = 0x14c;

constexpr std::uint32_t kGlyphStride = 8;
constexpr std::uint32_t kGlyphAdvance = 4;
constexpr std::uint32_t kGlyphBottom = 5;
constexpr std::uint32_t kGlyphTop = 6;

constexpr std::uint32_t kSpriteShadow = 0x04;
constexpr std::uint32_t kSpriteMode = 0x06;
constexpr std::uint32_t kSpriteRed = 0x07;
constexpr std::uint32_t kSpriteGreen = 0x08;
constexpr std::uint32_t kSpriteBlue = 0x09;
constexpr std::uint32_t kSpriteScale = 0x0c;

constexpr std::uint32_t kEffectPosition = 0x14;
constexpr std::uint32_t kEffectColour = 0x18;
constexpr std::uint32_t kEffectGlyph = 0x20;

constexpr std::uint32_t kFontTable = 0x0056161c;       // eight Font object pointers
constexpr std::uint32_t kFontNames = 0x0056163c;       // eight 0x10-byte names
constexpr std::uint32_t kFontSlots = 8;
constexpr std::uint32_t kFontNameSize = 0x10;

constexpr std::uint32_t kEffectCells = 0x7ff0fa00;     // live: the two words an effect's position method writes through (scratch window, 10g s23)
constexpr std::uint32_t kMaxDrawChars = 0x40;         // Font::draw keeps per-glyph positions for 64 characters

// Names of the services the font code reaches (RetailServices).
constexpr const char* kSpriteDraw = "font_sprite_draw";       // sprite vtable +0xc (x, y, arg): reported as (sprite, x, y, arg)
constexpr const char* kSpriteDelete = "font_sprite_delete";   // sprite vtable +0 (flag 1): reported as (sprite, 1)
constexpr const char* kEffectPos = "font_effect_position";    // effect +0x14 (&x, &y): (effect, x, y), then the new x, y are scripted
constexpr const char* kEffectCol = "font_effect_colour";      // effect +0x18 (&r, &g, &b): (effect, r-pointer, g-pointer, b-pointer), then new r, g, b
constexpr const char* kEffectGlyphDone = "font_effect_glyph"; // effect +0x20 (a, b, c, index)
constexpr const char* kFreeSlot = "free16_slot";              // Free16Slot(slot)
constexpr const char* kMemDelete = "mem_delete";              // Mem_Delete(block)

// Assertion messages (addresses of the original's format strings).
constexpr std::uint32_t kMsgTooManyCharacters = 0x0052bd24;
constexpr std::uint32_t kMsgBadMapping = 0x0052bd58;
constexpr std::uint32_t kMsgNotEscape = 0x0052bd74;
constexpr std::uint32_t kMsgMaxWidthSmall = 0x0052bd88;
constexpr std::uint32_t kMsgTextTooLong = 0x0052bdac;

class FontTools final {
public:
    FontTools(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // 0x0044a860: the escape class of a character: ']' '}' '>' end the colour, '[' blue-green, '{' amber, '<' grey,
    // '|' red, '~' green, '^' nothing; 0 for every other character.
    [[nodiscard]] static std::uint32_t escape_class(std::uint32_t c);
    // 0x0044a620: the glyph index of a character under the font's character mapping (0xff: no glyph).
    [[nodiscard]] std::uint32_t glyph_index(std::uint32_t font, std::uint32_t c);
    // 0x0044abb0: select the character mapping and rebuild the 256-entry glyph index table.
    void set_mapping(std::uint32_t font, std::uint32_t mapping);
    // 0x0044ab70, 0x0044ab90, 0x0044aba0.
    void set_rgb(std::uint32_t font, std::uint32_t r, std::uint32_t g, std::uint32_t b);
    void set_effect(std::uint32_t font, std::uint32_t effect);
    [[nodiscard]] std::uint32_t effect(std::uint32_t font) const;
    // 0x0044aaa0: the fixed advance of a character in monospace mode, in pixels.
    [[nodiscard]] std::int32_t char_advance(std::uint32_t font, std::uint32_t c);
    // 0x0044a4d0, 0x0044a570, 0x0044a5c0, 0x0044a540: width, tallest top, tallest glyph height, their sum.
    [[nodiscard]] std::int32_t text_width(std::uint32_t font, std::uint32_t text) const;
    [[nodiscard]] std::int32_t text_top(std::uint32_t font, std::uint32_t text) const;
    [[nodiscard]] std::int32_t text_height(std::uint32_t font, std::uint32_t text) const;
    [[nodiscard]] std::int32_t text_extent(std::uint32_t font, std::uint32_t text) const;
    // 0x0044a8e0: apply an escape character to the font's colour.
    void apply_escape(std::uint32_t font, std::uint32_t c);
    // 0x0044abe0: shorten `text` (in place) until it, with "..." appended, is no wider than `max_width`.
    void ellipsize(std::uint32_t font, std::uint32_t text, std::int32_t max_width);
    // The same on a string held by the caller (a window shortens a local copy of a name before showing it).
    void ellipsize_text(std::uint32_t font, std::string& text, std::int32_t max_width);
    [[nodiscard]] std::int32_t text_width_of(std::uint32_t font, const std::string& text) const;
    // 0x0044a070: draw a string at (x, y) pixels.
    void draw(std::uint32_t font, std::int32_t x, std::int32_t y, std::uint32_t text, std::uint32_t arg);
    // 0x0044ad30: release a font's CLUT slot, its glyph sprites and its glyph table.
    void unload(std::uint32_t font);
    // 0x0044af90: is a font of this name in the table.
    [[nodiscard]] bool is_loaded(std::uint32_t name);
    // 0x0044ad80: empty the font table.
    void clear_table();

private:
    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace frontend
}  // namespace opentony::runtime
