#include "auto_views.hpp"
#include "video_kit.hpp"
#include "seam_addresses_generated.hpp"

#include <string>
#include <vector>

// Phase 4.15b: one frame of the replay (video.cpp, 0x004cbbb0..0x004cbc40, 0x004cc0a0..0x004cc190, 0x004ccd70, 0x004cd750..0x004cdab0). A frame holds, for each skater, the eight body-part
// records (position and angles), the position as a delta of a second difference, the orientation as three angle deltas, then the state, animation, frame, facing, flip, the time step, a sound
// command and the events of the frame (a splat, a trick object). Fields that did not change cost one bit; the widths of the others come from small prefix codes (the tables at 0x005481ac).

namespace opentony::runtime::videokit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

// the code tables: field widths and the prefix codes that select them (dwords)
constexpr Word kPositionWidths = 0x005481ac;       // six widths, then the limit 0x14
constexpr Word kPositionLimit = 0x005481c4;
constexpr Word kAngleWidths = 0x005481c8;          // six widths
constexpr Word kFlipWidths = 0x005481e4;           // six widths (0: none)
constexpr Word kPrefixCodes = 0x00548200;          // seven codes
constexpr Word kPrefixLengths = 0x0054821c;        // seven lengths

// the state block (state::k* and the rest this file uses)
constexpr Word kBodyPositions = 0xc4;              // eight records of 0x10 bytes: three dwords
constexpr Word kBodyAngles = 0x144;                // eight records of 8 bytes: three shorts
constexpr Word kPosition = 0x98;                   // three dwords: where the skater was (whole units)
constexpr Word kPreviousPosition = 0xa4;           // three dwords: one frame earlier
constexpr Word kAngles = 0x88;                     // three shorts (x, y, z) and a fourth word
constexpr Word kPreviousAngles = 0x90;
constexpr Word kLastState = 0x7c;
constexpr Word kLastAnimation = 0x80;
constexpr Word kLastFrame = 0x84;
constexpr Word kLastFlip = 0xb0;
constexpr Word kLastFlipStep = 0xb4;
constexpr Word kTime = 0xb8;
constexpr Word kPlayTime = 0xbc;

// the skater
constexpr Word kSkaterPosition = 0x08;
constexpr Word kSkaterPrevious = 0xbc;
constexpr Word kSkaterVelocity = 0x4c;
constexpr Word kSkaterFlags = 0xd8;
constexpr Word kSkaterFrame = 0xf4;                // short
constexpr Word kSkaterAnimation = 0xf6;            // short
constexpr Word kSkaterMatrix = 0x2e58;
constexpr Word kSkaterFlipCount = 0x2c88;
constexpr Word kSkaterFlipPhase = 0x2c8c;
constexpr Word kSkaterPlayerNumber = 0x2cc4;
constexpr Word kSkaterState = 0x30b8;
constexpr Word kSkaterWaterY = 0x3080;

constexpr Word kSimulationTime = globals::kSimulationTime;
constexpr Word kAdvancing = 0x0056e770;            // set while frames are skipped (their sounds stay quiet)
constexpr Word kTrickObjectList = 0x0056db90;
constexpr Word kTrickObjectOwner = 0x0056db78;
constexpr Word kTrickObjectPulsing = 0x0056db88;
constexpr Word kOffsetList = 0x0056e210;
constexpr Word kQueryOwner = 0x0056a954;
constexpr Word kPowerUpList = 0x0056b830;
constexpr Word kBaddyList = 0x0055f6bc;
constexpr Word kGameList = 0x0056af40;
constexpr Word kGlassBreakLimit = 0x0055f72c;
constexpr Word kGlassFlag = 0x005244cc;
constexpr Word kPauseFlag = 0x0056e698;
constexpr Word kRepeat = 0x0056e77c;
constexpr Word kStepFlag = 0x0056a8e0;
constexpr Word kStepFlag2 = 0x0056a90c;
constexpr Word kStepButtonA = 0x0056b009;
constexpr Word kStepButtonB = 0x0056b175;
constexpr Word kRepeatButtonA = 0x0056b018;
constexpr Word kRepeatButtonB = 0x0056b184;
constexpr Word kPadA = 0x0056aff8;                 // InputActionStates
constexpr Word kPadB = 0x0056b164;                 // pad2
constexpr Word kStatus = globals::kStatus;
constexpr Word kPadPressed = 0x0056b009;
constexpr Word kPadStride = 0x16c;

constexpr Word kBodyPartScratch = kScratch;                  // the matrix's angles, a stack local of Replay_WriteFrame is not needed: the angles go to the state block
constexpr Word kLineInfo = kScratch + 0x100;                 // the 0xa4-byte query record
constexpr Word kSphereStart = kScratch + 0x200;
constexpr Word kSphereEnd = kScratch + 0x210;
constexpr Word kSphereZero = kScratch + 0x220;
constexpr Word kGlassAngles = kScratch + 0x240;              // three shorts
constexpr Word kGlassCentre = kScratch + 0x250;              // three shorts
constexpr Word kGlassVector = kScratch + 0x260;              // three dwords

constexpr Word kMatrixToAngles = 0x004657c0;
constexpr Word kBoardOn = 0x0046c630;
constexpr Word kBoardOff = 0x0046c5f0;
constexpr Word kRotationAgain = 0x004e81a0;
constexpr Word kFruVectors = 0x0049c7d0;
constexpr Word kBuildTransform = 0x00461bf0;
constexpr Word kSleep = seam::hal::WINMAIN_Sleep;
constexpr Word kPlaySound = 0x004ad620;
constexpr Word kStopVoice = 0x004ad9f0;
constexpr Word kSplatEffect = 0x0046dd20;
constexpr Word kFindEnviroItem = 0x004b1ef0;
constexpr Word kGetNodeLinks = 0x004c8550;
constexpr Word kSendPulse = 0x004c58b0;
constexpr Word kActivateGoal = 0x004bdc40;
constexpr Word kInitQuery = 0x004624d0;
constexpr Word kResetCollisionFlags = 0x004c4b80;
constexpr Word kBroadphase = 0x004660b0;
constexpr Word kResetExecutedFlags = 0x004c4ba0;
constexpr Word kLineToSphere = 0x00464200;
constexpr Word kClearInputFlags = 0x00480fd0;
constexpr Word kQuickAi = 0x00481110;
constexpr Word kObjectAi = 0x00480ff0;
constexpr Word kSetupGlassA = 0x00406070;
constexpr Word kSetupGlassB = 0x00406090;
constexpr Word kNewGlass = 0x00403d30;
constexpr Word kGlassMove = 0x0040b060;
constexpr Word kRestoreGround = 0x0046d7a0;
constexpr Word kUpdateBoard = 0x0046d040;
constexpr Word kBounce = 0x004a12d0;
constexpr Word kHandleWater = 0x0046e080;
constexpr Word kCameraTrigger = 0x00411fc0;
constexpr Word kVideoScreen = 0x0044e9c0;

