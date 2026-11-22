#include "hal_adapters.h"

#include "../hook_runtime.h"

#include <string.h>

// Input adapters (input=sdl). DirectInput is never created: the adapters keep
// the retail SDX wrapper records (re/symbols/globals.yml) filled exactly as
// the DirectInput wrappers would, from the HAL input state, and run the
// retail binding code (PCInput_BuildActionMask) unchanged. Recorded input
// enters through the action source right after the bindings.
//
// Device semantics kept from retail:
// - keyboard: exclusive/foreground DirectInput only updates the key array
//   while the window has focus; otherwise the previous bytes stay;
// - mouse: retail never acquires the SDX mouse (its acquire is reachable only
//   from its own update), so the mouse record is initialized and left alone;
// - joystick: DIJOYSTATE axes in -1000..1000, buttons in DirectInput order
//   for a standard pad (A, B, X, Y, LB, RB, Back, Start, LS, RS), plus the
//   triggers as buttons 10 and 11 and the d-pad on the axes.

namespace {

// SDXInput / KeyboardDevice / SDXMouse / SDXJoystick field offsets.
const uint32_t kInputKeyboard = 0x08;
const uint32_t kInputMouse = 0x0c;
const uint32_t kInputInstance = 0x10;
const uint32_t kInputWindow = 0x14;
const uint32_t kInputJoystickFound = 0x245c;
const uint32_t kInputForceFeedbackFound = 0x245d;
const uint32_t kKeyboardState = 0x004;
const uint32_t kKeyboardInput = 0x104;
const uint32_t kKeyboardAcquired = 0x10c;
const uint32_t kMouseSensitivity = 0x0c;
const uint32_t kMouseInput = 0x24;
const uint32_t kJoystickX = 0x04; // DIJOYSTATE.lX
const uint32_t kJoystickY = 0x08; // DIJOYSTATE.lY
const uint32_t kJoystickButtons = 0x34; // DIJOYSTATE.rgbButtons[32]
const uint32_t kJoystickXCopy = 0x64;
const uint32_t kJoystickYCopy = 0x68;
const uint32_t kJoystickButtonFlags = 0x6c; // 32 dwords
const uint32_t kJoystickAcquired = 0xec;
const uint32_t kJoystickButtonCount = 32;
const int32_t kJoystickRange = 1000;

const uint32_t kMouseSensitivityDefault = 0x80;
const int kMouseRight = 0x260;
const int kMouseBottom = 0x1c0;
const uint8_t kScreenshotKey = OT_KEY_F9;
const int16_t kTriggerThreshold = 8192;

typedef void(__cdecl *VoidFunction)(void);
typedef void(__cdecl *PortFunction)(int);
typedef void(__fastcall *SetBoundsFunction)(void *self, void *unused_edx, int left, int top, int right, int bottom);

OtHalActionSource g_source = 0;
void *g_source_user = 0;
int g_active = 0;
uint32_t g_polls = 0;
uint32_t g_source_changes = 0;
uint32_t g_joystick_connected = 0;
uint32_t g_key_polls = 0;    // polls with at least one key down
uint32_t g_last_key = 0;     // last set-1 code seen down
uint32_t g_pad_polls = 0;    // polls with a pad button or axis active

template <typename T>
T &field(unsigned char *base, uint32_t offset) {
    return *reinterpret_cast<T *>(base + offset);
}

int32_t joystick_axis(int16_t value, uint32_t buttons, uint32_t negative, uint32_t positive) {
    if (buttons & negative) {
        return -kJoystickRange;
    }
    if (buttons & positive) {
        return kJoystickRange;
    }
    int32_t scaled = static_cast<int32_t>(value) * kJoystickRange / 32767;
    return scaled < -kJoystickRange ? -kJoystickRange : scaled;
}

void write_joystick(unsigned char *joystick, const OtHalGamepadState &pad) {
    const uint32_t buttons = pad.buttons;
    const bool pressed[] = {
        (buttons & OT_GAMEPAD_SOUTH) != 0,
        (buttons & OT_GAMEPAD_EAST) != 0,
        (buttons & OT_GAMEPAD_WEST) != 0,
        (buttons & OT_GAMEPAD_NORTH) != 0,
        (buttons & OT_GAMEPAD_LEFT_SHOULDER) != 0,
        (buttons & OT_GAMEPAD_RIGHT_SHOULDER) != 0,
        (buttons & OT_GAMEPAD_BACK) != 0,
        (buttons & OT_GAMEPAD_START) != 0,
        (buttons & OT_GAMEPAD_LEFT_STICK) != 0,
        (buttons & OT_GAMEPAD_RIGHT_STICK) != 0,
        pad.axes[OT_GAMEPAD_LEFT_TRIGGER] > kTriggerThreshold,
        pad.axes[OT_GAMEPAD_RIGHT_TRIGGER] > kTriggerThreshold,
    };
    const int32_t x = joystick_axis(pad.axes[OT_GAMEPAD_LEFT_X], buttons, OT_GAMEPAD_DPAD_LEFT, OT_GAMEPAD_DPAD_RIGHT);
    const int32_t y = joystick_axis(pad.axes[OT_GAMEPAD_LEFT_Y], buttons, OT_GAMEPAD_DPAD_UP, OT_GAMEPAD_DPAD_DOWN);
    field<int32_t>(joystick, kJoystickX) = x;
    field<int32_t>(joystick, kJoystickY) = y;
    for (uint32_t index = 0; index < kJoystickButtonCount; ++index) {
        const bool down = index < sizeof(pressed) / sizeof(pressed[0]) && pressed[index];
        joystick[kJoystickButtons + index] = down ? 0x80 : 0x00;
        field<uint32_t>(joystick, kJoystickButtonFlags + index * 4u) = down ? 1u : 0u;
    }
    field<int32_t>(joystick, kJoystickXCopy) = x;
    field<int32_t>(joystick, kJoystickYCopy) = y;
}

void clear_joystick(unsigned char *joystick) {
    field<int32_t>(joystick, kJoystickX) = 0;
    field<int32_t>(joystick, kJoystickY) = 0;
    memset(joystick + kJoystickButtons, 0, kJoystickButtonCount);
    memset(joystick + kJoystickButtonFlags, 0, kJoystickButtonCount * 4u);
    field<int32_t>(joystick, kJoystickXCopy) = 0;
    field<int32_t>(joystick, kJoystickYCopy) = 0;
}

void __cdecl hal_PCInput_Initialize(HWND window, HINSTANCE instance) {
    unsigned char *input = ot_hal_data(OT_HAL_SYM_SDXInput);
    unsigned char *keyboard = ot_hal_data(OT_HAL_SYM_KeyboardDevice);
    unsigned char *mouse = ot_hal_data(OT_HAL_SYM_SDXMouse);
    unsigned char *joystick = ot_hal_data(OT_HAL_SYM_SDXJoystick);

    // Game logic first, exactly as retail.
    ot_hal_function<PortFunction>(OT_HAL_SYM_Options_ResetControllerDefaults)(0);
    ot_hal_function<PortFunction>(OT_HAL_SYM_Options_ResetControllerDefaults)(1);
    ot_hal_function<VoidFunction>(OT_HAL_SYM_PCInput_ResetBindings)();
    ot_hal_function<VoidFunction>(OT_HAL_SYM_PCInput_LoadBindings)();
    // Retail SetCapture(window): the HAL does not capture the mouse.

    // The wrapper records as the DirectInput create/acquire calls leave them.
    field<HINSTANCE>(input, kInputInstance) = instance;
    field<HWND>(input, kInputWindow) = window;
    field<unsigned char *>(input, kInputKeyboard) = keyboard;
    field<unsigned char *>(keyboard, kKeyboardInput) = input;
    keyboard[kKeyboardAcquired] = 1;
    field<unsigned char *>(input, kInputMouse) = mouse;
    field<unsigned char *>(mouse, kMouseInput) = input;

    const OtHalInputBackend *backend = ot_hal_input_backend();
    OtHalInputState state;
    if (backend != 0) {
        backend->poll(&state);
    } else {
        memset(&state, 0, sizeof(state));
    }
    g_joystick_connected = state.pads[0].connected;
    input[kInputJoystickFound] = static_cast<unsigned char>(g_joystick_connected ? 1 : 0);
    input[kInputForceFeedbackFound] = 0; // rumble goes through the HAL, not DirectInput effects
    joystick[kJoystickAcquired] = static_cast<unsigned char>(g_joystick_connected ? 1 : 0);

    ot_hal_function<SetBoundsFunction>(OT_HAL_SYM_SDXMouse_SetBounds)(mouse, 0, 0, 0, kMouseRight, kMouseBottom);
    field<uint32_t>(mouse, kMouseSensitivity) = kMouseSensitivityDefault;
    ot_log("hal input: PCInput_Initialize (window %08X, gamepad %s)",
        static_cast<unsigned>(reinterpret_cast<UINT_PTR>(window)), g_joystick_connected ? "connected" : "none");
}

void __cdecl hal_PCInput_Poll(void) {
    unsigned char *keyboard = ot_hal_data(OT_HAL_SYM_KeyboardDevice);
    unsigned char *joystick = ot_hal_data(OT_HAL_SYM_SDXJoystick);
    const OtHalInputBackend *backend = ot_hal_input_backend();
    OtHalInputState state;

    ot_hal_test_input_poll(g_polls);
    if (backend != 0) {
        backend->poll(&state);
    } else {
        memset(&state, 0, sizeof(state));
    }
    if (state.focused) {
        memcpy(keyboard + kKeyboardState, state.keys, OT_HAL_KEY_COUNT);
        for (uint32_t code = 0; code < OT_HAL_KEY_COUNT; ++code) {
            if (state.keys[code] != 0) {
                if (g_key_polls == 0) {
                    ot_log("hal input: first key down 0x%02X", (unsigned)code);
                }
                g_last_key = code;
                ++g_key_polls;
                break;
            }
        }
    }
    if (state.pads[0].buttons != 0 || state.pads[0].axes[OT_GAMEPAD_LEFT_X] > 8000 ||
        state.pads[0].axes[OT_GAMEPAD_LEFT_X] < -8000) {
        ++g_pad_polls;
    }
    if (state.pads[0].connected) {
        write_joystick(joystick, state.pads[0]);
    } else if (g_joystick_connected) {
        clear_joystick(joystick);
    }
    g_joystick_connected = state.pads[0].connected;
    joystick[kJoystickAcquired] = static_cast<unsigned char>(g_joystick_connected ? 1 : 0);

    ot_hal_function<VoidFunction>(OT_HAL_SYM_PCInput_BuildActionMask)();

    if (g_source != 0) {
        volatile uint16_t *mask = reinterpret_cast<volatile uint16_t *>(ot_hal_data(OT_HAL_SYM_ActionMask));
        uint32_t value = mask[0];
        g_source(g_source_user, 0, g_polls, &value);
        if (static_cast<uint16_t>(value) != mask[0]) {
            mask[0] = static_cast<uint16_t>(value);
            ++g_source_changes;
        }
    }
    ++g_polls;

    if (keyboard[kKeyboardState + kScreenshotKey] & OT_HAL_KEY_DOWN) {
        ot_hal_function<VoidFunction>(OT_HAL_SYM_D3D_SaveScreenshot)();
    }
}

void __cdecl hal_PCInput_Shutdown(void) {
    const OtHalInputBackend *backend = ot_hal_input_backend();
    if (backend != 0) {
        backend->rumble(0, 0, 0, 0);
        backend->rumble(1, 0, 0, 0);
    }
    ot_hal_data(OT_HAL_SYM_KeyboardDevice)[kKeyboardAcquired] = 0;
    ot_hal_data(OT_HAL_SYM_SDXJoystick)[kJoystickAcquired] = 0;
    // Retail ReleaseCapture(): nothing was captured.
}

// Retail: (unused, strength, port, duration) -> a DirectInput constant force
// on ports 0 and 1 when a force-feedback joystick was found.
void __cdecl hal_PCInput_ActuatorOn(uint32_t unused, uint16_t strength, uint8_t port, uint32_t duration) {
    (void)unused;
    (void)duration;
    const OtHalInputBackend *backend = ot_hal_input_backend();
    if (backend == 0 || port > 1) {
        return;
    }
    const uint32_t level = strength > 255u ? 255u : strength;
    const uint16_t motor = static_cast<uint16_t>(level * 257u);
    backend->rumble(port, motor, motor, 1000u); // until PCInput_ActuatorOff, at most a second
}

void __cdecl hal_PCInput_ActuatorOff(void) {
    const OtHalInputBackend *backend = ot_hal_input_backend();
    if (backend != 0) {
        backend->rumble(0, 0, 0, 0);
        backend->rumble(1, 0, 0, 0);
    }
}

// Retail SetCursorPos (screen coordinates; the game runs full screen, where
// screen and window coordinates coincide).
void __cdecl hal_PCInput_SetCursorPos(int x, int y) {
    const OtHalInputBackend *backend = ot_hal_input_backend();
    if (backend != 0) {
        backend->warp_mouse(x, y);
    }
}

} // namespace

