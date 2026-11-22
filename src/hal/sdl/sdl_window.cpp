#include "sdl_internal.h"

#ifdef _WIN32
#include <windows.h>
#ifndef HWND_MESSAGE
#define HWND_MESSAGE ((HWND)-3) /* WINVER 0x0500; the VC6 headers predate it */
#endif
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Window service on SDL3.
//
// In the hook DLL the window is created by retail Game_Startup and pumped by
// retail Game_FrameTick (which only ever reads MainWindow's messages); video
// is still DirectDraw on that HWND. The backend therefore adopts the window:
// SDL subclasses its window procedure and sees every message first, but SDL
// never runs a message loop of its own (SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP
// = 0), so WM_QUIT and message order stay exactly as retail. SDL's own
// message-only helper windows on this thread are drained here instead, since
// the retail pump filters them out.

namespace {

SDL_Window *g_window = 0;
int g_adopted = 0;
int g_opengl = 0;
int g_vulkan = 0;

const uint32_t kQueueSize = 64u;
OtHalWindowEvent g_queue[kQueueSize];
uint32_t g_queue_head = 0;
uint32_t g_queue_count = 0;

OtHalSdlStats g_stats;

// ---- play evidence: OPENTONY_INPUT_LOG=<file> appends every key, pad button, mouse button, trigger/stick crossing and window event with its time
// (seconds since the first event), one line each and flushed at once, so the lines before a crash or hang survive it. OT_HEARTBEAT=<name> names a
// Windows file mapping the launcher watches: its first dword counts the event pumps (a stopped count is a hang).
FILE *g_input_log = 0;
int g_input_log_tried = 0;
uint64_t g_input_log_origin = 0;
uint32_t g_input_log_poll = 0;   // the input poll (0 = the first PCInput_Poll) whose pump saw the event: what OT_INPUT_LOG scripts are numbered by
signed char g_axis_zone[16];
#ifdef _WIN32
volatile LONG *g_heartbeat = 0;
int g_heartbeat_tried = 0;
#endif

const char *pad_button_name(int button) {
    static const char *const names[] = {"A/South", "B/East", "X/West", "Y/North", "Back", "Guide", "Start", "LStick", "RStick",
        "LShoulder", "RShoulder", "DUp", "DDown", "DLeft", "DRight"};
    return button >= 0 && button < 15 ? names[button] : "?";
}

void input_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void input_log(const char *format, ...) {
    if (!g_input_log_tried) {
        g_input_log_tried = 1;
        if (const char *path = getenv("OPENTONY_INPUT_LOG")) {
            g_input_log = fopen(path, "a");
        }
        g_input_log_origin = OT_SDL(SDL_GetTicksNS)();
    }
    if (g_input_log == 0) return;
    fprintf(g_input_log, "%8.3f  poll %-6u  ", (double)(OT_SDL(SDL_GetTicksNS)() - g_input_log_origin) / 1e9, (unsigned)g_input_log_poll);
    va_list args;
    va_start(args, format);
    vfprintf(g_input_log, format, args);
    va_end(args);
    fputc('\n', g_input_log);
    fflush(g_input_log);
}

void heartbeat() {
#ifdef _WIN32
    if (!g_heartbeat_tried) {
        g_heartbeat_tried = 1;
        if (const char *name = getenv("OT_HEARTBEAT")) {
            if (HANDLE mapping = OpenFileMappingA(FILE_MAP_WRITE, FALSE, name)) {
                g_heartbeat = (volatile LONG *)MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 8);
            }
        }
    }
    if (g_heartbeat != 0) InterlockedIncrement((LONG *)g_heartbeat);
#endif
}

// An analogue axis is logged only when it moves between rest (|v| < 10000), negative or positive.
void log_axis(int axis, int value) {
    if (axis < 0 || axis >= 16) return;
    const signed char zone = value > 10000 ? 1 : value < -10000 ? -1 : 0;
    if (zone != g_axis_zone[axis]) {
        g_axis_zone[axis] = zone;
        input_log("pad axis %d -> %s", axis, zone > 0 ? "+" : zone < 0 ? "-" : "rest");
    }
}

#ifdef _WIN32
const uint32_t kMaxHelpers = 8u;
HWND g_helpers[kMaxHelpers];
uint32_t g_helper_count = 0;

// SDL's message-only windows owned by this thread ("SDLHelperWindow...").
void find_helpers() {
    const DWORD thread = GetCurrentThreadId();
    HWND window = 0;
    g_helper_count = 0;
    while ((window = FindWindowExA(HWND_MESSAGE, window, 0, 0)) != 0 && g_helper_count < kMaxHelpers) {
        char name[64];
        DWORD process = 0;
        if (GetWindowThreadProcessId(window, &process) != thread || process != GetCurrentProcessId()) {
            continue;
        }
        if (GetClassNameA(window, name, sizeof(name)) > 3 && memcmp(name, "SDL", 3) == 0) {
            g_helpers[g_helper_count++] = window;
        }
    }
}