constexpr Word kMsgStreamFull = 0x00548444;
constexpr Word kMsgPosBits = 0x0054842c;
constexpr Word kMsgEulerX = 0x00548424;
constexpr Word kMsgEulerY = 0x0054841c;
constexpr Word kMsgEulerZ = 0x00548414;
constexpr Word kMsgStateLarge = 0x00548404;
constexpr Word kMsgAnimLarge = 0x005483f4;
constexpr Word kMsgFrameLarge = 0x005483e4;
constexpr Word kMsgFlipLarge = 0x005483d0;
constexpr Word kMsgSoundLarge = 0x005483c0;
constexpr Word kMsgBadSound = 0x005483a8;
constexpr Word kMsgSplat1 = 0x00548394;
constexpr Word kMsgSplat2 = 0x00548380;
constexpr Word kMsgTrick1 = 0x0054836c;
constexpr Word kMsgTrick2 = 0x00548358;
constexpr Word kMsgNullSkater = 0x0054833c;
constexpr Word kMsgNoStream = 0x0054834c;
constexpr Word kMsgNoMoreFrames = 0x005484a0;

inline std::int32_t asr(std::int32_t v, unsigned n) { return v >> n; }

}  // namespace

// 0x004cc0a0: the orientation matrix with three of its entries negated (the replay keeps the mirrored handedness) becomes three angles
void VideoKit::matrix_to_angles(Word matrix, Word angles) {
    set_half_at(m_, matrix + 2, 0u - half_at(m_, matrix + 2));
    set_half_at(m_, matrix + 8, 0u - half_at(m_, matrix + 8));
    set_half_at(m_, matrix + 0xe, 0u - half_at(m_, matrix + 0xe));
    engine(kMatrixToAngles, {angles, matrix});
    take_filled(angles, 6, pcplatform::engine_name(kMatrixToAngles));
}

