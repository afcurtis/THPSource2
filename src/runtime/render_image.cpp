#include "auto_views.hpp"
#include "render_kit.hpp"
#include "seam_addresses_generated.hpp"

// Phase 4.15e: the image classes of `image.cpp` (0x004564b0..0x004581a0): the 2D images the front end and the loading screens draw (a VRAM-backed sprite object, a sliced image whose pixels come from a BMP
// file, the font glyph's image), the flat rectangle primitives they queue, the BMP header checks and the palette fix-ups. A primitive is built in the polygon arena (the cursor at 0x00560fd0, the limit
// at 0x00568628) and linked at the head of the current buffer's ordering table (or at its far end); the screen scale (0x029da394 / 0x029da398) turns the 512 x 240 layout into the display's pixels.

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kCursor = 0x00560fd0;
constexpr Word kLimit = 0x00568628;
constexpr Word kCurrent = 0x00560fd4;
constexpr Word kScaleX = 0x029da394;
constexpr Word kScaleY = 0x029da398;
constexpr Word kLastClut = 0x00562010;               // dword: what the last BMP load answered
constexpr Word kPal256X = 0x0056b6fc;
constexpr Word kPal256Y = 0x0056b2ec;
constexpr Word kPal16X = 0x0056b6f8;
constexpr Word kPal16Y = 0x0056b4f4;
constexpr Word kCheatFlag = 0x0056db40;
constexpr Word kRegionNames = 0x0056d428;            // PsxRegionNamesBySlot: the records are 0x44 bytes
constexpr Word kRegionUsable = 0x0056d432;
constexpr Word kRegionRaw = 0x0056d440;

constexpr Word kImageVtable = 0x00518a48;
constexpr Word kSlicedBaseVtable = 0x00518a4c;
constexpr Word kSlicedVtable = 0x00518a54;
constexpr Word kBmpVtable = 0x00518a68;
constexpr Word kSpriteVtable = 0x00518a7c;
constexpr Word kRawVtable = 0x005184b4;

}  // namespace

// the x of a layout position in display pixels: (value * 0x029da394) >> 9 (logical); the y: (value * 0x029da398) / 240 (unsigned)
Word RenderKit::screen_x(std::int32_t value) { return (u(value) * global_word(m_, kScaleX)) >> 9; }
Word RenderKit::screen_y(std::int32_t value) { return (u(value) * global_word(m_, kScaleY)) / 240; }

// the primitive is linked in front of the table's first entry, or in front of the far entry (+0x7ff8) the sprites sort behind
void RenderKit::link_primitive(Word prim, bool far_end) {
    const Word table = word_at(m_, global_word(m_, kCurrent) + 0x84) + (far_end ? 0x7ff8u : 0u);
    set_word_at(m_, prim, word_at(m_, table));
    set_word_at(m_, table, prim);
}

// 0x004564b0 (thiscall, two arguments): the screen object: vtable 0x00518a48, the base class's constructor, flags, a position and the font lookup of its region
Word RenderKit::image_construct(Word self, Word source, Word extent) {
    thiscall_service(0x0047fdb0, self);
    render_image_image_at(m_, self).set_field_5(static_cast<std::uint8_t>(render_image_image_at(m_, self).field_5() | 2));
    render_image_image_at(m_, self).set_field_18(0);
    render_image_image_at(m_, self).set_field_8(static_cast<Word>(source));
    render_image_image_at(m_, self).set_field_0(static_cast<Word>(kImageVtable));
    render_image_image_at(m_, self).set_field_c(static_cast<Word>(extent + 0xfffec000u));
    render_image_image_at(m_, self).set_field_14(0x180);
    thiscall_service(0x0047fe30, self, {0x0052ca44});
    render_image_image_at(m_, self).set_field_28(0x400);
    render_image_image_at(m_, self).set_field_2a(0x400);
    render_image_image_at(m_, self).set_field_2c(0x400);
    render_image_image_at(m_, self).set_field_1a(static_cast<std::uint16_t>(engine_value(0x004b1de0, {0x4605a8d0, render_image_image_at(m_, self).field_1f()})));
    return self;
}

// 0x00456550 (thiscall, one argument): the deleting destructor
Word RenderKit::image_scalar_delete(Word self, Word flags) {
    image_destruct(self);
    if ((flags & 1) != 0) {
        engine(0x0047fd60, {self});
    }
    return self;
}

// 0x00456570 (fastcall): restores the vtable and runs the base class's destructor
void RenderKit::image_destruct(Word self) {
    render_image_image_at(m_, self).set_field_0(static_cast<Word>(kImageVtable));
    thiscall_service(0x0047fe20, self);
}

