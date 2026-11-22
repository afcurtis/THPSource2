// The PC texture layer on the retail memory model (Phase 5.4a, 7b continued): the words a texture record holds, and the pixels its surface holds, after each call, with the
// engine behind it (the allocator, the file backend, the surface layer) played by a host that answers by address and by service name. The steps and arguments are the
// ones the disassembly shows (build/s4/asm_*.txt).
#include "surface_store.hpp"
#include "texture_kit.hpp"
#include "seam_addresses_generated.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::texturekit;
using opentony::runtime::surfaces::SurfaceStore;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

template <typename T>
void expect_eq(T actual, T expected, const char* what) {
    if (!(actual == expected)) {
        ++failures;
        std::cerr << "FAIL: " << what << ": 0x" << std::hex << static_cast<unsigned long long>(actual) << " != 0x" << static_cast<unsigned long long>(expected) << std::dec
                  << "\n";
    }
}

void expect_text(const std::string& actual, const std::string& expected, const char* what) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL: " << what << ": '" << actual << "' != '" << expected << "'\n";
    }
}

constexpr std::uint32_t kRdata = 0x00549000;          // the strings the layer names
constexpr std::uint32_t kRdataSize = 0x00020000;
constexpr std::uint32_t kGlobals = 0x006a0000;        // the texture globals
constexpr std::uint32_t kGlobalsSize = 0x00010000;
constexpr std::uint32_t kVideo = 0x029da000;          // D3DSoftwareRenderer, DirectDraw
constexpr std::uint32_t kVideoSize = 0x1000;
constexpr std::uint32_t kScratchWindow = 0x7ff00000;
constexpr std::uint32_t kScratchSize = 0x8000;
constexpr std::uint32_t kTest = 0x006a8000;            // where the tests build their descriptions
constexpr std::uint32_t kHeap = 0x01000000;
constexpr std::uint32_t kHeapSize = 0x00100000;

constexpr std::uint32_t kFormatLevelPath = 0x00549fe4;
constexpr std::uint32_t kFormatPath = 0x00549fd8;
constexpr std::uint32_t kFormatNamePath = 0x00549fc8;
constexpr std::uint32_t kNewTex = 0x00549ff4;
constexpr std::uint32_t kNewBmp = 0x00549fd0;

class Engine final : public ServiceHost {
public:
    explicit Engine(RetailMemory& memory)
        : m_(memory), store(memory, [this](std::uint32_t size) { return allocate(size); }) {}

    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string&) override {
        std::uint32_t result = 0;
        if (store.call(name, args, result)) {
            ++surface_calls;
            return result;
        }
        if (name.compare(0, 7, "engine_") != 0) {
            return 0;
        }
        switch (static_cast<std::uint32_t>(std::strtoul(name.c_str() + 7, nullptr, 16))) {
        case 0x005002fe:  // operator new(size)
            return allocate(args[0]);
        case 0x005002f3:  // operator delete(block)
            ++deleted;
            freed.push_back(args[0]);
            return 0;
        case 0x004e79f0:  // FileIO_BackendSeek(handle, offset, origin)
            position = args[1];
            return 0;
        case 0x004d9880: {  // D3DTex_AllocRecord()
            const std::uint32_t at = allocate(0x2c);
            m_.fill(at, 0, 0x2c);
            return at;
        }
        case 0x004da460: {  // Palette_AllocCacheRecord(): the first one is the list's head
            const std::uint32_t at = allocate(0x18);
            m_.fill(at, 0, 0x18);
            if (m_.u32(kPaletteHead) == 0) {
                m_.set32(kPaletteHead, at);
            }
            return at;
        }
        case 0x004da4b0:  // D3DTEX_FreePaletteEntry(record, free colours)
            freed_palettes.push_back(args[0]);
            return 0;
        case 0x004da3e0:  // D3DTex_AttachToSceneMaterial(material, record)
            attached.emplace_back(args[0], args[1]);
            return 0;
        case 0x0046f4d0:  // Mem_Delete(block)
            mem_deleted.push_back(args[0]);
            return 0;
        case 0x005002a1:  // CRT_sprintf(destination, format, ...): %s and %08X only
            return format(args);
        case 0x004cf850:  // printf_log
            ++logs;
            return 0;
        case 0x004e75b0:  // FileIO_BackendOpen(path, 0, 0)
            opened.push_back(m_.cstring(args[0]));
            position = 0;
            return open_ok ? 3 : 0xffffffffu;
        case 0x004e7ad0:  // FileIO_BackendRead(handle, buffer, count)
            for (std::uint32_t k = 0; k < args[2]; ++k) {
                m_.set8(args[1] + k, position < file.size() ? file[position] : 0);
                ++position;
            }
            return args[2];
        case 0x004e7bc0:  // FileIO_BackendClose(handle)
            ++closed;
            return 0;
        case 0x004d0a20: {  // D3D_BitCount(mask)
            std::uint32_t count = 0;
            for (std::uint32_t mask = args[0]; mask != 0; mask >>= 1) {
                count += mask & 1;
            }
            return count;
        }
        case 0x004d0a40: {  // D3D_LowestBit(mask)
            std::uint32_t at = 0;
            if (args[0] == 0) {
                return 0;
            }
            while (((args[0] >> at) & 1) == 0) {
                ++at;
            }
            return at;
        }
        default:
            ++unexpected;
            return 0;
        }
    }

    std::uint32_t allocate(std::uint32_t size) {
        const std::uint32_t at = heap_next;
        heap_next += (size + 0x3f) & ~0x3fu;
        return at;
    }

    std::uint32_t format(const std::vector<std::uint32_t>& args) {
        const std::string pattern = m_.cstring(args[1]);
        std::string out;
        std::size_t next = 2;
        for (std::size_t k = 0; k < pattern.size(); ++k) {
            if (pattern[k] != '%') {
                out += pattern[k];
            } else if (pattern.compare(k, 2, "%s") == 0) {
                out += m_.cstring(args[next++]);
                ++k;
            } else if (pattern.compare(k, 4, "%08X") == 0) {
                char text[16];
                std::snprintf(text, sizeof text, "%08X", args[next++]);
                out += text;
                k += 3;
            }
        }
        m_.write(args[0], {reinterpret_cast<const std::uint8_t*>(out.c_str()), out.size() + 1});
        return static_cast<std::uint32_t>(out.size());
    }

    RetailMemory& m_;
    SurfaceStore store;
    std::vector<std::string> opened;
    std::vector<std::uint32_t> freed;
    std::vector<std::uint32_t> freed_palettes;
    std::vector<std::uint32_t> mem_deleted;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> attached;
    std::vector<std::uint8_t> file;
    std::size_t position = 0;
    std::uint32_t heap_next = kHeap;
    int deleted = 0;
    int logs = 0;
    int closed = 0;
    int unexpected = 0;
    int surface_calls = 0;
    bool open_ok = true;
};

