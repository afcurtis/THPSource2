#include "skater_views.hpp"

// Phase 4.15a: the bail, the vibration and the pad history of physics.cpp.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t sext16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }

}  // namespace

// 0x00491b80: TrickScript_Start, the retail-memory copy of the 4.7a function (a script begins: the running flags, the script pointer and the defaults of the interpreter's registers)
void SkaterKit::trick_script_start(Word self, Word script) {
    skater_at(m_, self).set_field_2c68(1);
    skater_at(m_, self).set_field_29c8(1);
    skater_at(m_, self).set_field_2c6c(0);
    skater_at(m_, self).set_field_2f58(0);
    skater_at(m_, self).set_field_2f54(0);
    skater_at(m_, self).set_field_29d8(0);
    skater_at(m_, self).set_field_29d0(0);
    skater_at(m_, self).set_field_29d4(0);
    skater_at(m_, self).set_field_29cc(static_cast<Word>(script));
    skater_at(m_, self).set_field_29f2(0);
    skater_at(m_, self).set_field_29f0(0);
    skater_at(m_, self).set_field_2e2c(0);
    skater_at(m_, self).set_field_108(0x10000);
    skater_at(m_, self).set_field_29c0(0x7b);
    skater_at(m_, self).set_field_29c4(0x00536bc0);
    skater_at(m_, self).set_field_29e0(0);
    skater_at(m_, self).set_field_3210(0);
}

// 0x0048fb20: the end of a trick run: the counters and the flip state cleared, the crowd reacts to the landed value (or to the speed of a bail with nothing landed), the panel is told, and the pad rumbles
void SkaterKit::trick_bail(Word self) {
    if (global_word(m_, globals::kGameMode) == 7) {
        skater_at(m_, self).set_field_2cc8(0);
    }
    for (const Word field : {0x2d88u, 0x3064u, 0x306cu, 0x3068u, 0x3014u, 0x3018u, 0x2c88u, 0x2c84u, 0x2c80u, 0x2f6cu, 0x2f70u}) {
        set_word_at(m_, self + field, 0);
    }
    const std::int32_t speed = integer_sqrt(math_.dot_q12(self + trick::kMovement, self + trick::kMovement)) << 6;
    const Word landed = engine_value(kLandedValue, {self + trick::kPanel});
    if (landed != 0) {
        engine(kCrowdReactToTrick, {landed});
    } else {
        engine(kCrowdReactToBail, {u(speed >> 12)});
    }
    engine(kPanelBail, {self});
    skater_at(m_, self).set_field_2c70(0);
    skater_at(m_, self).set_field_2c74(0);
    vibrate(self, 8, 1, 0x96);
}

// 0x0048ffc0: rumble the pad of the skater's player (in the mode 7 the two players' indexes are swapped by a mask) unless the setting is off or a movie plays
void SkaterKit::vibrate(Word self, Word a, Word b, Word c) {
    Word player = static_cast<Word>(skater_at(m_, self).player_index());
    if (global_word(m_, globals::kGameMode) == 7) {
        player ^= global_word(m_, globals::kSecondPlayerMask);
    }
    if (word_at(m_, globals::kVibrationSettings + 4 * player) != 0 && (engine_value(kVideoPlaying) & 0xff) == 0) {
        engine(kActuatorOn, {player, a, b, c});
    }
}

// 0x004904d0: the skater falls. Its trick state is cleared, `Trick_Bail` runs, the state goes to 1 (cause 0x715), and the bail script of the given number (or number 1 when the speed is under 0x2d000 and
// none was given) is started and run once
void SkaterKit::bail(Word self, Word script, Word cause) {
    skater_at(m_, self).set_field_2c64(0);
    if ((engine_value(kIsRemotePlayer, {self}) & 0xff) != 0 && global_word(m_, globals::kGameMode) != 9) {
        engine(kSetLocalFlag, {0x100000, 1});
    }
    skater_at(m_, self).set_field_2f60(static_cast<Word>(cause));
    set_word_at(m_, self + skater::kFlipPhase, 0);
    skater_at(m_, self).set_field_302c(static_cast<Word>(skater_at(m_, self).field_302c() + 1));
    for (const Word field : {0x29dcu, 0x29e0u, 0x2bd8u, 0x2ec8u, 0x2e90u, 0x29d4u, 0x29d0u}) {
        set_word_at(m_, self + field, 0);
    }
    skater_at(m_, self).set_field_29f0(0);
    skater_at(m_, self).set_field_29f4(100);
    skater_at(m_, self).set_field_2dd0(0);
    skater_at(m_, self).set_field_2dd4(0);
    skater_at(m_, self).set_field_29c8(0);
    trick_bail(self);
    skater_at(m_, self).set_field_2f68(0);
    skater_at(m_, self).set_field_2f64(1);
    skater_at(m_, self).set_field_2de0(0);
    skater_at(m_, self).set_field_2de8(0);
    reset_trick_buffers(self);
    skater_at(m_, self).set_field_2c68(0);
    skater_at(m_, self).set_field_2e94(0);
    state_transition(self, 1);
    if (script == 0) {
        if ((integer_sqrt(math_.dot_q12(self + trick::kMovement, self + trick::kMovement)) << 6) < 0x2d000) {
            script = 1;
        }
    }
    const Word offsets = global_word(m_, globals::kStateScriptOffsets);
    trick_script_start(self, u(sext16(half_at(m_, offsets + 2 * script))) + global_word(m_, globals::kTricksBinary));
    engine(kScriptExecute, {self});
    skater_at(m_, self).set_field_2c68(0);
}

