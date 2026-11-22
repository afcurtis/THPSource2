#include "ground_physics.hpp"

#include <bit>

namespace opentony::runtime {

namespace {

[[nodiscard]] GroundAnimationControlHandoff animation_handoff(
    const GroundPhysicsInput& input) noexcept {
    // Retail loads +0x114 as a 16-bit word, stores its low byte in +0x101,
    // then loads +0x101 as a signed char before storing it in +0x114.
    return GroundAnimationControlHandoff{
        true,
        0,
        static_cast<std::int8_t>(-1),
        static_cast<std::uint8_t>(input.original_start_frame),
        static_cast<std::int16_t>(
            std::bit_cast<std::int8_t>(input.playback_endpoint)),
    };
}

// The four animation wrappers each take a trailing source-line tag they do
// not forward; RunAnim receives the fixed-arity arguments recorded here.
[[nodiscard]] GroundAnimationRequest brake_animation_request(
    GroundAnimationRequestWrapper wrapper,
    std::uint16_t animation,
    std::int32_t start = 0,
    std::int32_t end = -1,
    std::int32_t alternate = -1) noexcept {
    return GroundAnimationRequest{
        true, wrapper, animation, start, end, alternate, true, false};
}

[[nodiscard]] bool brake_pose_playing(std::uint16_t animation_state) noexcept {
    return animation_state == 0x5d || animation_state == 0x5f;
}

} // namespace

GroundPhysicsResult update_ground_physics(
    const GroundPhysicsInput& input) noexcept {
    GroundPhysicsResult result{};
    result.response = input.response;
    result.ground_update_state = input.ground_update_state;

    const bool eligible_state =
        input.physics_state == 0 || input.physics_state == 7;
    if (input.physics_locked || input.state_blocked || !eligible_state) {
        result.ground_update_state = 0;
        result.action = GroundPhysicsAction::ResetForNonGround;
        if (input.physics_state == 7) {
            result.physics_state_requested = true;
            result.requested_physics_state = 0;
            result.requested_physics_reason = 0x2c0f;
            result.action = GroundPhysicsAction::RequestGroundFromState7;
        }
        return result;
    }

    if (input.physics_state == 7 && !input.brake_requested) {
        result.physics_state_requested = true;
        result.requested_physics_state = 0;
        result.requested_physics_reason = 0x2c21;
        result.action = GroundPhysicsAction::RequestGroundFromState7;
    }

    const GroundBrakeResult brake = GroundBrake::apply(GroundBrakeInput{
        input.response,
        -1,
        input.slope_normal_y_q12,
        input.frame_scale_q8,
        input.vertical_lean, // +0x31a2 scales the brake term (0x0049e033)
        input.physics_state,
        input.brake_requested,
    });
    result.response = brake.response;
    result.speed_metric = brake.speed_metric;
    result.speed_threshold = brake.speed_threshold;
    result.response_decelerated = brake.decelerated;
    result.response_stopped = brake.stopped;
    if (brake.requested_state7) {
        result.physics_state_requested = true;
        result.requested_physics_state = 7;
        result.requested_physics_reason = 0x2c56;
        result.action = GroundPhysicsAction::StopAndRequestState7;
    }

    const auto write_cooldown = [&result]() noexcept {
        result.cooldown_written = true;
        result.cooldown_value = 2;
    };
    const auto set_mode = [&result](std::int32_t mode) noexcept {
        result.ground_update_state = mode;
    };

    switch (input.ground_update_state) {
    case 0:
        if (input.brake_requested) {
            if (result.speed_metric <= result.speed_threshold) {
                set_mode(0);
                write_cooldown();
                break;
            }
            const bool low_speed =
                !input.blocked_or_special && result.speed_metric < 0x30000;
            if (!brake_pose_playing(input.animation_state)) {
                result.animation_request = brake_animation_request(
                    GroundAnimationRequestWrapper::Start,
                    low_speed ? 0x31 : 0x83);
            }
            if (low_speed) {
                set_mode(5);
                result.action = GroundPhysicsAction::EnterLowSpeedMode;
            } else {
                set_mode(1);
                result.action = GroundPhysicsAction::EnterHighSpeedMode;
            }
            write_cooldown();
        }
        break;

    case 1:
        if (!input.brake_requested ||
            result.speed_metric <
                static_cast<std::int32_t>(input.animation_frame) * 0x1000
                    + result.speed_threshold) {
            result.animation_handoff = animation_handoff(input);
            set_mode(3);
            result.animation_transition = input.brake_requested;
            result.action = GroundPhysicsAction::AdvanceToAnimationMode;
            if (input.brake_requested) {
                result.services.random_release_sound = true;
                result.services.dust = true;
                result.services.dust_type = 2;
            }
        }
        write_cooldown();
        break;

    case 2:
        // Retail's case 2 falls through to the function return without a
        // cooldown write or an additional state mutation.
        break;

    case 3:
        if (input.animation_ready) {
            result.animation_request = input.blocked_or_special
                ? brake_animation_request(
                      GroundAnimationRequestWrapper::Full, 8, 0x13, 0x1a, 0x13)
                : brake_animation_request(
                      GroundAnimationRequestWrapper::Range, 8, 0x13, 0);
            set_mode(4);
            result.animation_transition = true;
            result.action = GroundPhysicsAction::AdvanceToAnimationComplete;
        }
        write_cooldown();
        break;

    case 4:
        if (!input.brake_requested) {
            set_mode(0);
            result.action = GroundPhysicsAction::ResetToIdleMode;
        }
        break;

    case 5:
        if (!input.brake_requested ||
            result.speed_metric <
                static_cast<std::int32_t>(input.animation_frame) * 0x1000
                    + result.speed_threshold) {
            // Same raw animation-control exchange as the mode 1 -> 3 release.
            result.animation_handoff = animation_handoff(input);
            set_mode(6);
            result.animation_transition = input.brake_requested;
            result.action = GroundPhysicsAction::AdvanceToAnimationMode;
            if (input.brake_requested && input.animation_frame > 4) {
                result.services.fixed_release_sound = true;
                result.services.fixed_sound_id = 2;
            }
        }
        write_cooldown();
        break;

    case 6:
        if (input.animation_ready) {
            result.animation_request = brake_animation_request(
                GroundAnimationRequestWrapper::Start, 0);
            set_mode(4);
            result.animation_transition = true;
            result.action = GroundPhysicsAction::AdvanceToAnimationComplete;
        }
        write_cooldown();
        break;

    default:
        // Retail reports an illegal brake mode and otherwise leaves the
        // record untouched. Preserve that conservative behavior here.
        break;
    }
    return result;
}

} // namespace opentony::runtime
