#include "frame_pacing.hpp"
#include "skater_views.hpp"

// Milestone 10g: the skater's ground physics (physics.cpp, 0x00496550 Skater_DoGroundPhysics, thiscall on the skater, a service of the dispatcher), written from the machine code. The skater
// moves by its velocity and half its acceleration (each scaled down by 256), the platforms bounce it, a landing trick ends when the skater turned round to its velocity, and the surface step runs.
// A line from the old position to the new (lengthened to 30 units, 100 with +0x2f64) is cast through the zone: a hit on a plain surface puts the skater back and bounces it off; a hit on a wall
// (a normal more than 60 degrees from the ground's) puts it back, or under a ceiling sets it below the hit and turns the basis; a hit near the ground's normal within the move puts the skater on
// the hit. Then the speed flag (+0x2d94) against stat 3, the basis from the orientation shorts, and a line down the up vector (70 above to 186 below the skater) finds the floor: the skater stands
// on it, takes off from a lip (surface flag 0x40) or a slope too steep, leaves the ground or lands, the acceleration takes gravity (along the floor when it stands on one) and, on the ground, the
// sideways speed is taken off and the auto-kick, surface and +0x2dfc brakes slow it.
// The vector helpers, the dot product, the root, the normalisation, the GTE cross product, the arccosine, the basis copies, the normal removal and M3D_ReadSurfaceFlags (0x0048ea80, it reads the
// record and the surface only) run here; every other function is a service. The query record is a stack local in retail: here it is in the scratch area, and its fields are copied out as soon as
// the query returns (retail's record is private to the function, so the callees after the query cannot change it).

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }
inline std::int32_t sx(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }
inline std::int32_t abs32(std::int32_t v) {
    const Word sign = static_cast<Word>(v >> 31);
    return static_cast<std::int32_t>((static_cast<Word>(v) ^ sign) - sign);
}

constexpr Word kMaybeManual = 0x00497bb0;         // CBruce__MaybeManual(0xf, 1) (thiscall)
constexpr Word kTerrainEffects = 0x00495af0;      // CBruce__DoTerrainEffects (fastcall)
constexpr Word kCheatOn = 0x00416980;             // Career_GetCheat(number): a bool in AL
constexpr Word kBounce = 0x004a12d0;              // Platform_HandleBounce(skater) (cdecl)
constexpr Word kFlipToVelocity = 0x0046d970;      // CBruce__FlipToVelocity (thiscall): did the board turn round
constexpr Word kLandPending = 0x0048f7c0;         // Trick_LandPending (thiscall)
constexpr Word kTrickLand = 0x0048f8f0;           // Trick_Land (thiscall)
constexpr Word kRequestStart = 0x004903f0;        // Animation_RequestStart(animation, source line) (thiscall)
constexpr Word kSurfaceResponse = 0x00496360;     // Skater_GroundSurfaceResponseStep (thiscall)
constexpr Word kInitQuery = 0x004624d0;           // M3D_InitCollisionQuery(record)
constexpr Word kZoneQuery = 0x00466090;           // M3D_QueryZoneCollision(record, 1)
constexpr Word kClearTrickFlags = 0x00496300;     // Skater_ClearTrickFlags (fastcall)
constexpr Word kGrunt = 0x0046e1d0;               // SFX_Grunt(skater) (cdecl)
constexpr Word kCollisionResponse = 0x0049bad0;   // Skater_ApplyCollisionResponse(normal xy, normal z, bump) (thiscall)
constexpr Word kTurnPhase = 0x0049b500;           // Skater_ApplyTurnVelocityPhase(0x898, 1, 0) (thiscall)
constexpr Word kPositionWrite = 0x00496060;       // Skater_PositionWritePath(x, y, z) (thiscall)
constexpr Word kRebuildRecovery = 0x0049d080;     // Skater_RebuildOrientationRecovery (thiscall)
constexpr Word kStateTransition = 0x004900b0;     // CBruce__StateTransition(state, source line) (thiscall)
constexpr Word kProfileValue = 0x0048f3a0;        // Skater_ProfileValue(selector) (thiscall)
constexpr Word kExitRecovery = 0x004956f0;        // Skater_ExitGroundCollisionRecovery (thiscall)
constexpr Word kAirRecovery = 0x00491780;         // Skater_AirCollisionOrientationRecovery(normal xy, normal z) (thiscall)
constexpr Word kTakeOff = 0x0048f5f0;             // Trick_TakeOff (thiscall)
constexpr Word kRegisterObject = 0x0048f800;      // Trick_RegisterObject(body) (thiscall)

// globals
constexpr Word kFrameClock = 0x005685f4;
constexpr Word kTimeScale = 0x0056865c;
constexpr Word kManualLock = 0x00568658;          // dword: 1 keeps the manual check off
constexpr Word kGameMode = 0x00533f38;
constexpr Word kAutoKick = 0x0056a3d8;            // a dword per player
constexpr Word kPlayerSwap = 0x0056a854;          // (mode 7: the player index is taken xor this)
constexpr Word kSlowMotion = 0x0056b7ec;
constexpr Word kStepScale = 0x00568804;           // dword: the time step the acceleration is scaled by
constexpr Word kZeroVector = 0x0056af28;          // three dwords the move is compared with
constexpr Word kFlagLip = 0x0056b768;             // CollisionResultFlagSurface40
constexpr Word kFlagInverse23 = 0x0056b7a8;
constexpr Word kFlagFace80 = 0x0056b7ac;
constexpr Word kFlagInverse24 = 0x0056b7b8;
constexpr Word kSurfaceClass = 0x0056b7e8;
constexpr Word kQueryOwner = 0x00567c7c;          // dword: set while the floor query of a standing skater runs
constexpr Word kDebugFlag = 0x005614c4;           // byte
constexpr Word kFrameStep = 0x0056a93c;
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
constexpr Word kSamePositionText = 0x00536dc8;    // the assertion message of the position check
const std::string kPhysicsFile = "H:\\TonyHawk\\Pc2\\physics.cpp";   // 0x00536a30

// the GTE words the cross product goes through
constexpr Word kGteRotation0 = 0x006a3e10;
constexpr Word kGteRotation1 = 0x006a3e18;
constexpr Word kGteRotation2 = 0x006a3e20;
constexpr Word kGteIr = 0x006a3eb0;               // three shorts
constexpr Word kGteMac = 0x006a3eb8;              // three dwords: the result

// the query record (a stack local in retail) and two vectors the helpers read (stack locals too; 0x00490610 uses kLocalVector itself)
constexpr Word kQuery = pcplatform::kScratchBase + 0xf50;
constexpr Word kVectorA = kLocalVector + 0x10;
constexpr Word kVectorB = kLocalVector + 0x20;

// what a query found (the record's fields)
struct Hit {
    Word body;                                    // +0x68: the object the line met (0 for none)
    Word x, y, z;                                 // +0x6c: where
    Word normal_xy, normal_z;                     // +0x78, +0x7c: the surface normal, three shorts
    Word distance;                                // +0x40
    Word surface;                                 // +0x80: the surface record (its word +0xc has the flags)
};

}  // namespace

