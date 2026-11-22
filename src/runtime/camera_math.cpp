#include "camera_views.hpp"

// Phase 4.15b: the camera's quaternion math (camera.h, 0x004a9650..0x004a9bf0 in the middle of pre.cpp's neighbourhood): composition, conversion to and from a rotation matrix, and the spherical
// interpolation the camera modes blend with. A quaternion is five dwords {vtable, x, y, z, w} in Q12; a matrix is nine shorts (Q12), row major, with six more shorts after it that the conversion clears.

namespace opentony::runtime::camerakit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t asr(std::int32_t v, unsigned n) { return v >> n; }

}  // namespace

// 0x004a9650: the Hamilton product a * b, each component rounded down (an arithmetic shift) from Q24
Word CameraKit::compose(Word out, Word a, Word b) {
    const std::int32_t bx = int_at(m_, b + quaternion::kX);
    const std::int32_t by = int_at(m_, b + quaternion::kY);
    const std::int32_t bz = int_at(m_, b + quaternion::kZ);
    const std::int32_t bw = int_at(m_, b + quaternion::kW);
    const std::int32_t ax = int_at(m_, a + quaternion::kX);
    const std::int32_t ay = int_at(m_, a + quaternion::kY);
    const std::int32_t az = int_at(m_, a + quaternion::kZ);
    const std::int32_t aw = int_at(m_, a + quaternion::kW);
    const auto mul = [](std::int32_t p, std::int32_t q) { return u(p) * u(q); };
    const Word x = (mul(ax, bw) - mul(ay, bz)) + mul(az, by) + mul(aw, bx);
    const Word y = (mul(ax, bz) - mul(az, bx)) + mul(ay, bw) + mul(aw, by);
    const Word z = (mul(az, bw) - mul(ax, by)) + mul(ay, bx) + mul(aw, bz);
    const Word w = ((mul(aw, bw) - mul(ay, by)) - mul(ax, bx)) - mul(az, bz);
    set_word_at(m_, out + quaternion::kX, u(asr(s(x), 12)));
    set_word_at(m_, out + quaternion::kY, u(asr(s(y), 12)));
    set_word_at(m_, out + quaternion::kZ, u(asr(s(z), 12)));
    set_word_at(m_, out + quaternion::kW, u(asr(s(w), 12)));
    set_word_at(m_, out + quaternion::kVtable, quaternion::kVtableValue);
    return out;
}

// 0x004a9750: the rotation vector (axis scaled by the angle in 12-bit turns) to a quaternion: sin(angle / 2) along the unit axis, cos(angle / 2) as w; the zero vector is the identity
void CameraKit::from_rotation_vector(Word out, Word vector) {
    const Word x = word_at(m_, vector);
    const Word y = word_at(m_, vector + 4);
    const Word z = word_at(m_, vector + 8);
    const Word angle = sqrt_truncated(x * x + y * y + z * z);
    if (angle == 0) {
        set_word_at(m_, out + quaternion::kX, 0);
        set_word_at(m_, out + quaternion::kY, 0);
        set_word_at(m_, out + quaternion::kZ, 0);
        set_word_at(m_, out + quaternion::kW, 0x1000);
        set_word_at(m_, out, quaternion::kVtableValue);
        return;
    }
    // the unit axis: Vector_NormalizeQ12 (a Q12 vector written to the scratch)
    constexpr Word unit = kScratch;
    set_word_at(m_, unit, 0);
    set_word_at(m_, unit + 4, 0);
    set_word_at(m_, unit + 8, 0);
    math_.normalize(vector, unit);
    const Word half = angle >> 1;
    const std::int32_t cosine = math_.cos_q12(s(half));
    const std::int32_t sine = math_.sin_q12(s(half));
    set_word_at(m_, out + quaternion::kX, u(asr(s(u(sine) * word_at(m_, unit)), 12)));
    set_word_at(m_, out + quaternion::kY, u(asr(s(word_at(m_, unit + 4) * u(sine)), 12)));
    set_word_at(m_, out + quaternion::kW, u(cosine));
    set_word_at(m_, out + quaternion::kZ, u(asr(s(word_at(m_, unit + 8) * u(sine)), 12)));
    set_word_at(m_, out, quaternion::kVtableValue);
}

