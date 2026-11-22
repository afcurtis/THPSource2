#include "frame_pacing.hpp"
#include "skater_views.hpp"

// Phase 4.15a: the ollie before the physics step (physics.cpp, 0x0049a280): a button held charges the jump (+0x2de8 counts the frames up to a limit set by the stats), the release makes it: the take-off, the
// impulse up (a polynomial of the stats and the charge), the trick that was asked for (an ollie, a nollie, a fakie ollie or a boneless, decided from the pad history), the animations and the sounds.
// The same function plays the landing animation and lands the trick when the skater touches the ground again. The animation requests are the ones of `skater_pose.cpp`; the pad history matcher,
// the record consumer, the turn phase, the landing of a trick and the take-off are services.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kMatchHistory = 0x00492560;        // TrickInput_MatchHistory(a, b, c, d, e) (thiscall on the skater) -> the next index of the pad history (negative: none)
constexpr Word kConsumeRecord = 0x00492030;       // TrickInput_ConsumeRecord(1, 1) (thiscall on the skater)
constexpr Word kTurnPhase = 0x0049b500;           // Skater_ApplyTurnVelocityPhase(turn, 1, 0) (thiscall on the skater)
constexpr Word kTrickLand = 0x0048f8f0;           // Trick_Land() (thiscall)
constexpr Word kTrickTakeOff = 0x0048f5f0;        // Trick_TakeOff() (thiscall)
constexpr Word kLandingEffects = 0x00431260;      // (surface, 0) (thiscall on the object at +0x2c14)
constexpr Word kMsgFirstEvent = 0x00536e84;       // "FirstEvent not set"
constexpr Word kNameWallie = 0x00536e98;
constexpr Word kNameNoComply = 0x00536e6c;
constexpr Word kNameBoneless = 0x00536e78;
constexpr Word kNameFakieOllie = 0x00536e60;
constexpr Word kNameNollie = 0x005330d4;

inline Word mulw(std::int32_t a, std::int32_t b) { return static_cast<Word>(a) * static_cast<Word>(b); }

}  // namespace

