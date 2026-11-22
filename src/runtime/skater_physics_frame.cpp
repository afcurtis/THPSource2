#include "frame_pacing.hpp"
#include "skater_views.hpp"

// Milestone 10g: the skater's physics frame (physics.cpp, 0x0049e680 Skater_PhysicsFrame), written from the machine code. The speed limits come from the stats (with the cheats, the slow-motion flag and the
// level's gravity), the action chain runs, the old position is kept, the dispatcher moves the skater, a wall bounce is tested with the zone query, the move is written and the frame's bookkeeping
// follows. The compiler's magic-number divisions are plain truncating divisions of the 32-bit products (which wrap as retail's `imul` does). Every function the frame calls is a service, except the
// vector and GTE helpers it uses as arithmetic (0x004cab50, 0x004f5f90, 0x004f53b0, 0x00465f60, 0x004e2ff0, 0x004cac30, 0x004cacd0, 0x004ca9f0), which run here.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }

constexpr Word kDustUpdate = 0x00431320;          // Dust_Update(skater) (thiscall on the skater's dust at +0x2c14)
constexpr Word kDustReset = 0x00431260;           // Dust_Reset(sound slot, 1) (thiscall on the dust)
constexpr Word kProfileValue = 0x0048f3a0;        // Skater_ProfileValue(selector) (thiscall)
constexpr Word kCheatOn = 0x00416980;            // Career_GetCheat(number): a bool in AL
constexpr Word kSetPanelNumber = 0x0048f5d0;
constexpr Word kHandleTimer = 0x0049e480;
constexpr Word kHandleFlips = 0x0049e430;
constexpr Word kRotationVectors = 0x0049c7d0;     // CBruce__FRUVectorsFromRotMatrix
constexpr Word kActionStep = 0x00493370;          // Skater_ActionPhysicsStep
constexpr Word kAcceleration = 0x0049b010;        // Skater_UpdateAcceleration
constexpr Word kPrePhysicsOllie = 0x0049a280;
constexpr Word kRequestStart = 0x004903f0;        // Animation_RequestStart(animation, source line)
constexpr Word kRequestFull = 0x00490480;         // Animation_RequestFull(animation, from, to, blend, source line)
constexpr Word kTrickScriptUpdate = 0x00492ea0;
constexpr Word kBrakeMode = 0x0049df00;           // Skater_UpdateBrakeMode
constexpr Word kContinuousSounds = 0x0049c970;    // CBruce__HandleContinuousSounds
constexpr Word kFloorRecovery = 0x00490730;       // Skater_OuterFloorRecoveryCheck(skater, x, y, z, message) (cdecl)
constexpr Word kNoBoardCheck = 0x0046c630;        // (thiscall) when +0x2f64 is clear
constexpr Word kRequestBloodSplat = 0x0046dd20;   // Skater_RequestBloodSplat(kind, count)
constexpr Word kDispatcher = 0x0049db80;          // Skater_PhysicsDispatcher
constexpr Word kBlockedReset = 0x0049d8a0;        // Skater_BlockedPhysicsReset
constexpr Word kTagCheckCaught = 0x004b8560;      // Tag_CheckCaught(skater) (cdecl)
constexpr Word kCollideOther = 0x0049d2f0;        // Skater_CollideWithOtherPlayer
constexpr Word kInitQuery = 0x004624d0;           // M3D_InitCollisionQuery(record)
constexpr Word kZoneQuery = 0x00466090;           // M3D_QueryZoneCollision(record, 1)
constexpr Word kBroadphaseQuery = 0x004660b0;      // M3D_ZoneCollisionBroadphase(record, 1)
constexpr Word kPublishBasis = 0x0049c850;        // Skater_PublishOrientationBasis
constexpr Word kWallResponse = 0x0049bad0;        // (thiscall, three arguments: the hit's normal words at +0x78 and +0x7c, 0x200)
constexpr Word kPositionWrite = 0x00496060;       // Skater_PositionWritePath(old x, old y, old z)
constexpr Word kLandingCleanup = 0x004914d0;
constexpr Word kCheckTriggers = 0x0049d740;       // CBruce__CheckForTriggers
constexpr Word kDampResponse = 0x0049d480;        // Skater_DampResponseVector
constexpr Word kNollieFlip = 0x0049e570;          // Skater_UpdateNollieFlip

