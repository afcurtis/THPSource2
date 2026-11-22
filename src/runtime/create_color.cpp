#include "auto_views.hpp"
#include "create_color.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::create {

using namespace opentony::runtime::autoviews;

std::uint8_t CreateColor::min3(std::uint8_t a, std::uint8_t b, std::uint8_t c) noexcept {
    std::uint8_t out = a;
    if (b < out) {
        out = b;
    }
    if (c < out) {
        out = c;
    }
    return out;
}

std::uint8_t CreateColor::max3(std::uint8_t a, std::uint8_t b, std::uint8_t c) noexcept {
    std::uint8_t out = a;
    if (b > out) {
        out = b;
    }
    if (c > out) {
        out = c;
    }
    return out;
}

Hsv CreateColor::to_hsv(std::uint32_t r, std::uint32_t g, std::uint32_t b) noexcept {
    const auto r8 = static_cast<std::uint8_t>(r);
    const auto g8 = static_cast<std::uint8_t>(g);
    const auto b8 = static_cast<std::uint8_t>(b);
    const std::int32_t lo = min3(r8, g8, b8);
    const std::int32_t hi = max3(r8, g8, b8);
    Hsv out;
    out.v = static_cast<std::uint32_t>(hi << 12);
    if (hi == 0) {
        out.s = 0;
        out.h = 0;
        return out;
    }
    const std::int32_t delta = hi - lo;
    out.s = static_cast<std::uint32_t>((delta * 0x100000) / hi);
    std::int32_t numerator;
    std::int32_t sector;
    if (r8 == hi) {
        numerator = static_cast<std::int32_t>(g8) - b8;
        sector = 0;
    } else if (g8 == hi) {
        numerator = static_cast<std::int32_t>(b8) - r8;
        sector = 2;
    } else {
        numerator = static_cast<std::int32_t>(r8) - g8;
        sector = 4;
    }
    std::uint32_t hue = 0;
    if (delta != 0) {
        hue = static_cast<std::uint32_t>((numerator * 0x100000) / delta + sector * 0x100000);
    }
    hue *= 60;  // the hue so far counts sixths of a turn; degrees are 60 of them
    if (static_cast<std::int32_t>(hue) < 0) {
        hue += static_cast<std::uint32_t>(kHueFull);
    }
    out.h = hue;
    return out;
}

Rgb CreateColor::to_rgb(std::int32_t h, std::int32_t s, std::int32_t v) noexcept {
    Rgb out;
    if (h < 0) {
        h = static_cast<std::int32_t>(static_cast<std::uint32_t>(h) + kHueFull);
    }
    out.hue_ok = h >= 0;
    std::int32_t wrapped = h;
    if (h >= static_cast<std::int32_t>(kHueFull)) {
        wrapped = static_cast<std::int32_t>(static_cast<std::uint32_t>(h) - kHueFull);
        if (wrapped >= static_cast<std::int32_t>(kHueFull)) {
            wrapped = static_cast<std::int32_t>(static_cast<std::uint32_t>(h) - 2 * kHueFull);
        }
    }
    if (v < 0x100000) {
        if (v < 0) {
            v = 0;
        }
    } else {
        v = 0xfffff;
    }
    const auto value = static_cast<std::uint8_t>(v >> 12);
    if (s < 0x100000) {
        if (s <= 0) {
            out.r = out.g = out.b = value;
            out.written = true;
            return out;
        }
    } else {
        s = 0xfffff;
    }
    const std::uint32_t sector = static_cast<std::uint32_t>(wrapped / 60);
    const std::int32_t v2 = v >> 10;
    const std::int32_t s2 = s >> 10;
    const auto frac = static_cast<std::int32_t>(sector & 0xfffff);
    const auto rising = static_cast<std::uint8_t>((((0x100000 - (((0x100000 - frac) >> 10) * s2)) >> 10) * v2) >> 12);
    const auto floor_value = static_cast<std::uint8_t>((((0x100000 - s) >> 10) * v2) >> 12);
    const auto falling = static_cast<std::uint8_t>((((0x100000 - ((frac >> 10) * s2)) >> 10) * v2) >> 12);
    switch (sector >> 20) {
    case 0: out.r = value; out.g = rising; out.b = floor_value; break;
    case 1: out.r = falling; out.g = value; out.b = floor_value; break;
    case 2: out.r = floor_value; out.g = value; out.b = rising; break;
    case 3: out.r = floor_value; out.g = falling; out.b = value; break;
    case 4: out.r = rising; out.g = floor_value; out.b = value; break;
    case 5: out.r = value; out.g = floor_value; out.b = falling; break;
    default: out.sector_ok = false; return out;
    }
    out.written = true;
    return out;
}

