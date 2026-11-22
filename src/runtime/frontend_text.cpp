#include "auto_views.hpp"
#include "frontend_text.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

// The original's 32-bit integer arithmetic wraps; so does this.
std::int32_t mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
std::int32_t sub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
std::int32_t shl12(std::int32_t a) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) << 12); }

// A signed `char` comparison, as the compiler emitted it for `c > '`'`.
bool in_range(std::uint32_t c, char lo_exclusive, char hi_exclusive) {
    const char ch = static_cast<char>(c & 0xff);
    return ch > lo_exclusive && ch < hi_exclusive;
}

}  // namespace

std::uint32_t FontTools::escape_class(std::uint32_t c) {
    switch (static_cast<char>(c & 0xff)) {
        case ']':
        case '}':
        case '>':
            return 0xfe;
        case '[':
            return 0xfd;
        case '{':
            return 0xfc;
        case '<':
            return 0xfb;
        case '|':
            return 0xf9;
        case '~':
            return 0xf8;
        case '^':
            return 0xfa;
        default:
            return 0;
    }
}

std::uint32_t FontTools::glyph_index(std::uint32_t font, std::uint32_t c) {
    const std::int32_t mapping = int_at(m_, font + kFontMapping);
    const std::int32_t count = int_at(m_, font + kFontGlyphCount);
    const std::uint32_t ch = c & 0xff;
    auto finish = [&]() -> std::uint32_t {
        // LAB_0044a843: a character with an escape class has no glyph; everything else maps to the sentinel glyph
        if (escape_class(c) != 0) {
            return 0xff;
        }
        return byte_at(m_, font + kFontGlyphCount);
    };
    if (mapping == 2) {
        if (in_range(c, '`', '{')) {
            return ch - 0x30;
        }
    } else if (mapping != 0) {
        if (mapping == 1) {
            if (in_range(c, '/', ':')) {
                return ch - 0x30;
            }
            if (static_cast<char>(ch) == ':') {
                return 10;
            }
            if (static_cast<char>(ch) == ' ') {
                return 0xff;
            }
        } else {
            s_.check(false, kMsgBadMapping);
        }
        return finish();
    }
    if (in_range(c, '@', '[')) {
        return ch - 0x41;
    }
    if (in_range(c, '`', '{')) {
        return ch - 0x61;
    }
    if (in_range(c, '/', ':')) {
        return ch - 0x16;
    }
    if (static_cast<char>(ch) == ' ') {
        return 0xff;
    }
    // punctuation lives after the letters and digits, present only if the font has that many glyphs
    struct Punct {
        char c;
        std::int32_t index;
    };
    static constexpr std::array<Punct, 13> table = {{{'?', 0x24}, {'!', 0x25}, {':', 0x26}, {'.', 0x27}, {',', 0x28},
                                                     {'-', 0x29}, {'%', 0x2a}, {'+', 0x2b}, {'\'', 0x2c}, {'#', 0x2d},
                                                     {'/', 0x2e}, {'$', 0x2f}, {'_', 0x30}}};
    for (const Punct& p : table) {
        if (static_cast<char>(ch) == p.c) {
            if (p.index < count) {
                return static_cast<std::uint32_t>(p.index);
            }
            break;
        }
    }
    return finish();
}

void FontTools::set_mapping(std::uint32_t font, std::uint32_t mapping) {
    set_word_at(m_, font + kFontMapping, mapping);
    for (std::uint32_t i = 0; i < 0x100; ++i) {
        // the character is passed as an int 0..255 and read back as a signed char
        set_byte_at(m_, font + kFontMap + i, glyph_index(font, i));
    }
}

void FontTools::set_rgb(std::uint32_t font, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    set_word_at(m_, font + kFontRed, r);
    set_word_at(m_, font + kFontGreen, g);
    set_word_at(m_, font + kFontBlue, b);
}

void FontTools::set_effect(std::uint32_t font, std::uint32_t effect) { set_word_at(m_, font + kFontEffect, effect); }

std::uint32_t FontTools::effect(std::uint32_t font) const { return word_at(m_, font + kFontEffect); }

std::int32_t FontTools::char_advance(std::uint32_t font, std::uint32_t c) {
    if (escape_class(c) != 0) {
        return 0;
    }
    const std::int32_t mapping = int_at(m_, font + kFontMapping);
    const std::uint32_t glyphs = word_at(m_, font + kFontGlyphs);
    std::uint32_t advance;
    if (mapping == 0 || mapping == 2) {
        advance = byte_at(m_, glyphs + kGlyphAdvance);
    } else {
        if (mapping != 1) {
            s_.check(false, kMsgBadMapping);
            return 0;
        }
        advance = static_cast<char>(c & 0xff) != ':' ? byte_at(m_, glyphs + kGlyphAdvance) : byte_at(m_, glyphs + 10 * kGlyphStride + kGlyphAdvance);
    }
    return mul(add(static_cast<std::int32_t>(advance), int_at(m_, font + kFontSpacing)), int_at(m_, font + kFontScale)) >> 12;
}

