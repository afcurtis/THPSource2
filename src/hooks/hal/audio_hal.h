#ifndef OPENTONY_HOOKS_HAL_AUDIO_HAL_H
#define OPENTONY_HOOKS_HAL_AUDIO_HAL_H

/* audio=sdl (docs/HAL.md, "Audio"): the game's sound goes through the HAL
 * audio backend instead of DirectSound.
 *
 * Audio_Init is replaced: AudioDevice becomes a stand-in IDirectSound whose
 * buffers are HAL sounds and voices. Everything else in pc\soundlib.cpp
 * (loading, the PlayStation-volume arithmetic, duplicates, looping) runs
 * unchanged on it, and so does Bink's DirectSound output (InitBink hands
 * AudioDevice to BinkSetSoundSystem), which carries the movie and music
 * sound. The stand-in keeps DirectSound's rules where retail can see them:
 * control flags and parameter ranges (DSERR_CONTROLUNAVAIL,
 * DSERR_INVALIDPARAM), a stopped buffer resuming where it stopped, a finished
 * one-shot back at position 0, duplicates sharing memory. */

#include "../../hal/audio.h"

/* The backend is initialized; the stand-in routes to it. */
void ot_audio_hal_start(const OtHalAudioBackend *audio);
/* SDL is about to stop. */
void ot_audio_hal_stop(void);
int ot_audio_hal_describe(char *out, int size);

#endif /* OPENTONY_HOOKS_HAL_AUDIO_HAL_H */
