#include "player_state.hpp"

#include "tests/test_check.hpp"
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    opentony::runtime::PlayerState player({100, 200, 300});
    player.set_collision_response({1, 2, 3});
    player.set_physics_state(3);
    player.set_ground_update_state(1);
    player.begin_physics_frame();
    CHECK(player.previous_position() == player.position());

    const opentony::runtime::PositionCommitResult result = player.commit_position(
        {200, 400, 600},
        [](const opentony::runtime::FixedPosition& candidate) {
            return candidate[0] > 150;
        });
    CHECK(result.position == opentony::runtime::FixedPosition({100, 400, 600}));
    CHECK(player.position() == result.position);
    CHECK(player.previous_position() == opentony::runtime::FixedPosition({100, 200, 300}));
    CHECK(player.collision_response() == opentony::runtime::FixedPosition({1, 2, 3}));

    player.set_motion_correction({4, 5, 6});
    player.set_air_motion({7, 8, 9});
    player.apply_restart({100, 200, 300}, 0x44556677, 0x8899);
    CHECK(player.position() == opentony::runtime::FixedPosition({100, 200, 300}));
    CHECK(player.previous_position() == player.position());
    CHECK(player.collision_response() == opentony::runtime::FixedPosition({0, 0, 0}));
    CHECK(player.motion_correction() == opentony::runtime::FixedPosition({0, 0, 0}));
    CHECK(player.air_motion() == opentony::runtime::FixedPosition({0, 0, 0}));
    CHECK(player.restart_auxiliary() == 0x44556677);
    CHECK(player.restart_auxiliary_word() == 0x8899);
    CHECK(opentony::runtime::retail_restart_angle12(0x08000000) == 0);
    CHECK(player.orientation() == opentony::runtime::q12_restart_matrix(
        0x44556677));
    CHECK(player.physics_state() == 3);
    CHECK(player.ground_update_state() == 1);
    opentony::runtime::InputState input;
    input.begin_frame(opentony::runtime::movement_bit(
        opentony::runtime::MovementAction::Left));
    const auto turn = player.update_ground_turn(input);
    CHECK(turn.accumulator == -0x3c00);
    CHECK(player.turn_mirror() == -0x3c00);
    // The accumulator producer runs before B010. Retail defers the -4
    // table-unit yaw until the 0x00496360 pre-query phase.
    const auto pre_turn_orientation = player.orientation();
    player.apply_ground_turn_velocity_phase();
    CHECK(player.orientation() != pre_turn_orientation);
    CHECK(player.orientation().at(1, 1) == -0x1000);
    CHECK(player.retail_basis().at_310c[1] == -0x1000);

    opentony::runtime::PlayerState air_orientation;
    air_orientation.set_physics_state(1);
    opentony::runtime::InputState up_input;
    up_input.begin_frame(opentony::runtime::movement_bit(
        opentony::runtime::MovementAction::Up));
    const auto air_angle = air_orientation.compute_in_air_orientation_angle(
        up_input,
        640,
        true);
    CHECK(air_angle.has_value());
    CHECK(*air_angle == -50);

    const opentony::runtime::Q12Matrix3 captured_orientation{
        {-4096, 0, -4, 0, -4091, -191, 4, 191, -4091}};
    air_orientation.set_orientation(captured_orientation);
    const auto pivot_delta = air_orientation.apply_in_air_orientation_pivot(-50);
    CHECK(pivot_delta == opentony::runtime::FixedPosition({0, -1890, -21840}));
    const opentony::runtime::Q12Matrix3 expected_air_orientation{
        {-4096, 0, -4, 0, -4064, -504, 4, 503, -4064}};
    CHECK(air_orientation.orientation() == expected_air_orientation);

    const opentony::runtime::AirOrientationTurnConfig air_turn_config{
        80,
        0,
        0x100,
        true,
    };
    CHECK(opentony::runtime::compute_air_orientation_turn_angle(
        45 * 0x1000,
        air_turn_config) == 42);
    opentony::runtime::PlayerState air_turn;
    air_turn.set_physics_state(1);
    opentony::runtime::InputState right_input;
    right_input.begin_frame(opentony::runtime::movement_bit(
        opentony::runtime::MovementAction::Right));
    air_turn.update_in_air_orientation_accumulator(right_input, 0x100);
    CHECK(air_turn.turn_accumulator() == 0xa000);
    CHECK(air_turn.turn_mirror() == 0xa000);

    // State-1 analog lean from the retail Warehouse lean trace (build/p41
    // lean-join, DAT_0056865c = 640): lean 0x20 targets (0xa0000 * 0x20) >> 7
    // = 163840 through the shared tail with the in-air 0xa000 step.
    {
        opentony::runtime::PlayerState lean_air;
        lean_air.set_physics_state(1);
        opentony::runtime::InputState no_direction;
        no_direction.begin_frame(0);
        lean_air.set_turn_accumulator(46080); // retail frame 38 entry
        lean_air.update_in_air_orientation_accumulator(no_direction, 640, 0x20);
        CHECK(lean_air.turn_accumulator() == 163840);
        CHECK(lean_air.turn_mirror() == 163840);
        lean_air.set_turn_accumulator(0); // retail frame 46, after landing reset
        lean_air.update_in_air_orientation_accumulator(no_direction, 640, 0x20);
        CHECK(lean_air.turn_accumulator() == 153600);
        // |lean| < 0x14 without a direction clears the accumulator first.
        lean_air.set_turn_accumulator(0x50000);
        lean_air.update_in_air_orientation_accumulator(no_direction, 0x100, 0x13);
        CHECK(lean_air.turn_accumulator() == 0);
        // 0x14 <= |lean| < 0x1a keeps it for the release decay (>> 2).
        lean_air.set_turn_accumulator(0x50000);
        lean_air.update_in_air_orientation_accumulator(no_direction, 0x100, -0x14);
        CHECK(lean_air.turn_accumulator() == 0x3c000);
        // A held direction still wins over the lean.
        lean_air.set_turn_accumulator(0);
        lean_air.update_in_air_orientation_accumulator(right_input, 0x100, -0x40);
        CHECK(lean_air.turn_accumulator() == 0xa000);
    }

    // FUN_00493370's remaining non-grounded controls (0x00493899..0x004939b9).
    {
        opentony::runtime::InputState no_direction;
        no_direction.begin_frame(0);
        opentony::runtime::InputState left_input;
        left_input.begin_frame(opentony::runtime::movement_bit(
            opentony::runtime::MovementAction::Left));
        opentony::runtime::InAirTurnControls clocked{};

        // The +0x40 record steers left and, as controller 0's record, also
        // selects the 0x64000 step; two frames reach the 0xa0000 clamp.
        opentony::runtime::PlayerState fast;
        fast.set_physics_state(1);
        opentony::runtime::InAirTurnControls nollie{};
        nollie.nollie_held = true;
        nollie.fast_step = true;
        fast.update_in_air_orientation_accumulator(no_direction, 0x100, 0, nollie);
        CHECK(fast.turn_accumulator() == -0x64000);
        fast.update_in_air_orientation_accumulator(no_direction, 0x100, 0, nollie);
        CHECK(fast.turn_accumulator() == -0xa0000);

        // With the DAT_0056b7f0 cheat the records no longer steer, and the
        // branch clears the +0x2c80 action gate.
        opentony::runtime::PlayerState cheat;
        cheat.set_physics_state(1);
        cheat.set_surface_response_action_gate(1);
        opentony::runtime::InAirTurnControls cheat_controls = nollie;
        cheat_controls.fast_step = false;
        cheat_controls.air_control_enabled = true;
        cheat.update_in_air_orientation_accumulator(no_direction, 0x100, 0, cheat_controls);
        CHECK(cheat.turn_accumulator() == 0);
        CHECK(!cheat.surface_response_action_gate_active());

        // A nonzero +0x2c80 gate discards every direction.
        opentony::runtime::PlayerState gated;
        gated.set_physics_state(1);
        gated.set_surface_response_action_gate(1);
        gated.set_turn_accumulator(0x50000);
        gated.update_in_air_orientation_accumulator(left_input, 0x100, 0, clocked);
        CHECK(gated.turn_accumulator() == 0);

        // Re-press window: a direction within 8 AnimationClock ticks of the
        // last direction-free frame is ignored; the stamp does not move while
        // the press is held.
        opentony::runtime::PlayerState window;
        window.set_physics_state(1);
        clocked.animation_clock = 100;
        window.update_in_air_orientation_accumulator(no_direction, 0x100, 0, clocked);
        CHECK(window.air_turn_release_clock() == 100);
        clocked.animation_clock = 107;
        window.update_in_air_orientation_accumulator(left_input, 0x100, 0, clocked);
        CHECK(window.turn_accumulator() == 0);
        CHECK(window.air_turn_release_clock() == 100);
        clocked.animation_clock = 108;
        window.update_in_air_orientation_accumulator(left_input, 0x100, 0, clocked);
        CHECK(window.turn_accumulator() == -0xa000);

        // The records are ORed in after the window and bypass it.
        opentony::runtime::PlayerState record_window;
        record_window.set_physics_state(1);
        clocked.animation_clock = 200;
        record_window.update_in_air_orientation_accumulator(no_direction, 0x100, 0, clocked);
        opentony::runtime::InAirTurnControls clocked_nollie = clocked;
        clocked_nollie.animation_clock = 201;
        clocked_nollie.nollie_held = true;
        record_window.update_in_air_orientation_accumulator(
            no_direction, 0x100, 0, clocked_nollie);
        CHECK(record_window.turn_accumulator() == -0xa000);

        // A spin in progress (+0x2e80) clears +0x3144 and skips the tail,
        // including the +0x3148 mirror.
        opentony::runtime::PlayerState spinning;
        spinning.set_physics_state(1);
        spinning.set_turn_accumulator(0x5000);
        spinning.set_surface_response_spin_phase(1);
        const std::int32_t mirror_before = spinning.turn_mirror();
        spinning.update_in_air_orientation_accumulator(right_input, 0x100);
        CHECK(spinning.turn_accumulator() == 0);
        CHECK(spinning.turn_mirror() == mirror_before); // no 0x00493cdd mirror
        CHECK(spinning.surface_response_spin_active());

        // Live retail frames (build/p42 air-controls run: Warehouse, analog
        // lean 0x20, DAT_0056865c = 640, controller records written at
        // 0x00493370 entry). Entry state in, exit state out.
        struct AirControlsFrame {
            int frame;
            std::int32_t turn_in;
            std::int32_t stamp_in;
            std::int32_t clock;
            bool nollie;
            bool left;
            std::int32_t turn_out;
            std::int32_t stamp_out;
        };
        const AirControlsFrame live_frames[] = {
            // NOLLIE: fast step to the -0xa0000 clamp, bypassing the window.
            {47, 163840, 224, 229, true, false, -655360, 224},
            // No direction: stamp the clock, then the analog lean target.
            {49, -655360, 224, 239, false, false, -296960, 239},
            // Left 5 ticks after the stamp: ignored, the lean target wins.
            {51, -51200, 244, 249, false, true, 117760, 244},
            // Left 10 ticks after: one 0xa000 step at dt 640.
            {52, 117760, 244, 254, false, true, 15360, 244},
        };
        for (const AirControlsFrame& live : live_frames) {
            opentony::runtime::PlayerState air;
            air.set_physics_state(1);
            air.set_turn_accumulator(live.turn_in);
            air.set_air_turn_release_clock(live.stamp_in);
            opentony::runtime::InAirTurnControls controls{};
            controls.nollie_held = live.nollie;
            controls.fast_step = live.nollie;
            controls.animation_clock = live.clock;
            air.update_in_air_orientation_accumulator(
                live.left ? left_input : no_direction, 640, 0x20, controls);
            CHECK(air.turn_accumulator() == live.turn_out);
            CHECK(air.turn_mirror() == live.turn_out);
            CHECK(air.air_turn_release_clock() == live.stamp_out);
        }

        // The ground branch zeroes both the spin count and the stamp.
        spinning.set_physics_state(0);
        spinning.update_in_air_orientation_accumulator(right_input, 0x100);
        CHECK(!spinning.surface_response_spin_active());
        CHECK(spinning.air_turn_release_clock() == 0);

        // Every non-grounded state runs the branch, not only 1/2; outside
        // 1/2 the spin count is zeroed first.
        opentony::runtime::PlayerState rail;
        rail.set_physics_state(4);
        rail.set_surface_response_spin_phase(1);
        rail.update_in_air_orientation_accumulator(left_input, 0x100);
        CHECK(rail.turn_accumulator() == -0xa000);
        CHECK(!rail.surface_response_spin_active());
    }
    CHECK(air_turn.apply_in_air_orientation_turn(
        opentony::runtime::AirOrientationTurnConfig{
            80,
            0,
            0x100,
            true,
        }) == 9);
    air_turn.set_orientation(opentony::runtime::q12_identity_matrix());
    air_turn.set_turn_accumulator(45 * 0x1000);
    CHECK(air_turn.apply_in_air_orientation_turn(air_turn_config) == 42);
    CHECK(air_turn.orientation()
        == opentony::runtime::q12_yaw_matrix(-42));

    player.set_collision_response({0x1000, 0x2000, 0});
    CHECK(player.remove_collision_normal_component({0x1000, 0, 0}) == 0x1000);
    CHECK(player.collision_response() == opentony::runtime::FixedPosition({0, 0x2000, 0}));
    player.set_collision_response({-0x1000, 0, 0});
    const auto response = player.apply_collision_response({0x1000, 0, 0});
    CHECK(response.dot == -0x1000);
    CHECK(response.adjusted);
    CHECK(player.collision_response() == opentony::runtime::FixedPosition({0xcd, 0, 0}));

    opentony::runtime::PlayerState collision_orientation;
    collision_orientation.set_air_motion({0, 0x1000, 0});
    const auto orientation_result =
        collision_orientation.apply_collision_orientation({0, 0, 0x1000}, 0x200);
    CHECK(orientation_result.adjusted);
    CHECK(orientation_result.forward_dot == -0x1000);
    CHECK(orientation_result.lateral_dot == 0);
    CHECK(collision_orientation.air_motion()
        == opentony::runtime::FixedPosition({0, 0x1000, 0}));
    CHECK(collision_orientation.orientation()
        != opentony::runtime::q12_identity_matrix());

    player.apply_bouncy_platform_response(2, {100, 0, -200}, 0);
    CHECK(player.collision_response()
        == opentony::runtime::FixedPosition({150, -0x50000, -300}));
    player.apply_bouncy_platform_response(1, {100, 0, -200}, 0x20);
    CHECK(player.collision_response()
        == opentony::runtime::FixedPosition({100, -0x20000, -200}));
    player.apply_bouncy_platform_response(1, {100, 0, -200}, 0x28);
    CHECK(player.collision_response()
        == opentony::runtime::FixedPosition({100, -0x40000, -200}));

    opentony::runtime::PlayerState ollie_player;
    ollie_player.set_physics_state(0);
    opentony::runtime::InputState ollie_input;
    ollie_input.begin_frame(opentony::runtime::kKickActionBit);
    const auto charging = ollie_player.run_ollie_prephysics(ollie_input);
    CHECK(charging.event == opentony::runtime::OlliePrePhysicsEvent::Charging);
    CHECK(charging.latch_set);
    CHECK(charging.pending_set);
    CHECK(charging.animation_request_issued);
    CHECK(charging.animation_request_id == 8);
    CHECK(charging.animation_request_start == 0);
    CHECK(charging.animation_request_end == 0x1a);
    CHECK(charging.animation_request_alternate == 0x13);
    CHECK(ollie_player.ollie().charge == 1);

    ollie_input.begin_frame(0);
    const auto launched = ollie_player.run_ollie_prephysics(ollie_input);
    CHECK(launched.event == opentony::runtime::OlliePrePhysicsEvent::Launched);
    CHECK(launched.state_requested);
    CHECK(launched.requested_state == 1);
    CHECK(launched.request_reason == opentony::runtime::kOrdinaryLaunchReason);
    CHECK(ollie_player.physics_state() == 1);
    CHECK(ollie_player.last_state_request().from == 0);
    CHECK(ollie_player.last_state_request().to == 1);
    CHECK(ollie_player.ollie().launch_count == 1);

    opentony::runtime::PlayerState air_hold;
    air_hold.set_physics_state(1);
    air_hold.set_collision_response({10, 20, 30});
    air_hold.set_motion_correction({40, 50, 60});
    opentony::runtime::InputState jump_input;
    jump_input.begin_frame(opentony::runtime::kJumpActionBit);
    jump_input.begin_frame(opentony::runtime::kJumpActionBit);
    jump_input.begin_frame(opentony::runtime::kJumpActionBit);
    CHECK(air_hold.apply_in_air_jump_hold_effect(jump_input));
    CHECK(air_hold.collision_response()[1] == 0);
    CHECK(air_hold.motion_correction()[1] == 0);

    opentony::runtime::PlayerState ground_player;
    ground_player.set_collision_response({0x1000, 0, 0});
    ground_player.clear_motion_correction();
    ground_player.prepare_ground_basis_correction(false);
    CHECK(ground_player.collision_response()
        == opentony::runtime::FixedPosition({0, 0, 0}));
    CHECK(ground_player.motion_correction()
        == opentony::runtime::FixedPosition({0, 0, 0}));

    ground_player.set_collision_response({0, 0, 0x1000});
    ground_player.clear_motion_correction();
    ground_player.prepare_ground_basis_correction(true, 0x100);
    CHECK(ground_player.motion_correction()[2] == -8);
    ground_player.clear_motion_correction();
    ground_player.set_collision_response({0, 0, 0x1000});
    ground_player.prepare_ground_basis_correction(true, 0x280);
    CHECK(ground_player.motion_correction()[2] == -20);

    opentony::runtime::PlayerState blocked_player;
    blocked_player.set_control_blocked(true);
    blocked_player.set_collision_response({100, -30, 50});
    blocked_player.set_motion_correction({1, 2, 3});
    blocked_player.set_control_blocked_velocity_decay_divisor(4);
    blocked_player.apply_control_blocked_reset();
    CHECK(blocked_player.collision_response()
        == opentony::runtime::FixedPosition({75, 0, 38}));
    CHECK(blocked_player.motion_correction()
        == opentony::runtime::FixedPosition({0, 2, 0}));

    opentony::runtime::PlayerState integrated;
    integrated.set_collision_response({100, 200, 300});
    integrated.set_motion_correction({0x1000, 0, 0});
    integrated.integrate_position();
    // dt=1: velocity contributes directly; the frame-scaled correction is
    // then divided by two by the retail helper sequence.
    CHECK(integrated.position()
        == opentony::runtime::FixedPosition({2148, 200, 300}));

    opentony::runtime::PlayerState action_player;
    const std::vector<std::uint8_t> response_command{
        opentony::runtime::kSetResponseVectorOpcode,
        0x03, 0x00,
        0xfe, 0xff,
        0x01, 0x00,
    };
    std::size_t action_cursor = 0;
    const auto action_result = action_player.dispatch_action_command(
        response_command,
        action_cursor);
    CHECK(action_result.recognized);
    CHECK(action_cursor == response_command.size());
    CHECK(action_player.collision_response()
        == opentony::runtime::FixedPosition({
            0x3000,
            -0x2000,
            0x1000,
        }));

    // Retail +0x3200: once FUN_004957c0's endpoint write raises the gate,
    // FUN_00496060 stores its proposal directly; without it the same blocked
    // probe falls back to the current position.
    const opentony::runtime::PositionCollisionProbe always_blocked =
        [](const opentony::runtime::FixedPosition&) { return true; };
    opentony::runtime::PlayerState gated_player({0, 0, 0});
    CHECK(!gated_player.position_commit_bypass());
    const auto gated_blocked =
        gated_player.commit_position({10, 20, 30}, always_blocked);
    CHECK(gated_blocked.blocked);
    CHECK(gated_player.position() == opentony::runtime::FixedPosition({0, 0, 0}));
    gated_player.set_position_commit_bypass(true);
    const auto gated_direct =
        gated_player.commit_position({10, 20, 30}, always_blocked);
    CHECK(!gated_direct.collided);
    CHECK(gated_direct.selected_candidate == static_cast<std::uint8_t>(
        opentony::runtime::PositionCommitCandidate::Direct));
    CHECK(gated_player.position() == opentony::runtime::FixedPosition({10, 20, 30}));
    std::cout << "Player state tests passed\n";
}