std::int32_t FontTools::text_width_of(std::uint32_t font, const std::string& text) const {
    const std::uint32_t glyphs = word_at(m_, font + kFontGlyphs);
    std::int32_t sum = 0;
    for (const char ch : text) {
        const std::uint32_t c = static_cast<std::uint8_t>(ch);
        if (c == 0) {
            break;
        }
        const std::uint32_t g = byte_at(m_, font + kFontMap + c);
        if (g == 0xff) {
            if (escape_class(c) == 0) {
                sum = add(sum, byte_at(m_, glyphs + kGlyphAdvance));
            }
        } else {
            sum = add(sum, add(byte_at(m_, glyphs + g * kGlyphStride + kGlyphAdvance), int_at(m_, font + kFontSpacing)));
        }
    }
    return mul(int_at(m_, font + kFontScale), sum) >> 12;
}

// 0x0044abe0 on a caller-held string: the same loop as ellipsize(), the shortened text kept in `text`.
void FontTools::ellipsize_text(std::uint32_t font, std::string& text, std::int32_t max_width) {
    s_.check(text.size() < 0x80, kMsgTextTooLong);
    if (text_width_of(font, text) < max_width) {
        return;
    }
    if (text_width_of(font, text + "...") <= max_width) {
        return;
    }
    do {
        const std::size_t len = text.size();
        s_.check(len >= 4, kMsgMaxWidthSmall);
        // retail writes "..." at len-4 and a NUL at len-1; for a text of fewer than four characters the first dots land before the buffer
        std::string next = text;
        for (std::size_t k = 0; k < 3; ++k) {
            const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(len) - 4 + static_cast<std::ptrdiff_t>(k);
            if (at >= 0) {
                next[static_cast<std::size_t>(at)] = '.';
            }
        }
        if (len >= 1) {
            next.resize(len - 1);
        }
        if (next == text) {
            return;
        }
        text = next;
    } while (text_width_of(font, text + "...") > max_width);
}

std::int32_t FontTools::text_width(std::uint32_t font, std::uint32_t text) const {
    const std::uint32_t glyphs = word_at(m_, font + kFontGlyphs);
    std::int32_t sum = 0;
    for (std::uint32_t p = text; byte_at(m_, p) != 0; ++p) {
        const std::uint32_t c = byte_at(m_, p);
        const std::uint32_t g = byte_at(m_, font + kFontMap + c);
        if (g == 0xff) {
            if (escape_class(c) == 0) {
                sum = add(sum, byte_at(m_, glyphs + kGlyphAdvance));
            }
        } else {
            sum = add(sum, add(byte_at(m_, glyphs + g * kGlyphStride + kGlyphAdvance), int_at(m_, font + kFontSpacing)));
        }
    }
    return mul(int_at(m_, font + kFontScale), sum) >> 12;
}

std::int32_t FontTools::text_top(std::uint32_t font, std::uint32_t text) const {
    const std::uint32_t glyphs = word_at(m_, font + kFontGlyphs);
    std::int32_t best = 0;
    for (std::uint32_t p = text; byte_at(m_, p) != 0; ++p) {
        const std::uint32_t g = byte_at(m_, font + kFontMap + byte_at(m_, p));
        if (g != 0xff) {
            best = std::max<std::int32_t>(best, byte_at(m_, glyphs + g * kGlyphStride + kGlyphTop));
        }
    }
    return mul(int_at(m_, font + kFontScale), best) >> 12;
}

std::int32_t FontTools::text_height(std::uint32_t font, std::uint32_t text) const {
    const std::uint32_t glyphs = word_at(m_, font + kFontGlyphs);
    std::int32_t best = 0;
    for (std::uint32_t p = text; byte_at(m_, p) != 0; ++p) {
        const std::uint32_t g = byte_at(m_, font + kFontMap + byte_at(m_, p));
        if (g != 0xff) {
            const std::int32_t h = static_cast<std::int32_t>(byte_at(m_, glyphs + g * kGlyphStride + kGlyphBottom)) -
                                   static_cast<std::int32_t>(byte_at(m_, glyphs + g * kGlyphStride + kGlyphTop));
            best = std::max(best, h);
        }
    }
    return mul(int_at(m_, font + kFontScale), best) >> 12;
}

