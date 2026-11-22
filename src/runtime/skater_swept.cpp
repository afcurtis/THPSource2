#include "skater_views.hpp"

// Phase 4.15a: the contact of a skater with the world over the distance it moved this tick (physics.cpp, 0x00499710): the position advances by the velocity and half the acceleration, a line from the old
// position to the new one (stretched to a minimum length) is cast into the world; a surface hit puts the skater at the hit (and may land it); a second line, short and straight down the skater's own up vector,
// finds the ground under it: close enough, the skater stands on it, otherwise it takes off. The collision query, the position commit, the response routines and the trick layer are services.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

namespace gte = mathkit::gte;

constexpr Word kReadSurfaceFlags = 0x0048ea80;     // M3D_ReadSurfaceFlags(info): sets the collision result flags
constexpr Word kPositionWrite = 0x00496060;        // Skater_PositionWritePath(x, y, z) (thiscall)
constexpr Word kRegisterTrickObject = 0x0048f800;  // Trick_RegisterObject(body) (thiscall)
constexpr Word kRebuildRecovery = 0x0049d080;      // Skater_RebuildOrientationRecovery() (thiscall)
constexpr Word kSurfaceCorrection = 0x0049c060;    // Skater_ApplySurfaceResponseCorrection(4) (thiscall)
constexpr Word kTurnPhase = 0x0049b500;            // Skater_ApplyTurnVelocityPhase(turn, flag, 0) (thiscall)
constexpr Word kCollisionResponse = 0x0049bad0;    // Skater_ApplyCollisionResponse(normal xy, normal z, 200) (thiscall)
constexpr Word kGrunt = 0x0046e1d0;                // SFX_Grunt(skater)
constexpr Word kTrickLand = 0x0048f8f0;            // Trick_Land() (thiscall)
constexpr Word kTrickTakeOff = 0x0048f5f0;         // Trick_TakeOff() (thiscall)
constexpr Word kMsgOldPosition = 0x00536dc8;       // "mOldPos incorrect"
constexpr Word kStepScale = 0x00568804;            // dword: the time step the acceleration is scaled by
constexpr Word kZeroVector = 0x0056af28;           // three dwords the displacement is compared with
constexpr Word kFlagFace80 = 0x0056b7ac;           // the collision result flags M3D_ReadSurfaceFlags sets
constexpr Word kFlagSurface40 = 0x0056b768;
constexpr Word kFlagInverse24 = 0x0056b7b8;
constexpr Word kFlagInverse23 = 0x0056b7a8;
constexpr Word kGravityCheat = 0x0056b7ec;         // dword: a cheat that lowers the gravity
constexpr Word kNormalRepeat = 0x0056a93c;         // dword added to the repeat counter of a surface
constexpr Word kCheatHighGravity = 0xc;
constexpr Word kCheatLowGravity = 0xe;

constexpr Word kLocals = pcplatform::kScratchBase + 0x900;
constexpr Word kDisplacement = kLocals;             // three dwords
constexpr Word kGravity = kLocals + 0x10;           // three dwords: the pull the skater gets when it leaves the ground
constexpr Word kLanding = kLocals + 0x20;           // three dwords

// hit_distance at +0x40, the hit body at +0x68, the hit position from +0x6c, the surface normal (three shorts, read as dwords by the response routines) at +0x78, the face record at +0x80
constexpr Word kHitDistance = 0x40;
constexpr Word kHitBody = 0x68;
constexpr Word kHitPosition = 0x6c;
constexpr Word kNormal = 0x78;
constexpr Word kHitFace = 0x80;

inline std::int32_t sext16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }
inline Word mulw(std::int32_t a, std::int32_t b) { return static_cast<Word>(a) * static_cast<Word>(b); }

}  // namespace

// the zone query: the line in the record is handed to the query layer; its answer fills the record from +0x40 (twenty dwords, scripted in a replay), then the surface flags are read
void SkaterKit::zone_query(Word info) {
    engine(collision::kInitCollisionQuery, {info});
    engine(collision::kZoneCollision, {info, 1});
    if (s_.live()) {
        return;  // (the real query filled the record itself; the scripted words are replay-only)
    }
    const std::string name = pcplatform::engine_name(collision::kZoneCollision);
    for (Word k = 0; k < 20; ++k) {
        set_word_at(m_, info + 0x40 + 4 * k, static_cast<Word>(s_.value(name)));
    }
}

