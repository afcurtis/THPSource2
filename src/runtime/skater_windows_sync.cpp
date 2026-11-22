#include "auto_views.hpp"
#include "skater_windows.hpp"

#include <array>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kGroup = 0xac;
constexpr std::uint32_t kTone = 0xb0;
constexpr std::uint32_t kIds = 0xb4;
constexpr std::uint32_t kItems = 0xd4;
constexpr std::uint32_t kSkater = 0x264;
constexpr std::uint32_t kItemSlots = 0x60;
// The locals retail keeps on its stack for the colour of a swatch are three words of a scratch region the window code owns (outside every
// region a replay compares).
constexpr std::uint32_t kScratch = 0x7ff00000;

}  // namespace

// 0x0042b4d0: bring the editor object's palettes in line with the items: a swatch that is shown takes the palette's average colour, the
// others are hidden; every colour kind's palette is rebuilt afterwards.
void SkaterWindows::appearance_refresh(std::uint32_t self) {
    m_.ensure_region(kScratch, 0x100);
    const std::uint32_t holder = global_word(m_, kTextureManagerPointer);
    const std::uint32_t object = skater_windows_sync_appearance_at(m_, self).field_264();
    auto colour = [&](std::uint32_t item) {
        h_.colour_text_colour(item, kScratch, kScratch + 4, kScratch + 8);
        return std::array<std::int32_t, 3>{global_int(m_, kScratch), global_int(m_, kScratch + 4), global_int(m_, kScratch + 8)};
    };
    auto average = [&](std::int32_t kind, std::uint32_t half) {
        pal_.average_hsv(object, kind, kScratch, kScratch + 4, kScratch + 8, half);
        return std::array<std::uint32_t, 3>{global_word(m_, kScratch), global_word(m_, kScratch + 4), global_word(m_, kScratch + 8)};
    };
    for (std::uint32_t slot = 0; slot < kItemSlots; ++slot) {
        const std::uint32_t item = word_at(m_, self + kItems + 4 * slot);
        if (item == 0) {
            continue;
        }
        const std::int32_t id = int_at(m_, item + 8);
        if (id == 4) {
            set_byte_at(m_, item + 0x0c, appearance_part_visible(self, 1) ? 1 : 0);
            if (byte_at(m_, item + 0x0c) != 0) {
                const auto hsv = colour(item);
                pal_.set_shift(object, 1, hsv[0], hsv[1], hsv[2], 0);
                const auto mean = average(1, 0);
                h_.colour_text_set_custom(item, mean[0], mean[1], mean[2]);
            }
            pal_.update_animation(object, 1, 0);
        } else if (id == 1) {
            const std::int32_t next = tex_.step(holder, int_at(m_, self + kIds + 4), int_at(m_, self + kIds + 8), 1);
            set_byte_at(m_, item + 0x0c, next != int_at(m_, self + kIds + 8) ? 1 : 0);
        } else if (id == 2) {
            set_byte_at(m_, item + 0x0c, appearance_part_visible(self, 7) ? 1 : 0);
        } else if (id == 5) {
            set_byte_at(m_, item + 0x0c, appearance_texture_flag(self, 2) ? 0 : 1);
            const std::uint32_t texture_set = tex_.find_by_id(holder, int_at(m_, self + kIds + 8));
            check(texture_set != 0, kAppMsgNoTextureSet);
            if (byte_at(m_, item + 0x0c) != 0) {
                set_word_at(m_, 0x0056a4f0 + custom_slot() * 0x80, word_at(m_, self + kIds + 4));
                const auto hsv = colour(item);
                const std::uint32_t half = byte_at(m_, word_at(m_, texture_set + 0x30) + 0x18);
                pal_.set_shift(object, 2, hsv[0], hsv[1], hsv[2], half);
                const auto mean = average(2, half);
                h_.colour_text_set_custom(item, mean[0], mean[1], mean[2]);
            }
            pal_.update_animation(object, 2, byte_at(m_, word_at(m_, texture_set + 0x30) + 0x18));
            pal_.update_animation(object, 3, 0);
        } else if (id == 6) {
            set_byte_at(m_, item + 0x0c, appearance_texture_flag(self, 3) ? 0 : 1);
            if (byte_at(m_, item + 0x0c) != 0) {
                set_word_at(m_, 0x0056a4f0 + custom_slot() * 0x80, word_at(m_, self + kIds + 4));
                const auto hsv = colour(item);
                pal_.set_shift(object, 3, hsv[0], hsv[1], hsv[2], 0);
                const auto mean = average(3, 0);
                h_.colour_text_set_custom(item, mean[0], mean[1], mean[2]);
            }
            pal_.update_animation(object, 3, 0);
        } else if (id == 7) {
            const auto hsv = colour(item);
            set_word_at(m_, 0x0056a4fc + custom_slot() * 0x80, word_at(m_, self + kIds + 0x10));
            pal_.set_shift(object, 6, hsv[0], hsv[1], hsv[2], 0);
            const auto mean = average(6, 0);
            h_.colour_text_set_custom(item, mean[0], mean[1], mean[2]);
            pal_.update_animation(object, 6, 0);
        }
    }
}