struct Rig {
    RetailMemory memory;
    RetailServices services;
    Engine host;
    TextureKit kit;

    Rig() : host(memory), kit(memory, services) {
        memory.add_region(kRdata, kRdataSize);
        memory.add_region(kGlobals, kGlobalsSize);
        memory.add_region(kVideo, kVideoSize);
        memory.add_region(kScratchWindow, kScratchSize);
        memory.add_region(kHeap, kHeapSize);
        services.attach(&host);
        text(kFormatLevelPath, "%s\\%s\\%08X.BMP");
        text(kFormatPath, "%s\\%08X.BMP");
        text(kFormatNamePath, "%s\\%s");
        text(kNewTex, "NEWTEX");
        text(kNewBmp, "NEWBMP");
        // the device: 16-bit ARGB 1555 surfaces, no size rules
        memory.set32(kPixelFormat, 0x20);
        memory.set32(kPixelFormat + 4, 0x41);
        memory.set32(kPixelFormat + 0xc, 16);
        memory.set32(kPixelFormat + 0x10, 0x7c00);
        memory.set32(kPixelFormat + 0x14, 0x03e0);
        memory.set32(kPixelFormat + 0x18, 0x001f);
        memory.set32(kPixelFormat + 0x1c, 0x8000);
        memory.set32(kMaxWidth, 256);
        memory.set32(kMaxHeight, 256);
        memory.set32(kMinWidth, 1);
        memory.set32(kMinHeight, 1);
        memory.set8(kConfiguredRoot, 1);
        // ... and the same format as bit counts and shifts per channel (byte 0 of a bitmap palette entry is blue)
        memory.set32(kRedBits, 5);
        memory.set32(kGreenBits, 5);
        memory.set32(kBlueBits, 5);
        memory.set32(kAlphaBits, 1);
        memory.set32(kRedShift, 10);
        memory.set32(kGreenShift, 5);
        memory.set32(kBlueShift, 0);
        memory.set32(kAlphaShift, 15);
    }
    // a surface of the device's format, through the store
    std::uint32_t surface(std::uint32_t width, std::uint32_t height) {
        memory.fill(kTest, 0, 0x7c);
        memory.set32(kTest, 0x7c);
        memory.set32(kTest + 4, 0x1007);
        memory.set32(kTest + 8, height);
        memory.set32(kTest + 0xc, width);
        memory.copy(kTest + 0x48, kPixelFormat, 0x20);
        host.store.create_surface(kTest, kOut);
        return memory.u32(kOut);
    }
    void text(std::uint32_t at, const std::string& value) { memory.write(at, {reinterpret_cast<const std::uint8_t*>(value.c_str()), value.size() + 1}); }

    // a texture record of the given size, flags and (optional) indexed pixels
    std::uint32_t record(std::uint32_t width, std::uint32_t height, std::uint32_t flags) {
        const std::uint32_t at = host.allocate(0x2c);
        memory.fill(at, 0, 0x2c);
        memory.set16(at + 0x14, width);
        memory.set16(at + 0x16, height);
        memory.set32(at + 0x10, flags);
        return at;
    }
    // a palette cache record: checksum, entry count, entries (16-bit), the 1555 copy
    std::uint32_t palette(std::uint32_t checksum, std::uint32_t count, const std::vector<std::uint16_t>& entries) {
        const std::uint32_t at = host.allocate(0x18);
        const std::uint32_t table = host.allocate(count * 2);
        memory.fill(at, 0, 0x18);
        for (std::size_t k = 0; k < entries.size(); ++k) {
            memory.set16(table + static_cast<std::uint32_t>(k) * 2, entries[k]);
        }
        memory.set32(at, checksum);
        memory.set32(at + 4, count);
        memory.set32(at + 8, table);
        memory.set32(at + 0xc, table);
        return at;
    }
    std::uint32_t pixels(const std::vector<std::uint8_t>& bytes) {
        const std::uint32_t at = host.allocate(static_cast<std::uint32_t>(bytes.size()));
        memory.write(at, bytes);
        return at;
    }
};

