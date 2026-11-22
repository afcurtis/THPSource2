#include "auto_views.hpp"
#include "level_gen.hpp"

namespace opentony::runtime::levelgen {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kModels = 0x18;
constexpr std::uint32_t kModelBytes = 0x24;
}  // namespace

// ---- the floor table -------------------------------------------------------------------------------------------------------

// 0x0043a3e0: FloorTab from the park's cells. Every cell gets the fill piece of its layer under the stack, then the footprint of each placed piece is cleared on the
// layers it takes, and neighbouring fill pieces are merged (pairs of cells in a row or a column become one wider piece).
void LevelGen::build_floor_table(std::uint32_t self) {
    const auto dst_row = [&](std::int32_t z) { return word_at(m_, level_gen_pieces_build_at(m_, self).field_0() + 4 * static_cast<std::uint32_t>(z)); };
    const auto src_row = [&](std::int32_t z) { return word_at(m_, global_word(m_, kRows) + 4 * static_cast<std::uint32_t>(z)); };
    for (std::uint32_t y = 0; y < 5; ++y) {
        for (std::int32_t z = 0; z < global_int(m_, kGridD); ++z) {
            for (std::int32_t x = 0; x < global_int(m_, kGridW); ++x) {
                const std::uint32_t dst = dst_row(z) + 0x10 * static_cast<std::uint32_t>(x);
                set_half_at(m_, dst + 2 * y, 0xffff);
                set_byte_at(m_, dst + 0xa + y, 0);
                if (y < byte_at(m_, src_row(z) + 0x10 * static_cast<std::uint32_t>(x) + 0xf)) {
                    set_half_at(m_, dst + 2 * y, half_at(m_, kSpecialA + 4 * y));
                }
            }
        }
    }
    std::int32_t w = 0;       // (a rotation outside 0..3 leaves the sizes of the cell before)
    std::int32_t d = 0;
    for (std::int32_t y = 0; y < 5; ++y) {
        for (std::int32_t z = 0; z < global_int(m_, kGridD); ++z) {
            for (std::int32_t x = 0; x < global_int(m_, kGridW); ++x) {
                const std::uint32_t cell = src_row(z) + 0x10 * static_cast<std::uint32_t>(x);
                if (y >= static_cast<std::int32_t>(byte_at(m_, cell + 0xf))) {
                    continue;
                }
                const std::int32_t model = short_at(m_, cell + 2 * static_cast<std::uint32_t>(y));
                if (model == -1) {
                    continue;
                }
                const std::uint32_t rotation = byte_at(m_, cell + 0xa + static_cast<std::uint32_t>(y));
                const std::uint32_t in = self + kModels + static_cast<std::uint32_t>(model) * kModelBytes;
                switch (rotation) {
                case 0:
                case 2:
                    w = sbyte_at(m_, in + 0xa);
                    d = sbyte_at(m_, in + 0xc);
                    break;
                case 1:
                case 3:
                    w = sbyte_at(m_, in + 0xc);
                    d = sbyte_at(m_, in + 0xa);
                    break;
                default:
                    break;
                }
                const std::int32_t height = sbyte_at(m_, in + 0xb);
                const std::uint32_t kind = byte_at(m_, in + 9);
                s_.check(z >= 0 && z < global_int(m_, kGridD), kMsgFloorZ);
                const std::int32_t z_end = d + z;
                s_.check(z_end >= 0 && z_end <= global_int(m_, kGridD), kMsgFloorZ);
                if ((kind & 0x10) != 0) {
                    const std::int32_t first = y <= 0 ? 1 : 0;
                    for (std::int32_t zz = z; zz < z_end; ++zz) {
                        for (std::int32_t xx = x; xx < x + w; ++xx) {
                            for (std::int32_t j = 0; j < height - first; ++j) {
                                set_half_at(m_, dst_row(zz) + 2 * static_cast<std::uint32_t>(first + 8 * xx + y + j), 0xffff);
                            }
                        }
                    }
                }
                if ((kind & 2) != 0) {
                    for (std::int32_t zz = z; zz < z_end; ++zz) {
                        for (std::int32_t xx = x; xx < x + w; ++xx) {
                            set_half_at(m_, dst_row(zz) + 2 * static_cast<std::uint32_t>(y) + 0x10 * static_cast<std::uint32_t>(xx), 0xffff);
                        }
                    }
                }
                if (level_gen_pieces_build_at(m_, self).field_3744() != 0 && (kind & 4) != 0 && y > 0) {
                    for (std::int32_t zz = z; zz < z_end; ++zz) {
                        for (std::int32_t xx = x; xx < x + w; ++xx) {
                            set_half_at(m_, dst_row(zz) + 2 * static_cast<std::uint32_t>(y) + 0x10 * static_cast<std::uint32_t>(xx), 0xffff);
                        }
                    }
                }
            }
        }
    }
    const std::int32_t layers = level_gen_pieces_build_at(m_, self).field_3744() != 0 ? 5 : 1;
    for (std::int32_t a = 0; a < layers; ++a) {
        for (std::int32_t zi = 0; zi < global_int(m_, kGridD); zi += 2) {
            for (std::int32_t xi = 0; xi < global_int(m_, kGridW); xi += 2) {
                const std::uint32_t offset = 2 * static_cast<std::uint32_t>(a) + 0x10 * static_cast<std::uint32_t>(xi);
                const std::int32_t fill = int_at(m_, kSpecialA + 4 * static_cast<std::uint32_t>(a));
                if (short_at(m_, dst_row(zi) + offset) == fill && short_at(m_, dst_row(zi) + offset + 0x10) == fill && short_at(m_, dst_row(zi + 1) + offset) == fill &&
                    short_at(m_, dst_row(zi + 1) + offset + 0x10) == fill) {
                    set_half_at(m_, dst_row(zi) + offset, half_at(m_, kSpecialC + 4 * static_cast<std::uint32_t>(a)));
                    set_half_at(m_, dst_row(zi) + offset + 0x10, 0xffff);
                    set_half_at(m_, dst_row(zi + 1) + offset, 0xffff);
                    set_half_at(m_, dst_row(zi + 1) + offset + 0x10, 0xffff);
                }
            }
        }
    }
    for (std::int32_t a = 0; a < layers; ++a) {
        for (std::int32_t zi = 0; zi < global_int(m_, kGridD); ++zi) {
            for (std::int32_t xi = 0; xi < global_int(m_, kGridW); ++xi) {
                const std::uint32_t offset = 2 * static_cast<std::uint32_t>(a) + 0x10 * static_cast<std::uint32_t>(xi);
                const std::int32_t fill = int_at(m_, kSpecialA + 4 * static_cast<std::uint32_t>(a));
                if (xi < global_int(m_, kGridW) - 1 && short_at(m_, dst_row(zi) + offset) == fill && short_at(m_, dst_row(zi) + offset + 0x10) == fill) {
                    set_half_at(m_, dst_row(zi) + offset, half_at(m_, kSpecialB + 4 * static_cast<std::uint32_t>(a)));
                    set_half_at(m_, dst_row(zi) + offset + 0x10, 0xffff);
                    set_byte_at(m_, dst_row(zi) + 0x10 * static_cast<std::uint32_t>(xi) + 0xa + static_cast<std::uint32_t>(a), 0);
                } else if (zi < global_int(m_, kGridD) - 1 && short_at(m_, dst_row(zi) + offset) == fill && short_at(m_, dst_row(zi + 1) + offset) == fill) {
                    set_half_at(m_, dst_row(zi) + offset, half_at(m_, kSpecialB + 4 * static_cast<std::uint32_t>(a)));
                    set_half_at(m_, dst_row(zi + 1) + offset, 0xffff);
                    set_byte_at(m_, dst_row(zi) + 0x10 * static_cast<std::uint32_t>(xi) + 0xa + static_cast<std::uint32_t>(a), 3);
                }
            }
        }
    }
}

