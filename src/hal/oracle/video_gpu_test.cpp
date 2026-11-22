#include "video_gpu.h"

#include <cstdint>
#include <cstdio>
#include <vector>

// Pure helpers for the SDL_GPU video backend (video_gpu.h).

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
    if (!condition) {
        std::fprintf(stderr, "video_gpu_test.cpp:%d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

void test_expand() {
    CHECK(ot_gpu_expand5(0) == 0);
    CHECK(ot_gpu_expand5(31) == 255);
    CHECK(ot_gpu_expand5(16) == (16 * 255 + 15) / 31);
    CHECK(ot_gpu_expand6(0) == 0);
    CHECK(ot_gpu_expand6(63) == 255);
    CHECK(ot_gpu_expand4(0) == 0);
    CHECK(ot_gpu_expand4(15) == 255);
    CHECK(ot_gpu_expand4(1) == 17);
    CHECK(ot_gpu_expand1(0) == 0);
    CHECK(ot_gpu_expand1(1) == 255);
}

void test_convert_argb8888() {
    const uint32_t texels[2] = {0x80112233u, 0x00445566u};
    uint32_t out[2] = {0, 0};
    CHECK(ot_gpu_convert_row(OT_HAL_PIXEL_ARGB8888, texels, 2, 0, out) == 1);
    // 0xAARRGGBB -> memory order R,G,B,A (little-endian 0xAABBGGRR as a uint32).
    CHECK(out[0] == (0x11u | (0x22u << 8) | (0x33u << 16) | (0x80u << 24)));
    CHECK(out[1] == (0x44u | (0x55u << 8) | (0x66u << 16) | (0x00u << 24)));
}

void test_convert_argb1555() {
    // Alpha bit set, full red, no green, no blue.
    const uint16_t texel = static_cast<uint16_t>(0x8000u | (0x1fu << 10));
    uint32_t out = 0;
    CHECK(ot_gpu_convert_row(OT_HAL_PIXEL_ARGB1555, &texel, 1, 0, &out) == 1);
    CHECK((out & 0xffu) == 255u);         // red
    CHECK(((out >> 8) & 0xffu) == 0u);    // green
    CHECK(((out >> 16) & 0xffu) == 0u);   // blue
    CHECK(((out >> 24) & 0xffu) == 255u); // alpha
}

void test_convert_rgb565_opaque() {
    const uint16_t texel = 0; // black; RGB565 has no alpha channel, always opaque
    uint32_t out = 0xdeadbeefu;
    CHECK(ot_gpu_convert_row(OT_HAL_PIXEL_RGB565, &texel, 1, 0, &out) == 1);
    CHECK(((out >> 24) & 0xffu) == 255u);
}

void test_convert_indexed() {
    uint32_t palette[16] = {};
    palette[5] = 0xff112233u; // 0xAARRGGBB
    const uint8_t index8[1] = {5};
    uint32_t out8 = 0;
    CHECK(ot_gpu_convert_row(OT_HAL_PIXEL_INDEX8, index8, 1, palette, &out8) == 1);
    CHECK(out8 == (0x11u | (0x22u << 8) | (0x33u << 16) | (0xffu << 24)));

    // Two INDEX4 texels packed low nibble first: texel 0 = 5, texel 1 = 0.
    const uint8_t index4[1] = {0x05u};
    uint32_t out4[2] = {0, 0};
    CHECK(ot_gpu_convert_row(OT_HAL_PIXEL_INDEX4, index4, 2, palette, out4) == 1);
    CHECK(out4[0] == (0x11u | (0x22u << 8) | (0x33u << 16) | (0xffu << 24)));
    CHECK(out4[1] == 0u); // palette[0] is zeroed
}

void test_convert_unknown_format() {
    uint32_t out = 0;
    CHECK(ot_gpu_convert_row(0xffffu, 0, 1, 0, &out) == 0);
}

void test_fan_to_list() {
    // A 5-vertex fan is 3 triangles: (1,2,0) (2,3,0) (3,4,0).
    uint32_t indices[9];
    CHECK(ot_gpu_fan_to_list_indices(5, indices) == 3);
    const uint32_t expected[9] = {1, 2, 0, 2, 3, 0, 3, 4, 0};
    for (int index = 0; index < 9; ++index) {
        CHECK(indices[index] == expected[index]);
    }
    // Every triangle keeps vertex 0 last, so a last-vertex-provoking backend
    // still takes its flat colour from the fan's first (shared) vertex.
    for (int triangle = 0; triangle < 3; ++triangle) {
        CHECK(indices[triangle * 3 + 2] == 0);
    }
    CHECK(ot_gpu_fan_to_list_indices(2, indices) == 0);
    CHECK(ot_gpu_fan_to_list_indices(0, indices) == 0);
}

void test_clear_quad() {
    OtHalVertex quad[4];
    ot_gpu_clear_quad(640, 480, 0.25f, 0xff223344u, quad);
    CHECK(quad[0].x == -0.5f && quad[0].y == -0.5f);
    CHECK(quad[1].x == 639.5f && quad[1].y == -0.5f);
    CHECK(quad[2].x == 639.5f && quad[2].y == 479.5f);
    CHECK(quad[3].x == -0.5f && quad[3].y == 479.5f);
    for (int index = 0; index < 4; ++index) {
        CHECK(quad[index].z == 0.25f);
        CHECK(quad[index].rhw == 1.0f);
        CHECK(quad[index].color == 0xff223344u);
    }
}

} // namespace

int main() {
    test_expand();
    test_convert_argb8888();
    test_convert_argb1555();
    test_convert_rgb565_opaque();
    test_convert_indexed();
    test_convert_unknown_format();
    test_fan_to_list();
    test_clear_quad();
    if (failures == 0) {
        std::printf("OK video_gpu_test\n");
    }
    return failures == 0 ? 0 : 1;
}
