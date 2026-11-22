#ifndef OPENTONY_HAL_ORACLE_DDRAW_SURFACE_H
#define OPENTONY_HAL_ORACLE_DDRAW_SURFACE_H

/* Pixel rules for the stand-in DirectDraw surfaces (docs/HAL.md, "Video",
 * 3.4e; re/evidence/functions/hal-adapters.md, "DirectDraw surfaces the
 * retail code still uses"). Pure and host-tested: no Windows, SDL or
 * DirectDraw types, matching Wine's CPU blitter, which is the parity
 * reference for every other video=sdl check. */

#include "../video.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CreateSurface's row pitch: width * bytes-per-pixel, rounded up to 8 bytes. */
int32_t OT_HAL_CALL ot_ddraw_pitch(int32_t width, int32_t bytes_per_pixel);

/* A DDPIXELFORMAT's RGB/alpha masks -> OtHalPixelFormat (0 = a combination
 * none of the game's or Bink's surfaces use). */
uint32_t OT_HAL_CALL ot_ddraw_pixel_format(uint32_t bits, uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha);

/* DDBLT_COLORFILL: DirectDraw takes dwFillColor as a value already in the
 * destination's native pixel format (only its low bytes matter for a
 * sub-32-bit surface, which is why retail's single constant 0xFF000000 fills
 * both a 16-bit surface with black and a 32-bit one with opaque black);
 * replicates it across width x height. */
void OT_HAL_CALL ot_ddraw_fill(
    void *dest, int32_t pitch, int32_t width, int32_t height, int32_t bytes_per_pixel, uint32_t fill_value);

/* Same-size Blt/BltFast: a row copy. */
void OT_HAL_CALL ot_ddraw_copy(
    void *dest, int32_t dest_pitch, const void *src, int32_t src_pitch, int32_t width, int32_t height,
    int32_t bytes_per_pixel);

/* Different-size Blt: nearest-pixel stretch, Wine's CPU blitter rule (16.16
 * fixed-point step starting at 0: source = dest * src_size / dest_size). */
void OT_HAL_CALL ot_ddraw_stretch(
    void *dest, int32_t dest_pitch, int32_t dest_width, int32_t dest_height, const void *src, int32_t src_pitch,
    int32_t src_width, int32_t src_height, int32_t bytes_per_pixel);

/* Same-size bit-depth conversion: `src` is always 0xAARRGGBB (an
 * OtHalVideoBackend::read_back() image), converted into `dest`'s own pixel
 * format as CreateSurface gave it (`dest_bits` 16 or 32, and the DDPIXELFORMAT
 * masks) for D3D_SaveScreenshot's Lock of the primary surface, which nothing
 * under video=sdl otherwise ever writes into. Each channel is taken from its
 * mask's bit position and width, so any mask layout CreateSurface accepts
 * (5-6-5 by default, but a 32-bit surface too) works without a case per
 * format. */
void OT_HAL_CALL ot_ddraw_convert_from_argb8888(
    void *dest, int32_t dest_pitch, int32_t dest_bits, uint32_t red_mask, uint32_t green_mask, uint32_t blue_mask,
    uint32_t alpha_mask, const void *src, int32_t src_pitch, int32_t width, int32_t height);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_ORACLE_DDRAW_SURFACE_H */
