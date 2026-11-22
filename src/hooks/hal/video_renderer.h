#ifndef OPENTONY_HOOKS_HAL_VIDEO_RENDERER_H
#define OPENTONY_HOOKS_HAL_VIDEO_RENDERER_H

/* Draws the game's primitive stream into a HAL video backend (docs/HAL.md,
 * "Video"). Shared by the live mirror (video_mirror.h) and video=sdl
 * (video_hal.cpp).
 *
 * Draw states name the game's IDirectDrawSurface7 texture surfaces; the
 * renderer copies each surface into a HAL texture on first use and again
 * after the game writes into it or releases it. Writes and releases are seen
 * where every one of them passes: the surface vtable's Unlock, Blt, BltFast
 * and last Release (patched while the renderer runs), whoever calls them
 * (D3DTex_*, D3DSprite_*, NextBinkFrame for the level-select clips); the
 * `texture` entry site reports D3DTex_UploadTexture/DestroyRecord too. All
 * copies are also dropped every kTextureLifetime frames, as a safety net. */

#include "../../hal/video.h"

void ot_video_renderer_start(const OtHalVideoBackend *video);
void ot_video_renderer_stop(void);
/* The backend, 0 unless started. */
const OtHalVideoBackend *ot_video_renderer_backend(void);

/* A frame begins (ages the texture copies). */
void ot_video_renderer_frame(void);
/* `state.texture` is the game's surface pointer (0 = untextured). A draw
 * whose surface cannot be copied is dropped rather than drawn untextured. */
void ot_video_renderer_draw(const OtHalDrawState *state, uint32_t topology, const OtHalVertex *vertices, uint32_t count);
/* The game is about to refill or release this surface: copy it again next time. */
void ot_video_renderer_forget(uint32_t surface);

int ot_video_renderer_describe(char *out, int size);

#endif /* OPENTONY_HOOKS_HAL_VIDEO_RENDERER_H */