// 0x00496550 (thiscall)
void SkaterKit::do_ground_physics(Word self) {
    const auto sk = skater_at(m_, self);
    const auto state = [&] { return sk.physics_state(); };
    const auto put = [&](Word at, std::int32_t x, std::int32_t y, std::int32_t z) {
        set_word_at(m_, at, u(x));
        set_word_at(m_, at + 4, u(y));
        set_word_at(m_, at + 8, u(z));
    };
    const auto put_normal = [&](Word at, const Hit& hit) { put(at, sx(hit.normal_xy), sx(hit.normal_xy >> 16), sx(hit.normal_z)); };
    const auto put_ground = [&](Word at) { put(at, sx(static_cast<std::uint16_t>(component(sk.orientation_angles(), 0))), sx(static_cast<std::uint16_t>(component(sk.orientation_angles(), 1))), sx(static_cast<std::uint16_t>(component(sk.orientation_angles(), 2)))); };
    const auto restore_position = [&] {
        for (Word k = 0; k < 12; k += 4) {
            set_component(sk.position(), k / 4, component(sk.previous_position(), k / 4));
        }
    };
    const auto cheat = [&](Word number) { return (engine_value(kCheatOn, {number}) & 0xff) != 0; };
    const auto query = [&](const Word* start, const Word* end) {
        for (Word k = 0; k < 3; ++k) {
            set_line_start(line_at(m_, kQuery), k, start[k]);
            set_line_end(line_at(m_, kQuery), k, end[k]);
        }
        engine(kInitQuery, {kQuery});
        engine(kZoneQuery, {kQuery, 1}, query_line(kQuery));
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(kZoneQuery);      // (the fields of the hit: the host's answers)
            for (const Word offset : {0x68u, 0x6cu, 0x70u, 0x74u, 0x78u, 0x7cu, 0x40u, 0x80u}) {
                set_word_at(m_, kQuery + offset, static_cast<Word>(s_.value(name)));
            }
        }
        read_surface_flags(kQuery);
        return Hit{line_at(m_, kQuery).hit_body().address, static_cast<Word>(line_at(m_, kQuery).hit_position(0)), static_cast<Word>(line_at(m_, kQuery).hit_position(1)), static_cast<Word>(line_at(m_, kQuery).hit_position(2)), word_at(m_, kQuery + 0x78), word_at(m_, kQuery + 0x7c),
                   static_cast<Word>(line_at(m_, kQuery).hit_distance()), line_at(m_, kQuery).hit_face_record().address};
    };
    // the hit's normal against the ground's: the same keeps counting the time on it (+0x3130), another resets it and becomes the ground's
    const auto take_ground = [&](const Hit& hit) {
        if (static_cast<std::uint16_t>(hit.normal_xy) == static_cast<std::uint16_t>(component(sk.orientation_angles(), 0)) && static_cast<std::uint16_t>(hit.normal_xy >> 16) == static_cast<std::uint16_t>(component(sk.orientation_angles(), 1)) &&
            static_cast<std::uint16_t>(hit.normal_z) == static_cast<std::uint16_t>(component(sk.orientation_angles(), 2))) {
            sk.set_field_3130(sk.field_3130() + global_word(m_, kFrameStep));
        } else {
            sk.set_field_3130(0);
            set_component(sk.orientation_angles(), 0, static_cast<std::int32_t>(hit.normal_xy));
            set_component(sk.orientation_angles(), 1, static_cast<std::int32_t>(hit.normal_xy >> 16));
            set_component(sk.orientation_angles(), 2, static_cast<std::int32_t>(hit.normal_z));
        }
        thiscall(kRebuildRecovery, self);
    };
    const auto position_write = [&](Word x, Word y, Word z) {
        thiscall(kPositionWrite, self, {x, y, z});
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(kPositionWrite);     // (the position the write left: the host's answers)
            for (Word k = 0; k < 12; k += 4) {
                set_component(sk.position(), k / 4, static_cast<Word>(s_.value(name)));
            }
        }
    };
    const auto settle = [&] {
        if (sk.field_2c60() == 0) {
            sk.set_field_2c60(2);
        } else if (global_byte(m_, kDebugFlag) != 0) {
            thiscall(kTurnPhase, self, {0x898, 1, 0});
        }
    };

    sk.set_field_2c08(0);
    const bool skating = state() == 2;
    if (sk.field_2c60() != 0) {
        sk.set_field_2c60(sk.field_2c60() - framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }));
    }
    if (s(global_word(m_, kFrameClock) - sk.field_2d98()) > framepacing::clock_threshold(global_int(m_, kTimeScale), 20) && global_word(m_, kManualLock) != 1) {
        thiscall(kMaybeManual, self, {0xf, 1});
    }
    for (Word k = 0; k < 12; k += 4) {
        set_component(sk.vector_2e18(), k / 4, component(sk.position(), k / 4));
    }
    const Word player = static_cast<Word>(sk.player_index());
    const Word autokick = word_at(m_, kAutoKick + 4 * (global_word(m_, kGameMode) == 7 ? player ^ global_word(m_, kPlayerSwap) : player));
    // the gravity of the frame: 0x1964, with the cheats and the slow motion
    std::int32_t gravity = 0x1964;
    if (cheat(0xc)) {
        gravity = mul(gravity, 150) / 100;
    }
    if (cheat(0xe)) {
        gravity = mul(gravity, 50) / 100;
    }
    if (global_word(m_, kSlowMotion) != 0) {
        gravity = mul(gravity, 50) / 200;
    }
    if (state() == 0) {
        sk.set_field_2c8c(0);
        sk.set_field_2d9c(0);
    }
    thiscall(kTerrainEffects, self);
    set_global_word(m_, kAssertionLine, 0x18ff);
    set_string(kAssertionFile, kPhysicsFile);
    bool same = true;
    for (Word k = 0; k < 12; k += 4) {
        same = same && component(sk.position(), k / 4) == component(sk.previous_position(), k / 4);
    }
    check(same, kSamePositionText);
    // the move: the velocity times the time scale and the acceleration times the step, each down by 256, the acceleration's halved
    {
        Word step[3];
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t pushed = (mul(static_cast<std::int32_t>(component(sk.acceleration(), k)), global_int(m_, kStepScale)) >> 8) / 2;
            const std::int32_t moved = mul(static_cast<std::int32_t>(component(sk.velocity(), k)), global_int(m_, kTimeScale)) >> 8;
            step[k] = u(moved) + u(pushed);
        }
        for (Word k = 0; k < 3; ++k) {
            set_component(sk.position(), k, component(sk.position(), k) + step[k]);
        }
    }
    engine(kBounce, {self});
    if (!skating && sk.field_2f64() == 0 && thiscall_value(kFlipToVelocity, self) != 0 && state() == 0 &&
        s(global_word(m_, kFrameClock) - sk.field_2d98()) > framepacing::clock_threshold(global_int(m_, kTimeScale), 5) && sk.field_2dd4() != 0) {
        thiscall(kLandPending, self);
        sk.set_field_29c8(0);
        thiscall(kTrickLand, self);
        thiscall(kRequestStart, self, {5, 0x191c});
    }
    thiscall(kSurfaceResponse, self);

    // the line of the move
    Word start[3], end[3], move[3];
    for (Word k = 0; k < 3; ++k) {
        start[k] = component(sk.previous_position(), k);
        end[k] = component(sk.position(), k);
        move[k] = end[k] - start[k];
    }
    if (move[0] != global_word(m_, kZeroVector) || move[1] != word_at(m_, kZeroVector + 4) || move[2] != word_at(m_, kZeroVector + 8)) {
        put(kVectorA, s(move[0]), s(move[1]), s(move[2]));
        std::int32_t reach = s(u(integer_sqrt(math_.dot_q12(kVectorA, kVectorA))) << 6);
        const std::int32_t least = sk.field_2f64() != 0 ? 0x64 : 0x1e;
        if (reach < (least << 12)) {
            if (reach < 0x800) {
                reach = 0x800;
            }
            for (Word k = 0; k < 3; ++k) {
                end[k] = (u(mul(least, s(move[k])) / reach) << 12) + component(sk.previous_position(), k);
            }
        }
        const Hit hit = query(start, end);
        if (hit.body != 0) {
            const bool plain = global_word(m_, kFlagInverse24) == 0 && global_word(m_, kFlagInverse23) == 0 && global_word(m_, kFlagLip) == 0;
            if (plain) {
                // a plain surface: back to where the skater was, a bump off it (with a bail when it met it head on)
                restore_position();
                Word bump = 0;
                if (sk.field_f6() != 0xc && sk.field_2f64() == 0 && sk.field_2c74() == 0) {
                    put_normal(kVectorA, hit);
                    if (math_.dot_q12(kVectorA, self + 0x30f4) > 0xb50) {
                        thiscall(kLandPending, self);
                        thiscall(kClearTrickFlags, self);
                        thiscall(kRequestStart, self, {0xc, 0x1967});
                        bump = 0xc8;
                        engine(kGrunt, {self});
                    }
                }
                sk.set_field_3074(1);
                thiscall(kCollisionResponse, self, {hit.normal_xy, hit.normal_z, bump});
                settle();
            } else if (global_word(m_, kFlagLip) == 0 || global_word(m_, kFlagFace80) != 0) {
                put_normal(kVectorA, hit);
                put_ground(kVectorB);
                const std::int32_t facing = math_.dot_q12(kVectorA, kVectorB);
                const std::int32_t nx = sx(hit.normal_xy);
                const std::int32_t ny = sx(hit.normal_xy >> 16);
                const std::int32_t nz = sx(hit.normal_z);
                if (facing < 0x800 && facing > -0x800) {
                    if (-ny > abs32(nx) && -ny > abs32(nz)) {
                        // a ceiling: the skater below the hit, the ground's normal is the ceiling's (and, skating, the basis turns to it)
                        position_write(hit.x + u(mul(nx, 30)), hit.y + u(mul(ny, 30)), hit.z + u(mul(nz, 30)));
                        sk.set_field_3130(0);
                        set_component(sk.orientation_angles(), 0, static_cast<std::int32_t>(hit.normal_xy));
                        set_component(sk.orientation_angles(), 1, static_cast<std::int32_t>(hit.normal_xy >> 16));
                        set_component(sk.orientation_angles(), 2, static_cast<std::int32_t>(hit.normal_z));
                        thiscall(kRebuildRecovery, self);
                        if (state() == 2) {
                            set_component(sk.orientation_angles(), 0, static_cast<std::int32_t>(hit.normal_xy));
                            set_component(sk.orientation_angles(), 1, static_cast<std::int32_t>(hit.normal_xy >> 16));
                            set_component(sk.orientation_angles(), 2, static_cast<std::int32_t>(hit.normal_z));
                            put_normal(self + 0x3134, hit);
                            sk.set_field_3130(0x19000);
                            remove_normal_component(self + 0x30f4, hit.normal_xy, hit.normal_z);
                            normalize_fixed12(self + 0x30f4);
                            put_normal(self + 0x310c, hit);
                            set_global_half(m_, kGteRotation0, static_cast<std::uint16_t>(component(sk.vector_30f4(), 0)));
                            set_global_half(m_, kGteRotation1, static_cast<std::uint16_t>(component(sk.vector_30f4(), 1)));
                            set_global_half(m_, kGteRotation2, static_cast<std::uint16_t>(component(sk.vector_30f4(), 2)));
                            set_global_half(m_, kGteIr, static_cast<std::uint16_t>(component(sk.vector_310c(), 0)));
                            set_half_at(m_, kGteIr + 2, static_cast<std::uint16_t>(component(sk.vector_310c(), 1)));
                            set_half_at(m_, kGteIr + 4, static_cast<std::uint16_t>(component(sk.vector_310c(), 2)));
                            math_.cross_sf12();
                            for (Word k = 0; k < 12; k += 4) {
                                set_component(sk.vector_3100(), k / 4, word_at(m_, kGteMac + k));
                            }
                            publish_orientation_basis(self);
                        }
                        thiscall(kStateTransition, self, {0, 0x19bf});
                    } else {
                        // a wall: back to where the skater was and bounced off (a bail when it went in head on and fast enough)
                        restore_position();
                        Word bump = 0;
                        for (Word k = 0; k < 12; k += 4) {
                            set_word_at(m_, kVectorA + k, component(sk.velocity(), k / 4));
                        }
                        if (sk.field_f6() != 0xc && vector_length(kVectorA) > 6 && sk.field_2f64() == 0 && sk.field_2c74() == 0) {
                            put_normal(kVectorB, hit);
                            if (math_.dot_q12(kVectorB, self + 0x30f4) > 0xb50) {
                                thiscall(kLandPending, self);
                                thiscall(kClearTrickFlags, self);
                                thiscall(kRequestStart, self, {0xc, 0x19da});
                                engine(kGrunt, {self});
                                bump = 0xc8;
                            }
                        }
                        thiscall(kCollisionResponse, self, {hit.normal_xy, hit.normal_z, bump});
                        settle();
                    }
                } else if ((reach >> 12) >= s(hit.distance)) {
                    // near the ground's slope and within the move: onto the hit
                    position_write(hit.x + u(mul(nx, 30)), hit.y + u(mul(ny, 30)), hit.z + u(mul(nz, 30)));
                    take_ground(hit);
                }
            }
        }
    }

    // the speed flag against stat 3, then the basis from the orientation shorts
    const std::int32_t speed = s(u(integer_sqrt(math_.dot_q12(self + 0x4c, self + 0x4c))) << 6);
    const std::int32_t stat = s(thiscall_value(kProfileValue, self, {3}));
    sk.set_field_2d94(speed > s(u(mul(s(u(stat) + 0x186), 45)) << 12) / 280 ? 1 : 0);
    fru_from_rotation_matrix(self);

    // the floor: a line from 70 units up the up vector to 256 below that
    for (Word k = 0; k < 3; ++k) {
        const Word up = component(sk.vector_310c(), k);
        start[k] = component(sk.position(), k) + up * 70;
        end[k] = start[k] - (up << 8);
    }
    if (state() == 0 && (static_cast<std::int32_t>(component(sk.velocity(), 1)) > 0 || static_cast<std::int32_t>(component(sk.vector_30f4(), 1)) <= 0)) {
        set_global_word(m_, kQueryOwner, 1);
    }
    const Hit floor = query(start, end);
    set_global_word(m_, kQueryOwner, 0);
    bool on_floor = false;
    if (floor.body != 0) {
        sk.set_field_30b0(global_word(m_, kSurfaceClass));
        if (global_word(m_, kFlagInverse24) != 0 || global_word(m_, kFlagInverse23) != 0 || global_word(m_, kFlagLip) != 0) {
            const std::int32_t distance = s(floor.distance);
            std::int32_t reach;
            if (state() == 2) {
                sk.set_field_2da4(0x14);
                reach = 0xa0;
            } else {
                reach = s(sk.field_2da4() + 0x74);
            }
            if (distance > reach && byte_at(m_, sk.action_record_bank().address + 0xa0) != 0) {
                // (a pad flag doubles the reach on a floor that slopes from the ground by less than about 22 degrees)
                put_normal(kVectorA, floor);
                put_ground(kVectorB);
                const std::int32_t facing = math_.dot_q12(kVectorB, kVectorA);
                if (facing > 0 && facing < 0xed8) {
                    reach = mul(reach, 2);
                }
            }
            if (distance > reach) {
                sk.set_field_2db4((word_at(m_, floor.surface + 0xc) >> 16) & 0x20);
            } else {
                on_floor = true;
            }
        }
    }
    if (on_floor) {
        // onto the floor (unless that moves the skater by less than a unit)
        sk.set_field_2db4((word_at(m_, floor.surface + 0xc) >> 16) & 0x20);
        Word before[3];
        for (Word k = 0; k < 3; ++k) {
            before[k] = component(sk.position(), k);
        }
        position_write(floor.x + component(sk.vector_310c(), 0) * 30, floor.y + component(sk.vector_310c(), 1) * 30, floor.z + component(sk.vector_310c(), 2) * 30);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kVectorA + 4 * k, before[k] - component(sk.position(), k));
        }
        if (math_.dot_q12(kVectorA, kVectorA) < 0x1000) {
            for (Word k = 0; k < 3; ++k) {
                set_component(sk.position(), k, before[k]);
            }
        }
        if (global_word(m_, kFlagLip) != 0) {
            if (state() == 2) {
                // skating on a lip: the angle between the velocity and the floor's normal adds up (+0x2d9c, restarted when its sign changes); past -0x400 the skater leaves the ground
                put_normal(kVectorA, floor);
                for (Word k = 0; k < 12; k += 4) {
                    set_word_at(m_, kVectorB + k, component(sk.velocity(), k / 4));
                }
                normalize_fixed12(kVectorA);
                normalize_fixed12(kVectorB);
                const std::int32_t angle = s(math_.acos_ratio_to_angle(math_.dot_q12(kVectorB, kVectorA))) - 0x3ff;
                if (abs32(angle) > 2 && (angle >= 0) != (static_cast<std::int32_t>(sk.field_2d9c()) >= 0)) {
                    sk.set_field_2d9c(u(angle));
                } else {
                    sk.set_field_2d9c(sk.field_2d9c() + u(angle));
                }
                if (static_cast<std::int32_t>(sk.field_2d9c()) < -0x400) {
                    thiscall(kStateTransition, self, {1, 0x1ae4});
                }
            } else {
                const Word since = sk.field_2d8c();
                if (since != 0 && s(global_word(m_, kFrameClock) - since) > 4 && global_word(m_, kFlagFace80) == 0 && sk.field_2f64() == 0) {
                    // off the lip into the air: a quarter of the speed taken off along the floor's normal across, half the upward speed
                    const std::int32_t quarter = speed / 4;
                    set_component(sk.velocity(), 0, component(sk.velocity(), 0) - u(math_.mul_q12(sx(floor.normal_xy), quarter)));
                    set_component(sk.velocity(), 2, component(sk.velocity(), 2) - u(math_.mul_q12(sx(floor.normal_z), quarter)));
                    set_component(sk.velocity(), 1, u(static_cast<std::int32_t>(component(sk.velocity(), 1)) / 2));
                    if (state() == 0) {
                        sk.set_field_306c(0);
                        sk.set_field_3068(0);
                    }
                    thiscall(kStateTransition, self, {1, 0x1ab6});
                    thiscall(kTakeOff, self);
                    sk.set_field_2db4(0);
                    thiscall(kRequestStart, self, {0xe, 0x1ab9});
                    sk.set_field_2ddc(1);
                    nothing();
                } else {
                    if (sk.field_2ddc() == 0 && sk.field_2f64() == 0) {
                        thiscall(kRequestStart, self, {0xe, 0x1ac2});
                        sk.set_field_2ddc(1);
                    }
                    thiscall(kTakeOff, self);
                    sk.set_field_3068(0);
                    sk.set_field_306c(0);
                    thiscall(kStateTransition, self, {2, 0x1ac9});
                }
            }
        } else if (global_word(m_, kFlagInverse24) == 0) {
            thiscall(kExitRecovery, self);
        } else {
            const Word now = state();
            if (now == 0) {
                // standing on a floor that faces away from the ground (and from the board), not rising: the recovery
                put_normal(kVectorA, floor);
                put_ground(kVectorB);
                const std::int32_t agreement = math_.dot_q12(kVectorB, kVectorA);
                if (math_.dot_q12(self + 0x30f4, kVectorA) < 0 && static_cast<std::int32_t>(component(sk.velocity(), 1)) <= 0 && agreement > 0 && agreement < 0xbb8) {
                    thiscall(kExitRecovery, self);
                }
            } else if (now == 7) {
                set_component(sk.velocity(), 0, 0);
                set_component(sk.velocity(), 1, 0);
                set_component(sk.velocity(), 2, 0);
            } else {
                // landing: the orientation from the floor, standing, the landing time and an empty pad history
                thiscall(kAirRecovery, self, {floor.normal_xy, floor.normal_z});
                thiscall(kStateTransition, self, {0, 0x1b19});
                sk.set_field_29e0(0);
                sk.set_field_2d98(global_word(m_, kFrameClock));
                for (Word k = 0; k < 0x20; ++k) {
                    set_byte_at(m_, self + 0x2a14 + 8 * k, 0);
                }
            }
            if (!skating) {
                if (sk.field_2dd4() == 0) {
                    sk.set_field_2d88(0);
                }
                thiscall(kRegisterObject, self, {floor.body});
            }
        }
        if (state() != 1) {
            take_ground(floor);
        }
    } else {
        thiscall(kExitRecovery, self);
    }

    // gravity: along the floor (and the velocity kept on its plane) when there is one, straight down otherwise
    if (state() != 1 && floor.body != 0) {
        if (state() == 2) {
            for (Word k = 0; k < 12; k += 4) {
                set_word_at(m_, kVectorA + k, component(sk.vector_2da8(), k / 4));
            }
        } else {
            put(kVectorA, 0, 0x1964, 0);
        }
        remove_normal_component(kVectorA, floor.normal_xy, floor.normal_z);
        for (Word k = 0; k < 12; k += 4) {
            set_component(sk.acceleration(), k / 4, component(sk.acceleration(), k / 4) + word_at(m_, kVectorA + k));
        }
        if (state() != 2) {
            for (Word k = 0; k < 12; k += 4) {
                set_component(sk.vector_3118(), k / 4, word_at(m_, kVectorA + k));
            }
        }
        sk.set_field_3128(floor.normal_xy);
        sk.set_field_312c(floor.normal_z);
        set_global_word(m_, kVectorB, floor.normal_xy);
        set_word_at(m_, kVectorB + 4, floor.normal_z);
        damp_to_ground_plane(self + 0x4c, kVectorB, self + 0x4c);
    } else {
        set_component(sk.acceleration(), 1, component(sk.acceleration(), 1) + u(gravity));
    }

    // standing on the ground: the sideways speed is taken off, then the brakes
    if (state() != 0 || sk.field_2f64() != 0) {
        return;
    }
    const std::int32_t side = math_.dot_q12(self + 0x4c, self + 0x3100);
    std::int32_t across[3];
    for (Word k = 0; k < 3; ++k) {
        across[k] = math_.mul_q12(side, static_cast<std::int32_t>(component(sk.vector_3100(), k)));
    }
    for (Word k = 0; k < 3; ++k) {
        set_component(sk.velocity(), k, component(sk.velocity(), k) - u(across[k]));
    }
    const std::int32_t along = math_.dot_q12(self + 0x4c, self + 0x30f4);
    std::int32_t forward[3];
    for (Word k = 0; k < 3; ++k) {
        forward[k] = math_.mul_q12(along, static_cast<std::int32_t>(component(sk.vector_30f4(), k)));
    }
    const auto brake = [&](std::int32_t amount) {
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t slowed = framepacing::acceleration_term(math_.mul_q12(amount, forward[k]), global_int(m_, kTimeScale));
            set_component(sk.acceleration(), k, component(sk.acceleration(), k) - u(slowed));
        }
    };
    if (sk.field_2d94() != 0) {
        brake(0);
    } else if (autokick != 0) {
        brake(8);
    }
    if (sk.field_30b0() == 5) {
        brake(0x78);
    }
    if (sk.field_30b0() == 6) {
        brake(0x78);
    }
    if (sk.field_30b0() == 3) {
        brake(0xb4);
    }
    if (sk.field_2dd4() != 0) {
        brake(sk.field_2dfc());
    }
}

