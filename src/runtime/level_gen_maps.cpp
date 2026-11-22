#include "auto_views.hpp"
#include "level_gen.hpp"

namespace opentony::runtime::levelgen {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kModelSlots = 250;
constexpr std::uint32_t kModelBytes = 0x24;
constexpr std::uint32_t kModels = 0x18;
}  // namespace

// ---- helpers ---------------------------------------------------------------------------------------------------------------

// FileIO_AllocLoadedBuffer(size, heap, 1, 0)
std::uint32_t LevelGen::alloc(std::uint32_t size, std::uint32_t heap, std::uint32_t one, std::uint32_t last) {
    s_.note("alloc", {size, heap, one, last});
    return static_cast<std::uint32_t>(s_.value("alloc"));
}

void LevelGen::free_block(std::uint32_t block) { s_.note("mem_delete", {block}); }

void LevelGen::log(std::uint32_t format) { s_.note("printf_log", {format}); }

void LevelGen::engine(std::uint32_t address, std::vector<std::uint32_t> args, std::string text) {
    s_.note(pspool::service_name(address), std::move(args), std::move(text));
}

std::uint32_t LevelGen::engine_value(std::uint32_t address, std::vector<std::uint32_t> args) {
    s_.note(pspool::service_name(address), std::move(args));
    return static_cast<std::uint32_t>(s_.value(pspool::service_name(address)));
}

// a C string and its terminator
void LevelGen::put_string(std::uint32_t at, const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        set_byte_at(m_, at + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(text[i]));
    }
    set_byte_at(m_, at + static_cast<std::uint32_t>(text.size()), 0);
}

// ---- construction ----------------------------------------------------------------------------------------------------------

// 0x004397d0: the constructor (the in-game flag says the object is not the editor's).
std::uint32_t LevelGen::construct(std::uint32_t self, std::uint32_t in_game) {
    for (std::uint32_t i = 0; i < kModelSlots; ++i) {
        g_.in_construct(self + kModels + i * kModelBytes);
    }
    g_.plist_init(self + 0x23b8);
    set_byte_at(m_, self + 0x3744, in_game);
    set_word_at(m_, self + 0x14, 0xffffffff);
    set_word_at(m_, self + 0x2380, 0xffffffff);
    set_global_word(m_, kSlot, 0xffffffff);
    set_word_at(m_, self + 0x23ac, 0x320);
    set_word_at(m_, self, 0);
    set_word_at(m_, self + 0x2348, 0);
    set_word_at(m_, self + 0x2350, 0);
    alloc_maps(self);
    set_word_at(m_, self + 0x2344, 0);
    release_pieces(self);
    set_word_at(m_, self + 0x234c, 0);
    free_floor_pieces(self);
    set_word_at(m_, self + 0x2388, 0);
    free_gaps(self);
    set_word_at(m_, self + 0x23a4, 0);
    free_starts(self);
    g_.model_info_clear(1);
    g_.out_init();
    set_word_at(m_, self + 0x23c8, 0);
    set_byte_at(m_, self + 0x369c, 0);
    set_byte_at(m_, self + 0x36b0, 0);
    for (std::uint32_t i = 0; i < 6; ++i) {
        set_byte_at(m_, self + 0x36c4 + 0x14 * i, 0);
    }
    set_word_at(m_, self + 0x3698, 0);
    set_word_at(m_, self + 0x2390, 0);
    set_byte_at(m_, self + 0x2394, 0);
    set_word_at(m_, self + 0x2398, 0);
    set_word_at(m_, self + 0x239c, 0x10);
    return self;
}

// 0x00439900: the destructor.
void LevelGen::destruct(std::uint32_t self) {
    unload_models(self);
    release_pieces(self);
    free_floor_pieces(self);
    free_gaps(self);
    free_starts(self);
    g_.model_info_free_all();
    g_.out_cleanup();
    free_maps(self);
    g_.plist_free_all_thunk(self + 0x23b8);
    for (std::uint32_t i = kModelSlots; i > 0; --i) {       // (the vector destructor runs the elements last to first)
        g_.in_destruct_thunk(self + kModels + (i - 1) * kModelBytes);
    }
}

