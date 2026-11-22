#include "camera_views.hpp"
#include "frame_pacing.hpp"
#include "free_look.hpp"

// Milestone 10e: Camera_Update (camera.cpp, 0x0040f850..0x00410376, vtable slot 4 of the camera object): the camera's per-frame update. It follows the look-at point and the look target, runs the debug
// camera's keys (a free-flying camera the pad chord toggles), counts the frame, hands the viewport its field of view, runs the mode the camera is in (follow, mode 2, point, death or the landing
// follow), smooths the result, applies the shake and commits the viewport. The vector helpers are the 4.14c native ones called on small vectors in the scratch area, as in camera_modes.cpp.

namespace opentony::runtime::camerakit {

using namespace views;

namespace {

using Word = std::uint32_t;

// the camera object
constexpr Word kTripod = 0x3a4;               // the skater the camera follows
constexpr Word kAnchorFlag = 0x3ac;
constexpr Word kAnchorPrevious = 0x3b0;
constexpr Word kAnchorLinkFlag = 0x3bc;       // set: the points are the linked objects' positions, clear: they trail them
constexpr Word kAnchor = 0x3c0;
constexpr Word kTargetLink = 0x3dc;
constexpr Word kTargetFlag = 0x3e0;
constexpr Word kTargetPrevious = 0x3e4;
constexpr Word kTarget = 0x3f0;
constexpr Word kViewportParameter = 0x40c;
constexpr Word kViewportRamp = 0x410;         // a short: the frames left of the ramp; +0x414 is the step
constexpr Word kRenderQuaternion = 0x444;
constexpr Word kRoll = 0x434;
constexpr Word kLift = 0x436;
constexpr Word kPlaced = 0x4a9;
constexpr Word kShake = 0x4f2;                // three shorts (x, y, z amplitudes), then three phases at +0x4fc..+0x500 and three decays at +0x4f8..+0x4fa
constexpr Word kMode = 0x504;
constexpr Word kTick = 0x510;
constexpr Word kDirection = 0x5b8;
constexpr Word kDirectionPrevious = 0x5c4;
constexpr Word kDirectionTarget = 0x610;
constexpr Word kLiftSum = 0x5ec;
constexpr Word kLiftFrames = 0x5f0;
constexpr Word kLostCounter = 0x61c;

// the camera globals
constexpr Word kViewportNumber = 0x0055fa3c;
constexpr Word kDebugFlyOn = 0x0055fa30;
constexpr Word kDebugChordSeen = 0x0055fa48;
constexpr Word kDebugChordUp = 0x0055fa4c;
constexpr Word kShakeWords = 0x0055fa50;      // two shorts
constexpr Word kDebugMode = 0x0056a8e0;
constexpr Word kDebugEnabled = 0x0056a900;
constexpr Word kChordKey = 0x0056b0e8;
constexpr Word kFrozen = 0x00561c04;
constexpr Word kCurrentViewport = 0x00563a38;
constexpr Word kMoviePending = 0x0056b7f0;
constexpr Word kShakeClock = 0x0056a8d4;
constexpr Word kTimeScale = 0x0056865c;       // AnimationTimeScale
constexpr Word kDistance = 0x00524a40;
constexpr Word kPitch = 0x00524a44;
constexpr Word kYOffset = 0x00524a48;
constexpr Word kSavedParameter = 0x00524aa4;
constexpr Word kClearedWord = 0x00524aa0;
constexpr Word kPitchOut = 0x00524a94;
constexpr Word kDistanceOut = 0x00524a98;
constexpr Word kSavedYOffset = 0x0055f978;
constexpr Word kSavedDistance = 0x0055f9a4;
constexpr Word kSavedPitch = 0x0055f910;
constexpr Word kShakeOffset = 0x0055f948;     // x, y << 12, z
constexpr Word kVideoPlaying = 0x004cde70;
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
const std::string kCameraFile = "H:\\TonyHawk\\Pc2\\camera.cpp";   // 0x00524af0

// the debug camera's pad bytes (the chord's other keys cleared at the toggle: 0x56b1c5, 0x56b1d5, 0x56b049, 0x56b0e9, 0x56b0d9)
constexpr Word kKeyFovDown = 0x0056b008;
constexpr Word kKeyFovUp = 0x0056b018;
constexpr Word kKeyFovReset = 0x0056aff8;
constexpr Word kKeySpinLeft = 0x0056b174;
constexpr Word kKeySpinRight = 0x0056b184;
constexpr Word kKeyDistanceDown = 0x0056b1e4;
constexpr Word kKeyDistanceUp = 0x0056b1f4;
constexpr Word kKeyPitchDown = 0x0056b204;
constexpr Word kKeyPitchUp = 0x0056b214;
constexpr Word kKeyHeightDown = 0x0056b1a4;
constexpr Word kKeyHeightUp = 0x0056b1b4;
constexpr Word kKeyRestore = 0x0056b244;
constexpr Word kKeyFast = 0x0056b254;

constexpr Word kMessageNull = 0x00524c18;     // "NULL pCurrentViewport in Camera::Update"
constexpr Word kMessageMinusOne = 0x00524bf4; // "-1 pCurrentViewport in Camera::Update"
constexpr Word kMessageMode = 0x00524bd4;     // "Unknown camera mode in switch"

// the scratch (the stack locals of the retail function): above the slots camera_modes.cpp uses
constexpr Word kUpdateScratch = kScratch + 0x400;
constexpr Word upd(Word n) { return kUpdateScratch + 0x10 * n; }                  // vectors and small variables
constexpr Word upd_quat(Word n) { return kUpdateScratch + 0x100 + 0x20 * n; }     // quaternions
constexpr Word kSaved = upd(15);                                                  // the anchor's previous position, put back after the mode ran

}  // namespace

// the assertion macro's line and file name (the retail macro copies the file name, with its NUL, to 0x0056881c before each check)
void CameraKit::camera_stamp(Word line) {
    set_global_word(m_, kAssertionLine, line);
    set_string(kAssertionFile, kCameraFile);
}

// 0x0040f850 (fastcall on the camera)
void CameraKit::update(Word self) {
    const auto word = [&](Word slot, Word value) {
        set_word_at(m_, upd(slot), value);
        return upd(slot);
    };
    const auto s16 = [](Word v) { return static_cast<std::int32_t>(static_cast<std::int16_t>(v)); };

    set_global_word(m_, kViewportNumber, cam_at(m_, self).field_0c8());

    // the look-at point and the look target: kept, then moved to the linked object's position or a third of the way (the weights 1:2) towards it
    const auto trail = [&](Word previous, Word current, Word link) {
        copy_vector(previous, current);
        if (cam_at(m_, self).tripod_anchor_flag() != 0) {
            copy_vector(current, word_at(m_, self + link) + 8);
        } else {
            const Word doubled = core_.vector_shift_left_out(upd(1), word_at(m_, self + link) + 8, word(0, 1));
            const Word sum = core_.vector_add_out(upd(2), doubled, previous);
            const Word third = core_.vector_div_out(upd(3), sum, word(4, 3));
            copy_vector(current, third);
        }
    };
    if (cam_at(m_, self).anchor_update_flag() != 0) {
        trail(self + kAnchorPrevious, self + kAnchor, kTripod);
    }
    if (cam_at(m_, self).secondary_target_valid() != 0) {
        trail(self + kTargetPrevious, self + kTarget, kTargetLink);
    }

    // the debug camera's chord: it toggles the free camera
    if (global_byte(m_, kChordKey) == 0) {
        set_global_word(m_, kDebugChordUp, 1);
    } else {
        if (global_word(m_, kDebugChordUp) != 0) {
            set_global_word(m_, kDebugChordSeen, 1);
        }
        set_global_word(m_, kDebugChordUp, 0);
    }
    bool fly = true;
    if (global_word(m_, kDebugEnabled) != 0 && global_word(m_, kDebugChordSeen) != 0 && global_byte(m_, kChordKey) != 0) {
        const Word on = global_word(m_, kDebugFlyOn) ^ 1u;
        set_global_byte(m_, 0x0056b1c5, 0);
        set_global_byte(m_, 0x0056b1d5, 0);
        set_global_word(m_, kDebugFlyOn, on);
        set_global_word(m_, kClearedWord, 0);
        set_global_byte(m_, 0x0056b049, 0);
        set_global_word(m_, kDebugChordSeen, 0);
        set_global_byte(m_, 0x0056b0e9, 0);
        set_global_byte(m_, 0x0056b0d9, 0);
        set_global_word(m_, kDebugMode, on);
        if (on != 0) {
            set_global_word(m_, kSavedYOffset, global_word(m_, kYOffset));
            set_global_word(m_, kSavedDistance, global_word(m_, kDistance));
            set_global_word(m_, kSavedPitch, global_word(m_, kPitch));
            set_global_word(m_, kSavedParameter, word_at(m_, self + kViewportParameter));
        } else {
            fly = false;
        }
    }
    if (fly && global_word(m_, kDebugFlyOn) != 0) {
        if (global_byte(m_, kKeyRestore) != 0) {
            set_global_word(m_, kYOffset, global_word(m_, kSavedYOffset));
            set_global_word(m_, kDistance, global_word(m_, kSavedDistance));
            set_global_word(m_, kPitch, global_word(m_, kSavedPitch));
            set_word_at(m_, self + kViewportParameter, global_word(m_, kSavedParameter));
        }
        if (global_byte(m_, kKeyFovDown) != 0) {
            set_word_at(m_, self + kViewportParameter, word_at(m_, self + kViewportParameter) - 1u);
        }
        if (global_byte(m_, kKeyFovUp) != 0) {
            set_word_at(m_, self + kViewportParameter, word_at(m_, self + kViewportParameter) + 1u);
        }
        if (global_byte(m_, kKeyFovReset) != 0) {
            set_word_at(m_, self + kViewportParameter, 0x100);
        }
        if (global_byte(m_, kKeySpinLeft) != 0) {
            set_word_at(m_, self + 0x5b4, word_at(m_, self + 0x5b4) - 10u);
        }
        if (global_byte(m_, kKeySpinRight) != 0) {
            set_word_at(m_, self + 0x5b4, word_at(m_, self + 0x5b4) + 10u);
        }
        const Word step = (global_byte(m_, kKeyFast) != 0 ? 0x18u : 0u) + 8u;
        if (global_byte(m_, kKeyDistanceDown) != 0) {
            set_global_word(m_, kDistance, global_word(m_, kDistance) - step);
        }
        if (global_byte(m_, kKeyDistanceUp) != 0) {
            set_global_word(m_, kDistance, global_word(m_, kDistance) + step);
        }
        Word pitch = global_word(m_, kPitch);
        if (global_byte(m_, kKeyPitchDown) != 0) {
            pitch -= step;
        }
        if (global_byte(m_, kKeyPitchUp) != 0) {
            pitch += step;
        }
        set_global_word(m_, kPitch, pitch & 0xfffu);
        if (global_byte(m_, kKeyHeightDown) != 0) {
            set_global_word(m_, kYOffset, global_word(m_, kYOffset) - step);
        }
        if (global_byte(m_, kKeyHeightUp) != 0) {
            set_global_word(m_, kYOffset, global_word(m_, kYOffset) + step);
        }
    }

    // what the free camera and the mode machinery publish for the shake and the lens
    set_global_word(m_, kPitchOut, global_word(m_, kPitch));
    set_global_word(m_, kDistanceOut, global_word(m_, kDistance));
    set_global_word(m_, kShakeOffset, 0);
    set_global_word(m_, kShakeOffset + 4, global_word(m_, kYOffset) << 12);
    set_global_word(m_, kShakeOffset + 8, 0);
    if (global_word(m_, kFrozen) != 0) {
        return;
    }
    if (global_word(m_, kDebugMode) != 0) {
        if (global_word(m_, kDebugFlyOn) == 0) {
            return;
        }
        follow_target(self);
        smooth_and_validate(self, 0x137);
        for (Word k = 0; k < 4; ++k) {
            set_word_at(m_, self + 0x470 + 4 * k, word_at(m_, self + 0x45c + 4 * k));
        }
        for (Word k = 0; k < 4; ++k) {
            set_word_at(m_, self + 0x448 + 4 * k, word_at(m_, self + 0x470 + 4 * k));
        }
        commit_viewport(self);
        return;
    }

    set_global_half(m_, kShakeWords, 0);
    set_global_half(m_, kShakeWords + 2, 0);
    // F1.5D: the camera's counters are in 30 Hz frames: with the flag a frame adds the frames it is worth (0 or 1 at 60 fps); retail adds 1
    const Word frames_worth = framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); });
    cam_at(m_, self).set_update_tick(static_cast<Word>(cam_at(m_, self).update_tick() + frames_worth));
    for (Word k = 0; k < frames_worth; ++k) {
        const Word ramp = static_cast<std::uint16_t>(cam_at(m_, self).field_410_s16());
        if (ramp != 0) {
            set_word_at(m_, self + kViewportParameter, word_at(m_, self + kViewportParameter) + word_at(m_, self + kViewportRamp + 4));
            cam_at(m_, self).set_field_410_s16(static_cast<std::int16_t>(ramp - 1u));
        }
    }
    camera_stamp(0xc2e);
    check(global_word(m_, kCurrentViewport) != 0, kMessageNull);
    camera_stamp(0xc2f);
    check(global_word(m_, kCurrentViewport) != 0xffffffffu, kMessageMinusOne);
    set_half_at(m_, global_word(m_, kCurrentViewport) + 0xe, half_at(m_, self + kViewportParameter));
    core_.look_angles(self + 0x14, self + 8, self + kTarget);

    // the lift: the skater's vertical speed raises the look-at point for a few frames
    Word tripod = cam_at(m_, self).tripod_link().address;
    if (tripod == 0 || followed_at(m_, tripod).field_31ec() == 0 || static_cast<std::uint16_t>(cam_at(m_, self).field_436_s16()) == 0) {
        cam_at(m_, self).set_field_5ec(0);
        cam_at(m_, self).set_field_5f0(0);
    } else {
        const Word vertical = word_at(m_, tripod + 0x50);
        const Word frames = static_cast<Word>(cam_at(m_, self).field_5f0()) + (s(vertical) < 0 ? frames_worth : 0u - frames_worth);
        cam_at(m_, self).set_field_5f0(static_cast<std::int32_t>(frames));
        const Word added = u(sar(s(0u - word_at(m_, tripod + 0x50)), 12)) * u(s16(static_cast<std::uint16_t>(cam_at(m_, self).field_436_s16()))) * frames_worth;
        const Word sum = static_cast<Word>(cam_at(m_, self).field_5ec()) + added;
        cam_at(m_, self).set_field_5ec(static_cast<std::int32_t>(sum));
        if (s(frames) > 0) {
            set_word_at(m_, self + kAnchor + 4, word_at(m_, self + kAnchor + 4) + sum);
        }
    }
    // the roll: the skater's vertical speed tips the pitch
    tripod = cam_at(m_, self).tripod_link().address;
    if (tripod != 0 && static_cast<std::uint16_t>(cam_at(m_, self).field_434_s16()) != 0) {
        if (int_at(m_, tripod + 0x50) < 0) {
            set_global_word(m_, kPitchOut, global_word(m_, kPitchOut) - u(s16(static_cast<std::uint16_t>(cam_at(m_, self).field_434_s16()))));
        }
        if (int_at(m_, cam_at(m_, self).tripod_link().address + 0x50) > 0) {
            set_global_word(m_, kPitchOut, global_word(m_, kPitchOut) - u(sar(s16(static_cast<std::uint16_t>(cam_at(m_, self).field_434_s16())), 1)));
        }
        set_global_word(m_, kPitchOut, global_word(m_, kPitchOut) & 0xfffu);
    }

    // the mode: the anchor's previous position is kept across it
    copy_vector(kSaved, self + kAnchorPrevious);
    if (global_word(m_, kMoviePending) != 0 && (engine_value(kVideoPlaying) & 0xffu) == 0) {
        cam_at(m_, self).set_field_4a9(1);
        copy_vector(self + kAnchorPrevious, cam_at(m_, self).tripod_link().address + 8);
    }
    const auto follow_landing = [&]() {     // 0x0040ff2b
        const Word skater = cam_at(m_, self).tripod_link().address;
        if (skater != 0) {
            if (int_at(m_, skater + 0x3110) > 0x64 && int_at(m_, skater + 0x30b8) == 1) {
                cam_at(m_, self).set_camera_mode(0x19);
            }
            if (cam_at(m_, self).field_4a9() != 0) {
                set_word_at(m_, upd(5), 0);
                set_word_at(m_, upd(5) + 4, 0);
                set_word_at(m_, upd(5) + 8, 0xfffff000u);
                math_.convert_vector(skater + 0x2e58, upd(5), upd(6));
                set_word_at(m_, self + kDirectionTarget, word_at(m_, upd(6)));
                set_word_at(m_, self + kDirectionTarget + 4, word_at(m_, upd(6) + 4));
                set_word_at(m_, self + kDirectionTarget + 8, word_at(m_, upd(6) + 8));
                const Word shifted = core_.vector_shift_left_out(upd(7), self + kDirectionTarget, word(8, 12));
                copy_vector(self + kDirectionPrevious, shifted);
                copy_vector(self + kDirection, shifted);
            }
        }
        follow_target(self);
    };
    if (freelook::enabled()) {   // the free look (free_look.hpp) steps once per update, here: a paused game (the frozen return above) holds it. Off: nothing is read
        const int player = static_cast<int>(cam_at(m_, self).field_0c8());
        freelook::advance(player, s(global_word(m_, kTimeScale)), [&] {   // (asked only when it can change the look; the fixed test look is held for every camera)
            return freelook::state().fixed || freelook::allowed({player, cam_at(m_, self).tripod_link().address != 0, cam_at(m_, self).camera_mode(), global_word(m_, 0x00568658) == 1,
                                                                 global_word(m_, 0x0056e5d8) == 2, cam_at(m_, self).update_tick() < 0xc});
        });
    }
    bool restore = true;
    switch (cam_at(m_, self).camera_mode()) {
    case 1:
        follow_landing();
        break;
    case 2:
        mode_2(self);
        break;
    case 23:
        point_mode(self);
        restore = false;
        break;
    case 24:
        death_mode(self);
        restore = false;
        break;
    case 25: {
        follow_target(self);
        smooth_and_validate(self, 0x2c7);
        const Word skater = cam_at(m_, self).tripod_link().address;
        if (int_at(m_, skater + 0x30b8) != 1 && word_at(m_, skater + 0x2f64) == 0 && int_at(m_, skater + 0x3110) < 0) {
            cam_at(m_, self).set_camera_mode(1);
        }
        follow_landing();
        break;
    }
    default:
        camera_stamp(0xcef);
        check(false, kMessageMode);
        break;
    }
    if (restore) {
        copy_vector(self + kAnchorPrevious, kSaved);

        // the smoothing step: 0x2c7 right after the camera was placed, 0x97 when the point stood still (or just started following), else 0x137
        Word step = 0x137;
        tripod = cam_at(m_, self).tripod_link().address;
        if (tripod == 0 || followed_at(m_, tripod).field_2f64() == 0) {
            if (cam_at(m_, self).field_4a9() != 0) {
                cam_at(m_, self).set_field_4a9(0);
                step = 0x2c7;
            } else if (vectors_equal(self + kAnchor, self + kAnchorPrevious)) {
                step = 0x97;
            } else if (cam_at(m_, self).field_61c() < 4) {
                step = 0x97;
            } else {
                tripod = cam_at(m_, self).tripod_link().address;
                if (tripod != 0 && static_cast<std::int32_t>(followed_at(m_, tripod).physics_state()) == 1 && cam_at(m_, self).update_tick() > 0x11) {
                    step = 0x97;
                }
            }
        }
        smooth_and_validate(self, step);
        core_.look_angles(self + 0x14, self + 8, self + kTarget);

        // the shake: three damped swings (about x, y and z) composed on the render quaternion, which lose their decay each frame and stop at a sign change
        const Word amplitude = self + kShake;
        if (half_at(m_, amplitude) != 0 || half_at(m_, amplitude + 2) != 0 || half_at(m_, amplitude + 4) != 0) {
            const auto swing = [&](Word phase, Word magnitude) {
                const std::int32_t index = s((half_at(m_, self + phase) * global_word(m_, kShakeClock)) & 0xfffu);
                const Word product = u(math_.sin_q12(index)) * u(s16(half_at(m_, magnitude)));
                const Word bias = u(s(product) >> 31) & 0xfffu;
                return sar(s(product + bias), 12);
            };
            about_z(upd_quat(0), swing(0x500, amplitude + 4));
            about_x(upd_quat(1), swing(0x4fc, amplitude));
            about_y(upd_quat(2), swing(0x4fe, amplitude + 2));
            const Word first = compose(upd_quat(3), upd_quat(2), self + kRenderQuaternion);
            const Word second = compose(upd_quat(4), first, upd_quat(1));
            const Word third = compose(upd_quat(5), second, upd_quat(0));
            for (Word k = 1; k < 5; ++k) {
                set_word_at(m_, self + kRenderQuaternion + 4 * k, word_at(m_, third + 4 * k));
            }
            const Word old_x = half_at(m_, amplitude);
            const Word old_y = half_at(m_, amplitude + 2);
            const Word old_z = half_at(m_, amplitude + 4);
            for (Word k = 0; k < 3; ++k) {
                const Word value = half_at(m_, amplitude + 2 * k);
                const Word decay = byte_at(m_, self + 0x4f8 + k) * frames_worth;   // F1.5D: the amplitude loses its decay per 30 Hz frame
                set_half_at(m_, amplitude + 2 * k, (value & 0x8000u) != 0 ? value + decay : value - decay);
            }
            if (((half_at(m_, amplitude) ^ old_x) & 0x8000u) != 0) {
                set_half_at(m_, amplitude, 0);
            }
            if (((half_at(m_, amplitude + 2) ^ old_y) & 0x8000u) != 0) {
                set_half_at(m_, amplitude + 2, 0);
            }
            if (((half_at(m_, amplitude + 4) ^ old_z) & 0x8000u) != 0) {
                set_half_at(m_, amplitude + 4, 0);
            }
        }
    }
    commit_viewport(self);
}

}  // namespace opentony::runtime::camerakit