// globals
constexpr Word kCurrentLevel = 0x0056a898;
constexpr Word kGravity = 0x0056b7c0;             // three dwords: the frame's gravity (0, -0x1000, 0)
constexpr Word kSlowMotion = 0x0056b7ec;
constexpr Word kFrameStep = 0x0056a93c;
constexpr Word kFrameClock = 0x005685f4;
constexpr Word kPhysicsActive = 0x00567c78;
constexpr Word kQueryOwner = 0x00567c7c;          // dword: set while the frame's own zone queries run (saved and restored)
constexpr Word kGameMode = 0x00533f38;
constexpr Word kTagState = 0x0056db64;
constexpr Word kTwoSkaterHold = 0x006a3b21;       // byte: the collision of the two skaters waits for frame 0xb4
constexpr Word kEditorMode = 0x0056a86c;
constexpr Word kSimulationTime = 0x0056e320;
constexpr Word kTimeScale = 0x0056865c;           // the scalar the move's response is multiplied by
constexpr Word kKillFrames = 0x005347b0;          // shorts: the frame of animation 0x2e that throws the blood, by character
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
const std::string kPhysicsFile = "H:\\TonyHawk\\Pc2\\physics.cpp";   // 0x00536a30

// the GTE words the cross products go through
constexpr Word kGteRotation0 = 0x006a3e10;
constexpr Word kGteRotation1 = 0x006a3e18;
constexpr Word kGteRotation2 = 0x006a3e20;
constexpr Word kGteIr = 0x006a3eb0;               // three shorts
constexpr Word kGteMac = 0x006a3eb8;              // three dwords: the result

// the zone query record (a stack local in retail)
constexpr Word kQuery = pcplatform::kScratchBase + 0xf50;
constexpr Word kQueryDistance = 0x40;
constexpr Word kQueryBody = 0x68;
constexpr Word kQueryNormal = 0x78;               // three shorts (the first two dwords are also passed whole)

}  // namespace

// the start and end of a query record as the probe's blob records them (the first 0x18 bytes)
std::string SkaterKit::query_line(Word record) const {
    std::string line;
    for (Word k = 0; k < 0x18; ++k) {
        line.push_back(static_cast<char>(byte_at(m_, record + k)));
    }
    return line;
}

