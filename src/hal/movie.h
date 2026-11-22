#ifndef OPENTONY_HAL_MOVIE_H
#define OPENTONY_HAL_MOVIE_H

/* Movie service (docs/HAL.md, "Movie").
 *
 * Bink stays the codec: binkw32.dll decodes the intro movies, the FMV
 * sequences and the music (Music/ltix*.dat are Bink audio streams, played by
 * PCMOVIE_StreamThread). Retail sends Bink's pictures to a DirectDraw surface
 * drawn as a Direct3D quad, and its sound to DirectSound. The movie service
 * owns only those two outputs: a CPU frame target the codec decodes into,
 * shown by the video service, and an audio stream (audio.h) for the PCM. */

#include "hal.h"
#include "video.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A locked frame: the codec writes `height` rows of `pitch` bytes. */
typedef struct OtHalMovieFrame {
    void *pixels;
    int32_t pitch;
    int32_t width;
    int32_t height;
    uint32_t format; /* OtHalPixelFormat (video.h): ARGB8888 or RGB565 */
} OtHalMovieFrame;

enum OtHalMovieFlags {
    OT_HAL_MOVIE_FULLSCREEN = 1u << 0, /* owns the screen until closed (startup movies) */
    OT_HAL_MOVIE_OVERLAY = 1u << 1     /* drawn over the game's frame (in-game FMV) */
};

typedef struct OtHalMovieBackend {
    const char *name;
    /* Prepares a frame target for a width x height movie. 1 on success. */
    int(OT_HAL_CALL *open)(int32_t width, int32_t height, uint32_t format, uint32_t flags);
    /* Hands out the frame target; 0 when the output is gone (focus lost). */
    int(OT_HAL_CALL *lock_frame)(OtHalMovieFrame *frame);
    void(OT_HAL_CALL *unlock_frame)(void);
    /* Shows the last decoded frame scaled into `target`, in render-target
     * pixels (0 = at its own size in the top-left corner), on black, and
     * presents; the video service scales the target to the window. */
    void(OT_HAL_CALL *present)(const OtHalRect *target);
    void(OT_HAL_CALL *close)(void);
    /* The frame target's texture, current (the last unlocked frame is uploaded
     * first), for a game that draws the picture itself as a textured quad (the
     * level select's video box). OT_HAL_INVALID_HANDLE when no movie is open.
     * May be 0 (a backend with no texture); its frame is U = V = 1 across. */
    OtHalHandle(OT_HAL_CALL *texture)(void);
} OtHalMovieBackend;

/* The movie backend drawn through a video backend (movie_video.cpp, portable):
 * the frame target is CPU memory, uploaded to one texture of the frame's
 * format (ARGB8888, RGB565, ARGB1555) and drawn as one quad on present. One
 * movie at a time, like retail's single Bink handle. */
const OtHalMovieBackend *OT_HAL_CALL ot_hal_movie_on_video(const OtHalVideoBackend *video);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_MOVIE_H */