// Milestone 10g, session 9: the platforms' bounce and the ground's surface response, written from the machine code.

// 0x004a12d0 Platform_HandleBounce (cdecl, the skater): every object on the game list (0x0056af40, linked at +0x20) that is a bouncy prop (word +4 bit 8, byte +0x178 bit 4) gets word +4 bit 5 set
// and, unless byte +0x178 has bit 5, is thrown (CPlatform__Bounce, a service) when its model's box (the shorts at +0xc..+0x16 of the model, in units of 0x1000, from the model table 0x0056d43c: the
// region byte +0x1f times 17 words, the word +0x1a) touches the box the skater swept this frame (the old position +0xbc and the new +8, 20 units larger on every side).
void SkaterKit::platform_bounce(Word skater) {
    const auto sk = skater_at(m_, skater);
    constexpr Word kGameListHead = 0x0056af40;
    constexpr Word kModelTable = 0x0056d43c;
    constexpr Word kBounceOwner = 0x0056a960;
    constexpr Word kPlatformBounce = 0x0049f4c0;       // CPlatform__Bounce(owner, skater + 0x4c) (thiscall on the platform)
    constexpr Word kMargin = 0x14000;
    Word low[3], high[3];                              // the swept box: x, y, z
    for (Word k = 0; k < 3; ++k) {
        std::int32_t a = static_cast<std::int32_t>(component(sk.previous_position(), k));
        std::int32_t b = static_cast<std::int32_t>(component(sk.position(), k));
        if (a > b) {
            std::swap(a, b);
        }
        low[k] = u(a) - kMargin;
        high[k] = u(b) + kMargin;
    }
    for (Word node = global_word(m_, kGameListHead); node != 0; node = next_object(m_, node)) {
        const Word flags = object_at(m_, node).visibility_flags();
        if ((flags & 0x100) == 0 || (byte_at(m_, node + 0x178) & 0x10) == 0) {
            continue;
        }
        object_at(m_, node).set_visibility_flags(static_cast<std::uint16_t>(flags | 0x20));
        if ((byte_at(m_, node + 0x178) & 0x20) != 0) {
            continue;
        }
        const Word table = word_at(m_, kModelTable + 4 * (object_at(m_, node).region() * 17));
        const Word model = word_at(m_, table + 4 * object_at(m_, node).model_index());
        const auto edge = [&](Word offset, Word axis) { return (static_cast<Word>(sx(half_at(m_, model + offset))) << 12) + component(object_at(m_, node).position(), axis); };
        if (s(high[0]) < s(edge(0xe, 0)) || s(low[0]) > s(edge(0xc, 0))) {
            continue;
        }
        if (s(high[2]) < s(edge(0x16, 2)) || s(low[2]) > s(edge(0x14, 2))) {
            continue;
        }
        if (s(high[1]) < s(edge(0x12, 1)) || s(low[1]) > s(edge(0x10, 1))) {
            continue;
        }
        thiscall(kPlatformBounce, node, {global_word(m_, kBounceOwner), skater + 0x4c});
    }
}

