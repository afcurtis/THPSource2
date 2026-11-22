#include "auto_views.hpp"
#include "render_kit.hpp"
#include "seam_addresses_generated.hpp"

// Phase 4.15e: the palette slots (`pal.cpp`, 0x0048a0b0..0x0048ab05) and the palette cache of the D3D texture layer (`d3dtex.cpp`, 0x004da460..0x004da840). The VRAM palette strip holds up to 256 palettes
// of 16 colours and up to 256 of 256 colours; a slot is free while its flag byte is 1. Every palette the PSX data supplies is also kept as a source-lookup node (0x1c bytes of bookkeeping per node
// in the byte counter). The cache keeps a converted copy of the colours (the 15-bit colours in the pixel format of the D3D device) per palette.

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kCount16 = 0x00535fac;               // dword: the number of 16-colour palette slots
constexpr Word kCount256 = 0x00535fb0;              // dword: the number of 256-colour palette slots
constexpr Word kLookupHead = 0x0056b2f0;            // dword: the source-lookup list
constexpr Word kLookupBytes = 0x0056b704;           // dword: the bytes the lookup nodes take
constexpr Word kTransparentPalette = 0x0056b700;    // byte: palettes keep colour 0 opaque when set
constexpr Word kPal256X = 0x0056b6fc;               // dword: where the 256-colour strip starts in VRAM (x)
constexpr Word kPal256Y = 0x0056b2ec;               // (y)
constexpr Word kPal16X = 0x0056b6f8;
constexpr Word kPal16Y = 0x0056b4f4;
constexpr Word kFree256 = 0x0056b2f4;               // 256 flag bytes: the 256-colour slot is free
constexpr Word kFree16 = 0x0056b3f4;                // 256 flag bytes: the 16-colour slot is free
constexpr Word kTrans16 = 0x0056b4f8;               // 256 bytes: the palette in the slot has a transparent colour
constexpr Word kTrans256 = 0x0056b5f8;
constexpr Word kSlotAllocator = 0x00489440;         // (-1, width, slots, &x, &y, 16, 0, 0): finds room for the strip in VRAM
constexpr Word kSlotRelease = 0x00489450;           // (y, x)
constexpr Word kSlotTail = 0x00489490;
constexpr Word kClearRect = 0x004e8730;             // (rectangle, r, g, b): fills the rectangle of VRAM
constexpr Word kLoadImage = 0x004e8770;             // (rectangle, data)
constexpr Word kDrawSync = 0x004e8740;

constexpr Word kCacheHead = 0x006a0524;
constexpr Word kCacheTail = 0x006a0528;
constexpr Word kCacheCount = 0x006a0518;
constexpr Word kTextureList = 0x006a051c;           // the textures that may use a palette
constexpr Word kRedBits = 0x006a0500;
constexpr Word kGreenBits = 0x006a04fc;
constexpr Word kBlueBits = 0x006a04f8;
constexpr Word kAlphaBits = 0x006a0504;
constexpr Word kRedShift = 0x006a0510;
constexpr Word kGreenShift = 0x006a050c;
constexpr Word kBlueShift = 0x006a0508;
constexpr Word kAlphaShift = 0x006a0514;

}  // namespace

