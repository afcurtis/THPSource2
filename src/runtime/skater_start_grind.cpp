#include "skater_views.hpp"

#include <stdexcept>

// Milestone 10g, session 24: the start of a grind (physics.cpp, 0x00490ef0 Skater_StartGrind, thiscall on the skater, one argument: the geometry hint; called by Skater_LandingCleanup), written from the
// machine code. The rail direction (+0x2eb4) is negated in place for the whole entry (and negated back at the end); the grind animation (0xe) is requested; with the hint the pending motion words
// (+0x2c78, +0x2c7c, +0x2c94..+0x2cb4) and the latch +0x2de0 are cleared. The skater moves to the rail point (+0x2ea8) 0x19000 lower, +0x2e90 is set, and the point is kept in +0x2ee0 and +0x2ecc.
// The rail speed +0x2efc grows by 0x28000 while the node (+0x2e9c) is the one before it (+0x2e98), else it falls by that (not below 0) and the node is taken over with +0x2ef4 reset; +0x2ef4 is then
// the random-speed formula over two reads of stat 7 (divided by an IDIV that faults on 0). The turn flags (+0x2bf0 / +0x2bf4 / +0x2bf8) come from the dot of the direction with a vector across
// (+0x2ea8 minus +0x2e18, the x and z turned a quarter) when the node changed, the direction flags (+0x2bec / +0x2be8) from the dots with the forward and right vectors; the stick's pattern
// (0x00492120) and the pattern word +0x2c04 follow. A waiting trick script (+0x29dc, started within 0x1e ticks) starts, else the grind trick of the five flags is started (0x00491c10); the script
// interpreter runs; with +0x2e90 still set the sound of the rail type (+0x2e8c, 0 to 3; any other value is an assertion) plays. The state becomes 4 and the velocity is its projection on the rail
// direction plus a tenth of the direction times (stat 3 + 100) / 2.
// The vector helpers (negate, subtract, scale, divide, add) and the dot / product run here as arithmetic; every other callee is a service. The locals are at a scratch address of their own.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }
inline std::int32_t plus(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) + static_cast<Word>(b)); }

constexpr Word kRequestStart = 0x004903f0;        // Animation_RequestStart(animation, source line) (thiscall)
constexpr Word kProfileValue = 0x0048f3a0;        // Skater_ProfileValue(selector) (thiscall)
constexpr Word kDerivePattern = 0x00492120;       // TrickInput_DerivePattern (thiscall): the stick's direction pattern 0..8
constexpr Word kGrindTrick = 0x00491c10;          // Skater_StartGrindTrick(index) (thiscall)
constexpr Word kStateTransition = 0x004900b0;     // CBruce__StateTransition(state, source line) (thiscall)

constexpr Word kFrameClock = 0x005685f4;
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
constexpr Word kBadRailTypeText = 0x00536b94;     // the assertion's message
const std::string kPhysicsFile = "H:\\TonyHawk\\Pc2\\physics.cpp";   // 0x00536a30

// the retail stack locals, at a scratch address no native the function calls (the air physics 0x4000, the trick script 0x4200) uses
constexpr Word kLocals = pcplatform::kScratchBase + 0x4600;
constexpr Word kAcross = kLocals + 0x00;          // (z, 0, -x) of the relative vector
constexpr Word kKick = kLocals + 0x10;            // the direction's copy: scaled, divided, then added to the velocity
constexpr Word kScalar = kLocals + 0x20;          // the scale (stat 3 + 100) / 2 and the divisor 10
constexpr Word kDivisor = kLocals + 0x24;

// idiv: the processor faults on a zero divisor and on the smallest value over -1
inline std::int32_t divide_checked(std::int32_t value, std::int32_t divisor) {
    if (divisor == 0 || (divisor == -1 && value == INT32_MIN)) {
        throw std::runtime_error("division fault in Skater_StartGrind");
    }
    return value / divisor;
}

}  // namespace

