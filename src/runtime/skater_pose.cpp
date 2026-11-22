#include "skater_views.hpp"

// Phase 4.15a: the small skater functions of physics.cpp: the pose and orientation helpers, the state change, the animation requests and the flip and timer handlers.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t sext16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }

}  // namespace

// 0x0048eb70: (60 / rate) frames pass per animation tick; the clock counted in those runs through a ping-pong of width 2 * (high - low) and the answer is low plus the distance travelled in
// the current leg. A rate or a width of 0 faults in retail (the divisions).
Word SkaterKit::ping_pong(std::int32_t low, std::int32_t high, std::int32_t rate) {
    const std::int32_t step = 60 / rate;
    const std::int32_t width = high - low;
    const std::int32_t period = width * 2;
    const std::int32_t remainder = (global_int(m_, globals::kAnimationClock) / step) % period;
    if (remainder > width) {
        return u(period - remainder + low);
    }
    return u(remainder + low);
}

// 0x0048f590: the saved orientation (32 bytes) and the saved position back into the skater
void SkaterKit::restore_saved_pose(Word self) {
    for (Word k = 0; k < 8; ++k) {
        set_word_at(m_, self + skater::kOrientation + 4 * k, word_at(m_, self + skater::kSavedOrientation + 4 * k));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + skater::kPosition + 4 * k, word_at(m_, self + skater::kSavedPosition + 4 * k));
    }
}

// 0x0048f5d0
void SkaterKit::set_panel_number(Word self, Word number) { engine(kPanelSetPlayerNumber, {number, self}); }

void SkaterKit::negate_vector(Word source, Word destination) {
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, destination + 4 * k, 0 - word_at(m_, source + 4 * k));
    }
}

// 0x004900b0: a change of the skater's state. Going between the air (1) and the ground (2) counts; leaving the ground for the air saves the forward vector scaled by 0x32c8 (the first and last
// components negated); leaving a grind (5) for the air turns the right and up vectors a quarter, which way depending on the animation and the facing bit
void SkaterKit::state_transition(Word self, Word state) {
    constexpr Word kGround = 2;
    constexpr Word kAir = 1;
    constexpr Word kGrind = 5;
    const Word old = skater_at(m_, self).physics_state();
    if (old != state) {
        if (state == kAir) {
            if (old != kGround) {
                skater_at(m_, self).set_field_3044(static_cast<Word>(skater_at(m_, self).field_3044() + 1));
            }
        } else if (state == kGround && old != kAir) {
            skater_at(m_, self).set_field_3044(static_cast<Word>(skater_at(m_, self).field_3044() + 1));
        }
    }
    if (old != kGround && state == kGround) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + skater::kLandingVelocity + 4 * k, word_at(m_, self + skater::kBasis + 4 * k));
        }
        set_word_at(m_, self + skater::kLandingVelocity, 0 - u(math_.mul_q12(int_at(m_, self + skater::kLandingVelocity), 0x32c8)));
        set_word_at(m_, self + skater::kLandingVelocity + 4, u(math_.mul_q12(int_at(m_, self + skater::kLandingVelocity + 4), 0x32c8)));
        set_word_at(m_, self + skater::kLandingVelocity + 8, 0 - u(math_.mul_q12(int_at(m_, self + skater::kLandingVelocity + 8), 0x32c8)));
    }
    if (skater_at(m_, self).physics_state() == kGrind && state == kAir) {
        const Word right = self + skater::kBasis + 0xc;
        const Word up = self + skater::kBasis + 0x18;
        const bool facing = (byte_at(m_, self + skater::kFlags) & 2) != 0;
        const bool turned = skater_at(m_, self).field_f6() == 0x23;
        if (turned == facing) {
            // (right, up) = (-up, right)
            const Word keep[3] = {word_at(m_, right), word_at(m_, right + 4), word_at(m_, right + 8)};
            negate_vector(up, right);
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, up + 4 * k, keep[k]);
            }
        } else {
            // (right, up) = (up, -right)
            const Word keep[3] = {word_at(m_, right), word_at(m_, right + 4), word_at(m_, right + 8)};
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, right + 4 * k, word_at(m_, up + 4 * k));
            }
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, up + 4 * k, 0 - keep[k]);
            }
        }
    }
    publish_orientation_basis(self);
    const Word before = skater_at(m_, self).physics_state();
    skater_at(m_, self).set_physics_state(static_cast<Word>(state));
    skater_at(m_, self).set_field_30c0(static_cast<Word>(before));
}

