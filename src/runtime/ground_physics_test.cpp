#include "ground_physics.hpp"

#include "tests/test_check.hpp"
#include <iostream>

using opentony::runtime::FixedPosition;
using opentony::runtime::GroundPhysicsAction;
using opentony::runtime::GroundPhysicsInput;
using opentony::runtime::update_ground_physics;

int main() {
    const auto non_ground = update_ground_physics(GroundPhysicsInput{
        FixedPosition{100, 0, 0},
        3,
        1,
        -0x1000,
        0,
        0,
        0,
        0,
        true,
        false,
        false,
        false,
        false,
    });
    CHECK(non_ground.ground_update_state == 0);
    CHECK(non_ground.action == GroundPhysicsAction::ResetForNonGround);
    CHECK(!non_ground.physics_state_requested);

    const auto state7_locked = update_ground_physics(GroundPhysicsInput{
        {},
        7,
        4,
        -0x1000,
        0,
        0,
        0,
        0,
        true,
        false,
        true,
        false,
        false,
    });
    CHECK(state7_locked.ground_update_state == 0);
    CHECK(state7_locked.physics_state_requested);
    CHECK(state7_locked.requested_physics_state == 0);
    CHECK(state7_locked.requested_physics_reason == 0x2c0f);

    const auto state7_surface = update_ground_physics(GroundPhysicsInput{
        {},
        7,
        4,
        -0x1000,
        0,
        0,
        0,
        0,
        false,
        false,
        false,
        false,
        false,
    });
    CHECK(state7_surface.physics_state_requested);
    CHECK(state7_surface.requested_physics_reason == 0x2c21);
    CHECK(state7_surface.ground_update_state == 0);
    CHECK(state7_surface.action == GroundPhysicsAction::ResetToIdleMode);

    const auto high_speed = update_ground_physics(GroundPhysicsInput{
        FixedPosition{0x80000, 0, 0},
        0,
        0,
        -0x1000,
        0,
        0,
        0,
        0,
        true,
        false,
        false,
        false,
        false,
    });
    CHECK(high_speed.response == FixedPosition({0x78000, 0, 0}));
    CHECK(high_speed.ground_update_state == 1);
    CHECK(high_speed.action == GroundPhysicsAction::EnterHighSpeedMode);
    CHECK(high_speed.cooldown_written);
    CHECK(high_speed.cooldown_value == 2);

    const auto low_speed = update_ground_physics(GroundPhysicsInput{
        FixedPosition{0x10000, 0, 0},
        0,
        0,
        -0x1000,
        0,
        0,
        0,
        0,
        true,
        false,
        false,
        false,
        false,
    });
    CHECK(low_speed.ground_update_state == 5);
    CHECK(low_speed.action == GroundPhysicsAction::EnterLowSpeedMode);

    const auto advance_three = update_ground_physics(GroundPhysicsInput{
        {},
        7,
        1,
        -0x1000,
        0,
        0,
        0,
        0,
        true,
        false,
        false,
        false,
        false,
    });
    CHECK(advance_three.ground_update_state == 3);
    CHECK(advance_three.action == GroundPhysicsAction::AdvanceToAnimationMode);
    CHECK(advance_three.cooldown_written);

    // FUN_0049df00 case 1 uses a strict comparison against
    // animation_frame * 0x1000 + slope_threshold. State 7 skips the
    // state-0 stop producer so this fixture isolates the mode-1 branch.
    GroundPhysicsInput mode_one_equal{
        FixedPosition{0xc000, 0, 0},
        7,
        1,
        -0x1000,
        2,
        0,
        0,
        0,
        true,
        false,
        false,
        false,
        false,
        0x100,
        0x82,
        0x1234,
    };
    const auto mode_one_equal_result =
        update_ground_physics(mode_one_equal);
    CHECK(mode_one_equal_result.speed_metric == 0xc000);
    CHECK(mode_one_equal_result.speed_threshold == 0xa000);
    CHECK(mode_one_equal_result.ground_update_state == 1);
    CHECK(!mode_one_equal_result.animation_handoff.applied);
    CHECK(mode_one_equal_result.cooldown_written);
    CHECK(mode_one_equal_result.cooldown_value == 2);

    mode_one_equal.response = FixedPosition{0xbfc0, 0, 0};
    const auto mode_one_below_result =
        update_ground_physics(mode_one_equal);
    CHECK(mode_one_below_result.speed_metric == 0xbfc0);
    CHECK(mode_one_below_result.ground_update_state == 3);
    CHECK(mode_one_below_result.action
        == GroundPhysicsAction::AdvanceToAnimationMode);
    CHECK(mode_one_below_result.animation_transition);
    CHECK(mode_one_below_result.animation_handoff.applied);
    CHECK(mode_one_below_result.animation_handoff.animation_finished == 0);
    CHECK(mode_one_below_result.animation_handoff.playback_direction == -1);
    CHECK(mode_one_below_result.animation_handoff.playback_endpoint == 0x34);
    CHECK(mode_one_below_result.animation_handoff.original_start_frame == -126);
    CHECK(mode_one_below_result.cooldown_written);
    CHECK(mode_one_below_result.cooldown_value == 2);

    // Surface failure takes the other side of the same OR predicate, still
    // performs the raw animation handoff, but leaves the audio/script service
    // disabled (animation_transition is false).
    mode_one_equal.response = FixedPosition{0x80000, 0, 0};
    mode_one_equal.brake_requested = false;
    const auto mode_one_surface_result =
        update_ground_physics(mode_one_equal);
    CHECK(mode_one_surface_result.ground_update_state == 3);
    CHECK(!mode_one_surface_result.animation_transition);
    CHECK(mode_one_surface_result.animation_handoff.applied);
    CHECK(mode_one_surface_result.physics_state_requested);
    CHECK(mode_one_surface_result.requested_physics_reason == 0x2c21);

    const auto advance_four = update_ground_physics(GroundPhysicsInput{
        {},
        7,
        3,
        -0x1000,
        0,
        0,
        0,
        0,
        true,
        false,
        false,
        false,
        true,
    });
    CHECK(advance_four.ground_update_state == 4);
    CHECK(advance_four.action == GroundPhysicsAction::AdvanceToAnimationComplete);

    // The brake input test at 0x0049df4e: Down without Left/Right, or the
    // vertical lean byte signed-positive with no horizontal lean.
    using opentony::runtime::ground_brake_requested;
    CHECK(ground_brake_requested(true, false, false, 0, 0));
    CHECK(!ground_brake_requested(true, true, false, 0, 0));
    CHECK(!ground_brake_requested(true, false, true, 0, 0));
    CHECK(!ground_brake_requested(false, false, false, 0, 0));
    CHECK(ground_brake_requested(false, false, false, 1, 0));
    CHECK(ground_brake_requested(true, true, true, 0x7f, 0));
    CHECK(!ground_brake_requested(false, false, false, 1, 1));
    CHECK(!ground_brake_requested(false, false, false, -1, 0));
    CHECK(!ground_brake_requested(true, true, false, 0x40, -3));

    using opentony::runtime::GroundAnimationRequestWrapper;
    using opentony::runtime::ground_brake_release_sound;
    CHECK(ground_brake_release_sound(0) == 0xb1);
    CHECK(ground_brake_release_sound(0x3f) == 0xb1);
    CHECK(ground_brake_release_sound(0x40) == 0xb0);
    CHECK(ground_brake_release_sound(0x7f) == 0xb0);

    // Mode 0 while braking above the threshold: low speed (< 0x30000, no
    // +0x2dd8) starts animation 0x31, otherwise 0x83; neither while the
    // brake pose 0x5d/0x5f already plays.
    GroundPhysicsInput mode_zero{};
    mode_zero.slope_normal_y_q12 = -0x1000;
    mode_zero.brake_requested = true;
    mode_zero.response = FixedPosition{0x20000, 0, 0};
    const auto enter_low = update_ground_physics(mode_zero);
    CHECK(enter_low.ground_update_state == 5);
    CHECK(enter_low.animation_request.issued);
    CHECK(enter_low.animation_request.wrapper == GroundAnimationRequestWrapper::Start);
    CHECK(enter_low.animation_request.animation == 0x31);
    CHECK(enter_low.animation_request.start == 0);
    mode_zero.response = FixedPosition{0x40000, 0, 0};
    const auto enter_high = update_ground_physics(mode_zero);
    CHECK(enter_high.ground_update_state == 1);
    CHECK(enter_high.animation_request.animation == 0x83);
    mode_zero.animation_state = 0x5f;
    const auto enter_high_posed = update_ground_physics(mode_zero);
    CHECK(enter_high_posed.ground_update_state == 1);
    CHECK(!enter_high_posed.animation_request.issued);

    // Mode 1 -> 3 while braking requests the random release sound and the
    // type-2 dust; the released (not braking) side requests neither.
    GroundPhysicsInput mode_one{};
    mode_one.slope_normal_y_q12 = -0x1000;
    mode_one.ground_update_state = 1;
    mode_one.brake_requested = true;
    // Below the 0xa000 slope threshold at frame 0: the brake releases.
    mode_one.response = FixedPosition{0x9000, 0, 0};
    const auto release_one = update_ground_physics(mode_one);
    CHECK(release_one.ground_update_state == 3);
    CHECK(release_one.services.random_release_sound);
    CHECK(release_one.services.dust);
    CHECK(release_one.services.dust_type == 2);
    CHECK(!release_one.services.fixed_release_sound);
    mode_one.brake_requested = false;
    const auto release_one_idle = update_ground_physics(mode_one);
    CHECK(release_one_idle.ground_update_state == 3);
    CHECK(release_one_idle.services == opentony::runtime::GroundBrakeServiceRequests{});

    // Mode 5 -> 6 performs the same raw animation-control exchange as mode
    // 1 -> 3 (0x0049e29c..0x0049e2c2), and plays sound 2 only past frame 4.
    GroundPhysicsInput mode_five{};
    mode_five.slope_normal_y_q12 = -0x1000;
    mode_five.ground_update_state = 5;
    mode_five.brake_requested = true;
    mode_five.response = FixedPosition{0xb000, 0, 0};
    mode_five.animation_frame = 4;
    mode_five.playback_endpoint = 0x82;
    mode_five.original_start_frame = 0x1234;
    const auto release_five_early = update_ground_physics(mode_five);
    CHECK(release_five_early.ground_update_state == 6);
    CHECK(release_five_early.animation_handoff.applied);
    CHECK(release_five_early.animation_handoff.animation_finished == 0);
    CHECK(release_five_early.animation_handoff.playback_direction == -1);
    CHECK(release_five_early.animation_handoff.playback_endpoint == 0x34);
    CHECK(release_five_early.animation_handoff.original_start_frame == -126);
    CHECK(!release_five_early.services.fixed_release_sound);
    mode_five.animation_frame = 5;
    const auto release_five = update_ground_physics(mode_five);
    CHECK(release_five.ground_update_state == 6);
    CHECK(release_five.services.fixed_release_sound);
    CHECK(release_five.services.fixed_sound_id == 2);
    CHECK(!release_five.services.dust);

    // Modes 3 and 6 start the recovery animation once +0x107 is set.
    GroundPhysicsInput mode_three{};
    mode_three.slope_normal_y_q12 = -0x1000;
    mode_three.ground_update_state = 3;
    mode_three.animation_ready = true;
    const auto finish_three = update_ground_physics(mode_three);
    CHECK(finish_three.ground_update_state == 4);
    CHECK(finish_three.animation_request.wrapper == GroundAnimationRequestWrapper::Range);
    CHECK(finish_three.animation_request.animation == 8);
    CHECK(finish_three.animation_request.start == 0x13);
    CHECK(finish_three.animation_request.end == 0);
    CHECK(finish_three.animation_request.alternate == -1);
    mode_three.blocked_or_special = true;
    const auto finish_three_special = update_ground_physics(mode_three);
    CHECK(finish_three_special.animation_request.wrapper == GroundAnimationRequestWrapper::Full);
    CHECK(finish_three_special.animation_request.end == 0x1a);
    CHECK(finish_three_special.animation_request.alternate == 0x13);
    mode_three.animation_ready = false;
    CHECK(!update_ground_physics(mode_three).animation_request.issued);
    GroundPhysicsInput mode_six{};
    mode_six.slope_normal_y_q12 = -0x1000;
    mode_six.ground_update_state = 6;
    mode_six.animation_ready = true;
    const auto finish_six = update_ground_physics(mode_six);
    CHECK(finish_six.ground_update_state == 4);
    CHECK(finish_six.animation_request.wrapper == GroundAnimationRequestWrapper::Start);
    CHECK(finish_six.animation_request.animation == 0);

    // Retail Warehouse brake trace (build/p41 brake-join, 2026-09-28;
    // re/evidence/functions/ground-movement.md "Brake mode 1 joined to its
    // animation, sound and dust requests"). DAT_0056865c = 640 in that
    // headless GDB run; the +0x4c values are the retail entry/exit words.
    {
        GroundPhysicsInput frame22{};
        frame22.response = FixedPosition{427, 0, 287768};
        frame22.slope_normal_y_q12 = -4096;
        frame22.animation_frame = 11;
        frame22.brake_requested = ground_brake_requested(true, false, false, 0, 0);
        frame22.frame_scale_q8 = 640;
        const auto result22 = update_ground_physics(frame22);
        CHECK(result22.ground_update_state == 1);
        CHECK(result22.response == FixedPosition({362, 0, 242806}));
        CHECK(result22.animation_request.wrapper == GroundAnimationRequestWrapper::Start);
        CHECK(result22.animation_request.animation == 0x83);
        CHECK(result22.cooldown_written && result22.cooldown_value == 2);

        GroundPhysicsInput frame28{};
        frame28.response = FixedPosition{97, 10722, 98963};
        frame28.ground_update_state = 1;
        frame28.slope_normal_y_q12 = -4073;
        frame28.animation_frame = 15;
        frame28.animation_state = 131;
        frame28.brake_requested = true;
        frame28.frame_scale_q8 = 640;
        frame28.playback_endpoint = 21;
        frame28.original_start_frame = 0;
        const auto result28 = update_ground_physics(frame28);
        CHECK(result28.ground_update_state == 3);
        CHECK(result28.response == FixedPosition({82, 9047, 83501}));
        CHECK(result28.animation_handoff.applied);
        CHECK(result28.animation_handoff.playback_direction == -1);
        CHECK(result28.animation_handoff.playback_endpoint == 0);
        CHECK(result28.animation_handoff.original_start_frame == 21);
        CHECK(result28.services.random_release_sound);
        CHECK(ground_brake_release_sound(4) == 0xb1); // retail roll 4 -> SFX 177
        CHECK(result28.services.dust && result28.services.dust_type == 2);

        GroundPhysicsInput frame34{};
        frame34.response = FixedPosition{0, 4320, 40097};
        frame34.ground_update_state = 3;
        frame34.slope_normal_y_q12 = -4073;
        frame34.animation_state = 131;
        frame34.brake_requested = true;
        frame34.frame_scale_q8 = 640;
        const auto result34 = update_ground_physics(frame34);
        CHECK(result34.speed_metric < result34.speed_threshold);
        CHECK(result34.response == FixedPosition({0, 0, 0}));
        CHECK(result34.physics_state_requested);
        CHECK(result34.requested_physics_state == 7);
        CHECK(result34.requested_physics_reason == 0x2c56);
        CHECK(result34.ground_update_state == 3);
        CHECK(!result34.animation_request.issued);

        GroundPhysicsInput frame35{};
        frame35.response = FixedPosition{0, 185, 1727};
        frame35.physics_state = 7;
        frame35.ground_update_state = 3;
        frame35.slope_normal_y_q12 = -4073;
        frame35.animation_state = 131;
        frame35.brake_requested = true;
        frame35.animation_ready = true;
        frame35.frame_scale_q8 = 640;
        const auto result35 = update_ground_physics(frame35);
        CHECK(result35.ground_update_state == 4);
        CHECK(result35.response == FixedPosition({0, 185, 1727}));
        CHECK(!result35.physics_state_requested);
        CHECK(result35.animation_request.wrapper == GroundAnimationRequestWrapper::Range);
        CHECK(result35.animation_request.animation == 8);
        CHECK(result35.animation_request.start == 0x13);
        CHECK(result35.animation_request.end == 0);

        GroundPhysicsInput frame96{};
        frame96.response = FixedPosition{0, 185, 1727};
        frame96.physics_state = 7;
        frame96.ground_update_state = 4;
        frame96.slope_normal_y_q12 = -4073;
        frame96.animation_frame = 11;
        frame96.brake_requested = ground_brake_requested(false, false, false, 0, 0);
        frame96.frame_scale_q8 = 640;
        const auto result96 = update_ground_physics(frame96);
        CHECK(result96.ground_update_state == 0);
        CHECK(result96.physics_state_requested);
        CHECK(result96.requested_physics_state == 0);
        CHECK(result96.requested_physics_reason == 0x2c21);
    }

    std::cout << "ground physics tests passed\n";
}