// a 24-bit bitmap as the file layer serves it: the 14-byte file header, the 40-byte info header, then rows bottom first, each padded to a multiple of 4 bytes
std::vector<std::uint8_t> bitmap24(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& bgr_rows_bottom_first) {
    std::vector<std::uint8_t> file(14 + 40, 0);
    file[0] = 'B';
    file[1] = 'M';
    auto put32 = [&](std::size_t at, std::uint32_t value) {
        for (int k = 0; k < 4; ++k) {
            file[at + k] = static_cast<std::uint8_t>(value >> (8 * k));
        }
    };
    put32(14, 40);
    put32(18, width);
    put32(22, height);
    file[26] = 1;
    file[28] = 24;
    const std::uint32_t row_bytes = ((width + 1) * 3) & ~3u;
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t k = 0; k < row_bytes; ++k) {
            const std::size_t at = static_cast<std::size_t>(row) * width * 3 + k;
            file.push_back(k < width * 3 && at < bgr_rows_bottom_first.size() ? bgr_rows_bottom_first[at] : 0);
        }
    }
    return file;
}

std::uint16_t surface_pixel(Rig& rig, std::uint32_t handle, std::uint32_t x, std::uint32_t y) {
    const auto* s = rig.host.store.find(handle);
    return s == nullptr ? 0xdead : rig.memory.u16(s->pixels + y * s->pitch + x * 2);
}

void test_store() {
    Rig rig;
    SurfaceStore& store = rig.host.store;
    const std::uint32_t desc = kTest;
    rig.memory.fill(desc, 0, 0x7c);
    rig.memory.set32(desc, 0x7c);
    rig.memory.set32(desc + 4, 0x1007);
    rig.memory.set32(desc + 8, 2);
    rig.memory.set32(desc + 0xc, 3);
    rig.memory.copy(desc + 0x48, kPixelFormat, 0x20);
    expect_eq(store.create_surface(desc, kOut), 0u, "create");
    const std::uint32_t handle = rig.memory.u32(kOut);
    expect(handle != 0, "the handle is not 0");
    const auto* s = store.find(handle);
    expect(s != nullptr && s->pitch == 8, "a 3-pixel 16-bit row is 8 bytes (rounded up to 8)");
    expect_eq(store.lock(handle, 0, kLock, 0x820), 0u, "lock");
    expect_eq(rig.memory.u32(kLock + 0x10), 8u, "lock pitch");
    expect_eq(store.lock(handle, 0, kLock + 0x80, 0x820), 0x80004005u, "a second lock fails");
    rig.memory.set16(rig.memory.u32(kLock + 0x24), 0x1234);
    expect_eq(store.unlock(handle), 0u, "unlock");
    expect_eq(s->version, 2u, "a write lock moves the version on");
    // stretch 3x2 -> 6x4: nearest pixel
    rig.memory.fill(desc, 0, 0x7c);
    rig.memory.set32(desc, 0x7c);
    rig.memory.set32(desc + 4, 0x1007);
    rig.memory.set32(desc + 8, 4);
    rig.memory.set32(desc + 0xc, 6);
    rig.memory.copy(desc + 0x48, kPixelFormat, 0x20);
    expect_eq(store.create_surface(desc, kOut), 0u, "create the second");
    const std::uint32_t big = rig.memory.u32(kOut);
    expect_eq(store.blt(big, 0, handle, 0), 0u, "blt stretches");
    expect_eq<std::uint32_t>(surface_pixel(rig, big, 1, 1), 0x1234, "the stretched pixel (1,1) is source (0,0)");
    expect_eq<std::uint32_t>(surface_pixel(rig, big, 2, 0), 0, "(2,0) is source (1,0)");
    expect_eq(store.release(handle), 0u, "release answers the references left");
    expect(store.find(handle) == nullptr, "a released surface is gone");
}

void test_open_image() {
    Rig rig;
    rig.text(kLevelName, "");
    rig.text(kEmptyName, "");
    expect_eq(rig.kit.open_image(0x12ab, 0), 3u, "no level: opens NEWTEX\\<checksum>");
    expect_text(rig.host.opened.back(), std::string("NEWTEX\\000012AB.BMP"), "the file name without a level");
    rig.text(kLevelName, "Hangar");
    rig.kit.open_image(0x12ab, 0);
    expect_text(rig.host.opened.back(), std::string("NEWTEX\\Hangar\\000012AB.BMP"), "the level's file is tried first, and taken when it opens");
    rig.host.open_ok = false;
    expect_eq(rig.kit.open_image(0x12ab, 0), 0xffffffffu, "a missing file answers -1");
    expect_text(rig.host.opened[rig.host.opened.size() - 2], std::string("NEWTEX\\Hangar\\000012AB.BMP"), "a missing level file is followed by the plain one");
    expect_text(rig.host.opened.back(), std::string("NEWTEX\\000012AB.BMP"), "the plain file is the last try");
    rig.host.open_ok = true;
    rig.text(kTest + 0x100, "dir\\name.bmp");
    rig.kit.open_image(0, kTest + 0x100);
    expect_text(rig.host.opened.back(), std::string("NEWBMP\\name.bmp"), "a name loses its first directory");
    rig.memory.set8(kConfiguredRoot, 0);
    expect_eq(rig.kit.open_image(0x12ab, 0), 0xffffffffu, "without the configured root a checksum opens nothing");
    expect_eq(rig.kit.open_image(0, 0), 0xffffffffu, "nothing named, nothing opened");
}