// 0x004902e0: a half-turn animation past frame 15 flips the facing bit and clears the spin reset; a pending facing flip toggles the bit; a pending orientation flip negates the six basis components
// the orientation is made of
void SkaterKit::flip_animation_transition(Word self) {
    if (skater_at(m_, self).field_f6() == 0x82 && sext16(skater_at(m_, self).field_f4()) > 0xf) {
        const Word flags = word_at(m_, self + skater::kFlags);
        skater_at(m_, self).set_field_2dd0(0);
        set_word_at(m_, self + skater::kFlags, flags ^ 2);
    }
    if (skater_at(m_, self).field_2c78() != 0) {
        const Word flags = word_at(m_, self + skater::kFlags);
        skater_at(m_, self).set_field_2c78(0);
        skater_at(m_, self).set_field_2c08(0);
        set_word_at(m_, self + skater::kFlags, flags ^ 2);
    }
    if (skater_at(m_, self).field_2c7c() != 0) {
        skater_at(m_, self).set_field_2c7c(0);
        skater_at(m_, self).set_field_2c08(0);
        // (the shorts of the orientation and the integers of the basis, one component at a time: the short is the negated low half, the integer the sign extension of that short)
        static constexpr Word kPairs[6][2] = {{0x30f4, 0x2e5c}, {0x30f8, 0x2e62}, {0x30fc, 0x2e68}, {0x3100, 0x2e58}, {0x3104, 0x2e5e}, {0x3108, 0x2e64}};
        for (const auto& pair : kPairs) {
            const std::uint16_t negated = static_cast<std::uint16_t>(0 - half_at(m_, self + pair[0]));
            set_half_at(m_, self + pair[1], negated);
            set_word_at(m_, self + pair[0], u(sext16(negated)));
        }
    }
}

// 0x004904c0
void SkaterKit::reset_trick_buffers(Word self) { set_word_at(m_, self + skater::kTrickBuffer, 0); }

// 0x00490610: the part of a vector along a Q12 normal taken off it (the normal is given as three shorts: x and y packed in one argument, z in the next)
void SkaterKit::remove_normal_component(Word vector, Word xy, Word z) {
    const std::int32_t nx = sext16(xy);
    const std::int32_t ny = sext16(xy >> 16);
    const std::int32_t nz = sext16(z);
    set_global_word(m_, kLocalVector, u(nx));
    set_global_word(m_, kLocalVector + 4, u(ny));
    set_global_word(m_, kLocalVector + 8, u(nz));
    const std::int32_t dot = math_.dot_q12(vector, kLocalVector);
    set_word_at(m_, vector, word_at(m_, vector) - u(math_.mul_q12(dot, nx)));
    set_word_at(m_, vector + 4, word_at(m_, vector + 4) - u(math_.mul_q12(dot, ny)));
    set_word_at(m_, vector + 8, word_at(m_, vector + 8) - u(math_.mul_q12(dot, nz)));
}

