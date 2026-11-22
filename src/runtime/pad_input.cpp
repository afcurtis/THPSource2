#include "auto_views.hpp"
#include "pad_input.hpp"
#include "seam_addresses_generated.hpp"

#include <stdexcept>

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

using namespace pad;
using namespace pcinput;

PadInput::Record PadInput::read_record(std::uint32_t address) const {
    return {{word_at(m_, address), word_at(m_, address + 4), word_at(m_, address + 8), word_at(m_, address + 12)}};
}

void PadInput::write_record(std::uint32_t address, const Record& record) {
    for (std::uint32_t k = 0; k < 4; ++k) {
        set_word_at(m_, address + 4 * k, record.w[k]);
    }
}

void PadInput::swap_records(std::uint32_t a, std::uint32_t b) {
    const Record first = read_record(a);
    write_record(a, read_record(b));
    write_record(b, first);
}

// ---- pad.cpp ----------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004894a0: the one-time set-up of the control system: each player's table of four record pointers, the records cleared, the PSX pad library told where the two buffers are
void PadInput::init_at_start() {
    check(true, kMsgNumpads);
    check(global_word(m_, kInitialised) == 0, kMsgAlreadyInitialised);
    for (std::uint32_t table = 0x0056b154; table < 0x0056b42c; table += kStride) {
        set_word_at(m_, table, table - 0x11c);
        set_word_at(m_, table + 4, table - 0x10c);
        set_word_at(m_, table + 8, table - 0xfc);
        set_word_at(m_, table + 12, table - 0xec);
    }
    clear_all();
    engine(0x004e8ab0, {0x0056af70, 0x0056af92});
    engine(0x004e8ad0);
    set_global_word(m_, kIdleFrames, 0);
    engine(kDemoTimer);
    set_global_word(m_, kInitialised, 1);
}

// 0x004895c0
void PadInput::clear_all_one(std::uint32_t player) {
    const std::uint32_t offset = player * kStride;
    clear(kStates + offset);
    set_word_at(m_, 0x0056b148 + offset, 0);
    for (std::uint32_t k = 0; k < 4; ++k) {
        set_byte_at(m_, 0x0056b140 + k + offset, 0);
    }
}

// 0x00489610
void PadInput::clear_all() {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        clear_all_one(player);
    }
}

// 0x00489630: the twenty records of a block cleared (held and latch bytes, the three counters); the analog bytes it clears are always the first player's
void PadInput::clear(std::uint32_t block) {
    for (std::uint32_t i = 0; i < kRecordCount; ++i) {
        const std::uint32_t record = block + i * kRecordSize;
        set_byte_at(m_, record, 0);
        set_byte_at(m_, record + 1, 0);
        set_word_at(m_, record + 4, 0);
        set_word_at(m_, record + 8, 0);
        set_word_at(m_, record + 12, 0);
    }
    for (std::uint32_t k = 0; k < 4; ++k) {
        set_byte_at(m_, kAnalog + k, 0);
    }
}

// 0x00489670: the pressed latch of every record
void PadInput::clear_triggers(std::uint32_t block) {
    for (std::uint32_t i = 0; i < kRecordCount; ++i) {
        set_byte_at(m_, block + i * kRecordSize + 1, 0);
    }
}

// 0x004896a0: start a motor (port 0 or 1, motor 0 or 1, strength, duration) and remember what was asked in the port's rumble records
void PadInput::actuator_on(std::uint32_t port, std::uint32_t strength, std::uint32_t motor, std::uint32_t duration) {
    check((port & 0xff) < 2, kMsgBadIndex);
    check((motor & 0xff) < 2, kMsgBadMotor);
    if (global_word(m_, kMotorLimit) != 0) {
        return;
    }
    engine(seam::hal::PCInput_ActuatorOn, {port, strength, motor, duration});
    const std::uint32_t player = global_word(m_, kModeSelector) == 7 ? 0 : (port & 0xff);
    const std::uint32_t psx_port = (port & 0xff) != 0 ? 0x10 : 0;
    const std::uint32_t state = engine_value(0x004e8b20, {psx_port});
    if (state == 0) {
        return;
    }
    const std::uint32_t flag = 0x0056b14f + player * kStride;
    if (state == 1) {
        set_byte_at(m_, flag, 0);
    }
    if (byte_at(m_, flag) == 0) {
        engine(0x004e8b10, {psx_port, 0x0056b14c + (port & 0xff) * kStride, 2});
        if (state == 2) {
            set_byte_at(m_, flag, 1);
        } else if (state == 6) {
            if (engine_value(0x004e8b00, {psx_port, 0x00535f44}) != 0) {
                set_byte_at(m_, flag, 1);
            }
        }
    }
    set_byte_at(m_, 0x0056b14c + (port & 0xff) * kStride + (motor & 0xff), duration);
    set_half_at(m_, 0x0056b150 + ((motor & 0xff) + (port & 0xff) * 0xb6) * 2, strength);
}

