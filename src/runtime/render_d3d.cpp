#include "auto_views.hpp"
#include "render_kit.hpp"

// Phase 4.15e: the D3D layer's start-up and sprite quad (`d3dmodel.cpp`, `d3d.cpp`, 0x004d2da0, 0x004d3f50, 0x004d7990): the table of per-face depth offsets the model renderer pushes polygons back by,
// the 256 x 256 fog tables (alpha, red, green and blue of a fog colour at each distance / density pair) and the four vertices of a sprite from its rectangle list. The floating point sequences are
// replayed at the precision control the forced call ran under (MathKit's precision), every operation rounded as the x87 does.

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kPushbacks = 0x005808ac;              // 15000 dwords: the pushback table (cleared)
constexpr Word kPushbackDwords = 15000;

struct Store {
    Word address;
    Word value;
};

// the words the start-up stores (0x1000: 1.0, 0x10 / 0xfff0 / 0x100 and so on: small offsets), in the order the function stores them
constexpr Store kStartupWords[] = {
    {0x00580bf4, 0x1000}, {0x00580bf6, 0x1000}, {0x00580bf8, 0x1000}, {0x00581fd6, 0x1000}, {0x00582c9e, 0x10}, {0x00582ca0, 0x10},
    {0x00582caa, 0x10}, {0x00582cae, 0x10}, {0x00582cb0, 0x10}, {0x00582cb4, 0x10}, {0x00582cb8, 0x10}, {0x00582cbe, 0x10},
    {0x00582cc0, 0x10}, {0x00582cc2, 0x10}, {0x00582cc6, 0x10}, {0x00582ccc, 0x10}, {0x00582cce, 0x10}, {0x00582d3c, 0x10},
    {0x00582d4e, 0x10}, {0x00583d34, 0xffe0}, {0x0058405a, 0xffe0}, {0x005809bc, 0x20}, {0x005809be, 0x20}, {0x00580928, 0x20},
    {0x00580ba0, 0x20}, {0x00580bcc, 0xfff0}, {0x00580bce, 0xfff0}, {0x005808ee, 0x80}, {0x005808f8, 0x80}, {0x005809ba, 0x80},
    {0x0058211a, 0x100}, {0x00581e06, 0x40}, {0x00581fd0, 0x800}, {0x00581b9e, 0xf00}, {0x00581fd2, 0x100}, {0x00581b40, 0x100},
    {0x00581aba, 0x100}, {0x00581ff6, 0x40}, {0x005820ee, 0x40}, {0x00581e7c, 0x40}, {0x00581ddc, 0x40}, {0x00581e0c, 0x40},
    {0x00581e0e, 0x40}, {0x005837aa, 0x2000}, {0x00583d82, 0x80}, {0x00583ee8, 0x80}, {0x00583eea, 0x80}, {0x0058476e, 0xfff0},
    {0x00584772, 0xfff0}, {0x00584774, 0xfff0}, {0x00584776, 0xfff0}, {0x00584778, 0xfff0}, {0x005847b0, 0xfff0}, {0x005847d8, 0xfff0},
    {0x00584c8c, 0xfff8}, {0x00584c8e, 0xfff0}, {0x00584c90, 0xffe8}, {0x00584d46, 0xfff8}, {0x00584d48, 0xfff0}, {0x00584d4a, 0xffe8},
    {0x00584d4c, 0xfff8}, {0x00584d4e, 0xfff0}, {0x00584d50, 0xffe8}, {0x00584d56, 0xffe8}, {0x00586a02, 0x80}, {0x0058c50e, 0x80},
    {0x0058c570, 0x80}, {0x00584d54, 0xfff0}, {0x00584d58, 0x40}, {0x0058978a, 1}, {0x0058979c, 1}, {0x0058979e, 1},
    {0x0058978e, 1}, {0x00589798, 1}, {0x0058979a, 1}, {0x0058978c, 1}, {0x0058b5c0, 0x40}, {0x00584d52, 0xfff8},
    {0x00585bd4, 0x20}, {0x00584d5a, 0x50}, {0x00584d5c, 0x60}, {0x005848f2, 0x100}, {0x00585d08, 0x100}, {0x00585d1a, 0x100},
    {0x00585d20, 0x100}, {0x00585d22, 0x100}, {0x00585b4a, 0x100}, {0x00585c28, 0x100}, {0x00585c2a, 0x100}, {0x00585c2c, 0x100},
    {0x00585c2e, 0x100}, {0x00585c30, 0x100}, {0x00585c44, 0x100}, {0x00585c46, 0x100}, {0x00585c48, 0x100}, {0x00585c4c, 0x100},
    {0x005858a4, 0x100}, {0x005858a6, 0x100}, {0x005859f6, 0x100}, {0x00585a94, 0x100}, {0x00585ab4, 0x100}, {0x00585ab6, 0x100},
    {0x00585a26, 0x100}, {0x00586a1c, 0x100}, {0x00586916, 0x100}, {0x00586912, 0x100}, {0x00586c7e, 0x100}, {0x00586c96, 0x100},
    {0x00586696, 0x100}, {0x0058668a, 0x100}, {0x00586680, 0x100}, {0x00586ca0, 0x100}, {0x00586676, 0x100}, {0x00586882, 0x100},
    {0x00587a14, 0xffc0}, {0x0058b4c0, 0xffd0}, {0x0058b4c2, 0xffd0}, {0x0058b4c4, 0xffd0}, {0x0058b5ec, 0xffd0}, {0x0058b5e6, 0xffd0},
    {0x0058b5ee, 0xffd0}, {0x0058b4da, 0x100}, {0x0058b4d8, 0x100}, {0x0058b4d6, 0x100}, {0x0058c558, 0xffc0}, {0x0058c4e8, 0xffc0},
};

