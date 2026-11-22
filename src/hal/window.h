#ifndef OPENTONY_HAL_WINDOW_H
#define OPENTONY_HAL_WINDOW_H

/* Window service (docs/HAL.md, "Window").
 *
 * Retail: Game_Startup registers WINMAIN_WndProc, creates the window and
 * shows it; Game_FrameTick pumps Win32 messages every frame limiter spin.
 * The HAL window either creates the window itself (standalone) or adopts the
 * one the game created (hook DLL, while video is still retail DirectDraw and
 * needs the native handle). Either way the HAL turns OS events into the
 * small event set below, which the adapter feeds to the game's own logic
 * (activation, close, mouse clicks). */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

enum OtHalWindowFlags {
    OT_HAL_WINDOW_FULLSCREEN = 1u << 0,
    OT_HAL_WINDOW_HIDDEN = 1u << 1,
    OT_HAL_WINDOW_RESIZABLE = 1u << 2,
    OT_HAL_WINDOW_BORDERLESS = 1u << 3, /* no frame or title bar */
    OT_HAL_WINDOW_EXCLUSIVE = 1u << 4   /* with FULLSCREEN: the display changes to the mode closest to width x height */
};

typedef struct OtHalWindowDesc {
    const char *title; /* UTF-8 */
    int32_t width;
    int32_t height;
    uint32_t flags; /* OtHalWindowFlags */
} OtHalWindowDesc;

enum OtHalWindowEventType {
    OT_HAL_WINDOW_EVENT_NONE = 0,
    OT_HAL_WINDOW_EVENT_CLOSE_REQUESTED = 1, /* the user asked to close the window */
    OT_HAL_WINDOW_EVENT_FOCUS_GAINED = 2,
    OT_HAL_WINDOW_EVENT_FOCUS_LOST = 3,
    OT_HAL_WINDOW_EVENT_MINIMIZED = 4,
    OT_HAL_WINDOW_EVENT_RESTORED = 5,
    OT_HAL_WINDOW_EVENT_RESIZED = 6, /* data1 x data2 = new client size */
    OT_HAL_WINDOW_EVENT_QUIT = 7     /* the application should exit */
};

typedef struct OtHalWindowEvent {
    uint32_t type; /* OtHalWindowEventType */
    int32_t data1;
    int32_t data2;
} OtHalWindowEvent;

typedef struct OtHalWindowBackend {
    const char *name;
    /* The HAL owns the window. 1 on success. */
    int(OT_HAL_CALL *create)(const OtHalWindowDesc *desc);
    /* Wrap a window someone else created (native_window is an HWND on
     * Windows). The owner keeps its window procedure; the HAL sees the same
     * messages first. 1 on success. */
    int(OT_HAL_CALL *adopt)(void *native_window);
    /* Destroys a created window; releases an adopted one without destroying it. */
    void(OT_HAL_CALL *destroy)(void);
    /* Processes pending OS events for this window and the input devices;
     * writes at most `capacity` events and returns how many it wrote. Events
     * beyond capacity are kept for the next call. */
    uint32_t(OT_HAL_CALL *pump)(OtHalWindowEvent *events, uint32_t capacity);
    /* The platform window handle (HWND on Windows) for code that still needs
     * it, such as the retail DirectDraw layer; 0 when there is none. */
    void *(OT_HAL_CALL *native_handle)(void);
    void(OT_HAL_CALL *get_size)(int32_t *width, int32_t *height);
    void(OT_HAL_CALL *set_title)(const char *title);
    void(OT_HAL_CALL *show_cursor)(int shown);
    int(OT_HAL_CALL *set_fullscreen)(int fullscreen); /* 1 on success */
} OtHalWindowBackend;

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_WINDOW_H */
