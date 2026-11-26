// The platform of a standalone program (milestone 8a): the window, the keyboard, the mouse and the gamepad that the game's input code reads, and the clock the frame
// loop runs on. The program (opentony_spike_video.dll: standalone_video.cpp) hands the HAL's window and input backends over (ot_standalone_set_platform); this file is
// what the retail functions it replaces do with them, read from the disassembly:
//
//  * PCInput_Poll (0x004e4a90, which ReadControllers 0x00489cc0 jumps to): polls the DirectInput keyboard into the wrapper at 0x006a43e0 (the 256 key bytes at +4,
//    bit 7 = down; only while the window has the focus), the mouse (never acquired in retail), and the joystick (the wrapper at 0x006a42e8), then runs the game's own
//    binding code, PCInput_BuildActionMask (a native). Here the HAL's input state is written where DirectInput would have written it.
//  * SDXKeyboard_KeyIsDown (0x004ec6b0, thiscall on the keyboard wrapper): the chord test. The argument is a zero-terminated string of key codes; the answer is 1 when any
//    of them is down (bit 7 of the key byte), 0 when none is. 40 bytes, transcribed as they are.
//  * GetCursorPos: the HAL's mouse position, scaled from the window to the 640 x 480 display the game believes it has.
//  * the message pump of Game_FrameTick: the HAL window's events (close, focus).
//
// Scripted input, for runs with no keyboard (tests, recordings): OT_INPUT_LOG=<file>, text, one event a line, `#` starts a comment:
//     <poll> key down <hex scan code>      the key is held from the poll with that number (0 = the first PCInput_Poll)
//     <poll> key up <hex scan code>
//     <poll> mouse <x> <y> [buttons]       the pointer, in the 640 x 480 display; buttons is a bit mask (1 = left)
//     <poll> pad down|up <button>          pad 0's button, numbered as the play log's `pad button` lines (SDL's: 0 south .. 14 d-pad right; OtHalGamepadButton bit N)
//     <poll> pad axis <n> <value>          pad 0's axis n (OtHalGamepadAxis: 0..3 sticks, 4 and 5 triggers), -32768..32767; any pad line makes the pad connected
// (scripts/input_log_to_script.py turns a play log, input.txt, into this.) A scripted key counts as held whatever the window's focus is. OT_INPUT_TRACE=1 prints each poll that changed the key set, and each that changed the pad (buttons, the six axes, and the joystick x y words the game reads).
// Values hold until changed, like keys; names are the SDL numbers above (axes 0 lx, 1 ly, 2 rx, 3 ry, 4 lt, 5 rt; buttons: 0 south, 1 east, 2 west, 3 north, 4 back, 6 start, 7 l3, 8 r3, 9 l1, 10 r1, 11..14 d-pad up down left right).
//
// The clock (OT_PACING=lockstep|realtime, set by the program): lockstep, the default, moves 1/60 s a Game_FrameTick, so a run is the same on every machine; realtime
// follows the wall clock and delivers the multimedia timer's ticks as they fall due.
#include "kit_bridge.hpp"
#include "standalone_options.hpp"
#include "standalone_music.hpp"
#include "standalone_scores.hpp"
#include "seam_addresses_generated.hpp"
#include "../../runtime/host_options.hpp"

#include "input.h"
#include "window.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "guest_arena.hpp"
#include "standalone_gamepad.hpp"