// an 8-bit indexed record: each index through the palette, an entry of 0 clears the "no transparent pixel" flag, rows padded to an even count
void test_indexed_8() {
    Rig rig;
    const std::uint32_t palette = rig.palette(7, 0x100, {0x0000, 0x7fff, 0x03e0, 0x001f});
    const std::uint32_t record = rig.record(3, 2, 0);
    // two rows of 3 pixels, each padded to 4 bytes
    rig.memory.set32(record + 8, rig.pixels({1, 2, 3, 0xee, 3, 2, 1, 0xee}));
    rig.memory.set32(record + 0x20, palette);
    const std::uint32_t desc = kTest;
    rig.memory.fill(desc, 0, 0x7c);
    rig.memory.set32(desc, 0x7c);
    rig.memory.set32(desc + 4, 0x1007);
    rig.memory.set32(desc + 8, 2);
    rig.memory.set32(desc + 0xc, 3);
    rig.memory.copy(desc + 0x48, kPixelFormat, 0x20);
    expect_eq(rig.host.store.create_surface(desc, kOut), 0u, "create");
    const std::uint32_t surface = rig.memory.u32(kOut);
    rig.kit.upload_indexed_pixels(surface, record, palette);
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 0, 0), 0x7fff, "(0,0) = entry 1");
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 2, 0), 0x001f, "(2,0) = entry 3");
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 0, 1), 0x001f, "(0,1) = entry 3 (the second row starts after the padding byte)");
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 2, 1), 0x7fff, "(2,1) = entry 1");
    expect((rig.memory.u32(record + 0x10) & 0x10) != 0, "no entry used was 0: still opaque");
    rig.memory.set32(record + 8, rig.pixels({0, 2, 3, 0xee, 3, 2, 1, 0xee}));
    rig.kit.upload_indexed_pixels(surface, record, palette);
    expect((rig.memory.u32(record + 0x10) & 0x10) == 0, "an index that maps to 0 clears the opaque flag");
}

// the 4-bit form: two pixels to a byte, low nibble first
void test_indexed_4() {
    Rig rig;
    std::vector<std::uint16_t> entries(16);
    for (std::uint16_t k = 0; k < 16; ++k) {
        entries[k] = static_cast<std::uint16_t>(0x100 + k);
    }
    const std::uint32_t palette = rig.palette(9, 16, entries);
    const std::uint32_t record = rig.record(4, 1, 0);
    rig.memory.set32(record + 8, rig.pixels({0x21, 0x43}));
    const std::uint32_t desc = kTest;
    rig.memory.fill(desc, 0, 0x7c);
    rig.memory.set32(desc, 0x7c);
    rig.memory.set32(desc + 4, 0x1007);
    rig.memory.set32(desc + 8, 1);
    rig.memory.set32(desc + 0xc, 4);
    rig.memory.copy(desc + 0x48, kPixelFormat, 0x20);
    expect_eq(rig.host.store.create_surface(desc, kOut), 0u, "create");
    const std::uint32_t surface = rig.memory.u32(kOut);
    rig.kit.upload_indexed_pixels(surface, record, palette);
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 0, 0), 0x101, "low nibble of the first byte");
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 1, 0), 0x102, "high nibble");
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 3, 0), 0x104, "the second byte's high nibble");
}

// a 2x2 24-bit bitmap: the rows come bottom first, B G R per pixel, converted to ARGB 1555; (R 0xff, G 0, B 0xff) is the key colour
void test_rgb() {
    Rig rig;
    rig.host.file = bitmap24(2, 2, {0x00, 0x00, 0xff, /**/ 0xff, 0x00, 0xff,    // bottom row: red, key
                                    0xff, 0x00, 0x00, /**/ 0x00, 0xff, 0x00});  // top row: blue, green
    const std::uint32_t record = rig.record(2, 2, 0x18);   // 24-bit bitmap, opaque
    rig.memory.set32(record + 0xc, 0xbeef);
    const std::uint32_t desc = kTest;
    rig.memory.fill(desc, 0, 0x7c);
    rig.memory.set32(desc, 0x7c);
    rig.memory.set32(desc + 4, 0x1007);
    rig.memory.set32(desc + 8, 2);
    rig.memory.set32(desc + 0xc, 2);
    rig.memory.copy(desc + 0x48, kPixelFormat, 0x20);
    expect_eq(rig.host.store.create_surface(desc, kOut), 0u, "create");
    const std::uint32_t surface = rig.memory.u32(kOut);
    rig.kit.upload_rgb_pixels(surface, record);
    expect_text(rig.host.opened.back(), std::string("NEWTEX\\0000BEEF.BMP"), "the bitmap is the record's checksum");
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 0, 0), 0x801f | 0x0000 | 0x0000, "top left: blue");
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 1, 0), 0x83e0, "top right: green");
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 0, 1), 0xfc00, "bottom left: red");
    expect_eq<std::uint32_t>(surface_pixel(rig, surface, 1, 1), 0x0000, "bottom right: the key colour is written as 0");
    expect((rig.memory.u32(record + 0x10) & 0x10) == 0, "the key colour clears the opaque flag");
    expect_eq(rig.host.closed, 1, "the file is closed");
}

