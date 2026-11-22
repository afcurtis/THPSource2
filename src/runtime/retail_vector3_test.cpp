#include "retail_vector3.hpp"

#include "tests/test_check.hpp"

#include <cstdint>

namespace {

using opentony::runtime::RetailVec3;
using opentony::runtime::RetailVec3s;
using opentony::runtime::retail_sar;
using opentony::runtime::retail_shl;
using opentony::runtime::retail_vec3_add;
using opentony::runtime::retail_vec3_decay_shift;
using opentony::runtime::retail_vec3_div_scalar;
using opentony::runtime::retail_vec3_equal;
using opentony::runtime::retail_vec3_mul_scalar;
using opentony::runtime::retail_vec3_not_equal;
using opentony::runtime::retail_vec3_shift_left;
using opentony::runtime::retail_vec3_shift_right;
using opentony::runtime::retail_vec3_sub;
using opentony::runtime::retail_vec3s_add;
using opentony::runtime::retail_vec3s_add_out;
using opentony::runtime::retail_vec3s_decay_shift;
using opentony::runtime::retail_vec3s_equal;
using opentony::runtime::retail_vec3s_mask12;
using opentony::runtime::retail_vec3s_not_equal;
using opentony::runtime::retail_vec3s_snap_near_zero;
using opentony::runtime::retail_vec3s_sub;
using opentony::runtime::retail_vec3s_sub_out;

constexpr std::int32_t kMin = INT32_MIN;
constexpr std::int32_t kMax = INT32_MAX;

bool same(const RetailVec3& value, std::int32_t x, std::int32_t y, std::int32_t z) {
    return value.x == x && value.y == y && value.z == z;
}

} // namespace

