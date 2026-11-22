#include "audio.h"

#include <math.h>

// Portable audio helpers (audio.h).

void OT_HAL_CALL ot_hal_audio_gains(int32_t volume_mb, int32_t pan_mb, float *left, float *right) {
    int32_t left_mb = volume_mb + (pan_mb > 0 ? -pan_mb : 0);
    int32_t right_mb = volume_mb + (pan_mb < 0 ? pan_mb : 0);
    *left = left_mb <= OT_HAL_VOLUME_SILENT ? 0.0f : static_cast<float>(pow(10.0, left_mb / 2000.0));
    *right = right_mb <= OT_HAL_VOLUME_SILENT ? 0.0f : static_cast<float>(pow(10.0, right_mb / 2000.0));
}

uint32_t OT_HAL_CALL ot_hal_audio_render(const OtHalPcmFormat *format, const void *data, uint32_t size,
    uint32_t *position, int loop, float left, float right, float *out, uint32_t frames) {
    const uint32_t channels = format->channels == 2 ? 2u : 1u;
    const uint32_t bytes = format->bits_per_sample == 16 ? 2u : 1u;
    const uint32_t frame = channels * bytes;
    const uint32_t end = size - size % frame;
    const unsigned char *pcm = static_cast<const unsigned char *>(data);
    uint32_t at = *position - *position % frame;
    uint32_t written = 0;
    while (written < frames && end != 0) {
        if (at >= end) {
            if (!loop) {
                break;
            }
            at = 0;
        }
        float sample[2];
        for (uint32_t channel = 0; channel < channels; ++channel) {
            const unsigned char *at_sample = pcm + at + channel * bytes;
            sample[channel] = bytes == 2
                                  ? static_cast<float>(static_cast<int16_t>(at_sample[0] | (at_sample[1] << 8))) / 32768.0f
                                  : (static_cast<float>(at_sample[0]) - 128.0f) / 128.0f;
        }
        if (channels == 1) {
            sample[1] = sample[0];
        }
        out[written * 2] = sample[0] * left;
        out[written * 2 + 1] = sample[1] * right;
        at += frame;
        ++written;
    }
    if (loop && at >= end) {
        at = 0; // a looping voice's position is always inside the sound
    }
    *position = at;
    return written;
}
