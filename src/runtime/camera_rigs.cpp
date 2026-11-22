#include "camera_views.hpp"
#include "host_options.hpp"
#include "seam_addresses_generated.hpp"

#include <vector>

// Phase 4.15b: the camera rigs' evaluation and blending (camera.cpp, 0x00411a00..0x00411ebe), the point camera mode (0x00410f70), the competition intro and the debug bars. A rig (0x94 bytes) holds a
// follow flag (+4), an owner (+8), a position (+0xc), a rotation flag (+0x18), a quaternion (+0x1c, the fixed one +0x20..0x2c), an offset vector (+0x30), a distance (+0x3c) and its outputs: the
// position (+0x88), the rotation quaternion (+0x74 / +0x78) and the matrix (+0x94).

namespace opentony::runtime::camerakit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kVec = kScratch;                       // slot n at kVec + 0x10 * n
constexpr Word kQuat = kScratch + 0x200;              // slot n at kQuat + 0x20 * n
constexpr Word kMatrix = kScratch + 0x300;            // a 0x20-byte matrix

constexpr Word vec(Word n) { return kVec + 0x10 * n; }
constexpr Word quat(Word n) { return kQuat + 0x20 * n; }

constexpr Word kLevelRecords = 0x0053c240;
constexpr Word kLevelStride = 0x1ac;
constexpr Word kCurrentLevel = 0x0056a898;
constexpr Word kPlayer0 = 0x0056a858;
constexpr Word kCameraList = 0x0055fa38;
constexpr Word kObjectAi = 0x00480ff0;
constexpr Word kCompetitionInit = 0x00419600;
constexpr Word kCompetition = 0x0055fe28;
constexpr Word kCompetitionStartDisplay = 0x0041b8b0;
constexpr Word kDrawBar = 0x0048b620;
constexpr Word kSprintf = seam::crt::CRT_sprintf;

}  // namespace

// a service that filled `bytes` bytes at `destination`: the probe scripted them as dwords under the service's name, the last one padded
void CameraKit::take_filled(Word destination, Word bytes, const std::string& service) {
    if (s_.live()) {
        return;  // (the real service already filled the bytes; the scripted words are replay-only)
    }
    std::vector<std::uint8_t> data;
    for (Word k = 0; k < (bytes + 3) / 4; ++k) {
        const auto word = static_cast<Word>(s_.value(service));
        for (unsigned b = 0; b < 4; ++b) {
            data.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
        }
    }
    data.resize(bytes);
    if (bytes != 0) {
        m_.write(destination, data);
    }
}