// 0x0048a0b0 Pal_Init(sixteen, two hundred fifty six) (cdecl): sets up the slot counts, asks for room in VRAM for both strips, marks every slot free and clears the strips' first rows to colour 0x80
void RenderKit::pal_init(Word sixteen, Word big) {
    check(s(sixteen) >= 0 && s(sixteen) <= 0x100, 0x00535fec);
    check(s(big) >= 0 && s(big) <= 0x100, 0x00535fcc);
    set_global_word(m_, kCount256, big);
    set_global_word(m_, kCount16, sixteen);
    set_global_word(m_, kLookupHead, 0);
    check(global_word(m_, kPal256X) == 0, 0x00535fb4);
    slot_allocator(0x100, global_word(m_, kCount256), kPal256X, kPal256Y);
    slot_allocator(0x10, sixteen, kPal16X, kPal16Y);
    for (Word k = 0; k < 256; ++k) {
        set_byte_at(m_, kFree16 + k, 1);
    }
    for (Word k = 0; k < 255; ++k) {
        set_byte_at(m_, kFree256 + 1 + k, 1);
    }
    const Word rect = kScratch;
    set_global_byte(m_, kFree256, 0);
    set_half_at(m_, rect, global_half(m_, kPal256X));
    set_half_at(m_, rect + 2, global_half(m_, kPal256Y));
    set_half_at(m_, rect + 4, 0x100);
    set_half_at(m_, rect + 6, 1);
    engine(kClearRect, {rect, 0, 0, 0}, bytes_text(rect, 8));
    engine(kDrawSync, {0});
    set_half_at(m_, rect, half_at(m_, rect) + 1);
    set_half_at(m_, rect + 4, half_at(m_, rect + 4) - 1);
    engine(kClearRect, {rect, 0x80, 0x80, 0x80}, bytes_text(rect, 8));
    engine(kDrawSync, {0});
    set_global_byte(m_, kFree16, 0);
    set_half_at(m_, rect, global_half(m_, kPal16X));
    set_half_at(m_, rect + 2, global_half(m_, kPal16Y));
    set_half_at(m_, rect + 4, 0x10);
    set_half_at(m_, rect + 6, 1);
    engine(kClearRect, {rect, 0, 0, 0}, bytes_text(rect, 8));
    engine(kDrawSync, {0});
    set_half_at(m_, rect, half_at(m_, rect) + 1);
    set_half_at(m_, rect + 4, half_at(m_, rect + 4) - 1);
    engine(kClearRect, {rect, 0x80, 0x80, 0x80}, bytes_text(rect, 8));
    engine(kDrawSync, {0});
}

// the VRAM allocator answers with the strip's position through its two pointer arguments (the replay scripts the two values)
void RenderKit::slot_allocator(Word width, Word slots, Word x_address, Word y_address) {
    engine(kSlotAllocator, {0xffffffff, width, slots, x_address, y_address, 0x10, 0, 0});
    if (s_.live()) {
        return;   // the PC build's allocator is `xor eax, eax; ret`: it writes neither position (milestone 9's cold boot runs Pal_Init live)
    }
    const Word x = static_cast<Word>(s_.value(pcplatform::engine_name(kSlotAllocator)));
    const Word y = static_cast<Word>(s_.value(pcplatform::engine_name(kSlotAllocator)));
    set_word_at(m_, x_address, x);
    set_word_at(m_, y_address, y);
}

// 0x0048a320 Pal_Cleanup: gives both strips back
void RenderKit::pal_cleanup() {
    engine(kSlotRelease, {global_half(m_, kPal256X), global_half(m_, kPal256Y)});
    engine(kSlotRelease, {global_half(m_, kPal16X), global_half(m_, kPal16Y)});
    set_global_word(m_, kPal256X, 0);
    set_global_word(m_, kPal16X, 0);
    engine(kSlotTail);
}

// 0x0048a360 Palette_AllocSourceLookupNode(key) (cdecl): a node at the head of the list (the key at +8, the use count at +4)
Word RenderKit::lookup_alloc(Word key) {
    const Word node = alloc(0x18, 0);
    set_global_word(m_, kLookupBytes, global_word(m_, kLookupBytes) + 0x1c);
    set_word_at(m_, node + 0x10, global_word(m_, kLookupHead));
    set_word_at(m_, node + 0x14, 0);
    set_global_word(m_, kLookupHead, node);
    const Word next = word_at(m_, node + 0x10);
    if (next != 0) {
        set_word_at(m_, next + 0x14, node);
    }
    set_byte_at(m_, node + 6, 0);
    set_word_at(m_, node + 8, key);
    set_half_at(m_, node + 4, 0);
    return node;
}

