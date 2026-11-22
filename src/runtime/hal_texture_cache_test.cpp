// The version-keyed HAL texture cache of the standalone's draw handlers (Phase 5.4a, 7b continued, part 3), against a recording HAL video backend.
#include "hal_texture_cache.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::surfaces;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

struct Recorder {
    struct Create {
        OtHalTextureDesc desc;
        OtHalHandle handle;
    };
    struct Update {
        OtHalHandle handle;
        OtHalRect area;
        std::int32_t pitch;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Create> created;
    std::vector<Update> updated;
    std::vector<OtHalHandle> destroyed;
    OtHalHandle next = 100;
};

Recorder g;

OtHalHandle OT_HAL_CALL fake_create(const OtHalTextureDesc* desc) {
    g.created.push_back({*desc, g.next});
    return g.next++;
}

void OT_HAL_CALL fake_update(OtHalHandle texture, const OtHalRect* area, const void* pixels, std::int32_t pitch) {
    const auto* bytes = static_cast<const std::uint8_t*>(pixels);
    g.updated.push_back({texture, *area, pitch, std::vector<std::uint8_t>(bytes, bytes + static_cast<std::size_t>(pitch) * static_cast<std::size_t>(area->h))});
}

void OT_HAL_CALL fake_destroy(OtHalHandle texture) { g.destroyed.push_back(texture); }

OtHalVideoBackend make_backend() {
    OtHalVideoBackend video;
    std::memset(&video, 0, sizeof video);
    video.name = "recorder";
    video.texture_create = &fake_create;
    video.texture_update = &fake_update;
    video.texture_destroy = &fake_destroy;
    return video;
}

constexpr std::uint32_t kHeap = 0x01000000;
constexpr std::uint32_t kTest = 0x00600000;

struct Rig {
    RetailMemory memory;
    std::uint32_t next = kHeap;
    SurfaceStore store;
    OtHalVideoBackend video = make_backend();
    HalTextureCache cache;

