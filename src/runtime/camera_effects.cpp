#include "camera_views.hpp"

// Milestone 10e: Camera_ApplyEffects (camera.cpp / camera.h, 0x0040c370..0x0040dff0, thiscall on the camera, called by Camera_SmoothAndValidate twice per frame): the collision-driven lean of the camera.
// Every third call (the phase byte +0x550) casts a ray from the camera down and, with the tripod's mode word (+0x30c4) set, behind it; the hit distances become the shorts the camera leans by
// (+0x530 / +0x532 sideways, +0x534 pitch, +0x536 / +0x538 the roll and tilt), the squared closeness of the hit scaled by a gain. The leans then turn the target quaternion (+0x458) about x and y, a saved
// transform (+0x494) eases the camera back from a lean once the rays stop hitting, and the camera's roll (+0x638) is applied last. The retail function keeps its working vectors, quaternions and matrices in
// its stack frame; the port keeps them in the scratch at the same body-relative offsets (F(offset)), so the code reads against the disassembly.

namespace opentony::runtime::camerakit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kFrame = 0x7ff0f000;                   // the frame of the function (0x7ff0f000..0x7ff0f1d0)
constexpr Word kQueryInfo = kScratch + 0x340;         // the 0xa4-byte collision query (the frame's +0xc4), at the address the services are recorded with
constexpr Word F(Word offset) { return kFrame + offset; }

constexpr Word kInitQuery = 0x004624d0;
constexpr Word kZoneQuery = 0x00466090;

// the collision query
constexpr Word kQueryStart = 0x00;                    // three dwords; the end follows at +0x0c
constexpr Word kQueryEnd = 0x0c;
constexpr Word kQueryDistance = 0x40;                 // the hit's distance
constexpr Word kQueryLength = 0x44;                   // the length of the line, in the same unit
constexpr Word kQueryBody = 0x68;                     // the body the line hit (0: none)
constexpr Word kQueryFlag = 0x88;

// globals
constexpr Word kFrozen = 0x00561c04;
constexpr Word kDebugMode = 0x0056a8e0;
constexpr Word kQueryOwner = 0x0056a86c;
constexpr Word kQueryMode = 0x00567c74;               // set while the lean's rays run
constexpr Word kDistance = 0x00524a98;
constexpr Word kTimeScale = 0x0056865c;
constexpr Word kLevelEventLatch = 0x00568658;
constexpr Word kSavedLatch = 0x005614c4;              // a byte
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
constexpr Word kStampMessage = 0x00524b54;            // "bad colcheck index"
const std::string kCameraHeader = "H:\\TonyHawk\\Pc2\\camera.h";      // 0x00524b68

// the camera
constexpr Word kTripod = 0x3a4;
constexpr Word kAnchor = 0x3c0;
constexpr Word kAnchorPrevious = 0x3b0;
constexpr Word kTarget = 0x458;
constexpr Word kRender = 0x444;
constexpr Word kSaved = 0x494;                        // the saved transform: a quaternion record, x at +0x498
constexpr Word kSavedOn = 0x4a8;
constexpr Word kSavedPlaced = 0x4a9;
constexpr Word kLeanOn = 0x4d4;
constexpr Word kLeanGain = 0x4dc;                     // a short
constexpr Word kPitchOn = 0x4e0;                      // a short
constexpr Word kLeanLeft = 0x530;                     // shorts: the lean sideways (left, right), pitch, roll, tilt; the +0x10 words are the values of the call before
constexpr Word kLeanRight = 0x532;
constexpr Word kLeanPitch = 0x534;
constexpr Word kLeanRoll = 0x536;
constexpr Word kLeanTilt = 0x538;
constexpr Word kLeanLeftPrevious = 0x540;
constexpr Word kLeanRightPrevious = 0x542;
constexpr Word kLeanPitchPrevious = 0x544;
constexpr Word kLeanRollPrevious = 0x546;
constexpr Word kLeanTiltPrevious = 0x548;
constexpr Word kPhase = 0x550;                        // a byte, 0..2 (3 and 4 are seen too: 4 runs the tilt ray)
constexpr Word kCycle = 0x554;
constexpr Word kHitDistance = 0x558;
constexpr Word kLostCounter = 0x61c;
constexpr Word kRoll = 0x638;                         // a short
constexpr Word kShakeFrames = 0x5d8;