int main() {
    // Ordinary Q12 arithmetic.
    RetailVec3 position{0x1000, -0x2000, 0x30};
    const RetailVec3 velocity{0x10, 0x20, -0x30};
    CHECK(&retail_vec3_add(position, velocity) == &position);
    CHECK(same(position, 0x1010, -0x1fe0, 0));
    CHECK(&retail_vec3_sub(position, velocity) == &position);
    CHECK(same(position, 0x1000, -0x2000, 0x30));

    // add/sub/imul wrap modulo 2^32 like the retail instructions.
    RetailVec3 wrap{kMax, kMin, -1};
    retail_vec3_add(wrap, RetailVec3{1, -1, 1});
    CHECK(same(wrap, kMin, kMax, 0));
    retail_vec3_sub(wrap, RetailVec3{1, -1, 1});
    CHECK(same(wrap, kMax, kMin, -1));
    RetailVec3 product{0x10000, kMin, 3};
    const std::int32_t scale = 0x10000;
    retail_vec3_mul_scalar(product, scale);
    CHECK(same(product, 0, 0, 0x30000));
    RetailVec3 negate{kMin, 5, -7};
    const std::int32_t minus_one = -1;
    retail_vec3_mul_scalar(negate, minus_one);
    CHECK(same(negate, kMin, -5, 7));

    // sar masks the count to five bits and shifts in sign bits.
    CHECK(retail_sar(-1, 31) == -1);
    CHECK(retail_sar(kMin, 31) == -1);
    CHECK(retail_sar(kMax, 31) == 0);
    CHECK(retail_sar(-0x1000, 4) == -0x100);
    CHECK(retail_sar(-5, 1) == -3);
    CHECK(retail_sar(0x1234, 32) == 0x1234);
    CHECK(retail_sar(0x1234, 33) == 0x91a);
    CHECK(retail_sar(-0x1234, -1) == -1);
    RetailVec3 shifted{0x7000, -0x7000, 1};
    const std::int32_t twelve = 12;
    retail_vec3_shift_right(shifted, twelve);
    CHECK(same(shifted, 7, -7, 0));

    // Operands are re-read per component, so aliasing the destination sees
    // earlier writes.
    RetailVec3 self_add{1, 2, 3};
    retail_vec3_add(self_add, self_add);
    CHECK(same(self_add, 2, 4, 6));
    RetailVec3 self_sub{1, 2, 3};
    retail_vec3_sub(self_sub, self_sub);
    CHECK(same(self_sub, 0, 0, 0));
    RetailVec3 alias_scale{3, 4, 5};
    retail_vec3_mul_scalar(alias_scale, alias_scale.x);
    CHECK(same(alias_scale, 9, 36, 45));
    RetailVec3 alias_scale_z{3, 4, 5};
    retail_vec3_mul_scalar(alias_scale_z, alias_scale_z.z);
    CHECK(same(alias_scale_z, 15, 20, 25));
    RetailVec3 alias_shift{1, 0x100, 0x100};
    retail_vec3_shift_right(alias_shift, alias_shift.x);
    CHECK(same(alias_shift, 0, 0x100, 0x100));
    RetailVec3 alias_shift_y{0x40, 2, 0x40};
    retail_vec3_shift_right(alias_shift_y, alias_shift_y.y);
    CHECK(same(alias_shift_y, 0x10, 0, 0x40));

    // Overlapping destination/source (rhs starts at this->y).
    std::int32_t buffer[4] = {1, 10, 100, 1000};
    RetailVec3* overlap = reinterpret_cast<RetailVec3*>(&buffer[0]);
    const RetailVec3* shifted_rhs = reinterpret_cast<const RetailVec3*>(&buffer[1]);
    retail_vec3_add(*overlap, *shifted_rhs);
    CHECK(buffer[0] == 11 && buffer[1] == 110 && buffer[2] == 1100 && buffer[3] == 1000);

    CHECK(retail_vec3_equal(RetailVec3{1, 2, 3}, RetailVec3{1, 2, 3}) == 1);
    CHECK(retail_vec3_equal(RetailVec3{1, 2, 3}, RetailVec3{1, 2, 4}) == 0);
    CHECK(retail_vec3_equal(RetailVec3{0, 2, 3}, RetailVec3{1, 2, 3}) == 0);
    CHECK(retail_vec3_equal(RetailVec3{kMin, 0, 0}, RetailVec3{kMin, 0, 0}) == 1);

    // Division truncates toward zero; the divisor is re-read per component,
    // so aliasing self.x turns the later divisors into x / x == 1.
    RetailVec3 quotient{7, -7, kMin};
    const std::int32_t two = 2;
    CHECK(&retail_vec3_div_scalar(quotient, two) == &quotient);
    CHECK(same(quotient, 3, -3, kMin / 2));
    RetailVec3 alias_div{6, 12, -18};
    retail_vec3_div_scalar(alias_div, alias_div.x);
    CHECK(same(alias_div, 1, 12, -18));
    RetailVec3 alias_div_y{12, 6, -18};
    retail_vec3_div_scalar(alias_div_y, alias_div_y.y);
    CHECK(same(alias_div_y, 2, 1, -18));

    // shl keeps the low 32 bits and masks the count to five bits.
    CHECK(retail_shl(1, 31) == kMin);
    CHECK(retail_shl(3, 31) == kMin);
    CHECK(retail_shl(0x1234, 32) == 0x1234);
    CHECK(retail_shl(0x1234, 33) == 0x2468);
    CHECK(retail_shl(-1, 4) == -16);
    RetailVec3 left{1, -1, 0x40000000};
    const std::int32_t one = 1;
    retail_vec3_shift_left(left, one);
    CHECK(same(left, 2, -2, kMin));

    // Decay: v - (v >> count[i]); byte counts are masked to five bits and
    // re-read after the earlier components were written.
    RetailVec3 decay{0x1000, -0x1000, kMin};
    const std::uint8_t counts[3] = {4, 4, 32};
    CHECK(&retail_vec3_decay_shift(decay, counts) == &decay);
    CHECK(same(decay, 0xf00, -0xf00, 0));
    RetailVec3 decay_neg{-1, -3, 5};
    const std::uint8_t big_counts[3] = {31, 33, 255};
    retail_vec3_decay_shift(decay_neg, big_counts);
    CHECK(same(decay_neg, 0, -1, 5));
    RetailVec3 decay_alias{0x101, 0x100, 0x100};
    // counts alias self.x's bytes: counts[0] = 1 makes x 0x101 - 0x80 = 0x81,
    // which clears the byte counts[1] then reads (a stale 1 would leave
    // y = 0x80); counts[2] is x's third byte (0).
    retail_vec3_decay_shift(decay_alias, reinterpret_cast<const std::uint8_t*>(&decay_alias.x));
    CHECK(same(decay_alias, 0x81, 0, 0));

    CHECK(retail_vec3_not_equal(RetailVec3{1, 2, 3}, RetailVec3{1, 2, 3}) == 0);
    CHECK(retail_vec3_not_equal(RetailVec3{1, 2, 3}, RetailVec3{1, 2, 4}) == 1);
    CHECK(retail_vec3_not_equal(RetailVec3{0, 2, 3}, RetailVec3{1, 2, 3}) == 1);

    // Packed 16-bit helpers.
    RetailVec3s angles{static_cast<std::int16_t>(-1), 0x1234, 0x0fff};
    retail_vec3s_mask12(angles);
    CHECK(angles.x == 0x0fff && angles.y == 0x0234 && angles.z == 0x0fff);

    RetailVec3s snap{-1, 1, -2};
    retail_vec3s_snap_near_zero(snap);
    CHECK(snap.x == 0 && snap.y == 0 && snap.z == -2);
    RetailVec3s snap_edges{0, 2, INT16_MIN};
    retail_vec3s_snap_near_zero(snap_edges);
    CHECK(snap_edges.x == 0 && snap_edges.y == 2 && snap_edges.z == INT16_MIN);

    RetailVec3s wrap16{INT16_MAX, INT16_MIN, 1};
    CHECK(&retail_vec3s_add(wrap16, RetailVec3s{1, -1, -1}) == &wrap16);
    CHECK(wrap16.x == INT16_MIN && wrap16.y == INT16_MAX && wrap16.z == 0);
    retail_vec3s_sub(wrap16, RetailVec3s{1, -1, -1});
    CHECK(wrap16.x == INT16_MAX && wrap16.y == INT16_MIN && wrap16.z == 1);
    RetailVec3s self_add16{1, 2, 3};
    retail_vec3s_add(self_add16, self_add16);
    CHECK(self_add16.x == 2 && self_add16.y == 4 && self_add16.z == 6);

    // sar r16: counts 16..31 leave only sign bits, 32 masks to 0.
    RetailVec3s decay16{-0x1000, 0x7fff, -2};
    const std::uint8_t counts16[3] = {4, 17, 32};
    CHECK(&retail_vec3s_decay_shift(decay16, counts16) == &decay16);
    CHECK(decay16.x == -0xf00 && decay16.y == 0x7fff && decay16.z == 0);
    RetailVec3s decay16_neg{-5, -5, INT16_MIN};
    const std::uint8_t counts16_neg[3] = {16, 31, 0};
    retail_vec3s_decay_shift(decay16_neg, counts16_neg);
    CHECK(decay16_neg.x == -4 && decay16_neg.y == -4 && decay16_neg.z == 0);

    CHECK(retail_vec3s_equal(RetailVec3s{1, 2, 3}, RetailVec3s{1, 2, 3}) == 1);
    CHECK(retail_vec3s_equal(RetailVec3s{1, 2, 3}, RetailVec3s{1, 2, -3}) == 0);
    CHECK(retail_vec3s_not_equal(RetailVec3s{1, 2, 3}, RetailVec3s{1, 2, 3}) == 0);
    CHECK(retail_vec3s_not_equal(RetailVec3s{-1, 2, 3}, RetailVec3s{1, 2, 3}) == 1);

    // Value-returning operators read every input before the result exists,
    // so writing the result over an input is safe.
    const RetailVec3s sum = retail_vec3s_add_out(RetailVec3s{INT16_MAX, 5, -5}, RetailVec3s{1, 5, 5});
    CHECK(sum.x == INT16_MIN && sum.y == 10 && sum.z == 0);
    const RetailVec3s diff = retail_vec3s_sub_out(RetailVec3s{INT16_MIN, 5, -5}, RetailVec3s{1, 5, 5});
    CHECK(diff.x == INT16_MAX && diff.y == 0 && diff.z == -10);
    std::int16_t packed[5] = {1, 2, 3, 4, 5};
    RetailVec3s* out = reinterpret_cast<RetailVec3s*>(&packed[0]);
    *out = retail_vec3s_add_out(*reinterpret_cast<const RetailVec3s*>(&packed[0]),
                                *reinterpret_cast<const RetailVec3s*>(&packed[2]));
    CHECK(packed[0] == 4 && packed[1] == 6 && packed[2] == 8 && packed[3] == 4 && packed[4] == 5);
    return 0;
}
