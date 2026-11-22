#ifndef OPENTONY_HAL_ORACLE_VIDEO_GPU_H
#define OPENTONY_HAL_ORACLE_VIDEO_GPU_H

/* Pure helpers for the SDL_GPU video backend (docs/HAL.md, "Video", 3.4d;
 * src/hal/sdl/sdl_video_gpu.cpp). Host-tested, no SDL or Vulkan types, same
 * split as the DirectDraw pixel rules (ddraw_surface.h): the backend file
 * only calls SDL_GPU and never does arithmetic the oracle cannot also run. */

#include "../video.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Unorm channel expansion, Direct3D/GL's own rule for widening a narrow
 * channel to 8 bits (not a naive shift, which leaves the top value short of
 * 255): 5-bit v -> (v*255+15)/31, 6-bit -> (v*255+31)/63, 4-bit -> v*17,
 * 1-bit -> v?255:0. */
uint8_t OT_HAL_CALL ot_gpu_expand5(uint32_t value);
uint8_t OT_HAL_CALL ot_gpu_expand6(uint32_t value);
uint8_t OT_HAL_CALL ot_gpu_expand4(uint32_t value);
uint8_t OT_HAL_CALL ot_gpu_expand1(uint32_t value);

/* Converts one row of `width` texels from `format` (any OtHalPixelFormat) to
 * 0xAABBGGRR bytes (R first in memory, matching SDL_GPU_TEXTUREFORMAT_
 * R8G8B8A8_UNORM) into `out` (width * 4 bytes). Indexed formats read through
 * `palette` (256 entries, 0xAARRGGBB, indexed by the source pixel); unused
 * for direct-color formats (`palette` may be 0 then). Returns 0 for an
 * unrecognised format (out left untouched), 1 otherwise. */
int OT_HAL_CALL ot_gpu_convert_row(
    uint32_t format, const void *row, int32_t width, const uint32_t *palette, uint32_t *out);

/* Triangle-fan -> triangle-list index order for one fan of `count` vertices
 * (count - 2 triangles): SDL_GPU's Vulkan backend provokes the LAST vertex of
 * a triangle, while the retail/GL convention needs flat shading from the
 * fan's shared (first) vertex, so each triangle is reordered
 * (i+1, i+2, 0) instead of (0, i+1, i+2). Writes 3 * (count - 2) indices into
 * `out` (caller-sized for count < 3: writes nothing, returns 0 triangles).
 * Returns the triangle count written. */
uint32_t OT_HAL_CALL ot_gpu_fan_to_list_indices(uint32_t count, uint32_t *out);

/* The clear quad's four corners (triangle fan order), covering the render
 * target from -0.5 to size-0.5 in each axis so pixel-centred rasterization
 * covers every pixel exactly once, matching the fixed-function `clear`'s
 * scissor-limited full-target quad. z/rhw carry the requested clear depth. */
void OT_HAL_CALL ot_gpu_clear_quad(int32_t width, int32_t height, float depth, uint32_t color, OtHalVertex quad[4]);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_ORACLE_VIDEO_GPU_H */
