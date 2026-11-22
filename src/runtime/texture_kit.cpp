#include "auto_views.hpp"
#include "texture_kit.hpp"
#include "seam_addresses_generated.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace opentony::runtime::texturekit {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kBackendOpen = 0x004e75b0;          // FileIO_BackendOpen(path, mode, 0): a handle
constexpr std::uint32_t kBackendRead = 0x004e7ad0;          // FileIO_BackendRead(handle, buffer, count)
constexpr std::uint32_t kBackendClose = 0x004e7bc0;         // FileIO_BackendClose(handle)
constexpr std::uint32_t kBitCount = 0x004d0a20;             // D3D_BitCount(mask)
constexpr std::uint32_t kLowestBit = 0x004d0a40;            // D3D_LowestBit(mask)
constexpr std::uint32_t kBackendSeek = 0x004e79f0;          // FileIO_BackendSeek(handle, offset, origin)
constexpr std::uint32_t kAllocRecord = 0x004d9880;          // D3DTex_AllocRecord()
constexpr std::uint32_t kAllocPalette = 0x004da460;         // Palette_AllocCacheRecord()
constexpr std::uint32_t kFreePaletteEntry = 0x004da4b0;     // D3DTEX_FreePaletteEntry(record, free colours)
constexpr std::uint32_t kAttachToMaterial = 0x004da3e0;     // D3DTex_AttachToSceneMaterial(material, record)
constexpr std::uint32_t kMemDelete = 0x0046f4d0;            // Mem_Delete(block): the free of a resource's bitmap

// strings the retail code names
constexpr std::uint32_t kFormatLevelPath = 0x00549fe4;      // "%s\%s\%08X.BMP"  (NEWTEX, level name, checksum)
constexpr std::uint32_t kFormatPath = 0x00549fd8;           // "%s\%08X.BMP"     (NEWTEX, checksum)
constexpr std::uint32_t kFormatNamePath = 0x00549fc8;       // "%s\%s"           (NEWBMP, name)
constexpr std::uint32_t kNewTex = 0x00549ff4;               // "NEWTEX"
constexpr std::uint32_t kNewBmp = 0x00549fd0;               // "NEWBMP"
constexpr std::uint32_t kMsgPaletteMissing = 0x0054a044;    // "Palette Checksum not found %x"
constexpr std::uint32_t kMsgTooBig = 0x00549ffc;            // "TEX too big ..."
constexpr std::uint32_t kMsgNon8Bit = 0x0054a098;           // "NON 8-bit, %i-bit texture in AddToTextureTable3"
constexpr std::uint32_t kMsgPaletteSizes = 0x0054a064;      // "ACK! Palette sizes do not match %s - %i != %i"
constexpr std::uint32_t kMsgWontRead = 0x0054a0cc;          // "ACK!!!! %s won't read"

// flags of a texture record
constexpr std::uint32_t kOwnPixels = 0x001;
constexpr std::uint32_t kBitmap24 = 0x008;
constexpr std::uint32_t kOpaque = 0x010;
constexpr std::uint32_t kNamedFile = 0x040;
constexpr std::uint32_t kFirstEntryClear = 0x100;
constexpr std::uint32_t kSoftwareCopy = 0x200;
constexpr std::uint32_t kPlanar = 0x00c;                    // the two bits (4 and 8) that make the surface a rotated copy

constexpr std::uint32_t kDescFlagsTexture = 0x1007;         // DDSD_CAPS | HEIGHT | WIDTH | PIXELFORMAT
constexpr std::uint32_t kCapsStaging = 0x1800;              // DDSCAPS_TEXTURE | SYSTEMMEMORY
constexpr std::uint32_t kCapsManaged = 0x1000;              // DDSCAPS_TEXTURE, with DDSCAPS2_TEXTUREMANAGE
constexpr std::uint32_t kCaps2Managed = 0x10;
constexpr std::uint32_t kLockWriteOnly = 0x820;             // DDLOCK_WAIT | WRITEONLY
constexpr std::uint32_t kLockReadOnly = 0x810;              // DDLOCK_WAIT | READONLY
constexpr std::uint32_t kBlitFlags = 0x1000200;

// DDSURFACEDESC2 and DDPIXELFORMAT, as retail's stack frames hold them
constexpr std::uint32_t kDescSize = 0x7c;
constexpr std::uint32_t kDescFlags = 0x04;
constexpr std::uint32_t kDescHeight = 0x08;
constexpr std::uint32_t kDescWidth = 0x0c;
constexpr std::uint32_t kDescPitch = 0x10;
constexpr std::uint32_t kDescSurface = 0x24;
constexpr std::uint32_t kDescPixelFormat = 0x48;
constexpr std::uint32_t kDescCaps = 0x68;
constexpr std::uint32_t kDescCaps2 = 0x6c;
constexpr std::uint32_t kFormatSize = 0x20;
constexpr std::uint32_t kFormatRed = 0x10;
constexpr std::uint32_t kFormatGreen = 0x14;
constexpr std::uint32_t kFormatBlue = 0x18;

// one 16-bit pixel from three 8-bit channels (0x004d95xx): each channel loses its low (8 - width) bits and moves to its mask's lowest bit; bit 15 is set
std::uint16_t pack_pixel(std::uint16_t blue, std::uint16_t green, std::uint16_t red, const std::uint32_t widths[3], const std::uint32_t shifts[3]) {
    const auto narrow = [](std::uint16_t value, std::uint32_t width) -> std::uint32_t {
        const std::uint32_t count = (8u - width) & 0x1fu;     // a 16-bit shift by 16 or more leaves nothing
        return count >= 16 ? 0u : static_cast<std::uint32_t>(value >> count);
    };
    const std::uint32_t pixel = (narrow(blue, widths[2]) << (shifts[2] & 0x1f)) | (narrow(green, widths[1]) << (shifts[1] & 0x1f)) |
                                (narrow(red, widths[0]) << (shifts[0] & 0x1f)) | 0x8000u;
    return static_cast<std::uint16_t>(pixel);
}

}  // namespace

std::uint32_t TextureKit::call(std::uint32_t address, std::vector<std::uint32_t> args) {
    char name[24];
    std::snprintf(name, sizeof name, "engine_%08x", address);
    s_.note(name, std::move(args));
    return static_cast<std::uint32_t>(s_.value(name));
}

std::uint32_t TextureKit::surface_call(const char* name, std::vector<std::uint32_t> args) {
    s_.note(name, std::move(args));
    return static_cast<std::uint32_t>(s_.value(name));
}

void TextureKit::fail(std::uint32_t result, std::uint32_t line, const char* file) const {
    char text[96];
    std::snprintf(text, sizeof text, "%s(%u): the surface layer answered 0x%08x", file, line, result);
    throw ServiceFault(text);
}

std::uint32_t TextureKit::release_surface(std::uint32_t surface) {
    std::uint32_t result = surface_call("ddraw_surface_release", {surface});
    if (result == 1) {
        result = surface_call("ddraw_device_set_texture", {0, 0});
    }
    return result;
}