// 0x004a9820, 0x004a9870, 0x004a98c0: a turn about one axis; the argument is a short, halved (arithmetic) for the half angle
void CameraKit::about_x(Word out, std::int32_t angle) {
    const std::int32_t half = asr(static_cast<std::int16_t>(angle), 1);
    const std::int32_t cosine = math_.cos_q12(half);
    const std::int32_t sine = math_.sin_q12(half);
    set_word_at(m_, out + quaternion::kW, u(cosine));
    set_word_at(m_, out + quaternion::kX, u(sine));
    set_word_at(m_, out + quaternion::kY, 0);
    set_word_at(m_, out + quaternion::kZ, 0);
    set_word_at(m_, out, quaternion::kVtableValue);
}

void CameraKit::about_y(Word out, std::int32_t angle) {
    const std::int32_t half = asr(static_cast<std::int16_t>(angle), 1);
    const std::int32_t cosine = math_.cos_q12(half);
    const std::int32_t sine = math_.sin_q12(half);
    set_word_at(m_, out + quaternion::kW, u(cosine));
    set_word_at(m_, out + quaternion::kX, 0);
    set_word_at(m_, out + quaternion::kY, u(sine));
    set_word_at(m_, out + quaternion::kZ, 0);
    set_word_at(m_, out, quaternion::kVtableValue);
}

void CameraKit::about_z(Word out, std::int32_t angle) {
    const std::int32_t half = asr(static_cast<std::int16_t>(angle), 1);
    const std::int32_t cosine = math_.cos_q12(half);
    const std::int32_t sine = math_.sin_q12(half);
    set_word_at(m_, out + quaternion::kW, u(cosine));
    set_word_at(m_, out + quaternion::kX, 0);
    set_word_at(m_, out + quaternion::kY, 0);
    set_word_at(m_, out + quaternion::kZ, u(sine));
    set_word_at(m_, out, quaternion::kVtableValue);
}

// 0x004a9910: the rotation matrix of a unit quaternion (Q12 entries from 2ab >> 12 style products, truncated to shorts); the six shorts after the matrix are cleared
void CameraKit::to_matrix(Word quat, Word matrix) {
    const std::int32_t w = int_at(m_, quat + quaternion::kW);
    const std::int32_t x = int_at(m_, quat + quaternion::kX);
    const std::int32_t y = int_at(m_, quat + quaternion::kY);
    const std::int32_t z = int_at(m_, quat + quaternion::kZ);
    const auto prod = [](std::int32_t p, std::int32_t q) { return static_cast<std::int16_t>(asr(s(u(p) * u(q)), 11)); };
    const std::int16_t zz = prod(z, z);
    const std::int16_t yy = prod(y, y);
    set_half_at(m_, matrix, u((0x1000 - zz) - yy));
    const std::int16_t zw = prod(z, w);
    const std::int16_t yx = prod(y, x);
    set_half_at(m_, matrix + 2, u(zw + yx));
    set_half_at(m_, matrix + 6, u(yx - zw));
    const std::int16_t yw = prod(y, w);
    const std::int16_t zx = prod(z, x);
    const std::int16_t xx = static_cast<std::int16_t>(0x1000 - prod(x, x));
    set_half_at(m_, matrix + 4, u(zx - yw));
    set_half_at(m_, matrix + 8, u(xx - zz));
    const std::int16_t xw = prod(x, w);
    const std::int16_t zy = prod(z, y);
    set_half_at(m_, matrix + 10, u(xw + zy));
    set_half_at(m_, matrix + 14, u(zy - xw));
    set_half_at(m_, matrix + 12, u(yw + zx));
    set_half_at(m_, matrix + 16, u(xx - yy));
    for (Word k = 20; k <= 30; k += 2) {
        set_half_at(m_, matrix + k, 0);
    }
}

