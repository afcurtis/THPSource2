#include "camera_views.hpp"
#include "frame_pacing.hpp"
#include "free_look.hpp"

// Phase 4.15b: the follow, death and point camera modes and the orientation helpers they share (camera.cpp, 0x004103e0..0x00411870 and 0x0040be70, 0x0040c1a0). A camera (CCamera) keeps its position at
// +8, the look-at point history at +0x3b0 / +0x3c0, the look direction at +0x5b8 / +0x5c4 / +0x610, the quaternion it renders with at +0x444 and the one it is steering towards at +0x458. The vector
// helpers (Vector3_Subtract and friends) are the 4.14c native ones, called on small vectors in the scratch area exactly as the retail code calls them on its stack locals.

namespace opentony::runtime::camerakit {

using namespace views;

namespace {

using Word = std::uint32_t;

// the camera object
constexpr Word kTripod = 0x3a4;
constexpr Word kPosition = 0x08;
constexpr Word kLookAtPrevious = 0x3b0;
constexpr Word kLookAt = 0x3c0;
constexpr Word kDirection = 0x5b8;
constexpr Word kDirectionPrevious = 0x5c4;
constexpr Word kDirectionTarget = 0x610;
constexpr Word kRenderQuaternion = 0x444;
constexpr Word kTargetQuaternion = 0x458;
constexpr Word kMode = 0x504;
constexpr Word kSpin = 0x5b4;
constexpr Word kFollowing = 0x5d4;
constexpr Word kFrameCounter = 0x5e8;
constexpr Word kLostCounter = 0x60c;
constexpr Word kSettled = 0x418;
constexpr Word kLevelRecords = 0x0053c240;     // the level records (0x1ac bytes each)
constexpr Word kLevelStride = 0x1ac;
constexpr Word kCurrentLevel = 0x0056a898;
constexpr Word kPlayer0 = 0x0056a858;
constexpr Word kPlayer1 = 0x0056a85c;
constexpr Word kCameraList = 0x0055fa38;
constexpr Word kViewport = 0x0055fa3c;
constexpr Word kViewportRecords = 0x0055f7cc;  // per viewport (0xa4 bytes): the position (whole units), the shake offset, the matrix
constexpr Word kViewportStride = 0xa4;
constexpr Word kRotationMatrix = 0x0055f958;   // the nine shorts the follow modes build the view matrix in

// the scratch (stack locals of the retail functions): 16-byte slots for vectors, 0x20-byte slots for quaternions
constexpr Word kVec = kScratch;                       // slot n at kVec + 0x10 * n
constexpr Word kQuat = kScratch + 0x200;              // slot n at kQuat + 0x20 * n
constexpr Word kMatrix = kScratch + 0x300;            // a 0x20-byte matrix

constexpr Word vec(Word n) { return kVec + 0x10 * n; }
constexpr Word quat(Word n) { return kQuat + 0x20 * n; }

constexpr Word kCompetitionStartDisplay = 0x0041b8b0;
constexpr Word kObjectAi = 0x00480ff0;

}  // namespace

// a quaternion object as the copy constructor builds it on the stack: the class vtable and the four components
std::array<Word, 5> CameraKit::copy_quaternion(Word at) {
    return {quaternion::kVtableValue, word_at(m_, at + quaternion::kX), word_at(m_, at + quaternion::kY), word_at(m_, at + quaternion::kZ), word_at(m_, at + quaternion::kW)};
}

void CameraKit::copy_vector(Word to, Word from) {
    set_word_at(m_, to, word_at(m_, from));
    set_word_at(m_, to + 4, word_at(m_, from + 4));
    set_word_at(m_, to + 8, word_at(m_, from + 8));
}

bool CameraKit::vectors_equal(Word a, Word b) {
    return word_at(m_, a) == word_at(m_, b) && word_at(m_, a + 4) == word_at(m_, b + 4) && word_at(m_, a + 8) == word_at(m_, b + 8);
}

// a variable holding a small count, in the scratch (retail passes the address of a stack local)
Word CameraKit::constant(Word slot, Word value) {
    set_word_at(m_, vec(slot), value);
    return vec(slot);
}

// the end of Camera_FollowTarget, Camera_Mode2 and Camera_BuildTripodRotation (0x004109fa, 0x004115de): the direction vector goes through the GTE with the target's up vector to a view
// matrix, which becomes the quaternion the camera steers to, turned by the camera's spin about y
void CameraKit::steer_to_view(Word self, Word up) {
    const Word shifted = core_.vector_shift_right_out(vec(10), self + kDirectionTarget, constant(15, 12));
    const Word negated_x = u(-s(word_at(m_, up)));
    const Word negated_y = u(-s(word_at(m_, up + 4)));
    const Word negated_z = u(-s(word_at(m_, up + 8)));
    set_global_half(m_, mathkit::gte::kRotation, negated_x);
    set_global_half(m_, mathkit::gte::kRotation + 8, negated_y);
    set_global_half(m_, mathkit::gte::kRotation + 16, negated_z);
    set_global_half(m_, mathkit::gte::kIr, word_at(m_, shifted));
    set_global_half(m_, mathkit::gte::kIr + 2, word_at(m_, shifted + 4));
    set_global_half(m_, mathkit::gte::kIr + 4, word_at(m_, shifted + 8));
    math_.cross_sf0();
    const Word ax = u(sar(global_int(m_, mathkit::gte::kMac), 12));
    const Word ay = u(sar(global_int(m_, mathkit::gte::kMac + 4), 12));
    const Word az = u(sar(global_int(m_, mathkit::gte::kMac + 8), 12));
    set_global_half(m_, mathkit::gte::kIr, negated_x);
    set_global_half(m_, mathkit::gte::kRotation, ax);
    set_global_half(m_, mathkit::gte::kRotation + 8, ay);
    set_global_half(m_, mathkit::gte::kRotation + 16, az);
    set_global_half(m_, mathkit::gte::kIr + 2, negated_y);
    set_global_half(m_, mathkit::gte::kIr + 4, negated_z);
    math_.cross_sf0();
    const Word bx = u(sar(global_int(m_, mathkit::gte::kMac), 12));
    const Word by = u(sar(global_int(m_, mathkit::gte::kMac + 4), 12));
    const Word bz = u(sar(global_int(m_, mathkit::gte::kMac + 8), 12));
    set_global_half(m_, kRotationMatrix + 0, ax);
    set_global_half(m_, kRotationMatrix + 2, negated_x);
    set_global_half(m_, kRotationMatrix + 4, bx);
    set_global_half(m_, kRotationMatrix + 6, ay);
    set_global_half(m_, kRotationMatrix + 8, negated_y);
    set_global_half(m_, kRotationMatrix + 10, by);
    set_global_half(m_, kRotationMatrix + 12, az);
    set_global_half(m_, kRotationMatrix + 14, negated_z);
    set_global_half(m_, kRotationMatrix + 16, bz);
    from_matrix(kRotationMatrix, self + kTargetQuaternion);
    turn_target_about_y(self);
}

// 0x004a9870 about the camera's spin, composed on the right of the target quaternion; the result replaces the four components. The free look's yaw adds to the spin (free_look.hpp; none: the spin)
void CameraKit::turn_target_about_y(Word self) {
    about_y(quat(0), s(freelook::spin(half_at(m_, self + kSpin), [&] { return cam_at(m_, self).field_0c8(); })));
    const Word composed = compose(quat(1), self + kTargetQuaternion, quat(0));
    set_word_at(m_, self + kTargetQuaternion + 4, word_at(m_, composed + 4));
    set_word_at(m_, self + kTargetQuaternion + 8, word_at(m_, composed + 8));
    set_word_at(m_, self + kTargetQuaternion + 12, word_at(m_, composed + 12));
    set_word_at(m_, self + kTargetQuaternion + 16, word_at(m_, composed + 16));
}

// 0x00410610 (fastcall): the follow mode. The camera trails the target: the direction it looks along is the target's travel when that is fast and the target is on the ground (or the camera has not given
// up following), else the old direction; the direction is smoothed and turned into the quaternion the camera steers to
void CameraKit::follow_target(Word self) {
    if (cam_at(m_, self).tripod_link().address == 0) {
        return;
    }
    Word delta = vec(1);
    const Word angles = vec(4);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, vec(0) + 4 * k, 0);
        set_half_at(m_, angles + 2 * k, 0);
    }
    core_.vector_subtract_out(vec(2), self + kLookAt, self + kLookAtPrevious);
    copy_vector(delta, vec(2));
    Word tripod = cam_at(m_, self).tripod_link().address;
    if (followed_at(m_, tripod).field_2f64() != 0) {
        cam_at(m_, self).set_follow_state_flag(0);
        cam_at(m_, self).set_smoothing_counter_a(0);
        cam_at(m_, self).set_follow_transition_active(0);
    }
    const Word up = vec(3);
    if (cam_at(m_, self).camera_mode() == 0x19) {
        set_word_at(m_, up, 0);
        set_word_at(m_, up + 4, 0xfffff000);
        set_word_at(m_, up + 8, 0);
    } else {
        copy_vector(up, tripod + 0x310c);
    }
    const Word y_axis = vec(5);
    set_word_at(m_, y_axis, 0);
    set_word_at(m_, y_axis + 4, 0x1000);
    set_word_at(m_, y_axis + 8, 0);
    const Word zero = vec(6);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, zero + 4 * k, 0);
    }
    const std::int32_t dot = math_.dot_q12(y_axis, up);
    const std::int32_t up_alignment = abs_wrap(dot);
    core_.look_angles(angles, zero, delta);
    const Word direction = vec(7);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, direction + 4 * k, 0);
    }
    core_.direction_from_angles(direction, 0x1000, angles);
    const std::int32_t dot2 = math_.dot_q12(direction, up);
    tripod = cam_at(m_, self).tripod_link().address;
    cam_at(m_, self).set_follow_distance_counter(static_cast<Word>(cam_at(m_, self).follow_distance_counter() + framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); })));   // F1.5D: 30 Hz frames
    const std::int32_t state = static_cast<std::int32_t>(followed_at(m_, tripod).physics_state());
    if (state != 1 && state != 3 && state != 2) {
        cam_at(m_, self).set_follow_state_flag(0);
    }
    const bool on_ground_path = up_alignment < 0xdf4 && (static_cast<std::int32_t>(followed_at(m_, tripod).physics_state()) == 2 || static_cast<std::int32_t>(followed_at(m_, tripod).physics_state()) == 8 || cam_at(m_, self).follow_state_flag() != 0);
    const bool landing_path = cam_at(m_, self).follow_transition_active() != 0 && static_cast<std::int32_t>(followed_at(m_, tripod).physics_state()) == 1;
    if (on_ground_path || landing_path) {
        const bool was_following = cam_at(m_, self).follow_transition_active() != 0;
        cam_at(m_, self).set_follow_state_flag(1);
        if (!was_following && followed_at(m_, tripod).field_2ddc() != 0) {
            followed_at(m_, tripod).set_field_3058(static_cast<Word>(followed_at(m_, tripod).field_3058() + 1u));
        }
        const std::int32_t lost = static_cast<std::int32_t>(cam_at(m_, self).follow_preparation_counter()) + static_cast<std::int32_t>(framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }));
        cam_at(m_, self).set_follow_preparation_counter(static_cast<Word>(u(lost)));
        if (lost >= 4) {
            set_word_at(m_, self + kDirectionTarget, 0);
            cam_at(m_, self).set_follow_transition_active(1);
            cam_at(m_, self).set_follow_distance_counter(0);
            set_word_at(m_, self + kDirectionTarget + 4, 0x1000000);
            set_word_at(m_, self + kDirectionTarget + 8, 0);
        } else {
            core_.direction_from_angles(self + kDirectionTarget, 0x1000, angles);
            cam_at(m_, self).set_follow_transition_active(1);
            cam_at(m_, self).set_follow_distance_counter(0);
        }
    } else {
        const std::int32_t alignment2 = abs_wrap(dot2) & static_cast<std::int32_t>(0xfffff000);
        if (alignment2 <= 0x7d0000 && s(core_.vector_length(delta)) >= 5) {
            cam_at(m_, self).set_follow_preparation_counter(0);
            core_.direction_from_angles(self + kDirectionTarget, 0x1000, angles);
            if (!(up_alignment >= 0xdf4) && cam_at(m_, self).follow_transition_active() == 1) {
                cam_at(m_, self).set_follow_distance_counter(0);
            }
        } else {
            cam_at(m_, self).set_follow_preparation_counter(0);
            if (!(up_alignment >= 0xdf4) && cam_at(m_, self).follow_transition_active() == 1) {
                cam_at(m_, self).set_follow_distance_counter(0);
            }
        }
        cam_at(m_, self).set_follow_transition_active(0);
    }
    // the direction is smoothed with the one of the last frame (unless the look-at point stood still or the camera was just placed)
    copy_vector(self + kDirectionPrevious, self + kDirection);
    if (!vectors_equal(self + kLookAt, self + kLookAtPrevious) && cam_at(m_, self).field_4a9() == 0) {
        const Word sum = core_.vector_add_out(vec(8), self + kDirectionPrevious, self + kDirectionTarget);
        const Word half = core_.vector_shift_right_out(vec(9), sum, constant(14, 1));
        copy_vector(self + kDirection, half);
    } else {
        copy_vector(self + kDirection, self + kDirectionPrevious);
    }
    if (cam_at(m_, self).update_tick() < 0xc && cam_at(m_, self).tripod_link().address != 0 && (cam_at(m_, self).tripod_link().address == global_word(m_, kPlayer0) || cam_at(m_, self).tripod_link().address == global_word(m_, kPlayer1))) {
        const Word negated = core_.vector_negate_out(cam_at(m_, self).tripod_link().address + 0x30f4, vec(11));
        const Word shifted = core_.vector_shift_left_out(vec(12), negated, constant(15, 12));
        copy_vector(self + kDirection, shifted);
        copy_vector(self + kDirectionPrevious, shifted);
        copy_vector(self + kDirectionTarget, shifted);
    }
    copy_vector(self + kDirectionTarget, self + kDirection);
    steer_to_view(self, up);
}