// the description of a surface the texture layer creates: size, the device's pixel format, the caps (0x004d8fa6 and 0x004d907a)
void TextureKit::make_surface_desc(std::uint32_t desc, std::uint32_t width, std::uint32_t height, std::uint32_t caps, std::uint32_t caps2) {
    m_.fill(desc, 0, kDescSize);
    set_word_at(m_, desc, kDescSize);
    set_word_at(m_, desc + kDescFlags, kDescFlagsTexture);
    set_word_at(m_, desc + kDescHeight, height);
    set_word_at(m_, desc + kDescWidth, width);
    m_.copy(desc + kDescPixelFormat, kPixelFormat, kFormatSize);
    set_word_at(m_, desc + kDescCaps, caps);
    set_word_at(m_, desc + kDescCaps2, caps2);
}

// 0x004d8df0: open the bitmap of a texture. A checksum names NEWTEX\<level>\<checksum>.BMP when a level is chosen (and the configured root or texture mode 1 says so), else
// NEWTEX\<checksum>.BMP; a name (no checksum) names NEWBMP\<the part after its first backslash>.
std::uint32_t TextureKit::open_image(std::uint32_t checksum, std::uint32_t name) {
    if (checksum == 0) {
        if (name == 0) {
            return 0xffffffffu;
        }
        const std::string text = m_.cstring(name);
        std::uint32_t at = name;
        if (text.find('\\') != std::string::npos) {
            while (byte_at(m_, at) != '\\') {
                ++at;
            }
            ++at;
        }
        call(seam::crt::CRT_sprintf, {kPath, kFormatNamePath, kNewBmp, at});
        return call(kBackendOpen, {kPath, 0, 0});
    }
    if (global_byte(m_, kConfiguredRoot) == 0 && global_word(m_, kTextureMode) != 1) {
        return 0xffffffffu;
    }
    if (m_.cstring(kLevelName) != m_.cstring(kEmptyName)) {
        call(seam::crt::CRT_sprintf, {kPath, kFormatLevelPath, kNewTex, kLevelName, checksum});
        const std::uint32_t handle = call(kBackendOpen, {kPath, 0, 0});
        if (static_cast<std::int32_t>(handle) >= 0) {
            return handle;
        }
    }
    call(seam::crt::CRT_sprintf, {kPath, kFormatPath, kNewTex, checksum});
    return call(kBackendOpen, {kPath, 0, 0});
}

// 0x004d8990: give the record a copy of its surface's pixels in memory (the software renderer reads them there), and let the surface go unless it is to be kept
void TextureKit::ensure_source_pixels(std::uint32_t record, std::uint32_t keep_surface) {
    const std::uint32_t flags = word_at(m_, record + 0x10);
    if ((flags & kOwnPixels) != 0) {
        if (global_word(m_, kSoftwareRenderer) == 0) {
            call(seam::crt::FUN_005002f3, {word_at(m_, record + 8)});
        } else {
            if ((flags & kSoftwareCopy) != 0) {
                call(seam::crt::FUN_005002f3, {word_at(m_, record + 0xc)});
            }
            set_word_at(m_, record + 0xc, word_at(m_, record + 8));
            set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) | kSoftwareCopy);
        }
    }
    const std::uint32_t width = static_cast<std::uint32_t>(short_at(m_, record + 0x18));
    const std::uint32_t height = static_cast<std::uint32_t>(short_at(m_, record + 0x1a));
    const std::uint32_t copy = call(seam::crt::CRT_operator_new, {height * width * 2});
    set_word_at(m_, record + 8, copy);
    m_.fill(kLock, 0, kDescSize);
    set_global_word(m_, kLock, kDescSize);
    check(surface_call("ddraw_surface_lock", {word_at(m_, record), 0, kLock, kLockReadOnly}), 0x4fe);
    std::uint32_t from = global_word(m_, kLock + kDescSurface);
    const std::uint32_t pitch = global_word(m_, kLock + kDescPitch);
    const std::uint32_t locked_width = global_word(m_, kLock + kDescWidth);
    std::uint32_t to = copy;
    for (std::int32_t row = 0; row < short_at(m_, record + 0x1a); ++row) {
        m_.copy(to, from, static_cast<std::uint32_t>(short_at(m_, record + 0x18)) * 2);
        to += locked_width * 2;
        from += pitch;
    }
    check(surface_call("ddraw_surface_unlock", {word_at(m_, record), 0}), 0x50d);
    if (keep_surface == 0) {
        check(surface_call("ddraw_surface_release", {word_at(m_, record)}), 0x512);
        set_word_at(m_, record, 0);
    }
}

// 0x004d9310: a 24-bit bitmap's pixels into the (locked) surface in the device's 16-bit format, bottom row first as a bitmap stores them. A pixel of (R > 0xf7, G = 0,
// B > 0xf7) is the key colour: it is written as 0 and the record loses its "no transparent pixel" flag. A keyed record (0x80) gets mid grey in every pixel.
void TextureKit::upload_rgb_pixels(std::uint32_t surface, std::uint32_t record) {
    const bool keyed = ((word_at(m_, record + 0x10) >> 7) & 1) != 0;
    m_.fill(kFormat, 0, kFormatSize);
    set_global_word(m_, kFormat, kFormatSize);
    check(surface_call("ddraw_surface_pixel_format", {surface, kFormat}), 0x407);
    const std::uint32_t masks[3] = {global_word(m_, kFormat + kFormatRed), global_word(m_, kFormat + kFormatGreen), global_word(m_, kFormat + kFormatBlue)};
    std::uint32_t widths[3];
    std::uint32_t shifts[3];
    for (int k = 0; k < 3; ++k) {
        widths[k] = call(kBitCount, {masks[k]});
    }
    for (int k = 0; k < 3; ++k) {
        shifts[k] = call(kLowestBit, {masks[k]});
    }
    const std::uint32_t handle = (word_at(m_, record + 0x10) & kNamedFile) != 0 ? open_image(0, word_at(m_, record + 8)) : open_image(word_at(m_, record + 0xc), 0);
    call(kBackendRead, {handle, kHeader, 0xe});
    call(kBackendRead, {handle, kInfo, 0x28});
    const std::uint32_t width = global_word(m_, kInfo + 4);
    const std::uint32_t height = global_word(m_, kInfo + 8);
    const std::uint32_t row_bytes = ((width + 1) * 3) & ~3u;
    const std::uint32_t row_buffer = call(seam::crt::CRT_operator_new, {row_bytes});
    m_.fill(kLock, 0, kDescSize);
    set_global_word(m_, kLock, kDescSize);
    check(surface_call("ddraw_surface_lock", {surface, 0, kLock, kLockWriteOnly}), 0x428);
    const std::uint32_t pitch = global_word(m_, kLock + kDescPitch);
    std::uint32_t row_start = (height - 1) * pitch + global_word(m_, kLock + kDescSurface);
    for (std::int32_t row = static_cast<std::int32_t>(height); row > 0; --row) {
        call(kBackendRead, {handle, row_buffer, row_bytes});
        std::uint32_t dst = row_start;
        std::uint32_t src = row_buffer;
        for (std::uint32_t column = 0; column < width; ++column) {
            std::uint16_t blue = 0x80;
            std::uint16_t green = 0x80;
            std::uint16_t red = 0x80;
            bool key = false;
            if (!keyed) {
                blue = byte_at(m_, src);
                green = byte_at(m_, src + 1);
                red = byte_at(m_, src + 2);
                key = red > 0xf7 && green == 0 && blue > 0xf7;
            }
            if (key) {
                set_half_at(m_, dst, 0);
                set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) & ~kOpaque);
            } else {
                set_half_at(m_, dst, pack_pixel(blue, green, red, widths, shifts));
            }
            dst += 2;
            src += 3;
        }
        row_start -= pitch;
    }
    check(surface_call("ddraw_surface_unlock", {surface, 0}), 0x459);
    call(seam::crt::FUN_005002f3, {row_buffer});
    call(kBackendClose, {handle});
}

