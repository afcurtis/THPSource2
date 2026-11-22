#include "frame_pacing.hpp"
#include "skater_views.hpp"

// Milestone 10g, session 18: the skater's air physics (physics.cpp, 0x00497f40 Skater_DoPhysicsInAir, thiscall on the skater, a service of the dispatcher), written from the machine code.
// With the air control on (0x0056b7f0) the air basis is updated, the pad profile's bytes push the acceleration along the up (+0x30, 180 percent of +0x2dac), forward (+0xa0 back, +0xb0 on) and
// right (+0x40 on, +0x60 back) vectors (150 percent, each >> 12), byte +0 with a hold count above 2 stops the vertical speed and acceleration, and a 32nd of the sideways speed is taken off. Two
// assertions (not on the ground, the old position is the position), the air basis again when +0x3204 asks for it, then the move (the velocity times the time scale and half the acceleration times
// the step, each >> 8), the platforms' bounce and, with a lean (+0x2e84), the orientation turned about x through a point 0x46000 above the skater (put back when it ends upside down, else the
// skater moves so that point stays). The speed is checked against stat 3 (only the call shows), then the spin: the turn accumulator (+0x3144 >> 12) times stat 4 and the chain (+0x2858, or the
// spin count +0x306c) turns the orientation about y and adds up in +0x3068, a half turn counting one more spin (+0x306c, the panel's text refreshed); a skater on a wall (+0x30c4) takes the
// surface step instead. The upright correction, then a line from the old position to the new is cast: nothing is met, gravity (+0x2da8) is added; a lip that is not a face is cast again with
// the query owner set; a landing surface (or a lip in state 3) puts the skater on the hit (30 units out) and lands it (or the air collision recovery takes the lip); a wall the skater meets
// side on, falling fast enough, starts a wall ride (state 5); a wall met otherwise puts the skater back and bails it; any other surface throws it back (state 1, a random wobble). Last, a skater
// riding a wall (state 3) that falls within 30 ticks of its start casts a line down its up vector and the air collision recovery takes a lip it meets.
// The vector helpers, the dot product, the root, the normalisation, the GTE (cross product, rtir, the angle matrix, the matrix-vector product, the translation), the basis publication, the normal
// removal and M3D_ReadSurfaceFlags run here; every other function is a service. The query record is a stack local in retail: here it has a scratch address of its own (the position write's
// native uses the shared record at +0xf50, and the air collision recovery is handed this one), and its fields are copied out as soon as the query returns.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }
inline std::int32_t plus(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) + static_cast<Word>(b)); }
inline std::int32_t sx(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }
inline std::int32_t abs32(std::int32_t v) {
    const Word sign = static_cast<Word>(v >> 31);
    return static_cast<std::int32_t>((static_cast<Word>(v) ^ sign) - sign);
}

constexpr Word kUpdateAirBasis = 0x00497df0;      // Skater_UpdateAirBasis (thiscall)
constexpr Word kBounce = 0x004a12d0;              // Platform_HandleBounce(skater) (cdecl)
constexpr Word kProfileValue = 0x0048f3a0;        // Skater_ProfileValue(selector) (thiscall)
constexpr Word kSurfaceResponse = 0x00496360;     // Skater_GroundSurfaceResponseStep (thiscall)
constexpr Word kChainValue = 0x0048cb60;          // TrickScore_ChainValue(index) (thiscall on the panel data at +0x168)
constexpr Word kCheatOn = 0x00416980;             // Career_GetCheat(number): a bool in AL
constexpr Word kRefreshTrickText = 0x0048ddc0;    // Panel_RefreshTrickText (thiscall on the panel data at +0x168)
constexpr Word kUprightCorrection = 0x0049c330;   // Skater_UprightCorrection (thiscall)
constexpr Word kInitQuery = 0x004624d0;           // M3D_InitCollisionQuery(record)
constexpr Word kZoneQuery = 0x00466090;           // M3D_QueryZoneCollision(record, 1)
constexpr Word kPositionWrite = 0x00496060;       // Skater_PositionWritePath(x, y, z) (thiscall)
constexpr Word kAirRecovery = 0x00497aa0;         // Skater_AirCollisionRecovery(record) (thiscall)
constexpr Word kLandingCleanup = 0x004914d0;      // Skater_LandingCleanup (thiscall)
constexpr Word kStateTransition = 0x004900b0;     // CBruce__StateTransition(state, source line) (thiscall)
constexpr Word kAirOrientation = 0x00491780;      // Skater_AirCollisionOrientationRecovery(normal xy, normal z) (thiscall)
constexpr Word kAirLanding = 0x00497960;          // Skater_AirLandingOrientationRecovery(normal xy, normal z) (thiscall)
constexpr Word kBailWhenFlipping = 0x004916d0;    // Skater_BailWhenFlipping(source line) (thiscall)
constexpr Word kMaybeManual = 0x00497bb0;         // CBruce__MaybeManual(0x28, 0) (thiscall)
constexpr Word kRebuildRecovery = 0x0049d080;     // Skater_RebuildOrientationRecovery (thiscall)
constexpr Word kSound = 0x004ad620;               // SFX_PlayX(sound, volume, pan) (cdecl)
constexpr Word kRandom = 0x004c9340;              // Rnd(n) -> 0..n-1 (cdecl)
constexpr Word kTurnPhase = 0x0049b500;           // Skater_ApplyTurnVelocityPhase(turn, 1, 0) (thiscall)
constexpr Word kCollisionResponse = 0x0049bad0;   // Skater_ApplyCollisionResponse(normal xy, normal z, bump) (thiscall)
constexpr Word kBail = 0x004904d0;                // CBruce__Bail(0, 0) (thiscall)
constexpr Word kApplyGap = 0x0048f690;            // Gap_ApplyDefinitionState(name, value, flag, index) (thiscall)
constexpr Word kFlipToVelocity = 0x0046d970;      // CBruce__FlipToVelocity (thiscall)
constexpr Word kRequestStart = 0x004903f0;        // Animation_RequestStart(animation, source line) (thiscall)
constexpr Word kTakeOff = 0x0048f5f0;             // Trick_TakeOff (thiscall)
constexpr Word kNothing = 0x004962f0;             // Skater_Nothing (thiscall, empty)

