#include "skater_views.hpp"

#include "frame_pacing.hpp"

#include <stdexcept>

// Milestone 10g, session 20: the trick script interpreter (trickscript.cpp, 0x004be450 TrickScript_Execute, thiscall on the skater), written from the machine code; the Phase 4 model over a byte image
// (trick_script.cpp) was the guide. The cursor at +0x29cc is read a byte at a time (the opcode, 0x01..0x59 through the table at 0x004bf550; 0x04-0x06, 0x08, 0x00 and anything above 0x59 are the undefined
// command, which logs, asserts and clears +0x2c68) and the commands run until one returns to the frame loop: 0x07 ends the script, 0x16 waits with the cursor past it, 0x1a and 0x2c wait on the opcode
// (cursor - 1), the frame gates 0x0d / 0x10 / 0x11 wait on the whole command (cursor - 3). Branch targets are offsets from TricksBinaryBase (0x0056a894). The reads of the stream (0x004be3b0,
// 0x004be3c0), the frame operand (0x004be3e0), the history query (0x004924f0), the dot product, the root, the normalisation, the angle matrix, the GTE's rtir, the vector helpers and the basis from
// the matrix (0x0049c7d0) run here; the animation requests, the trick score, the bail, the state change, the board, the sound, the pad, Rnd and the gap are services. The retail stack locals (the
// angle vector and its matrix of 0x21, the step of 0x42, the scaled up vector of 0x49) have a scratch address of their own.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }
inline std::int32_t plus(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) + static_cast<Word>(b)); }
inline std::int32_t sx16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }
inline std::int32_t sx8(Word v) { return static_cast<std::int8_t>(static_cast<std::uint8_t>(v)); }

constexpr Word kTricksBase = 0x0056a894;          // TricksBinaryBase: where TRICKS.BIN was loaded
constexpr Word kFrameClock = 0x005685f4;
constexpr Word kTimeScale = 0x0056865c;
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
const std::string kScriptFile = "H:\\TonyHawk\\Pc2\\trickscript.cpp";   // 0x00541724

constexpr Word kRailFlipText = 0x00541708;        // "RailFlip must be 1 or -1"
constexpr Word kMotorText = 0x005416e4;           // "TRICK_VIBRATE Motor (%d) not 0 or 1"
constexpr Word kPowerText = 0x005416c0;           // "TRICK_VIBRATE Invalid Power (%d) "
constexpr Word kLipSoundText = 0x005416a0;        // "TRICK_LIPSND %d,%d,%d,%d (%d)\n"
constexpr Word kNestedGosubText = 0x0054168c;     // "Double nested Gosub"
constexpr Word kNoReturnText = 0x0054166c;        // "No return address from Gosub"
constexpr Word kUndefinedBytesText = 0x00541650;  // "%d %d %d %d (%d) %d %d %d\n"
constexpr Word kUndefinedText = 0x00541620;       // "Undefined Scrip command or script parse error"

constexpr Word kRequestStart = 0x004903f0;        // Animation_RequestStart(animation, source line) (thiscall)
constexpr Word kRequestFull = 0x00490480;         // Animation_RequestFull(animation, from, to, blend, source line) (thiscall)
constexpr Word kStateTransition = 0x004900b0;     // CBruce__StateTransition(state, source line) (thiscall)
constexpr Word kPointTimer = 0x0048f720;          // TrickScore_AdvancePointTimer(points) (thiscall)
constexpr Word kBail = 0x004904d0;                // CBruce__Bail(script, cause) (thiscall)
constexpr Word kTakeOff = 0x0048f5f0;             // CBruce__Trick_TakeOff (thiscall)
constexpr Word kLand = 0x0048f8f0;                // CBruce__Trick_Land (thiscall)
constexpr Word kApplyGap = 0x0048f690;            // Gap_ApplyDefinitionState(name, points, flag, index) (thiscall)
constexpr Word kBoardOn = 0x0046c630;             // (thiscall)
constexpr Word kBoardOff = 0x0046c5f0;            // (thiscall)
constexpr Word kSound = 0x004ad620;               // SFX_PlayX(sound, volume, pan) (cdecl)
constexpr Word kVibrate = 0x0048ffc0;             // CBruce__Vibrate(pad, motor, power) (thiscall)
constexpr Word kCompleteRailGap = 0x00490c70;     // Skater_CompleteRailGap (thiscall)
constexpr Word kFn46dd20 = 0x0046dd20;            // (thiscall, two arguments: 0, 0xfa)
constexpr Word kFn431260 = 0x00431260;            // (thiscall on the skater's record at +0x2c14: +0x30b0, 1)
constexpr Word kRandom = 0x004c9340;              // Rnd(n) -> 0..n-1 (cdecl)

