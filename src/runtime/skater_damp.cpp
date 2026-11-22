#include "skater_views.hpp"

// Phase 4.15a: the speed damping, the collision of two skaters and the trigger check of physics.cpp (0x0049d2f0, 0x0049d480, 0x0049d740).

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }

}  // namespace

// 0x004c9470: the vector divided by its length (the GTE squares the whole-unit components, the integer root of the sum is the length); a zero length gives the zero vector
void SkaterKit::unit_vector(Word out, Word in) {
    const Word x = word_at(m_, in);
    const Word y = word_at(m_, in + 4);
    const Word z = word_at(m_, in + 8);
    set_global_half(m_, mathkit::gte::kIr, u(s(x) >> 12));
    set_global_half(m_, mathkit::gte::kIr + 2, u(s(y) >> 12));
    set_global_half(m_, mathkit::gte::kIr + 4, u(s(z) >> 12));
    math_.sqr0();
    const std::int32_t length = integer_sqrt(s(global_word(m_, mathkit::gte::kMac + 4) + global_word(m_, mathkit::gte::kMac + 8) + global_word(m_, mathkit::gte::kMac)));
    if (length == 0) {
        set_word_at(m_, out, 0);
        set_word_at(m_, out + 4, 0);
        set_word_at(m_, out + 8, 0);
        return;
    }
    set_word_at(m_, out, u(s(word_at(m_, in)) / length));
    set_word_at(m_, out + 4, u(s(word_at(m_, in + 4)) / length));
    set_word_at(m_, out + 8, u(s(word_at(m_, in + 8)) / length));
}

// 0x004ca980: the vector divided by its length when that is positive (the retail z is the already divided y divided once more)
void SkaterKit::vector_normalize(Word vector) {
    const std::int32_t length = vector_length(vector);
    if (length > 0) {
        set_word_at(m_, vector, u(int_at(m_, vector) / length));
        const std::int32_t y = int_at(m_, vector + 4) / length;
        set_word_at(m_, vector + 4, u(y));
        set_word_at(m_, vector + 8, u(y / length));
    }
}

// 0x0049d480: the speed (the length of the velocity at +0x4c, times 64) above a limit set by stat 3 is scaled down to it; above a second limit it loses a bit more each tick; and with auto-kick off the
// speed bleeds away (a thirty-second under 0x10000, a quarter under 0x2000 and then nothing under 16)
void SkaterKit::damp_response_vector(Word self) {
    constexpr Word kVelocity = trick::kMovement;
    const auto limit = [this, self](std::int32_t base) { return mul(profile_value(self, 3) + base, 0x2d000) / 0x118; };
    std::int32_t speed = integer_sqrt(math_.dot_q12(self + kVelocity, self + kVelocity)) << 6;
    if (speed > limit(500) && (static_cast<Word>(speed) & 0xfffff000u) != 0) {
        const std::int32_t divisor = speed >> 12;
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t factor = limit(500) >> 12;
            set_word_at(m_, self + kVelocity + 4 * k, u(mul(factor, int_at(m_, self + kVelocity + 4 * k)) / divisor));
        }
        speed = integer_sqrt(math_.dot_q12(self + kVelocity, self + kVelocity)) << 6;
    }
    if (speed > limit(0x186)) {
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t drag = math_.mul_q12(100, int_at(m_, self + kVelocity + 4 * k));
            set_word_at(m_, self + kVelocity + 4 * k, u(int_at(m_, self + kVelocity + 4 * k) - (mul(drag, global_int(m_, globals::kAnimationTimeScale)) >> 8)));
        }
    }
    Word player = static_cast<Word>(skater_at(m_, self).player_index());
    if (global_word(m_, globals::kGameMode) == 7) {
        player ^= global_word(m_, globals::kSecondPlayerMask);
    }
    if (word_at(m_, globals::kAutoKickSettings + 4 * player) != 0) {
        return;
    }
    const std::int32_t magnitude = speed < 0 ? -speed : speed;
    if (magnitude < 0x10000) {
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t v = int_at(m_, self + kVelocity + 4 * k);
            set_word_at(m_, self + kVelocity + 4 * k, u(v - ((v + ((v >> 31) & 0x1f)) >> 5)));
        }
    }
    if (magnitude < 0x2000) {
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t v = int_at(m_, self + kVelocity + 4 * k);
            const std::int32_t smaller = v - ((v + ((v >> 31) & 3)) >> 2);
            set_word_at(m_, self + kVelocity + 4 * k, u((smaller < 0 ? -smaller : smaller) < 0x10 ? 0 : smaller));
        }
    }
}