// ---- pieces ------------------------------------------------------------------------------------------------------------------

// The size of a piece by its turn: the width and depth in cells swap for the odd turns, and the model's extent gives the offsets the position is moved by.
void LevelGen::turn_piece(std::uint32_t piece, std::uint32_t in, std::uint32_t rotation, std::int32_t& offset_a, std::int32_t& offset_b) {
    switch (rotation) {
    case 0:
        offset_a = short_at(m_, in + 0xe);
        offset_b = short_at(m_, in + 0x12);
        set_byte_at(m_, piece + 8, byte_at(m_, in + 0xa));
        set_byte_at(m_, piece + 0xa, byte_at(m_, in + 0xc));
        break;
    case 1:
        offset_a = short_at(m_, in + 0x12);
        offset_b = short_at(m_, in + 0x10);
        set_byte_at(m_, piece + 8, byte_at(m_, in + 0xc));
        set_byte_at(m_, piece + 0xa, byte_at(m_, in + 0xa));
        break;
    case 2:
        offset_a = short_at(m_, in + 0x10);
        offset_b = short_at(m_, in + 0x14);
        set_byte_at(m_, piece + 8, byte_at(m_, in + 0xa));
        set_byte_at(m_, piece + 0xa, byte_at(m_, in + 0xc));
        break;
    case 3:
        offset_a = short_at(m_, in + 0x14);
        offset_b = short_at(m_, in + 0xe);
        set_byte_at(m_, piece + 8, byte_at(m_, in + 0xc));
        set_byte_at(m_, piece + 0xa, byte_at(m_, in + 0xa));
        break;
    default:
        break;
    }
}

