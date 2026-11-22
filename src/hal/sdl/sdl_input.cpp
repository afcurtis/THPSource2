#include "sdl_internal.h"

#include <string.h>

// Input service on SDL3: keyboard state from the (adopted) window, mouse
// state, and up to two gamepads in the standard layout. Keyboard events
// reach SDL through the window's message procedure; poll only reads state.

namespace {

struct Pad {
    SDL_Gamepad *handle;
    SDL_JoystickID id;
};

Pad g_pads[OT_HAL_MAX_GAMEPADS];
uint32_t g_sequence = 0;
int g_ready = 0;

const struct {
    SDL_GamepadButton sdl;
    uint32_t hal;
} kButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, OT_GAMEPAD_SOUTH},
    {SDL_GAMEPAD_BUTTON_EAST, OT_GAMEPAD_EAST},
    {SDL_GAMEPAD_BUTTON_WEST, OT_GAMEPAD_WEST},
    {SDL_GAMEPAD_BUTTON_NORTH, OT_GAMEPAD_NORTH},
    {SDL_GAMEPAD_BUTTON_BACK, OT_GAMEPAD_BACK},
    {SDL_GAMEPAD_BUTTON_GUIDE, OT_GAMEPAD_GUIDE},
    {SDL_GAMEPAD_BUTTON_START, OT_GAMEPAD_START},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, OT_GAMEPAD_LEFT_STICK},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, OT_GAMEPAD_RIGHT_STICK},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, OT_GAMEPAD_LEFT_SHOULDER},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, OT_GAMEPAD_RIGHT_SHOULDER},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, OT_GAMEPAD_DPAD_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, OT_GAMEPAD_DPAD_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, OT_GAMEPAD_DPAD_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, OT_GAMEPAD_DPAD_RIGHT},
};

