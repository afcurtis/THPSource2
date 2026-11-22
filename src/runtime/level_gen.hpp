#pragma once

#include "gen_objs.hpp"
#include "player_spooler.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace opentony::runtime {

// Phase 4.11c: the park generator of the editor on retail addresses: levelgen.cpp (0x004397d0..0x0043f720).
//
// One `LevelGen` object (0x3748 bytes, the editor keeps it) holds the library of source models and the pieces built from a park. The park's cells (a 16-byte cell: five
// 16-bit piece references at +0, five turns at +0xa and the number of layers used at +0xf) are kept in the retail global `0x005614d4` (rows of cells; the editor owns it);
// the object keeps `FloorTab` (+0x0: the cells with the floor and wall fill pieces applied) and two maps of out-models (+0x2348, +0x2350: five pointers per cell, one per
// layer) with their piece lists (+0x2344, +0x234c). Everything is read and written at its retail address; the engine (allocator, the PSX region spooler, the rail and
// zone object classes) is a named service whose result the replay scripts. The out-model, gap member and polygon classes are `genobjs::GenObjs` (4.11b).
//
// Object layout (offsets in the LevelGen object):
//   +0x000 FloorTab (rows of 16-byte cells)           +0x004 name of the source PSX (0x10)     +0x014 its region slot (-1 none)
//   +0x018 the source models: 250 in-models of 0x24   +0x2340 how many are loaded
//   +0x2344 first piece (out-model) of the park       +0x2348 OutModelMap (rows of 5 piece pointers per cell)
//   +0x234c first piece of the floor                  +0x2350 FloorModelMap
//   +0x2354 objects in the region built               +0x2358 pieces in the first and second list
//   +0x235c "<name>_L" (0x10) and its slot +0x236c    +0x2370 "<name>_W<n>" (0x10) and its slot +0x2380
//   +0x2384 models in the second region               +0x2388 first gap member    +0x238c gaps built    +0x2390 the disabled item's gap member
//   +0x2394 flash on  +0x2398 flash level  +0x239c flash step    +0x23a0 first gap object of the region    +0x23a4 first start position  +0x23a8 start positions
//   +0x23ac 0x320    +0x23b0 size of the zone packet    +0x23b4 the zone grid's counts (while the packet is made)    +0x23b8 a polygon list (0xc, the rails' outlines)
//   +0x23c4 first rail/zone node object    +0x23c8
//   +0x369c, +0x36b0 two start records of 0x14 bytes (flag, x, z, layer, direction)    +0x3698 horse points made    +0x36c4 six horse point records of 0x14
//   +0x373c, +0x3740 the fog range    +0x3744 the object is the in-game one (not the editor's)
namespace levelgen {

// ---- statics ---------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kGridW = 0x00528398;            // park width and depth in cells, and the row of the size table
constexpr std::uint32_t kGridD = 0x0052839c;
constexpr std::uint32_t kVersion = 0x005283a0;
constexpr std::uint32_t kVersionTable = 0x005283a4;     // 8 bytes per version: width, depth
constexpr std::uint32_t kTheme = 0x005614c8;
constexpr std::uint32_t kRowsSource = 0x005614cc;       // the editor's cell rows and item records, and the copies the generator reads
constexpr std::uint32_t kItemsSource = 0x005614d0;
constexpr std::uint32_t kRows = 0x005614d4;
constexpr std::uint32_t kItems = 0x005614d8;
constexpr std::uint32_t kCellX = 0x00561488;
constexpr std::uint32_t kCellZ = 0x0056148c;
constexpr std::uint32_t kSlot = 0x005613b8;             // the region slot of the last park built
constexpr std::uint32_t kStartTypes = 0x005613b0;       // two ids: the pieces that mark the skater starts
constexpr std::uint32_t kHorseType = 0x005611a8;        // the piece id of the horse (restart) point
constexpr std::uint32_t kStartMarker = 0x005611ac;      // the piece id whose position starts a run
constexpr std::uint32_t kSpecialA = 0x005614a8;         // five 16-bit piece ids each (the fill pieces of the floor), 4 bytes apart
constexpr std::uint32_t kSpecialB = 0x00561490;
constexpr std::uint32_t kSpecialC = 0x00561174;
constexpr std::uint32_t kGapMarker = 0x005286fc;        // the marker byte a gap item gets when it is updated
constexpr std::uint32_t kTrailing = 0x0056a1fc;         // eight 8-byte records ("AAA", 0, 0)
constexpr std::uint32_t kLiveCount = 0x00561198;
constexpr std::uint32_t kObjectTables = 0x0056d438;     // per region slot (0x44 bytes): +0 the object records, +4 the models, +8 the raw region, +0x18 the colour table
constexpr std::uint32_t kLevelState = 0x00533f38;
constexpr std::uint32_t kFogTable = 0x0052a4a0;         // fog settings per theme
constexpr std::uint32_t kFogSize = 0x0052a428;          // fog range per size and level kind
constexpr std::uint32_t kFreeA = 0x00568628;            // (the low-memory test of the fog dimmer)
constexpr std::uint32_t kFreeB = 0x00560fd0;

// where the native code builds the strings retail builds in locals (the probe records the stack addresses as these)
constexpr std::uint32_t kScratchName = 0x7ff00800;
constexpr std::uint32_t kScratchLibrary = 0x7ff00840;
constexpr std::uint32_t kScratchPosition = 0x7ff00880;
constexpr std::uint32_t kScratchBytes = 0x100;

constexpr std::uint32_t kItemRecords = 10;
constexpr std::uint32_t kItemBytes = 0x2c;
constexpr std::uint32_t kRecordBytes = 0x4c;

// assertion messages (the address of the format string names the assertion)
constexpr std::uint32_t kMsgNoMap = 0x0052a9a0;
constexpr std::uint32_t kMsgNoGaps = 0x0052a97c;
constexpr std::uint32_t kMsgSourceLoaded = 0x0052aa20;
constexpr std::uint32_t kMsgTooManyItems = 0x0052aa00;
constexpr std::uint32_t kMsgTooManyModels = 0x0052a9e8;
constexpr std::uint32_t kMsgFloorTabAllocated = 0x0052aa7c;
constexpr std::uint32_t kMsgOutMapAllocated = 0x0052aa5c;
constexpr std::uint32_t kMsgFloorMapAllocated = 0x0052aa3c;
constexpr std::uint32_t kMsgFloorTabFree = 0x0052aad0;
constexpr std::uint32_t kMsgOutMapFree = 0x0052aab4;
constexpr std::uint32_t kMsgFloorMapFree = 0x0052aa98;
constexpr std::uint32_t kMsgFloorZ = 0x0052ab34;
constexpr std::uint32_t kMsgOutOfBounds = 0x0052ab94;
constexpr std::uint32_t kMsgOverlap3 = 0x0052ab70;
constexpr std::uint32_t kMsgOverlap = 0x0052abc0;
constexpr std::uint32_t kMsgHorseFull = 0x0052ab54;
constexpr std::uint32_t kMsgNeighbours = 0x0052aba4;
constexpr std::uint32_t kMsgNoGapModel = 0x0052abd4;
constexpr std::uint32_t kMsgGapZ = 0x0052abec;
constexpr std::uint32_t kMsgGapX = 0x0052ac0c;
constexpr std::uint32_t kMsgNullString = 0x0052ac2c;
constexpr std::uint32_t kMsgNoTrickob = 0x0052ac3c;
constexpr std::uint32_t kMsgNoColourTable = 0x0052ac5c;
constexpr std::uint32_t kMsgGridZ = 0x0052a730;
constexpr std::uint32_t kMsgOutMapZ = 0x0052ad88;
constexpr std::uint32_t kMsgNoHorse = 0x0052ad64;
constexpr std::uint32_t kMsgNoParkMap = 0x0052ae28;
constexpr std::uint32_t kMsgPsxFull = 0x0052adf0;
constexpr std::uint32_t kMsgModelList = 0x0052ada4;
constexpr std::uint32_t kMsgItemSize = 0x0052add0;
constexpr std::uint32_t kMsgZoneItems = 0x0052ae40;
constexpr std::uint32_t kMsgNoRailNode = 0x0052ae94;
constexpr std::uint32_t kMsgNoPrevNode = 0x0052aea8;
constexpr std::uint32_t kMsgRailLoop = 0x0052ae78;
constexpr std::uint32_t kMsgNodeList = 0x0052aebc;
constexpr std::uint32_t kMsgNoModel = 0x0052aee4;
constexpr std::uint32_t kMsgFloorModelZ = 0x0052aefc;
constexpr std::uint32_t kMsgVersion = 0x0052ace4;

class LevelGen final {
public:
    LevelGen(RetailMemory& memory, RetailServices& services, genobjs::GenObjs& generator) : m_(memory), s_(services), g_(generator) {}

