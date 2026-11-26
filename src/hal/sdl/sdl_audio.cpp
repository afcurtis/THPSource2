#include "sdl_internal.h"

#include <math.h>
#include <string.h>

// SDL3 audio backend (audio.h). One SDL audio stream per voice, bound to the
// default playback device; SDL resamples (the voice frequency is the stream's
// frequency ratio) and mixes. A voice's stream pulls PCM through a callback
// that renders the sound from the voice's position (ot_hal_audio_render), so
// a sound rewritten in place is heard when the voice gets there.
//
// Locking: g_lock guards sounds and voices, held briefly by the game side and
// by the stream callbacks (which SDL runs with the stream locked). The game
// side never calls an SDL stream function while holding g_lock.

namespace {

const uint32_t kMaxSounds = 1024;
const uint32_t kMaxVoices = 256; // slots 1..255; handle = generation << 8 | slot
const uint32_t kMaxStreams = 8;
const uint32_t kChunkFrames = 512;

struct Sound {
    unsigned char *data;
    uint32_t size;
    OtHalPcmFormat format;
    uint8_t used;
};

struct Voice {
    SDL_AudioStream *stream;
    uint32_t sound;
    uint32_t position;
    float left;
    float right;
    uint16_t generation;
    uint8_t used;
    uint8_t finished;
    uint8_t paused;
    uint8_t loop;
};

struct Stream {
    SDL_AudioStream *stream;
    uint8_t used;
    uint8_t paused;
};

SDL_Mutex *g_lock = 0;
SDL_AudioDeviceID g_device = 0;
SDL_AudioSpec g_device_spec;
Sound g_sounds[kMaxSounds];
Voice g_voices[kMaxVoices];
Stream g_streams[kMaxStreams];
char g_driver[32];
OtHalSdlAudioStats g_stats;

struct Held {
    Held() { OT_SDL(SDL_LockMutex)(g_lock); }
    ~Held() { OT_SDL(SDL_UnlockMutex)(g_lock); }
};

Voice *voice_of(OtHalHandle handle) {
    const uint32_t slot = handle & 0xffu;
    if (slot == 0 || slot >= kMaxVoices) {
        return 0;
    }
    Voice &voice = g_voices[slot];
    return voice.used && voice.generation == (handle >> 8) ? &voice : 0;
}

Sound *sound_of(OtHalHandle handle) {
    return handle != 0 && handle < kMaxSounds && g_sounds[handle].used ? &g_sounds[handle] : 0;
}

float ratio_of(const Sound &sound, uint32_t frequency) {
    return frequency == 0 || sound.format.sample_rate == 0
               ? 1.0f
               : static_cast<float>(frequency) / static_cast<float>(sound.format.sample_rate);
}

void SDLCALL feed_voice(void *userdata, SDL_AudioStream *stream, int additional, int) {
    const uint32_t slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userdata));
    float buffer[2 * kChunkFrames];
    int remaining = additional;
    while (remaining > 0) {
        uint32_t frames = static_cast<uint32_t>((remaining + 7) / 8);
        frames = frames < kChunkFrames ? frames : kChunkFrames;
        uint32_t rendered = 0;
        bool end_of_sound = false;
        {
            Held held;
            Voice &voice = g_voices[slot];
            if (!voice.used || voice.stream != stream || voice.paused || voice.finished) {
                return;
            }
            Sound *sound = sound_of(voice.sound);
            if (sound == 0) {
                voice.finished = 1;
                return;
            }
            rendered = ot_hal_audio_render(&sound->format, sound->data, sound->size, &voice.position, voice.loop,
                voice.left, voice.right, buffer, frames);
            if (rendered < frames && !voice.loop) {
                voice.finished = 1;
                end_of_sound = true;
            }
            g_stats.frames_rendered += rendered;
        }
        if (rendered != 0) {
            OT_SDL(SDL_PutAudioStreamData)(stream, buffer, static_cast<int>(rendered * 8u));
        }
        if (end_of_sound) {
            // Allow SDL's resampler to emit the remaining samples without more input.
            OT_SDL(SDL_FlushAudioStream)(stream);
        }
        if (rendered < frames) {
            return;
        }
        remaining -= static_cast<int>(rendered * 8u);
    }
}