const SDL_GamepadAxis kAxes[OT_GAMEPAD_AXIS_COUNT] = {
    SDL_GAMEPAD_AXIS_LEFTX,        SDL_GAMEPAD_AXIS_LEFTY,         SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY,       SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

void open_pad(SDL_JoystickID id) {
    uint32_t index;
    for (index = 0; index < OT_HAL_MAX_GAMEPADS; ++index) {
        if (g_pads[index].handle != 0 && g_pads[index].id == id) {
            return; // already open
        }
    }
    for (index = 0; index < OT_HAL_MAX_GAMEPADS; ++index) {
        if (g_pads[index].handle == 0) {
            g_pads[index].handle = OT_SDL(SDL_OpenGamepad)(id);
            if (g_pads[index].handle != 0) {
                g_pads[index].id = id;
                ++ot_hal_sdl_stats_mutable()->pads_opened;
            }
            return;
        }
    }
}

int32_t round_float(float value) {
    return static_cast<int32_t>(value < 0.0f ? value - 0.5f : value + 0.5f);
}

int OT_HAL_CALL input_init(void) {
    int count = 0;
    memset(g_pads, 0, sizeof(g_pads));
    SDL_JoystickID *ids = OT_SDL(SDL_GetGamepads)(&count);
    if (ids != 0) {
        for (int index = 0; index < count; ++index) {
            open_pad(ids[index]);
        }
        OT_SDL(SDL_free)(ids);
    }
    float x;
    float y;
    OT_SDL(SDL_GetRelativeMouseState)(&x, &y); // start the relative motion from here
    g_ready = 1;
    return 1;
}

void OT_HAL_CALL input_shutdown(void) {
    for (uint32_t index = 0; index < OT_HAL_MAX_GAMEPADS; ++index) {
        if (g_pads[index].handle != 0) {
            OT_SDL(SDL_CloseGamepad)(g_pads[index].handle);
        }
    }
    memset(g_pads, 0, sizeof(g_pads));
    g_ready = 0;
}

void OT_HAL_CALL input_poll(OtHalInputState *state) {
    memset(state, 0, sizeof(*state));
    state->sequence = ++g_sequence;
    if (!g_ready) {
        return;
    }
    ot_hal_sdl_log_poll(static_cast<uint32_t>(g_sequence - 1));
    ot_hal_sdl_process_events();

    SDL_Window *window = ot_hal_sdl_window_get();
    state->focused = window != 0 && OT_SDL(SDL_GetKeyboardFocus)() == window;
    OtHalSdlStats *stats = ot_hal_sdl_stats_mutable();
    if (state->focused) {
        ++stats->focused_polls;
        int count = 0;
        const bool *keys = OT_SDL(SDL_GetKeyboardState)(&count);
        for (int usage = 0; usage < count && usage < 0x100; ++usage) {
            if (keys[usage]) {
                const uint8_t code = ot_hal_key_from_usb(static_cast<uint32_t>(usage));
                if (code != 0) {
                    state->keys[code] = OT_HAL_KEY_DOWN;
                }
            }
        }
    } else {
        ++stats->unfocused_polls;
    }

    float dx = 0.0f;
    float dy = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    OT_SDL(SDL_GetRelativeMouseState)(&dx, &dy);
    const SDL_MouseButtonFlags buttons = OT_SDL(SDL_GetMouseState)(&x, &y);
    state->mouse.dx = round_float(dx);
    state->mouse.dy = round_float(dy);
    state->mouse.x = round_float(x);
    state->mouse.y = round_float(y);
    state->mouse.buttons = ((buttons & SDL_BUTTON_LMASK) ? OT_MOUSE_LEFT : 0u) |
                           ((buttons & SDL_BUTTON_RMASK) ? OT_MOUSE_RIGHT : 0u) |
                           ((buttons & SDL_BUTTON_MMASK) ? OT_MOUSE_MIDDLE : 0u);

    for (uint32_t index = 0; index < OT_HAL_MAX_GAMEPADS; ++index) {
        SDL_Gamepad *pad = g_pads[index].handle;
        if (pad == 0) {
            continue;
        }
        OtHalGamepadState &out = state->pads[index];
        out.connected = 1;
        for (const auto &button : kButtons) {
            if (OT_SDL(SDL_GetGamepadButton)(pad, button.sdl)) {
                out.buttons |= button.hal;
            }
        }
        for (uint32_t axis = 0; axis < OT_GAMEPAD_AXIS_COUNT; ++axis) {
            out.axes[axis] = OT_SDL(SDL_GetGamepadAxis)(pad, kAxes[axis]);
        }
    }
}

int OT_HAL_CALL input_rumble(uint32_t pad, uint16_t low, uint16_t high, uint32_t duration_ms) {
    if (pad >= OT_HAL_MAX_GAMEPADS || g_pads[pad].handle == 0) {
        return 0;
    }
    return OT_SDL(SDL_RumbleGamepad)(g_pads[pad].handle, low, high, duration_ms) ? 1 : 0;
}

void OT_HAL_CALL input_warp_mouse(int32_t x, int32_t y) {
    SDL_Window *window = ot_hal_sdl_window_get();
    if (window != 0) {
        OT_SDL(SDL_WarpMouseInWindow)(window, static_cast<float>(x), static_cast<float>(y));
    }
}

} // namespace

extern "C" const OtHalInputBackend ot_hal_sdl_input = {
    "sdl", input_init, input_shutdown, input_poll, input_rumble, input_warp_mouse,
};

extern "C" uint32_t ot_hal_sdl_input_subsystems(void) {
    return SDL_INIT_GAMEPAD;
}

void ot_hal_sdl_input_pad_added(SDL_JoystickID id) {
    if (g_ready) {
        open_pad(id);
    }
}

void ot_hal_sdl_input_pad_removed(SDL_JoystickID id) {
    for (uint32_t index = 0; index < OT_HAL_MAX_GAMEPADS; ++index) {
        if (g_pads[index].handle != 0 && g_pads[index].id == id) {
            OT_SDL(SDL_CloseGamepad)(g_pads[index].handle);
            g_pads[index].handle = 0;
            g_pads[index].id = 0;
        }
    }
}
