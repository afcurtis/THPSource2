#include "frame_pacing.hpp"
#include "skater_views.hpp"

// Milestone 10g: the skater's push-off and brake (physics.cpp, 0x0049b010 Skater_UpdateAcceleration), written from the machine code. With the skater on the ground (state 0), not in the air or the
// level-end freeze, it watches the speed (the length of the velocity, times 64) and the velocity's component along the slope: a push-off (the jump button's profile byte +0x10, or the player slot's
// word in the table at 0x0056a3d8) raises the push limit +0x2dc8 from the stat 3 and plays the start animation; the animations 2, 3 and 0x5e (and every other when the skater is slow) push the
// response vector (+0x58) against the skater's right vector (+0x30f4) while the speed is under the push limit. The compiler's magic-number division is a plain truncating division by 280.
// The vector helpers (0x004f5f90, 0x004f53b0, 0x00490610) run here; the animation, the stat and the sound are services.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kRequestStart = 0x004903f0;        // Animation_RequestStart(animation, source line)
constexpr Word kRequestStartDefaultEnd = 0x00490420;   // (animation, from, source line)
constexpr Word kProfileValue = 0x0048f3a0;        // Skater_ProfileValue(selector) (thiscall)
constexpr Word kPlaySound = 0x004ad620;           // (sound, volume, 0) (cdecl)

constexpr Word kGameMode = 0x00533f38;
constexpr Word kSlotSwap = 0x0056a854;            // dword: the second player's table index flips
constexpr Word kSlotTable = 0x0056a3d8;           // dwords by the player slot
constexpr Word kVelocity = pcplatform::kScratchBase + 0xf00;   // the velocity without its component along the surface normal (a stack local in retail)

constexpr std::int32_t kSteepSlope = -1228;       // 0xfffffb34

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }
inline std::int32_t negate(std::int32_t a) { return static_cast<std::int32_t>(0u - static_cast<Word>(a)); }

}  // namespace