namespace opentony::kitdll {
namespace {

using Args = std::vector<std::uint32_t>;

template <typename T>
volatile T& retail(std::uint32_t address) {
    return *arena::host<volatile T>(address);
}

constexpr std::uint32_t kBuildActionMask = 0x004e42c0;
constexpr std::uint32_t kKeyboard = 0x006a43e0;        // the keyboard wrapper: +4 the 256 key bytes
constexpr std::uint32_t kMouseButton = 0x006a44f8;     // the left button, as the window's WM_LBUTTONDOWN / UP leave it
constexpr std::int32_t kDisplayWidth = 640;            // the display the game is told it has, unless the program made another (display(), standalone_options.hpp)
constexpr std::int32_t kDisplayHeight = 480;

struct Platform {
    const OtHalInputBackend* input = nullptr;
    const OtHalWindowBackend* window = nullptr;
    bool realtime = false;
} g_platform;

OtHalInputState g_state;           // the last poll
std::uint32_t g_polls = 0;         // PCInput_Poll calls so far
bool g_mouse_seen = false;         // the pointer moved or a button was used: before that it rests at the centre, where X puts it on a new display
std::int32_t g_mouse_x = kDisplayWidth / 2;
std::int32_t g_mouse_y = kDisplayHeight / 2;
bool g_left_down = false;

// ---- scripted input ------------------------------------------------------------------------------------------------------------
struct ScriptEvent {
    std::uint32_t poll;
    char kind;                     // 'd' key down, 'u' key up, 'm' mouse
    std::uint32_t a, b, c;         // the scan code; or x, y, buttons
};
std::vector<ScriptEvent> g_script;
std::size_t g_script_next = 0;
std::uint8_t g_script_keys[OT_HAL_KEY_COUNT];
bool g_script_mouse = false;
std::int32_t g_script_x = 0, g_script_y = 0;
std::uint32_t g_script_buttons = 0;
bool g_script_pad = false;
std::uint32_t g_script_pad_buttons = 0;
std::int16_t g_script_pad_axes[OT_GAMEPAD_AXIS_COUNT];
bool g_script_loaded = false;

void load_script() {
    g_script_loaded = true;
    const char* path = std::getenv("OT_INPUT_LOG");
    if (path == nullptr) {
        return;
    }
    std::ifstream file(path);
    if (!file) {
        std::fprintf(stderr, "input: cannot read OT_INPUT_LOG %s\n", path);
        std::fflush(stderr);
        ExitProcess(66);
    }
    std::string line;
    unsigned number = 0;
    while (std::getline(file, line)) {
        ++number;
        line = line.substr(0, line.find('#'));
        std::istringstream words(line);
        std::string poll, what;
        if (!(words >> poll)) {
            continue;
        }
        ScriptEvent event{static_cast<std::uint32_t>(std::strtoul(poll.c_str(), nullptr, 10)), 0, 0, 0, 0};
        words >> what;
        if (what == "key") {
            std::string direction, code;
            words >> direction >> code;
            event.kind = direction == "down" ? 'd' : direction == "up" ? 'u' : 0;
            event.a = static_cast<std::uint32_t>(std::strtoul(code.c_str(), nullptr, 16)) & 0xff;
        } else if (what == "mouse") {
            event.kind = 'm';
            words >> event.a >> event.b >> event.c;
        } else if (what == "pad") {
            std::string direction, first, second;
            words >> direction >> first;
            if (direction == "axis") {
                words >> second;
                event.kind = 'a';
                event.a = static_cast<std::uint32_t>(std::strtoul(first.c_str(), nullptr, 10));
                event.b = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::strtol(second.c_str(), nullptr, 10)));
                if (event.a >= OT_GAMEPAD_AXIS_COUNT) {
                    event.kind = 0;
                }
            } else {
                event.kind = direction == "down" ? 'P' : direction == "up" ? 'Q' : 0;
                event.a = static_cast<std::uint32_t>(std::strtoul(first.c_str(), nullptr, 10));
                if (event.a >= OT_GAMEPAD_BUTTON_COUNT) {
                    event.kind = 0;
                }
            }
        }
        if (event.kind == 0) {
            std::fprintf(stderr, "input: %s:%u: not an input event: %s\n", path, number, line.c_str());
            std::fflush(stderr);
            ExitProcess(66);
        }
        g_script.push_back(event);
    }
    std::stable_sort(g_script.begin(), g_script.end(), [](const ScriptEvent& l, const ScriptEvent& r) { return l.poll < r.poll; });
}

