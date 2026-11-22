#ifndef OPENTONY_HAL_SDL_VIDEO_GPU_H
#define OPENTONY_HAL_SDL_VIDEO_GPU_H

/* Video service on SDL3 + SDL_GPU (sdl_video_gpu.cpp): the preferred backend
 * (docs/HAL.md, "Video", 3.4d), Vulkan on this project's tested platforms
 * (Wine + Mesa lavapipe headless, Windows 11 + NVIDIA). GL 3.3
 * (sdl_video_gl.cpp) stays the fallback when a GPU device cannot be created.
 * Uses the window service's SDL window, claimed for the GPU device. */

#include "../video.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const OtHalVideoBackend ot_hal_sdl_video_gpu;

/* Tries to create an SDL_GPU device requesting SPIR-V shaders (the only
 * format `tony hal shaders` compiles). Must be called after SDL_Init, before
 * the window exists (video_hal.cpp's ot_video_hal_prepare): the result picks
 * whether the window is created/adopted as an OpenGL or a Vulkan surface
 * (SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN). 1 on success; the device is kept
 * for video_init to claim the window with. Calling it twice without an
 * intervening ot_hal_sdl_video_gpu_release is a no-op returning the same
 * result. */
int ot_hal_sdl_video_gpu_probe(void);
/* Destroys the device created by ot_hal_sdl_video_gpu_probe if video_init
 * never claimed it (the GL fallback path, or an auto probe that failed). */
void ot_hal_sdl_video_gpu_release(void);
/* The Vulkan driver name SDL picked ("vulkan"; "" before a successful
 * probe). */
const char *ot_hal_sdl_video_gpu_driver(void);
/* Why the last probe/init/compile failed. */
const char *ot_hal_sdl_video_gpu_error(void);

typedef struct OtHalVideoGpuStats {
    uint32_t frames;
    uint32_t draws;
    uint32_t vertices;
    uint32_t textures;
    uint32_t pipelines;
    uint32_t batches; /* command buffers submitted (flush() calls) */
} OtHalVideoGpuStats;
const OtHalVideoGpuStats *ot_hal_sdl_video_gpu_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_SDL_VIDEO_GPU_H */
