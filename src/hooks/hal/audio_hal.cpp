#include "audio_hal.h"

#include "hal_adapters.h"
#include "../hook_runtime.h"
#include "../../hal/audio.h"

#include <string.h>

// audio=sdl adapters (audio_hal.h). Retail layouts: DirectSound (DirectX 7
// headers) IDirectSound and IDirectSoundBuffer vtables, DSBUFFERDESC,
// WAVEFORMATEX, DSCAPS, DSBCAPS.

namespace {

// ------------------------------------------------------------ DirectSound

const HRESULT kOk = 0;
const HRESULT kNoInterface = static_cast<HRESULT>(0x80004002L);
const HRESULT kInvalidParam = static_cast<HRESULT>(0x80070057L);    // DSERR_INVALIDPARAM
const HRESULT kOutOfMemory = static_cast<HRESULT>(0x8007000EL);     // DSERR_OUTOFMEMORY
const HRESULT kControlUnavailable = static_cast<HRESULT>(0x8878001EL); // DSERR_CONTROLUNAVAIL
const HRESULT kInvalidCall = static_cast<HRESULT>(0x88780032L);     // DSERR_INVALIDCALL
const HRESULT kBadFormat = static_cast<HRESULT>(0x88780064L);       // DSERR_BADFORMAT
const HRESULT kAlreadyInitialized = static_cast<HRESULT>(0x88780082L); // DSERR_ALREADYINITIALIZED

const DWORD kCapsPrimary = 0x1, kCapsLocSoftware = 0x8, kCapsCtrlFrequency = 0x20,
            kCapsCtrlPan = 0x40, kCapsCtrlVolume = 0x80;
const DWORD kPlayLooping = 0x1;
const DWORD kStatusPlaying = 0x1, kStatusLooping = 0x4;
const DWORD kLockFromWriteCursor = 0x1, kLockEntireBuffer = 0x2;
const LONG kVolumeMin = -10000, kVolumeMax = 0, kPanLeft = -10000, kPanRight = 10000;
const DWORD kFrequencyMin = 100, kFrequencyMax = 200000; // DSBFREQUENCY_MIN/MAX (0 = the format's rate)

#pragma pack(push, 1)
struct WaveFormat { // WAVEFORMATEX
    WORD tag, channels;
    DWORD rate, bytes_per_second;
    WORD align, bits, extra;
};
#pragma pack(pop)

struct BufferDesc { // DSBUFFERDESC (0x14 in DirectX 6, 0x24 with guid3DAlgorithm)
    DWORD size, flags, bytes, reserved;
    WaveFormat *format;
};

struct BufferCaps { // DSBCAPS
    DWORD size, flags, bytes, unlock_transfer_rate, play_cpu_overhead;
};

// IID_IDirectSound, IID_IDirectSoundBuffer
const GUID kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIidDirectSound = {0x279afa83, 0x4981, 0x11ce, {0xa5, 0x21, 0x00, 0x20, 0xaf, 0x0b, 0xe5, 0x60}};
const GUID kIidDirectSoundBuffer = {0x279afa85, 0x4981, 0x11ce, {0xa5, 0x21, 0x00, 0x20, 0xaf, 0x0b, 0xe5, 0x60}};

template <typename T>
T &global(int symbol) {
    return *reinterpret_cast<T *>(ot_hal_data(symbol));
}

// ------------------------------------------------------------------ state

const OtHalAudioBackend *g_audio = 0;
CRITICAL_SECTION g_lock; // the game thread and Bink's sound thread both call in

struct Guard {
    Guard() { EnterCriticalSection(&g_lock); }
    ~Guard() { LeaveCriticalSection(&g_lock); }
};

const DWORD kDeviceSlots = 11;
const DWORD kBufferSlots = 21;
DWORD g_device_calls[kDeviceSlots];
DWORD g_buffer_calls[kBufferSlots];
DWORD g_buffers_created = 0;
DWORD g_buffers_live = 0;
DWORD g_duplicates = 0;
DWORD g_plays = 0;
DWORD g_rejected = 0; // calls answered with a DirectSound error

// The PCM a buffer and its duplicates share (DirectSound duplicates share
// memory), mirrored in one HAL sound.
struct Shared {
    LONG references;
    unsigned char *data;
    DWORD size;
    WaveFormat format;
    OtHalHandle sound;
};

struct Buffer {
    void *const *vtable;
    LONG references;
    Shared *shared; // 0 for the primary buffer
    DWORD flags;
    OtHalHandle voice;
    DWORD position; // while there is no voice
    LONG volume;
    LONG pan;
    DWORD frequency; // 0 = the format's rate
    bool playing;    // Play without Stop since
    bool looping;
};

extern void *const g_buffer_vtable[kBufferSlots];
WaveFormat g_primary_format = {1, 2, 22050, 88200, 4, 16, 0};
Buffer g_primary = {g_buffer_vtable, 0, 0, kCapsPrimary, 0, 0, 0, 0, 0, false, false};

struct Device {
    void *const *vtable;
};
extern void *const g_device_vtable[kDeviceSlots];
Device g_device = {g_device_vtable};
LONG g_device_references = 0;

bool same_guid(const GUID *left, const GUID &right) {
    return left != 0 && memcmp(left, &right, sizeof(GUID)) == 0;
}

HRESULT reject(HRESULT result) {
    ++g_rejected;
    return result;
}

OtHalVoiceParams params_of(const Buffer &buffer) {
    OtHalVoiceParams params;
    params.volume_mb = buffer.volume;
    params.pan_mb = buffer.pan;
    params.frequency = buffer.frequency;
    params.flags = buffer.looping ? OT_HAL_VOICE_LOOP : 0u;
    return params;
}

// A one-shot voice that played to its end is gone and its buffer is back at
// the start, as DirectSound leaves it.
void settle(Buffer &buffer) {
    if (buffer.voice != 0 && !g_audio->voice_playing(buffer.voice)) {
        g_audio->voice_stop(buffer.voice);
        buffer.voice = 0;
        buffer.position = 0;
        buffer.playing = false;
    }
}

DWORD current_position(Buffer &buffer) {
    settle(buffer);
    return buffer.voice != 0 ? g_audio->voice_position(buffer.voice) : buffer.position;
}

// ------------------------------------------------- stand-in IDirectSoundBuffer

HRESULT __stdcall buffer_query_interface(Buffer *buffer, const GUID *iid, void **out) {
    ++g_buffer_calls[0];
    if (out == 0) {
        return reject(kInvalidParam);
    }
    if (same_guid(iid, kIidUnknown) || same_guid(iid, kIidDirectSoundBuffer)) {
        InterlockedIncrement(&buffer->references);
        *out = buffer;
        return kOk;
    }
    *out = 0;
    return reject(kNoInterface);
}

ULONG __stdcall buffer_add_ref(Buffer *buffer) {
    ++g_buffer_calls[1];
    return static_cast<ULONG>(InterlockedIncrement(&buffer->references));
}

ULONG __stdcall buffer_release(Buffer *buffer) {
    ++g_buffer_calls[2];
    Guard guard;
    const LONG left = --buffer->references;
    if (left > 0 || buffer == &g_primary) {
        return static_cast<ULONG>(left > 0 ? left : 0);
    }
    if (buffer->voice != 0) {
        g_audio->voice_stop(buffer->voice);
    }
    Shared *shared = buffer->shared;
    if (shared != 0 && --shared->references == 0) {
        g_audio->sound_destroy(shared->sound);
        HeapFree(GetProcessHeap(), 0, shared->data);
        HeapFree(GetProcessHeap(), 0, shared);
    }
    HeapFree(GetProcessHeap(), 0, buffer);
    --g_buffers_live;
    return 0;
}

HRESULT __stdcall buffer_get_caps(Buffer *buffer, BufferCaps *caps) {
    ++g_buffer_calls[3];
    if (caps == 0 || caps->size < sizeof(BufferCaps)) {
        return reject(kInvalidParam);
    }
    caps->flags = buffer->flags | (buffer->shared != 0 ? kCapsLocSoftware : 0u);
    caps->bytes = buffer->shared != 0 ? buffer->shared->size : 0u;
    caps->unlock_transfer_rate = 0;
    caps->play_cpu_overhead = 0;
    return kOk;
}

HRESULT __stdcall buffer_get_current_position(Buffer *buffer, DWORD *play, DWORD *write) {
    ++g_buffer_calls[4];
    Guard guard;
    // The backend reports what it has taken for mixing: writing from there
    // on is safe, so the write cursor is the play cursor.
    const DWORD position = buffer->shared != 0 ? current_position(*buffer) : 0u;
    if (play != 0) {
        *play = position;
    }
    if (write != 0) {
        *write = position;
    }
    return kOk;
}

HRESULT __stdcall buffer_get_format(Buffer *buffer, WaveFormat *format, DWORD size, DWORD *written) {
    ++g_buffer_calls[5];
    const WaveFormat &own = buffer->shared != 0 ? buffer->shared->format : g_primary_format;
    const DWORD needed = sizeof(WaveFormat);
    if (format == 0) {
        if (written == 0) {
            return reject(kInvalidParam);
        }
        *written = needed;
        return kOk;
    }
    const DWORD copied = size < needed ? size : needed;
    memcpy(format, &own, copied);
    if (written != 0) {
        *written = copied;
    }
    return kOk;
}

HRESULT __stdcall buffer_get_volume(Buffer *buffer, LONG *volume) {
    ++g_buffer_calls[6];
    if (volume == 0) {
        return reject(kInvalidParam);
    }
    if (!(buffer->flags & kCapsCtrlVolume) && buffer != &g_primary) {
        return reject(kControlUnavailable);
    }
    *volume = buffer->volume;
    return kOk;
}

HRESULT __stdcall buffer_get_pan(Buffer *buffer, LONG *pan) {
    ++g_buffer_calls[7];
    if (pan == 0) {
        return reject(kInvalidParam);
    }
    if (!(buffer->flags & kCapsCtrlPan) && buffer != &g_primary) {
        return reject(kControlUnavailable);
    }
    *pan = buffer->pan;
    return kOk;
}

HRESULT __stdcall buffer_get_frequency(Buffer *buffer, DWORD *frequency) {
    ++g_buffer_calls[8];
    if (frequency == 0) {
        return reject(kInvalidParam);
    }
    if (!(buffer->flags & kCapsCtrlFrequency)) {
        return reject(kControlUnavailable);
    }
    const DWORD rate = buffer->shared != 0 ? buffer->shared->format.rate : g_primary_format.rate;
    *frequency = buffer->frequency != 0 ? buffer->frequency : rate;
    return kOk;
}

HRESULT __stdcall buffer_get_status(Buffer *buffer, DWORD *status) {
    ++g_buffer_calls[9];
    if (status == 0) {
        return reject(kInvalidParam);
    }
    Guard guard;
    if (buffer->shared != 0) {
        settle(*buffer);
    }
    *status = buffer->playing ? kStatusPlaying | (buffer->looping ? kStatusLooping : 0u) : 0u;
    return kOk;
}

HRESULT __stdcall buffer_initialize(Buffer *, void *, const BufferDesc *) {
    ++g_buffer_calls[10];
    return reject(kAlreadyInitialized);
}

HRESULT __stdcall buffer_lock(Buffer *buffer, DWORD offset, DWORD bytes, void **first, DWORD *first_bytes,
    void **second, DWORD *second_bytes, DWORD flags) {
    ++g_buffer_calls[11];
    Guard guard;
    Shared *shared = buffer->shared;
    if (shared == 0) {
        return reject(kInvalidCall); // the primary: only with DSSCL_WRITEPRIMARY
    }
    if (flags & kLockEntireBuffer) {
        bytes = shared->size;
    }
    if (flags & kLockFromWriteCursor) {
        offset = current_position(*buffer);
    }
    // Bink keeps its own write offset and asks for offset == size when it
    // has filled the ring to the end: it means the start again.
    offset %= shared->size;
    if (first == 0 || first_bytes == 0 || bytes == 0 || bytes > shared->size) {
        return reject(kInvalidParam);
    }
    const DWORD head = bytes < shared->size - offset ? bytes : shared->size - offset;
    *first = shared->data + offset;
    *first_bytes = head;
    if (second != 0) {
        *second = head < bytes ? shared->data : 0;
    }
    if (second_bytes != 0) {
        *second_bytes = head < bytes ? bytes - head : 0u;
    }
    return kOk;
}

HRESULT __stdcall buffer_play(Buffer *buffer, DWORD, DWORD, DWORD flags) {
    ++g_buffer_calls[12];
    Guard guard;
    buffer->looping = (flags & kPlayLooping) != 0;
    if (buffer->shared == 0) {
        buffer->playing = true;
        return kOk;
    }
    settle(*buffer);
    const OtHalVoiceParams params = params_of(*buffer);
    if (buffer->voice != 0) {
        g_audio->voice_update(buffer->voice, &params);
        g_audio->voice_pause(buffer->voice, 0);
    } else {
        buffer->voice = g_audio->voice_play(buffer->shared->sound, &params, buffer->position);
    }
    buffer->playing = true;
    ++g_plays;
    return kOk;
}

HRESULT __stdcall buffer_set_current_position(Buffer *buffer, DWORD position) {
    ++g_buffer_calls[13];
    Guard guard;
    if (buffer->shared == 0) {
        return reject(kInvalidCall);
    }
    const WaveFormat &format = buffer->shared->format;
    if (position >= buffer->shared->size) {
        return reject(kInvalidParam);
    }
    position -= position % (format.align != 0 ? format.align : 1u);
    if (buffer->voice != 0) {
        g_audio->voice_set_position(buffer->voice, position);
    } else {
        buffer->position = position;
    }
    return kOk;
}

HRESULT __stdcall buffer_set_format(Buffer *buffer, const WaveFormat *format) {
    ++g_buffer_calls[14];
    if (buffer->shared != 0) {
        return reject(kInvalidCall); // secondary buffers keep their format
    }
    if (format == 0 || format->tag != 1) {
        return reject(kInvalidParam);
    }
    g_primary_format = *format;
    g_primary_format.extra = 0;
    return kOk;
}

HRESULT __stdcall buffer_set_volume(Buffer *buffer, LONG volume) {
    ++g_buffer_calls[15];
    Guard guard;
    if (!(buffer->flags & kCapsCtrlVolume) && buffer != &g_primary) {
        return reject(kControlUnavailable);
    }
    if (volume < kVolumeMin || volume > kVolumeMax) {
        return reject(kInvalidParam);
    }
    buffer->volume = volume;
    if (buffer->voice != 0) {
        const OtHalVoiceParams params = params_of(*buffer);
        g_audio->voice_update(buffer->voice, &params);
    }
    return kOk;
}

HRESULT __stdcall buffer_set_pan(Buffer *buffer, LONG pan) {
    ++g_buffer_calls[16];
    Guard guard;
    if (!(buffer->flags & kCapsCtrlPan) && buffer != &g_primary) {
        return reject(kControlUnavailable);
    }
    if (pan < kPanLeft || pan > kPanRight) {
        return reject(kInvalidParam);
    }
    buffer->pan = pan;
    if (buffer->voice != 0) {
        const OtHalVoiceParams params = params_of(*buffer);
        g_audio->voice_update(buffer->voice, &params);
    }
    return kOk;
}

HRESULT __stdcall buffer_set_frequency(Buffer *buffer, DWORD frequency) {
    ++g_buffer_calls[17];
    Guard guard;
    if (!(buffer->flags & kCapsCtrlFrequency)) {
        return reject(kControlUnavailable);
    }
    if (frequency != 0 && (frequency < kFrequencyMin || frequency > kFrequencyMax)) {
        return reject(kInvalidParam);
    }
    buffer->frequency = frequency;
    if (buffer->voice != 0) {
        const OtHalVoiceParams params = params_of(*buffer);
        g_audio->voice_update(buffer->voice, &params);
    }
    return kOk;
}

// DirectSound keeps the play cursor where Stop left it; Play resumes there.
HRESULT __stdcall buffer_stop(Buffer *buffer) {
    ++g_buffer_calls[18];
    Guard guard;
    if (buffer->shared != 0) {
        settle(*buffer);
        if (buffer->voice != 0) {
            g_audio->voice_pause(buffer->voice, 1);
        }
    }
    buffer->playing = false;
    return kOk;
}

HRESULT __stdcall buffer_unlock(Buffer *buffer, void *first, DWORD first_bytes, void *second, DWORD second_bytes) {
    ++g_buffer_calls[19];
    Guard guard;
    Shared *shared = buffer->shared;
    if (shared == 0) {
        return reject(kInvalidCall);
    }
    const unsigned char *start = shared->data;
    const unsigned char *end = shared->data + shared->size;
    const unsigned char *parts[2] = {static_cast<unsigned char *>(first), static_cast<unsigned char *>(second)};
    const DWORD sizes[2] = {first_bytes, second_bytes};
    for (int index = 0; index < 2; ++index) {
        if (parts[index] != 0 && sizes[index] != 0) {
            if (parts[index] < start || parts[index] + sizes[index] > end) {
                return reject(kInvalidParam);
            }
            g_audio->sound_write(shared->sound, static_cast<uint32_t>(parts[index] - start), parts[index], sizes[index]);
        }
    }
    return kOk;
}

HRESULT __stdcall buffer_restore(Buffer *) {
    ++g_buffer_calls[20];
    return kOk; // never lost
}

#define OT_SLOT(function) reinterpret_cast<void *>(&function)

void *const g_buffer_vtable[kBufferSlots] = {
    OT_SLOT(buffer_query_interface),       // 0
    OT_SLOT(buffer_add_ref),               // 1
    OT_SLOT(buffer_release),               // 2
    OT_SLOT(buffer_get_caps),              // 3
    OT_SLOT(buffer_get_current_position),  // 4
    OT_SLOT(buffer_get_format),            // 5
    OT_SLOT(buffer_get_volume),            // 6
    OT_SLOT(buffer_get_pan),               // 7
    OT_SLOT(buffer_get_frequency),         // 8
    OT_SLOT(buffer_get_status),            // 9
    OT_SLOT(buffer_initialize),            // 10
    OT_SLOT(buffer_lock),                  // 11
    OT_SLOT(buffer_play),                  // 12
    OT_SLOT(buffer_set_current_position),  // 13
    OT_SLOT(buffer_set_format),            // 14
    OT_SLOT(buffer_set_volume),            // 15
    OT_SLOT(buffer_set_pan),               // 16
    OT_SLOT(buffer_set_frequency),         // 17
    OT_SLOT(buffer_stop),                  // 18
    OT_SLOT(buffer_unlock),                // 19
    OT_SLOT(buffer_restore),               // 20
};

// ------------------------------------------------------- stand-in IDirectSound

Buffer *new_buffer(Shared *shared, DWORD flags) {
    Buffer *buffer = static_cast<Buffer *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Buffer)));
    if (buffer == 0) {
        return 0;
    }
    buffer->vtable = g_buffer_vtable;
    buffer->references = 1;
    buffer->shared = shared;
    buffer->flags = flags;
    ++g_buffers_created;
    ++g_buffers_live;
    return buffer;
}