// the events due at this poll change the held keys and the pointer; the result is merged into the HAL's state
bool apply_script(OtHalInputState& state) {
    bool changed = false;
    while (g_script_next < g_script.size() && g_script[g_script_next].poll <= g_polls) {
        const ScriptEvent& event = g_script[g_script_next++];
        changed = true;
        if (event.kind == 'm') {
            g_script_mouse = true;
            g_script_x = static_cast<std::int32_t>(event.a);
            g_script_y = static_cast<std::int32_t>(event.b);
            g_script_buttons = event.c;
        } else if (event.kind == 'P' || event.kind == 'Q') {
            g_script_pad = true;
            g_script_pad_buttons = event.kind == 'P' ? (g_script_pad_buttons | (1u << event.a)) : (g_script_pad_buttons & ~(1u << event.a));
        } else if (event.kind == 'a') {
            g_script_pad = true;
            g_script_pad_axes[event.a] = static_cast<std::int16_t>(static_cast<std::int32_t>(event.b));
        } else {
            g_script_keys[event.a] = event.kind == 'd' ? OT_HAL_KEY_DOWN : 0;
        }
    }
    for (std::uint32_t code = 0; code < OT_HAL_KEY_COUNT; ++code) {
        if (g_script_keys[code] != 0) {
            state.keys[code] = OT_HAL_KEY_DOWN;
        }
    }
    if (!g_script.empty()) {
        state.focused = 1;   // a script is the keyboard: its releases reach the key bytes too
    }
    if (g_script_mouse) {
        state.mouse.x = g_script_x;   // script coordinates are display coordinates already
        state.mouse.y = g_script_y;
        state.mouse.buttons |= g_script_buttons;
    }
    if (g_script_pad) {   // a scripted pad is the pad: connected, with the scripted buttons and axes (nothing a real one reports is merged in)
        state.pads[0].connected = 1;
        state.pads[0].buttons = g_script_pad_buttons;
        for (std::uint32_t axis = 0; axis < OT_GAMEPAD_AXIS_COUNT; ++axis) {
            state.pads[0].axes[axis] = g_script_pad_axes[axis];
        }
    }
    return changed;
}

// ---- the hotkeys (the launcher's Hotkeys tab) ------------------------------------------------------------------------------------
// OT_HOTKEY_<NAME>=<key>,<mods>,<pad>,<hold>: the key is a DirectInput code (0 none), mods a mask (1 Ctrl, 2 Alt, 4 Shift) that must also be down, pad a mask of
// OtHalGamepadButton bits (bit 15 the left trigger, bit 16 the right; every one must be held, 0 none), hold the milliseconds the key or the pad buttons must stay down
// (0 = on the press). Either the key or the pad fires it. An empty value is unbound; a name that is not set keeps its default (the screenshot's F8, the others none).
// The host acts only here, between polls: nothing reads a word the game is using.
enum Hotkey { kHotScreenshot, kHotRetry, kHotQuit, kHotFullscreen, kHotCount };
constexpr const char* kHotkeyNames[kHotCount] = {"SCREENSHOT", "RETRY", "QUIT", "FULLSCREEN"};
constexpr std::uint32_t kPadLeftTrigger = 1u << 15;
constexpr std::uint32_t kPadRightTrigger = 1u << 16;

struct HotkeyBinding {
    std::uint8_t key = 0;
    std::uint32_t mods = 0;
    std::uint32_t pad = 0;
    std::uint32_t hold = 0;
    ULONGLONG since = 0;      // when the binding became active (0 = not active)
    bool fired = false;       // fired during this activation
    bool pending = false;     // the screenshot: waiting for the video to take it
} g_hotkeys[kHotCount];
bool g_hotkeys_loaded = false;
bool g_fullscreen = false;
bool g_pause_on_focus_loss = false;

