#ifndef OPENTONY_HAL_AUDIO_H
#define OPENTONY_HAL_AUDIO_H

/* Audio service (docs/HAL.md, "Audio").
 *
 * Retail loads every sound effect from a PCM WAV into its own DirectSound
 * buffer (Audio_LoadSoundBank); a voice is one of up to eight duplicates of
 * that buffer, addressed (duplicate << 16) | sound. Volume, pan and frequency
 * are set in DirectSound units computed from the PlayStation left/right
 * volumes. Bink streams movie and music sound through a looping DirectSound
 * buffer it refills behind the play cursor. The HAL keeps those units and that
 * model, which are plain acoustics rather than DirectSound types: a sound is
 * PCM that may be rewritten in place, a voice plays it from a byte position,
 * once or looping, and reports where it is. Only the backend converts to
 * linear gain:
 *
 *   gain = 10 ^ (millibels / 2000)
 *
 * Streams carry PCM produced elsewhere, queued rather than looped. */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OT_HAL_VOLUME_SILENT (-10000) /* millibels; -100 dB is silence */
#define OT_HAL_PAN_LEFT (-10000)      /* right channel attenuated 100 dB */
#define OT_HAL_PAN_RIGHT 10000        /* left channel attenuated 100 dB */
#define OT_HAL_MAX_FREQUENCY 88200u   /* retail clamps voice frequency here */

/* Interleaved little-endian PCM (8-bit unsigned or 16-bit signed). */
typedef struct OtHalPcmFormat {
    uint32_t sample_rate;
    uint16_t channels;        /* 1 or 2 */
    uint16_t bits_per_sample; /* 8 or 16 */
} OtHalPcmFormat;

typedef struct OtHalSoundDesc {
    OtHalPcmFormat format;
    const void *data; /* copied by sound_create; 0 = silence */
    uint32_t size;    /* bytes, a whole number of frames */
} OtHalSoundDesc;

enum OtHalVoiceFlags {
    OT_HAL_VOICE_LOOP = 1u << 0
};

typedef struct OtHalVoiceParams {
    int32_t volume_mb;  /* attenuation, OT_HAL_VOLUME_SILENT .. 0 */
    int32_t pan_mb;     /* OT_HAL_PAN_LEFT .. OT_HAL_PAN_RIGHT */
    uint32_t frequency; /* playback rate in Hz; 0 = the sound's own rate */
    uint32_t flags;     /* OtHalVoiceFlags */
} OtHalVoiceParams;

typedef struct OtHalAudioBackend {
    const char *name;
    int(OT_HAL_CALL *init)(void); /* 1 on success; 0 = no audio at all */
    void(OT_HAL_CALL *shutdown)(void);

    OtHalHandle(OT_HAL_CALL *sound_create)(const OtHalSoundDesc *desc);
    /* Replaces `size` bytes at `offset` in place: a voice playing the sound
     * hears them when it gets there (how a looping stream is refilled). */
    void(OT_HAL_CALL *sound_write)(OtHalHandle sound, uint32_t offset, const void *data, uint32_t size);
    void(OT_HAL_CALL *sound_destroy)(OtHalHandle sound); /* stops its voices */

    /* Starts a new voice of `sound` at byte `position` (a frame boundary). */
    OtHalHandle(OT_HAL_CALL *voice_play)(OtHalHandle sound, const OtHalVoiceParams *params, uint32_t position);
    void(OT_HAL_CALL *voice_update)(OtHalHandle voice, const OtHalVoiceParams *params);
    void(OT_HAL_CALL *voice_stop)(OtHalHandle voice); /* ends it; the handle is gone */
    void(OT_HAL_CALL *voice_pause)(OtHalHandle voice, int paused);
    /* 0 once a voice played to its end (not looping) or was stopped. */
    int(OT_HAL_CALL *voice_playing)(OtHalHandle voice);
    /* The byte offset in the sound of the next frame the voice plays. */
    uint32_t(OT_HAL_CALL *voice_position)(OtHalHandle voice);
    void(OT_HAL_CALL *voice_set_position)(OtHalHandle voice, uint32_t position);

    /* Streaming PCM: the producer writes, the device drains. */
    OtHalHandle(OT_HAL_CALL *stream_open)(const OtHalPcmFormat *format);
    /* Queues `size` bytes; returns the bytes accepted. */
    uint32_t(OT_HAL_CALL *stream_write)(OtHalHandle stream, const void *data, uint32_t size);
    uint32_t(OT_HAL_CALL *stream_queued)(OtHalHandle stream); /* bytes not yet played */
    void(OT_HAL_CALL *stream_set_volume)(OtHalHandle stream, int32_t volume_mb);
    void(OT_HAL_CALL *stream_pause)(OtHalHandle stream, int paused);
    void(OT_HAL_CALL *stream_close)(OtHalHandle stream);
} OtHalAudioBackend;

/* Portable helpers for backends (audio.cpp). */

/* Linear channel gains for a volume and a pan in millibels: the pan
 * attenuates the other channel, as DirectSound does; OT_HAL_VOLUME_SILENT or
 * below is silence. */
void OT_HAL_CALL ot_hal_audio_gains(int32_t volume_mb, int32_t pan_mb, float *left, float *right);

/* Renders up to `frames` frames of `format` PCM (`data`, `size` bytes) from
 * byte `*position` as interleaved stereo float, scaled by the gains; a
 * looping voice wraps to 0 at the end, a one-shot stops there. Returns the
 * frames written and advances `*position` (a looping voice's position
 * wraps to 0 at the end, a finished one-shot's stays there). */
uint32_t OT_HAL_CALL ot_hal_audio_render(const OtHalPcmFormat *format, const void *data, uint32_t size,
    uint32_t *position, int loop, float left, float right, float *out, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_AUDIO_H */
