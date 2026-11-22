#ifndef OPENTONY_HAL_SDL_BACKENDS_H
#define OPENTONY_HAL_SDL_BACKENDS_H

/* The SDL3 implementations of the HAL services (docs/HAL.md). Every SDL call
 * goes through OT_SDL (sdl_api.h); the caller loads SDL and calls SDL_Init
 * before using a backend. */

#include "../audio.h"
#include "../clock.h"
#include "../input.h"
#include "../window.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const OtHalClockBackend ot_hal_sdl_clock;
extern const OtHalWindowBackend ot_hal_sdl_window;
extern const OtHalInputBackend ot_hal_sdl_input;
/* Initializes SDL's audio subsystem itself (init), falling back to SDL's
 * "dummy" driver when there is no device, so voices still advance in time. */
extern const OtHalAudioBackend ot_hal_sdl_audio;

/* The SDL audio driver in use ("wasapi", "dummy", ...; "" before init), and
 * the backend's counters, for the stats line. */
const char *ot_hal_sdl_audio_driver(void);
typedef struct OtHalSdlAudioStats {
    uint32_t sounds;
    uint32_t voices;
    uint32_t voices_live;
    uint32_t sound_writes;
    uint32_t frames_rendered;
} OtHalSdlAudioStats;
const OtHalSdlAudioStats *ot_hal_sdl_audio_stats(void);

/* SDL subsystems (SDL_INIT_* flags) the window and input backends need. */
uint32_t ot_hal_sdl_window_subsystems(void);
uint32_t ot_hal_sdl_input_subsystems(void);

/* Hints that must be set before SDL_Init when the window service adopts a
 * window someone else pumps (the retail Game_FrameTick). */
void ot_hal_sdl_window_hints(void);

/* Windows the backend creates from now on get an OpenGL surface (the video
 * service on GL 3.3 draws into them). */
void ot_hal_sdl_window_request_opengl(int enabled);

/* Windows the backend creates from now on get a Vulkan-capable surface (the
 * video service on SDL_GPU claims them with SDL_ClaimWindowForGPUDevice).
 * Mutually exclusive with ot_hal_sdl_window_request_opengl; video_hal.cpp
 * sets exactly one before the window is created or adopted. */
void ot_hal_sdl_window_request_vulkan(int enabled);

/* Processes pending SDL events once: window events are queued for
 * ot_hal_sdl_window.pump, gamepad arrivals and removals are applied. Both the
 * window pump and the input poll call it; it is cheap when idle. */
void ot_hal_sdl_process_events(void);
void ot_hal_sdl_log_poll(uint32_t poll);   /* the poll number the play log (OPENTONY_INPUT_LOG) stamps on its lines */

/* Statistics for the hook DLL's stats line. */
typedef struct OtHalSdlStats {
    uint32_t events;
    uint32_t window_events;
    uint32_t focused_polls;
    uint32_t unfocused_polls;
    uint32_t pads_opened;
    uint32_t helper_messages;
} OtHalSdlStats;
const OtHalSdlStats *ot_hal_sdl_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_SDL_BACKENDS_H */
