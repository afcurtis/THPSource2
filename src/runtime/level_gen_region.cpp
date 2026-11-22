#include "auto_views.hpp"
#include "level_gen.hpp"

namespace opentony::runtime::levelgen {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kModels = 0x18;
constexpr std::uint32_t kModelBytes = 0x24;
constexpr std::uint32_t kObjectVtable = 0x00519388;      // the vtable PSX_InitObjectRecord gives a record
constexpr std::uint32_t kObjectDefault = 0x00533910;
constexpr std::uint32_t kRegionNames = 0x0056d428;       // the name of each region slot (0x44 apart)
constexpr std::uint32_t kRegionSlots = 20;
constexpr std::uint32_t kColourTables = 0x0056d450;
constexpr std::uint32_t kColourCurrent = 0x00563a58;
constexpr std::uint32_t kOrderTable = 0x0056d41c;
constexpr std::uint32_t kEnvironmentList = 0x0056db28;
constexpr std::uint32_t kEnvironmentSlot = 0x00537c80;
constexpr std::uint32_t kEnvironmentLink = 0x00537c84;
constexpr std::uint32_t kObjectTotal = 0x005611a4;
constexpr std::uint32_t kPieceTotal = 0x005614c0;
constexpr std::uint32_t kZoneCellBytes = 0x1ce;          // the zone grid is 20 x 20 cells: a count, then up to 0xe6 16-bit items
constexpr std::uint32_t kZoneRowBytes = 0x2418;
constexpr std::uint32_t kZoneRows = 0x50;
}  // namespace

// ---- the engine's objects ---------------------------------------------------------------------------------------------------

// 0x0047fdb0: a fresh region object record (0x4c bytes).
void LevelGen::init_record(std::uint32_t record) {
    set_word_at(m_, record + 8, 0);
    set_word_at(m_, record + 0xc, 0);
    set_word_at(m_, record + 0x10, 0);
    set_half_at(m_, record + 0x14, 0);
    set_half_at(m_, record + 0x16, 0);
    set_half_at(m_, record + 0x18, 0);
    set_word_at(m_, record, kObjectVtable);
    set_half_at(m_, record + 0x28, 0x1000);
    set_half_at(m_, record + 0x2a, 0x1000);
    set_half_at(m_, record + 0x2c, 0x1000);
    set_word_at(m_, record + 0x40, kObjectDefault);
}

// 0x0046f2e0: Mem_Copy (both addresses word aligned or the assertion logs).
void LevelGen::mem_copy(std::uint32_t destination, std::uint32_t source, std::uint32_t size) {
    s_.check((destination & 3) == 0, 0x00534bd8);
    s_.check((source & 3) == 0, 0x00534bd8);
    if (static_cast<std::int32_t>(size) > 0) {
        m_.copy(destination, source, size);
    }
}

// 0x0043e080: the scalar deleting destructor of an array of region object records (flag 2: an array whose length is the word before it; flag 1: free it).
std::uint32_t LevelGen::destroy_object_array(std::uint32_t self, std::uint32_t flags) {
    if ((flags & 2) != 0) {
        for (std::uint32_t i = word_at(m_, self - 4); i > 0; --i) {
            set_word_at(m_, self + (i - 1) * kRecordBytes, kObjectVtable);
        }
        if ((flags & 1) != 0) {
            free_block(self - 4);
        }
        return self - 4;
    }
    set_word_at(m_, self, kObjectVtable);
    if ((flags & 1) != 0) {
        free_block(self);
    }
    return self;
}

// ---- the zone packet ---------------------------------------------------------------------------------------------------------