void CreateColor::rgb_to_hsv(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t h_out, std::uint32_t s_out,
                             std::uint32_t v_out) {
    s_.check(h_out != 0, kMsgNoPointerH);
    s_.check(s_out != 0, kMsgNoPointerS);
    s_.check(v_out != 0, kMsgNoPointerV);
    const Hsv hsv = to_hsv(r, g, b);
    set_word_at(m_, v_out, hsv.v);
    set_word_at(m_, s_out, hsv.s);
    set_word_at(m_, h_out, hsv.h);
}

void CreateColor::hsv_to_rgb(std::int32_t h, std::int32_t s, std::int32_t v, std::uint32_t r_out, std::uint32_t g_out,
                             std::uint32_t b_out) {
    s_.check(r_out != 0, kMsgNoPointerR);
    s_.check(g_out != 0, kMsgNoPointerG);
    s_.check(b_out != 0, kMsgNoPointerB);
    const Rgb rgb = to_rgb(h, s, v);
    s_.check(rgb.hue_ok, kMsgBadHue);
    if (!rgb.sector_ok) {
        s_.check(false, kMsgBadSector);
        return;
    }
    set_byte_at(m_, r_out, rgb.r);
    set_byte_at(m_, g_out, rgb.g);
    set_byte_at(m_, b_out, rgb.b);
}

Hsv CreateColor::preset(std::int32_t index) {
    if (index == 0) {
        return {0xffffffffu, 0xffffffffu, 0xffffffffu};
    }
    const std::uint32_t row = kPalettePresets + static_cast<std::uint32_t>(index) * 12u;
    return {word_at(m_, row), word_at(m_, row + 4), word_at(m_, row + 8)};
}

void CreateColor::preset_hsv(std::int32_t index, std::uint32_t h_out, std::uint32_t s_out, std::uint32_t v_out) {
    s_.check(h_out != 0, kMsgNoPointerH);
    s_.check(s_out != 0, kMsgNoPointerS);
    s_.check(v_out != 0, kMsgNoPointerV);
    const Hsv p = preset(index);
    set_word_at(m_, h_out, p.h);
    set_word_at(m_, s_out, p.s);
    set_word_at(m_, v_out, p.v);
}

SavedColour CreateColor::saved_colour(std::int32_t index, std::uint32_t saved) {
    s_.check(saved != 0, kMsgNoSourcePalette);
    // Retail walks `index` entries but keeps the one before the last step: entry `index - 1`
    // (entry 0 for an index of zero or less).
    std::uint32_t entry = saved;
    if (index > 0) {
        entry = saved + static_cast<std::uint32_t>(index - 1) * 16u;
    }
    SavedColour out;
    out.h = int_at(m_, entry) >> 20;
    out.s = (int_at(m_, entry + 4) * 255) >> 20;
    out.v = (int_at(m_, entry + 8) * 255) >> 20;
    return out;
}

void CreateColor::palette_hsv(std::int32_t index, std::uint32_t saved, std::uint32_t h_out, std::uint32_t s_out,
                              std::uint32_t v_out) {
    const SavedColour c = saved_colour(index, saved);
    set_word_at(m_, h_out, static_cast<std::uint32_t>(c.h));
    set_word_at(m_, s_out, static_cast<std::uint32_t>(c.s));
    set_word_at(m_, v_out, static_cast<std::uint32_t>(c.v));
}