// 0x004a9a00: a rotation matrix to a quaternion. A positive trace gives w = sqrt(trace + 1) / 2 and the vector from the antisymmetric part; otherwise the largest diagonal entry picks the axis
void CameraKit::from_matrix(Word matrix, Word quat) {
    const auto entry = [&](std::int32_t k) { return static_cast<std::int32_t>(short_at(m_, matrix + 2u * u(k))); };
    const std::int32_t trace = entry(0) + entry(4) + entry(8);
    if (0 < trace) {
        const std::int32_t root = math_.isqrt(s((u(trace) + 0x1000u) * 0x1000u));
        set_word_at(m_, quat + quaternion::kW, u(asr(root, 1)));
        std::int32_t scale = 0;
        if (root != 0) {
            scale = static_cast<std::int32_t>(0x800000ll / root);
        }
        set_word_at(m_, quat + quaternion::kX, u(asr(s(u(entry(5) - entry(7)) * u(scale)), 12)));
        set_word_at(m_, quat + quaternion::kY, u(asr(s(u(entry(6) - entry(2)) * u(scale)), 12)));
        set_word_at(m_, quat + quaternion::kZ, u(asr(s(u(entry(1) - entry(3)) * u(scale)), 12)));
        return;
    }
    std::int32_t axis = entry(0) < entry(4) ? 1 : 0;
    if (entry(axis * 4) < entry(8)) {
        axis = 2;
    }
    const std::int32_t next = s(word_at(m_, kAxisTable + 4u * u(axis)));
    const std::int32_t third = s(word_at(m_, kAxisTable + 4u * u(next)));
    const std::int32_t root = math_.isqrt(s(u((entry(axis * 4) - entry(third * 4)) - entry(next * 4) + 0x1000) * 0x1000u));
    const std::int32_t half = asr(root, 1);
    if (axis == 0) {
        set_word_at(m_, quat + quaternion::kX, u(half));
    } else if (axis == 1) {
        set_word_at(m_, quat + quaternion::kY, u(half));
    } else if (axis == 2) {
        set_word_at(m_, quat + quaternion::kZ, u(half));
    }
    std::int32_t scale = 0;
    if (root != 0) {
        scale = static_cast<std::int32_t>(0x800000ll / root);
    }
    set_word_at(m_, quat + quaternion::kW, u(asr(s(u(entry(next + third + next * 2) - entry(third + next + third * 2)) * u(scale)), 12)));
    // the two other components: the symmetric part of the matrix, scaled
    const auto symmetric = [&](std::int32_t component) {
        const std::int32_t value = [&]() {
            if (component == 0) {
                return entry(axis * 3) + entry(axis);
            }
            if (component == 1) {
                return entry(axis + 3) + entry(axis * 3 + 1);
            }
            return entry(axis + 6) + entry(axis * 3 + 2);
        }();
        return u(asr(s(u(value) * u(scale)), 12));
    };
    if (next == 0) {
        set_word_at(m_, quat + quaternion::kX, symmetric(0));
    } else if (next == 1) {
        set_word_at(m_, quat + quaternion::kY, symmetric(1));
    } else if (next == 2) {
        set_word_at(m_, quat + quaternion::kZ, symmetric(2));
    }
    if (third == 0) {
        set_word_at(m_, quat + quaternion::kX, symmetric(0));
    } else if (third == 1) {
        set_word_at(m_, quat + quaternion::kY, symmetric(1));
    } else if (third == 2) {
        set_word_at(m_, quat + quaternion::kZ, symmetric(2));
    }
}