// 0x00496360 Skater_GroundSurfaceResponseStep (thiscall, no arguments). The slope response: the amount (+0x3144 in units; for a bouncing state - 2, or 1 with +0x30c4 - scaled by the profile stat 4
// and the level, 9 per cent of it taken off for each level) is lengthened by the extra the first branch finds (+0x2d90 counts it down, +0x3124 is what 0x0049c060 adds), handed to the turn phase
// (0x0049b500), and added to the phase accumulator (+0x3068) unless +0x2dd4; every 0x800 the accumulator wraps and, unless +0x2c84, the count (+0x306c) goes up and 0x0048ddc0 runs.
void SkaterKit::ground_surface_response(Word self) {
    const auto sk = skater_at(m_, self);
    constexpr Word kTimeScale = 0x0056865c;
    constexpr Word kProfileValue = 0x0048f3a0;         // Skater_ProfileValue(selector) (thiscall)
    constexpr Word kTurnPhase = 0x0049b500;            // Skater_ApplyTurnVelocityPhase(amount, flag, 0) (thiscall)
    constexpr Word kLevelValue = 0x0048cb60;           // (thiscall on skater + 0x168, one argument): a level's value
    constexpr Word kBounceAmount = 0x0049c060;         // (thiscall, one argument): sets +0x3124
    constexpr Word kLevelUp = 0x0048ddc0;              // (thiscall on skater + 0x168)
    const Word state = sk.physics_state();
    const bool bouncing = state == 2 || (state == 1 && sk.field_30c4() != 0);
    if (sk.field_2c88() != 0 || sk.field_2e80() != 0 || sk.field_2c80() != 0) {
        sk.set_field_2d90(0);
    }
    std::int32_t amount = sk.turn_accumulator() >> 12;
    if (bouncing) {
        const Word counter = static_cast<Word>(sk.active_trick_stack_count());
        const Word level = counter != 0 ? thiscall_value(kLevelValue, self + 0x168, {counter - 1}) : sk.field_306c();
        const Word stat = thiscall_value(kProfileValue, self, {4}) + 0x12c;
        const std::int32_t product = mul(mul(mul(s(stat), amount), 5), 5);
        std::int32_t scaled = static_cast<std::int32_t>((static_cast<std::int64_t>(0x68db8bad) * product) >> 32) >> 12;     // (a division by 10000 as the compiler built it)
        scaled += static_cast<std::int32_t>(static_cast<Word>(scaled) >> 31);
        const std::int32_t reduced = mul(scaled, s(100u - level * 9));
        std::int32_t result = static_cast<std::int32_t>((static_cast<std::int64_t>(0x51eb851f) * reduced) >> 32) >> 5;       // (a division by 100)
        result += static_cast<std::int32_t>(static_cast<Word>(result) >> 31);
        amount = result;
    }
    const Word extra = sk.field_2d90();
    sk.set_field_3124(0);
    if (extra != 0) {
        if (amount == 0) {
            thiscall(kBounceAmount, self, {0x28});
            if (!s_.live()) {
                sk.set_field_3124(static_cast<Word>(s_.value(pcplatform::engine_name(kBounceAmount))));      // (what the service wrote: the host's answer)
            }
            amount = s(u(amount) + sk.field_3124());
        } else {
            const Word record = sk.action_record_bank().address;
            if (byte_at(m_, record + 0x10) == 0 && byte_at(m_, record + 0x20) == 0) {
                const Word left = extra - 3;      // (unsigned: the subtraction wraps as retail's does)
                sk.set_field_2d90(s(left) < 0 ? 0 : left);
            } else {
                sk.set_field_2d90(extra - 1);
            }
            amount = s(u(amount) + sk.field_3124());
        }
    } else {
        sk.set_field_2d90(0);
    }
    const std::int32_t turned = framepacing::scaled_carry(framepacing::CarrySite::ground_turn, self, mul(global_int(m_, kTimeScale), amount));   // retail: >> 8, at both uses (frame_pacing.hpp)
    thiscall(kTurnPhase, self, {u(turned), bouncing ? 0u : 1u, 0});
    if (sk.field_2dd4() == 0) {
        sk.set_field_3068(sk.field_3068() + u(turned));
    }
    for (const std::int32_t edge : {0x800, -0x800}) {
        const std::int32_t phase = static_cast<std::int32_t>(sk.field_3068());
        if (edge > 0 ? phase >= edge : phase <= edge) {
            sk.set_field_3068(u(phase - edge));
            if (sk.field_2c84() == 0) {
                sk.set_field_306c(sk.field_306c() + 1);
                thiscall(kLevelUp, self + 0x168);
            }
        }
    }
}

// Milestone 10g, session 10: the turn phase and the surface response's correction, written from the machine code.

namespace {

constexpr Word kTurnLocals = pcplatform::kScratchBase + 0xb00;     // the stack locals of 0x0049b500 and 0x0049c060
constexpr Word kTurnAngles = kTurnLocals + 0x00;                    // three shorts
constexpr Word kTurnMatrix = kTurnLocals + 0x20;                    // the matrix of the angles (0x20 bytes)
constexpr Word kTurnSaved = kTurnLocals + 0x40;                     // the saved orientation transposed, then turned (0x20 bytes)
constexpr Word kTurnRow = kTurnLocals + 0x80;                       // a row of the product, or the cross product (three dwords)
constexpr Word kCorrectionVector = kTurnLocals + 0xa0;              // the unit vector of 0x0049c060 (three dwords)
constexpr Word kTurnGteRotation = 0x006a3e10;                       // nine shorts
constexpr Word kTurnGteIr = 0x006a3eb0;                             // three shorts
constexpr Word kTurnGteMac = 0x006a3eb8;                            // three dwords

// the compiler's division by a constant: the high word of the signed product with the multiplier, the dividend added, shifted, one added for a negative result
inline std::int32_t magic_divide(std::int32_t value, std::int32_t multiplier, int shift) {
    const std::int32_t high = static_cast<std::int32_t>((static_cast<std::int64_t>(multiplier) * value) >> 32);
    std::int32_t quotient = static_cast<std::int32_t>(static_cast<Word>(high) + static_cast<Word>(value)) >> shift;
    quotient += static_cast<std::int32_t>(static_cast<Word>(quotient) >> 31);
    return quotient;
}

}  // namespace

// the GTE's rotation is loaded from 'source' (nine shorts); column c of 'target' becomes that rotation times column c of 'factor'
void SkaterKit::multiply_columns(Word source, Word factor, Word target) {
    for (Word k = 0; k < 9; ++k) {
        set_half_at(m_, kTurnGteRotation + 2 * k, half_at(m_, source + 2 * k));
    }
    for (Word column = 0; column < 3; ++column) {
        for (Word row = 0; row < 3; ++row) {
            set_half_at(m_, kTurnGteIr + 2 * row, half_at(m_, factor + 2 * (3 * row + column)));
        }
        math_.rtir();
        for (Word row = 0; row < 3; ++row) {
            set_half_at(m_, target + 2 * (3 * row + column), half_at(m_, kTurnGteIr + 2 * row));
        }
    }
}

// 0x0049b500 Skater_ApplyTurnVelocityPhase (thiscall: the angle, whether the velocity follows, a second angle). The orientation is turned about the vertical by the angle (the matrix of (0, angle, 0)
// on its right, column by column through the GTE) and the basis published. When the velocity follows it is turned too: the saved orientation (the record at +0x2e38) transposed and turned by the
// angle less the second one, then multiplied into the saved record; the rows of that product are the velocity's new coordinates, and the velocity keeps its length (times the old over the new).
void SkaterKit::apply_turn_velocity_phase(Word self, Word amount, Word follow, Word second) {
    const auto sk = skater_at(m_, self);
    if (amount == 0 || sk.field_2f64() != 0) {
        return;
    }
    const Word orientation = self + 0x2e58;
    const Word saved = self + 0x2e38;
    const Word velocity = self + 0x4c;
    const auto turn_matrix = [&](Word angle) {
        set_half_at(m_, kTurnAngles + 4, 0);
        set_global_half(m_, kTurnAngles, 0);
        set_half_at(m_, kTurnAngles + 2, static_cast<std::uint16_t>(angle & 0xfff));
        math_.rotation_from_angles(kTurnAngles, kTurnMatrix);
    };
    turn_matrix(amount);
    multiply_columns(orientation, kTurnMatrix, orientation);
    fru_from_rotation_matrix(self);
    if (follow == 0) {
        return;
    }
    const std::int32_t length_before = s(u(integer_sqrt(math_.dot_q12(velocity, velocity))) << 6);
    math_.matrix_transpose(saved, kTurnSaved);
    turn_matrix(amount - second);
    multiply_columns(kTurnSaved, kTurnMatrix, kTurnSaved);
    // the saved record (retail copies it to a local first; nothing writes it meanwhile) times the turned transpose: row r of the result is (column 0, 1, 2 of the product's row r)
    for (Word k = 0; k < 9; ++k) {
        set_half_at(m_, kTurnGteRotation + 2 * k, half_at(m_, saved + 2 * k));
    }
    Word product[3][3];                               // [column][row]
    for (Word column = 0; column < 3; ++column) {
        for (Word row = 0; row < 3; ++row) {
            set_half_at(m_, kTurnGteIr + 2 * row, half_at(m_, kTurnSaved + 2 * (3 * row + column)));
        }
        math_.rtir();
        for (Word row = 0; row < 3; ++row) {
            product[column][row] = half_at(m_, kTurnGteIr + 2 * row);
        }
    }
    Word turned[3];
    for (Word row = 0; row < 3; ++row) {
        for (Word column = 0; column < 3; ++column) {
            set_word_at(m_, kTurnRow + 4 * column, u(sx(product[column][row])));
        }
        turned[row] = u(math_.dot_q12(kTurnRow, velocity));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, velocity + 4 * k, turned[k]);
    }
    const std::int32_t after = s(u(integer_sqrt(math_.dot_q12(velocity, velocity))) << 6) >> 8;
    if (after > 0) {
        for (Word k = 0; k < 3; ++k) {
            const Word scaled = word_at(m_, velocity + 4 * k) * u(length_before >> 8);
            set_word_at(m_, velocity + 4 * k, u(s(scaled) / after));
        }
    }
}

