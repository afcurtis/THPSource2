#include "skater_views.hpp"

// Phase 4.15a: the basis helper and the rail exit of physics.cpp (0x00493e10, 0x00494090, 0x00494180).

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

}  // namespace

// 0x00465f60: a Q12 unit vector in place. The components are halved until every one is strictly inside +-0x6883, the GTE squares the 16-bit components, the integer root of the sum is the length, and
// each component becomes (component << 12) / length; a zero length gives the x axis.
void SkaterKit::normalize_fixed12(Word vector) {
    const auto outside = [this, vector] {
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t v = int_at(m_, vector + 4 * k);
            if (v >= 0x6883 || v <= -0x6883) {
                return true;
            }
        }
        return false;
    };
    while (outside()) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, vector + 4 * k, u(int_at(m_, vector + 4 * k) >> 1));
        }
    }
    set_global_half(m_, mathkit::gte::kIr, half_at(m_, vector));
    set_global_half(m_, mathkit::gte::kIr + 2, half_at(m_, vector + 4));
    set_global_half(m_, mathkit::gte::kIr + 4, half_at(m_, vector + 8));
    math_.sqr0();
    const std::int32_t length = integer_sqrt(s(global_word(m_, mathkit::gte::kMac + 4) + global_word(m_, mathkit::gte::kMac + 8) + global_word(m_, mathkit::gte::kMac)));
    if (length == 0) {
        set_word_at(m_, vector, 0x1000);
        set_word_at(m_, vector + 4, 0);
        set_word_at(m_, vector + 8, 0);
        return;
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, vector + 4 * k, u(s(word_at(m_, vector + 4 * k) << 12) / length));
    }
}

// 0x00493e10: the skater's up vector is set to straight up (0, -0x1000, 0); the right vector is the normalised cross product of the forward and up vectors, and the up vector is then made again as the
// cross product of the right and forward vectors; the orientation shorts follow
void SkaterKit::rebuild_sideways_basis(Word self) {
    constexpr Word kForward = skater::kBasis;
    constexpr Word kRight = skater::kBasis + 0xc;
    constexpr Word kUp = skater::kBasis + 0x18;
    set_word_at(m_, self + kUp, 0);
    set_word_at(m_, self + kUp + 4, 0xfffff000u);
    set_word_at(m_, self + kUp + 8, 0);
    const auto cross = [this, self](Word left, Word right) {
        // the GTE's outer product: the first vector on the diagonal of the rotation matrix, the second in IR1..3
        set_global_half(m_, mathkit::gte::kRotation, half_at(m_, self + left));
        set_global_half(m_, mathkit::gte::kRotation + 8, half_at(m_, self + left + 4));
        set_global_half(m_, mathkit::gte::kRotation + 16, half_at(m_, self + left + 8));
        set_global_half(m_, mathkit::gte::kIr, half_at(m_, self + right));
        set_global_half(m_, mathkit::gte::kIr + 2, half_at(m_, self + right + 4));
        set_global_half(m_, mathkit::gte::kIr + 4, half_at(m_, self + right + 8));
        math_.cross_sf12();
    };
    cross(kForward, kUp);
    set_word_at(m_, self + kRight, global_word(m_, mathkit::gte::kMac));
    set_word_at(m_, self + kRight + 4, global_word(m_, mathkit::gte::kMac + 4));
    set_word_at(m_, self + kRight + 8, global_word(m_, mathkit::gte::kMac + 8));
    normalize_fixed12(self + kRight);
    cross(kRight, kForward);
    set_word_at(m_, self + kUp, global_word(m_, mathkit::gte::kMac));
    set_word_at(m_, self + kUp + 4, global_word(m_, mathkit::gte::kMac + 4));
    set_word_at(m_, self + kUp + 8, global_word(m_, mathkit::gte::kMac + 8));
    publish_orientation_basis(self);
}

// 0x00494090: the sound of leaving a rail by its type (+0x2e8c): 0 and 2 always the same sound, 1 depends on a flag, 3 another; any other type is an assertion
void SkaterKit::grind_off_sound(Word self) {
    constexpr Word kMsgType = 0x00536d00;
    switch (skater_at(m_, self).field_2e8c()) {
    case 0:
        engine(kSoundPlay, {6, 0x2000, 0});
        return;
    case 1:
        engine(kSoundPlay, {skater_at(m_, self).field_2c0c() == 0 ? 0xffffffffu : 0x10u, 0x2000, 0});
        return;
    case 2:
        engine(kSoundPlay, {0xc6, 0x2000, 0});
        return;
    case 3:
        engine(kSoundPlay, {0x10, 0x2000, 0});
        return;
    default:
        check(false, kMsgType);
        return;
    }
}

// 0x00494180: the skater leaves a rail: the air state, the rail timers set (5, 15), the grind and rail flags cleared, the sound; a pending trick script is started, else the fall animation
void SkaterKit::leave_rail(Word self) {
    state_transition(self, 1);
    skater_at(m_, self).set_field_2ec0(5);
    skater_at(m_, self).set_field_2ec4(0xf);
    skater_at(m_, self).set_field_2e90(0);
    skater_at(m_, self).set_field_2e94(0);
    skater_at(m_, self).set_field_2ddc(1);
    skater_at(m_, self).set_field_2c68(0);
    skater_at(m_, self).set_field_306c(0);
    skater_at(m_, self).set_field_3068(0);
    grind_off_sound(self);
    const Word pending = skater_at(m_, self).field_29d0();
    skater_at(m_, self).set_field_29d4(0);
    if (pending != 0) {
        trick_script_start(self, pending);
        skater_at(m_, self).set_field_29d0(0);
        return;
    }
    animation_request_start(self, 0xe);
    skater_at(m_, self).set_field_29c8(0);
}

}  // namespace opentony::runtime::skaterkit