// 0x00456580 (thiscall, the object in ecx): renders one object with the scene renderer from a fixed camera (identity rotation, translation (0, 0, -100))
void RenderKit::render_single_object(Word object) {
    m3d_.set_identity_matrix(0x00561f9c);
    set_global_word(m_, 0x00561f6c, 0);
    set_global_word(m_, 0x00561f70, 0);
    set_global_word(m_, 0x00561f74, 0xffffff9c);
    math().matrix_set_translation(0x00561f9c, 0x00561f6c);
    engine(0x0045ef80, {0x00561f68});
    set_global_word(m_, 0x0056200c, 1);
    engine(0x0045f530, {object});
    set_global_word(m_, 0x0056200c, 0);
}

// 0x00456600 (cdecl: x, y, width, height, red, green, blue): a flat rectangle in the screen's units and a terminator primitive behind it
void RenderKit::flat_rectangle(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, Word red, Word green, Word blue, bool with_terminator) {
    const Word prim = global_word(m_, kCursor);
    if (prim > global_word(m_, kLimit)) {
        return;
    }
    set_global_word(m_, kCursor, prim + 0x1c);
    set_byte_at(m_, prim + 5, green);
    set_byte_at(m_, prim + 4, red);
    set_byte_at(m_, prim + 6, blue);
    const std::int32_t x0 = static_cast<std::int16_t>(x);
    const std::int32_t y0 = static_cast<std::int16_t>(y);
    const std::int32_t x1 = static_cast<std::int16_t>(width + x);
    const std::int32_t y1 = static_cast<std::int16_t>(height + y);
    set_half_at(m_, prim + 8, u(x0));
    set_half_at(m_, prim + 0x10, u(x0));
    set_half_at(m_, prim + 0xa, u(y0));
    set_half_at(m_, prim + 0xc, u(x1));
    set_half_at(m_, prim + 0xe, u(y0));
    set_half_at(m_, prim + 0x12, u(y1));
    set_half_at(m_, prim + 0x14, u(x1));
    set_half_at(m_, prim + 0x16, u(y1));
    set_byte_at(m_, prim + 7, with_terminator ? 0x2a : 0x28);
    set_half_at(m_, prim + 8, screen_x(x0));
    set_half_at(m_, prim + 0xa, screen_y(y0));
    set_half_at(m_, prim + 0xc, screen_x(x1));
    set_half_at(m_, prim + 0xe, screen_y(y0));
    set_half_at(m_, prim + 0x10, screen_x(x0));
    set_half_at(m_, prim + 0x12, screen_y(y1));
    set_half_at(m_, prim + 0x14, screen_x(x1));
    set_half_at(m_, prim + 0x16, screen_y(y1));
    set_word_at(m_, prim + 0x18, 0xbf800000);
    link_primitive(prim, false);
    if (!with_terminator) {
        return;
    }
    const Word terminator = global_word(m_, kCursor);
    if (terminator > global_word(m_, kLimit)) {
        return;
    }
    set_global_word(m_, kCursor, terminator + 0x10);
    engine(0x004e8940, {terminator, 0, 1, 0x140, 0});
    set_word_at(m_, terminator + 0xc, 0xbf800000);
    link_primitive(terminator, false);
}

// 0x004568d0 (cdecl: x, y, width, height, far): a rectangle primitive from the PSX library, drawn below the buffer's vertical offset
void RenderKit::rect_primitive(Word x, Word y, Word width, Word height, Word far_end) {
    const Word rect = kScratch;
    set_half_at(m_, rect, x);
    set_half_at(m_, rect + 2, half_at(m_, global_word(m_, kCurrent) + 0x90) + y);
    set_half_at(m_, rect + 4, width);
    set_half_at(m_, rect + 6, height);
    const Word prim = global_word(m_, kCursor);
    if (prim > global_word(m_, kLimit)) {
        return;
    }
    engine(0x004e87f0, {prim, rect}, bytes_text(rect, 8));
    set_global_word(m_, kCursor, prim + 0x14);
    set_word_at(m_, prim + 0x10, 0xbf800000);
    link_primitive(prim, (far_end & 0xff) != 0);
}

// 0x004569a0 (cdecl): the colour depth of a BMP header (the short at +0x1c), asserted to be 4 or 8
Word RenderKit::bmp_depth(Word header) {
    const Word depth = half_at(m_, header + 0x1c);
    check(depth == 4 || depth == 8, 0x0052d920);
    return depth;
}

// 0x00456a00 (4-bit) / 0x00456a70 (8-bit) (cdecl: header, &unused, &width, &height, &clut word, name): loads the BMP's palette and answers its size and a palette word
Word RenderKit::bmp_load(Word header, Word clear_this, Word width_out, Word height_out, Word clut_out, Word name, bool eight_bit) {
    set_word_at(m_, clear_this, 0);
    const Word width = half_at(m_, header + 0x12);
    const Word height = half_at(m_, header + 0x16);
    set_global_word(m_, kLastClut, engine_value(eight_bit ? seam::hal::D3DTex_LoadIndexedBitmap : 0x004d9f10, {header, 0, 0, name}));
    set_word_at(m_, width_out, width);
    set_word_at(m_, height_out, height);
    set_half_at(m_, clut_out, ((clear_this >> 4) & 0x3f) | (clear_this << 6));
    return 1;
}