constexpr Word kFogColour = 0x00549e94;              // dword: the colour the tables were built for
constexpr Word kFogAlpha = 0x0059d340;
constexpr Word kFogBlue = 0x005dd740;
constexpr Word kFogRed = 0x0061d82c;
constexpr Word kFogGreen = 0x0065fc60;
constexpr Word kRenderPrimitive = 0x00560fd0;
constexpr Word kCurrentBuffer = 0x00560fd4;
constexpr Word kViewport = 0x00563a38;
constexpr Word kScaleX = 0x029da394;
constexpr Word kScaleY = 0x029da398;
constexpr Word kSoftwareRenderer = 0x029da3a0;
constexpr Word kSoftwareTexture = 0x006a04c8;

}  // namespace

// 0x004d2da0 D3DMODEL_Startup: clears the pushback table and sets the face depth offsets
void RenderKit::d3dmodel_startup() {
    for (Word k = 0; k < kPushbackDwords; ++k) {
        set_word_at(m_, kPushbacks + 4 * k, 0);
    }
    for (const Store& store : kStartupWords) {
        set_half_at(m_, store.address, store.value);
    }
}

// 0x004d3f50 D3D_SetupFog(colour) (cdecl): the four 256 x 256 tables of the fog for a colour (alpha in the top byte, red, green, blue); the same colour as last time does nothing. At alpha 2 the three colour
// channels follow the row alone. Only the entries the memory holds are written (a replay captures a few rows).
void RenderKit::d3d_setup_fog(Word colour) {
    if (colour == global_word(m_, kFogColour)) {
        return;
    }
    const std::uint32_t alpha = colour >> 24;
    set_global_word(m_, kFogColour, colour);
    const long double red = (colour >> 16) & 0xff;
    const long double green = (colour >> 8) & 0xff;
    const long double blue = colour & 0xff;
    const long double near_constant = float_at(0x00518d70);
    const long double step_constant = float_at(0x00518d74);
    auto clamp = [](std::int32_t value) { return static_cast<Word>(value) > 0xff ? 0xffu : static_cast<Word>(value); };
    auto round = [this](long double value) { return mathkit::x87::round_to(math().precision(), value); };
    for (Word row = 0; row < 0x100; ++row) {
        Word offset = row << 10;
        for (Word column = 0; column < 0x100; ++column, offset += 4) {
            const long double steps = round(static_cast<long double>(column) * step_constant);
            const long double span = round(near_constant - steps);
            const long double product = round(span * static_cast<long double>(row));
            std::int32_t a = mathkit::x87::ftol(product);
            std::int32_t r, g, b;
            if (alpha == 2) {
                r = g = b = mathkit::x87::ftol(static_cast<long double>(row));
            } else {
                r = mathkit::x87::ftol(round(round(steps * red) + product));
                g = mathkit::x87::ftol(round(round(steps * green) + product));
                b = mathkit::x87::ftol(round(round(steps * blue) + product));
            }
            const Word ca = clamp(a), cr = clamp(r), cg = clamp(g), cb = clamp(b);
            const std::pair<Word, Word> stores[] = {{kFogBlue + offset, cb}, {kFogAlpha + offset, ca << 24}, {kFogRed + offset, cr << 16}, {kFogGreen + offset, cg << 8}};
            for (const auto& [address, value] : stores) {
                if (m_.contains(address, 4)) {
                    set_word_at(m_, address, value);
                }
            }
        }
    }
}