// globals
constexpr Word kAirControl = 0x0056b7f0;          // dword: the pad steers the skater in the air
constexpr Word kFrameClock = 0x005685f4;
constexpr Word kTimeScale = 0x0056865c;
constexpr Word kStepScale = 0x00568804;           // dword: the time step the acceleration is scaled by
constexpr Word kFlagLip = 0x0056b768;             // CollisionResultFlagSurface40
constexpr Word kFlagInverse23 = 0x0056b7a8;
constexpr Word kFlagFace80 = 0x0056b7ac;
constexpr Word kFlagInverse24 = 0x0056b7b8;
constexpr Word kWallAxis = 0x0056b7c0;            // three dwords: the axis the wall ride's along vector is crossed from (their low shorts)
constexpr Word kSurfaceClass = 0x0056b7e8;
constexpr Word kQueryOwner = 0x00567c7c;          // dword: set while the second query of a lip runs
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
constexpr Word kNotOnGroundText = 0x00536e3c;     // "P.O.I. in DoPhysicsInAir"
constexpr Word kOldPositionText = 0x00536e10;     // "mOldPos wrong at start of DoInAirPhysics"
constexpr Word kWallRideText = 0x00536de8;        // "Entering wallride, when already in it"
constexpr Word kWallRideName = 0x00536ddc;        // "WALL_RIDE"
const std::string kPhysicsFile = "H:\\TonyHawk\\Pc2\\physics.cpp";   // 0x00536a30

// the GTE words
constexpr Word kGteRotation = 0x006a3e10;         // nine shorts
constexpr Word kGteIr = 0x006a3eb0;               // three shorts
constexpr Word kGteMac = 0x006a3eb8;              // three dwords: the result

// the retail stack locals, at a scratch address no native the function calls uses
constexpr Word kLocals = pcplatform::kScratchBase + 0x4000;
constexpr Word kQuery = kLocals + 0x000;          // the query record (0xa0 bytes)
constexpr Word kVectorA = kLocals + 0x0a0;        // a pushing vector, the move, the velocity's direction across
constexpr Word kVectorB = kLocals + 0x0b0;        // the sideways speed, the wall's normal across
constexpr Word kAngles = kLocals + 0x0c0;         // three shorts
constexpr Word kTurnMatrix = kLocals + 0x0d0;     // a matrix record (0x20 bytes)
constexpr Word kZero = kLocals + 0x0f0;
constexpr Word kPivot = kLocals + 0x100;          // (0, 0x46000, 0)
constexpr Word kBefore = kLocals + 0x110;         // the pivot through the old orientation
constexpr Word kAfter = kLocals + 0x120;          // and through the new one
constexpr Word kSaved = kLocals + 0x130;          // the orientation record before the lean (0x20 bytes)
constexpr Word kAlong = kLocals + 0x150;          // the wall ride's along vector