// 0x004d9630: the record's indexed pixels (4 bits, two to a byte low nibble first, or 8 bits) through the palette cache record's 16-bit entries into the surface, top row
// first. An entry of 0 clears the "no transparent pixel" flag. The source rows are padded (a 4-bit row to ((width + 3) & ~3) / 2 bytes, an 8-bit row to an even count).
void TextureKit::upload_indexed_pixels(std::uint32_t surface, std::uint32_t record, std::uint32_t palette) {
    const std::int32_t height = short_at(m_, record + 0x16);
    const std::int32_t width = short_at(m_, record + 0x14);
    m_.fill(kLock, 0, kDescSize);
    set_global_word(m_, kLock, kDescSize);
    check(surface_call("ddraw_surface_lock", {surface, 0, kLock, kLockWriteOnly}), 0x478);
    std::uint32_t src = word_at(m_, record + 8);
    set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) | kOpaque);
    const std::uint32_t pitch = global_word(m_, kLock + kDescPitch);
    std::uint32_t row_start = global_word(m_, kLock + kDescSurface);
    const std::uint32_t count = word_at(m_, palette + 4);
    const std::uint32_t entries = word_at(m_, palette + 8);
    const auto entry = [&](std::uint32_t index) {
        const std::uint32_t value = half_at(m_, entries + index * 2);
        if (value == 0) {
            set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) & ~kOpaque);
        }
        return value;
    };
    if (count == 0x10) {
        const std::int32_t bytes_per_row = static_cast<std::int32_t>((static_cast<std::uint32_t>(width) + 3) & ~3u) >> 1;
        for (std::int32_t row = height; row > 0; --row) {
            std::uint32_t dst = row_start;
            std::int32_t remaining = width;
            for (std::int32_t k = 0; k < bytes_per_row; ++k) {
                const std::uint8_t byte = byte_at(m_, src);
                if (remaining != 0) {
                    set_half_at(m_, dst, entry(byte & 0xf));
                    dst += 2;
                    if (--remaining != 0) {
                        set_half_at(m_, dst, entry(byte >> 4));
                        dst += 2;
                        --remaining;
                    }
                }
                ++src;
            }
            row_start += pitch;
        }
    } else if (count == 0x100) {
        const std::int32_t bytes_per_row = static_cast<std::int32_t>((static_cast<std::uint32_t>(width) + 1) & ~1u);
        for (std::int32_t row = height; row > 0; --row) {
            std::uint32_t dst = row_start;
            std::int32_t remaining = width;
            for (std::int32_t k = 0; k < bytes_per_row; ++k) {
                const std::uint8_t byte = byte_at(m_, src);
                if (remaining != 0) {
                    set_half_at(m_, dst, entry(byte));
                    dst += 2;
                    --remaining;
                }
                ++src;
            }
            row_start += pitch;
        }
    }
    check(surface_call("ddraw_surface_unlock", {surface, 0}), 0x4d2);
}

