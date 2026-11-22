#include "camera_views.hpp"
#include "frame_pacing.hpp"
#include "free_look.hpp"

// Phase 4.15b: Camera_SmoothAndValidate (camera.cpp, 0x0040e090..0x0040ed6e): the end of the camera's per-frame update. Without a target the camera looks along the line to its look target and settles
// there; with one it keeps a running average of the target's speed, derives the distance and the shake from it, steers the render quaternion (+0x444) to the one the follow mode chose (+0x458) with a
// factor that depends on the situation, runs the effects (the Y-lean raycast, owned by the 4.3 camera model) and finally derives the position and the velocity from the quaternion.

namespace opentony::runtime::camerakit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kVec = kScratch;                       // slot n at kVec + 0x10 * n
constexpr Word kQuat = kScratch + 0x200;              // slot n at kQuat + 0x20 * n
constexpr Word kMatrix = kScratch + 0x300;            // 0x20-byte matrices
constexpr Word kQueryInfo = kScratch + 0x340;         // the 0xa4-byte collision query

constexpr Word vec(Word n) { return kVec + 0x10 * n; }
constexpr Word quat(Word n) { return kQuat + 0x20 * n; }

constexpr Word kRotationMatrix = 0x0055f958;
constexpr Word kApplyEffects = 0x0040c370;            // Camera_ApplyEffects (thiscall): modelled by the 4.3 camera code, a service here
constexpr Word kInitQuery = 0x004624d0;
constexpr Word kZoneQuery = 0x00466090;
constexpr Word kTimeScale = 0x0056865c;               // AnimationTimeScale
constexpr Word kShake = 0x0055f948;                   // three dwords: the shake offset

}  // namespace