// 0x0049b010 (thiscall)
void SkaterKit::update_acceleration(Word self) {
    const Word slot = static_cast<Word>(skater_at(m_, self).player_index());
    const Word held = global_word(m_, kGameMode) != 7 ? word_at(m_, kSlotTable + 4 * slot) : word_at(m_, kSlotTable + 4 * (slot ^ global_word(m_, kSlotSwap)));
    if (static_cast<Word>(skater_at(m_, self).field_2dd4()) != 0 || skater_at(m_, self).field_2df8() != 0) {
        return;
    }
    if (skater_at(m_, self).field_107() != 0) {
        const Word animation = skater_at(m_, self).field_f6();
        if (animation == 1) {
            engine(kRequestStart, {self, 3, 0x2531});
            skater_at(m_, self).set_field_108(0x14000);
        } else if (animation == 3 || animation == 0x5e) {
            engine(kRequestStart, {self, 0, 0x2537});
        }
    }
    if (skater_at(m_, self).physics_state() != 0) {
        return;
    }
    if (static_cast<std::int32_t>(skater_at(m_, self).field_2f2c()) > 0) {
        skater_at(m_, self).set_field_2f2c(static_cast<Word>(skater_at(m_, self).field_2f2c() - framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); })));
    }
    const Word velocity = self + 0x4c;
    const auto speed = [&]() { return mul(integer_sqrt(math_.dot_q12(velocity, velocity)), 0x40); };
    const std::int32_t first_speed = speed();
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, kVelocity + k, word_at(m_, velocity + k));
    }
    remove_normal_component(kVelocity, word_at(m_, self + 0x3128), word_at(m_, self + 0x312c));
    const std::int32_t slope = math_.dot_q12(self + 0x3118, kVelocity);

    // the push limit from the stat 3 (and a bonus), the start animation and the hold-off of 20 frames; the order of the two stores around the call differs between the two places
    const auto start_push = [&](Word bonus, Word line, bool flag_first) {
        const Word stat = thiscall_value(kProfileValue, self, {3});
        const std::int32_t limit = mul(mul(s(stat + bonus), 45), 0x1000) / 280;
        skater_at(m_, self).set_field_2f2c(0x14);
        if (flag_first) {
            skater_at(m_, self).set_field_30a8(1);
        }
        skater_at(m_, self).set_field_2dc8(static_cast<Word>(u(limit)));
        engine(kRequestStartDefaultEnd, {self, 1, 0, line});
        skater_at(m_, self).set_field_108(0x14000);
        if (!flag_first) {
            skater_at(m_, self).set_field_30a8(1);
        }
    };
    const auto start_allowed = [&]() {
        const Word animation = skater_at(m_, self).field_f6();
        return animation == 0 || (animation > 4 && animation <= 7);
    };
    // the response vector set against the right vector: -(right * factor), or -right << 3 for the animation 0x5e
    const auto push_against = [&](std::int32_t factor) {
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, self + 0x58 + k, u(negate(mul(int_at(m_, self + 0x30f4 + k), factor))));
        }
    };
    const auto push_against_scaled = [&](std::int32_t factor) {
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, self + 0x58 + k, u(mul(negate(int_at(m_, self + 0x30f4 + k)), factor)));
        }
    };

    if (byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x10) != 0 || held != 0) {
        if (skater_at(m_, self).field_2e78() == 0 || skater_at(m_, self).field_2e7c() == 0) {
            if (skater_at(m_, self).field_2f2c() == 0) {
                if ((slope >= kSteepSlope && first_speed > static_cast<std::int32_t>(skater_at(m_, self).field_2dc8()) / 2) || skater_at(m_, self).field_2dd8() != 0 || skater_at(m_, self).field_f6() == 0x5e) {
                    skater_at(m_, self).set_field_2f2c(0x14);
                } else if (skater_at(m_, self).physics_state() == 0 && start_allowed()) {
                    start_push(0xaa, 0x2570, true);
                }
            }
            const std::int32_t fast = speed();
            const Word animation = skater_at(m_, self).field_f6();
            if (animation == 3 || animation == 2) {
                // the push-off sound once past frame 10, then the push while the frame is 11..15
                if (static_cast<std::int16_t>(skater_at(m_, self).field_f4()) > 10 && skater_at(m_, self).field_30a8() != 0) {
                    skater_at(m_, self).set_field_30ac(0x22);
                    engine(kPlaySound, {0x22, 0x2000, 0});
                    skater_at(m_, self).set_field_30a8(0);
                }
                const std::int16_t frame = static_cast<std::int16_t>(skater_at(m_, self).field_f4());
                if (frame >= 0xb && frame <= 0xf) {
                    const std::int32_t factor = byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x10) != 0 ? 8 : 4;
                    if (fast < static_cast<std::int32_t>(skater_at(m_, self).field_2dc8()) && static_cast<std::int32_t>(skater_at(m_, self).physics_state()) != 2) {
                        push_against(factor);
                    }
                }
            } else if (animation == 0x5e) {
                const std::int16_t frame = static_cast<std::int16_t>(skater_at(m_, self).field_f4());
                if (frame > 0xf && frame < 0x14 && fast < static_cast<std::int32_t>(skater_at(m_, self).field_2dc8()) && static_cast<std::int32_t>(skater_at(m_, self).physics_state()) != 2) {
                    push_against_scaled(8);
                }
            } else if (!(fast <= 0x4e20 && int_at(m_, self + 0x30f8) >= 0x1f4) && fast < static_cast<std::int32_t>(skater_at(m_, self).field_2dc8()) && static_cast<std::int32_t>(skater_at(m_, self).physics_state()) != 2) {
                push_against(1);
            }
        }
        if (held != 0) {
            return;
        }
    }

    // ---- the jump button with nothing held: the push-off from rest ----
    if (byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x10) != 0 && slope >= kSteepSlope && skater_at(m_, self).field_2dd8() == 0 && skater_at(m_, self).physics_state() == 0 && start_allowed()) {
        start_push(0xdc, 0x25e5, false);
    }
    // the animations 2 and 3 once more, with the speed of this moment
    const std::int32_t fast = speed();
    const Word animation = skater_at(m_, self).field_f6();
    if (animation != 3 && animation != 2) {
        return;
    }
    const std::int16_t frame = static_cast<std::int16_t>(skater_at(m_, self).field_f4());
    if (frame < 0xb || frame > 0xf) {
        return;
    }
    const std::int32_t factor = byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x10) != 0 ? 8 : 4;
    if (fast < static_cast<std::int32_t>(skater_at(m_, self).field_2dc8()) && static_cast<std::int32_t>(skater_at(m_, self).physics_state()) != 2) {
        push_against(factor);
    }
}

}  // namespace opentony::runtime::skaterkit