void SkaterKit::cast_line(Word info) {
    zone_query(info);
    engine(kReadSurfaceFlags, {info});
}

// the skater put on a surface it hit: the normal the surface has is its up vector, the forward vector loses its component along it, the right vector is the cross product, and the orientation
// follows
void SkaterKit::stand_on_surface(Word self, Word info) {
    const Word forward = self + skater::kBasis;
    remove_normal_component(forward, word_at(m_, info + kNormal), word_at(m_, info + kNormal + 4));
    normalize_fixed12(forward);
    set_word_at(m_, self + 0x310c, u(sext16(half_at(m_, info + kNormal))));
    set_word_at(m_, self + 0x3110, u(sext16(half_at(m_, info + kNormal + 2))));
    set_word_at(m_, self + 0x3114, u(sext16(half_at(m_, info + kNormal + 4))));
    set_global_half(m_, gte::kRotation, half_at(m_, forward));
    set_global_half(m_, gte::kRotation + 8, half_at(m_, self + 0x30f8));
    set_global_half(m_, gte::kRotation + 16, half_at(m_, self + 0x30fc));
    set_global_half(m_, gte::kIr, half_at(m_, self + 0x310c));
    set_global_half(m_, gte::kIr + 2, half_at(m_, self + 0x3110));
    set_global_half(m_, gte::kIr + 4, half_at(m_, self + 0x3114));
    math_.cross_sf12();
    set_word_at(m_, self + 0x3100, global_word(m_, gte::kMac));
    set_word_at(m_, self + 0x3104, global_word(m_, gte::kMac + 4));
    set_word_at(m_, self + 0x3108, global_word(m_, gte::kMac + 8));
    publish_orientation_basis(self);
    set_half_at(m_, self + 0x82, half_at(m_, info + kNormal + 2));
    set_word_at(m_, self + 0x3138, u(sext16(half_at(m_, info + kNormal + 2))));
    set_half_at(m_, self + 0x80, half_at(m_, info + kNormal));
    skater_at(m_, self).set_field_3130(0);
    set_half_at(m_, self + 0x84, half_at(m_, info + kNormal + 4));
    set_word_at(m_, self + 0x3134, u(sext16(half_at(m_, info + kNormal))));
    set_word_at(m_, self + 0x313c, u(sext16(half_at(m_, info + kNormal + 4))));
    remove_normal_component(self + trick::kMovement, word_at(m_, info + kNormal), word_at(m_, info + kNormal + 4));
}

// the surface normal remembered in the skater: the same one again adds to the repeat count, another one starts it afresh
void SkaterKit::remember_normal(Word self, Word info) {
    const Word x = half_at(m_, info + kNormal);
    const Word y = half_at(m_, info + kNormal + 2);
    const Word z = half_at(m_, info + kNormal + 4);
    if (x == half_at(m_, self + 0x80) && y == half_at(m_, self + 0x82) && z == half_at(m_, self + 0x84)) {
        skater_at(m_, self).set_field_3130(static_cast<Word>(skater_at(m_, self).field_3130() + global_word(m_, kNormalRepeat)));
    } else {
        skater_at(m_, self).set_field_3130(0);
        set_half_at(m_, self + 0x80, x);
        set_half_at(m_, self + 0x82, y);
        set_half_at(m_, self + 0x84, z);
    }
}

