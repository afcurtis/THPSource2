#include "auto_views.hpp"
#include "level_gen.hpp"

namespace opentony::runtime::levelgen {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kModels = 0x18;
constexpr std::uint32_t kModelBytes = 0x24;
}  // namespace

// 0x0043c770: put a piece of `type` into cell (x, z) on a layer with a turn: the cell remembers it, a piece object is made and put at the end of the list, and the cells it
// covers point to it. A start piece first takes the old start piece away; a horse piece is recorded.
void LevelGen::place_piece(std::uint32_t self, std::uint32_t x, std::uint32_t z, std::uint32_t layer, std::uint32_t rotation, std::uint32_t type) {
    for (std::uint32_t j = 0; j < 2; ++j) {
        if (type == word_at(m_, kStartTypes + 4 * j)) {
            const std::uint32_t record = self + 0x369c + 0x14 * j;
            if (byte_at(m_, record) != 0) {
                remove_top(self, word_at(m_, record + 4), word_at(m_, record + 8), 0, 1);
            }
            set_byte_at(m_, record, 1);
            set_word_at(m_, record + 4, x);
            set_word_at(m_, record + 8, z);
            set_word_at(m_, record + 0xc, layer);
            set_word_at(m_, record + 0x10, (rotation - 1u) & 3);
        }
    }
    if (type == global_word(m_, kHorseType)) {
        s_.check(int_at(m_, self + 0x3698) < 6, kMsgHorseFull);
        for (std::uint32_t i = 0; i < 6; ++i) {
            const std::uint32_t record = self + 0x36c4 + 0x14 * i;
            if (byte_at(m_, record) == 0) {
                set_byte_at(m_, record, 1);
                set_word_at(m_, record + 4, x);
                set_word_at(m_, record + 8, z);
                set_word_at(m_, record + 0xc, layer);
                set_word_at(m_, record + 0x10, (rotation - 1u) & 3);
                set_word_at(m_, self + 0x3698, word_at(m_, self + 0x3698) + 1);
                break;
            }
        }
    }
    s_.check(static_cast<std::int32_t>(z) >= 0 && static_cast<std::int32_t>(z) < global_int(m_, kGridD), kMsgGridZ);
    const std::uint32_t row = word_at(m_, global_word(m_, kRows) + 4 * z);
    set_half_at(m_, row + (layer + 8 * x) * 2, type);
    set_byte_at(m_, word_at(m_, global_word(m_, kRows) + 4 * z) + x * 0x10 + 0xa + layer, rotation);
    const std::uint32_t in = self + kModels + type * kModelBytes;
    const std::uint32_t block = g_.new_zeroed(0x238);
    const std::uint32_t piece = block != 0 ? g_.out_construct(block, in) : 0;
    std::uint32_t tail = word_at(m_, self + 0x2344);
    if (tail == 0) {
        set_word_at(m_, self + 0x2344, piece);
    } else {
        for (std::uint32_t next = word_at(m_, tail + 0x230); next != 0; next = word_at(m_, next + 0x230)) {
            tail = next;
        }
        set_word_at(m_, tail + 0x230, piece);
    }
    std::int32_t offset_a = 0;
    std::int32_t offset_b = 0;
    turn_piece(piece, in, rotation, offset_a, offset_b);
    set_byte_at(m_, piece + 9, byte_at(m_, in + 0xb));
    const std::int32_t cell_x = global_int(m_, kCellX);
    const std::int32_t cell_z = global_int(m_, kCellZ);
    set_byte_at(m_, piece + 5, x);
    set_byte_at(m_, piece + 7, z);
    set_half_at(m_, piece + 0x21e, static_cast<std::uint32_t>(-(cell_z * static_cast<std::int32_t>(layer))));
    set_half_at(m_, piece + 0x220, static_cast<std::uint32_t>(((global_int(m_, kGridD) >> 1) - static_cast<std::int32_t>(z)) * cell_x - offset_b));
    set_half_at(m_, piece + 0x21c, static_cast<std::uint32_t>(((-global_int(m_, kGridW) >> 1) + static_cast<std::int32_t>(x)) * cell_x + offset_a));
    set_byte_at(m_, piece + 6, layer);
    set_byte_at(m_, piece + 0xc, byte_at(m_, word_at(m_, global_word(m_, kRows) + 4 * z) + x * 0x10 + 0xa + layer));
    for (std::int32_t zz = static_cast<std::int32_t>(z); zz < sbyte_at(m_, piece + 0xa) + static_cast<std::int32_t>(z); ++zz) {
        std::uint32_t slot = (layer + 5 * x) * 4;
        for (std::int32_t xx = static_cast<std::int32_t>(x); xx < sbyte_at(m_, piece + 8) + static_cast<std::int32_t>(x); ++xx) {
            for (std::int32_t k = 0; k < sbyte_at(m_, piece + 9); ++k) {
                s_.check(zz >= 0 && zz < global_int(m_, kGridD) && xx >= 0 && xx < global_int(m_, kGridW), kMsgOutOfBounds);
                const std::uint32_t map_row = word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(zz));
                s_.check(word_at(m_, map_row + slot + 4 * static_cast<std::uint32_t>(k)) == 0, kMsgOverlap3);
                set_word_at(m_, map_row + slot + 4 * static_cast<std::uint32_t>(k), piece);
            }
            slot += 0x14;
        }
    }
}

