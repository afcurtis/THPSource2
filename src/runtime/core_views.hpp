#pragma once

// 11c, Kit core (core_init.cpp, core_pkr.cpp, core_utils.cpp, core_display.cpp, core_main.cpp): the typed views of re/types/core.yml (and the layouts it shares with other subsystems) and the few helpers the
// core sources share. Included by those .cpp files only (never by a header: the generated views are large and the Kit's header is included by half the runtime).

#include "core_kit.hpp"
#include "guest_views_generated.hpp"
#include "heap_views.hpp"
#include "math_views.hpp"

namespace opentony::runtime::core::views {

using guest::CCamera;
using guest::CCameraView;
using guest::GuestPtr;
using guest::RuntimeD3DPolygonCommandHeaderView;
using guest::RuntimeGameplaySkater;
using guest::RuntimeGameplaySkaterView;
using guest::RuntimeMatrixTransformView;
using guest::RuntimeQ12MatrixView;
using guest::RuntimeRenderStateView;
using guest::RuntimeScreenPrimitiveView;
using guest::RuntimeViewportRectView;
using guest::RuntimeBufferedStreamObjectView;
using guest::RuntimeExplosionObjectView;
using guest::RuntimeExplosionParticleView;
using guest::RuntimeGteRegisterFileView;
using guest::RuntimeIntVector3View;
using guest::RuntimeListObjectView;
using guest::RuntimePkrFileRecordView;
using guest::RuntimePkrPackageHeaderView;
using guest::RuntimePsxModelPacketHeaderView;
using guest::RuntimePsxRegionSlotView;
using guest::RuntimeRelocatableModuleNodeView;
using guest::RuntimeShortVector3View;
using guest::RuntimeTypeTaggedListNodeView;
using guest::RuntimeViewportRecordView;
using guest::SLineInfoView;
using guest::read_scalar;
using guest::write_scalar;
using mathkit::views::component;
using mathkit::views::set_component;

using Word = std::uint32_t;

// The constants of core_kit.hpp are addresses of fields of the layouts: the corpus decides, and this is where the two are tied.
static_assert(object::kVtable == guest::kRuntimeListObject_vtable);
static_assert(object::kFlags == guest::kRuntimeListObject_visibility_flags);
static_assert(object::kPosition == guest::kRuntimeListObject_position);
static_assert(object::kNext == guest::kRuntimeListObject_next);
static_assert(object::kRegion == guest::kRuntimeListObject_region);
static_assert(line::kStart == guest::kSLineInfo_start_x);
static_assert(line::kEnd == guest::kSLineInfo_end_x);
static_assert(line::kHitBody == guest::kSLineInfo_hit_body);
static_assert(line::kHitY == guest::kSLineInfo_hit_position + 4);
static_assert(line::kSlope == guest::kSLineInfo_hit_normal + 2);
static_assert(line::kFlag == guest::kSLineInfo_query_mask_mode);
static_assert(globals::kViewStride == guest::kRuntimeViewportRecordSize);

// the region slots: kRegionValid is the byte at +0xa of slot 0 (the region type, nonzero for a loaded region)
inline constexpr Word kRegionSlots = 0x0056d428;
inline constexpr Word kRegionStride = guest::kRuntimePsxRegionSlotSize;
static_assert(globals::kRegionValid == kRegionSlots + guest::kRuntimePsxRegionSlot_region_type);
static_assert(kRegionStride == 0x44);

// the heap's zone table (Init_ResetMemory lays the two zones out); heap_views.hpp has the layout of the whole heap globals
[[nodiscard]] inline guest::RuntimeHeapZoneBoundsView memory_zone(RetailMemory& m, Word zone) { return heap::views::zone_bounds(m, zone); }

[[nodiscard]] inline RuntimeListObjectView object_at(RetailMemory& m, Word at) { return RuntimeListObjectView(m, at); }
[[nodiscard]] inline Word next_object(RetailMemory& m, Word at) { return object_at(m, at).next().address; }
[[nodiscard]] inline RuntimePsxRegionSlotView region_slot(RetailMemory& m, Word region) { return RuntimePsxRegionSlotView(m, kRegionSlots + region * kRegionStride); }
[[nodiscard]] inline RuntimeViewportRecordView viewport(RetailMemory& m, Word index) { return RuntimeViewportRecordView(m, globals::kViews + index * globals::kViewStride); }
[[nodiscard]] inline RuntimeGteRegisterFileView gte_registers(RetailMemory& m) { return RuntimeGteRegisterFileView(m, mathkit::views::kRegisters); }

// a single global named by its retail address (no recovered layout contains it): one explicit-width access
[[nodiscard]] inline Word global_word(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }
inline void set_global_word(RetailMemory& m, Word at, Word value) { write_scalar<Word>(m, at, value); }

// a word of an untyped buffer, an out-parameter or a scratch local
[[nodiscard]] inline Word word_at(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }
inline void set_word_at(RetailMemory& m, Word at, Word value) { write_scalar<Word>(m, at, value); }
[[nodiscard]] inline std::int32_t int_at(const RetailMemory& m, Word at) { return read_scalar<std::int32_t>(m, at); }
[[nodiscard]] inline std::uint16_t half_at(const RetailMemory& m, Word at) { return read_scalar<std::uint16_t>(m, at); }
[[nodiscard]] inline std::int16_t short_at(const RetailMemory& m, Word at) { return read_scalar<std::int16_t>(m, at); }
inline void set_half_at(RetailMemory& m, Word at, Word value) { write_scalar<std::uint16_t>(m, at, static_cast<std::uint16_t>(value)); }

// the same widths for a single global named by its retail address (the setters narrow the value the way the retail store does)
[[nodiscard]] inline std::int32_t global_int(const RetailMemory& m, Word at) { return read_scalar<std::int32_t>(m, at); }
[[nodiscard]] inline std::uint16_t global_half(const RetailMemory& m, Word at) { return read_scalar<std::uint16_t>(m, at); }
[[nodiscard]] inline std::int16_t global_short(const RetailMemory& m, Word at) { return read_scalar<std::int16_t>(m, at); }
[[nodiscard]] inline std::uint8_t global_byte(const RetailMemory& m, Word at) { return read_scalar<std::uint8_t>(m, at); }
inline void set_global_half(RetailMemory& m, Word at, Word value) { write_scalar<std::uint16_t>(m, at, static_cast<std::uint16_t>(value)); }
inline void set_global_byte(RetailMemory& m, Word at, Word value) { write_scalar<std::uint8_t>(m, at, static_cast<std::uint8_t>(value)); }

[[nodiscard]] inline RuntimeRelocatableModuleNodeView module_at(RetailMemory& m, Word at) { return RuntimeRelocatableModuleNodeView(m, at); }

// a byte of a string or an untyped buffer
[[nodiscard]] inline std::uint8_t byte_at(const RetailMemory& m, Word at) { return read_scalar<std::uint8_t>(m, at); }
inline void set_byte_at(RetailMemory& m, Word at, Word value) { write_scalar<std::uint8_t>(m, at, static_cast<std::uint8_t>(value)); }
[[nodiscard]] inline std::int8_t sbyte_at(const RetailMemory& m, Word at) { return read_scalar<std::int8_t>(m, at); }
[[nodiscard]] inline std::int8_t global_sbyte(const RetailMemory& m, Word at) { return read_scalar<std::int8_t>(m, at); }

// the skater (player) object by address, the camera object it owns, and the two player slots
[[nodiscard]] inline RuntimeGameplaySkaterView skater_at(RetailMemory& m, Word at) { return RuntimeGameplaySkaterView(m, at); }
[[nodiscard]] inline CCameraView camera_at(RetailMemory& m, Word at) { return CCameraView(m, at); }

// the render state the pointer at 0x00560fd4 names, and the 0x1c-byte primitives of the polygon arena
inline constexpr Word kRenderStatePointer = 0x00560fd4;
[[nodiscard]] inline RuntimeRenderStateView render_state(RetailMemory& m) { return RuntimeRenderStateView(m, global_word(m, kRenderStatePointer)); }
[[nodiscard]] inline RuntimeScreenPrimitiveView primitive_at(RetailMemory& m, Word at) { return RuntimeScreenPrimitiveView(m, at); }
static_assert(guest::kRuntimeScreenPrimitiveSize == 0x1c);

// the nine screen rectangles from 0x00533fa8
inline constexpr Word kViewportRects = 0x00533fa8;
inline constexpr Word kViewportRectCount = 9;
[[nodiscard]] inline RuntimeViewportRectView viewport_rect(RetailMemory& m, Word index) { return RuntimeViewportRectView(m, kViewportRects + index * guest::kRuntimeViewportRectSize); }
static_assert(guest::kRuntimeViewportRectSize == 0x18);

// a 3-short record (an angle triple) read the way the retail code reads it: each short zero-extended
[[nodiscard]] inline Word u16_of(std::int16_t value) { return static_cast<std::uint16_t>(value); }

// a vector of three dwords as the unsigned words the retail code adds and compares, and its three stores in order
[[nodiscard]] inline Word ux(const RuntimeIntVector3View& v) { return static_cast<Word>(v.x()); }
[[nodiscard]] inline Word uy(const RuntimeIntVector3View& v) { return static_cast<Word>(v.y()); }
[[nodiscard]] inline Word uz(const RuntimeIntVector3View& v) { return static_cast<Word>(v.z()); }
inline void set_vector(const RuntimeIntVector3View& v, Word x, Word y, Word z) {
    v.set_x(static_cast<std::int32_t>(x));
    v.set_y(static_cast<std::int32_t>(y));
    v.set_z(static_cast<std::int32_t>(z));
}

}  // namespace opentony::runtime::core::views