// a service that filled `bytes` bytes at `destination`: the probe scripted them as dwords under the service's name, the last one padded
void VideoKit::take_filled(Word destination, Word bytes, const std::string& service) {
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

// 0x004cc0e0
void VideoKit::write_frame_checked(Word skater) {
    check(skater != 0, kMsgNullSkater);
    check(word_at(m_, state_of(skater) + state::kBoundary) != 0, kMsgNoStream);
    write_frame(skater);
}

// 0x004cc190: the frame of a skater goes to the end of its stream
void VideoKit::write_frame(Word skater) {
    const Word block = state_of(skater);
    const std::int32_t now = global_int(m_, kSimulationTime);
    if (word_at(m_, block + state::kBoundary) + 0x7b800u <= word_at(m_, block + state::kCursor) + 0x218u) {
        // the buffer is full: the recording ends
        log_line(kMsgStreamFull);
        reset_skater_input(skater);
        return;
    }
    set_word_at(m_, block + state::kFrames, word_at(m_, block + state::kFrames) + 1);
    for (Word k = 0; k < 8; ++k) {
        const Word position = block + kBodyPositions + 0x10 * k;
        const Word angles = block + kBodyAngles + 8 * k;
        for (Word j = 0; j < 3; ++j) {
            write_bits(block, u(asr(int_at(m_, position + 4 * j), 12)), 0x14);
        }
        for (Word j = 0; j < 3; ++j) {
            write_bits(block, u(short_at(m_, angles + 2 * j)), 0x10);
        }
    }
    // the position: the second difference of the whole-unit position, coded with the narrowest width that holds the three components
    const std::int32_t dx = asr(int_at(m_, skater + kSkaterPosition), 12) + s(word_at(m_, block + kPosition)) * -2 + s(word_at(m_, block + kPreviousPosition));
    const std::int32_t dy = asr(int_at(m_, skater + kSkaterPosition + 4), 12) + s(word_at(m_, block + kPosition + 4)) * -2 + s(word_at(m_, block + kPreviousPosition + 4));
    const std::int32_t dz = asr(int_at(m_, skater + kSkaterPosition + 8), 12) + s(word_at(m_, block + kPosition + 8)) * -2 + s(word_at(m_, block + kPreviousPosition + 8));
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, block + kPreviousPosition + 4 * j, word_at(m_, block + kPosition + 4 * j));
    }
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, block + kPosition + 4 * j, u(asr(int_at(m_, skater + kSkaterPosition + 4 * j), 12)));
    }
    const std::int32_t bits_x = s(bits_needed(dx));
    const std::int32_t bits_y = s(bits_needed(dy));
    const std::int32_t bits_z = s(bits_needed(dz));
    std::int32_t width = bits_x;
    if (width < bits_y) {
        width = bits_y;
    }
    if (width < bits_z) {
        width = bits_z;
    }
    check(width <= global_int(m_, kPositionLimit), kMsgPosBits);
    Word index = 0;
    for (Word entry = kPositionWidths; entry < kPositionLimit; entry += 4) {
        if (width <= int_at(m_, entry)) {
            break;
        }
        ++index;
    }
    write_bits(block, word_at(m_, kPrefixCodes + 4 * index), s(word_at(m_, kPrefixLengths + 4 * index)));
    write_bits(block, u(dx), s(word_at(m_, kPositionWidths + 4 * index)));
    write_bits(block, u(dy), s(word_at(m_, kPositionWidths + 4 * index)));
    write_bits(block, u(dz), s(word_at(m_, kPositionWidths + 4 * index)));

    // the orientation: three angle deltas
    const Word angles = block + kAngles;
    matrix_to_angles(skater + kSkaterMatrix, angles);
    set_half_at(m_, angles, half_at(m_, angles) & 0xfffu);
    set_half_at(m_, angles + 2, half_at(m_, angles + 2) & 0xfffu);
    set_half_at(m_, angles + 4, half_at(m_, angles + 4) & 0xfffu);
    std::int32_t turn_x = s((half_at(m_, angles) - half_at(m_, block + kPreviousAngles)) & 0xfffu);
    std::int32_t turn_y = s((half_at(m_, angles + 2) - half_at(m_, block + kPreviousAngles + 2)) & 0xfffu);
    std::int32_t turn_z = s((half_at(m_, angles + 4) - half_at(m_, block + kPreviousAngles + 4)) & 0xfffu);
    if (0x7ff < turn_x) {
        turn_x -= 0x1000;
    }
    if (0x7ff < turn_y) {
        turn_y -= 0x1000;
    }
    if (0x7ff < turn_z) {
        turn_z -= 0x1000;
    }
    check(half_at(m_, angles) == ((short_at(m_, block + kPreviousAngles) + static_cast<std::int16_t>(turn_x)) & 0xfffu), kMsgEulerX);
    check(half_at(m_, angles + 2) == ((short_at(m_, block + kPreviousAngles + 2) + static_cast<std::int16_t>(turn_y)) & 0xfffu), kMsgEulerY);
    check(half_at(m_, angles + 4) == ((short_at(m_, block + kPreviousAngles + 4) + static_cast<std::int16_t>(turn_z)) & 0xfffu), kMsgEulerZ);
    set_word_at(m_, block + kPreviousAngles, word_at(m_, angles));
    set_word_at(m_, block + kPreviousAngles + 4, word_at(m_, angles + 4));
    std::int32_t angle_width = s(bits_needed(turn_x));
    const std::int32_t angle_width_y = s(bits_needed(turn_y));
    const std::int32_t angle_width_z = s(bits_needed(turn_z));
    if (angle_width < angle_width_y) {
        angle_width = angle_width_y;
    }
    if (angle_width < angle_width_z) {
        angle_width = angle_width_z;
    }
    index = 0;
    for (Word entry = kAngleWidths; entry < kAngleWidths + 0x18; entry += 4) {
        if (angle_width <= int_at(m_, entry)) {
            break;
        }
        ++index;
    }
    write_bits(block, word_at(m_, kPrefixCodes + 4 * index), s(word_at(m_, kPrefixLengths + 4 * index)));
    write_bits(block, u(turn_x), s(word_at(m_, kAngleWidths + 4 * index)));
    write_bits(block, u(turn_y), s(word_at(m_, kAngleWidths + 4 * index)));
    write_bits(block, u(turn_z), s(word_at(m_, kAngleWidths + 4 * index)));

    // the state, the animation and its frame: one bit when unchanged
    check(int_at(m_, skater + kSkaterState) < 0x10, kMsgStateLarge);
    if (word_at(m_, skater + kSkaterState) == word_at(m_, block + kLastState)) {
        write_bits(block, 0, 1);
    } else {
        write_bits(block, word_at(m_, skater + kSkaterState) * 2u | 1u, 5);
    }
    set_word_at(m_, block + kLastState, word_at(m_, skater + kSkaterState));
    check(half_at(m_, skater + kSkaterAnimation) < 0x100, kMsgAnimLarge);
    if (static_cast<Word>(half_at(m_, skater + kSkaterAnimation)) == word_at(m_, block + kLastAnimation)) {
        write_bits(block, 0, 1);
    } else {
        write_bits(block, static_cast<Word>(half_at(m_, skater + kSkaterAnimation)) * 2u | 1u, 9);
    }
    set_word_at(m_, block + kLastAnimation, half_at(m_, skater + kSkaterAnimation));
    check(short_at(m_, skater + kSkaterFrame) < 0x100, kMsgFrameLarge);
    const std::int32_t step = static_cast<std::int32_t>(short_at(m_, skater + kSkaterFrame)) - s(word_at(m_, block + kLastFrame));
    if (step < 0 || 3 < step) {
        write_bits(block, u(static_cast<std::int32_t>(short_at(m_, skater + kSkaterFrame)) * 2) | 1u, 9);
    } else {
        write_bits(block, u(step * 2), 3);
    }
    set_word_at(m_, block + kLastFrame, u(static_cast<std::int32_t>(short_at(m_, skater + kSkaterFrame))));
    write_bits(block, (word_at(m_, skater + kSkaterFlags) >> 1) & 1u, 1);

    // the flip
    Word flip_delta = 0;
    if (word_at(m_, skater + kSkaterFlipCount) == 0) {
        write_bits(block, 0, 1);
        set_word_at(m_, block + kLastFlip, 0);
    } else {
        write_bits(block, 1, 1);
        const std::int32_t delta = s((word_at(m_, skater + kSkaterFlipPhase) - word_at(m_, block + kLastFlipStep)) - word_at(m_, block + kLastFlip));
        std::int32_t needed = 0;
        if (delta != 0) {
            needed = s(bits_needed(delta));
        }
        std::int32_t flip_index = 0;
        for (Word entry = kFlipWidths; entry < kFlipWidths + 0x18; entry += 4) {
            if (needed <= int_at(m_, entry)) {
                break;
            }
            ++flip_index;
        }
        check(flip_index != 6, kMsgFlipLarge);
        write_bits(block, word_at(m_, kPrefixCodes + 4 * u(flip_index)), s(word_at(m_, kPrefixLengths + 4 * u(flip_index))));
        flip_delta = u(delta);
        write_bits(block, flip_delta, s(word_at(m_, kFlipWidths + 4 * u(flip_index))));
        set_word_at(m_, block + kLastFlip, word_at(m_, skater + kSkaterFlipPhase));
    }
    set_word_at(m_, block + kLastFlipStep, flip_delta);

    // the time step: up to 15 ticks since the last frame
    std::int32_t elapsed = now - int_at(m_, block + kTime);
    if (elapsed < 0x10) {
        set_word_at(m_, block + kTime, u(now));
    } else {
        const std::int32_t before = now - elapsed;
        elapsed = 0xf;
        set_word_at(m_, block + kTime, u(before + 0xf));
    }
    write_bits(block, u(elapsed), 4);

    // a sound command
    if (sound_pending() == 0) {
        write_bits(block, 0, 1);
    } else {
        write_bits(block, 1, 1);
        constexpr Word kPopKind = kScratch + 0x300;
        constexpr Word kPopSound = kScratch + 0x304;
        pop_sound(kPopKind, kPopSound);
        const std::int32_t sound = global_int(m_, kPopSound);
        const std::int32_t kind = global_int(m_, kPopKind);
        check(sound < 0x200, kMsgSoundLarge);
        if (kind == 1) {
            write_bits(block, 1, 1);
            write_bits(block, u(sound), 9);
        } else if (kind == 2) {
            write_bits(block, 0, 1);
            write_bits(block, u(sound), 9);
        } else {
            check(false, kMsgBadSound);
        }
    }

    // the events: a splat of a body part, a trick object touched
    constexpr Word kSplatOther = 0x0056e43c;
    constexpr Word kSplatPlayer = 0x0056e440;
    constexpr Word kTrickPlayer = 0x0056e430;
    constexpr Word kTrickOther = 0x0056e434;
    const bool player_one = skater == global_word(m_, globals::kPlayerOne);
    if (global_word(m_, kSplatPlayer) == 0xffffffff && global_word(m_, kSplatOther) == 0xffffffff && global_word(m_, kTrickPlayer) == 0xffffffff && global_word(m_, kTrickOther) == 0xffffffff) {
        write_bits(block, 0, 1);
        return;
    }
    write_bits(block, 1, 1);
    if (player_one) {
        if (global_word(m_, kSplatPlayer) == 0xffffffff) {
            write_bits(block, 0, 1);
        } else {
            check(global_int(m_, kSplatPlayer) < 4, kMsgSplat1);
            write_bits(block, 1, 1);
            write_bits(block, global_word(m_, kSplatPlayer), 2);
            set_global_word(m_, kSplatPlayer, 0xffffffff);
        }
    } else {
        if (global_word(m_, kSplatOther) == 0xffffffff) {
            write_bits(block, 0, 1);
        } else {
            check(global_int(m_, kSplatOther) < 4, kMsgSplat2);
            write_bits(block, 1, 1);
            write_bits(block, global_word(m_, kSplatOther), 2);
            set_global_word(m_, kSplatOther, 0xffffffff);
        }
    }
    if (player_one) {
        if (global_word(m_, kTrickPlayer) != 0xffffffff) {
            check(global_int(m_, kTrickPlayer) < 0x200, kMsgTrick1);
            write_bits(block, 1, 1);
            write_bits(block, global_word(m_, kTrickPlayer), 9);
            set_global_word(m_, kTrickPlayer, 0xffffffff);
            return;
        }
    } else if (global_word(m_, kTrickOther) != 0xffffffff) {
        check(global_int(m_, kTrickOther) < 0x200, kMsgTrick2);
        write_bits(block, 1, 1);
        write_bits(block, global_word(m_, kTrickOther), 9);
        set_global_word(m_, kTrickOther, 0xffffffff);
        return;
    }
    write_bits(block, 0, 1);
}

