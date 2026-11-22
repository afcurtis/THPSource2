#include "frame_pacing.hpp"
#include "free_look.hpp"
#include "skater_views.hpp"

// Milestone 10g: the skater's action step (physics.cpp, 0x00493370 Skater_ActionPhysicsStep), written from the machine code. Three parts: the queued turn (+0x2ca0..0x2ca8: what is left of three queued
// motions, spent at the rates in +0x2c94..0x2c9c) rotates the orientation about the vertical through a point 0x46000 above the skater and moves the skater so that point stays; the spin counter
// (+0x2e80, states 1 and 2) is moved by the pad's two spin buttons and the angle (+0x3068) turned by it; the turn accumulator (+0x3144) is moved toward the left or right input or the analog
// stick's target (limit 0x2d000, 0x5a000 at speed or off the ground 0xa0000) and the heading update runs on it. The state 1 tail sets the lean word +0x2e84 from the stick and the
// camera side. The compiler's magic-number divisions are plain truncating divisions. Everything it calls is a service (the turn phase, the trick text, the heading update), except the matrix and
// GTE helpers (0x004e8460, 0x004e85a0, 0x004e80e0, 0x004e3130) and the dot product and root (0x004f5f90, 0x004f53b0), which run here.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }
inline Word add(Word a, Word b) { return a + b; }
inline std::int32_t plus(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) + static_cast<Word>(b)); }
inline std::int32_t minus(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) - static_cast<Word>(b)); }
inline std::int32_t sar(std::int32_t v, int n) { return v >> n; }
inline std::int32_t absolute(std::int32_t v) {
    const Word sign = static_cast<Word>(v >> 31);
    return static_cast<std::int32_t>((static_cast<Word>(v) ^ sign) - sign);
}

constexpr Word kTurnPhase = 0x0049b500;           // Skater_ApplyTurnVelocityPhase(turn, 0, 0) (thiscall)
constexpr Word kRefreshTrickText = 0x0048ddc0;    // Panel_RefreshTrickText (thiscall on the panel at +0x168)
constexpr Word kUpdateHeading = 0x00492f20;       // Skater_UpdateHeading (thiscall)

constexpr Word kTimeScale = 0x0056865c;           // the frame scale: 0x100 is one frame
constexpr Word kFrameClock = 0x005685f4;
constexpr Word kSwapSpinButtons = 0x0056a88c;     // dword: the two spin buttons exchanged
constexpr Word kDigitalPad = 0x0056b7f0;          // dword: the pad profile's direction bytes are used as they are
constexpr Word kFastTurnA = 0x0056b038;           // bytes: either one makes the turn step 10 times larger
constexpr Word kFastTurnB = 0x0056b058;

constexpr Word kGteRotation = 0x006a3e10;         // nine shorts
constexpr Word kGteIr = 0x006a3eb0;               // three shorts

// the retail stack locals, in the scratch area
constexpr Word kLocals = pcplatform::kScratchBase + 0xe00;
constexpr Word kZero = kLocals + 0x00;            // a zero vector
constexpr Word kPivot = kLocals + 0x10;           // (0, 0x46000, 0)
constexpr Word kBefore = kLocals + 0x20;          // the pivot through the old orientation
constexpr Word kAfter = kLocals + 0x30;           // and through the new one
constexpr Word kAngles = kLocals + 0x40;          // three shorts
constexpr Word kTurnMatrix = kLocals + 0x50;      // the rotation about the vertical (a matrix record, 0x20 bytes)
constexpr Word kSide = kLocals + 0x70;            // the skater's offset from its camera

}  // namespace

