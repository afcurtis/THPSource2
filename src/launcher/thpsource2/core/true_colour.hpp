// THPSource2 launcher: true-colour replacement bitmaps (docs/LAUNCHER.md, "True-colour textures"). Portable, so the native tests use it as is.
//
// The PC port's level textures are 24-bit BMPs, but the game keeps every texture in a 16-bit 1555 surface: 5 bits a channel. This turns each 24-bit bitmap of the unpacked
// game files into a PNG named by the key the game's own upload gets (src/hal/texture_key_hash.h: the hash of the 1555 rows), at the bitmap's own size, in all 8 bits. The GL
// backend's texture pack (OT_TEXTURE_PACK) then draws that picture for the texture, so the same picture is drawn without the banding the 15-bit cut made. No dump and no census:
// the key is computed from the file, the way TextureKit::upload_rgb_pixels (0x004d9310) makes the surface from it.
#pragma once

#include "png.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace thpsource2::true_colour {

constexpr int kVersion = 1;
constexpr int kMaxSide = 256;   // the largest texture the game uploads whole from a bitmap

// A 24-bit BMP file's bytes as the game uploads it: `key` is the texture key of its 1555 surface, `picture` the same texels in 8 bits a channel (a key-colour pixel, R > 0xf7,
// G = 0, B > 0xf7, is black with alpha 0 as it is in the surface; every other texel is opaque). False for anything else (not 24-bit, compressed, top-down, empty, over kMaxSide).
bool convert(const std::vector<std::uint8_t>& bmp, std::uint64_t& key, png::Image& picture, bool& has_key_colour);

struct Result {
    int written = 0;     // PNG files written
    int unchanged = 0;   // already there
    int skipped = 0;     // not a bitmap this handles, or a key already written
    int failed = 0;
    bool cancelled = false;
};

// Every .bmp under <package_data>\newtex and <package_data>\newbmp. `progress(done, total)` returns false to stop.
Result build(const std::filesystem::path& package_data, const std::filesystem::path& out, const std::function<bool(int, int)>& progress = {});

// `out` holds a complete set made by this version for the bitmaps now in `package_data` (stamp.txt says: version and how many bitmaps)
bool is_current(const std::filesystem::path& package_data, const std::filesystem::path& out);
// Writes stamp.txt for `package_data`; build() does it when it finishes
bool write_stamp(const std::filesystem::path& package_data, const std::filesystem::path& out);

// Files in the folder that are keyed pictures (for the status line)
int count_files(const std::filesystem::path& out);

}  // namespace thpsource2::true_colour
