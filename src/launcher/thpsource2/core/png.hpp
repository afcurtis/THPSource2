// THPSource2 launcher: PNG files for the texture upscaler (docs/LAUNCHER.md, "Upscaled textures"). Portable (zlib only), so the native tests use it as is.
// Reads the 8-bit PNGs the dump and the upscaler write (grey, grey + alpha, RGB, RGBA, palette; not interlaced) and writes RGB or RGBA with per-row filters.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace thpsource2::png {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;   // 4 bytes a texel (R, G, B, A), rows top first

    Image() = default;
    Image(int w, int h) : width(w), height(h), rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0) {}
    std::uint8_t* at(int x, int y) { return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u; }
    const std::uint8_t* at(int x, int y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
    }
};

// A PNG file's bytes into `image`; false with `error` for anything this reader does not take (16-bit, interlaced, a bad CRC or a short stream).
bool decode(const std::vector<std::uint8_t>& file, Image& image, std::string& error);
// The image as a PNG file: RGBA, or RGB (the alpha left out) when `alpha` is false. zlib level 6, each row's filter chosen by the smallest sum of its bytes.
std::vector<std::uint8_t> encode(const Image& image, bool alpha);

bool read(const std::filesystem::path& path, Image& image, std::string& error);
// Written to `<path>.tmp` and renamed, so a stopped run never leaves half a file under the real name.
bool write(const std::filesystem::path& path, const Image& image, bool alpha, std::string& error);

}  // namespace thpsource2::png