// the whole upload: a palette found by checksum, the staging surface, a texture surface rounded up to a power of two and blitted from it
void test_upload_texture() {
    Rig rig;
    rig.memory.set32(kPowerOfTwo, 1);
    const std::uint32_t palette = rig.palette(0x55, 0x100, {0x0000, 0x7fff, 0x7c00});
    rig.memory.set32(kPaletteHead, palette);
    const std::uint32_t record = rig.record(3, 3, 0);
    rig.memory.set32(record + 8, rig.pixels({1, 1, 1, 0, 2, 2, 2, 0, 1, 2, 1, 0}));
    rig.kit.upload_texture(record, 0x55, 0, 0);
    expect_eq(rig.memory.u32(record + 0x20), palette, "the palette cache record is found by its checksum");
    expect((rig.memory.u32(record + 0x10) & 0x100) != 0, "its first entry is 0: flag 0x100");
    const std::uint32_t handle = rig.memory.u32(record);
    expect(handle != 0, "the record has a surface");
    expect_eq<std::uint32_t>(rig.memory.u16(record + 0x18), 4, "the realized width is a power of two");
    expect_eq<std::uint32_t>(rig.memory.u16(record + 0x1a), 4, "the realized height is a power of two");
    expect_eq<std::uint32_t>(rig.memory.u16(record + 0x14), 3, "the declared size is kept");
    const auto* s = rig.host.store.find(handle);
    expect(s != nullptr && s->width == 4 && s->height == 4, "the surface is 4x4");
    expect_eq<std::uint32_t>(surface_pixel(rig, handle, 0, 0), 0x7fff, "(0,0) = entry 1");
    expect_eq<std::uint32_t>(surface_pixel(rig, handle, 3, 3), 0x7fff, "(3,3): the stretch takes source (2,2) = entry 1");
    expect_eq<std::size_t>(rig.host.store.count(), 1, "the staging surface is released");
    expect_eq(rig.host.unexpected, 0, "no call outside the ones the test answers");

    // a record that has a surface keeps it: the staging pixels are blitted into the part the source covers (`replace`)
    Rig other;
    const std::uint32_t pal = other.palette(0x66, 0x100, {0x0000, 0x4321});
    other.memory.set32(kPaletteHead, pal);
    const std::uint32_t second = other.record(2, 2, 0);
    other.memory.set32(second + 8, other.pixels({1, 1, 1, 1}));
    other.kit.upload_texture(second, 0x66, 0, 1);
    expect_eq<std::uint32_t>(other.memory.u16(second + 0x14), 2, "replace: the declared size is the surface's");
    expect_eq<std::uint32_t>(surface_pixel(other, other.memory.u32(second), 1, 1), 0x4321, "replace: the pixels arrive");

    // an unknown palette checksum is logged and the head record taken
    Rig missing;
    const std::uint32_t head = missing.palette(1, 0x100, {0x0000, 0x1111});
    missing.memory.set32(kPaletteHead, head);
    const std::uint32_t third = missing.record(2, 2, 0);
    missing.memory.set32(third + 8, missing.pixels({1, 1, 1, 1}));
    missing.kit.upload_texture(third, 0x99, 0, 0);
    expect_eq(missing.host.logs, 1, "an unknown palette checksum is logged");
    expect_eq(missing.memory.u32(third + 0x20), head, "and the head record is taken");
}

void test_failure_is_fatal() {
    Rig rig;
    const std::uint32_t record = rig.record(0, 0, 0);   // a surface of no size cannot be created
    rig.memory.set32(record + 0x20, rig.palette(3, 0x100, {0}));
    bool faulted = false;
    try {
        rig.kit.upload_texture(record, 3, 0, 0);
    } catch (const ServiceFault& fault) {
        faulted = std::string(fault.what()).find("pc\\d3dtex.cpp(1577)") != std::string::npos;   // 0x629
    }
    expect(faulted, "a failed CreateSurface ends the program with the retail line (0x629)");
}

// a paletted 8-bit BMP file image as the bitmap loader takes it: file header, info header, `palette` entries of four bytes (B, G, R, 0), then the rows
// bottom first, each padded to a multiple of four bytes (`rows` holds them already padded)
std::vector<std::uint8_t> bitmap8(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& palette, const std::vector<std::uint8_t>& rows) {
    std::vector<std::uint8_t> file(14 + 40, 0);
    auto put32 = [&](std::size_t at, std::uint32_t value) {
        for (int k = 0; k < 4; ++k) {
            file[at + k] = static_cast<std::uint8_t>(value >> (8 * k));
        }
    };
    file[0] = 'B';
    file[1] = 'M';
    put32(2, static_cast<std::uint32_t>(14 + 40 + palette.size() + rows.size()));
    put32(10, static_cast<std::uint32_t>(14 + 40 + palette.size()));
    put32(14, 40);
    put32(18, width);
    put32(22, height);
    file[26] = 1;
    file[28] = 8;
    put32(46, static_cast<std::uint32_t>(palette.size() / 4));    // the colours in use
    file.insert(file.end(), palette.begin(), palette.end());
    file.insert(file.end(), rows.begin(), rows.end());
    return file;
}

// the four colours of the loader tests: black, the key colour, a mixed one, a dark one (byte 0 of an entry is blue)
const std::vector<std::uint8_t> kPalette = {0, 0, 0, 0,   0xff, 0, 0xff, 0,   0xf8, 0x80, 0x08, 0,   0x08, 0, 0, 0};

