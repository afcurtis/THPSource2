#pragma once

#include "../hal/video.h"
#include "surface_store.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace opentony::runtime::surfaces {

// Phase 5.4a, continued (7b, part 3): the HAL texture of a surface of the SurfaceStore, for the draw handlers of a program with no retail image.
//
// The hooked build learns when a game surface changed by patching the surface vtable (src/hooks/hal/video_renderer.cpp). The store has no vtable: it counts. Every write
// that changes the pixels (an Unlock of a write lock, a Blt into the surface) moves `Surface::version`, and this cache keeps the version a HAL texture was made from. A draw
// handler asks `texture_for(surface)` before it draws with a surface: the texture is created the first time, updated in place when the version moved (or made again when the
// size or format did), and left alone otherwise. A surface that was released (the store forgot it) loses its texture when it is asked for, or at the next
// `sweep()` (once per frame is enough).
//
// The pixel formats are the ones video_renderer.cpp names (1555 with alpha, 4444, 565, 8888); a surface of another format has no texture (INVALID), as there.
class HalTextureCache final {
public:
    HalTextureCache(RetailMemory& memory, const SurfaceStore& store, const OtHalVideoBackend& video) : m_(memory), store_(store), video_(video) {}
    ~HalTextureCache() { forget_all(); }
    HalTextureCache(const HalTextureCache&) = delete;
    HalTextureCache& operator=(const HalTextureCache&) = delete;

    // The texture to draw `surface` with: OT_HAL_INVALID_HANDLE for 0, a surface the store no longer has, or a format with no HAL name.
    [[nodiscard]] OtHalHandle texture_for(std::uint32_t surface);
    // The texture of a surface that the game released goes now
    void forget(std::uint32_t surface);
    void forget_all();
    // Drops the textures of every surface the store no longer has
    void sweep();

    [[nodiscard]] std::size_t cached() const noexcept { return entries_.size(); }
    [[nodiscard]] std::uint32_t uploads() const noexcept { return uploads_; }    // textures created or updated so far

    // The HAL's name for a DDPIXELFORMAT (the 0x20 bytes of Surface::format); 0 when there is none
    [[nodiscard]] static std::uint32_t hal_format(const std::uint8_t (&format)[kFormatSize]);

private:
    struct Entry {
        OtHalHandle texture = OT_HAL_INVALID_HANDLE;
        std::uint32_t version = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t format = 0;
    };
    [[nodiscard]] bool upload(const Surface& surface, std::uint32_t format, Entry& entry);

    RetailMemory& m_;
    const SurfaceStore& store_;
    const OtHalVideoBackend& video_;
    std::map<std::uint32_t, Entry> entries_;
    std::uint32_t uploads_ = 0;
};

}  // namespace opentony::runtime::surfaces
