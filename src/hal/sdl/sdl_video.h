#ifndef OPENTONY_HAL_SDL_VIDEO_H
#define OPENTONY_HAL_SDL_VIDEO_H

/* Video service on SDL3 (sdl_video_gl.cpp): OpenGL 3.3 core through SDL's
 * GL context. SDL_GPU is the preferred long-term backend; it needs offline
 * shader compilation (SPIR-V/DXIL), so GL 3.3, the stated fallback, came
 * first. Uses the window service's SDL window. */

#include "../video.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const OtHalVideoBackend ot_hal_sdl_video_gl;

/* SDL_GL_* attributes for a 3.3 core context: call before creating the
 * window the backend will draw into. */
void ot_hal_sdl_video_gl_hints(void);
/* Why the last init/compile failed. */
const char *ot_hal_sdl_video_error(void);

/* How the GL backend renders and presents, apart from the mode (which stays
 * the size the game draws in: its vertices and rectangles are in mode
 * pixels). All zero is the default: a target of the mode's size, the draws'
 * own filter, a bilinear blit to the window with the mode's aspect kept.
 * Set it before init, or later: a new target takes effect at the next frame. */
enum OtHalSdlTextureFilter {
    OT_HAL_SDL_FILTER_AS_DRAWN = 0,
    OT_HAL_SDL_FILTER_NEAREST = 1,
    OT_HAL_SDL_FILTER_LINEAR = 2
};
enum OtHalSdlPresentFilter {
    OT_HAL_SDL_PRESENT_LINEAR = 0,
    OT_HAL_SDL_PRESENT_NEAREST = 1,
    OT_HAL_SDL_PRESENT_SHARP = 2, /* whole texels with antialiased edges ("sharp bilinear") */
    OT_HAL_SDL_PRESENT_CRT = 3    /* scanlines that follow the source's lines, a soft blur across them, a faint aperture mask at four output pixels a texel or more */
};
typedef struct OtHalSdlVideoOptions {
    int32_t target_width;   /* the render target in pixels; 0 = the mode's size */
    int32_t target_height;
    int32_t follow_window;  /* 1: the target is the part of the window the picture covers, remade when the window changes */
    int32_t samples;        /* multisampling: 0 or 1 none, else 2, 4, 8 (capped at what the driver has) */
    int32_t texture_filter; /* OtHalSdlTextureFilter */
    int32_t present_filter; /* OtHalSdlPresentFilter */
    int32_t stretch;        /* 1: the picture fills the window whatever its shape */
    int32_t anisotropy;     /* texture detail: 0 off (the draws' own filter, no mips); 1 mips with trilinear filtering; 2, 4, 8, 16 also anisotropic
                               (capped at the driver's limit). Only world draws that filter use mips: the 2D layer, nearest draws and
                               OT_HAL_SDL_FILTER_NEAREST are unchanged */
    float lod_bias;         /* added to the mip level of those draws (0 = none; negative sharpens) */
    int32_t supersample;    /* x100: 0 or 100 off; 150, 200, 300, 400 multiply the size of a target that follows the window or has a size set (never the mode's own).
                               Lowered until the target fits GL_MAX_RENDERBUFFER_SIZE and about 1 GB; world draws then get a LOD bias of -log2(factor) (at most -2) */
    int32_t alpha_to_coverage; /* 1: with multisampling, a cut-out texture (alpha only 0 or 255) drawn as a plain blended world draw is drawn without blending, its
                               alpha sharpened around one half and turned into coverage, so fences and leaves get antialiased edges */
    int32_t sharp_2d;          /* 1: a 2D draw (every reciprocal depth exactly 1: HUD, fonts, menus) that filters linearly keeps whole texels and blends only the last output
                               pixel at each texel edge, so magnified 2D art stays crisp at high resolution. World draws are unchanged */
    const char *texture_pack;  /* (up to four folders joined by '|': the first that holds a key supplies its picture) a folder of <key>.png files (texture_key_hash.h): a texture the game uploads whole whose key is there is drawn from that picture, larger than
                               the game's own, which it keeps believing in (its size, a sprite's source rectangle). A miss, an unreadable file or a size that does not fit
                               (not the same shape, smaller, over 1024 on a side) draws the original. 0 or "" = off: nothing is hashed or read. Copied */
    int32_t texture_pack_log;  /* 1, with a pack: one stdout line `video: pack use: <key> <w>x<h> replaced|missed 2d|world` the first time each hashed texture is drawn in 2D and in the world */
    int32_t time_draws;        /* 1: the stats' draw_us and swap_us count the time spent in video_draw and in the swap (a measurement; the default makes no timer call) */
    int32_t ambient_occlusion; /* an experiment: 0 off, 1 low (8 taps), 2 high (12 taps). The depth buffer becomes a texture; once per viewport, before the first 2D draw that follows a world
                               draw (and at a depth clear and at the present), a half-resolution occlusion pass over a copy of the depth darkens the colour where surfaces meet.
                               The 2D layer is drawn after it and is not touched. Off: no GL call differs */
    int32_t psx_dither;        /* 1: a pass over the finished picture before the present adds the PlayStation's 4 x 4 dither matrix and cuts each channel to 5 bits. Meant for a
                               240-line target. Off: no GL call differs */
    int32_t crt_curve;         /* 1: the present bends the picture as on a curved tube (a barrel warp, the screen's edge a rounded rectangle, black outside it), whatever the
                               present filter and the target's size. Off: the picture is as it was before the option */
} OtHalSdlVideoOptions;
void ot_hal_sdl_video_gl_options(const OtHalSdlVideoOptions *options);
/* The render target's size right now (the mode's before init). */
void ot_hal_sdl_video_gl_target(int32_t *width, int32_t *height, int32_t *samples);
/* The supersampling factor (x100) the target actually has: 100 when off or when it could not be kept. */
int32_t ot_hal_sdl_video_gl_supersample(void);
/* The frame as the window shows it, for a screenshot: with the CRT scaling, the curved screen or the dither on, the present's own pass run over the frame
 * in the render target (no swap) and the picture's rectangle read back at the window's resolution, 32-bit X8R8G8B8, top row first; the backend owns the
 * pixels until the next call. 0 (use read_back) when none of those is on or there is no frame. */