    Rig()
        : store(memory, [this](std::uint32_t size) {
              const std::uint32_t at = next;
              next += (size + 0x3f) & ~0x3fu;
              return at;
          }),
          cache(memory, store, video) {
        memory.add_region(kHeap, 0x100000);
        memory.add_region(kTest, 0x1000);
        g = Recorder();
    }
    std::uint32_t surface(std::uint32_t width, std::uint32_t height, std::uint32_t bits, std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha) {
        memory.fill(kTest, 0, 0x100);
        memory.set32(kTest, kDescSize);
        memory.set32(kTest + kDescFlags, 0x1007);
        memory.set32(kTest + kDescHeight, height);
        memory.set32(kTest + kDescWidth, width);
        const std::uint32_t format = kTest + kDescPixelFormat;
        memory.set32(format, kFormatSize);
        memory.set32(format + 4, 0x41);
        memory.set32(format + kFormatBits, bits);
        memory.set32(format + 0x10, red);
        memory.set32(format + 0x14, green);
        memory.set32(format + 0x18, blue);
        memory.set32(format + 0x1c, alpha);
        expect(store.create_surface(kTest, kTest + 0x200) == kOk, "the surface is created");
        return memory.u32(kTest + 0x200);
    }
    // a write lock: `value` into pixel (x, y)
    void poke(std::uint32_t handle, std::uint32_t x, std::uint32_t y, std::uint16_t value) {
        expect(store.lock(handle, 0, kTest + 0x300, 0x820) == kOk, "lock");
        memory.set16(memory.u32(kTest + 0x300 + kDescSurface) + y * memory.u32(kTest + 0x300 + kDescPitch) + x * 2, value);
        expect(store.unlock(handle) == kOk, "unlock");
    }
};

void test_formats() {
    std::uint8_t format[kFormatSize] = {};
    const auto put = [&](std::uint32_t offset, std::uint32_t value) { std::memcpy(format + offset, &value, 4); };
    put(kFormatBits, 16);
    put(0x10, 0x7c00);
    put(0x14, 0x03e0);
    put(0x18, 0x001f);
    put(0x1c, 0x8000);
    expect(HalTextureCache::hal_format(format) == OT_HAL_PIXEL_ARGB1555, "1555 with alpha");
    put(0x1c, 0);
    expect(HalTextureCache::hal_format(format) == 0, "1555 without an alpha bit has no name (as in the hooked renderer)");
    put(0x10, 0xf800);
    put(0x14, 0x07e0);
    expect(HalTextureCache::hal_format(format) == OT_HAL_PIXEL_RGB565, "565");
    put(0x10, 0x0f00);
    put(0x14, 0x00f0);
    put(0x18, 0x000f);
    expect(HalTextureCache::hal_format(format) == OT_HAL_PIXEL_ARGB4444, "4444");
    put(kFormatBits, 32);
    put(0x10, 0x00ff0000);
    expect(HalTextureCache::hal_format(format) == OT_HAL_PIXEL_ARGB8888, "8888");
    put(kFormatBits, 8);
    expect(HalTextureCache::hal_format(format) == 0, "an 8-bit surface has no name");
}

void test_upload_follows_the_version() {
    Rig rig;
    expect(rig.cache.texture_for(0) == OT_HAL_INVALID_HANDLE, "surface 0 is untextured");
    expect(rig.cache.texture_for(77) == OT_HAL_INVALID_HANDLE, "a surface the store does not have has no texture");
    const std::uint32_t handle = rig.surface(3, 2, 16, 0x7c00, 0x03e0, 0x001f, 0x8000);
    rig.poke(handle, 0, 0, 0x8001);
    rig.poke(handle, 2, 1, 0x8002);
    const OtHalHandle first = rig.cache.texture_for(handle);
    expect(first == 100 && g.created.size() == 1, "the first use creates a texture");
    expect(g.created[0].desc.width == 3 && g.created[0].desc.height == 2 && g.created[0].desc.format == OT_HAL_PIXEL_ARGB1555, "its size and format");
    expect(g.updated.size() == 1 && g.updated[0].pitch == 6 && g.updated[0].bytes.size() == 12, "its pixels go up with the rows packed (the store's pitch is 8)");
    expect(g.updated[0].bytes[0] == 0x01 && g.updated[0].bytes[1] == 0x80 && g.updated[0].bytes[10] == 0x02 && g.updated[0].bytes[11] == 0x80, "the pixels are the surface's");
    expect(rig.cache.texture_for(handle) == first && g.updated.size() == 1, "an unchanged surface is not uploaded again");

    rig.poke(handle, 1, 0, 0x8003);
    expect(rig.cache.texture_for(handle) == first, "a written surface keeps its texture");
    expect(g.created.size() == 1 && g.updated.size() == 2 && g.updated[1].bytes[2] == 0x03, "and the new pixels are uploaded in place");

    // a Blt into the surface moves the version as well
    const std::uint32_t source = rig.surface(3, 2, 16, 0x7c00, 0x03e0, 0x001f, 0x8000);
    rig.poke(source, 0, 0, 0x8abc);
    expect(rig.store.blt(handle, 0, source, 0) == kOk, "blt");
    expect(rig.cache.texture_for(handle) == first && g.updated.size() == 3 && g.updated[2].bytes[0] == 0xbc, "a Blt into the surface is seen too");
    expect(rig.cache.uploads() == 3, "three uploads in all");
}

void test_release_and_unnamed_formats() {
    Rig rig;
    const std::uint32_t handle = rig.surface(2, 2, 16, 0xf800, 0x07e0, 0x001f, 0);
    const OtHalHandle texture = rig.cache.texture_for(handle);
    expect(texture != OT_HAL_INVALID_HANDLE && g.created[0].desc.format == OT_HAL_PIXEL_RGB565, "a 565 surface");
    const std::uint32_t other = rig.surface(2, 2, 16, 0x7c00, 0x03e0, 0x001f, 0x8000);
    const OtHalHandle second = rig.cache.texture_for(other);
    expect(second != texture && rig.cache.cached() == 2, "each surface has its own texture");
    expect(rig.store.release(handle) == 0, "the game releases the first surface");
    expect(rig.cache.texture_for(handle) == OT_HAL_INVALID_HANDLE, "asking for it after that answers nothing");
    expect(g.destroyed.size() == 1 && g.destroyed[0] == texture && rig.cache.cached() == 1, "and its texture goes");
    expect(rig.store.release(other) == 0, "the second surface is released without anyone asking about it");
    rig.cache.sweep();
    expect(g.destroyed.size() == 2 && g.destroyed[1] == second && rig.cache.cached() == 0, "a sweep drops it");
    const std::uint32_t odd = rig.surface(2, 2, 16, 0x7c00, 0x03e0, 0x001f, 0);   // 5-5-5 without an alpha bit
    expect(rig.cache.texture_for(odd) == OT_HAL_INVALID_HANDLE && g.created.size() == 2, "a format the HAL has no name for is not uploaded");
}

void test_forget_all_gives_the_textures_back() {
    Rig rig;
    const std::uint32_t handle = rig.surface(2, 2, 16, 0x7c00, 0x03e0, 0x001f, 0x8000);
    expect(rig.cache.texture_for(handle) == 100, "first texture");
    rig.cache.forget_all();
    expect(g.destroyed.size() == 1 && g.destroyed[0] == 100 && rig.cache.cached() == 0, "forget_all gives the textures back");
}

}  // namespace

int main() {
    test_formats();
    test_upload_follows_the_version();
    test_release_and_unnamed_formats();
    test_forget_all_gives_the_textures_back();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "hal texture cache: ok\n";
    return 0;
}
