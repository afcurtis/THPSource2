#include "skater_views.hpp"

// Phase 4.15a: the footplant entry, the special basis update and the manual of physics.cpp (0x004993f0, 0x004995d0, 0x00497bb0). Their stat arithmetic is 32-bit integer math with truncating
// divisions (the compiler's multiply-by-reciprocal sequences give exactly the truncating quotient).

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }

}  // namespace

// 0x004993f0: the skater plants a foot (a handplant-like stop). The gap "FOOTPLANT" is scored, the countdown (+0x2de8) is 15 less a twentieth of the sum of two stats, the state goes to 1, the speed
// becomes its own length against the forward direction (plus a push of 5 along it), and the vertical speed gets a lift worked out from the stats
void SkaterKit::enter_footplant_state(Word self) {
    engine(kApplyGapDefinition, {self, 0x00536e54, 100, 0, 0xffffffffu});
    {
        const std::int32_t a = profile_value(self, 2);
        const std::int32_t b = profile_value(self, 0);
        skater_at(m_, self).set_field_2de8(static_cast<std::int32_t>(u(0xf - (a + b) / 0x14)));
    }
    state_transition(self, 1);
    skater_at(m_, self).set_field_2ddc(1);
    const std::int32_t length = vector_length(self + trick::kMovement);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + trick::kMovement + 4 * k, u(mul(s(0 - word_at(m_, self + skater::kBasis + 4 * k)), length)));
    }
    set_word_at(m_, self + 0x4c, word_at(m_, self + 0x4c) + word_at(m_, self + skater::kBasis) * 5);
    set_word_at(m_, self + 0x54, word_at(m_, self + 0x54) + word_at(m_, self + skater::kBasis + 8) * 5);
    const std::int32_t first = profile_value(self, 2);
    const std::int32_t second = profile_value(self, 0);
    const std::int32_t divisor = 0xf - (first + second) / 0x14;
    const std::int32_t third = profile_value(self, 2);
    std::int32_t value = mul(third + 0x21c, 5000) / 10000;
    const std::int32_t fourth = profile_value(self, 2);
    const std::int32_t reduced = (mul(fourth, 3300) + 0x166e30) / 10000;
    value = mul(value - reduced, skater_at(m_, self).field_2de8());
    value = mul(value, 0x400) / 10;
    value /= divisor;
    const std::int32_t fifth = profile_value(self, 2);
    const std::int32_t lift = mul(mul(0 - (fifth + 0x21c), 5000) / 10000, 0x400) / 10;
    set_word_at(m_, self + 0x50, word_at(m_, self + 0x50) + u(mul(lift - value, 3)));
}

// 0x004995d0: a skater held in the special state is turned about the vertical axis: the rail timer is set, a forward y of 0xda8 or more is pushed on by 300, the forward vector is normalised and
// the up and right vectors are rebuilt from it through the GTE's outer product
void SkaterKit::update_special_basis_state(Word self) {
    constexpr Word kForward = skater::kBasis;
    constexpr Word kRight = skater::kBasis + 0xc;
    constexpr Word kUp = skater::kBasis + 0x18;
    skater_at(m_, self).set_field_2ec0(5);
    if (int_at(m_, self + kForward + 4) >= 0xda8) {
        set_word_at(m_, self + kForward + 4, word_at(m_, self + kForward + 4) + 300);
    }
    normalize_fixed12(self + kForward);
    const auto cross = [this, self](Word left, Word right) {
        set_global_half(m_, mathkit::gte::kRotation, half_at(m_, self + left));
        set_global_half(m_, mathkit::gte::kRotation + 8, half_at(m_, self + left + 4));
        set_global_half(m_, mathkit::gte::kRotation + 16, half_at(m_, self + left + 8));
        set_global_half(m_, mathkit::gte::kIr, half_at(m_, self + right));
        set_global_half(m_, mathkit::gte::kIr + 2, half_at(m_, self + right + 4));
        set_global_half(m_, mathkit::gte::kIr + 4, half_at(m_, self + right + 8));
        math_.cross_sf12();
    };
    cross(kRight, kForward);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + kUp + 4 * k, word_at(m_, mathkit::gte::kMac + 4 * k));
    }
    normalize_fixed12(self + kUp);
    cross(kForward, kUp);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + kRight + 4 * k, word_at(m_, mathkit::gte::kMac + 4 * k));
    }
    publish_orientation_basis(self);
}