int ot_hal_sdl_video_gl_capture_shown(OtHalImage *image);

typedef struct OtHalVideoStats {
    uint32_t frames;
    uint32_t draws;
    uint32_t vertices;
    uint32_t textures;
    uint32_t mip_builds;   /* mip chains built (texture detail on); a texture is rebuilt when drawn after its texels or palette changed */
    uint32_t mip_cutouts;  /* of those, chains of cut-out textures (made on the CPU to keep their coverage) */
    uint32_t pack_files;   /* texture pack: keys found in the folder (read at the first upload) */
    uint32_t pack_hashes;  /* whole-texture uploads hashed */
    uint32_t pack_hits;    /* ... that had a picture in the pack and were replaced */
    uint32_t pack_rejected;/* ... that had a file the backend did not use (unreadable, too small, too big, another shape) */
    uint32_t pack_live;    /* textures drawn from the pack now */
    uint32_t pack_flat_draws; /* draws of a picture from the pack in 2D (every reciprocal depth 1, a sprite included): the HUD and menus should have none */
    uint64_t pack_bytes;   /* their size on the GPU: RGBA level 0, a third more with mip maps */
    uint64_t pack_us;      /* microseconds spent in the pack's hashing, decoding and uploading, in all */
    uint64_t mip_us;       /* microseconds spent building mip chains, in all */
    uint64_t draw_us;      /* time_draws: microseconds spent inside video_draw (the CPU side of the GL calls, driver stalls included), in all */
    uint64_t swap_us;      /* time_draws: microseconds spent in SDL_GL_SwapWindow (the wait for the display or for a GPU that is behind), in all */
    uint32_t ao_passes;    /* ambient occlusion: passes run, in all */
    uint32_t ao_late;      /* ... of those, passes after which another world draw followed in the same frame (a second pass darkens the same pixels again) */
} OtHalVideoStats;
const OtHalVideoStats *ot_hal_sdl_video_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_SDL_VIDEO_H */