// the retail stack locals, at a scratch address no native the interpreter calls uses
constexpr Word kLocals = pcplatform::kScratchBase + 0x4200;
constexpr Word kAngles = kLocals + 0x000;         // 0x21: (0, angle, 0) as three shorts
constexpr Word kTurnMatrix = kLocals + 0x010;     // 0x21: its matrix record (0x20 bytes)
constexpr Word kStep = kLocals + 0x030;           // 0x42: the right vector without its y, normalised

constexpr std::size_t kCommandBudget = 1000000;   // retail loops for ever on a script that never returns; the port stops

}  // namespace

// 0x004924f0 (thiscall): 0 when the history ring (32 entries of 8 bytes from +0x2a14, the next one at +0x2b14) holds a release of the selected button (+0x2a0c) newer than the selection's time
// (+0x2a10), searched from the newest entry back; else 1
Word SkaterKit::trick_history_clear(Word self) {
    const Word write = skater_at(m_, self).field_2b14();
    std::int32_t index = static_cast<std::int32_t>(write) - 1;
    if (index < 0) {
        index = 0x1f;
    }
    while (static_cast<Word>(index) != write) {
        const Word entry = self + 0x2a14 + 8 * static_cast<Word>(index);
        if (byte_at(m_, entry) == skater_at(m_, self).field_2a0c() && byte_at(m_, entry + 1) == 0 && word_at(m_, entry + 4) > skater_at(m_, self).field_2a10()) {
            return 0;
        }
        --index;
        if (index < 0) {
            index = 0x1f;
        }
    }
    return 1;
}

// 0x004be3e0 (thiscall, one argument): a frame number of the script, in the animation's range: 0x2711 is the end frame (+0x101); a negative one counts from the end frame, backwards when the animation
// runs backwards (+0x100), held to 0 and to the last frame (+0x106 - 1). Only the low 16 bits matter to the callers (the end frame's case leaves the upper half of EAX as it was).
std::int32_t SkaterKit::trick_resolve_frame(Word self, std::int32_t operand) {
    if (operand == 0x2711) {
        return sx8(skater_at(m_, self).field_101());
    }
    const std::int32_t last = static_cast<std::int32_t>(skater_at(m_, self).field_106()) - 1;
    if (operand >= 0) {
        return operand > last ? last : operand;
    }
    const std::int32_t end = sx8(skater_at(m_, self).field_101());
    if (sx8(skater_at(m_, self).field_100()) > 0) {
        const std::int32_t frame = plus(end, operand);
        return frame < 0 ? 0 : frame;
    }
    const std::int32_t frame = plus(end, -operand);
    return frame > last ? last : frame;
}