void test_destroy_record() {
    Rig rig;
    const std::uint32_t first = rig.record(2, 2, 1);      // flag 1: it owns its pixels
    const std::uint32_t second = rig.record(2, 2, 0);
    rig.memory.set32(first + 0x24, second);               // next
    rig.memory.set32(second + 0x28, first);               // previous
    rig.memory.set32(kListHead, first);
    rig.memory.set32(kListTail, second);
    rig.memory.set32(kRecordCount, 2);
    rig.memory.set32(first + 4, 0x1234);                  // its scene material
    rig.memory.set32(first + 8, 0x5000);                  // its pixels
    const std::uint32_t surface = rig.surface(2, 2);
    rig.memory.set32(first, surface);
    expect_eq(rig.kit.destroy_record(first), second, "the head moves on and is the answer");
    expect_eq(rig.memory.u32(kListHead), second, "the list head is the next record");
    expect_eq(rig.memory.u32(second + 0x28), 0u, "the next record has no previous one now");
    expect_eq(rig.memory.u32(kListTail), second, "the tail is untouched");
    expect_eq(rig.memory.u32(kRecordCount), 1u, "one record fewer");
    expect(rig.host.store.find(surface) == nullptr, "the surface is released");
    expect(rig.host.attached.size() == 1 && rig.host.attached[0] == std::make_pair(0x1234u, 0u), "the material forgets the record");
    expect(rig.host.freed.size() == 2 && rig.host.freed[0] == 0x5000 && rig.host.freed[1] == first, "its pixels and then the record are freed");
    // the tail leaves: the list is empty and the tail is the previous one (none)
    rig.memory.set8(kNoMaterialLink, 1);
    expect_eq(rig.kit.destroy_record(second), 0u, "the last record leaves an empty list");
    expect_eq(rig.memory.u32(kListTail), 0u, "an empty list has no tail");
    expect_eq(rig.host.attached.size(), std::size_t{1}, "the byte 0x029da3a5 keeps the material alone");
    expect_eq(rig.host.unexpected, 0, "no call outside the ones the test answers");
}

void test_gray_texture() {
    Rig rig;
    const std::uint32_t record = rig.record(2, 1, 0);
    const std::uint32_t handle = rig.surface(2, 1);
    rig.memory.set32(record, handle);
    const std::uint32_t at = rig.host.store.find(handle)->pixels;
    rig.memory.set16(at, 0x7fff);
    rig.memory.set16(at + 2, 0x83e0);
    rig.kit.gray_texture(record);
    // the channels' raw values added (31 * 3), times 550, over 3000 = 17, in every channel; the alpha bit stays
    expect_eq<std::uint32_t>(surface_pixel(rig, handle, 0, 0), 0x4631, "white without alpha: 17 in each channel");
    expect_eq<std::uint32_t>(surface_pixel(rig, handle, 1, 0), 0x94a5, "full green with alpha: (31 * 550) / 3000 = 5 in each channel");
    expect_eq(rig.host.store.find(handle)->version, 2u, "the surface was written (a write lock and its unlock)");
    // the software renderer greys the record's own pixels, in the 1555 layout
    Rig software;
    software.memory.set32(kSoftwareRenderer, 1);
    const std::uint32_t soft = software.record(2, 1, 0);
    software.memory.set16(soft + 0x18, 2);
    software.memory.set16(soft + 0x1a, 1);
    software.memory.set32(soft + 8, software.pixels({0xff, 0x7f, 0xe0, 0x83}));
    software.kit.gray_texture(soft);
    expect_eq<std::uint32_t>(software.memory.u16(software.memory.u32(soft + 8)), 0x4631, "software: the same grey");
    expect_eq<std::uint32_t>(software.memory.u16(software.memory.u32(soft + 8) + 2), 0x94a5, "software: the same grey with alpha");
}