// 0x0048a3b0 RemovePaletteEntry(node) (cdecl)
void RenderKit::lookup_remove(Word node) {
    const Word next = word_at(m_, node + 0x10);
    if (next != 0) {
        set_word_at(m_, next + 0x14, word_at(m_, node + 0x14));
    }
    const Word previous = word_at(m_, node + 0x14);
    if (previous != 0) {
        set_word_at(m_, previous + 0x10, word_at(m_, node + 0x10));
    }
    if (node == global_word(m_, kLookupHead)) {
        set_global_word(m_, kLookupHead, word_at(m_, node + 0x10));
    }
    mem_free(node);
    set_global_word(m_, kLookupBytes, global_word(m_, kLookupBytes) - 0x1c);
}

// 0x0048a400 Palette_FindSourceLookupNode(key) (cdecl): the node of a key, 0 for none
Word RenderKit::lookup_find(Word key) {
    Word node = global_word(m_, kLookupHead);
    while (node != 0 && word_at(m_, node + 8) != key) {
        node = word_at(m_, node + 0x10);
    }
    return node;
}

// 0x0048a420 Palette_ClearSourceLookup: every node nobody uses releases its cache record and its slot
void RenderKit::lookup_clear() {
    Word node = global_word(m_, kLookupHead);
    while (node != 0) {
        const Word next = word_at(m_, node + 0x10);
        if (half_at(m_, node + 4) == 0) {
            d3dtex_free_palette_entry(word_at(m_, node + 0xc), 0);
            const std::uint8_t flags = byte_at(m_, node + 3);
            set_word_at(m_, node + 0xc, 0);
            if ((flags & 1) != 0) {
                set_byte_at(m_, kFree16 + byte_at(m_, node + 2), 1);
            }
            if ((byte_at(m_, node + 3) & 2) != 0) {
                set_byte_at(m_, kFree256 + byte_at(m_, node + 2), 1);
            }
            lookup_remove(node);
        }
        node = next;
    }
}

// 0x0048a490 GetFree16Slot / 0x0048a590 GetFree256Slot: the first free slot (taken now); the assertion fires when there is none
Word RenderKit::get_free_slot(Word count_address, Word flags, Word message) {
    const std::int32_t count = s(word_at(m_, count_address));
    std::int32_t slot = 0;
    if (count > 0) {
        do {
            if (byte_at(m_, flags + slot) != 0) {
                break;
            }
            ++slot;
        } while (slot < count);
    }
    check(slot < count, message);
    set_byte_at(m_, flags + slot, 0);
    return static_cast<Word>(slot);
}

// 0x0048a490 GetFree16Slot, 0x0048a590 GetFree256Slot
Word RenderKit::get_free16_slot() { return get_free_slot(kCount16, kFree16, 0x0053603c); }
Word RenderKit::get_free256_slot() { return get_free_slot(kCount256, kFree256, 0x00536084); }

// 0x0048a500 Free16Slot(palette) (cdecl)
void RenderKit::free16_slot(Word palette) { free_slot(palette, kCount16, kPal16X, kPal16Y, kFree16, 0x0053605c); }

// the slot of a palette word: the slot whose VRAM row matches the palette word is free again
void RenderKit::free_slot(Word palette, Word count_address, Word x_address, Word y_address, Word flags, Word message) {
    const std::int32_t count = s(word_at(m_, count_address));
    std::int32_t slot = 0;
    if (count > 0) {
        const Word column = static_cast<Word>(s(word_at(m_, x_address)) >> 4) & 0x3f;
        Word row = word_at(m_, y_address) << 6;
        do {
            if ((row | column) == (palette & 0xffff)) {
                break;
            }
            ++slot;
            row += 0x40;
        } while (slot < count);
    }
    check(slot < count, message);
    set_byte_at(m_, flags + slot, 1);
}