// 0x004be450 (thiscall)
void SkaterKit::trick_script_execute(Word self) {
    const Word cursor = self + 0x29cc;
    const auto at = [&] { return word_at(m_, cursor); };
    const auto byte = [&]() -> Word {
        const Word p = at();
        set_word_at(m_, cursor, p + 1);
        return byte_at(m_, p);
    };
    const auto word = [&]() -> std::int32_t {
        const Word p = at();
        set_word_at(m_, cursor, p + 2);
        return sx16(half_at(m_, p));
    };
    const auto target = [&](std::int32_t relative) { return global_word(m_, kTricksBase) + u(relative); };
    const auto jump = [&](std::int32_t relative) { set_word_at(m_, cursor, target(relative)); };
    const auto branch_if = [&](bool condition) {
        const std::int32_t relative = word();
        if (condition) {
            jump(relative);
        }
    };
    const auto skip_string = [&] {
        Word p = at();
        while (byte_at(m_, p) != 0) {
            ++p;
        }
        set_word_at(m_, cursor, p + 1);
    };
    const auto stamp = [&](Word line) {
        set_global_word(m_, kAssertionLine, line);
        set_string(kAssertionFile, kScriptFile);
    };
    const auto field = [&](Word offset) { return word_at(m_, self + offset); };
    const auto set = [&](Word offset, Word value) { set_word_at(m_, self + offset, value); };
    const auto frame_before = [&](std::int32_t resolved) {
        // the animation has not reached the frame yet (in its direction) and has not finished
        const std::int32_t direction = sx8(skater_at(m_, self).field_100());
        return mul(sx16(skater_at(m_, self).field_f4()), direction) < mul(sx16(u(resolved)), direction) && skater_at(m_, self).field_107() == 0;
    };
    const auto back = [&](Word bytes) { set_word_at(m_, cursor, at() - bytes); };

    for (std::size_t commands = 0;; ++commands) {
        if (commands > kCommandBudget) {
            throw std::runtime_error("TrickScript_Execute: the script did not return to the frame loop");
        }
        const Word opcode = byte();
        switch (opcode) {
        case 0x01:
            skater_at(m_, self).set_field_f6(static_cast<std::uint16_t>(u(word())));
            skater_at(m_, self).set_field_f4(0);
            break;
        case 0x02:
            thiscall(kRequestStart, self, {skater_at(m_, self).field_f6(), 0xd8});
            break;
        case 0x03: {
            skater_at(m_, self).set_field_100(static_cast<std::uint8_t>(u(-sx8(skater_at(m_, self).field_100()))));
            const Word start = skater_at(m_, self).field_114();
            skater_at(m_, self).set_field_114(static_cast<std::uint16_t>(u(sx8(skater_at(m_, self).field_101()))));
            skater_at(m_, self).set_field_101(static_cast<std::uint8_t>(start));
            skater_at(m_, self).set_field_107(0);
            break;
        }
        case 0x07:
            set(0x2c68, 0);
            set(0x29c8, 0);
            set(0x2c6c, 1);
            return;
        case 0x09: {
            const std::int32_t amount = word();
            skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(skater_at(m_, self).field_f4() + u(mul(sx8(skater_at(m_, self).field_100()), amount))));
            const std::int32_t start = sx16(skater_at(m_, self).field_114());
            const auto frame = [&] { return sx16(skater_at(m_, self).field_f4()); };
            const std::int32_t end = sx8(skater_at(m_, self).field_101());
            if (sx8(skater_at(m_, self).field_100()) > 0) {
                if (frame() < start) {
                    skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(u(start)));
                }
                if (frame() > end) {
                    skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(u(end)));
                }
            } else {
                if (frame() > start) {
                    skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(u(start)));
                }
                if (frame() < end) {
                    skater_at(m_, self).set_field_f4(static_cast<std::uint16_t>(u(end)));
                }
            }
            break;
        }
        case 0x0a:
            skater_at(m_, self).set_field_29c0(static_cast<std::uint16_t>(u(word())));
            break;
        case 0x0b:
            set(0x29c4, at());
            skip_string();
            break;
        case 0x0c: {
            Word flag = 1;
            if (field(0x30b8) != 4) {
                if (field(0x3064) == 0) {
                    thiscall(kTakeOff, self);
                }
                flag = field(0x2dd4) != 0 ? 1 : 0;
            }
            thiscall(kApplyGap, self, {field(0x29c4), u(sx16(skater_at(m_, self).field_29c0())), flag, 0xffffffffU});
            set(0x2c70, 1);
            break;
        }
        case 0x0d:
            if (frame_before(trick_resolve_frame(self, word()))) {
                back(3);
                return;
            }
            break;
        case 0x0e:
            skater_at(m_, self).set_field_2c64(static_cast<std::uint16_t>(u(word())));
            break;
        case 0x0f:
            for (Word k = 0; k < 12; k += 4) {
                set(0x4c + k, u(word()) << 12);
            }
            break;
        case 0x10:
            if (frame_before(trick_resolve_frame(self, word())) && trick_history_clear(self) != 0) {
                back(3);
                return;
            }
            break;
        case 0x11: {
            const std::int32_t points = word();
            if (trick_history_clear(self) != 0) {
                thiscall(kPointTimer, self, {u(points)});
                back(3);
                return;
            }
            break;
        }
        case 0x12: {
            const std::int32_t relative = word();
            if (trick_history_clear(self) != 0) {
                jump(relative);
            }
            break;
        }
        case 0x13:
            skater_at(m_, self).set_field_f8(2);
            break;
        case 0x14:
            skater_at(m_, self).set_field_f8(0);
            break;
        case 0x15:
            jump(word());
            break;
        case 0x16:
            return;
        case 0x17: {
            const std::int32_t a = word();
            const std::int32_t b = word();
            const std::int32_t seconds = s(field(0x2f54)) >> 12;
            const std::int32_t points = plus(mul(mul(seconds, seconds), b) / 1000, framepacing::script_rate_points(s(global_word(m_, kTimeScale)), a));   // retail: mul(scale, a) >> 8
            thiscall(kPointTimer, self, {u(points)});
            break;
        }
        case 0x18: {
            const std::int32_t resolved = trick_resolve_frame(self, word());
            skater_at(m_, self).set_field_fa(static_cast<std::uint16_t>(skater_at(m_, self).field_f4()));
            skater_at(m_, self).set_field_fc(static_cast<std::uint16_t>(u(resolved)));
            skater_at(m_, self).set_field_fe(static_cast<std::uint16_t>(global_half(m_, kFrameClock)));
            skater_at(m_, self).set_field_f8(3);
            break;
        }
        case 0x19:
            set(0x29d0, target(word()));
            break;
        case 0x2a:
            set(0x29d4, target(word()));
            break;
        case 0x1a:
            if (field(0x2e90) != 0) {
                back(1);
                return;
            }
            break;
        case 0x1b: {
            const std::int32_t flip = word();
            set(0x2ed8, u(flip));
            stamp(0x1fc);
            check(flip == 1 || flip == -1, kRailFlipText);
            break;
        }
        case 0x1c:
            skater_at(m_, self).set_field_f8(4);
            break;
        case 0x1d:
            set(0x108, u(s(u(word()) << 17) / 60));
            break;
        case 0x1e:
            thiscall(kBail, self, {0, 0});
            break;
        case 0x1f:
            set(0x29ec, u(word()));
            break;
        case 0x20:
            set(0x2c68, 0);
            break;
        case 0x21: {
            // the orientation turned about its up axis: the matrix of (0, angle, 0) on its right, column by column through the GTE, and the basis from it
            const Word angle = u(word()) & 0xfff;
            set_global_half(m_, kAngles, 0);
            set_global_half(m_, kAngles + 2, angle);
            set_global_half(m_, kAngles + 4, 0);
            math_.rotation_from_angles(kAngles, kTurnMatrix);
            multiply_columns(self + 0x2e58, kTurnMatrix, self + 0x2e58);
            fru_from_rotation_matrix(self);
            break;
        }
        case 0x22:
            set(0xd8, field(0xd8) ^ 2U);
            break;
        case 0x23:
            set(0x2f00, u(word()));
            break;
        case 0x24: branch_if(field(0x2be8) != 0); break;
        case 0x25: branch_if(field(0x2bec) != 0); break;
        case 0x26: branch_if(field(0x2bf0) != 0); break;
        case 0x27: branch_if(field(0x2bf4) != 0); break;
        case 0x28: branch_if(field(0x2bf8) != 0); break;
        case 0x41: branch_if(field(0x2bfc) != 0); break;
        case 0x53: branch_if(field(0x29e8) != 0); break;
        case 0x29:
            set(0x2c0c, u(word()));
            break;
        case 0x2b: {
            const Word slot = 4 * u(word());
            const std::int32_t amount = word();
            set(0x2ca0 + slot, u(amount));
            const std::int32_t rate = word();
            set(0x2c94 + slot, u(rate));
            if (rate == 0) {
                set(0x2ca0 + slot, 0);
                set(0x2cac + slot, 0);
            }
            break;
        }
        case 0x2c:
            if (field(0x2ca0) != 0 || field(0x2ca4) != 0 || field(0x2ca8) != 0) {
                back(1);
                return;
            }
            break;
        case 0x2d: {
            const std::int32_t value = word();
            set(0x2e2c, u(value < 0 ? -value : value));
            break;
        }
        case 0x2e:
            set(0x2c10, u(word()));
            break;
        case 0x2f:
            branch_if(field(0x30b8) == 1 || field(0x30b8) == 2);
            break;
        case 0x30:
            thiscall(kFn46dd20, self, {0, 0xfa});
            break;
        case 0x31:
            set(0x2f64, 0);
            set(0x2c10, 0);
            set(0x2f2c, 4);
            if (field(0x2dd8) != 0 && byte_at(m_, field(0x2ccc) + 0x10) != 0) {
                thiscall(kRequestFull, self, {8, 0, 0x1a, 0x13, 0x3b4});
            } else {
                set(0x2dd8, 0);
                thiscall(kRequestStart, self, {0, 0x3b9});
            }
            set(0x54, 0);
            set(0x50, 0);
            set(0x2f64, 0);
            set(0x4c, 0);
            thiscall(kBoardOn, self);
            break;
        case 0x32:
            thiscall(kBoardOn, self);
            break;
        case 0x33:
            thiscall(kBoardOff, self);
            break;
        case 0x34: {
            const std::int32_t relative = word();
            if (field(0x2c04) != 0) {
                jump(relative);
            }
            set(0x2c04, 0);
            break;
        }
        case 0x35:
            set(0x2c78, 1);
            break;
        case 0x36:
            engine(kSound, {u(word()), 0x2000, 0});
            break;
        case 0x37: {
            const std::int32_t id = word();
            const std::int32_t volume = word();
            engine(kSound, {u(id), u(volume), 0});
            break;
        }
        case 0x38: {
            const std::int32_t id = word();
            const std::int32_t volume = word();
            const std::int32_t pan = word();
            engine(kSound, {u(id), u(volume), u(pan)});
            break;
        }
        case 0x39: {
            // a jump through a table of `count` words, the entry picked by Rnd
            const std::int32_t count = word();
            const Word pick = engine_value(kRandom, {u(count)});
            set_word_at(m_, cursor, at() + 2 * pick);
            jump(word());
            break;
        }
        case 0x3a: {
            const Word fakie = (field(0xd8) >> 1) & 1U;
            branch_if((fakie ^ skater_at(m_, self).selectable()) != 0);
            break;
        }
        case 0x43:
            branch_if((byte_at(m_, self + 0xd8) & 2U) != 0);
            break;
        case 0x3b:
            set(0x2c80, 1);
            break;
        case 0x3c:
            set(0x2c80, 0);
            break;
        case 0x3d: {
            // gosub: the return address is the command after this one
            const std::int32_t relative = word();
            stamp(0x444);
            check(field(0x29d8) == 0, kNestedGosubText);
            set(0x29d8, at());
            jump(relative);
            break;
        }
        case 0x3e:
            stamp(0x450);
            check(field(0x29d8) != 0, kNoReturnText);
            set_word_at(m_, cursor, field(0x29d8));
            set(0x29d8, 0);
            break;
        case 0x3f:
            skater_at(m_, self).set_field_29f2(static_cast<std::uint16_t>(u(word())));
            break;
        case 0x40:
            skater_at(m_, self).set_field_29f0(static_cast<std::uint16_t>(u(word())));
            break;
        case 0x44:
            skater_at(m_, self).set_field_29f4(static_cast<std::uint16_t>(u(word())));
            break;
        case 0x45: {
            const std::int32_t raw = word();
            const std::int32_t pad = sx16(u(raw < 0 ? -raw : raw));
            const std::int32_t motor = word();
            const std::int32_t power = word();
            stamp(0x3a7);
            check(motor == 0 || motor == 1, kMotorText);
            stamp(0x3a8);
            check(power >= 0 && power <= 0xff, kPowerText);
            thiscall(kVibrate, self, {u(pad), u(motor), u(power)});
            break;
        }
        case 0x46:
            thiscall(kFn431260, self + 0x2c14, {field(0x30b0), 1});
            break;
        case 0x47: {
            // a jump when the speed (the root of the velocity's square, times 64 >> 12) is above the threshold
            const std::int32_t threshold = word();
            const std::int32_t relative = word();
            const Word squared = u(math_.dot_q12(self + 0x4c, self + 0x4c));
            const std::int32_t speed = s(sqrt_truncated(squared) << 6) >> 12;
            if (speed > threshold) {
                jump(relative);
            }
            break;
        }
        case 0x48: {
            const std::int32_t resolved = trick_resolve_frame(self, word());
            const std::int32_t relative = word();
            const std::int32_t direction = sx8(skater_at(m_, self).field_100());
            if (mul(sx16(skater_at(m_, self).field_f4()), direction) >= mul(sx16(u(resolved)), direction) || skater_at(m_, self).field_107() != 0) {
                jump(relative);
            }
            break;
        }
        case 0x49:
            // a grind restarted from the rail's saved point: the timers and the velocity cleared (its y reversed), state 8, the position at +0x2ea8 moved 0x1e times the up vector
            set(0x2f58, u(word()));
            set(0x2f54, 0);
            set(0x4c, 0);
            set(0x50, u(-s(field(0x50))));
            set(0x54, 0);
            set(0x2e90, 0);
            set(0x30b8, 8);
            for (Word k = 0; k < 12; k += 4) {
                set(0x08 + k, field(0x2ea8 + k));
            }
            for (Word k = 0; k < 12; k += 4) {
                set(0x08 + k, field(0x08 + k) + u(mul(0x1e, s(field(0x310c + k)))));
            }
            thiscall(kCompleteRailGap, self);
            set(0x2c74, 0);
            set(0x2c68, 0);
            break;
        case 0x4a:
            if (field(0x2bfc) != 0) {
                set(0x2ddc, 0);
                thiscall(kStateTransition, self, {0, 0x26c});
                set(0x2c70, 1);
                thiscall(kLand, self);
            } else {
                set(0x2c70, 1);
                thiscall(kStateTransition, self, {1, 0x275});
            }
            set(0x50, u((mul(s(field(0x2f54)) >> 12, 3) / 2 + 0x28)) << 12);
            break;
        case 0x4b: {
            const std::int32_t relative = word();
            if (math_.dot_q12(self + 0x30f4, self + 0x4c) < -0x800) {
                jump(relative);
            }
            break;
        }
        case 0x4c: {
            const Word wanted = byte();
            branch_if(wanted == field(0x30b8));
            break;
        }
        case 0x4d:
            set(0x2c7c, 1);
            break;
        case 0x4e: {
            const std::int32_t relative = word();
            if (field(0x31f8) != 0) {
                set(0x31f8, 0);
                jump(relative);
            }
            break;
        }
        case 0x4f: {
            // the lip sound: one of four, by the lip's kind (+0x2e8c); logged when +0x29ec asks
            std::int32_t ids[4];
            for (auto& id : ids) {
                id = word();
            }
            const Word pick = field(0x2e8c);
            if (pick > 3) {
                throw std::runtime_error("TrickScript_Execute: TRICK_LIPSND index out of range (retail reads its stack)");
            }
            engine(kSound, {u(ids[pick]), 0x2000, 0});
            if (field(0x29ec) != 0) {
                log_line(kLipSoundText);
            }
            break;
        }
        case 0x50:
            skip_string();
            break;
        case 0x51:
        case 0x58:
            static_cast<void>(word());
            break;
        case 0x52:
            set(0x2dd4, byte());
            if (field(0x306c) == 0) {
                set(0x3078, 0);
            }
            break;
        case 0x54:
            set(0x2dfc, u(word()));
            break;
        case 0x55:
            set(0x31f8, byte());
            break;
        case 0x56:
            if (field(0x2dd8) != 0) {
                thiscall(kRequestFull, self, {8, 0x13, 0x1a, 0x13, 0x50f});
            } else {
                thiscall(kRequestStart, self, {0, 0x513});
            }
            break;
        case 0x57:
            set(0x2c08, 1);
            break;
        case 0x59:
            set(0x3210, byte());
            break;
        case 0x42: {
            // a step along the right vector (its y dropped, normalised) times the distance
            const std::int32_t distance = word();
            set_global_word(m_, kStep, field(0x3100));
            set_global_word(m_, kStep + 4, 0);
            set_global_word(m_, kStep + 8, field(0x3108));
            normalize_fixed12(kStep);
            for (Word k = 0; k < 12; k += 4) {
                set(0x08 + k, field(0x08 + k) + u(mul(s(word_at(m_, kStep + k)), distance)));
            }
            break;
        }
        default:
            // the undefined command: the eight bytes around the cursor logged, an assertion, the script stopped (+0x2c68) and the loop goes on
            log_line(kUndefinedBytesText);
            stamp(0x533);
            check(false, kUndefinedText);
            set(0x2c68, 0);
            break;
        }
    }
}

}  // namespace opentony::runtime::skaterkit