void drain_helpers() {
    MSG message;
    for (uint32_t index = 0; index < g_helper_count; ++index) {
        while (PeekMessageA(&message, g_helpers[index], 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
            ++g_stats.helper_messages;
        }
    }
}
#endif

void push(uint32_t type, int32_t data1, int32_t data2) {
    if (g_queue_count == kQueueSize) {
        g_queue_head = (g_queue_head + 1u) % kQueueSize; // drop the oldest
        --g_queue_count;
    }
    OtHalWindowEvent &event = g_queue[(g_queue_head + g_queue_count) % kQueueSize];
    event.type = type;
    event.data1 = data1;
    event.data2 = data2;
    ++g_queue_count;
    ++g_stats.window_events;
}

bool ours(SDL_WindowID id) {
    return g_window != 0 && OT_SDL(SDL_GetWindowID)(g_window) == id;
}

int OT_HAL_CALL window_create(const OtHalWindowDesc *desc) {
    if (g_window != 0) {
        return 0;
    }
    SDL_PropertiesID properties = OT_SDL(SDL_CreateProperties)();
    OT_SDL(SDL_SetStringProperty)(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, desc->title ? desc->title : "");
    OT_SDL(SDL_SetNumberProperty)(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, desc->width);
    OT_SDL(SDL_SetNumberProperty)(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, desc->height);
    OT_SDL(SDL_SetBooleanProperty)(properties, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,
        (desc->flags & OT_HAL_WINDOW_FULLSCREEN) != 0);
    OT_SDL(SDL_SetBooleanProperty)(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN,
        (desc->flags & OT_HAL_WINDOW_HIDDEN) != 0);
    OT_SDL(SDL_SetBooleanProperty)(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN,
        (desc->flags & OT_HAL_WINDOW_RESIZABLE) != 0);
    OT_SDL(SDL_SetBooleanProperty)(properties, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN,
        (desc->flags & OT_HAL_WINDOW_BORDERLESS) != 0);
    OT_SDL(SDL_SetBooleanProperty)(properties, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, g_opengl != 0);
    OT_SDL(SDL_SetBooleanProperty)(properties, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, g_vulkan != 0);
    g_window = OT_SDL(SDL_CreateWindowWithProperties)(properties);
    OT_SDL(SDL_DestroyProperties)(properties);
    g_adopted = 0;
    // The game draws its own pointer (menus, park editor) at the position it reads from the HAL; the desktop pointer on top of it
    // is a second, differently placed one whenever the window is not 640 x 480 or is letterboxed. SDL hides it over this window only.
    // OT_SHOW_CURSOR=1 keeps it for debugging.
    if (g_window != 0 && getenv("OT_SHOW_CURSOR") == 0) {
        OT_SDL(SDL_HideCursor)();
    }
    // SDL's fullscreen is the desktop's size; an exclusive one changes the display to the mode closest to the size asked for
    if (g_window != 0 && (desc->flags & OT_HAL_WINDOW_FULLSCREEN) != 0 && (desc->flags & OT_HAL_WINDOW_EXCLUSIVE) != 0) {
        SDL_DisplayMode mode;
        if (OT_SDL(SDL_GetClosestFullscreenDisplayMode)(OT_SDL(SDL_GetDisplayForWindow)(g_window), desc->width, desc->height, 0.0f, true, &mode)) {
            OT_SDL(SDL_SetWindowFullscreenMode)(g_window, &mode);
        }
    }
#ifdef _WIN32
    find_helpers(); // with the message loop off, this window's messages are drained with the helpers
#endif
    return g_window != 0;
}

int OT_HAL_CALL window_adopt(void *native_window) {
    if (g_window != 0 || native_window == 0) {
        return 0;
    }
    SDL_PropertiesID properties = OT_SDL(SDL_CreateProperties)();
#ifdef _WIN32
    OT_SDL(SDL_SetPointerProperty)(properties, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, native_window);
#endif
    // With OpenGL the adopted window gets a GL pixel format (video=sdl draws into it);
    // with Vulkan it gets a surface the SDL_GPU backend can claim.
    OT_SDL(SDL_SetBooleanProperty)(properties, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, g_opengl != 0);
    OT_SDL(SDL_SetBooleanProperty)(properties, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, g_vulkan != 0);
    g_window = OT_SDL(SDL_CreateWindowWithProperties)(properties);
    OT_SDL(SDL_DestroyProperties)(properties);
    if (g_window == 0) {
        return 0;
    }
    g_adopted = 1;
#ifdef _WIN32
    find_helpers();
#endif
    return 1;
}

void OT_HAL_CALL window_destroy(void) {
    if (g_window != 0) {
        OT_SDL(SDL_DestroyWindow)(g_window); // an adopted HWND gets its window procedure back
        g_window = 0;
    }
    g_adopted = 0;
    g_queue_head = 0;
    g_queue_count = 0;
}

uint32_t OT_HAL_CALL window_pump(OtHalWindowEvent *events, uint32_t capacity) {
    uint32_t written = 0;
    ot_hal_sdl_process_events();
    while (written < capacity && g_queue_count != 0) {
        events[written++] = g_queue[g_queue_head];
        g_queue_head = (g_queue_head + 1u) % kQueueSize;
        --g_queue_count;
    }
    return written;
}

void *OT_HAL_CALL window_native_handle(void) {
    if (g_window == 0) {
        return 0;
    }
#ifdef _WIN32
    return OT_SDL(SDL_GetPointerProperty)(OT_SDL(SDL_GetWindowProperties)(g_window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, 0);
#else
    return 0;
#endif
}

void OT_HAL_CALL window_get_size(int32_t *width, int32_t *height) {
    int w = 0;
    int h = 0;
    if (g_window != 0) {
        OT_SDL(SDL_GetWindowSize)(g_window, &w, &h);
    }
    *width = w;
    *height = h;
}

void OT_HAL_CALL window_set_title(const char *title) {
    if (g_window != 0) {
        OT_SDL(SDL_SetWindowTitle)(g_window, title);
    }
}

void OT_HAL_CALL window_show_cursor(int shown) {
    if (shown) {
        OT_SDL(SDL_ShowCursor)();
    } else {
        OT_SDL(SDL_HideCursor)();
    }
}

int OT_HAL_CALL window_set_fullscreen(int fullscreen) {
    // An adopted window belongs to the retail DirectDraw layer, which owns
    // the display mode.
    return g_window != 0 && !g_adopted && OT_SDL(SDL_SetWindowFullscreen)(g_window, fullscreen != 0);
}

} // namespace

extern "C" const OtHalWindowBackend ot_hal_sdl_window = {
    "sdl",
    window_create,
    window_adopt,
    window_destroy,
    window_pump,
    window_native_handle,
    window_get_size,
    window_set_title,
    window_show_cursor,
    window_set_fullscreen,
};

extern "C" uint32_t ot_hal_sdl_window_subsystems(void) {
    return SDL_INIT_VIDEO;
}

extern "C" void ot_hal_sdl_window_request_opengl(int enabled) {
    g_opengl = enabled;
}

extern "C" void ot_hal_sdl_window_request_vulkan(int enabled) {
    g_vulkan = enabled;
}

extern "C" void ot_hal_sdl_window_hints(void) {
    OT_SDL(SDL_SetHint)(SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP, "0");
}

extern "C" void ot_hal_sdl_log_poll(uint32_t poll) {
    g_input_log_poll = poll;
}

extern "C" void ot_hal_sdl_process_events(void) {
    SDL_Event event;
#ifdef _WIN32
    drain_helpers();
#endif
    OT_SDL(SDL_PumpEvents)();
    heartbeat();
    while (OT_SDL(SDL_PollEvent)(&event)) {
        ++g_stats.events;
        switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (!event.key.repeat) input_log("key %-4s %s (scancode %d)", event.key.down ? "down" : "up", OT_SDL(SDL_GetScancodeName)(event.key.scancode), (int)event.key.scancode);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            input_log("pad button %-4s %s (%d)", event.gbutton.down ? "down" : "up", pad_button_name(event.gbutton.button), (int)event.gbutton.button);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            log_axis(event.gaxis.axis, event.gaxis.value);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            input_log("mouse button %d %s at %d,%d", (int)event.button.button, event.button.down ? "down" : "up", (int)event.button.x, (int)event.button.y);
            break;
        case SDL_EVENT_QUIT:
            input_log("window: quit");
            push(OT_HAL_WINDOW_EVENT_QUIT, 0, 0);
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (ours(event.window.windowID)) push(OT_HAL_WINDOW_EVENT_CLOSE_REQUESTED, 0, 0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (ours(event.window.windowID)) input_log("window: focus gained");
            if (ours(event.window.windowID)) push(OT_HAL_WINDOW_EVENT_FOCUS_GAINED, 0, 0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (ours(event.window.windowID)) input_log("window: focus lost");
            if (ours(event.window.windowID)) push(OT_HAL_WINDOW_EVENT_FOCUS_LOST, 0, 0);
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            if (ours(event.window.windowID)) input_log("window: minimized");
            if (ours(event.window.windowID)) push(OT_HAL_WINDOW_EVENT_MINIMIZED, 0, 0);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            if (ours(event.window.windowID)) input_log("window: restored");
            if (ours(event.window.windowID)) push(OT_HAL_WINDOW_EVENT_RESTORED, 0, 0);
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            if (ours(event.window.windowID)) push(OT_HAL_WINDOW_EVENT_RESIZED, event.window.data1, event.window.data2);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            ot_hal_sdl_input_pad_added(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            ot_hal_sdl_input_pad_removed(event.gdevice.which);
            break;
        default:
            break;
        }
    }
}

SDL_Window *ot_hal_sdl_window_get(void) {
    return g_window;
}

OtHalSdlStats *ot_hal_sdl_stats_mutable(void) {
    return &g_stats;
}

extern "C" const OtHalSdlStats *ot_hal_sdl_stats(void) {
    return &g_stats;
}