// 0x00411a00 (fastcall): the rig's output. The orientation is the rig's own quaternion turned by the owner's matrix when the rotation flag is set, else the fixed quaternion; it is scaled to unit length
// and turned into a matrix, the offset (0, 0, -distance) goes through it, and the result is added to the position (the owner's, when the rig follows it)
void CameraKit::rig_update(Word self) {
    const Word offset = vec(1);                 // (0, 0, -distance << 12)
    set_word_at(m_, offset, 0);
    set_word_at(m_, offset + 4, 0);
    set_word_at(m_, offset + 8, u(-s(u(static_cast<std::int16_t>(rig_at(m_, self).field_3c())) << 12)));
    const Word moved = vec(2);                  // the offset through the matrix
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, moved + 4 * k, 0);
    }
    const Word turned = quat(0);                // the quaternion being built: vtable, x, y, z, w
    set_word_at(m_, turned, quaternion::kVtableValue);
    set_word_at(m_, turned + 4, 0);
    set_word_at(m_, turned + 8, 0);
    set_word_at(m_, turned + 12, 0);
    set_word_at(m_, turned + 16, 0x1000);
    Word x;
    Word y;
    Word z;
    Word w;
    if (rig_at(m_, self).field_18() != 0) {
        from_matrix(rig_at(m_, self).field_8() + 0x2e58, turned);
        const Word composed = compose(quat(1), self + 0x1c, turned);
        x = word_at(m_, composed + 4);
        y = word_at(m_, composed + 8);
        z = word_at(m_, composed + 12);
        w = word_at(m_, composed + 16);
    } else {
        x = rig_at(m_, self).field_20();
        y = rig_at(m_, self).field_24();
        z = rig_at(m_, self).field_28();
        w = rig_at(m_, self).field_2c();
    }
    set_word_at(m_, turned + 4, x);
    set_word_at(m_, turned + 8, y);
    set_word_at(m_, turned + 12, z);
    set_word_at(m_, turned + 16, w);
    std::int32_t length = math_.isqrt(s(w * w + z * z + y * y + x * x));
    if (length == 0) {
        length = 1;
    }
    x = u(divide(s(shl(x, 12)), length));
    y = u(divide(s(shl(y, 12)), length));
    z = u(divide(s(shl(z, 12)), length));
    w = u(divide(s(shl(w, 12)), length));
    set_word_at(m_, turned + 4, x);
    set_word_at(m_, turned + 8, y);
    set_word_at(m_, turned + 12, z);
    set_word_at(m_, turned + 16, w);
    to_matrix(turned, kMatrix);
    math_.convert_vector(kMatrix, offset, moved);
    if (rig_at(m_, self).field_4() != 0) {
        const Word owner = rig_at(m_, self).field_8();
        rig_at(m_, self).set_field_c(static_cast<Word>(word_at(m_, owner + 8)));
        rig_at(m_, self).set_field_10(static_cast<Word>(word_at(m_, owner + 0xc)));
        rig_at(m_, self).set_field_14(static_cast<Word>(word_at(m_, owner + 0x10)));
    }
    rig_at(m_, self).set_field_88(static_cast<Word>(u(sar(s(rig_at(m_, self).field_c() + word_at(m_, moved)), 12))));
    rig_at(m_, self).set_field_8c(static_cast<Word>(u(sar(s(rig_at(m_, self).field_10() + word_at(m_, moved + 4)), 12))));
    rig_at(m_, self).set_field_90(static_cast<Word>(u(sar(s(rig_at(m_, self).field_14() + word_at(m_, moved + 8)), 12))));
    rig_at(m_, self).set_field_78(0);
    rig_at(m_, self).set_field_7c(0);
    rig_at(m_, self).set_field_80(0);
    rig_at(m_, self).set_field_84(0x1000);
    if (rig_at(m_, self).field_64() == rig_at(m_, self).field_8()) {
        rig_at(m_, self).set_field_78(static_cast<Word>(x));
        rig_at(m_, self).set_field_7c(static_cast<Word>(y));
        rig_at(m_, self).set_field_80(static_cast<Word>(z));
        rig_at(m_, self).set_field_84(static_cast<Word>(w));
    }
}

// 0x00411c00 (fastcall): the matrix of the rig's output quaternion with the output position as its translation
void CameraKit::rig_build_matrix(Word self) {
    to_matrix(self + 0x74, self + 0x94);
    const Word position = vec(1);
    set_word_at(m_, position, rig_at(m_, self).field_88());
    set_word_at(m_, position + 4, rig_at(m_, self).field_8c());
    set_word_at(m_, position + 8, rig_at(m_, self).field_90());
    math_.matrix_set_translation(self + 0x94, position);
}

// 0x00411ce0 (cdecl): the rigs a and b blended into `out` at t / 4096
void CameraKit::rig_blend(Word a, Word b, Word t, Word out) {
    check(t < 0x1000, 0x00524c8c);
    if (byte_at(m_, a + 4) != 0) {
        copy_vector(a + 0xc, word_at(m_, a + 8) + 8);
    }
    if (byte_at(m_, b + 4) != 0) {
        copy_vector(b + 0xc, word_at(m_, b + 8) + 8);
    }
    set_word_at(m_, out + 8, word_at(m_, b + 8));
    // position: a + ((b - a) * t >> 12)
    const Word t_cell = constant(14, t);
    const Word twelve = constant(15, 12);
    Word difference = core_.vector_subtract_out(vec(1), b + 0xc, a + 0xc);
    Word scaled = core_.vector_mul_out(vec(2), difference, t_cell);
    Word shifted = core_.vector_shift_right_out(vec(3), scaled, twelve);
    Word sum = core_.vector_add_out(vec(4), shifted, a + 0xc);
    copy_vector(out + 0xc, sum);
    // orientation: the spherical interpolation from b's quaternion to a's at the scaled factor
    const Word factor = static_cast<Word>(static_cast<std::uint64_t>(global_word(m_, 0x0056865c)) * t & 0xffffffffull) >> 8;
    slerp(copy_quaternion(b + 0x1c), copy_quaternion(a + 0x1c), s(factor), out + 0x1c);
    // the offset vector
    difference = core_.vector_subtract_out(vec(5), b + 0x30, a + 0x30);
    scaled = core_.vector_mul_out(vec(6), difference, t_cell);
    shifted = core_.vector_shift_right_out(vec(7), scaled, twelve);
    sum = core_.vector_add_out(vec(8), shifted, a + 0x30);
    copy_vector(out + 0x30, sum);
    // the distance (a short): the unsigned shift of a signed product, as retail has it
    const Word from = half_at(m_, a + 0x3c);
    const Word span = u(short_at(m_, b + 0x3c) - static_cast<std::int16_t>(from));
    set_half_at(m_, out + 0x3c, ((span * t) >> 12) + from);
    set_byte_at(m_, out + 0x18, byte_at(m_, b + 0x18));
    set_word_at(m_, out + 0x64, word_at(m_, b + 0x64));
}