// 0x0049c060 Skater_ApplySurfaceResponseCorrection (thiscall, one argument). Clears +0x3124 and, off the +0x2f64 mode, finds the correction: the lengths of the velocity and of the vector at +0x3118
// (times 64) against stat 3's limit (45 * (stat + 0x186) * 0x1000 / 280), the argument (in units of 0x1000) added to the limit less the speed, scaled by the vector's length and divided by the limit again.
// Unless that is 0, the side of the slope vector (+0x30f4) against the unit vector of +0x3118 crossed with the normal's diagonal decides: beyond +-0x29 it sets +0x3124 to the amount (negated on the
// positive side) and the state +0x2e24 to 1 or 2 (the same side twice does nothing); within them it clears the state.
void SkaterKit::surface_response_correction(Word self, Word argument) {
    const auto sk = skater_at(m_, self);
    constexpr Word kProfileValue = 0x0048f3a0;         // Skater_ProfileValue(selector) (thiscall)
    constexpr Word kFrameClock = 0x005685f4;
    sk.set_field_3124(0);
    if (sk.field_2f64() != 0) {
        return;
    }
    std::int32_t speed = s(u(integer_sqrt(math_.dot_q12(self + 0x4c, self + 0x4c))) << 6);
    const Word turn_vector = self + 0x3118;
    const std::int32_t length = s(u(integer_sqrt(math_.dot_q12(turn_vector, turn_vector))) << 6);
    const std::int32_t first = magic_divide(mul(length, 50), static_cast<std::int32_t>(0xa151c331u), 13);
    const std::int32_t fraction = magic_divide(mul(first, length), static_cast<std::int32_t>(0xa151c331u), 13);
    const auto limit = [&]() {
        const std::int32_t stat = s(thiscall_value(kProfileValue, self, {3}));
        return magic_divide(s(u(mul(s(u(stat) + 0x186), 45)) << 12), static_cast<std::int32_t>(0xea0ea0ebu), 8);
    };
    if (speed > limit()) {
        speed = limit();
    }
    std::int32_t numerator = s(u(limit()) + (argument << 12));
    numerator = mul(s(u(numerator) - u(speed)), fraction);
    const std::int32_t divisor = limit();
    if (divisor == 0 || (divisor == -1 && numerator == INT32_MIN)) {
        throw std::runtime_error("division fault in Skater_ApplySurfaceResponseCorrection");
    }
    const std::int32_t amount = numerator / divisor;
    if (amount == 0) {
        return;
    }
    if (s(sk.field_2e28() + 0xa) < global_int(m_, kFrameClock)) {
        sk.set_field_2e24(0);
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kCorrectionVector + 4 * k, word_at(m_, turn_vector + 4 * k));
    }
    sk.set_field_2e28(global_word(m_, kFrameClock));
    normalize_fixed12(kCorrectionVector);
    set_global_half(m_, kTurnGteRotation, static_cast<std::uint16_t>(sk.field_3128()));
    set_half_at(m_, kTurnGteRotation + 8, half_at(m_, self + 0x312a));
    set_half_at(m_, kTurnGteRotation + 16, static_cast<std::uint16_t>(sk.field_312c()));
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, kTurnGteIr + 2 * k, half_at(m_, kCorrectionVector + 4 * k));
    }
    math_.cross_sf12();
    // (retail also copies the slope vector, takes its component along the normal off and dots that with the unit vector; the result is dropped)
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kTurnRow + 4 * k, word_at(m_, kTurnGteMac + 4 * k));
    }
    const std::int32_t side = math_.dot_q12(self + 0x30f4, kTurnRow);
    if (side > 0x29) {
        if (sk.field_2e24() == 2) {
            return;
        }
        sk.set_field_3124(0u - u(amount));
        sk.set_field_2e24(1);
    } else if (side >= -0x29) {
        sk.set_field_2e24(0);
    } else {
        if (sk.field_2e24() == 1) {
            return;
        }
        sk.set_field_3124(u(amount));
        sk.set_field_2e24(2);
    }
}

// 0x00496060 Skater_PositionWritePath (thiscall: x, y, z). With +0x3200 set the position is written as it is. Otherwise a zone query (the owner flag 0x00567c7c set for it) goes from the old position
// to the new one: with no hit the new position is written; else the move is tried along fewer axes, in the order (old x, y, z), (x, y, old z), (x, old y, z), (x, old y, old z), (old x, old y, z),
// (old x, y, old z), and the first that meets nothing is written; if every one does, the skater stays. The record is the query record of the scratch area (a stack local in retail), and only the
// fields retail rewrites between the queries are rewritten here.
void SkaterKit::position_write_path(Word self, Word x, Word y, Word z) {
    const auto sk = skater_at(m_, self);
    constexpr Word kInitQuery = 0x004624d0;           // M3D_InitCollisionQuery(record)
    constexpr Word kZoneQuery = 0x00466090;           // M3D_QueryZoneCollision(record, 1)
    constexpr Word kOwner = 0x00567c7c;               // dword: set while a query of a skater's move runs
    if (sk.field_3200() != 0) {
        set_component(sk.position(), 0, x);
        set_component(sk.position(), 1, y);
        set_component(sk.position(), 2, z);
        return;
    }
    const Word owner = global_word(m_, kOwner);
    set_global_word(m_, kOwner, 1);
    const Word old_x = component(sk.position(), 0);
    const Word old_y = component(sk.position(), 1);
    const Word old_z = component(sk.position(), 2);
    const Word record = pcplatform::kScratchBase + 0xf50;
    set_line_start(line_at(m_, record), 0, old_x);
    set_line_start(line_at(m_, record), 1, old_y);
    set_line_start(line_at(m_, record), 2, old_z);
    set_line_end(line_at(m_, record), 0, x);
    set_line_end(line_at(m_, record), 1, y);
    set_line_end(line_at(m_, record), 2, z);
    // true when the line from the old position to the record's end meets something
    const auto hit = [&]() {
        engine(kInitQuery, {record});
        engine(kZoneQuery, {record, 1}, query_line(record));
        if (!s_.live()) {
            line_at(m_, record).set_hit_body(GuestPtr<RuntimeCollisionObject>{static_cast<Word>(static_cast<Word>(s_.value(pcplatform::engine_name(kZoneQuery))))});      // (the body the line met: the host's answer)
        }
        return line_at(m_, record).hit_body().address != 0;
    };
    Word out_x, out_y, out_z;
    const auto end = [&](Word at_x, Word at_y, Word at_z) { set_line_end(line_at(m_, record), 0, at_x); set_line_end(line_at(m_, record), 1, at_y); set_line_end(line_at(m_, record), 2, at_z); };
    const auto result = [&]() { out_x = line_end(line_at(m_, record), 0); out_y = line_end(line_at(m_, record), 1); out_z = line_end(line_at(m_, record), 2); };
    if (!hit()) {
        result();
    } else {
        set_line_end(line_at(m_, record), 0, old_x);
        if (!hit()) {
            result();
        } else {
            set_line_end(line_at(m_, record), 0, x);
            set_line_end(line_at(m_, record), 2, old_z);
            if (!hit()) {
                result();
            } else {
                set_line_end(line_at(m_, record), 1, old_y);
                set_line_end(line_at(m_, record), 2, z);
                if (!hit()) {
                    result();
                } else {
                    end(x, old_y, old_z);
                    if (!hit()) {
                        result();
                    } else {
                        end(old_x, old_y, z);
                        if (!hit()) {
                            result();
                        } else {
                            end(old_x, y, old_z);
                            if (!hit()) {
                                result();
                            } else {
                                out_x = old_x;
                                out_y = old_y;
                                out_z = old_z;
                            }
                        }
                    }
                }
            }
        }
    }
    set_component(sk.position(), 1, out_y);
    set_component(sk.position(), 0, out_x);
    set_component(sk.position(), 2, out_z);
    set_global_word(m_, kOwner, owner);
}

// Milestone 10g, session 11: the services of the ground physics' collision (the response, the recovery, the exit), the air hit and the landing, written from the machine code.

namespace {

constexpr Word kRebuildLocals = pcplatform::kScratchBase + 0xd00;   // 0x0049d080's six stack dwords: the target (3), then the right vector (3)
constexpr Word kResponseNormal = pcplatform::kScratchBase + 0xd40;  // 0x0049bad0's normal (three dwords, negated halfway)
constexpr Word kResponseMatrix = pcplatform::kScratchBase + 0xd50;  // the turn matrix (nine shorts; the angles' matrix fills it in place)
constexpr Word kResponseAngles = pcplatform::kScratchBase + 0xd70;  // three shorts
constexpr Word kAirVelocity = pcplatform::kScratchBase + 0xd80;     // 0x00491780's stack vectors (three dwords each, 0x10 apart)
constexpr Word kAirSide = kAirVelocity + 0x10;                      // the forward vector without the normal
constexpr Word kAirDirection = kAirVelocity + 0x20;                 // the velocity's direction
constexpr Word kAirOffset = kAirVelocity + 0x30;                    // the move since the saved position
constexpr Word kAirMove = kAirVelocity + 0x40;                      // its copy, scaled down and normalised

constexpr Word kRailUpdate = 0x004aaf70;          // Rail_Update(skater) (cdecl)
constexpr Word kSelectSequence = 0x004925e0;      // TrickInput_SelectSequence (thiscall)
constexpr Word kStartGrind = 0x00490ef0;          // Skater_StartGrind(flag) (thiscall)
constexpr Word kBail = 0x004904d0;                // CBruce__Bail(script, cause) (thiscall)
constexpr Word kApplyGap = 0x0048f690;            // Gap_ApplyDefinitionState(name, value, flag, index) (thiscall)
constexpr Word kGapName = 0x00536bb4;
constexpr Word kAirLock = 0x0056a890;             // dword: non-zero ends the air response before the bail
constexpr Word kSecondFlag = 0x0056b7f0;

}  // namespace

