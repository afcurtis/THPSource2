#include "skater_views.hpp"

// Milestone 10g: the floor check of the skater's frame (physics.cpp, 0x00490730 Skater_OuterFloorRecoveryCheck, cdecl: the skater, the position x y z and a message), written from the machine code. A
// line is cast down from the position through the zone (0x004624d0 initialises the query record, 0x00466090 runs it); where it hits, the distance below the skater is kept (+0x2f40) and the debug
// flag 0x005614c4 may recheck the floor and fall back to the last good position (+0x2e0c) with a turn; with no hit a shorter line is cast from 0x12c000 below, and the skater is lifted onto what it
// finds; with none again the last good position is tried (and, when that fails too, taken with the speed damped, +0x4c and +0x54, which are turned into a quarter of the old +0x54 backwards).
// The query records are stack locals in retail; here they are in the scratch area (the first at the one the frame's wall test uses, the debug rechecks' at a second). The zone-query results are read
// from the record where the service leaves them.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kInitQuery = 0x004624d0;           // M3D_InitCollisionQuery(record)
constexpr Word kZoneQuery = 0x00466090;           // M3D_QueryZoneCollision(record, 1)
constexpr Word kTurnPhase = 0x0049b500;           // Skater_ApplyTurnVelocityPhase(turn, 1, 0) (thiscall)
constexpr Word kLandOnSurface = 0x0046d2e0;       // (surface, body, x, y) (thiscall)
constexpr Word kDebugFlag = 0x005614c4;           // byte

constexpr Word kQuery = pcplatform::kScratchBase + 0xf50;
constexpr Word kDebugQuery = pcplatform::kScratchBase + 0x300;
constexpr Word kBody = 0x68;                      // the object the line met (0 for none)
constexpr Word kHitX = 0x6c;
constexpr Word kHitY = 0x70;
constexpr Word kHitZ = 0x74;
constexpr Word kSurface = 0x80;                   // a surface record whose word +0xc has the flags

inline std::int32_t abs32(std::int32_t v) {
    const Word sign = static_cast<Word>(v >> 31);
    return static_cast<std::int32_t>((static_cast<Word>(v) ^ sign) - sign);
}

}  // namespace

