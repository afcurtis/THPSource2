// The texture key and the RGBA conversion of src/hal/texture_key.hpp.
#include "texture_key.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>

using namespace opentony::hal::texture;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

}  // namespace

int main() {
    // the 64-bit FNV-1a of the 12-byte header (2, 2, ARGB1555 = 2 as little-endian words) and eight texel bytes; the value was computed apart, in Python:
    //   h = 0xcbf29ce484222325; for b in header + rows: h = ((h ^ b) * 0x100000001b3) & (2**64 - 1)
    const std::uint8_t rows[8] = {0x00, 0x80, 0xff, 0xff, 0x1f, 0x00, 0xe0, 0x83};
    const std::uint64_t k = key(2, 2, OT_HAL_PIXEL_ARGB1555, rows, sizeof rows);
    expect(k == 0x7fdbfcc0739a8eafull, "FNV-1a of header and rows");
    expect(key_text(k) == "7fdbfcc0739a8eaf", "16 hex digits");
    expect(key_text(0x1u) == "0000000000000001", "leading zeros");
    std::uint64_t parsed = 0;
    expect(parse_key("7fdbfcc0739a8eaf", parsed) && parsed == k, "parse");
    expect(!parse_key("7FDBFCC0739A8EAF", parsed), "upper case is not a key");
    expect(!parse_key("7fdbfcc0739a8ea", parsed), "15 digits are not a key");
    expect(key(2, 2, OT_HAL_PIXEL_ARGB4444, rows, sizeof rows) != k, "the format is part of the key");
    expect(key(4, 1, OT_HAL_PIXEL_ARGB1555, rows, sizeof rows) != k, "the size is part of the key");
    std::uint8_t other[8];
    std::memcpy(other, rows, sizeof rows);
    other[5] ^= 1;
    expect(key(2, 2, OT_HAL_PIXEL_ARGB1555, other, sizeof other) != k, "a texel is part of the key");

    // ARGB1555: alpha bit set -> 255; 0x001f is blue; 0x03e0 green; 0x7c00 red. Rows: opaque white, transparent black, then a transparent texel that keeps its colour
    Alpha alpha;
    const std::uint16_t cutout[4] = {0xffff, 0x0000, 0x7c00, 0x83e0};
    auto out = to_rgba8(2, 2, OT_HAL_PIXEL_ARGB1555, cutout, sizeof cutout, alpha);
    expect(out.size() == 16, "2 x 2 x 4 bytes");
    expect(alpha == Alpha::Cutout, "alpha bit set and clear is a cut-out");
    expect(out[0] == 255 && out[1] == 255 && out[2] == 255 && out[3] == 255, "white");
    expect(out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0, "clear black");
    expect(out[8] == 255 && out[9] == 0 && out[10] == 0 && out[11] == 0, "a clear texel keeps its red");
    expect(out[12] == 0 && out[13] == 255 && out[14] == 0 && out[15] == 255, "green, set");
    const std::uint16_t opaque[1] = {0x8000u | (15u << 10) | (7u << 5) | 1u};
    out = to_rgba8(1, 1, OT_HAL_PIXEL_ARGB1555, opaque, sizeof opaque, alpha);
    expect(alpha == Alpha::Opaque, "every alpha bit set is opaque");
    expect(out[0] == ((15 << 3) | (15 >> 2)) && out[1] == ((7 << 3) | (7 >> 2)) && out[2] == ((1 << 3) | (1 >> 2)), "5 bits widen by repeating the top three");

    const std::uint16_t smooth[2] = {0x8888, 0xf000};
    out = to_rgba8(2, 1, OT_HAL_PIXEL_ARGB4444, smooth, sizeof smooth, alpha);
    expect(alpha == Alpha::Smooth && out[3] == 0x88 && out[7] == 255, "4444 with a partial alpha is smooth");
    expect(out[0] == 0x88 && out[1] == 0x88 && out[2] == 0x88, "a nibble times 17");

    const std::uint16_t five65[1] = {0xf800};
    out = to_rgba8(1, 1, OT_HAL_PIXEL_RGB565, five65, sizeof five65, alpha);
    expect(alpha == Alpha::Opaque && out[0] == 255 && out[1] == 0 && out[2] == 0 && out[3] == 255, "565 red");

    const std::uint32_t wide[2] = {0x80ff8040u, 0xffffffffu};
    out = to_rgba8(2, 1, OT_HAL_PIXEL_ARGB8888, wide, sizeof wide, alpha);
    expect(alpha == Alpha::Smooth && out[0] == 0xff && out[1] == 0x80 && out[2] == 0x40 && out[3] == 0x80, "8888 is A R G B in a word");

    expect(to_rgba8(2, 2, OT_HAL_PIXEL_INDEX8, rows, sizeof rows, alpha).empty(), "an indexed format is not converted");
    expect(to_rgba8(4, 4, OT_HAL_PIXEL_ARGB1555, rows, sizeof rows, alpha).empty(), "too few bytes");
    expect(std::strcmp(format_name(OT_HAL_PIXEL_RGB565), "rgb565") == 0 && std::strcmp(alpha_name(Alpha::Cutout), "cutout") == 0, "names");

    if (failures != 0) {
        return 1;
    }
    std::cout << "texture key: ok\n";
    return 0;
}
