#include "frame_pacing.hpp"
#include "skater_views.hpp"

// Milestone 10g: the heading update (physics.cpp, 0x00492f20 Skater_UpdateHeading), written from the machine code. The turn accumulator mirror (+0x3148) picks the lean animation of a grounded
// skater: the frame word +0xf4 of the animation (6 or 7, by the side, with the board flag +0xd8 bit 1 swapping them) is moved toward a target the size of the turn, and the animation is restarted on
// it; with the second flag (+0x2dd8) the same is done with the animations 9 and 10 and, at their last frame, the completion check runs. With no turn the animation that is playing falls back to
// the idle or the landing pose. The frame stepper 0x00492ed0 (5, 3 or 1 frames toward the target) is arithmetic and runs here. Everything else is a service.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kRequestStart = 0x004903f0;        // Animation_RequestStart(animation, source line)
constexpr Word kRequestRange = 0x00490450;        // Animation_RequestRange(animation, from, to, source line)
constexpr Word kRequestFull = 0x00490480;         // Animation_RequestFull(animation, from, to, blend, source line)
constexpr Word kCompletionCheck = 0x00496280;     // Skater_AnimationCompletionCheck (fastcall)

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }

// 0x00492ed0 (cdecl): a frame moved toward its target by 5, 3 or 1 as the gap is over 12, over 3 or less
std::int32_t approach_frame(std::int32_t current, std::int32_t target) {
    if (current == target) {
        return target;
    }
    const std::int32_t gap = static_cast<std::int32_t>(static_cast<Word>(target) - static_cast<Word>(current));
    const std::int32_t sign = gap >= 0 ? 1 : -1;
    const Word magnitude = gap >= 0 ? static_cast<Word>(gap) : 0u - static_cast<Word>(gap);
    const std::int32_t step = static_cast<std::int32_t>(magnitude) > 12 ? 5 : (static_cast<std::int32_t>(magnitude) > 3 ? 3 : 1);
    return static_cast<std::int32_t>(static_cast<Word>(current) + static_cast<Word>(sign * step));
}

}  // namespace

// 0x00492f20 (thiscall)
void SkaterKit::update_heading(Word self) {
    std::int32_t limit = 0x2d000;
    if (static_cast<std::int8_t>(skater_at(m_, self).field_31a2()) > 0x1e || byte_at(m_, skater_at(m_, self).action_record_bank().address + 0xb0) != 0) {
        limit = 0x5a000;
    }
    const auto target_of = [&](std::int32_t numerator, std::int32_t cap) {
        const std::int32_t target = numerator / limit;
        return target > cap ? cap : target;
    };
    const auto frame = [&]() { return static_cast<std::int32_t>(static_cast<std::int16_t>(skater_at(m_, self).field_f4())); };
    const auto within = [](Word animation) { return animation == 0 || (animation > 5 && animation <= 10); };
    const auto restart = [&](Word animation, Word line, std::int32_t target) {
        const std::int32_t moved = framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }) != 0 ? approach_frame(frame(), target) : frame();      // the stepper moves once per 30 Hz frame
        skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(u(moved)));
        const Word word = u(static_cast<std::int32_t>(static_cast<std::int16_t>(skater_at(m_, self).field_f4())));
        engine(kRequestRange, {self, animation, word, word, line});
    };

    // ---- the plain lean animations (6 and 7) ----
    std::int32_t turn = skater_at(m_, self).turn_accumulator_mirror();
    if (turn != 0 && skater_at(m_, self).field_2dd8() == 0 && within(skater_at(m_, self).field_f6())) {
        const Word animation = skater_at(m_, self).field_f6();
        const bool flagged = (byte_at(m_, self + 0xd8) & 2) != 0;
        if (turn < 0) {
            const std::int32_t target = target_of(mul(turn, -22), 0x16);
            const Word wanted = flagged ? 7 : 6;
            if (animation != wanted) {
                skater_at(m_, self).set_field_f4(0);
            }
            restart(wanted, flagged ? 0x10d4 : 0x10eb, target);
        } else {
            const std::int32_t target = target_of(mul(turn, 22), 0x16);
            const Word wanted = flagged ? 6 : 7;
            if (animation != wanted) {
                skater_at(m_, self).set_field_f4(0);
            }
            if (flagged) {
                // (only this side counts the frame up by one before the stepper)
                const Word current = skater_at(m_, self).field_f4();
                if (static_cast<std::int16_t>(current) < target) {
                    skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(current + 1));
                }
            }
            restart(wanted, flagged ? 0x10de : 0x10f4, target);
        }
    }

    // ---- the animations 9 and 10 ----
    turn = skater_at(m_, self).turn_accumulator_mirror();
    if (turn != 0 && skater_at(m_, self).field_2dd8() != 0 && within(skater_at(m_, self).field_f6())) {
        const Word animation = skater_at(m_, self).field_f6();
        const bool flagged = (byte_at(m_, self + 0xd8) & 2) != 0;
        Word wanted;
        std::int32_t cap;
        std::int32_t numerator;
        Word line;
        if (flagged) {
            wanted = turn < 0 ? 10 : 9;
            cap = turn < 0 ? 0xc : 0xf;
            numerator = turn < 0 ? mul(turn, -12) : mul(turn, 15);
            line = turn < 0 ? 0x1118 : 0x1128;
        } else {
            wanted = turn < 0 ? 9 : 10;
            cap = turn < 0 ? 0xf : 0xc;
            numerator = turn < 0 ? mul(turn, -15) : mul(turn, 12);
            line = turn < 0 ? 0x113d : 0x114d;
        }
        const std::int32_t target = target_of(numerator, cap);
        // the animation that comes before the wanted one (7 before 10, 6 before 9) is taken over at the target frame
        if (animation == (wanted == 10 ? 7u : 6u)) {
            skater_at(m_, self).set_field_f6(static_cast<std::uint16_t>(wanted));
            skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(u(target)));
        }
        if (skater_at(m_, self).field_f6() != wanted) {
            skater_at(m_, self).set_field_f4(0);
        }
        restart(wanted, line, target);
        if (static_cast<std::int16_t>(skater_at(m_, self).field_f4()) == cap) {
            thiscall(kCompletionCheck, self);
        }
    }

    // ---- no turn: the lean animations return to rest ----
    if (skater_at(m_, self).turn_accumulator_mirror() == 0) {
        switch (skater_at(m_, self).field_f6()) {
        case 6:
        case 7:
            engine(kRequestStart, {self, 0, 0x115c});
            break;
        case 9:
        case 10:
            engine(kRequestFull, {self, 8, 0x13, 0x1a, 0x13, 0x1162});
            break;
        default:
            break;
        }
    }
}

}  // namespace opentony::runtime::skaterkit
