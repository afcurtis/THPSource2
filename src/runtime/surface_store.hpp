#pragma once

#include "retail_memory.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace opentony::runtime::surfaces {

// Phase 5.4a, continued (7b): the DirectDraw surfaces the texture layer (pc\d3dtex.cpp, pc\d3dsprite.cpp) keeps its pixels in, for a program with no DirectDraw. Retail
// creates a system-memory surface per texture, locks it, writes 16-bit pixels, unlocks it and blits it into the surface the Direct3D device draws from. This store is
// the part of that the game can see: a surface is a number (the value retail keeps in word 0 of a texture record, 4 bytes), its pixels live in retail memory (the
// allocator is the host's) so a Lock answers a real address and pitch, and Blt copies or stretches by the portable rules of src/hal/oracle/ddraw_surface.h
// (pitch rounded up to 8 bytes, nearest-pixel stretch). The HAL's draw handlers look a handle up and upload its pixels to a texture when the surface was written
// (`version`); nothing here knows about SDL.
//
// The calls are named services the Kits report (`ddraw_create_surface` ...); the descriptions (DDSURFACEDESC2, 0x7c bytes; DDPIXELFORMAT, 0x20) are read from and written
// to retail memory as DirectDraw does.

// DDSURFACEDESC2 offsets
constexpr std::uint32_t kDescSize = 0x7c;
constexpr std::uint32_t kDescFlags = 0x04;
constexpr std::uint32_t kDescHeight = 0x08;
constexpr std::uint32_t kDescWidth = 0x0c;
constexpr std::uint32_t kDescPitch = 0x10;
constexpr std::uint32_t kDescSurface = 0x24;
constexpr std::uint32_t kDescPixelFormat = 0x48;
constexpr std::uint32_t kDescCaps = 0x68;
constexpr std::uint32_t kDescCaps2 = 0x6c;
// DDPIXELFORMAT offsets
constexpr std::uint32_t kFormatSize = 0x20;
constexpr std::uint32_t kFormatBits = 0x0c;

constexpr std::uint32_t kLockReadOnly = 0x10;       // DDLOCK_READONLY

constexpr std::uint32_t kOk = 0;
constexpr std::uint32_t kInvalidParameters = 0x80070057;
constexpr std::uint32_t kFailed = 0x80004005;

struct Surface {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pitch = 0;
    std::uint32_t bytes_per_pixel = 0;
    std::uint32_t pixels = 0;            // the address of height * pitch bytes in retail memory
    std::uint32_t caps = 0;
    std::uint32_t caps2 = 0;
    std::uint32_t references = 1;
    std::uint32_t version = 1;           // moves on whenever the pixels were written (an Unlock of a write lock, a Blt into the surface)
    bool locked = false;
    bool write_lock = false;
    std::uint8_t format[kFormatSize] = {};
};

class SurfaceStore final {
public:
    // `allocate` gives `size` bytes of retail memory (and 0 for none); `release` gives them back (may be empty).
    using Allocate = std::function<std::uint32_t(std::uint32_t size)>;
    using Release = std::function<void(std::uint32_t address)>;

    SurfaceStore(RetailMemory& memory, Allocate allocate, Release release = {})
        : m_(memory), allocate_(std::move(allocate)), release_(std::move(release)) {}

    // IDirectDraw7::CreateSurface(desc, &out): the handle goes to `out`
    std::uint32_t create_surface(std::uint32_t desc, std::uint32_t out);
    // IDirectDrawSurface7::Lock(rect, &desc, flags): the description of the pixels (with the address and pitch) goes to `desc_out`
    std::uint32_t lock(std::uint32_t surface, std::uint32_t rect, std::uint32_t desc_out, std::uint32_t flags);
    std::uint32_t unlock(std::uint32_t surface);
    // IDirectDrawSurface7::Blt(destination rectangle, source, source rectangle): a null rectangle is the whole surface; sizes that differ stretch
    std::uint32_t blt(std::uint32_t destination, std::uint32_t destination_rect, std::uint32_t source, std::uint32_t source_rect);
    // IUnknown::Release: the references that remain
    std::uint32_t release(std::uint32_t surface);
    // IDirectDrawSurface7::GetPixelFormat(&format)
    std::uint32_t pixel_format(std::uint32_t surface, std::uint32_t out);
    // IDirectDrawSurface7::GetSurfaceDesc(&desc): size, flags, height, width, pitch, pixel format and caps (the address of the pixels is not part of it)
    std::uint32_t surface_desc(std::uint32_t surface, std::uint32_t out);

    // the services above by name; false when `name` is none of them
    bool call(const std::string& name, const std::vector<std::uint32_t>& args, std::uint32_t& result);

    [[nodiscard]] const Surface* find(std::uint32_t handle) const {
        const auto it = surfaces_.find(handle);
        return it == surfaces_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] std::size_t count() const noexcept { return surfaces_.size(); }
    // calls `visit(handle, surface)` for every surface, in handle order (the texture dump walks the store with it)
    template <typename Visit>
    void for_each(Visit&& visit) const {
        for (const auto& [handle, surface] : surfaces_) {
            visit(handle, surface);
        }
    }

private:
    struct Rect {
        std::uint32_t left, top, right, bottom;
    };
    [[nodiscard]] Rect rectangle(std::uint32_t address, const Surface& surface) const;

    RetailMemory& m_;
    Allocate allocate_;
    Release release_;
    std::map<std::uint32_t, Surface> surfaces_;
    std::uint32_t next_ = 1;
};

}  // namespace opentony::runtime::surfaces