// 0x00410f70 (fastcall): the point camera: from a point of the level the camera swings to the skater over 130 frames; the first frame places it
void CameraKit::point_mode(Word self) {
    const Word counter = self + 0x55c;
    if (word_at(m_, counter) == 0) {
        cam_at(m_, self).set_camera_mode(1);
        for (Word axis = 0; axis < 3; ++axis) {
            const Word player = global_word(m_, kPlayer0);
            set_word_at(m_, player + 0x4c + 4 * axis, word_at(m_, player + 0x30f4 + 4 * axis) * 20u);
        }
        for (int k = 0; k < 10; ++k) {
            engine(kObjectAi, {kCameraList});
        }
        const Word player = global_word(m_, kPlayer0);
        set_word_at(m_, player + 0x4c, 0);
        set_word_at(m_, player + 0x50, 0);
        set_word_at(m_, player + 0x54, 0);
        cam_at(m_, self).set_camera_mode(0x17);
        const std::int32_t distance = sar(global_int(m_, 0x00524a98), 1);
        cam_at(m_, self).set_camera_distance_q4(static_cast<std::int32_t>(u(distance)));
        const Word offset = vec(1);
        set_word_at(m_, offset, 0);
        set_word_at(m_, offset + 4, 0);
        set_word_at(m_, offset + 8, u(distance * -0x1000));
        to_matrix(self + 0x444, kMatrix);
        const Word first = vec(2);
        math_.convert_vector(kMatrix, offset, first);
        set_word_at(m_, offset, global_word(m_, 0x0055f948));
        set_word_at(m_, offset + 4, global_word(m_, 0x0055f94c));
        set_word_at(m_, offset + 8, global_word(m_, 0x0055f950));
        math_.convert_vector(kMatrix, offset, self + 0x63c);
        const Word x = word_at(m_, self + 0x3c0) + word_at(m_, first);
        const Word y = word_at(m_, first + 4) + word_at(m_, self + 0x3c4);
        const Word z = word_at(m_, self + 0x3c8) + word_at(m_, first + 8);
        set_word_at(m_, self + 8, x);
        set_word_at(m_, self + 0xc, y);
        set_word_at(m_, self + 0x10, z);
        set_word_at(m_, self + 0x564, x);
        set_word_at(m_, self + 0x568, y);
        set_word_at(m_, self + 0x56c, z);
    }
    // the orientation the level asks for: turns about z, x and y composed, then blended with the camera's current one
    const Word record = kLevelRecords + global_word(m_, kCurrentLevel) * kLevelStride;
    set_word_at(m_, quat(0), quaternion::kVtableValue);
    set_word_at(m_, quat(0) + 4, 0);
    set_word_at(m_, quat(0) + 8, 0);
    set_word_at(m_, quat(0) + 12, 0);
    set_word_at(m_, quat(0) + 16, 0x1000);
    set_word_at(m_, quat(1), quaternion::kVtableValue);
    for (Word k = 0; k < 4; ++k) {
        set_word_at(m_, quat(1) + 4 + 4 * k, word_at(m_, self + 0x45c + 4 * k));
    }
    about_z(quat(2), half_at(m_, record + 0x64));
    about_x(quat(3), half_at(m_, record + 0x60));
    about_y(quat(4), half_at(m_, record + 0x62));
    const Word turned_yx = compose(quat(5), quat(4), quat(3));
    const Word turned_all = compose(quat(6), turned_yx, quat(2));
    // (host_options.hpp hold_intro: a level with no shot of its own keeps the camera's orientation, so the slerp below holds it)
    const bool hold = host_options().hold_intro;
    for (Word k = 0; k < 4; ++k) {
        set_word_at(m_, quat(0) + 4 + 4 * k, word_at(m_, hold ? self + 0x45c + 4 * k : turned_all + 4 + 4 * k));
    }
    const Word t = static_cast<Word>((static_cast<std::uint64_t>(word_at(m_, counter) << 12) * 0xfc0fc0fdull) >> 39);
    slerp(copy_quaternion(quat(0)), copy_quaternion(quat(1)), s(t), self + 0x444);
    if (word_at(m_, counter) > 0x82) {
        cam_at(m_, self).set_camera_mode(1);
        return;
    }
    engine(kCompetitionStartDisplay, {kCompetition});
    const Word end = vec(1);
    set_word_at(m_, end, hold ? word_at(m_, self + 0x564) : word_at(m_, record + 0x54) << 12);   // (hold: the level's point is where the shot starts)
    set_word_at(m_, end + 4, hold ? word_at(m_, self + 0x568) : word_at(m_, record + 0x58) << 12);
    set_word_at(m_, end + 8, hold ? word_at(m_, self + 0x56c) : word_at(m_, record + 0x5c) << 12);
    const Word difference = core_.vector_subtract_out(vec(2), end, self + 0x564);
    const Word quotient = core_.vector_div_out(vec(3), difference, constant(5, 0x82));
    copy_vector(vec(4), quotient);
    const Word travelled = core_.scalar_mul_out(vec(3), counter, vec(4));
    const Word whole = core_.vector_mul_out(vec(2), vec(4), constant(5, 0x82));
    const Word remaining = core_.vector_subtract_out(vec(6), whole, travelled);
    set_word_at(m_, self + 8, word_at(m_, remaining) + word_at(m_, self + 0x564));
    set_word_at(m_, self + 0xc, word_at(m_, self + 0x568) + word_at(m_, remaining + 4));
    const Word frames = word_at(m_, counter);
    set_word_at(m_, self + 0x10, word_at(m_, self + 0x56c) + word_at(m_, remaining + 8));
    if (s(frames) > 10 && (global_word(m_, 0x0056b028) & 0xffu) != 0) {
        cam_at(m_, self).set_point_acceleration_flag(1);
    }
    set_word_at(m_, counter, cam_at(m_, self).point_acceleration_flag() != 0 ? frames + 6 : frames + 1);
}