// 0x00490680: out = the vector without its component along the normal (given as two dwords holding three shorts), scaled back to the vector's own length (times 64, in units of 256)
void SkaterKit::damp_to_ground_plane(Word vector, Word normal, Word out) {
    const auto root = [](std::int32_t value) { return integer_sqrt(value); };
    const std::int32_t length_in = root(math_.dot_q12(vector, vector)) << 6;
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, out + 4 * k, word_at(m_, vector + 4 * k));
    }
    remove_normal_component(out, word_at(m_, normal), word_at(m_, normal + 4));
    const std::int32_t length_out = (root(math_.dot_q12(out, out)) << 6) >> 8;
    const std::int32_t length_before = length_in >> 8;
    if (length_out > 0) {
        // out = out * length_before / length_out (component by component through the vector operators of the core layer)
        for (Word k = 0; k < 3; ++k) {
            const Word scaled = word_at(m_, out + 4 * k) * u(length_before);
            set_word_at(m_, out + 4 * k, u(s(scaled) / length_out));
        }
    }
}

// 0x0049c7d0: the nine orientation shorts as the integer basis: forward = column 0x2e5c / 0x2e62 / 0x2e68, right = 0x2e58 / 0x2e5e / 0x2e64, up = 0x2e5a / 0x2e60 / 0x2e66
void SkaterKit::fru_from_rotation_matrix(Word self) {
    static constexpr Word kPairs[9][2] = {{0x30f4, 0x2e5c}, {0x30f8, 0x2e62}, {0x30fc, 0x2e68}, {0x3100, 0x2e58}, {0x3104, 0x2e5e}, {0x3108, 0x2e64}, {0x310c, 0x2e5a}, {0x3110, 0x2e60}, {0x3114, 0x2e66}};
    for (const auto& pair : kPairs) {
        set_word_at(m_, self + pair[0], u(sext16(half_at(m_, self + pair[1]))));
    }
}

// 0x0049c850: the integer basis (the low halves) as the orientation shorts
void SkaterKit::publish_orientation_basis(Word self) {
    static constexpr Word kPairs[9][2] = {{0x3100, 0x2e58}, {0x3104, 0x2e5e}, {0x3108, 0x2e64}, {0x310c, 0x2e5a}, {0x3110, 0x2e60}, {0x3114, 0x2e66}, {0x30f4, 0x2e5c}, {0x30f8, 0x2e62}, {0x30fc, 0x2e68}};
    for (const auto& pair : kPairs) {
        set_half_at(m_, self + pair[1], half_at(m_, self + pair[0]));
    }
}

// 0x0049e430: counts a flip down when the phase of the flip (advanced by its step scaled by the animation) crosses bit 12; the last one clears the phase and the step
void SkaterKit::handle_flips(Word self) {
    const Word count = skater_at(m_, self).field_2c88();
    if (count == 0) {
        return;
    }
    const Word before = word_at(m_, self + skater::kFlipPhase);
    const std::int32_t step = (skater_at(m_, self).field_2c90() * global_int(m_, globals::kAnimationTimeScale)) >> 8;
    const Word after = before + u(step);
    set_word_at(m_, self + skater::kFlipPhase, after);
    if (((after ^ before) & 0x1000) == 0) {
        return;
    }
    skater_at(m_, self).set_field_2c88(static_cast<Word>(count - 1));
    if (count - 1 == 0) {
        set_word_at(m_, self + skater::kFlipPhase, 0);
        skater_at(m_, self).set_field_2c90(0);
    }
}

