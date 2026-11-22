#include "gen_views.hpp"

#include "player_spooler.hpp"

namespace opentony::runtime::genobjs {

using namespace views;

namespace {
constexpr const char* kAlloc = "alloc";                 // the loader's allocator: (size, heap, 1, 0)
constexpr const char* kMemDelete = "mem_delete";        // Mem_Delete(block)
constexpr const char* kLog = "printf_log";              // printf_log(format, ...): only the format is reported
constexpr std::uint32_t kMsgNoTable = 0x0052afdc;       // "ring table"
constexpr std::uint32_t kMsgRingFull = 0x0052afcc;
constexpr std::uint32_t kMsgFaceTiling = 0x0052af78;
constexpr std::uint32_t kLightFormat = 0x0052af9c;
}  // namespace

// ---- helpers ---------------------------------------------------------------------------------------------------------------

std::uint32_t GenObjs::alloc(std::uint32_t size, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    s_.note(kAlloc, {size, b, c, d});
    return static_cast<std::uint32_t>(s_.value(kAlloc));
}

void GenObjs::free_block(std::uint32_t block) { s_.note(kMemDelete, {block}); }

void GenObjs::log(std::uint32_t format) { s_.note(kLog, {format}); }

// 0x0046f2e0: Mem_Copy(destination, source, size): both addresses must be word aligned (a bad one only logs), then the words and the leftover bytes are copied.
void GenObjs::mem_copy(std::uint32_t destination, std::uint32_t source, std::uint32_t size) {
    s_.check((destination & 3) == 0, 0x00534bd8);
    s_.check((source & 3) == 0, 0x00534bd8);
    const std::int32_t count = static_cast<std::int32_t>(size);
    if (count > 0) {
        m_.copy(destination, source, static_cast<std::size_t>(count));
    }
}

// 0x004436a0: a block of `size` bytes from the allocator, zero filled (in whole words).
std::uint32_t GenObjs::new_zeroed(std::uint32_t size) {
    const std::uint32_t block = alloc(size, 0, 1, 0);
    const std::uint32_t words = ((size + 3) & 0xfffffffc) >> 2;
    if (words != 0) {
        m_.fill(block, 0, static_cast<std::size_t>(words) * 4);
    }
    return block;
}

// 0x004436d0
void GenObjs::delete_block(std::uint32_t block) { free_block(block); }

// ---- the model-info pool ---------------------------------------------------------------------------------------------------

// 0x0043f730: forget every block pointer (a block still held is a leak unless `allow`).
void GenObjs::model_info_clear(std::uint32_t allow) {
    for (std::uint32_t i = 0; i < kPoolSlots; ++i) {
        const std::uint32_t slot = kPoolTable + 4 * i;
        s_.check((allow & 0xff) != 0 || word_at(m_, slot) == 0, kMsgPoolNotEmpty);
        set_word_at(m_, slot, 0);
    }
    set_global_word(m_, kPoolIndex, 0);
}

// 0x0043f7b0: free the blocks 0..index (the block being filled included) and start again.
void GenObjs::model_info_free_all() {
    const std::int32_t index = global_int(m_, kPoolIndex);
    for (std::int32_t i = 0; i < index; ++i) {
        free_block(word_at(m_, kPoolTable + 4 * static_cast<std::uint32_t>(i)));
        set_word_at(m_, kPoolTable + 4 * static_cast<std::uint32_t>(i), 0);
    }
    const std::uint32_t current = word_at(m_, kPoolTable + 4 * static_cast<std::uint32_t>(index));
    if (current != 0) {
        free_block(current);
        set_word_at(m_, kPoolTable + 4 * global_word(m_, kPoolIndex), 0);
    }
    set_global_word(m_, kPoolIndex, 0);
}

// 0x0043f820: the next free element (0x2c bytes) of the pool; a full block moves on to the next one.
std::uint32_t GenObjs::model_info_alloc() {
    s_.check(global_int(m_, kPoolIndex) < static_cast<std::int32_t>(kPoolSlots), kMsgPoolFull);
    if (word_at(m_, kPoolTable + 4 * global_word(m_, kPoolIndex)) == 0) {
        const std::uint32_t block = alloc(kPoolBlockBytes, 0, 1, 0);
        set_word_at(m_, kPoolTable + 4 * global_word(m_, kPoolIndex), block);
        set_word_at(m_, block, 0);
    }
    const std::uint32_t block = word_at(m_, kPoolTable + 4 * global_word(m_, kPoolIndex));
    const std::uint32_t used = word_at(m_, block);
    const std::uint32_t element = block + 4 + used * kPoolElement;
    set_word_at(m_, block, used + 1);
    if (int_at(m_, word_at(m_, kPoolTable + 4 * global_word(m_, kPoolIndex))) >= 100) {
        set_global_word(m_, kPoolIndex, global_word(m_, kPoolIndex) + 1);
    }
    return element;
}

// ---- the in-model ----------------------------------------------------------------------------------------------------------

// 0x0043f900 (0x0043f8f0 jumps here): free the face list's holder.
void GenObjs::in_destruct(std::uint32_t self) {
    const std::uint32_t holder = gen_in_at(m_, self).field_20();
    if (holder != 0) {
        plist_free_all(holder);
        delete_block(holder);
    }
    gen_in_at(m_, self).set_field_20(0);
}

// 0x0043f930: take a model: its size in cells, then one flat element per wall face (a loop of 3 or 4 points in x, y, z halves), linked in a chain from +0.
void GenObjs::in_set_data(std::uint32_t self, std::uint32_t model, std::uint32_t type) {
    gen_in_at(m_, self).set_field_4(static_cast<Word>(model));
    gen_in_at(m_, self).set_field_8(static_cast<std::uint8_t>(type));
    const std::int32_t cell_x = global_int(m_, kCellX);
    const std::int32_t cell_z = global_int(m_, kCellZ);
    const std::int32_t xa = short_at(m_, model + 0xc);
    const std::int32_t xb = short_at(m_, model + 0xe);
    const std::int32_t width = xa - xb;
    std::uint8_t cells = static_cast<std::uint8_t>(width / cell_x);
    gen_in_at(m_, self).set_field_a(static_cast<std::uint8_t>(cells));
    if (width % cell_x > cell_x - 4) {
        gen_in_at(m_, self).set_field_a(static_cast<std::uint8_t>(static_cast<std::uint8_t>(cells + 1)));
    }
    const std::int32_t za = short_at(m_, model + 0x10);
    const std::int32_t zb = short_at(m_, model + 0x12);
    const std::int32_t depth = za - zb;
    cells = static_cast<std::uint8_t>(depth / cell_z);
    gen_in_at(m_, self).set_field_b(static_cast<std::uint8_t>(cells));
    if (depth % cell_z > cell_z - 4) {
        gen_in_at(m_, self).set_field_b(static_cast<std::uint8_t>(static_cast<std::uint8_t>(cells + 1)));
    }
    if (static_cast<std::int8_t>(gen_in_at(m_, self).field_b()) < 1) {
        gen_in_at(m_, self).set_field_b(1);
    }
    gen_in_at(m_, self).set_field_d(static_cast<std::uint8_t>(static_cast<std::uint8_t>((depth + cell_z - 4) / cell_z)));
    if (static_cast<std::int8_t>(gen_in_at(m_, self).field_d()) < 0) {
        gen_in_at(m_, self).set_field_d(0);
    }
    const std::int32_t ya = short_at(m_, model + 0x14);
    const std::int32_t yb = short_at(m_, model + 0x16);
    const std::int32_t height = ya - yb;
    cells = static_cast<std::uint8_t>(height / cell_x);
    gen_in_at(m_, self).set_field_c(static_cast<std::uint8_t>(cells));
    if (height % cell_x > cell_x - 4) {
        gen_in_at(m_, self).set_field_c(static_cast<std::uint8_t>(static_cast<std::uint8_t>(cells + 1)));
    }
    gen_in_at(m_, self).set_field_12(static_cast<std::uint16_t>(half_at(m_, model + 0x14)));
    gen_in_at(m_, self).set_field_10(static_cast<std::uint16_t>(half_at(m_, model + 0xc)));
    gen_in_at(m_, self).set_field_e(static_cast<std::uint16_t>(static_cast<std::uint32_t>(-xb)));
    gen_in_at(m_, self).set_field_14(static_cast<std::uint16_t>(static_cast<std::uint32_t>(-static_cast<std::int32_t>(short_at(m_, model + 0x16)))));
    gen_in_at(m_, self).set_field_16(static_cast<std::uint16_t>(half_at(m_, model + 0x10)));
    const std::uint32_t kind = piece_kind(type & 0xff);
    gen_in_at(m_, self).set_field_9(static_cast<std::uint8_t>(kind));
    if ((kind & 0x10) == 0) {
        gen_in_at(m_, self).set_field_b(1);
    }

    gen_in_at(m_, self).set_field_0(0);
    const std::uint32_t count = half_at(m_, model + 6);
    std::uint32_t face = model + 0x1c + (static_cast<std::uint32_t>(half_at(m_, model + 4)) + half_at(m_, model + 2)) * 8;
    std::uint32_t previous = 0;
    const bool open = (kind & 1) == 0;
    for (std::uint32_t k = 0; k < count && open; ++k) {
        const std::uint32_t flags = half_at(m_, face);
        s_.check((flags & 1) != 0 || (flags & 0x20) == 0, kMsgFaceTiling);
        const std::uint32_t stride = half_at(m_, face + 2);
        const std::uint32_t next_face = face + stride;
        const std::uint32_t vertices = gen_in_at(m_, self).field_4() + 0x1c;
        std::int16_t px[5] = {}, py[5] = {}, pz[5] = {};
        for (std::uint32_t v = 0; v < 4; ++v) {
            const std::int32_t index = sbyte_at(m_, face + 4 + v);
            px[v] = short_at(m_, vertices + index * 8);
            py[v] = short_at(m_, vertices + index * 8 + 2);
            pz[v] = short_at(m_, vertices + index * 8 + 4);
        }
        std::uint8_t axes = 3;
        for (int v = 1; v < 4; ++v) {
            if (px[v] - px[0] < -1 || px[v] - px[0] > 1) {
                axes &= 1;
            }
            if (pz[v] - pz[0] < -1 || pz[v] - pz[0] > 1) {
                axes &= 2;
            }
        }
        if (axes != 0) {
            std::uint32_t points = 4;
            const int mode = ((axes & 2) != 0 ? 1 : 0) + 1;
            if (px[3] == px[2] && py[3] == py[2] && pz[3] == pz[2]) {
                points = 3;
            }
            std::uint32_t flags_out = 0;
            for (std::uint32_t v = 0; v < points; ++v) {
                if (mode == 1) {
                    flags_out = 1;
                    pz[v] = static_cast<std::int16_t>(-pz[v]);
                } else if (mode == 2) {
                    flags_out = 0;
                    const std::int16_t old_x = px[v];
                    px[v] = pz[v];
                    pz[v] = old_x;
                }
            }
            if (points == 4) {
                std::swap(px[2], px[3]);
                std::swap(py[2], py[3]);
                std::swap(pz[2], pz[3]);
            }
            px[points] = px[0];
            py[points] = py[0];
            pz[points] = pz[0];
            const std::int32_t area = (static_cast<std::int32_t>(px[1]) - px[0]) * (static_cast<std::int32_t>(py[2]) - py[1]) -
                                      (static_cast<std::int32_t>(py[1]) - py[0]) * (static_cast<std::int32_t>(px[2]) - px[1]);
            const std::uint32_t orientation = area >= 0 ? 2 : 0;
            flags_out |= orientation;
            for (std::uint32_t v = 0; v < points; ++v) {
                if (orientation == 0) {
                    px[v] = static_cast<std::int16_t>(-px[v]);
                    pz[v] = static_cast<std::int16_t>(-pz[v]);
                }
            }
            const std::uint32_t element = model_info_alloc();
            set_byte_at(m_, element, flags_out);
            set_byte_at(m_, element + 2, points);
            set_word_at(m_, element + 8, face);
            set_word_at(m_, element + 4, 0);
            for (std::uint32_t v = 0; v < 5; ++v) {
                set_half_at(m_, element + 0xc + 6 * v, static_cast<std::uint16_t>(px[v]));
                set_half_at(m_, element + 0xe + 6 * v, static_cast<std::uint16_t>(py[v]));
                set_half_at(m_, element + 0x10 + 6 * v, static_cast<std::uint16_t>(pz[v]));
            }
            if (points == 3) {
                unspecified_.emplace_back(element + 0x24, 6);      // (the fifth point is whatever the stack held)
            }
            if (previous == 0) {
                gen_in_at(m_, self).set_field_0(static_cast<Word>(element));
            } else {
                set_word_at(m_, previous + 4, element);
            }
            previous = element;
        }
        face = next_face;
    }
    const std::uint32_t holder = new_zeroed(0xc);
    gen_in_at(m_, self).set_field_20(static_cast<Word>(holder != 0 ? plist_init(holder) : 0));
}

// 0x0043fda0: faces with a transparency mode (bits 6 and 7) are set to the one asked for.
void GenObjs::in_set_transparency(std::uint32_t self, std::uint32_t flag) {
    const std::uint32_t model = gen_in_at(m_, self).field_4();
    std::uint32_t face = model + 0x1c + (static_cast<std::uint32_t>(half_at(m_, model + 4)) + half_at(m_, model + 2)) * 8;
    std::uint32_t count = half_at(m_, model + 6);
    for (; count != 0; --count) {
        std::uint32_t flags = half_at(m_, face);
        const std::uint32_t stride = half_at(m_, face + 2);
        if ((flags & 0xc0) != 0) {
            if ((flag & 0xff) != 0) {
                flags = (flags & 0xff7f) | 0x40;
            } else {
                flags = (flags & 0xffbf) | 0x80;
            }
            set_half_at(m_, face, flags);
        }
        face += stride;
    }
}

// ---- the out-model's statics -----------------------------------------------------------------------------------------------

// 0x0043fe00: take the park theme's lights (a 32-byte row per theme and size), place them on the grid, build the lookup tables and the ring of nodes.
void GenObjs::out_init() {
    const std::uint32_t theme = global_word(m_, kTheme);
    const std::uint32_t rows = global_word(m_, kThemeRows);
    s_.note(kLog, {kLightFormat});
    const std::uint32_t row = kLightRows + (theme * 5 + rows) * 32;
    const std::uint32_t lights = byte_at(m_, row);
    set_global_word(m_, kLightA, word_at(m_, row + 4));
    set_global_word(m_, kLightB, word_at(m_, row + 8));
    set_global_word(m_, kLightCount, lights);
    set_global_word(m_, kLightC, word_at(m_, row + 0xc));
    if (static_cast<std::int32_t>(lights) > 0) {
        const std::int32_t scale = global_int(m_, kCellX);
        const std::int32_t half_depth = global_int(m_, kGridDepth) >> 1;
        const std::int32_t half_width = global_int(m_, kGridWidth) >> 1;
        for (std::uint32_t i = 0; i < lights; ++i) {
            const std::int32_t x = (static_cast<std::int32_t>(byte_at(m_, row + 0x10 + 2 * i)) - half_width) * scale;
            const std::int32_t z = (half_depth - static_cast<std::int32_t>(byte_at(m_, row + 0x11 + 2 * i))) * scale;
            set_word_at(m_, kLightPositions + 12 * i, static_cast<std::uint32_t>(x));
            set_word_at(m_, kLightPositions + 8 + 12 * i, static_cast<std::uint32_t>(z));
            set_word_at(m_, kLightDirections + 12 * i, 0);
            set_word_at(m_, kLightDirections + 4 + 12 * i, 0x1000);
            set_word_at(m_, kLightPositions + 4 + 12 * i, 0xfffffc18);
            set_word_at(m_, kLightDirections + 8 + 12 * i, 0);
        }
    }
    out_build_tables();
    const std::uint32_t ring = alloc(kRingNodes * 16, 0, 1, 0);
    set_global_word(m_, kRingTable, ring);
    for (std::uint32_t offset = 0; offset < kRingNodes * 16; offset += 0x10) {
        set_word_at(m_, ring + offset, kRingFree);
    }
    set_global_word(m_, kRingCursor, 0);
    set_global_word(m_, kRingUsed, 0);
}

// 0x0043ff30: the 1024-byte falloff table (by squared distance) and the 256-byte shade table.
void GenObjs::out_build_tables() {
    if (const std::uint32_t old = global_word(m_, kFalloffTable); old != 0) {
        free_block(old);
    }
    const std::uint32_t falloff = alloc(0x400, 1, 1, 0);
    set_global_word(m_, kFalloffTable, falloff);
    const std::int32_t scale = global_int(m_, kLightC);
    for (std::int32_t i = 0; i < 0x400; ++i) {
        const std::int32_t step = 0x3ff - i;
        const std::int32_t product = static_cast<std::int32_t>(static_cast<std::uint32_t>(step) * static_cast<std::uint32_t>(scale));
        std::int32_t quotient = static_cast<std::int32_t>((static_cast<std::int64_t>(product) * 0x66666667) >> 32) >> 2;
        quotient += static_cast<std::int32_t>(static_cast<std::uint32_t>(quotient) >> 31);
        std::int32_t value = static_cast<std::int32_t>(static_cast<std::uint32_t>(0x3ff - quotient) << 8);
        value += (value >> 31) & 0x3ff;
        value >>= 10;
        if (value < 0) {
            value = 0;
        }
        set_byte_at(m_, falloff + static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(value));
    }
    if (const std::uint32_t old = global_word(m_, kShadeTable); old != 0) {
        free_block(old);
    }
    const std::uint32_t shade = alloc(0x100, 1, 1, 0);
    set_global_word(m_, kShadeTable, shade);
    const std::int32_t gain = global_int(m_, kLightA);
    for (std::int32_t i = 0; i < 0x100; ++i) {
        const std::int32_t value = static_cast<std::int32_t>(static_cast<std::uint32_t>(i - 0x80) * static_cast<std::uint32_t>(gain)) + 0x3200;
        std::int32_t quotient = static_cast<std::int32_t>((static_cast<std::int64_t>(value) * 0x51eb851f) >> 32) >> 5;
        quotient += static_cast<std::int32_t>(static_cast<std::uint32_t>(quotient) >> 31);
        set_byte_at(m_, shade + static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(quotient));
    }
}

// 0x00440010
void GenObjs::out_cleanup() {
    free_block(global_word(m_, kFalloffTable));
    const std::uint32_t shade = global_word(m_, kShadeTable);
    set_global_word(m_, kFalloffTable, 0);
    free_block(shade);
    const std::uint32_t ring = global_word(m_, kRingTable);
    set_global_word(m_, kShadeTable, 0);
    free_block(ring);
    set_global_word(m_, kRingTable, 0);
}

// 0x00440060: the next free node of the ring (the scan starts at the cursor and wraps; a full ring logs and hands out the node under the cursor anyway).
std::uint32_t GenObjs::node_alloc() {
    s_.check(global_word(m_, kRingTable) != 0, kMsgNoTable);
    const std::uint32_t table = global_word(m_, kRingTable);
    std::uint32_t cursor = global_word(m_, kRingCursor);
    std::uint32_t left = kRingNodes;
    while (word_at(m_, table + (cursor << 4)) != kRingFree) {
        ++cursor;
        set_global_word(m_, kRingCursor, cursor);
        if (cursor >= kRingNodes) {
            cursor = 0;
            set_global_word(m_, kRingCursor, 0);
        }
        if (--left == 0) {
            break;
        }
    }
    s_.check(left != 0, kMsgRingFull);
    set_global_word(m_, kRingUsed, global_word(m_, kRingUsed) + 1);
    const std::uint32_t at = global_word(m_, kRingCursor);
    const std::uint32_t node = (at << 4) + global_word(m_, kRingTable);
    set_global_word(m_, kRingCursor, at + 1);
    set_byte_at(m_, node + 4, 1);
    set_byte_at(m_, node + 5, 0xff);
    set_word_at(m_, node, kRingLive);
    set_word_at(m_, node + 0xc, 0);
    set_word_at(m_, node + 8, 0);
    return node;
}

// 0x00440170: how many nodes are left of the 0x898 the generator may use (a small overdraw reads as none).
std::int32_t GenObjs::node_spare() {
    const std::int32_t left = 0x898 - static_cast<std::int32_t>(global_word(m_, kRingUsed));
    if (left >= 0) {
        return left;
    }
    return left <= -0x28 ? left : 0;
}

}  // namespace opentony::runtime::genobjs
