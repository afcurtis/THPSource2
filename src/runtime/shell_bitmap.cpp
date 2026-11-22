#include "auto_views.hpp"
#include "shell_helpers.hpp"

#include <cstring>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

using namespace shellkit;

// shellutils.cpp (Phase 4.10i3): the 8-bit BMP loader (the one the shell uses, two dead variants that expand the picture to 16-bit pixels and upload it, a file wrapper) and the
// 2D screen primitives that sit between its functions (the flat quad, the draw area, the textured quad, the palette semi-transparency pass).

namespace {

constexpr std::uint32_t kPaletteX = 0x0056b6fc;         // where the next 256-colour palette goes in VRAM (x), and the row base (y)
constexpr std::uint32_t kPaletteY = 0x0056b2ec;
constexpr std::uint32_t kOverlaySlot = 0x00537b3c;      // the ordering-table slot of the next 2D primitive (counts down from 0xfff)
constexpr std::uint32_t kOverlayCount = 0x0056c57c;
constexpr std::uint32_t kViewportEdges = 0x00563a38;    // the pointer whose +8 word is the depth source
constexpr std::uint32_t kDepthThreshold = 0x00518484;   // a float, zero in the data

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t sx16(std::uint32_t v) { return static_cast<std::int16_t>(v & 0xffff); }
std::uint32_t div240(std::int32_t value, std::uint32_t height) { return (static_cast<std::uint32_t>(value) * height) / 240u; }
float as_float(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
std::uint32_t float_bits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

}  // namespace

std::string ShellHelpers::rect_text(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    std::string out;
    for (std::uint32_t v : {x, y, w, h}) {
        out.push_back(static_cast<char>(v & 0xff));
        out.push_back(static_cast<char>((v >> 8) & 0xff));
    }
    return out;
}

// ---- the 2D primitive slots -----------------------------------------------------------------------------------------------------------------------------------------

// A primitive is linked into the ordering table slot the counter names (the viewport's table is an array of eight-byte cells, the head of each at its start), and the counter moves
// down one. Its depth float is -1 when the renderer's main-list head and viewport table are the same cell, else the viewport's depth word over the slot (the slot times four)
// when that is above the threshold, else the depth word alone.
void ShellHelpers::link_overlay(std::uint32_t packet, std::uint32_t depth_offset) {
    const std::int32_t slot = global_int(m_, kOverlaySlot);
    const float scaled = static_cast<float>(static_cast<std::int32_t>(static_cast<std::uint32_t>(slot) * 4u));
    const std::uint32_t renderer = global_word(m_, kRenderer);
    if (word_at(m_, renderer + 0x88) == word_at(m_, renderer + 0x84)) {
        set_word_at(m_, packet + depth_offset, 0xbf800000);
    } else {
        const float source = static_cast<float>(half_at(m_, global_word(m_, kViewportEdges) + 8));
        set_word_at(m_, packet + depth_offset, float_bits(scaled > as_float(global_word(m_, kDepthThreshold)) ? source / scaled : source));
    }
    const std::uint32_t table = word_at(m_, global_word(m_, kRenderer) + 0x88);
    const std::uint32_t cell = table + static_cast<std::uint32_t>(slot) * 8u;
    set_word_at(m_, packet, word_at(m_, cell));
    set_word_at(m_, cell, packet);
    set_global_word(m_, kOverlaySlot, static_cast<std::uint32_t>(slot - 1));
}

// 0x004b0600: a flat quad (code 0x28, colour r g b) over the rectangle (x, y, w, h) of the 512x240 screen
void ShellHelpers::screen_fill_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    const std::uint32_t packet = global_word(m_, kArenaCursor);
    if (packet > global_word(m_, kArenaEnd)) {
        return;
    }
    set_global_word(m_, kArenaCursor, packet + 0x1c);
    set_byte_at(m_, packet + 5, g);
    set_byte_at(m_, packet + 4, r);
    set_byte_at(m_, packet + 6, b);
    set_byte_at(m_, packet + 7, 0x28);
    const std::uint32_t width = global_word(m_, kScreenWidth), height = global_word(m_, kScreenHeight);
    auto across = [&](std::uint32_t v) { return (u(sx16(v)) * width >> 9) & 0xffff; };
    auto down = [&](std::uint32_t v) { return div240(sx16(v), height) & 0xffff; };
    const std::uint32_t right = x + w, bottom = y + h;
    set_half_at(m_, packet + 0x08, across(x));
    set_half_at(m_, packet + 0x0a, down(y));
    set_half_at(m_, packet + 0x0c, across(right));
    set_half_at(m_, packet + 0x0e, down(y));
    set_half_at(m_, packet + 0x10, across(x));
    set_half_at(m_, packet + 0x12, down(bottom));
    set_half_at(m_, packet + 0x14, across(right));
    set_half_at(m_, packet + 0x16, down(bottom));
    link_overlay(packet, 0x18);
}