// 0x00497bb0: the manual. Not on the ground, not bailing, not in the half-turn animation and not with the manual state set: a queued script (+0x29e0) is started and run once. In the manual state (+0x2dd4
// = 1 forward, else back) the balance (+0x2f18) is pushed towards a limit set by stat 9, then updated from the stat, the time (+0x2f20) and the balance bar's position (+0x2f14, kept in step with its sign)
void SkaterKit::maybe_manual(Word self, Word, Word reset) {
    if (skater_at(m_, self).field_2f64() != 0 || skater_at(m_, self).field_f6() == 0x82 || static_cast<Word>(skater_at(m_, self).field_2dd4()) != 0 || skater_at(m_, self).physics_state() == 2) {
        return;
    }
    const Word queued = skater_at(m_, self).field_29e0();
    if (queued != 0) {
        if (reset != 0) {
            skater_at(m_, self).set_field_306c(0);
            skater_at(m_, self).set_field_3068(0);
        }
        skater_at(m_, self).set_field_29e8(static_cast<Word>(reset));
        skater_at(m_, self).set_field_2dfc(0x14);
        trick_script_start(self, queued);
        // the script may put the skater into the manual state (+0x2dd4), which the rest of the function then acts on
        // (live, the interpreter writes the field itself; a replay scripts it as the call's answer)
        if (s_.live()) {
            engine(kScriptExecute, {self});
        } else {
            skater_at(m_, self).set_field_2dd4(static_cast<std::int32_t>(engine_answer(kScriptExecute, {self}, static_cast<Word>(skater_at(m_, self).field_2dd4()))));
        }
        skater_at(m_, self).set_field_29e0(0);
        skater_at(m_, self).set_field_2bd8(0);
    }
    const Word mode = static_cast<Word>(skater_at(m_, self).field_2dd4());
    if (mode == 0) {
        return;
    }
    skater_at(m_, self).set_field_2f10(0);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 0x2f04 + 4 * k, word_at(m_, self + skater::kPosition + 4 * k));
    }
    if (mode == 1) {
        const std::int32_t limit = mul(profile_value(self, 9), 3000) / 10000;
        if (static_cast<std::int32_t>(skater_at(m_, self).field_2f18()) < limit) {
            skater_at(m_, self).set_field_2f18(static_cast<Word>(u(mul(profile_value(self, 9), 3000) / 10000)));
        }
    } else {
        const std::int32_t limit = mul(profile_value(self, 9), -3000) / 10000;
        if (static_cast<std::int32_t>(skater_at(m_, self).field_2f18()) > limit) {
            skater_at(m_, self).set_field_2f18(static_cast<Word>(u(mul(profile_value(self, 9), -3000) / 10000)));
        }
    }
    const std::int32_t stat = profile_value(self, 9);
    std::int32_t value = mul(mul(200 - stat, 5000) / 10000, static_cast<std::int32_t>(skater_at(m_, self).field_2f18())) / 100;
    value = mul(value, (static_cast<std::int32_t>(skater_at(m_, self).field_2f20()) >> 12) + 0x3c);
    const std::int32_t divisor = (mul(profile_value(self, 9), 6000) + 200000) / 10000;
    const std::int32_t balance = value / divisor;
    const std::int32_t bar = static_cast<std::int32_t>(skater_at(m_, self).field_2f14());
    const std::int32_t pushed = mul(bar >= 0 ? 1 : -1, static_cast<std::int32_t>(skater_at(m_, self).field_2f24())) + bar;
    const std::int32_t magnitude = pushed < 0 ? mul(-1, pushed) : pushed;
    skater_at(m_, self).set_field_2f18(static_cast<Word>(u(balance)));
    skater_at(m_, self).set_field_2f14(static_cast<Word>(u(mul(magnitude, balance >= 0 ? 1 : -1))));
}

}  // namespace opentony::runtime::skaterkit
