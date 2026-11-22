#pragma once

#include "retail_memory.hpp"
#include "guest_view.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace opentony::runtime {

// Phase 4.11b: the generator object layer of the park editor on retail addresses: genobjs.cpp (0x0043f730..0x004436d0: the model-info pool, the editor's
// in-model, the out-model that merges neighbouring pieces, the gap members, and the ring-of-points polygon code they use), faceremoval.cpp (0x004436e0..0x00444010:
// the hidden-face removal on 0x18-byte points) and editorpieces2.cpp (0x00444010..0x00444510: the piece id tables).
//
// Engine calls are services: the allocator (`alloc`, 0x0046f490), `mem_delete` (0x0046f4d0), the logger (`printf_log`) and the one GTE call (`engine_004e3ce0`).
// The assertion routine only logs (as everywhere else): a failed check is reported with its message address and the code carries on.
namespace genobjs {

// ---- statics ---------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kPoolIndex = 0x00528710;        // the model-info block being filled
constexpr std::uint32_t kPoolTable = 0x005611bc;        // 100 block pointers (a block: a count and 100 elements of 0x2c bytes)
constexpr std::uint32_t kPoolSlots = 100;
constexpr std::uint32_t kPoolBlockBytes = 0x1134;
constexpr std::uint32_t kPoolElement = 0x2c;

constexpr std::uint32_t kRingTable = 0x00561454;        // 0x8c0 sixteen-byte nodes
constexpr std::uint32_t kRingCursor = 0x00561458;
constexpr std::uint32_t kRingUsed = 0x005614e4;
constexpr std::uint32_t kRingNodes = 0x8c0;
constexpr std::uint32_t kRingFree = 0xffffd8f1;         // marker of a free node
constexpr std::uint32_t kRingLive = 0xffffdd48;         // marker of a node that is in use

constexpr std::uint32_t kTheme = 0x005614c8;            // the park theme (the column of the piece tables)
constexpr std::uint32_t kThemeRows = 0x005283a0;
constexpr std::uint32_t kGridWidth = 0x00528398;
constexpr std::uint32_t kGridDepth = 0x0052839c;
constexpr std::uint32_t kCellX = 0x00561488;            // the size of one cell in each direction
constexpr std::uint32_t kCellZ = 0x0056148c;
constexpr std::uint32_t kLightRows = 0x0052a190;        // 32-byte rows: lights per theme
constexpr std::uint32_t kLightA = 0x00561160;
constexpr std::uint32_t kLightB = 0x0056119c;
constexpr std::uint32_t kLightCount = 0x00561168;
constexpr std::uint32_t kLightC = 0x00561194;
constexpr std::uint32_t kLightPositions = 0x00561350;   // twelve bytes per light: x, y, z
constexpr std::uint32_t kLightDirections = 0x005613f0;
constexpr std::uint32_t kFalloffTable = 0x005614dc;     // 0x400 bytes
constexpr std::uint32_t kShadeTable = 0x005614e0;       // 0x100 bytes
constexpr std::uint32_t kGteInput = 0x006a3eb0;         // the GTE's vector register (three halves) and the squares it leaves
constexpr std::uint32_t kGteSquares = 0x006a3eb8;
constexpr std::uint32_t kGteSqr0 = 0x004e3ce0;

constexpr std::uint32_t kMsgPoolNotEmpty = 0x0052af24;
constexpr std::uint32_t kMsgPoolFull = 0x0052af60;

class GenObjs final {
public:
    GenObjs(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // bytes retail copies from uninitialised stack (an element's fifth point when it has three): the replay does not compare them
    [[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint32_t>>& unspecified() const noexcept { return unspecified_; }
    void reset_unspecified() { unspecified_.clear(); }

    // ---- the model-info pool (genobjs.cpp) ----
    void model_info_clear(std::uint32_t allow);                                       // 0x0043f730
    void model_info_free_all();                                                       // 0x0043f7b0
    std::uint32_t model_info_alloc();                                                 // 0x0043f820

    // ---- the editor's in-model: the faces of a model as flat points ----
    std::uint32_t in_construct(std::uint32_t self) { return self; }                   // 0x0043f8e0
    void in_destruct_thunk(std::uint32_t self) { in_destruct(self); }                 // 0x0043f8f0
    void in_destruct(std::uint32_t self);                                             // 0x0043f900
    void in_set_data(std::uint32_t self, std::uint32_t model, std::uint32_t type);    // 0x0043f930
    void in_set_transparency(std::uint32_t self, std::uint32_t flag);                 // 0x0043fda0

    // ---- the out-model's statics: lights, lookup tables, the ring of nodes ----
    void out_init();                                                                  // 0x0043fe00
    void out_build_tables();                                                          // 0x0043ff30
    void out_cleanup();                                                               // 0x00440010
    std::uint32_t node_alloc();                                                       // 0x00440060
    std::int32_t node_spare();                                                        // 0x00440170

    // ---- the out-model (0x238 bytes): one piece placed in the park ----
    std::uint32_t out_construct(std::uint32_t self, std::uint32_t in);                // 0x00440190
    void out_release_nodes(std::uint32_t self);                                       // 0x00440290
    void out_show_nodes(std::uint32_t self);                                          // 0x004402d0
    void out_hide_against(std::uint32_t self, std::uint32_t other, std::uint32_t mode);   // 0x004402f0
    void out_hide_pair(std::uint32_t self, std::uint32_t other, std::uint32_t a, std::uint32_t b, std::uint32_t mode);   // 0x004403c0
    void out_remove_hidden(std::uint32_t self);                                       // 0x004407c0
    std::uint32_t out_data_size(std::uint32_t self, std::uint32_t flag);              // 0x00440920
    std::uint32_t out_create_data(std::uint32_t self, std::uint32_t buffer, std::uint32_t flag);   // 0x004409e0
    void out_mark_faces(std::uint32_t self, std::uint32_t wire_pass, std::uint32_t wire, std::uint32_t flags);   // 0x00440e80
    void out_set_fog(std::uint32_t self, std::uint32_t on, std::uint32_t colour);     // 0x00441050

    // ---- gap members (0x58 bytes) ----
    std::uint32_t gap_init(std::uint32_t self);                                       // 0x004410b0
    void gap_nothing() {}                                                             // 0x004410d0
    void gap_compute_edges(std::uint32_t self, std::uint32_t rotation, std::uint32_t before, std::uint32_t after);   // 0x004410e0
    std::uint32_t gap_set_constants(std::uint32_t self);                              // 0x00441240
    std::uint32_t gap_build_prims(std::uint32_t self, std::uint32_t buffer);          // 0x00441270
    void gap_build_header(std::uint32_t self);                                        // 0x00441400
    void gap_set_active(std::uint32_t self, std::uint32_t flag);                      // 0x00441630
    void gap_set_visible(std::uint32_t self, std::uint32_t flag);                     // 0x00441640
    std::uint32_t gap_return_this(std::uint32_t self) { return self; }                // 0x00441670
    void gap_nothing_b() {}                                                           // 0x00441680
    std::uint32_t gap_set_defaults(std::uint32_t self);                               // 0x00441690
    std::uint32_t gap_build_quad(std::uint32_t self, std::uint32_t buffer);           // 0x004416b0

    // ---- points and polygons (rings of points) ----
    std::uint32_t point_init(std::uint32_t self);                                     // 0x004417c0
    bool point_near(std::uint32_t self, std::uint32_t other);                         // 0x004417e0
    std::uint32_t poly_init(std::uint32_t self);                                      // 0x00441830
    void poly_free_ring(std::uint32_t self);                                          // 0x00441850
    void poly_append(std::uint32_t self, std::uint32_t point, std::uint32_t closed);  // 0x00441880
    void poly_fix_winding(std::uint32_t self);                                        // 0x004419c0
    void poly_reverse(std::uint32_t self);                                            // 0x00441aa0
    std::uint32_t poly_copy_transformed(std::uint32_t self, std::uint32_t source, std::uint32_t dx, std::uint32_t dy, std::uint32_t dz, std::uint32_t rotation,
                                        std::uint32_t tag);                           // 0x00441ae0
    std::uint32_t poly_try_merge(std::uint32_t self, std::uint32_t other, std::uint32_t mode);   // 0x00441cd0
    bool poly_join_ends(std::uint32_t self, std::uint32_t other);                     // 0x00441da0
    bool poly_edge_runs(std::uint32_t a, std::uint32_t b, std::int32_t count_a, std::int32_t count_b, std::uint32_t* first, std::uint32_t* last,
                        std::uint32_t* b_near_last, std::uint32_t* b_near_first);     // 0x00442100
    std::uint32_t poly_merge_loops(std::uint32_t self, std::uint32_t other);          // 0x00442210
    bool poly_cut_by(std::uint32_t self, std::uint32_t other, std::uint32_t& rest);   // 0x00442670
    std::uint32_t poly_split(std::uint32_t self);                                     // 0x00442780
    std::int32_t poly_update_bounds(std::uint32_t self);                              // 0x00442970
    bool poly_cut_out(std::uint32_t self, std::uint32_t kill, std::uint32_t& rest);   // 0x00442ae0
    bool edge_straight(std::uint32_t p, std::uint32_t q, std::uint32_t r, std::int32_t limit);   // 0x00442e90
    std::uint32_t poly_trim(std::uint32_t self, std::uint32_t b);                     // 0x00442f40

    // ---- the entry points that write a result through a pointer argument ----
    bool poly_cut_by_at(std::uint32_t self, std::uint32_t other, std::uint32_t rest_at) {   // 0x00442670, forced
        std::uint32_t rest = guest::read_scalar<std::uint32_t>(m_, rest_at);       // (only written when the cut is reached)
        const bool done = poly_cut_by(self, other, rest);
        guest::write_scalar<std::uint32_t>(m_, rest_at, static_cast<std::uint32_t>(rest));
        return done;
    }
    bool poly_cut_out_at(std::uint32_t self, std::uint32_t kill, std::uint32_t rest_at) {   // 0x00442ae0, forced
        std::uint32_t rest = 0;
        const bool done = poly_cut_out(self, kill, rest);
        guest::write_scalar<std::uint32_t>(m_, rest_at, static_cast<std::uint32_t>(rest));
        return done;
    }

    // ---- lists of polygons ----
    std::uint32_t plist_init(std::uint32_t self);                                     // 0x004431f0
    void plist_free_all_thunk(std::uint32_t self) { plist_free_all(self); }           // 0x00443200
    std::uint32_t plist_new(std::uint32_t self);                                      // 0x00443210
    void plist_free_all(std::uint32_t self);                                          // 0x00443250
    std::uint32_t plist_count(std::uint32_t self);                                    // 0x00443290
    std::uint32_t plist_points(std::uint32_t self);                                   // 0x004432b0
    void plist_merge(std::uint32_t self, std::uint32_t incoming, std::uint32_t mode);   // 0x004432d0
    std::uint32_t plist_pair(std::uint32_t self, std::uint32_t poly, std::uint32_t mode);   // 0x004434e0
    std::uint32_t plist_pop(std::uint32_t self);                                      // 0x00443580
    void plist_append(std::uint32_t self, std::uint32_t poly);                        // 0x004435e0

    // ---- helpers ----
    std::uint32_t new_zeroed(std::uint32_t size);                                     // 0x004436a0
    void delete_block(std::uint32_t block);                                           // 0x004436d0

    // ---- faceremoval.cpp ----
    void face_setup();                                                                // 0x004436e0
    void face_cleanup();                                                              // 0x00443710
    std::uint32_t point_new();                                                        // 0x00443730
    void points_reset();                                                              // 0x004437d0
    void point_print(std::uint32_t point);                                            // 0x004437f0
    std::int32_t faces_overlap(std::uint32_t ring_a, std::uint32_t ring_b);           // 0x00443820
    bool points_equal(std::uint32_t p, std::uint32_t q);                              // 0x00443eb0
    std::int32_t points_area(std::uint32_t ring);                                     // 0x00443f00
    std::int32_t points_cross(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d);   // 0x00443f30
    bool points_side(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d);   // 0x00443f70
    void ring_print(std::uint32_t start);                                             // 0x00443fc0

    // ---- editorpieces2.cpp ----
    void pieces_clear_flag();                                                         // 0x00444010
    void pieces_setup(std::uint32_t theme);                                           // 0x00444020
    std::int32_t piece_find(std::uint32_t id, std::uint32_t theme);                   // 0x004441c0
    std::uint32_t piece_theme_id(std::uint32_t id, std::uint32_t theme);              // 0x00444210
    bool piece_is_special(std::uint32_t id);                                          // 0x00444280
    std::uint32_t piece_list_index(std::uint32_t id);                                 // 0x004442b0
    std::int32_t piece_translate(std::uint32_t position);                             // 0x00444380
    std::uint32_t piece_kind(std::uint32_t id);                                       // 0x00444410
    std::uint32_t piece_turned(std::uint32_t id, std::uint32_t a, std::uint32_t b);   // 0x00444450

private:
    [[nodiscard]] std::uint32_t alloc(std::uint32_t size, std::uint32_t b, std::uint32_t c, std::uint32_t d);
    void free_block(std::uint32_t block);
    void log(std::uint32_t format);
    void mem_copy(std::uint32_t destination, std::uint32_t source, std::uint32_t size);

    RetailMemory& m_;
    RetailServices& s_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> unspecified_;
};

}  // namespace genobjs
}  // namespace opentony::runtime