// 0x004399a0: point the generator's globals at the editor's cell rows and item records, and (when asked) empty the park: every cell holds no piece and one layer, every
// item is free ("Gap n"), and the eight trailing records are reset.
void LevelGen::init_statics(std::uint32_t reset) {
    const std::uint32_t table = global_word(m_, kRowsSource);
    const std::uint32_t first = word_at(m_, table);
    const std::int32_t width = global_int(m_, kGridW);
    std::uint32_t cells = 0;
    for (std::int32_t z = 0; z < global_int(m_, kGridD); ++z) {
        set_word_at(m_, table + 4 * static_cast<std::uint32_t>(z), cells * 0x10 + first);
        cells += static_cast<std::uint32_t>(width);
    }
    set_global_word(m_, kRows, table);
    s_.check(table != 0, kMsgNoMap);
    const std::uint32_t items = global_word(m_, kItemsSource);
    set_global_word(m_, kItems, items);
    s_.check(items != 0, kMsgNoGaps);
    set_global_word(m_, kCellX, 0x154);
    set_global_word(m_, kCellZ, 0xaa);
    if ((reset & 0xff) != 0) {
        for (std::int32_t z = 0; z < global_int(m_, kGridD); ++z) {
            for (std::int32_t x = 0; x < global_int(m_, kGridW); ++x) {
                const std::uint32_t cell = word_at(m_, global_word(m_, kRows) + 4 * static_cast<std::uint32_t>(z)) + 0x10 * static_cast<std::uint32_t>(x);
                for (std::uint32_t k = 0; k < 5; ++k) {
                    set_half_at(m_, cell + 2 * k, 0xffff);
                    set_byte_at(m_, cell + 0xf, 1);
                    set_byte_at(m_, cell + 0xa + k, 0);
                }
            }
        }
        for (std::uint32_t i = 0; i < kItemRecords; ++i) {
            const std::uint32_t record = global_word(m_, kItems) + i * kItemBytes;
            set_byte_at(m_, record + 0xc, 0x80);
            put_string(record + 0xd, "Gap " + std::to_string(i + 1));
            set_word_at(m_, record + 0x28, 0);
        }
        for (std::uint32_t j = 0; j < 8; ++j) {
            const std::uint32_t at = kTrailing + 8 * j;
            set_half_at(m_, at, 0x4141);
            set_byte_at(m_, at + 2, 0x41);
            set_byte_at(m_, at + 3, 0);
            set_word_at(m_, at + 4, 0);
        }
    }
    set_global_word(m_, kSlot, 0xffffffff);
}

// 0x00439e10: give back the source models, the pieces and the regions they came from.
void LevelGen::unload_models(std::uint32_t self) {
    release_pieces(self);
    free_floor_pieces(self);
    free_gaps(self);
    free_starts(self);
    g_.model_info_free_all();
    for (std::uint32_t i = 0; i < word_at(m_, self + 0x2340); ++i) {
        g_.in_destruct(self + kModels + i * kModelBytes);
    }
    clear_source(self);
    if (int_at(m_, self + 0x2380) != -1) {
        engine(0x004b3270, {self + 0x2370});
    }
    set_word_at(m_, self + 0x2380, 0xffffffff);
    if (int_at(m_, self + 0x236c) != -1) {
        engine(0x004b3270, {self + 0x235c});
    }
    set_word_at(m_, self + 0x236c, 0xffffffff);
    if (byte_at(m_, self + 0x3744) == 0) {
        engine(0x004b2440);
    }
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
}

// 0x00439ed0
void LevelGen::clear_source(std::uint32_t self) {
    if (int_at(m_, self + 0x14) != -1) {
        engine(0x004b3270, {self + 4});
    }
    set_word_at(m_, self + 0x14, 0xffffffff);
}

// 0x00439ef0: FloorTab (rows of 16-byte cells) and the two maps of piece pointers (five per cell).
void LevelGen::alloc_maps(std::uint32_t self) {
    s_.check(word_at(m_, self) == 0, kMsgFloorTabAllocated);
    const std::int32_t width = global_int(m_, kGridW);
    const std::int32_t depth = global_int(m_, kGridD);
    std::uint32_t table = alloc(static_cast<std::uint32_t>(depth) * 4);
    set_word_at(m_, self, table);
    std::uint32_t block = alloc(static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(depth) * 0x10);
    for (std::int32_t z = 0; z < depth; ++z) {
        set_word_at(m_, word_at(m_, self) + 4 * static_cast<std::uint32_t>(z), block);
        block += static_cast<std::uint32_t>(width) * 0x10;
    }
    s_.check(word_at(m_, self + 0x2348) == 0, kMsgOutMapAllocated);
    table = alloc(static_cast<std::uint32_t>(depth) * 4);
    set_word_at(m_, self + 0x2348, table);
    block = alloc(static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(depth) * 0x14);
    for (std::int32_t z = 0; z < depth; ++z) {
        set_word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(z), block);
        block += static_cast<std::uint32_t>(width) * 0x14;
    }
    s_.check(word_at(m_, self + 0x2350) == 0, kMsgFloorMapAllocated);
    table = alloc(static_cast<std::uint32_t>(depth) * 4);
    set_word_at(m_, self + 0x2350, table);
    block = alloc(static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(depth) * 0x14);
    for (std::int32_t z = 0; z < depth; ++z) {
        set_word_at(m_, word_at(m_, self + 0x2350) + 4 * static_cast<std::uint32_t>(z), block);
        block += static_cast<std::uint32_t>(width) * 0x14;
    }
}