// 0x00456ae0 MakeTransparentClut2(name, &a, &width, &height, &clut, &depth) (cdecl): reads the BMP file into a buffer, takes its depth and runs the matching loader
Word RenderKit::make_transparent_clut(Word name, Word out_a, Word out_width, Word out_height, Word out_clut, Word out_depth) {
    const std::int32_t size = s(engine_value(0x00449030, {name}));
    if (size <= 0) {
        log_line(0x0052d9a8);
        return 0;
    }
    check(size > 0x10, 0x0052d990);
    const Word buffer = alloc(u(size));
    if (buffer == 0) {
        log_line(0x0052d958);
        return 0;
    }
    engine(0x00449230, {buffer});
    if (!s_.live()) {
        // a replay has no file layer behind the call, so the BMP header it leaves is scripted; a live call has really read the file into the buffer
        for (Word offset = 0; offset < 0x40; offset += 4) {
            set_word_at(m_, buffer + offset, static_cast<Word>(s_.value(pcplatform::engine_name(0x00449230))));
        }
    }
    engine(0x00449660);
    const Word depth = bmp_depth(buffer);
    set_word_at(m_, out_depth, depth);
    if (depth == 4) {
        bmp_load(buffer, out_a, out_width, out_height, out_clut, name, false);
        mem_free(buffer);
        return 1;
    }
    if (depth == 8) {
        bmp_load(buffer, out_a, out_width, out_height, out_clut, name, true);
        mem_free(buffer);
        return 1;
    }
    check(false, 0x0052d978);
    mem_free(buffer);
    return 1;
}

// 0x00456c60 (fastcall): the sliced image's base: colour 0, grey 0x80, a scale of 1.0 (Q12)
Word RenderKit::sliced_base_construct(Word self) {
    render_image_sliced_at(m_, self).set_field_0(static_cast<Word>(kSlicedBaseVtable));
    render_image_sliced_at(m_, self).set_field_4(0);
    render_image_sliced_at(m_, self).set_field_5(0);
    render_image_sliced_at(m_, self).set_field_6(0);
    render_image_sliced_at(m_, self).set_field_7(0x80);
    render_image_sliced_at(m_, self).set_field_8(0x80);
    render_image_sliced_at(m_, self).set_field_9(0x80);
    render_image_sliced_at(m_, self).set_field_a(0);
    render_image_sliced_at(m_, self).set_field_b(0);
    render_image_sliced_at(m_, self).set_field_c(0x1000);
    return self;
}

// 0x00456c90 (fastcall): the cache record's colours of the image's palette
Word RenderKit::sliced_palette_source(Word self) {
    const Word record = engine_value(0x004d9f40, {render_image_sliced_at(m_, self).field_24()});
    return engine_value(0x004da580, {record});
}

// 0x00456cb0 (the image in ecx, one stack argument): hands the image's palette record new colours
void RenderKit::sliced_set_palette(Word self, Word colours) {
    const Word record = engine_value(0x004d9f40, {render_image_sliced_at(m_, self).field_24()});
    engine(0x004da620, {record, colours});
}

// 0x00456cd0 (fastcall): the sliced image: the base class, the vtable and cleared flags
Word RenderKit::sliced_construct(Word self) {
    sliced_base_construct(self);
    render_image_sliced_at(m_, self).set_field_0(static_cast<Word>(kSlicedVtable));
    render_image_sliced_at(m_, self).set_field_56(0);
    render_image_sliced_at(m_, self).set_field_57(0);
    render_image_sliced_at(m_, self).set_field_3c(0);
    return self;
}

// 0x00456cf0 (thiscall, one argument): the deleting destructor
Word RenderKit::sliced_scalar_delete(Word self, Word flags) {
    sliced_remove_from_memory(self);
    if ((flags & 1) != 0) {
        engine(0x00466bb0, {self});
    }
    return self;
}

// 0x00456d10 FontGlyph_ConstructImage (thiscall, seven arguments)
Word RenderKit::font_glyph_construct(Word self, Word a0, Word a1, Word a2, Word a3, Word a4, Word a5, Word a6) {
    sliced_base_construct(self);
    set_word_at(m_, self, kSlicedVtable);
    set_byte_at(m_, self + 0x56, 0);
    set_byte_at(m_, self + 0x57, 0);
    set_word_at(m_, self + 0x3c, 0);
    font_glyph_set_image_format(self, a0, a1, a2, a3, a4, a5, a6);
    return self;
}

