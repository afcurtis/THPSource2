#pragma once

#include "ground_animation.hpp"
#include "ground_brake.hpp"

#include <cstdint>

namespace opentony::runtime {

// Raw inputs read by retail Skater_UpdateBrakeMode (FUN_0049df00).
// brake_requested is ground_brake_requested() over the controller Down/Left/
// Right records and the skater lean bytes; the caller evaluates it because
// the controller records live outside this movement boundary.
struct GroundPhysicsInput final {
    FixedPosition response{};
    std::int32_t physics_state{};       // skater +0x30b8
    std::int32_t ground_update_state{}; // skater +0x2df8
    std::int32_t slope_normal_y_q12{};  // skater +0x312a, signed short
    std::int16_t animation_frame{};     // skater +0xf4
    std::uint16_t animation_state{};    // skater +0xf6
    std::int8_t vertical_lean{};        // skater +0x31a2
    std::int8_t horizontal_lean{};      // skater +0x31a1
    bool brake_requested{};        // bVar4, see ground_brake_requested()
    bool physics_locked{};              // skater +0x2dd4
    bool state_blocked{};                // +0x29c8/+0x2dd0/+0x2f64 aggregate
    bool blocked_or_special{};          // skater +0x2dd8
    bool animation_ready{};             // skater +0x107
    std::int32_t frame_scale_q8{0x100};  // DAT_0056865c-equivalent scale
    // Raw CSuper animation fields read by the mode-1 transition. They remain
    // caller-owned so this movement boundary does not select an animation.
    std::uint8_t playback_endpoint{};     // skater +0x101 raw byte
    std::int16_t original_start_frame{};  // skater +0x114
};

// The mode-1 branch updates these animation-control bytes before handing the
// actual animation request to the animation service. Keep the values raw and
// preserve the retail byte/short truncation at this boundary.
struct GroundAnimationControlHandoff final {
    bool applied{};
    std::uint8_t animation_finished{};   // skater +0x107, cleared by retail
    std::int8_t playback_direction{};    // skater +0x100, written as 0xff
    std::uint8_t playback_endpoint{};    // skater +0x101, old +0x114 low byte
    std::int16_t original_start_frame{}; // skater +0x114, sign-extended +0x101

    friend bool operator==(
        const GroundAnimationControlHandoff&,
        const GroundAnimationControlHandoff&) = default;
};

enum class GroundPhysicsAction : std::uint8_t {
    None,
    ResetForNonGround,
    RequestGroundFromState7,
    StopAndRequestState7,
    EnterLowSpeedMode,
    EnterHighSpeedMode,
    AdvanceToAnimationMode,
    AdvanceToAnimationComplete,
    ResetToIdleMode,
};

// Sound and dust services Skater_UpdateBrakeMode calls on a brake release.
// They are reported, not performed: the audio and particle services stay
// outside this boundary.
struct GroundBrakeServiceRequests final {
    // Mode 1 -> 3 while braking: draw Rnd(0x80) (FUN_004c9340), then
    // SFX_PlayX(ground_brake_release_sound(roll), 0x2000, 0).
    bool random_release_sound{};
    // Mode 5 -> 6 while braking past animation frame 4: SFX_PlayX(2, 0x2000, 0).
    bool fixed_release_sound{};
    std::uint16_t fixed_sound_id{};
    // Mode 1 -> 3 while braking: the skater's embedded dust emitter
    // (+0x2c14, dust.cpp FUN_00431260) starts dust type 2 with argument +0x30b0.
    bool dust{};
    std::int32_t dust_type{};

    friend bool operator==(
        const GroundBrakeServiceRequests&,
        const GroundBrakeServiceRequests&) = default;
};

// Sound chosen by the mode 1 -> 3 release from its Rnd(0x80) draw.
[[nodiscard]] constexpr std::uint16_t ground_brake_release_sound(
    std::int32_t roll) noexcept {
    return roll < 0x40 ? 0xb1 : 0xb0;
}

struct GroundPhysicsResult final {
    FixedPosition response{};
    std::int32_t ground_update_state{};
    std::int32_t speed_metric{};
    std::int32_t speed_threshold{};
    bool response_decelerated{};
    bool response_stopped{};
    bool cooldown_written{};
    std::int32_t cooldown_value{};
    bool physics_state_requested{};
    std::int32_t requested_physics_state{};
    std::uint32_t requested_physics_reason{};
    bool animation_transition{};
    GroundPhysicsAction action{GroundPhysicsAction::None};
    GroundAnimationControlHandoff animation_handoff{};
    // The animation wrapper call the retail branch makes, as data (the
    // trailing source-line tag each wrapper ignores is not kept):
    //   mode 0 -> 5: Start(0x31); mode 0 -> 1: Start(0x83), both skipped
    //     while animation 0x5d or 0x5f is playing;
    //   mode 3 -> 4: Range(8, 0x13, 0), or Full(8, 0x13, 0x1a, 0x13) while
    //     +0x2dd8 is set; mode 6 -> 4: Start(0).
    GroundAnimationRequest animation_request{};
    GroundBrakeServiceRequests services{};

    friend bool operator==(
        const GroundPhysicsResult&,
        const GroundPhysicsResult&) = default;
};

// Port of retail Skater_UpdateBrakeMode (FUN_0049df00): the exact
// threshold/deceleration branch, the +0x2df8 brake-mode transitions, and the
// animation/sound/dust requests as data. Dispatching those requests stays
// with the animation, audio and particle services.
[[nodiscard]] GroundPhysicsResult update_ground_physics(
    const GroundPhysicsInput& input) noexcept;

} // namespace opentony::runtime