// 0x004113f0 (fastcall): the simpler follow mode, without the state machine: the direction is the travel direction when it is fast enough, else the old one
void CameraKit::mode_2(Word self) {
    const Word angles = vec(4);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, vec(0) + 4 * k, 0);
        set_half_at(m_, angles + 2 * k, 0);
    }
    core_.vector_subtract_out(vec(2), self + kLookAt, self + kLookAtPrevious);
    const Word delta = vec(1);
    copy_vector(delta, vec(2));
    const Word tripod = cam_at(m_, self).tripod_link().address;
    const Word up = vec(3);
    if (tripod == 0) {
        set_word_at(m_, up, 0);
        set_word_at(m_, up + 4, 0xfffff000);
        set_word_at(m_, up + 8, 0);
    } else {
        copy_vector(up, tripod + 0x310c);
    }
    const Word y_axis = vec(5);
    set_word_at(m_, y_axis, 0);
    set_word_at(m_, y_axis + 4, 0x1000);
    set_word_at(m_, y_axis + 8, 0);
    const Word zero = vec(6);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, zero + 4 * k, 0);
    }
    (void)math_.dot_q12(y_axis, up);
    core_.look_angles(angles, zero, delta);
    const Word direction = vec(7);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, direction + 4 * k, 0);
    }
    core_.direction_from_angles(direction, 0x1000, angles);
    const std::int32_t alignment = abs_wrap(math_.dot_q12(direction, up)) & static_cast<std::int32_t>(0xfffff000);
    if (alignment <= 0x7d0000 && s(core_.vector_length(delta)) >= 5) {
        cam_at(m_, self).set_follow_transition_active(0);
        core_.direction_from_angles(self + kDirectionTarget, 0x1000, angles);
    } else {
        cam_at(m_, self).set_follow_transition_active(0);
    }
    copy_vector(self + kDirectionPrevious, self + kDirection);
    if (!vectors_equal(self + kLookAt, self + kLookAtPrevious)) {
        const Word sum = core_.vector_add_out(vec(8), self + kDirectionPrevious, self + kDirectionTarget);
        const Word half = core_.vector_shift_right_out(vec(9), sum, constant(14, 1));
        copy_vector(self + kDirection, half);
    } else {
        copy_vector(self + kDirection, self + kDirectionPrevious);
    }
    copy_vector(self + kDirectionTarget, self + kDirection);
    steer_to_view(self, up);
    cam_at(m_, self).set_follow_transition_active(0);
}