// The pieces of the park (0x0043ab00, from the editor's cells, into the first list and OutModelMap) and of the floor (0x0043b410, from FloorTab, into the second list
// and FloorModelMap): one out-model for every layer of every cell that holds a piece, placed on the grid by its cell, layer and turn; the cells it covers point to it.
void LevelGen::build_pieces(std::uint32_t self, bool park) {
    const std::uint32_t list_head = park ? 0x2344 : 0x234c;
    const std::uint32_t map = park ? 0x2348 : 0x2350;
    if (park) {
        release_pieces(self);
    } else {
        free_floor_pieces(self);
        for (std::int32_t z = 0; z < global_int(m_, kGridD); ++z) {
            std::uint32_t offset = 0;
            for (std::int32_t x = 0; x < global_int(m_, kGridW); ++x) {
                for (std::uint32_t k = 0; k < 5; ++k) {
                    set_word_at(m_, word_at(m_, word_at(m_, self + map) + 4 * static_cast<std::uint32_t>(z)) + offset, 0);
                    offset += 4;
                }
            }
        }
    }
    const std::int32_t cell_x = global_int(m_, kCellX);
    const std::int32_t cell_z = global_int(m_, kCellZ);
    std::int32_t z_offset = (cell_x * global_int(m_, kGridD)) >> 1;
    std::uint32_t previous = 0;
    std::int32_t offset_a = 0;       // (stale across pieces, as in retail)
    std::int32_t offset_b = 0;
    for (std::int32_t z = 0; z < global_int(m_, kGridD); ++z) {
        std::int32_t x_offset = -(global_int(m_, kGridW) * cell_x) >> 1;
        for (std::int32_t x = 0; x < global_int(m_, kGridW); ++x) {
            std::int32_t y_offset = 0;
            for (std::int32_t k = 0; k < 5; ++k) {
                const std::uint32_t rows = park ? global_word(m_, kRows) : level_gen_pieces_build_at(m_, self).field_0();
                const std::uint32_t cell = word_at(m_, rows + 4 * static_cast<std::uint32_t>(z)) + 0x10 * static_cast<std::uint32_t>(x);
                const bool present = half_at(m_, cell + 2 * static_cast<std::uint32_t>(k)) != 0xffff && (!park || k < static_cast<std::int32_t>(byte_at(m_, cell + 0xf)));
                if (present) {
                    const std::uint32_t in = self + kModels + static_cast<std::uint32_t>(short_at(m_, cell + 2 * static_cast<std::uint32_t>(k))) * kModelBytes;
                    const std::uint32_t block = g_.new_zeroed(0x238);
                    const std::uint32_t piece = block != 0 ? g_.out_construct(block, in) : 0;
                    const std::uint32_t rotation = byte_at(m_, cell + 0xa + static_cast<std::uint32_t>(k));
                    turn_piece(piece, in, rotation, offset_a, offset_b);
                    set_byte_at(m_, piece + 9, byte_at(m_, in + 0xb));
                    set_half_at(m_, piece + 0x21c, static_cast<std::uint32_t>(offset_a + x_offset));
                    set_half_at(m_, piece + 0x21e, static_cast<std::uint32_t>(y_offset));
                    set_half_at(m_, piece + 0x220, static_cast<std::uint32_t>(z_offset - offset_b));
                    set_byte_at(m_, piece + 5, static_cast<std::uint32_t>(x));
                    set_byte_at(m_, piece + 6, static_cast<std::uint32_t>(k));
                    set_byte_at(m_, piece + 7, static_cast<std::uint32_t>(z));
                    set_byte_at(m_, piece + 0xc, rotation);
                    for (std::int32_t zz = z; zz < sbyte_at(m_, piece + 0xa) + z; ++zz) {
                        std::uint32_t slot = (static_cast<std::uint32_t>(k) + 5 * static_cast<std::uint32_t>(x)) * 4;
                        for (std::int32_t xx = x; xx < sbyte_at(m_, piece + 8) + x; ++xx) {
                            // (the park's pieces take every layer they span; a floor piece only the one it stands on)
                            for (std::int32_t layer = 0; layer < (park ? sbyte_at(m_, piece + 9) : 1); ++layer) {
                                s_.check(zz >= 0 && zz < global_int(m_, kGridD) && xx >= 0 && xx < global_int(m_, kGridW), kMsgOutOfBounds);
                                const std::uint32_t row = word_at(m_, word_at(m_, self + map) + 4 * static_cast<std::uint32_t>(zz));
                                s_.check(word_at(m_, row + slot + 4 * static_cast<std::uint32_t>(layer)) == 0, park ? kMsgOverlap3 : kMsgOverlap);
                                set_word_at(m_, row + slot + 4 * static_cast<std::uint32_t>(layer), piece);
                            }
                            slot += 0x14;
                        }
                    }
                    if (park) {
                        for (std::uint32_t j = 0; j < 2; ++j) {
                            if (byte_at(m_, in + 8) == word_at(m_, kStartTypes + 4 * j)) {
                                const std::uint32_t record = self + 0x369c + 0x14 * j;
                                set_byte_at(m_, record, 1);
                                set_word_at(m_, record + 4, static_cast<std::uint32_t>(x));
                                set_word_at(m_, record + 8, static_cast<std::uint32_t>(z));
                                set_word_at(m_, record + 0xc, static_cast<std::uint32_t>(k));
                                set_word_at(m_, record + 0x10, (byte_at(m_, piece + 0xc) - 1u) & 3);
                            }
                        }
                        if (byte_at(m_, in + 8) == global_word(m_, kHorseType)) {
                            s_.check(static_cast<std::int32_t>(level_gen_pieces_build_at(m_, self).field_3698()) < 6, kMsgHorseFull);
                            const std::uint32_t record = self + 0x36c4 + 0x14 * level_gen_pieces_build_at(m_, self).field_3698();
                            set_byte_at(m_, record, 1);
                            set_word_at(m_, record + 4, static_cast<std::uint32_t>(x));
                            set_word_at(m_, record + 8, static_cast<std::uint32_t>(z));
                            set_word_at(m_, record + 0xc, static_cast<std::uint32_t>(k));
                            set_word_at(m_, record + 0x10, (byte_at(m_, piece + 0xc) - 1u) & 3);
                            level_gen_pieces_build_at(m_, self).set_field_3698(static_cast<Word>(level_gen_pieces_build_at(m_, self).field_3698() + 1));
                        }
                    }
                    if (previous == 0) {
                        set_word_at(m_, self + list_head, piece);
                    } else {
                        set_word_at(m_, previous + 0x230, piece);
                    }
                    previous = piece;
                }
                y_offset -= cell_z;
            }
            x_offset += cell_x;
        }
        z_offset -= cell_x;
    }
}