// 0x004b0800: a draw-area packet (0x14 bytes, written by the GPU packet routine from the rectangle)
void ShellHelpers::screen_set_draw_area(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    const std::uint32_t packet = global_word(m_, kArenaCursor);
    if (packet > global_word(m_, kArenaEnd)) {
        return;
    }
    engine(0x004e87f0, {packet, kScratchLine}, rect_text(x, y, w, h));
    set_global_word(m_, kArenaCursor, packet + 0x14);
    link_overlay(packet, 0x10);
}

// 0x004b0950
void ShellHelpers::screen_reset_primitives() {
    set_global_word(m_, kOverlayCount, 0);
    set_global_word(m_, kOverlaySlot, 0xfff);
}

// 0x004b0970: a textured quad drawn at once. The record at `rect` holds the texture position (+0 x and the page's column, +2 y and its row), the width (+4) and the height (+6).
// A 16-bit texture (`depth` 0x10) uses its size as it is; any other depth is an 8-bit one whose x extent doubles, and takes the clut word. The tag, the clut of the 16-bit
// form and the two pad words of the packet are stack leftovers in retail: the text leaves them zero.
void ShellHelpers::screen_draw_textured_rect(std::uint32_t rect, std::uint32_t x, std::uint32_t y, std::uint32_t depth, std::uint32_t clut) {
    std::uint8_t p[0x28] = {};
    const std::uint32_t r0 = half_at(m_, rect), r2 = half_at(m_, rect + 2), r4 = half_at(m_, rect + 4), r6 = half_at(m_, rect + 6);
    const bool full = depth == 0x10;
    auto put16 = [&](std::uint32_t at, std::uint32_t v) {
        p[at] = static_cast<std::uint8_t>(v);
        p[at + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    p[7] = 0x2d;
    const std::uint32_t right = (x + r4) & 0xffff, bottom = (y + r6) & 0xffff;
    std::uint32_t xr = right;
    std::uint8_t u0 = static_cast<std::uint8_t>(r0 & 0x3f);
    std::uint8_t u1 = static_cast<std::uint8_t>(u0 + r4);
    const std::uint8_t v0 = static_cast<std::uint8_t>(r2);
    const std::uint8_t v1 = static_cast<std::uint8_t>(r6 + v0);
    std::uint16_t page;
    if (full) {
        std::uint16_t a = static_cast<std::uint16_t>((r0 & 0x3c0) | 0x4000);
        a = static_cast<std::uint16_t>(static_cast<std::int16_t>(a) >> 2);
        a = static_cast<std::uint16_t>(a | (r2 & 0x100));
        a = static_cast<std::uint16_t>(static_cast<std::int16_t>(a) >> 4);
        page = static_cast<std::uint16_t>(a | ((r2 & 0x200) << 2));
    } else {
        xr = (x + r4 * 2) & 0xffff;                                   // (the width doubles: `shl si,1` then the add)
        u0 = static_cast<std::uint8_t>(u0 << 1);
        u1 = static_cast<std::uint8_t>(static_cast<std::uint8_t>((r0 & 0x3f) + r4) << 1);
        std::uint16_t a = static_cast<std::uint16_t>((r0 & 0x3c0) | 0x2000);
        a = static_cast<std::uint16_t>(static_cast<std::int16_t>(a) >> 2);
        a = static_cast<std::uint16_t>(a | (r2 & 0x100));
        a = static_cast<std::uint16_t>(static_cast<std::int16_t>(a) >> 4);
        page = static_cast<std::uint16_t>(a | ((r2 & 0x200) << 2));
        put16(0x0e, clut);
    }
    put16(0x08, x);
    put16(0x18, x);
    put16(0x10, xr);
    put16(0x20, xr);
    put16(0x0a, y);
    put16(0x12, y);
    put16(0x1a, bottom);
    put16(0x22, bottom);
    p[0x0c] = u0;
    p[0x1c] = u0;
    p[0x14] = u1;
    p[0x24] = u1;
    p[0x0d] = v0;
    p[0x15] = v0;
    p[0x1d] = v1;
    p[0x25] = v1;
    put16(0x16, page);
    engine(0x004e87e0, {kScratchLine}, std::string(reinterpret_cast<const char*>(p), sizeof p));
    engine(0x004e8740, {0});
}

// ---- the 8-bit BMP loader --------------------------------------------------------------------------------------------------------------------------------------------

// The palette, as the loader converts it: each entry is blue, green, red, pad. The bytes are sign-extended to 16 bits before they are compared, so a channel at 0x80 or above
// counts as "above 0xf7": an entry with both blue and red high and green zero is the transparent colour (stored 0, its index remembered); the rest are shifted down three
// bits (the sign extension leaves the high channels with six bits of their own and a bit pattern that overflows into the next channel) and packed blue-green-red; an entry
// that comes out all zero becomes 0x8000 (black that is not transparent).
ShellHelpers::BmpPalette ShellHelpers::bmp_palette(std::uint32_t bmp) {
    BmpPalette palette;
    palette.count = half_at(m_, bmp + 0x2e);
    if (palette.count == 0) {
        palette.count = 0x100;
    }
    std::uint32_t entry = bmp + 0x36;
    for (std::uint32_t i = 0; i < palette.count; ++i, entry += 4) {
        auto channel = [&](std::uint32_t at) { return static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::int8_t>(byte_at(m_, at)))); };
        std::uint16_t c = channel(entry), g = channel(entry + 1), r = channel(entry + 2);
        if (c > 0xf7 && g == 0 && r > 0xf7) {
            palette.transparent = i;
            palette.colour[i] = g;
            continue;
        }
        c >>= 3;
        g >>= 3;
        r >>= 3;
        if (c == 0 && g == 0 && r == 0) {
            palette.colour[i] = 0x8000;
        } else {
            palette.colour[i] = static_cast<std::uint16_t>((((static_cast<std::uint32_t>(c) << 5) + g) << 5) + r);
        }
    }
    return palette;
}