// 0x0049e680 (thiscall)
void SkaterKit::physics_frame(Word self) {
    if (framepacing::runs_30hz_frame([&] { return global_word(m_, 0x0056a940); })) {   // F1.5F: the dust puffs once per 30 Hz frame
        thiscall(kDustUpdate, self + 0x2c14, {self});
    }
    set_global_word(m_, kGravity, 0);
    set_global_word(m_, kGravity + 4, 0xfffff000u);
    set_global_word(m_, kGravity + 8, 0);
    // the speed limit: 130 per unit of the stat word +0x29f4 (the level-9 form divides by 100 twice; the result is the same)
    const std::int32_t stat = static_cast<std::int16_t>(skater_at(m_, self).field_29f4());
    std::int32_t limit = mul(stat, 13000) / 100;
    if (global_word(m_, kCurrentLevel) == 9) {
        limit = mul(limit, 100) / 100;
    }
    set_word_at(m_, self + 0x2da8, 0);
    set_word_at(m_, self + 0x2dac, u(limit));
    set_word_at(m_, self + 0x2db0, 0);
    if (skater_at(m_, self).physics_state() == 2) {
        const std::int32_t value = s(thiscall_value(kProfileValue, self, {1}));
        set_word_at(m_, self + 0x2dac, u(mul(mul(500 - value, int_at(m_, self + 0x2dac)), 20) / 10000));
    }
    if ((engine_value(kCheatOn, {0xc}) & 0xff) != 0) {
        set_word_at(m_, self + 0x2dac, u(mul(int_at(m_, self + 0x2dac), 150) / 100));
    }
    if ((engine_value(kCheatOn, {0xe}) & 0xff) != 0) {
        set_word_at(m_, self + 0x2dac, u(mul(int_at(m_, self + 0x2dac), 50) / 100));
    }
    if (global_word(m_, kSlowMotion) != 0) {
        set_word_at(m_, self + 0x2dac, u(mul(int_at(m_, self + 0x2dac), 50) / 200));
    }
    skater_at(m_, self).set_field_2f3c(0x3c3);
    const std::int32_t level = global_int(m_, kCurrentLevel);
    if (level >= 10 && level != 13) {
        skater_at(m_, self).set_field_2f3c(0x786);
    }
    const std::int32_t stat5 = s(thiscall_value(kProfileValue, self, {5}));
    skater_at(m_, self).set_field_2f3c(static_cast<Word>(u(mul(mul(stat5, 70) + 10000, static_cast<std::int32_t>(skater_at(m_, self).field_2f3c())) / 10000)));
    if (skater_at(m_, self).field_2da4() != 0) {
        const std::int32_t left = static_cast<std::int32_t>(skater_at(m_, self).field_2da4()) - 5 * static_cast<std::int32_t>(framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }));
        skater_at(m_, self).set_field_2da4(static_cast<Word>(left < 0 ? 0 : u(left)));
    }
    // the two spin-down words fall by twice the frame step and stop below 0x1000
    for (const Word field : {0x2f6cu, 0x2f70u}) {
        Word value = word_at(m_, self + field);
        if ((value & 0xfffff000u) != 0) {
            value -= 2 * global_word(m_, kFrameStep);
            set_word_at(m_, self + field, s(value) >= 0x1000 ? value : 0);
        }
    }
    // the idle stamp: kept while no button is down and the stick is near the centre
    const Word profile = skater_at(m_, self).action_record_bank().address;
    bool idle = false;
    if (byte_at(m_, profile + 0xa0) != 0 || static_cast<std::int8_t>(skater_at(m_, self).field_31a2()) < -40) {
        const std::int32_t stick = static_cast<std::int8_t>(skater_at(m_, self).field_31a1());
        idle = byte_at(m_, profile + 0x80) == 0 && byte_at(m_, profile + 0x90) == 0 && (stick < 0 ? -stick : stick) < 0x32;
    }
    if (!idle) {
        skater_at(m_, self).set_field_2d8c(0);
    } else if (skater_at(m_, self).field_2d8c() == 0) {
        skater_at(m_, self).set_field_2d8c(static_cast<Word>(global_word(m_, kFrameClock)));
    }
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, self + 0x2dbc + k, word_at(m_, self + 0x4c + k));
    }
    set_global_word(m_, kPhysicsActive, 1);
    set_global_word(m_, kQueryOwner, 0);
    thiscall(kSetPanelNumber, self, {0});
    thiscall(kHandleTimer, self);
    thiscall(kHandleFlips, self);
    thiscall(kRotationVectors, self);
    thiscall(kActionStep, self);
    if (skater_at(m_, self).physics_state() != 1) {
        skater_at(m_, self).set_field_29b8(0);
        skater_at(m_, self).set_field_3204(0);
    }
    set_word_at(m_, self + 0x60, 0);
    set_word_at(m_, self + 0x5c, 0);
    set_word_at(m_, self + 0x58, 0);
    thiscall(kAcceleration, self);
    thiscall(kPrePhysicsOllie, self);
    const Word state = skater_at(m_, self).physics_state();
    if (state != 5) {
        const Word animation = skater_at(m_, self).field_f6();
        if (animation == 0x23 || animation == 0x24) {
            if (state == 0) {
                thiscall(kRequestStart, self, {0, 0x2e11});
            } else {
                thiscall(kRequestStart, self, {0xe, 0x2e13});
            }
        }
    }
    thiscall(kTrickScriptUpdate, self);
    thiscall(kBrakeMode, self);
    if (skater_at(m_, self).field_107() != 0) {
        const Word animation = skater_at(m_, self).field_f6();
        if (animation == 0xb || animation == 0xc) {
            if (skater_at(m_, self).field_2dd8() != 0) {
                thiscall(kRequestFull, self, {8, 0x13, 0x1a, 0x13, 0x2e28});
            } else {
                thiscall(kRequestStart, self, {0, 0x2e2c});
            }
        }
    }
    // the history: the state, the orientation rows, the old position (from the position of the last frame) and the position of this one
    skater_at(m_, self).set_field_30c0(static_cast<Word>(skater_at(m_, self).physics_state()));
    for (Word k = 0; k < 0x20; k += 4) {
        set_word_at(m_, self + 0x2e38 + k, word_at(m_, self + 0x2e58 + k));
    }
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, self + 0x2e00 + k, word_at(m_, self + 0xbc + k));
    }
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, self + 0xbc + k, word_at(m_, self + 0x08 + k));
    }
    thiscall(kContinuousSounds, self);
    const auto same_position = [&]() {
        return word_at(m_, self + 0x08) == word_at(m_, self + 0xbc) && word_at(m_, self + 0x0c) == word_at(m_, self + 0xc0) && word_at(m_, self + 0x10) == word_at(m_, self + 0xc4);
    };
    set_global_word(m_, kAssertionLine, 0x2e3e);
    set_string(kAssertionFile, kPhysicsFile);
    check(same_position(), 0x00536f4c);
    // the push-off limit +0x2dc8: set from stat 3 while +0x2dd8 is on, else it falls by one a frame to its floor
    if (skater_at(m_, self).field_2dd8() != 0) {
        const std::int32_t value = s(thiscall_value(kProfileValue, self, {3}));
        skater_at(m_, self).set_field_2dc8(static_cast<Word>(u(mul(value + 0xdc, 0x2d000) / 280)));
    } else {
        const std::int32_t current = static_cast<std::int32_t>(skater_at(m_, self).field_2dc8());
        const std::int32_t value = s(thiscall_value(kProfileValue, self, {3}));
        if (current > mul(value + 0xaa, 0x2d000) / 280) {
            skater_at(m_, self).set_field_2dc8(static_cast<Word>(u(current - static_cast<std::int32_t>(framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); })))));
        }
    }
    // a push in progress (+0x2dcc frames): the response vector grows by the speed's direction, in whole units
    if (skater_at(m_, self).field_2dcc() != 0) {
        const std::int32_t length = mul(integer_sqrt(math_.dot_q12(self + 0x4c, self + 0x4c)), 0x40);
        if (length >= 0x1000) {
            const std::int32_t unit = length >> 12;
            set_word_at(m_, self + 0x58, u(int_at(m_, self + 0x58) + int_at(m_, self + 0x4c) / unit));
            set_word_at(m_, self + 0x5c, u(int_at(m_, self + 0x5c) + int_at(m_, self + 0x50) / unit));
            set_word_at(m_, self + 0x60, u(int_at(m_, self + 0x60) + int_at(m_, self + 0x54) / unit));
        }
        skater_at(m_, self).set_field_2dcc(static_cast<Word>(skater_at(m_, self).field_2dcc() - framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); })));
    }
    engine(kFloorRecovery, {self, word_at(m_, self + 0x08), word_at(m_, self + 0x0c), word_at(m_, self + 0x10), 0x00536f24});
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, self + 0x2e0c + k, word_at(m_, self + 0x08 + k));
    }
    set_global_word(m_, kAssertionLine, 0x2e67);
    set_string(kAssertionFile, kPhysicsFile);
    check(same_position(), 0x00536f00);
    if (skater_at(m_, self).field_2f64() == 0) {
        thiscall(kNoBoardCheck, self);
    }
    if (skater_at(m_, self).field_2f64() != 0 && skater_at(m_, self).field_f6() == 0x2e && skater_at(m_, self).field_f4() == half_at(m_, kKillFrames + 2 * skater_at(m_, self).skater_resource().address)) {
        thiscall(kRequestBloodSplat, self, {0, 0xfa});
        thiscall(kDustReset, self + 0x2c14, {skater_at(m_, self).field_30b0(), 1});
    }
    if (skater_at(m_, self).field_2c64() == 0) {
        thiscall(kDispatcher, self);
        thiscall(kBlockedReset, self);
    }
    if (skater_at(m_, self).physics_state() != 0) {
        skater_at(m_, self).set_field_2dd4(0);
    } else if (static_cast<Word>(skater_at(m_, self).field_2dd4()) == 0) {
        for (const Word field : {0x2f20u, 0x2f18u, 0x2f1cu, 0x2f14u, 0x2f24u}) {
            set_word_at(m_, self + field, 0);
        }
    }
    if (global_word(m_, kGameMode) == 9 && global_word(m_, kTagState) == 2) {
        engine(kTagCheckCaught, {self});
    } else if (global_byte(m_, kTwoSkaterHold) == 0 || global_int(m_, kFrameClock) > 0xb4) {
        thiscall(kCollideOther, self);
    }
    bool move = true;
    const Word standing = skater_at(m_, self).physics_state();
    if (standing == 0 || standing == 7) {
        if (global_word(m_, kEditorMode) != 0) {
            move = false;
        } else {
            const Word saved = global_word(m_, kQueryOwner);
            set_global_word(m_, kQueryOwner, 1);
            wall_bounce(self);
            set_global_word(m_, kQueryOwner, saved);
        }
    }
    if (move && global_word(m_, kEditorMode) == 0 && skater_at(m_, self).field_2e90() == 0 && skater_at(m_, self).physics_state() != 8) {
        const Word x = word_at(m_, self + 0x08);
        const Word y = word_at(m_, self + 0x0c);
        const Word z = word_at(m_, self + 0x10);
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, self + 0x08 + k, word_at(m_, self + 0xbc + k));
        }
        thiscall(kPositionWrite, self, {x, y, z});
    }
    if (skater_at(m_, self).physics_state() != 8) {
        thiscall(kLandingCleanup, self);
    }
    const std::int32_t rise = s(word_at(m_, self + 0x0c) - word_at(m_, self + 0xc0));
    if (skater_at(m_, self).physics_state() == 1 && rise > 0 && skater_at(m_, self).field_30c4() == 0) {
        skater_at(m_, self).set_field_2f38(static_cast<Word>(skater_at(m_, self).field_2f38() + u(rise)));
        skater_at(m_, self).set_field_2f40(static_cast<Word>(skater_at(m_, self).field_2f40() - u(rise)));
    } else {
        skater_at(m_, self).set_field_2f38(0);
    }
    thiscall(kCheckTriggers, self);
    const std::int32_t after = static_cast<std::int32_t>(skater_at(m_, self).physics_state());
    if (after > 0 && after <= 3) {
        if (skater_at(m_, self).field_2de4() == 0) {
            skater_at(m_, self).set_field_2de4(static_cast<Word>(global_word(m_, kFrameClock)));
        }
    } else {
        skater_at(m_, self).set_field_2de4(0);
        skater_at(m_, self).set_field_29f4(0x64);
        set_word_at(m_, self + 0x2cac, 0);
        skater_at(m_, self).set_field_2cb0(0);
        set_word_at(m_, self + 0x2cb4, 0);
    }
    if (int_at(m_, self + 0x3110) < -0xed8) {
        skater_at(m_, self).set_field_2f44(static_cast<Word>(global_word(m_, kSimulationTime)));
        skater_at(m_, self).set_field_2f48(static_cast<Word>(word_at(m_, self + 0x0c)));
    }
    if (skater_at(m_, self).physics_state() == 0) {
        skater_at(m_, self).set_field_2f4c(static_cast<Word>(word_at(m_, self + 0x0c)));
    }
    if (skater_at(m_, self).field_2c64() == 0) {
        // the response (+0x58) times the time scale, shifted down by 8, is added to the speed (+0x4c)
        const Word k = global_word(m_, kTimeScale);
        for (Word c = 0; c < 12; c += 4) {
            set_word_at(m_, self + 0x4c + c, word_at(m_, self + 0x4c + c) + u(s(word_at(m_, self + 0x58 + c) * k) >> 8));
        }
        thiscall(kDampResponse, self);
    } else {
        set_word_at(m_, self + 0x60, 0);
        set_word_at(m_, self + 0x5c, 0);
        set_word_at(m_, self + 0x58, 0);
    }
    thiscall(kNollieFlip, self);
    skater_at(m_, self).set_field_3200(0);
}