// 0x0043cc30: take (or update) an item record: a free record (marker 0x80) gets the cell on both ends and the disabled marker; a given record only gets its second end
// moved, its turns worked out from the direction between the ends, and the current marker. Returns the record's index (-1: none free).
std::uint32_t LevelGen::add_item(std::uint32_t x, std::uint32_t layer, std::uint32_t z, std::uint32_t index) {
    const bool update = index != 0xffffffff;
    std::uint32_t found = index;
    if (!update) {
        found = 0;
        while (found < kItemRecords && byte_at(m_, global_word(m_, kItems) + found * kItemBytes + 0xc) != 0x80) {
            ++found;
        }
        if (found == kItemRecords) {
            return 0xffffffff;
        }
    }
    const std::uint32_t offset = found * kItemBytes;
    set_byte_at(m_, offset + global_word(m_, kItems) + 0xc, update ? global_byte(m_, kGapMarker) : 0x81);
    for (std::uint32_t end = update ? 1 : 0; end < 2; ++end) {
        set_byte_at(m_, global_word(m_, kItems) + offset + end, x);
        set_byte_at(m_, global_word(m_, kItems) + offset + end + 2, layer);
        set_byte_at(m_, global_word(m_, kItems) + offset + end + 4, z);
        set_byte_at(m_, global_word(m_, kItems) + offset + end + 8, 0);
        set_byte_at(m_, global_word(m_, kItems) + offset + end + 0xa, 0);
    }
    if (update) {
        const std::uint32_t item = global_word(m_, kItems) + offset;
        const std::int32_t dx = static_cast<std::int32_t>(byte_at(m_, item + 1)) - static_cast<std::int32_t>(byte_at(m_, item));
        const std::int32_t dz = static_cast<std::int32_t>(byte_at(m_, item + 5)) - static_cast<std::int32_t>(byte_at(m_, item + 4));
        if ((dx < 0 ? -dx : dx) > (dz < 0 ? -dz : dz)) {
            if (dx < 0) {
                set_byte_at(m_, item + 6, 2);
                set_byte_at(m_, global_word(m_, kItems) + offset + 7, 0);
            } else {
                set_byte_at(m_, item + 6, 0);
                set_byte_at(m_, global_word(m_, kItems) + offset + 7, 2);
            }
        } else if (dz < 0) {
            set_byte_at(m_, item + 6, 1);
            set_byte_at(m_, global_word(m_, kItems) + offset + 7, 3);
        } else {
            set_byte_at(m_, item + 6, 3);
            set_byte_at(m_, global_word(m_, kItems) + offset + 7, 1);
        }
    }
    return found;
}

// 0x0043cd90: the gap members of one end of an item are laid out again from the item's record.
void LevelGen::refresh_item_gaps(std::uint32_t self, std::uint32_t item_index, std::uint32_t side) {
    for (std::uint32_t gap = word_at(m_, self + 0x2388); gap != 0; gap = word_at(m_, gap + 0xc)) {
        if (word_at(m_, gap + 0x1c) == item_index && (byte_at(m_, gap) == 0 ? 1u : 0u) == side) {
            const std::uint32_t record = global_word(m_, kItems) + side + item_index * kItemBytes;
            g_.gap_compute_edges(gap, byte_at(m_, record + 6), byte_at(m_, record + 8), byte_at(m_, record + 0xa));
            g_.gap_build_header(gap);
        }
    }
}