// The rows of the BMP (the first row in the file is the bottom one) go into the buffer from row `first` upwards: each row's `width` bytes, then `pad` bytes of the fill value,
// `copies` times over (the tall variant writes every row twice, one above the other).
void ShellHelpers::bmp_rows(std::uint32_t source, std::uint32_t first, std::uint32_t width, std::uint32_t pad, std::uint32_t height, std::uint32_t fill, std::uint32_t copies) {
    const std::uint32_t stride = width + pad;
    std::uint32_t at = first;
    for (std::int32_t row = static_cast<std::int32_t>(height); row > 0; --row) {
        for (std::uint32_t copy = 0; copy < copies; ++copy) {
            for (std::uint32_t k = 0; k < width; ++k) {
                set_byte_at(m_, at + k, byte_at(m_, source + k));
            }
            for (std::uint32_t k = 0; k < pad; ++k) {
                set_byte_at(m_, at + width + k, fill);
            }
            at -= stride;
        }
        source += stride;
    }
}

// The pixels become 16-bit words in place, last first: word j is the palette entry the byte j named. The index is a signed byte, so an index at 0x80 or above reads below the
// palette array, in whatever the stack held (the dead variants never meet one in the game's own files); the port reads those as zero.
void ShellHelpers::bmp_expand(std::uint32_t buffer, std::uint32_t count, const BmpPalette& palette) {
    for (std::int32_t j = static_cast<std::int32_t>(count) - 1; j >= 0; --j) {
        const std::int8_t index = sbyte_at(m_, buffer + static_cast<std::uint32_t>(j));
        set_half_at(m_, buffer + 2u * static_cast<std::uint32_t>(j), index >= 0 ? palette.colour[index] : 0);
    }
}

