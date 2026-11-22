#ifndef OPENTONY_HOOKS_HAL_VIDEO_MIRROR_H
#define OPENTONY_HOOKS_HAL_VIDEO_MIRROR_H

/* Live HAL mirror (test switch OPENTONY_VIDEO_MIRROR=<N>; docs/HAL.md,
 * "Video"). Retail keeps rendering through DirectDraw/Direct3D; alongside,
 * the ordering-table adapter (video_shadow.cpp) draws every frame through the
 * SDL video backend (video_renderer.h) into a second window. Scene brackets, clears and
 * viewports come from the Direct3D device hooks, textures are mirrored from
 * the game's surfaces. Every Nth frame (N > 0) the mirror frame is compared
 * with retail's render target and the result logged: a live image oracle.
 *
 * Needs window=retail (the mirror window is the window service's window). */

#include "../../hal/video.h"

void ot_video_mirror_configure(void);
int ot_video_mirror_enabled(void);
/* At HAL startup, SDL loaded and initialized: window + backend. */
int ot_video_mirror_start(void);
void ot_video_mirror_stop(void);

void ot_video_mirror_begin(void);
void ot_video_mirror_clear(uint32_t flags, uint32_t color, float depth, const OtHalRect *area);
void ot_video_mirror_viewport(const OtHalRect *area);
/* After retail's EndScene; `device` is the IDirect3DDevice7 (for the oracle). */
void ot_video_mirror_present(void *device);
int ot_video_mirror_describe(char *out, int size);

#endif /* OPENTONY_HOOKS_HAL_VIDEO_MIRROR_H */