// 0x004d7990 D3D_BuildSpriteQuad (thiscall: the sprite list; stack: the ordering table, an x and a y offset as floats): a quad of four vertices for each rectangle of the list (0x10 bytes each: left, top,
// right, bottom as integers) with the sprite's texture word, in the screen's pixels; linked into the table (in front, or behind for sprites whose word is not 0.9)
void RenderKit::d3d_build_sprite_quad(Word self, Word table, Word x_offset_bits, Word y_offset_bits) {
    if (global_word(m_, kSoftwareRenderer) != 0 && global_word(m_, kSoftwareTexture) == self) {
        return;
    }
    const std::int32_t count = s(word_at(m_, self + 0xc));
    if (count <= 0) {
        return;
    }
    auto round = [this](long double value) { return mathkit::x87::round_to(math().precision(), value); };
    const long double x_offset = float_from(x_offset_bits);
    const long double y_offset = float_from(y_offset_bits);
    Word rect_cursor = 0;
    Word info_cursor = 0;
    for (std::int32_t k = 0; k < count; ++k) {
        const Word prim = global_word(m_, kRenderPrimitive);
        set_global_word(m_, kRenderPrimitive, prim + 0xc0);
        for (Word offset = 0; offset < 0xc0; offset += 4) {
            set_word_at(m_, prim + offset, 0);
        }
        const Word rect = word_at(m_, self + 8) + rect_cursor;
        const float top = static_cast<float>(s(word_at(m_, rect + 4)));
        const float right = static_cast<float>(s(word_at(m_, rect + 8)));
        const float bottom = static_cast<float>(s(word_at(m_, rect + 0xc)));
        const Word word14 = word_at(m_, self + 0x14);
        set_word_at(m_, prim + 0x10, info_cursor + word_at(m_, self + 0x10));
        const Word texture = word_at(m_, info_cursor + word_at(m_, self + 0x10) + 0x14);
        const float x0 = static_cast<float>(round(static_cast<long double>(s(word_at(m_, rect))) + x_offset));
        const float y0 = static_cast<float>(round(static_cast<long double>(top) + y_offset));
        const float x1 = static_cast<float>(round(static_cast<long double>(right) + x_offset));
        const float y1 = static_cast<float>(round(static_cast<long double>(bottom) + y_offset));
        set_byte_at(m_, prim + 4, 0xff);
        set_byte_at(m_, prim + 5, 0xff);
        set_byte_at(m_, prim + 6, 0xff);
        set_word_at(m_, prim + 8, 0);
        set_byte_at(m_, prim + 7, 0xb0);
        set_word_at(m_, prim + 0x14, 4);
        set_word_at(m_, prim + 0x18, float_bits(x0));
        set_word_at(m_, prim + 0x1c, float_bits(y0));
        set_word_at(m_, prim + 0x20, word14);
        set_word_at(m_, prim + 0x24, 0x3f800000);
        set_word_at(m_, prim + 0x28, 0xffffffff);
        set_word_at(m_, prim + 0x2c, 0);
        set_word_at(m_, prim + 0x30, 0);
        set_word_at(m_, prim + 0x34, float_bits(x1));
        set_word_at(m_, prim + 0x38, float_bits(y0));
        set_word_at(m_, prim + 0x3c, word14);
        set_word_at(m_, prim + 0x40, 0x3f800000);
        set_word_at(m_, prim + 0x44, 0xffffffff);
        set_word_at(m_, prim + 0x48, 0x3f800000);
        set_word_at(m_, prim + 0x4c, 0);
        set_word_at(m_, prim + 0x50, float_bits(x1));
        set_word_at(m_, prim + 0x54, float_bits(y1));
        set_word_at(m_, prim + 0x58, word14);
        set_word_at(m_, prim + 0x5c, 0x3f800000);
        set_word_at(m_, prim + 0x60, 0xffffffff);
        set_word_at(m_, prim + 0x64, 0x3f800000);
        set_word_at(m_, prim + 0x68, 0x3f800000);
        set_word_at(m_, prim + 0x6c, float_bits(x0));
        set_word_at(m_, prim + 0x70, float_bits(y1));
        set_word_at(m_, prim + 0x74, word14);
        set_word_at(m_, prim + 0x78, 0x3f800000);
        set_word_at(m_, prim + 0x7c, 0xffffffff);
        set_word_at(m_, prim + 0x80, 0);
        set_word_at(m_, prim + 0x84, 0x3f800000);
        // the vertices in the display's pixels: the width and height of the display (as unsigned numbers) times the coordinate times a constant (two sets: textures with bit 3 are scaled differently)
        const bool wide = (byte_at(m_, texture + 0x10) & 8) != 0;
        const long double kx = float_at(wide ? 0x00519968 : 0x00519970);
        const long double ky = float_at(wide ? 0x00519964 : 0x0051996c);
        const long double width = global_word(m_, kScaleX);
        const long double height = global_word(m_, kScaleY);
        for (Word offset : {0x18u, 0x34u, 0x50u, 0x6cu}) {
            set_word_at(m_, prim + offset, float_bits(static_cast<float>(round(round(width * float_from(word_at(m_, prim + offset))) * kx))));
            set_word_at(m_, prim + offset + 4, float_bits(static_cast<float>(round(round(height * float_from(word_at(m_, prim + offset + 4))) * ky))));
        }
        const Word head = word_at(m_, global_word(m_, kCurrentBuffer) + 0x84);
        const Word viewport = global_word(m_, kViewport);
        const long double depth = half_at(m_, viewport + 8);
        if (word14 == 0x3f666666) {
            if (table == head) {
                set_word_at(m_, prim + 0xc, 0xbf800000);
            } else {
                set_word_at(m_, prim + 0xc, float_bits(static_cast<float>(depth)));
            }
            set_word_at(m_, prim, word_at(m_, table));
            set_word_at(m_, table, prim);
        } else {
            if (table == head) {
                set_word_at(m_, prim + 0xc, 0xbf800000);
            } else {
                set_word_at(m_, prim + 0xc, float_bits(static_cast<float>(round(depth * float_at(0x00518d64)))));
            }
            set_word_at(m_, prim, word_at(m_, table + 0x7ff8));
            set_word_at(m_, table + 0x7ff8, prim);
        }
        rect_cursor += 0x10;
        info_cursor += 0x2c;
    }
}