// 0x00456d90 SlicedImage2__removeFromMemory (fastcall): releases the image's palette record, and its pixel buffer
void RenderKit::sliced_remove_from_memory(Word self) {
    render_image_sliced_at(m_, self).set_field_0(static_cast<Word>(kSlicedVtable));
    engine(0x004da400, {render_image_sliced_at(m_, self).field_24()});
    if (render_image_sliced_at(m_, self).field_57() != 0) {
        // 0x00457150: nothing
    }
    if (render_image_sliced_at(m_, self).field_56() != 0) {
        // 0x00457120: nothing
    }
    const Word pixels = render_image_sliced_at(m_, self).field_3c();
    if (pixels != 0) {
        mem_free(pixels);
    }
    render_image_sliced_at(m_, self).set_field_0(static_cast<Word>(kRawVtable));
}

// 0x00456e10 FontGlyph_SetImageFormat (thiscall: -, width, height, x, y, depth, clut word): the image's size, position and depth (4, 8 or 16 bits)
void RenderKit::font_glyph_set_image_format(Word self, Word a0, Word width, Word height, Word x, Word y, Word depth, Word clut) {
    set_half_at(m_, self + 0x42, height);
    set_half_at(m_, self + 0x4e, x);
    set_half_at(m_, self + 0x5a, clut);
    set_half_at(m_, self + 0x40, width);
    set_half_at(m_, self + 0x50, y);
    const Word bits = depth & 0xff;
    check(bits == 4 || bits == 8 || bits == 0x10, 0x0052da58);
    set_byte_at(m_, self + 0x58, bits == 4 ? 2 : (bits == 8 ? 1 : 0));
    check(s(width) > 0 && s(width) < 0x280, 0x0052da40);
    check(s(height) > 0 && s(height) < 0x1e0, 0x0052da24);
    check(s(x) >= 0 && s(x) < 0x280, 0x0052da04);
    check(s(y) >= 0 && s(y) < 0x1e0, 0x0052d9e4);
    check(!(byte_at(m_, self + 0x58) == 1 && (x & 1) != 0), 0x0052d9bc);
    if (half_at(m_, self + 0x4e) == 0) {
        set_half_at(m_, self + 0x4e, 0xff);
    }
    if (half_at(m_, self + 0x50) == 0) {
        set_half_at(m_, self + 0x50, 0xff);
    }
    image_bind_palette(self, a0);
}

// 0x00457090 (thiscall, one argument): the image takes the palette the last BMP load left (or asserts there is none)
void RenderKit::image_bind_palette(Word self, Word) {
    render_image_image_at(m_, self).set_field_52(0);
    render_image_image_at(m_, self).set_field_54(0);
    render_image_image_at(m_, self).set_field_56(0);
    const Word handle = render_image_image_at(m_, self).field_24();
    if (handle != 0) {
        engine(0x004da400, {handle});
    }
    const Word clut = global_word(m_, kLastClut);
    if (clut != 0) {
        engine(0x004da3e0, {self + 0x10, clut});
        set_global_word(m_, kLastClut, 0);
        return;
    }
    check(false, 0x0052da70);
}

