#ifndef OPENTONY_HOOKS_HAL_MOVIE_HAL_H
#define OPENTONY_HOOKS_HAL_MOVIE_HAL_H

/* movie=sdl (docs/HAL.md, "Movie"): the full-screen movies are shown through
 * the movie service.
 *
 * Retail PCMovie_PlayStartupMedia (the startup logos and intro, GrayMat and
 * the skater videos, all 320x240) decodes each Bink frame, doubled to
 * 640x480, straight into DirectDraw's primary surface when D3DTextureMode is
 * nonzero; with video=sdl that surface is the offscreen stand-in, so nothing
 * reached the screen. The adapter runs retail's loop unchanged and, after
 * each decoded frame, hands the stand-in's picture to the movie backend,
 * which draws it through the video service. With D3DTextureMode 0 retail
 * draws the frame as a Direct3D quad and flips; video=sdl already presents
 * that flip.
 *
 * Bink stays the codec and its other users are untouched: the level-select
 * preview clips (176x48, decoded into a texture surface by PCMOVIE_Update)
 * and the music thread (Bink audio, no video).
 *
 * Test switch: OPENTONY_MOVIE_TRACE=<first>:<count>[:<step>] writes `count`
 * movie frames from `first` on, every `step` (frames counted over all movies),
 * to <log dir>/video/movie_NNNNN.hal.ottx. */

/* At HAL startup, before the movie hook points are replaced; then 1 when the
 * adapter has what it needs (binkw32.dll's BinkService and BinkWait). Frames
 * go to video=sdl's backend, which starts later (WINMAIN_InitDirectDraw). */
void ot_movie_hal_prepare(void);
int ot_movie_hal_ready(void);
int ot_movie_hal_describe(char *out, int size);

#endif /* OPENTONY_HOOKS_HAL_MOVIE_HAL_H */
