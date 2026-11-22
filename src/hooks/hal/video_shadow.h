#ifndef OPENTONY_HOOKS_HAL_VIDEO_SHADOW_H
#define OPENTONY_HOOKS_HAL_VIDEO_SHADOW_H

/* Ordering-table adapter in shadow mode (docs/HAL.md, "Video").
 *
 * While OPENTONY_VIDEO_TRACE records a frame, every ordering table
 * D3D_DrawPolygonList is about to draw is first walked here and flattened
 * into HAL draws (OtHalDrawState + OtHalVertex), exactly as the retail
 * D3D_Draw* functions would submit them; retail then draws as usual. The
 * result goes to frame_NNNNN.otvh beside the retail .otvs, and
 * opentony_video_shadow_compare checks the two draw by draw.
 *
 * .otvh: "OTVH", u32 version 1, u32 frame, u32 record count, then records
 * u16 type, u16 0, u32 size, payload:
 *   1 draw        OtHalDrawState (texture = the game's surface pointer),
 *                 u32 topology, u32 count, count x OtHalVertex
 *   2 viewport    i32 x, y, w, h
 *   3 unsupported u32 command byte, u32 reason */

enum OtVideoShadowLive {
    OT_VIDEO_SHADOW_RECORD_ONLY = 0,
    /* Also draw through the HAL renderer (video_renderer.h); retail draws the
     * list afterwards and does the rest (the live mirror). */
    OT_VIDEO_SHADOW_MIRROR = 1,
    /* In place of retail D3D_DrawPolygonList (video=sdl): also apply the
     * viewports and leave the retail state the walk leaves behind. */
    OT_VIDEO_SHADOW_SOLE = 2
};

/* Flattens `list`: into the .otvh buffer when `record`, into the HAL
 * renderer as `live` (OtVideoShadowLive) says. */
void ot_video_shadow_list(const void *list, int record, int live);
/* Writes what was flattened since the last flush to `path`; 0 when empty. */
int ot_video_shadow_flush(const char *path, unsigned frame);
int ot_video_shadow_describe(char *out, int size);

#endif /* OPENTONY_HOOKS_HAL_VIDEO_SHADOW_H */
