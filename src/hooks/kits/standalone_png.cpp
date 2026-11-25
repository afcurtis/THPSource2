#include "standalone_png.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include "win32_posix.h"
#endif
#include <zlib.h>

#include <cstdio>
#include <vector>

namespace {

void put_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void put_chunk(std::vector<std::uint8_t>& out, const char* kind, const std::uint8_t* body, std::size_t size) {
    put_be32(out, static_cast<std::uint32_t>(size));
    const std::size_t from = out.size();
    out.insert(out.end(), kind, kind + 4);
    out.insert(out.end(), body, body + size);
    put_be32(out, static_cast<std::uint32_t>(crc32(0, out.data() + from, static_cast<uInt>(out.size() - from))));
}

}  // namespace

bool standalone_write_png(const std::string& path, std::int32_t width, std::int32_t height, int channels, const std::uint8_t* rows) {
    if (width <= 0 || height <= 0 || (channels != 3 && channels != 4)) {
        return false;
    }
    const std::size_t line = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    std::vector<std::uint8_t> raw((line + 1u) * static_cast<std::size_t>(height));
    for (std::int32_t y = 0; y < height; ++y) {
        std::uint8_t* out = raw.data() + static_cast<std::size_t>(y) * (line + 1u);
        *out++ = 0;
        const std::uint8_t* in = rows + static_cast<std::size_t>(y) * line;
        for (std::size_t x = 0; x < line; ++x) {
            out[x] = in[x];
        }
    }
    uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> packed(packed_size);
    if (compress2(packed.data(), &packed_size, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) {
        return false;
    }
    std::vector<std::uint8_t> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> header;
    put_be32(header, static_cast<std::uint32_t>(width));
    put_be32(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, static_cast<std::uint8_t>(channels == 4 ? 6 : 2), 0, 0, 0});   // 8 bits, colour type 6 (RGBA) or 2 (RGB)
    put_chunk(file, "IHDR", header.data(), header.size());
    put_chunk(file, "IDAT", packed.data(), packed_size);
    put_chunk(file, "IEND", nullptr, 0);
    const std::string temporary = path + ".tmp";
    FILE* out = std::fopen(temporary.c_str(), "wb");
    if (out == nullptr) {
        return false;
    }
    const bool written = std::fwrite(file.data(), 1, file.size(), out) == file.size();
    const bool closed = std::fclose(out) == 0;
    if (!written || !closed || !MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(temporary.c_str());
        return false;
    }
    return true;
}