// one component pair of the matrix the look-at vector's basis builds, as the two paths of the no-target branch lay it out
void CameraKit::smooth_without_target(Word self) {
    const Word unit = vec(1);                     // the unit vector from the look-at point to the look target
    const Word y_axis = vec(2);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, vec(0) + 4 * k, 0);
        set_word_at(m_, vec(5) + 4 * k, 0);
    }
    const Word delta = core_.vector_subtract_out(vec(3), self + 0x3f0, self + 0x3c0);
    core_.unit_vector(unit, delta);
    set_word_at(m_, y_axis, 0);
    set_word_at(m_, y_axis + 4, 0x1000);
    set_word_at(m_, y_axis + 8, 0);
    const std::int32_t alignment = abs_wrap(math_.dot_q12(unit, y_axis));
    using mathkit::gte::kIr;
    using mathkit::gte::kMac;
    using mathkit::gte::kRotation;
    Word first[3];
    Word second[3];
    if (alignment >= 0xfe0) {
        // looking nearly straight up or down: the first axis is x
        set_global_half(m_, kRotation, half_at(m_, unit));
        set_global_half(m_, kRotation + 8, half_at(m_, unit + 4));
        set_global_half(m_, kRotation + 16, half_at(m_, unit + 8));
        set_global_half(m_, kIr, 0x1000);
        set_global_half(m_, kIr + 2, 0);
        set_global_half(m_, kIr + 4, 0);
        math_.cross_sf0();
        for (Word k = 0; k < 3; ++k) {
            second[k] = u(sar(int_at(m_, kMac + 4 * k), 12));      // (kept in the vector slot the retail code keeps it in)
        }
        set_global_half(m_, kRotation, second[0]);
        set_global_half(m_, kRotation + 8, second[1]);
        set_global_half(m_, kRotation + 16, second[2]);
        set_global_half(m_, kIr, half_at(m_, unit));
        set_global_half(m_, kIr + 2, half_at(m_, unit + 4));
        set_global_half(m_, kIr + 4, half_at(m_, unit + 8));
        math_.cross_sf0();
        for (Word k = 0; k < 3; ++k) {
            first[k] = u(sar(int_at(m_, kMac + 4 * k), 12));
        }
    } else {
        set_global_half(m_, kRotation, 0);
        set_global_half(m_, kRotation + 8, 0x1000);
        set_global_half(m_, kRotation + 16, 0);
        set_global_half(m_, kIr, half_at(m_, unit));
        set_global_half(m_, kIr + 2, half_at(m_, unit + 4));
        set_global_half(m_, kIr + 4, half_at(m_, unit + 8));
        math_.cross_sf0();
        for (Word k = 0; k < 3; ++k) {
            first[k] = u(sar(int_at(m_, kMac + 4 * k), 12));
        }
        set_global_half(m_, kRotation, half_at(m_, unit));
        set_global_half(m_, kRotation + 8, half_at(m_, unit + 4));
        set_global_half(m_, kRotation + 16, half_at(m_, unit + 8));
        set_global_half(m_, kIr, first[0]);
        set_global_half(m_, kIr + 2, first[1]);
        set_global_half(m_, kIr + 4, first[2]);
        math_.cross_sf0();
        for (Word k = 0; k < 3; ++k) {
            second[k] = u(sar(int_at(m_, kMac + 4 * k), 12));
        }
    }
    // the matrix: the first vector in column 0, the second in column 1, the unit vector in column 2
    set_global_half(m_, kRotationMatrix + 0, first[0]);
    set_global_half(m_, kRotationMatrix + 2, second[0]);
    set_global_half(m_, kRotationMatrix + 4, half_at(m_, unit));
    set_global_half(m_, kRotationMatrix + 6, first[1]);
    set_global_half(m_, kRotationMatrix + 8, second[1]);
    set_global_half(m_, kRotationMatrix + 10, half_at(m_, unit + 4));
    set_global_half(m_, kRotationMatrix + 12, first[2]);
    set_global_half(m_, kRotationMatrix + 14, second[2]);
    set_global_half(m_, kRotationMatrix + 16, half_at(m_, unit + 8));
    const Word turned = quat(0);
    set_word_at(m_, turned, quaternion::kVtableValue);
    set_word_at(m_, turned + 4, 0);
    set_word_at(m_, turned + 8, 0);
    set_word_at(m_, turned + 12, 0);
    set_word_at(m_, turned + 16, 0x1000);
    from_matrix(kRotationMatrix, turned);
    std::int32_t length = math_.isqrt(s(word_at(m_, turned + 16) * word_at(m_, turned + 16) + word_at(m_, turned + 12) * word_at(m_, turned + 12) + word_at(m_, turned + 8) * word_at(m_, turned + 8) +
                                       word_at(m_, turned + 4) * word_at(m_, turned + 4)));
    if (length == 0) {
        length = 1;
    }
    for (Word offset : {4u, 8u, 12u, 16u}) {
        set_word_at(m_, turned + offset, u(divide(s(shl(word_at(m_, turned + offset), 12)), length)));
    }
    const Word render = self + 0x444;
    if (cam_at(m_, self).tripod_anchor_flag() != 0) {
        for (Word offset : {4u, 8u, 12u, 16u}) {
            set_word_at(m_, render + offset, word_at(m_, turned + offset));
        }
        cam_at(m_, self).set_tripod_anchor_flag(0);
    } else {
        slerp(copy_quaternion(self + 0x46c), copy_quaternion(turned), 0x7ff, render);
    }
    length = math_.isqrt(s(word_at(m_, render + 4) * word_at(m_, render + 4) + word_at(m_, render + 8) * word_at(m_, render + 8) + word_at(m_, render + 12) * word_at(m_, render + 12) + word_at(m_, render + 16) * word_at(m_, render + 16)));
    if (length == 0) {
        length = 1;
    }
    for (Word offset : {4u, 8u, 12u, 16u}) {
        set_word_at(m_, render + offset, u(divide(s(shl(word_at(m_, render + offset), 12)), length)));
    }
    copy_vector(self + 8, self + 0x3c0);
}

