#include "frame_pacing.hpp"
#include "skater_views.hpp"

// Phase 4.15a: small handlers of physics.cpp: the movement score timer, the animation completion sound, the trick flags, the take-off and landing sounds, the orientation angles, the blocked-skater
// reset and the nollie flip.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t sext8(Word v) { return static_cast<std::int8_t>(static_cast<std::uint8_t>(v)); }

}  // namespace

// 0x004ca8f0: the length of a Q12 vector in whole units: the GTE squares the components (shifted down by 12, as shorts) and the integer root of the sum is the answer
std::int32_t SkaterKit::vector_length(Word vector) {
    set_global_half(m_, mathkit::gte::kIr, u(int_at(m_, vector) >> 12));
    set_global_half(m_, mathkit::gte::kIr + 2, u(int_at(m_, vector + 4) >> 12));
    set_global_half(m_, mathkit::gte::kIr + 4, u(int_at(m_, vector + 8) >> 12));
    math_.sqr0();
    return integer_sqrt(s(global_word(m_, mathkit::gte::kMac + 4) + global_word(m_, mathkit::gte::kMac + 8) + global_word(m_, mathkit::gte::kMac)));
}

// 0x00494020: once the skater has moved more than 30 units from where the last point was counted, the point timer is advanced (by the value at +0x2f00) and the spot is taken again
void SkaterKit::advance_point_timer_when_moved(Word self) {
    constexpr Word kSpot = 0x2ee0;
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kLocalVector + 4 * k, word_at(m_, self + skater::kPosition + 4 * k) - word_at(m_, self + kSpot + 4 * k));
    }
    // 30 units is the way of a retail frame: a shorter frame moves less, and with the flag the distance is the frame's share of it (a grind earned half its points at 120 fps, and a slow one at 60)
    if (vector_length(kLocalVector) > framepacing::scaled_step(0x1e, [&] { return s(global_word(m_, globals::kAnimationTimeScale)); })) {
        engine(kAdvancePointTimer, {self, skater_at(m_, self).field_2f00()});
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + kSpot + 4 * k, word_at(m_, self + skater::kPosition + 4 * k));
        }
    }
}

// 0x00496280: the first time the skater is moving (the speed's two horizontal components, in whole units, beyond 1) a sound plays
void SkaterKit::animation_completion_check(Word self) {
    const auto still = [this, self](Word field) {
        const std::int32_t v = s(word_at(m_, self + field) & 0xfffff000u);
        return v <= 0x1000 && v >= -0x1000;
    };
    const bool quiet = still(0x4c) && still(0x54);
    if (skater_at(m_, self).field_30a4() == 0 && !quiet) {
        engine(kSoundPlay, {0xe8, 0x2000, 0});
        skater_at(m_, self).set_field_30a4(1);
    }
}

// 0x00496300: the running trick script's flags are cleared; a script that was not carried on ends and the score stack is rolled back once
void SkaterKit::clear_trick_flags(Word self) {
    skater_at(m_, self).set_field_29c8(0);
    skater_at(m_, self).set_field_29dc(0);
    skater_at(m_, self).set_field_2bd8(0);
    skater_at(m_, self).set_field_29e0(0);
    if (skater_at(m_, self).field_2c68() != 0 && skater_at(m_, self).field_2c6c() == 0) {
        skater_at(m_, self).set_field_2c68(0);
        skater_at(m_, self).set_field_2c70(0);
        skater_at(m_, self).set_field_2c74(0);
        if (static_cast<Word>(skater_at(m_, self).active_trick_stack_count()) != 0) {
            engine(kRollbackStack, {self + trick::kPanel});
            skater_at(m_, self).set_active_trick_stack_count(0);
        }
    }
}

// 0x0049a1b0
void SkaterKit::play_takeoff_sound(Word kind) { engine(kSoundPlay, {kind == 0xc ? 199u : 6u, 0x2000, 0}); }

// 0x0049a1e0: the sound of a landing by kind: 1, 3, 5, 8 one sound, 2 and 14 another, 12 a third, 6 none, anything else the default
void SkaterKit::play_landing_sound(Word kind) {
    switch (kind) {
    case 1:
    case 3:
    case 5:
    case 8:
        engine(kSoundPlay, {0xca, 0x2000, 0});
        return;
    case 2:
    case 0xe:
        engine(kSoundPlay, {200, 0x2000, 0});
        return;
    case 6:
        return;
    case 0xc:
        engine(kSoundPlay, {0xcb, 0x2000, 0});
        return;
    default:
        engine(kSoundPlay, {5, 0x2000, 0});
        return;
    }
}