bool open_device() {
    SDL_AudioSpec wanted;
    wanted.format = SDL_AUDIO_F32;
    wanted.channels = 2;
    wanted.freq = 44100;
    g_device = OT_SDL(SDL_OpenAudioDevice)(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wanted);
    if (g_device == 0) {
        return false;
    }
    if (!OT_SDL(SDL_GetAudioDeviceFormat)(g_device, &g_device_spec, 0)) {
        g_device_spec = wanted;
    }
    OT_SDL(SDL_ResumeAudioDevice)(g_device);
    return true;
}

void destroy_streams(SDL_AudioStream **streams, uint32_t count) {
    for (uint32_t index = 0; index < count; ++index) {
        OT_SDL(SDL_DestroyAudioStream)(streams[index]);
    }
}

int OT_HAL_CALL audio_init(void) {
    memset(g_sounds, 0, sizeof(g_sounds));
    memset(g_voices, 0, sizeof(g_voices));
    memset(g_streams, 0, sizeof(g_streams));
    memset(&g_stats, 0, sizeof(g_stats));
    g_driver[0] = 0;
    bool ready = OT_SDL(SDL_InitSubSystem)(SDL_INIT_AUDIO) && open_device();
    if (!ready) {
        // No device (a headless test machine): SDL's dummy driver consumes in
        // real time, so voices still advance and end.
        OT_SDL(SDL_QuitSubSystem)(SDL_INIT_AUDIO);
        OT_SDL(SDL_SetHint)(SDL_HINT_AUDIO_DRIVER, "dummy");
        ready = OT_SDL(SDL_InitSubSystem)(SDL_INIT_AUDIO) && open_device();
    }
    if (!ready) {
        OT_SDL(SDL_QuitSubSystem)(SDL_INIT_AUDIO);
        return 0;
    }
    g_lock = OT_SDL(SDL_CreateMutex)();
    const char *driver = OT_SDL(SDL_GetCurrentAudioDriver)();
    OT_SDL(SDL_strlcpy)(g_driver, driver != 0 ? driver : "?", sizeof(g_driver));
    return g_lock != 0;
}

void OT_HAL_CALL audio_shutdown(void) {
    if (g_lock == 0) {
        return;
    }
    SDL_AudioStream *streams[kMaxVoices + kMaxStreams];
    uint32_t count = 0;
    {
        Held held;
        for (uint32_t slot = 1; slot < kMaxVoices; ++slot) {
            if (g_voices[slot].used) {
                streams[count++] = g_voices[slot].stream;
                g_voices[slot].used = 0;
            }
        }
        for (uint32_t index = 0; index < kMaxStreams; ++index) {
            if (g_streams[index].used) {
                streams[count++] = g_streams[index].stream;
                g_streams[index].used = 0;
            }
        }
    }
    destroy_streams(streams, count);
    OT_SDL(SDL_CloseAudioDevice)(g_device);
    g_device = 0;
    for (uint32_t index = 0; index < kMaxSounds; ++index) {
        OT_SDL(SDL_free)(g_sounds[index].data);
    }
    memset(g_sounds, 0, sizeof(g_sounds));
    OT_SDL(SDL_DestroyMutex)(g_lock);
    g_lock = 0;
    OT_SDL(SDL_QuitSubSystem)(SDL_INIT_AUDIO);
}

