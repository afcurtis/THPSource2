#pragma once

#include "guest_arena.hpp"
#include "../../hal/input.h"

#include <cstdlib>
#include <cstring>

namespace opentony::kitdll::gamepad {

constexpr std::uint32_t kJoystick = 0x006a42e8;  // the game's DIJOYSTATE wrapper
constexpr std::uint32_t kJoystickAcquired = 0x006a43d4;
constexpr std::uint32_t kJoystickRange = 1000;

inline std::int32_t joystick_axis(std::int16_t value, std::uint32_t buttons, std::uint32_t negative, std::uint32_t positive) {
    if (buttons & negative) {
        return -static_cast<std::int32_t>(kJoystickRange);
    }
    if (buttons & positive) {
        return static_cast<std::int32_t>(kJoystickRange);
    }
    const std::int32_t scaled = static_cast<std::int32_t>(value) * static_cast<std::int32_t>(kJoystickRange) / 32767;
    return scaled < -static_cast<std::int32_t>(kJoystickRange) ? -static_cast<std::int32_t>(kJoystickRange) : scaled;
}

// DIJOYSTATE as the hooked build's input adapter writes it (src/hooks/hal/hal_input.cpp): the stick (or the d-pad) in -1000..1000 and the buttons numbered as the game's
// default JOYPAD bindings (JUMP=1 KICK=0 GRAB=2 GRIND=4 SPINLEFT=3 SPINRIGHT=5 NOLLIE=6 SWITCH=7) expect: the layout of the console game on an Xbox pad, A ollie, X flip,
// B grab, Y grind, LB / RB spin, LT / RT nollie and switch. (SDL's own order, A B X Y, made B the ollie and Start a switch.) Back and Start follow as 8 and 9.
inline void write_joystick(const OtHalGamepadState& pad) {
    const std::uint32_t buttons = pad.buttons;
    const bool pressed[] = {
        (buttons & OT_GAMEPAD_WEST) != 0,           (buttons & OT_GAMEPAD_SOUTH) != 0,         (buttons & OT_GAMEPAD_EAST) != 0,
        (buttons & OT_GAMEPAD_LEFT_SHOULDER) != 0,  (buttons & OT_GAMEPAD_NORTH) != 0,         (buttons & OT_GAMEPAD_RIGHT_SHOULDER) != 0,
        pad.axes[OT_GAMEPAD_LEFT_TRIGGER] > 8192,   pad.axes[OT_GAMEPAD_RIGHT_TRIGGER] > 8192, (buttons & OT_GAMEPAD_BACK) != 0,
        (buttons & OT_GAMEPAD_START) != 0,          (buttons & OT_GAMEPAD_LEFT_STICK) != 0,    (buttons & OT_GAMEPAD_RIGHT_STICK) != 0,
    };
    // OT_PAD_STICK left (the default) | right | none: the stick that moves the skater and the menus besides the d-pad (PARTYMOD's MovementStick; the launcher sets it)
    static const int stick = [] {
        const char* wanted = std::getenv("OT_PAD_STICK");
        return wanted == nullptr ? 0 : std::strcmp(wanted, "right") == 0 ? 1 : std::strcmp(wanted, "none") == 0 ? -1 : 0;
    }();
    const std::int16_t stick_x = stick == 0 ? pad.axes[OT_GAMEPAD_LEFT_X] : stick == 1 ? pad.axes[OT_GAMEPAD_RIGHT_X] : 0;
    const std::int16_t stick_y = stick == 0 ? pad.axes[OT_GAMEPAD_LEFT_Y] : stick == 1 ? pad.axes[OT_GAMEPAD_RIGHT_Y] : 0;
    const std::int32_t x = joystick_axis(stick_x, buttons, OT_GAMEPAD_DPAD_LEFT, OT_GAMEPAD_DPAD_RIGHT);
    const std::int32_t y = joystick_axis(stick_y, buttons, OT_GAMEPAD_DPAD_UP, OT_GAMEPAD_DPAD_DOWN);
    arena::at<std::int32_t>(kJoystick + 0x04) = x;
    arena::at<std::int32_t>(kJoystick + 0x08) = y;
    for (std::uint32_t index = 0; index < 32; ++index) {
        const bool down = index < sizeof pressed / sizeof pressed[0] && pressed[index];
        arena::at<std::uint8_t>(kJoystick + 0x34 + index) = down ? 0x80 : 0x00;
        arena::at<std::uint32_t>(kJoystick + 0x6c + 4 * index) = down ? 1u : 0u;
    }
    arena::at<std::int32_t>(kJoystick + 0x64) = x;
    arena::at<std::int32_t>(kJoystick + 0x68) = y;
}

inline void clear_joystick() {
    arena::at<std::int32_t>(kJoystick + 0x04) = 0;
    arena::at<std::int32_t>(kJoystick + 0x08) = 0;
    for (std::uint32_t index = 0; index < 32; ++index) {
        arena::at<std::uint8_t>(kJoystick + 0x34 + index) = 0;
        arena::at<std::uint32_t>(kJoystick + 0x6c + 4 * index) = 0;
    }
    arena::at<std::int32_t>(kJoystick + 0x64) = 0;
    arena::at<std::int32_t>(kJoystick + 0x68) = 0;
}

inline void update_joystick(const OtHalGamepadState& pad) {
    if (pad.connected) {
        write_joystick(pad);
    } else if (arena::at<std::uint8_t>(kJoystickAcquired) != 0) {
        clear_joystick();
    }
    arena::at<std::uint8_t>(kJoystickAcquired) = pad.connected ? 1 : 0;
}

}  // namespace opentony::kitdll::gamepad