// 0x004d8f10: make the record's pixels the texture the device draws from. The record's palette cache record is found by checksum when it has none; its pixels go through a
// staging surface (the bitmap's, or the indexed ones through the palette), and the texture surface is created when the record has none, sized to what the device accepts
// (a power of two, square, within its limits) and blitted from the staging one (stretching when the sizes differ). `replace` blits into the part the source covers
// and records the sizes the surface ended up with; the software renderer's copy of the pixels is kept when the device says it draws from memory.
void TextureKit::upload_texture(std::uint32_t record, std::uint32_t palette_checksum, std::uint32_t keep_surface, std::uint32_t replace) {
    if (word_at(m_, record + 0x20) == 0) {
        std::uint32_t node = global_word(m_, kPaletteHead);
        std::uint32_t found = 0;
        for (; node != 0; node = word_at(m_, node + 0x10)) {
            if (word_at(m_, node) == palette_checksum) {
                found = node;
                set_word_at(m_, record + 0x20, node);
                break;
            }
        }
        if (word_at(m_, record + 0x20) == 0) {
            call(seam::hal::printf_log, {kMsgPaletteMissing, palette_checksum});
            set_word_at(m_, record + 0x20, global_word(m_, kPaletteHead));
        }
        const std::uint32_t entries = found != 0 ? word_at(m_, found + 0xc) : 0;
        if (entries != 0 && half_at(m_, entries) == 0) {
            set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) | kFirstEntryClear);
        }
    }
    const std::uint32_t source_height = static_cast<std::uint32_t>(short_at(m_, record + 0x16));
    const std::uint32_t source_width = static_cast<std::uint32_t>(short_at(m_, record + 0x14));
    make_surface_desc(kDescStaging, source_width, source_height, kCapsStaging, 0);
    check(surface_call("ddraw_create_surface", {kDescStaging, kOut}), 0x629);
    const std::uint32_t staging = global_word(m_, kOut);
    if ((word_at(m_, record + 0x10) & kBitmap24) != 0) {
        upload_rgb_pixels(staging, record);
    } else {
        upload_indexed_pixels(staging, record, word_at(m_, record + 0x20));
    }
    // retail writes each staging surface as a BMP here (D3DTex_SaveSurfaceAsBMP, a debug option of the original developers' build and out of scope): not reproduced
    if (global_word(m_, kSaveBitmaps) != 0) {
        throw ServiceFault("D3DTex_UploadTexture: the texture dump switch (0x006a0544) is set; the debug dump is not part of a standalone program");
    }
    std::uint32_t width = source_width;
    std::uint32_t height = source_height;
    if (word_at(m_, record) == 0) {
        width = static_cast<std::uint32_t>(short_at(m_, record + 0x14));
        height = static_cast<std::uint32_t>(short_at(m_, record + 0x16));
        if (global_word(m_, kPowerOfTwo) != 0) {
            std::uint32_t w = 1;
            while (w < width) {
                w <<= 1;
            }
            std::uint32_t h = 1;
            while (h < height) {
                h <<= 1;
            }
            width = w;
            height = h;
        }
        if (global_word(m_, kSquare) != 0) {
            // both sides become the larger one
            if (height < width) {
                height = width;
            } else {
                width = height;
            }
        }
        if (width > global_word(m_, kMaxWidth) || height > global_word(m_, kMaxHeight)) {
            call(seam::hal::printf_log, {kMsgTooBig});
            width = static_cast<std::uint32_t>(short_at(m_, record + 0x14));
            height = static_cast<std::uint32_t>(short_at(m_, record + 0x16));
        }
        width = std::max(width, global_word(m_, kMinWidth));
        height = std::max(height, global_word(m_, kMinHeight));
        set_half_at(m_, record + 0x18, width);
        set_half_at(m_, record + 0x1a, height);
        make_surface_desc(kDescTexture, width, height, kCapsManaged, kCaps2Managed);
        check(surface_call("ddraw_create_surface", {kDescTexture, record}), 0x684);
    }
    if (replace != 0) {
        const std::uint32_t rect_width = static_cast<std::uint32_t>(short_at(m_, record + 0x14));
        const std::uint32_t rect_height = static_cast<std::uint32_t>(short_at(m_, record + 0x16));
        set_global_word(m_, kRect, 0);
        set_global_word(m_, kRect + 4, 0);
        set_global_word(m_, kRect + 8, rect_width);
        set_global_word(m_, kRect + 12, rect_height);
        check(surface_call("ddraw_surface_blt", {word_at(m_, record), kRect, staging, 0}), 0x692);
        if ((word_at(m_, record + 0x10) & kPlanar) == 0) {
            set_half_at(m_, record + 0x14, width);
            set_half_at(m_, record + 0x16, height);
        } else {
            set_half_at(m_, record + 0x1c, width);
            set_half_at(m_, record + 0x1e, height);
        }
    } else {
        check(surface_call("ddraw_surface_blt", {word_at(m_, record), 0, staging, 0}), 0x6a3);
    }
    check(surface_call("ddraw_surface_release", {staging}), 0x6a7);
    if (global_word(m_, kSoftwareRenderer) != 0) {
        ensure_source_pixels(record, keep_surface);
    }
    if ((word_at(m_, record + 0x10) & kPlanar) != 0) {
        const std::uint16_t width_a = half_at(m_, record + 0x14);
        const std::uint16_t height_a = half_at(m_, record + 0x16);
        const std::uint16_t width_b = half_at(m_, record + 0x1c);
        const std::uint16_t height_b = half_at(m_, record + 0x1e);
        set_half_at(m_, record + 0x1c, width_a);
        set_half_at(m_, record + 0x1e, height_a);
        set_half_at(m_, record + 0x14, width_b);
        set_half_at(m_, record + 0x16, height_b);
    }
}

// 0x004d8b50: release a record's surface and unlink it from the list (head 0x006a051c, tail 0x006a0520, links +0x24 next / +0x28 previous), free its pixels, its palette
// entry when it owns one (flag 0x20) and the record. The scene material the record was attached to forgets it, unless the byte 0x029da3a5 says not to touch it.
std::uint32_t TextureKit::destroy_record(std::uint32_t record) {
    if (word_at(m_, record) != 0) {
        check(release_surface(word_at(m_, record)), 0x72e);
    }
    if (global_byte(m_, kNoMaterialLink) == 0) {
        call(kAttachToMaterial, {word_at(m_, record + 4), 0});
    }
    if (record == global_word(m_, kListHead)) {
        set_global_word(m_, kListHead, word_at(m_, global_word(m_, kListHead) + 0x24));
    }
    if (const std::uint32_t previous = word_at(m_, record + 0x28); previous != 0) {
        set_word_at(m_, previous + 0x24, word_at(m_, record + 0x24));
    }
    if (const std::uint32_t next = word_at(m_, record + 0x24); next != 0) {
        set_word_at(m_, next + 0x28, word_at(m_, record + 0x28));
    }
    if (record == global_word(m_, kListTail)) {
        set_global_word(m_, kListTail, word_at(m_, record + 0x28));
    }
    set_global_word(m_, kRecordCount, global_word(m_, kRecordCount) - 1);
    if (global_word(m_, kSoftwareRenderer) != 0) {
        call(seam::crt::FUN_005002f3, {word_at(m_, record + 8)});
    }
    const std::uint32_t flags = word_at(m_, record + 0x10);
    if ((flags & kOwnPixels) != 0) {
        call(seam::crt::FUN_005002f3, {(flags & kSoftwareCopy) != 0 ? word_at(m_, record + 0xc) : word_at(m_, record + 8)});
    }
    if ((byte_at(m_, record + 0x10) & 0x20) != 0) {
        call(kFreePaletteEntry, {word_at(m_, record + 0x20), 1});
    }
    call(seam::crt::FUN_005002f3, {record});
    return global_word(m_, kListHead);
}

