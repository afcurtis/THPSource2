#include "movie.h"

#include <stdlib.h>
#include <string.h>

// Movie frames through the video service (movie.h): a CPU frame target, one
// texture, one quad.

namespace {

const OtHalVideoBackend *g_video = 0;
OtHalHandle g_texture = OT_HAL_INVALID_HANDLE;
unsigned char *g_pixels = 0;
int32_t g_pitch = 0;
int32_t g_width = 0;
int32_t g_height = 0;
uint32_t g_format = 0;
int g_dirty = 0;

int32_t bytes_per_pixel(uint32_t format) {
    switch (format) {
    case OT_HAL_PIXEL_ARGB8888: return 4;
    case OT_HAL_PIXEL_RGB565:
    case OT_HAL_PIXEL_ARGB1555: return 2;
    default: return 0;
    }
}

void OT_HAL_CALL movie_close(void) {
    if (g_texture != OT_HAL_INVALID_HANDLE && g_video != 0) {
        g_video->texture_destroy(g_texture);
    }
    g_texture = OT_HAL_INVALID_HANDLE;
    free(g_pixels);
    g_pixels = 0;
    g_width = g_height = g_pitch = 0;
    g_dirty = 0;
}

int OT_HAL_CALL movie_open(int32_t width, int32_t height, uint32_t format, uint32_t) {
    movie_close();
    const int32_t bytes = bytes_per_pixel(format);
    if (g_video == 0 || bytes == 0 || width <= 0 || height <= 0) {
        return 0;
    }
    g_pitch = width * bytes;
    g_pixels = static_cast<unsigned char *>(calloc(static_cast<size_t>(height), static_cast<size_t>(g_pitch)));
    const OtHalTextureDesc desc = {width, height, format, OT_HAL_INVALID_HANDLE};
    g_texture = g_pixels != 0 ? g_video->texture_create(&desc) : OT_HAL_INVALID_HANDLE;
    if (g_texture == OT_HAL_INVALID_HANDLE) {
        movie_close();
        return 0;
    }
    g_width = width;
    g_height = height;
    g_format = format;
    g_dirty = 1;
    return 1;
}

int OT_HAL_CALL movie_lock_frame(OtHalMovieFrame *frame) {
    if (g_pixels == 0 || frame == 0) {
        return 0;
    }
    frame->pixels = g_pixels;
    frame->pitch = g_pitch;
    frame->width = g_width;
    frame->height = g_height;
    frame->format = g_format;
    return 1;
}

void OT_HAL_CALL movie_unlock_frame(void) {
    g_dirty = 1;
}

OtHalHandle OT_HAL_CALL movie_texture(void) {
    if (g_texture == OT_HAL_INVALID_HANDLE) {
        return OT_HAL_INVALID_HANDLE;
    }
    if (g_dirty) {
        const OtHalRect area = {0, 0, g_width, g_height};
        g_video->texture_update(g_texture, &area, g_pixels, g_pitch);
        g_dirty = 0;
    }
    return g_texture;
}

void OT_HAL_CALL movie_present(const OtHalRect *target) {
    if (g_texture == OT_HAL_INVALID_HANDLE) {
        return;
    }
    if (g_dirty) {
        const OtHalRect area = {0, 0, g_width, g_height};
        g_video->texture_update(g_texture, &area, g_pixels, g_pitch);
        g_dirty = 0;
    }
    const OtHalRect where = target != 0 ? *target : OtHalRect{0, 0, g_width, g_height};
    OtHalDrawState state;
    memset(&state, 0, sizeof(state));
    state.texture = g_texture;
    if (where.w != g_width || where.h != g_height) {
        state.flags = OT_HAL_DRAW_FILTER; // scaled; texel for pixel otherwise
    }
    // Pixel centres on integers (the Direct3D convention the backends honour).
    const float x0 = static_cast<float>(where.x) - 0.5f;
    const float y0 = static_cast<float>(where.y) - 0.5f;
    const float x1 = x0 + static_cast<float>(where.w);
    const float y1 = y0 + static_cast<float>(where.h);
    const OtHalVertex quad[4] = {
        {x0, y0, 0.0f, 1.0f, 0xffffffffu, 0xff000000u, 0.0f, 0.0f},
        {x1, y0, 0.0f, 1.0f, 0xffffffffu, 0xff000000u, 1.0f, 0.0f},
        {x1, y1, 0.0f, 1.0f, 0xffffffffu, 0xff000000u, 1.0f, 1.0f},
        {x0, y1, 0.0f, 1.0f, 0xffffffffu, 0xff000000u, 0.0f, 1.0f},
    };
    g_video->begin_frame();
    g_video->clear(OT_HAL_CLEAR_COLOR, 0xff000000u, 0.0f, 0);
    g_video->set_scissor(0);
    g_video->draw(&state, OT_HAL_TRIANGLE_FAN, quad, 4);
    g_video->present();
}

const OtHalMovieBackend kMovieOnVideo = {
    "movie-on-video", movie_open, movie_lock_frame, movie_unlock_frame, movie_present, movie_close, movie_texture,
};

} // namespace

const OtHalMovieBackend *OT_HAL_CALL ot_hal_movie_on_video(const OtHalVideoBackend *video) {
    if (video != g_video) {
        movie_close();
        g_video = video;
    }
    return &kMovieOnVideo;
}