// 0x004ccd70: the next frame of the stream into the skater. `mode` 1: the frame is played for the screen (the board and the transform are rebuilt, the clock is waited for); -1: skipped over
void VideoKit::apply_frame(Word skater, std::int32_t mode) {
    const Word block = state_of(skater);
    set_word_at(m_, block + state::kFramesLeft, word_at(m_, block + state::kFramesLeft) - 1u);
    for (Word k = 0; k < 8; ++k) {
        const Word position = block + kBodyPositions + 0x10 * k;
        const Word angles = block + kBodyAngles + 8 * k;
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, position + 4 * j, read_bits(block, 0x14) << 12);
        }
        for (Word j = 0; j < 3; ++j) {
            set_half_at(m_, angles + 2 * j, read_bits(block, 0x10));
        }
    }
    // the position
    const Word at = skater + kSkaterPosition;
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, skater + kSkaterPrevious + 4 * j, word_at(m_, at + 4 * j));
    }
    Word index = 0;
    do {
        if (read_flag(block) == 0) {
            break;
        }
        ++index;
    } while (index < 6);
    for (Word j = 0; j < 3; ++j) {
        const Word delta = read_signed_bits(block, s(word_at(m_, kPositionWidths + 4 * index)));
        set_word_at(m_, at + 4 * j, delta + (word_at(m_, block + kPosition + 4 * j) * 2u - word_at(m_, block + kPreviousPosition + 4 * j)));
    }
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, block + kPreviousPosition + 4 * j, word_at(m_, block + kPosition + 4 * j));
    }
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, block + kPosition + 4 * j, word_at(m_, at + 4 * j));
    }
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, at + 4 * j, word_at(m_, at + 4 * j) << 12);
    }
    // the velocity: this position less the previous one
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, skater + kSkaterVelocity + 4 * j, word_at(m_, at + 4 * j) - word_at(m_, skater + kSkaterPrevious + 4 * j));
    }
    // the orientation
    index = 0;
    do {
        if (read_flag(block) == 0) {
            break;
        }
        ++index;
    } while (index < 6);
    const Word turn_x = read_signed_bits(block, s(word_at(m_, kAngleWidths + 4 * index)));
    const Word turn_y = read_signed_bits(block, s(word_at(m_, kAngleWidths + 4 * index))) & 0xfffu;
    const Word turn_z = read_signed_bits(block, s(word_at(m_, kAngleWidths + 4 * index)));
    const Word angles = block + kAngles;
    set_half_at(m_, angles, (half_at(m_, block + kPreviousAngles) + (turn_x & 0xfffu)) & 0xfffu);
    set_half_at(m_, angles + 2, (half_at(m_, block + kPreviousAngles + 2) + turn_y) & 0xfffu);
    set_half_at(m_, angles + 4, (half_at(m_, block + kPreviousAngles + 4) + (turn_z & 0xfffu)) & 0xfffu);
    set_word_at(m_, block + kPreviousAngles, word_at(m_, angles));
    set_word_at(m_, block + kPreviousAngles + 4, word_at(m_, angles + 4));
    if (mode == 1) {
        math_.rotation_from_angles_again(angles, skater + kSkaterMatrix);
        set_half_at(m_, skater + kSkaterMatrix + 2, 0u - half_at(m_, skater + kSkaterMatrix + 2));
        set_half_at(m_, skater + kSkaterMatrix + 8, 0u - half_at(m_, skater + kSkaterMatrix + 8));
        set_half_at(m_, skater + kSkaterMatrix + 0xe, 0u - half_at(m_, skater + kSkaterMatrix + 0xe));
        engine(kFruVectors, {skater});
        engine(kBuildTransform, {skater});
    }
    // state, animation, frame, facing
    if (read_flag(block) == 1) {
        set_word_at(m_, skater + kSkaterState, read_bits(block, 4));
    }
    if (read_flag(block) == 1) {
        set_half_at(m_, skater + kSkaterAnimation, read_bits(block, 8));
    }
    set_word_at(m_, block + kLastAnimation, half_at(m_, skater + kSkaterAnimation));
    std::int16_t frame;
    if (read_flag(block) == 0) {
        frame = static_cast<std::int16_t>(read_bits(block, 2) + half_at(m_, block + kLastFrame));
    } else {
        frame = static_cast<std::int16_t>(read_bits(block, 8));
    }
    set_half_at(m_, skater + kSkaterFrame, u(frame));
    set_word_at(m_, block + kLastFrame, u(frame));
    if (read_flag(block) == 0) {
        set_word_at(m_, skater + kSkaterFlags, word_at(m_, skater + kSkaterFlags) & 0xfffffffdu);
    } else {
        set_word_at(m_, skater + kSkaterFlags, word_at(m_, skater + kSkaterFlags) | 2u);
    }
    // the flip
    if (read_flag(block) == 0) {
        set_word_at(m_, block + kLastFlip, 0);
        set_word_at(m_, block + kLastFlipStep, 0);
        set_word_at(m_, skater + kSkaterFlipPhase, 0);
        set_word_at(m_, skater + kSkaterFlipCount, 0);
    } else {
        set_word_at(m_, skater + kSkaterFlipCount, 1000);
        index = 0;
        do {
            if (read_flag(block) == 0) {
                break;
            }
            ++index;
        } while (index < 6);
        const Word width = word_at(m_, kFlipWidths + 4 * index);
        if (width != 0) {
            const Word delta = read_signed_bits(block, s(width));
            const Word phase = word_at(m_, block + kLastFlipStep) + delta + word_at(m_, block + kLastFlip);
            set_word_at(m_, skater + kSkaterFlipPhase, phase);
            set_word_at(m_, block + kLastFlip, phase);
            set_word_at(m_, block + kLastFlipStep, delta);
        }
    }
    // the clock: a played frame waits until its time
    const Word elapsed = read_bits(block, 4);
    const Word due = word_at(m_, block + kPlayTime) + elapsed;
    set_word_at(m_, block + kPlayTime, due);
    if (mode == 1 && global_word(m_, kSimulationTime) < due) {
        do {
            engine(kSleep, {0});
            // (the clock moves while the game sleeps: the host's answer is the new time)
            set_global_word(m_, kSimulationTime, static_cast<Word>(s_.value(pcplatform::engine_name(kSleep), global_int(m_, kSimulationTime))));
        } while (global_word(m_, kSimulationTime) < word_at(m_, block + kPlayTime));
    }
    // a sound command
    if (read_flag(block) != 0) {
        if (read_flag(block) == 0) {
            const Word sound = read_bits(block, 9);
            if (global_word(m_, kAdvancing) == 0) {
                engine(kStopVoice, {sound});
            }
        } else {
            const Word sound = read_bits(block, 9);
            if (global_word(m_, kAdvancing) == 0) {
                engine(kPlaySound, {sound, 0x2000, 0});
            }
        }
    }
    // the events
    if (read_flag(block) != 0) {
        if (read_flag(block) != 0) {
            const Word part = read_bits(block, 2);
            engine(kSplatEffect, {skater, part, 0xfa});
        }
        if (read_flag(block) != 0) {
            std::int32_t count = s(read_bits(block, 9));
            Word object = global_word(m_, kTrickObjectList);
            if (0 < count) {
                do {
                    object = word_at(m_, object + 0x10);
                    --count;
                } while (count != 0);
            }
            const Word item = engine_value(kFindEnviroItem, {word_at(m_, object + 4)});
            set_word_at(m_, object + 0x14, item);
            set_byte_at(m_, item + 5, byte_at(m_, item + 5) | 4u);
            set_word_at(m_, word_at(m_, object + 0x14) + 0x24, 0x202020);
            set_byte_at(m_, object + 0xa, 1);
            set_byte_at(m_, object + 0xb, skater == global_word(m_, globals::kPlayerOne) ? 1 : 2);
            set_global_word(m_, kTrickObjectOwner, byte_at(m_, object + 0xb));
            const Word links = engine_value(kGetNodeLinks, {half_at(m_, object + 8)});
            Word remaining = half_at(m_, links);
            Word link = links + 2;
            while (remaining != 0) {
                const Word node = half_at(m_, link);
                const Word record = word_at(m_, global_word(m_, kOffsetList) + node * 4);
                const std::uint16_t kind = half_at(m_, record);
                if (kind == 0xe || kind == 0xc) {
                    engine(kActivateGoal, {node});
                } else {
                    set_global_word(m_, kTrickObjectPulsing, 1);
                    engine(kSendPulse, {node});
                    set_global_word(m_, kTrickObjectPulsing, 0);
                }
                link += 2;
                --remaining;
            }
        }
    }
    // what the skater touched between the last two positions: trigger points, then the power-ups
    if (word_at(m_, block + kPreviousPosition) != 0 || word_at(m_, block + kPreviousPosition + 4) != 0 || word_at(m_, block + kPreviousPosition + 8) != 0) {
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, kLineInfo + 4 * j, word_at(m_, block + kPreviousPosition + 4 * j) << 12);
            set_word_at(m_, kLineInfo + 12 + 4 * j, word_at(m_, block + kPosition + 4 * j) << 12);
        }
        engine(kInitQuery, {kLineInfo});
        set_global_byte(m_, kLineInfo + 0x88, 1);
        const Word player = word_at(m_, skater + kSkaterPlayerNumber) + 1u;
        engine(kResetCollisionFlags, {player});
        set_global_word(m_, kQueryOwner, skater);
        engine(kBroadphase, {kLineInfo, 1});
        set_global_word(m_, kQueryOwner, 0);
        engine(kResetExecutedFlags, {player});
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, kSphereStart + 4 * j, word_at(m_, block + kPreviousPosition + 4 * j) << 12);
            set_word_at(m_, kSphereEnd + 4 * j, word_at(m_, block + kPosition + 4 * j) << 12);
            set_word_at(m_, kSphereZero + 4 * j, 0);
        }
        const Word hit = engine_value(kLineToSphere, {kSphereStart, kSphereEnd, kSphereZero, global_word(m_, kPowerUpList), 0, 0x1000});
        if (hit != 0) {
            virtual_call(hit, 2);
        }
    }
    // the board follows the animation: off in the tricks that throw it away, on otherwise
    const std::int16_t animation = short_at(m_, skater + kSkaterAnimation);
    const std::int32_t frame_now = short_at(m_, skater + kSkaterFrame);
    bool off = false;
    switch (animation) {
        case 0x2c: case 0x2e: case 0x36: case 0x37: case 0x88: case 0x8a: case 0xb1: case 0xcb:
            off = true;
            break;
        case 0x89:
            off = !(frame_now < 0x16);
            break;
        case 0xcc:
            off = !(frame_now > 0x25);
            break;
        case 0xce:
            off = !(frame_now > 0x23);
            break;
        case 0xcf:
            off = !(frame_now > 0x19);
            break;
        default:
            break;
    }
    engine(off ? kBoardOff : kBoardOn, {skater});
    if (mode != 1) {
        set_word_at(m_, block + kPlayTime, global_word(m_, kSimulationTime));
    }
}