// 0x004d9f50: turn the record's pixels grey in place: the sum of the three channels' raw values (not scaled to eight bits) times 550 / 3000 (retail divides by multiplying with
// 0x057619f1 and keeping the bits from 38 up) goes into every colour channel, the alpha bit stays. The hardware path locks the surface; with the software renderer the record's
// own pixels (+8) are changed, and retail takes the channel shifts from stack words it never wrote there: the 1555 layout its masks describe is used.
void TextureKit::gray_texture(std::uint32_t record) {
    const bool software = global_word(m_, kSoftwareRenderer) != 0;
    std::uint32_t mask_red;
    std::uint32_t mask_green;
    std::uint32_t mask_blue;
    std::uint32_t mask_alpha;
    std::uint32_t shift_red;
    std::uint32_t shift_green;
    std::uint32_t shift_blue;
    std::uint32_t shift_alpha;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t row_start;
    std::uint32_t row_step;
    if (!software) {
        m_.fill(kFormat, 0, kFormatSize);
        set_global_word(m_, kFormat, kFormatSize);
        check(surface_call("ddraw_surface_pixel_format", {word_at(m_, record), kFormat}), 0xa6a);
        if (global_word(m_, kFormat + 0xc) < 0x10) {
            return;
        }
        mask_red = global_word(m_, kFormat + kFormatRed);
        mask_green = global_word(m_, kFormat + kFormatGreen);
        mask_blue = global_word(m_, kFormat + kFormatBlue);
        mask_alpha = global_word(m_, kFormat + 0x1c);
        // (retail also counts the bits of each mask and drops the answers)
        shift_alpha = call(kLowestBit, {mask_alpha});
        shift_red = call(kLowestBit, {mask_red});
        shift_green = call(kLowestBit, {mask_green});
        shift_blue = call(kLowestBit, {mask_blue});
        m_.fill(kLock, 0, kDescSize);
        set_global_word(m_, kLock, kDescSize);
        check(surface_call("ddraw_surface_lock", {word_at(m_, record), 0, kLock, kLockWriteOnly}), 0xa8a);
        height = global_word(m_, kLock + kDescHeight);
        width = global_word(m_, kLock + kDescWidth);
        row_start = global_word(m_, kLock + kDescSurface);
        row_step = global_word(m_, kLock + kDescPitch);
    } else {
        width = static_cast<std::uint32_t>(short_at(m_, record + 0x18));
        height = static_cast<std::uint32_t>(short_at(m_, record + 0x1a));
        row_start = word_at(m_, record + 8);
        row_step = width * 2;
        mask_alpha = 0x8000;
        mask_red = 0x7c00;
        mask_green = 0x3e0;
        mask_blue = 0x1f;
        shift_alpha = 15;
        shift_red = 10;
        shift_green = 5;
        shift_blue = 0;
    }
    for (std::uint32_t row = 0; row < height; ++row) {
        std::uint32_t at = row_start;
        for (std::uint32_t column = 0; column < width; ++column, at += 2) {
            const std::uint32_t pixel = half_at(m_, at);
            const std::uint32_t sum = ((pixel & mask_blue) >> (shift_blue & 31)) + ((pixel & mask_green) >> (shift_green & 31)) + ((pixel & mask_red) >> (shift_red & 31));
            const auto grey = static_cast<std::uint32_t>((static_cast<std::uint64_t>(sum * 550u) * 0x057619f1u) >> 38);
            std::uint32_t out = (((pixel & mask_alpha) >> (shift_alpha & 31)) << (shift_alpha & 31)) & mask_alpha;
            out |= (grey << (shift_blue & 31)) & mask_blue;
            out |= (grey << (shift_green & 31)) & mask_green;
            out |= (grey << (shift_red & 31)) & mask_red;
            set_half_at(m_, at, out);
        }
        row_start += row_step;
    }
    if (!software) {
        check(surface_call("ddraw_surface_unlock", {word_at(m_, record), 0}), 0xac7);
    }
}

namespace {

// a 16-bit `shr` by a count that x86 takes modulo 32: from 16 up nothing is left
std::uint32_t shift_right16(std::uint32_t value, std::uint32_t count) {
    count &= 31u;
    return count >= 16 ? 0u : (value & 0xffffu) >> count;
}

}  // namespace

