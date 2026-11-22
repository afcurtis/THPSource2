#ifndef OPENTONY_HAL_VIDEO_H
#define OPENTONY_HAL_VIDEO_H

/* Video service (docs/HAL.md, "Video").
 *
 * Retail builds a PlayStation-style ordering table every frame (libgpu
 * POLY_F3/FT4/GT4, LINE_*, TILE, DR_TPAGE, DR_AREA, plus a PC-only
 * pre-transformed polygon, command 0xB0) and D3D_DrawPolygonList walks it,
 * dispatching each packet through D3DPrimitiveDispatchTable to Direct3D 7
 * DrawPrimitive calls on pre-transformed vertices.
 *
 * The HAL does not see the ordering table. The adapter walks it and
 * flattens every packet into draw calls on pre-transformed vertices with an
 * explicit render state: the primitive stream. The stream is what the
 * primitive-stream oracle compares, retail against HAL, call by call.
 * Textures, palettes and sprites are uploaded through the same backend;
 * present ends the frame. */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One pre-transformed vertex: screen-space position, reciprocal W for
 * perspective-correct texturing, colour and fog, texture coordinates.
 * The layout of a Direct3D 7 D3DTLVERTEX, which is what retail submits. */
typedef struct OtHalVertex {
    float x; /* pixels, render-target space */
    float y;
    float z;   /* depth, 0 (near) .. 1 (far) */
    float rhw; /* 1 / w */
    uint32_t color;    /* 0xAARRGGBB diffuse */
    uint32_t specular; /* 0xAARRGGBB; alpha is the fog factor (255 = no fog) */
    float u;
    float v;
} OtHalVertex;

enum OtHalTopology {
    OT_HAL_TRIANGLES = 0,
    OT_HAL_TRIANGLE_STRIP = 1,
    OT_HAL_TRIANGLE_FAN = 2,
    OT_HAL_LINES = 3,
    OT_HAL_LINE_STRIP = 4
};

/* Fixed-function blending: result = op(src_factor * F, dst_factor * B),
 * F = incoming fragment, B = framebuffer. General enough for both the
 * PlayStation modes and what retail Direct3D actually programs for them
 * (which only approximates some: its "subtract" is B * (1 - F)). */
enum OtHalBlendFactor {
    OT_HAL_FACTOR_ZERO = 0,
    OT_HAL_FACTOR_ONE = 1,
    OT_HAL_FACTOR_SRC_COLOR = 2,
    OT_HAL_FACTOR_INV_SRC_COLOR = 3,
    OT_HAL_FACTOR_SRC_ALPHA = 4,
    OT_HAL_FACTOR_INV_SRC_ALPHA = 5,
    OT_HAL_FACTOR_DST_COLOR = 6,
    OT_HAL_FACTOR_INV_DST_COLOR = 7,
    OT_HAL_FACTOR_DST_ALPHA = 8,
    OT_HAL_FACTOR_INV_DST_ALPHA = 9
};

enum OtHalBlendOp {
    OT_HAL_BLEND_OP_ADD = 0,         /* src + dst */
    OT_HAL_BLEND_OP_SUBTRACT = 1,    /* src - dst */
    OT_HAL_BLEND_OP_REV_SUBTRACT = 2 /* dst - src */
};

/* PlayStation semi-transparency modes (DR_TPAGE abr) as presets for
 * ot_hal_blend_preset. B = framebuffer, F = incoming fragment. */
enum OtHalBlend {
    OT_HAL_BLEND_OPAQUE = 0,
    OT_HAL_BLEND_HALF = 1,         /* 0.5 B + 0.5 F */
    OT_HAL_BLEND_ADD = 2,          /* B + F */
    OT_HAL_BLEND_SUBTRACT = 3,     /* B - F */
    OT_HAL_BLEND_ADD_QUARTER = 4,  /* B + 0.25 F */
    OT_HAL_BLEND_ALPHA = 5         /* (1 - a) B + a F */
};

enum OtHalDrawFlags {
    OT_HAL_DRAW_DEPTH_TEST = 1u << 0, /* pass when z <= stored z (>= with DEPTH_GREATER) */
    OT_HAL_DRAW_DEPTH_WRITE = 1u << 1,
    OT_HAL_DRAW_ALPHA_TEST = 1u << 2, /* discard alpha <= alpha_ref (PSX colour key: ref 0) */
    OT_HAL_DRAW_FILTER = 1u << 3,     /* bilinear; nearest otherwise */
    OT_HAL_DRAW_WRAP = 1u << 4,       /* repeat; clamp otherwise */
    OT_HAL_DRAW_FOG = 1u << 5,
    OT_HAL_DRAW_MODULATE = 1u << 6,   /* texture x colour; texture only otherwise */
    OT_HAL_DRAW_BLEND = 1u << 7,      /* use the blend fields; opaque otherwise */
    OT_HAL_DRAW_FLAT = 1u << 8,       /* one colour per triangle (its first vertex) */
    OT_HAL_DRAW_PERSPECTIVE = 1u << 9, /* perspective-correct texturing from rhw; affine otherwise */
    OT_HAL_DRAW_DEPTH_GREATER = 1u << 10, /* reversed depth: pass when z >= stored z (retail clears to 0) */
    OT_HAL_DRAW_MODULATE_2X = 1u << 11    /* with MODULATE: texture x colour x 2 (the PlayStation's blend: 0x80 is the texture as it is) */
};