// 0x0049bad0 Skater_ApplyCollisionResponse (thiscall: the normal as two packed shorts and a short, a bump angle that is 0x19 when 0). A velocity moving into the surface loses the part along the normal and
// is pushed off it by 0xcd/0x1000 of the normal. Then, when the forward vector goes into the surface (its dot with the reversed normal is negative, the cosine c), the orientation is turned about the
// vertical by the matrix (c, 0, -s / 0, 0x1000, 0 / s, 0, c), s the root of 1 - c^2 with the sign of the right vector against the normal, and then by the bump angle (a quarter turn less or more); the
// basis is rebuilt from the shorts and made orthonormal (forward and up normalised, right their cross product, forward the cross product of up and right).
void SkaterKit::collision_response(Word self, Word normal_xy, Word normal_z, Word bump) {
    const auto sk = skater_at(m_, self);
    if (bump == 0) {
        bump = 0x19;
    }
    const std::int32_t nx = sx(normal_xy);
    const std::int32_t ny = sx(normal_xy >> 16);
    const std::int32_t nz = sx(normal_z);
    const Word velocity = self + 0x4c;
    set_global_word(m_, kResponseNormal, u(nx));
    set_word_at(m_, kResponseNormal + 4, u(ny));
    set_word_at(m_, kResponseNormal + 8, u(nz));
    const std::int32_t into = math_.dot_q12(velocity, kResponseNormal);
    if (into < 0) {
        const std::int32_t normal[3] = {nx, ny, nz};
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, velocity + 4 * k, word_at(m_, velocity + 4 * k) - u(math_.mul_q12(normal[k], into)));
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, velocity + 4 * k, word_at(m_, velocity + 4 * k) + u(math_.mul_q12(normal[k], 0xcd)));
        }
    }
    set_global_word(m_, kResponseNormal, 0u - u(nx));
    set_word_at(m_, kResponseNormal + 4, 0u - u(ny));
    set_word_at(m_, kResponseNormal + 8, 0u - u(nz));
    const std::int32_t cosine = math_.dot_q12(self + 0x30f4, kResponseNormal);
    if (cosine >= 0) {
        return;
    }
    std::int32_t sine = integer_sqrt(s(0x1000000u - u(mul(cosine, cosine))));
    const std::int32_t side = math_.dot_q12(self + 0x3100, kResponseNormal);
    if (side > 0) {
        sine = -sine;
    }
    const Word orientation = self + 0x2e58;
    const auto set_matrix = [&](std::int32_t c, std::int32_t sn) {
        const std::int32_t values[9] = {c, 0, -sn, 0, 0x1000, 0, sn, 0, c};
        for (Word k = 0; k < 9; ++k) {
            set_half_at(m_, kResponseMatrix + 2 * k, static_cast<std::uint16_t>(values[k]));
        }
    };
    set_matrix(cosine, sine);
    multiply_columns(orientation, kResponseMatrix, orientation);
    set_global_half(m_, kResponseAngles, 0);
    set_half_at(m_, kResponseAngles + 4, 0);
    set_half_at(m_, kResponseAngles + 2, static_cast<std::uint16_t>(side > 0 ? ((bump - 0x400) & 0xfff) : ((0x400 - bump) & 0xfff)));
    math_.rotation_from_angles(kResponseAngles, kResponseMatrix);
    multiply_columns(orientation, kResponseMatrix, orientation);
    fru_from_rotation_matrix(self);
    normalize_fixed12(self + 0x30f4);
    normalize_fixed12(self + 0x310c);
    set_global_half(m_, kTurnGteRotation, static_cast<std::uint16_t>(component(sk.vector_30f4(), 0)));
    set_half_at(m_, kTurnGteRotation + 8, static_cast<std::uint16_t>(component(sk.vector_30f4(), 1)));
    set_half_at(m_, kTurnGteRotation + 16, static_cast<std::uint16_t>(component(sk.vector_30f4(), 2)));
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, kTurnGteIr + 2 * k, static_cast<std::uint16_t>(component(sk.vector_310c(), k)));
    }
    math_.cross_sf12();
    for (Word k = 0; k < 3; ++k) {
        set_component(sk.vector_3100(), k, word_at(m_, kTurnGteMac + 4 * k));
    }
    normalize_fixed12(self + 0x3100);
    set_global_half(m_, kTurnGteRotation, static_cast<std::uint16_t>(component(sk.vector_310c(), 0)));
    set_half_at(m_, kTurnGteRotation + 8, static_cast<std::uint16_t>(component(sk.vector_310c(), 1)));
    set_half_at(m_, kTurnGteRotation + 16, static_cast<std::uint16_t>(component(sk.vector_310c(), 2)));
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, kTurnGteIr + 2 * k, static_cast<std::uint16_t>(component(sk.vector_3100(), k)));
    }
    math_.cross_sf12();
    for (Word k = 0; k < 3; ++k) {
        set_component(sk.vector_30f4(), k, word_at(m_, kTurnGteMac + 4 * k));
    }
    publish_orientation_basis(self);
}

// 0x0049d080 Skater_RebuildOrientationRecovery (thiscall). Unless the progress (+0x3130) is past 0x18000, the ground's normal (shorts at +0x80) is the target at 0x18000 and, before it, a quarter of the
// way from the saved vector (+0x3134) to it. The target is normalised; the basis becomes: up = target, right = the normalised cross product of the forward vector and the target, forward = the cross
// product of target and right. The orientation shorts, the integer basis (+0x30f4, +0x3100, +0x310c) and the saved vector (+0x3134, the target) are written.
void SkaterKit::rebuild_orientation_recovery(Word self) {
    const auto sk = skater_at(m_, self);
    const std::int32_t progress = static_cast<std::int32_t>(sk.field_3130());
    if (progress > 0x18000) {
        return;
    }
    const Word target = kRebuildLocals;
    const Word right = kRebuildLocals + 0xc;
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, right + 4 * k, 0);
        const Word ground = u(sx(static_cast<std::uint16_t>(component(sk.orientation_angles(), k))));
        if (progress == 0x18000) {
            set_word_at(m_, target + 4 * k, ground);
        } else {
            const Word saved = component(sk.vector_3134(), k);
            set_word_at(m_, target + 4 * k, u(s(ground - saved) >> 2) + saved);
        }
    }
    normalize_fixed12(target);
    const auto cross_of = [&](Word diagonal0, Word diagonal1, Word diagonal2, Word ir) {
        set_global_half(m_, kTurnGteRotation, half_at(m_, diagonal0));
        set_half_at(m_, kTurnGteRotation + 8, half_at(m_, diagonal1));
        set_half_at(m_, kTurnGteRotation + 16, half_at(m_, diagonal2));
        for (Word k = 0; k < 3; ++k) {
            set_half_at(m_, kTurnGteIr + 2 * k, half_at(m_, ir + 4 * k));
        }
        math_.cross_sf12();
    };
    cross_of(self + 0x2e5c, self + 0x2e62, self + 0x2e68, target);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, right + 4 * k, word_at(m_, kTurnGteMac + 4 * k));
    }
    normalize_fixed12(right);
    cross_of(target, target + 4, target + 8, right);
    const Word forward[3] = {global_word(m_, kTurnGteMac), word_at(m_, kTurnGteMac + 4), word_at(m_, kTurnGteMac + 8)};
    for (Word k = 0; k < 3; ++k) {
        sk.model_matrix().rotation().set_elements(3 * k, static_cast<std::int16_t>(half_at(m_, right + 4 * k)));         // right: +0x2e58, +0x2e5e, +0x2e64
        sk.model_matrix().rotation().set_elements(3 * k + 1, static_cast<std::int16_t>(half_at(m_, target + 4 * k)));        // up: +0x2e5a, +0x2e60, +0x2e66
        sk.model_matrix().rotation().set_elements(3 * k + 2, static_cast<std::int16_t>(forward[k]));                    // forward: +0x2e5c, +0x2e62, +0x2e68
        set_component(sk.vector_3100(), k, u(sx(half_at(m_, right + 4 * k))));
        set_component(sk.vector_310c(), k, u(sx(half_at(m_, target + 4 * k))));
        set_component(sk.vector_30f4(), k, u(sx(forward[k])));
        set_component(sk.vector_3134(), k, word_at(m_, target + 4 * k));
    }
}

// 0x004956f0 Skater_ExitGroundCollisionRecovery (thiscall). In state 2 the skater goes to state 1 (line 0x1605) and that is all. Otherwise: state 1 (0x160b), the take-off, the trick counters cleared and
// +0x2ddc set. Unless +0x2f64 is set, a trick script that waits (+0x29d0, with +0x2dd4 set) starts; else the animation 0x1a (when +0x2dd8 is set) or 0x1b is requested. +0x2dd4 ends cleared.
void SkaterKit::exit_ground_collision_recovery(Word self) {
    const auto sk = skater_at(m_, self);
    if (sk.physics_state() == 2) {
        thiscall(kStateTransition, self, {1, 0x1605});
        return;
    }
    thiscall(kStateTransition, self, {1, 0x160b});
    thiscall(kTakeOff, self);
    const Word frozen = sk.field_2f64();
    sk.set_field_3068(0);
    sk.set_field_306c(0);
    sk.set_field_2c68(0);
    sk.set_field_2ddc(1);
    sk.set_field_2dd0(0);
    if (frozen == 0) {
        const Word script = sk.field_29d0();
        if (script != 0 && sk.field_2dd4() != 0) {
            trick_script_start(self, script);
            sk.set_field_29d0(0);
            sk.set_field_2dd4(0);
            return;
        }
        sk.set_field_29c8(0);
        if (sk.field_2dd8() != 0) {
            thiscall(kRequestStart, self, {0x1a, 0x1628});
        } else {
            thiscall(kRequestStart, self, {0x1b, 0x162a});
        }
    }
    sk.set_field_2dd4(0);
}