// 0x00457160 (thiscall: x, y, far): draws the sliced image at (x, y) in the layout units, scaled by the image's Q12 scale; flags 0xa / 0xb centre the position on an axis
void RenderKit::sliced_draw(Word self, std::int32_t x, std::int32_t y, Word far_end) {
    std::int32_t width = render_image_sliced_at(m_, self).field_a() == 0 ? 0 : static_cast<std::int16_t>(half_at(m_, self + 0x40));
    std::int32_t height = render_image_sliced_at(m_, self).field_b() == 0 ? 0 : static_cast<std::int16_t>(half_at(m_, self + 0x42));
    const std::int32_t scale = s(render_image_sliced_at(m_, self).field_c());
    width >>= (render_image_sliced_at(m_, self).field_a() == 1 ? 1 : 0);
    const std::int32_t offset_x = (scale * width) >> 12;
    render_image_sliced_at(m_, self).set_field_44(static_cast<Word>(u(x - offset_x)));
    height >>= (render_image_sliced_at(m_, self).field_b() == 1 ? 1 : 0);
    const std::int32_t offset_y = (height * scale) >> 12;
    const std::int32_t top = y - offset_y;
    render_image_sliced_at(m_, self).set_field_48(static_cast<Word>(u(top)));
    const Word prim = global_word(m_, kCursor);
    if (prim > global_word(m_, kLimit)) {
        return;
    }
    set_global_word(m_, kCursor, prim + 0x30);
    const std::int32_t x0 = x - offset_x;
    const std::int32_t full_width = (static_cast<std::int16_t>(half_at(m_, self + 0x40)) * scale) >> 12;
    const std::int32_t full_height = (static_cast<std::int16_t>(half_at(m_, self + 0x42)) * scale) >> 12;
    set_byte_at(m_, prim + 7, 0x2c);
    set_half_at(m_, prim + 8, u(x0));
    set_half_at(m_, prim + 0xa, u(top));
    set_half_at(m_, prim + 0x12, u(top));
    set_half_at(m_, prim + 0x18, u(x0));
    set_half_at(m_, prim + 0x10, u(full_width - offset_x + x));
    set_half_at(m_, prim + 0x1a, u(full_height + top));
    set_half_at(m_, prim + 0x20, u(full_width - offset_x + x));
    set_byte_at(m_, prim + 0xc, 0);
    set_half_at(m_, prim + 0x22, u(full_height + top));
    set_byte_at(m_, prim + 0xd, 0);
    set_byte_at(m_, prim + 0x15, 0);
    set_byte_at(m_, prim + 0x1c, 0);
    set_byte_at(m_, prim + 0x14, byte_at(m_, self + 0x40) - 1);
    set_byte_at(m_, prim + 0x1d, byte_at(m_, self + 0x42) - 1);
    set_byte_at(m_, prim + 0x24, byte_at(m_, self + 0x40) - 1);
    set_byte_at(m_, prim + 0x25, byte_at(m_, self + 0x42) - 1);
    set_byte_at(m_, prim + 4, render_image_sliced_at(m_, self).field_7());
    set_byte_at(m_, prim + 5, render_image_sliced_at(m_, self).field_8());
    set_byte_at(m_, prim + 6, render_image_sliced_at(m_, self).field_9());
    set_byte_at(m_, prim + 7, (render_image_sliced_at(m_, self).field_4() != 0 ? 2 : 0) + 0x2c);
    set_byte_at(m_, prim + 7, render_image_sliced_at(m_, self).field_5() == 0 ? (byte_at(m_, prim + 7) | 1) : (byte_at(m_, prim + 7) & 0xfe));
    const Word mode = 0xff00 - (static_cast<Word>(render_image_sliced_at(m_, self).field_58()) << 7);
    set_half_at(m_, prim + 0x16, (mode & 0x180) | ((static_cast<Word>(render_image_sliced_at(m_, self).field_6()) & 3) << 5));
    set_half_at(m_, prim + 0xe, render_image_sliced_at(m_, self).field_5a());
    set_half_at(m_, prim + 8, screen_x(static_cast<std::int16_t>(half_at(m_, prim + 8))));
    set_half_at(m_, prim + 0xa, screen_y(static_cast<std::int16_t>(top)));
    set_half_at(m_, prim + 0x10, screen_x(static_cast<std::int16_t>(half_at(m_, prim + 0x10))));
    set_half_at(m_, prim + 0x12, screen_y(static_cast<std::int16_t>(top)));
    set_half_at(m_, prim + 0x18, screen_x(static_cast<std::int16_t>(x0)));
    set_half_at(m_, prim + 0x1a, screen_y(static_cast<std::int16_t>(half_at(m_, prim + 0x1a))));
    set_half_at(m_, prim + 0x20, screen_x(static_cast<std::int16_t>(half_at(m_, prim + 0x20))));
    set_half_at(m_, prim + 0x22, screen_y(static_cast<std::int16_t>(half_at(m_, prim + 0x22))));
    set_word_at(m_, prim + 0x2c, self + 0x10);
    set_word_at(m_, prim + 0x28, 0xbf800000);
    link_primitive(prim, far_end != 0);
}

// 0x004573e0 (fastcall), 0x00457400 (thiscall, one argument): the raw image's vtable, and its deleting destructor
void RenderKit::raw_destruct(Word self) { set_word_at(m_, self, kRawVtable); }
Word RenderKit::raw_scalar_delete(Word self, Word flags) {
    raw_destruct(self);
    if ((flags & 1) != 0) {
        engine(0x00466bb0, {self});
    }
    return self;
}

// 0x00457420 (thiscall: file name, flag, x, y): a bitmap image loaded from a BMP file: the file's size and palette, then the format; the flag marks it for the VRAM
Word RenderKit::bmp_image_construct(Word self, Word name, Word vram, Word x, Word y) {
    sliced_construct(self);
    render_image_bmp_at(m_, self).set_field_0(static_cast<Word>(kBmpVtable));
    const Word locals = kScratch + 0x80;
    make_transparent_clut(name, locals, locals + 4, locals + 8, locals + 0x10, locals + 0xc);
    font_glyph_set_image_format(self, word_at(m_, locals), word_at(m_, locals + 4), word_at(m_, locals + 8), x, y, word_at(m_, locals + 0xc), word_at(m_, locals + 0x10));
    if (vram != 0) {
        // 0x00457130: nothing
    }
    const Word depth = word_at(m_, locals + 0xc);
    if (depth == 8) {
        // Get256SlotNumber(clut, 8) is 0
    }
    return self;
}