typedef struct OtHalDrawState {
    OtHalHandle texture;  /* OT_HAL_INVALID_HANDLE = untextured */
    uint32_t flags;       /* OtHalDrawFlags */
    uint8_t src_factor;   /* OtHalBlendFactor, with OT_HAL_DRAW_BLEND */
    uint8_t dst_factor;
    uint8_t blend_op;     /* OtHalBlendOp */
    uint8_t alpha_ref;    /* with OT_HAL_DRAW_ALPHA_TEST */
    uint32_t fog_color;   /* 0x00RRGGBB, with OT_HAL_DRAW_FOG */
} OtHalDrawState;

/* Fills the blend fields and OT_HAL_DRAW_BLEND of `state` for a PlayStation
 * mode. HALF and ADD_QUARTER weight F by the source alpha the caller puts in
 * the vertex colours (0x80 / 0x40), as the PSX GPU weights by 0.5 / 0.25. */
void OT_HAL_CALL ot_hal_blend_preset(OtHalDrawState *state, uint32_t blend);

enum OtHalPixelFormat {
    OT_HAL_PIXEL_ARGB8888 = 1, /* uint32 0xAARRGGBB */
    OT_HAL_PIXEL_ARGB1555 = 2, /* uint16; retail texture surfaces */
    OT_HAL_PIXEL_ARGB4444 = 3,
    OT_HAL_PIXEL_RGB565 = 4,
    OT_HAL_PIXEL_INDEX8 = 5,   /* uint8 into a palette */
    OT_HAL_PIXEL_INDEX4 = 6    /* two texels per byte, low nibble first */
};

typedef struct OtHalTextureDesc {
    int32_t width;
    int32_t height;
    uint32_t format;     /* OtHalPixelFormat */
    OtHalHandle palette; /* indexed formats only */
} OtHalTextureDesc;

/* Mode of the render target / output. */
typedef struct OtHalVideoMode {
    int32_t width;
    int32_t height;
    uint32_t bits_per_pixel; /* 16 or 32; the presentation format */
    uint32_t fullscreen;
    uint32_t vsync;
} OtHalVideoMode;

/* A CPU image, for screenshots and the image oracle. */
typedef struct OtHalImage {
    int32_t width;
    int32_t height;
    int32_t pitch;   /* bytes per row */
    uint32_t format; /* OtHalPixelFormat */
    void *pixels;
} OtHalImage;

enum OtHalClearFlags {
    OT_HAL_CLEAR_COLOR = 1u << 0,
    OT_HAL_CLEAR_DEPTH = 1u << 1
};

typedef struct OtHalVideoBackend {
    const char *name;
    /* Uses the window service's window. 1 on success. */
    int(OT_HAL_CALL *init)(const OtHalVideoMode *mode);
    void(OT_HAL_CALL *shutdown)(void);
    int(OT_HAL_CALL *set_mode)(const OtHalVideoMode *mode);
    /* Modes the output supports; returns the total count, writes at most `capacity`. */
    uint32_t(OT_HAL_CALL *enumerate_modes)(OtHalVideoMode *modes, uint32_t capacity);

    /* Textures and palettes. `pixels` rows are `pitch` bytes apart. */
    OtHalHandle(OT_HAL_CALL *texture_create)(const OtHalTextureDesc *desc);
    void(OT_HAL_CALL *texture_update)(OtHalHandle texture, const OtHalRect *area, const void *pixels, int32_t pitch);
    void(OT_HAL_CALL *texture_destroy)(OtHalHandle texture);
    /* `colors` are 0xAARRGGBB; count is 16 or 256. Textures that use the
     * palette see the change from the next draw on. */
    OtHalHandle(OT_HAL_CALL *palette_create)(uint32_t count);
    void(OT_HAL_CALL *palette_update)(OtHalHandle palette, uint32_t first, uint32_t count, const uint32_t *colors);
    void(OT_HAL_CALL *palette_destroy)(OtHalHandle palette);

    /* Frame. */
    void(OT_HAL_CALL *begin_frame)(void);
    void(OT_HAL_CALL *clear)(uint32_t flags, uint32_t color, float depth, const OtHalRect *area /* 0 = all */);
    void(OT_HAL_CALL *set_viewport)(const OtHalRect *area);
    void(OT_HAL_CALL *set_scissor)(const OtHalRect *area /* 0 = none */);
    void(OT_HAL_CALL *draw)(const OtHalDrawState *state, uint32_t topology, const OtHalVertex *vertices, uint32_t count);
    /* Sprite: a screen-aligned textured rectangle (`source` in texels),
     * modulated by `color`. Equivalent to a two-triangle draw; kept separate
     * because retail sprites (D3DSprite) are tiled images, not packets. */
    void(OT_HAL_CALL *draw_sprite)(const OtHalDrawState *state, const OtHalRect *source, const OtHalRect *target, uint32_t color);
    void(OT_HAL_CALL *present)(void);

    /* Copies the last presented frame (or the current target if none). The
     * backend owns `image->pixels` until the next call. 1 on success. */
    int(OT_HAL_CALL *read_back)(OtHalImage *image);
} OtHalVideoBackend;

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_VIDEO_H */
