#pragma once

#include "position_commit.hpp"

#include <cstdint>

namespace opentony::runtime {

// The brake input test at the top of retail Skater_UpdateBrakeMode
// (FUN_0049df00, 0x0049df4e..0x0049df90): Down held (controller +0xb0) with
// neither Left (+0x80) nor Right (+0x90), or the skater's vertical analog byte
// (+0x31a2) pushed down (signed > 0) with no horizontal lean (+0x31a1 == 0).
// Earlier notes called this the "surface" predicate; it reads only input.
[[nodiscard]] constexpr bool ground_brake_requested(
    bool down,
    bool left,
    bool right,
    std::int8_t vertical_lean,
    std::int8_t horizontal_lean) noexcept {
    return (down && !left && !right) ||
        (vertical_lean > 0 && horizontal_lean == 0);
}

// Inputs corresponding to the proven scalar/vector portion of retail
// FUN_0049df00. The brake request (ground_brake_requested) and the raw slope
// normal are supplied by the caller. The response vector remains in the
// retail fixed-point units.
struct GroundBrakeInput {
    FixedPosition response{};
    std::int32_t magnitude_q12{-1};
    std::int32_t slope_normal_y_q12{};
    std::int32_t frame_scale_q8{0x100};
    std::int32_t vertical_lean{}; // skater +0x31a2: > 0 scales the brake term by lean/16
    std::int32_t physics_state{};
    bool brake_requested{};
};

struct GroundBrakeResult {
    FixedPosition response{};
    std::int32_t magnitude_q12{};
    std::int32_t speed_metric{};
    std::int32_t speed_threshold{};
    bool decelerated{};
    bool stopped{};
    bool requested_state7{};

    friend bool operator==(
        const GroundBrakeResult&,
        const GroundBrakeResult&) = default;
};

// Fixed-point reconstruction of the grounded threshold/deceleration branch
// in FUN_0049df00. The separate brake-mode state machine remains outside this
// producer; this class only owns the observed slope threshold, component
// braking, and state-7 stop condition.
class GroundBrake final {
public:
    [[nodiscard]] static GroundBrakeResult apply(
        const GroundBrakeInput& input) noexcept;
};

} // namespace opentony::runtime