// 0x004cbbb0: `count` frames are played without being shown: every object's input flags are cleared, the frame applied to each skater, and the objects run
void VideoKit::advance_frames(std::int32_t count) {
    const Word second = global_word(m_, globals::kPlayerOne + 4);
    const Word first = global_word(m_, globals::kPlayerOne);
    set_global_word(m_, kAdvancing, 1);
    if (count < 1) {
        set_global_word(m_, kAdvancing, 0);
        return;
    }
    do {
        engine(kClearInputFlags, {global_word(m_, kBaddyList)});
        engine(kClearInputFlags, {global_word(m_, kGameList)});
        apply_frame(first, -1);
        if (second != 0) {
            apply_frame(second, -1);
        }
        engine(kQuickAi, {kGameList});
        engine(kObjectAi, {kBaddyList});
        --count;
    } while (count != 0);
    set_global_word(m_, kAdvancing, 0);
}

// 0x004cbc40: skip over the stretch before the highlight now playing
void VideoKit::advance_highlight() {
    const Word cursor = global_word(m_, globals::kHighlightCursor);
    if (cursor == 1) {
        if (global_short(m_, globals::kHighlightRanges) != 1) {
            advance_frames(global_short(m_, globals::kHighlightRanges) - 1);
        }
        return;
    }
    advance_frames((static_cast<std::int32_t>(short_at(m_, 0x0056e624 + cursor * 8)) - static_cast<std::int32_t>(short_at(m_, 0x0056e61e + cursor * 8))) - 2);
}

