#pragma once

#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>

namespace opentony::runtime {

// Phase 4.9: the colour half of create-a-skater (create_color.cpp). Hue, saturation and value are
// 12.20 fixed point (hue 0..360 << 20, saturation and value 0..1 << 20); a palette is a run of
// 16-byte entries (hue, saturation, value, alpha word) kept next to the game's 15-bit palette so
// the editor can shift a whole ramp without round-off. Every function reads and writes memory at
// retail addresses; the palette itself lives in the engine (Pal_CopyToMemory / Pal_StoreFromMemory)
// and is a service: the read hands back 15-bit colours, the store reports the colours it was given.
namespace create {

// Service names (RetailServices): the palette read is scripted with one value per dword, the store
// is reported with the 15-bit colours as its text.
constexpr const char* kPaletteRead = "palette_read";
constexpr const char* kPaletteStore = "palette_store";
// Live, the read and the store are Pal_CopyToMemory / Pal_StoreFromMemory(palette, colours) on a 0x200-byte buffer, a stack local of the
// retail caller: here in pc_platform's scratch window, clear of every Kit's area.
constexpr std::uint32_t kPaletteBuffer = 0x7ff0e000;

constexpr std::uint32_t kPalettePresets = 0x00525c00;   // 12-byte rows of (hue, saturation, value)
constexpr std::uint32_t kHueFull = 0x16800000;          // 360 degrees << 20

// Assertion messages (addresses of the format strings).
constexpr std::uint32_t kMsgNoPointerH = 0x00525718;
constexpr std::uint32_t kMsgNoPointerS = 0x00525708;
constexpr std::uint32_t kMsgNoPointerV = 0x005256f8;
constexpr std::uint32_t kMsgNoPointerR = 0x005266f8;
constexpr std::uint32_t kMsgNoPointerG = 0x005266e8;
constexpr std::uint32_t kMsgNoPointerB = 0x005266d8;
constexpr std::uint32_t kMsgBadHue = 0x005266c4;
constexpr std::uint32_t kMsgBadSector = 0x005266ac;
constexpr std::uint32_t kMsgNoSourcePalette = 0x00526708;
constexpr std::uint32_t kMsgNoRectangle = 0x00526738;
constexpr std::uint32_t kMsgNoSourceToModify = 0x0052671c;

struct Hsv final {
    std::uint32_t h = 0;
    std::uint32_t s = 0;
    std::uint32_t v = 0;
};

// The result of a hue/saturation/value conversion. `hue_ok` is false when the hue was still negative
// after one turn was added (retail asserts and goes on); `sector_ok` is false when the hue named no
// sector, in which case retail writes nothing.
struct Rgb final {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    bool hue_ok = true;
    bool sector_ok = true;
    bool written = false;
};

// A saved palette entry as the editor reads it: hue in degrees, saturation and value 0..255.
struct SavedColour final {
    std::int32_t h = 0;
    std::int32_t s = 0;
    std::int32_t v = 0;
};

class CreateColor final {
public:
    CreateColor(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // 0x00423c90 / 0x00423cb0: smallest and largest of three bytes.
    [[nodiscard]] static std::uint8_t min3(std::uint8_t a, std::uint8_t b, std::uint8_t c) noexcept;
    [[nodiscard]] static std::uint8_t max3(std::uint8_t a, std::uint8_t b, std::uint8_t c) noexcept;

    // The arithmetic of 0x00423cd0 and 0x00423ea0 without the memory traffic (only the low byte of
    // each colour argument is used).
    [[nodiscard]] static Hsv to_hsv(std::uint32_t r, std::uint32_t g, std::uint32_t b) noexcept;
    [[nodiscard]] static Rgb to_rgb(std::int32_t h, std::int32_t s, std::int32_t v) noexcept;

    // 0x00423cd0: 8-bit red, green, blue -> hue, saturation, value written through three pointers.
    void rgb_to_hsv(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t h_out, std::uint32_t s_out,
                    std::uint32_t v_out);
    // 0x00423ea0: hue, saturation, value -> 8-bit red, green, blue written through three pointers.
    void hsv_to_rgb(std::int32_t h, std::int32_t s, std::int32_t v, std::uint32_t r_out, std::uint32_t g_out,
                    std::uint32_t b_out);
    // 0x0041f0c0: row `index` of the preset table (0 = no preset: all three -1).
    void preset_hsv(std::int32_t index, std::uint32_t h_out, std::uint32_t s_out, std::uint32_t v_out);
    // 0x00424280: the entry of a saved palette that `index` names, as hue degrees and 0..255 saturation / value.
    void palette_hsv(std::int32_t index, std::uint32_t saved, std::uint32_t h_out, std::uint32_t s_out, std::uint32_t v_out);
    [[nodiscard]] SavedColour saved_colour(std::int32_t index, std::uint32_t saved);   // palette_hsv without the pointers
    [[nodiscard]] Hsv preset(std::int32_t index);                                        // preset_hsv without the pointers
    // 0x00424330: rebuild a palette from the saved HSV entries, shifting the first `limit` of them by
    // (dh, ds, dv) - hue added, saturation and value scaled - and store it in the engine.
    void shift_palette(std::uint32_t palette, std::uint32_t saved, std::int32_t dh, std::int32_t ds, std::int32_t dv,
                       std::int32_t limit);
    // 0x004241a0: read the engine's palette and save each colour as an HSV entry.
    void remember_palette(std::uint32_t palette, std::uint32_t saved);

private:
    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace create
}  // namespace opentony::runtime