// 0x004916d0: a skater whose trick script runs or whose flip counter is up falls when the flip phase says the board comes round the wrong way, or when no script flag is set; not while it is already
// bailing or when bailing is locked
void SkaterKit::bail_when_flipping(Word self) {
    if (skater_at(m_, self).field_2f64() != 0 || global_word(m_, globals::kBailLocked) != 0) {
        return;
    }
    if (skater_at(m_, self).field_2c68() == 0 && skater_at(m_, self).field_2c88() == 0) {
        return;
    }
    const bool quiet = skater_at(m_, self).field_2c6c() == 0;
    bool fall = quiet;
    if (skater_at(m_, self).field_2c88() != 0) {
        const Word phase = word_at(m_, self + skater::kFlipPhase) & 0xfff;
        if (skater_at(m_, self).field_2c90() > 0) {
            fall = phase < 0xed4 ? true : quiet;
        } else {
            fall = phase > 0x12c ? true : quiet;
        }
    }
    if (!fall) {
        return;
    }
    bail(self, u(sext16(skater_at(m_, self).field_29f0())), 0);
    skater_at(m_, self).set_field_29f0(0);
    skater_at(m_, self).set_field_3034(static_cast<Word>(skater_at(m_, self).field_3034() + 1));
}

// 0x00491c10: start the grind trick script of a number (0..0xcf)
void SkaterKit::grind_trick_start(Word self, Word index) {
    constexpr Word kMsgIndex = 0x00536bc8;
    check(s(index) >= 0 && s(index) < 0xd0, kMsgIndex);
    const Word offsets = global_word(m_, globals::kGrindScriptOffsets);
    trick_script_start(self, u(sext16(half_at(m_, offsets + 2 * index))) + global_word(m_, globals::kTricksBinary));
}

// 0x00491da0: the time (the animation clock) a button was pressed, looking back from the newest entry of the 32-entry pad history; `skip` older presses are passed over; -1 when there is none
Word SkaterKit::pad_press_time(Word self, Word button, Word skip) {
    constexpr Word kMsgButton = 0x00536be0;
    const std::uint8_t wanted = static_cast<std::uint8_t>(button);
    check(wanted <= 0x10, kMsgButton);
    const std::int32_t head = static_cast<std::int32_t>(skater_at(m_, self).field_2b14());
    std::int32_t at = head - 1;
    if (at < 0) {
        at = 0x1f;
    }
    while (at != head) {
        const Word entry = self + trick::kPadHistory + 8 * static_cast<Word>(at);
        if (byte_at(m_, entry) == wanted && byte_at(m_, entry + 1) != 0) {
            if (skip == 0) {
                return word_at(m_, entry + 4);
            }
            --skip;
        }
        --at;
        if (at < 0) {
            at = 0x1f;
        }
    }
    return 0xffffffffu;
}

// 0x00491e50: one of two buttons was pressed on this very tick and the other one less than `window` ticks before
Word SkaterKit::pad_pressed_with(Word self, Word first, Word second, Word window) {
    constexpr Word kMsgFirst = 0x00536c30;
    constexpr Word kMsgSecond = 0x00536c14;
    check(static_cast<std::uint8_t>(first) <= 0x10, kMsgFirst);
    check(static_cast<std::uint8_t>(second) <= 0x10, kMsgSecond);
    const Word a = pad_press_time(self, first, 0);
    const Word b = pad_press_time(self, second, 0);
    const Word clock = global_word(m_, globals::kAnimationClock);
    if (a == clock && s(clock - b) < s(window)) {
        return 1;
    }
    if (b == clock && s(clock - a) < s(window)) {
        return 1;
    }
    return 0;
}

// 0x00491f40: both buttons were pressed (ever) and both less than `window` ticks ago
Word SkaterKit::pad_both_recent(Word self, Word first, Word second, Word window) {
    constexpr Word kMsgFirst = 0x00536c30;
    constexpr Word kMsgSecond = 0x00536c14;
    check(static_cast<std::uint8_t>(first) <= 0x10, kMsgFirst);
    check(static_cast<std::uint8_t>(second) <= 0x10, kMsgSecond);
    const Word a = pad_press_time(self, first, 0);
    const Word b = pad_press_time(self, second, 0);
    if (a == 0xffffffffu || b == 0xffffffffu) {
        return 0;
    }
    const Word clock = global_word(m_, globals::kAnimationClock);
    if (s(clock - a) < s(window) && s(clock - b) < s(window)) {
        return 1;
    }
    return 0;
}

}  // namespace opentony::runtime::skaterkit