// 0x00410bb0 (fastcall): the competition intro shot: the mode, the competition set up on first use, and the heat title ("level - heat n") built into the global text
void CameraKit::start_competition_intro(Word self) {
    cam_at(m_, self).set_point_sequence_tick(0);
    cam_at(m_, self).set_camera_mode(0x17);
    Word heat = global_word(m_, 0x0055ff1c);
    if ((heat & 0xffu) == 0 && ((heat >> 8) & 0xffu) == 0) {
        engine(kCompetitionInit, {kCompetition});
        heat = global_word(m_, 0x0055ff1c);
    }
    const Word level = global_word(m_, kCurrentLevel);
    set_global_word(m_, 0x0055ff8c, 0);
    set_global_word(m_, 0x0055ff3c, 0);
    const Word record = level * kLevelStride;
    engine(kSprintf, {0x0055ff40, 0x00524c40, record + 0x0053c1f8, word_at(m_, record + 0x0053c238), (heat & 0xffu) + 1u});
    take_filled(0x0055ff40, 16, pcplatform::engine_name(kSprintf));
    set_global_word(m_, 0x0055ff3c, 0x0055ff40);
}

// 0x0040f200: when the debug flag is set, two bars are drawn: a short one at the camera's mode and view and a long frame
void CameraKit::draw_debug_bars() {
    if (global_byte(m_, 0x0055fa40) == 0) {
        return;
    }
    const Word mode = global_half(m_, 0x0055fa42);
    const Word view = global_word(m_, 0x0055fa44) & 0xffffu;
    engine(kDrawBar, {view * 100u + 0xc4u, mode * 8u + 0x31u, 0x5a, 8, 0, 0, 0, 0});
    engine(kDrawBar, {0x5a, 0x1e, 0x12c, 0xb4, 0x14, 0x14, 0x14, 0});
}