// 0x0049e480: the run's clock. In the modes other than 2 and 4 the time left (or, with no time limit, a fixed 100) less the animation clock decides whether the run has ended; when it has, the
// end is announced (a sound in the career, the versus modes check the leader first) and the netgame layer is asked whether the pause is agreed before the level event is dispatched
void SkaterKit::handle_timer(Word self) {
    const Word mode = global_word(m_, globals::kGameMode);
    if (mode == 2 || mode == 4) {
        return;
    }
    const Word timeless = global_word(m_, globals::kTimeless);
    std::int32_t remaining = timeless == 0 ? static_cast<std::int32_t>(skater_at(m_, self).field_2cc8()) : 0x64;
    remaining -= global_int(m_, globals::kAnimationClock);
    if (global_byte(m_, globals::kTimeCountsUp) != 0) {
        remaining = static_cast<std::int32_t>(skater_at(m_, self).field_2cc8());
    }
    if (global_byte(m_, globals::kNetworkFlag) != 0) {
        if ((global_word(m_, globals::kNetworkStatus) & 0x80000) == 0 && remaining >= 0) {
            return;
        }
    } else if (remaining >= 0) {
        return;
    }
    if (global_word(m_, globals::kRunLocked) != 0) {
        return;
    }
    if (timeless != 0) {
        set_global_word(m_, globals::kEndCause, 8);
        return;
    }
    Word current = mode;
    bool announce = true;
    if (current == 1) {
        const Word level = global_word(m_, globals::kCurrentLevel);
        if (byte_at(m_, globals::kLevelRecords + 0x1ac * level) != 0) {
            engine(kSoundPlay, {0xa0, 0x2000, 0});
            current = global_word(m_, globals::kGameMode);
        } else {
            announce = false;
        }
    }
    if (announce && current == 9) {
        const Word first = global_word(m_, globals::kPlayerOne);
        if (word_at(m_, first + skater::kState) != 0 || word_at(m_, first + 0x2dd4) != 0) {
            return;
        }
    }
    if ((engine_value(kPauseAgreement) & 0xff) != 0) {
        engine(kInitializeAndDispatch);
    }
}

void SkaterKit::request_animation(Word self, Word animation, Word from, Word to, Word blend) {
    flip_animation_transition(self);
    skater_at(m_, self).set_field_108(0x10000);
    engine(kAnimationStart, {self, animation, from, to, blend});
}

// 0x004903f0: (animation), from the start to the end of the animation
void SkaterKit::animation_request_start(Word self, Word animation) { request_animation(self, animation, 0, 0xffffffffu, 0xffffffffu); }

// 0x00490420: (animation, from) to the end of the animation
void SkaterKit::animation_request_start_default_end(Word self, Word animation, Word from) { request_animation(self, animation, from, 0xffffffffu, 0xffffffffu); }

// 0x00490450: (animation, from, to)
void SkaterKit::animation_request_range(Word self, Word animation, Word from, Word to) { request_animation(self, animation, from, to, 0xffffffffu); }

// 0x00490480: (animation, from, to, blend)
void SkaterKit::animation_request_full(Word self, Word animation, Word from, Word to, Word blend) { request_animation(self, animation, from, to, blend); }

// 0x00480890 (anim.cpp, from the disassembly): the object's animation set (+0xf6) and checked against the count of its region's table (AnimationTablesByRegion, 0x44 bytes a region
// at 0x0056d444, the table's word 0 the count); the frame count is byte 0 of the table's 8-byte entry after the count, the cycle starts at frame 0 in mode 1 with the direction given.
// A failed check is reported and the entry is read anyway, as retail does.
void SkaterKit::animation_cycle(Word self, Word animation, Word direction) {
    constexpr Word kRegionTables = 0x0056d444;
    constexpr Word kMsgBadAnimation = 0x00535ba4;
    skater_at(m_, self).set_field_f6(static_cast<std::uint16_t>(static_cast<std::uint16_t>(animation)));
    const Word table = word_at(m_, kRegionTables + 0x44 * skater_at(m_, self).psx_region_slot());
    const Word id = skater_at(m_, self).field_f6();
    s_.check(id < word_at(m_, table), kMsgBadAnimation);
    skater_at(m_, self).set_field_100(static_cast<std::uint8_t>(static_cast<std::uint8_t>(direction)));
    skater_at(m_, self).set_field_106(static_cast<std::uint8_t>(byte_at(m_, table + 8 + 8 * id)));
    skater_at(m_, self).set_field_f8(1);
    skater_at(m_, self).set_field_f4(0);
    skater_at(m_, self).set_field_104(0);
    skater_at(m_, self).set_field_107(0);
}

}  // namespace opentony::runtime::skaterkit