// 0x00411fc0 (cdecl): the replay camera trigger. The level's camera points (the shorts at 0x0055fa58, counted at 0x0055fae4) are measured from the skater's position; the nearest the
// line of sight reaches, closer than 0x2328, takes the camera over once the one held before has been held 0x3c ticks. The registry flags (0x00524cb8 + 4 * point) choose the camera
// mode (0x800: a fixed view, 0x400: a tripod) and the framing of the view at 0x00563a38. The registry's first word is the retail code's scratch for the flags it builds (written
// whichever point is taken); the replay of the retail calls checks that too.
void VideoKit::camera_trigger(Word skater) {
    constexpr Word kCandidate = kScratch + 0x340;     // the view position (a local of the retail function)
    constexpr Word kBest = kScratch + 0x350;          // the nearest point
    constexpr Word kSecond = kScratch + 0x360;        // the nearest but one
    constexpr Word kNode = kScratch + 0x370;          // the node position TRG_DecodeNodePosition fills
    constexpr Word kPointList = 0x0055fa58;
    constexpr Word kPointCount = 0x0055fae4;
    constexpr Word kRegistry = 0x00524cb8;
    constexpr Word kClock = 0x0056e320;
    constexpr Word kViewState = 0x00563a38;
    constexpr Word kFrameWord = 0x00563a3c;
    constexpr Word kFrameStart = 0x00563a44;
    constexpr Word kRangeStart = 0x00567ca4;
    constexpr Word kRangeEnd = 0x00567ca8;
    constexpr Word kFraming = 0x00524a40;
    constexpr Word kLineOfSight = 0x004ca0c0;
    constexpr Word kDecodeNode = 0x004c8650;
    constexpr Word kCrapDistance = 0x004c9620;
    constexpr Word kRandom = 0x004c9340;
    constexpr Word kDistance = 0x004c9590;
    // the playback state block's own words
    constexpr Word kCurrent = 0x28;                   // the point held now
    constexpr Word kCurrentPoint = 0x2c;              // its position (three dwords)
    constexpr Word kHeldAt = 0x3c;                    // the clock when it was taken
    constexpr Word kSkip = 0x40;                      // a point left out of the search
    constexpr Word kBlocked = 0x44;                   // the frames the view has been blocked
    constexpr Word kPrevious = 0x48;                  // the point held before
    constexpr Word kVariant = 0x4c;                   // the camera variant of the point held
    constexpr Word kNone = 0xffffffffu;

    const Word camera = word_at(m_, skater + 0x29b0);
    const Word state = state_of(skater);
    // the two positions a service is handed: the probe records the bytes they hold (the locals' addresses are not repeatable)
    const auto positions = [this](Word a, Word b) {
        std::string text;
        for (Word k = 0; k < 12; ++k) {
            text.push_back(static_cast<char>(byte_at(m_, a + k)));
        }
        for (Word k = 0; k < 12; ++k) {
            text.push_back(static_cast<char>(byte_at(m_, b + k)));
        }
        return text;
    };
    const auto two_points = [this, &positions](Word address, Word a, Word b) {
        if (s_.live()) {
            engine(address, {a, b});
        } else {
            engine(address, {}, positions(a, b));
        }
        return static_cast<Word>(s_.value(pcplatform::engine_name(address)));
    };
    // TRG_DecodeNodePosition(out, node): the position goes to `out`, the answer points at the words after it (the camera point's state word and variant)
    const auto decode_node = [this](Word node) {
        const std::string name = pcplatform::engine_name(kDecodeNode);
        if (s_.live()) {
            engine(kDecodeNode, {kNode, node});
        } else {
            engine(kDecodeNode, {node});
            take_filled(kNode, 12, name);
        }
        return static_cast<Word>(s_.value(name));
    };
    const auto copy_point = [this](Word to, Word from) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, to + 4 * k, word_at(m_, from + 4 * k));
        }
    };

    Word best = kNone;
    Word best_distance = 0x7fffffffu;
    Word second = kNone;
    Word second_distance = 0;
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kBest + 4 * k, 0);
        set_word_at(m_, kSecond + 4 * k, 0);
        set_word_at(m_, kCandidate + 4 * k, word_at(m_, skater + kSkaterPosition + 4 * k) + word_at(m_, skater + 0x310c + 4 * k) * 0x6e);
    }
    if (two_points(kLineOfSight, kCandidate, camera + 8) != 0) {
        set_word_at(m_, state + kBlocked, 0);
    } else {
        const Word blocked = word_at(m_, state + kBlocked) + 1;
        set_word_at(m_, state + kBlocked, blocked);
        if (s(blocked) > 10) {
            set_word_at(m_, state + kSkip, word_at(m_, state + kCurrent));
        }
    }
    if (int_at(m_, kPointCount) > 0) {
        Word index = 0;
        do {
            if (index != word_at(m_, state + kSkip)) {
                (void)decode_node(u(short_at(m_, kPointList + 2 * index)));
                const Word distance = two_points(kCrapDistance, skater + kSkaterPosition, kNode);
                if (distance < best_distance) {
                    copy_point(kSecond, kBest);
                    second_distance = best_distance;
                    second = best;
                    copy_point(kBest, kNode);
                    best_distance = distance;
                    best = index;
                }
            }
            ++index;
        } while (s(index) < int_at(m_, kPointCount));
    }
    if (best != word_at(m_, state + kCurrent) && best != kNone && two_points(kLineOfSight, kCandidate, kBest) == 0) {
        // the nearest is out of sight: the one after it, if that is in sight
        best = second;
        if (best != word_at(m_, state + kCurrent) && best != kNone && two_points(kLineOfSight, kCandidate, kSecond) != 0) {
            copy_point(kBest, kSecond);
            best_distance = second_distance;
        } else {
            best = kNone;
        }
    }
    if (best_distance <= 0x2328 && best != kNone && best != word_at(m_, state + kCurrent) &&
        (best != word_at(m_, state + kPrevious) || !(global_word(m_, kClock) < word_at(m_, state + kHeldAt) + 0x3c))) {
        const Word record = decode_node(u(short_at(m_, kPointList + 2 * best)));
        const Word variant = u(short_at(m_, record + 2));
        Word flags = word_at(m_, kRegistry) & ~0xc00u;
        flags |= short_at(m_, record) == 1 ? 0x400u : 0x800u;
        set_word_at(m_, kRegistry, flags);
        const Word roll = engine_value(kRandom, {5});
        flags = (word_at(m_, kRegistry) & ~0xffu) + (roll + 5) * 10;
        set_word_at(m_, kRegistry, flags);
        set_word_at(m_, kRegistry + 4 * best, flags);
        if (best_distance > 0xfa0) {
            std::int32_t rest = 0x2710 - s(best_distance);
            if (rest < 0) {
                rest = 0;
            }
            const std::int32_t scaled = rest * 30;
            std::int32_t quotient = static_cast<std::int32_t>((static_cast<std::int64_t>(0x57619f1) * scaled) >> 32) >> 7;
            quotient += static_cast<Word>(quotient) >> 31;
            set_word_at(m_, kRegistry, (word_at(m_, kRegistry) & ~0xffu) + u(quotient) + 0xa);
        }
        set_word_at(m_, state + kHeldAt, global_word(m_, kClock));
        set_word_at(m_, state + kPrevious, word_at(m_, state + kCurrent));
        set_word_at(m_, state + kCurrent, best);
        copy_point(state + kCurrentPoint, kBest);
        const Word entry = word_at(m_, kRegistry + 4 * best);
        if ((entry & 0x800u) != 0) {
            set_word_at(m_, camera + 0x504, 1);
            set_byte_at(m_, camera + 0x3e0, 1);
            set_word_at(m_, camera + 0x3dc, skater);
            set_byte_at(m_, camera + 0x3bc, 1);
            set_byte_at(m_, camera + 0x3ac, 1);
            set_word_at(m_, camera + 0x3a4, skater);
            set_word_at(m_, state + kVariant, variant);
        } else if ((entry & 0x400u) != 0) {
            set_word_at(m_, camera + 0x504, 2);
            set_byte_at(m_, camera + 0x3e0, 1);
            set_word_at(m_, camera + 0x3dc, skater);
            set_byte_at(m_, camera + 0x3bc, 1);
            set_byte_at(m_, camera + 0x3ac, 0);
            copy_point(camera + 0x3c0, kBest);
            set_word_at(m_, camera + 0x3a4, 0);
            set_byte_at(m_, camera + 0x3bc, 1);
        }
    }
    // the framing of the view for the point held
    const Word held = word_at(m_, state + kCurrent);
    const Word flags = word_at(m_, kRegistry + 4 * held);
    const Word view = global_word(m_, kViewState);
    if ((flags & 0x800u) != 0) {
        set_word_at(m_, camera + 0x5b4, 0);
        const Word variant = word_at(m_, state + kVariant);
        struct Framing {
            Word word;
            Word x;
            Word y;
            Word z;
            Word rotation;
        };
        const Framing* framing = nullptr;
        static constexpr Framing kFramings[7] = {
            {0x96a, 0xf5, 0xd7, 0xffffffbau, 0},      {0xb72, 0xaf, 0x32, 0xffffffefu, 0}, {0xd52, 0x122, 0x96, 0xffffff94u, 0}, {0, 0, 0, 0, 0},
            {0x96a, 0xf5, 0xd7, 0xffffffbau, 0x800}, {0xb72, 0xc3, 0x32, 0xffffffefu, 0x800}, {0xd52, 0x122, 0x96, 0xffffff94u, 0x800},
        };
        if (variant <= 6 && variant != 3) {
            framing = &kFramings[variant];
        }
        if (framing != nullptr) {
            set_half_at(m_, view + 0xc, framing->word);
            set_word_at(m_, kFraming, framing->x);
            set_word_at(m_, kFraming + 4, framing->y);
            set_word_at(m_, kFraming + 8, framing->z);
            set_word_at(m_, camera + 0x5b4, framing->rotation);
        }
    } else if ((flags & 0x400u) != 0) {
        if ((flags & 0x200u) != 0) {
            set_half_at(m_, view + 0xc, 0xb2c - (flags & 0xffu) * 10);
        } else if ((flags & 0x100u) != 0) {
            // the view's distance from the point held sets the framing word, clamped to 0x1c2..0x960, and the frame range moves by the distance
            const Word distance = engine_value(kDistance, {skater + kSkaterPosition, state + kCurrentPoint});
            Word clamped = distance;
            if (s(distance) > 0x960) {
                clamped = 0x960;
            } else if (s(distance) < 0x1c2) {
                clamped = 0x1c2;
            }
            const std::int32_t numerator = s((flags & 0xffu) * 0x8552u);
            set_half_at(m_, view + 0xc, u(numerator / s(clamped)));
            const Word start = global_word(m_, kRangeStart);
            const Word end = global_word(m_, kRangeEnd);
            const Word moved = start + distance;
            set_global_word(m_, kFrameStart, moved);
            set_global_word(m_, kFrameWord, end - start + moved);
        }
    }
}