constexpr Word kTripodMode = 0x30b8;
constexpr Word kTripodLeaning = 0x30c4;
constexpr Word kTripodHeld = 0x2f64;

}  // namespace

void CameraKit::apply_effects(Word self) {
    const auto stamp = [&](Word line) {
        set_global_word(m_, kAssertionLine, line);
        set_string(kAssertionFile, kCameraHeader);
        check(true, kStampMessage);
    };
    const auto s16 = [](Word v) { return static_cast<std::int32_t>(static_cast<std::int16_t>(v)); };
    const auto half_sum = [&](Word a, Word b) { return (s16(half_at(m_, self + a)) + s16(half_at(m_, self + b))) >> 1; };
    const auto tripod = [&] { return cam_at(m_, self).tripod_link().address; };

    if (global_word(m_, kFrozen) != 0 || global_word(m_, kDebugMode) != 0 || cam_at(m_, self).update_tick() <= 0xc || global_word(m_, kQueryOwner) != 0) {
        return;
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, F(0x38) + 4 * k, 0);
        set_word_at(m_, F(0x94) + 4 * k, 0);
        set_word_at(m_, F(0x174) + 4 * k, 0);
    }
    const Word target = self + kTarget;
    to_matrix(target, F(0x180));

    const Word phase_next = (cam_at(m_, self).effect_sample_phase_u8() + 1u) & 0xffu;
    cam_at(m_, self).set_effect_sample_phase_u8(static_cast<std::uint8_t>(phase_next));
    if (phase_next > 2) {
        cam_at(m_, self).set_effect_sample_phase_u8(0);
        set_word_at(m_, self + kCycle, word_at(m_, self + kCycle) + 1u);
    }

    // a ray: the query record is filled, initialised and run; the service answers the hit in the record
    const auto ray = [&](const Word start[3], const Word end[3], bool flagged) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kQueryInfo + kQueryStart + 4 * k, start[k]);
            set_word_at(m_, kQueryInfo + kQueryEnd + 4 * k, end[k]);
        }
        engine(kInitQuery, {kQueryInfo});
        set_global_byte(m_, kQueryInfo + kQueryFlag, 0);
        if (flagged) {
            set_global_word(m_, kQueryMode, 1);
        }
        std::string line;                                   // (the line the ray runs along, recorded with the call)
        for (Word k = 0; k < 0x18; ++k) {
            line.push_back(static_cast<char>(byte_at(m_, kQueryInfo + k)));
        }
        engine(kZoneQuery, {kQueryInfo, 1}, line);
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(kZoneQuery);        // (the body the line hit and the two distances: the host's answers)
            set_global_word(m_, kQueryInfo + kQueryBody, static_cast<Word>(s_.value(name)));
            set_global_word(m_, kQueryInfo + kQueryLength, static_cast<Word>(s_.value(name)));
            set_global_word(m_, kQueryInfo + kQueryDistance, static_cast<Word>(s_.value(name)));
        }
        if (flagged) {
            set_global_word(m_, kQueryMode, 0);
        }
    };
    const auto hit = [&] { return global_word(m_, kQueryInfo + kQueryBody) != 0; };
    // how close a hit is in Q12, ((length - distance) << 12) / length, from the record (the length is at least one); squared() is the product that retail wraps at 32 bits
    const auto closeness = [&] {
        Word length = global_word(m_, kQueryInfo + kQueryLength);
        if (length == 0) {
            length = 1;
            set_global_word(m_, kQueryInfo + kQueryLength, 1);
        }
        const std::int32_t ratio = divide(s(shl(length - global_word(m_, kQueryInfo + kQueryDistance), 12)), s(length));
        return ratio;
    };
    const auto squared = [&](std::int32_t ratio) { return s(u(ratio) * u(ratio)); };

    // ---- the primary ray (phase 2): straight down, and with the tripod leaning the one behind the camera ----
    if (cam_at(m_, self).effect_sample_phase_u8() == 2) {
        stamp(0x24d);
        cam_at(m_, self).set_effect_sample_fallback_s16(static_cast<std::int16_t>(static_cast<std::uint16_t>(cam_at(m_, self).effect_sample_magnitude_s16())));
        const Word position[3] = {word_at(m_, self + 8), word_at(m_, self + 0xc), word_at(m_, self + 0x10)};
        const Word down[3] = {position[0], position[1] + 0x00bb8000u, position[2]};
        ray(position, down, false);
        if (!hit()) {
            if (int_at(m_, tripod() + kTripodMode) == 1 && word_at(m_, tripod() + kTripodLeaning) == 0) {
                cam_at(m_, self).set_effect_sample_magnitude_s16(0x200);
            } else {
                cam_at(m_, self).set_effect_sample_magnitude_s16(static_cast<std::int16_t>(static_cast<std::uint16_t>(cam_at(m_, self).effect_sample_fallback_s16())));
            }
            cam_at(m_, self).set_effect_sample_distance(static_cast<std::int32_t>(0xffffffff));
        } else {
            const std::int32_t distance = global_int(m_, kQueryInfo + kQueryDistance);
            cam_at(m_, self).set_effect_sample_distance(static_cast<std::int32_t>(u(distance)));
            if (distance < 0x78) {
                const std::int32_t ratio = divide(s(shl(0x78u - u(distance), 12)), 0x78);
                cam_at(m_, self).set_effect_sample_magnitude_s16(static_cast<std::int16_t>(u(sar(s(shl(u(sar(squared(ratio), 12)), 9)), 12))));
            } else {
                cam_at(m_, self).set_effect_sample_magnitude_s16(0);
            }
        }
        if (word_at(m_, tripod() + kTripodLeaning) != 0) {
            set_word_at(m_, F(0x2c), global_word(m_, kDistance) << 13);
            set_word_at(m_, F(0x28), 0);
            set_word_at(m_, F(0x30), 0);
            math_.convert_vector(F(0x180), F(0x28), F(0x78));
            const Word behind[3] = {position[0] + word_at(m_, F(0x78)), position[1] + word_at(m_, F(0x7c)), position[2] + word_at(m_, F(0x80))};
            ray(position, behind, true);
            cam_at(m_, self).set_effect_sample_magnitude_s16(0);
            if (hit()) {
                const std::int32_t ratio = closeness();
                cam_at(m_, self).set_effect_sample_distance(static_cast<std::int32_t>(global_word(m_, kQueryInfo + kQueryDistance)));
                cam_at(m_, self).set_effect_sample_magnitude_s16(static_cast<std::int16_t>(u(sar(s(u(sar(squared(ratio), 12)) * 300u), 12))));
            } else {
                cam_at(m_, self).set_effect_sample_distance(static_cast<std::int32_t>(global_word(m_, kDistance)));
            }
        }
    }

    // the sideways rays' direction: a vector the distance behind the camera, turned by the target's matrix, and its opposite
    set_word_at(m_, F(0x30), 0);
    set_word_at(m_, F(0x2c), 0);
    set_word_at(m_, F(0x28), u(-s(global_word(m_, kDistance) << 13)));
    math_.convert_vector(F(0x180), F(0x28), F(0xb4));
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, F(0x44) + 4 * k, u(-s(word_at(m_, F(0xb4) + 4 * k))));
    }

    // ---- the side rays (phases 0 and 1), when the lean is on ----
    if (cam_at(m_, self).field_4d4() != 0) {
        const auto side = [&](Word direction, Word previous, Word lean) {
            stamp(0x24d);
            set_half_at(m_, self + previous, half_at(m_, self + lean));
            Word start[3];
            Word end[3];
            for (Word k = 0; k < 3; ++k) {
                const Word at = word_at(m_, self + 8 + 4 * k);
                if ((byte_at(m_, self + kCycle) & 1u) != 0) {
                    start[k] = at;
                    end[k] = at + word_at(m_, direction + 4 * k);
                } else {
                    const Word anchor = word_at(m_, self + kAnchor + 4 * k);
                    start[k] = anchor;
                    end[k] = u(sar(s(at + anchor), 1)) + word_at(m_, direction + 4 * k);
                }
            }
            ray(start, end, true);
            set_half_at(m_, self + lean, 0);
            return hit();
        };
        if (cam_at(m_, self).effect_sample_phase_u8() == 0) {
            if (side(F(0xb4), kLeanLeftPrevious, kLeanLeft)) {
                const std::int32_t ratio = closeness();
                cam_at(m_, self).set_effect_sample_vertical_s16(static_cast<std::int16_t>(u(sar(s(u(s16(cam_at(m_, self).field_4dc())) * u(sar(squared(ratio), 12))), 12))));
            }
        }
        if (cam_at(m_, self).effect_sample_phase_u8() == 1) {
            if (side(F(0x44), kLeanRightPrevious, kLeanRight)) {
                const std::int32_t ratio = closeness();
                cam_at(m_, self).set_field_532(static_cast<std::uint16_t>(u(-sar(s(u(s16(cam_at(m_, self).field_4dc())) * u(sar(squared(ratio), 12))), 12))));
            }
        }
    }

    // the vectors the roll rays and the tilt use: the distance behind the camera (z), and a long step to the side (x), turned by the target's matrix
    set_word_at(m_, F(0x30), u(-s(global_word(m_, kDistance) << 12)));
    set_word_at(m_, F(0x28), 0);
    set_word_at(m_, F(0x2c), 0);
    math_.convert_vector(F(0x180), F(0x28), F(0x78));
    set_word_at(m_, F(0x28), 0x64000);
    set_word_at(m_, F(0x2c), 0);
    set_word_at(m_, F(0x30), 0);
    math_.convert_vector(F(0x180), F(0x28), F(0x54));

    // ---- the roll: two rays across the camera's view, when the camera is not shaking and not leaning ----
    if (cam_at(m_, self).smoothing_counter_a() <= 0 && word_at(m_, tripod() + kTripodLeaning) == 0 && s16(cam_at(m_, self).field_4e0()) != 0) {
        stamp(0x24d);
        cam_at(m_, self).set_field_546(static_cast<std::uint16_t>(cam_at(m_, self).field_536()));
        set_word_at(m_, F(0x10), 0);
        set_word_at(m_, F(0x14), 0);
        const auto vector = [&](Word base, Word k) { return word_at(m_, base + 4 * k); };
        Word start[3];
        Word end[3];
        for (Word k = 0; k < 3; ++k) {
            const Word anchor = word_at(m_, self + kAnchor + 4 * k);
            start[k] = u(sar(s(vector(F(0x54), k)), 1)) + anchor;
            end[k] = u(vector(F(0x78), k) * 2u - vector(F(0x54), k)) + anchor;
        }
        ray(start, end, true);
        if (hit()) {
            set_word_at(m_, F(0x14), u(squared(closeness())) >> 12);
        }
        for (Word k = 0; k < 3; ++k) {
            const Word anchor = word_at(m_, self + kAnchor + 4 * k);
            start[k] = anchor - u(sar(s(vector(F(0x54), k)), 1));
            end[k] = u(vector(F(0x54), k) + vector(F(0x78), k) * 2u) + anchor;
        }
        ray(start, end, true);
        if (hit()) {
            set_word_at(m_, F(0x10), u(squared(closeness())) >> 12);
        }
        if (int_at(m_, tripod() + kTripodMode) == 4) {
            set_word_at(m_, F(0x14), 0);
            set_word_at(m_, F(0x10), 0);
        }
        set_word_at(m_, F(0x18), 0x568);
        cam_at(m_, self).set_field_536(0);
        if (word_at(m_, F(0x10)) == 0 && word_at(m_, F(0x14)) == 0) {
            cam_at(m_, self).set_field_536(0);
        } else {
            stamp(0x24e);
            if (half_sum(kLeanRightPrevious, kLeanRight) != 0) {
                stamp(0x24e);
                stamp(0x24e);
                if (-half_sum(kLeanRightPrevious, kLeanRight) > half_sum(kLeanLeftPrevious, kLeanLeft)) {
                    set_word_at(m_, F(0x18), u(sar(s(word_at(m_, F(0x10)) * 0x568u), 12)));
                    cam_at(m_, self).set_field_536(static_cast<std::uint16_t>(u(-s(word_at(m_, F(0x18))))));
                }
            }
            stamp(0x24e);
            if (half_sum(kLeanLeftPrevious, kLeanLeft) != 0) {
                stamp(0x24e);
                stamp(0x24e);
                if (half_sum(kLeanLeftPrevious, kLeanLeft) > -half_sum(kLeanRightPrevious, kLeanRight)) {
                    cam_at(m_, self).set_field_536(static_cast<std::uint16_t>(u(sar(s(word_at(m_, F(0x18)) * word_at(m_, F(0x14))), 12))));
                }
            }
        }
    }
    if (cam_at(m_, self).smoothing_counter_a() >= 1 || word_at(m_, tripod() + kTripodLeaning) != 0) {
        cam_at(m_, self).set_field_536(0);
        cam_at(m_, self).set_field_546(0);
    }

    // ---- the tilt (phase 4): a ray along the camera's velocity ----
    core_.unit_vector(F(0x38), self + 0x4c);
    if (cam_at(m_, self).effect_sample_phase_u8() == 4) {
        stamp(0x24d);
        cam_at(m_, self).set_field_548(static_cast<std::uint16_t>(cam_at(m_, self).field_538()));
        const Word gain = kDistance;
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, F(0x38) + 4 * k, word_at(m_, F(0x38) + 4 * k) * word_at(m_, gain));
        }
        const Word position[3] = {word_at(m_, self + 8), word_at(m_, self + 0xc), word_at(m_, self + 0x10)};
        const Word ahead[3] = {position[0] + word_at(m_, F(0x38)), position[1] + word_at(m_, F(0x3c)), position[2] + word_at(m_, F(0x40))};
        ray(position, ahead, true);
        core_.unit_vector(F(0x38), self + 0x4c);
        const Word offset = core_.vector_subtract_out(F(0x44), tripod() + 8, self + 8);
        core_.unit_vector(F(0x94), offset);
        cam_at(m_, self).set_field_538(0);
        if (hit()) {
            const std::int32_t ratio = closeness();
            cam_at(m_, self).set_field_538(static_cast<std::uint16_t>(u(sar(s(shl(u(sar(squared(ratio), 12)), 8)), 12))));
        }
    }

    // ---- the leans turn the target quaternion ----
    const auto multiply = [&](Word q) {
        // target = target * q (the product the compiler inlined: x, y, z, w at +4..+0x10, Q12)
        const Word tx = word_at(m_, target + 4);
        const Word ty = word_at(m_, target + 8);
        const Word tz = word_at(m_, target + 0xc);
        const Word tw = word_at(m_, target + 0x10);
        const Word qx = word_at(m_, q + 4);
        const Word qy = word_at(m_, q + 8);
        const Word qz = word_at(m_, q + 0xc);
        const Word qw = word_at(m_, q + 0x10);
        set_word_at(m_, target + 4, u(sar(s((ty * qz - qy * tz) + qx * tw + tx * qw), 12)));
        set_word_at(m_, target + 8, u(sar(s((qx * tz - tx * qz) + qy * tw + ty * qw), 12)));
        set_word_at(m_, target + 0xc, u(sar(s((tx * qy - qx * ty) + qz * tw + tz * qw), 12)));
        set_word_at(m_, target + 0x10, u(sar(s(((tw * qw - tx * qx) - ty * qy) - tz * qz), 12)));
    };
    const auto turn = [&](bool about_y_axis, std::int32_t angle) {
        if (about_y_axis) {
            about_y(F(0x64), angle);
        } else {
            about_x(F(0x64), angle);
        }
        const Word composed = compose(F(0xa0), target, F(0x64));
        for (Word offset : {4u, 8u, 12u, 16u}) {
            set_word_at(m_, target + offset, word_at(m_, composed + offset));
        }
    };

    stamp(0x24e);
    if (half_sum(kLeanPitch, kLeanPitchPrevious) != 0) {
        stamp(0x24e);
        turn(false, half_sum(kLeanPitch, kLeanPitchPrevious));
    }
    stamp(0x24e);
    if (half_sum(kLeanLeftPrevious, kLeanLeft) != 0) {
        stamp(0x24e);
        turn(true, half_sum(kLeanLeftPrevious, kLeanLeft));
    }
    stamp(0x24e);
    if (half_sum(kLeanRightPrevious, kLeanRight) != 0) {
        stamp(0x24e);
        turn(true, half_sum(kLeanRightPrevious, kLeanRight));
    }
    if (s16(cam_at(m_, self).field_538()) != 0) {
        // the tilt: the axis is the cross product of the velocity direction and the direction from the camera to the tripod
        const Word offset = core_.vector_subtract_out(F(0x44), tripod() + 8, self + 8);
        core_.unit_vector(F(0x94), offset);
        using mathkit::gte::kIr;
        using mathkit::gte::kMac;
        using mathkit::gte::kRotation;
        set_global_half(m_, kRotation, half_at(m_, F(0x38)));
        set_global_half(m_, kRotation + 8, half_at(m_, F(0x3c)));
        set_global_half(m_, kRotation + 16, half_at(m_, F(0x40)));
        set_global_half(m_, kIr, half_at(m_, F(0x94)));
        set_global_half(m_, kIr + 2, half_at(m_, F(0x98)));
        set_global_half(m_, kIr + 4, half_at(m_, F(0x9c)));
        math_.cross_sf0();
        const Word angle = u(s16(cam_at(m_, self).field_538()));
        set_word_at(m_, F(0x174), angle * global_word(m_, kMac));
        set_word_at(m_, F(0x178), global_word(m_, kMac + 4) * angle);
        set_word_at(m_, F(0x17c), global_word(m_, kMac + 8) * angle);
        from_rotation_vector(F(0x64), F(0x174));
        multiply(F(0x64));
    }
    stamp(0x24e);
    if (half_sum(kLeanRollPrevious, kLeanRoll) != 0) {
        stamp(0x24e);
        about_y(F(0x64), half_sum(kLeanRollPrevious, kLeanRoll));
        multiply(F(0x64));
    }

    // ---- the saved transform: while a lean is held it records where the camera was, and eases back to it afterwards ----
    if (cam_at(m_, self).transform_valid() != 0) {
        set_word_at(m_, F(0x44), 0);
        set_word_at(m_, F(0x48), 0x1000);
        set_word_at(m_, F(0x4c), 0);
        set_word_at(m_, F(0x168), 0);
        set_word_at(m_, F(0x16c), 0);
        set_word_at(m_, F(0x170), 0);
        set_word_at(m_, F(0x88), 0);
        set_word_at(m_, F(0x8c), 0);
        set_word_at(m_, F(0x90), 0);
        set_word_at(m_, F(0xa0), 0);
        set_word_at(m_, F(0xa4), 0x1000);
        set_word_at(m_, F(0xa8), 0);
        math_.convert_vector(F(0x180), F(0xa0), F(0x64));                    // the up axis through the target's matrix
        set_word_at(m_, F(0x168), word_at(m_, F(0x64)));
        set_word_at(m_, F(0x16c), word_at(m_, F(0x68)));
        set_word_at(m_, F(0x170), word_at(m_, F(0x6c)));
        to_matrix(self + kSaved, F(0x1a0));
        set_word_at(m_, F(0xa0), 0);
        set_word_at(m_, F(0xa4), 0);
        set_word_at(m_, F(0xa8), 0x1000);
        math_.convert_vector(F(0x1a0), F(0xa0), F(0x64));                    // the forward axis through the saved matrix
        set_word_at(m_, F(0x88), word_at(m_, F(0x64)));
        set_word_at(m_, F(0x8c), word_at(m_, F(0x68)));
        set_word_at(m_, F(0x90), word_at(m_, F(0x6c)));
        const std::int32_t across = abs_wrap(math_.dot_q12(F(0x168), F(0x88)));
        math_.dot_q12(F(0x44), F(0x88));                                      // (the second dot product's value is unused; it leaves the GTE as retail does)
        if (cam_at(m_, self).effect_sample_distance() < 0 || across > 0x76c) {
            cam_at(m_, self).set_transform_valid(0);
        }
    }
    cam_at(m_, self).set_field_4a9(0);
    const auto sum_left = [&] { return half_sum(kLeanLeftPrevious, kLeanLeft); };
    const auto sum_right = [&] { return half_sum(kLeanRightPrevious, kLeanRight); };
    const auto sum_roll = [&] { return half_sum(kLeanRollPrevious, kLeanRoll); };
    if (cam_at(m_, self).field_61c() >= 4 && !vectors_equal(self + kAnchor, self + kAnchorPrevious) && word_at(m_, tripod() + kTripodHeld) == 0) {
        // the look-at point is moving and nothing holds the camera: the saved transform and the roll are dropped
        cam_at(m_, self).set_transform_valid(0);
        cam_at(m_, self).set_field_638(0);
    } else {
        if (cam_at(m_, self).transform_valid() == 0 && cam_at(m_, self).effect_sample_distance() > -1) {
            // the saved transform starts from the render quaternion, a step of the clock towards the target
            const std::int32_t factor = sar(s(shl(global_word(m_, kTimeScale) * 3u, 9)), 8);
            slerp(copy_quaternion(self + kRender), copy_quaternion(target), factor, self + kSaved);
            cam_at(m_, self).set_transform_valid(1);
        }
        if (global_word(m_, kLevelEventLatch) == 0 && cam_at(m_, self).transform_valid() != 0) {
            stamp(0x24e);
            bool held = sum_left() != 0;
            if (!held) {
                stamp(0x24e);
                held = sum_right() != 0;
            }
            if (!held) {
                stamp(0x24e);
                held = sum_roll() != 0;
            }
            if (!held) {
                cam_at(m_, self).set_field_4a9(static_cast<std::uint8_t>((int_at(m_, tripod() + kTripodMode) == 8 || word_at(m_, tripod() + kTripodHeld) != 0) ? 0 : 1));
                if (global_byte(m_, kSavedLatch) == 0) {
                    const std::int32_t factor = s(global_word(m_, kTimeScale) * 1511u) >> 8;
                    slerp(copy_quaternion(self + kRender), copy_quaternion(target), factor, self + kSaved);
                }
            }
            // the target takes the saved transform back; a lean about y that is still held (the right one alone, or the left one alone) is put on it again
            for (Word k = 0; k < 4; ++k) {
                set_word_at(m_, target + 4 + 4 * k, word_at(m_, self + kSaved + 4 + 4 * k));
            }
            stamp(0x24e);
            bool left_alone = sum_right() == 0;
            if (!left_alone) {
                stamp(0x24e);
                left_alone = sum_left() != 0;
                if (!left_alone) {
                    stamp(0x24e);
                    left_alone = sum_roll() != 0;
                    if (!left_alone) {
                        stamp(0x24e);
                        turn(true, sum_right());
                    }
                }
            }
            if (left_alone) {
                stamp(0x24e);
                if (sum_right() == 0) {
                    stamp(0x24e);
                    if (sum_left() != 0) {
                        stamp(0x24e);
                        if (sum_roll() == 0) {
                            stamp(0x24e);
                            turn(true, sum_left());
                        }
                    }
                }
            }
        }
    }

    // the camera's roll
    if (s16(cam_at(m_, self).field_638()) != 0) {
        about_y(F(0x1c0), s16(cam_at(m_, self).field_638()));
        multiply(F(0x1c0));
    }
}

}  // namespace opentony::runtime::camerakit