// 0x0043e0e0: the cells of the 20 x 20 zone grid that a box (x, z, w, d in world units; `extent` the world's size) touches; with `build` each of them remembers the object
// `index` (a count, then 16-bit items). Returns how many cells the box touches.
std::uint32_t LevelGen::rasterise(std::uint32_t self, std::int32_t extent, std::int32_t x, std::int32_t z, std::int32_t w, std::int32_t d, std::uint32_t index,
                                  std::uint32_t build) {
    std::uint32_t touched = 0;
    std::int32_t x0 = (x * 20) / extent - 1;
    std::int32_t x1 = ((w + x) * 20) / extent + 1;
    std::int32_t z1 = 0x12 - ((d + z) * 20) / extent;
    std::int32_t z0 = 0x14 - (z * 20) / extent;
    x0 = x0 < 0 ? 0 : (x0 > 0x13 ? 0x13 : x0);
    z1 = z1 < 0 ? 0 : (z1 > 0x13 ? 0x13 : z1);
    x1 = x1 < 1 ? 1 : (x1 > 0x14 ? 0x14 : x1);
    z0 = z0 < 1 ? 1 : (z0 > 0x14 ? 0x14 : z0);
    for (std::int32_t column = x0; column < x1; ++column) {
        if (z1 < z0) {
            touched += static_cast<std::uint32_t>(z0 - z1);
            for (std::int32_t row = z1; row < z0; ++row) {
                if ((build & 0xff) != 0) {
                    const std::uint32_t cell = word_at(m_, word_at(m_, self + 0x23b4) + 4 * static_cast<std::uint32_t>(row)) + static_cast<std::uint32_t>(column) * kZoneCellBytes;
                    set_half_at(m_, cell + 2 + byte_at(m_, cell) * 2, index);
                    set_byte_at(m_, cell, byte_at(m_, cell) + 1);
                    s_.check(byte_at(m_, cell) <= 0xe6, kMsgZoneItems);
                }
            }
        }
    }
    return touched;
}