std::int32_t FontTools::text_extent(std::uint32_t font, std::uint32_t text) const {
    const std::int32_t height = text_height(font, text);
    return add(text_top(font, text), height);
}

void FontTools::apply_escape(std::uint32_t font, std::uint32_t c) {
    const std::uint32_t cls = escape_class(c);
    s_.check(cls != 0, kMsgNotEscape);
    auto save = [&] {
        set_byte_at(m_, font + kFontSaved, byte_at(m_, font + kFontRed));
        set_byte_at(m_, font + kFontSaved + 1, byte_at(m_, font + kFontGreen));
        set_byte_at(m_, font + kFontSaved + 2, byte_at(m_, font + kFontBlue));
    };
    switch (cls) {
        case 0xfe:  // restore the saved colour
            set_word_at(m_, font + kFontRed, byte_at(m_, font + kFontSaved));
            set_word_at(m_, font + kFontGreen, byte_at(m_, font + kFontSaved + 1));
            set_word_at(m_, font + kFontBlue, byte_at(m_, font + kFontSaved + 2));
            break;
        case 0xfd:  // '[' : save, then blue-green (0, 0x80, 0xff)
            save();
            set_word_at(m_, font + kFontRed, 0);
            set_word_at(m_, font + kFontGreen, 0x80);
            set_word_at(m_, font + kFontBlue, 0xff);
            break;
        case 0xfc:  // '{' : save, then amber (0xff, 0x80, 0)
            save();
            set_word_at(m_, font + kFontBlue, 0);
            set_word_at(m_, font + kFontRed, 0xff);
            set_word_at(m_, font + kFontGreen, 0x80);
            break;
        case 0xfb:  // '<' : save, then grey (0x40, 0x40, 0x40)
            save();
            set_word_at(m_, font + kFontRed, 0x40);
            set_word_at(m_, font + kFontGreen, 0x40);
            set_word_at(m_, font + kFontBlue, 0x40);
            break;
        case 0xf9:  // '|' : save, then red (0x80, 0x40, 0x40)
            save();
            set_word_at(m_, font + kFontRed, 0x80);
            set_word_at(m_, font + kFontGreen, 0x40);
            set_word_at(m_, font + kFontBlue, 0x40);
            break;
        case 0xf8:  // '~' : save, then green (0x40, 0x80, 0x40)
            save();
            set_word_at(m_, font + kFontRed, 0x40);
            set_word_at(m_, font + kFontGreen, 0x80);
            set_word_at(m_, font + kFontBlue, 0x40);
            break;
        default:  // '^' (0xfa): nothing
            break;
    }
}

void FontTools::ellipsize(std::uint32_t font, std::uint32_t text, std::int32_t max_width) {
    auto length = [&] { return static_cast<std::uint32_t>(m_.cstring(text, 0x1000).size()); };
    s_.check(length() < 0x80, kMsgTextTooLong);
    // the original measures a stack copy of the text followed by "..." (sprintf "%s..."); the copy is
    // laid next to the text in a scratch region so the width code reads it the same way
    auto width_with_dots = [&] {
        std::string copy = m_.cstring(text, 0x1000) + "...";
        copy.push_back('\0');
        // measure without a memory region of its own: the widths are a sum over the characters
        const std::uint32_t glyphs = word_at(m_, font + kFontGlyphs);
        std::int32_t sum = 0;
        for (const char ch : copy) {
            const std::uint32_t c = static_cast<std::uint8_t>(ch);
            if (c == 0) {
                break;
            }
            const std::uint32_t g = byte_at(m_, font + kFontMap + c);
            if (g == 0xff) {
                if (escape_class(c) == 0) {
                    sum = add(sum, byte_at(m_, glyphs + kGlyphAdvance));
                }
            } else {
                sum = add(sum, add(byte_at(m_, glyphs + g * kGlyphStride + kGlyphAdvance), int_at(m_, font + kFontSpacing)));
            }
        }
        return mul(int_at(m_, font + kFontScale), sum) >> 12;
    };
    if (text_width(font, text) < max_width) {
        return;
    }
    if (width_with_dots() <= max_width) {
        return;
    }
    do {
        const std::uint32_t len = length();
        s_.check(len >= 4, kMsgMaxWidthSmall);
        set_byte_at(m_, text + len - 4, '.');
        set_byte_at(m_, text + len - 3, '.');
        set_byte_at(m_, text + len - 2, '.');
        set_byte_at(m_, text + len - 1, 0);
    } while (width_with_dots() > max_width);
}

