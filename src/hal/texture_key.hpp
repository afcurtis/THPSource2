#pragma once

// The key of a texture and its texels as 8-bit RGBA, for the texture dump (OT_TEXTURE_DUMP) and the upscaled texture pack (docs/LAUNCHER.md). Portable and header-only: the
// game's video layer (src/hooks/kits/standalone_texture_dump.cpp), the launcher and the native tests all use these two functions, so a key always means the same thing.
//
// The key is FNV-1a (64 bits) over the 12 bytes width, height, HAL pixel format (each a little-endian 32-bit word) and then the packed level-0 rows exactly as
// HalTextureCache::upload hands them to the HAL (texture_update with pitch = width x bytes a texel). Printed as 16 lower-case hex digits. A Python check is three lines:
//   h = 0xcbf29ce484222325; for b in header + rows: h = ((h ^ b) * 0x100000001b3) & (2**64 - 1)

#include "texture_key_hash.h"
#include "video.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opentony::hal::texture {

// the one implementation is the C header the GL backend includes too (texture_key_hash.h)
[[nodiscard]] inline std::uint64_t key(std::int32_t width, std::int32_t height, std::uint32_t format, const void* rows, std::size_t size) {
    return ot_hal_texture_key(width, height, format, rows, size);
}

[[nodiscard]] inline std::string key_text(std::uint64_t value) {
    static const char kDigits[] = "0123456789abcdef";
    std::string text(16, '0');
    for (int index = 15; index >= 0; --index) {
        text[static_cast<std::size_t>(index)] = kDigits[value & 0xfu];
        value >>= 4;
    }
    return text;
}

// 16 hex digits to the key; false for anything else
[[nodiscard]] inline bool parse_key(const std::string& text, std::uint64_t& value) {
    if (text.size() != 16) {
        return false;
    }
    std::uint64_t result = 0;
    for (const char c : text) {
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0) {
            return false;
        }
        result = (result << 4) | static_cast<std::uint64_t>(digit);
    }
    value = result;
    return true;
}

[[nodiscard]] inline std::size_t bytes_per_texel(std::uint32_t format) {
    switch (format) {
    case OT_HAL_PIXEL_ARGB8888: return 4;
    case OT_HAL_PIXEL_ARGB1555:
    case OT_HAL_PIXEL_ARGB4444:
    case OT_HAL_PIXEL_RGB565: return 2;
    default: return 0;   // indexed textures are not sent by the texture cache
    }
}

[[nodiscard]] inline const char* format_name(std::uint32_t format) {
    switch (format) {
    case OT_HAL_PIXEL_ARGB8888: return "argb8888";
    case OT_HAL_PIXEL_ARGB1555: return "argb1555";
    case OT_HAL_PIXEL_ARGB4444: return "argb4444";
    case OT_HAL_PIXEL_RGB565: return "rgb565";
    default: return "other";
    }
}

enum class Alpha { Opaque, Cutout, Smooth };

[[nodiscard]] inline const char* alpha_name(Alpha alpha) {
    return alpha == Alpha::Opaque ? "opaque" : alpha == Alpha::Cutout ? "cutout" : "smooth";
}

// width x height x 4 bytes, R G B A in memory order, rows top down, for packed rows of a format the texture cache sends; the channels are widened by repeating their top
// bits (31 -> 255, 15 -> 255), the way a driver does. A texel with alpha 0 keeps its colour (the upscaler needs it). `alpha` is opaque when every texel is 255, cut-out when
// every texel is 0 or 255, smooth otherwise (a format without alpha is opaque). Empty when the format or the size is not one the cache sends.
[[nodiscard]] inline std::vector<std::uint8_t> to_rgba8(std::int32_t width, std::int32_t height, std::uint32_t format, const void* rows, std::size_t size, Alpha& alpha) {
    alpha = Alpha::Opaque;
    const std::size_t texel = bytes_per_texel(format);
    const std::size_t count = static_cast<std::size_t>(width > 0 ? width : 0) * static_cast<std::size_t>(height > 0 ? height : 0);
    if (texel == 0 || count == 0 || size < count * texel) {
        return {};
    }
    std::vector<std::uint8_t> out(count * 4);
    const auto* in = static_cast<const std::uint8_t*>(rows);
    bool any_clear = false, any_partial = false;
    for (std::size_t index = 0; index < count; ++index) {
        std::uint32_t r, g, b, a;
        if (texel == 4) {
            const std::uint32_t v = in[4 * index] | (in[4 * index + 1] << 8) | (in[4 * index + 2] << 16) | (static_cast<std::uint32_t>(in[4 * index + 3]) << 24);
            b = v & 0xffu;
            g = (v >> 8) & 0xffu;
            r = (v >> 16) & 0xffu;
            a = v >> 24;
        } else {
            const std::uint32_t v = in[2 * index] | (in[2 * index + 1] << 8);
            if (format == OT_HAL_PIXEL_ARGB1555) {
                const std::uint32_t r5 = (v >> 10) & 31u, g5 = (v >> 5) & 31u, b5 = v & 31u;
                r = (r5 << 3) | (r5 >> 2);
                g = (g5 << 3) | (g5 >> 2);
                b = (b5 << 3) | (b5 >> 2);
                a = (v & 0x8000u) != 0 ? 255u : 0u;
            } else if (format == OT_HAL_PIXEL_ARGB4444) {
                r = ((v >> 8) & 15u) * 17u;
                g = ((v >> 4) & 15u) * 17u;
                b = (v & 15u) * 17u;
                a = ((v >> 12) & 15u) * 17u;
            } else {
                const std::uint32_t r5 = (v >> 11) & 31u, g6 = (v >> 5) & 63u, b5 = v & 31u;
                r = (r5 << 3) | (r5 >> 2);
                g = (g6 << 2) | (g6 >> 4);
                b = (b5 << 3) | (b5 >> 2);
                a = 255u;
            }
        }
        out[4 * index] = static_cast<std::uint8_t>(r);
        out[4 * index + 1] = static_cast<std::uint8_t>(g);
        out[4 * index + 2] = static_cast<std::uint8_t>(b);
        out[4 * index + 3] = static_cast<std::uint8_t>(a);
        any_clear = any_clear || a == 0;
        any_partial = any_partial || (a != 0 && a != 255);
    }
    alpha = any_partial ? Alpha::Smooth : any_clear ? Alpha::Cutout : Alpha::Opaque;
    return out;
}

}  // namespace opentony::hal::texture