// 0x0043e2d0: the zone packet: for each of the 20 x 20 zone cells the objects that reach it. The first call (`dry`) only counts (the size comes back); the second writes
// the packet at `dest`. The editor's object (not the game's) keeps the counts in a table it makes for the call. Every piece, the second region's models, the start
// positions and the gap members are boxes on the zone grid.
std::uint32_t LevelGen::create_zone_packet(std::uint32_t self, std::uint32_t dest, std::uint32_t dry, std::uint32_t skip) {
    bool build = false;
    if ((dry & 0xff) == 0 && (skip & 0xff) == 0) {
        build = true;
        const std::uint32_t rows = alloc(0x50);
        set_word_at(m_, self + 0x23b4, rows);
        std::uint32_t block = alloc(0x2d1e0);
        for (std::uint32_t at = 0; at < kZoneRows; at += 4) {
            set_word_at(m_, rows + at, block);
            block += kZoneRowBytes;
        }
    }
    std::int32_t width = global_int(m_, kGridW);
    std::int32_t depth = global_int(m_, kGridD);
    std::int32_t cell = global_int(m_, kCellX);
    const std::int32_t extent = cell * (width > depth ? width : depth);
    if (build) {
        for (std::uint32_t row = 0; row < kZoneRows; row += 4) {
            for (std::uint32_t at = 0; at < kZoneRowBytes; at += kZoneCellBytes) {
                set_byte_at(m_, word_at(m_, word_at(m_, self + 0x23b4) + row) + at, 0);
            }
        }
        width = global_int(m_, kGridW);
        depth = global_int(m_, kGridD);
        cell = global_int(m_, kCellX);
    }
    std::uint32_t total = 0;
    std::uint32_t index = 0;
    for (std::uint32_t pass = 0; pass < 2; ++pass) {
        for (std::uint32_t piece = word_at(m_, self + (pass == 0 ? 0x2344 : 0x234c)); piece != 0; piece = word_at(m_, piece + 0x230)) {
            total += rasterise(self, extent, byte_at(m_, piece + 5) * cell, byte_at(m_, piece + 7) * cell, sbyte_at(m_, piece + 8) * cell, sbyte_at(m_, piece + 0xa) * cell, index++, build ? 1 : 0);
        }
    }
    width = global_int(m_, kGridW);
    depth = global_int(m_, kGridD);
    cell = global_int(m_, kCellX);
    const std::int32_t half_w = (width >> 1) * cell;
    const std::int32_t half_d = (depth >> 1) * cell;
    const std::uint32_t second = word_at(m_, self + 0x2380);
    const std::uint32_t models = word_at(m_, kObjectTables + 8 + second * 0x44) + 0x10 + word_at(m_, self + 0x2384) * 0x24;
    std::uint32_t objects = word_at(m_, kObjectTables + second * 0x44) + 0x10;
    for (std::int32_t j = 0; j < int_at(m_, self + 0x2384); ++j) {
        const std::uint32_t model = word_at(m_, models + 4 * static_cast<std::uint32_t>(j));
        const std::int32_t left = short_at(m_, model + 0xe) + half_w + (int_at(m_, objects - 8) >> 12);
        const std::int32_t right = short_at(m_, model + 0xc) + half_w + (int_at(m_, objects - 8) >> 12);
        const std::int32_t near_z = (half_d - short_at(m_, model + 0x14)) - (int_at(m_, objects) >> 12);
        const std::int32_t far_z = (half_d - short_at(m_, model + 0x16)) - (int_at(m_, objects) >> 12);
        total += rasterise(self, extent, left, near_z, right - left, far_z - near_z, index + static_cast<std::uint32_t>(j), build ? 1 : 0);
        objects += kRecordBytes;
        width = global_int(m_, kGridW);
        depth = global_int(m_, kGridD);
        cell = global_int(m_, kCellX);
    }
    std::uint32_t at_index = word_at(m_, self + 0x2384) + word_at(m_, self + 0x2358);
    for (std::uint32_t start = word_at(m_, self + 0x23a4); start != 0; start = word_at(m_, start + 8)) {
        total += rasterise(self, extent, (width >> 1) * cell + int_at(m_, start), (depth >> 1) * cell - int_at(m_, start + 4), cell * 2, cell * 2, at_index, build ? 1 : 0);
        ++at_index;
        width = global_int(m_, kGridW);
        depth = global_int(m_, kGridD);
        cell = global_int(m_, kCellX);
    }
    at_index = word_at(m_, self + 0x23a8) + word_at(m_, self + 0x2384) + word_at(m_, self + 0x2358);
    for (std::uint32_t gap = word_at(m_, self + 0x2388); gap != 0; gap = word_at(m_, gap + 0xc)) {
        total += rasterise(self, extent, (width >> 1) * cell + int_at(m_, gap + 0x20), (depth >> 1) * cell - int_at(m_, gap + 0x2c), int_at(m_, gap + 0x30), int_at(m_, gap + 0x34), at_index,
                           build ? 1 : 0);
        ++at_index;
        width = global_int(m_, kGridW);
        depth = global_int(m_, kGridD);
        cell = global_int(m_, kCellX);
    }
    if ((skip & 0xff) != 0) {
        total = 0;
    }
    const std::uint32_t size = total * 4 + 0x191c;
    set_word_at(m_, self + 0x23b0, size);
    if ((dry & 0xff) == 0) {
        set_word_at(m_, dest, 10);
        set_word_at(m_, dest + 0xc, static_cast<std::uint32_t>((half_d - extent) * 0x1000));
        set_word_at(m_, dest + 0x10, static_cast<std::uint32_t>((extent - half_w) * 0x1000));
        set_word_at(m_, dest + 0x14, static_cast<std::uint32_t>(half_d * 0x1000));
        set_word_at(m_, dest + 4, size - 8);
        set_word_at(m_, dest + 8, static_cast<std::uint32_t>(half_w * -0x1000));
        set_half_at(m_, dest + 0x18, 0x14);
        set_half_at(m_, dest + 0x1a, 0x14);
        std::uint32_t position = 7;
        for (std::uint32_t row = 0; row < kZoneRows; row += 4) {
            for (std::uint32_t at = 0; at < kZoneRowBytes; at += kZoneCellBytes) {
                std::uint32_t next;
                if (!build) {
                    set_word_at(m_, dest + 4 * (position + 2), 0);
                    next = position + 3;
                } else {
                    next = position + 3;
                    const std::uint32_t count = byte_at(m_, word_at(m_, word_at(m_, self + 0x23b4) + row) + at);
                    set_word_at(m_, dest + 4 * (position + 2), count);
                    for (std::uint32_t k = 0; k < count; ++k) {
                        set_word_at(m_, dest + 4 * next, half_at(m_, word_at(m_, word_at(m_, self + 0x23b4) + row) + at + 2 + 2 * k));
                        ++next;
                    }
                }
                set_word_at(m_, dest + 4 * next, 0);
                position = next + 1;
            }
        }
        if (build) {
            free_block(word_at(m_, word_at(m_, self + 0x23b4)));
            free_block(word_at(m_, self + 0x23b4));
        }
    }
    return word_at(m_, self + 0x23b0);
}