HRESULT __stdcall device_query_interface(Device *device, const GUID *iid, void **out) {
    ++g_device_calls[0];
    if (out == 0) {
        return reject(kInvalidParam);
    }
    if (same_guid(iid, kIidUnknown) || same_guid(iid, kIidDirectSound)) {
        InterlockedIncrement(&g_device_references);
        *out = device;
        return kOk;
    }
    *out = 0;
    return reject(kNoInterface);
}

ULONG __stdcall device_add_ref(Device *) {
    ++g_device_calls[1];
    return static_cast<ULONG>(InterlockedIncrement(&g_device_references));
}

ULONG __stdcall device_release(Device *) {
    ++g_device_calls[2];
    const LONG left = InterlockedDecrement(&g_device_references);
    return static_cast<ULONG>(left > 0 ? left : 0); // the backend stops with the HAL
}

HRESULT __stdcall device_create_sound_buffer(Device *, const BufferDesc *desc, Buffer **out, void *outer) {
    ++g_device_calls[3];
    Guard guard;
    if (desc == 0 || out == 0 || outer != 0 || desc->size < 0x14) {
        return reject(kInvalidParam);
    }
    *out = 0;
    if (desc->flags & kCapsPrimary) {
        if (desc->bytes != 0 || desc->format != 0) {
            return reject(kInvalidParam);
        }
        g_primary.flags = desc->flags;
        InterlockedIncrement(&g_primary.references);
        *out = &g_primary;
        return kOk;
    }
    const WaveFormat *format = desc->format;
    if (format == 0 || desc->bytes < 4 || desc->bytes > 0x0fffffff) {
        return reject(kInvalidParam);
    }
    if (format->tag != 1 || (format->channels != 1 && format->channels != 2) ||
        (format->bits != 8 && format->bits != 16) || format->rate < kFrequencyMin || format->rate > kFrequencyMax) {
        return reject(kBadFormat);
    }
    Shared *shared = static_cast<Shared *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Shared)));
    unsigned char *data = static_cast<unsigned char *>(HeapAlloc(GetProcessHeap(), 0, desc->bytes));
    if (shared == 0 || data == 0) {
        HeapFree(GetProcessHeap(), 0, shared);
        HeapFree(GetProcessHeap(), 0, data);
        return reject(kOutOfMemory);
    }
    memset(data, format->bits == 8 ? 0x80 : 0, desc->bytes);
    shared->references = 1;
    shared->data = data;
    shared->size = desc->bytes;
    shared->format = *format;
    shared->format.extra = 0;
    OtHalSoundDesc sound;
    sound.format.sample_rate = format->rate;
    sound.format.channels = format->channels;
    sound.format.bits_per_sample = format->bits;
    sound.data = data;
    sound.size = desc->bytes;
    shared->sound = g_audio->sound_create(&sound);
    Buffer *buffer = shared->sound != 0 ? new_buffer(shared, desc->flags) : 0;
    if (buffer == 0) {
        if (shared->sound != 0) {
            g_audio->sound_destroy(shared->sound);
        }
        HeapFree(GetProcessHeap(), 0, data);
        HeapFree(GetProcessHeap(), 0, shared);
        return reject(kOutOfMemory);
    }
    *out = buffer;
    return kOk;
}