// 0x0042b880: copy each swatch's colour index into the editor object (four bytes from +0x318).
void SkaterWindows::appearance_read_colours(std::uint32_t self) {
    for (std::uint32_t slot = 0; slot < kItemSlots; ++slot) {
        const std::uint32_t item = word_at(m_, self + kItems + 4 * slot);
        if (item == 0) {
            continue;
        }
        const std::int32_t id = int_at(m_, item + 8);
        if (id >= 4 && id <= 7) {
            set_byte_at(m_, skater_windows_sync_appearance_at(m_, self).field_264() + 0x318 + u(id - 4), h_.colour_text_index(item));
        }
    }
}

// 0x0042b910: does the texture set of part 1, 4 or 7 have a second texture (any other part: yes)
bool SkaterWindows::appearance_part_visible(std::uint32_t self, std::uint32_t part) {
    std::uint32_t id = 0;
    if (part == 1) {
        id = skater_windows_sync_appearance_at(m_, self).field_b4();
    } else if (part == 4) {
        id = word_at(m_, self + kIds + 8);
    } else if (part == 7) {
        id = word_at(m_, self + kIds + 0x10);
    } else {
        return true;
    }
    const std::uint32_t texture_set = tex_.find_by_id(global_word(m_, kTextureManagerPointer), static_cast<std::int32_t>(id));
    return byte_at(m_, texture_set + 0x41) != 0;
}

// 0x0042b960: for part 2 or 3 does the texture file's name have a skin tone (its sixteenth character is not '0')? Any other part asserts.
bool SkaterWindows::appearance_texture_flag(std::uint32_t self, std::uint32_t part) {
    if (part != 2 && part != 3) {
        check(false, kAppMsgNoTexturePart);
        return false;
    }
    const std::uint32_t texture_set = tex_.find_by_id(global_word(m_, kTextureManagerPointer), int_at(m_, self + kIds + 8));
    const std::uint32_t texture = word_at(m_, texture_set + (part == 2 ? 0x30 : 0x34));
    return byte_at(m_, texture + 0x0f) != '0';
}