OT_REGISTER_HAL_ADAPTER(PCInput_Initialize, hal_PCInput_Initialize)
OT_REGISTER_HAL_ADAPTER(PCInput_Poll, hal_PCInput_Poll)
OT_REGISTER_HAL_ADAPTER(PCInput_Shutdown, hal_PCInput_Shutdown)
OT_REGISTER_HAL_ADAPTER(PCInput_ActuatorOn, hal_PCInput_ActuatorOn)
OT_REGISTER_HAL_ADAPTER(PCInput_ActuatorOff, hal_PCInput_ActuatorOff)
OT_REGISTER_HAL_ADAPTER(PCInput_SetCursorPos, hal_PCInput_SetCursorPos)

int ot_hal_input_adapter_start(void) {
    const OtHalInputBackend *backend = ot_hal_input_backend();
    g_active = backend != 0 && backend->init();
    return g_active;
}

void ot_hal_input_adapter_stop(void) {
    const OtHalInputBackend *backend = ot_hal_input_backend();
    if (g_active && backend != 0) {
        backend->shutdown();
    }
    g_active = 0;
}

int ot_hal_input_adapter_describe(char *out, int size) {
    char line[160];
    const int length = wsprintfA(line, "input polls=%u source=%s source_changes=%u gamepad=%u key_polls=%u last_key=0x%02X pad_polls=%u",
        g_polls, g_source != 0 ? "registered" : "none", g_source_changes, g_joystick_connected, g_key_polls,
        g_last_key, g_pad_polls);
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}

// Exported from opentony.dll: see src/hal/input.h.
extern "C" OT_HAL_EXPORT int OT_HAL_CALL ot_hal_input_set_action_source(OtHalActionSource source, void *user) {
    const OtHalConfig *config = ot_hal_config();
    if (config->backend[OT_HAL_INPUT] != OT_HAL_SDL) {
        return 0;
    }
    g_source = source;
    g_source_user = user;
    ot_log("hal input: action source %s", source != 0 ? "registered" : "removed");
    return 1;
}