// 0x00489850
void PadInput::actuator_off(std::uint32_t port, std::uint32_t motor) {
    check((port & 0xff) < 2, kMsgBadIndex);
    check((motor & 0xff) < 2, kMsgBadMotor);
    engine(0x004e4ce0, {port, motor});
    set_byte_at(m_, 0x0056b14c + (port & 0xff) * kStride + (motor & 0xff), 0);
    set_half_at(m_, 0x0056b150 + ((motor & 0xff) + (port & 0xff) * 0xb6) * 2, 0);
}

// 0x00489930: one action record for a frame: held byte, latch on the press, and the counters (+4 frames held, +8 frames released, +0xc since the latch)
void PadInput::advance_action_state(std::uint32_t record, std::uint32_t pressed) {
    set_word_at(m_, record + 0xc, word_at(m_, record + 0xc) + 1);
    bool released = false;
    if (byte_at(m_, record) == 0) {
        if (pressed == 0) {
            released = true;
        } else {
            set_byte_at(m_, record + 1, 1);
            set_word_at(m_, record + 0xc, 0);
        }
    }
    if (!released && pressed != 0) {
        set_global_word(m_, kIdleFrames, 0);
        engine(kDemoTimer);
        set_byte_at(m_, record, 1);
    } else {
        set_byte_at(m_, record, 0);
    }
    if (byte_at(m_, record) != 0) {
        set_word_at(m_, record + 8, 0);
        set_word_at(m_, record + 4, word_at(m_, record + 4) + 1);
    } else {
        set_word_at(m_, record + 4, 0);
        set_word_at(m_, record + 8, word_at(m_, record + 8) + 1);
    }
}

// 0x00489990: a stick sample scaled to -127..127 around its centre, with a dead zone of 32 on either side; a sample outside the dead zone ends the idle count
std::int32_t PadInput::analog(std::int32_t value, std::int32_t low, std::int32_t high, std::int32_t middle) {
    if (value < low) {
        return -0x7f;
    }
    if (value > high) {
        return 0x7f;
    }
    if (value < middle - 0x20) {
        const std::int32_t divisor = (middle - low) - 0x20;
        if (divisor == 0) {
            throw std::runtime_error("Pad_Analog: division by zero");
        }
        set_global_word(m_, kIdleFrames, 0);
        return static_cast<std::int32_t>(static_cast<std::uint32_t>((value - middle) + 0x20) * 127u) / divisor;
    }
    if (value > middle + 0x20) {
        const std::int32_t divisor = (high - middle) - 0x20;
        if (divisor == 0) {
            throw std::runtime_error("Pad_Analog: division by zero");
        }
        set_global_word(m_, kIdleFrames, 0);
        return static_cast<std::int32_t>(static_cast<std::uint32_t>((value - middle) - 0x20) * 127u) / divisor;
    }
    return 0;
}