// 0x00499710 (fastcall)
void SkaterKit::update_swept_contact_state(Word self) {
    const Word info = collision::kInfoScratch;
    const Word position = self + skater::kPosition;
    const Word old_position = self + 0xbc;
    const Word velocity = self + trick::kMovement;
    const Word acceleration = self + 0x58;
    const bool swept = skater_at(m_, self).physics_state() == 2;
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kDisplacement + 4 * k, 0);
        set_word_at(m_, kLanding + 4 * k, 0);
    }
    std::int32_t pull = 9000;
    if ((engine_value(kGetCheat, {kCheatHighGravity}) & 0xff) != 0) {
        pull = s(mulw(pull, 150)) / 100;
    }
    if ((engine_value(kGetCheat, {kCheatLowGravity}) & 0xff) != 0) {
        pull = s(mulw(pull, 50)) / 100;
    }
    if (global_word(m_, kGravityCheat) != 0) {
        pull = s(mulw(pull, 50)) / 200;
    }
    set_global_word(m_, kGravity, 0);
    set_global_word(m_, kGravity + 4, u(pull));
    set_global_word(m_, kGravity + 8, 0);
    engine(kAdvancePointTimer, {self, 10});
    if (skater_at(m_, self).field_107() != 0 && (skater_at(m_, self).field_f6() == 0x24 || skater_at(m_, self).field_f6() == 0x23)) {
        skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(u(ping_pong(7, 9, 0xf))));
    }
    check(word_at(m_, position) == word_at(m_, old_position) && word_at(m_, position + 4) == word_at(m_, old_position + 4) && word_at(m_, position + 8) == word_at(m_, old_position + 8), kMsgOldPosition);
    // the move: the velocity over the time scale and half the acceleration over the step
    for (Word k = 0; k < 3; ++k) {
        const std::int32_t from_acceleration = (s(mulw(s(word_at(m_, acceleration + 4 * k)), s(global_word(m_, kStepScale)))) >> 8) / 2;
        const std::int32_t from_velocity = s(mulw(s(word_at(m_, velocity + 4 * k)), s(global_word(m_, globals::kAnimationTimeScale)))) >> 8;
        set_word_at(m_, position + 4 * k, word_at(m_, position + 4 * k) + u(from_velocity) + u(from_acceleration));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kDisplacement + 4 * k, word_at(m_, position + 4 * k) - word_at(m_, old_position + 4 * k));
    }
    const std::int32_t travelled = s(sqrt_truncated(u(math_.dot_q12(kDisplacement, kDisplacement))) << 6);
    if (travelled > s(mulw(profile_value(self, 3) + 0x186, 0x2d000)) / 0x118) {
        profile_value(self, 3);       // (the answer is not used)
    }
    std::int32_t turn = 0;
    if (skater_at(m_, self).field_2c88() == 0 && skater_at(m_, self).field_2e80() == 0) {
        engine(kSurfaceCorrection, {self, 4});
        turn = s(skater_at(m_, self).field_3124());
    }
    engine(kTurnPhase, {self, u(s(mulw(s(global_word(m_, globals::kAnimationTimeScale)), turn)) >> 8), swept ? 0u : 1u, 0});

    // the swept line: from the old position to the new one, stretched to 30 (70 while a flag is set) units at least
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, info + collision::line::kStart + 4 * k, word_at(m_, old_position + 4 * k));
        set_word_at(m_, info + collision::line::kEnd + 4 * k, word_at(m_, position + 4 * k));
        set_word_at(m_, kDisplacement + 4 * k, word_at(m_, position + 4 * k) - word_at(m_, old_position + 4 * k));
    }
    if (global_word(m_, kDisplacement) != global_word(m_, kZeroVector) || global_word(m_, kDisplacement + 4) != global_word(m_, kZeroVector + 4) || global_word(m_, kDisplacement + 8) != global_word(m_, kZeroVector + 8)) {
        std::int32_t length = s(sqrt_truncated(u(math_.dot_q12(kDisplacement, kDisplacement))) << 6);
        const std::int32_t reach = (skater_at(m_, self).field_2f64() != 0 ? 0x46 : 0) + 0x1e;
        if (length < reach * 0x1000) {
            if (length < 0x800) {
                length = 0x800;
            }
            for (Word k = 0; k < 3; ++k) {
                const std::int32_t along = s(mulw(reach, s(word_at(m_, kDisplacement + 4 * k)))) / length;
                set_word_at(m_, info + collision::line::kEnd + 4 * k, u(along) * 0x1000u + word_at(m_, old_position + 4 * k));
            }
        }
        cast_line(info);
        if (word_at(m_, info + kHitBody) != 0 && (global_word(m_, kFlagFace80) != 0 || global_word(m_, kFlagSurface40) == 0)) {
            engine(kPositionWrite, {self, word_at(m_, info + kHitPosition) + word_at(m_, self + 0x310c) * 30u, word_at(m_, info + kHitPosition + 4) + word_at(m_, self + 0x3110) * 30u,
                                    word_at(m_, info + kHitPosition + 8) + word_at(m_, self + 0x3114) * 30u});
            if (!swept) {
                engine(kRegisterTrickObject, {self, word_at(m_, info + kHitBody)});
            }
            remember_normal(self, info);
            engine(kRebuildRecovery, {self});
            if (global_word(m_, kFlagInverse24) != 0) {
                engine(kTrickLand, {self});
                state_transition(self, 0);
                stand_on_surface(self, info);
                animation_request_start(self, 5);
                skater_at(m_, self).set_field_2ddc(0);
            }
        }
    }
    fru_from_rotation_matrix(self);

    // the ground under the skater: a line from 70 units up its up vector to 58 below
    for (Word k = 0; k < 3; ++k) {
        const Word up = word_at(m_, self + 0x310c + 4 * k);
        const Word start = word_at(m_, position + 4 * k) + up * 0x46u;
        set_word_at(m_, info + collision::line::kStart + 4 * k, start);
        set_word_at(m_, info + collision::line::kEnd + 4 * k, start - (up << 7));
    }
    cast_line(info);
    if (word_at(m_, info + kHitBody) != 0 && global_word(m_, kFlagInverse23) != 0) {
        if (s(word_at(m_, info + kHitDistance)) <= 0x74) {
            skater_at(m_, self).set_field_2db4(static_cast<Word>((word_at(m_, word_at(m_, info + kHitFace) + 0xc) >> 16) & 0x20));
            engine(kPositionWrite, {self, word_at(m_, info + kHitPosition) + word_at(m_, self + 0x310c) * 30u, word_at(m_, info + kHitPosition + 4) + word_at(m_, self + 0x3110) * 30u,
                                    word_at(m_, info + kHitPosition + 8) + word_at(m_, self + 0x3114) * 30u});
            if (!swept) {
                engine(kRegisterTrickObject, {self, word_at(m_, info + kHitBody)});
            }
            remember_normal(self, info);
            engine(kRebuildRecovery, {self});
            if (global_word(m_, kFlagInverse24) != 0) {
                state_transition(self, 0);
                stand_on_surface(self, info);
            } else if (sext16(half_at(m_, info + kNormal + 2)) > 0xccd) {
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, position + 4 * k, word_at(m_, old_position + 4 * k));
                }
                state_transition(self, 1);
                skater_at(m_, self).set_field_2ddc(1);
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, acceleration + 4 * k, word_at(m_, acceleration + 4 * k) + word_at(m_, kGravity + 4 * k));
                }
                engine(kCollisionResponse, {self, word_at(m_, info + kNormal), word_at(m_, info + kNormal + 4), 200});
                engine(kGrunt, {self});
            }
        }
        // what the acceleration is given from the pull: the pull without its component along the normal
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kLanding + 4 * k, word_at(m_, kGravity + 4 * k));
        }
        remove_normal_component(kLanding, word_at(m_, info + kNormal), word_at(m_, info + kNormal + 4));
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, acceleration + 4 * k, word_at(m_, acceleration + 4 * k) + word_at(m_, kLanding + 4 * k));
            set_word_at(m_, self + 0x3118 + 4 * k, word_at(m_, kLanding + 4 * k));
        }
        set_word_at(m_, self + 0x3128, word_at(m_, info + kNormal));
        set_word_at(m_, self + 0x312c, word_at(m_, info + kNormal + 4));
        damp_to_ground_plane(velocity, info + kNormal, velocity);
        return;
    }
    // nothing under the skater: it takes off
    state_transition(self, 1);
    engine(kTrickTakeOff, {self});
    skater_at(m_, self).set_field_3068(0);
    skater_at(m_, self).set_field_306c(0);
    skater_at(m_, self).set_field_2c68(0);
    skater_at(m_, self).set_field_2ddc(1);
    skater_at(m_, self).set_field_2dd4(0);
    if (skater_at(m_, self).field_2f64() == 0) {
        animation_request_start(self, skater_at(m_, self).field_2dd8() == 0 ? 0x1b : 0x1a);
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, acceleration + 4 * k, word_at(m_, acceleration + 4 * k) + word_at(m_, kGravity + 4 * k));
    }
}

}  // namespace opentony::runtime::skaterkit
