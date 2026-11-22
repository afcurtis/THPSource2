#ifndef OPENTONY_HAL_ORACLE_D3D7_STREAM_H
#define OPENTONY_HAL_ORACLE_D3D7_STREAM_H

/* Replays a recorded retail Direct3D 7 frame (OPENTONY_VIDEO_TRACE, see
 * src/hooks/hal/video_trace.cpp and tony/hal/video.py) onto a HAL video
 * backend: the image oracle renders retail's own primitive stream through
 * the HAL and compares the result with retail's frame.
 *
 * This file knows Direct3D 7 numbering (render states, blend factors,
 * primitive types) but includes no DirectX header; it is portable C++ with
 * no standard library, so it builds for the host, the hook DLL and tools. */

#include "../video.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OtD3d7ReplayStats {
    uint32_t records;
    uint32_t draws;
    uint32_t vertices;
    uint32_t textures_loaded;
    uint32_t textures_missing;
    uint32_t unsupported; /* draws or records the replay could not express */
} OtD3d7ReplayStats;

/* Finds the texture a trace recorded by content hash (`surface` is the
 * game's IDirectDrawSurface7 pointer, 0 in older traces). On success fills
 * `desc`, `pixels` and `pitch` (valid until the next call) and returns 1. */
typedef int (*OtD3d7TextureLoader)(void *user, uint64_t hash, uint32_t surface, OtHalTextureDesc *desc,
    const void **pixels, int32_t *pitch);

/* Parses an .ottx image (retail texture or render target). Returns 0 for a
 * malformed file or a pixel layout the HAL has no format for. */
int ot_ottx_parse(const uint8_t *data, uint32_t size, OtHalTextureDesc *desc, const uint8_t **pixels, int32_t *pitch);

/* The HAL draw state for the current Direct3D state: `render` holds render
 * states by D3DRENDERSTATETYPE (256 entries), `stage` texture-stage-0
 * states by D3DTEXTURESTAGESTATETYPE (32 entries). */
void ot_d3d7_draw_state(const uint32_t *render, const uint32_t *stage, OtHalHandle texture, OtHalDrawState *state);

/* Building blocks shared with the live Direct3D 7 stand-in (video=sdl):
 * D3DPRIMITIVETYPE as an OtHalTopology (0xffffffff for points, which the HAL
 * does not draw); one pre-transformed vertex of flexible vertex format `fvf`
 * (0 when the format is not D3DFVF_XYZRHW); the bytes per vertex of `fvf`. */
uint32_t ot_d3d7_topology(uint32_t primitive);
int ot_d3d7_convert_vertex(const uint8_t *at, uint32_t fvf, OtHalVertex *out);
uint32_t ot_d3d7_fvf_stride(uint32_t fvf);

/* Replays one .otvs frame. Returns 0 when the stream is malformed. */
int ot_d3d7_replay(const uint8_t *stream, uint32_t size, const OtHalVideoBackend *video, OtD3d7TextureLoader loader,
    void *user, OtD3d7ReplayStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_ORACLE_D3D7_STREAM_H */