void LevelGen::build_piece_objects(std::uint32_t self) { build_pieces(self, true); }                 // 0x0043ab00
void LevelGen::build_floor_objects(std::uint32_t self) { build_pieces(self, false); }                // 0x0043b410

// 0x0043b080: pieces marked as removed go (nodes first); the others that were hidden are shown again.
void LevelGen::prune_pieces(std::uint32_t self) {
    std::uint32_t piece = word_at(m_, self + 0x2344);
    std::uint32_t previous = 0;
    while (piece != 0) {
        const std::uint32_t next = word_at(m_, piece + 0x230);
        if (byte_at(m_, piece) != 0) {
            g_.out_release_nodes(piece);
            g_.delete_block(piece);
            if (previous != 0) {
                set_word_at(m_, previous + 0x230, next);
            } else {
                set_word_at(m_, self + 0x2344, next);
            }
        } else {
            if (byte_at(m_, piece + 2) != 0) {
                g_.out_show_nodes(piece);
            }
            set_byte_at(m_, piece + 2, 0);
            previous = piece;
        }
        piece = next;
    }
}

// 0x0043b0f0: the neighbours of each piece: the pieces on its far sides (one row beyond its depth, one column beyond its width) that reach above its base, collected
// without repeats. The first pass (the row beyond) also sets how many neighbours come before the piece.
void LevelGen::compute_neighbours(std::uint32_t self) {
    for (std::uint32_t piece = word_at(m_, self + 0x2344); piece != 0; piece = word_at(m_, piece + 0x230)) {
        set_byte_at(m_, piece + 3, 0);
        set_byte_at(m_, piece + 4, 0);
        const std::int32_t z_end = sbyte_at(m_, piece + 0xa) + static_cast<std::int32_t>(byte_at(m_, piece + 7));
        const std::int32_t x0 = byte_at(m_, piece + 5);
        const std::int32_t x_end = sbyte_at(m_, piece + 8) + x0;
        const auto consider = [&](std::int32_t z, std::int32_t x, std::uint32_t k, bool first_pass) {
            s_.check(byte_at(m_, piece + 3) < 0x80, kMsgNeighbours);
            const std::uint32_t row = word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(z));
            const std::uint32_t other = word_at(m_, row + (5 * static_cast<std::uint32_t>(x) + k) * 4);
            if (other == 0) {
                return;
            }
            if (sbyte_at(m_, other + 0xb) + static_cast<std::int32_t>(byte_at(m_, other + 6)) <= static_cast<std::int32_t>(byte_at(m_, piece + 6))) {
                return;
            }
            const std::uint32_t count = byte_at(m_, piece + 3);
            std::uint32_t found = 0;
            while (found < count && word_at(m_, piece + 0x18 + 4 * found) != other) {
                ++found;
            }
            if (found != count) {
                return;
            }
            set_word_at(m_, piece + 0x18 + 4 * count, other);
            set_byte_at(m_, piece + 3, byte_at(m_, piece + 3) + 1);
            if (first_pass) {
                set_byte_at(m_, piece + 4, byte_at(m_, piece + 3));
            }
        };
        if (z_end < global_int(m_, kGridD) && x0 < x_end) {
            for (std::int32_t x = x0; x < x_end && x < global_int(m_, kGridW); ++x) {
                for (std::uint32_t k = 0; k < 5; ++k) {
                    consider(z_end, x, k, true);
                }
            }
        }
        const std::int32_t z0 = byte_at(m_, piece + 7);
        if (x_end < global_int(m_, kGridW) && z0 < z_end) {
            for (std::int32_t z = z0; z < z_end && z < global_int(m_, kGridD); ++z) {
                for (std::uint32_t k = 0; k < 5; ++k) {
                    consider(z, x_end, k, false);
                }
            }
        }
        s_.check(byte_at(m_, piece + 3) <= 0x80, kMsgNeighbours);
    }
}