HRESULT __stdcall device_get_caps(Device *, DWORD *caps) {
    ++g_device_calls[4];
    if (caps == 0 || caps[0] < 0x60) {
        return reject(kInvalidParam);
    }
    // DSCAPS: a software mixer, continuous rates, no hardware buffers.
    // DSCAPS_PRIMARYMONO|STEREO|8BIT|16BIT, CONTINUOUSRATE, SECONDARYMONO|STEREO|8BIT|16BIT.
    memset(caps + 1, 0, 0x60 - sizeof(DWORD));
    caps[1] = 0x00000f1fu;
    caps[2] = kFrequencyMin;
    caps[3] = kFrequencyMax;
    caps[4] = 1; // primary buffers
    return kOk;
}

HRESULT __stdcall device_duplicate_sound_buffer(Device *, Buffer *source, Buffer **out) {
    ++g_device_calls[5];
    Guard guard;
    if (source == 0 || out == 0) {
        return reject(kInvalidParam);
    }
    *out = 0;
    if (source->shared == 0) {
        return reject(kInvalidCall); // the primary cannot be duplicated
    }
    Buffer *buffer = new_buffer(source->shared, source->flags);
    if (buffer == 0) {
        return reject(kOutOfMemory);
    }
    ++source->shared->references;
    buffer->volume = source->volume;
    buffer->pan = source->pan;
    buffer->frequency = source->frequency;
    ++g_duplicates;
    *out = buffer;
    return kOk;
}

