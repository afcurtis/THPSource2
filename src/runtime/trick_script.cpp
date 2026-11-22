// Native TrickScript_Execute (0x004be450), Phase 4.7a. The decompile was checked against
// the disassembly of the jump table at 0x004bf550: opcodes 0x04-0x06, 0x08 and >= 0x5a
// take the undefined-command path, 0x16 returns to the frame loop with the cursor already
// past the opcode, 0x3a branches when bit 1 of +0xd8 differs from byte +0x163, 0x43
// branches when bit 1 of +0xd8 is set, and 0x31/0x49 fall through into the tail of 0x32/0x20.
#include "trick_script.hpp"

#include "fixed_math.hpp"

#include <cmath>

namespace opentony::runtime {
namespace {

// Skater offsets (raw retail names).
constexpr std::size_t kPosition = 0x08;
constexpr std::size_t kAnimFrame = 0xf4;
constexpr std::size_t kAnimId = 0xf6;
constexpr std::size_t kAnimMode = 0xf8;
constexpr std::size_t kAnimTweenStart = 0xfa;
constexpr std::size_t kAnimTweenEnd = 0xfc;
constexpr std::size_t kAnimTweenClock = 0xfe;
constexpr std::size_t kAnimDirection = 0x100;
constexpr std::size_t kAnimEndFrame = 0x101;
constexpr std::size_t kAnimFrameCount = 0x106;
constexpr std::size_t kAnimFinished = 0x107;
constexpr std::size_t kAnimRate = 0x108;
constexpr std::size_t kAnimStartFrame = 0x114;
constexpr std::size_t kFlags = 0xd8;
constexpr std::size_t kMotion = 0x4c;
constexpr std::size_t kCursor = 0x29cc;

[[nodiscard]] std::int32_t wrap(std::uint32_t value) noexcept { return static_cast<std::int32_t>(value); }
[[nodiscard]] std::uint32_t u(std::int32_t value) noexcept { return static_cast<std::uint32_t>(value); }
[[nodiscard]] std::int32_t sar(std::int32_t value, unsigned count) noexcept {
    return value >= 0 ? static_cast<std::int32_t>(static_cast<std::uint32_t>(value) >> count)
                      : static_cast<std::int32_t>(~(~static_cast<std::uint32_t>(value) >> count));
}
[[nodiscard]] std::int32_t mul(std::int32_t a, std::int32_t b) noexcept { return wrap(u(a) * u(b)); }
[[nodiscard]] std::int32_t add(std::int32_t a, std::int32_t b) noexcept { return wrap(u(a) + u(b)); }
[[nodiscard]] std::int32_t divide(std::int32_t a, std::int32_t b) noexcept { return a / b; }  // b is a nonzero constant

// Math_NormalizeFixed12 (0x00465f60): scale into +-0x6882, then divide by the truncated
// square root of the sum of squares (FUN_004f53b0, computed in single precision by the
// game's x87 setup); a zero length yields (0x1000, 0, 0).
void normalize_q12(std::array<std::int32_t, 3>& vector) {
    const auto too_big = [](std::int32_t value) { return value > 0x6882 || value < -0x6882; };
    while (too_big(vector[0]) || too_big(vector[1]) || too_big(vector[2])) {
        for (auto& component : vector) {
            component = sar(component, 1);
        }
    }
    std::int32_t sum = 0;
    for (const std::int32_t component : vector) {
        const auto narrowed = static_cast<std::int16_t>(component);
        sum = add(sum, mul(narrowed, narrowed));
    }
    const std::int32_t length = sum < 1 ? 0 : static_cast<std::int32_t>(static_cast<float>(std::sqrt(static_cast<double>(sum))));
    if (length == 0) {
        vector = {0x1000, 0, 0};
        return;
    }
    for (auto& component : vector) {
        component = wrap(u(component) << 12) / length;
    }
}

FixedPosition vector_at(const RetailImage& image, std::size_t offset) {
    return {image.s32(offset), image.s32(offset + 4), image.s32(offset + 8)};
}

class Machine final {
public:
    Machine(RetailImage& skater, const TrickWorld& world, TrickServices& services)
        : s_(skater), w_(world), svc_(services) {}