// 0x0042ba00: when the custom skater's name is one of the preset rows (case-insensitive), copy the preset over the record and the page.
bool SkaterWindows::appearance_load_preset(std::uint32_t self) {
    if (global_word(m_, kPresetsEnabled) == 0) {
        return false;
    }
    const std::uint32_t slot = custom_slot();
    if (global_byte(m_, kPresetNameTable) == 0) {
        return false;
    }
    std::uint32_t row = kPresetNameTable;
    const std::uint32_t name = record(slot);
    for (;;) {
        if (names_equal(name, row, 0xffffffff)) {
            break;
        }
        const std::uint8_t next = byte_at(m_, row + 0x80);
        row += 0x80;
        if (next == 0) {
            return false;
        }
    }
    check(m_.cstring(row + 0x10, 0x100).size() < 0x10, kPersonalMsgHometown);
    const std::uint32_t base = slot << 7;
    copy_text(0x0056a498 + base, m_.cstring(row + 0x10, 0x100));
    set_byte_at(m_, 0x0056a4a8 + base, byte_at(m_, row + 0x20));
    set_byte_at(m_, 0x0056a4ad + base, byte_at(m_, row + 0x25));
    set_byte_at(m_, 0x0056a4ac + base, byte_at(m_, row + 0x24));
    set_byte_at(m_, 0x0056a4ab + base, byte_at(m_, row + 0x23));
    set_word_at(m_, 0x0056a4dc + base, word_at(m_, row + 0x54));
    set_word_at(m_, 0x0056a4e0 + base, word_at(m_, row + 0x58));
    const std::uint32_t object = skater_windows_sync_appearance_at(m_, self).field_264();
    set_word_at(m_, object + 0x0c, word_at(m_, row + 0x54));
    static constexpr std::uint32_t kSwatchItems[4] = {0xdc, 0xe8, 0xec, 0xf4};
    for (std::uint32_t k = 0; k < 4; ++k) {
        set_byte_at(m_, 0x0056a4af + k + base, byte_at(m_, row + 0x27 + k));
        set_byte_at(m_, object + 0x318 + k, byte_at(m_, row + 0x27 + k));
        h_.colour_text_set_index(word_at(m_, self + kSwatchItems[k]), byte_at(m_, object + 0x318 + k));
    }
    appearance_refresh(self);
    for (std::uint32_t i = 0; i < 7; ++i) {
        const std::uint32_t id = word_at(m_, row + 0x64 + 4 * i);
        set_word_at(m_, 0x0056a4ec + base + 4 * i, id);
        set_word_at(m_, self + kIds + 4 * i, id);
    }
    const std::uint32_t holder = global_word(m_, kTextureManagerPointer);
    const std::uint32_t texture_set = tex_.find_by_id(holder, static_cast<std::int32_t>(skater_windows_sync_appearance_at(m_, self).field_b4()));
    skater_windows_sync_appearance_at(m_, self).set_field_b0(static_cast<Word>(u(add(sbyte_at(m_, word_at(m_, texture_set + 0x30) + 0x0f), -0x31))));
    skater_windows_sync_appearance_at(m_, self).set_field_ac(static_cast<Word>(u(tex_.find_by_group(holder, 0))));
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(skater_windows_sync_appearance_at(m_, self).field_b0()); ++i) {
        skater_windows_sync_appearance_at(m_, self).set_field_ac(static_cast<Word>(u(tex_.step(holder, -1, static_cast<std::int32_t>(skater_windows_sync_appearance_at(m_, self).field_ac()), 1))));
    }
    body_group_apply(self, 0, skater_windows_sync_appearance_at(m_, self).field_b4(), skater_windows_sync_appearance_at(m_, self).field_b0());
    engine(0x00422d80, {object, word_at(m_, self + kIds + 8), word_at(m_, self + kIds + 0x10), word_at(m_, self + kIds + 0x14), skater_windows_sync_appearance_at(m_, self).field_b0()});
    body_group_apply(self, 6, word_at(m_, self + kIds + 0x18), skater_windows_sync_appearance_at(m_, self).field_b0());
    engine(0x00421410, {object});
    return true;
}

// 0x0042bcf0: the page's debug dump (printf_log calls: only the format of each is reported).
void SkaterWindows::appearance_dump(std::uint32_t self, std::uint32_t record_pointer) {
    check(record_pointer != 0, kEmptyText);
    s_.note(kSvcPrintfLog, {0x00527c04});
    s_.note(kSvcPrintfLog, {0x00527bf4});
    s_.note(kSvcPrintfLog, {0x00527be8});
    s_.note(kSvcPrintfLog, {0x00527bdc});
    switch (sbyte_at(m_, record_pointer + 0x24)) {
    case 0: s_.note(kSvcPrintfLog, {0x00527b98}); break;
    case 1: s_.note(kSvcPrintfLog, {0x00527bac}); break;
    case 2: s_.note(kSvcPrintfLog, {0x00527bc4}); break;
    default: break;
    }
    switch (sbyte_at(m_, record_pointer + 0x25)) {
    case 0: s_.note(kSvcPrintfLog, {0x00527b68}); break;
    case 1: s_.note(kSvcPrintfLog, {0x00527b80}); break;
    default: break;
    }
    (void)byte_at(m_, skater_windows_sync_appearance_at(m_, self).field_264() + 0x318);
    s_.note(kSvcPrintfLog, {0x00527b4c});
    s_.note(kSvcPrintfLog, {0x00527b2c});
}

}  // namespace opentony::runtime::frontend
