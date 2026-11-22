#ifndef OPENTONY_HOOKS_HAL_VIDEO_SURFACE_H
#define OPENTONY_HOOKS_HAL_VIDEO_SURFACE_H

/* Stand-in IDirectDraw7 (30 slots) and IDirectDrawSurface7 (49 slots), in
 * place of ddraw.dll (docs/HAL.md, "Video", 3.4e;
 * re/evidence/functions/hal-adapters.md, "DirectDraw surfaces the retail code
 * still uses"). The retail texture code (D3DTex_*, D3DSprite_*, Bink's
 * CreateBinkSurface/NextBinkFrame/CloseBinkMovie) calls these vtables exactly
 * as it would call real DirectDraw's, unaware the surfaces are HAL textures.
 * Pixel rules (pitch, fill, copy, stretch, format mapping) are portable
 * (src/hal/oracle/ddraw_surface.h), host-tested.
 *
 * Not the same object as real DirectDraw, which the live mirror
 * (video_mirror.cpp, needs window=retail) still uses: video_renderer.cpp
 * tells the two apart with ot_ddraw_is_surface before deciding which texture
 * path a surface pointer takes. */

#include "../../hal/video.h"

/* Runs instead of the usual Flip no-op when the primary surface (marked
 * below) is flipped: DrawFullScreenMovie's presentation of the startup-movie
 * quad, under D3DTextureMode 0. */
typedef void (*OtDdrawPresentCallback)(void);

/* The one DirectDraw object, one reference; `on_primary_flip` is `present_frame`. */
void *ot_ddraw_create(OtDdrawPresentCallback on_primary_flip);

/* Marks `surface` (a stand-in surface) as the primary. Locking it read-only
 * refreshes its pixel memory from the active video backend's read_back()
 * first, converted to the surface's own pixel format (src/hal/oracle/
 * ddraw_surface.h) -- otherwise it stays whatever it was last explicitly
 * written to (blank, unless a movie decoded into it), which is what
 * D3D_SaveScreenshot read before this existed. */
void ot_ddraw_surface_set_primary(void *surface);

/* Turns the refresh above off for the duration a caller needs the primary's
 * raw pixel memory instead of the current frame: the movie adapter's own
 * read of D3DPrimarySurface right after Bink decoded into it must see Bink's
 * write, not the video backend's frame. Not reentrant; nests only as 0/1. */
void ot_ddraw_primary_suppress_refresh(int suppress);

/* True when `object`'s vtable is the stand-in's, never a live-mirror surface
 * on real DirectDraw. */
int ot_ddraw_is_surface(const void *object);

/* The video backend surfaces upload into and the thread their Unlock/Blt/
 * BltFast/Release run on (the render thread); a release off that thread
 * queues its HAL texture for destruction at the next ot_ddraw_frame. */
void ot_ddraw_start(const OtHalVideoBackend *video);
void ot_ddraw_stop(void);
/* A frame begins: flushes textures queued for destruction by an off-thread
 * release. */
void ot_ddraw_frame(void);

/* The surface's HAL texture, created on first use and re-uploaded only when
 * the surface was written (Unlock/Blt/BltFast) since the last call;
 * OT_HAL_INVALID_HANDLE when `object` isn't a stand-in surface or its pixel
 * format has no HAL mapping. */
OtHalHandle ot_ddraw_surface_texture(void *object);

int ot_ddraw_describe(char *out, int size);

#endif /* OPENTONY_HOOKS_HAL_VIDEO_SURFACE_H */