// 0x004cd750: a frame of the playback; at the end of the stream the next highlight, or the video screen
void VideoKit::playback(Word skater) {
    check(skater != 0, kMsgNullSkater);
    engine(kCameraTrigger, {skater});
    const Word block = state_of(skater);
    if (word_at(m_, block + state::kFramesLeft) != 0) {
        apply_frame(skater, 1);
        bool board_trick = false;
        switch (half_at(m_, skater + kSkaterAnimation)) {
            case 0x28: case 0x29: case 0x2a: case 0x39: case 0x3a: case 0x41: case 0x42: case 0x44: case 0x47: case 0x48: case 0x8b: case 0x8c:
                board_trick = true;
                break;
            default:
                break;
        }
        if (int_at(m_, skater + kSkaterState) == 4 && (board_trick || half_at(m_, skater + kSkaterAnimation) == 0x3a) && global_int(m_, kGlassBreakLimit) < 0xfa) {
            // the glass of a window the skater is going through breaks: nine shards
            set_global_half(m_, kGlassAngles, half_at(m_, block + kAngles));
            set_global_half(m_, kGlassAngles + 2, half_at(m_, block + kAngles + 2));
            set_global_half(m_, kGlassAngles + 4, half_at(m_, block + kAngles + 4));
            engine(kSetupGlassA, {kGlassAngles});
            set_global_half(m_, kGlassCentre, 0x1c2);
            set_global_half(m_, kGlassCentre + 2, 900);
            set_global_half(m_, kGlassCentre + 4, 0);
            engine(kSetupGlassB, {kGlassCentre});
            set_global_word(m_, kGlassFlag, 0);
            set_global_word(m_, kGlassVector, 0);
            set_global_word(m_, kGlassVector + 4, 0x19000);
            set_global_word(m_, kGlassVector + 8, 0);
            for (Word shard = 0; shard < 9; ++shard) {
                const Word bit = engine_value(kNewGlass, {0x6c});
                if (bit != 0) {
                    engine(kGlassMove, {bit, skater, kGlassVector, 0xffffffd8, 0x1000, 5});
                }
            }
            set_global_word(m_, kGlassFlag, 1);
        }
        const std::int32_t climb = s(word_at(m_, skater + 0xc) - word_at(m_, skater + kSkaterWaterY));
        const Word mask = u(climb >> 31);
        if (word_at(m_, skater + kSkaterState) == 0 && s((mask ^ u(climb)) - mask) <= 0x40) {
            set_word_at(m_, skater + 0x90, word_at(m_, skater + 8));
            set_word_at(m_, skater + 0x98, word_at(m_, skater + 0x10));
        } else {
            set_word_at(m_, skater + kSkaterWaterY, word_at(m_, skater + 0xc));
            engine(kRestoreGround, {skater});
        }
        engine(kUpdateBoard, {skater});
        if (word_at(m_, block + kPreviousPosition) != 0 || word_at(m_, block + kPreviousPosition + 4) != 0 || word_at(m_, block + kPreviousPosition + 8) != 0) {
            engine(kBounce, {skater});
        }
        engine(kHandleWater, {skater});
        return;
    }
    log_line(kMsgNoMoreFrames);
    const std::int32_t current = int_at(m_, block + state::kHighlightCurrent);
    if (0 < current && current < 5 && word_at(m_, block + state::kHighlights + 8 * static_cast<Word>(current) + 4) != 0) {
        const std::int32_t end = short_at(m_, block + state::kHighlights + 8 * static_cast<Word>(current) + 2);
        if (end != 0x7fffffff) {
            set_word_at(m_, block + state::kFramesLeft, u((end - short_at(m_, block + state::kHighlights + 8 * static_cast<Word>(current))) + 1));
        }
        set_global_word(m_, globals::kHighlightCursor, global_word(m_, globals::kHighlightCursor) + 1u);
        set_word_at(m_, block + state::kHighlightCurrent, word_at(m_, block + state::kHighlightCurrent) + 1u);
        advance_highlight();
        return;
    }
    const std::int32_t status = global_int(m_, kStatus);
    if (status != 0x14 && status != 0x16 && status != 0x15) {
        reset_skater_input(skater);
        if (global_word(m_, globals::kNumberOfPlayers) == 1 || skater == global_word(m_, globals::kPlayerOne)) {
            engine(kVideoScreen);
        }
    }
}

