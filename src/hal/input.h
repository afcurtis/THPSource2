#ifndef OPENTONY_HAL_INPUT_H
#define OPENTONY_HAL_INPUT_H

/* Input service (docs/HAL.md, "Input").
 *
 * Two layers, split where retail splits them:
 *
 * 1. Devices. A backend reports raw keyboard, mouse and gamepad state once
 *    per poll (OtHalInputState). Retail reads the same devices through
 *    DirectInput (the SDX keyboard/mouse/joystick wrappers under
 *    PCInput_Poll).
 * 2. Actions. The game's own binding code (PCInput_BuildActionMask) turns
 *    device state into the action bank: one 16-bit PlayStation pad word per
 *    player (OtHalPadButton). The HAL keeps that code; it does not re-bind.
 *
 * Recorded or scripted input enters at the action layer through an action
 * source, after the bindings have run, exactly where the capture recorder
 * injects today. */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The action bank: the PlayStation controller word the game logic reads
 * (ActionMask). Bits name controller positions, not tricks: which button is
 * ollie or grind is the game's configuration. */
enum OtHalPadButton {
    OT_PAD_L2 = 0x0001,
    OT_PAD_R2 = 0x0002,
    OT_PAD_L1 = 0x0004,
    OT_PAD_R1 = 0x0008,
    OT_PAD_TRIANGLE = 0x0010,
    OT_PAD_CIRCLE = 0x0020,
    OT_PAD_CROSS = 0x0040,
    OT_PAD_SQUARE = 0x0080,
    OT_PAD_SELECT = 0x0100,
    OT_PAD_L3 = 0x0200,
    OT_PAD_R3 = 0x0400,
    OT_PAD_START = 0x0800,
    OT_PAD_UP = 0x1000,
    OT_PAD_RIGHT = 0x2000,
    OT_PAD_DOWN = 0x4000,
    OT_PAD_LEFT = 0x8000
};

/* Keyboard keys are PC set-1 scan codes with 0xE0-prefixed keys at
 * 0x80 | code: the numbering the retail binding tables and saved settings
 * use (and that DirectInput calls DIK_*). A few the adapter itself needs: */
enum OtHalKey {
    OT_KEY_ESCAPE = 0x01,
    OT_KEY_Q = 0x10,
    OT_KEY_RETURN = 0x1c,
    OT_KEY_LEFT_ALT = 0x38,
    OT_KEY_F4 = 0x3e,
    OT_KEY_F9 = 0x43, /* retail screenshot key */
    OT_KEY_RIGHT_ALT = 0xb8,
    OT_KEY_UP = 0xc8,
    OT_KEY_LEFT = 0xcb,
    OT_KEY_RIGHT = 0xcd,
    OT_KEY_DOWN = 0xd0
};

#define OT_HAL_KEY_COUNT 256u
#define OT_HAL_KEY_DOWN 0x80u /* keys[] value of a held key (0 when up) */

/* USB HID keyboard usage (page 0x07, which SDL scancodes follow) to the
 * set-1 code above; 0 when the key has none (src/hal/keymap.cpp). */
uint8_t OT_HAL_CALL ot_hal_key_from_usb(uint32_t usage);

enum OtHalMouseButton {
    OT_MOUSE_LEFT = 1u << 0,
    OT_MOUSE_RIGHT = 1u << 1,
    OT_MOUSE_MIDDLE = 1u << 2
};

typedef struct OtHalMouseState {
    int32_t dx; /* relative motion since the previous poll, in mickeys */
    int32_t dy;
    int32_t x; /* cursor position in window client coordinates */
    int32_t y;
    int32_t wheel;    /* wheel steps since the previous poll */
    uint32_t buttons; /* OtHalMouseButton */
} OtHalMouseState;

/* Gamepad buttons in the common "standard gamepad" layout, named by
 * position: SOUTH is Xbox A / PlayStation cross. */
