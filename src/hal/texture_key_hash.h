#ifndef OPENTONY_HAL_TEXTURE_KEY_HASH_H
#define OPENTONY_HAL_TEXTURE_KEY_HASH_H

/* The key of a texture (docs/LAUNCHER.md, texture dump and texture pack): FNV-1a, 64 bits, over the 12 bytes width, height and HAL pixel format (each a little-endian
 * 32-bit word) and then the packed level-0 rows exactly as HalTextureCache::upload hands them to the HAL (texture_update with pitch = width x bytes a texel).
 *
 * C and header-only on purpose: the GL backend (sdl_video_gl.cpp, also built into the Visual C++ 6 hook DLL, which has no <cstdint> or STL to lean on) and the
 * portable texture_key.hpp (dump, launcher, tests) both include it, so a key means the same thing everywhere. */

#include <stddef.h>
#include <stdint.h>

static inline uint64_t ot_hal_texture_key(int32_t width, int32_t height, uint32_t format, const void *rows, size_t size) {
    const uint64_t prime = 0x00000100000001b3ull;
    uint64_t hash = 0xcbf29ce484222325ull;
    const uint32_t header[3] = {(uint32_t)width, (uint32_t)height, format};
    for (int word = 0; word < 3; ++word) {
        for (int shift = 0; shift < 32; shift += 8) {
            hash = (hash ^ ((header[word] >> shift) & 0xffu)) * prime;
        }
    }
    const uint8_t *bytes = (const uint8_t *)rows;
    for (size_t index = 0; index < size; ++index) {
        hash = (hash ^ bytes[index]) * prime;
    }
    return hash;
}

#endif /* OPENTONY_HAL_TEXTURE_KEY_HASH_H */