// 0x004d9950: load a paletted bitmap held in memory (`image`, a BMP file image) into a texture record: a new one, or `existing`, which loses what it held first. A file of the
// same name (`name`, found by OpenImage) replaces the bitmap: a 24-bit one stays on disk and is read when the texture is uploaded (the record keeps the name, flags 0x48);
// another is read whole and replaces `image`. A paletted bitmap becomes a top-down copy with rows padded to four bytes (record +8, flag 1) and gets a palette cache record: its
// 0x100 (or the bitmap's count of) colours as 16-bit entries in the device's format (+8) and in a fixed 1555 layout with byte 0 of an entry in the top bits (+0xc), the key
// colour (R > 0xf7, G = 0, B > 0xf7) clear in both; a keyed record (flag 0x80) gets mid grey. The texture is uploaded. A record that existed keeps its old palette
// cache record when its size is the new one's (the entries are copied into it), and the new one is freed.
std::uint32_t TextureKit::load_indexed_bitmap(std::uint32_t image, std::uint32_t existing, std::uint32_t keep_surface, std::uint32_t name) {
    const std::uint32_t software = global_word(m_, kSoftwareRenderer);
    std::uint32_t record;
    std::uint32_t palette;
    std::uint32_t old_palette = 0;
    std::uint32_t file_copy = 0;
    bool keyed = false;
    if (existing == 0) {
        record = call(kAllocRecord);
        palette = call(kAllocPalette);
        set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) | 0x20);
    } else {
        record = existing;
        const std::uint32_t flags = word_at(m_, record + 0x10);
        keyed = ((flags >> 7) & 1) != 0;
        if ((flags & kOwnPixels) != 0 && (flags & kBitmap24) == 0) {
            if (software == 0) {
                call(seam::crt::FUN_005002f3, {word_at(m_, record + 8)});
            }
            if ((word_at(m_, record + 0x10) & kSoftwareCopy) != 0) {
                call(seam::crt::FUN_005002f3, {word_at(m_, record + 0xc)});
            }
        }
        if (software != 0) {
            call(seam::crt::FUN_005002f3, {word_at(m_, record + 8)});
        }
        const std::uint32_t held = word_at(m_, record + 0x20);
        set_word_at(m_, record + 0x10, 0);
        if (held == 0) {
            set_word_at(m_, record + 0x10, 0x20);
            palette = call(kAllocPalette);
        } else {
            old_palette = held;
            palette = call(kAllocPalette);
        }
    }
    std::uint32_t pixels_image = image;                                  // the bitmap the pixels are taken from (the file's copy when one is read)
    std::uint32_t info = image + 0xe;
    std::uint32_t width = word_at(m_, image + 0x12);
    std::uint32_t height = word_at(m_, image + 0x16);
    const std::uint32_t handle = open_image(0, name);
    if (handle != 0xffffffffu) {
        call(kBackendRead, {handle, kHeader, 0xe});
        call(kBackendRead, {handle, kInfo, 0x28});
        set_half_at(m_, record + 0x1c, width);
        set_half_at(m_, record + 0x1e, height);
        const std::uint32_t flags = word_at(m_, record + 0x10);
        if (global_half(m_, kInfo + 0xe) == 0x18) {
            set_word_at(m_, record + 8, name);
            set_word_at(m_, record + 0x10, flags | 0x48);
            width = global_word(m_, kInfo + 4);
            height = global_word(m_, kInfo + 8);
        } else {
            set_word_at(m_, record + 0x10, flags | 4);
            const std::uint32_t size = global_word(m_, kHeader + 2);
            file_copy = call(seam::crt::CRT_operator_new, {size});
            call(kBackendSeek, {handle, 0, 0});
            if (call(kBackendRead, {handle, file_copy, size}) != size) {
                call(seam::hal::printf_log, {kMsgWontRead, name});
            }
            pixels_image = file_copy;
            info = file_copy + 0xe;
            width = word_at(m_, file_copy + 0x12);
            height = word_at(m_, file_copy + 0x16);
        }
        call(kBackendClose, {handle});
        set_half_at(m_, record + 0x14, global_half(m_, kInfo + 4));
        set_half_at(m_, record + 0x16, global_half(m_, kInfo + 8));
        if (word_at(m_, record) != 0) {
            check(release_surface(word_at(m_, record)), 0x88a);
            set_word_at(m_, record, 0);
        }
    }
    std::uint32_t device_entries = 0;
    std::uint32_t fixed_entries = 0;
    if ((word_at(m_, record + 0x10) & kBitmap24) == 0) {
        const std::uint32_t bits = half_at(m_, info + 0xe);
        s_.check(bits == 8, kMsgNon8Bit);
        std::int32_t colours = static_cast<std::int32_t>(word_at(m_, info + 0x20));
        if (colours == 0) {
            colours = 0x100;
        }
        std::uint32_t entry = pixels_image + 0x36;
        device_entries = call(seam::crt::CRT_operator_new, {0x200});
        fixed_entries = call(seam::crt::CRT_operator_new, {0x200});
        for (std::int32_t k = 0; k < colours; ++k, entry += 4) {
            std::uint32_t blue = 0x80;                                   // byte 0 of the entry (a BMP palette entry is B, G, R, 0)
            std::uint32_t green = 0x80;                                  // byte 1
            std::uint32_t red = 0x80;                                    // byte 2
            std::uint32_t alpha = 0xff;
            std::uint16_t fixed;
            if (!keyed) {
                blue = byte_at(m_, entry);
                green = byte_at(m_, entry + 1);
                red = byte_at(m_, entry + 2);
            }
            if (!keyed && red > 0xf7 && green == 0 && blue > 0xf7) {
                red = green = blue = alpha = 0;
                fixed = 0;
            } else if ((blue >> 3) == 0 && (green >> 3) == 0 && (red >> 3) == 0) {
                fixed = 0x8000;
            } else {
                fixed = static_cast<std::uint16_t>((((blue >> 3) * 32 + (green >> 3)) * 32 + (red >> 3)) | 0x8000);   // byte 0 in the top bits
            }
            set_half_at(m_, fixed_entries + 2 * k, fixed);
            const std::uint32_t pixel = (shift_right16(red, 8 - global_word(m_, kRedBits)) << (global_word(m_, kRedShift) & 31)) |
                                        (shift_right16(alpha, 8 - global_word(m_, kAlphaBits)) << (global_word(m_, kAlphaShift) & 31)) |
                                        (shift_right16(green, 8 - global_word(m_, kGreenBits)) << (global_word(m_, kGreenShift) & 31)) |
                                        (shift_right16(blue, 8 - global_word(m_, kBlueBits)) << (global_word(m_, kBlueShift) & 31));
            set_half_at(m_, device_entries + 2 * k, pixel);
        }
        set_word_at(m_, palette + 8, device_entries);
        set_word_at(m_, palette, 0xffffffffu);
        set_word_at(m_, palette + 4, 0x100);
        set_word_at(m_, palette + 0xc, fixed_entries);
        // the rows padded to a multiple of four bytes, stored top row first
        std::int32_t padding = static_cast<std::int32_t>(width & 0x80000003u);
        if (padding < 0) {
            padding = ((padding - 1) | -4) + 1;
        }
        if (padding > 0) {
            padding = 4 - padding;
        }
        width += static_cast<std::uint32_t>(padding);
        const std::uint32_t buffer = call(seam::crt::CRT_operator_new, {(width + 3) * height});
        std::uint32_t from = word_at(m_, pixels_image + 0xa) + pixels_image;
        std::uint32_t to = (height - 1) * width + buffer;
        for (std::int32_t row = static_cast<std::int32_t>(height); row > 0; --row) {
            const std::int32_t copied = static_cast<std::int32_t>(width) - padding;
            if (copied > 0) {
                m_.copy(to, from, static_cast<std::uint32_t>(copied));
                to += static_cast<std::uint32_t>(copied);
                from += static_cast<std::uint32_t>(copied);
            }
            if (padding > 0) {
                m_.fill(to, 0, static_cast<std::uint32_t>(padding));
                from += static_cast<std::uint32_t>(padding);
                to += static_cast<std::uint32_t>(padding);
            }
            to -= 2 * width;
        }
        set_word_at(m_, record + 8, buffer);
        set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) | kOwnPixels);
    }
    if (existing == 0) {
        set_word_at(m_, record + 0xc, 0);
    }
    if ((word_at(m_, record + 0x10) & kPlanar) == 0) {
        set_half_at(m_, record + 0x14, width);
        set_half_at(m_, record + 0x16, height);
    }
    set_word_at(m_, record + 0x20, palette);
    set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) | kOpaque);
    if (file_copy != 0) {
        call(seam::crt::FUN_005002f3, {file_copy});
    }
    upload_texture(record, word_at(m_, palette), keep_surface, 0);
    if (existing != 0 && (word_at(m_, record + 0x10) & kBitmap24) == 0 && old_palette != 0) {
        const std::uint32_t count = word_at(m_, old_palette + 4);
        if (count == word_at(m_, palette + 4)) {
            m_.copy(word_at(m_, old_palette + 8), device_entries, count * 2);
            m_.copy(word_at(m_, old_palette + 0xc), fixed_entries, count * 2);
        } else {
            call(seam::hal::printf_log, {kMsgPaletteSizes, name, count, word_at(m_, palette + 4)});
        }
        set_word_at(m_, record + 0x20, old_palette);
        call(kFreePaletteEntry, {palette, 1});
    }
    return record;
}