void FontTools::draw(std::uint32_t font, std::int32_t x, std::int32_t y, std::uint32_t text, std::uint32_t arg) {
    const std::int32_t scale = int_at(m_, font + kFontScale);
    if (scale == 0 && word_at(m_, font + kFontMono) == 0) {
        return;
    }
    const std::uint32_t glyphs = word_at(m_, font + kFontGlyphs);
    const std::uint32_t effect = word_at(m_, font + kFontEffect);
    // per-glyph positions (12-bit fixed point): x, y, and the effect's copies of y and x before it ran. The
    // original keeps 64 of each on the stack.
    std::array<std::int32_t, kMaxDrawChars> xs{}, ys{}, ys_before{}, xs_before{};
    std::int32_t x_acc = 0;
    std::int32_t max_height = 0;
    std::int32_t max_top = 0;
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; byte_at(m_, text + i) != 0; ++i) {
        s_.check(i < kMaxDrawChars, kMsgTooManyCharacters);
        if (i >= kMaxDrawChars) {
            throw MemoryFault("Font::draw: a string of more than 64 characters runs the original's position arrays into each other");
        }
        const std::uint32_t c = byte_at(m_, text + i);
        const std::uint32_t g = byte_at(m_, font + kFontMap + c);
        ++count;
        if (g == 0xff) {
            if (escape_class(c) == 0) {
                // a character without a glyph is a space: the first glyph's advance less a quarter. The product
                // and the scaling by 0.25 are done by the x87 at 24 bits, truncated to an integer.
                const std::int32_t advance = byte_at(m_, glyphs + kGlyphAdvance);
                const float product = static_cast<float>(advance) * static_cast<float>(scale);
                const float quarter = product * 0.25f;
                x_acc = add(x_acc, sub(mul(advance, scale), static_cast<std::int32_t>(quarter)));
            }
            continue;
        }
        const std::uint32_t entry = glyphs + g * kGlyphStride;
        xs[i] = x_acc;
        const std::int32_t top = byte_at(m_, entry + kGlyphTop);
        const std::int32_t height = mul(static_cast<std::int32_t>(byte_at(m_, entry + kGlyphBottom)) - top, scale);
        if (height > max_height) {
            max_height = height;
        }
        const std::int32_t top_scaled = mul(top, scale);
        if (top_scaled > max_top) {
            max_top = top_scaled;
        }
        ys[i] = -top_scaled;
        if (effect != 0) {
            ys_before[i] = -top_scaled;
            xs_before[i] = x_acc;
            if (s_.live()) {
                // the effect's method takes (&x, &y) and writes through them (the replay reports the values and scripts the new ones)
                set_global_word(m_, kEffectCells, static_cast<std::uint32_t>(xs[i]));
                set_global_word(m_, kEffectCells + 4, static_cast<std::uint32_t>(ys[i]));
                s_.note(kEffectPos, {effect, kEffectCells, kEffectCells + 4});
                xs[i] = global_int(m_, kEffectCells);
                ys[i] = global_int(m_, kEffectCells + 4);
            } else {
                s_.note(kEffectPos, {effect, static_cast<std::uint32_t>(xs[i]), static_cast<std::uint32_t>(ys[i])});
                xs[i] = s_.value(kEffectPos);
                ys[i] = s_.value(kEffectPos);
            }
        }
        if (word_at(m_, font + kFontMono) == 0) {
            x_acc = add(x_acc, mul(add(byte_at(m_, entry + kGlyphAdvance), int_at(m_, font + kFontSpacing)), scale));
        } else {
            const std::int32_t step = char_advance(font, c);
            xs[i] = add(xs[i], sub(shl12(step), mul(byte_at(m_, entry + kGlyphAdvance), scale)));
            x_acc = add(x_acc, shl12(char_advance(font, c)));
        }
    }
    std::int32_t x_start = x_acc;
    switch (int_at(m_, font + kFontHAlign)) {
        case 0:
            x_start = shl12(x);
            break;
        case 1:
            x_start = sub(mul(x, 0x1000), x_acc >> 1);
            break;
        case 2:
            x_start = sub(mul(x, 0x1000), x_acc);
            break;
        default:
            break;
    }
    std::int32_t y_start = 0;
    switch (word_at(m_, font + kFontVAlign)) {
        case 0:
            y_start = add(shl12(y), max_top);
            break;
        case 1:
            y_start = sub(add((add(max_height, max_top)) >> 1, shl12(y)), max_height);
            break;
        case 2:
            y_start = sub(shl12(y), max_height);
            break;
        case 3:
            y_start = shl12(y);
            break;
        default:
            // retail uses whatever its stack slot holds here; no caller sets an alignment above 3
            throw MemoryFault("Font::draw: vertical alignment above 3 reads an uninitialised stack slot");
    }
    const std::int32_t shadow_x = mul(int_at(m_, font + kFontShadowX), scale);
    const std::int32_t shadow_y = mul(int_at(m_, font + kFontShadowY), scale);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t c = byte_at(m_, text + i);
        const std::uint32_t g = byte_at(m_, font + kFontMap + c);
        if (g == 0xff) {
            if (escape_class(c) != 0) {
                apply_escape(font, c);
            }
            continue;
        }
        const std::uint32_t sprite = word_at(m_, glyphs + g * kGlyphStride);
        set_word_at(m_, sprite + kSpriteScale, static_cast<std::uint32_t>(scale));
        set_byte_at(m_, sprite + kSpriteShadow, 0);
        set_byte_at(m_, sprite + kSpriteMode, 1);
        set_byte_at(m_, sprite + kSpriteRed, byte_at(m_, font + kFontRed));
        set_byte_at(m_, sprite + kSpriteGreen, byte_at(m_, font + kFontGreen));
        set_byte_at(m_, sprite + kSpriteBlue, byte_at(m_, font + kFontBlue));
        if (effect != 0) {
            s_.note(kEffectCol, {effect, sprite + kSpriteRed, sprite + kSpriteGreen, sprite + kSpriteBlue});
            // the effect writes the three colour bytes through the pointers it was given (live, it already has)
            if (!s_.live()) {
                set_byte_at(m_, sprite + kSpriteRed, static_cast<std::uint32_t>(s_.value(kEffectCol)));
                set_byte_at(m_, sprite + kSpriteGreen, static_cast<std::uint32_t>(s_.value(kEffectCol)));
                set_byte_at(m_, sprite + kSpriteBlue, static_cast<std::uint32_t>(s_.value(kEffectCol)));
            }
        }
        s_.note(kSpriteDraw,
                {sprite, static_cast<std::uint32_t>(add(xs[i], x_start) >> 12), static_cast<std::uint32_t>(add(ys[i], y_start) >> 12), arg});
        if (byte_at(m_, font + kFontShadow) != 0) {
            set_byte_at(m_, sprite + kSpriteShadow, 1);
            set_byte_at(m_, sprite + kSpriteMode, 2);
            set_byte_at(m_, sprite + kSpriteRed, 0xff);
            set_byte_at(m_, sprite + kSpriteGreen, 0xff);
            set_byte_at(m_, sprite + kSpriteBlue, 0xff);
            s_.note(kSpriteDraw, {sprite, static_cast<std::uint32_t>(add(add(xs[i], shadow_x), x_start) >> 12),
                                  static_cast<std::uint32_t>(add(add(ys[i], shadow_y), y_start) >> 12), arg});
        }
        if (effect != 0) {
            s_.note(kEffectGlyphDone, {effect, static_cast<std::uint32_t>(add(xs_before[i], x_start)),
                                       static_cast<std::uint32_t>(add(ys_before[i], y_start)),
                                       static_cast<std::uint32_t>(add(ys[i], y_start)), i});
        }
    }
}