// 0x00457520 (thiscall, one argument): the deleting destructor of the bitmap image
Word RenderKit::bmp_image_scalar_delete(Word self, Word flags) {
    bmp_image_destruct(self);
    if ((flags & 1) != 0) {
        engine(0x00466bb0, {self});
    }
    return self;
}

// 0x00457540 (thiscall: BMP data, flag, x, y): the bitmap image of an already loaded BMP (4 bits: its header, else the 8-bit loader)
Word RenderKit::bmp_data_image_construct(Word self, Word data, Word, Word x, Word y) {
    sliced_construct(self);
    render_image_bmp_at(m_, self).set_field_0(static_cast<Word>(kBmpVtable));
    const Word depth = bmp_depth(data);
    const Word locals = kScratch + 0x80;
    if (depth == 4) {
        bmp_load(data, locals, locals + 4, locals + 8, locals + 0x10, 0, false);
    } else {
        bmp_load(data, locals, locals + 4, locals + 8, locals + 0x10, 0, true);
    }
    font_glyph_set_image_format(self, word_at(m_, locals), word_at(m_, locals + 4), word_at(m_, locals + 8), x, y, depth, word_at(m_, locals + 0x10));
    return self;
}

// 0x00457660 (fastcall): the "VRAM based sprite" is not supported: the constructor asserts
Word RenderKit::vram_sprite_construct(Word self) {
    sliced_construct(self);
    set_word_at(m_, self, kBmpVtable);
    check(false, 0x0052da8c);
    return self;
}

// 0x004576f0 (fastcall)
void RenderKit::bmp_image_destruct(Word self) {
    render_image_bmp_at(m_, self).set_field_0(static_cast<Word>(kBmpVtable));
    sliced_remove_from_memory(self);
}

// 0x00457700 (thiscall, one argument; slot 2 of the bitmap image, in no symbol table; from the disassembly, milestone 10: the career's level pictures load through it):
// the image again from a BMP in memory (the 8-bit loader when the depth byte at +0x58 is 1, the 4-bit one when it is 2), the loaded size asserted to be the image's
// (+0x40, +0x42), the CLUT word at +0x5a and the palette bound. Retail's out-arguments are three stack locals and the argument's own slot (the CLUT), which keeps the
// data pointer when neither loader runs.
void RenderKit::bmp_image_reload(Word self, Word data) {
    const Word locals = kScratch + 0x80;
    set_word_at(m_, locals + 0x10, data);
    const std::uint32_t depth = render_image_bmp_at(m_, self).field_58();
    if (depth == 1 || depth == 2) {
        bmp_load(data, locals + 8, locals, locals + 4, locals + 0x10, 0, depth == 1);
    }
    check(render_image_bmp_at(m_, self).field_40() == s(word_at(m_, locals)), 0x0052dae4);
    check(render_image_bmp_at(m_, self).field_42() == s(word_at(m_, locals + 4)), 0x0052dab4);
    render_image_bmp_at(m_, self).set_field_5a(static_cast<std::uint16_t>(word_at(m_, locals + 0x10)));
    image_bind_palette(self, word_at(m_, locals + 8));
}

// 0x00457810 (fastcall): what the texture layer answers for the image's handle
Word RenderKit::image_texture_value(Word self) { return engine_value(seam::hal::D3DTex_GrayTexture, {word_at(m_, self + 0x24)}); }

// 0x00457820 / 0x00457830 (fastcall): the image's width / height in layout units (the Q12 scale applied)
Word RenderKit::image_scaled_width(Word self) { return u((static_cast<std::int16_t>(half_at(m_, self + 0x40)) * s(word_at(m_, self + 0xc))) >> 12); }
Word RenderKit::image_scaled_height(Word self) { return u((static_cast<std::int16_t>(half_at(m_, self + 0x42)) * s(word_at(m_, self + 0xc))) >> 12); }

// 0x00457870 (thiscall: four arguments of 0x00457420): the sprite loaded from a file; its vtable and a flag
Word RenderKit::sprite_construct(Word self, Word a0, Word a1, Word a2, Word a3) {
    bmp_image_construct(self, a0, a1, a2, a3);
    render_image_sprite_at(m_, self).set_field_0(static_cast<Word>(kSpriteVtable));
    render_image_sprite_at(m_, self).set_field_5c(1);
    return self;
}

// 0x004578a0 (thiscall, one argument) / 0x004578c0 (fastcall)
Word RenderKit::sprite_scalar_delete(Word self, Word flags) {
    sprite_destruct(self);
    if ((flags & 1) != 0) {
        engine(0x00466bb0, {self});
    }
    return self;
}
void RenderKit::sprite_destruct(Word self) {
    render_image_sprite_at(m_, self).set_field_0(static_cast<Word>(kSpriteVtable));
    bmp_image_destruct(self);
}