// 0x00489a10: one frame of input: the DirectInput poll, a rumble stop for a pad that cannot rumble, the stick samples of an analog pad scaled, and the sixteen action records
// of each player advanced from that player's half of the pending mask
std::uint32_t PadInput::update_action_states() {
    engine(0x00489cc0);
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        const std::uint32_t type = byte_at(m_, kPadRegisters + player * kPadRegisterStride);
        if (type != 'A' && type != 's') {
            actuator_off(player, 0);
            actuator_off(player, 1);
        }
    }
    std::uint32_t mask = get_action_mask();
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        const std::uint32_t samples = kPadRegisters + 4 + player * kPadRegisterStride;       // 0x0056afbd
        const std::uint32_t out = 0x0056b143 + player * kStride;
        const std::uint32_t type = word_at(m_, out + 1);
        if (type == 0x73 || type == 0x53) {
            set_byte_at(m_, out, static_cast<std::uint32_t>(analog(byte_at(m_, samples - 1), global_int(m_, 0x0056b2d8), global_int(m_, 0x00535f2c), global_int(m_, 0x00535f3c))));
            set_byte_at(m_, out - 1, static_cast<std::uint32_t>(analog(byte_at(m_, samples), global_int(m_, 0x0056b2dc), global_int(m_, 0x00535f30), global_int(m_, 0x00535f40))));
            set_byte_at(m_, out - 2, static_cast<std::uint32_t>(analog(byte_at(m_, samples + 1), global_int(m_, 0x0056b2d0), global_int(m_, 0x00535f24), global_int(m_, 0x00535f34))));
            set_byte_at(m_, out - 3, static_cast<std::uint32_t>(analog(byte_at(m_, samples + 2), global_int(m_, 0x0056b2d4), global_int(m_, 0x00535f28), global_int(m_, 0x00535f38))));
        } else {
            for (std::uint32_t k = 0; k < 4; ++k) {
                set_byte_at(m_, out - k, 0);
            }
        }
    }
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        const std::uint32_t base = kStates + player * kStride;
        const std::uint32_t horizontal = 0x0056b141 + player * kStride;
        const std::uint32_t vertical = 0x0056b140 + player * kStride;
        advance_action_state(base + 0x00, mask & 0x10);
        advance_action_state(base + 0x10, mask & 0x80);
        advance_action_state(base + 0x20, mask & 0x20);
        advance_action_state(base + 0x30, mask & 0x40);
        advance_action_state(base + 0x40, mask & 0x04);
        advance_action_state(base + 0x50, mask & 0x01);
        advance_action_state(base + 0x60, mask & 0x08);
        advance_action_state(base + 0x70, mask & 0x02);
        advance_action_state(base + 0x80, (mask & 0x8000) != 0 || sbyte_at(m_, horizontal) < -0x28);
        advance_action_state(base + 0x90, (mask & 0x2000) != 0 || sbyte_at(m_, horizontal) > 0x28);
        advance_action_state(base + 0xa0, (mask & 0x1000) != 0 || sbyte_at(m_, vertical) < -0x28);
        advance_action_state(base + 0xb0, (mask & 0x4000) != 0 || sbyte_at(m_, vertical) > 0x28);
        advance_action_state(base + 0xc0, mask & 0x200);
        advance_action_state(base + 0xd0, mask & 0x400);
        advance_action_state(base + 0xe0, mask & 0x800);
        advance_action_state(base + 0xf0, mask & 0x100);
        mask >>= 16;
    }
    return 0;
}

// 0x00489cd0: in the head-to-head mode the left/right and the record pair at +0x40 / +0x60 swap places (the players swap sides), then the four records the pointer table names
// are written from the records now at +0x40..+0x70
void PadInput::restore() {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        const std::uint32_t base = kStates + player * kStride;
        if (global_word(m_, kPlayerCount) != 0 && global_word(m_, kGameMode) == 1) {
            swap_records(base + 0x80, base + 0x90);
            swap_records(base + 0x60, base + 0x40);
        }
        const Record b = read_record(base + 0x40);
        const Record a = read_record(base + 0x50);
        const Record c = read_record(base + 0x60);
        const Record d = read_record(base + 0x70);
        write_record(word_at(m_, base + kPointerTable), b);
        write_record(word_at(m_, base + kPointerTable + 4), a);
        write_record(word_at(m_, base + kPointerTable + 8), c);
        write_record(word_at(m_, base + kPointerTable + 12), d);
    }
}

// 0x00489e70: the reverse: the four records at +0x40..+0x70 are loaded from what the pointer table names, then (head-to-head) swapped as above
void PadInput::remap() {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        const std::uint32_t base = kStates + player * kStride;
        const Record p0 = read_record(word_at(m_, base + kPointerTable));
        const Record p1 = read_record(word_at(m_, base + kPointerTable + 4));
        const Record p2 = read_record(word_at(m_, base + kPointerTable + 8));
        const Record p3 = read_record(word_at(m_, base + kPointerTable + 12));
        write_record(base + 0x40, p0);
        write_record(base + 0x50, p1);
        write_record(base + 0x60, p2);
        write_record(base + 0x70, p3);
        if (global_word(m_, kPlayerCount) != 0 && global_word(m_, kGameMode) == 1) {
            swap_records(base + 0x90, base + 0x80);
            swap_records(base + 0x60, base + 0x40);
        }
    }
}