// 0x004103e0 (fastcall): the target's own basis is the view: forward and up from the skater, the matrix straight from its orientation matrix (+0x118)
void CameraKit::build_tripod_rotation(Word self) {
    const Word tripod = cam_at(m_, self).tripod_link().address;
    const Word up = vec(3);
    copy_vector(up, tripod + 0x310c);
    copy_vector(self + kDirection, tripod + 0x30f4);
    copy_vector(self + kDirectionPrevious, tripod + 0x30f4);
    copy_vector(self + kDirectionTarget, self + kDirection);
    const Word shifted = core_.vector_shift_right_out(vec(10), self + kDirectionTarget, constant(15, 12));
    const Word negated_x = u(-s(word_at(m_, up)));
    const Word negated_y = u(-s(word_at(m_, up + 4)));
    const Word negated_z = u(-s(word_at(m_, up + 8)));
    set_global_half(m_, mathkit::gte::kIr, word_at(m_, shifted));
    set_global_half(m_, mathkit::gte::kIr + 2, word_at(m_, shifted + 4));
    set_global_half(m_, mathkit::gte::kIr + 4, word_at(m_, shifted + 8));
    set_global_half(m_, mathkit::gte::kRotation, negated_x);
    set_global_half(m_, mathkit::gte::kRotation + 8, negated_y);
    set_global_half(m_, mathkit::gte::kRotation + 16, negated_z);
    math_.cross_sf0();
    const Word ax = u(sar(global_int(m_, mathkit::gte::kMac), 12));
    const Word ay = u(sar(global_int(m_, mathkit::gte::kMac + 4), 12));
    const Word az = u(sar(global_int(m_, mathkit::gte::kMac + 8), 12));
    set_global_half(m_, mathkit::gte::kRotation, ax);
    set_global_half(m_, mathkit::gte::kRotation + 8, ay);
    set_global_half(m_, mathkit::gte::kRotation + 16, az);
    set_global_half(m_, mathkit::gte::kIr, negated_x);
    set_global_half(m_, mathkit::gte::kIr + 2, negated_y);
    set_global_half(m_, mathkit::gte::kIr + 4, negated_z);
    math_.cross_sf0();
    const Word bz = u(sar(global_int(m_, mathkit::gte::kMac + 8), 12));
    set_global_half(m_, kRotationMatrix + 0, ax);
    set_global_half(m_, kRotationMatrix + 4, u(sar(global_int(m_, mathkit::gte::kMac), 12)));
    set_global_half(m_, kRotationMatrix + 6, ay);
    set_global_half(m_, kRotationMatrix + 10, u(sar(global_int(m_, mathkit::gte::kMac + 4), 12)));
    set_global_half(m_, kRotationMatrix + 12, az);
    set_global_half(m_, kRotationMatrix + 16, bz);
    set_global_half(m_, kRotationMatrix + 2, negated_x);
    set_global_half(m_, kRotationMatrix + 8, negated_y);
    set_global_half(m_, kRotationMatrix + 14, negated_z);
    from_matrix(kRotationMatrix, self + kTargetQuaternion);
    from_matrix(tripod + 0x118, self + kTargetQuaternion);
    turn_target_about_y(self);
}