// 0x00457940 (thiscall: x, y, angle; from the disassembly, milestone 9: the main menu's wheel draws its sprites with it, and no symbol table had it): the sprite as
// a textured quad (0x30 bytes, code 0x2c) turned by `angle` (Q12, asserted 0..0xfff) about its anchor and scaled by its Q12 scale (+0xc). The x axis is the
// cosine and the negated sine times a mode factor (+0x5c: 0 is 0x1000, 1 is 0xa00; the other values read a stack word retail never wrote, 0 here), the y axis
// the sine and the cosine times the factor, each product shifted 12 as retail does it. The anchor is the sprite's left / centre / right (+0xa: 0, 1, else) and
// top / middle / bottom (+0xb) of its width and height (+0x40, +0x42). The corners in Q12 are taken to whole units, then to the display as every layout
// primitive is (screen_x / screen_y of their 16-bit values); u and v run over the whole bitmap. The colour is +7..+9, semi-transparency +4 (code bit 1),
// shading off unless +5 (bit 0), the texture page from +0x58 and +6, the palette +0x5a, the depth -1.0 and the image's texture record (+0x10) at +0x2c.
void RenderKit::sprite_draw_rotated(Word self, std::int32_t x, std::int32_t y, std::int32_t angle) {
    check(angle >= 0 && angle < 0x1000, 0x0052db10);
    const std::int32_t factor = render_image_sprite_at(m_, self).field_5c() == 0 ? 0x1000 : render_image_sprite_at(m_, self).field_5c() == 1 ? 0xa00 : 0;
    const std::int32_t cosine = math().cos_q12(angle);
    const std::int32_t sine = math().sin_q12(angle);
    const std::int32_t scale = s(render_image_sprite_at(m_, self).field_c());
    const std::int32_t x_cos = (cosine * scale) >> 12;                           // the x axis: (x_cos, x_sin)
    const std::int32_t x_sin = ((-(sine * factor) >> 12) * scale) >> 12;
    const std::int32_t y_sin = (sine * scale) >> 12;                             // the y axis: (y_sin, y_cos)
    const std::int32_t y_cos = (((cosine * factor) >> 12) * scale) >> 12;
    const std::int32_t width = static_cast<std::int16_t>(half_at(m_, self + 0x40));
    const std::int32_t height = static_cast<std::int16_t>(half_at(m_, self + 0x42));
    const std::uint8_t horizontal = render_image_sprite_at(m_, self).field_a();
    const std::uint8_t vertical = render_image_sprite_at(m_, self).field_b();
    const std::int32_t left = (horizontal != 0 ? width : 0) >> (horizontal == 1 ? 1 : 0);
    const std::int32_t top = (vertical != 0 ? height : 0) >> (vertical == 1 ? 1 : 0);
    const std::int32_t x0 = static_cast<std::int32_t>(u(x) << 12) - top * y_sin - left * x_cos;
    const std::int32_t y0 = static_cast<std::int32_t>(u(y) << 12) - top * y_cos - left * x_sin;
    const Word prim = global_word(m_, kCursor);
    if (prim > global_word(m_, kLimit)) {
        return;
    }
    set_global_word(m_, kCursor, prim + 0x30);
    const std::int32_t x2 = x0 + height * y_sin;
    const std::int32_t y2 = y0 + height * y_cos;
    const std::int32_t corners[4][2] = {{x0, y0}, {x0 + width * x_cos, y0 + width * x_sin}, {x2, y2}, {x2 + width * x_cos, y2 + width * x_sin}};
    set_byte_at(m_, prim + 0xc, 0);
    set_byte_at(m_, prim + 0xd, 0);
    set_byte_at(m_, prim + 0x14, byte_at(m_, self + 0x40) - 1u);
    set_byte_at(m_, prim + 0x15, 0);
    set_byte_at(m_, prim + 0x1c, 0);
    set_byte_at(m_, prim + 0x1d, byte_at(m_, self + 0x42) - 1u);
    set_byte_at(m_, prim + 0x24, byte_at(m_, self + 0x40) - 1u);
    set_byte_at(m_, prim + 0x25, byte_at(m_, self + 0x42) - 1u);
    set_byte_at(m_, prim + 4, render_image_sprite_at(m_, self).field_7());
    set_byte_at(m_, prim + 5, render_image_sprite_at(m_, self).field_8());
    set_byte_at(m_, prim + 6, render_image_sprite_at(m_, self).field_9());
    Word code = 0x2c + (render_image_sprite_at(m_, self).field_4() != 0 ? 2u : 0u);
    code = render_image_sprite_at(m_, self).field_5() == 0 ? (code | 1u) : (code & ~1u);
    set_byte_at(m_, prim + 7, code);
    set_half_at(m_, prim + 0x16, ((0xff00u - (u(render_image_sprite_at(m_, self).field_58()) << 7)) & 0x180u) | ((render_image_sprite_at(m_, self).field_6() & 3u) << 5));
    set_half_at(m_, prim + 0xe, render_image_sprite_at(m_, self).field_5a());
    for (Word k = 0; k < 4; ++k) {
        const Word at = prim + 8 + 8 * k;
        set_half_at(m_, at, screen_x(static_cast<std::int16_t>(corners[k][0] >> 12)));
        set_half_at(m_, at + 2, screen_y(static_cast<std::int16_t>(corners[k][1] >> 12)));
    }
    set_word_at(m_, prim + 0x28, 0xbf800000);
    set_word_at(m_, prim + 0x2c, self + 0x10);
    link_primitive(prim, false);
}

