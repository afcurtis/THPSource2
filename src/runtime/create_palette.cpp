#include "auto_views.hpp"
#include "create_palette.hpp"

#include "create_records.hpp"

namespace opentony::runtime::create {

using namespace opentony::runtime::autoviews;

namespace {

// CaseInsensitiveBoundedNameEqual(a, b, unlimited): ASCII letters fold, bytes above 0x7f do not.
bool names_equal(const RetailMemory& m, std::uint32_t a, std::uint32_t b) {
    auto fold = [](std::uint8_t c) {
        return static_cast<std::int8_t>(c) >= 'A' && static_cast<std::int8_t>(c) <= 'Z' ? static_cast<std::uint8_t>(c + 0x20) : c;
    };
    for (;; ++a, ++b) {
        const std::uint8_t x = fold(guest::read_scalar<std::uint8_t>(m, a));
        const std::uint8_t y = fold(guest::read_scalar<std::uint8_t>(m, b));
        if (x != y) {
            return false;
        }
        if (x == 0) {
            return true;
        }
    }
}

}  // namespace

std::optional<std::uint32_t> CreatePalette::lookup_bitmap_hue(std::uint32_t name) {
    // the table ends with a pointer to an empty name
    for (std::uint32_t at = kBitmapHues; byte_at(m_, word_at(m_, at)) != 0; at += 8) {
        if (names_equal(m_, word_at(m_, at), name)) {
            return word_at(m_, at + 4);
        }
    }
    return std::nullopt;
}

bool CreatePalette::bitmap_hue(std::uint32_t name, std::uint32_t h_out, std::uint32_t s_out, std::uint32_t v_out) {
    s_.check(h_out != 0 && s_out != 0 && v_out != 0, kMsgEmpty);
    if (const auto hue = lookup_bitmap_hue(name)) {
        set_word_at(m_, h_out, *hue);
        return true;
    }
    return false;
}

CreatePalette::Average CreatePalette::average(std::uint32_t object, std::int32_t kind, std::uint32_t half) {
    const std::int32_t count = (kind == 2 && half != 0) ? 0x80 : 0x100;
    const std::uint32_t saved = word_at(m_, object + kObjSaved + 4u * static_cast<std::uint32_t>(kind));
    std::int32_t sum_h = 0;
    std::int32_t sum_s = 0;
    std::int32_t sum_v = 0;
    std::int32_t reds = 0;       // hues below 90 degrees
    std::int32_t magentas = 0;   // hues from 270 to 360 degrees
    for (std::int32_t i = 0; i < count; ++i) {
        const SavedColour c = color_.saved_colour(i, saved);
        if (c.h < 0x5a) {
            ++reds;
        } else if (c.h >= 0x10e && c.h < 0x168) {
            ++magentas;
        }
        sum_h += c.h;
        sum_s += c.s;
        sum_v += c.v;
    }
    Average out;
    out.h = sum_h / count;
    out.s = sum_s / count;
    out.v = sum_v / count;
    if (reds > 0x20 && magentas > 0x20) {
        // the palette straddles the 360 / 0 degree seam: average the hues measured from -180 degrees
        std::int32_t sum = 0;
        for (std::int32_t i = 0; i < count; ++i) {
            std::int32_t h = color_.saved_colour(i, saved).h;
            if (h > 0xb4) {
                h -= 0x168;
            }
            sum += h;
        }
        out.h = sum / count;
        if (out.h > 0x168) {
            out.h -= 0x168;
        } else if (out.h < 0) {
            out.h += 0x168;
        }
    }
    if (kind != 6 && kind != 2 && kind != 3) {
        return out;
    }
    s_.check(global_word(m_, kEditorTextureManager) != 0 || global_word(m_, kTextureManager) != 0, kMsgEmpty);
    std::uint32_t holder = global_word(m_, kTextureManager);
    if (holder == 0) {
        holder = global_word(m_, kEditorTextureManager);
        if (holder == 0) {
            return out;
        }
    }
    // the bitmap of the skater's own texture set names a hue of its own
    const std::uint32_t slot = word_at(m_, object + kObjCustomSlot);
    std::uint32_t texture;
    if (kind == 6) {
        texture = word_at(m_, tex_.find_by_id(holder, int_at(m_, kCustomRecords + slot * kCustomStride + 0x74)) + kSetTextures);
    } else {
        const std::uint32_t set = tex_.find_by_id(holder, int_at(m_, kCustomRecords + slot * kCustomStride + 0x68));
        texture = word_at(m_, set + (kind == 2 ? kSetTextures : kSetTextures + 4));
    }
    s_.check(texture != 0, kMsgEmpty);
    if (const auto hue = lookup_bitmap_hue(texture + 8)) {
        out.h = static_cast<std::int32_t>(*hue);
    }
    return out;
}

void CreatePalette::average_hsv(std::uint32_t object, std::int32_t kind, std::uint32_t h_out, std::uint32_t s_out,
                                std::uint32_t v_out, std::uint32_t half) {
    const Average a = average(object, kind, half);
    set_word_at(m_, h_out, static_cast<std::uint32_t>(a.h));
    set_word_at(m_, s_out, static_cast<std::uint32_t>(a.s));
    set_word_at(m_, v_out, static_cast<std::uint32_t>(a.v));
}

void CreatePalette::set_shift(std::uint32_t object, std::int32_t kind, std::int32_t target, std::int32_t saturation,
                              std::int32_t value, std::uint32_t half) {
    const std::uint32_t shift = object + kObjShift + 0x10u * static_cast<std::uint32_t>(kind);
    if (target == -1) {
        set_word_at(m_, shift, 0);
        set_word_at(m_, shift + 4, 0x80);
        set_word_at(m_, shift + 8, 0x80);
        return;
    }
    // the average's own saturation and value are dropped: the preset's are stored
    const Average a = average(object, kind, half);
    set_word_at(m_, shift, static_cast<std::uint32_t>(target - a.h));
    set_word_at(m_, shift + 4, static_cast<std::uint32_t>(saturation));
    set_word_at(m_, shift + 8, static_cast<std::uint32_t>(value));
}

void CreatePalette::apply_preset(std::uint32_t object, std::int32_t kind, std::uint32_t half) {
    std::uint32_t palette_at;
    std::uint32_t preset_at;
    std::uint32_t use_half = 0;
    switch (kind) {
    case 1: palette_at = object + 0x144; preset_at = object + kObjColourPresets; break;
    case 2: palette_at = object + 0x148; preset_at = object + kObjColourPresets + 1; use_half = half; break;
    case 3: palette_at = object + 0x14c; preset_at = object + kObjColourPresets + 2; break;
    case 6: palette_at = object + 0x158; preset_at = object + kObjColourPresets + 3; break;
    default: return;
    }
    if (word_at(m_, palette_at) == 0) {
        return;
    }
    const Hsv p = color_.preset(byte_at(m_, preset_at));
    set_shift(object, kind, static_cast<std::int32_t>(p.h), static_cast<std::int32_t>(p.s), static_cast<std::int32_t>(p.v), use_half);
}

void CreatePalette::update_animation(std::uint32_t object, std::int32_t kind, std::uint32_t half) {
    s_.check(kind >= 0 && kind < 9, kMsgInvalidTexpart);
    const std::uint32_t k = static_cast<std::uint32_t>(kind);
    const std::uint32_t control = object + k * 0x10;
    if (byte_at(m_, control + kObjName + 7) != '0') {
        return;
    }
    const std::int32_t limit = half != 0 ? 0x80 : 0x100;
    const std::uint32_t palette = word_at(m_, object + kObjPalettes + 4 * k);
    if (palette == 0) {
        return;
    }
    // the hue is kept below a full turn; the shift words are passed on as 16 bits (only the low twelve bits of each
    // are used by the converter, so the stale upper halves retail passes do not matter)
    set_word_at(m_, control + kObjShift, static_cast<std::uint32_t>(int_at(m_, control + kObjShift) % 0x168));
    color_.shift_palette(palette, word_at(m_, object + kObjSaved + 4 * k), short_at(m_, control + kObjShift), short_at(m_, control + kObjShift + 4),
                         short_at(m_, control + kObjShift + 8), limit);
    if (kind != 2) {
        return;
    }
    s_.check(word_at(m_, object + kObjPelvisPalette) != 0, kMsgNoPelvisPalette);
    s_.check(word_at(m_, object + kObjSaved + 8) != 0, kMsgNoPaletteBuffer);
    color_.shift_palette(word_at(m_, object + kObjPelvisPalette), word_at(m_, object + kObjSaved + 8), short_at(m_, object + kObjShift + 0x20),
                         short_at(m_, object + kObjShift + 0x24), short_at(m_, object + kObjShift + 0x28), limit);
}

void CreatePalette::set_palette_handle(std::uint32_t object, std::int32_t kind, std::uint32_t checksum) {
    const std::uint32_t k = static_cast<std::uint32_t>(kind);
    auto palette_of = [&](std::uint32_t sum) {
        s_.note(kFindMaterial, {sum});
        const auto material = static_cast<std::uint32_t>(s_.value(kFindMaterial));
        s_.check(material != 0, kMsgTextureNotFound);
        const std::uint32_t palette = word_at(m_, material + kMaterialPalette);
        s_.check(palette != 0, kMsgPaletteNotFound);
        return palette;
    };
    if (checksum != 0) {
        const std::uint32_t palette = palette_of(checksum);
        set_word_at(m_, object + kObjPalettes + 4 * k, palette);
        s_.check(word_at(m_, object + kObjSaved + 4 * k) != 0, kMsgNoPaletteBuffer);
        color_.remember_palette(palette, word_at(m_, object + kObjSaved + 4 * k));
    } else {
        set_word_at(m_, object + kObjPalettes + 4 * k, 0);
    }
    if (kind != 2) {
        return;
    }
    if (word_at(m_, object + kObjPelvisMaterial) != 0) {
        set_word_at(m_, object + kObjPelvisPalette, palette_of(word_at(m_, object + kObjPelvisMaterial)));
    } else {
        set_word_at(m_, object + kObjPelvisPalette, 0);
    }
}

}  // namespace opentony::runtime::create