// the wall test of a standing skater (inside 0x0049e680, with the query owner set): a move along the wall vector +0x310c that meets the zone turns the skater's basis away from it; then, in the
// animations 6, 7, 9 and 10 with a lean (+0x3148), a line back from the wall at the animation's frame finds how far it is and sets the frame, and a close one pushes the skater out along the normal
void SkaterKit::wall_bounce(Word self) {
    const Word wall = self + 0x310c;
    const auto fill_query = [&](Word scale, std::int32_t back) {
        const Word x = word_at(m_, self + 0x08);
        const Word y = word_at(m_, self + 0x0c);
        const Word z = word_at(m_, self + 0x10);
        const Word sx = x + word_at(m_, wall) * scale;
        const Word sy = y + word_at(m_, wall + 4) * scale;
        const Word sz = z + word_at(m_, wall + 8) * scale;
        if (back == 0 && scale == 60) {
            set_global_word(m_, kQuery, x);
            set_global_word(m_, kQuery + 4, y);
            set_global_word(m_, kQuery + 8, z);
            set_global_word(m_, kQuery + 0xc, sx);
            set_global_word(m_, kQuery + 0x10, sy);
            set_global_word(m_, kQuery + 0x14, sz);
        } else {
            set_global_word(m_, kQuery, sx);
            set_global_word(m_, kQuery + 4, sy);
            set_global_word(m_, kQuery + 8, sz);
            set_global_word(m_, kQuery + 0xc, sx - word_at(m_, self + 0x3100) * u(back));
            set_global_word(m_, kQuery + 0x10, sy - word_at(m_, self + 0x3104) * u(back));
            set_global_word(m_, kQuery + 0x14, sz - word_at(m_, self + 0x3108) * u(back));
        }
    };
    const auto query = [&](Word service, std::initializer_list<Word> offsets) {
        engine(kInitQuery, {kQuery});
        engine(service, {kQuery, 1}, query_line(kQuery));
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(service);        // (the fields of the hit: the host's answers)
            for (const Word offset : offsets) {
                set_word_at(m_, kQuery + offset, static_cast<Word>(s_.value(name)));
            }
        }
        return global_word(m_, kQuery + kQueryBody) != 0;
    };
    if (word_at(m_, wall) != 0 || word_at(m_, wall + 8) != 0) {
        fill_query(60, 0);
        if (query(kZoneQuery, {kQueryBody, kQueryNormal, kQueryNormal + 4})) {
            set_word_at(m_, wall, 0);
            set_word_at(m_, wall + 4, 0xfffff000u);
            set_word_at(m_, wall + 8, 0);
            normalize_fixed12(self + 0x30f4);
            normalize_fixed12(wall);
            const auto cross = [&](Word a, Word b) {
                set_global_half(m_, kGteRotation0, half_at(m_, a));
                set_global_half(m_, kGteRotation1, half_at(m_, a + 4));
                set_global_half(m_, kGteRotation2, half_at(m_, a + 8));
                set_global_half(m_, kGteIr, half_at(m_, b));
                set_global_half(m_, kGteIr + 2, half_at(m_, b + 4));
                set_global_half(m_, kGteIr + 4, half_at(m_, b + 8));
                math_.cross_sf12();
            };
            cross(self + 0x30f4, wall);
            for (Word k = 0; k < 12; k += 4) {
                set_word_at(m_, self + 0x3100 + k, word_at(m_, kGteMac + k));
            }
            normalize_fixed12(self + 0x3100);
            cross(wall, self + 0x3100);
            for (Word k = 0; k < 12; k += 4) {
                set_word_at(m_, self + 0x30f4 + k, word_at(m_, kGteMac + k));
            }
            thiscall(kPublishBasis, self);
            thiscall(kWallResponse, self, {global_word(m_, kQuery + kQueryNormal), global_word(m_, kQuery + kQueryNormal + 4), 0x200});
            skater_at(m_, self).set_field_3130(0);
        }
    }
    const Word lean = static_cast<Word>(skater_at(m_, self).turn_accumulator_mirror());
    if (lean == 0 || global_word(m_, kEditorMode) != 0) {
        return;
    }
    const Word animation = skater_at(m_, self).field_f6();
    std::int32_t step = 0;
    const std::int32_t sign = s(lean) >= 0 ? 1 : -1;
    if (animation == 6 || animation == 7) {
        step = 22 * sign;
    } else if (animation == 9 || animation == 10) {
        step = 12 * sign;
    } else {
        return;
    }
    const std::int32_t back = mul(static_cast<std::int16_t>(skater_at(m_, self).field_f4()), 0x5a) / step;
    fill_query(100, back);
    if (!query(kBroadphaseQuery, {kQueryBody, kQueryDistance, kQueryNormal, kQueryNormal + 4})) {
        return;
    }
    const std::int32_t distance = global_int(m_, kQuery + kQueryDistance);
    const std::int32_t frame = mul(distance, step) / 90;
    skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(u(frame < 0 ? -frame : frame)));
    if (distance >= 0x3c) {
        return;
    }
    set_word_at(m_, self + 0x08, word_at(m_, self + 0x08) + u(2 * static_cast<std::int16_t>(global_half(m_, kQuery + kQueryNormal))));
    set_word_at(m_, self + 0x0c, word_at(m_, self + 0x0c) + u(2 * static_cast<std::int16_t>(global_half(m_, kQuery + kQueryNormal + 2))));
    set_word_at(m_, self + 0x10, word_at(m_, self + 0x10) + u(2 * static_cast<std::int16_t>(global_half(m_, kQuery + kQueryNormal + 4))));
}

}  // namespace opentony::runtime::skaterkit