// 0x004d11d0 M3D_SubmitGeometryPackets(packets, count, light matrix, reset) (cdecl): the vertex colours of a model: each packet (8 bytes: a normal of three shorts and a flag word) is lit through the
// light matrix (the frame's, or the caller's when given) and the colour matrix plus the background colour of the GTE, clamped to 1.0 and written as a colour (red / green / blue bytes of 255 times
// the result, truncated) into the array at 0x0057e888; flag 1 also appends the colour to the list at the cursor 0x0058f324, flag 2 takes the colour from the table at 0x0056e874 by index instead.
// The float sequences are replayed at the precision control the call ran under.
void RenderKit::submit_geometry_packets(Word packets, std::int32_t count, Word light, Word reset) {
    constexpr Word kCursorAddress = 0x0058f324;
    constexpr Word kTable = 0x0056e874;
    constexpr Word kColours = 0x0057e888;
    auto r = [this](long double value) { return mathkit::x87::round_to(math().precision(), value); };
    auto f32 = [](long double value) { return static_cast<float>(value); };
    const long double q = float_at(0x00518910);
    const float zero = float_at(0x00518484);
    const float one = float_at(0x00518d70);
    const long double scale = float_at(0x00519938);
    float background[3];
    for (Word k = 0; k < 3; ++k) {
        background[k] = f32(r(static_cast<long double>(s(word_at(m_, 0x006a3e48 + 4 * k))) * q));
    }
    float colour[9];
    for (Word k = 0; k < 9; ++k) {
        colour[k] = f32(r(static_cast<long double>(static_cast<std::int16_t>(half_at(m_, 0x006a3ec8 + 2 * k))) * q));
    }
    float lighting[9];
    Word source = 0x006a3e80;
    if (light != 0) {
        if ((reset & 0xff) != 0) {
            set_global_word(m_, kCursorAddress, kTable);
        }
        source = light;
    } else {
        set_global_word(m_, kCursorAddress, kTable);
    }
    for (Word k = 0; k < 9; ++k) {
        lighting[k] = f32(r(static_cast<long double>(static_cast<std::int16_t>(half_at(m_, source + 2 * k))) * q));
    }
    if (count <= 0) {
        return;
    }
    Word output = kColours;
    Word packet = packets;
    for (std::int32_t k = 0; k < count; ++k, packet += 8, output += 4) {
        const std::int32_t flags = static_cast<std::int16_t>(half_at(m_, packet + 6));
        if ((flags & 2) != 0) {
            set_word_at(m_, output, word_at(m_, kTable + 4 * u(s(word_at(m_, packet)) >> 3)));
            continue;
        }
        const long double nx = r(static_cast<long double>(static_cast<std::int16_t>(half_at(m_, packet))) * q);
        const long double ny = r(static_cast<long double>(static_cast<std::int16_t>(half_at(m_, packet + 2))) * q);
        const long double nz = r(static_cast<long double>(static_cast<std::int16_t>(half_at(m_, packet + 4))) * q);
        auto row = [&](int a, int b, int c) {
            long double t = r(static_cast<long double>(lighting[c]) * nz);
            t = r(t + r(static_cast<long double>(lighting[b]) * ny));
            return r(t + r(static_cast<long double>(lighting[a]) * nx));
        };
        float l0 = f32(row(0, 1, 2));
        float l1 = f32(row(3, 4, 5));
        long double l2 = row(6, 7, 8);
        if (l0 < zero) {
            l0 = 0.0f;
        }
        if (l1 < zero) {
            l1 = 0.0f;
        }
        if (l2 < static_cast<long double>(zero)) {
            l2 = zero;
        }
        auto shade = [&](int a, int b, int c, float bg) {
            long double t = r(static_cast<long double>(colour[c]) * l2);
            t = r(t + r(static_cast<long double>(colour[b]) * static_cast<long double>(l1)));
            t = r(t + r(static_cast<long double>(colour[a]) * static_cast<long double>(l0)));
            return r(t + static_cast<long double>(bg));
        };
        float c0 = f32(shade(0, 1, 2, background[0]));
        float c1 = f32(shade(3, 4, 5, background[1]));
        long double c2 = shade(6, 7, 8, background[2]);
        if (!(c0 <= one)) {
            c0 = 1.0f;
        }
        if (!(c1 <= one)) {
            c1 = 1.0f;
        }
        if (!(c2 <= static_cast<long double>(one))) {
            c2 = one;
        }
        set_byte_at(m_, output + 2, u(mathkit::x87::ftol(r(static_cast<long double>(c0) * scale))));
        set_byte_at(m_, output + 1, u(mathkit::x87::ftol(r(static_cast<long double>(c1) * scale))));
        set_byte_at(m_, output, u(mathkit::x87::ftol(r(c2 * scale))));
        if ((flags & 1) != 0) {
            const Word cursor = global_word(m_, kCursorAddress);
            set_word_at(m_, cursor, word_at(m_, output));
            set_global_word(m_, kCursorAddress, cursor + 4);
        }
    }
}

}  // namespace opentony::runtime::renderkit