// 0x00491780 Skater_AirCollisionOrientationRecovery (thiscall: the normal as two packed shorts and a short). The velocity without its part along the normal, when faster than 0x1e000 (its length times
// 64), is compared in direction with the forward vector without that part (their cosine): +0x3078 is set above 0xfc1 and +0x307c under 0xb50 (unless +0x3074). Unless the air lock 0x56a890 is set, a cosine
// under 0x666 turns the skater: the up vector becomes the reverse of the move since the saved position (+0x2e00; scaled down by 16 when its three components add up past 0x8000, normalised), the right
// vector their cross product with the normal and the forward vector the cross product of that and the normal, and the skater bails (script 0 or 2 by the side of the velocity). Then, unless +0x2f64,
// +0x30c4, cheat 14 or the slow motion flags, the height (+0x2f38 + +0x2f40 >> 12) against the table (+0x2f3c, 60 less for each of the 11 levels of +0x2de8) bails or applies the gap definition 0x536bb4.
void SkaterKit::air_collision_recovery(Word self, Word normal_xy, Word normal_z) {
    const auto sk = skater_at(m_, self);
    const auto copy = [&](Word to, Word from) {
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, to + k, word_at(m_, from + k));
        }
    };
    sk.set_field_3078(0);
    sk.set_field_307c(0);
    copy(kAirVelocity, self + 0x4c);
    remove_normal_component(kAirVelocity, normal_xy, normal_z);
    const std::int32_t speed = s(u(integer_sqrt(math_.dot_q12(kAirVelocity, kAirVelocity))) << 6);
    if (speed > 0x1e000) {
        copy(kAirDirection, kAirVelocity);
        normalize_fixed12(kAirDirection);
        copy(kAirSide, self + 0x30f4);
        remove_normal_component(kAirSide, normal_xy, normal_z);
        normalize_fixed12(kAirSide);
        const std::int32_t cosine = math_.dot_q12(kAirDirection, kAirSide);
        if (sk.field_3074() == 0) {
            if (abs32(cosine) > 0xfc1) {
                sk.set_field_3078(1);
            }
            if (abs32(cosine) < 0xb50) {
                sk.set_field_307c(1);
            }
        }
        if (global_word(m_, kAirLock) != 0) {
            return;
        }
        if (abs32(cosine) < 0x666) {
            thiscall_value(kFlipToVelocity, self);
            const std::int32_t along = math_.dot_q12(kAirVelocity, self + 0x3100);
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kAirOffset + 4 * k, component(sk.previous_position(), k) - component(sk.vector_2e00(), k));
            }
            copy(kAirMove, kAirOffset);
            const Word sum = u(abs32(global_int(m_, kAirMove))) + u(abs32(int_at(m_, kAirMove + 4))) + u(abs32(int_at(m_, kAirMove + 8)));
            if (s(sum) > 0x8000) {
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, kAirMove + 4 * k, u(int_at(m_, kAirMove + 4 * k) >> 4));
                }
            }
            math_.normalize(kAirMove, kAirMove);
            for (Word k = 0; k < 3; ++k) {
                set_component(sk.vector_30f4(), k, 0u - word_at(m_, kAirMove + 4 * k));
            }
            set_component(sk.vector_310c(), 0, u(sx(normal_xy)));
            set_component(sk.vector_310c(), 1, u(sx(normal_xy >> 16)));
            set_component(sk.vector_310c(), 2, u(sx(normal_z)));
            const auto cross_of = [&](Word diagonal, Word ir, Word out) {
                set_global_half(m_, kTurnGteRotation, half_at(m_, diagonal));
                set_half_at(m_, kTurnGteRotation + 8, half_at(m_, diagonal + 4));
                set_half_at(m_, kTurnGteRotation + 16, half_at(m_, diagonal + 8));
                for (Word k = 0; k < 3; ++k) {
                    set_half_at(m_, kTurnGteIr + 2 * k, half_at(m_, ir + 4 * k));
                }
                math_.cross_sf12();
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, out + 4 * k, word_at(m_, kTurnGteMac + 4 * k));
                }
            };
            cross_of(self + 0x30f4, self + 0x310c, self + 0x3100);
            cross_of(self + 0x310c, self + 0x3100, self + 0x30f4);
            publish_orientation_basis(self);
            const std::int32_t direction = (sk.field_d8() & 2) != 0 ? s(0u - u(along)) : along;
            thiscall(kBail, self, {direction > 0 ? 0u : 2u, 0});
            sk.set_field_3030(sk.field_3030() + 1);
        }
    }
    if (sk.field_2f64() != 0 || sk.field_30c4() != 0) {
        return;
    }
    if ((engine_value(kCheatOn, {0xe}) & 0xff) != 0) {
        return;
    }
    if (global_word(m_, kSlowMotion) != 0 || global_word(m_, kSecondFlag) != 0) {
        return;
    }
    const std::int32_t level = sk.field_2de8();
    const Word base = sk.field_2f3c();
    Word limit = base;
    if (level != 0) {
        limit = base + u(mul(11 - level, 60));
    }
    const std::int32_t height = s(sk.field_2f40() + sk.field_2f38()) >> 12;
    if (height > s(limit)) {
        sk.set_field_3078(0);
        sk.set_field_307c(0);
        thiscall(kBail, self, {0, 0});
        return;
    }
    if (height <= s(base)) {
        return;
    }
    thiscall(kApplyGap, self, {kGapName, u(mul(mul(level, 5) - 0x37, 20)), 0, 0xffffffffu});
}

// 0x004914d0 Skater_LandingCleanup (thiscall). The rail check runs; the counters +0x2ec4 and +0x2ec0 run down (the second ends the call, clearing +0x2e90 and +0x2e94). When the ground flag (the byte
// +0x2ccc points at) is set and nothing holds the landing off (+0x2f64, state 2 moving up, +0x2e34, +0x2e90; +0x2e94 must be set), the landing is judged: a flip count whose phase (+0x2c8c) is on the wrong
// side, or a trick on (+0x2c68, with +0x2c6c clear), bails when the animation frame (+0xf4) is out of the window around +0x101 (by +0x2e2c, the other way with +0x100 = 1) or +0x2e2c is 0; else the trick
// sequence is selected, and with a landing trick (+0x3064) the grind starts (flag: moving down, the turn small, the slope steep, the lean small).
void SkaterKit::landing_cleanup(Word self) {
    const auto sk = skater_at(m_, self);
    engine(kRailUpdate, {self});
    const Word ground = sk.action_record_bank().address;
    const bool on_ground = byte_at(m_, ground) != 0;
    if (!on_ground && sk.field_2e90() != 0) {
        sk.set_field_2e30(0);
    }
    if (sk.field_2ec4() != 0) {
        sk.set_field_2ec4(sk.field_2ec4() - framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }));
    }
    const std::int32_t count = sk.field_2ec0();
    const auto stop = [&] { sk.set_field_2e94(0); };
    if (count > 0) {
        sk.set_field_2e90(0);
        sk.set_field_2ec0(static_cast<std::int32_t>(u(count - static_cast<std::int32_t>(framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); })))));
        stop();
        return;
    }
    if (!on_ground || sk.field_2f64() != 0) {
        stop();
        return;
    }
    const Word state = sk.physics_state();
    if (state == 2 && static_cast<std::int32_t>(component(sk.velocity(), 1)) > 0) {
        stop();
        return;
    }
    if (sk.field_2e34() != 0) {
        stop();
        return;
    }
    if (sk.field_2e94() == 0) {
        return;
    }
    if (sk.field_2e90() != 0) {
        stop();
        return;
    }
    sk.set_field_2ec0(0);
    if (state == 0) {
        sk.set_field_306c(0);
        sk.set_field_3068(0);
    }
    bool turned = false;
    if (sk.field_2c88() != 0) {
        const std::int32_t phase = sk.field_2c8c() & 0xfff;
        turned = sk.field_2c90() > 0 ? phase < 0xed4 : phase > 0x12c;
    }
    const Word trick = sk.field_2c68();
    if ((trick != 0 && sk.field_2c6c() == 0) || turned) {
        const std::int32_t window = s(sk.field_2e2c());
        bool bailing = window == 0;
        if (!bailing) {
            const std::int32_t edge = static_cast<std::int8_t>(sk.field_101());
            const std::int32_t frame = sx(sk.field_f4());
            bailing = sk.field_100() == 1 ? frame < edge - window : frame > edge + window;
        }
        if (bailing) {
            thiscall(kBail, self, {u(sx(sk.field_29f2())), 0});
            const Word bails = sk.field_3034();
            sk.set_field_29f2(0);
            sk.set_field_3034(bails + 1);
            return;
        }
    }
    if (trick != 0) {
        sk.set_field_2c70(1);
    }
    bool grind = false;
    if (static_cast<std::int32_t>(component(sk.velocity(), 1)) < 0 && abs32(static_cast<std::int32_t>(component(sk.vector_310c(), 1))) < 0xbb8 && static_cast<std::int32_t>(component(sk.vector_30f4(), 1)) > 0x800 && abs32(sx(static_cast<std::uint16_t>(component(sk.orientation_angles(), 1)))) < 0x5dc &&
        abs32(static_cast<std::int32_t>(component(sk.vector_3100(), 1))) < 0x800) {
        grind = true;
    }
    thiscall(kSelectSequence, self);
    if (sk.field_3064() == 0) {
        stop();
        return;
    }
    thiscall(kStartGrind, self, {grind ? 1u : 0u});
}

// Milestone 10g, session 12: the sway after a ground collision, the grounded bounce probe and the air landing's orientation recovery, written from the machine code.

namespace {

constexpr Word kProbeLocals = pcplatform::kScratchBase + 0xe00;     // 0x004957c0's stack vectors (three dwords each): the hit's normal, its copy for the second dot, the velocity copy
constexpr Word kProbeNormal = kProbeLocals;
constexpr Word kProbeCopy = kProbeLocals + 0x10;
constexpr Word kProbeVelocity = kProbeLocals + 0x20;
constexpr Word kAirLandingNormal = pcplatform::kScratchBase + 0xe40;   // 0x00497960's vector (three dwords)

constexpr Word kRandom = 0x004c9340;              // Rnd(n) -> 0..n-1 (cdecl)
constexpr Word kDebugLog = seam::hal::printf_log; // (format, line) (cdecl)
constexpr Word kLogFormat = 0x00536dbc;

// the compiler's division by 10000: the high word of the signed product with 0x68db8bad, shifted down by 12, one added for a negative result
inline std::int32_t divide_by_10000(std::int32_t value) {
    const std::int32_t high = static_cast<std::int32_t>((static_cast<std::int64_t>(0x68db8bad) * value) >> 32);
    std::int32_t quotient = high >> 12;
    quotient += static_cast<std::int32_t>(static_cast<Word>(quotient) >> 31);
    return quotient;
}

// idiv: the processor faults on a zero divisor and on the smallest value over -1
inline std::int32_t divide_checked(std::int32_t value, std::int32_t divisor) {
    if (divisor == 0 || (divisor == -1 && value == INT32_MIN)) {
        throw std::runtime_error("division fault in Skater_GroundPostCollision");
    }
    return value / divisor;
}

}  // namespace