// 0x0049a280 (fastcall)
void SkaterKit::pre_physics_ollie(Word self) {
    const auto read = [this, self](Word field) { return word_at(m_, self + field); };
    const auto write = [this, self](Word field, Word value) { set_word_at(m_, self + field, value); };
    const auto stat = [this, self](Word selector) { return profile_value(self, selector); };
    const auto clock = [this] { return global_word(m_, globals::kAnimationClock); };
    // 15 less a twentieth of two stats (the charge needed)
    const auto charge_limit = [&stat] {
        const std::int32_t first = stat(2);
        const std::int32_t second = stat(0);
        return 0xf - (first + second) / 0x14;
    };
    if (read(0x2f64) != 0) {
        return;
    }

    // the animation the held button asks for
    if (read(0x2dd8) != 0) {
        if (skater_at(m_, self).field_f6() == 0) {
            animation_request_full(self, 8, 0, 0x1a, 0x13);
        }
    } else if (skater_at(m_, self).field_f6() == 8) {
        if (skater_at(m_, self).field_102() == 0xff) {
            if (skater_at(m_, self).field_f4() == 0) {
                animation_request_start(self, 0);
            }
        } else {
            animation_request_range(self, 8, 0x13, 0);
        }
    }
    if (skater_at(m_, self).field_107() != 0) {
        switch (skater_at(m_, self).field_f6()) {
        default:
            if ((read(skater::kState) == 1 || read(skater::kState) == 2) && read(0x29c8) == 0 && read(0x2f64) == 0) {
                animation_request_start(self, 0xe);
            }
            break;
        case 4:
        case 0xaf:
            if (read(0x2dd8) == 0) {
                animation_request_start(self, 0xe);
            }
            break;
        case 5:
        case 0x19:
            if (read(0x2dd8) == 0) {
                animation_request_start(self, 0);
            } else {
                animation_request_full(self, 8, 0x13, 0x1a, 0x13);
            }
            break;
        case 8:
            if (skater_at(m_, self).field_f4() == 0) {
                animation_request_start(self, 0);
            } else {
                animation_request_full(self, 8, 0x13, 0x1a, 0x13);
            }
            break;
        case 0xe:
            animation_request_start(self, 0xe);
            break;
        case 0x22: case 0x30: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x93: case 0x94: case 0x95: case 0x97: case 0x98: case 0xa8: case 0xb5:
            if (read(0x2dd8) == 0) {
                animation_request_range(self, 8, 0x13, 0);
            } else {
                animation_request_full(self, 8, 0x13, 0x1a, 0x13);
            }
            break;
        case 0x82:
            write(0x2dd0, 0);
            if (read(0x2dd8) == 0) {
                animation_request_start(self, 0);
            } else {
                animation_request_full(self, 8, 0, 0x1a, 0x13);
            }
            break;
        case 0xa2:
        case 0xa3:
        case 0:
            if (skater_at(m_, self).field_f6() != 0) {
                write(0x31f0, skater_at(m_, self).field_f4() == 0 ? 0 : 0xfa);
            }
            if (read(0x2dd8) == 0) {
                animation_request_start(self, 0);
            } else {
                animation_request_full(self, 8, 0x13, 0x1a, 0x13);
            }
            break;
        }
    }

    // landing: back on the ground after the jump
    if (read(0x2ddc) != 0 && read(0x30c0) != 0 && (read(skater::kState) == 0 || read(skater::kState) == 7)) {
        if (read(0x2dd4) == 0) {
            write(0x29c8, 0);
        }
        write(0x2ddc, 0);
        write(0x2db8, 0);
        if (read(0x2f64) == 0) {
            play_landing_sound(read(0x30b0));
            vibrate(self, 2, 1, 0xff);
            if (read(0x2dd4) == 0) {
                if (read(0x2dd8) != 0) {
                    animation_request_start_default_end(self, 0x19, 3);
                } else {
                    animation_request_start_default_end(self, 5, 0);
                }
                bail_when_flipping(self);
            }
            if (read(0x2f64) == 0) {
                std::int32_t value = s(engine_value(kLandedValue, {self + trick::kPanel})) / 100;
                if (value > 0x14) {
                    value = 0x14;
                }
                write(0x2dcc, read(0x2dcc) + u(value));
                if (read(0x2dd4) == 0) {
                    engine(kTrickLand, {self});
                    engine(kLandingEffects, {self + 0x2c14, read(0x30b0), 0});
                }
            }
        }
        write(0x2c68, 0);
    }

    const Word pad = read(0x2ccc);
    if (byte_at(m_, pad + 0x30) != 0 && read(0x2f64) == 0) {
        // the button is down: the charge grows up to its limit (or at once, when the bail lock is on)
        write(0x2de8, read(0x2de8) + framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }));   // frames: at 60 fps the 30 Hz frames this one is worth (frame_pacing.hpp)
        if (charge_limit() < s(read(0x2de8)) || global_word(m_, globals::kBailLocked) != 0) {
            write(0x2de8, u(charge_limit()));
        }
        const Word state = read(skater::kState);
        if ((state == 0 || state == 7) && read(0x2e30) == 0) {
            write(0x2de0, 1);
            if (read(0x2dd8) == 0) {
                const std::int32_t drift = s(read(0x3144));
                if ((drift < 0 ? -drift : drift) < 0xa000) {
                    write(0x3144, 0);
                    if (read(0x2dd4) == 0) {
                        animation_request_full(self, 8, 0, 0x1a, 0x13);
                    }
                }
            }
        } else if (state == 4 && read(0x2e30) == 0) {
            write(0x2de0, 1);
        } else if (state == 5) {
            if (read(0x2e30) != 0) {
                return;
            }
            write(0x2de0, 1);
        }
        if (read(0x2e30) != 0) {
            return;
        }
        write(0x2dd8, 1);
        return;
    }

    // the button is up: the jump
    write(0x2e30, 0);
    write(0x2dd8, 0);
    if (s(read(0x2de4)) > 0 && s(clock() - read(0x2de4)) > 0x14) {
        write(0x2de0, 0);
    }
    if (read(0x2de0) != 0) {
        const Word state = read(skater::kState);
        if (global_word(m_, globals::kRunLocked) == 1 && state != 4 && state != 5 && read(0x2dd4) == 0) {
            write(0x2de0, 0);
            animation_request_start(self, 0);
            write(0x2de8, 0);
            return;
        }
        write(0x2dec, read(0x2de8));
        write(0x2de0, 0);
        write(0x2ddc, 1);
        write(0x2c08, 0);
        const Word velocity = self + trick::kMovement;
        write(0x2f30, sqrt_truncated(u(math_.dot_q12(velocity, velocity))) << 6);
        if (read(0x2e90) != 0) {
            write(0x29c8, 0);
            write(0x2ec0, 5);
            write(0x2ec4, 5);
            write(0x2e90, 0);
            write(0x2e94, 0);
            write(0x2c68, 0);
            for (Word k = 0; k < 3; ++k) {
                write(skater::kBasis + 4 * k, read(0x2eb4 + 4 * k));
            }
            rebuild_sideways_basis(self);
            const std::int32_t along = math_.dot_q12(self + 0x2eb4, velocity);
            set_word_at(m_, velocity, u(math_.mul_q12(s(read(0x2eb4)), along)));
            set_word_at(m_, velocity + 4, u(math_.mul_q12(s(read(0x2eb8)), along)));
            set_word_at(m_, velocity + 8, u(math_.mul_q12(s(read(0x2ebc)), along)));
            std::int32_t turn = 0;
            if (byte_at(m_, pad + 0x80) != 0) {
                turn = -200;
            }
            if (byte_at(m_, pad + 0x90) != 0) {
                turn = 200;
            }
            engine(kTurnPhase, {self, u(turn), 1, 0});
        }
        write(0x2db8, read(0x2db4));
        const Word entered_state = read(skater::kState);
        write(0x2df4, entered_state == 5 ? 1 : 0);
        if (entered_state == 5) {
            engine(kApplyGapDefinition, {self, kNameWallie, 200, 1, 0xffffffffu});
            write(0x2de8, u(charge_limit()));
            const std::int32_t scaled[3] = {2 * static_cast<std::int16_t>(half_at(m_, self + 0x3128)), 2 * static_cast<std::int16_t>(half_at(m_, self + 0x312a)), 2 * static_cast<std::int16_t>(half_at(m_, self + 0x312c))};
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, self + 0x58 + 4 * k, word_at(m_, self + 0x58 + 4 * k) + u(scaled[k]));
            }
            write(0x50, read(0x50) + 0xffff1000u);
        }
        bool script_ran = false;
        if (read(skater::kState) == 4) {
            if (read(0x29d4) != 0) {
                trick_script_start(self, read(0x29d4));
                write(0x29d4, 0);
                grind_off_sound(self);
                script_ran = true;
            }
        } else if (read(skater::kState) == 0 && read(0x2dd4) == 0 && read(0x2c68) != 0) {
            write(0x29c8, 0);
            write(0x2c68, 0);
            engine(kTrickLand, {self});
        }

        // the jump's impulse
        std::int32_t impulse;
        const std::int32_t side = s(read(0x3110));
        if ((side < 0 ? -side : side) < 0x9c4) {
            const std::int32_t limit = charge_limit();
            const std::int32_t first = s(mulw(stat(0) + 900, 3000)) / 10000;
            const std::int32_t second = s(mulw(stat(0), 3300) + 0x166e30u) / 10000;
            std::int32_t part = s(mulw(first - second, s(read(0x2de8))));
            part = s(u(part) << 10) / 10 / limit;
            std::int32_t base = s(mulw(-900 - stat(0), 3000)) / 10000;
            base = s(u(base) << 10) / 10;
            impulse = s(mulw(base - part, 3));
            const std::int32_t air = s(read(0x2f48) - read(0x2f4c)) >> 12;
            if (s(read(0x2f30)) >= 0x28000 && air > 500) {
                const std::int32_t capped = air > 700 ? 700 : air;
                impulse = s(mulw((capped - 300), impulse)) / 200;
                impulse = s(mulw(impulse, 0x78)) / 100;
            }
        } else {
            const std::int32_t limit = charge_limit();
            const std::int32_t first = s(mulw(stat(2) + 0x21c, 5000)) / 10000;
            const std::int32_t second = s(mulw(stat(2), 3300) + 0x166e30u) / 10000;
            std::int32_t part = s(mulw(first - second, s(read(0x2de8))));
            part = s(u(part) << 10) / 10 / limit;
            std::int32_t base = s(mulw(-0x21c - stat(2), 5000)) / 10000;
            base = s(u(base) << 10) / 10;
            impulse = s(mulw(base - part, 3));
        }
        write(0x50, read(0x50) + u(impulse));
        write(0x303c, read(0x303c) + 1);
        if (s(read(0x2de8)) < charge_limit()) {
            write(0x3040, read(0x3040) + 1);
        }
        write(0x3068, 0);
        write(0x306c, 0);
        if (read(skater::kState) != 2) {
            if (read(0x2db8) != 0 && read(skater::kState) == 0) {
                state_transition(self, 3);
                write(0x2da0, clock());
            } else {
                state_transition(self, 1);
            }
        }
        if (read(0x2dd4) != 0) {
            write(0x2c68, 0);
            write(0x29c8, 0);
            if (read(0x29d4) != 0) {
                trick_script_start(self, read(0x29d4));
                write(0x29d4, 0);
            }
            write(0x29e0, 0);
            write(0x2f24, 0);
        }
        write(0x29e0, 0);
        write(0x31f8, 0);
        if (read(0x29c8) == 0) {
            // the pad history: the first two press events decide which trick the release makes
            std::int32_t newest = s(read(0x2b14)) - 1;
            if (newest < 0) {
                newest = 0x1f;
            }
            Word window = 0x14;
            std::int32_t first_press = -1;
            std::int32_t second_press = -1;
            std::int32_t index = s(engine_value(kMatchHistory, {self, u(newest), 0, 0x14, u(newest), 0}));
            while (index >= 0) {
                const std::uint8_t button = byte_at(m_, self + trick::kPadHistory + 8 * u(index));
                if (first_press == -1) {
                    if (button == 1) {
                        window = 0x23;
                        first_press = index;
                    }
                } else if (button == 1) {
                    second_press = index;
                    check(first_press != -1, kMsgFirstEvent);
                    break;
                }
                index = s(engine_value(kMatchHistory, {self, u(index), 1, window, u(newest), 0}));
            }
            if (read(0x31f4) == 0 && (read(0x2dd4) != 0 || first_press == -1 || entered_state == 5)) {
                engine(kTrickTakeOff, {self});
                animation_request_start(self, 4);
            } else {
                write(0x3068, 0);
                write(0x306c, 0);
                if (second_press == -1) {
                    engine(kTrickTakeOff, {self});
                    if (read(0x31f4) == 0) {
                        engine(kApplyGapDefinition, {self, kNameNoComply, 200, 0, 0xffffffffu});
                        animation_request_start_default_end(self, 0xd8, 0);
                    } else {
                        if (((read(skater::kFlags) >> 1) & 1) != skater_at(m_, self).selectable()) {
                            write(0x31fc, 1);
                        }
                        engine(kApplyGapDefinition, {self, read(0x31fc) == 0 ? kNameNollie : kNameFakieOllie, 200, 0, 0xffffffffu});
                        animation_request_start(self, 0xaf);
                        engine(kConsumeRecord, {self, 1, 1});
                        write(0x31f8, 1);
                    }
                } else {
                    engine(kTrickTakeOff, {self});
                    write(0x50, read(0x50) - 0x5000);
                    engine(kApplyGapDefinition, {self, kNameBoneless, 0xfa, 0, 0xffffffffu});
                    animation_request_start_default_end(self, 0x91, 5);
                    engine(kSoundPlay, {0xb4, 0x2000, 0});
                    engine(kConsumeRecord, {self, 1, 1});
                    engine(kConsumeRecord, {self, 1, 1});
                }
                write(0x2c70, 1);
            }
        }
        if (!script_ran) {
            play_takeoff_sound(read(0x30b0));
        }
        write(0x2f34, clock());
    }
    write(0x2de8, 0);
}

}  // namespace opentony::runtime::skaterkit