// 0x00457e10 (cdecl: material checksum, BMP data, name): loads a BMP into a scene material's texture; an 8-bit one may be flagged for the career cheat that shows the transparent texels
void RenderKit::load_bmp_into_material(Word checksum, Word data, Word name) {
    check(data != 0, 0x0052dbf4);
    const Word material = engine_value(0x004b2030, {checksum});
    if (material == 0) {
        log_line(0x0052dbd8);
        return;
    }
    const Word depth = bmp_depth(data);
    if (depth == 8) {
        const bool cheat = (engine_value(0x00416980, {0xd}) & 0xff) != 0 && global_byte(m_, kCheatFlag) != 0;
        engine(0x004d8c60, {word_at(m_, material + 0x14), data, material, name, 1, cheat ? 1u : 0u});
        return;
    }
    if (bmp_depth(data) == 4) {
        log_line(0x0052dba8);
        const bool cheat = (engine_value(0x00416980, {0xd}) & 0xff) != 0 && global_byte(m_, kCheatFlag) != 0;
        engine(0x004d8c60, {word_at(m_, material + 0x14), data, material, name, 2, cheat ? 1u : 0u});
    }
}

// 0x00457f30 (cdecl: region): releases a region's textures: for every material of every model of the region, one use less and the palette cleared to black
void RenderKit::decrement_region_textures(Word region) {
    check(s(region) >= 0 && s(region) <= 0x14, 0x0052dce4);
    const Word record = kRegionNames + region * 0x44;
    check(byte_at(m_, record) != 0, 0x0052dcc8);
    check(byte_at(m_, kRegionUsable + region * 0x44) != 0, 0x0052dc90);
    const Word raw = word_at(m_, kRegionRaw + region * 0x44);
    if (byte_at(m_, kRegionUsable + region * 0x44) == 2) {
        return;
    }
    const Word models = word_at(m_, raw + 0xc + word_at(m_, raw + 8) * 0x24);
    check(models < 10000, 0x0052dc5c);
    const Word tables = engine_value(0x004b20f0, {raw});
    Word at = tables + 4 + models * 4;
    Word count = word_at(m_, at - 4);
    check(count < 0x3e9, 0x0052dc3c);
    for (; count != 0; --count) {
        const Word entry = word_at(m_, at);
        check(half_at(m_, entry + 0x10) < 10, 0x0052dc24);
        check(half_at(m_, entry + 0x10) != 0, 0x0052dc04);
        clear_material_palette(word_at(m_, entry + 0x18), 0xff, 0, 0);
        at += 4;
    }
}

// 0x004581a0 (cdecl: material checksum, red, green, blue): fills the material's palette row in VRAM with one colour (for a 16-colour palette the colour is inverted)
void RenderKit::clear_material_palette(Word checksum, Word red, Word green, Word blue) {
    const Word material = engine_value(0x004b2030, {checksum});
    if (material == 0) {
        log_line(0x0052dbd8);
        return;
    }
    const Word node = word_at(m_, material + 0x1c);
    const Word rect = kScratch;
    if ((byte_at(m_, node + 3) & 1) == 0) {
        set_half_at(m_, rect, global_word(m_, kPal256X));
        set_half_at(m_, rect + 2, byte_at(m_, node + 2) + global_word(m_, kPal256Y));
        set_half_at(m_, rect + 4, 0x100);
    } else {
        set_half_at(m_, rect, global_word(m_, kPal16X));
        set_half_at(m_, rect + 4, 0x10);
        set_half_at(m_, rect + 2, byte_at(m_, node + 2) + global_word(m_, kPal16Y));
        red = (red & ~0xffu) | ((0xffu - red) & 0xff);
        green = (green & ~0xffu) | ((0xffu - green) & 0xff);
        blue = (blue & ~0xffu) | ((0xffu - blue) & 0xff);
    }
    set_half_at(m_, rect + 6, 1);
    engine(0x004e8730, {rect, red, green, blue}, bytes_text(rect, 8));
}

}  // namespace opentony::runtime::renderkit
