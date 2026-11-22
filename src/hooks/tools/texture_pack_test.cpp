// opentony_texture_pack_test: the GL backend's texture pack (OT_TEXTURE_PACK, sdl_video_gl.cpp) against a real GL context.
//
//   opentony_texture_pack_test <pack dir\> <SDL3 dir\>
//
// tests/test_texture_pack_backend.py makes the pack folder (PNG files named by the keys of the textures below, computed in Python from the documented FNV-1a) and runs this
// under Wine and Xvfb. Each line printed is `ok <what>` or `FAIL <what>`; the exit code is the number of failures.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../hal/sdl/sdl_api.h"
#include "../../hal/sdl/sdl_backends.h"
#include "../../hal/sdl/sdl_video.h"

namespace {

int g_failures = 0;
const OtHalVideoBackend &video = ot_hal_sdl_video_gl;

void check(bool condition, const char *what) {
    printf("%s %s\n", condition ? "ok" : "FAIL", what);
    g_failures += condition ? 0 : 1;
}

const int32_t kSize = 64;

// The texture drawn over the whole 64 x 64 target (a sprite, or a two-triangle world draw with a reciprocal depth other than 1); the pixel in the middle, 0xAARRGGBB
uint32_t centre(OtHalHandle texture, bool world) {
    video.begin_frame();
    video.clear(OT_HAL_CLEAR_COLOR | OT_HAL_CLEAR_DEPTH, 0xff000000u, 1.0f, 0);
    OtHalDrawState state = {};
    state.texture = texture;
    state.flags = OT_HAL_DRAW_FILTER;
    if (world) {
        const float w = static_cast<float>(kSize);
        const OtHalVertex quad[6] = {
            {0, 0, 0.5f, 0.5f, 0xffffffffu, 0xff000000u, 0, 0}, {w, 0, 0.5f, 0.5f, 0xffffffffu, 0xff000000u, 1, 0}, {w, w, 0.5f, 0.5f, 0xffffffffu, 0xff000000u, 1, 1},
            {0, 0, 0.5f, 0.5f, 0xffffffffu, 0xff000000u, 0, 0}, {w, w, 0.5f, 0.5f, 0xffffffffu, 0xff000000u, 1, 1}, {0, w, 0.5f, 0.5f, 0xffffffffu, 0xff000000u, 0, 1}};
        video.draw(&state, OT_HAL_TRIANGLES, quad, 6);
    } else {
        const OtHalRect target = {0, 0, kSize, kSize};
        const OtHalRect source = {0, 0, 4, 4};   // the textures below are 4 x 4 texels as far as the game knows
        video.draw_sprite(&state, &source, &target, 0xffffffffu);
    }
    OtHalImage image;
    if (!video.read_back(&image) || image.pixels == 0) {
        return 0;
    }
    return static_cast<const uint32_t *>(image.pixels)[(kSize / 2) * (image.pitch / 4) + kSize / 2];
}

bool is_colour(uint32_t pixel, int r, int g, int b) {
    const int dr = static_cast<int>((pixel >> 16) & 0xffu) - r, dg = static_cast<int>((pixel >> 8) & 0xffu) - g, db = static_cast<int>(pixel & 0xffu) - b;
    return dr * dr + dg * dg + db * db < 40 * 40;
}

OtHalHandle make(uint32_t width, uint32_t height) {
    OtHalTextureDesc desc = {static_cast<int32_t>(width), static_cast<int32_t>(height), OT_HAL_PIXEL_ARGB1555, OT_HAL_INVALID_HANDLE};
    return video.texture_create(&desc);
}

void upload(OtHalHandle texture, const uint16_t *texels) {
    const OtHalRect area = {0, 0, 4, 4};
    video.texture_update(texture, &area, texels, 8);
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: opentony_texture_pack_test <pack dir\\> <SDL3 dir\\>\n");
        return 2;
    }
    const char *missing = 0;
    if (!ot_sdl_load(argv[2], &missing)) {
        printf("FAIL SDL3.dll: %s\n", missing != 0 ? missing : "");
        return 1;
    }
    if (!OT_SDL(SDL_Init)(SDL_INIT_VIDEO)) {
        printf("FAIL SDL_Init: %s\n", OT_SDL(SDL_GetError)());
        return 1;
    }
    ot_hal_sdl_video_gl_hints();
    ot_hal_sdl_window_request_opengl(1);
    OtHalWindowDesc window = {"OpenTony texture pack test", kSize, kSize, OT_HAL_WINDOW_HIDDEN};
    if (!ot_hal_sdl_window.create(&window)) {
        printf("FAIL window: %s\n", OT_SDL(SDL_GetError)());
        return 1;
    }
    OtHalSdlVideoOptions options = {};
    options.anisotropy = 1;   // mip maps: a world draw builds the chain of the texture it draws
    options.texture_pack = argv[1];
    ot_hal_sdl_video_gl_options(&options);
    OtHalVideoMode mode = {kSize, kSize, 32u, 0u, 0u};
    if (!video.init(&mode)) {
        printf("FAIL video init: %s\n", ot_hal_sdl_video_error());
        return 1;
    }
    const OtHalVideoStats *stats = ot_hal_sdl_video_stats();