// 0x004d83a0: the tiles of a surface. One that fits the device's limits (0x006a0534 by 0x006a0538) is its own single tile: the record is copied (and freed), the rectangle is
// its size, and with the software renderer the surface is let go (the renderer draws from memory). A larger one is copied to a system-memory surface and cut into
// columns of limit-wide tiles, each (up to limit-high, in a column) a new managed texture of the size the device accepts, with a record (flags and size from the original,
// +0x18/+0x1a the realized size) and the rectangle of the original it came from; the original is let go. Answers the number of tiles.
std::uint32_t TextureKit::build_tiles(std::uint32_t record, std::uint32_t out_tiles, std::uint32_t out_rectangles) {
    const std::uint32_t software = global_word(m_, kSoftwareRenderer);
    m_.fill(kDescStaging, 0, kDescSize);
    set_global_word(m_, kDescStaging, kDescSize);
    check_sprite(surface_call("ddraw_surface_desc", {word_at(m_, record), kDescStaging}), 0x533);
    const std::uint32_t width = global_word(m_, kDescStaging + kDescWidth);
    const std::uint32_t height = global_word(m_, kDescStaging + kDescHeight);
    const std::uint32_t limit_width = global_word(m_, kMaxWidth);
    const std::uint32_t limit_height = global_word(m_, kMaxHeight);
    if (width <= limit_width && height <= limit_height) {
        const std::uint32_t tile = call(seam::crt::CRT_operator_new, {0x2c});
        set_word_at(m_, out_tiles, tile);
        m_.copy(tile, record, 0x2c);
        const std::uint32_t rectangle = call(seam::crt::CRT_operator_new, {0x10});
        set_word_at(m_, out_rectangles, rectangle);
        set_word_at(m_, rectangle, 0);
        set_word_at(m_, rectangle + 4, 0);
        set_word_at(m_, rectangle + 8, static_cast<std::uint32_t>(short_at(m_, record + 0x14)));
        set_word_at(m_, rectangle + 12, static_cast<std::uint32_t>(short_at(m_, record + 0x16)));
        call(seam::crt::FUN_005002f3, {record});
        if (software != 0) {
            check(surface_call("ddraw_surface_release", {word_at(m_, tile)}), 0x547);
            set_word_at(m_, tile, 0);
        }
        return 1;
    }
    if (limit_width == 0 || limit_height == 0) {
        throw ServiceFault("pc\\d3dsprite.cpp: the device's texture limit is zero; retail divides by it");
    }
    if (software != 0) {
        call(seam::crt::FUN_005002f3, {word_at(m_, record + 8)});
    }
    set_global_word(m_, kDescStaging + kDescCaps, kCapsStaging);
    set_global_word(m_, kDescStaging + kDescCaps2, 0);
    check_sprite(surface_call("ddraw_create_surface", {kDescStaging, kOut}), 0x558);
    const std::uint32_t staging = global_word(m_, kOut);
    check_sprite(surface_call("ddraw_surface_blt", {staging, 0, word_at(m_, record), 0}), 0x55c);
    check_sprite(surface_call("ddraw_surface_release", {word_at(m_, record)}), 0x560);
    std::uint32_t columns = width / limit_width;
    if (columns * limit_width < width) {
        ++columns;
    }
    std::uint32_t rows = height / limit_height;
    if (rows * limit_height < height) {
        ++rows;
    }
    const std::uint32_t count = rows * columns;
    if (count == 0) {
        set_word_at(m_, out_tiles, 0);
        set_word_at(m_, out_rectangles, 0);
        return 0;
    }
    const std::uint32_t tiles = call(seam::crt::CRT_operator_new, {count * 0x2c});
    set_word_at(m_, out_tiles, tiles);
    m_.fill(tiles, 0, count * 0x2c);
    const std::uint32_t rectangles = call(seam::crt::CRT_operator_new, {count * 0x10});
    set_word_at(m_, out_rectangles, rectangles);
    m_.fill(rectangles, 0, count * 0x10);
    std::uint32_t index = 0;
    for (std::uint32_t column = 0; column < columns; ++column) {
        for (std::uint32_t row = 0; row < rows; ++row, ++index) {
            const std::uint32_t tile = tiles + index * 0x2c;
            const std::uint32_t rectangle = rectangles + index * 0x10;
            set_word_at(m_, tile + 0x10, word_at(m_, record + 0x10));
            set_half_at(m_, tile + 0x14, half_at(m_, record + 0x14));
            set_half_at(m_, tile + 0x16, half_at(m_, record + 0x16));
            const std::uint32_t left = limit_width * column;
            const std::uint32_t top = limit_height * row;
            std::uint32_t right = left + limit_width;
            if (static_cast<std::int32_t>(right) > static_cast<std::int32_t>(width)) {
                right = width;
            }
            std::uint32_t bottom = top + limit_height;
            if (static_cast<std::int32_t>(bottom) > static_cast<std::int32_t>(height)) {
                bottom = height;
            }
            set_word_at(m_, rectangle, left);
            set_word_at(m_, rectangle + 4, top);
            set_word_at(m_, rectangle + 8, right);
            set_word_at(m_, rectangle + 12, bottom);
            std::uint32_t tile_width = right - left;
            std::uint32_t tile_height = bottom - top;
            if (global_word(m_, kPowerOfTwo) != 0) {
                std::uint32_t w = 1;
                while (tile_width > 1 && w < tile_width) {
                    w <<= 1;
                }
                std::uint32_t h = 1;
                while (tile_height > 1 && h < tile_height) {
                    h <<= 1;
                }
                tile_width = w;
                tile_height = h;
            }
            if (global_word(m_, kSquare) != 0) {
                if (tile_height < tile_width) {
                    tile_height = tile_width;
                } else {
                    tile_width = tile_height;
                }
            }
            tile_width = std::max(tile_width, global_word(m_, kMinWidth));
            tile_height = std::max(tile_height, global_word(m_, kMinHeight));
            set_half_at(m_, tile + 0x18, tile_width);
            set_half_at(m_, tile + 0x1a, tile_height);
            make_surface_desc(kDescTexture, tile_width, tile_height, kCapsManaged, kCaps2Managed);
            check_sprite(surface_call("ddraw_create_surface", {kDescTexture, tile}), 0x5c7);
            check_sprite(surface_call("ddraw_surface_blt", {word_at(m_, tile), 0, staging, rectangle}), 0x5cb);
            if (software != 0) {
                ensure_source_pixels(tile, 0);
            }
        }
    }
    call(seam::crt::FUN_005002f3, {record});
    check_sprite(surface_call("ddraw_surface_release", {staging}), 0x5e5);
    return count;
}