void load_hotkeys() {
    g_hotkeys_loaded = true;
    g_hotkeys[kHotScreenshot].key = 0x42;   // F8
    for (int id = 0; id < kHotCount; ++id) {
        const char* text = std::getenv((std::string("OT_HOTKEY_") + kHotkeyNames[id]).c_str());
        if (text == nullptr) {
            continue;
        }
        unsigned key = 0, mods = 0, pad = 0, hold = 0;
        HotkeyBinding& binding = g_hotkeys[id];
        binding = HotkeyBinding{};
        if (std::sscanf(text, "%u,%u,%u,%u", &key, &mods, &pad, &hold) >= 3) {
            binding.key = static_cast<std::uint8_t>(key & 0xff);
            binding.mods = mods & 7u;
            binding.pad = pad & 0x1ffffu;
            binding.hold = hold > 10000u ? 10000u : hold;
        }
    }
    const char* mode = std::getenv("OT_WINDOW_MODE");
    g_fullscreen = mode != nullptr && std::strcmp(mode, "fullscreen") == 0;
    const char* pause = std::getenv("OT_PAUSE_ON_FOCUS_LOSS");
    g_pause_on_focus_loss = pause != nullptr && std::strcmp(pause, "1") == 0;
}

bool hotkey_active(const HotkeyBinding& binding, const OtHalInputState& state) {
    if (binding.key != 0 && state.focused && (state.keys[binding.key] & OT_HAL_KEY_DOWN) != 0) {
        const auto down = [&state](std::uint8_t left, std::uint8_t right) { return (state.keys[left] & OT_HAL_KEY_DOWN) != 0 || (state.keys[right] & OT_HAL_KEY_DOWN) != 0; };
        const bool mods = ((binding.mods & 1u) == 0 || down(0x1d, 0x9d)) && ((binding.mods & 2u) == 0 || down(0x38, 0xb8)) && ((binding.mods & 4u) == 0 || down(0x2a, 0x36));
        if (mods) {
            return true;
        }
    }
    if (binding.pad != 0 && state.pads[0].connected != 0) {
        const OtHalGamepadState& pad = state.pads[0];
        const std::uint32_t buttons = pad.buttons;
        std::uint32_t held = buttons & ((1u << OT_GAMEPAD_BUTTON_COUNT) - 1u);
        if (pad.axes[OT_GAMEPAD_LEFT_TRIGGER] > 8192) {
            held |= kPadLeftTrigger;
        }
        if (pad.axes[OT_GAMEPAD_RIGHT_TRIGGER] > 8192) {
            held |= kPadRightTrigger;
        }
        return (held & binding.pad) == binding.pad;
    }
    return false;
}

void run_hotkey(int id) {
    switch (id) {
    case kHotScreenshot:
        g_hotkeys[kHotScreenshot].pending = true;
        break;
    case kHotRetry:
        if (opentony::runtime::host_options().request == 0) {
            opentony::runtime::host_options().request = 2;   // answered by the front end in a level (FrontScreens::update)
            std::printf("hotkey: retry\n");
            std::fflush(stdout);
        }
        break;
    case kHotQuit:
        std::printf("hotkey: quit after %u input polls\n", static_cast<unsigned>(g_polls));
        std::fflush(stdout);
        scores_shutdown();
        ExitProcess(0);
    case kHotFullscreen:
        if (g_platform.window != nullptr && g_platform.window->set_fullscreen != nullptr && g_platform.window->set_fullscreen(g_fullscreen ? 0 : 1) != 0) {
            g_fullscreen = !g_fullscreen;
            std::printf("hotkey: %s\n", g_fullscreen ? "fullscreen" : "windowed");
            std::fflush(stdout);
        }
        break;
    default:
        break;
    }
}

void poll_hotkeys(const OtHalInputState& state) {
    if (!g_hotkeys_loaded) {
        load_hotkeys();
    }
    const ULONGLONG now = GetTickCount64();
    for (int id = 0; id < kHotCount; ++id) {
        HotkeyBinding& binding = g_hotkeys[id];
        if (binding.key == 0 && binding.pad == 0) {
            continue;
        }
        if (!hotkey_active(binding, state)) {
            binding.since = 0;
            binding.fired = false;
            continue;
        }
        if (binding.since == 0) {
            binding.since = now;
        }
        if (!binding.fired && now - binding.since >= binding.hold) {
            binding.fired = true;
            run_hotkey(id);
        }
    }
}