// what a query found (the record's fields)
struct Hit {
    Word body;                                    // +0x68: the object the line met (0 for none)
    Word x, y, z;                                 // +0x6c: where
    Word normal_xy, normal_z;                     // +0x78, +0x7c: the surface normal, three shorts
    Word surface;                                 // +0x80: the surface record (its word +0xc has the flags)
};

}  // namespace

// 0x00497aa0 (thiscall, one argument: the query record of the lip the skater met, its normal at +0x78 / +0x7c): while the air state is set (+0x2db8) and the skater has been in the air for more than
// four frames (+0x2d8c, the frame it left) with no face flag (0x0056b7ac) the lip is handled below; otherwise the skater goes to state 2 and that is all. Below: state 1, and with the pad's
// byte +0xa0 set (or the speed byte +0x31a2 below -40) the speed is worn down along the normal: the speed times 64 / 4 (the root of the velocity's dot product) times the normal's x and z (Q12) is
// taken from the velocity's x and z. The counter +0x2db4 is cleared and, unless the take-off has been done (+0x2ddc) with a script queued (+0x2c68), the take-off runs, the animation 0xe is requested
// and the flag +0x2ddc set. The dot product, the root and the Q12 product run here; the rest are services.
void SkaterKit::air_recovery_for_hit(Word self, Word record) {
    const Word launch = word_at(m_, self + 0x2d8c);
    const bool handled = word_at(m_, self + 0x2db8) == 0 || (launch != 0 && s(global_word(m_, kFrameClock) - launch) > 4 && global_word(m_, kFlagFace80) == 0);
    if (!handled) {
        thiscall(kStateTransition, self, {2, 0x1caa});
        return;
    }
    thiscall(kStateTransition, self, {1, 0x1cb1});
    if (byte_at(m_, word_at(m_, self + 0x2ccc) + 0xa0) != 0 || static_cast<std::int8_t>(byte_at(m_, self + 0x31a2)) < -40) {
        const std::int32_t speed = s(u(integer_sqrt(math_.dot_q12(self + 0x4c, self + 0x4c))) << 6);
        const std::int32_t unit = speed / 4;
        set_word_at(m_, self + 0x4c, word_at(m_, self + 0x4c) - u(math_.mul_q12(sx(half_at(m_, record + 0x78)), unit)));
        set_word_at(m_, self + 0x54, word_at(m_, self + 0x54) - u(math_.mul_q12(sx(half_at(m_, record + 0x7c)), unit)));
        thiscall(kNothing, self);
    }
    const Word taken_off = word_at(m_, self + 0x2ddc);
    set_word_at(m_, self + 0x2db4, 0);
    if (taken_off == 0 || word_at(m_, self + 0x2c68) == 0) {
        thiscall(kTakeOff, self);
        thiscall(kRequestStart, self, {0xe, 0x1cc7});
        set_word_at(m_, self + 0x2ddc, 1);
    }
}

// 0x00497df0 (thiscall): the up vector (+0x310c) has its y (+0x3110) eased down by 500 (below -3596 it is reset to (0, -0x1000, 0)), is normalised, and the right vector (+0x3100) becomes the cross
// product of the forward vector (+0x30f4) with it, normalised, and the forward vector the cross product of the up vector with the right one; the integer basis is published as the orientation.
// The normalisation, the GTE cross product and the publication run here.
void SkaterKit::update_air_basis(Word self) {
    if (int_at(m_, self + 0x3110) < -0xe0c) {
        set_word_at(m_, self + 0x3110, 0xfffff000);
        set_word_at(m_, self + 0x310c, 0);
        set_word_at(m_, self + 0x3114, 0);
    } else {
        set_word_at(m_, self + 0x3110, word_at(m_, self + 0x3110) - u(framepacing::scaled_step(500, [&] { return global_int(m_, kTimeScale); })));
    }
    // the GTE operands: the three shorts of a vector in the rotation's first column (every 8 bytes) and in IR
    const auto load = [&](Word column, Word vector) {
        set_global_half(m_, kGteRotation, half_at(m_, column));
        set_global_half(m_, kGteRotation + 8, half_at(m_, column + 4));
        set_global_half(m_, kGteRotation + 0x10, half_at(m_, column + 8));
        set_global_half(m_, kGteIr, half_at(m_, vector));
        set_global_half(m_, kGteIr + 2, half_at(m_, vector + 4));
        set_global_half(m_, kGteIr + 4, half_at(m_, vector + 8));
    };
    const auto take_result = [&](Word to) {
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, to + k, global_word(m_, kGteMac + k));
        }
    };
    normalize_fixed12(self + 0x310c);
    load(self + 0x30f4, self + 0x310c);
    math_.cross_sf12();
    take_result(self + 0x3100);
    normalize_fixed12(self + 0x3100);
    load(self + 0x310c, self + 0x3100);
    math_.cross_sf12();
    take_result(self + 0x30f4);
    publish_orientation_basis(self);
}