// 0x0049d2f0: with the collision between two skaters on, two skaters moving less than 60 units apart: when this skater runs into the other the one that is slower (or the one not moving at the other)
// bails; not when either is already bailing
void SkaterKit::collide_with_other_player(Word self) {
    constexpr Word kMsgOther = 0x00536ec8;
    if (global_word(m_, globals::kTwoPlayers) == 0) {
        return;
    }
    check(skater_at(m_, self).peer_skater().address != 0, kMsgOther);
    if (skater_at(m_, self).field_2f64() != 0) {
        return;
    }
    const Word other = skater_at(m_, self).peer_skater().address;
    if (other == 0 || word_at(m_, other + 0x2f64) != 0) {
        return;
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kLocalVector + 4 * k, word_at(m_, other + skater::kPosition + 4 * k) - word_at(m_, self + skater::kPosition + 4 * k));
    }
    if (vector_length(kLocalVector) >= 0x3c) {
        return;
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kLocalVector + 4 * k, word_at(m_, other + skater::kPosition + 4 * k) - word_at(m_, self + 0xbc + 4 * k));
    }
    vector_normalize(kLocalVector);
    const std::int32_t mine = math_.dot_q12(self + trick::kMovement, kLocalVector);
    const std::int32_t theirs = math_.dot_q12(other + trick::kMovement, kLocalVector);
    if (mine > 0) {
        if (theirs > 0) {
            bail(other, 0, 0);
            return;
        }
        const std::int32_t other_speed = vector_length(other + trick::kMovement);
        const std::int32_t my_speed = vector_length(self + trick::kMovement);
        bail(other_speed > my_speed ? self : other, 0, 0);
        return;
    }
    if (theirs > 0) {
        return;
    }
    bail(self, 0, 0);
}

// 0x0049d740: the trigger nodes the skater passed through this tick: a zone query along the line from 16 units behind the previous position to the new one, with the skater recorded as the owner of
// the trigger flags
void SkaterKit::check_for_triggers(Word self) {
    if ((global_byte(m_, globals::kStaticGuard) & 1) == 0) {
        set_global_byte(m_, globals::kStaticGuard, global_byte(m_, globals::kStaticGuard) | 1);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, globals::kLastMove + 4 * k, 0);
        }
        engine(kAtExit, {0x0049d890});
    }
    Word move[3];
    for (Word k = 0; k < 3; ++k) {
        move[k] = word_at(m_, self + skater::kPosition + 4 * k) - word_at(m_, self + 0xbc + 4 * k);
    }
    if (move[0] == 0 && move[1] == 0 && move[2] == 0) {
        return;
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, globals::kLastMove + 4 * k, move[k]);
        set_word_at(m_, kLocalVector + 0x40 + 4 * k, 0);
    }
    unit_vector(kLocalVector + 0x40, globals::kLastMove);
    Word start[3];
    Word end[3];
    for (Word k = 0; k < 3; ++k) {
        start[k] = word_at(m_, self + 0xbc + 4 * k) - word_at(m_, kLocalVector + 0x40 + 4 * k) * 0x10;
        end[k] = word_at(m_, self + skater::kPosition + 4 * k);
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, collision::kInfoScratch + collision::line::kStart + 4 * k, start[k]);
        set_word_at(m_, collision::kInfoScratch + collision::line::kEnd + 4 * k, end[k]);
    }
    engine(collision::kInitCollisionQuery, {collision::kInfoScratch});
    set_global_byte(m_, collision::kInfoScratch + 0x88, 1);
    engine(kResetCollisionFlags, {static_cast<Word>(skater_at(m_, self).player_index()) + 1});
    set_global_word(m_, globals::kTriggerOwner, self);
    engine(kZoneBroadphase, {collision::kInfoScratch, 1});
    engine(kResetExecutedFlags, {static_cast<Word>(skater_at(m_, self).player_index()) + 1});
    set_global_word(m_, globals::kTriggerOwner, 0);
}

}  // namespace opentony::runtime::skaterkit