// ---- the devices ---------------------------------------------------------------------------------------------------------------
// The rectangle of the window the picture covers (the video's present: the display's shape kept and centred, or the whole window when stretched)
struct Shown {
    double left, top, width, height;
};
Shown shown_rectangle() {
    const Display& d = display();
    std::int32_t width = d.width, height = d.height;
    if (g_platform.window != nullptr && g_platform.window->get_size != nullptr) {
        g_platform.window->get_size(&width, &height);
    }
    width = (std::max)(width, 1);
    height = (std::max)(height, 1);
    Shown shown = {0.0, 0.0, static_cast<double>(width), static_cast<double>(height)};
    if (!d.stretch) {
        shown.height = static_cast<double>(width) * d.height / d.width;
        if (shown.height > height) {
            shown.height = height;
            shown.width = static_cast<double>(height) * d.width / d.height;
        }
        shown.left = (width - shown.width) / 2.0;
        shown.top = (height - shown.height) / 2.0;
    }
    return shown;
}

// the pointer in window coordinates to the display the game thinks it has. The 2D layer the pointer is for (the menus, the park editor) may be drawn narrower than the
// display, in its middle (Display::hud_width): the pointer is then where the game would have drawn what is under it.
void window_to_display(std::int32_t x, std::int32_t y) {
    const Display& d = display();
    const Shown shown = shown_rectangle();
    double game_x = (x - shown.left) * d.width / shown.width;
    const double game_y = (y - shown.top) * d.height / shown.height;
    if (d.hud_width < d.width) {
        game_x = (game_x - (d.width - d.hud_width) / 2.0) * d.width / d.hud_width;
    }
    g_mouse_x = (std::min)((std::max)(static_cast<std::int32_t>(game_x), 0), d.width - 1);
    g_mouse_y = (std::min)((std::max)(static_cast<std::int32_t>(game_y), 0), d.height - 1);
}

void poll_devices() {
    if (!g_script_loaded) {
        load_script();
    }
    OtHalInputState state;
    std::memset(&state, 0, sizeof state);
    if (g_platform.input != nullptr) {
        g_platform.input->poll(&state);
    }
    const std::int32_t window_x = state.mouse.x, window_y = state.mouse.y;
    const bool changed = apply_script(state);
    static const bool trace = std::getenv("OT_INPUT_TRACE") != nullptr;
    if (trace && changed) {
        std::string held;
        for (std::uint32_t code = 0; code < OT_HAL_KEY_COUNT; ++code) {
            if (state.keys[code] != 0) {
                char text[8];
                std::snprintf(text, sizeof text, " %02x", static_cast<unsigned>(code));
                held += text;
            }
        }
        std::fprintf(stderr, "input: poll %u: keys down:%s\n", static_cast<unsigned>(g_polls), held.empty() ? " none" : held.c_str());
    }
    g_state = state;
    freelook_each_poll(state.pads[0].connected != 0, state.pads[0].axes);   // the free look's stick (standalone_freelook.cpp); nothing unless OT_FREELOOK

    // the keyboard: DirectInput only updates the key bytes while the window has the focus
    if (state.focused) {
        std::memcpy(arena::host<void>(kKeyboard + 4), state.keys, OT_HAL_KEY_COUNT);
    }
    poll_hotkeys(state);
    music_each_poll();           // the song's name (standalone_music.cpp); nothing without OT_MUSIC_NOW_PLAYING
    options_each_poll();         // the player's options a save file or a menu may have put back (standalone_options.cpp); nothing without them
    scenario_after_keyboard();   // 10f: the scenario's pulsed keys are gone at the next poll, as DirectInput's real state replaces them
    session_after_poll();   // 9: a session scenario's keys, where the hooked probe writes them (after the keyboard device update)

    // the mouse: where it is, once it has been used; the left button as the window's messages leave it
    const bool moved = state.mouse.dx != 0 || state.mouse.dy != 0;
    if (g_script_mouse || moved || state.mouse.buttons != 0) {
        g_mouse_seen = true;
    }
    if (g_mouse_seen) {
        if (g_script_mouse) {
            g_mouse_x = state.mouse.x;
            g_mouse_y = state.mouse.y;
        } else {
            window_to_display(window_x, window_y);
        }
    }
    const bool left = (state.mouse.buttons & OT_MOUSE_LEFT) != 0;
    if (left != g_left_down) {
        g_left_down = left;
        retail<std::uint8_t>(kMouseButton) = left ? 1 : 0;
    }

    // the joystick
    gamepad::update_joystick(state.pads[0]);
    if (state.pads[0].connected != 0) {
        static const bool pad_trace = std::getenv("OT_INPUT_TRACE") != nullptr;
        static std::uint32_t traced_buttons = 0;
        static std::int16_t traced_axes[OT_GAMEPAD_AXIS_COUNT];
        const OtHalGamepadState& pad = state.pads[0];
        if (pad_trace && (pad.buttons != traced_buttons || std::memcmp(pad.axes, traced_axes, sizeof traced_axes) != 0)) {
            traced_buttons = pad.buttons;
            std::memcpy(traced_axes, pad.axes, sizeof traced_axes);
            std::fprintf(stderr, "input: poll %u: pad buttons %05x axes %d %d %d %d %d %d joystick %d %d\n", static_cast<unsigned>(g_polls), static_cast<unsigned>(pad.buttons),
                         pad.axes[0], pad.axes[1], pad.axes[2], pad.axes[3], pad.axes[4], pad.axes[5],
                         static_cast<int>(retail<std::int32_t>(gamepad::kJoystick + 0x04)), static_cast<int>(retail<std::int32_t>(gamepad::kJoystick + 0x08)));
        }
    }
    ++g_polls;
}

}  // namespace

