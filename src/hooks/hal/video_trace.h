#ifndef OPENTONY_HOOKS_HAL_VIDEO_TRACE_H
#define OPENTONY_HOOKS_HAL_VIDEO_TRACE_H

/* Primitive-stream oracle, retail half (docs/HAL.md, "Video").
 *
 * OPENTONY_VIDEO_TRACE=<first frame>:<frame count> records every call the
 * game makes on its Direct3D 7 device for those frames (counted by EndScene):
 * scene brackets, clears, viewports, render and texture-stage states, texture
 * binds and every draw with its vertices and indices. Each frame goes to
 * <log dir>/video/frame_NNNNN.otvs, the render target after EndScene to
 * frame_NNNNN.ottx, and every distinct texture once to textures/<hash>.ottx.
 * `tony hal video` decodes them (tony/hal/video.py describes the format).
 *
 * The device vtable is patched when the `video` HAL entry site first sees a
 * device; nothing is patched unless the switch is set. Recording only reads:
 * every call goes on to the retail device unchanged. */

void ot_video_trace_configure(void);
int ot_video_trace_enabled(void);
/* `device` is the game's IDirect3DDevice7 (D3DDevice). */
void ot_video_trace_attach(void *device);
int ot_video_trace_describe(char *out, int size);

/* D3D_DrawPolygonList is about to draw `list` (an ordering table). */
void ot_video_trace_draw_list(const void *list);

#endif /* OPENTONY_HOOKS_HAL_VIDEO_TRACE_H */