// 0x0048a7e0 Palette_CreateFromPsxColors(key, colours, flags) (cdecl): a lookup node for a palette of the PSX data; flag 1 is a 16-colour palette (else 256), flag 2 a 256-colour one. The colours
// are made ready (magenta is the transparent colour; the others get the opaque bit unless the palette is transparent), a slot is taken, the position of the palette is the node's word, and the
// colours go to VRAM
Word RenderKit::palette_create(Word key, Word colours, Word flags) {
    const Word node = lookup_alloc(key);
    set_byte_at(m_, node + 3, flags);
    check((flags & 3) != 3, 0x0053615c);
    check((flags & 3) != 0, 0x0053615c);
    const std::int32_t count = (flags & 1) != 0 ? 0x10 : 0x100;
    bool transparent = false;
    for (std::int32_t k = 0; k < count; ++k) {
        if ((half_at(m_, colours + 2 * k) & 0x7fff) == 0x7c1f && !transparent) {
            transparent = true;
        }
    }
    for (std::int32_t k = 0; k < count; ++k) {
        const Word at = colours + 2 * k;
        const Word colour = half_at(m_, at);
        if ((colour & 0x7fff) == 0x7c1f) {
            set_half_at(m_, at, 0);
        } else if (transparent || colour == 0 || global_byte(m_, kTransparentPalette) != 0) {
            set_half_at(m_, at, colour | 0x8000);
        }
    }
    set_byte_at(m_, node + 7, 0);
    if (!transparent) {
        for (std::int32_t k = 0; k < count; ++k) {
            set_byte_at(m_, colours + 2 * k + 1, byte_at(m_, colours + 2 * k + 1) | 0x80);
        }
    }
    const Word rect = kScratch;
    if (count == 0x10) {
        const Word slot = get_free16_slot();
        set_byte_at(m_, node + 2, slot);
        set_half_at(m_, rect, global_word(m_, kPal16X));
        set_half_at(m_, rect + 2, slot + global_word(m_, kPal16Y));
        set_half_at(m_, node, ((slot + global_word(m_, kPal16Y)) << 6) | (static_cast<Word>(s(global_word(m_, kPal16X)) >> 4) & 0x3f));
        check(global_word(m_, kPal16Y) == 0, 0x00536114);
        set_byte_at(m_, kTrans16 + byte_at(m_, node + 2), transparent ? 1 : 0);
    } else {
        const Word slot = get_free256_slot();
        set_byte_at(m_, node + 2, slot);
        set_half_at(m_, rect, global_word(m_, kPal256X));
        set_half_at(m_, rect + 2, slot + global_word(m_, kPal256Y));
        set_half_at(m_, node, ((slot + global_word(m_, kPal256Y)) << 6) | (static_cast<Word>(s(global_word(m_, kPal256X)) >> 4) & 0x3f));
        check(global_word(m_, kPal256Y) == 0, 0x00536114);
        set_byte_at(m_, kTrans256 + byte_at(m_, node + 2), transparent ? 1 : 0);
    }
    set_half_at(m_, rect + 4, count);
    set_half_at(m_, rect + 6, 1);
    engine(kLoadImage, {rect, colours}, bytes_text(rect, 8));
    set_byte_at(m_, node + 6, 1);
    return node;
}

// 0x0048aad0 / 0x0048ab00 style helpers are services of the palette store (not in this milestone)

// ---- the palette cache of the D3D texture layer ----

