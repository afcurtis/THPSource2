#include "skater_views.hpp"

// Milestone 10g: the skater's brake mode (physics.cpp, 0x0049df00 Skater_UpdateBrakeMode), written from the machine code. While the skater is on the ground (state 0 or 7) with no trick script, rail
// or other lock, the brake input (the profile's byte +0xb0 with neither side button, or the stick pulled back with the other axis centred) puts it in state 7 or back to 0 and, above a speed that falls
// as the board's tilt word +0x312a grows (floor 0xa000), wears the velocity down (by the stick's amount, or by 1/16 a frame); the brake phase +0x2df8 (0 rest, 1 and 5 the two pushes of a stop,
// 3 and 6 the turn of the board, 4 the wait for the button to go up) plays the stop animations and the sounds. The compiler's magic-number division is a plain truncating division by 208. The
// state change, the animation, the sound, the dust and the random roll are services; the dot product and root run here.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kStateTransition = 0x004900b0;     // CBruce__StateTransition(state, source line) (thiscall)
constexpr Word kRequestStart = 0x004903f0;        // Animation_RequestStart(animation, source line)
constexpr Word kRequestRange = 0x00490450;        // Animation_RequestRange(animation, from, to, source line)
constexpr Word kRequestFull = 0x00490480;         // Animation_RequestFull(animation, from, to, blend, source line)
constexpr Word kPlaySound = 0x004ad620;           // (sound, volume, 0) (cdecl)
constexpr Word kRandom = 0x004c9340;              // Rnd(n) -> 0..n-1 (cdecl)
constexpr Word kDustReset = 0x00431260;           // Dust_Reset(sound slot, 2) (thiscall on the dust at +0x2c14)

constexpr Word kTimeScale = 0x0056865c;
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
const std::string kPhysicsFile = "H:\\TonyHawk\\Pc2\\physics.cpp";   // 0x00536a30
constexpr Word kPhaseText = 0x00536ee8;           // "Unknown brake phase %d"

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }
inline std::int32_t minus(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) - static_cast<Word>(b)); }

}  // namespace