// 0x004a9bf0: the spherical interpolation of two quaternions (each passed whole: vtable, x, y, z, w) at t / 4096, into `out`. Both are first scaled to unit length; the shorter way round is taken (the
// first one negated when the dot product is negative); nearly opposite ones are blended through the perpendicular, nearly equal ones linearly, the rest by the sine ratios; the result is scaled to unit length again.
void CameraKit::slerp(const std::array<Word, 5>& a, const std::array<Word, 5>& b, std::int32_t t, Word out) {
    const auto length = [&](std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t w) {
        const std::int32_t root = math_.isqrt(s(u(w) * u(w) + u(z) * u(z) + u(y) * u(y) + u(x) * u(x)));
        return root == 0 ? 1 : root;
    };
    std::int32_t ax = s(a[1]);
    std::int32_t ay = s(a[2]);
    std::int32_t az = s(a[3]);
    std::int32_t aw = s(a[4]);
    const std::int32_t la = length(ax, ay, az, aw);
    ax = divide(s(shl(u(ax), 12)), la);
    ay = divide(s(shl(u(ay), 12)), la);
    az = divide(s(shl(u(az), 12)), la);
    aw = divide(s(shl(u(aw), 12)), la);
    std::int32_t bx = s(b[1]);
    std::int32_t by = s(b[2]);
    std::int32_t bz = s(b[3]);
    std::int32_t bw = s(b[4]);
    const std::int32_t lb = length(bx, by, bz, bw);
    bx = divide(s(shl(u(bx), 12)), lb);
    by = divide(s(shl(u(by), 12)), lb);
    bz = divide(s(shl(u(bz), 12)), lb);
    bw = divide(s(shl(u(bw), 12)), lb);
    std::int32_t dot = asr(s(u(bw) * u(aw) + u(bz) * u(az) + u(by) * u(ay) + u(bx) * u(ax)), 12);
    if (dot < 0) {
        dot = -dot;
        ax = -ax;
        ay = -ay;
        az = -az;
        aw = -aw;
    }
    if (0x1000 < dot) {
        dot = 0x1000;
    }
    std::int32_t from_weight = 0;
    std::int32_t to_weight = t;
    if (dot + 0x1000 < 0x81) {
        // nearly opposite: go through the quaternion perpendicular to a (-ay, -ax, -aw swapped as the retail code has it)
        const Word qx = u(-ay);
        const Word qy = u(-ax);
        const Word qz = u(-aw);
        const Word qw = u(az);
        const std::int32_t sa = math_.sin_q12(asr(s(u(0x1000 - t) * 0x1922u), 12));
        const std::int32_t sb = math_.sin_q12(asr(s(u(t) * 0x1922u), 12));
        set_word_at(m_, out + quaternion::kX, u(asr(s(u(sa) * u(ax) + u(sb) * qx), 12)));
        set_word_at(m_, out + quaternion::kY, u(asr(s(qy * u(sb) + u(sa) * u(ay)), 12)));
        set_word_at(m_, out + quaternion::kZ, u(asr(s(u(sa) * u(az) + u(sb) * qz), 12)));
        set_word_at(m_, out + quaternion::kW, u(asr(s(qw * u(sb) + u(sa) * u(aw)), 12)));
    } else {
        if (0x1000 - dot < 0x81) {
            from_weight = 0x1000 - t;
        } else {
            const std::int32_t angle = static_cast<std::int32_t>(core_.acos_ratio_to_angle(dot));
            const std::int32_t sine = math_.sin_q12(angle);
            const std::int32_t first = math_.sin_q12(asr(s(u(0x1000 - t) * u(angle)), 12));
            from_weight = divide(s(shl(u(first), 12)), sine);
            const std::int32_t second = math_.sin_q12(asr(s(u(angle) * u(t)), 12));
            to_weight = divide(s(shl(u(second), 12)), sine);
        }
        set_word_at(m_, out + quaternion::kX, u(asr(s(u(to_weight) * u(bx) + u(from_weight) * u(ax)), 12)));
        set_word_at(m_, out + quaternion::kY, u(asr(s(u(to_weight) * u(by) + u(from_weight) * u(ay)), 12)));
        set_word_at(m_, out + quaternion::kZ, u(asr(s(u(to_weight) * u(bz) + u(from_weight) * u(az)), 12)));
        set_word_at(m_, out + quaternion::kW, u(asr(s(u(to_weight) * u(bw) + u(from_weight) * u(aw)), 12)));
    }
    const std::int32_t lr = length(int_at(m_, out + quaternion::kX), int_at(m_, out + quaternion::kY), int_at(m_, out + quaternion::kZ), int_at(m_, out + quaternion::kW));
    set_word_at(m_, out + quaternion::kX, u(divide(s(shl(word_at(m_, out + quaternion::kX), 12)), lr)));
    set_word_at(m_, out + quaternion::kY, u(divide(s(shl(word_at(m_, out + quaternion::kY), 12)), lr)));
    set_word_at(m_, out + quaternion::kZ, u(divide(s(shl(word_at(m_, out + quaternion::kZ), 12)), lr)));
    set_word_at(m_, out + quaternion::kW, u(divide(s(shl(word_at(m_, out + quaternion::kW), 12)), lr)));
}

// 0x004103b0: (to & 0xfff) - (from & 0xfff), folded into -0x800..0x800 by the short's sign
std::int32_t CameraKit::angle_delta(Word from, Word to) {
    std::int32_t delta = s((to & 0xfff) - (from & 0xfff));
    if (0x800 < static_cast<std::int16_t>(delta)) {
        return delta - 0x1000;
    }
    if (static_cast<std::int16_t>(delta) < -0x800) {
        delta += 0x1000;
    }
    return delta;
}

}  // namespace opentony::runtime::camerakit