// the 15-bit colour of the PSX (a 1-bit alpha at bit 15) in the pixel format of the device: each component is taken to the number of bits the format has and shifted into place
Word RenderKit::device_colour(Word colour) {
    Word red = ((colour >> 7) & 0xf8);
    red = ((red & 0xffff) >> ((8 - global_half(m_, kRedBits)) & 31)) & 0xffff;
    red <<= (global_word(m_, kRedShift) & 31);
    Word green = ((colour >> 2) & 0xf8);
    green = ((green & 0xffff) >> ((8 - global_half(m_, kGreenBits)) & 31)) & 0xffff;
    green <<= (global_word(m_, kGreenShift) & 31);
    const Word merged = red | green;
    Word alpha = (colour >> 8) & 0x80;
    alpha = ((alpha & 0xffff) >> ((8 - global_half(m_, kAlphaBits)) & 31)) & 0xffff;
    alpha <<= (global_word(m_, kAlphaShift) & 31);
    Word blue = (colour & 0x1f) << 3;
    blue = ((blue & 0xffff) >> ((8 - global_half(m_, kBlueBits)) & 31)) & 0xffff;
    blue <<= (global_word(m_, kBlueShift) & 31);
    return merged | alpha | blue;
}

// 0x004da460 Palette_AllocCacheRecord: a zeroed record (0x18 bytes) at the tail of the cache list
Word RenderKit::cache_alloc() {
    const Word record = engine_value(seam::crt::CRT_operator_new, {0x18});
    for (Word offset = 0; offset < 0x18; offset += 4) {
        set_word_at(m_, record + offset, 0);
    }
    if (global_word(m_, kCacheHead) == 0) {
        set_global_word(m_, kCacheHead, record);
    }
    const Word tail = global_word(m_, kCacheTail);
    if (tail != 0) {
        set_word_at(m_, tail + 0x10, record);
        set_word_at(m_, record + 0x14, global_word(m_, kCacheTail));
    }
    const Word count = global_word(m_, kCacheCount);
    set_global_word(m_, kCacheTail, record);
    set_global_word(m_, kCacheCount, count + 1);
    return record;
}

// 0x004da4b0 D3DTEX_FreePaletteEntry(record, free colours) (cdecl)
void RenderKit::d3dtex_free_palette_entry(Word record, Word free_colours) { cache_free(record, free_colours); }

// 0x004da4d0 Palette_FreeCacheRecord(record, free the source colours too) (cdecl)
void RenderKit::cache_free(Word record, Word free_colours) {
    if (record == global_word(m_, kCacheHead)) {
        set_global_word(m_, kCacheHead, word_at(m_, record + 0x10));
    }
    const Word previous = word_at(m_, record + 0x14);
    if (previous != 0) {
        set_word_at(m_, previous + 0x10, word_at(m_, record + 0x10));
    }
    const Word next = word_at(m_, record + 0x10);
    if (next != 0) {
        set_word_at(m_, next + 0x14, word_at(m_, record + 0x14));
    }
    if (record == global_word(m_, kCacheTail)) {
        set_global_word(m_, kCacheTail, word_at(m_, record + 0x14));
    }
    set_global_word(m_, kCacheCount, global_word(m_, kCacheCount) - 1);
    engine(seam::crt::FUN_005002f3, {word_at(m_, record + 8)});
    if (free_colours != 0) {
        engine(seam::crt::FUN_005002f3, {word_at(m_, record + 0xc)});
    }
    engine(seam::crt::FUN_005002f3, {record});
}

// 0x004da550: frees every record of the cache
void RenderKit::cache_free_all() {
    while (global_word(m_, kCacheHead) != 0) {
        cache_free(global_word(m_, kCacheHead), 0);
    }
    set_global_word(m_, kCacheTail, 0);
    set_global_word(m_, kCacheCount, 0);
}

// 0x004da580 (cdecl): the colours the record was made from
Word RenderKit::cache_source(Word record) { return word_at(m_, record + 0xc); }