// 0x0043a100: the three maps go back (the first row pointer is the block).
void LevelGen::free_maps(std::uint32_t self) {
    s_.check(level_gen_maps_free_at(m_, self).field_0() != 0, kMsgFloorTabFree);
    free_block(word_at(m_, level_gen_maps_free_at(m_, self).field_0()));
    free_block(level_gen_maps_free_at(m_, self).field_0());
    level_gen_maps_free_at(m_, self).set_field_0(0);
    s_.check(level_gen_maps_free_at(m_, self).field_2348() != 0, kMsgOutMapFree);
    free_block(word_at(m_, level_gen_maps_free_at(m_, self).field_2348()));
    free_block(level_gen_maps_free_at(m_, self).field_2348());
    level_gen_maps_free_at(m_, self).set_field_2348(0);
    s_.check(level_gen_maps_free_at(m_, self).field_2350() != 0, kMsgFloorMapFree);
    free_block(word_at(m_, level_gen_maps_free_at(m_, self).field_2350()));
    free_block(level_gen_maps_free_at(m_, self).field_2350());
    level_gen_maps_free_at(m_, self).set_field_2350(0);
}

// 0x0043a240: a new park size: the maps are made again.
void LevelGen::resize(std::uint32_t self, std::uint32_t version) {
    free_maps(self);
    set_global_word(m_, kVersion, version);
    set_global_word(m_, kGridW, word_at(m_, kVersionTable + version * 8));
    set_global_word(m_, kGridD, word_at(m_, kVersionTable + 4 + version * 8));
    alloc_maps(self);
    init_statics(1);
}

// ---- the piece, floor, gap and start lists ---------------------------------------------------------------------------------

// 0x0043aa30: every piece goes (nodes first), the map is emptied, and the start and horse records are cleared.
void LevelGen::release_pieces(std::uint32_t self) {
    std::uint32_t piece = word_at(m_, self + 0x2344);
    while (piece != 0) {
        const std::uint32_t next = word_at(m_, piece + 0x230);
        g_.out_release_nodes(piece);
        g_.delete_block(piece);
        piece = next;
    }
    set_word_at(m_, self + 0x2344, 0);
    if (word_at(m_, self + 0x2348) != 0 && global_int(m_, kGridD) > 0) {
        for (std::int32_t z = 0; z < global_int(m_, kGridD); ++z) {
            if (global_int(m_, kGridW) <= 0) {
                break;
            }
            std::uint32_t offset = 0;
            for (std::int32_t x = 0; x < global_int(m_, kGridW); ++x) {
                for (std::uint32_t k = 0; k < 5; ++k) {
                    set_word_at(m_, word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(z)) + offset, 0);
                    offset += 4;
                }
            }
        }
    }
    for (std::uint32_t i = 0; i < 2; ++i) {
        set_byte_at(m_, self + 0x369c + 0x14 * i, 0);
    }
    for (std::uint32_t i = 0; i < 6; ++i) {
        set_byte_at(m_, self + 0x36c4 + 0x14 * i, 0);
    }
    set_word_at(m_, self + 0x3698, 0);
}

// 0x0043b3c0: the floor pieces go.
void LevelGen::free_floor_pieces(std::uint32_t self) {
    std::uint32_t piece = level_gen_maps_free_at(m_, self).field_234c();
    if (piece == 0) {
        level_gen_maps_free_at(m_, self).set_field_234c(0);
        return;
    }
    while (piece != 0) {
        const std::uint32_t next = word_at(m_, piece + 0x230);
        g_.out_release_nodes(piece);
        g_.delete_block(piece);
        piece = next;
    }
    level_gen_maps_free_at(m_, self).set_field_234c(0);
}

// 0x0043b880: the gap members go (each piece forgets its member).
void LevelGen::free_gaps(std::uint32_t self) {
    std::uint32_t gap = level_gen_maps_free_at(m_, self).field_2388();
    while (gap != 0) {
        set_word_at(m_, word_at(m_, gap + 4) + 0x234, 0);
        const std::uint32_t next = word_at(m_, gap + 0xc);
        g_.gap_nothing();
        g_.delete_block(gap);
        gap = next;
    }
    level_gen_maps_free_at(m_, self).set_field_2388(0);
}

// 0x0043bba0: the start positions go.
void LevelGen::free_starts(std::uint32_t self) {
    std::uint32_t start = level_gen_maps_free_at(m_, self).field_23a4();
    while (start != 0) {
        const std::uint32_t next = word_at(m_, start + 8);
        g_.gap_nothing_b();
        g_.delete_block(start);
        start = next;
    }
    level_gen_maps_free_at(m_, self).set_field_23a4(0);
}

}  // namespace opentony::runtime::levelgen