    uint16_t red[16], green[16], yellow[16], magenta[16], cutout[16];
    for (int i = 0; i < 16; ++i) {
        red[i] = 0xfc00u;                      // alpha set, red
        green[i] = 0x83e0u;
        yellow[i] = 0xffe0u;
        magenta[i] = 0xfc1fu;
        cutout[i] = i < 8 ? 0x7fffu : 0xffffu; // white, the first two rows clear
    }

    // a hit: the picture (16 x 16, solid blue) is level 0, the texture is still 4 x 4 to the game
    const OtHalHandle a = make(4, 4);
    upload(a, red);
    check(stats->pack_files == 4, "the folder holds the four files the test made");
    check(stats->pack_hits == 1 && stats->pack_live == 1 && stats->pack_hashes == 1, "a texture whose key is in the folder is replaced");
    check(stats->pack_bytes == 16u * 16u * 4u + 16u * 16u * 4u / 3u, "its size on the GPU is level 0 and a third for the mips");
    check(is_colour(centre(a, false), 0, 0, 255), "a sprite of it draws the picture (blue), through the logical 4 x 4 source rectangle");
    check(stats->pack_flat_draws == 1, "a 2D draw of a replaced texture is counted (once: a sprite goes through the same draw)");

    // other texels in the same texture: the pack no longer applies, the texture is its own size again
    upload(a, green);
    check(stats->pack_live == 0 && stats->pack_bytes == 0 && stats->pack_hashes == 2, "other texels: hashed, no file, back to the original");
    check(is_colour(centre(a, false), 0, 255, 0), "and the original is drawn (green)");

    // the first texels again: replaced again
    upload(a, red);
    check(stats->pack_live == 1 && stats->pack_hits == 2, "the first texels again: replaced again");
    check(is_colour(centre(a, false), 0, 0, 255), "blue again");

    // a partial update cannot be hashed: the texture goes back to its own size
    const OtHalRect corner = {0, 0, 2, 2};
    const uint16_t patch[4] = {0x83e0u, 0x83e0u, 0x83e0u, 0x83e0u};
    video.texture_update(a, &corner, patch, 4);
    check(stats->pack_live == 0, "a partial update ends the replacement");

    upload(a, red);
    check(stats->pack_live == 1, "replaced once more");
    video.texture_destroy(a);
    check(stats->pack_live == 0 && stats->pack_bytes == 0, "destroying a replaced texture gives its count back");

    // files made for another texture: not the same shape (8 x 4 for a 4 x 4), smaller (2 x 2)
    const OtHalHandle wrong = make(4, 4);
    upload(wrong, yellow);
    check(stats->pack_rejected == 1 && stats->pack_live == 0, "a picture of another shape is refused");
    check(is_colour(centre(wrong, false), 255, 255, 0), "the original is drawn (yellow)");
    const OtHalHandle small = make(4, 4);
    upload(small, magenta);
    check(stats->pack_rejected == 2 && stats->pack_live == 0, "a picture smaller than the texture is refused");

    // a cut-out whose picture has a smooth alpha: still the cut-out mip chain (coverage-preserving), made on the first world draw
    const OtHalHandle fence = make(4, 4);
    upload(fence, cutout);
    check(stats->pack_live == 1, "a cut-out is replaced");
    const uint32_t before = stats->mip_cutouts;
    const uint32_t flat = stats->pack_flat_draws;
    (void)centre(fence, true);
    check(stats->mip_cutouts == before + 1 && stats->mip_builds == 1, "the picture's smooth alpha still gets the cut-out mip chain");
    check(stats->pack_flat_draws == flat, "a world draw is not counted as 2D");

    // no pack in the options: nothing is hashed
    options.texture_pack = 0;
    ot_hal_sdl_video_gl_options(&options);
    const uint32_t hashed = stats->pack_hashes;
    const OtHalHandle plain = make(4, 4);
    upload(plain, red);
    check(stats->pack_hashes == hashed && stats->pack_live == 1, "without the option nothing is hashed or replaced");
    check(is_colour(centre(plain, false), 255, 0, 0), "and the original is drawn (red)");

    video.shutdown();
    ot_hal_sdl_window.destroy();
    OT_SDL(SDL_Quit)();
    printf("%s: %d failures\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures;
}