// ---- pc\pcinput.cpp ---------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004e3e80: the saved chords become mode 0's, the extra joystick buttons are unset and the others take their defaults
void PadInput::reset_bindings() {
    set_global_word(m_, kCurrentSlots + 4, global_word(m_, kDefaultSlots + 4));
    set_global_word(m_, pcinput::kJoyPause, 0xffffffff);
    set_global_word(m_, pcinput::kJoyCamera, 0xffffffff);
    set_global_word(m_, kCurrentSlots, global_word(m_, kDefaultSlots));
    for (std::uint32_t k = 2; k < pcinput::kSlotCount; ++k) {
        set_word_at(m_, kCurrentSlots + 4 * k, word_at(m_, kDefaultSlots + 4 * k));
    }
    set_global_word(m_, pcinput::kJoyJump, 1);
    set_global_word(m_, pcinput::kJoyGrind, 4);
    set_global_word(m_, pcinput::kJoyGrab, 2);
    set_global_word(m_, pcinput::kJoyKick, 0);
    set_global_word(m_, pcinput::kJoySpinLeft, 3);
    set_global_word(m_, pcinput::kJoySpinRight, 5);
    set_global_word(m_, pcinput::kJoyNollie, 6);
    set_global_word(m_, pcinput::kJoySwitch, 7);
}

// 0x004e3f80: choose which of the three chord sets the 14 binding pointers name (mode 1 and 2 are the two menu sets, anything else the game's), then let the net layer know
void PadInput::set_control_mode(std::uint32_t mode) {
    using namespace pcinput;
    set_global_word(m_, kControlMode, mode);
    if (mode == 1) {
        static constexpr std::uint32_t kOffsets[kSlotCount] = {0x00, 0x04, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38};
        for (std::uint32_t k = 0; k < kSlotCount; ++k) {
            set_word_at(m_, kSlots + 4 * k, 0x0054ad20 + kOffsets[k]);
        }
        engine(seam::hal::PCInput_ActuatorOff);
        engine(0x004dbd90);
        return;
    }
    if (mode == 2) {
        for (std::uint32_t k = 0; k < kSlotCount; ++k) {
            set_word_at(m_, kSlots + 4 * k, 0x0054ad5c + 4 * k);
        }
        engine(0x004dbd90);
        return;
    }
    for (std::uint32_t k = 0; k < kSlotCount; ++k) {
        set_word_at(m_, kSlots + 4 * k, kCurrentSlots + 4 * k);
    }
    engine(0x004dbe60);
}

// 0x004e4190: a button mask to be ORed into the next poll (only while the count of held frames is not running)
void PadInput::set_pending_buttons(std::uint32_t buttons) {
    if (global_int(m_, pcinput::kPendingFrames) <= 0) {
        set_global_half(m_, pcinput::kPendingWord, buttons);
    }
}

// 0x004e41b0
std::uint32_t PadInput::key_down(std::uint32_t code) { return byte_at(m_, pcinput::kKeyState + (code & 0xff)) >> 7; }

// 0x004e41d0: the first key at or after `start` that is down (0 when none, or when start is past the last code)
std::int32_t PadInput::first_key_down(std::int32_t start) {
    for (std::int32_t code = start; code <= 0xed; ++code) {
        if ((byte_at(m_, pcinput::kKeyState + (static_cast<std::uint32_t>(code) & 0xff)) & 0x80) != 0) {
            return code;
        }
    }
    return 0;
}

// 0x004e4230: the character a key types (a signed byte of the table at 0x0052f3f0)
std::int32_t PadInput::key_char(std::uint32_t code) { return sbyte_at(m_, 0x0052f3f0 + code); }

// 0x004e4250: the name of a key (a pointer from the table at 0x0052eff0)
std::uint32_t PadInput::key_name(std::uint32_t code) { return word_at(m_, 0x0052eff0 + 4 * code); }

// 0x004e4260: the first joystick button that is down, 0 to 31 (-1 when none; the scan starts at the wrapper's fourth flag dword)
std::int32_t PadInput::first_joystick_button() {
    std::int32_t index = 0;
    for (std::uint32_t offset = 0x6c; offset < 0xec; offset += 4, ++index) {
        if (word_at(m_, pcinput::kJoystick + offset) != 0) {
            return index;
        }
    }
    return -1;
}