// 0x0040c1a0 (fastcall): the camera behind its target. A camera with no target takes the one the query owner names (and is marked as having borrowed it)
void CameraKit::reset_orientation(Word self) {
    if (freelook::enabled()) {   // a camera put behind its skater (a bail's get-up, a retry, a trigger's reset) starts with the free look home (free_look.hpp); off: nothing is read
        freelook::place(cam_at(m_, self).field_0c8());
    }
    if (cam_at(m_, self).tripod_link().address == 0) {
        cam_at(m_, self).set_anchor_update_flag(1);
        cam_at(m_, self).set_tripod_link(GuestPtr<guest::RuntimeGameplaySkater>{static_cast<Word>(global_word(m_, 0x0056a954))});
    }
    const Word forward = vec(1);
    set_word_at(m_, forward, 0);
    set_word_at(m_, forward + 4, 0);
    set_word_at(m_, forward + 8, 0xfffff000);
    const Word out = vec(4);
    if (cam_at(m_, self).tripod_link().address != 0) {
        math_.convert_vector(cam_at(m_, self).tripod_link().address + 0x2e58, forward, out);
    }
    set_word_at(m_, self + 0x614, word_at(m_, out + 4));
    set_word_at(m_, self + 0x610, word_at(m_, out));
    set_word_at(m_, self + 0x618, word_at(m_, out + 8));
    const Word shifted = core_.vector_shift_left_out(vec(5), self + kDirectionTarget, constant(6, 0xc));
    copy_vector(self + kDirectionPrevious, shifted);
    copy_vector(self + kDirection, shifted);
    const Word tripod = cam_at(m_, self).tripod_link().address;
    if (tripod != 0) {
        copy_vector(self + kLookAt, tripod + 8);
        copy_vector(self + kLookAtPrevious, tripod + 8);
    }
    set_global_word(m_, kViewport, cam_at(m_, self).field_0c8());
    follow_target(self);
    for (Word k = 0; k < 4; ++k) {
        set_word_at(m_, self + kRenderQuaternion + 4 + 4 * k, word_at(m_, self + kTargetQuaternion + 4 + 4 * k));
    }
    for (Word k = 0; k < 4; ++k) {
        set_word_at(m_, self + 0x470 + 4 * k, word_at(m_, self + 0x448 + 4 * k));
    }
    for (Word k = 0; k < 4; ++k) {
        set_word_at(m_, self + 0x498 + 4 * k, word_at(m_, self + 0x470 + 4 * k));
    }
    cam_at(m_, self).set_transform_valid(1);
    cam_at(m_, self).set_field_4a9(0);
    commit_viewport(self);
}

