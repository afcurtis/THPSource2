#include "ddraw_surface.h"

#include <string.h>

int32_t OT_HAL_CALL ot_ddraw_pitch(int32_t width, int32_t bytes_per_pixel) {
    const int32_t bytes = width * bytes_per_pixel;
    return (bytes + 7) & ~7;
}

uint32_t OT_HAL_CALL ot_ddraw_pixel_format(uint32_t bits, uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha) {
    if (bits == 16 && red == 0x7c00u && green == 0x03e0u && blue == 0x001fu && alpha == 0x8000u) {
        return OT_HAL_PIXEL_ARGB1555;
    }
    if (bits == 16 && red == 0x0f00u && green == 0x00f0u && blue == 0x000fu && alpha == 0xf000u) {
        return OT_HAL_PIXEL_ARGB4444;
    }
    if (bits == 16 && red == 0xf800u && green == 0x07e0u && blue == 0x001fu && alpha == 0u) {
        return OT_HAL_PIXEL_RGB565;
    }
    if (bits == 32 && red == 0x00ff0000u && green == 0x0000ff00u && blue == 0x000000ffu) {
        return OT_HAL_PIXEL_ARGB8888;
    }
    return 0;
}

void OT_HAL_CALL ot_ddraw_fill(
    void *dest, int32_t pitch, int32_t width, int32_t height, int32_t bytes_per_pixel, uint32_t fill_value) {
    unsigned char *rows = static_cast<unsigned char *>(dest);
    if (bytes_per_pixel == 2) {
        const uint16_t pixel = static_cast<uint16_t>(fill_value);
        for (int32_t y = 0; y < height; ++y) {
            uint16_t *row = reinterpret_cast<uint16_t *>(rows + y * pitch);
            for (int32_t x = 0; x < width; ++x) {
                row[x] = pixel;
            }
        }
    } else {
        for (int32_t y = 0; y < height; ++y) {
            uint32_t *row = reinterpret_cast<uint32_t *>(rows + y * pitch);
            for (int32_t x = 0; x < width; ++x) {
                row[x] = fill_value;
            }
        }
    }
}

void OT_HAL_CALL ot_ddraw_copy(
    void *dest, int32_t dest_pitch, const void *src, int32_t src_pitch, int32_t width, int32_t height,
    int32_t bytes_per_pixel) {
    unsigned char *out = static_cast<unsigned char *>(dest);
    const unsigned char *in = static_cast<const unsigned char *>(src);
    const size_t row_bytes = static_cast<size_t>(width) * static_cast<size_t>(bytes_per_pixel);
    for (int32_t y = 0; y < height; ++y) {
        memmove(out + y * dest_pitch, in + y * src_pitch, row_bytes);
    }
}

namespace {

uint32_t channel_shift(uint32_t mask) {
    uint32_t shift = 0;
    while (mask != 0 && (mask & 1u) == 0u) {
        mask >>= 1;
        ++shift;
    }
    return shift;
}

uint32_t channel_bits(uint32_t mask) {
    uint32_t count = 0;
    while (mask != 0) {
        count += mask & 1u;
        mask >>= 1;
    }
    return count;
}

uint32_t pack_channel(uint8_t value8, uint32_t mask) {
    if (mask == 0) {
        return 0;
    }
    const uint32_t bits = channel_bits(mask);
    const uint32_t narrowed = bits >= 8 ? static_cast<uint32_t>(value8) : static_cast<uint32_t>(value8) >> (8 - bits);
    return narrowed << channel_shift(mask);
}

} // namespace

void OT_HAL_CALL ot_ddraw_convert_from_argb8888(
    void *dest, int32_t dest_pitch, int32_t dest_bits, uint32_t red_mask, uint32_t green_mask, uint32_t blue_mask,
    uint32_t alpha_mask, const void *src, int32_t src_pitch, int32_t width, int32_t height) {
    unsigned char *out = static_cast<unsigned char *>(dest);
    const unsigned char *in = static_cast<const unsigned char *>(src);
    for (int32_t y = 0; y < height; ++y) {
        const uint32_t *source_row = reinterpret_cast<const uint32_t *>(in + y * src_pitch);
        for (int32_t x = 0; x < width; ++x) {
            const uint32_t argb = source_row[x];
            const uint8_t a = static_cast<uint8_t>(argb >> 24);
            const uint8_t r = static_cast<uint8_t>(argb >> 16);
            const uint8_t g = static_cast<uint8_t>(argb >> 8);
            const uint8_t b = static_cast<uint8_t>(argb);
            const uint32_t packed = pack_channel(r, red_mask) | pack_channel(g, green_mask) |
                                     pack_channel(b, blue_mask) | pack_channel(a, alpha_mask);
            if (dest_bits == 16) {
                reinterpret_cast<uint16_t *>(out + y * dest_pitch)[x] = static_cast<uint16_t>(packed);
            } else {
                reinterpret_cast<uint32_t *>(out + y * dest_pitch)[x] = packed;
            }
        }
    }
}

void OT_HAL_CALL ot_ddraw_stretch(
    void *dest, int32_t dest_pitch, int32_t dest_width, int32_t dest_height, const void *src, int32_t src_pitch,
    int32_t src_width, int32_t src_height, int32_t bytes_per_pixel) {
    if (dest_width <= 0 || dest_height <= 0 || src_width <= 0 || src_height <= 0) {
        return;
    }
    unsigned char *out = static_cast<unsigned char *>(dest);
    const unsigned char *in = static_cast<const unsigned char *>(src);
    const uint32_t step_x = (static_cast<uint32_t>(src_width) << 16) / static_cast<uint32_t>(dest_width);
    const uint32_t step_y = (static_cast<uint32_t>(src_height) << 16) / static_cast<uint32_t>(dest_height);
    uint32_t source_y = 0;
    for (int32_t y = 0; y < dest_height; ++y, source_y += step_y) {
        int32_t sy = static_cast<int32_t>(source_y >> 16);
        sy = sy < src_height ? sy : src_height - 1;
        const unsigned char *source_row = in + sy * src_pitch;
        unsigned char *dest_row = out + y * dest_pitch;
        uint32_t source_x = 0;
        for (int32_t x = 0; x < dest_width; ++x, source_x += step_x) {
            int32_t sx = static_cast<int32_t>(source_x >> 16);
            sx = sx < src_width ? sx : src_width - 1;
            memcpy(dest_row + x * bytes_per_pixel, source_row + sx * bytes_per_pixel,
                static_cast<size_t>(bytes_per_pixel));
        }
    }
}