// 0x004e4290
void PadInput::clear_pad_triggers() {
    clear_triggers(kStates);
    clear_triggers(0x0056b164);
}

// 0x004e42b0
void PadInput::enable_joystick_buttons(std::uint32_t enabled) { set_global_word(m_, pcinput::kJoystickEnabled, enabled); }

bool PadInput::chord_down(std::uint32_t slot) {
    return (engine_value(pcinput::kKeyIsDown, {pcinput::kKeyboard, word_at(m_, pcinput::kSlots + 4 * slot)}) & 0xff) != 0;
}

// 0x004e42c0: the pending button mask of this poll from the keyboard chords, the joystick (the first stick as a d-pad, the buttons) and the one-shot sources, stored for the active port
void PadInput::build_action_mask() {
    using namespace pcinput;
    set_global_half(m_, kActionMask, 0);
    set_global_half(m_, kActionMask + 2, 0);
    std::uint32_t mask = 0;
    // the order of the tests is retail's: each is a call into the keyboard wrapper
    static constexpr std::uint32_t kOrder[kSlotCount][2] = {{0, 0x10}, {3, 0x20}, {2, 0x80}, {1, 0x40}, {4, 0x1000}, {5, 0x4000}, {6, 0x8000},
                                                         {7, 0x2000}, {8, 0x4}, {9, 0x1}, {10, 0x8}, {11, 0x2}, {12, 0x800}, {13, 0x100}};
    for (const auto& entry : kOrder) {
        if (chord_down(entry[0])) {
            mask |= entry[1];
        }
    }
    if (global_byte(m_, kJoystickAcquired) != 0) {
        const std::int32_t x = global_int(m_, 0x006a42ec);
        if (x < -500) {
            mask |= 0x8000;
        } else if (x > 500) {
            mask |= 0x2000;
        }
        const std::int32_t y = global_int(m_, 0x006a42f0);
        if (y < -500) {
            mask |= 0x1000;
        } else if (y > 500) {
            mask |= 0x4000;
        }
        if (global_word(m_, kJoystickEnabled) != 0) {
            static constexpr std::uint32_t kButtons[10][2] = {{kJoyGrab, 0x20}, {kJoyKick, 0x80}, {kJoyGrind, 0x10}, {kJoyJump, 0x40},
                                                           {kJoySpinLeft, 0x4}, {kJoyNollie, 0x1}, {kJoySpinRight, 0x8}, {kJoySwitch, 0x2},
                                                           {kJoyPause, 0x800}, {kJoyCamera, 0x100}};
            for (const auto& entry : kButtons) {
                const std::int32_t button = int_at(m_, entry[0]);
                if (button >= 0 && word_at(m_, kButtonFlags + 4 * static_cast<std::uint32_t>(button)) != 0) {
                    mask |= entry[1];
                }
            }
        }
    }
    const std::uint32_t mode = global_word(m_, kControlMode);
    if (mode == 0) {
        if ((global_byte(m_, kKeyboard + 5) & 0x80) != 0 && global_byte(m_, kPauseLatch) != 0) {
            mask = 0;
        }
        if (global_word(m_, kTouchedFlag) != 0) {
            if ((mask & 0xffff) == 0) {
                set_global_byte(m_, kPauseLatch, 1);
            }
        } else {
            set_global_byte(m_, kPauseLatch, 0);
        }
    }
    const std::int32_t frames = global_int(m_, kPendingFrames);
    if (frames > 0) {
        set_global_word(m_, kPendingFrames, static_cast<std::uint32_t>(frames - 1));
    }
    if (global_half(m_, kPendingWord) != 0) {
        const std::uint32_t pending = global_word(m_, kPendingWord);
        set_global_half(m_, kPendingWord, 0);
        mask |= pending;
    }
    if (mode == 0) {
        set_half_at(m_, kActionMask + 2 * global_word(m_, kPortSelect), mask);
    } else {
        if (key_down(0x1c) != 0) {
            mask |= 0x40;
        }
        set_half_at(m_, kActionMask + 2 * global_word(m_, kPlayerPort), mask);
    }
    if (global_byte(m_, kMouseEnabled) != 0) {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        if (s_.live()) {
            // a live GetCursorPos writes the POINT it is given (retail's is a stack local)
            s_.note("win32_GetCursorPos", {kStackLocal});
            (void)s_.value("win32_GetCursorPos");
            x = global_word(m_, kStackLocal);
            y = global_word(m_, kStackLocal + 4);
        } else {
            s_.note("win32_GetCursorPos");
            x = static_cast<std::uint32_t>(s_.value("win32_GetCursorPos"));
            y = static_cast<std::uint32_t>(s_.value("win32_GetCursorPos"));
        }
        set_global_word(m_, kMousePosition, x);
        set_global_word(m_, kMousePosition + 4, y);
        engine(kMouseTracker, {x, y, global_word(m_, kMouseButton) & 0xff});
    }
}