// 0x0040f270 (fastcall): the debug pad of the camera. Four buttons change the view number (a word at 0x0055fa44) or step the camera mode (a word at 0x0055fa42) through its two tables; the others turn the
// chosen rig's quaternion by 10 units about an axis or move its distance by 10 (in the modes 5 and 6)
void CameraKit::handle_pad(Word self) {
    constexpr Word kViewNumber = 0x0055fa44;
    constexpr Word kModeWord = 0x0055fa42;
    constexpr Word kNextView = 0x0056b1f5;
    constexpr Word kPreviousView = 0x0056b1e5;
    constexpr Word kNextMode = 0x0056b215;
    constexpr Word kPreviousMode = 0x0056b205;
    if (global_byte(m_, kNextView) != 0) {
        const Word view = global_word(m_, kViewNumber) & 0xffffu;
        if (view == 0 || view == 1) {
            set_global_half(m_, kViewNumber, 1);
        } else {
            check(false, 0x00524bc4);
        }
        set_global_byte(m_, kNextView, 0);
    }
    if (global_byte(m_, kPreviousView) != 0) {
        const Word view = global_word(m_, kViewNumber) & 0xffffu;
        if (view == 0 || view == 1) {
            set_global_half(m_, kViewNumber, 0);
        } else {
            check(false, 0x00524bb4);
        }
        set_global_byte(m_, kPreviousView, 0);
    }
    // the mode tables: -1 is the assertion (a mode that has no successor)
    static constexpr int kNext[11] = {1, 2, 3, 5, -1, 6, 8, -1, 9, 10, 0};
    static constexpr int kPrevious[11] = {10, 0, 1, 2, -1, 3, 5, -1, 6, 8, 9};
    if (global_byte(m_, kNextMode) != 0) {
        const Word mode = global_half(m_, kModeWord);
        if (mode <= 10 && kNext[mode] >= 0) {
            set_global_half(m_, kModeWord, static_cast<Word>(kNext[mode]));
        } else {
            check(false, 0x00524bc4);
        }
        set_global_byte(m_, kNextMode, 0);
    }
    if (global_byte(m_, kPreviousMode) != 0) {
        const Word mode = global_half(m_, kModeWord);
        if (mode <= 10 && kPrevious[mode] >= 0) {
            set_global_half(m_, kModeWord, static_cast<Word>(kPrevious[mode]));
        } else {
            check(false, 0x00524bc4);
        }
        set_global_byte(m_, kPreviousMode, 0);
    }
    const Word view = global_word(m_, kViewNumber) & 0xffffu;
    Word rig = 0;
    if (view == 0) {
        rig = self + 0xcc;
    } else if (view == 1) {
        rig = self + 0x2e8;
    } else {
        check(false, 0x00524ba0);
        return;           // (retail goes on with a stale register here; no capture can)
    }
    const auto rotate = [&](int axis, std::int32_t angle) {
        const Word turned = quat(7);
        if (axis == 0) {
            about_x(turned, u(angle));
        } else if (axis == 1) {
            about_y(turned, u(angle));
        } else {
            about_z(turned, u(angle));
        }
        const Word composed = compose(quat(8), rig + 0x1c, turned);
        for (Word k = 0; k < 4; ++k) {
            set_word_at(m_, rig + 0x1c + 4 + 4 * k, word_at(m_, composed + 4 + 4 * k));
        }
    };
    const Word mode = global_half(m_, kModeWord);
    if (global_byte(m_, 0x0056b164) != 0) {
        if (mode == 5) {
            rotate(0, 10);
        } else if (mode == 6) {
            set_half_at(m_, rig + 0x3c, half_at(m_, rig + 0x3c) + 0xfff6u);
        }
    }
    if (global_byte(m_, 0x0056b194) != 0) {
        if (global_half(m_, kModeWord) == 5) {
            rotate(0, -10);
        } else if (global_half(m_, kModeWord) == 6) {
            set_half_at(m_, rig + 0x3c, half_at(m_, rig + 0x3c) + 0xau);
        }
    }
    if (global_byte(m_, 0x0056b174) != 0 && global_half(m_, kModeWord) == 5) {
        rotate(1, -10);
    }
    if (global_byte(m_, 0x0056b184) != 0 && global_half(m_, kModeWord) == 5) {
        rotate(1, 10);
    }
    if (global_byte(m_, 0x0056b1c4) != 0 && global_half(m_, kModeWord) == 5) {
        rotate(2, 10);
    }
    if (global_byte(m_, 0x0056b1d4) != 0 && global_half(m_, kModeWord) == 5) {
        rotate(2, -10);
    }
}

}  // namespace opentony::runtime::camerakit
