// Phase 5.4a, continued (7b): the DirectDraw surfaces of the texture layer (src/runtime/texture_kit.*) in a standalone program. The store itself
// (src/runtime/surface_store.*) is portable and unit-tested; this is its host: the pixels of a surface are a block of the guest heap (11b), so the address a Lock answers
// is a guest address the Kits write through.
#include "draw_kit.hpp"
#include "guest_arena.hpp"
#include "hal_texture_cache.hpp"
#include "kit_bridge.hpp"
#include "surface_store.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace opentony::kitdll {
namespace {

std::unique_ptr<runtime::surfaces::SurfaceStore> g_store;
const OtHalVideoBackend* g_video = nullptr;                              // set by the program (ot_standalone_set_video_hal)
std::unique_ptr<runtime::surfaces::HalTextureCache> g_textures;          // made on first use, over the store and the backend

// 11b (docs/PORT64_AUDIT.md D2): the pixels are a block of the guest heap, so a Lock answers a guest address on both builds
std::uint32_t allocate(std::uint32_t size) { return arena::heap_allocate(size); }

void release(std::uint32_t address) { arena::heap_release(address); }

}  // namespace

runtime::surfaces::SurfaceStore& surface_store(RetailMemory& memory) {
    if (!g_store) {
        g_store = std::make_unique<runtime::surfaces::SurfaceStore>(memory, &allocate, &release);
    }
    return *g_store;
}

bool read_surface(RetailMemory& memory, std::uint32_t handle, std::uint32_t& width, std::uint32_t& height, std::uint32_t& bytes_per_pixel,
                  std::vector<std::uint8_t>& pixels) {
    const runtime::surfaces::Surface* surface = surface_store(memory).find(handle);
    if (surface == nullptr) {
        return false;
    }
    width = surface->width;
    height = surface->height;
    bytes_per_pixel = surface->bytes_per_pixel;
    pixels.clear();
    for (std::uint32_t row = 0; row < surface->height; ++row) {
        const std::vector<std::uint8_t> line = memory.bytes(surface->pixels + row * surface->pitch, surface->width * surface->bytes_per_pixel);
        pixels.insert(pixels.end(), line.begin(), line.end());
    }
    return true;
}

std::uint32_t list_surfaces(RetailMemory& memory, std::uint32_t* handles, std::uint32_t* versions, std::uint32_t* caps, std::uint32_t capacity) {
    std::uint32_t seen = 0;
    surface_store(memory).for_each([&](std::uint32_t handle, const runtime::surfaces::Surface& surface) {
        if (seen < capacity) {
            handles[seen] = handle;
            versions[seen] = surface.version;
            caps[seen] = surface.caps;
        }
        ++seen;
    });
    return seen;
}

bool read_surface_texels(RetailMemory& memory, std::uint32_t handle, std::uint32_t& width, std::uint32_t& height, std::uint32_t& hal_format, std::vector<std::uint8_t>& packed) {
    const runtime::surfaces::Surface* surface = surface_store(memory).find(handle);
    if (surface == nullptr) {
        return false;
    }
    width = surface->width;
    height = surface->height;
    hal_format = runtime::surfaces::HalTextureCache::hal_format(surface->format);
    packed.clear();
    const std::uint32_t row_bytes = surface->width * surface->bytes_per_pixel;
    for (std::uint32_t row = 0; row < surface->height; ++row) {
        const std::vector<std::uint8_t> line = memory.bytes(surface->pixels + row * surface->pitch, row_bytes);
        packed.insert(packed.end(), line.begin(), line.end());
    }
    return true;
}

// The surface services of the texture layer (ddraw_*), and the one a draw handler asks: ddraw_hal_texture_for(surface) answers the HAL texture to draw the surface
// with, uploading it when its pixels changed since the last answer (src/runtime/hal_texture_cache.hpp); 0 (untextured) when no video backend is set.
bool call_surface_service(RetailMemory& memory, const std::string& name, const std::vector<std::uint32_t>& args, std::uint32_t& result) {
    if (name.compare(0, 6, "ddraw_") != 0) {
        return false;
    }
    if (name == "ddraw_hal_texture_for") {
        result = OT_HAL_INVALID_HANDLE;
        if (g_video != nullptr && !args.empty()) {
            if (!g_textures) {
                g_textures = std::make_unique<runtime::surfaces::HalTextureCache>(memory, surface_store(memory), *g_video);
            }
            result = g_textures->texture_for(args[0]);
        }
        return true;
    }
    const bool handled = surface_store(memory).call(name, args, result);
    if (handled && g_textures && name == "ddraw_surface_release" && result == 0 && !args.empty()) {
        g_textures->forget(args[0]);     // the surface is gone: so is its texture
    }
    return handled;
}

}  // namespace opentony::kitdll

namespace opentony::kitdll {
namespace {

runtime::drawkit::Output g_draw_output;   // what the draw path (src/runtime/draw_kit.*) draws into and calls at a present

}  // namespace
}  // namespace opentony::kitdll

// The video backend the draw path draws into and uploads textures to. Call it before the first draw; a different backend replaces the cache (its textures belonged to the
// old one).
extern "C" __declspec(dllexport) void __cdecl ot_standalone_set_video_hal(const OtHalVideoBackend* video) {
    opentony::kitdll::g_textures.reset();
    opentony::kitdll::g_video = video;
    opentony::kitdll::g_draw_output.video = video;
    opentony::kitdll::g_draw_output.movie_texture = &opentony::kitdll::movie_overlay_texture;   // the level select's video box
    opentony::runtime::drawkit::set_output(opentony::kitdll::g_draw_output);
}

// record != 0 keeps each frame's primitive stream (ot_standalone_draw_stream); on_present runs after each present with the frame number (0 first), from inside the
// native that presented.
extern "C" __declspec(dllexport) void __cdecl ot_standalone_set_draw_output(int record, void(__cdecl* on_present)(std::uint32_t frame, void* user), void* user) {
    opentony::kitdll::g_draw_output.record = record != 0;
    opentony::kitdll::g_draw_output.on_present = on_present;
    opentony::kitdll::g_draw_output.user = user;
    opentony::runtime::drawkit::set_output(opentony::kitdll::g_draw_output);
}

// The draw path's counts since the start (draw_kit.hpp, Counters): draws, unsupported, presents, packets, no texture, oversized, faded
extern "C" __declspec(dllexport) void __cdecl ot_standalone_draw_counters(std::uint32_t out[7]) {
    const auto& c = opentony::runtime::drawkit::counters();
    const std::uint32_t values[7] = {c.draws, c.unsupported, c.presents, c.packets, c.no_texture, c.oversized, c.faded};
    std::memcpy(out, values, sizeof values);
}

// The current frame's primitive stream as an .otvh file (src/hooks/hal/video_shadow.cpp's format) for frame number `frame`: its size; copied when `capacity` is enough.
extern "C" __declspec(dllexport) std::uint32_t __cdecl ot_standalone_draw_stream(std::uint32_t frame, std::uint8_t* out, std::uint32_t capacity) {
    const std::vector<std::uint8_t> file = opentony::runtime::drawkit::stream_file(frame);
    if (out != nullptr && capacity >= file.size()) {
        std::memcpy(out, file.data(), file.size());
    }
    return static_cast<std::uint32_t>(file.size());
}
