#include "true_colour.hpp"

#include "hal/texture_key_hash.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace thpsource2::true_colour {

namespace {

constexpr std::uint32_t kFormatArgb1555 = 2;   // OT_HAL_PIXEL_ARGB1555 (src/hal/video.h): the surfaces TextureKit makes

std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | (static_cast<std::uint32_t>(d[at + 1]) << 8) | (static_cast<std::uint32_t>(d[at + 2]) << 16) |
           (static_cast<std::uint32_t>(d[at + 3]) << 24);
}

std::string key_text(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(value));
    return text;
}

bool read_file(const fs::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool is_bmp(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".bmp";
}

std::vector<fs::path> list_bitmaps(const fs::path& package_data) {
    std::vector<fs::path> files;
    for (const char* folder : {"newtex", "newbmp"}) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(package_data / folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && is_bmp(it->path())) {
                files.push_back(it->path());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

bool convert(const std::vector<std::uint8_t>& bmp, std::uint64_t& key, png::Image& picture, bool& has_key_colour) {
    if (bmp.size() < 54 || bmp[0] != 'B' || bmp[1] != 'M') {
        return false;
    }
    const std::uint32_t offset = le32(bmp, 10);
    const std::uint32_t header = le32(bmp, 14);
    const std::int32_t width = static_cast<std::int32_t>(le32(bmp, 18));
    const std::int32_t height = static_cast<std::int32_t>(le32(bmp, 22));
    const std::uint32_t bits = static_cast<std::uint32_t>(bmp[28]) | (static_cast<std::uint32_t>(bmp[29]) << 8);
    if (header < 40 || bits != 24 || le32(bmp, 30) != 0 || width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide) {
        return false;
    }
    // the game reads ((width + 1) * 3) & ~3 bytes a row
    const std::size_t row_bytes = static_cast<std::size_t>(((width + 1) * 3) & ~3);
    if (static_cast<std::size_t>(offset) + row_bytes * static_cast<std::size_t>(height) > bmp.size() || row_bytes < static_cast<std::size_t>(width) * 3) {
        return false;
    }
    picture = png::Image(width, height);
    std::vector<std::uint8_t> rows(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 2);
    has_key_colour = false;
    for (int file_row = 0; file_row < height; ++file_row) {
        const int y = height - 1 - file_row;   // a bitmap stores its bottom row first; the surface is top down
        const std::uint8_t* from = bmp.data() + offset + row_bytes * static_cast<std::size_t>(file_row);
        for (int x = 0; x < width; ++x, from += 3) {
            const std::uint8_t blue = from[0], green = from[1], red = from[2];
            const bool is_key = red > 0xf7 && green == 0 && blue > 0xf7;
            std::uint16_t texel = 0;
            std::uint8_t* out = picture.at(x, y);
            if (is_key) {
                has_key_colour = true;
                out[0] = out[1] = out[2] = out[3] = 0;
            } else {
                texel = static_cast<std::uint16_t>(((red >> 3) << 10) | ((green >> 3) << 5) | (blue >> 3) | 0x8000u);
                out[0] = red;
                out[1] = green;
                out[2] = blue;
                out[3] = 255;
            }
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 2;
            rows[at] = static_cast<std::uint8_t>(texel & 0xff);
            rows[at + 1] = static_cast<std::uint8_t>(texel >> 8);
        }
    }
    key = ot_hal_texture_key(width, height, kFormatArgb1555, rows.data(), rows.size());
    return true;
}

Result build(const fs::path& package_data, const fs::path& out, const std::function<bool(int, int)>& progress) {
    Result result;
    std::error_code ec;
    fs::create_directories(out, ec);
    const std::vector<fs::path> files = list_bitmaps(package_data);
    std::set<std::uint64_t> seen;
    int done = 0;
    for (const fs::path& file : files) {
        if (progress && !progress(done, static_cast<int>(files.size()))) {
            result.cancelled = true;
            return result;
        }
        ++done;
        std::vector<std::uint8_t> bytes;
        std::uint64_t key = 0;
        png::Image picture;
        bool keyed = false;
        if (!read_file(file, bytes) || !convert(bytes, key, picture, keyed) || !seen.insert(key).second) {
            ++result.skipped;
            continue;
        }
        const fs::path target = out / (key_text(key) + ".png");
        if (fs::is_regular_file(target, ec)) {
            ++result.unchanged;
            continue;
        }
        std::string error;
        if (png::write(target, picture, keyed, error)) {
            ++result.written;
        } else {
            ++result.failed;
        }
    }
    if (result.failed == 0 && !result.cancelled) {
        write_stamp(package_data, out);
    }
    return result;
}

bool write_stamp(const fs::path& package_data, const fs::path& out) {
    std::ofstream stamp(out / "stamp.txt", std::ios::trunc);
    stamp << "true-colour " << kVersion << "\nbitmaps " << list_bitmaps(package_data).size() << "\n";
    return static_cast<bool>(stamp);
}

bool is_current(const fs::path& package_data, const fs::path& out) {
    std::ifstream stamp(out / "stamp.txt");
    std::string a, b;
    int version = 0;
    std::size_t bitmaps = 0;
    if (!(stamp >> a >> version >> b >> bitmaps) || a != "true-colour" || b != "bitmaps" || version != kVersion) {
        return false;
    }
    return bitmaps != 0 && bitmaps == list_bitmaps(package_data).size();
}

int count_files(const fs::path& out) {
    int count = 0;
    std::error_code ec;
    for (fs::directory_iterator it(out, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".png") {
            ++count;
        }
    }
    return count;
}

}  // namespace thpsource2::true_colour