void CreateColor::shift_palette(std::uint32_t palette, std::uint32_t saved, std::int32_t dh, std::int32_t ds, std::int32_t dv,
                                std::int32_t limit) {
    s_.check(palette != 0, kMsgNoRectangle);
    s_.check(saved != 0, kMsgNoSourceToModify);
    const std::int32_t count = (byte_at(m_, palette + 3) & 1) != 0 ? 0x10 : 0x100;
    std::string colours(static_cast<std::size_t>(count) * 2, '\0');
    // The converted colour stays in locals that a rejected hue leaves as the previous entry's.
    std::uint32_t r = 0;
    std::uint32_t g = 0;
    std::uint32_t b = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t entry = saved + static_cast<std::uint32_t>(i) * 16u;
        std::int32_t h = int_at(m_, entry);
        std::int32_t s = int_at(m_, entry + 4);
        std::int32_t v = int_at(m_, entry + 8);
        const std::uint32_t alpha = word_at(m_, entry + 12);
        if (i < limit) {
            h = static_cast<std::int32_t>(static_cast<std::uint32_t>(dh) * 0x100000u + static_cast<std::uint32_t>(h));
            const auto sd = static_cast<std::int32_t>(static_cast<std::uint32_t>(ds) * 0x100000u);
            const auto vd = static_cast<std::int32_t>(static_cast<std::uint32_t>(dv) * 0x100000u);
            s = ((sd + ((sd >> 31) & 0x7f)) >> 17) * (s >> 10);
            v = ((vd + ((vd >> 31) & 0x7f)) >> 17) * (v >> 10);
        }
        const Rgb rgb = to_rgb(h, s, v);
        s_.check(rgb.hue_ok, kMsgBadHue);
        if (!rgb.sector_ok) {
            s_.check(false, kMsgBadSector);
        } else {
            r = rgb.r;
            g = rgb.g;
            b = rgb.b;
        }
        r >>= 3;
        g >>= 3;
        b >>= 3;
        if (r == 0) {
            r = 1;
        }
        if (i > 0xff) {
            r = g = b = 0;
        }
        const std::uint32_t packed = ((b * 0x20u + g) * 0x20u) + (alpha & 0x8000u) + r;
        colours[static_cast<std::size_t>(i) * 2] = static_cast<char>(packed);
        colours[static_cast<std::size_t>(i) * 2 + 1] = static_cast<char>(packed >> 8);
    }
    if (s_.live()) {
        m_.write(kPaletteBuffer, std::vector<std::uint8_t>(colours.begin(), colours.end()));
        s_.note(kPaletteStore, {palette, kPaletteBuffer}, colours);
        return;
    }
    s_.note(kPaletteStore, {palette}, colours);
}

void CreateColor::remember_palette(std::uint32_t palette, std::uint32_t saved) {
    const std::int32_t count = (byte_at(m_, palette + 3) & 1) != 0 ? 0x10 : 0x100;
    std::vector<std::uint16_t> colours(0x100);
    if (s_.live()) {
        s_.note(kPaletteRead, {palette, kPaletteBuffer});
        for (std::int32_t i = 0; i < count; ++i) {
            colours[static_cast<std::size_t>(i)] = half_at(m_, kPaletteBuffer + 2 * static_cast<std::uint32_t>(i));
        }
    } else {
        s_.note(kPaletteRead, {palette});
        for (std::int32_t i = 0; i < (count == 0x10 ? 8 : 128); ++i) {
            const auto word = static_cast<std::uint32_t>(s_.value(kPaletteRead));
            colours[static_cast<std::size_t>(i) * 2] = static_cast<std::uint16_t>(word);
            colours[static_cast<std::size_t>(i) * 2 + 1] = static_cast<std::uint16_t>(word >> 16);
        }
    }
    for (std::int32_t i = 0; i < count && i < 0x100; ++i) {
        const std::uint16_t c = colours[static_cast<std::size_t>(i)];
        const Hsv hsv = to_hsv((c & 0x1fu) << 3, ((c >> 5) & 0x1fu) << 3, ((c >> 10) & 0x1fu) << 3);
        const std::uint32_t entry = saved + static_cast<std::uint32_t>(i) * 16u;
        set_word_at(m_, entry, hsv.h);
        set_word_at(m_, entry + 4, hsv.s);
        set_word_at(m_, entry + 8, hsv.v);
        set_word_at(m_, entry + 12, c & 0x8000u);
    }
}

}  // namespace opentony::runtime::create