    // ---- construction, the maps and the source models ----
    std::uint32_t construct(std::uint32_t self, std::uint32_t in_game);              // 0x004397d0
    void destruct(std::uint32_t self);                                               // 0x00439900
    void init_statics(std::uint32_t reset);                                          // 0x004399a0 (cdecl)
    void load_models(std::uint32_t self, std::uint32_t name);                        // 0x00439b70
    void unload_models(std::uint32_t self);                                          // 0x00439e10
    void clear_source(std::uint32_t self);                                           // 0x00439ed0
    void alloc_maps(std::uint32_t self);                                             // 0x00439ef0
    void free_maps(std::uint32_t self);                                              // 0x0043a100
    void resize(std::uint32_t self, std::uint32_t version);                          // 0x0043a240
    void select_variant(std::uint32_t self, std::uint32_t theme, std::uint32_t version, std::uint32_t reset);   // 0x0043a280
    void build_floor_table(std::uint32_t self);                                      // 0x0043a3e0

    // ---- pieces, gaps and starts ----
    void release_pieces(std::uint32_t self);                                         // 0x0043aa30
    void build_piece_objects(std::uint32_t self);                                    // 0x0043ab00
    void prune_pieces(std::uint32_t self);                                           // 0x0043b080
    void compute_neighbours(std::uint32_t self);                                     // 0x0043b0f0
    void free_floor_pieces(std::uint32_t self);                                      // 0x0043b3c0
    void build_floor_objects(std::uint32_t self);                                    // 0x0043b410
    void flag_floor_pieces(std::uint32_t self);                                      // 0x0043b850
    void free_gaps(std::uint32_t self);                                              // 0x0043b880
    void build_gap_objects(std::uint32_t self);                                      // 0x0043b8d0
    void free_starts(std::uint32_t self);                                            // 0x0043bba0
    void build_starts(std::uint32_t self);                                           // 0x0043bbf0
    void load_rails(std::uint32_t self, std::uint32_t name);                         // 0x0043bcd0
    void unload_out_psx(std::uint32_t free_colours);                                 // 0x0043bf80 (cdecl)

