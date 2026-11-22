#pragma once

// 11c, Kit math: the typed views of re/types/math.yml and the few helpers the three math_*.cpp files share. Included by those sources only (never by a header: the generated views
// are large and the Kit's header is included by half the runtime).

#include "guest_views_generated.hpp"
#include "math_kit.hpp"

namespace opentony::runtime::mathkit::views {

using guest::RuntimeGteRegisterFileView;
using guest::RuntimeIntVector3View;
using guest::RuntimeMatrixTransformView;
using guest::RuntimePsxDrawAreaPrimitiveView;
using guest::RuntimePsxRectView;
using guest::RuntimeQ12MatrixView;
using guest::RuntimeShortVector3View;
using guest::RetailTimerStateView;
using guest::read_scalar;
using guest::write_scalar;

using Word = std::uint32_t;

// The constants of math_kit.hpp are the addresses of the register file's fields: the layout in re/types decides, and this is where the two are tied.
inline constexpr std::uint32_t kRegisters = gte::kWindow;
static_assert(gte::kWindowBytes == guest::kRuntimeGteRegisterFileSize);
static_assert(gte::kH == kRegisters + guest::kRuntimeGteRegisterFile_h);
static_assert(gte::kScreen == kRegisters + guest::kRuntimeGteRegisterFile_screen_xy);
static_assert(gte::kRotation == kRegisters + guest::kRuntimeGteRegisterFile_rotation);
static_assert(gte::kIr0 == kRegisters + guest::kRuntimeGteRegisterFile_ir0);
static_assert(gte::kBackground == kRegisters + guest::kRuntimeGteRegisterFile_background);
static_assert(gte::kV0 == kRegisters + guest::kRuntimeGteRegisterFile_v0);
static_assert(gte::kV1 == kRegisters + guest::kRuntimeGteRegisterFile_v1);
static_assert(gte::kV2 == kRegisters + guest::kRuntimeGteRegisterFile_v2);
static_assert(gte::kRgbFifo == kRegisters + guest::kRuntimeGteRegisterFile_rgb_fifo);
static_assert(gte::kLight == kRegisters + guest::kRuntimeGteRegisterFile_light);
static_assert(gte::kTranslation == kRegisters + guest::kRuntimeGteRegisterFile_translation);
static_assert(gte::kIr == kRegisters + guest::kRuntimeGteRegisterFile_ir);
static_assert(gte::kMac == kRegisters + guest::kRuntimeGteRegisterFile_mac);
static_assert(gte::kColour == kRegisters + guest::kRuntimeGteRegisterFile_colour);
static_assert(gte::kOfx == kRegisters + guest::kRuntimeGteRegisterFile_ofx);
static_assert(gte::kOfy == kRegisters + guest::kRuntimeGteRegisterFile_ofy);
static_assert(gte::kOtz == kRegisters + guest::kRuntimeGteRegisterFile_otz);
static_assert(gte::kSz == kRegisters + guest::kRuntimeGteRegisterFile_sz);
static_assert(gte::kLzcr == kRegisters + guest::kRuntimeGteRegisterFile_lzcr);
static_assert(gte::kMac0 == kRegisters + guest::kRuntimeGteRegisterFile_mac0);
static_assert(gte::kMac0b == kRegisters + guest::kRuntimeGteRegisterFile_mac0_copy);

// component k of a vector, widened the way the retail `movsx` reads it
[[nodiscard]] inline std::int32_t component(const RuntimeShortVector3View& v, std::uint32_t k) { return k == 0 ? v.x() : (k == 1 ? v.y() : v.z()); }
[[nodiscard]] inline Word component(const RuntimeIntVector3View& v, std::uint32_t k) { return static_cast<Word>(k == 0 ? v.x() : (k == 1 ? v.y() : v.z())); }
inline void set_component(const RuntimeShortVector3View& v, std::uint32_t k, std::int32_t value) {
    const auto narrow = static_cast<std::int16_t>(value);
    if (k == 0) {
        v.set_x(narrow);
    } else if (k == 1) {
        v.set_y(narrow);
    } else {
        v.set_z(narrow);
    }
}
inline void set_component(const RuntimeIntVector3View& v, std::uint32_t k, Word value) {
    const auto word = static_cast<std::int32_t>(value);
    if (k == 0) {
        v.set_x(word);
    } else if (k == 1) {
        v.set_y(word);
    } else {
        v.set_z(word);
    }
}
inline void set_mac(const RuntimeGteRegisterFileView& g, Word a, Word b, Word c) {
    const RuntimeIntVector3View mac = g.mac();
    mac.set_x(static_cast<std::int32_t>(a));
    mac.set_y(static_cast<std::int32_t>(b));
    mac.set_z(static_cast<std::int32_t>(c));
}
// the matrix element at (row, column)
[[nodiscard]] inline std::int32_t element(const RuntimeQ12MatrixView& matrix, std::uint32_t row, std::uint32_t column) { return matrix.elements(3 * row + column); }
// a 16-bit register or matrix word widened to the 32-bit register the retail code multiplies in
[[nodiscard]] constexpr Word widen(std::int16_t value) { return static_cast<Word>(static_cast<std::int32_t>(value)); }

}  // namespace opentony::runtime::mathkit::views