// 0x0049c8d0: the angles (three shorts at +0x14) of the orientation: the converter wants the up axis the other way round, so its three shorts are negated for the call and put back
void SkaterKit::orientation_angles(Word self) {
    constexpr Word kUp[3] = {0x2e5a, 0x2e60, 0x2e66};
    for (const Word field : kUp) {
        set_half_at(m_, self + field, 0 - half_at(m_, self + field));
    }
    const std::string service = pcplatform::engine_name(kAnglesFromMatrix);
    s_.note(service, {pcplatform::kScratchBase + 0x700, self + skater::kOrientation});
    if (!s_.live()) {
        // (the host's answers; live, the real converter wrote the three shorts into the buffer itself)
        std::vector<std::uint8_t> bytes;
        for (int k = 0; k < 2; ++k) {
            const auto word = static_cast<Word>(s_.value(service));
            for (unsigned b = 0; b < 4; ++b) {
                bytes.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
            }
        }
        bytes.resize(6);
        m_.write(pcplatform::kScratchBase + 0x700, bytes);
    }
    for (const Word field : kUp) {
        set_half_at(m_, self + field, 0 - half_at(m_, self + field));
    }
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, self + 0x14 + 2 * k, half_at(m_, pcplatform::kScratchBase + 0x700 + 2 * k));
    }
}

// 0x0049d8a0: while the skater is blocked its sideways speed and two counters are cleared, it cannot sink, and the horizontal speed is damped by the animation clock; the playing speed is raised by
// a third of 0x5555 (a third of a part in four) for every distinct pad press in the last 20 ticks
void SkaterKit::blocked_physics_reset(Word self) {
    if (skater_at(m_, self).field_2f64() == 0) {
        return;
    }
    set_word_at(m_, self + 0x58, 0);
    set_word_at(m_, self + 0x60, 0);
    skater_at(m_, self).set_field_2de0(0);
    skater_at(m_, self).set_field_2ddc(0);
    if ((skater_at(m_, self).field_2f60() == 0 || skater_at(m_, self).physics_state() == 2) && int_at(m_, self + 0x50) < 0) {
        set_word_at(m_, self + 0x50, 0);
    }
    const std::int32_t divisor = skater_at(m_, self).field_2c10();
    if (divisor != 0) {
        for (const Word field : {0x4cu, 0x50u, 0x54u}) {
            const std::int32_t v = int_at(m_, self + field);
            set_word_at(m_, self + field, u(v - (s(u(v / divisor) * global_word(m_, globals::kAnimationTimeScale)) >> 8)));
        }
    }
    const Word head = skater_at(m_, self).field_2b14();
    std::int32_t at = s(head) - 1;
    if (at < 0) {
        at = 0x1f;
    }
    const Word limit = global_word(m_, globals::kAnimationClock) - 0x14;
    Word previous = 0;
    Word count = 0;
    while (word_at(m_, self + trick::kPadHistory + 4 + 8 * static_cast<Word>(at)) > limit && static_cast<Word>(at) != head) {
        const Word time = word_at(m_, self + trick::kPadHistory + 4 + 8 * static_cast<Word>(at));
        if (time != previous && byte_at(m_, self + trick::kPadHistory + 8 * static_cast<Word>(at)) != 0) {
            previous = time;
            ++count;
        }
        --at;
        if (at < 0) {
            at = 0x1f;
        }
    }
    skater_at(m_, self).set_field_108(static_cast<Word>(u(s(count * 0x5555u) / 3 + 0x10000)));
}

// 0x0049e570: the nollie flip. Standing still on the ground (state 0 or 7) with no trick, bail or half-turn animation, a press registered in the pad record (the byte at +0x51 of the structure at +0x2ccc)
// toggles the flip flag and starts the matching animation (0xa2, or 0xa3 with the other foot); undoing it swaps the animation frame with a byte pair. The byte is always cleared.
void SkaterKit::update_nollie_flip(Word self) {
    const Word state = skater_at(m_, self).physics_state();
    if (skater_at(m_, self).field_30c0() == state && (state == 0 || state == 7)) {
        skater_at(m_, self).set_field_31f8(0);
        skater_at(m_, self).set_field_31fc(0);
    }
    if ((state == 0 || state == 7) && skater_at(m_, self).field_2f64() == 0 && static_cast<Word>(skater_at(m_, self).field_2dd4()) == 0 && skater_at(m_, self).field_f6() != 0x82) {
        if (byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x51) == 0) {
            set_byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x51, 0);
            return;
        }
        skater_at(m_, self).set_field_31f4(static_cast<Word>(skater_at(m_, self).field_31f4() ^ 1));
        animation_request_start(self, skater_at(m_, self).field_2dd8() != 0 ? 0xa3u : 0xa2u);
        if (skater_at(m_, self).field_31f4() == 0) {
            const std::uint16_t frame = skater_at(m_, self).field_f4();
            const std::uint8_t other = skater_at(m_, self).field_100();
            skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(static_cast<std::uint16_t>(sext8(skater_at(m_, self).field_101()))));
            skater_at(m_, self).set_field_101(static_cast<std::uint8_t>(frame & 0xff));
            skater_at(m_, self).set_field_100(static_cast<std::uint8_t>(0 - other));
            skater_at(m_, self).set_field_31f0(0);
        }
        set_byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x51, 0);
        return;
    }
    skater_at(m_, self).set_field_31f4(0);
    skater_at(m_, self).set_field_31f0(0);
    set_byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x51, 0);
}

}  // namespace opentony::runtime::skaterkit