void FontTools::unload(std::uint32_t font) {
    const std::uint32_t clut = word_at(m_, font + kFontClut);
    if (clut != 0xffffffffu) {
        s_.note(kFreeSlot, {clut});
    }
    const std::int32_t count = int_at(m_, font + kFontGlyphCount);
    const std::uint32_t glyphs = word_at(m_, font + kFontGlyphs);
    if (count != -1 && -1 < count + 1) {
        for (std::int32_t i = 0; i < int_at(m_, font + kFontGlyphCount) + 1; ++i) {
            const std::uint32_t sprite = word_at(m_, glyphs + static_cast<std::uint32_t>(i) * kGlyphStride);
            if (sprite != 0) {
                s_.note(kSpriteDelete, {sprite, 1});
            }
        }
    }
    s_.note(kMemDelete, {glyphs});
}

bool FontTools::is_loaded(std::uint32_t name) {
    const std::string wanted = m_.cstring(name, 0x100);
    for (std::uint32_t slot = 0; slot < kFontSlots; ++slot) {
        if (m_.cstring(kFontNames + slot * kFontNameSize, kFontNameSize) == wanted && word_at(m_, kFontTable + 4 * slot) != 0) {
            return true;
        }
    }
    return false;
}

void FontTools::clear_table() {
    for (std::uint32_t slot = 0; slot < kFontSlots; ++slot) {
        set_word_at(m_, kFontTable + 4 * slot, 0);
    }
}

}  // namespace opentony::runtime::frontend