    TrickExecuteResult run();

private:
    [[nodiscard]] std::uint32_t cursor() const { return s_.u32(kCursor); }
    void set_cursor(std::uint32_t value) { s_.set32(kCursor, value); }
    [[nodiscard]] std::uint8_t peek(std::uint32_t address) const {
        const std::uint32_t offset = address - w_.tricks_base;
        if (address < w_.tricks_base || offset >= w_.tricks_bin.size()) {
            throw TrickScriptFault("trick script cursor left TRICKS.BIN");
        }
        return w_.tricks_bin[offset];
    }
    std::uint8_t byte() {
        const std::uint32_t at = cursor();
        const std::uint8_t value = peek(at);
        set_cursor(at + 1);
        return value;
    }
    // TrickScript_ReadWord (0x004be3c0): a signed little-endian word.
    std::int32_t word() {
        const std::uint32_t at = cursor();
        const auto value = static_cast<std::int16_t>(static_cast<std::uint16_t>(peek(at) | (peek(at + 1) << 8)));
        set_cursor(at + 2);
        return value;
    }
    void jump(std::int32_t relative) { set_cursor(u(add(wrap(w_.tricks_base), relative))); }
    void skip_string() {
        std::uint32_t at = cursor();
        while (peek(at) != 0) {
            ++at;
        }
        set_cursor(at + 1);
    }
    std::uint32_t service(TrickService which, std::initializer_list<std::uint32_t> args) {
        TrickServiceCall call;
        call.service = which;
        std::size_t i = 0;
        for (const std::uint32_t arg : args) {
            call.args.at(i++) = arg;
        }
        return svc_.call(s_, call);
    }
    // M3dUtils_InBetween only logs; a passing check is invisible, a failing one is reported.
    void assertion(bool condition) {
        if (!condition) {
            service(TrickService::Assert, {0U});
        }
    }
    [[nodiscard]] std::uint32_t sx(std::int32_t value) const noexcept { return u(value); }

    void branch_if(bool condition, std::int32_t relative) {
        if (condition) {
            jump(relative);
        }
    }
    // Opcodes with a word operand that branch when a skater word is nonzero.
    void branch_if_word(std::size_t offset) {
        const std::int32_t relative = word();
        branch_if(s_.u32(offset) != 0, relative);
    }
    [[nodiscard]] bool frame_gate(std::int32_t target) const {
        const std::int32_t direction = s_.s8(kAnimDirection);
        return mul(s_.s16(kAnimFrame), direction) < mul(static_cast<std::int16_t>(target), direction)
            && s_.u8(kAnimFinished) == 0;
    }