// 0x004afd40: returns 0 (with the buffer pointer cleared) when the picture is not 8-bit
std::uint32_t ShellHelpers::load_8bit_bmp(std::uint32_t bmp, std::uint32_t out_buffer, std::uint32_t out_stride, std::uint32_t out_height, std::uint32_t out_clut) {
    set_word_at(m_, out_buffer, 0);
    const std::uint32_t width = half_at(m_, bmp + 0x12), height = half_at(m_, bmp + 0x16);
    if (half_at(m_, bmp + 0x1c) != 8) {
        s_.note(kSvcPrintfLog, {0x00537b40});
        return 0;
    }
    const BmpPalette palette = bmp_palette(bmp);
    const std::uint32_t x = global_word(m_, kPaletteX);
    const std::uint32_t y = engine_value(0x0048a590) + global_word(m_, kPaletteY);
    // the palette goes to VRAM as a 256x1 rectangle: the text is the rectangle then the 512 bytes, the entries past the colour count blank
    std::string text = rect_text(x, y, 0x100, 1);
    for (std::uint32_t i = 0; i < 0x100; ++i) {
        const std::uint32_t colour = i < palette.count ? palette.colour[i] : 0;
        text.push_back(static_cast<char>(colour & 0xff));
        text.push_back(static_cast<char>(colour >> 8));
    }
    engine(0x004e8770, {kScratchLine, kScratchLine}, text);
    engine(0x004e8740, {0});
    std::uint32_t pad = width & 3;
    if (pad > 0) {
        pad = 4 - pad;
    }
    const std::uint32_t stride = width + pad;
    s_.note(kAllocBuffer, {height * stride, 1, 1, 0});
    const std::uint32_t buffer = u(s_.value(kAllocBuffer));
    set_word_at(m_, out_buffer, buffer);
    bmp_rows(bmp + 0x36 + palette.count * 4, buffer + (height - 1) * stride, width, pad, height, palette.transparent, 1);
    s_.note(kMemDeleteService, {bmp});
    set_word_at(m_, out_stride, stride);
    set_word_at(m_, out_height, height);
    set_half_at(m_, out_clut, (y << 6) | ((static_cast<std::int32_t>(x) >> 4) & 0x3f));
    return 1;
}

// 0x004affe0: the file by name (the opener answers the size, which must be positive), read into a buffer the loader then frees; true even when the loader refused the picture
std::uint32_t ShellHelpers::load_8bit_bmp_file(std::uint32_t name, std::uint32_t out_buffer, std::uint32_t out_stride, std::uint32_t out_height, std::uint32_t out_clut) {
    const std::int32_t size = static_cast<std::int32_t>(engine_value(0x00449030, {name}, m_.cstring(name, 0x40)));
    if (size <= 0) {
        s_.note(kSvcPrintfLog, {0x0052d9a8});
        return 0;
    }
    s_.note(kAllocBuffer, {u(size), 1, 1, 0});
    const std::uint32_t buffer = u(s_.value(kAllocBuffer));
    if (buffer == 0) {
        s_.note(kSvcPrintfLog, {0x0052d958});
        return 0;
    }
    engine(0x00449230, {buffer});
    // (the file read writes the picture into the buffer: the replay hands the bytes back as scripted dwords)
    for (std::int32_t k = 0; !s_.live() && k < size; k += 4) {
        set_word_at(m_, buffer + static_cast<std::uint32_t>(k), u(static_cast<std::int32_t>(s_.value(engine_name(0x00449230)))));
    }
    engine(0x00449660);
    load_8bit_bmp(buffer, out_buffer, out_stride, out_height, out_clut);
    return 1;
}

