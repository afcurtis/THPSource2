#pragma once

#include "position_commit.hpp"

#include <cstdint>
#include <functional>

namespace opentony::runtime {

class PlayerState;

// Constants from the PC retail grounded bounce/recovery helpers.  These are
// fixed-point/world-space contracts, not replay-tuned thresholds.
inline constexpr std::int32_t kGroundedBounceProbeHeight = 0x1e;
inline constexpr std::int32_t kGroundedBounceNormalOffset = 1;
inline constexpr std::int32_t kGroundedBounceDefaultDistance = 0x0a;
inline constexpr std::int32_t kGroundedBounceBlockedDistance = 0x46;
inline constexpr std::int32_t kGroundedBounceResponseBiasQ12 = 0xcd;
inline constexpr std::int32_t kGroundedBounceDefaultHeading = 0x19;
inline constexpr std::int32_t kGroundedBounceAlternateHeading = 200;
inline constexpr std::int32_t kGroundedBounceOrthogonalLimitQ12 = 0x400;

[[nodiscard]] constexpr std::int32_t grounded_bounce_probe_distance(
    bool control_blocked) noexcept {
    return control_blocked
        ? kGroundedBounceBlockedDistance
        : kGroundedBounceDefaultDistance;
}

[[nodiscard]] constexpr bool accepts_grounded_bounce_normal(
    std::int32_t normal_alignment_q12) noexcept {
    return normal_alignment_q12 > -kGroundedBounceOrthogonalLimitQ12
        && normal_alignment_q12 < kGroundedBounceOrthogonalLimitQ12;
}

struct GroundedBounceProbeGeometry final {
    FixedPosition first_start{};
    FixedPosition first_end{};
};

// Build the two lines used by one 0x004957c0 call.  Retail uses the live
// position for the first line, but the position restored from +0xbc for both
// endpoints of the verification line.
[[nodiscard]] GroundedBounceProbeGeometry make_grounded_bounce_probe_geometry(
    const FixedPosition& live_position,
    const FixedPosition& right_basis,
    const FixedPosition& up_basis,
    std::int32_t direction,
    std::int32_t distance) noexcept;

[[nodiscard]] FixedPosition make_grounded_bounce_verification_start(
    const FixedPosition& previous_position,
    const FixedPosition& hit_normal) noexcept;

// Endpoint of FUN_004957c0's verification line and of its direct write:
// previous position + normal * distance, whatever the probe direction.
[[nodiscard]] FixedPosition make_grounded_bounce_verification_end(
    const FixedPosition& previous_position,
    const FixedPosition& hit_normal,
    std::int32_t distance) noexcept;

// FUN_00496550's state-0 support tail calls FUN_004956f0 before publishing a
// response or recovery basis when the selected support face is not eligible
// for the ordinary grounded path.  The predicate is expressed in terms of
// the decoded face bits and the three live player vectors used by the PC
// branch; it is deliberately independent of a surface/material number.
[[nodiscard]] bool support_hit_requests_ground_exit(
    const PositionCollisionHit& hit,
    const FixedPosition& recovery_target_normal,
    const FixedPosition& current_forward,
    const FixedPosition& collision_response) noexcept;

// The branch FUN_00496550 takes after its ordinary movement query hits
// (0x0049696b..0x00496db8). Only Direct and Wall call FUN_0049bad0.
enum class GroundMovementHitBranch {
    // Inverse24, Inverse23 and Surface40 all clear (0x00496999): restore the
    // position, FUN_0049bad0.
    Direct,
    // Surface40 set with Face80 clear (0x00496a7c): straight to the support
    // sweep.
    Skipped,
    // |dot(normal, +0x80)| >= 0x800 (0x00496ded): the same surface; commit.
    SameSurface,
    // -n.y beats |n.x| and |n.z| (0x00496b39): commit contact + 30n,
    // publish +0x80, request state 0.
    Floor,
    // Anything else (0x00496cdf): restore the position, FUN_0049bad0.
    Wall,
};

[[nodiscard]] GroundMovementHitBranch classify_ground_movement_hit(
    const PositionCollisionHit& hit,
    const FixedPosition& ground_normal_80) noexcept;

[[nodiscard]] constexpr bool ground_movement_hit_calls_response(
    GroundMovementHitBranch branch) noexcept {
    return branch == GroundMovementHitBranch::Direct
        || branch == GroundMovementHitBranch::Wall;
}

struct OuterFloorRecoveryResult final {
    bool gated{};
    bool upward_hit{};
    bool restart_probe_hit{};
    bool restart_probe_rejected{};
    bool short_recovery_hit{};
    bool x_recovery_hit{};
    bool z_recovery_hit{};
    bool position_changed{};
    bool response_yaw_applied{};
    bool external_service_requested{};

    friend bool operator==(
        const OuterFloorRecoveryResult&,
        const OuterFloorRecoveryResult&) = default;
};

// Reconstructs the pre-dispatch FUN_00490730 query chain. The helper owns the
// player writes at +0x2f40, +0x08/+0x10, +0xbc/+0xc4, and the persistent
// +0x2e0c scratch reference. The caller supplies the retail global restart
// predicate and keeps FUN_0046d2e0 as an explicit external service boundary.
[[nodiscard]] OuterFloorRecoveryResult apply_outer_floor_recovery(
    PlayerState& player,
    const FixedPosition& query_position,
    const PositionCollisionQuery& query,
    bool restart_at_start,
    const std::function<void(const PositionCollisionHit&)>&
        on_external_service = {});

} // namespace opentony::runtime
