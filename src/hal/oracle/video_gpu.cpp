#include "video_gpu.h"

uint8_t OT_HAL_CALL ot_gpu_expand5(uint32_t value) {
    return static_cast<uint8_t>((value * 255u + 15u) / 31u);
}

uint8_t OT_HAL_CALL ot_gpu_expand6(uint32_t value) {
    return static_cast<uint8_t>((value * 255u + 31u) / 63u);
}

uint8_t OT_HAL_CALL ot_gpu_expand4(uint32_t value) {
    return static_cast<uint8_t>(value * 17u);
}

uint8_t OT_HAL_CALL ot_gpu_expand1(uint32_t value) {
    return value != 0 ? 0xffu : 0u;
}

namespace {

uint32_t pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

uint32_t from_argb8888(uint32_t argb) {
    const uint8_t a = static_cast<uint8_t>(argb >> 24);
    const uint8_t r = static_cast<uint8_t>(argb >> 16);
    const uint8_t g = static_cast<uint8_t>(argb >> 8);
    const uint8_t b = static_cast<uint8_t>(argb);
    return pack(r, g, b, a);
}

} // namespace

int OT_HAL_CALL ot_gpu_convert_row(
    uint32_t format, const void *row, int32_t width, const uint32_t *palette, uint32_t *out) {
    if (width <= 0) {
        return format == OT_HAL_PIXEL_ARGB8888 || format == OT_HAL_PIXEL_ARGB1555 ||
                       format == OT_HAL_PIXEL_ARGB4444 || format == OT_HAL_PIXEL_RGB565 ||
                       format == OT_HAL_PIXEL_INDEX8 || format == OT_HAL_PIXEL_INDEX4
                   ? 1
                   : 0;
    }
    switch (format) {
    case OT_HAL_PIXEL_ARGB8888: {
        const uint32_t *texels = static_cast<const uint32_t *>(row);
        for (int32_t x = 0; x < width; ++x) {
            out[x] = from_argb8888(texels[x]);
        }
        return 1;
    }
    case OT_HAL_PIXEL_ARGB1555: {
        const uint16_t *texels = static_cast<const uint16_t *>(row);
        for (int32_t x = 0; x < width; ++x) {
            const uint16_t texel = texels[x];
            out[x] = pack(ot_gpu_expand5((texel >> 10) & 0x1fu), ot_gpu_expand5((texel >> 5) & 0x1fu),
                ot_gpu_expand5(texel & 0x1fu), ot_gpu_expand1((texel >> 15) & 0x1u));
        }
        return 1;
    }
    case OT_HAL_PIXEL_ARGB4444: {
        const uint16_t *texels = static_cast<const uint16_t *>(row);
        for (int32_t x = 0; x < width; ++x) {
            const uint16_t texel = texels[x];
            out[x] = pack(ot_gpu_expand4((texel >> 8) & 0xfu), ot_gpu_expand4((texel >> 4) & 0xfu),
                ot_gpu_expand4(texel & 0xfu), ot_gpu_expand4((texel >> 12) & 0xfu));
        }
        return 1;
    }
    case OT_HAL_PIXEL_RGB565: {
        const uint16_t *texels = static_cast<const uint16_t *>(row);
        for (int32_t x = 0; x < width; ++x) {
            const uint16_t texel = texels[x];
            out[x] = pack(ot_gpu_expand5((texel >> 11) & 0x1fu), ot_gpu_expand6((texel >> 5) & 0x3fu),
                ot_gpu_expand5(texel & 0x1fu), 0xffu);
        }
        return 1;
    }
    case OT_HAL_PIXEL_INDEX8: {
        const uint8_t *texels = static_cast<const uint8_t *>(row);
        for (int32_t x = 0; x < width; ++x) {
            out[x] = palette != 0 ? from_argb8888(palette[texels[x]]) : 0u;
        }
        return 1;
    }
    case OT_HAL_PIXEL_INDEX4: {
        const uint8_t *bytes = static_cast<const uint8_t *>(row);
        for (int32_t x = 0; x < width; ++x) {
            const uint8_t index = static_cast<uint8_t>((bytes[x / 2] >> ((x & 1) * 4)) & 0xfu);
            out[x] = palette != 0 ? from_argb8888(palette[index]) : 0u;
        }
        return 1;
    }
    default: return 0;
    }
}

uint32_t OT_HAL_CALL ot_gpu_fan_to_list_indices(uint32_t count, uint32_t *out) {
    if (count < 3) {
        return 0;
    }
    const uint32_t triangles = count - 2u;
    for (uint32_t triangle = 0; triangle < triangles; ++triangle) {
        out[triangle * 3u + 0u] = triangle + 1u;
        out[triangle * 3u + 1u] = triangle + 2u;
        out[triangle * 3u + 2u] = 0u;
    }
    return triangles;
}

void OT_HAL_CALL ot_gpu_clear_quad(int32_t width, int32_t height, float depth, uint32_t color, OtHalVertex quad[4]) {
    const float x0 = -0.5f;
    const float y0 = -0.5f;
    const float x1 = static_cast<float>(width) - 0.5f;
    const float y1 = static_cast<float>(height) - 0.5f;
    quad[0] = {x0, y0, depth, 1.0f, color, 0xff000000u, 0.0f, 0.0f};
    quad[1] = {x1, y0, depth, 1.0f, color, 0xff000000u, 0.0f, 0.0f};
    quad[2] = {x1, y1, depth, 1.0f, color, 0xff000000u, 0.0f, 0.0f};
    quad[3] = {x0, y1, depth, 1.0f, color, 0xff000000u, 0.0f, 0.0f};
}