enum OtHalGamepadButton {
    OT_GAMEPAD_SOUTH = 1u << 0,
    OT_GAMEPAD_EAST = 1u << 1,
    OT_GAMEPAD_WEST = 1u << 2,
    OT_GAMEPAD_NORTH = 1u << 3,
    OT_GAMEPAD_BACK = 1u << 4,
    OT_GAMEPAD_GUIDE = 1u << 5,
    OT_GAMEPAD_START = 1u << 6,
    OT_GAMEPAD_LEFT_STICK = 1u << 7,
    OT_GAMEPAD_RIGHT_STICK = 1u << 8,
    OT_GAMEPAD_LEFT_SHOULDER = 1u << 9,
    OT_GAMEPAD_RIGHT_SHOULDER = 1u << 10,
    OT_GAMEPAD_DPAD_UP = 1u << 11,
    OT_GAMEPAD_DPAD_DOWN = 1u << 12,
    OT_GAMEPAD_DPAD_LEFT = 1u << 13,
    OT_GAMEPAD_DPAD_RIGHT = 1u << 14,
    OT_GAMEPAD_BUTTON_COUNT = 15
};

enum OtHalGamepadAxis {
    OT_GAMEPAD_LEFT_X = 0, /* -32768 (left) .. 32767 (right) */
    OT_GAMEPAD_LEFT_Y = 1, /* -32768 (up) .. 32767 (down) */
    OT_GAMEPAD_RIGHT_X = 2,
    OT_GAMEPAD_RIGHT_Y = 3,
    OT_GAMEPAD_LEFT_TRIGGER = 4, /* 0 .. 32767 */
    OT_GAMEPAD_RIGHT_TRIGGER = 5,
    OT_GAMEPAD_AXIS_COUNT = 6
};

#define OT_HAL_MAX_GAMEPADS 2u /* retail supports two joysticks */

typedef struct OtHalGamepadState {
    uint32_t connected;
    uint32_t buttons; /* OtHalGamepadButton */
    int16_t axes[OT_GAMEPAD_AXIS_COUNT];
    uint16_t reserved;
} OtHalGamepadState;

/* Everything one poll reports. */
typedef struct OtHalInputState {
    uint32_t sequence; /* increments on every poll */
    uint32_t focused;  /* the game window has keyboard focus */
    uint8_t keys[OT_HAL_KEY_COUNT];
    OtHalMouseState mouse;
    OtHalGamepadState pads[OT_HAL_MAX_GAMEPADS];
} OtHalInputState;

typedef struct OtHalInputBackend {
    const char *name;
    int(OT_HAL_CALL *init)(void); /* 1 on success; missing devices are not failures */
    void(OT_HAL_CALL *shutdown)(void);
    /* Samples every device. Events are processed by the window service's
     * pump; poll only reads the resulting state. */
    void(OT_HAL_CALL *poll)(OtHalInputState *state);
    /* Rumble: low/high frequency motor strength 0..65535 for duration_ms
     * (0 stops). Returns 0 when the pad cannot rumble. */
    int(OT_HAL_CALL *rumble)(uint32_t pad, uint16_t low, uint16_t high, uint32_t duration_ms);
    /* Moves the cursor, in window client coordinates. */
    void(OT_HAL_CALL *warp_mouse)(int32_t x, int32_t y);
} OtHalInputBackend;

/* Action source: called once per game input poll, after the game's bindings
 * have written the action bank. `action_mask` is the player's pad word
 * (OtHalPadButton in the low 16 bits); a source may rewrite it. `poll` counts
 * input polls from start-up. */
typedef void(OT_HAL_CALL *OtHalActionSource)(void *user, uint32_t player, uint32_t poll, uint32_t *action_mask);

/* Exported by opentony.dll. Registers the one action source (0 removes it).
 * Returns 1 when HAL input is active and the source will be called, 0 when
 * input is retail, so the caller keeps whatever injection it had. */
OT_HAL_EXPORT int OT_HAL_CALL ot_hal_input_set_action_source(OtHalActionSource source, void *user);
typedef int(OT_HAL_CALL *OtHalSetActionSourceFn)(OtHalActionSource source, void *user);
#define OT_HAL_SET_ACTION_SOURCE_EXPORT "ot_hal_input_set_action_source"

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_INPUT_H */