// PCInput_Poll / ReadControllers: the devices, then the game's own bindings
std::uint32_t platform_poll_controllers() {
    poll_devices();
    scenario_before_bindings();
    options_before_bindings();
    const std::uint32_t result = call_native_or_seam(kBuildActionMask, {});
    scenario_after_bindings();   // 10f: a Warehouse scenario's recorded input, where the retail capture injects it (after the bindings)
    static const bool trace = std::getenv("OT_INPUT_TRACE") != nullptr;
    static std::uint32_t last = 0;
    if (trace) {
        const std::uint32_t masks = retail<std::uint32_t>(0x006a3f1c);   // the two pending action words
        if (masks != last) {
            std::fprintf(stderr, "input: poll %u: action masks %04x %04x\n", static_cast<unsigned>(g_polls - 1), static_cast<unsigned>(masks & 0xffff),
                         static_cast<unsigned>(masks >> 16));
            last = masks;
        }
    }
    return result;
}

// SDXKeyboard_KeyIsDown(this, chord): AL = any key of the zero-terminated chord is down (0x004ec6b0)
std::uint32_t platform_key_is_down(std::uint32_t keyboard, std::uint32_t chord) {
    std::uint32_t result = 0;
    // A program that has not mapped the retail image (the movie player of tests/test_ffmpeg_movie.py reaches this through Input_UpdateActionStates, the movies' skip test)
    // has no keyboard object: no key is down.
    if (IsBadReadPtr(arena::host<const void>(keyboard + 4), 0x100) ||
        IsBadReadPtr(arena::host<const void>(chord), 1)) {
        return 0;
    }
    for (const volatile std::uint8_t* code = arena::host<const volatile std::uint8_t>(chord); *code != 0; ++code) {
        if ((retail<std::uint8_t>(keyboard + 4 + *code) & 0x80) != 0) {
            result = 1;
        }
    }
    return result;
}

// GetCursorPos(&point): the pointer on the 640 x 480 display
void platform_cursor(std::uint32_t point) {
    if (point != 0) {
        retail<std::uint32_t>(point) = static_cast<std::uint32_t>(g_mouse_x);
        retail<std::uint32_t>(point + 4) = static_cast<std::uint32_t>(g_mouse_y);
    }
}

