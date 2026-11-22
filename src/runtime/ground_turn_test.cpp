#include "fixed_matrix.hpp"
#include "ground_turn.hpp"

#include "tests/test_check.hpp"
#include <cstdint>
#include <iostream>

int main() {
    using opentony::runtime::GroundTurn;
    using opentony::runtime::GroundTurnConfig;

    const auto left = GroundTurn::update(0, true, false);
    CHECK(left.accumulator == -0x3c00);
    CHECK(left.mirror == left.accumulator);
    CHECK(left.delta == -0x3c00);

    const auto right = GroundTurn::update(0, false, true);
    CHECK(right.accumulator == 0x3c00);
    const auto both = GroundTurn::update(0, true, true);
    CHECK(both.accumulator == -0x3c00);
    const auto released = GroundTurn::update(-0x4000, false, false);
    CHECK(released.accumulator == -0x3000);
    const auto released_to_zero = GroundTurn::update(-0x400, false, false);
    CHECK(released_to_zero.accumulator == 0);

    const auto alternate = GroundTurn::update(
        0,
        false,
        true,
        GroundTurnConfig{0x100, 1, 0xa0000});
    CHECK(alternate.accumulator == 0x7800);

    const auto extended_profile = GroundTurn::update(
        0,
        false,
        true,
        GroundTurnConfig{0x100, 2, 0xa0000});
    CHECK(extended_profile.accumulator == 0xb400);

    const auto lean_target = GroundTurn::update(
        0,
        false,
        false,
        GroundTurnConfig{0x100, 0, 0x2d000, 2, 0x40});
    CHECK(lean_target.accumulator == 0x7800);
    CHECK(lean_target.policy_changed);

    // +0x31a1 is a signed byte and the branch changes at the inclusive
    // magnitude boundary: 0x19 releases toward zero, while 0x1a targets the
    // analog lean value.
    const auto just_inside_lean = GroundTurn::update(
        0x4000,
        false,
        false,
        GroundTurnConfig{0x100, 0, 0x2d000, 2, 0x19});
    CHECK(just_inside_lean.accumulator == 0x3000);
    CHECK(!just_inside_lean.policy_changed);

    const auto analog_boundary = GroundTurn::update(
        0,
        false,
        false,
        GroundTurnConfig{0x100, 0, 0x2d000, 2, 0x1a});
    CHECK(analog_boundary.accumulator == 0x548b);
    CHECK(analog_boundary.policy_changed);

    const auto negative_lean = GroundTurn::update(
        0,
        false,
        false,
        GroundTurnConfig{0x100, 0, 0x2d000, 2, -0x40});
    CHECK(negative_lean.accumulator == -0x7800);
    CHECK(negative_lean.mirror == negative_lean.accumulator);

    const auto analog_overshoot = GroundTurn::update(
        0x15000,
        false,
        false,
        GroundTurnConfig{0x100, 0, 0x2d000, 2, 0x40});
    CHECK(analog_overshoot.accumulator == 0x16800);
    CHECK(analog_overshoot.policy_changed);

    const auto analog_settled = GroundTurn::update(
        0x16800,
        false,
        false,
        GroundTurnConfig{0x100, 0, 0x2d000, 2, 0x40});
    CHECK(analog_settled.accumulator == 0x16800);
    CHECK(!analog_settled.policy_changed);

    // The retail flag records branch selection, not whether clamping changed
    // the accumulator. Repeated input at the positive cap still sets +2e7c.
    const auto capped = GroundTurn::update(
        0x5a000,
        false,
        true,
        GroundTurnConfig{0x100, 0, 0x5a000});
    CHECK(capped.accumulator == 0x5a000);
    CHECK(capped.policy_changed);

    // The sign-corrected SAR 7 is observable for a non-canonical test limit;
    // -0x1a targets -0x340 rather than flooring to -0x341.
    const auto negative_target_boundary = GroundTurn::update(
        0,
        false,
        false,
        GroundTurnConfig{0x100, 0, 0x1001, 2, -0x1a});
    CHECK(negative_target_boundary.accumulator == -0x340);

    const auto clamped = GroundTurn::update(
        0x5a000,
        false,
        true,
        GroundTurnConfig{0x100, 0, 0x5a000});
    CHECK(clamped.accumulator == 0x5a000);
    CHECK(clamped.wide_profile);
    CHECK(clamped.policy_changed);
    CHECK(!clamped.response_normalized);
    CHECK(left.policy_changed);
    CHECK(GroundTurn::angle12(-0x3c00, 0x100) == 0xffc);
    CHECK(GroundTurn::angle12(0x78000, 0x100) == 0x78);

    // Retail Warehouse analog-lean trace (build/p41 lean-join, 2026-09-28;
    // re/evidence/functions/ground-movement.md "Analog lean joined to the
    // orientation and commit"). Profile 1, DAT_0056865c = 640, no digital
    // Left/Right; the controller's horizontal axis byte is +/-0x20.
    {
        using opentony::runtime::FixedPosition;
        using opentony::runtime::Q12Matrix3;
        const GroundTurnConfig lean_right{640, 1, 0x2d000, 2, 0x20};
        const auto frame31 = GroundTurn::update(0, false, false, lean_right);
        CHECK(frame31.accumulator == 46080);
        CHECK(frame31.policy_changed);
        // FUN_00496360 passes the unmasked (dt * (turn >> 12)) >> 8 = 27.
        CHECK(GroundTurn::angle12(frame31.accumulator, 640) == 27);
        const Q12Matrix3 before31{{-4096, 0, -6, 0, -4096, 0, 6, 0, -4096}};
        const Q12Matrix3 after31 =
            opentony::runtime::q12_apply_ground_yaw(before31, 27);
        CHECK((after31 == Q12Matrix3{{-4092, 0, -175, 0, -4096, 0, 174, 0, -4092}}));
        CHECK(opentony::runtime::retail_basis_from_matrix(after31).at_30f4
              == FixedPosition({-175, 0, -4092}));
        CHECK(opentony::runtime::q12_rotate_ground_velocity(
                  FixedPosition{290, 0, 195101}, before31, 27)
              == FixedPosition({8339, 0, 194898}));
        // The target is reached, so the next frame selects no branch.
        CHECK(!GroundTurn::update(46080, false, false, lean_right).policy_changed);

        const GroundTurnConfig lean_left{640, 1, 0x2d000, 2, -0x20};
        const auto frame71 = GroundTurn::update(46080, false, false, lean_left);
        CHECK(frame71.accumulator == -46080);
        CHECK((GroundTurn::angle12(-46080, 640) | ~0xfff) == -30);
        const Q12Matrix3 before95{{-4093, 0, 155, -14, -4081, -349, -155, 349, -4079}};
        const Q12Matrix3 after95 =
            opentony::runtime::q12_apply_ground_yaw(before95, -30);
        CHECK((after95 == Q12Matrix3{{-4081, 0, 342, -31, -4081, -348, -343, 349, -4067}}));
        CHECK(opentony::runtime::retail_basis_from_matrix(after95).at_30f4
              == FixedPosition({342, -348, -4067}));
        // Released lean: >> 2 decay, as at retail frames 110/111.
        CHECK(GroundTurn::update(-46080, false, false,
                  GroundTurnConfig{640, 1, 0x2d000, 2, 0}).accumulator == -34560);
        CHECK(GroundTurn::update(-34560, false, false,
                  GroundTurnConfig{640, 1, 0x2d000, 2, 0}).accumulator == -25920);
    }

    std::cout << "Ground turn tests passed\n";
}