// 0x00490ef0 (thiscall, one argument)
void SkaterKit::start_grind(Word self, Word geometry_hint) {
    const Word direction = self + 0x2eb4;
    const Word position = self + 0x8;
    const auto negate_direction = [&] {
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, direction + k, 0u - word_at(m_, direction + k));
        }
    };

    negate_direction();
    thiscall(kRequestStart, self, {0xe, 0x9d5});
    if (geometry_hint != 0) {
        skater_at(m_, self).set_field_2c78(0);
        skater_at(m_, self).set_field_2c7c(0);
        for (Word at = 0x2c94; at <= 0x2cb4; at += 4) {
            set_word_at(m_, self + at, 0);
        }
        skater_at(m_, self).set_field_2de0(0);
    }
    skater_at(m_, self).set_field_2eec(0);

    // ---- the skater goes to the rail point, 0x19000 lower ----
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, position + k, word_at(m_, self + 0x2ea8 + k));
    }
    set_word_at(m_, self + 0xc, word_at(m_, self + 0xc) - 0x19000);
    skater_at(m_, self).set_field_2e94(0);
    skater_at(m_, self).set_field_2e90(1);
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, self + 0x2ee0 + k, word_at(m_, position + k));
    }
    for (Word k = 0; k < 12; k += 4) {
        set_word_at(m_, self + 0x2ecc + k, word_at(m_, position + k));
    }

    // ---- the rail speed and the random-speed formula ----
    const Word node = word_at(m_, self + 0x2e9c);
    if (node == skater_at(m_, self).field_2e98()) {
        skater_at(m_, self).set_field_2efc(static_cast<Word>(skater_at(m_, self).field_2efc() + 0x28000));
    } else {
        const Word lowered = skater_at(m_, self).field_2efc() - 0x28000;
        skater_at(m_, self).set_field_2efc(static_cast<Word>(lowered));
        if (s(lowered) < 0) {
            skater_at(m_, self).set_field_2efc(0);
        }
        skater_at(m_, self).set_field_2e98(static_cast<Word>(node));
        skater_at(m_, self).set_field_2ef4(0);
    }
    const std::int32_t first_stat = s(thiscall_value(kProfileValue, self, {7}));
    const std::int32_t first = s(0x493e0u - u(mul(first_stat, 1800))) / 10000;
    const std::int32_t scaled = mul(first, static_cast<std::int32_t>(skater_at(m_, self).field_2ef4())) / 100;
    const std::int32_t product = mul(scaled, plus(static_cast<std::int32_t>(skater_at(m_, self).field_2efc()) >> 12, 0x3c));
    const std::int32_t second_stat = s(thiscall_value(kProfileValue, self, {7}));
    const std::int32_t divisor = mul(plus(second_stat, 0x15e), 2000) / 10000;
    skater_at(m_, self).set_field_2c68(1);
    skater_at(m_, self).set_field_2e88(static_cast<Word>(skater_at(m_, self).field_2e88() + 1));
    skater_at(m_, self).set_field_3070(static_cast<Word>(skater_at(m_, self).field_3070() + 1));
    skater_at(m_, self).set_field_304c(static_cast<Word>(skater_at(m_, self).field_304c() + 1));
    skater_at(m_, self).set_field_2ef4(static_cast<Word>(u(divide_checked(product, divisor))));

    // ---- the turn flags and the direction flags ----
    // across = (rail point - +0x2e18) with its x and z turned a quarter: (z, 0, -x)
    const Word relative_x = skater_at(m_, self).field_2ea8() - word_at(m_, self + 0x2e18);
    const Word relative_z = word_at(m_, self + 0x2ea8 + 8) - word_at(m_, self + 0x2e18 + 8);
    set_global_word(m_, kAcross, relative_z);
    set_global_word(m_, kAcross + 4, 0);
    set_global_word(m_, kAcross + 8, 0u - relative_x);
    const std::int32_t tangent_dot = math_.dot_q12(direction, self + 0x30f4);
    const std::int32_t rail_dot = math_.dot_q12(direction, self + 0x3100);
    const std::int32_t across_dot = math_.dot_q12(direction, kAcross);
    if (skater_at(m_, self).field_2ec8() != node) {
        skater_at(m_, self).set_field_2bf0(0);
        skater_at(m_, self).set_field_2bf4(0);
        if (across_dot > 0) {
            skater_at(m_, self).set_field_2bf0(1);
        }
        skater_at(m_, self).set_field_2bf8(0);
        if ((byte_at(m_, self + 0xd8) & 2) != 0) {
            skater_at(m_, self).set_field_2bf8(1);
        }
        if (skater_at(m_, self).field_2bf0() != skater_at(m_, self).field_2bf8()) {
            skater_at(m_, self).set_field_2bf4(1);
        }
    }
    skater_at(m_, self).set_field_2bec(0);
    skater_at(m_, self).set_field_2be8(0);
    const Word sign = u(rail_dot >> 31);
    if (s((u(rail_dot) ^ sign) - sign) < 0xb33) {
        skater_at(m_, self).set_field_2be8(1);
        if (tangent_dot > 0) {
            skater_at(m_, self).set_field_2bec(1);
        }
    } else if (rail_dot < 0) {
        skater_at(m_, self).set_field_2bec(1);
    }
    skater_at(m_, self).set_field_2ec8(static_cast<Word>(node));

    // ---- the stick's pattern, then the trick script ----
    const Word pattern = static_cast<Word>(thiscall_value(kDerivePattern, self));
    skater_at(m_, self).set_field_2c04(0);
    if (skater_at(m_, self).field_2bfc() != 0) {
        const std::int32_t halved = s((1u - skater_at(m_, self).field_2c00()) ^ skater_at(m_, self).field_2bfc()) >> 1;
        skater_at(m_, self).set_field_2c04(static_cast<Word>(u(halved) ^ skater_at(m_, self).field_2bf0()));
    }
    skater_at(m_, self).set_field_2ed8(1);
    skater_at(m_, self).set_field_2c0c(0);
    const Word waiting = skater_at(m_, self).field_29dc();
    bool started = false;
    if (waiting != 0) {
        const Word since = skater_at(m_, self).field_2be0();
        if (s(global_word(m_, kFrameClock) - since) < 0x1e && (geometry_hint != 0 || skater_at(m_, self).field_29e4() == 0)) {
            skater_at(m_, self).set_field_2a0c(0xc);
            skater_at(m_, self).set_field_2a10(static_cast<Word>(since));
            trick_script_start(self, waiting);
            skater_at(m_, self).set_field_29dc(0);
            started = true;
        }
    }
    if (!started) {
        if (skater_at(m_, self).field_2c08() != 0) {
            skater_at(m_, self).set_field_2bec(static_cast<Word>(skater_at(m_, self).field_2bec() ^ 1));
            skater_at(m_, self).set_field_2bf8(static_cast<Word>(skater_at(m_, self).field_2bf8() ^ 1));
        }
        // the five flags as one number, the pattern the high part: ((((pattern * 2 + +0x2bf8) * 2 + +0x2bec) * 2 + +0x2be8) * 2 + +0x2bf0)
        Word index = skater_at(m_, self).field_2bf8() + pattern * 2;
        index = skater_at(m_, self).field_2bec() + index * 2;
        index = skater_at(m_, self).field_2be8() + index * 2;
        index = skater_at(m_, self).field_2bf0() + index * 2;
        thiscall(kGrindTrick, self, {index});
    }
    thiscall(kScriptExecute, self);
    if (!s_.live()) {
        // the interpreter may clear the rail flag +0x2e90 and set the lip word +0x2c0c (the host's answers; live, the real call left them in memory)
        const std::string name = pcplatform::engine_name(kScriptExecute);
        skater_at(m_, self).set_field_2e90(static_cast<Word>(static_cast<Word>(s_.value(name))));
        skater_at(m_, self).set_field_2c0c(static_cast<Word>(static_cast<Word>(s_.value(name))));
    }

    // ---- the sound of the rail type ----
    if (skater_at(m_, self).field_2e90() != 0) {
        const bool lit = skater_at(m_, self).field_2c0c() != 0;
        Word sound = 0;
        bool bad = false;
        switch (skater_at(m_, self).field_2e8c()) {
        case 0:
            sound = 5;
            break;
        case 1:
            sound = lit ? 0xd5 : 0xd6;
            break;
        case 2:
            sound = 0xc8;
            break;
        case 3:
            sound = lit ? 0xd7 : 0xd6;
            break;
        default:
            bad = true;
            break;
        }
        if (bad) {
            set_global_word(m_, kAssertionLine, 0xb18);
            set_string(kAssertionFile, kPhysicsFile);
            check(false, kBadRailTypeText);
        } else {
            engine(kSoundPlay, {sound, 0x2000, 0});
        }
        thiscall(kStateTransition, self, {4, 0xb1c});

        // ---- the velocity: its projection on the direction, plus a tenth of the direction times (stat 3 + 100) / 2 ----
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, kKick + k, word_at(m_, direction + k));
        }
        const std::int32_t along = math_.dot_q12(direction, self + 0x4c);
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, self + 0x4c + k, u(math_.mul_q12(int_at(m_, direction + k), along)));
        }
        const std::int32_t stat = s(thiscall_value(kProfileValue, self, {3}));
        set_global_word(m_, kScalar, u(mul(plus(stat, 0x64), 5000) / 10000));
        set_global_word(m_, kDivisor, 10);
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, kKick + k, u(mul(int_at(m_, kKick + k), global_int(m_, kScalar))));
        }
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, kKick + k, u(int_at(m_, kKick + k) / global_int(m_, kDivisor)));
        }
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, self + 0x4c + k, word_at(m_, self + 0x4c + k) + word_at(m_, kKick + k));
        }
    }
    negate_direction();
}

}  // namespace opentony::runtime::skaterkit