    RetailImage& s_;
    const TrickWorld& w_;
    TrickServices& svc_;
};

TrickExecuteResult Machine::run() {
    TrickExecuteResult result;
    while (true) {
        if (++result.commands > kTrickCommandBudget) {
            throw TrickScriptFault("trick script did not yield");
        }
        const std::uint8_t opcode = byte();
        if (w_.opcode_counts != nullptr) {
            ++(*w_.opcode_counts)[opcode];
        }
        switch (opcode) {
        case 0x01:
            s_.set16(kAnimId, static_cast<std::uint32_t>(word()));
            s_.set16(kAnimFrame, 0);
            break;
        case 0x02:
            service(TrickService::AnimStart, {u(s_.s16(kAnimId)), 0xd8});
            break;
        case 0x03: {
            s_.set8(kAnimDirection, u(-s_.s8(kAnimDirection)));
            const std::uint16_t start = s_.u16(kAnimStartFrame);
            s_.set16(kAnimStartFrame, u(s_.s8(kAnimEndFrame)));
            s_.set8(kAnimEndFrame, start);
            s_.set8(kAnimFinished, 0);
            break;
        }
        case 0x07:
            s_.set32(0x2c68, 0);
            s_.set32(0x29c8, 0);
            s_.set32(0x2c6c, 1);
            result.finished = true;
            return result;
        case 0x09: {
            const std::int32_t amount = word();
            s_.set16(kAnimFrame, u(add(s_.s16(kAnimFrame), mul(s_.s8(kAnimDirection), amount))));
            const std::int32_t start = s_.s16(kAnimStartFrame);
            if (s_.s8(kAnimDirection) < 1) {
                if (start < s_.s16(kAnimFrame)) {
                    s_.set16(kAnimFrame, u(start));
                }
                if (s_.s16(kAnimFrame) < s_.s8(kAnimEndFrame)) {
                    s_.set16(kAnimFrame, u(s_.s8(kAnimEndFrame)));
                }
            } else {
                if (s_.s16(kAnimFrame) < start) {
                    s_.set16(kAnimFrame, u(start));
                }
                if (s_.s8(kAnimEndFrame) < s_.s16(kAnimFrame)) {
                    s_.set16(kAnimFrame, u(s_.s8(kAnimEndFrame)));
                }
            }
            break;
        }
        case 0x0a:
            s_.set16(0x29c0, u(word()));
            break;
        case 0x0b:
            s_.set32(0x29c4, cursor());
            skip_string();
            break;
        case 0x0c: {
            std::uint32_t flag = 1;
            if (s_.s32(0x30b8) != 4) {
                if (s_.s32(0x3064) == 0) {
                    service(TrickService::TakeOff, {});
                }
                flag = s_.s32(0x2dd4) == 0 ? 0 : 1;
            }
            service(TrickService::GapApply, {s_.u32(0x29c4), u(s_.s16(0x29c0)), flag, 0xffffffffU});
            s_.set32(0x2c70, 1);
            break;
        }
        case 0x0d: {
            const std::int32_t operand = word();
            if (frame_gate(trick_resolve_frame_operand(s_, operand))) {
                set_cursor(cursor() - 3);
                result.yielded = true;
                return result;
            }
            break;
        }
        case 0x0e:
            s_.set16(0x2c64, u(word()));
            break;
        case 0x0f:
            s_.set32(kMotion, u(word()) << 12);
            s_.set32(kMotion + 4, u(word()) << 12);
            s_.set32(kMotion + 8, u(word()) << 12);
            break;
        case 0x10: {
            const std::int32_t operand = word();
            if (frame_gate(trick_resolve_frame_operand(s_, operand)) && trick_history_query(s_)) {
                set_cursor(cursor() - 3);
                result.yielded = true;
                return result;
            }
            break;
        }
        case 0x11: {
            const std::int32_t points = word();
            if (trick_history_query(s_)) {
                service(TrickService::PointTimer, {u(points)});
                set_cursor(cursor() - 3);
                result.yielded = true;
                return result;
            }
            break;
        }
        case 0x12: {
            const std::int32_t relative = word();
            branch_if(trick_history_query(s_), relative);
            break;
        }
        case 0x13:
            s_.set8(kAnimMode, 2);
            break;
        case 0x14:
            s_.set8(kAnimMode, 0);
            break;
        case 0x15:
            jump(word());
            break;
        case 0x16:
            result.yielded = true;
            return result;
        case 0x17: {
            const std::int32_t a = word();
            const std::int32_t b = word();
            const std::int32_t seconds = sar(s_.s32(0x2f54), 12);
            const std::int32_t points = add(divide(mul(mul(seconds, seconds), b), 1000), sar(mul(w_.time_scale, a), 8));
            service(TrickService::PointTimer, {u(points)});
            break;
        }
        case 0x18: {
            const std::int32_t operand = word();
            const std::int32_t target = trick_resolve_frame_operand(s_, operand);
            s_.set16(kAnimTweenStart, s_.u16(kAnimFrame));
            s_.set16(kAnimTweenEnd, u(target));
            s_.set16(kAnimTweenClock, w_.clock);
            s_.set8(kAnimMode, 3);
            break;
        }
        case 0x19:
            s_.set32(0x29d0, u(add(wrap(w_.tricks_base), word())));
            break;
        case 0x2a:
            s_.set32(0x29d4, u(add(wrap(w_.tricks_base), word())));
            break;
        case 0x1a:
            if (s_.s32(0x2e90) != 0) {
                set_cursor(cursor() - 1);
                result.yielded = true;
                return result;
            }
            break;
        case 0x1b: {
            const std::int32_t rail_flip = word();
            s_.set32(0x2ed8, u(rail_flip));
            assertion(rail_flip == 1 || rail_flip == -1);
            break;
        }
        case 0x1c:
            s_.set8(kAnimMode, 4);
            break;
        case 0x1d:
            s_.set32(kAnimRate, u(divide(mul(word(), 0x20000), 60)));
            break;
        case 0x1e:
            service(TrickService::Bail, {0, 0});
            break;
        case 0x1f:
            s_.set32(0x29ec, u(word()));
            break;
        case 0x20:
            s_.set32(0x2c68, 0);
            break;
        case 0x22:
            s_.set32(kFlags, s_.u32(kFlags) ^ 2U);
            break;
        case 0x23:
            s_.set32(0x2f00, u(word()));
            break;
        case 0x24: branch_if_word(0x2be8); break;
        case 0x25: branch_if_word(0x2bec); break;
        case 0x26: branch_if_word(0x2bf0); break;
        case 0x27: branch_if_word(0x2bf4); break;
        case 0x28: branch_if_word(0x2bf8); break;
        case 0x41: branch_if_word(0x2bfc); break;
        case 0x53: branch_if_word(0x29e8); break;
        case 0x29:
            s_.set32(0x2c0c, u(word()));
            break;
        case 0x2b: {
            const std::int32_t axis = word();
            const std::size_t slot = static_cast<std::size_t>(axis) * 4;
            const std::int32_t amount = word();
            const std::int32_t rate = word();
            s_.set32(0x2ca0 + slot, u(amount));
            s_.set32(0x2c94 + slot, u(rate));
            if (rate == 0) {
                s_.set32(0x2ca0 + slot, 0);
                s_.set32(0x2cac + slot, 0);
            }
            break;
        }
        case 0x2c:
            if (s_.u32(0x2ca0) != 0 || s_.u32(0x2ca4) != 0 || s_.u32(0x2ca8) != 0) {
                set_cursor(cursor() - 1);
                result.yielded = true;
                return result;
            }
            break;
        case 0x2d: {
            const std::int32_t value = word();
            s_.set32(0x2e2c, u(value < 0 ? -value : value));
            break;
        }
        case 0x2e:
            s_.set32(0x2c10, u(word()));
            break;
        case 0x2f: {
            const std::int32_t relative = word();
            const std::int32_t state = s_.s32(0x30b8);
            branch_if(state == 1 || state == 2, relative);
            break;
        }
        case 0x30:
            service(TrickService::Fn46dd20, {0, 0xfa});
            break;
        case 0x31:
            s_.set32(0x2f64, 0);
            s_.set32(0x2c10, 0);
            s_.set32(0x2f2c, 4);
            if (s_.s32(0x2dd8) == 0 || w_.controller == nullptr || w_.controller->u8(0x10) == 0) {
                s_.set32(0x2dd8, 0);
                service(TrickService::AnimStart, {0, 0x3b9});
            } else {
                service(TrickService::AnimFull, {8, 0, 0x1a, 0x13, 0x3b4});
            }
            s_.set32(kMotion + 8, 0);
            s_.set32(kMotion + 4, 0);
            s_.set32(0x2f64, 0);
            s_.set32(kMotion, 0);
            service(TrickService::BoardOn, {});
            break;
        case 0x32:
            service(TrickService::BoardOn, {});
            break;
        case 0x33:
            service(TrickService::BoardOff, {});
            break;
        case 0x34: {
            const std::int32_t relative = word();
            branch_if(s_.s32(0x2c04) != 0, relative);
            s_.set32(0x2c04, 0);
            break;
        }
        case 0x35:
            s_.set32(0x2c78, 1);
            break;
        case 0x36:
            service(TrickService::SfxPlay, {u(word()), 0x2000, 0});
            break;
        case 0x37: {
            const std::int32_t id = word();
            const std::int32_t volume = word();
            service(TrickService::SfxPlay, {u(id), u(volume), 0});
            break;
        }
        case 0x38: {
            const std::int32_t id = word();
            const std::int32_t volume = word();
            const std::int32_t pan = word();
            service(TrickService::SfxPlay, {u(id), u(volume), u(pan)});
            break;
        }
        case 0x39: {
            const std::int32_t count = word();
            const std::int32_t pick = wrap(service(TrickService::Rnd, {u(count)}));
            set_cursor(u(add(wrap(cursor()), mul(pick, 2))));
            jump(word());
            break;
        }
        case 0x3a: {
            const std::int32_t relative = word();
            branch_if(((s_.u32(kFlags) >> 1) & 1U) != s_.u8(0x163), relative);
            break;
        }
        case 0x43: {
            const std::int32_t relative = word();
            branch_if((s_.u8(kFlags) & 2U) != 0, relative);
            break;
        }
        case 0x3b:
            s_.set32(0x2c80, 1);
            break;
        case 0x3c:
            s_.set32(0x2c80, 0);
            break;
        case 0x3d: {
            const std::int32_t relative = word();
            assertion(s_.s32(0x29d8) == 0);
            s_.set32(0x29d8, cursor());
            jump(relative);
            break;
        }
        case 0x3e:
            assertion(s_.s32(0x29d8) != 0);
            set_cursor(s_.u32(0x29d8));
            s_.set32(0x29d8, 0);
            break;
        case 0x3f:
            s_.set16(0x29f2, u(word()));
            break;
        case 0x40:
            s_.set16(0x29f0, u(word()));
            break;
        case 0x44:
            s_.set16(0x29f4, u(word()));
            break;
        case 0x45: {
            const std::int32_t pad_raw = word();
            const std::int32_t pad = pad_raw < 0 ? -pad_raw : pad_raw;
            const std::int32_t motor = word();
            const std::int32_t power = word();
            assertion(motor == 0 || motor == 1);
            assertion(power >= 0 && power <= 0xff);
            service(TrickService::Vibrate, {u(static_cast<std::int16_t>(pad)), u(motor), u(power)});
            break;
        }
        case 0x46:
            service(TrickService::Fn431260, {s_.u32(0x30b0), 1});
            break;
        case 0x47: {
            const std::int32_t threshold = word();
            const std::int32_t target = word();
            const std::int32_t magnitude = sar(mul(retail_vector_magnitude_q12(vector_at(s_, kMotion)), 64), 12);
            branch_if(threshold < magnitude, target);
            break;
        }
        case 0x48: {
            const std::int32_t operand = word();
            const std::int32_t resolved = static_cast<std::int16_t>(trick_resolve_frame_operand(s_, operand));
            const std::int32_t relative = word();
            const std::int32_t direction = s_.s8(kAnimDirection);
            branch_if(mul(resolved, direction) <= mul(s_.s16(kAnimFrame), direction) || s_.u8(kAnimFinished) != 0,
                      relative);
            break;
        }
        case 0x4a:
            if (s_.s32(0x2bfc) == 0) {
                s_.set32(0x2c70, 1);
                service(TrickService::StateTransition, {1, 0x275});
            } else {
                s_.set32(0x2ddc, 0);
                service(TrickService::StateTransition, {0, 0x26c});
                s_.set32(0x2c70, 1);
                service(TrickService::Land, {});
            }
            s_.set32(kMotion + 4, u(mul(add(divide(mul(sar(s_.s32(0x2f54), 12), 3), 2), 0x28), 0x1000)));
            break;
        case 0x4b: {
            const std::int32_t relative = word();
            branch_if(fixed_dot_q12(vector_at(s_, 0x30f4), vector_at(s_, kMotion)) < -0x800, relative);
            break;
        }
        case 0x4c: {
            const std::uint8_t wanted = byte();
            const std::int32_t relative = word();
            branch_if(static_cast<std::uint32_t>(wanted) == s_.u32(0x30b8), relative);
            break;
        }
        case 0x4d:
            s_.set32(0x2c7c, 1);
            break;
        case 0x4e: {
            const std::int32_t relative = word();
            if (s_.s32(0x31f8) != 0) {
                s_.set32(0x31f8, 0);
                jump(relative);
            }
            break;
        }
        case 0x4f: {
            std::array<std::int32_t, 4> ids{};
            for (auto& id : ids) {
                id = word();
            }
            const std::int32_t pick = s_.s32(0x2e8c);
            if (pick < 0 || pick > 3) {
                throw TrickScriptFault("TRICK_LIPSND index out of range");
            }
            service(TrickService::SfxPlay, {u(ids[static_cast<std::size_t>(pick)]), 0x2000, 0});
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
            s_.set32(0x2dd4, byte());
            if (s_.s32(0x306c) == 0) {
                s_.set32(0x3078, 0);
            }
            break;
        case 0x54:
            s_.set32(0x2dfc, u(word()));
            break;
        case 0x55:
            s_.set32(0x31f8, byte());
            break;
        case 0x56:
            if (s_.s32(0x2dd8) == 0) {
                service(TrickService::AnimStart, {0, 0x513});
            } else {
                service(TrickService::AnimFull, {8, 0x13, 0x1a, 0x13, 0x50f});
            }
            break;
        case 0x57:
            s_.set32(0x2c08, 1);
            break;
        case 0x59:
            s_.set32(0x3210, byte());
            break;
        case 0x42: {
            // Step the position along the horizontal forward basis (+0x3100/+0x3108).
            const std::int32_t distance = word();
            std::array<std::int32_t, 3> direction{s_.s32(0x3100), 0, s_.s32(0x3108)};
            normalize_q12(direction);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                s_.set32(kPosition + axis * 4, u(add(s_.s32(kPosition + axis * 4), mul(direction[axis], distance))));
            }
            break;
        }
        case 0x49: {
            // Restart a rail grind from the saved node: reset the trick timer and motion,
            // enter physics state 8, and move the position by 0x1e times the +0x310c basis.
            s_.set32(0x2f58, u(word()));
            s_.set32(0x2f54, 0);
            s_.set32(kMotion, 0);
            s_.set32(kMotion + 4, u(-s_.s32(kMotion + 4)));
            s_.set32(kMotion + 8, 0);
            s_.set32(0x2e90, 0);
            s_.set32(0x30b8, 8);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                s_.set32(kPosition + axis * 4, s_.u32(0x2ea8 + axis * 4));
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                s_.set32(kPosition + axis * 4, u(add(s_.s32(kPosition + axis * 4), mul(0x1e, s_.s32(0x310c + axis * 4)))));
            }
            service(TrickService::RailRestart, {});
            s_.set32(0x2c74, 0);
            s_.set32(0x2c68, 0);
            break;
        }
        case 0x21:
            throw TrickScriptFault("trick opcode 0x21 (basis rotation) is not modelled yet");
        default:
            // Undefined script command: logged, then the script-active word is cleared.
            assertion(false);
            s_.set32(0x2c68, 0);
            break;
        }
    }
}

} // namespace

std::int32_t trick_resolve_frame_operand(const RetailImage& skater, std::int32_t operand) {
    // TrickScript_ResolveFrameOperand (0x004be3e0), result truncated to 16 bits by callers.
    if (operand == 0x2711) {
        return static_cast<std::uint16_t>(static_cast<std::int16_t>(skater.s8(kAnimEndFrame)));
    }
    const std::int32_t last = static_cast<std::int32_t>(skater.u8(kAnimFrameCount)) - 1;
    if (operand < 0) {
        if (skater.s8(kAnimDirection) < 1) {
            operand = static_cast<std::int32_t>(skater.s8(kAnimEndFrame)) - operand;
            if (last < operand) {
                operand = last;
            }
        } else {
            operand = static_cast<std::int32_t>(skater.s8(kAnimEndFrame)) + operand;
            if (operand < 0) {
                return 0;
            }
        }
    } else if (last < operand) {
        return last;
    }
    return operand;
}

TrickExecuteResult trick_script_execute(RetailImage& skater, const TrickWorld& world, TrickServices& services) {
    return Machine(skater, world, services).run();
}

} // namespace opentony::runtime