    // ---- the PRK buffer ----
    void parse_buffer(std::uint32_t buffer, std::uint32_t out);                      // 0x0043c050 (cdecl)
    std::uint32_t serialize_buffer(std::uint32_t out);                               // 0x0043c480 (cdecl)
    void translate_references(std::uint32_t theme);                                  // 0x0043c6b0 (cdecl)

    // ---- editing ----
    void place_piece(std::uint32_t self, std::uint32_t x, std::uint32_t z, std::uint32_t layer, std::uint32_t rotation, std::uint32_t type);   // 0x0043c770
    std::uint32_t add_item(std::uint32_t x, std::uint32_t layer, std::uint32_t z, std::uint32_t index);   // 0x0043cc30 (stdcall)
    void refresh_item_gaps(std::uint32_t self, std::uint32_t item_index, std::uint32_t side);   // 0x0043cd90
    void remove_items_at(std::uint32_t x, std::uint32_t layer, std::uint32_t z);     // 0x0043ce00 (stdcall)
    void set_flash(std::uint32_t self, std::uint32_t on);                            // 0x0043ce60
    bool remove_top(std::uint32_t self, std::uint32_t x, std::uint32_t z, std::uint32_t limit_at, std::uint32_t force);   // 0x0043cf20

    // ---- the region ----
    std::uint32_t finalize_region(std::uint32_t self, std::uint32_t out_slot, std::uint32_t rebuild);   // 0x0043d400
    std::uint32_t destroy_object_array(std::uint32_t self, std::uint32_t flags);     // 0x0043e080
    std::uint32_t rasterise(std::uint32_t self, std::int32_t extent, std::int32_t x, std::int32_t z, std::int32_t w, std::int32_t d, std::uint32_t index,
                            std::uint32_t build);                                    // 0x0043e0e0
    std::uint32_t create_zone_packet(std::uint32_t self, std::uint32_t dest, std::uint32_t dry, std::uint32_t skip);   // 0x0043e2d0
    void create_rails(std::uint32_t self);                                           // 0x0043e7c0
    void build_nodes(std::uint32_t self);                                            // 0x0043e980
    void mark_edges(std::uint32_t self, std::uint32_t piece, std::uint32_t mode);    // 0x0043f1b0
    void set_cell_flags(std::uint32_t self, std::uint32_t piece, std::uint32_t clear);   // 0x0043f400
    void set_floor_flags(std::uint32_t self, std::uint32_t on);                      // 0x0043f540
    void mark_gaps(std::uint32_t self, std::uint32_t piece);                         // 0x0043f5a0
    void set_fog(std::uint32_t self, std::uint32_t halve);                           // 0x0043f5e0
    void dim_fog(std::uint32_t self);                                                // 0x0043f6c0
    std::uint32_t zone_room();                                                       // 0x0043f710

    // the second list rails are merged with: its deadline word holds the live clock (the replay does not compare it)
    [[nodiscard]] std::uint32_t rails_scratch_list() const noexcept { return rails_list_; }

private:
    [[nodiscard]] std::uint32_t alloc(std::uint32_t size, std::uint32_t heap = 0, std::uint32_t one = 1, std::uint32_t last = 0);
    void free_block(std::uint32_t block);
    void log(std::uint32_t format);
    void engine(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {});
    [[nodiscard]] std::uint32_t engine_value(std::uint32_t address, std::vector<std::uint32_t> args = {});
    void put_string(std::uint32_t at, const std::string& text);
    void init_record(std::uint32_t record);
    void mem_copy(std::uint32_t destination, std::uint32_t source, std::uint32_t size);
    [[nodiscard]] std::uint32_t op_new(std::uint32_t size);
    void turn_piece(std::uint32_t piece, std::uint32_t in, std::uint32_t rotation, std::int32_t& offset_a, std::int32_t& offset_b);
    void build_pieces(std::uint32_t self, bool park);
    [[nodiscard]] std::uint32_t virtual_call(std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {});

    RetailMemory& m_;
    RetailServices& s_;
    genobjs::GenObjs& g_;
    std::uint32_t rails_list_ = 0;
};

}  // namespace levelgen
}  // namespace opentony::runtime