// 0x004cda90
Word VideoKit::is_paused() { return global_word(m_, globals::kState0) == 2 && global_word(m_, kPauseFlag) != 0 ? 1u : 0u; }

// 0x004cdab0: the pause and frame-step buttons of the video screen
void VideoKit::handle_stepping() {
    const bool restart = global_byte(m_, globals::kRestartActive) != 0;
    if (global_word(m_, globals::kState0) == 2 && !restart) {
        const std::int32_t status = global_int(m_, kStatus);
        if (status == 0x14 || status == 0x16 || status == 0x15) {
            return;
        }
        const Word players = global_word(m_, globals::kNumberOfPlayers);
        Word paused;
        if (global_byte(m_, kStepButtonA) != 0 || (players == 2 && global_byte(m_, kStepButtonB) != 0)) {
            paused = global_word(m_, kPauseFlag) ^ 1u;
            set_global_byte(m_, kStepButtonA, 0);
            set_global_byte(m_, kStepButtonB, 0);
            set_global_word(m_, kPauseFlag, paused);
            set_global_word(m_, kStepFlag, paused);
            set_global_word(m_, kStepFlag2, paused);
        } else {
            paused = global_word(m_, kPauseFlag);
        }
        if (paused == 0) {
            return;
        }
        set_global_word(m_, kStepFlag, 1);
        if (global_byte(m_, kRepeatButtonA) != 0 || (players == 2 && global_byte(m_, kRepeatButtonB) != 0)) {
            if (global_word(m_, kRepeat) != 0) {
                set_global_word(m_, kRepeat, global_word(m_, kRepeat) - 1u);
                return;
            }
            set_global_word(m_, kRepeat, 6);
            set_global_word(m_, kStepFlag, 0);
            return;
        }
        if (global_byte(m_, kPadA) == 0 && (players != 2 || global_byte(m_, kPadB) == 0)) {
            set_global_word(m_, kRepeat, 0);
            return;
        }
        if (global_word(m_, kRepeat) != 0) {
            set_global_word(m_, kRepeat, global_word(m_, kRepeat) - 1u);
            return;
        }
        set_global_word(m_, kRepeat, 1);
        set_global_word(m_, kStepFlag, 0);
        return;
    }
    if (!restart) {
        return;
    }
    // (a restart in progress: any button of a pad is a press)
    for (Word pad = kPadPressed; static_cast<std::int32_t>(pad) < 0x56b2e1; pad += kPadStride) {
        const bool any = byte_at(m_, pad - 0x10) != 0 || byte_at(m_, pad) != 0 || byte_at(m_, pad + 0x30) != 0 || byte_at(m_, pad + 0x40) != 0 || byte_at(m_, pad + 0x50) != 0 || byte_at(m_, pad + 0x60) != 0 ||
                         byte_at(m_, pad + 0x70) != 0 || byte_at(m_, pad + 0x80) != 0 || byte_at(m_, pad + 0x90) != 0 || byte_at(m_, pad + 0xa0) != 0 || byte_at(m_, pad + 0x10) != 0 || byte_at(m_, pad + 0xe0) != 0;
        if (any) {
            set_byte_at(m_, pad + 0x20, 1);
        }
    }
}

}  // namespace opentony::runtime::videokit
