#include "collision_recovery.hpp"

#include "player_state.hpp"
#include "tests/test_check.hpp"

#include <iostream>

namespace {

using opentony::runtime::FixedPosition;
using opentony::runtime::PlayerState;
using opentony::runtime::PositionCollisionHit;

PositionCollisionHit make_hit(
    FixedPosition position,
    std::uint32_t raw_collision_word = 0) {
    PositionCollisionHit hit{};
    hit.position = position;
    hit.normal = {0, 0x1000, 0};
    hit.raw_collision_word = raw_collision_word;
    return hit;
}

} // namespace

int main() {
    using opentony::runtime::apply_outer_floor_recovery;

    PlayerState upward_player({100, 200, 300});
    upward_player.set_outer_floor_reference_position({10, 20, 30});
    bool service_called = false;
    const auto upward_result = apply_outer_floor_recovery(
        upward_player,
        upward_player.position(),
        [](const FixedPosition&, const FixedPosition&)
            -> std::optional<PositionCollisionHit> {
            return make_hit({100, 1000, 300});
        },
        false,
        [&service_called](const PositionCollisionHit& hit) {
            service_called = hit.position[1] == 1000;
        });
    CHECK(upward_result.upward_hit);
    CHECK(upward_result.external_service_requested);
    CHECK(service_called);
    CHECK(upward_player.outer_floor_distance() == 800 - 0x1e000);
    CHECK(upward_player.position() == FixedPosition({100, 200, 300}));

    PlayerState restart_player({100, 200, 300});
    restart_player.set_outer_floor_reference_position({10, 20, 30});
    int restart_calls = 0;
    const auto restart_result = apply_outer_floor_recovery(
        restart_player,
        restart_player.position(),
        [&restart_calls](const FixedPosition&, const FixedPosition& end)
            -> std::optional<PositionCollisionHit> {
            ++restart_calls;
            if (restart_calls == 1) {
                return make_hit({100, 1000, 300});
            }
            CHECK(end[1] == 200);
            return make_hit({100, 150, 300});
        },
        true);
    CHECK(restart_result.restart_probe_hit);
    CHECK(restart_result.response_yaw_applied);
    CHECK(restart_player.position() == FixedPosition({10, 20, 30}));
    CHECK(restart_player.previous_position() == FixedPosition({10, 20, 30}));

    PlayerState short_player({100, 200, 300});
    int short_calls = 0;
    const auto short_result = apply_outer_floor_recovery(
        short_player,
        short_player.position(),
        [&short_calls](const FixedPosition&, const FixedPosition& end)
            -> std::optional<PositionCollisionHit> {
            ++short_calls;
            if (short_calls == 2) {
                CHECK(end[1] == 200 + 0x64000);
                return make_hit({700, 500, 900});
            }
            return std::nullopt;
        },
        false);
    CHECK(!short_result.upward_hit);
    CHECK(short_result.short_recovery_hit);
    CHECK(short_player.position() == FixedPosition({700, 500 - 0x1e000, 900}));
    CHECK(short_player.previous_position() == short_player.position());

    PlayerState fallback_player({100, 200, 300});
    fallback_player.set_outer_floor_reference_position({10, 20, 30});
    fallback_player.set_collision_response({1, 2, 100});
    const auto fallback_result = apply_outer_floor_recovery(
        fallback_player,
        fallback_player.position(),
        [](const FixedPosition&, const FixedPosition&)
            -> std::optional<PositionCollisionHit> {
            return std::nullopt;
        },
        false);
    CHECK(fallback_result.z_recovery_hit == false);
    CHECK(fallback_player.position() == FixedPosition({10, 200, 30}));
    CHECK(fallback_player.previous_position() == fallback_player.position());
    CHECK(fallback_player.collision_response() == FixedPosition({-50, 2, -25}));

    // FUN_00496550's movement-hit branches (0x0049696b..0x00496db8), with the
    // long idle run's frame-385 wall and the floor around it as fixtures.
    {
        using opentony::runtime::GroundMovementHitBranch;
        using opentony::runtime::classify_ground_movement_hit;
        const auto hit_with = [](FixedPosition normal,
                                 bool bit6,
                                 bool bit7_clear,
                                 bool bit8_clear,
                                 bool face80) {
            PositionCollisionHit hit{};
            hit.normal = normal;
            hit.surface_bit_6 = bit6;
            hit.surface_bit_7_clear = bit7_clear;
            hit.surface_bit_8_clear = bit8_clear;
            hit.face_flag_80 = face80;
            return hit;
        };
        const FixedPosition floor_80{240, -3959, 1024};
        // Surface word 0x1800 (both inverse bits set), Face80 set, a steep
        // wall facing slightly down: dot with +0x80 is 1748 < 0x800 and -n.y
        // (895) does not beat |n.x| (1199).
        const PositionCollisionHit wall =
            hit_with({-1199, -895, 3813}, false, true, true, true);
        CHECK(classify_ground_movement_hit(wall, floor_80)
            == GroundMovementHitBranch::Wall);
        CHECK(opentony::runtime::ground_movement_hit_calls_response(
            GroundMovementHitBranch::Wall));
        // The same floor as +0x80 is the same-surface branch.
        CHECK(classify_ground_movement_hit(
                  hit_with({1090, -3948, 0}, false, true, true, true), floor_80)
            == GroundMovementHitBranch::SameSurface);
        // A floor that differs from +0x80 by more than the 0x800 window.
        CHECK(classify_ground_movement_hit(
                  hit_with({0, -0x1000, 0}, false, true, false, false),
                  FixedPosition{0x1000, 0, 0})
            == GroundMovementHitBranch::Floor);
        // Both inverse bits and Surface40 clear: the direct branch, whatever
        // the geometry.
        CHECK(classify_ground_movement_hit(
                  hit_with({0, -0x1000, 0}, false, false, false, false), floor_80)
            == GroundMovementHitBranch::Direct);
        // Surface40 without Face80 skips straight to the support sweep; with
        // Face80 it is classified like the others.
        CHECK(classify_ground_movement_hit(
                  hit_with({-1199, -895, 3813}, true, true, true, false), floor_80)
            == GroundMovementHitBranch::Skipped);
        CHECK(classify_ground_movement_hit(
                  hit_with({-1199, -895, 3813}, true, true, true, true), floor_80)
            == GroundMovementHitBranch::Wall);
        CHECK(!opentony::runtime::ground_movement_hit_calls_response(
            GroundMovementHitBranch::Floor));

        // FUN_004ca8f0 on the frame-385 response: [2, -12, -46] after the
        // signed >> 12, 4 + 144 + 2116 = 2264, isqrt 47.
        CHECK(opentony::runtime::retail_quantized_vector_magnitude(
                  FixedPosition{8716, -46675, -185839}) == 47);
        // Each square saturates at 0x7fff.
        CHECK(opentony::runtime::retail_quantized_vector_magnitude(
                  FixedPosition{0x7fff000, 0, 0}) == 181);
    }

    std::cout << "Collision recovery tests passed\n";
}