// 0x004e4650: the pending masks of both ports as one dword; stores the two pad type bytes the player blocks read
std::uint32_t PadInput::get_action_mask() {
    const std::uint32_t types = global_word(m_, kPadTypes);
    set_global_word(m_, kPadType, types & 0xff);
    set_global_word(m_, 0x0056b2b0, (types >> 8) & 0xff);
    return (static_cast<std::uint32_t>(global_half(m_, pcinput::kActionMask + 2)) << 16) | global_half(m_, pcinput::kActionMask);
}

// 0x004e4bf0: the window's left-button messages (0x201 down, 0x202 up) set the mouse button byte
void PadInput::mouse_button_message(std::uint32_t message) {
    if (message == 0x201) {
        set_global_byte(m_, pcinput::kMouseButton, 1);
    } else if (message == 0x202) {
        set_global_byte(m_, pcinput::kMouseButton, 0);
    }
}

namespace {

constexpr std::uint32_t kJoypadSection = 0x0054b08c;        // "JOYPAD"
constexpr std::uint32_t kKeyboardSection = 0x00531138;      // "KEYBOARD"
constexpr std::uint32_t kPairs = 0x0054ae28;                // the keyboard entries: {name of the action, chord (4 key-code bytes)}, ended by a null name
constexpr std::uint32_t kNameTable = 0x0052eff0;            // the 256 key names

// the ten joystick entries of the options file in the order they are read and written: the variable holding the button number and the entry's name
struct JoystickEntry {
    std::uint32_t variable;
    std::uint32_t name;
};
constexpr JoystickEntry kJoystickEntries[10] = {
    {0x0054ae04, 0x0054aed0},     // JUMP
    {0x0054ae08, 0x00531370},     // GRIND
    {0x0054ae0c, 0x0054aec8},     // GRAB
    {0x006a6984, 0x0054aec0},     // KICK
    {0x0054ae10, 0x0054aeb4},     // SPINLEFT
    {0x0054ae18, 0x005330d4},     // NOLLIE
    {0x0054ae14, 0x0054aea8},     // SPINRIGHT
    {0x0054ae1c, 0x0054aea0},     // SWITCH
    {0x0054ae20, 0x00531310},     // PAUSE
    {0x0054ae24, 0x00531308},     // CAMERA
};

}  // namespace

// The chord's key names joined as the options file writes them; false when a fourth key is set (retail then formats nothing and the text is whatever the stack held)
bool PadInput::chord_text_into(std::uint32_t chord, std::string& text) {
    const std::uint8_t b0 = byte_at(m_, chord);
    const std::uint8_t b1 = byte_at(m_, chord + 1);
    const std::uint8_t b2 = byte_at(m_, chord + 2);
    const std::uint8_t b3 = byte_at(m_, chord + 3);
    if (b1 == 0) {
        text = key_name_text(b0);
    } else if (b2 == 0) {
        text = key_name_text(b0) + "," + key_name_text(b1);
    } else if (b3 == 0) {
        text = key_name_text(b0) + "," + key_name_text(b1) + "," + key_name_text(b2);
    } else {
        return false;
    }
    return true;
}