// 0x00497f40 (thiscall)
void SkaterKit::do_air_physics(Word self) {
    const auto state = [&] { return skater_at(m_, self).physics_state(); };
    const Word matrix = self + 0x2e58;
    const auto put = [&](Word at, std::int32_t x, std::int32_t y, std::int32_t z) {
        set_word_at(m_, at, u(x));
        set_word_at(m_, at + 4, u(y));
        set_word_at(m_, at + 8, u(z));
    };
    const auto copy3 = [&](Word to, Word from) {
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, to + k, word_at(m_, from + k));
        }
    };
    const auto cheat = [&](Word number) { return (engine_value(kCheatOn, {number}) & 0xff) != 0; };
    // the query of the record's line as it stands (the line is written by the caller)
    const auto query = [&] {
        engine(kInitQuery, {kQuery});
        engine(kZoneQuery, {kQuery, 1}, query_line(kQuery));
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(kZoneQuery);      // (the fields of the hit: the host's answers)
            for (const Word offset : {0x68u, 0x6cu, 0x70u, 0x74u, 0x78u, 0x7cu, 0x40u, 0x80u}) {
                set_word_at(m_, kQuery + offset, static_cast<Word>(s_.value(name)));
            }
        }
        read_surface_flags(kQuery);
        return Hit{global_word(m_, kQuery + 0x68), global_word(m_, kQuery + 0x6c), global_word(m_, kQuery + 0x70), global_word(m_, kQuery + 0x74), global_word(m_, kQuery + 0x78), global_word(m_, kQuery + 0x7c), global_word(m_, kQuery + 0x80)};
    };
    const auto set_line = [&](const Word* start, const Word* end) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kQuery + 4 * k, start[k]);
            set_word_at(m_, kQuery + 0xc + 4 * k, end[k]);
        }
    };
    const auto position_write = [&](Word x, Word y, Word z) {
        thiscall(kPositionWrite, self, {x, y, z});
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(kPositionWrite);     // (the position the write left: the host's answers)
            for (Word k = 0; k < 12; k += 4) {
                set_word_at(m_, self + 8 + k, static_cast<Word>(s_.value(name)));
            }
        }
    };
    const auto restore_position = [&] { copy3(self + 8, self + 0xbc); };
    // the ground's normal as the hit's (shorts) and as integers, its time reset
    const auto take_normal = [&](const Hit& hit) {
        set_half_at(m_, self + 0x82, hit.normal_xy >> 16);
        set_half_at(m_, self + 0x80, hit.normal_xy);
        set_half_at(m_, self + 0x84, hit.normal_z);
        set_word_at(m_, self + 0x3138, u(sx(hit.normal_xy >> 16)));
        set_word_at(m_, self + 0x3134, u(sx(hit.normal_xy)));
        set_word_at(m_, self + 0x313c, u(sx(hit.normal_z)));
        skater_at(m_, self).set_field_3130(0);
    };
    // a basis vector times a percentage of +0x2dac, >> 12, added to (or taken from) the acceleration
    const auto push = [&](Word basis, std::int32_t percent, bool add) {
        const std::int32_t scale = mul(int_at(m_, self + 0x2dac), percent) / 100;
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t v = mul(int_at(m_, basis + 4 * k), scale) >> 12;
            const Word at = self + 0x58 + 4 * k;
            set_word_at(m_, at, add ? word_at(m_, at) + u(v) : word_at(m_, at) - u(v));
        }
    };

    // ---- the air control ----
    if (global_word(m_, kAirControl) != 0) {
        thiscall(kUpdateAirBasis, self);
        if (byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x30) != 0) {
            push(self + 0x310c, 180, true);
        }
        const Word pad = skater_at(m_, self).action_record_bank().address;
        if (byte_at(m_, pad) != 0 && int_at(m_, pad + 4) > 2) {
            set_word_at(m_, self + 0x5c, 0);
            set_word_at(m_, self + 0x50, 0);
        }
        if (byte_at(m_, skater_at(m_, self).action_record_bank().address + 0xa0) != 0) {
            push(self + 0x30f4, 150, false);
        }
        if (byte_at(m_, skater_at(m_, self).action_record_bank().address + 0xb0) != 0) {
            push(self + 0x30f4, 150, true);
        }
        if (byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x40) != 0) {
            push(self + 0x3100, 150, true);
        }
        if (byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x60) != 0) {
            push(self + 0x3100, 150, false);
        }
        // a 32nd of the speed along the right vector taken off
        const std::int32_t side = math_.dot_q12(self + 0x4c, self + 0x3100);
        std::int32_t across[3];
        for (Word k = 0; k < 3; ++k) {
            across[k] = math_.mul_q12(side, int_at(m_, self + 0x3100 + 4 * k));
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 0x4c + 4 * k, word_at(m_, self + 0x4c + 4 * k) - u(across[k] / 32));
        }
    }

    // ---- the assertions ----
    set_global_word(m_, kAssertionLine, 0x1da3);
    set_string(kAssertionFile, kPhysicsFile);
    check(state() != 2, kNotOnGroundText);
    set_string(kAssertionFile, kPhysicsFile);
    set_global_word(m_, kAssertionLine, 0x1daa);
    bool same = true;
    for (Word k = 0; k < 12; k += 4) {
        same = same && word_at(m_, self + 8 + k) == word_at(m_, self + 0xbc + k);
    }
    check(same, kOldPositionText);
    if (skater_at(m_, self).field_3204() != 0 && skater_at(m_, self).field_30c4() == 0) {
        thiscall(kUpdateAirBasis, self);
    }

    // ---- the move: the velocity times the time scale and the acceleration times the step, each >> 8, the acceleration's halved ----
    {
        Word step[3];
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t pushed = (mul(int_at(m_, self + 0x58 + 4 * k), global_int(m_, kStepScale)) >> 8) / 2;
            const std::int32_t moved = mul(int_at(m_, self + 0x4c + 4 * k), global_int(m_, kTimeScale)) >> 8;
            step[k] = u(moved) + u(pushed) + u(framepacing::retail_step_offset(int_at(m_, self + 0x2da8 + 4 * k), global_int(m_, kTimeScale)));
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 8 + 4 * k, word_at(m_, self + 8 + 4 * k) + step[k]);
        }
    }
    engine(kBounce, {self});

    // ---- the lean: the orientation turned about x through the pivot ----
    if (word_at(m_, self + 0x2e84) != 0) {
        for (Word k = 0; k < 0x20; k += 4) {
            set_word_at(m_, kSaved + k, word_at(m_, matrix + k));
        }
        put(kZero, 0, 0, 0);
        math_.matrix_set_translation(matrix, kZero);
        math_.matrix_set_translation(kTurnMatrix, kZero);
        put(kPivot, 0, 0x46000, 0);
        math_.convert_vector(matrix, kPivot, kBefore);
        set_global_half(m_, kAngles + 4, 0);
        set_global_half(m_, kAngles + 2, 0);
        set_global_half(m_, kAngles, static_cast<std::uint16_t>(half_at(m_, self + 0x2e84) & 0xfff));
        math_.rotation_from_angles(kAngles, kTurnMatrix);
        multiply_columns(matrix, kTurnMatrix, matrix);
        math_.convert_vector(matrix, kPivot, kAfter);
        if (sx(half_at(m_, self + 0x2e60)) < 1) {
            for (Word k = 0; k < 12; k += 4) {
                set_word_at(m_, self + 8 + k, word_at(m_, self + 8 + k) + (word_at(m_, kBefore + k) - word_at(m_, kAfter + k)));
            }
        } else {
            for (Word k = 0; k < 0x20; k += 4) {
                set_word_at(m_, matrix + k, word_at(m_, kSaved + k));
            }
        }
    }

    // ---- the speed against stat 3 (only the second call shows) ----
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, kVectorA + k, word_at(m_, self + 8 + k) - word_at(m_, self + 0xbc + k));
    }
    const std::int32_t speed = s(u(integer_sqrt(math_.dot_q12(kVectorA, kVectorA))) << 6);
    const std::int32_t stat = s(thiscall_value(kProfileValue, self, {3}));
    if (speed > s(u(mul(plus(stat, 0x186), 45)) << 12) / 280) {
        thiscall_value(kProfileValue, self, {3});
    }

    // ---- the spin ----
    if (skater_at(m_, self).field_30c4() == 0) {
        const std::int32_t turn = skater_at(m_, self).turn_accumulator() >> 12;
        const Word chain_index = static_cast<Word>(skater_at(m_, self).active_trick_stack_count());
        const std::int32_t chain = chain_index == 0 ? static_cast<std::int32_t>(skater_at(m_, self).field_306c()) : s(thiscall_value(kChainValue, self + 0x168, {chain_index - 1}));
        const std::int32_t stat4 = s(thiscall_value(kProfileValue, self, {4}));
        std::int32_t amount = mul(mul(plus(stat4, 300), turn), 25) / 10000;
        amount = mul(amount, s(100u - u(mul(chain, 9)))) / 100;
        if (amount != 0) {
            set_global_half(m_, kAngles + 4, 0);
            set_global_half(m_, kAngles, 0);
            const std::int32_t stepped = framepacing::scaled_carry(framepacing::CarrySite::air_spin, self, mul(amount, global_int(m_, kTimeScale)));   // retail: >> 8, at both uses (frame_pacing.hpp)
            const Word angle = cheat(10) ? u(amount) : u(stepped);
            set_global_half(m_, kAngles + 2, static_cast<std::uint16_t>(angle & 0xfff));
            math_.rotation_from_angles(kAngles, kTurnMatrix);
            multiply_columns(matrix, kTurnMatrix, matrix);
            if (!cheat(10)) {
                amount = stepped;
            }
            const std::int32_t total = plus(static_cast<std::int32_t>(skater_at(m_, self).field_3068()), amount);
            skater_at(m_, self).set_field_3068(static_cast<Word>(u(total)));
            if (total >= 0x800) {
                skater_at(m_, self).set_field_3068(static_cast<Word>(u(total) - 0x800));
                if (skater_at(m_, self).field_2c84() == 0) {
                    skater_at(m_, self).set_field_306c(static_cast<Word>(skater_at(m_, self).field_306c() + 1));
                    engine(kRefreshTrickText, {self + 0x168});
                }
            }
            const std::int32_t now = static_cast<std::int32_t>(skater_at(m_, self).field_3068());
            if (now <= -0x800) {
                skater_at(m_, self).set_field_3068(static_cast<Word>(u(now) + 0x800));
                if (skater_at(m_, self).field_2c84() == 0) {
                    skater_at(m_, self).set_field_306c(static_cast<Word>(skater_at(m_, self).field_306c() + 1));
                    engine(kRefreshTrickText, {self + 0x168});
                }
            }
        }
    } else {
        thiscall(kSurfaceResponse, self);
    }
    if (state() != 2 && skater_at(m_, self).field_30c4() == 0) {
        thiscall(kUprightCorrection, self);
    }

    // ---- the line of the move ----
    const auto tail = [&] {
        // a skater riding a wall that falls soon after it started: a line down its up vector; a lip there is the air collision recovery's
        if (int_at(m_, self + 0x50) < 0 && state() == 3 && s(global_word(m_, kFrameClock) - skater_at(m_, self).field_2f34()) < 0x1e && skater_at(m_, self).field_2f64() == 0) {
            Word start[3], end[3];
            for (Word k = 0; k < 3; ++k) {
                start[k] = word_at(m_, self + 8 + 4 * k);
                end[k] = start[k] - (word_at(m_, self + 0x310c + 4 * k) << 7);
            }
            set_line(start, end);
            const Hit down = query();
            if (down.body != 0 && global_word(m_, kFlagLip) != 0) {
                thiscall(kAirRecovery, self, {kQuery});
            }
        }
    };
    {
        Word start[3], end[3];
        for (Word k = 0; k < 3; ++k) {
            start[k] = word_at(m_, self + 0xbc + 4 * k);
            end[k] = word_at(m_, self + 8 + 4 * k);
        }
        set_line(start, end);
    }
    Hit hit = query();
    if (hit.body != 0 && global_word(m_, kFlagLip) != 0 && global_word(m_, kFlagFace80) == 0 && state() != 3) {
        // a lip: cast again (the record as the first query left it) with the owner set
        set_global_word(m_, kQueryOwner, 1);
        hit = query();
        set_global_word(m_, kQueryOwner, 0);
    }
    if (hit.body == 0) {
        // nothing met: gravity
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, self + 0x58 + k, word_at(m_, self + 0x58 + k) + word_at(m_, self + 0x2da8 + k));
        }
        tail();
        return;
    }
    const std::int32_t nx = sx(hit.normal_xy);
    const std::int32_t ny = sx(hit.normal_xy >> 16);
    const std::int32_t nz = sx(hit.normal_z);
    const Word pad = skater_at(m_, self).action_record_bank().address;
    const bool landing = (global_word(m_, kFlagInverse24) != 0 && (global_word(m_, kFlagLip) == 0 || global_word(m_, kFlagFace80) == 0)) || global_word(m_, kFlagInverse23) == 0 || skater_at(m_, self).field_2f64() != 0 ||
                         (byte_at(m_, pad) == 0 && int_at(m_, pad + 8) >= 0x14) || s(global_word(m_, kFrameClock) - skater_at(m_, self).field_2f50()) <= 0x28;
    if (!landing) {
        // a wall: met side on (the velocity across against the normal across), not too steep, falling fast enough, a wall ride starts; otherwise back and a bail
        const bool steep = ny > 0xccd;
        put(kVectorA, int_at(m_, self + 0x4c), 0, int_at(m_, self + 0x54));
        normalize_fixed12(kVectorA);
        put(kVectorB, nx, 0, nz);
        normalize_fixed12(kVectorB);
        const std::int32_t facing = math_.dot_q12(kVectorB, kVectorA);
        if (steep || abs32(facing) >= 0xddb || int_at(m_, self + 0x3110) >= -0x5dc) {
            restore_position();
            thiscall(kBail, self, {0, 0});
        } else {
            if (skater_at(m_, self).field_2c68() != 0 && skater_at(m_, self).field_2c6c() == 0) {
                thiscall(kBail, self, {0, 0});
                skater_at(m_, self).set_field_3034(static_cast<Word>(skater_at(m_, self).field_3034() + 1));
            } else {
                skater_at(m_, self).set_field_2c6c(1);
            }
            if (skater_at(m_, self).field_2f64() != 0) {
                restore_position();
            } else {
                set_global_word(m_, kAssertionLine, 0x1ec8);
                set_string(kAssertionFile, kPhysicsFile);
                check(state() != 5, kWallRideText);
                thiscall(kStateTransition, self, {5, 0x1eca});
                thiscall(kApplyGap, self, {kWallRideName, 500, 1, 0xffffffff});
                engine(kSound, {0xb3, 0x2000, 0});
                skater_at(m_, self).set_field_2f50(static_cast<Word>(global_word(m_, kFrameClock)));
                position_write(hit.x + u(mul(nx, 30)), hit.y + u(mul(ny, 30)), hit.z + u(mul(nz, 30)));
                thiscall(kFlipToVelocity, self);
                // the forward vector along the wall, the up vector the wall's normal, the right vector their cross product
                remove_normal_component(self + 0x30f4, hit.normal_xy, hit.normal_z);
                normalize_fixed12(self + 0x30f4);
                put(self + 0x310c, nx, ny, nz);
                set_global_half(m_, kGteRotation, half_at(m_, self + 0x30f4));
                set_global_half(m_, kGteRotation + 8, half_at(m_, self + 0x30f8));
                set_global_half(m_, kGteRotation + 0x10, half_at(m_, self + 0x30fc));
                set_global_half(m_, kGteIr, half_at(m_, self + 0x310c));
                set_global_half(m_, kGteIr + 2, half_at(m_, self + 0x3110));
                set_global_half(m_, kGteIr + 4, half_at(m_, self + 0x3114));
                math_.cross_sf12();
                copy3(self + 0x3100, kGteMac);
                publish_orientation_basis(self);
                thiscall(kFlipToVelocity, self);
                take_normal(hit);
                remove_normal_component(self + 0x4c, hit.normal_xy, hit.normal_z);
                // the along vector: the wall axis crossed with the up vector; the animation by its side of the velocity and the facing
                set_global_half(m_, kGteRotation, global_half(m_, kWallAxis));
                set_global_half(m_, kGteRotation + 8, global_half(m_, kWallAxis + 4));
                set_global_half(m_, kGteRotation + 0x10, global_half(m_, kWallAxis + 8));
                set_global_half(m_, kGteIr, half_at(m_, self + 0x310c));
                set_global_half(m_, kGteIr + 2, half_at(m_, self + 0x3110));
                put(kAlong, 0, 0, 0);
                set_global_half(m_, kGteIr + 4, half_at(m_, self + 0x3114));
                math_.cross_sf12();
                copy3(kAlong, kGteMac);
                publish_orientation_basis(self);
                skater_at(m_, self).set_field_29c8(0);
                const bool fakie = (byte_at(m_, self + 0xd8) & 2) != 0;
                const bool behind = math_.dot_q12(kAlong, self + 0x4c) < 0;
                if (fakie) {
                    thiscall(kRequestStart, self, behind ? std::vector<Word>{0x24, 0x1eff} : std::vector<Word>{0x23, 0x1efb});
                } else {
                    thiscall(kRequestStart, self, behind ? std::vector<Word>{0x23, 0x1f0a} : std::vector<Word>{0x24, 0x1f06});
                }
            }
        }
        tail();
        return;
    }
    if (global_word(m_, kFlagInverse24) != 0 || (global_word(m_, kFlagLip) != 0 && state() == 3)) {
        // a landing surface: onto the hit, 30 units out
        position_write(hit.x + u(mul(nx, 30)), hit.y + u(mul(ny, 30)), hit.z + u(mul(nz, 30)));
        if (skater_at(m_, self).field_2f64() == 0 && global_word(m_, kFlagLip) != 0) {
            thiscall(kAirRecovery, self, {kQuery});
        } else {
            skater_at(m_, self).set_turn_accumulator(0);
            skater_at(m_, self).set_field_29dc(0);
            if (state() != 3 || s(global_word(m_, kFrameClock) - skater_at(m_, self).field_2f34()) > 0x1e) {
                skater_at(m_, self).set_field_2ec0(1);
                thiscall(kLandingCleanup, self);
                if (skater_at(m_, self).field_2e90() != 0) {
                    return;
                }
                skater_at(m_, self).set_field_30b0(static_cast<Word>(global_word(m_, kSurfaceClass)));
                thiscall(kStateTransition, self, {0, 0x1fd6});
                if (skater_at(m_, self).field_2f64() == 0) {
                    thiscall(kAirOrientation, self, {hit.normal_xy, hit.normal_z});
                    thiscall(kAirLanding, self, {hit.normal_xy, hit.normal_z});
                    thiscall(kBailWhenFlipping, self, {0x1fdc});
                    thiscall(kMaybeManual, self, {0x28, 0});
                }
                skater_at(m_, self).set_field_2d98(static_cast<Word>(global_word(m_, kFrameClock)));
            }
        }
        skater_at(m_, self).set_field_2db4(static_cast<Word>((word_at(m_, hit.surface + 0xc) >> 16) & 0x20));
        if (state() != 1) {
            remove_normal_component(self + 0x4c, hit.normal_xy, hit.normal_z);
            take_normal(hit);
            thiscall(kRebuildRecovery, self);
        }
        tail();
        return;
    }
    // any other surface: the sound of its class, the ground's normal straight up, the skater a unit out and thrown back into the air with a quarter of the normal and, now and then, a wobble
    const Word surface_class = global_word(m_, kSurfaceClass);
    if (surface_class == 7) {
        engine(kSound, {0x35, 0x2000, 0});
    } else if (surface_class == 9) {
        engine(kSound, {0x28, 0x2000, 0});
    }
    if (static_cast<Word>(skater_at(m_, self).field_2ec0()) != 0) {
        skater_at(m_, self).set_field_2ec0(10);
        const Word side = engine_value(kRandom, {2});
        thiscall(kTurnPhase, self, {(side << 9) - 0x100, 1, 0});
    }
    set_half_at(m_, self + 0x80, 0);
    set_half_at(m_, self + 0x82, 0xf000);
    set_half_at(m_, self + 0x84, 0);
    set_word_at(m_, self + 0x3134, 0);
    set_word_at(m_, self + 0x3138, 0xfffff000);
    set_word_at(m_, self + 0x313c, 0);
    skater_at(m_, self).set_field_3130(0);
    thiscall(kRebuildRecovery, self);
    skater_at(m_, self).set_field_2cbc(0xf);
    position_write(hit.x + u(nx), hit.y + u(ny), hit.z + u(nz));
    skater_at(m_, self).set_field_2cbc(0xf);
    thiscall(kStateTransition, self, {1, 0x1f83});
    skater_at(m_, self).set_field_2ddc(1);
    thiscall(kCollisionResponse, self, {hit.normal_xy, hit.normal_z, 0});
    set_word_at(m_, self + 0x4c, word_at(m_, self + 0x4c) + u(nx / 4));
    set_word_at(m_, self + 0x50, word_at(m_, self + 0x50) + u(ny / 4));
    set_word_at(m_, self + 0x54, word_at(m_, self + 0x54) + u(nz / 4));
    if (engine_value(kRandom, {3}) != 0) {
        return;
    }
    set_word_at(m_, self + 0x4c, word_at(m_, self + 0x4c) + engine_value(kRandom, {0x4000}) - 0x2000);
    set_word_at(m_, self + 0x54, word_at(m_, self + 0x54) + engine_value(kRandom, {0x4000}) - 0x2000);
}

}  // namespace opentony::runtime::skaterkit
