#include "skater_views.hpp"

// Milestone 10g: two small steps of the ground physics, written from the machine code (physics.cpp). 0x0049dad0 Skater_GroundPreparation: with the skater in the brake phase 0 or 4, not in the
// air or locked, a press of the profile's byte +0x71 (the ollie button's second state) is taken: the byte is cleared, one of two sounds plays, the animation 0x82 starts and the skater is marked as
// in a spin-up (+0x2dd0 = 1). 0x0049d9c0 Skater_RunGroundedBounceRecovery: the position is tested for a bounce on both sides with the bounce probe (0x004957c0, the sides -1 and 1 and the distance
// 10, or 0x46 in the physics lock); when both sides hit, the position put back where it was (or, with the debug flag 0x005614c4 on, to the one the frame began with, after a log line) and the
// turn phase runs with 0x898. Everything else is a service.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kRandom = 0x004c9340;              // Rnd(n) -> 0..n-1 (cdecl)
constexpr Word kPlaySound = 0x004ad620;           // (sound, volume, 0) (cdecl)
constexpr Word kRequestStart = 0x004903f0;        // Animation_RequestStart(animation, source line)
constexpr Word kBounceProbe = 0x004957c0;         // Skater_GroundedBounceProbe(side, distance, 1) -> 0 or 1 (thiscall)
constexpr Word kDebugLog = seam::hal::printf_log;   // (format, line) (cdecl)
constexpr Word kTurnPhase = 0x0049b500;           // Skater_ApplyTurnVelocityPhase(turn, 1, 0) (thiscall)
constexpr Word kDebugFlag = 0x005614c4;           // byte
constexpr Word kLogFormat = 0x00536dbc;

}  // namespace

// 0x0049dad0 (thiscall)
void SkaterKit::ground_preparation(Word self) {
    const Word phase = skater_at(m_, self).field_2df8();
    if (phase != 0 && phase != 4) {
        return;
    }
    if (skater_at(m_, self).field_2f64() != 0 || static_cast<Word>(skater_at(m_, self).field_2dd4()) != 0 || skater_at(m_, self).field_f6() == 0x82) {
        return;
    }
    const Word profile = skater_at(m_, self).action_record_bank().address;
    if (byte_at(m_, profile + 0x71) == 0) {
        return;
    }
    set_byte_at(m_, profile + 0x71, 0);
    const Word roll = engine_value(kRandom, {0x80});
    engine(kPlaySound, {static_cast<std::int32_t>(roll) < 0x40 ? 0xafu : 0xaeu, 0x2000, 0});
    engine(kRequestStart, {self, 0x82, 0x2b05});
    skater_at(m_, self).set_field_2dd0(1);
    skater_at(m_, self).set_field_2dd4(0);
    skater_at(m_, self).set_field_31f0(0);
}

// 0x0049d9c0 (thiscall)
void SkaterKit::run_grounded_bounce_recovery(Word self) {
    const Word position = self + 0x8;
    const Word saved[3] = {word_at(m_, position), word_at(m_, position + 4), word_at(m_, position + 8)};
    const Word distance = skater_at(m_, self).field_2f64() != 0 ? 0x46 : 0x0a;
    const auto probe = [&](Word side) { return thiscall_value(kBounceProbe, self, {side, distance, 1}) != 0; };
    const auto recover = [&](Word line) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, position + 4 * k, saved[k]);
        }
        if (global_byte(m_, kDebugFlag) == 0) {
            return;
        }
        engine(kDebugLog, {kLogFormat, line});
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, position + k, word_at(m_, self + 0x2e0c + k));
        }
        thiscall(kTurnPhase, self, {0x898, 1, 0});
    };
    if (probe(0xffffffffu)) {
        if (probe(1)) {
            recover(0x2ab6);
        }
        return;
    }
    if (probe(1) && probe(0xffffffffu)) {
        recover(0x2ac5);
    }
}

}  // namespace opentony::runtime::skaterkit