HRESULT __stdcall device_set_cooperative_level(Device *, HWND, DWORD) {
    ++g_device_calls[6];
    return kOk;
}

HRESULT __stdcall device_compact(Device *) {
    ++g_device_calls[7];
    return kOk;
}

HRESULT __stdcall device_get_speaker_config(Device *, DWORD *config) {
    ++g_device_calls[8];
    if (config == 0) {
        return reject(kInvalidParam);
    }
    *config = 4; // DSSPEAKER_STEREO
    return kOk;
}

HRESULT __stdcall device_set_speaker_config(Device *, DWORD) {
    ++g_device_calls[9];
    return kOk;
}

HRESULT __stdcall device_initialize(Device *, const GUID *) {
    ++g_device_calls[10];
    return reject(kAlreadyInitialized);
}

void *const g_device_vtable[kDeviceSlots] = {
    OT_SLOT(device_query_interface),        // 0
    OT_SLOT(device_add_ref),                // 1
    OT_SLOT(device_release),                // 2
    OT_SLOT(device_create_sound_buffer),    // 3
    OT_SLOT(device_get_caps),               // 4
    OT_SLOT(device_duplicate_sound_buffer), // 5
    OT_SLOT(device_set_cooperative_level),  // 6
    OT_SLOT(device_compact),                // 7
    OT_SLOT(device_get_speaker_config),     // 8
    OT_SLOT(device_set_speaker_config),     // 9
    OT_SLOT(device_initialize),             // 10
};