// 0x0043b850: the editor's pieces start with the cell flags cleared on the floor under them.
void LevelGen::flag_floor_pieces(std::uint32_t self) {
    if (byte_at(m_, self + 0x3744) == 0) {
        for (std::uint32_t piece = word_at(m_, self + 0x2344); piece != 0; piece = word_at(m_, piece + 0x230)) {
            set_cell_flags(self, piece, 0);
        }
    }
}

// 0x0043f400: bit 5 and bit 1 of the built model's first word: set (or cleared) for the floor pieces under a piece's footprint, on its layer.
void LevelGen::set_cell_flags(std::uint32_t self, std::uint32_t piece, std::uint32_t clear) {
    const std::uint32_t layer = byte_at(m_, piece + 6);
    const std::int32_t z0 = byte_at(m_, piece + 7);
    const std::int32_t x0 = byte_at(m_, piece + 5);
    const std::int32_t z_end = sbyte_at(m_, piece + 0xa) + z0;
    const std::int32_t x_end = sbyte_at(m_, piece + 8) + x0;
    const std::uint32_t in = word_at(m_, piece + 0x224);
    if ((byte_at(m_, in + 9) & 4) == 0 || layer == 0 || z0 >= z_end) {
        return;
    }
    for (std::int32_t z = z0; z < z_end; ++z) {
        if (x0 >= x_end) {
            continue;
        }
        std::uint32_t offset = (layer + 5 * static_cast<std::uint32_t>(x0)) * 4;
        for (std::int32_t x = x0; x < x_end; ++x) {
            s_.check(z >= 0 && z < global_int(m_, kGridD), kMsgFloorModelZ);
            const std::uint32_t other = word_at(m_, word_at(m_, word_at(m_, self + 0x2350) + 4 * static_cast<std::uint32_t>(z)) + offset);
            if (other != 0) {
                const std::uint32_t built = word_at(m_, other + 0x228);
                if ((clear & 0xff) == 0) {
                    set_byte_at(m_, built, byte_at(m_, built) | 0x20);
                    set_byte_at(m_, word_at(m_, other + 0x228), byte_at(m_, word_at(m_, other + 0x228)) | 2);
                } else {
                    set_half_at(m_, built, half_at(m_, built) & 0xffdf);
                    set_half_at(m_, word_at(m_, other + 0x228), half_at(m_, word_at(m_, other + 0x228)) & 0xfffd);
                }
            }
            offset += 0x14;
        }
    }
}