// 0x0049df00 (thiscall)
void SkaterKit::update_brake_mode(Word self) {
    const auto reset = [&]() {
        if (skater_at(m_, self).physics_state() == 7) {
            thiscall(kStateTransition, self, {0, 0x2c0f});
        }
        skater_at(m_, self).set_field_2df8(0);
    };
    if (skater_at(m_, self).field_29c8() != 0 || skater_at(m_, self).field_2dd0() != 0 || static_cast<Word>(skater_at(m_, self).field_2dd4()) != 0 || skater_at(m_, self).field_2f64() != 0) {
        reset();
        return;
    }
    const Word state = skater_at(m_, self).physics_state();
    if (state != 7 && state != 0) {
        reset();
        return;
    }
    // the brake input
    const Word profile = skater_at(m_, self).action_record_bank().address;
    bool braking = false;
    if (byte_at(m_, profile + 0xb0) != 0 && byte_at(m_, profile + 0x80) == 0 && byte_at(m_, profile + 0x90) == 0) {
        braking = true;
    } else if (static_cast<std::int8_t>(skater_at(m_, self).field_31a2()) > 0 && skater_at(m_, self).field_31a1() == 0) {
        braking = true;
    }
    if (state == 7 && !braking) {
        thiscall(kStateTransition, self, {0, 0x2c21});
    }
    // the speed above which the velocity is worn down
    std::int32_t tilt = -static_cast<std::int32_t>(static_cast<std::int16_t>(half_at(m_, self + 0x312a)));
    tilt = static_cast<std::int32_t>(static_cast<Word>(tilt) << 12) >> 12;
    if (tilt < 0x300) {
        tilt = 0x300;
    }
    std::int32_t limit = mul(mul(0x1000 - tilt, 3), 0x800) / 208;
    if (limit < 0xa000) {
        limit = 0xa000;
    }
    const Word velocity = self + 0x4c;
    const std::int32_t speed = mul(integer_sqrt(math_.dot_q12(velocity, velocity)), 0x40);
    if (skater_at(m_, self).physics_state() == 0 && braking) {
        if (speed > limit) {
            const std::int8_t stick = static_cast<std::int8_t>(skater_at(m_, self).field_31a2());
            if (stick > 0) {
                const std::int32_t amount = static_cast<std::int32_t>(stick) >> 4;
                for (Word k = 0; k < 12; k += 4) {
                    const std::int32_t value = int_at(m_, velocity + k);
                    std::int32_t part = mul(value, amount);
                    part += static_cast<std::int32_t>(static_cast<Word>(part >> 31) & 0x7f);
                    part >>= 7;
                    part = mul(part, global_int(m_, kTimeScale)) >> 8;
                    set_word_at(m_, velocity + k, static_cast<Word>(minus(value, part)));
                }
            } else {
                for (Word k = 0; k < 12; k += 4) {
                    const std::int32_t value = int_at(m_, velocity + k);
                    set_word_at(m_, velocity + k, static_cast<Word>(minus(value, mul(value >> 4, global_int(m_, kTimeScale)) >> 8)));
                }
            }
        } else if (limit <= 0xa000) {
            thiscall(kStateTransition, self, {7, 0x2c56});
            for (Word k = 0; k < 12; k += 4) {
                set_word_at(m_, velocity + k, 0);
            }
        }
    }

    const auto frame_word = [&]() { return static_cast<std::int32_t>(static_cast<std::int16_t>(skater_at(m_, self).field_f4())); };
    const auto not_standing = [&]() { const Word animation = skater_at(m_, self).field_f6(); return animation != 0x5d && animation != 0x5f; };
    // the two sides of the board swap (the stance bytes and the word at +0x114) at a stop
    const auto swap_stance = [&](Word phase) {
        const Word wide = static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int16_t>(skater_at(m_, self).field_114())));
        const Word narrow = static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int8_t>(skater_at(m_, self).field_101())));
        skater_at(m_, self).set_field_107(0);
        skater_at(m_, self).set_field_100(0xff);
        skater_at(m_, self).set_field_114(static_cast<std::uint16_t>(narrow));
        skater_at(m_, self).set_field_101(static_cast<std::uint8_t>(wide));
        skater_at(m_, self).set_field_2df8(static_cast<Word>(phase));
    };
    const auto rest = [&]() { skater_at(m_, self).set_field_2f2c(2); };

    switch (skater_at(m_, self).field_2df8()) {
    case 0:
        if (!braking) {
            return;
        }
        if (speed <= limit) {
            skater_at(m_, self).set_field_2df8(0);
            rest();
            return;
        }
        if (skater_at(m_, self).field_2dd8() == 0 && speed < 0x30000) {
            if (not_standing()) {
                engine(kRequestStart, {self, 0x31, 0x2c6a});
            }
            skater_at(m_, self).set_field_2df8(5);
        } else {
            if (not_standing()) {
                engine(kRequestStart, {self, 0x83, 0x2c70});
            }
            skater_at(m_, self).set_field_2df8(1);
        }
        rest();
        return;
    case 1:
        if (braking && speed >= static_cast<std::int32_t>((static_cast<Word>(frame_word()) << 12) + static_cast<Word>(limit))) {
            rest();
            return;
        }
        swap_stance(3);
        if (braking) {
            const Word roll = engine_value(kRandom, {0x80});
            engine(kPlaySound, {static_cast<std::int32_t>(roll) < 0x40 ? 0xb1u : 0xb0u, 0x2000, 0});
            thiscall(kDustReset, self + 0x2c14, {skater_at(m_, self).field_30b0(), 2});
        }
        rest();
        return;
    case 2:
        return;
    case 3:
        if (skater_at(m_, self).field_107() != 0) {
            if (skater_at(m_, self).field_2dd8() == 0) {
                engine(kRequestRange, {self, 8, 0x13, 0, 0x2cb8});
            } else {
                engine(kRequestFull, {self, 8, 0x13, 0x1a, 0x13, 0x2cba});
            }
            skater_at(m_, self).set_field_2df8(4);
        }
        rest();
        return;
    case 4:
        if (!braking) {
            skater_at(m_, self).set_field_2df8(0);
        }
        return;
    case 5:
        if (braking && speed >= static_cast<std::int32_t>((static_cast<Word>(frame_word()) << 12) + static_cast<Word>(limit))) {
            rest();
            return;
        }
        swap_stance(6);
        if (braking && frame_word() > 4) {
            engine(kPlaySound, {2, 0x2000, 0});
        }
        rest();
        return;
    case 6:
        if (skater_at(m_, self).field_107() != 0) {
            engine(kRequestStart, {self, 0, 0x2cc4});
            skater_at(m_, self).set_field_2df8(4);
        }
        rest();
        return;
    default:
        set_global_word(m_, kAssertionLine, 0x2cd1);
        set_string(kAssertionFile, kPhysicsFile);
        check(false, kPhaseText);
        return;
    }
}

}  // namespace opentony::runtime::skaterkit