#undef OT_SLOT

// ---------------------------------------------------------------- adapters

// Retail: DirectSoundCreate into AudioDevice, SetCooperativeLevel(window,
// DSSCL_EXCLUSIVE), GetCaps into AudioCaps, then the sound tables cleared.
// Here AudioDevice is the stand-in; the rest is as retail.
void __cdecl hal_Audio_Init(HWND window) {
    g_device_references = 1;
    global<Device *>(OT_HAL_SYM_AudioDevice) = &g_device;
    device_set_cooperative_level(&g_device, window, 3);
    DWORD *caps = &global<DWORD>(OT_HAL_SYM_AudioCaps);
    memset(caps, 0, 0x60);
    caps[0] = 0x60;
    device_get_caps(&g_device, caps);
    memset(&global<DWORD>(OT_HAL_SYM_AudioSoundIdMap), 0, 400 * sizeof(DWORD));
    memset(&global<DWORD>(OT_HAL_SYM_AudioBankRecords), 0, 0x500 * sizeof(DWORD));
}

} // namespace

void ot_audio_hal_start(const OtHalAudioBackend *audio) {
    InitializeCriticalSection(&g_lock);
    g_audio = audio;
}

void ot_audio_hal_stop(void) {
    if (g_audio != 0) {
        Guard guard;
        g_audio->shutdown();
        g_audio = 0;
    }
}

int ot_audio_hal_describe(char *out, int size) {
    char line[512];
    int length = wsprintfA(line, "audio_hal buffers=%u live=%u duplicates=%u plays=%u rejected=%u device_calls",
        static_cast<unsigned>(g_buffers_created), static_cast<unsigned>(g_buffers_live),
        static_cast<unsigned>(g_duplicates), static_cast<unsigned>(g_plays), static_cast<unsigned>(g_rejected));
    for (DWORD slot = 0; slot < kDeviceSlots; ++slot) {
        if (g_device_calls[slot] != 0) {
            length += wsprintfA(line + length, " %u:%u", static_cast<unsigned>(slot), static_cast<unsigned>(g_device_calls[slot]));
        }
    }
    length += wsprintfA(line + length, " buffer_calls");
    for (DWORD slot = 0; slot < kBufferSlots && length < static_cast<int>(sizeof(line)) - 16; ++slot) {
        if (g_buffer_calls[slot] != 0) {
            length += wsprintfA(line + length, " %u:%u", static_cast<unsigned>(slot), static_cast<unsigned>(g_buffer_calls[slot]));
        }
    }
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}

OT_REGISTER_HAL_ADAPTER(Audio_Init, hal_Audio_Init)
