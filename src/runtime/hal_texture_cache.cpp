#include "hal_texture_cache.hpp"

#include <cstring>

namespace opentony::runtime::surfaces {

namespace {

std::uint32_t word(const std::uint8_t (&format)[kFormatSize], std::uint32_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, format + offset, sizeof value);
    return value;
}

}  // namespace

// the same table as video_renderer.cpp's hal_format: DDPIXELFORMAT bits at +0xc, red, green, blue and alpha masks at +0x10..+0x1c
std::uint32_t HalTextureCache::hal_format(const std::uint8_t (&format)[kFormatSize]) {
    const std::uint32_t bits = word(format, kFormatBits);
    const std::uint32_t red = word(format, 0x10);
    const std::uint32_t green = word(format, 0x14);
    const std::uint32_t blue = word(format, 0x18);
    const std::uint32_t alpha = word(format, 0x1c);
    if (bits == 16 && red == 0x7c00u && green == 0x03e0u && blue == 0x001fu) {
        return alpha == 0x8000u ? OT_HAL_PIXEL_ARGB1555 : 0;
    }
    if (bits == 16 && red == 0x0f00u && green == 0x00f0u && blue == 0x000fu) {
        return OT_HAL_PIXEL_ARGB4444;
    }
    if (bits == 16 && red == 0xf800u && green == 0x07e0u && blue == 0x001fu) {
        return OT_HAL_PIXEL_RGB565;
    }
    if (bits == 32 && red == 0x00ff0000u) {
        return OT_HAL_PIXEL_ARGB8888;
    }
    return 0;
}

bool HalTextureCache::upload(const Surface& surface, std::uint32_t format, Entry& entry) {
    const bool reusable = entry.texture != OT_HAL_INVALID_HANDLE && entry.width == surface.width && entry.height == surface.height && entry.format == format;
    if (!reusable) {
        if (entry.texture != OT_HAL_INVALID_HANDLE) {
            video_.texture_destroy(entry.texture);
            entry.texture = OT_HAL_INVALID_HANDLE;
        }
        OtHalTextureDesc desc = {static_cast<std::int32_t>(surface.width), static_cast<std::int32_t>(surface.height), format, OT_HAL_INVALID_HANDLE};
        entry.texture = video_.texture_create(&desc);
        if (entry.texture == OT_HAL_INVALID_HANDLE) {
            return false;
        }
        entry.width = surface.width;
        entry.height = surface.height;
        entry.format = format;
    }
    // the rows packed: the store's pitch is padding the HAL has no use for
    const std::uint32_t row_bytes = surface.width * surface.bytes_per_pixel;
    std::vector<std::uint8_t> packed;
    packed.reserve(static_cast<std::size_t>(row_bytes) * surface.height);
    for (std::uint32_t row = 0; row < surface.height; ++row) {
        const std::vector<std::uint8_t> line = m_.bytes(surface.pixels + row * surface.pitch, row_bytes);
        packed.insert(packed.end(), line.begin(), line.end());
    }
    const OtHalRect area = {0, 0, static_cast<std::int32_t>(surface.width), static_cast<std::int32_t>(surface.height)};
    video_.texture_update(entry.texture, &area, packed.data(), static_cast<std::int32_t>(row_bytes));
    entry.version = surface.version;
    ++uploads_;
    return true;
}

OtHalHandle HalTextureCache::texture_for(std::uint32_t handle) {
    if (handle == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    const Surface* surface = store_.find(handle);
    if (surface == nullptr) {
        forget(handle);
        return OT_HAL_INVALID_HANDLE;
    }
    const std::uint32_t format = hal_format(surface->format);
    if (format == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    Entry& entry = entries_[handle];
    if (entry.texture != OT_HAL_INVALID_HANDLE && entry.version == surface->version) {
        return entry.texture;
    }
    if (!upload(*surface, format, entry)) {
        entries_.erase(handle);
        return OT_HAL_INVALID_HANDLE;
    }
    return entry.texture;
}

void HalTextureCache::forget(std::uint32_t surface) {
    const auto it = entries_.find(surface);
    if (it == entries_.end()) {
        return;
    }
    if (it->second.texture != OT_HAL_INVALID_HANDLE) {
        video_.texture_destroy(it->second.texture);
    }
    entries_.erase(it);
}

void HalTextureCache::forget_all() {
    for (const auto& [handle, entry] : entries_) {
        (void)handle;
        if (entry.texture != OT_HAL_INVALID_HANDLE) {
            video_.texture_destroy(entry.texture);
        }
    }
    entries_.clear();
}

void HalTextureCache::sweep() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (store_.find(it->first) == nullptr) {
            if (it->second.texture != OT_HAL_INVALID_HANDLE) {
                video_.texture_destroy(it->second.texture);
            }
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace opentony::runtime::surfaces
