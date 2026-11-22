#include "auto_views.hpp"
#include "surface_store.hpp"

#include <algorithm>
#include <utility>

namespace opentony::runtime::surfaces {

using namespace opentony::runtime::autoviews;

namespace {

// src/hal/oracle/ddraw_surface.cpp, ot_ddraw_pitch: width * bytes per pixel rounded up to 8
std::uint32_t pitch_of(std::uint32_t width, std::uint32_t bytes_per_pixel) { return (width * bytes_per_pixel + 7u) & ~7u; }

}  // namespace

std::uint32_t SurfaceStore::create_surface(std::uint32_t desc, std::uint32_t out) {
    if (word_at(m_, desc) != kDescSize || out == 0) {
        return kInvalidParameters;
    }
    Surface surface;
    surface.height = word_at(m_, desc + kDescHeight);
    surface.width = word_at(m_, desc + kDescWidth);
    const std::uint32_t bits = word_at(m_, desc + kDescPixelFormat + kFormatBits);
    if (surface.width == 0 || surface.height == 0 || (bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
        return kInvalidParameters;
    }
    surface.bytes_per_pixel = bits / 8;
    surface.pitch = pitch_of(surface.width, surface.bytes_per_pixel);
    surface.caps = word_at(m_, desc + kDescCaps);
    surface.caps2 = word_at(m_, desc + kDescCaps2);
    surface.pixels = allocate_(surface.pitch * surface.height);
    if (surface.pixels == 0) {
        return kFailed;
    }
    m_.fill(surface.pixels, 0, surface.pitch * surface.height);
    const std::vector<std::uint8_t> format = m_.bytes(desc + kDescPixelFormat, kFormatSize);
    std::copy(format.begin(), format.end(), surface.format);
    const std::uint32_t handle = next_++;
    surfaces_.emplace(handle, surface);
    set_word_at(m_, out, handle);
    return kOk;
}

std::uint32_t SurfaceStore::lock(std::uint32_t surface, std::uint32_t rect, std::uint32_t desc_out, std::uint32_t flags) {
    const auto it = surfaces_.find(surface);
    if (it == surfaces_.end() || it->second.locked || desc_out == 0) {
        return kFailed;
    }
    Surface& s = it->second;
    std::uint32_t at = s.pixels;
    if (rect != 0) {
        const Rect r = rectangle(rect, s);
        at += r.top * s.pitch + r.left * s.bytes_per_pixel;
    }
    m_.fill(desc_out, 0, kDescSize);
    set_word_at(m_, desc_out, kDescSize);
    set_word_at(m_, desc_out + kDescFlags, 0x100f);  // DDSD_CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    set_word_at(m_, desc_out + kDescHeight, s.height);
    set_word_at(m_, desc_out + kDescWidth, s.width);
    set_word_at(m_, desc_out + kDescPitch, s.pitch);
    set_word_at(m_, desc_out + kDescSurface, at);
    m_.write(desc_out + kDescPixelFormat, {s.format, kFormatSize});
    set_word_at(m_, desc_out + kDescCaps, s.caps);
    set_word_at(m_, desc_out + kDescCaps2, s.caps2);
    s.locked = true;
    s.write_lock = (flags & kLockReadOnly) == 0;
    return kOk;
}

std::uint32_t SurfaceStore::unlock(std::uint32_t surface) {
    const auto it = surfaces_.find(surface);
    if (it == surfaces_.end() || !it->second.locked) {
        return kFailed;
    }
    Surface& s = it->second;
    s.locked = false;
    if (s.write_lock) {
        ++s.version;
    }
    return kOk;
}

SurfaceStore::Rect SurfaceStore::rectangle(std::uint32_t address, const Surface& surface) const {
    if (address == 0) {
        return {0, 0, surface.width, surface.height};
    }
    return {word_at(m_, address), word_at(m_, address + 4), word_at(m_, address + 8), word_at(m_, address + 12)};
}

std::uint32_t SurfaceStore::blt(std::uint32_t destination, std::uint32_t destination_rect, std::uint32_t source, std::uint32_t source_rect) {
    const auto d = surfaces_.find(destination);
    const auto s = surfaces_.find(source);
    if (d == surfaces_.end() || s == surfaces_.end() || d->second.locked || s->second.locked) {
        return kFailed;
    }
    Surface& to = d->second;
    const Surface& from = s->second;
    if (to.bytes_per_pixel != from.bytes_per_pixel) {
        return kInvalidParameters;
    }
    const Rect dr = rectangle(destination_rect, to);
    const Rect sr = rectangle(source_rect, from);
    if (dr.right <= dr.left || dr.bottom <= dr.top || sr.right <= sr.left || sr.bottom <= sr.top || dr.right > to.width || dr.bottom > to.height ||
        sr.right > from.width || sr.bottom > from.height) {
        return kInvalidParameters;
    }
    const std::uint32_t dw = dr.right - dr.left;
    const std::uint32_t dh = dr.bottom - dr.top;
    const std::uint32_t sw = sr.right - sr.left;
    const std::uint32_t sh = sr.bottom - sr.top;
    const std::uint32_t bpp = to.bytes_per_pixel;
    if (dw == sw && dh == sh) {
        for (std::uint32_t row = 0; row < dh; ++row) {
            m_.copy(to.pixels + (dr.top + row) * to.pitch + dr.left * bpp, from.pixels + (sr.top + row) * from.pitch + sr.left * bpp, dw * bpp);
        }
    } else {
        // Wine's CPU blitter, which src/hal/oracle/ddraw_surface.cpp (ot_ddraw_stretch) follows: source = destination * source size / destination size
        for (std::uint32_t row = 0; row < dh; ++row) {
            const std::uint32_t source_row = sr.top + static_cast<std::uint32_t>(static_cast<std::uint64_t>(row) * sh / dh);
            for (std::uint32_t column = 0; column < dw; ++column) {
                const std::uint32_t source_column = sr.left + static_cast<std::uint32_t>(static_cast<std::uint64_t>(column) * sw / dw);
                m_.copy(to.pixels + (dr.top + row) * to.pitch + (dr.left + column) * bpp, from.pixels + source_row * from.pitch + source_column * bpp, bpp);
            }
        }
    }
    ++to.version;
    return kOk;
}

std::uint32_t SurfaceStore::release(std::uint32_t surface) {
    const auto it = surfaces_.find(surface);
    if (it == surfaces_.end()) {
        return 0;
    }
    const std::uint32_t remaining = --it->second.references;
    if (remaining == 0) {
        if (release_) {
            release_(it->second.pixels);
        }
        surfaces_.erase(it);
    }
    return remaining;
}

std::uint32_t SurfaceStore::pixel_format(std::uint32_t surface, std::uint32_t out) {
    const auto it = surfaces_.find(surface);
    if (it == surfaces_.end() || out == 0) {
        return kFailed;
    }
    m_.write(out, {it->second.format, kFormatSize});
    return kOk;
}

std::uint32_t SurfaceStore::surface_desc(std::uint32_t surface, std::uint32_t out) {
    const auto it = surfaces_.find(surface);
    if (it == surfaces_.end() || out == 0) {
        return kFailed;
    }
    const Surface& s = it->second;
    m_.fill(out, 0, kDescSize);
    set_word_at(m_, out, kDescSize);
    set_word_at(m_, out + kDescFlags, 0x100f);  // DDSD_CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    set_word_at(m_, out + kDescHeight, s.height);
    set_word_at(m_, out + kDescWidth, s.width);
    set_word_at(m_, out + kDescPitch, s.pitch);
    m_.write(out + kDescPixelFormat, {s.format, kFormatSize});
    set_word_at(m_, out + kDescCaps, s.caps);
    set_word_at(m_, out + kDescCaps2, s.caps2);
    return kOk;
}

bool SurfaceStore::call(const std::string& name, const std::vector<std::uint32_t>& args, std::uint32_t& result) {
    const auto arg = [&](std::size_t k) { return k < args.size() ? args[k] : 0u; };
    if (name == "ddraw_create_surface") {
        result = create_surface(arg(0), arg(1));
    } else if (name == "ddraw_surface_lock") {
        result = lock(arg(0), arg(1), arg(2), arg(3));
    } else if (name == "ddraw_surface_unlock") {
        result = unlock(arg(0));
    } else if (name == "ddraw_surface_blt") {
        result = blt(arg(0), arg(1), arg(2), arg(3));
    } else if (name == "ddraw_surface_release") {
        result = release(arg(0));
    } else if (name == "ddraw_surface_pixel_format") {
        result = pixel_format(arg(0), arg(1));
    } else if (name == "ddraw_surface_desc") {
        result = surface_desc(arg(0), arg(1));
    } else if (name == "ddraw_device_set_texture") {
        // IDirect3DDevice7::SetTexture(stage, texture): the texture layer unbinds a surface that the device still holds before it lets it go. The store has no device and
        // its surfaces are never held by one, so there is nothing to unbind.
        result = kOk;
    } else {
        return false;
    }
    return true;
}

}  // namespace opentony::runtime::surfaces
