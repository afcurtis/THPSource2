#pragma once

// 11c, Kit collision: the typed views of re/types/collision.yml and the few helpers the collision_*.cpp files share. Included by those sources only (never by a header: the generated
// views are large and the Kit's header is included by half the runtime).

#include "collision_kit.hpp"
#include "math_views.hpp"

namespace opentony::runtime::collision::views {

using guest::CollisionCandidateHeadArrayEntryView;
using guest::CollisionFacePrefixView;
using guest::CollisionModelDataHeaderView;
using guest::CollisionNormalRecordView;
using guest::CollisionVertexRecordView;
using guest::CollisionZoneGridRecordView;
using guest::GuestPtr;
using guest::RuntimeCollisionObject;
using guest::RuntimeCollisionObjectView;
using guest::SColCacheView;
using guest::SFaceCacheView;
using guest::SLineInfoView;
using guest::read_scalar;
using guest::write_scalar;
using mathkit::views::RuntimeGteRegisterFileView;
using mathkit::views::RuntimeIntVector3View;
using mathkit::views::RuntimeQ12MatrixView;
using mathkit::views::RuntimeShortVector3View;
using mathkit::views::component;
using mathkit::views::set_component;

using Word = std::uint32_t;

// The constants of collision_kit.hpp stay for the Kits that still read the record raw; this is where they are tied to the layouts.
static_assert(line::kStart == guest::kSLineInfo_start_x);
static_assert(line::kEnd == guest::kSLineInfo_end_x);
static_assert(line::kHitBody == guest::kSLineInfo_hit_body);
static_assert(line::kHitY == guest::kSLineInfo_hit_position + 4);
static_assert(line::kSlope == guest::kSLineInfo_hit_normal + 2);
static_assert(globals::kCacheEntriesEnd - globals::kCacheEntries == guest::kCollisionModelCacheSize);
static_assert(guest::kSColCacheSize == 0x10);
static_assert(guest::kSFaceCacheSize == 0x1c);

constexpr Word kBlockmap = 0x00567f80;             // the zone record (CollisionZoneGridRecord); the level's only zone
constexpr Word kBasis = 0x00567bb0;                // nine shorts (RuntimeQ12Matrix): the turn about y onto the z axis (written by the slanted case; the commit reuses it)
constexpr Word kBestNormal = 0x00564390;           // three dwords (RuntimeIntVector3): the normal of the best face, in the hit object's frame
constexpr Word kFaceBoxes = 0x005643b0;            // the face cache's ring of 500 SFaceCache boxes
constexpr Word kModelTables = 0x0056d43c;          // PsxModelTablesByRegion: per region (0x44 bytes) the pointer to the array of model pointers
constexpr Word kModelTableStride = 0x44;
constexpr Word kCellColumn = 20;                   // cells along z in a column of x (CollisionZoneGridRecord.cells)
static_assert(kBestNormal + guest::kRuntimeIntVector3Size < kFaceBoxes);
static_assert(globals::kCacheEntriesEnd == kBasis);

// the retail registers the Kit loads: the GTE's file
inline constexpr Word kRegisters = mathkit::views::kRegisters;

// a cache slot by index, without a range test (retail's own assertions test the index; a bad one reads whatever is there)
[[nodiscard]] inline SColCacheView cache_slot(RetailMemory& m, Word index) { return SColCacheView(m, globals::kCacheEntries + static_cast<Word>(guest::kSColCacheSize) * index); }
[[nodiscard]] inline SFaceCacheView face_box(RetailMemory& m, Word index) { return SFaceCacheView(m, kFaceBoxes + static_cast<Word>(guest::kSFaceCacheSize) * index); }

// the model of an object: the region's table of model pointers, entry model_index
[[nodiscard]] inline Word model_of(RetailMemory& m, const RuntimeCollisionObjectView& item) {
    const Word table = read_scalar<Word>(m, kModelTables + kModelTableStride * item.model_region_slot());
    return read_scalar<Word>(m, table + 4 * Word{item.model_index()});
}

// the two words of a face record the walkers read whole: word 0 (the base flags with the length in the upper half) and the flags word at +0xc (the normal index << 3, the surface flags above)
[[nodiscard]] inline Word face_word0(const CollisionFacePrefixView& face) { return Word{face.base_flags()} | (Word{face.length_bytes()} << 16); }
[[nodiscard]] inline Word face_flags(const CollisionFacePrefixView& face) { return Word{face.normal_index_shifted()} | (Word{face.surface_flags()} << 16); }
// the byte stride to the next face record
[[nodiscard]] inline Word face_stride(const CollisionFacePrefixView& face) { return 4 * (face_word0(face) >> 18); }

// the items of a cell: a list of object pointers ending in 0
[[nodiscard]] inline Word cell_item(RetailMemory& m, Word at) { return CollisionCandidateHeadArrayEntryView(m, at).object_head().address; }

// the line's ends and box by axis
[[nodiscard]] inline Word line_start(const SLineInfoView& l, Word k) { return static_cast<Word>(k == 0 ? l.start_x() : (k == 1 ? l.start_y() : l.start_z())); }
[[nodiscard]] inline Word line_end(const SLineInfoView& l, Word k) { return static_cast<Word>(k == 0 ? l.end_x() : (k == 1 ? l.end_y() : l.end_z())); }
[[nodiscard]] inline Word line_low(const SLineInfoView& l, Word k) { return static_cast<Word>(k == 0 ? l.bounds_min_x() : (k == 1 ? l.bounds_min_y() : l.bounds_min_z())); }
[[nodiscard]] inline Word line_high(const SLineInfoView& l, Word k) { return static_cast<Word>(k == 0 ? l.bounds_max_x() : (k == 1 ? l.bounds_max_y() : l.bounds_max_z())); }
inline void set_line_low(const SLineInfoView& l, Word k, Word v) {
    const auto word = static_cast<std::int32_t>(v);
    if (k == 0) {
        l.set_bounds_min_x(word);
    } else if (k == 1) {
        l.set_bounds_min_y(word);
    } else {
        l.set_bounds_min_z(word);
    }
}
inline void set_line_high(const SLineInfoView& l, Word k, Word v) {
    const auto word = static_cast<std::int32_t>(v);
    if (k == 0) {
        l.set_bounds_max_x(word);
    } else if (k == 1) {
        l.set_bounds_max_y(word);
    } else {
        l.set_bounds_max_z(word);
    }
}

// a face box's corners by axis
inline void set_box_low(const SFaceCacheView& box, Word k, Word v) {
    const auto word = static_cast<std::int32_t>(v);
    if (k == 0) {
        box.set_bounds_min_x(word);
    } else if (k == 1) {
        box.set_bounds_min_y(word);
    } else {
        box.set_bounds_min_z(word);
    }
}
inline void set_box_high(const SFaceCacheView& box, Word k, Word v) {
    const auto word = static_cast<std::int32_t>(v);
    if (k == 0) {
        box.set_bounds_max_x(word);
    } else if (k == 1) {
        box.set_bounds_max_y(word);
    } else {
        box.set_bounds_max_z(word);
    }
}

// a vertex or normal component by axis, widened the way the retail `movsx` reads it
[[nodiscard]] inline std::int32_t component(const CollisionVertexRecordView& v, Word k) { return k == 0 ? v.x() : (k == 1 ? v.y() : v.z()); }
[[nodiscard]] inline std::int32_t component(const CollisionNormalRecordView& n, Word k) { return k == 0 ? n.x() : (k == 1 ? n.y() : n.z()); }

// the matrix `from` into the matrix `to` (nine shorts)
inline void copy_matrix(const RuntimeQ12MatrixView& to, const RuntimeQ12MatrixView& from) {
    for (Word k = 0; k < 9; ++k) {
        to.set_elements(k, from.elements(k));
    }
}

}  // namespace opentony::runtime::collision::views
