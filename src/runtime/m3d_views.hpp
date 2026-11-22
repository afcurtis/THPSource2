#pragma once

// 11c, Kit m3d (m3d_matrix.cpp, m3d_boxes.cpp, m3d_transform.cpp, m3d_collision.cpp): the typed views of the layouts the M3D helpers work on (the GTE register file, the matrices and transforms of
// shorts, the culling box, the linked objects and their models, the line record) and the few helpers the four sources share. Included by those .cpp files only (never by a header: the
// generated views are large and the Kit's header is included by half the runtime).

#include "collision_views.hpp"
#include "guest_views_generated.hpp"
#include "m3d_kit.hpp"

namespace opentony::runtime::m3d::views {

using namespace collision::views;

using guest::RuntimeClippedVertexView;
using guest::RuntimeCullBoxView;
using guest::RuntimeLightSetView;
using guest::RuntimeListObject;
using guest::RuntimeListObjectView;
using guest::RuntimeShortTransformView;
using guest::RuntimeViewportRecordView;

using Word = std::uint32_t;

static_assert(kBasis == 0x00567bb0);
static_assert(kBestNormal == 0x00564390);
static_assert(guest::kRuntimeCullBoxSize == 0x30);
static_assert(guest::kRuntimeClippedVertexSize == 0x08);
static_assert(guest::kRuntimeShortTransformSize == 0x18);
static_assert(guest::kRuntimeLightSetSize == 0x20);
static_assert(guest::kRuntimeCollisionObject_flags == 0x04);
static_assert(guest::kRuntimeListObject_next == guest::kRuntimeCollisionObject_collision_next);
static_assert(guest::kRuntimeListObject_position == guest::kRuntimeCollisionObject_position_q12);

// the GTE register file (memory at 0x006a3e00)
[[nodiscard]] inline RuntimeGteRegisterFileView gte_file(RetailMemory& m) { return RuntimeGteRegisterFileView(m, kRegisters); }

// a single global named by its retail address (no recovered layout contains it): one explicit-width access
[[nodiscard]] inline Word global_word(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }
[[nodiscard]] inline std::int32_t global_int(const RetailMemory& m, Word at) { return read_scalar<std::int32_t>(m, at); }
[[nodiscard]] inline std::int16_t global_short(const RetailMemory& m, Word at) { return read_scalar<std::int16_t>(m, at); }
inline void set_global_word(RetailMemory& m, Word at, Word value) { write_scalar<Word>(m, at, value); }

// a word of an untyped table (the pointer arrays of the object lists and the trigger commands)
[[nodiscard]] inline Word word_at(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }

// a short, widened the way the retail `movsx` reads it
[[nodiscard]] constexpr std::int32_t sext16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }
[[nodiscard]] constexpr Word u(std::int32_t v) { return static_cast<Word>(v); }

// the screen word SXY<index>: the two shorts of the pair as one dword, the way the retail code copies it
[[nodiscard]] inline Word screen_word(const RuntimeGteRegisterFileView& g, std::size_t index) {
    return Word{static_cast<std::uint16_t>(g.screen_xy(2 * index))} | (Word{static_cast<std::uint16_t>(g.screen_xy(2 * index + 1))} << 16);
}

// the matrix `from` into the rotation, light or colour matrix of the register file is copy_matrix(to, from) (collision_views.hpp): nine shorts, element by element in order.

// the three components of a short vector as the register-width values a GTE load takes
inline void set_vector3(const RuntimeShortVector3View& v, std::int32_t x, std::int32_t y, std::int32_t z) {
    v.set_x(static_cast<std::int16_t>(x));
    v.set_y(static_cast<std::int16_t>(y));
    v.set_z(static_cast<std::int16_t>(z));
}

// the three stores of a vector of dwords, in order
inline void set_vector3(const RuntimeIntVector3View& v, Word x, Word y, Word z) {
    v.set_x(static_cast<std::int32_t>(x));
    v.set_y(static_cast<std::int32_t>(y));
    v.set_z(static_cast<std::int32_t>(z));
}

}  // namespace opentony::runtime::m3d::views