// 0x0043ce00: every item with an end at (x, layer, z) is freed.
void LevelGen::remove_items_at(std::uint32_t x, std::uint32_t layer, std::uint32_t z) {
    for (std::uint32_t offset = 0; offset < kItemRecords * kItemBytes; offset += kItemBytes) {
        for (std::uint32_t end = 0; end < 2; ++end) {
            const std::uint32_t item = global_word(m_, kItems) + offset;
            if (byte_at(m_, item + end) == x && byte_at(m_, item + end + 2) == layer && byte_at(m_, item + end + 4) == z) {
                set_byte_at(m_, offset + global_word(m_, kItems) + 0xc, 0x80);
            }
        }
    }
}

// 0x0043ce60: the flash of the pieces that have a gap, and of the disabled item's piece, steps on (the level goes up and down between 0 and 0x7f).
void LevelGen::set_flash(std::uint32_t self, std::uint32_t on) {
    set_byte_at(m_, self + 0x2394, on);
    for (std::uint32_t piece = word_at(m_, self + 0x2344); piece != 0; piece = word_at(m_, piece + 0x230)) {
        if (word_at(m_, piece + 0x234) != 0 && byte_at(m_, self + 0x2394) != 0) {
            g_.out_set_fog(piece, 1, 0x80);
        } else {
            g_.out_set_fog(piece, 0, 0);
        }
    }
    const std::uint32_t disabled = word_at(m_, self + 0x2390);
    if (disabled != 0 && byte_at(m_, self + 0x2394) != 0) {
        g_.out_set_fog(word_at(m_, disabled + 4), 1, word_at(m_, self + 0x2398));
        const std::int32_t level = int_at(m_, self + 0x2398) + int_at(m_, self + 0x239c);
        set_word_at(m_, self + 0x2398, static_cast<std::uint32_t>(level));
        if (level < 0) {
            set_word_at(m_, self + 0x2398, 0);
            set_word_at(m_, self + 0x239c, 0x10);
            return;
        }
        if (level > 0x7f) {
            set_word_at(m_, self + 0x2398, 0x7f);
            set_word_at(m_, self + 0x239c, 0xfffffff0);
        }
    }
}