// 0x0040e090 (fastcall; one stack argument, the factor of the steering in the plain case)
void CameraKit::smooth_and_validate(Word self, Word step) {
    for (Word k = 0; k < 4; ++k) {
        set_word_at(m_, self + 0x470 + 4 * k, word_at(m_, self + 0x448 + 4 * k));
    }
    if (cam_at(m_, self).tripod_link().address == 0) {
        smooth_without_target(self);
        return;
    }
    camera_stamp(0x773);
    check(cam_at(m_, self).tripod_link().address != 0, 0x00524b84);
    const Word tripod = cam_at(m_, self).tripod_link().address;
    const Word frames_worth = framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); });   // F1.5D: the counters below are in 30 Hz frames (retail: 1 a frame)
    const Word old_position = vec(8);                 // the position before this frame
    copy_vector(old_position, self + 8);
    const Word velocity = vec(9);                     // the target's velocity
    copy_vector(velocity, tripod + 0x4c);
    const std::int32_t state = static_cast<std::int32_t>(followed_at(m_, tripod).physics_state());
    if (state == 0 || state == 4) {
        for (Word k = 5; k > 0; --k) {
            set_word_at(m_, self + 0x620 + 4 * k, word_at(m_, self + 0x620 + 4 * (k - 1)));
        }
        if (s(core_.vector_length(velocity)) > 0x64) {
            set_word_at(m_, velocity, 0x64000);
            set_word_at(m_, velocity + 4, 0);
            set_word_at(m_, velocity + 8, 0);
        }
        const std::int32_t squared = math_.dot_q12(velocity, velocity);
        set_word_at(m_, self + 0x620, sqrt_truncated(u(squared)) << 6);
    }
    std::int32_t total = 0;
    for (Word k = 0; k < 6; ++k) {
        total = s(u(total) + word_at(m_, self + 0x620 + 4 * k));
    }
    set_word_at(m_, velocity + 4, 0);
    set_word_at(m_, velocity + 8, 0);
    const std::int32_t scaled = (((total / 0xcc) * 0x3c) / 2);
    const std::int32_t speed_factor = (s(u(scaled) * 0xe10u) / 0x149e) >> 12;
    cam_at(m_, self).set_field_61c(static_cast<Word>(u(speed_factor)));
    const auto player = [&] { return cam_at(m_, self).field_0c8(); };   // (the free look's offsets, free_look.hpp: each is the game's value while the look is at rest)
    const std::int32_t distance = s(static_cast<Word>(cam_at(m_, self).camera_distance_q4()) + u(speed_factor) * 4u + freelook::distance(global_word(m_, 0x00524a98), player)) >> 1;
    cam_at(m_, self).set_camera_distance_q4(static_cast<std::int32_t>(u(distance)));
    const Word pull = vec(10);                        // (0, 0, -distance) in Q12: the camera's offset
    set_word_at(m_, pull, 0);
    set_word_at(m_, pull + 4, 0);
    set_word_at(m_, pull + 8, u(-(distance * 0x1000)));
    const Word turned = quat(0);
    if (static_cast<std::int32_t>(cam_at(m_, self).follow_preparation_counter()) >= 4) {
        about_x(quat(1), 0xfffffe3e);
        compose(turned, self + 0x458, quat(1));
        const Word tilt = vec(11);
        set_word_at(m_, tilt, 0);
        set_word_at(m_, tilt + 4, 0);
        set_word_at(m_, tilt + 8, u(-s(global_word(m_, 0x00524a98) << 12)));
        to_matrix(turned, kMatrix);
        const Word shifted = vec(12);
        math_.convert_vector(kMatrix, tilt, shifted);
        bool hit = false;
        if (global_word(m_, 0x0056a86c) == 0) {
            set_global_word(m_, kQueryInfo, word_at(m_, self + 0x3c0));
            set_global_word(m_, kQueryInfo + 4, word_at(m_, self + 0x3c4));
            set_global_word(m_, kQueryInfo + 8, word_at(m_, self + 0x3c8));
            set_global_word(m_, kQueryInfo + 12, word_at(m_, self + 0x3c0) + word_at(m_, shifted));
            set_global_word(m_, kQueryInfo + 16, word_at(m_, self + 0x3c4) + word_at(m_, shifted + 4));
            set_global_word(m_, kQueryInfo + 20, word_at(m_, self + 0x3c8) + word_at(m_, shifted + 8));
            engine(kInitQuery, {kQueryInfo});
            set_global_byte(m_, kQueryInfo + 0x88, 0);
            set_global_word(m_, 0x00567c74, 1);
            engine(kZoneQuery, {kQueryInfo, 1});
            if (!s_.live()) set_global_word(m_, kQueryInfo + 0x68, static_cast<Word>(s_.value(pcplatform::engine_name(kZoneQuery))));       // (the body the query hit: the host's answer)
            set_global_word(m_, 0x00567c74, 0);
            hit = global_word(m_, kQueryInfo + 0x68) != 0;
        }
        if (!hit) {
            for (Word k = 0; k < 4; ++k) {
                set_word_at(m_, self + 0x45c + 4 * k, word_at(m_, turned + 4 + 4 * k));
            }
        }
    }
    const Word target = self + 0x458;
    const Word render = self + 0x444;
    bool counting = false;
    if (global_word(m_, 0x0055fa30) != 0 || ((cam_at(m_, self).follow_transition_active() == 0 || followed_at(m_, tripod).field_2c68() == 0) && cam_at(m_, self).smoothing_counter_a() <= 0)) {
        // the plain case: the target is turned a little, the effects run, a jump may add a turn; then the steering
        if (static_cast<std::int32_t>(cam_at(m_, self).field_670()) > 0) {
            cam_at(m_, self).set_field_670(static_cast<Word>(cam_at(m_, self).field_670() - std::min<Word>(frames_worth, cam_at(m_, self).field_670())));
            counting = true;
        }
        about_x(quat(1), s(freelook::pitch(global_half(m_, 0x00524a94), player)));
        Word composed = compose(quat(2), target, quat(1));
        for (Word k = 0; k < 4; ++k) {
            set_word_at(m_, self + 0x45c + 4 * k, word_at(m_, composed + 4 + 4 * k));
        }
        engine(kApplyEffects, {self});
        if (word_at(m_, cam_at(m_, self).tripod_link().address + 0x2ddc) != 0) {
            const std::int32_t air = int_at(m_, cam_at(m_, self).tripod_link().address + 0x30b8);
            if (air == 3) {
                counting = true;
                cam_at(m_, self).set_field_670(0x14);
            } else if (air != 2 && air == 1) {
                about_x(quat(1), 0xc8);
                composed = compose(quat(2), target, quat(1));
                for (Word k = 0; k < 4; ++k) {
                    set_word_at(m_, self + 0x45c + 4 * k, word_at(m_, composed + 4 + 4 * k));
                }
            }
        }
        std::int32_t factor;
        const std::int32_t frames = static_cast<std::int32_t>(cam_at(m_, self).follow_distance_counter());
        if (frames > 1 && frames < 0x14) {
            // ((scale * 5 << 4) - scale) * 9 = 711 * scale, shifted right (arithmetic) by 8
            factor = sar(s(u(((global_word(m_, kTimeScale) * 5u) << 4) - global_word(m_, kTimeScale)) * 9u), 8);
        } else if (counting) {
            const std::int32_t left = static_cast<std::int32_t>(cam_at(m_, self).field_670());
            const std::int32_t ramp = s(u(0x14 - left) * 65u * 4u);
            std::int32_t quotient = ramp / 20;
            factor = sar(s(global_word(m_, kTimeScale) * u(quotient + 0x33)), 8);
        } else {
            factor = s((global_word(m_, kTimeScale) * freelook::step(step, player)) >> 8);
        }
        slerp(copy_quaternion(self + 0x46c), copy_quaternion(target), factor, render);
    } else {
        engine(kApplyEffects, {self});
        const std::int32_t frames = cam_at(m_, self).smoothing_counter_a();
        std::int32_t phase = frames;
        if (frames == 0) {
            cam_at(m_, self).set_smoothing_counter_c(static_cast<std::int32_t>(u(speed_factor)));
        }
        if (cam_at(m_, self).smoothing_counter_c() > 0x1e) {
            cam_at(m_, self).set_smoothing_counter_c(0x1e);
        }
        if (cam_at(m_, self).follow_transition_active() == 0 || (cam_at(m_, self).follow_state_flag() != 0 && static_cast<std::int32_t>(followed_at(m_, tripod).physics_state()) != 1 && static_cast<std::int32_t>(followed_at(m_, tripod).physics_state()) != 2)) {
            // the shake dies away: the squared phase scales the push
            bool reset = false;
            if (phase < 1) {
                phase = 0;
                reset = true;
            } else {
                if (static_cast<Word>(cam_at(m_, self).smoothing_counter_b()) == 0) {
                    if (phase > 7) {
                        phase = 7;
                    }
                    cam_at(m_, self).set_smoothing_counter_b(static_cast<std::int32_t>(u(phase + 1)));
                } else {
                    phase = s(static_cast<Word>(cam_at(m_, self).smoothing_counter_b())) - 1;
                }
                reset = phase == 0;
            }
            if (reset) {
                cam_at(m_, self).set_smoothing_counter_b(1);
                cam_at(m_, self).set_smoothing_counter_a(static_cast<std::int32_t>(0xffffffff));
                cam_at(m_, self).set_smoothing_counter_d(0);
            }
            const std::int32_t squared = phase * phase;
            cam_at(m_, self).set_smoothing_counter_b(static_cast<std::int32_t>(static_cast<Word>(cam_at(m_, self).smoothing_counter_b()) - frames_worth));
            const std::int32_t push = s(u(s(u(cam_at(m_, self).smoothing_counter_c()) * u(squared)) * 8000u)) / 30;
            set_global_word(m_, kShake + 4, global_word(m_, kShake + 4) + u(push));
            const std::int32_t drop = s(u(s(u(cam_at(m_, self).smoothing_counter_c()) * u(squared)) * 18000u)) / 30;
            set_word_at(m_, pull + 8, word_at(m_, pull + 8) + u(drop));
        } else {
            cam_at(m_, self).set_smoothing_counter_b(0);
            if (phase < 7) {
                if (phase >= 0) {
                    cam_at(m_, self).set_smoothing_counter_d(0);
                    const std::int32_t squared = phase * phase;
                    const std::int32_t push = s(u(s(u(cam_at(m_, self).smoothing_counter_c()) * u(squared)) * 8000u)) / 30;
                    set_global_word(m_, kShake + 4, global_word(m_, kShake + 4) + u(push));
                    const std::int32_t drop = s(u(s(u(cam_at(m_, self).smoothing_counter_c()) * u(squared)) * 18000u)) / 30;
                    set_word_at(m_, pull + 8, word_at(m_, pull + 8) + u(drop));
                }
            } else {
                const std::int32_t push = s(u(cam_at(m_, self).smoothing_counter_c()) * 392000u) / 30;
                set_global_word(m_, kShake + 4, global_word(m_, kShake + 4) + u(push));
                const std::int32_t drop = s(u(cam_at(m_, self).smoothing_counter_c()) * 882000u) / 30;
                set_word_at(m_, pull + 8, word_at(m_, pull + 8) + u(drop));
                cam_at(m_, self).set_smoothing_counter_d(static_cast<std::int32_t>(static_cast<Word>(cam_at(m_, self).smoothing_counter_d()) + frames_worth));
            }
        }
        if (const std::int32_t look_pitch = s(freelook::pitch(0, player)); look_pitch != 0) {   // (this steering has no pitch turn of the game's own, and the look's must not drop out with it: a held look would snap for the landing. At rest: nothing is read or changed)
            about_x(quat(1), look_pitch);
            const Word composed = compose(quat(2), target, quat(1));
            for (Word k = 0; k < 4; ++k) {
                set_word_at(m_, self + 0x45c + 4 * k, word_at(m_, composed + 4 + 4 * k));
            }
        }
        const std::int32_t factor = sar(s(global_word(m_, kTimeScale) * 222u), 8);
        slerp(copy_quaternion(self + 0x46c), copy_quaternion(target), factor, render);
        cam_at(m_, self).set_smoothing_counter_a(static_cast<std::int32_t>(static_cast<Word>(cam_at(m_, self).smoothing_counter_a()) + frames_worth));
    }
    if (cam_at(m_, self).update_tick() < 0xc) {
        for (Word k = 0; k < 4; ++k) {
            set_word_at(m_, self + 0x448 + 4 * k, word_at(m_, self + 0x45c + 4 * k));
        }
        for (Word k = 0; k < 4; ++k) {
            set_word_at(m_, self + 0x470 + 4 * k, word_at(m_, self + 0x448 + 4 * k));
        }
    }
    to_matrix(render, kMatrix + 0x40);
    const Word placed = vec(13);
    math_.convert_vector(kMatrix + 0x40, pull, placed);
    const Word shaken = vec(14);
    set_word_at(m_, shaken, global_word(m_, kShake));
    set_word_at(m_, shaken + 4, global_word(m_, kShake + 4));
    set_word_at(m_, shaken + 8, global_word(m_, kShake + 8));
    math_.convert_vector(kMatrix + 0x40, shaken, self + 0x63c);
    set_word_at(m_, self + 8, word_at(m_, self + 0x3c0) + word_at(m_, placed));
    set_word_at(m_, self + 0xc, word_at(m_, self + 0x3c4) + word_at(m_, placed + 4));
    set_word_at(m_, self + 0x10, word_at(m_, self + 0x3c8) + word_at(m_, placed + 8));
    const Word moved = core_.vector_subtract_out(vec(15), self + 8, old_position);
    copy_vector(self + 0x4c, moved);
}

}  // namespace opentony::runtime::camerakit
