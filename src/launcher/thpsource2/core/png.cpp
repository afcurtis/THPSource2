#include "png.hpp"

#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace thpsource2::png {
namespace {

constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
constexpr int kMaxSide = 16384;

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

void put_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void put_chunk(std::vector<std::uint8_t>& out, const char* kind, const std::uint8_t* body, std::size_t size) {
    put_be32(out, static_cast<std::uint32_t>(size));
    const std::size_t from = out.size();
    out.insert(out.end(), kind, kind + 4);
    if (size != 0) {
        out.insert(out.end(), body, body + size);
    }
    put_be32(out, static_cast<std::uint32_t>(crc32(0, out.data() + from, static_cast<uInt>(out.size() - from))));
}

std::uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return static_cast<std::uint8_t>(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

}  // namespace

bool decode(const std::vector<std::uint8_t>& file, Image& image, std::string& error) {
    if (file.size() < 8 || std::memcmp(file.data(), kSignature, 8) != 0) {
        error = "not a PNG file";
        return false;
    }
    std::uint32_t width = 0, height = 0;
    int depth = 0, type = -1, interlace = 0;
    std::vector<std::uint8_t> idat, palette, transparency;
    bool ended = false;
    for (std::size_t at = 8; at + 12 <= file.size() && !ended;) {
        const std::uint32_t size = be32(file.data() + at);
        if (size > file.size() - at - 12) {
            error = "a chunk runs past the end of the file";
            return false;
        }
        const std::uint8_t* kind = file.data() + at + 4;
        const std::uint8_t* body = kind + 4;
        if (crc32(crc32(0, kind, 4), body, size) != be32(body + size)) {
            error = "bad CRC";
            return false;
        }
        if (std::memcmp(kind, "IHDR", 4) == 0 && size == 13) {
            width = be32(body);
            height = be32(body + 4);
            depth = body[8];
            type = body[9];
            interlace = body[12];
        } else if (std::memcmp(kind, "PLTE", 4) == 0) {
            palette.assign(body, body + size);
        } else if (std::memcmp(kind, "tRNS", 4) == 0) {
            transparency.assign(body, body + size);
        } else if (std::memcmp(kind, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + size);
        } else if (std::memcmp(kind, "IEND", 4) == 0) {
            ended = true;
        }
        at += 12u + size;
    }
    int channels = 0;
    switch (type) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: error = "no IHDR or an unknown colour type"; return false;
    }
    if (width == 0 || height == 0 || width > kMaxSide || height > kMaxSide) {
        error = "size out of range";
        return false;
    }
    if (depth != 8 || interlace != 0) {
        error = "only 8-bit, not interlaced PNGs are read";
        return false;
    }
    if (type == 3 && palette.empty()) {
        error = "palette image without PLTE";
        return false;
    }
    const std::size_t line = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    std::vector<std::uint8_t> raw((line + 1u) * height);
    uLongf raw_size = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &raw_size, idat.data(), static_cast<uLong>(idat.size())) != Z_OK || raw_size != raw.size()) {
        error = "the image data does not inflate to its size";
        return false;
    }
    const std::size_t bpp = static_cast<std::size_t>(channels);
    std::vector<std::uint8_t> previous(line, 0), current(line);
    image = Image(static_cast<int>(width), static_cast<int>(height));
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* in = raw.data() + static_cast<std::size_t>(y) * (line + 1u);
        const std::uint8_t filter = in[0];
        ++in;
        for (std::size_t x = 0; x < line; ++x) {
            const int a = x >= bpp ? current[x - bpp] : 0;
            const int b = previous[x];
            const int c = x >= bpp ? previous[x - bpp] : 0;
            int value = in[x];
            switch (filter) {
                case 0: break;
                case 1: value += a; break;
                case 2: value += b; break;
                case 3: value += (a + b) / 2; break;
                case 4: value += paeth(a, b, c); break;
                default: error = "unknown row filter"; return false;
            }
            current[x] = static_cast<std::uint8_t>(value);
        }
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t* s = current.data() + static_cast<std::size_t>(x) * bpp;
            std::uint8_t* d = image.at(static_cast<int>(x), static_cast<int>(y));
            switch (type) {
                case 0: d[0] = d[1] = d[2] = s[0]; d[3] = 255; break;
                case 2: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; break;
                case 4: d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; break;
                case 6: std::memcpy(d, s, 4); break;
                case 3: {
                    const std::size_t index = s[0];
                    if (index * 3u + 2u >= palette.size()) {
                        error = "palette index out of range";
                        return false;
                    }
                    d[0] = palette[index * 3u];
                    d[1] = palette[index * 3u + 1u];
                    d[2] = palette[index * 3u + 2u];
                    d[3] = index < transparency.size() ? transparency[index] : 255;
                    break;
                }
            }
        }
        previous.swap(current);
    }
    return true;
}

std::vector<std::uint8_t> encode(const Image& image, bool alpha) {
    const std::size_t channels = alpha ? 4u : 3u;
    const std::size_t line = static_cast<std::size_t>(image.width) * channels;
    std::vector<std::uint8_t> raw;
    raw.reserve((line + 1u) * static_cast<std::size_t>(image.height));
    std::vector<std::uint8_t> previous(line, 0), current(line), best(line), trial(line);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            std::memcpy(current.data() + static_cast<std::size_t>(x) * channels, image.at(x, y), channels);
        }
        unsigned long best_cost = ~0ul;
        std::uint8_t best_filter = 0;
        for (std::uint8_t filter = 0; filter <= 4; ++filter) {
            unsigned long cost = 0;
            for (std::size_t x = 0; x < line; ++x) {
                const int a = x >= channels ? current[x - channels] : 0;
                const int b = previous[x];
                const int c = x >= channels ? previous[x - channels] : 0;
                int predicted = 0;
                switch (filter) {
                    case 1: predicted = a; break;
                    case 2: predicted = b; break;
                    case 3: predicted = (a + b) / 2; break;
                    case 4: predicted = paeth(a, b, c); break;
                    default: break;
                }
                trial[x] = static_cast<std::uint8_t>(current[x] - predicted);
                cost += static_cast<unsigned long>(std::abs(static_cast<int>(static_cast<std::int8_t>(trial[x]))));
            }
            if (cost < best_cost) {
                best_cost = cost;
                best_filter = filter;
                best.swap(trial);
            }
        }
        raw.push_back(best_filter);
        raw.insert(raw.end(), best.begin(), best.end());
        previous.swap(current);
    }
    uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> packed(packed_size);
    compress2(packed.data(), &packed_size, raw.data(), static_cast<uLong>(raw.size()), 6);
    std::vector<std::uint8_t> file(kSignature, kSignature + 8);
    std::vector<std::uint8_t> header;
    put_be32(header, static_cast<std::uint32_t>(image.width));
    put_be32(header, static_cast<std::uint32_t>(image.height));
    header.insert(header.end(), {8, static_cast<std::uint8_t>(alpha ? 6 : 2), 0, 0, 0});
    put_chunk(file, "IHDR", header.data(), header.size());
    put_chunk(file, "IDAT", packed.data(), packed_size);
    put_chunk(file, "IEND", nullptr, 0);
    return file;
}

bool read(const std::filesystem::path& path, Image& image, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open";
        return false;
    }
    const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decode(file, image, error);
}

bool write(const std::filesystem::path& path, const Image& image, bool alpha, std::string& error) {
    const std::vector<std::uint8_t> file = encode(image, alpha);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()))) {
            error = "cannot write";
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        error = "cannot rename the written file";
        return false;
    }
    return true;
}

}  // namespace thpsource2::png