OtHalHandle OT_HAL_CALL audio_sound_create(const OtHalSoundDesc *desc) {
    if (g_lock == 0 || desc == 0 || desc->size == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    unsigned char *data = static_cast<unsigned char *>(OT_SDL(SDL_malloc)(desc->size));
    if (data == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    if (desc->data != 0) {
        memcpy(data, desc->data, desc->size);
    } else {
        memset(data, desc->format.bits_per_sample == 8 ? 0x80 : 0, desc->size);
    }
    Held held;
    for (uint32_t index = 1; index < kMaxSounds; ++index) {
        if (!g_sounds[index].used) {
            g_sounds[index].data = data;
            g_sounds[index].size = desc->size;
            g_sounds[index].format = desc->format;
            g_sounds[index].used = 1;
            ++g_stats.sounds;
            return index;
        }
    }
    OT_SDL(SDL_free)(data);
    return OT_HAL_INVALID_HANDLE;
}

void OT_HAL_CALL audio_sound_write(OtHalHandle handle, uint32_t offset, const void *data, uint32_t size) {
    if (g_lock == 0 || data == 0) {
        return;
    }
    Held held;
    Sound *sound = sound_of(handle);
    if (sound != 0 && offset < sound->size) {
        memcpy(sound->data + offset, data, size < sound->size - offset ? size : sound->size - offset);
        ++g_stats.sound_writes;
    }
}

void OT_HAL_CALL audio_sound_destroy(OtHalHandle handle) {
    if (g_lock == 0) {
        return;
    }
    SDL_AudioStream *streams[kMaxVoices];
    uint32_t count = 0;
    unsigned char *data = 0;
    {
        Held held;
        Sound *sound = sound_of(handle);
        if (sound == 0) {
            return;
        }
        for (uint32_t slot = 1; slot < kMaxVoices; ++slot) {
            if (g_voices[slot].used && g_voices[slot].sound == handle) {
                streams[count++] = g_voices[slot].stream;
                g_voices[slot].used = 0;
                --g_stats.voices_live;
            }
        }
        data = sound->data;
        memset(sound, 0, sizeof(*sound));
    }
    destroy_streams(streams, count);
    OT_SDL(SDL_free)(data);
}

// Voices that played to their end release their slot and stream here (the
// handle then reads as finished: not playing, position 0).
void reap_finished() {
    uint32_t slots[kMaxVoices];
    uint16_t generations[kMaxVoices];
    SDL_AudioStream *streams[kMaxVoices];
    uint32_t count = 0;
    {
        Held held;
        for (uint32_t slot = 1; slot < kMaxVoices; ++slot) {
            const Voice &voice = g_voices[slot];
            if (voice.used && voice.finished) {
                slots[count] = slot;
                generations[count] = voice.generation;
                streams[count++] = voice.stream;
            }
        }
    }
    // Drained ones only, asked without g_lock (the stream lock comes first).
    uint32_t drained = 0;
    for (uint32_t index = 0; index < count; ++index) {
        if (OT_SDL(SDL_GetAudioStreamQueued)(streams[index]) <= 0) {
            slots[drained] = slots[index];
            generations[drained] = generations[index];
            streams[drained++] = streams[index];
        }
    }
    uint32_t reaped = 0;
    {
        Held held;
        for (uint32_t index = 0; index < drained; ++index) {
            Voice &voice = g_voices[slots[index]];
            if (voice.used && voice.finished && voice.generation == generations[index]) {
                voice.used = 0;
                --g_stats.voices_live;
                streams[reaped++] = streams[index];
            }
        }
    }
    destroy_streams(streams, reaped);
}

OtHalHandle OT_HAL_CALL audio_voice_play(OtHalHandle handle, const OtHalVoiceParams *params, uint32_t position) {
    if (g_lock == 0 || params == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    reap_finished();
    SDL_AudioSpec source;
    float ratio = 1.0f;
    {
        Held held;
        Sound *sound = sound_of(handle);
        if (sound == 0) {
            return OT_HAL_INVALID_HANDLE;
        }
        source.format = SDL_AUDIO_F32;
        source.channels = 2;
        source.freq = static_cast<int>(sound->format.sample_rate);
        ratio = ratio_of(*sound, params->frequency);
    }
    SDL_AudioStream *stream = OT_SDL(SDL_CreateAudioStream)(&source, &g_device_spec);
    if (stream == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    OT_SDL(SDL_SetAudioStreamFrequencyRatio)(stream, ratio);
    uint32_t slot = 0;
    OtHalHandle voice_handle = OT_HAL_INVALID_HANDLE;
    {
        Held held;
        for (slot = 1; slot < kMaxVoices && g_voices[slot].used; ++slot) {
        }
        if (slot < kMaxVoices) {
            Voice &voice = g_voices[slot];
            voice.stream = stream;
            voice.sound = handle;
            voice.position = position;
            voice.loop = (params->flags & OT_HAL_VOICE_LOOP) != 0;
            voice.finished = 0;
            voice.paused = 0;
            ot_hal_audio_gains(params->volume_mb, params->pan_mb, &voice.left, &voice.right);
            voice.generation = static_cast<uint16_t>(voice.generation + 1u);
            voice.used = 1;
            voice_handle = static_cast<OtHalHandle>(voice.generation) << 8 | slot;
            ++g_stats.voices;
            ++g_stats.voices_live;
        }
    }
    if (voice_handle == OT_HAL_INVALID_HANDLE) {
        OT_SDL(SDL_DestroyAudioStream)(stream);
        return OT_HAL_INVALID_HANDLE;
    }
    OT_SDL(SDL_SetAudioStreamGetCallback)(stream, feed_voice, reinterpret_cast<void *>(static_cast<uintptr_t>(slot)));
    OT_SDL(SDL_BindAudioStream)(g_device, stream);
    return voice_handle;
}

void OT_HAL_CALL audio_voice_update(OtHalHandle handle, const OtHalVoiceParams *params) {
    if (g_lock == 0 || params == 0) {
        return;
    }
    SDL_AudioStream *stream = 0;
    float ratio = 1.0f;
    {
        Held held;
        Voice *voice = voice_of(handle);
        Sound *sound = voice != 0 ? sound_of(voice->sound) : 0;
        if (voice == 0 || sound == 0) {
            return;
        }
        voice->loop = (params->flags & OT_HAL_VOICE_LOOP) != 0;
        ot_hal_audio_gains(params->volume_mb, params->pan_mb, &voice->left, &voice->right);
        ratio = ratio_of(*sound, params->frequency);
        stream = voice->stream;
    }
    OT_SDL(SDL_SetAudioStreamFrequencyRatio)(stream, ratio);
}

void OT_HAL_CALL audio_voice_stop(OtHalHandle handle) {
    if (g_lock == 0) {
        return;
    }
    SDL_AudioStream *stream = 0;
    {
        Held held;
        Voice *voice = voice_of(handle);
        if (voice == 0) {
            return;
        }
        stream = voice->stream;
        voice->used = 0;
        --g_stats.voices_live;
    }
    OT_SDL(SDL_DestroyAudioStream)(stream);
}

void OT_HAL_CALL audio_voice_pause(OtHalHandle handle, int paused) {
    if (g_lock == 0) {
        return;
    }
    SDL_AudioStream *stream = 0;
    {
        Held held;
        Voice *voice = voice_of(handle);
        if (voice == 0) {
            return;
        }
        voice->paused = paused != 0;
        stream = paused != 0 ? voice->stream : 0;
    }
    if (stream != 0) {
        OT_SDL(SDL_ClearAudioStream)(stream); // silent at once, as DirectSound's Stop
    }
}

int OT_HAL_CALL audio_voice_playing(OtHalHandle handle) {
    if (g_lock == 0) {
        return 0;
    }
    SDL_AudioStream *stream;
    {
        Held held;
        const Voice *voice = voice_of(handle);
        if (voice == 0) {
            return 0;
        }
        if (!voice->finished) {
            return 1;
        }
        stream = voice->stream;
    }
    // The producer can reach EOF before the device has played the queued tail.
    return OT_SDL(SDL_GetAudioStreamQueued)(stream) > 0;
}

uint32_t OT_HAL_CALL audio_voice_position(OtHalHandle handle) {
    if (g_lock == 0) {
        return 0;
    }
    Held held;
    const Voice *voice = voice_of(handle);
    return voice != 0 && !voice->finished ? voice->position : 0;
}

void OT_HAL_CALL audio_voice_set_position(OtHalHandle handle, uint32_t position) {
    if (g_lock == 0) {
        return;
    }
    Held held;
    Voice *voice = voice_of(handle);
    if (voice != 0) {
        voice->position = position;
        voice->finished = 0;
    }
}

Stream *stream_of(OtHalHandle handle) {
    return handle != 0 && handle <= kMaxStreams && g_streams[handle - 1].used ? &g_streams[handle - 1] : 0;
}

OtHalHandle OT_HAL_CALL audio_stream_open(const OtHalPcmFormat *format) {
    if (g_lock == 0 || format == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    SDL_AudioSpec source;
    source.format = format->bits_per_sample == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    source.channels = format->channels;
    source.freq = static_cast<int>(format->sample_rate);
    for (uint32_t index = 0; index < kMaxStreams; ++index) {
        if (!g_streams[index].used) {
            SDL_AudioStream *stream = OT_SDL(SDL_CreateAudioStream)(&source, &g_device_spec);
            if (stream == 0 || !OT_SDL(SDL_BindAudioStream)(g_device, stream)) {
                OT_SDL(SDL_DestroyAudioStream)(stream);
                return OT_HAL_INVALID_HANDLE;
            }
            g_streams[index].stream = stream;
            g_streams[index].used = 1;
            g_streams[index].paused = 0;
            return index + 1u;
        }
    }
    return OT_HAL_INVALID_HANDLE;
}

uint32_t OT_HAL_CALL audio_stream_write(OtHalHandle handle, const void *data, uint32_t size) {
    Stream *stream = stream_of(handle);
    return stream != 0 && OT_SDL(SDL_PutAudioStreamData)(stream->stream, data, static_cast<int>(size)) ? size : 0u;
}

uint32_t OT_HAL_CALL audio_stream_queued(OtHalHandle handle) {
    Stream *stream = stream_of(handle);
    const int queued = stream != 0 ? OT_SDL(SDL_GetAudioStreamQueued)(stream->stream) : 0;
    return queued > 0 ? static_cast<uint32_t>(queued) : 0u;
}

void OT_HAL_CALL audio_stream_set_volume(OtHalHandle handle, int32_t volume_mb) {
    Stream *stream = stream_of(handle);
    if (stream != 0) {
        const float gain = volume_mb <= OT_HAL_VOLUME_SILENT ? 0.0f : static_cast<float>(pow(10.0, volume_mb / 2000.0));
        OT_SDL(SDL_SetAudioStreamGain)(stream->stream, gain);
    }
}

void OT_HAL_CALL audio_stream_pause(OtHalHandle handle, int paused) {
    Stream *stream = stream_of(handle);
    if (stream != 0 && (stream->paused != 0) != (paused != 0)) {
        stream->paused = paused != 0;
        if (paused != 0) {
            OT_SDL(SDL_UnbindAudioStream)(stream->stream); // keeps what is queued
        } else {
            OT_SDL(SDL_BindAudioStream)(g_device, stream->stream);
        }
    }
}

void OT_HAL_CALL audio_stream_close(OtHalHandle handle) {
    Stream *stream = stream_of(handle);
    if (stream != 0) {
        OT_SDL(SDL_DestroyAudioStream)(stream->stream);
        stream->used = 0;
    }
}

} // namespace

extern "C" const OtHalAudioBackend ot_hal_sdl_audio = {
    "sdl-audio",
    audio_init,
    audio_shutdown,
    audio_sound_create,
    audio_sound_write,
    audio_sound_destroy,
    audio_voice_play,
    audio_voice_update,
    audio_voice_stop,
    audio_voice_pause,
    audio_voice_playing,
    audio_voice_position,
    audio_voice_set_position,
    audio_stream_open,
    audio_stream_write,
    audio_stream_queued,
    audio_stream_set_volume,
    audio_stream_pause,
    audio_stream_close,
};

extern "C" const char *ot_hal_sdl_audio_driver(void) {
    return g_driver;
}

extern "C" const OtHalSdlAudioStats *ot_hal_sdl_audio_stats(void) {
    return &g_stats;
}