// 0x0043f540: the objects the region made for the floor pieces get bit 5 of their first word set (or cleared).
void LevelGen::set_floor_flags(std::uint32_t self, std::uint32_t on) {
    const std::uint32_t slot = global_word(m_, kSlot);
    const std::uint32_t region = word_at(m_, kObjectTables + slot * 0x44 + 8);
    const std::uint32_t table = region + word_at(m_, self + 0x2354) * 0x24 + 0x10;
    const std::uint32_t first = word_at(m_, self + 0x2358);
    const std::uint32_t last = first + word_at(m_, self + 0x2384);
    for (std::uint32_t i = first; static_cast<std::int32_t>(i) < static_cast<std::int32_t>(last); ++i) {
        const std::uint32_t object = word_at(m_, table + 4 * i);
        if ((on & 0xff) != 0) {
            set_byte_at(m_, object, byte_at(m_, object) | 0x20);
        } else {
            set_half_at(m_, object, half_at(m_, object) & 0xffdf);
        }
    }
}

// 0x0043f5a0: the gap members of one piece show; the others do not.
void LevelGen::mark_gaps(std::uint32_t self, std::uint32_t piece) {
    for (std::uint32_t gap = word_at(m_, self + 0x2388); gap != 0; gap = word_at(m_, gap + 0xc)) {
        const bool mine = piece != 0 && (word_at(m_, gap + 4) == piece || word_at(m_, word_at(m_, gap + 8) + 4) == piece);
        g_.gap_set_visible(gap, mine ? 1 : 0);
    }
}

// 0x0043f1b0: the faces of the neighbours that look at a piece (four sides, five layers) are marked for it.
void LevelGen::mark_edges(std::uint32_t self, std::uint32_t piece, std::uint32_t mode) {
    s_.check(piece != 0, kMsgNoModel);
    const std::uint32_t in = word_at(m_, piece + 0x224);
    if ((byte_at(m_, in + 9) & 1) != 0) {
        return;
    }
    const std::int32_t x0 = byte_at(m_, piece + 5);
    const std::int32_t z0 = byte_at(m_, piece + 7);
    const std::int32_t x1 = sbyte_at(m_, piece + 8) + x0;
    const std::int32_t z1 = sbyte_at(m_, piece + 0xa) + z0;
    const std::int32_t base = byte_at(m_, piece + 6);
    const auto look = [&](std::int32_t z, std::int32_t x, std::int32_t layer, std::uint32_t direction) {
        const std::uint32_t other = word_at(m_, word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(z)) + static_cast<std::uint32_t>(layer + 5 * x) * 4);
        if (other != 0 && base < sbyte_at(m_, other + 0xb) + static_cast<std::int32_t>(byte_at(m_, other + 6))) {
            g_.out_mark_faces(other, mode, piece, direction);
        }
    };
    for (std::int32_t layer = 0; layer < 5; ++layer) {
        if (x0 - 1 >= 0 && z0 < z1) {
            for (std::int32_t z = z0; z < z1; ++z) {
                look(z, x0 - 1, layer, 0);
            }
        }
        if (x1 < global_int(m_, kGridW) && z0 < z1) {
            for (std::int32_t z = z0; z < z1; ++z) {
                look(z, x1, layer, 2);
            }
        }
        if (z0 - 1 >= 0 && x0 < x1) {
            for (std::int32_t x = x0; x < x1; ++x) {
                look(z0 - 1, x, layer, 1);
            }
        }
        if (z1 < global_int(m_, kGridD) && x0 < x1) {
            for (std::int32_t x = x0; x < x1; ++x) {
                look(z1, x, layer, 3);
            }
        }
    }
}

// ---- gap members and start positions ------------------------------------------------------------------------------------