// 0x004b0320: the BMP as 16-bit pixels (palette entries, then one 2-byte word per pixel in place) uploaded at (0, 0) with the picture's own width and height
void ShellHelpers::bmp8_upload(std::uint32_t bmp) {
    const std::uint32_t width = half_at(m_, bmp + 0x12), height = half_at(m_, bmp + 0x16);
    if (half_at(m_, bmp + 0x1c) != 8) {
        s_.note(kSvcPrintfLog, {0x00537b6c});
        return;
    }
    const BmpPalette palette = bmp_palette(bmp);
    std::uint32_t pad = width & 3;
    if (pad > 0) {
        pad = 4 - pad;
    }
    const std::uint32_t stride = width + pad;
    const std::uint32_t count = height * stride;
    s_.note(kAllocBuffer, {count * 2, 1, 1, 0});
    const std::uint32_t buffer = u(s_.value(kAllocBuffer));
    bmp_rows(bmp + 0x36 + palette.count * 4, buffer + (height - 1) * stride, width, pad, height, palette.transparent, 1);
    s_.note(kMemDeleteService, {bmp});
    bmp_expand(buffer, count, palette);
    engine(0x004e8770, {kScratchLine, buffer}, rect_text(0, 0, stride, height));
    engine(0x004e8740, {0});
    s_.note(kMemDeleteService, {buffer});
}

// 0x004b0060: the same with every row written twice (the picture is twice as tall), into a buffer of four bytes a pixel, with a wait for the GPU before the upload and a vertical
// sync after
void ShellHelpers::bmp8_upload_tall(std::uint32_t bmp) {
    const std::uint32_t width = half_at(m_, bmp + 0x12), height = half_at(m_, bmp + 0x16);
    if (half_at(m_, bmp + 0x1c) != 8) {
        s_.note(kSvcPrintfLog, {0x00537b6c});
        return;
    }
    const BmpPalette palette = bmp_palette(bmp);
    std::uint32_t pad = width & 3;
    if (pad > 0) {
        pad = 4 - pad;
    }
    const std::uint32_t stride = width + pad;
    s_.note(kAllocBuffer, {height * stride * 4, 1, 1, 0});
    const std::uint32_t buffer = u(s_.value(kAllocBuffer));
    bmp_rows(bmp + 0x36 + palette.count * 4, buffer + (2 * height - 1) * stride, width, pad, height, palette.transparent, 2);
    s_.note(kMemDeleteService, {bmp});
    bmp_expand(buffer, 2 * height * stride, palette);
    engine(0x004e8740, {0});
    engine(0x004e8770, {kScratchLine, buffer}, rect_text(0, 0, stride, 2 * height));
    engine(0x004e8740, {0});
    engine(0x004e8960, {0});
    s_.note(kMemDeleteService, {buffer});
}

// 0x004b0580: a 256-colour palette row read back from VRAM, every non-zero colour given the semi-transparency bit, written again
void ShellHelpers::clut_set_semi_transparent(std::uint32_t x, std::uint32_t y) {
    s_.note(kAllocBuffer, {0x200, 1, 1, 0});
    const std::uint32_t buffer = u(s_.value(kAllocBuffer));
    engine(0x004e8750, {kScratchLine, buffer}, rect_text(x, y, 0x100, 1));
    for (std::uint32_t k = 0; k < 0x200; k += 4) {
        set_word_at(m_, buffer + k, u(s_.value(engine_name(0x004e8750))));      // (the read fills the buffer: scripted dwords)
    }
    for (std::uint32_t k = 0; k < 0x100; ++k) {
        const std::uint32_t at = buffer + 2 * k;
        if (half_at(m_, at) != 0) {
            set_half_at(m_, at, half_at(m_, at) | 0x8000);
        }
    }
    std::string text = rect_text(x, y, 0x100, 1);
    for (const std::uint8_t byte : m_.bytes(buffer, 0x200)) {
        text.push_back(static_cast<char>(byte));
    }
    engine(0x004e8770, {kScratchLine, buffer}, text);
    s_.note(kMemDeleteService, {buffer});
}

}  // namespace opentony::runtime::frontend