// 0x004da590 (cdecl): a texture re-uploads itself with the colours given as its palette's (the record's colour pointer is swapped for the call; flag 4 swaps two pairs of shorts)
void RenderKit::texture_reload_palette(Word texture, Word record, Word colours) {
    const Word saved = word_at(m_, record + 8);
    set_word_at(m_, record + 8, colours);
    Word flags = word_at(m_, texture + 0x10);
    if ((flags & 4) != 0) {
        const Word first = half_at(m_, texture + 0x14);
        const Word second = half_at(m_, texture + 0x16);
        set_half_at(m_, texture + 0x14, half_at(m_, texture + 0x1c));
        set_half_at(m_, texture + 0x16, half_at(m_, texture + 0x1e));
        set_half_at(m_, texture + 0x1c, first);
        set_half_at(m_, texture + 0x1e, second);
    }
    if (global_word(m_, 0x029da3a0) != 0 && (flags & 1) != 0) {
        engine(seam::crt::FUN_005002f3, {word_at(m_, texture + 8)});
        flags = word_at(m_, texture + 0x10) & ~0x200u;
        set_word_at(m_, texture + 8, word_at(m_, texture + 0xc));
        set_word_at(m_, texture + 0x10, flags);
    }
    engine(0x004d8f10, {texture, word_at(m_, word_at(m_, texture + 0x20)), 0, 0});
    set_word_at(m_, record + 8, saved);
}

// 0x004da620 (cdecl, record, colours): converts new colours to the device format, re-uploads every texture that uses the palette and keeps the converted colours in the record
void RenderKit::cache_set_colours(Word record, Word colours) {
    convert_in_place(colours, word_at(m_, record + 4));
    Word texture = global_word(m_, kTextureList);
    while (texture != 0) {
        const Word used = word_at(m_, texture + 0x20);
        if (used != 0 && used == record) {
            texture_reload_palette(texture, record, colours);
        }
        texture = word_at(m_, texture + 0x24);
    }
    const Word bytes = word_at(m_, record + 4) * 2;
    m_.copy(word_at(m_, record + 8), colours, bytes);
}

// 0x004da680 Palette_ConvertColors(tag, colours, count) (cdecl): a cache record with a converted copy of `count` colours
Word RenderKit::palette_convert(Word tag, Word colours, Word count) {
    const Word record = cache_alloc();
    set_word_at(m_, record, tag);
    set_word_at(m_, record + 4, count);
    set_word_at(m_, record + 0xc, colours);
    const Word copy = engine_value(seam::crt::CRT_operator_new, {count * 2});
    set_word_at(m_, record + 8, copy);
    m_.copy(copy, colours, count * 2);
    for (std::int32_t k = 0; k < s(count); ++k) {
        const Word at = word_at(m_, record + 8) + 2 * static_cast<Word>(k);
        set_half_at(m_, at, device_colour(half_at(m_, at)));
    }
    return record;
}

// 0x004da780 (cdecl, colours, count): converts `count` colours in place; a non-zero colour gets the opaque bit first
void RenderKit::convert_in_place(Word colours, Word count) {
    for (std::int32_t k = 0; k < s(count); ++k) {
        const Word at = colours + 2 * static_cast<Word>(k);
        Word colour = half_at(m_, at);
        colour = colour != 0 ? (colour | 0x8000) : 0;
        set_half_at(m_, at, device_colour(colour));
    }
}

// 0x0048aad0 Pal_CopyToMemory(palette, destination) (cdecl): the colours of the palette's cache record (16 for a 16-colour palette, 256 otherwise: eight or 128 dwords)
void RenderKit::pal_copy_to_memory(Word palette, Word destination) {
    const Word source = cache_source(word_at(m_, palette + 0xc));
    const Word dwords = (byte_at(m_, palette + 3) & 1) != 0 ? 8 : 0x80;
    for (Word k = 0; k < dwords; ++k) {
        set_word_at(m_, destination + 4 * k, word_at(m_, source + 4 * k));
    }
}

// 0x0048ab00 Pal_StoreFromMemory(palette, colours) (cdecl): new colours for the palette's cache record
void RenderKit::pal_store_from_memory(Word palette, Word colours) { cache_set_colours(word_at(m_, palette + 0xc), colours); }

}  // namespace opentony::runtime::renderkit