// the palette conversion, the padding and flip of the pixels, the palette cache record, the upload, and the cases around them
void test_load_indexed_bitmap() {
    Rig rig;
    // 3x2, rows bottom first, padded to 4: {1,2,3} then {3,2,1}
    const std::uint32_t image = rig.pixels(bitmap8(3, 2, kPalette, {1, 2, 3, 0xaa, 3, 2, 1, 0xbb}));
    const std::uint32_t record = rig.kit.load_indexed_bitmap(image, 0, 0, 0);
    expect(record != 0, "a record is made");
    expect_eq(rig.memory.u32(record + 0x10), 0x21u, "flags: own palette, own pixels; opaque went when the key colour was used");
    expect_eq<std::uint32_t>(rig.memory.u16(record + 0x14), 4, "the declared width is the padded one");
    expect_eq<std::uint32_t>(rig.memory.u16(record + 0x16), 2, "the declared height");
    const std::uint32_t buffer = rig.memory.u32(record + 8);
    const std::uint8_t expected_rows[8] = {3, 2, 1, 0, 1, 2, 3, 0};
    bool rows_ok = true;
    for (std::uint32_t k = 0; k < 8; ++k) {
        rows_ok = rows_ok && rig.memory.u8(buffer + k) == expected_rows[k];
    }
    expect(rows_ok, "the rows are top first and padded with zeros (the bitmap's own padding bytes are not copied)");
    const std::uint32_t palette = rig.memory.u32(record + 0x20);
    expect_eq(rig.memory.u32(palette), 0xffffffffu, "the palette cache record has no checksum");
    expect_eq(rig.memory.u32(palette + 4), 0x100u, "and 0x100 colours");
    const std::uint32_t device = rig.memory.u32(palette + 8);
    const std::uint32_t fixed = rig.memory.u32(palette + 0xc);
    expect_eq<std::uint32_t>(rig.memory.u16(device + 0), 0x8000, "entry 0 in the device's format: black with alpha");
    expect_eq<std::uint32_t>(rig.memory.u16(device + 2), 0x0000, "entry 1 is the key colour: 0");
    expect_eq<std::uint32_t>(rig.memory.u16(device + 4), 0x861f, "entry 2: blue 31 (byte 0), green 16, red 1 (byte 2), alpha");
    expect_eq<std::uint32_t>(rig.memory.u16(device + 6), 0x8001, "entry 3: blue 1");
    expect_eq<std::uint32_t>(rig.memory.u16(fixed + 0), 0x8000, "entry 0 in the fixed layout");
    expect_eq<std::uint32_t>(rig.memory.u16(fixed + 2), 0x0000, "entry 1 (key)");
    expect_eq<std::uint32_t>(rig.memory.u16(fixed + 4), 0xfe01, "entry 2");
    expect_eq<std::uint32_t>(rig.memory.u16(fixed + 6), 0x8400, "entry 3");
    const std::uint32_t handle = rig.memory.u32(record);
    expect(handle != 0, "the texture is uploaded");
    expect_eq<std::uint32_t>(surface_pixel(rig, handle, 0, 0), 0x8001, "(0,0) = entry 3");
    expect_eq<std::uint32_t>(surface_pixel(rig, handle, 1, 0), 0x861f, "(1,0) = entry 2");
    expect_eq<std::uint32_t>(surface_pixel(rig, handle, 2, 0), 0x0000, "(2,0) = the key colour");
    expect_eq<std::uint32_t>(surface_pixel(rig, handle, 3, 1), 0x8000, "(3,1): the padding byte is entry 0");
    expect_eq(rig.host.unexpected, 0, "no call outside the ones the test answers");

    // the record is loaded again: it keeps its palette cache record (same size) and gets the new entries; the new one is freed
    std::vector<std::uint8_t> other_palette = kPalette;
    other_palette[12] = 0x10;
    const std::uint32_t other = rig.pixels(bitmap8(3, 2, other_palette, {1, 2, 3, 0xaa, 3, 2, 1, 0xbb}));
    const std::size_t freed_before = rig.host.freed.size();
    expect_eq(rig.kit.load_indexed_bitmap(other, record, 0, 0), record, "the same record comes back");
    expect_eq(rig.memory.u32(record + 0x20), palette, "it keeps its palette cache record");
    expect_eq<std::uint32_t>(rig.memory.u16(device + 6), 0x8002, "whose entries are the new ones");
    expect(rig.host.freed.size() > freed_before && rig.host.freed[freed_before] == buffer, "the old pixels were freed first");
    expect(rig.host.freed_palettes.size() == 1 && rig.host.freed_palettes[0] != palette, "the new palette cache record was freed");
    expect_eq(rig.memory.u32(record + 0x10), 0x01u, "flags: own pixels (the palette is the old one's, so no own-palette flag)");
    expect_eq(rig.host.unexpected, 0, "still nothing unexpected");

    // a file of the same name that is 24-bit stays on disk: the record keeps the name and the upload reads it
    Rig file_rig;
    file_rig.host.file = bitmap24(2, 2, {0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00});
    file_rig.text(kTest + 0x100, "dir\\name.bmp");
    const std::uint32_t small = file_rig.pixels(bitmap8(3, 2, kPalette, {1, 2, 3, 0xaa, 3, 2, 1, 0xbb}));
    const std::uint32_t named = file_rig.kit.load_indexed_bitmap(small, 0, 0, kTest + 0x100);
    expect_eq(file_rig.memory.u32(named + 0x10) & 0x68, 0x68u, "flags: named file, 24-bit, own palette cache record");
    expect_eq(file_rig.memory.u32(named + 8), kTest + 0x100, "the record keeps the name");
    // the file is 2x2 and the bitmap in memory 3x2: the upload swaps the record's size with the source size of a record with the 24-bit flag
    expect_eq<std::uint32_t>(file_rig.memory.u16(named + 0x14), 3, "its size is the in-memory bitmap's after the upload's swap");
    expect_eq<std::uint32_t>(file_rig.memory.u16(named + 0x1c), 2, "and the file's is the source size");
    expect(file_rig.memory.u32(named) != 0, "and the upload made the texture from the file");
}