// 0x0040be70 (fastcall): the camera's position and orientation go to the viewport record of its player (the shaken one for a skater that is not in the state 5)
void CameraKit::commit_viewport(Word self) {
    const Word viewport = global_word(m_, kViewport);
    const Word record = kViewportRecords + viewport * kViewportStride;
    const Word tripod = cam_at(m_, self).tripod_link().address;
    if (tripod == 0 || static_cast<std::int32_t>(followed_at(m_, tripod).physics_state()) != 5) {
        set_word_at(m_, record, u(sar(s(word_at(m_, self + 0x63c) + word_at(m_, self + 8)), 12)));
        set_word_at(m_, record + 4, u(sar(s(word_at(m_, self + 0x640) + word_at(m_, self + 0xc)), 12)));
        set_word_at(m_, record + 8, u(sar(s(word_at(m_, self + 0x644) + word_at(m_, self + 0x10)), 12)));
        for (Word k = 0; k < 4; ++k) {
            set_word_at(m_, self + 0x650 + 4 * k, word_at(m_, self + 0x448 + 4 * k));
        }
        cam_at(m_, self).set_field_660(static_cast<Word>(word_at(m_, record) * 0x1000u - word_at(m_, self + 0x3c0)));
        cam_at(m_, self).set_field_664(static_cast<Word>(word_at(m_, record + 4) * 0x1000u - word_at(m_, self + 0x3c4)));
        cam_at(m_, self).set_field_668(static_cast<Word>(word_at(m_, record + 8) * 0x1000u - word_at(m_, self + 0x3c8)));
    } else {
        set_word_at(m_, record, u(sar(s(cam_at(m_, self).field_660() + word_at(m_, self + 0x3c0)), 12)));
        set_word_at(m_, record + 4, u(sar(s(cam_at(m_, self).field_664() + word_at(m_, self + 0x3c4)), 12)));
        set_word_at(m_, record + 8, u(sar(s(cam_at(m_, self).field_668() + word_at(m_, self + 0x3c8)), 12)));
        for (Word k = 0; k < 4; ++k) {
            set_word_at(m_, self + 0x448 + 4 * k, word_at(m_, self + 0x650 + 4 * k));
        }
    }
    const Word shaken = kViewportRecords + 0x10 + viewport * kViewportStride;
    set_word_at(m_, shaken, u(sar(s(word_at(m_, self + 0x3f0) + global_word(m_, 0x0055f948)), 12)));
    set_word_at(m_, shaken + 4, u(sar(s(word_at(m_, self + 0x3f4) + global_word(m_, 0x0055f94c)), 12)));
    set_word_at(m_, shaken + 8, u(sar(s(word_at(m_, self + 0x3f8) + global_word(m_, 0x0055f950)), 12)));
    to_matrix(self + kRenderQuaternion, record + 0x30);
    math_.matrix_set_translation(record + 0x30, record);
}