// 0x004e4d10: read the joystick button numbers and the keyboard chords from the options file. A button number out of range (below -1 or above 32), two actions on one button, an
// unknown key name, or a key used by another action puts the defaults back and writes them out.
void PadInput::load_bindings() {
    log_line(0x0054b094);
    std::string loaded[10];
    for (std::uint32_t k = 0; k < 10; ++k) {
        set_string(kScratchDefault, std::to_string(int_at(m_, kJoystickEntries[k].variable)));
        load_setting(kJoypadSection, kJoystickEntries[k].name, kScratchDefault, kScratchOut, 16);
        loaded[k] = m_.cstring(kScratchOut, 16);
    }
    for (std::uint32_t k = 0; k < 10; ++k) {
        std::int32_t value = 0;
        if (crt::scan_int(loaded[k], value)) {
            set_word_at(m_, kJoystickEntries[k].variable, static_cast<std::uint32_t>(value));
        }
    }
    std::int32_t numbers[10] = {};          // (a string that does not scan leaves its stack slot as it was: the probe never makes one)
    for (std::uint32_t k = 0; k < 10; ++k) {
        crt::scan_int(loaded[k], numbers[k]);
    }
    bool reset = false;
    for (std::uint32_t i = 0; i < 10 && !reset; ++i) {
        if (numbers[i] == -1) {
            continue;
        }
        for (std::uint32_t j = 0; j < 10; ++j) {
            if (i != j && numbers[i] == numbers[j]) {
                reset = true;
                break;
            }
        }
    }
    for (std::uint32_t k = 0; k < 10 && !reset; ++k) {
        const std::int32_t value = int_at(m_, kJoystickEntries[k].variable);
        reset = value < -1 || value > 0x20;
    }
    if (reset) {
        reset_bindings();
        save_bindings();
        return;
    }
    if (global_word(m_, kPairs) == 0) {
        return;
    }
    for (std::uint32_t pair = 0;; ++pair) {
        const std::uint32_t entry = kPairs + 8 * pair;
        const std::uint32_t chord = word_at(m_, entry + 4);
        std::string parts[3] = {key_name_text(0), key_name_text(0), key_name_text(0)};
        std::string current;
        if (chord_text_into(chord, current)) {
            set_string(kScratchDefault, current);
        }
        load_setting(kKeyboardSection, word_at(m_, entry), kScratchDefault, kScratchOut, 256);
        crt::Strtok tokens(m_.cstring(kScratchOut, 0x100), ",");
        std::string token;
        if (tokens.next(token)) {
            parts[0] = token;
        }
        for (std::uint32_t index = 1; index < 3; ++index) {
            if (!tokens.next(token)) {
                break;
            }
            parts[index] = token;
        }
        set_byte_at(m_, chord + 3, 0);
        for (std::uint32_t index = 0; index < 3; ++index) {
            if (crt::equal_nocase(parts[index], key_name_text(0))) {
                continue;
            }
            bool taken = false;
            for (std::uint32_t other = 0; !taken; ++other) {
                const std::uint32_t other_entry = kPairs + 8 * other;
                if (other != pair) {
                    const std::uint32_t other_chord = word_at(m_, other_entry + 4);
                    for (std::uint32_t k = 0; k < 4; ++k) {
                        if (index != k && crt::equal_nocase(key_name_text(byte_at(m_, other_chord + k)), parts[index])) {
                            taken = true;
                            break;
                        }
                    }
                }
                if (word_at(m_, other_entry + 8) == 0) {
                    break;
                }
            }
            bool known = false;
            for (std::uint32_t code = 0; code < 256; ++code) {
                if (crt::equal_nocase(parts[index], key_name_text(code))) {
                    known = true;
                    set_byte_at(m_, chord + index, code);
                    break;
                }
            }
            if (taken || !known) {
                reset_bindings();
                save_bindings();
                return;
            }
        }
        if (word_at(m_, entry + 8) == 0) {
            return;
        }
    }
}

// 0x004e5460: write the joystick button numbers and the keyboard chords to the options file
void PadInput::save_bindings() {
    for (std::uint32_t k = 0; k < 10; ++k) {
        set_string(kScratchDefault, std::to_string(int_at(m_, kJoystickEntries[k].variable)));
        save_setting(kJoypadSection, kJoystickEntries[k].name, kScratchDefault);
    }
    if (global_word(m_, kPairs) == 0) {
        return;
    }
    for (std::uint32_t pair = 0;; ++pair) {
        const std::uint32_t entry = kPairs + 8 * pair;
        std::string text;
        if (chord_text_into(word_at(m_, entry + 4), text)) {
            set_string(kScratchDefault, text);
        }
        save_setting(kKeyboardSection, word_at(m_, entry), kScratchDefault);
        if (word_at(m_, entry + 8) == 0) {
            break;
        }
    }
}

}  // namespace opentony::runtime::pcplatform
