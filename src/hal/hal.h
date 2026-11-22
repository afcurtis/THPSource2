#ifndef OPENTONY_HAL_H
#define OPENTONY_HAL_H

/* OpenTony platform HAL: conventions and service selection shared by every
 * service header (clock.h, window.h, input.h, video.h, audio.h, movie.h,
 * fs.h) and every backend.
 *
 * The HAL headers in src/hal/ are portable C: no SDL, Windows or Direct3D
 * types, fixed-width integers only, so they survive the move to a 64-bit
 * standalone build (docs/HAL.md). Each service is chosen once at startup;
 * device state cannot be swapped in the middle of a frame. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Calling convention of every HAL function pointer and exported HAL entry
 * point: cdecl on 32-bit x86 (the game and the hook DLL agree on it whatever
 * the compiler default), the platform default elsewhere. */
#if defined(_WIN32) && !defined(_WIN64)
#define OT_HAL_CALL __cdecl
#else
#define OT_HAL_CALL
#endif

/* Entry points a hook DLL exports (built with OT_HAL_EXPORTS defined). */
#if defined(_WIN32) && defined(OT_HAL_EXPORTS)
#define OT_HAL_EXPORT __declspec(dllexport)
#else
#define OT_HAL_EXPORT
#endif

/* Opaque object handle (texture, palette, sound, voice, stream, file, ...).
 * 0 is never a valid handle. Handles are 32-bit ids, not pointers, so logs,
 * oracles and recordings stay identical between 32- and 64-bit builds. */
typedef uint32_t OtHalHandle;
#define OT_HAL_INVALID_HANDLE 0u

/* Integer rectangle in pixels; w and h are never negative. */
typedef struct OtHalRect {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} OtHalRect;

enum OtHalService {
    OT_HAL_CLOCK = 0,
    OT_HAL_WINDOW = 1,
    OT_HAL_INPUT = 2,
    OT_HAL_VIDEO = 3,
    OT_HAL_AUDIO = 4,
    OT_HAL_MOVIE = 5,
    OT_HAL_SERVICE_COUNT = 6
    /* fs (fs.h) is not selectable: the hook DLL keeps retail file I/O, which
     * never leaves the game's static C runtime. */
};

enum OtHalBackend {
    OT_HAL_RETAIL = 0, /* leave the retail DirectX/Win32 code in charge */
    OT_HAL_SDL = 1     /* the SDL3 backend under src/hal/sdl */
};

/* video=sdl's own choice of SDL video backend (docs/HAL.md, "Video", 3.4d):
 * `[hal] video_backend` / OPENTONY_VIDEO_BACKEND, independent of whether the
 * video service itself is sdl or retail. */
enum OtHalVideoBackendSetting {
    OT_HAL_VIDEO_BACKEND_AUTO = 0, /* SDL_GPU if a device can be created, else GL 3.3 */
    OT_HAL_VIDEO_BACKEND_GPU = 1,  /* SDL_GPU only; fails loudly if unavailable */
    OT_HAL_VIDEO_BACKEND_GL = 2    /* OpenGL 3.3 only */
};

typedef struct OtHalConfig {
    uint8_t backend[OT_HAL_SERVICE_COUNT]; /* OtHalBackend per OtHalService */
    uint8_t sdl_probe;                     /* start and stop SDL even when every service is retail */
    uint8_t video_backend;                 /* OtHalVideoBackendSetting */
} OtHalConfig;

/* "clock", "window", ... ; 0 for an unknown service. */
const char *ot_hal_service_name(int service);
/* Parses "clock", "window", ...; returns -1 when unknown. */
int ot_hal_service_parse(const char *text, unsigned length);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_H */