// PCInput_SetCursorPos(x, y) (0x004e4c10, USER32's SetCursorPos behind it): the park editor's pointer frame puts the pointer back in the middle of the display (320, 240) every
// frame it scrolls. The display the game believes it has is 640 x 480; the HAL's pointer is moved to the same place of the window (a scripted pointer has no window to move).
void platform_set_cursor(std::int32_t x, std::int32_t y) {
    const Display& d = display();
    g_mouse_x = (std::min)((std::max)(x, 0), d.width - 1);
    g_mouse_y = (std::min)((std::max)(y, 0), d.height - 1);
    if (g_script_mouse || g_platform.input == nullptr || g_platform.input->warp_mouse == nullptr) {
        return;
    }
    // window_to_display, the other way
    const Shown shown = shown_rectangle();
    double game_x = g_mouse_x;
    if (d.hud_width < d.width) {
        game_x = game_x * d.hud_width / d.width + (d.width - d.hud_width) / 2.0;
    }
    g_platform.input->warp_mouse(static_cast<std::int32_t>(shown.left + game_x * shown.width / d.width),
                                 static_cast<std::int32_t>(shown.top + g_mouse_y * shown.height / d.height));
}

// Retail's Game_FrameTick and its wait loops pump the thread's Windows messages themselves (PeekMessage / TranslateMessage / DispatchMessage), so the window is never
// "not responding". SDL's own pump (below) did not keep Windows' five-second hang timer reset in this process: IsHungAppWindow was true for the idle window from about five
// seconds after the start (the player's 12 s4 run on Windows reported an application hang in the menu); with this loop it stays false. The messages go to SDL's window
// procedure, which makes them SDL events.
void platform_drain_messages() {
    if (g_platform.window == nullptr) {
        return;
    }
    MSG message;
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

// the message pump of Game_FrameTick: a closed window ends the program
void platform_pump_window() {
    platform_drain_messages();
    if (g_platform.window == nullptr) {
        return;
    }
    OtHalWindowEvent events[16];
    const std::uint32_t count = g_platform.window->pump(events, 16);
    for (std::uint32_t k = 0; k < count; ++k) {
        if (events[k].type == OT_HAL_WINDOW_EVENT_FOCUS_LOST && g_pause_on_focus_loss && opentony::runtime::host_options().request == 0) {
            opentony::runtime::host_options().request = 1;   // the pause menu, if a level is in play
            std::printf("window: focus lost, pause requested\n");
            std::fflush(stdout);
        }
        if (events[k].type == OT_HAL_WINDOW_EVENT_CLOSE_REQUESTED || events[k].type == OT_HAL_WINDOW_EVENT_QUIT) {
            std::printf("window: closed after %u input polls\n", static_cast<unsigned>(g_polls));
            std::fflush(stdout);
            scores_shutdown();   // a run held for its initials entry is written with the letters it started from
            ExitProcess(0);
        }
    }
}

bool platform_realtime() { return g_platform.realtime; }
const OtHalInputBackend* platform_input() { return g_platform.input; }
std::uint32_t platform_input_polls() { return g_polls; }

}  // namespace opentony::kitdll

// The program's window and input (the HAL's backends, already initialised), and the pacing: flags bit 0 = realtime. Null leaves a service out: no window pump, no devices.
extern "C" __declspec(dllexport) void __cdecl ot_standalone_set_platform(const OtHalInputBackend* input, const OtHalWindowBackend* window, std::uint32_t flags) {
    opentony::kitdll::g_platform.input = input;
    opentony::kitdll::g_platform.window = window;
    opentony::kitdll::g_platform.realtime = (flags & 1u) != 0;
}

// The video's screenshot key (standalone_video.cpp): true once for each press of the screenshot hotkey since the last call
extern "C" __declspec(dllexport) int __cdecl ot_standalone_take_screenshot_hotkey() {
    using namespace opentony::kitdll;
    const bool pending = g_hotkeys[kHotScreenshot].pending;
    g_hotkeys[kHotScreenshot].pending = false;
    return pending ? 1 : 0;
}