// 0x0043cf20: take the top piece of cell (x, z) away. Fails when the cell is empty, the piece is lower than `*limit_at`, or it is a solid block (unless forced) or has something
// on top of it. A piece leaves its cells and layers in the park, its neighbours are asked to look again, and its start, horse and item records go.
bool LevelGen::remove_top(std::uint32_t self, std::uint32_t x, std::uint32_t z, std::uint32_t limit_at, std::uint32_t force) {
    const std::int32_t limit = limit_at != 0 ? int_at(m_, limit_at) : 0;
    std::uint32_t k = 4;
    std::uint32_t offset = x * 0x14 + 0x10;
    std::uint32_t piece = 0;
    for (;;) {
        s_.check(static_cast<std::int32_t>(z) >= 0 && static_cast<std::int32_t>(z) < global_int(m_, kGridD), kMsgOutMapZ);
        piece = word_at(m_, word_at(m_, word_at(m_, self + 0x2348) + 4 * z) + offset);
        if (piece != 0) {
            break;
        }
        if (k == 0) {
            return false;
        }
        --k;
        offset -= 4;
    }
    const std::uint32_t layer = byte_at(m_, piece + 6);
    if (static_cast<std::int32_t>(layer) < limit) {
        return false;
    }
    const std::uint32_t in = word_at(m_, piece + 0x224);
    if ((byte_at(m_, in + 9) & 0x10) != 0 && (force & 0xff) == 0) {
        return false;
    }
    if (limit_at != 0) {
        set_word_at(m_, limit_at, layer);
    }
    const std::int32_t z0 = byte_at(m_, piece + 7);
    const std::int32_t x0 = byte_at(m_, piece + 5);
    const std::int32_t z_end = sbyte_at(m_, piece + 0xa) + z0;
    const std::int32_t x_end = sbyte_at(m_, piece + 8) + x0;
    const std::uint32_t type = byte_at(m_, in + 8);
    if ((byte_at(m_, in + 9) & 0x10) != 0) {
        const std::int32_t height = sbyte_at(m_, piece + 9);
        for (std::int32_t zz = z0; zz < z_end; ++zz) {
            if (x0 < x_end) {
                std::uint32_t slot = (static_cast<std::uint32_t>(height) + 1 + layer + static_cast<std::uint32_t>(x0) * 5) * 4;
                for (std::int32_t xx = x0; xx < x_end; ++xx) {
                    s_.check(zz >= 0 && zz < global_int(m_, kGridD), kMsgOutMapZ);
                    if (word_at(m_, word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(zz)) + slot - 4) != 0) {
                        return false;
                    }
                    slot += 0x14;
                }
            }
        }
    }
    for (std::int32_t zz = z0; zz < z_end; ++zz) {
        if (x0 < x_end) {
            std::uint32_t cell_offset = static_cast<std::uint32_t>(x0) << 4;
            std::uint32_t cell_index = layer + static_cast<std::uint32_t>(x0) * 8;
            std::uint32_t slot = (layer + static_cast<std::uint32_t>(x0) * 5) * 4;
            for (std::int32_t count = x_end - x0; count > 0; --count) {
                std::uint32_t at = slot;
                for (std::int32_t kk = 0; kk < sbyte_at(m_, piece + 9); ++kk) {
                    s_.check(zz >= 0 && zz < global_int(m_, kGridD), kMsgOutMapZ);
                    const std::uint32_t map_row = word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(zz));
                    const std::uint32_t other = word_at(m_, map_row + at);
                    if (other != 0) {
                        set_byte_at(m_, other, 1);
                        set_word_at(m_, word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(zz)) + at, 0);
                        const std::uint32_t source_row = word_at(m_, global_word(m_, kRows) + 4 * static_cast<std::uint32_t>(zz));
                        set_half_at(m_, source_row + (cell_index + static_cast<std::uint32_t>(kk)) * 2, 0xffff);
                        set_byte_at(m_, word_at(m_, global_word(m_, kRows) + 4 * static_cast<std::uint32_t>(zz)) + 0xf + cell_offset, layer + 1);
                    }
                    at += 4;
                }
                slot += 0x14;
                cell_index += 8;
                cell_offset += 0x10;
            }
        }
    }
    for (std::int32_t zz = z0 - 1; zz < z_end + 1; ++zz) {
        std::int32_t xx = x0 - 1;
        if (xx < x_end + 1) {
            std::int32_t base = xx * 5;
            do {
                for (std::int32_t k2 = 0; k2 < 5; ++k2) {
                    if (base > -1 && xx < global_int(m_, kGridW) && zz > -1 && zz < global_int(m_, kGridD)) {
                        const std::uint32_t other =
                            word_at(m_, word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(zz)) + static_cast<std::uint32_t>(base + k2) * 4);
                        if (other != 0) {
                            set_byte_at(m_, other + 1, 2);
                            set_byte_at(m_, other + 2, 1);
                        }
                    }
                }
                ++xx;
                base += 5;
            } while (xx < x_end + 1);
        }
    }
    for (std::uint32_t j = 0; j < 2; ++j) {
        if (type == word_at(m_, kStartTypes + 4 * j)) {
            set_byte_at(m_, self + 0x369c + 0x14 * j, 0);
        }
    }
    if (type == global_word(m_, kHorseType)) {
        std::int32_t i = 0;
        for (; i < 6; ++i) {
            const std::uint32_t record = self + 0x36c8 + 0x14 * static_cast<std::uint32_t>(i);
            if (byte_at(m_, record - 4) != 0 && word_at(m_, record) == static_cast<std::uint32_t>(x0) && word_at(m_, record + 4) == static_cast<std::uint32_t>(z0)) {
                set_byte_at(m_, self + 0x36c4 + 0x14 * static_cast<std::uint32_t>(i), 0);
                set_word_at(m_, self + 0x3698, word_at(m_, self + 0x3698) - 1);
                break;
            }
        }
        s_.check(i < 6, kMsgNoHorse);
    }
    remove_items_at(static_cast<std::uint32_t>(x0), layer, static_cast<std::uint32_t>(z0));
    return true;
}

}  // namespace opentony::runtime::levelgen