// 0x004d74a0: a sprite from a bitmap (and the file of the same name, when there is one). The bitmap's record is loaded with its surface kept, its pixel copies and palette
// are let go and the bitmap is freed (the sprite is the only thing that keeps the texture); the record is cut into tiles, and each tile gets a zeroed scene material (0x2c bytes)
// that it is attached to. The scale is 0.9, or 0.01 for the background sprite, which is also remembered in 0x006a04c8. With the software renderer the background sprite is
// one tile: the surface is copied through a screen-sized system-memory surface into a buffer in memory (+0x18) with its 565 pixels reshuffled to the 1555-like layout.
std::uint32_t TextureKit::sprite_create(std::uint32_t sprite, std::uint32_t image, std::uint32_t name, std::uint32_t flag) {
    const bool background = (flag & 0xff) != 0;
    set_word_at(m_, sprite + 0x18, 0);
    if (background) {
        set_global_word(m_, kBackgroundSprite, sprite);
        set_word_at(m_, sprite + 0x14, 0x3c23d70a);
    } else {
        set_word_at(m_, sprite + 0x14, 0x3f666666);
    }
    const std::uint32_t blank = call(seam::crt::CRT_operator_new, {0x2c});
    m_.fill(blank, 0, 0x2c);
    set_word_at(m_, sprite, blank);
    const std::uint32_t record = load_indexed_bitmap(image, blank, 1, name);
    set_word_at(m_, sprite, record);
    const std::uint32_t software = global_word(m_, kSoftwareRenderer);
    if (software != 0) {
        call(seam::crt::FUN_005002f3, {word_at(m_, record + 0xc)});
    } else if ((word_at(m_, record + 0x10) & kBitmap24) == 0) {
        call(seam::crt::FUN_005002f3, {word_at(m_, record + 8)});
    }
    call(kFreePaletteEntry, {word_at(m_, record + 0x20), 1});
    call(kMemDelete, {image});
    if (software != 0 && global_word(m_, kBackgroundSprite) == sprite) {
        set_word_at(m_, sprite + 0xc, 1);
        const std::uint32_t tile = call(seam::crt::CRT_operator_new, {0x2c});
        set_word_at(m_, sprite + 4, tile);
        m_.copy(tile, record, 0x2c);
        const std::uint32_t rectangle = call(seam::crt::CRT_operator_new, {0x10});
        set_word_at(m_, sprite + 8, rectangle);
        set_word_at(m_, rectangle, 0);
        set_word_at(m_, rectangle + 4, 0);
        set_word_at(m_, rectangle + 8, static_cast<std::uint32_t>(short_at(m_, record + 0x14)));
        set_word_at(m_, rectangle + 12, static_cast<std::uint32_t>(short_at(m_, record + 0x16)));
        call(seam::crt::FUN_005002f3, {record});
        const std::uint32_t screen_width = global_word(m_, kScreenWidth);
        const std::uint32_t screen_height = global_word(m_, kScreenHeight);
        make_surface_desc(kDescStaging, screen_width, screen_height, kCapsStaging, 0);
        check_sprite(surface_call("ddraw_create_surface", {kDescStaging, kOut}), 0x11f);
        const std::uint32_t staging = global_word(m_, kOut);
        check_sprite(surface_call("ddraw_surface_blt", {staging, 0, word_at(m_, tile), 0}), 0x122);
        check_sprite(release_surface(word_at(m_, tile)), 0x129);
        set_word_at(m_, tile, 0);
        const std::uint32_t buffer = call(seam::crt::CRT_operator_new, {screen_height * screen_width * 2});
        set_word_at(m_, sprite + 0x18, buffer);
        m_.fill(kLock, 0, kDescSize);
        set_global_word(m_, kLock, kDescSize);
        check_sprite(surface_call("ddraw_surface_lock", {staging, 0, kLock, kLockReadOnly}), 0x136);
        std::uint32_t from = global_word(m_, kLock + kDescSurface);
        const std::uint32_t pitch = global_word(m_, kLock + kDescPitch);
        std::uint32_t to = buffer;
        for (std::uint32_t row = 0; row < screen_height; ++row) {
            m_.copy(to, from, screen_width * 2);
            for (std::uint32_t column = 0; column < screen_width; ++column) {
                const std::uint32_t pixel = half_at(m_, to + column * 2);
                set_half_at(m_, to + column * 2, ((pixel & 0xffe0u) << 1) | (pixel & 0x1fu));
            }
            from += pitch;
            to += screen_width * 2;
        }
        check_sprite(surface_call("ddraw_surface_unlock", {staging, 0}), 0x156);
        check_sprite(surface_call("ddraw_surface_release", {staging}), 0x159);
    } else {
        set_word_at(m_, sprite + 0xc, build_tiles(record, sprite + 4, sprite + 8));
    }
    const std::uint32_t count = word_at(m_, sprite + 0xc);
    const std::uint32_t materials = call(seam::crt::CRT_operator_new, {count * 0x2c});
    set_word_at(m_, sprite + 0x10, materials);
    m_.fill(materials, 0, count * 0x2c);
    const std::uint32_t tiles = word_at(m_, sprite + 4);
    for (std::int32_t k = 0; k < static_cast<std::int32_t>(count); ++k) {
        call(kAttachToMaterial, {materials + static_cast<std::uint32_t>(k) * 0x2c, tiles + static_cast<std::uint32_t>(k) * 0x2c});
    }
    return sprite;
}

// 0x004d78c0: free the sprite's screen copy, forget the background sprite when its first tile is 512 wide or more, free the materials and rectangles, let every tile's surface
// go (and its pixel copy with the software renderer) and free the tiles.
void TextureKit::sprite_destroy(std::uint32_t sprite) {
    const std::uint32_t buffer = word_at(m_, sprite + 0x18);
    const std::uint32_t tiles = word_at(m_, sprite + 4);
    if (buffer != 0) {
        call(seam::crt::FUN_005002f3, {buffer});
    }
    if (short_at(m_, tiles + 0x14) >= 0x200) {
        set_global_word(m_, kBackgroundSprite, 0);
    }
    call(seam::crt::FUN_005002f3, {word_at(m_, sprite + 0x10)});
    call(seam::crt::FUN_005002f3, {word_at(m_, sprite + 8)});
    std::uint32_t tile = tiles;
    for (std::int32_t k = 0; k < static_cast<std::int32_t>(word_at(m_, sprite + 0xc)); ++k, tile += 0x2c) {
        if (word_at(m_, tile) != 0) {
            check_sprite(release_surface(word_at(m_, tile)), 0x18b);
        }
        if (global_word(m_, kSoftwareRenderer) != 0) {
            call(seam::crt::FUN_005002f3, {word_at(m_, tile + 8)});
        }
    }
    call(seam::crt::FUN_005002f3, {tiles});
}

// 0x004da260: how many of the 256 palette entries the texture's pixels use. A table maps each 16-bit colour of the record's palette cache (+0x20: count at +4, the colours
// at +8) to its index; the surface is locked and every pixel marks its index. The software renderer reads the record's own pixels instead, stepping rows by their width in
// bytes (as the retail code does).
std::uint32_t TextureKit::count_used_palette_entries(std::uint32_t record) {
    const std::uint32_t palette = word_at(m_, record + 0x20);
    const std::uint32_t table = call(seam::crt::CRT_operator_new, {0x10000});
    for (std::uint32_t k = 0; k < word_at(m_, palette + 4); ++k) {
        set_byte_at(m_, table + half_at(m_, word_at(m_, palette + 8) + k * 2), k);
    }
    bool used[256] = {};
    std::uint32_t pixels = 0;
    std::uint32_t row_step = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    if (global_word(m_, kSoftwareRenderer) == 0) {
        m_.fill(kLock, 0, kDescSize);
        set_global_word(m_, kLock, kDescSize);
        check(surface_call("ddraw_surface_lock", {word_at(m_, record), 0, kLock, kLockReadOnly}), 0xaf4);
        width = global_int(m_, kLock + kDescWidth);
        height = global_int(m_, kLock + kDescHeight);
        pixels = global_word(m_, kLock + kDescSurface);
        row_step = global_word(m_, kLock + kDescPitch);
    } else {
        width = short_at(m_, record + 0x18);
        height = short_at(m_, record + 0x1a);
        pixels = word_at(m_, record + 8);
        row_step = static_cast<std::uint32_t>(short_at(m_, record + 0x18));
    }
    for (; height != 0; --height) {
        std::uint32_t at = pixels;
        for (std::int32_t k = width; k != 0; --k, at += 2) {
            used[byte_at(m_, table + half_at(m_, at))] = true;
        }
        pixels += row_step;
    }
    if (global_word(m_, kSoftwareRenderer) == 0) {
        check(surface_call("ddraw_surface_unlock", {word_at(m_, record), 0}), 0xb17);
    }
    call(seam::crt::FUN_005002f3, {table});
    std::uint32_t count = 0;
    for (const bool entry : used) {
        count += entry ? 1u : 0u;
    }
    return count;
}

}  // namespace opentony::runtime::texturekit