// ---- the region ------------------------------------------------------------------------------------------------------------------

// 0x0043bf80: give the region (and with the flag its colour table) back.
void LevelGen::unload_out_psx(std::uint32_t free_colours) {
    const std::uint32_t slot = global_word(m_, kSlot);
    if (slot != 0xffffffff) {
        if ((free_colours & 0xff) != 0) {
            s_.check(word_at(m_, kColourTables + slot * 0x44) != 0, kMsgNoColourTable);
            free_block(word_at(m_, kColourTables + global_word(m_, kSlot) * 0x44));
            set_global_word(m_, kColourCurrent, 0);
            set_word_at(m_, kColourTables + global_word(m_, kSlot) * 0x44, 0);
        }
        engine(0x004b32f0, {slot, 0, 0});
    }
    set_global_word(m_, kSlot, 0xffffffff);
}

// 0x0043d400: build the park's region: a PSX region slot named "edit" holds, in one block, the models of every piece (the pieces' built models, the second region's models,
// the start markers and the gap members' primitives), the table of their object records, the zone packet and the ordering table. The object records (0x4c bytes) are
// made for the pieces, the second region's models, the starts and the gaps, in that order. Returns the region; the slot is stored at `out_slot`.
std::uint32_t LevelGen::finalize_region(std::uint32_t self, std::uint32_t out_slot, std::uint32_t rebuild) {
    s_.check(global_word(m_, kRows) != 0, kMsgNoParkMap);
    build_floor_table(self);
    g_.pieces_clear_flag();
    set_global_word(m_, kLiveCount, 0);
    if (global_int(m_, kSlot) == -1) {
        std::int32_t found = -1;
        for (std::uint32_t i = 0; i < kRegionSlots; ++i) {
            if (byte_at(m_, kRegionNames + 0x44 * i) == 0) {
                found = static_cast<std::int32_t>(i);
                set_global_word(m_, kSlot, i);
                set_word_at(m_, kColourTables + 0x44 * i, 0);
                break;
            }
        }
        s_.check(found != -1, kMsgPsxFull);
    }
    std::uint32_t slot = global_word(m_, kSlot);
    engine(0x004b32f0, {slot, 0, 0});
    const std::uint32_t old_table = word_at(m_, kColourTables + slot * 0x44);
    if (old_table != 0) {
        free_block(old_table);
    }
    const std::uint32_t table = alloc(0x400, 1, 1, 0);
    slot = global_word(m_, kSlot);
    set_word_at(m_, kColourTables + slot * 0x44, table);
    {
        std::uint32_t index = 0;
        for (std::uint32_t value = 0; value < 0x1010100; value += 0x10101, ++index) {
            set_word_at(m_, word_at(m_, kColourTables + global_word(m_, kSlot) * 0x44) + 4 * index, value);
        }
    }
    slot = global_word(m_, kSlot);
    set_global_word(m_, kColourCurrent, word_at(m_, kColourTables + slot * 0x44));
    put_string(kRegionNames + slot * 0x44, "edit");
    set_word_at(m_, out_slot, slot);
    const std::uint32_t source = word_at(m_, kObjectTables + 8 + word_at(m_, self + 0x14) * 0x44);
    if ((rebuild & 0xff) != 0) {
        build_piece_objects(self);
    }
    prune_pieces(self);
    compute_neighbours(self);
    build_floor_objects(self);
    build_gap_objects(self);
    build_starts(self);
    g_.face_setup();
    for (std::uint32_t piece = word_at(m_, self + 0x2344); piece != 0; piece = word_at(m_, piece + 0x230)) {
        g_.out_remove_hidden(piece);
    }
    g_.face_cleanup();
    for (std::uint32_t piece = word_at(m_, self + 0x2344); piece != 0; piece = word_at(m_, piece + 0x230)) {
        set_byte_at(m_, piece + 1, 0);
    }
    set_word_at(m_, self + 0x2358, 0);
    std::uint32_t total = 0;
    for (std::uint32_t pass = 0; pass < 2; ++pass) {
        for (std::uint32_t piece = word_at(m_, self + (pass == 0 ? 0x2344 : 0x234c)); piece != 0; piece = word_at(m_, piece + 0x230)) {
            set_word_at(m_, self + 0x2358, word_at(m_, self + 0x2358) + 1);
            total += g_.out_data_size(piece, byte_at(m_, self + 0x3744));
        }
    }
    const std::uint32_t second = word_at(m_, kObjectTables + 8 + word_at(m_, self + 0x2380) * 0x44);
    const std::uint32_t second_models = word_at(m_, second + 8);
    set_word_at(m_, self + 0x2384, second_models);
    const std::uint32_t second_first = word_at(m_, second + 0x10 + second_models * 0x24);
    const std::uint32_t second_end = word_at(m_, second + 4) + second;
    set_word_at(m_, self + 0x23a8, 0);
    total += second_end - second_first;
    for (std::uint32_t start = word_at(m_, self + 0x23a4); start != 0; start = word_at(m_, start + 8)) {
        set_word_at(m_, self + 0x23a8, word_at(m_, self + 0x23a8) + 1);
        total += g_.gap_set_defaults(start);
    }
    set_word_at(m_, self + 0x238c, 0);
    for (std::uint32_t gap = word_at(m_, self + 0x2388); gap != 0; gap = word_at(m_, gap + 0xc)) {
        set_word_at(m_, self + 0x238c, word_at(m_, self + 0x238c) + 1);
        total += g_.gap_set_constants(gap);
    }
    const std::uint32_t objects = word_at(m_, self + 0x2384) + word_at(m_, self + 0x238c) + word_at(m_, self + 0x2358) + word_at(m_, self + 0x23a8);
    set_global_word(m_, kObjectTotal, objects);
    set_word_at(m_, self + 0x2354, objects);
    set_global_word(m_, kPieceTotal, word_at(m_, self + 0x2358));
    const std::uint32_t count = word_at(m_, self + 0x2354);
    const std::uint32_t table_bytes = count * 4 + 4;
    const std::uint32_t zone = create_zone_packet(self, 0, 1, byte_at(m_, self + 0x3744) == 0 ? 1 : 0);
    const std::uint32_t first_table = engine_value(0x004b20f0, {source});
    const std::uint32_t first_trailing = first_table + 4 + word_at(m_, self + 0x2340) * 4;
    const std::uint32_t first_count = word_at(m_, first_trailing - 4);
    const std::uint32_t second_table = engine_value(0x004b20f0, {second});
    std::uint32_t second_trailing = second_table + 4 + word_at(m_, self + 0x2384) * 4;
    const std::uint32_t second_count = word_at(m_, second_trailing - 4);
    const std::uint32_t region = alloc(count * 4 + (second_count + first_count) * 4 + zone + 4 + table_bytes + count * 0x24 + 0x10 + total, 1, 1, 0);
    set_word_at(m_, region, 0x20004);
    set_word_at(m_, region + 4, table_bytes + count * 0x24 + 0xc + total);
    set_word_at(m_, region + 8, word_at(m_, self + 0x2354));
    const std::uint32_t source_objects = word_at(m_, kObjectTables + word_at(m_, self + 0x14) * 0x44);
    const std::uint32_t records = g_.new_zeroed(word_at(m_, self + 0x2354) * kRecordBytes + 4);
    const std::uint32_t first = records + 4;
    if (records != 0) {
        set_word_at(m_, records, word_at(m_, self + 0x2354));
        for (std::uint32_t i = 0; i < word_at(m_, self + 0x2354); ++i) {
            init_record(first + i * kRecordBytes);
        }
    }
    // the pieces
    std::uint32_t index = 0;
    for (std::uint32_t pass = 0; pass < 2; ++pass) {
        for (std::uint32_t piece = word_at(m_, self + (pass == 0 ? 0x2344 : 0x234c)); piece != 0; piece = word_at(m_, piece + 0x230)) {
            const std::uint32_t record = first + index * kRecordBytes;
            s_.check(static_cast<std::int32_t>(index) < int_at(m_, self + 0x2358), kMsgModelList);
            set_half_at(m_, record + 4, 0);
            set_half_at(m_, record + 6, 0);
            set_byte_at(m_, record + 0x1d, 0);
            set_word_at(m_, record + 0x24, 0);
            set_half_at(m_, record + 0x14, 0);
            set_half_at(m_, record + 0x16, 0);
            set_half_at(m_, record + 0x18, 0);
            set_half_at(m_, record + 0x1a, index);
            set_byte_at(m_, record + 0x1f, global_byte(m_, kSlot));
            set_word_at(m_, record + 0x20, record + kRecordBytes);
            set_word_at(m_, record + 8, static_cast<std::uint32_t>(short_at(m_, piece + 0x21c)) << 12);
            set_word_at(m_, record + 0x10, static_cast<std::uint32_t>(short_at(m_, piece + 0x220)) << 12);
            set_word_at(m_, record + 0xc, static_cast<std::uint32_t>(short_at(m_, piece + 0x21e)) << 12);
            ++index;
            const std::uint32_t original = source_objects + byte_at(m_, word_at(m_, piece + 0x224) + 8) * kRecordBytes;
            set_byte_at(m_, record + 0x1c, byte_at(m_, original + 0x1c));
            set_byte_at(m_, record + 0x1e, byte_at(m_, original + 0x1e));
            set_word_at(m_, piece + 0x22c, record);
        }
    }
    // the second region's models
    {
        const std::uint32_t base = word_at(m_, kObjectTables + word_at(m_, self + 0x2380) * 0x44);
        for (std::uint32_t i = 0; i < word_at(m_, self + 0x2384); ++i) {
            const std::uint32_t at = word_at(m_, self + 0x2358) + i;
            const std::uint32_t record = first + at * kRecordBytes;
            const std::uint32_t src = base + i * kRecordBytes;
            set_half_at(m_, record + 4, half_at(m_, src + 4));
            set_half_at(m_, record + 6, half_at(m_, src + 6));
            set_word_at(m_, record + 8, word_at(m_, src + 8));
            set_word_at(m_, record + 0xc, word_at(m_, src + 0xc));
            set_word_at(m_, record + 0x10, word_at(m_, src + 0x10));
            set_word_at(m_, record + 0x14, word_at(m_, src + 0x14));
            set_half_at(m_, record + 0x18, half_at(m_, src + 0x18));
            set_half_at(m_, record + 0x1a, half_at(m_, src + 0x1a));
            set_byte_at(m_, record + 0x1c, byte_at(m_, src + 0x1c));
            set_byte_at(m_, record + 0x1d, byte_at(m_, src + 0x1d));
            set_byte_at(m_, record + 0x1e, byte_at(m_, src + 0x1e));
            set_byte_at(m_, record + 0x1f, byte_at(m_, src + 0x1f));
            set_word_at(m_, record + 0x20, word_at(m_, src + 0x20));
            set_word_at(m_, record + 0x24, word_at(m_, src + 0x24));
            set_word_at(m_, record + 0x28, word_at(m_, src + 0x28));
            set_half_at(m_, record + 0x2c, half_at(m_, src + 0x2c));
            set_word_at(m_, record + 0x30, word_at(m_, src + 0x30));
            set_word_at(m_, record + 0x34, word_at(m_, src + 0x34));
            set_word_at(m_, record + 0x38, word_at(m_, src + 0x38));
            set_half_at(m_, record + 0x3c, half_at(m_, src + 0x3c));
            set_word_at(m_, record + 0x40, word_at(m_, src + 0x40));
            set_half_at(m_, record + 0x44, half_at(m_, src + 0x44));
            set_half_at(m_, record + 0x46, half_at(m_, src + 0x46));
            set_half_at(m_, record + 0x48, half_at(m_, src + 0x48));
            set_half_at(m_, record + 0x4a, half_at(m_, src + 0x4a));
            set_half_at(m_, record + 0x1a, at);
            set_byte_at(m_, record + 0x1f, global_byte(m_, kSlot));
            set_word_at(m_, record + 0x20, record + kRecordBytes);
        }
    }
    // the start positions
    {
        std::uint32_t at = word_at(m_, self + 0x2358) + word_at(m_, self + 0x2384);
        for (std::uint32_t start = word_at(m_, self + 0x23a4); start != 0; start = word_at(m_, start + 8)) {
            const std::uint32_t record = first + at * kRecordBytes;
            set_half_at(m_, record + 4, 0);
            set_half_at(m_, record + 6, 0);
            set_byte_at(m_, record + 0x1d, 0);
            set_word_at(m_, record + 0x24, 0);
            set_half_at(m_, record + 0x14, 0);
            set_half_at(m_, record + 0x16, 0);
            set_half_at(m_, record + 0x18, 0);
            set_half_at(m_, record + 0x1a, at);
            set_byte_at(m_, record + 0x1f, global_byte(m_, kSlot));
            set_word_at(m_, record + 0x20, record + kRecordBytes);
            set_word_at(m_, record + 8, static_cast<std::uint32_t>((int_at(m_, start) + global_int(m_, kCellX)) * 0x1000));
            set_word_at(m_, record + 0xc, 0);
            set_word_at(m_, record + 0x10, static_cast<std::uint32_t>((int_at(m_, start + 4) - global_int(m_, kCellX)) * 0x1000));
            set_byte_at(m_, record + 0x1c, 0);
            set_byte_at(m_, record + 0x1e, 0);
            ++at;
            set_word_at(m_, start + 0xc, at);
        }
    }
    const std::uint32_t starts_end = word_at(m_, self + 0x2358) + word_at(m_, self + 0x2384) + word_at(m_, self + 0x23a8);
    set_word_at(m_, first + starts_end * kRecordBytes - 0x2c, 0);
    // the gap members
    {
        std::uint32_t at = word_at(m_, self + 0x2358) + word_at(m_, self + 0x23a8) + word_at(m_, self + 0x2384);
        for (std::uint32_t gap = word_at(m_, self + 0x2388); gap != 0; gap = word_at(m_, gap + 0xc)) {
            const std::uint32_t record = first + at * kRecordBytes;
            set_half_at(m_, record + 4, 0);
            set_half_at(m_, record + 6, 0);
            set_byte_at(m_, record + 0x1d, 0);
            set_word_at(m_, record + 0x24, 0);
            set_half_at(m_, record + 0x14, 0);
            set_half_at(m_, record + 0x16, 0);
            set_half_at(m_, record + 0x18, 0);
            set_half_at(m_, record + 0x1a, at);
            ++at;
            set_byte_at(m_, record + 0x1f, global_byte(m_, kSlot));
            set_word_at(m_, record + 8, 0);
            set_word_at(m_, record + 0xc, 0);
            set_word_at(m_, record + 0x10, 0);
            set_byte_at(m_, record + 0x1c, 0);
            set_byte_at(m_, record + 0x1e, 0);
            set_word_at(m_, record + 0x20, record + kRecordBytes);
            set_word_at(m_, gap + 0x14, record);
        }
    }
    set_word_at(m_, first + word_at(m_, self + 0x2354) * kRecordBytes - 0x2c, 0);
    if (word_at(m_, self + 0x2388) == 0) {
        set_word_at(m_, self + 0x23a0, 0);
    } else {
        set_word_at(m_, self + 0x23a0, first + (word_at(m_, self + 0x2358) + word_at(m_, self + 0x2384) + word_at(m_, self + 0x23a8)) * kRecordBytes);
    }
    // the models
    const std::uint32_t models = region + count * 0x24 + 0x10;
    set_word_at(m_, models - 4, word_at(m_, self + 0x2354));
    std::uint32_t cursor = models + word_at(m_, self + 0x2354) * 4;
    std::uint32_t model_index = 0;
    for (std::uint32_t pass = 0; pass < 2; ++pass) {
        for (std::uint32_t piece = word_at(m_, self + (pass == 0 ? 0x2344 : 0x234c)); piece != 0; piece = word_at(m_, piece + 0x230)) {
            s_.check(static_cast<std::int32_t>(model_index) < int_at(m_, self + 0x2358), kMsgModelList);
            set_word_at(m_, models + 4 * model_index, cursor);
            g_.out_create_data(piece, cursor, byte_at(m_, self + 0x3744));
            cursor += half_at(m_, piece + 0xe);
            set_global_word(m_, kLiveCount, global_word(m_, kLiveCount) + half_at(m_, piece + 0x14));
            ++model_index;
        }
    }
    {
        std::uint32_t table_at = second + 0x10 + second_models * 0x24;
        for (std::uint32_t i = 0; i < word_at(m_, self + 0x2384); ++i) {
            set_word_at(m_, models + 4 * (word_at(m_, self + 0x2358) + i), cursor);
            const std::uint32_t end = i != word_at(m_, self + 0x2384) - 1 ? word_at(m_, table_at + 4) : second_end;
            const std::uint32_t size = end - word_at(m_, table_at);
            mem_copy(cursor, word_at(m_, table_at), size);
            set_global_word(m_, kLiveCount, global_word(m_, kLiveCount) + half_at(m_, cursor + 6));
            cursor += size;
            table_at += 4;
        }
    }
    {
        std::uint32_t at = models + 4 * (word_at(m_, self + 0x2358) + word_at(m_, self + 0x2384));
        for (std::uint32_t start = word_at(m_, self + 0x23a4); start != 0; start = word_at(m_, start + 8)) {
            set_word_at(m_, at, cursor);
            cursor += g_.gap_build_quad(start, cursor);
            at += 4;
        }
    }
    {
        std::uint32_t gap_index = word_at(m_, self + 0x2358) + word_at(m_, self + 0x23a8) + word_at(m_, self + 0x2384);
        for (std::uint32_t gap = word_at(m_, self + 0x2388); gap != 0; gap = word_at(m_, gap + 0xc)) {
            set_word_at(m_, models + 4 * gap_index, cursor);
            ++gap_index;
            set_word_at(m_, gap + 0x18, gap_index);
            cursor += g_.gap_build_prims(gap, cursor);
        }
    }
    const std::uint32_t zone_at = cursor;
    const std::uint32_t order = cursor + (((zone + 4) >> 2) << 2);
    set_global_word(m_, kOrderTable, order);
    set_word_at(m_, order - 4, 0xffffffff);
    for (std::uint32_t i = 0; i < word_at(m_, self + 0x2354); ++i) {
        set_word_at(m_, order + 4 * i, i + 1);
    }
    set_word_at(m_, order + 4 * word_at(m_, self + 0x2354), second_count + first_count);
    std::uint32_t entry = order + 4 * (word_at(m_, self + 0x2354) + 1);
    for (std::uint32_t i = 0; i < first_count; ++i) {
        const std::uint32_t object = word_at(m_, first_trailing + 4 * i);
        set_word_at(m_, entry + 4 * i, object);
        if (half_at(m_, object + 0x10) != 0xffff) {
            set_half_at(m_, object + 0x10, half_at(m_, object + 0x10) + 1);
        }
    }
    entry += 4 * first_count;
    for (std::uint32_t i = 0; i < second_count; ++i) {
        const std::uint32_t object = word_at(m_, second_trailing);
        set_word_at(m_, entry + 4 * i, object);
        if (half_at(m_, object + 0x10) != 0xffff) {
            set_half_at(m_, object + 0x10, half_at(m_, object + 0x10) + 1);
        }
        second_trailing += 4;
    }
    slot = global_word(m_, kSlot);
    set_global_word(m_, kEnvironmentSlot, slot);
    set_global_word(m_, kEnvironmentLink, 0xffffffff);
    set_word_at(m_, kObjectTables + 8 + slot * 0x44, region);
    set_word_at(m_, kObjectTables + 4 + slot * 0x44, models);
    set_word_at(m_, kObjectTables - 4 + slot * 0x44, order);              // 0x0056d434
    set_byte_at(m_, 0x0056d463 + slot * 0x44, 1);
    set_word_at(m_, kObjectTables + slot * 0x44, first);
    set_byte_at(m_, 0x0056d431 + slot * 0x44, 0);
    set_word_at(m_, 0x0056d454 + slot * 0x44, 0);
    set_word_at(m_, 0x0056d458 + slot * 0x44, 0);
    set_byte_at(m_, 0x0056d432 + slot * 0x44, 1);
    set_byte_at(m_, 0x0056d465 + slot * 0x44, 1);
    set_global_word(m_, kEnvironmentList, word_at(m_, kObjectTables + slot * 0x44));
    set_global_word(m_, kOrderTable, order);
    if (byte_at(m_, self + 0x3744) != 0) {
        clear_source(self);
    }
    create_zone_packet(self, zone_at, 0, byte_at(m_, self + 0x3744) == 0 ? 1 : 0);
    engine(0x004667e0, {0, zone_at + 8});
    if (byte_at(m_, self + 0x3744) != 0) {
        create_rails(self);
    }
    flag_floor_pieces(self);
    return region;
}

}  // namespace opentony::runtime::levelgen