void test_build_tiles() {
    // 300x100 against a limit of 256x256: two tiles side by side, each a texture of its own, the original and the staging surface gone
    Rig rig;
    const std::uint32_t record = rig.record(300, 100, 0x10);
    const std::uint32_t handle = rig.surface(300, 100);
    rig.memory.set32(record, handle);
    const auto* original = rig.host.store.find(handle);
    rig.memory.set16(original->pixels, 0x2222);
    rig.memory.set16(original->pixels + 256 * 2, 0x1111);
    rig.memory.set16(original->pixels + 99 * original->pitch + 299 * 2, 0x3333);
    const std::uint32_t out = kTest + 0x200;
    expect_eq(rig.kit.build_tiles(record, out, out + 4), 2u, "two tiles");
    const std::uint32_t tiles = rig.memory.u32(out);
    const std::uint32_t rects = rig.memory.u32(out + 4);
    expect(rig.memory.u32(rects) == 0 && rig.memory.u32(rects + 4) == 0 && rig.memory.u32(rects + 8) == 256 && rig.memory.u32(rects + 12) == 100, "the first rectangle");
    expect(rig.memory.u32(rects + 16) == 256 && rig.memory.u32(rects + 20) == 0 && rig.memory.u32(rects + 24) == 300 && rig.memory.u32(rects + 28) == 100, "the second rectangle");
    expect_eq(rig.memory.u32(tiles + 0x10), 0x10u, "a tile has the record's flags");
    expect_eq<std::uint32_t>(rig.memory.u16(tiles + 0x18), 256, "tile 0 is 256 wide");
    expect_eq<std::uint32_t>(rig.memory.u16(tiles + 0x2c + 0x18), 44, "tile 1 is 44 wide");
    expect_eq<std::uint32_t>(rig.memory.u16(tiles + 0x2c + 0x1a), 100, "and 100 high");
    expect_eq<std::uint32_t>(surface_pixel(rig, rig.memory.u32(tiles), 0, 0), 0x2222, "tile 0 starts at the original's (0,0)");
    expect_eq<std::uint32_t>(surface_pixel(rig, rig.memory.u32(tiles + 0x2c), 0, 0), 0x1111, "tile 1 starts at the original's (256,0)");
    expect_eq<std::uint32_t>(surface_pixel(rig, rig.memory.u32(tiles + 0x2c), 43, 99), 0x3333, "and ends at its last pixel");
    expect(rig.host.store.find(handle) == nullptr, "the original surface is released");
    expect_eq(rig.host.store.count(), std::size_t{2}, "only the two tiles remain (the staging surface is released)");
    expect(!rig.host.freed.empty() && rig.host.freed.back() == record, "the record is freed");

    // the device wants power-of-two textures: the second tile grows to 64x128 and the blit stretches
    Rig pow2;
    pow2.memory.set32(kPowerOfTwo, 1);
    const std::uint32_t big = pow2.record(300, 100, 0);
    pow2.memory.set32(big, pow2.surface(300, 100));
    pow2.kit.build_tiles(big, out, out + 4);
    expect_eq<std::uint32_t>(pow2.memory.u16(pow2.memory.u32(out) + 0x2c + 0x18), 64, "power of two: 44 becomes 64");
    expect_eq<std::uint32_t>(pow2.memory.u16(pow2.memory.u32(out) + 0x2c + 0x1a), 128, "power of two: 100 becomes 128");

    // a surface within the limits is its own tile: the record is copied (and freed), the rectangle is its size
    Rig fit;
    const std::uint32_t small = fit.record(100, 50, 0x10);
    const std::uint32_t kept = fit.surface(100, 50);
    fit.memory.set32(small, kept);
    expect_eq(fit.kit.build_tiles(small, out, out + 4), 1u, "one tile");
    expect_eq(fit.memory.u32(fit.memory.u32(out)), kept, "the tile holds the same surface");
    expect(fit.memory.u32(fit.memory.u32(out + 4) + 8) == 100 && fit.memory.u32(fit.memory.u32(out + 4) + 12) == 50, "the rectangle is the record's size");
    expect(fit.host.store.find(kept) != nullptr, "the surface stays");
    expect(!fit.host.freed.empty() && fit.host.freed.back() == small, "the old record is freed");
}

void test_sprite() {
    Rig rig;
    const std::uint32_t image = rig.pixels(bitmap8(3, 2, kPalette, {1, 2, 3, 0xaa, 3, 2, 1, 0xbb}));
    const std::uint32_t sprite = rig.host.allocate(0x40);
    rig.memory.fill(sprite, 0, 0x40);
    expect_eq(rig.kit.sprite_create(sprite, image, 0, 0), sprite, "the sprite is the answer");
    expect_eq(rig.memory.u32(sprite + 0x14), 0x3f666666u, "the scale of an ordinary sprite is 0.9");
    expect_eq(rig.memory.u32(sprite + 0xc), 1u, "one tile");
    const std::uint32_t tile = rig.memory.u32(sprite + 4);
    const std::uint32_t materials = rig.memory.u32(sprite + 0x10);
    expect(rig.memory.u32(tile) != 0 && rig.host.store.find(rig.memory.u32(tile)) != nullptr, "the tile has the bitmap's texture");
    expect(rig.host.attached.size() == 1 && rig.host.attached[0] == std::make_pair(materials, tile), "the tile is attached to its scene material");
    expect(rig.host.mem_deleted.size() == 1 && rig.host.mem_deleted[0] == image, "the bitmap is freed");
    expect_eq(rig.host.freed_palettes.size(), std::size_t{1}, "the palette cache record goes");
    expect_eq(rig.host.store.count(), std::size_t{1}, "one surface: the tile's");
    expect_eq(rig.memory.u32(kBackgroundSprite), 0u, "this is not the background sprite");
    rig.kit.sprite_destroy(sprite);
    expect_eq(rig.host.store.count(), std::size_t{0}, "destroying the sprite releases the tile's surface");
    expect_eq(rig.host.unexpected, 0, "no call outside the ones the test answers");

    // the background sprite is remembered, with the other scale; a first tile 512 wide or more makes the sprite forget it
    Rig background;
    const std::uint32_t pic = background.pixels(bitmap8(3, 2, kPalette, {1, 2, 3, 0xaa, 3, 2, 1, 0xbb}));
    const std::uint32_t back = background.host.allocate(0x40);
    background.memory.fill(back, 0, 0x40);
    background.kit.sprite_create(back, pic, 0, 1);
    expect_eq(background.memory.u32(kBackgroundSprite), back, "the flag makes it the background sprite");
    expect_eq(background.memory.u32(back + 0x14), 0x3c23d70au, "its scale is 0.01");
    background.kit.sprite_destroy(back);
    expect_eq(background.memory.u32(kBackgroundSprite), back, "a narrow sprite does not clear it");
    const std::uint32_t wide = background.host.allocate(0x40);
    background.memory.fill(wide, 0, 0x40);
    const std::uint32_t wide_tile = background.record(512, 8, 0);
    background.memory.set32(wide + 4, wide_tile);
    background.kit.sprite_destroy(wide);
    expect_eq(background.memory.u32(kBackgroundSprite), 0u, "a 512-wide first tile clears the background sprite");
}

}  // namespace

int main() {
    test_destroy_record();
    test_gray_texture();
    test_load_indexed_bitmap();
    test_build_tiles();
    test_sprite();
    test_store();
    test_open_image();
    test_indexed_8();
    test_indexed_4();
    test_rgb();
    test_upload_texture();
    test_failure_is_fatal();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "texture kit: ok\n";
    return 0;
}