// 0x00490730 (cdecl): the arguments are the skater, the position and a message that the check does not use
void SkaterKit::outer_floor_recovery(Word self, Word x, Word y, Word z) {
    if (skater_at(m_, self).field_2e90() != 0) {
        return;
    }
    const auto line = [&](Word record, Word lx, Word ly, Word lz, Word end_y) {
        set_word_at(m_, record, lx);
        set_word_at(m_, record + 4, ly);
        set_word_at(m_, record + 8, lz);
        set_word_at(m_, record + 0xc, lx);
        set_word_at(m_, record + 0x10, end_y);
        set_word_at(m_, record + 0x14, lz);
    };
    const auto cast = [&](Word record) {
        engine(kInitQuery, {record});
        engine(kZoneQuery, {record, 1}, query_line(record));
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(kZoneQuery);      // (the fields of the hit: the host's answers)
            for (const Word offset : {kBody, kHitX, kHitY, kHitZ, kSurface}) {
                set_word_at(m_, record + offset, static_cast<Word>(s_.value(name)));
            }
        }
        return word_at(m_, record + kBody) != 0;
    };
    // the hit: the surface is handed to the landing code (the hit point in whole units)
    const auto land = [&]() {
        thiscall(kLandOnSurface, self, {global_word(m_, kQuery + kSurface), global_word(m_, kQuery + kBody), static_cast<Word>(global_int(m_, kQuery + kHitX) >> 12), static_cast<Word>(global_int(m_, kQuery + kHitZ) >> 12)});
    };

    line(kQuery, x, y, z, y + 0x1f40000);
    if (cast(kQuery)) {
        skater_at(m_, self).set_field_2f40(static_cast<Word>(global_word(m_, kQuery + kHitY) - word_at(m_, self + 0xc) - 0x1e000));
        if (global_byte(m_, kDebugFlag) != 0 && abs32(global_int(m_, kQuery + kHitY)) < 0x1770) {
            // the debug recheck: a line down to the position from 0x7d0000 above, and again from above what it met when that has the flag 0x40000
            const auto flagged = [&]() { return (word_at(m_, global_word(m_, kDebugQuery + kSurface) + 0xc) & 0x40000) != 0; };
            line(kDebugQuery, x, y - 0x7d0000, z, y);
            if (!cast(kDebugQuery)) {
                land();
                return;
            }
            if (flagged()) {
                set_global_word(m_, kDebugQuery + 4, global_word(m_, kDebugQuery + kHitY) + 0x6000);
                if (!cast(kDebugQuery) || flagged()) {
                    land();
                    return;
                }
            }
            for (Word k = 0; k < 12; k += 4) {
                set_word_at(m_, self + 0x8 + k, word_at(m_, self + 0x2e0c + k));
            }
            for (Word k = 0; k < 12; k += 4) {
                set_word_at(m_, self + 0xbc + k, word_at(m_, self + 0x2e0c + k));
            }
            thiscall(kTurnPhase, self, {0x4b0, 1, 0});
            return;
        }
        land();
        return;
    }

    // no hit: a shorter line from below
    const Word low = y - 0x12c000;
    line(kQuery, x, low, z, low + 0x190000);
    if (cast(kQuery)) {
        set_word_at(m_, self + 0x10, global_word(m_, kQuery + kHitZ));
        set_word_at(m_, self + 0x8, global_word(m_, kQuery + kHitX));
        set_word_at(m_, self + 0xc, global_word(m_, kQuery + kHitY) - 0x1e000);
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, self + 0xbc + k, word_at(m_, self + 0x8 + k));
        }
        return;
    }
    // none again: the last good position
    const Word good = self + 0x2e0c;
    const Word old_x = word_at(m_, self + 0x8);
    const Word old_bc = word_at(m_, self + 0xbc);
    const Word old_y = word_at(m_, self + 0xc);
    const Word old_z = word_at(m_, self + 0x10);
    set_word_at(m_, self + 0x8, word_at(m_, good));
    set_word_at(m_, self + 0xbc, word_at(m_, good));
    line(kQuery, word_at(m_, good), old_y, old_z, old_y + 0x190000);
    if (cast(kQuery)) {
        return;
    }
    const Word good_z = word_at(m_, self + 0x2e14);
    const Word here_y = word_at(m_, self + 0xc);
    set_word_at(m_, self + 0x8, old_x);
    set_word_at(m_, self + 0xbc, old_bc);
    set_word_at(m_, self + 0x10, good_z);
    set_word_at(m_, self + 0xc4, good_z);
    line(kQuery, old_x, here_y, good_z, here_y + 0x190000);
    if (cast(kQuery)) {
        return;
    }
    // the last good position, with the speed in +0x4c and +0x54 turned back
    set_word_at(m_, self + 0x8, word_at(m_, good));
    set_word_at(m_, self + 0xbc, word_at(m_, good));
    set_word_at(m_, self + 0xc, word_at(m_, good + 4));
    set_word_at(m_, self + 0xc0, word_at(m_, good + 4));
    const std::int32_t speed = int_at(m_, self + 0x54);
    const std::int32_t half = (speed - (speed >> 31)) >> 1;
    const std::int32_t turned = static_cast<std::int32_t>(0u - static_cast<Word>(half));
    set_word_at(m_, self + 0x4c, static_cast<Word>(turned));
    set_word_at(m_, self + 0x10, word_at(m_, good + 8));
    set_word_at(m_, self + 0xc4, word_at(m_, good + 8));
    set_word_at(m_, self + 0x54, static_cast<Word>((turned - (turned >> 31)) >> 1));
}

}  // namespace opentony::runtime::skaterkit
