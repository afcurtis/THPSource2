#include "ddraw_surface.h"

#include <algorithm>
#include <cstdio>
#include <vector>

// Pixel rules for the stand-in DirectDraw surfaces (ddraw_surface.h).

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
    if (!condition) {
        std::fprintf(stderr, "ddraw_surface_test.cpp:%d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

void test_pitch() {
    CHECK(ot_ddraw_pitch(4, 2) == 8);   // 8 bytes already a multiple of 8
    CHECK(ot_ddraw_pitch(3, 2) == 8);   // 6 bytes rounds up to 8
    CHECK(ot_ddraw_pitch(64, 2) == 128);
    CHECK(ot_ddraw_pitch(1, 4) == 8);   // 4 bytes rounds up to 8
    CHECK(ot_ddraw_pitch(256, 4) == 1024);
}

void test_pixel_format() {
    CHECK(ot_ddraw_pixel_format(16, 0x7c00u, 0x03e0u, 0x001fu, 0x8000u) == OT_HAL_PIXEL_ARGB1555);
    CHECK(ot_ddraw_pixel_format(16, 0x0f00u, 0x00f0u, 0x000fu, 0xf000u) == OT_HAL_PIXEL_ARGB4444);
    CHECK(ot_ddraw_pixel_format(16, 0xf800u, 0x07e0u, 0x001fu, 0u) == OT_HAL_PIXEL_RGB565);
    CHECK(ot_ddraw_pixel_format(32, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0u) == OT_HAL_PIXEL_ARGB8888);
    CHECK(ot_ddraw_pixel_format(24, 0xff0000u, 0x00ff00u, 0x0000ffu, 0u) == 0);
    CHECK(ot_ddraw_pixel_format(16, 0xfc00u, 0x03f0u, 0x000fu, 0u) == 0); // not a supported 5-6-5 order
}

void test_fill() {
    // A 16-bit surface: retail's fixed 0xFF000000 truncates to 0x0000 (black).
    const int32_t pitch16 = ot_ddraw_pitch(3, 2); // 6 bytes rounds up to 8
    std::vector<uint16_t> pixels16(static_cast<size_t>(pitch16 / 2) * 2, 0xdead);
    ot_ddraw_fill(pixels16.data(), pitch16, 3, 2, 2, 0xff000000u);
    for (int32_t row = 0; row < 2; ++row) {
        for (int32_t x = 0; x < 3; ++x) {
            CHECK(pixels16[static_cast<size_t>(row) * (pitch16 / 2) + static_cast<size_t>(x)] == 0);
        }
    }
    // The same constant on a 32-bit surface stays opaque black.
    const int32_t pitch32 = ot_ddraw_pitch(3, 4); // 12 bytes, already a multiple of 8... rounds to 16
    std::vector<uint32_t> pixels32(static_cast<size_t>(pitch32 / 4) * 2, 0xdeadbeefu);
    ot_ddraw_fill(pixels32.data(), pitch32, 3, 2, 4, 0xff000000u);
    for (int32_t row = 0; row < 2; ++row) {
        for (int32_t x = 0; x < 3; ++x) {
            CHECK(pixels32[static_cast<size_t>(row) * (pitch32 / 4) + static_cast<size_t>(x)] == 0xff000000u);
        }
    }
    // A non-black fill and a pitch wider than the width (row padding untouched).
    uint16_t padded[2 * 4] = {0xdead, 0xdead, 0xdead, 0xdead, 0xdead, 0xdead, 0xdead, 0xdead};
    ot_ddraw_fill(padded, 4 * 2, 3, 2, 2, 0x1234u);
    CHECK(padded[0] == 0x1234u && padded[1] == 0x1234u && padded[2] == 0x1234u && padded[3] == 0xdeadu);
    CHECK(padded[4] == 0x1234u && padded[5] == 0x1234u && padded[6] == 0x1234u && padded[7] == 0xdeadu);
}

void test_copy() {
    uint16_t src[3 * 2] = {1, 2, 3, 4, 5, 6};
    uint16_t dest[4 * 2] = {0, 0, 0, 0, 0, 0, 0, 0};
    ot_ddraw_copy(dest, 4 * 2, src, 3 * 2, 3, 2, 2);
    CHECK(dest[0] == 1 && dest[1] == 2 && dest[2] == 3 && dest[3] == 0);
    CHECK(dest[4] == 4 && dest[5] == 5 && dest[6] == 6 && dest[7] == 0);
}

void test_stretch_identity() {
    // Same size both ways: a stretch degenerates to a copy.
    uint16_t src[2 * 2] = {10, 20, 30, 40};
    uint16_t dest[2 * 2] = {0, 0, 0, 0};
    ot_ddraw_stretch(dest, 2 * 2, 2, 2, src, 2 * 2, 2, 2, 2);
    CHECK(dest[0] == 10 && dest[1] == 20 && dest[2] == 30 && dest[3] == 40);
}

void test_stretch_up() {
    // 2x2 -> 4x4: each source texel covers a 2x2 block (nearest, step 0.5).
    uint16_t src[2 * 2] = {1, 2, 3, 4};
    uint16_t dest[4 * 4];
    std::fill(dest, dest + 16, uint16_t(0xffff));
    ot_ddraw_stretch(dest, 4 * 2, 4, 4, src, 2 * 2, 2, 2, 2);
    const uint16_t expected[4 * 4] = {1, 1, 2, 2, 1, 1, 2, 2, 3, 3, 4, 4, 3, 3, 4, 4};
    for (int i = 0; i < 16; ++i) {
        CHECK(dest[i] == expected[i]);
    }
}

void test_stretch_down() {
    // The bottom row of a 480-line image stretched onto a 256-line texture
    // tile (224 lines onto 256, per D3DSprite_BuildTiles): every destination
    // row must land on a valid source row.
    std::vector<uint16_t> src(1 * 224, 7);
    std::vector<uint16_t> dest(1 * 256, 0);
    ot_ddraw_stretch(dest.data(), 2, 1, 256, src.data(), 2, 1, 224, 2);
    for (uint16_t value : dest) {
        CHECK(value == 7);
    }
}

void test_convert_from_argb8888_565() {
    // 0xFF804020 (opaque, r=0x80 g=0x40 b=0x20) narrowed to 5-6-5: r>>3=0x10,
    // g>>2=0x10, b>>3=0x04.
    const uint32_t src[2] = {0xff804020u, 0x00000000u};
    uint16_t dest[2] = {0xdead, 0xdead};
    ot_ddraw_convert_from_argb8888(dest, 2 * 2, 16, 0xf800u, 0x07e0u, 0x001fu, 0u, src, 2 * 4, 2, 1);
    CHECK(dest[0] == ((0x10u << 11) | (0x10u << 5) | 0x04u));
    CHECK(dest[1] == 0);
}

void test_convert_from_argb8888_1555() {
    // Alpha becomes the top bit: opaque -> 1, transparent -> 0.
    const uint32_t src[2] = {0xff808080u, 0x00808080u};
    uint16_t dest[2] = {0, 0};
    ot_ddraw_convert_from_argb8888(dest, 2 * 2, 16, 0x7c00u, 0x03e0u, 0x001fu, 0x8000u, src, 2 * 4, 2, 1);
    CHECK((dest[0] & 0x8000u) != 0);
    CHECK((dest[1] & 0x8000u) == 0);
    CHECK((dest[0] & 0x7fffu) == (dest[1] & 0x7fffu)); // same colour, only alpha differs
}

void test_convert_from_argb8888_32bit_identity() {
    // Same layout both ways (0xAARRGGBB in, 0xAARRGGBB masks out): exact copy.
    const uint32_t src[2] = {0x11223344u, 0xaabbccddu};
    uint32_t dest[2] = {0, 0};
    ot_ddraw_convert_from_argb8888(
        dest, 2 * 4, 32, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0xff000000u, src, 2 * 4, 2, 1);
    CHECK(dest[0] == src[0]);
    CHECK(dest[1] == src[1]);
}

void test_convert_from_argb8888_pitch() {
    // Source and dest pitches wider than the row (padding on both sides
    // untouched, each row read/written independently).
    const uint32_t src[2 * 3] = {0xff000000u, 0, 0, 0xff0000ffu, 0, 0}; // black row, then red-blue row, 2 cols wide
    uint16_t dest[2 * 3] = {0xdead, 0xdead, 0xdead, 0xdead, 0xdead, 0xdead};
    ot_ddraw_convert_from_argb8888(dest, 3 * 2, 16, 0xf800u, 0x07e0u, 0x001fu, 0u, src, 3 * 4, 1, 2);
    CHECK(dest[0] == 0);        // row 0: black
    CHECK(dest[1] == 0xdeadu);  // row 0 padding untouched
    CHECK(dest[3] == 0x001fu);  // row 1: blue
    CHECK(dest[4] == 0xdeadu);  // row 1 padding untouched
}

} // namespace

int main() {
    test_pitch();
    test_pixel_format();
    test_fill();
    test_copy();
    test_stretch_identity();
    test_stretch_up();
    test_stretch_down();
    test_convert_from_argb8888_565();
    test_convert_from_argb8888_1555();
    test_convert_from_argb8888_32bit_identity();
    test_convert_from_argb8888_pitch();
    if (failures == 0) {
        std::printf("ddraw_surface_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}