// 0x00493370 (thiscall)
void SkaterKit::action_physics_step(Word self) {
    const Word matrix = self + 0x2e58;
    // ---- the queued turn ----
    if (skater_at(m_, self).field_2ca0() != 0 || skater_at(m_, self).field_2ca4() != 0 || skater_at(m_, self).field_2ca8() != 0) {
        std::int32_t spent[3] = {0, 0, 0};
        for (Word k = 0; k < 3; ++k) {
            const Word queue = self + 0x2ca0 + 4 * k;
            const std::int32_t left = int_at(m_, queue);
            if (left == 0) {
                continue;
            }
            const std::int32_t rate = int_at(m_, queue - 0xc);
            const std::int32_t step = sar(mul(absolute(rate), global_int(m_, kTimeScale)), 8);
            if (left < step) {
                spent[k] = mul(left, rate >= 0 ? 1 : -1);
                set_word_at(m_, queue, 0);
            } else {
                set_word_at(m_, queue, u(minus(left, step)));
                const std::int32_t moved = sar(mul(global_int(m_, kTimeScale), int_at(m_, queue - 0xc)), 8);
                spent[k] = moved;
                set_word_at(m_, queue + 0xc, add(word_at(m_, queue + 0xc), u(moved)));
            }
        }
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, kZero + k, 0);
        }
        math_.matrix_set_translation(matrix, kZero);
        math_.matrix_set_translation(kTurnMatrix, kZero);
        set_global_word(m_, kPivot, 0);
        set_global_word(m_, kPivot + 4, 0x46000);
        set_global_word(m_, kPivot + 8, 0);
        math_.convert_vector(matrix, kPivot, kBefore);
        set_global_half(m_, kAngles, 0);
        set_global_half(m_, kAngles + 2, static_cast<std::uint16_t>(spent[1] & 0xfff));
        set_global_half(m_, kAngles + 4, 0);
        math_.rotation_from_angles(kAngles, kTurnMatrix);
        // the new orientation is the old one times the turn, column by column through the GTE
        for (Word k = 0; k < 9; ++k) {
            set_half_at(m_, kGteRotation + 2 * k, half_at(m_, matrix + 2 * k));
        }
        for (Word column = 0; column < 3; ++column) {
            for (Word row = 0; row < 3; ++row) {
                set_half_at(m_, kGteIr + 2 * row, half_at(m_, kTurnMatrix + 2 * (3 * row + column)));
            }
            math_.rtir();
            for (Word row = 0; row < 3; ++row) {
                set_half_at(m_, matrix + 2 * (3 * row + column), half_at(m_, kGteIr + 2 * row));
            }
        }
        math_.convert_vector(matrix, kPivot, kAfter);
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, self + 0x08 + k, add(word_at(m_, self + 0x08 + k), word_at(m_, kBefore + k) - word_at(m_, kAfter + k)));
        }
    }

    // ---- the spin counter ----
    if (const Word spin_state = skater_at(m_, self).physics_state(); spin_state == 1 || spin_state == 2) {
        if (skater_at(m_, self).field_2c80() == 0) {
            const Word swap = global_word(m_, kSwapSpinButtons);
            const auto exchange = [&]() {
                const Word profile = skater_at(m_, self).action_record_bank().address;
                const std::uint8_t first = byte_at(m_, profile + 0x51);
                const std::uint8_t second = byte_at(m_, profile + 0x71);
                set_byte_at(m_, profile + 0x51, second);
                set_byte_at(m_, profile + 0x71, first);
            };
            if (swap != 0) {
                exchange();
            }
            Word profile = skater_at(m_, self).action_record_bank().address;
            if (byte_at(m_, profile + 0x51) != 0) {
                set_byte_at(m_, profile + 0x51, 0);
                if (static_cast<std::int32_t>(skater_at(m_, self).field_2e80()) > 0) {
                    skater_at(m_, self).set_field_2e80(0);
                }
                skater_at(m_, self).set_field_3074(1);
                skater_at(m_, self).set_field_2e80(static_cast<Word>(skater_at(m_, self).field_2e80() - 1));
            }
            profile = skater_at(m_, self).action_record_bank().address;
            if (byte_at(m_, profile + 0x71) != 0) {
                set_byte_at(m_, profile + 0x71, 0);
                if (static_cast<std::int32_t>(skater_at(m_, self).field_2e80()) < 0) {
                    skater_at(m_, self).set_field_2e80(0);
                }
                skater_at(m_, self).set_field_3074(1);
                skater_at(m_, self).set_field_2e80(static_cast<Word>(skater_at(m_, self).field_2e80() + 1));
            }
            if (global_word(m_, kSwapSpinButtons) != 0) {
                exchange();
            }
        }
        const std::int32_t count = static_cast<std::int32_t>(skater_at(m_, self).field_2e80());
        if (count > 0) {
            engine(kTurnPhase, {self, u(sar(mul(global_int(m_, kTimeScale), 150), 8)), 0, 0});
            const std::int32_t angle = plus(static_cast<std::int32_t>(skater_at(m_, self).field_3068()), sar(mul(global_int(m_, kTimeScale), 150), 8));
            skater_at(m_, self).set_field_3068(static_cast<Word>(u(angle)));
            if (angle >= 0x800) {
                skater_at(m_, self).set_field_2e80(static_cast<Word>(skater_at(m_, self).field_2e80() - 1));
                skater_at(m_, self).set_field_3068(static_cast<Word>(u(minus(angle, 0x800))));
                if (skater_at(m_, self).field_2c84() == 0) {
                    skater_at(m_, self).set_field_306c(static_cast<Word>(skater_at(m_, self).field_306c() + 1));
                    thiscall(kRefreshTrickText, self + 0x168);
                }
            }
        } else if (count < 0) {
            engine(kTurnPhase, {self, u(minus(0, sar(mul(global_int(m_, kTimeScale), 150), 8))), 0, 0});
            const std::int32_t angle = minus(static_cast<std::int32_t>(skater_at(m_, self).field_3068()), sar(mul(global_int(m_, kTimeScale), 150), 8));
            skater_at(m_, self).set_field_3068(static_cast<Word>(u(angle)));
            if (angle <= -0x800) {
                skater_at(m_, self).set_field_2e80(static_cast<Word>(skater_at(m_, self).field_2e80() + 1));
                skater_at(m_, self).set_field_3068(static_cast<Word>(u(plus(angle, 0x800))));
                if (skater_at(m_, self).field_2c84() == 0) {
                    skater_at(m_, self).set_field_306c(static_cast<Word>(skater_at(m_, self).field_306c() + 1));
                    thiscall(kRefreshTrickText, self + 0x168);
                }
            }
        }
    } else {
        skater_at(m_, self).set_field_2e80(0);
    }
    if (static_cast<std::int32_t>(skater_at(m_, self).field_2e80()) != 0) {
        skater_at(m_, self).set_turn_accumulator(0);
        return;
    }

    // ---- the turn accumulator ----
    skater_at(m_, self).set_field_2e7c(0);
    const Word state = skater_at(m_, self).physics_state();
    Word left;
    Word right;
    std::int32_t step;
    std::int32_t limit;
    if (state == 0 || state == 7) {
        // on the ground: the turn base by the profile byte, doubled with the wide limit at speed or with the profile's switch
        const Word kind = skater_at(m_, self).field_29b7();
        const std::int32_t base = kind == 0 ? 0x3c : (kind == 1 ? 0x78 : 0xb4);
        step = sar(mul(base << 8, global_int(m_, kTimeScale)), 8);
        const Word profile = skater_at(m_, self).action_record_bank().address;
        left = byte_at(m_, profile + 0x80);
        right = byte_at(m_, profile + 0x90);
        limit = 0x2d000;
        skater_at(m_, self).set_field_3060(0);
        if (static_cast<std::int8_t>(skater_at(m_, self).field_31a2()) > 0x1e || byte_at(m_, profile + 0xb0) != 0) {
            skater_at(m_, self).set_field_2e78(1);
            step = plus(step, step);
            limit = 0x5a000;
            // the speed bleeds off the response vector by one unit of its direction
            const std::int32_t speed = mul(integer_sqrt(math_.dot_q12(self + 0x4c, self + 0x4c)), 0x40);
            if (speed >= 0x1000) {
                const std::int32_t unit = sar(speed, 12);
                set_word_at(m_, self + 0x58, u(minus(int_at(m_, self + 0x58), int_at(m_, self + 0x4c) / unit)));
                set_word_at(m_, self + 0x5c, u(minus(int_at(m_, self + 0x5c), int_at(m_, self + 0x50) / unit)));
                set_word_at(m_, self + 0x60, u(minus(int_at(m_, self + 0x60), int_at(m_, self + 0x54) / unit)));
            }
        } else {
            skater_at(m_, self).set_field_2e78(0);
        }
        if (left == 0 && right == 0) {
            skater_at(m_, self).set_field_30a4(0);
        }
    } else {
        // off the ground (states 3 and up): the pad's directions, debounced by the frame clock, and the wide limit
        const Word profile = skater_at(m_, self).action_record_bank().address;
        const bool digital = global_word(m_, kDigitalPad) != 0;
        if (digital) {
            left = byte_at(m_, profile + 0x80);
            right = byte_at(m_, profile + 0x90);
        } else {
            left = (byte_at(m_, profile + 0x80) != 0 || byte_at(m_, profile + 0x40) != 0) ? 1 : 0;
            right = (byte_at(m_, profile + 0x90) != 0 || byte_at(m_, profile + 0x60) != 0) ? 1 : 0;
        }
        const Word kept_right = right;
        step = sar(static_cast<std::int32_t>(static_cast<Word>(mul(5, global_int(m_, kTimeScale))) << 13), 8);
        if (global_byte(m_, kFastTurnA) != 0 || global_byte(m_, kFastTurnB) != 0) {
            step = sar(mul(global_int(m_, kTimeScale), 0x64000), 8);
        }
        limit = 0xa0000;
        if (left == 0 && right == 0) {
            skater_at(m_, self).set_field_3060(static_cast<Word>(global_word(m_, kFrameClock)));
        } else if (static_cast<std::int32_t>(skater_at(m_, self).field_3060()) > 0) {
            if (minus(global_int(m_, kFrameClock), static_cast<std::int32_t>(skater_at(m_, self).field_3060())) >= 8) {
                right = kept_right;
            } else {
                left = 0;
                right = 0;
            }
        }
        if (global_word(m_, kDigitalPad) == 0) {
            left |= byte_at(m_, profile + 0x40);
            right |= byte_at(m_, profile + 0x60);
        } else {
            skater_at(m_, self).set_field_2c80(0);
        }
        if (skater_at(m_, self).field_2c80() != 0) {
            right = 0;
            left = 0;
        }
        if (skater_at(m_, self).field_2c80() != 0 || (left == 0 && right == 0)) {
            if (absolute(static_cast<std::int8_t>(skater_at(m_, self).field_31a1())) < 0x14) {
                skater_at(m_, self).set_turn_accumulator(0);
            }
        }
        skater_at(m_, self).set_field_2e78(0);
    }

    const std::int32_t stick = static_cast<std::int8_t>(skater_at(m_, self).field_31a1());
    if (absolute(stick) > 0x19 && left == 0 && right == 0) {
        // the analog stick: the accumulator moves toward (limit * stick) / 128 within the limit
        std::int32_t product = mul(limit, stick);
        product = static_cast<std::int32_t>(add(u(product), static_cast<Word>(product >> 31) & 0x7f));
        std::int32_t target = sar(product, 7);
        if (target < -limit) {
            target = -limit;
        }
        if (target > limit) {
            target = limit;
        }
        std::int32_t reach = sar(limit, 12);
        reach -= reach >> 31;
        reach = sar(reach, 1);
        if (skater_at(m_, self).turn_accumulator() < target) {
            skater_at(m_, self).set_field_2e7c(1);
            const std::int32_t gain = mul(sar(minus(target, skater_at(m_, self).turn_accumulator()), 12), step) / reach;
            const std::int32_t moved = plus(skater_at(m_, self).turn_accumulator(), plus(gain, step));
            skater_at(m_, self).set_turn_accumulator(static_cast<std::int32_t>(u(moved)));
            if (moved > target) {
                skater_at(m_, self).set_turn_accumulator(static_cast<std::int32_t>(u(target)));
            }
            const std::int32_t mirror = plus(skater_at(m_, self).turn_accumulator_mirror(), step);
            skater_at(m_, self).set_turn_accumulator_mirror(static_cast<std::int32_t>(u(mirror)));
            if (mirror > target) {
                skater_at(m_, self).set_turn_accumulator_mirror(static_cast<std::int32_t>(u(target)));
            }
        }
        const std::int32_t accumulator = skater_at(m_, self).turn_accumulator();
        if (accumulator > target) {
            skater_at(m_, self).set_field_2e7c(1);
            const std::int32_t loss = mul(sar(minus(accumulator, target), 12), step) / reach;
            const std::int32_t moved = minus(minus(accumulator, loss), step);
            skater_at(m_, self).set_turn_accumulator(static_cast<std::int32_t>(u(moved)));
            if (moved < target) {
                skater_at(m_, self).set_turn_accumulator(static_cast<std::int32_t>(u(target)));
            }
            const std::int32_t mirror = minus(skater_at(m_, self).turn_accumulator_mirror(), step);
            skater_at(m_, self).set_turn_accumulator_mirror(static_cast<std::int32_t>(u(mirror)));
            if (mirror < target) {
                skater_at(m_, self).set_turn_accumulator_mirror(static_cast<std::int32_t>(u(target)));
            }
        }
    } else if (left != 0) {
        const std::int32_t moved = minus(skater_at(m_, self).turn_accumulator(), step);
        skater_at(m_, self).set_field_2e7c(1);
        skater_at(m_, self).set_turn_accumulator(static_cast<std::int32_t>(u(moved)));
        if (moved < -limit) {
            skater_at(m_, self).set_turn_accumulator(static_cast<std::int32_t>(u(-limit)));
        }
        const std::int32_t mirror = minus(skater_at(m_, self).turn_accumulator_mirror(), step);
        if (!(mirror < -limit)) {
            skater_at(m_, self).set_turn_accumulator_mirror(static_cast<std::int32_t>(u(mirror)));
        }
    } else if (right != 0) {
        const std::int32_t moved = plus(skater_at(m_, self).turn_accumulator(), step);
        skater_at(m_, self).set_field_2e7c(1);
        skater_at(m_, self).set_turn_accumulator(static_cast<std::int32_t>(u(moved)));
        if (moved > limit) {
            skater_at(m_, self).set_turn_accumulator(static_cast<std::int32_t>(u(limit)));
        }
        const std::int32_t mirror = plus(step, skater_at(m_, self).turn_accumulator_mirror());
        if (!(mirror > limit)) {
            skater_at(m_, self).set_turn_accumulator_mirror(static_cast<std::int32_t>(u(mirror)));
        }
    } else {
        // no input: the accumulator decays (by a half at speed, a quarter otherwise) and stops below 0x800
        std::int32_t accumulator = skater_at(m_, self).turn_accumulator();
        accumulator = minus(accumulator, framepacing::decay_amount(accumulator, skater_at(m_, self).field_2e78() != 0 ? 1 : 2, [&] { return global_int(m_, kTimeScale); }));
        skater_at(m_, self).set_turn_accumulator(static_cast<std::int32_t>(u(accumulator)));
        if (((u(accumulator) + 0x800) & 0xfffff000u) == 0) {
            skater_at(m_, self).set_turn_accumulator(0);
        }
        std::int32_t drag = sar(skater_at(m_, self).turn_accumulator_mirror(), 4);
        if (drag < 0) {
            if (drag > -0x400) {
                drag = -0x400;
            }
        } else if (drag > 0 && drag < 0x400) {
            drag = 0x400;
        }
        const std::int32_t mirror = minus(skater_at(m_, self).turn_accumulator_mirror(), drag);
        skater_at(m_, self).set_turn_accumulator_mirror(static_cast<std::int32_t>(u(mirror)));
        if (((u(mirror) + 0x800) & 0xfffff000u) == 0) {
            skater_at(m_, self).set_turn_accumulator_mirror(0);
        }
    }
    skater_at(m_, self).set_turn_accumulator_mirror(static_cast<std::int32_t>(static_cast<Word>(skater_at(m_, self).turn_accumulator())));
    thiscall(kUpdateHeading, self);

    // ---- the lean word of state 1 ----
    set_word_at(m_, self + 0x2e84, 0);
    if (skater_at(m_, self).physics_state() != 1) {
        return;
    }
    const Word profile = skater_at(m_, self).action_record_bank().address;
    if (byte_at(m_, profile + 0x10) != 0 || byte_at(m_, profile + 0x20) != 0) {
        return;
    }
    const std::int32_t lean_step = sar(mul(global_int(m_, kTimeScale), 20), 8);
    if (byte_at(m_, profile + 0xa0) != 0 || static_cast<std::int8_t>(skater_at(m_, self).field_31a2()) < -40) {
        set_word_at(m_, self + 0x2e84, u(minus(0, lean_step)));
    }
    if (byte_at(m_, profile + 0xb0) != 0 || static_cast<std::int8_t>(skater_at(m_, self).field_31a2()) > 0x28) {
        set_word_at(m_, self + 0x2e84, u(lean_step));
    }
    if (global_word(m_, kDigitalPad) != 0) {
        return;
    }
    // which side of the skater's right vector the camera is on turns the lean around
    const Word camera = skater_at(m_, self).camera().address;
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, kSide + k, camera != 0 ? word_at(m_, self + 0x08 + k) - word_at(m_, camera + 0x08 + k) : word_at(m_, self + 0x08 + k));
    }
    if (camera != 0 && freelook::state().moved) {   // the free look is visual only: while it turns this camera the lean asks where the game's own camera would be (free_look.hpp)
        const freelook::Look look = freelook::look(half_at(m_, camera + 0xc8));
        if (!look.offsets_home()) {   // (not while only the quick steering tapers off: the picture is the game's then)
            const freelook::Quat render{int_at(m_, camera + 0x448), int_at(m_, camera + 0x44c), int_at(m_, camera + 0x450), int_at(m_, camera + 0x454)};
            const freelook::Vec3 offset{int_at(m_, camera + 0x08) - int_at(m_, camera + 0x3c0), int_at(m_, camera + 0x0c) - int_at(m_, camera + 0x3c4),
                                        int_at(m_, camera + 0x10) - int_at(m_, camera + 0x3c8)};
            freelook::Vec3 stock;
            freelook::stock_offset(render, global_half(m_, 0x00524a94), look, offset, stock, [&](std::int32_t a) { return math_.cos_q12(a); },
                                   [&](std::int32_t a) { return math_.sin_q12(a); });
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kSide + 4 * k, word_at(m_, self + 0x08 + 4 * k) - (word_at(m_, camera + 0x3c0 + 4 * k) + static_cast<Word>(stock[k])));
            }
        }
    }
    if (math_.dot_q12(self + 0x30f4, kSide) > 0) {
        set_word_at(m_, self + 0x2e84, u(minus(0, int_at(m_, self + 0x2e84))));
    }
}

}  // namespace opentony::runtime::skaterkit
