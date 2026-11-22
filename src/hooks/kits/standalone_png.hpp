#pragma once

#include <cstdint>
#include <string>

// A PNG file of 8-bit rows: `channels` 3 (RGB) or 4 (RGBA), `rows` packed top down, no filter, zlib's deflate. The screenshot key (standalone_video.cpp) and the texture dump
// (standalone_texture_dump.cpp) both write through it. false when the file cannot be written; the file appears whole or not at all (written beside it, then moved).
bool standalone_write_png(const std::string& path, std::int32_t width, std::int32_t height, int channels, const std::uint8_t* rows);
