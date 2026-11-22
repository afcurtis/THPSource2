#ifndef OPENTONY_HOOKS_HAL_VIDEO_HAL_H
#define OPENTONY_HOOKS_HAL_VIDEO_HAL_H

/* video=sdl (docs/HAL.md, "Video"): the game draws through the HAL video
 * backend instead of Direct3D.
 *
 * DirectDraw stays only as the store of the game's texture and sprite
 * surfaces (normal cooperative level, no display mode, no flipping chain):
 * the retail texture code runs unchanged and the renderer copies the
 * surfaces into HAL textures. The primary/back buffer the rest of the game
 * expects is one offscreen system-memory surface. D3DDevice is a stand-in
 * IDirect3DDevice7 that forwards scene brackets, clears, viewports, state and
 * every draw retail makes on it to the HAL; the ordering tables are drawn by
 * the adapter (video_shadow.cpp) in place of D3D_DrawPolygonList. The frame
 * is presented on the adopted game window through OpenGL 3.3.
 *
 * Test switch: OPENTONY_VIDEO_TRACE=<first>:<count>[:<step>] writes those
 * frames (counted by D3D_PresentFrame; every `step`th) to
 * <log dir>/video/frame_NNNNN.hal.ottx, to compare with retail's
 * frame_NNNNN.ottx (`tony hal video ingame`). */

#include "../../hal/video.h"

/* At HAL startup, SDL video up, before the window service adopts MainWindow:
 * the adopted window must get an OpenGL pixel format. */
void ot_video_hal_prepare(void);
/* SDL is about to stop: the backend lets go of the window. */
void ot_video_hal_stop(void);
int ot_video_hal_running(void);
int ot_video_hal_describe(char *out, int size);
/* The backend video=sdl draws through (0 when it does not run): the movie
 * service presents on it. */
const OtHalVideoBackend *ot_video_hal_backend(void);
/* Writes the backend's last frame to `path` as .ottx (32-bit, top down);
 * 1 on success. */
int ot_video_hal_save_frame(const char *path);

#endif /* OPENTONY_HOOKS_HAL_VIDEO_HAL_H */