// 0x00410c40 (fastcall): the orientation reset, then ten updates of a still target
void CameraKit::reset_and_settle(Word self) {
    reset_orientation(self);
    const Word tripod = cam_at(m_, self).tripod_link().address;
    cam_at(m_, self).set_camera_mode(1);
    if (tripod != 0) {
        set_word_at(m_, tripod + 0x4c, 0);
        set_word_at(m_, tripod + 0x50, 0);
        set_word_at(m_, tripod + 0x54, 0);
        for (int k = 0; k < 10; ++k) {
            virtual_call(self, 3);
        }
        set_word_at(m_, tripod + 0x4c, 0);
        set_word_at(m_, tripod + 0x50, 0);
        set_word_at(m_, tripod + 0x54, 0);
    }
}

// 0x00410c90 (fastcall): the death camera: the position and orientation of the skater at its death are the start; the camera blends its quaternion towards the one it was given over thirty frames and
// its position along the line to the point it was given
void CameraKit::death_mode(Word self) {
    const Word counter = self + 0x570;
    if (word_at(m_, counter) == 0) {
        camera_stamp(0xf30);
        check(cam_at(m_, self).tripod_link().address != 0, 0x00524c74);
        const Word tripod = cam_at(m_, self).tripod_link().address;
        camera_stamp(0xf31);
        check(tripod == global_word(m_, kPlayer0) || tripod == global_word(m_, kPlayer1), 0x00524c50);
        copy_vector(self + kPosition, tripod + 8);
        copy_vector(self + 0x594, tripod + 8);
    }
    const auto from = copy_quaternion(self + 0x580);
    const auto to = copy_quaternion(self + 0x5a0);
    const Word t = static_cast<Word>((static_cast<std::uint64_t>(word_at(m_, counter) << 12) * 0x88888889ull) >> 36);
    slerp(from, to, s(t), self + kRenderQuaternion);
    if (word_at(m_, counter) > 0x1e) {
        cam_at(m_, self).set_camera_mode(1);
        return;
    }
    const Word difference = core_.vector_subtract_out(vec(12), self + 0x574, self + 0x594);
    const Word quotient = core_.vector_div_out(vec(13), difference, constant(1, 0x82));
    copy_vector(vec(3), quotient);
    const Word product = core_.scalar_mul_out(vec(13), counter, vec(3));
    const Word scaled = core_.vector_mul_out(vec(12), vec(3), constant(1, 0x1e));
    const Word offset = core_.vector_subtract_out(vec(14), scaled, product);
    set_word_at(m_, self + 8, word_at(m_, self + 0x594) + word_at(m_, offset));
    set_word_at(m_, self + 0xc, word_at(m_, self + 0x598) + word_at(m_, offset + 4));
    set_word_at(m_, self + 0x10, word_at(m_, self + 0x59c) + word_at(m_, offset + 8));
    set_word_at(m_, counter, word_at(m_, counter) + 1u);
}

}  // namespace opentony::runtime::camerakit