// 0x00495cc0 Skater_GroundPostCollision (thiscall, a service of the dispatcher). With +0x2dd4 set: the count +0x2f24 runs down by 200 (not below 0), the point timer advances, +0x2f20 takes the frame step
// and the balance +0x2f14 moves: by +0x2f14 / c1 times (+0x2f20 >> 12) + 30, divided by c2, and by +0x2f18 times the same divided by c3, each times the time scale >> 8 (c1 to c3 are stat 9 through
// 4 * 10000 divisions; cheat 5 clears the balance). A press on the edge of the pad (the byte +0x31a2 against +-40 with the ground record's bytes +0xa0 and +0xb0) sets +0x2f10 and a set +0x2f10 moves the
// speed +0x2f18 by 20 * the time scale >> 8 towards the other side; otherwise it is kicked by two random numbers (a small one when it is already large). A balance past 4000 ends the sway: in state 1 with
// a positive balance, or state 2 with a negative one, the bail script +0x29f0 (6 or 0 when it is 0) is bailed with and the balance cleared; else the balance is cleared and the pending landing, the waiting
// script (+0x29d0) or animation 5 follows.
void SkaterKit::ground_post_collision(Word self) {
    const auto sk = skater_at(m_, self);
    if (sk.field_2dd4() == 0) {
        return;
    }
    Word timer = sk.field_2f24();
    if (timer != 0) {
        timer -= 200;
        sk.set_field_2f24(timer);
        if (s(timer) < 0) {
            sk.set_field_2f24(0);
        }
    }
    thiscall(kAdvancePointTimer, self, {sk.field_2f00()});
    sk.set_field_2f20(sk.field_2f20() + global_word(m_, kFrameStep));
    const auto stat = [&] { return s(thiscall_value(kProfileValue, self, {9})); };
    const auto elapsed = [&] { return (static_cast<std::int32_t>(sk.field_2f20()) >> 12) + 0x1e; };
    const auto divisor = [&](std::int32_t value) { return divide_by_10000(s(u(mul(value, 6000)) + 200000u)); };
    const auto drift = [&](framepacing::CarrySite site, std::int32_t quotient) {      // retail: quotient x scale >> 8 (frame_pacing.hpp)
        sk.set_field_2f14(sk.field_2f14() + u(framepacing::scaled_carry(site, self, mul(quotient, global_int(m_, kTimeScale)))));
    };
    const std::int32_t first = divide_by_10000(s(u(mul(stat(), 3300)) + 280000u));
    const std::int32_t scaled = mul(divide_checked(static_cast<std::int32_t>(sk.field_2f14()), first), elapsed());
    const std::int32_t second = divisor(stat());
    drift(framepacing::CarrySite::manual_lean, divide_checked(scaled, second));
    const std::int32_t third = divisor(stat());
    drift(framepacing::CarrySite::manual_push, divide_checked(mul(static_cast<std::int32_t>(sk.field_2f18()), elapsed()), third));
    if ((engine_value(kCheatOn, {5}) & 0xff) != 0) {
        sk.set_field_2f14(0);
    }

    const Word record = sk.action_record_bank().address;
    const bool left_flag = byte_at(m_, record + 0xa0) != 0;
    const bool right_flag = byte_at(m_, record + 0xb0) != 0;
    const std::int32_t lean = static_cast<std::int8_t>(sk.field_31a2());
    if (!left_flag && lean >= -40 && !right_flag && lean <= 40) {
        sk.set_field_2f10(1);
    }
    const std::int32_t shove = mul(20, global_int(m_, kTimeScale)) >> 8;
    if ((left_flag || lean < -40) && sk.field_2f10() != 0) {
        sk.set_field_2f18(sk.field_2f18() - u(shove));
    } else if ((right_flag || lean > 40) && sk.field_2f10() != 0) {
        sk.set_field_2f18(sk.field_2f18() + u(shove));
    } else if (framepacing::runs_30hz_frame([&] { return global_word(m_, 0x0056a940); })) {      // the random kicks of the balance speed: one a 30 Hz frame, not one a frame (4 times as many at 120)
        const std::int32_t speed = static_cast<std::int32_t>(sk.field_2f18());
        const std::int32_t limit = divide_by_10000(mul(s(200u - u(stat())), 1000));
        if (abs32(speed) < limit) {
            const std::int32_t spread = divide_by_10000(s(800000u - u(mul(stat(), 3300))));
            const std::int32_t random = s(engine_value(kRandom, {u(spread)}));
            std::int32_t back = divide_by_10000(s(u(mul(stat(), 3300)) - 800000u));
            back = (back + (back < 0 ? 1 : 0)) >> 1;
            sk.set_field_2f18(u(random) + u(back));
        } else {
            const Word random = engine_value(kRandom, {4});
            sk.set_field_2f18(sk.field_2f18() + random - 2u);
        }
    }

    const std::int32_t balance = static_cast<std::int32_t>(sk.field_2f14());
    if (abs32(balance) <= 0xfa0) {
        return;
    }
    const Word state = static_cast<Word>(sk.field_2dd4());
    const auto bail_with = [&](Word fallback) {
        const std::uint16_t script = sk.field_29f0();
        sk.set_field_2f14(0);
        thiscall(kBail, self, {script == 0 ? fallback : u(sx(script)), 0});
    };
    if (state == 1 && balance > 0) {
        bail_with(6);
        return;
    }
    if (state == 2 && balance < 0) {
        bail_with(0);
        return;
    }
    sk.set_field_2f14(0);
    thiscall(kLandPending, self);
    const Word script = sk.field_29d0();
    if (script != 0) {
        trick_script_start(self, script);
        sk.set_field_29d0(0);
        return;
    }
    thiscall(kRequestStart, self, {5, 0x1773});
}

// 0x004957c0 Skater_GroundedBounceProbe (thiscall: the side (+-1), the distance and whether to respond; returns 1 when the skater was moved off). A line from 30 above the skater to that point plus the
// right vector times the side and the distance goes through the zone: no hit (the body +0x68 is 0) returns 0. A hit whose normal is within 0x400 of at right angles to the up vector puts the skater
// back where it was; with the response flag, a head-on hit (not in animation 0xc, speed over 6, no +0x2f64 or +0x2c74, the normal's dot with the forward vector over 0xb50) cancels the landing and the
// tricks, starts animation 0xc and grunts, and the collision response runs (the bump 0xc8 after such a hit). A second line from one normal's length out to the normal times the distance, from the
// old position, that meets nothing moves the skater to its end (+0x3200 set, 1); else the debug flag logs and restores the position the frame began with, with the turn phase 0x898, and 0 is returned.
Word SkaterKit::grounded_bounce_probe(Word self, Word side, Word distance, Word respond) {
    const auto sk = skater_at(m_, self);
    const Word position = self + 8;
    const Word up = self + 0x310c;
    const Word right = self + 0x3100;
    const Word record = kQuery;
    const auto query = [&]() {
        engine(kInitQuery, {record});
        engine(kZoneQuery, {record, 1}, query_line(record));
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(kZoneQuery);      // (the fields of the hit: the host's answers)
            for (const Word offset : {0x68u, 0x6cu, 0x70u, 0x74u, 0x78u, 0x7cu, 0x40u, 0x80u}) {
                set_word_at(m_, record + offset, static_cast<Word>(s_.value(name)));
            }
        }
        read_surface_flags(record);
    };
    for (Word k = 0; k < 3; ++k) {
        const Word start = word_at(m_, position + 4 * k) + u(mul(30, int_at(m_, up + 4 * k)));
        set_line_start(line_at(m_, record), k, start);
        set_line_end(line_at(m_, record), k, start + u(mul(mul(int_at(m_, right + 4 * k), s(side)), s(distance))));
    }
    query();
    if (line_at(m_, record).hit_body().address == 0) {
        return 0;
    }
    const Word normal_xy = word_at(m_, record + 0x78);
    const Word normal_z = word_at(m_, record + 0x7c);
    const std::int32_t normal[3] = {sx(normal_xy), sx(normal_xy >> 16), sx(normal_z)};
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kProbeNormal + 4 * k, u(normal[k]));
    }
    const std::int32_t across = math_.dot_q12(kProbeNormal, up);
    if (across >= 0x400 || across <= -0x400) {
        return 0;
    }
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, position + k, component(sk.previous_position(), k / 4));
    }
    if (respond != 0) {
        Word bump = 0;
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, kProbeVelocity + k, component(sk.velocity(), k / 4));
        }
        if (sk.field_f6() != 0xc && vector_length(kProbeVelocity) > 6 && sk.field_2f64() == 0 && sk.field_2c74() == 0) {
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kProbeCopy + 4 * k, u(normal[k]));
            }
            if (math_.dot_q12(kProbeCopy, self + 0x30f4) > 0xb50) {
                thiscall(kLandPending, self);
                thiscall(kClearTrickFlags, self);
                thiscall(kRequestStart, self, {0xc, 0x167f});
                engine(kGrunt, {self});
                bump = 0xc8;
            }
        }
        thiscall(kCollisionResponse, self, {normal_xy, normal_z, bump});
    }
    for (Word k = 0; k < 3; ++k) {
        const Word at = word_at(m_, position + 4 * k);
        set_line_end(line_at(m_, record), k, at + u(mul(normal[k], s(distance))));
        set_line_start(line_at(m_, record), k, at + u(normal[k]));
    }
    query();
    if (line_at(m_, record).hit_body().address == 0) {
        set_component(sk.position(), 1, line_end(line_at(m_, record), 1));
        set_component(sk.position(), 2, line_end(line_at(m_, record), 2));
        sk.set_field_3200(1);
        set_word_at(m_, position, line_end(line_at(m_, record), 0));
        return 1;
    }
    if (global_byte(m_, kDebugFlag) != 0) {
        engine(kDebugLog, {kLogFormat, 0x16a7});
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, position + k, component(sk.vector_2e0c(), k / 4));
        }
        thiscall(kTurnPhase, self, {0x898, 1, 0});
    }
    return 0;
}

// 0x00497960 Skater_AirLandingOrientationRecovery (thiscall: the normal as two packed shorts and a short). Unless +0x2f64 or state 2: when the normal's dot with the up vector is under 0x7d0, the up
// vector's y over -0x7d0 and either the vertical speed (+0x50) over 0x249f0 or the dot under 0xc8, the skater bails (script 3) and the basis is turned over: the forward vector becomes the reverse of the
// up vector and the up vector the old forward vector; the basis is published, the velocity is the reverse of the new forward vector times 10 and its y is cleared.
void SkaterKit::air_landing_orientation_recovery(Word self, Word normal_xy, Word normal_z) {
    const auto sk = skater_at(m_, self);
    if (sk.field_2f64() != 0 || sk.physics_state() == 2) {
        return;
    }
    set_global_word(m_, kAirLandingNormal, u(sx(normal_xy)));
    set_word_at(m_, kAirLandingNormal + 4, u(sx(normal_xy >> 16)));
    set_word_at(m_, kAirLandingNormal + 8, u(sx(normal_z)));
    const std::int32_t along = math_.dot_q12(self + 0x310c, kAirLandingNormal);
    if (along >= 0x7d0 || static_cast<std::int32_t>(component(sk.vector_310c(), 1)) <= -0x7d0) {
        return;
    }
    if (static_cast<std::int32_t>(component(sk.velocity(), 1)) <= 0x249f0 && along >= 0xc8) {
        return;
    }
    thiscall(kBail, self, {3, 0});
    Word forward[3];
    for (Word k = 0; k < 3; ++k) {
        forward[k] = component(sk.vector_30f4(), k);
    }
    for (Word k = 0; k < 3; ++k) {
        set_component(sk.vector_30f4(), k, 0u - component(sk.vector_310c(), k));
    }
    for (Word k = 0; k < 3; ++k) {
        set_component(sk.vector_310c(), k, forward[k]);
    }
    publish_orientation_basis(self);
    for (Word k = 0; k < 3; ++k) {
        set_component(sk.velocity(), k, (0u - component(sk.vector_30f4(), k)) * 10u);
    }
    set_component(sk.velocity(), 1, 0);
}

}  // namespace opentony::runtime::skaterkit