// 0x0043b8d0: a pair of gap members for every item in use: each end names a cell and layer of OutModelMap, whose piece the member is attached to.
void LevelGen::build_gap_objects(std::uint32_t self) {
    free_gaps(self);
    level_gen_pieces_build_at(m_, self).set_field_2390(0);
    std::uint32_t previous = 0;
    for (std::uint32_t item_index = 0, offset = 0; offset < kItemRecords * kItemBytes; offset += kItemBytes, ++item_index) {
        const std::uint32_t marker = byte_at(m_, global_word(m_, kItems) + offset + 0xc);
        if (marker == 0x80) {
            continue;
        }
        if (level_gen_pieces_build_at(m_, self).field_3744() != 0 && marker == 0x81) {
            continue;
        }
        std::uint32_t member[2] = {0, 0};
        for (std::uint32_t end = 0; end < 2; ++end) {
            const std::uint32_t item = global_word(m_, kItems) + offset;
            const std::int32_t layer = byte_at(m_, item + end + 2);
            const std::int32_t x = byte_at(m_, item + end);
            const std::int32_t z = byte_at(m_, item + end + 4);
            s_.check(x >= 0 && x < global_int(m_, kGridW), kMsgGapX);
            s_.check(z >= 0 && z < global_int(m_, kGridD), kMsgGapZ);
            const std::uint32_t piece =
                word_at(m_, word_at(m_, level_gen_pieces_build_at(m_, self).field_2348() + 4 * static_cast<std::uint32_t>(z)) + (static_cast<std::uint32_t>(layer) + 5 * static_cast<std::uint32_t>(x)) * 4);
            s_.check(piece != 0, kMsgNoGapModel);
            if (piece == 0) {
                return;
            }
            const std::uint32_t block = g_.new_zeroed(0x58);
            const std::uint32_t gap = block != 0 ? g_.gap_init(block) : 0;
            set_word_at(m_, gap + 4, piece);
            set_word_at(m_, piece + 0x234, gap);
            member[end] = gap;
            g_.gap_compute_edges(gap, byte_at(m_, item + end + 6), byte_at(m_, item + end + 8), byte_at(m_, item + end + 0xa));
            set_byte_at(m_, gap, end == 0 ? 1 : 0);
            set_word_at(m_, gap + 0x1c, item_index);
            const bool active = byte_at(m_, global_word(m_, kItems) + offset + 0xc) != 0x81;
            g_.gap_set_active(gap, active ? 1 : 0);
            if (!active) {
                level_gen_pieces_build_at(m_, self).set_field_2390(static_cast<Word>(gap));
            }
        }
        set_word_at(m_, member[0] + 8, member[1]);
        set_word_at(m_, member[1] + 8, member[0]);
        if (previous != 0) {
            set_word_at(m_, previous + 0xc, member[0]);
        } else {
            level_gen_pieces_build_at(m_, self).set_field_2388(static_cast<Word>(member[0]));
        }
        set_word_at(m_, member[0] + 0xc, member[1]);
        previous = member[1];
    }
}

// 0x0043bbf0: the start positions (a piece of the start type becomes an x, z pair on the grid, in cell units).
void LevelGen::build_starts(std::uint32_t self) {
    free_starts(self);
    std::uint32_t previous = 0;
    for (std::uint32_t piece = level_gen_pieces_build_at(m_, self).field_2344(); piece != 0; piece = word_at(m_, piece + 0x230)) {
        if (byte_at(m_, word_at(m_, piece + 0x224) + 8) != global_word(m_, kStartMarker)) {
            continue;
        }
        const std::uint32_t block = g_.new_zeroed(0x1c);
        const std::uint32_t start = block != 0 ? g_.gap_return_this(block) : 0;
        set_word_at(m_, start, static_cast<std::uint32_t>((static_cast<std::int32_t>(byte_at(m_, piece + 5)) - (global_int(m_, kGridW) >> 1)) * global_int(m_, kCellX)));
        set_word_at(m_, start + 4, static_cast<std::uint32_t>(((global_int(m_, kGridD) >> 1) - static_cast<std::int32_t>(byte_at(m_, piece + 7))) * global_int(m_, kCellX)));
        if (previous != 0) {
            set_word_at(m_, previous + 8, start);
        } else {
            level_gen_pieces_build_at(m_, self).set_field_23a4(static_cast<Word>(start));
        }
        previous = start;
    }
}

}  // namespace opentony::runtime::levelgen
