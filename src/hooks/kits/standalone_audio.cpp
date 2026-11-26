// The sound of a standalone program (milestone 8a): the retail sound front's voices on the HAL audio service (src/hal/audio.h).
//
// Retail keeps every sound effect in its own DirectSound buffer (pc\soundlib.cpp) and a "voice" is (duplicate << 16) | slot: a slot is one buffer plus up to seven
// duplicates of it (DuplicateSoundBuffer shares the samples), 128 slots of 0x28 bytes at 0x029d6920 (eight buffer pointers, +0x20 flags, +0x24 the sound's record), and
// the sounds' own records come from the VAB tables (0x42 entries of 0x34 bytes, 19 banks from 0x0054ca50; +0 the sound id, +4 the pitch scale as a float, +8 the pitch,
// +0xc the name, +0x2c a byte: looping, +0x30 the slot). This file is those functions, transcribed from the disassembly (Audio_Init 0x004f27c0, Audio_LoadSoundBank
// 0x004f2960 with Audio_CreateBufferFromWav 0x004f2b40, Audio_FreeSoundBank 0x004f2ad0 with Audio_FreeSound 0x004f2da0, Audio_PlaySound 0x004f2e20, Audio_StartVoice
// 0x004f3200, Audio_SetVoiceVolume 0x004f34b0, Audio_SetVoiceFrequency 0x004f3690, Audio_GetVoiceFrequency 0x004f3720, Audio_StopSound 0x004f37c0, Audio_HaltSound
// 0x004f3870, Audio_PauseLooping 0x004f3900, Audio_ResumeLooping 0x004f3940), with the buffers on the HAL: a buffer is a sound, a cursor, the settings DirectSound keeps
// (volume, pan, frequency) and, while it plays, a HAL voice. The numbers the retail code computes (millibels from the PlayStation volumes, the pan from their ratio, the
// frequency from the pitch) are computed the same way, at the x87 precision the game has set (D3D leaves it at 24 bits; the program's own default is 53).
//
// Kept exactly, quirks included: Audio_ResumeLooping calls Audio_StartVoice with both volumes -1, which clamps them to 0, and zero volumes stop the voice, so "resume" stops
// every looping slot (the game starts its looping sounds again itself); Audio_SetVoiceVolume on a slot whose looping flag is clear first starts the slot's first buffer (the
// original, not the duplicate) at that volume.
//
// What is the host's, not retail's: whether a buffer exists is the host table, not the pointer in memory (a session image carries pointers into a heap this program does not
// have; those slots are silent). Pointers written to memory are tokens (non-zero, unique per buffer). OT_AUDIO_LOG=1 lists every call on stderr.
#include "kit_bridge.hpp"
#include "standalone_options.hpp"

#include "audio.h"
#include "wav_asset.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>
#include "guest_arena.hpp"

namespace opentony::kitdll {
namespace {

using Args = std::vector<std::uint32_t>;

template <typename T>
volatile T& retail(std::uint32_t address) {
    return *arena::host<volatile T>(address);
}

constexpr std::uint32_t kAudioInit = 0x004f27c0;
constexpr std::uint32_t kLoadSoundBank = 0x004f2960;
constexpr std::uint32_t kFreeSoundBank = 0x004f2ad0;
constexpr std::uint32_t kPlaySound = 0x004f2e20;
constexpr std::uint32_t kSetVoiceVolume = 0x004f34b0;
constexpr std::uint32_t kSetVoiceFrequency = 0x004f3690;
constexpr std::uint32_t kGetVoiceFrequency = 0x004f3720;
constexpr std::uint32_t kStopSound = 0x004f37c0;
constexpr std::uint32_t kPauseLooping = 0x004f3900;
constexpr std::uint32_t kResumeLooping = 0x004f3940;

constexpr std::uint32_t kSlots = 0x029d6920;      // 128 slots of 0x28 bytes
constexpr std::uint32_t kSlotSize = 0x28;
constexpr std::uint32_t kSlotCount = 128;
constexpr std::uint32_t kIdMap = 0x029d7d20;      // 400 pointers to the sounds' records, by sound id
constexpr std::uint32_t kIdCount = 400;
constexpr std::uint32_t kDevice = 0x029d8360;     // AudioDevice: non-zero when the sound is up
constexpr std::uint32_t kBanks = 0x0054ca50;      // 19 banks of 0x42 entries of 0x34 bytes (the loops address an entry's +0x30 field at 0x0054ca80)
constexpr std::uint32_t kBankSize = 0xd68;
constexpr std::uint32_t kEntrySize = 0x34;
constexpr std::uint32_t kEntryCount = 0x42;
constexpr std::uint32_t kNoName = 0x0055fe10;     // the name of an entry that has no sound
constexpr std::uint32_t kPathPrefix = 0x0055bbc8; // "audio/"
constexpr std::uint32_t kPathSuffix = 0x0055bbc0; // ".wav"
constexpr std::uint32_t kControlMode = 0x006a6964; // non-zero: the windowed set-up, a sound plays on its first buffer and no duplicates are made
constexpr std::uint32_t kFreeSoundSlot = 0x029d691c;   // the "slot -1" the failed loads write to
constexpr std::uint32_t kConstFrequencyBase = 0x00519a58;   // 44100.0
constexpr std::uint32_t kConstFrequencyScale = 0x00519a5c;  // 1 / 60
constexpr std::uint32_t kConstPanRight = 0x00519a60;        // 10000.0
constexpr std::uint32_t kConstPanLeft = 0x00519a64;         // -10000.0
constexpr std::uint32_t kConstVolumeCeiling = 0x00519a68;   // 2857.1428
constexpr std::uint32_t kConstVolumeScale = 0x00519a6c;     // 0.4
constexpr std::uint32_t kConstFrequencyBack = 0x00519a70;   // 1 / 44100
constexpr std::uint32_t kConstSixty = 0x0051998c;           // 60.0
constexpr std::uint32_t kBackendOpen = 0x004e75b0, kBackendSeek = 0x004e79f0, kBackendRead = 0x004e7ad0, kBackendClose = 0x004e7bc0;
constexpr std::uint32_t kFrameCounter = 0x0056a8d4;   // once a frame (core_main.cpp)
constexpr std::uint32_t kAnimationClock = 0x005685f4; // the game's tick, set from the accumulator
constexpr std::uint32_t kMaxFrequency = 0x15888;
constexpr std::int32_t kVolumeFloor = 0xb29;       // 2857: the volume is the scaled level minus this, in millibels

bool log_calls() {
    static const bool on = std::getenv("OT_AUDIO_LOG") != nullptr;
    return on;
}

// ---- a HAL audio service that makes no sound -------------------------------------------------------------------------------------
// For a program with no audio device: the same bookkeeping, time from the program's clock, so a one-shot ends and a duplicate is free again.
struct SilentSound {
    OtHalPcmFormat format{};
    std::uint32_t size = 0;
};
struct SilentVoice {
    OtHalHandle sound = 0;
    OtHalVoiceParams params{};
    std::uint64_t at = 0;            // clock when the voice started or last moved
    std::uint32_t position = 0;
    bool paused = false;
    bool ended = false;
};
std::vector<SilentSound> g_silent_sounds(1);
std::vector<SilentVoice> g_silent_voices(1);

std::uint32_t silent_rate(const SilentVoice& voice) {
    const SilentSound& sound = g_silent_sounds[voice.sound];
    const std::uint32_t rate = voice.params.frequency != 0 ? voice.params.frequency : sound.format.sample_rate;
    return rate * sound.format.channels * (sound.format.bits_per_sample / 8u);
}
void silent_settle(SilentVoice& voice) {
    if (voice.ended || voice.paused || voice.sound == 0) {
        return;
    }
    const std::uint64_t now = standalone_clock_micros();
    const std::uint64_t moved = (now - voice.at) * silent_rate(voice) / 1000000u;
    const std::uint32_t size = g_silent_sounds[voice.sound].size;
    if ((voice.params.flags & OT_HAL_VOICE_LOOP) != 0 && size != 0) {
        voice.position = static_cast<std::uint32_t>((voice.position + moved) % size);
    } else if (voice.position + moved >= size) {
        voice.position = size;
        voice.ended = true;
    } else {
        voice.position += static_cast<std::uint32_t>(moved);
    }
    voice.at = now;
}
int OT_HAL_CALL silent_init() { return 1; }
void OT_HAL_CALL silent_shutdown() {}
OtHalHandle OT_HAL_CALL silent_sound_create(const OtHalSoundDesc* desc) {
    g_silent_sounds.push_back({desc->format, desc->size});
    return static_cast<OtHalHandle>(g_silent_sounds.size() - 1);
}
void OT_HAL_CALL silent_sound_write(OtHalHandle, std::uint32_t, const void*, std::uint32_t) {}
void OT_HAL_CALL silent_sound_destroy(OtHalHandle) {}
OtHalHandle OT_HAL_CALL silent_voice_play(OtHalHandle sound, const OtHalVoiceParams* params, std::uint32_t position) {
    SilentVoice voice;
    voice.sound = sound;
    voice.params = *params;
    voice.position = position;
    voice.at = standalone_clock_micros();
    g_silent_voices.push_back(voice);
    return static_cast<OtHalHandle>(g_silent_voices.size() - 1);
}
void OT_HAL_CALL silent_voice_update(OtHalHandle handle, const OtHalVoiceParams* params) {
    silent_settle(g_silent_voices[handle]);
    g_silent_voices[handle].params = *params;
}
void OT_HAL_CALL silent_voice_stop(OtHalHandle handle) {
    g_silent_voices[handle].ended = true;
    g_silent_voices[handle].sound = 0;
}
void OT_HAL_CALL silent_voice_pause(OtHalHandle handle, int paused) {
    silent_settle(g_silent_voices[handle]);
    g_silent_voices[handle].paused = paused != 0;
    g_silent_voices[handle].at = standalone_clock_micros();
}
int OT_HAL_CALL silent_voice_playing(OtHalHandle handle) {
    silent_settle(g_silent_voices[handle]);
    return g_silent_voices[handle].sound != 0 && !g_silent_voices[handle].ended ? 1 : 0;
}
std::uint32_t OT_HAL_CALL silent_voice_position(OtHalHandle handle) {
    silent_settle(g_silent_voices[handle]);
    return g_silent_voices[handle].position;
}
void OT_HAL_CALL silent_voice_set_position(OtHalHandle handle, std::uint32_t position) {
    g_silent_voices[handle].position = position;
    g_silent_voices[handle].at = standalone_clock_micros();
}
OtHalHandle OT_HAL_CALL silent_stream_open(const OtHalPcmFormat*) { return 0; }
std::uint32_t OT_HAL_CALL silent_stream_write(OtHalHandle, const void*, std::uint32_t size) { return size; }
std::uint32_t OT_HAL_CALL silent_stream_queued(OtHalHandle) { return 0; }
void OT_HAL_CALL silent_stream_set_volume(OtHalHandle, std::int32_t) {}
void OT_HAL_CALL silent_stream_pause(OtHalHandle, int) {}
void OT_HAL_CALL silent_stream_close(OtHalHandle) {}

const OtHalAudioBackend kSilent = {
    "silent",           silent_init,         silent_shutdown,         silent_sound_create,       silent_sound_write,      silent_sound_destroy,
    silent_voice_play,  silent_voice_update, silent_voice_stop,       silent_voice_pause,        silent_voice_playing,    silent_voice_position,
    silent_voice_set_position, silent_stream_open, silent_stream_write, silent_stream_queued, silent_stream_set_volume, silent_stream_pause,
    silent_stream_close,
};

const OtHalAudioBackend* g_audio = &kSilent;

// ---- DirectSound's buffers on the HAL --------------------------------------------------------------------------------------------
struct Buffer {
    bool exists = false;
    OtHalHandle voice = 0;           // the HAL voice while it plays
    std::int32_t volume_mb = 0;
    std::int32_t pan_mb = 0;
    std::uint32_t frequency = 0;     // 0: the sound's own rate
    std::uint32_t cursor = 0;        // bytes: where a Play begins
    bool looping = false;
};
struct Slot {
    OtHalHandle sound = 0;
    std::uint32_t rate = 0;
    Buffer buffers[8];
};
Slot g_slots[kSlotCount];

std::uint32_t slot_address(std::uint32_t slot) { return kSlots + kSlotSize * slot; }
std::uint32_t token(std::uint32_t slot, std::uint32_t dup) { return 0x80000000u | (slot << 8) | dup; }

OtHalVoiceParams params_of(const Slot& slot, const Buffer& buffer) {
    OtHalVoiceParams params{};
    params.volume_mb = buffer.volume_mb;
    params.pan_mb = buffer.pan_mb;
    params.frequency = buffer.frequency;
    params.flags = buffer.looping ? OT_HAL_VOICE_LOOP : 0u;
    (void)slot;
    return params;
}

// IDirectSoundBuffer::GetStatus & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING): a voice that played to its end is gone and its buffer is back at the start
bool busy(Buffer& buffer) {
    if (buffer.voice == 0) {
        return false;
    }
    if (g_audio->voice_playing(buffer.voice) != 0) {
        return true;
    }
    g_audio->voice_stop(buffer.voice);
    buffer.voice = 0;
    buffer.cursor = 0;
    return false;
}

void hal_log(const char* what, std::uint32_t handle, const OtHalVoiceParams* params = nullptr, std::uint32_t position = 0) {
    if (!log_calls()) {
        return;
    }
    if (params != nullptr) {
        std::fprintf(stderr, "audio: hal %s %u volume %d pan %d frequency %u flags %u at %u\n", what, static_cast<unsigned>(handle), static_cast<int>(params->volume_mb),
                     static_cast<int>(params->pan_mb), static_cast<unsigned>(params->frequency), static_cast<unsigned>(params->flags), static_cast<unsigned>(position));
    } else {
        std::fprintf(stderr, "audio: hal %s %u\n", what, static_cast<unsigned>(handle));
    }
    std::fflush(stderr);
}

void play(Slot& slot, Buffer& buffer, bool loop) {
    if (busy(buffer)) {
        buffer.looping = loop;
        const OtHalVoiceParams params = params_of(slot, buffer);
        g_audio->voice_update(buffer.voice, &params);
        hal_log("voice_update", buffer.voice, &params);
        return;
    }
    buffer.looping = loop;
    const OtHalVoiceParams params = params_of(slot, buffer);
    buffer.voice = g_audio->voice_play(slot.sound, &params, buffer.cursor);
    hal_log("voice_play sound", slot.sound, &params, buffer.cursor);
}

// Stop: the cursor stays where it was
void stop(Buffer& buffer) {
    if (buffer.voice != 0) {
        if (g_audio->voice_playing(buffer.voice) != 0) {
            buffer.cursor = g_audio->voice_position(buffer.voice);
        } else {
            buffer.cursor = 0;
        }
        hal_log("voice_stop", buffer.voice);
        g_audio->voice_stop(buffer.voice);
        buffer.voice = 0;
    }
}

void apply(const Slot& slot, Buffer& buffer) {
    if (buffer.voice != 0) {
        const OtHalVoiceParams params = params_of(slot, buffer);
        g_audio->voice_update(buffer.voice, &params);
        hal_log("voice_update", buffer.voice, &params);
    }
}

bool device_up() { return retail<std::uint32_t>(kDevice) != 0; }

// ---- vehicles that share a sound ----------------------------------------------------------------------------------------------------
// Every car (and every moving platform, script object ...) with the same looping sound moves the same voice: Audio_PlaySound makes a sound's slot play on its first buffer, so the
// emitters share one voice. Each calls Audio_SetVoiceVolume with its own distance every few frames. A car out of range asks for silence, and silence stops the voice (the flag that
// says "looping and playing" is cleared, the position goes back to the start); the next car, in range, sees the flag clear and starts the voice again from its first sample. With two
// cars the voice stops and starts every frame: a rapidly repeating stutter (NYC, Philadelphia; PARTYMOD's README: "vehicle sounds constantly resetting").
// OT_VEHICLE_SOUND_FIX=1 (the launcher's default) arbitrates the shared voice, PARTYMOD's way (sfx.c: only the nearest emitter moves the sound), without knowing the emitters: the
// loudest call of a tick is the nearest emitter's (the levels come from the distance), so it alone is applied, and a silent call on a playing voice only mutes it and is carried out
// when a whole tick went by with nobody in range. One-shots and a run without the variable keep retail's behaviour.
#ifdef _WIN32
struct SharedVoice {
    std::uint64_t stamp = ~0ull;   // the tick of the calls below
    std::int32_t best = -1;        // the loudest level asked for in that tick
    bool pending = false;          // the voice is muted and waits for a tick in which nobody wanted it
};
SharedVoice g_shared[kSlotCount];

#else
// Resolve the competing requests before publishing gain and pan to the
// asynchronous mixer so it cannot render an intermediate emitter's settings.
struct LoopVolume {
    std::uint32_t duplicate = 0;
    std::int32_t left = 0;
    std::int32_t right = 0;
    std::int32_t level = -1;
};
LoopVolume g_loop_volumes[kSlotCount];
#endif

bool vehicle_sound_fix() {
    static const bool on = [] {
        const char* value = std::getenv("OT_VEHICLE_SOUND_FIX");
        return value != nullptr && value[0] == '1';
    }();
    return on;
}

#ifdef _WIN32
// the frame counter and the animation clock: constant while the objects of one tick update, different in the next
std::uint64_t tick_stamp() { return (static_cast<std::uint64_t>(retail<std::uint32_t>(kFrameCounter)) << 32) | retail<std::uint32_t>(kAnimationClock); }
#endif

// ---- the arithmetic of the retail code -------------------------------------------------------------------------------------------
// `_ftol` (VC6): truncation to a 64-bit integer, the low dword returned
std::int32_t ftol(long double value) {
    if (!(value > -9223372036854775808.0L && value < 9223372036854775808.0L)) {
        return 0;
    }
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(value))));
}
// the result of an x87 operation at the precision control the game has set (24, 53 or 64 bits)
long double rounded(long double value) {
    switch (standalone_precision()) {
    case 0:
        return static_cast<float>(value);
    case 2:
        return static_cast<double>(value);
    default:
        return value;
    }
}
float constant(std::uint32_t address) { return retail<float>(address); }

// the record of a slot's sound (+0x24)
std::uint32_t record_of(std::uint32_t slot) { return retail<std::uint32_t>(slot_address(slot) + 0x24); }
std::uint32_t flags_of(std::uint32_t slot) { return retail<std::uint32_t>(slot_address(slot) + 0x20); }
void set_flags(std::uint32_t slot, std::uint32_t value) { retail<std::uint32_t>(slot_address(slot) + 0x20) = value; }

// the volume of a pair of PlayStation levels in millibels: the louder one times the sound's scale times 0.4, at most 2857.14, minus 2857 (0x004f2fc9..0x004f3012)
std::int32_t volume_mb(std::uint32_t record, std::int32_t left, std::int32_t right) {
    const std::int32_t loudest = (std::max)(left, right);
    long double level = rounded(static_cast<long double>(loudest) * constant(record + 4));
    level = rounded(level * constant(kConstVolumeScale));
    float stored = static_cast<float>(level);           // fst dword
    if (!(stored <= constant(kConstVolumeCeiling))) {
        stored = constant(kConstVolumeCeiling);          // 0x45329249
    }
    return ftol(stored) - kVolumeFloor;
}
// the pan: the quieter side over the louder, scaled to +-10000 (0x004f30b7..0x004f30f1)
std::int32_t pan_mb(std::uint32_t record, std::int32_t left, std::int32_t right) {
#ifndef _WIN32
    if (vehicle_sound_fix() && retail<std::uint8_t>(record + 0x2c) != 0 && left != right) {
        // The HAL expects attenuation in millibels. Preserve the requested
        // channel ratio, with no discontinuity as the levels cross.
        const auto quieter = (std::min)(left, right);
        const auto louder = (std::max)(left, right);
        const auto attenuation = quieter == 0 ? 10000 : (std::min)(
            10000, static_cast<int>(-2000.0 * std::log10(static_cast<double>(quieter) / louder)));
        return left > right ? -attenuation : attenuation;
    }
#else
    (void)record;
#endif
    if (right < left) {
        return ftol(rounded(rounded(static_cast<long double>(right) / left) * constant(kConstPanLeft)));
    }
    if (right > left) {
        return ftol(rounded(rounded(static_cast<long double>(left) / right) * constant(kConstPanRight)));
    }
    return 0;
}
// the frequency from the record's pitch (0x004f3131..0x004f314c): pitch / 60 * 44100, at most 88200
std::uint32_t frequency_of(std::uint32_t record) {
    long double value = rounded(static_cast<long double>(static_cast<std::int32_t>(retail<std::uint32_t>(record + 8))) * constant(kConstFrequencyScale));
    value = rounded(value * constant(kConstFrequencyBase));
    const auto frequency = static_cast<std::uint32_t>(ftol(value));
    return frequency > kMaxFrequency ? kMaxFrequency : frequency;
}

void report(const char* what, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0) {
    if (log_calls()) {
        std::fprintf(stderr, "audio: %s(%u, %u, %u)\n", what, static_cast<unsigned>(a), static_cast<unsigned>(b), static_cast<unsigned>(c));
        std::fflush(stderr);
    }
}

// ---- the functions ---------------------------------------------------------------------------------------------------------------
// 0x004f37c0
void stop_sound(std::uint32_t voice) {
    if (!device_up()) {
        return;
    }
    const std::uint32_t slot = voice & 0xffff;
    if (slot >= kSlotCount) {
        return;
    }
#ifdef _WIN32
    g_shared[slot] = SharedVoice{};
#else
    g_loop_volumes[slot] = LoopVolume{};
#endif
    for (Buffer& buffer : g_slots[slot].buffers) {
        if (buffer.exists) {
            stop(buffer);
            buffer.cursor = 0;
            if (log_calls()) report("  stop", slot);
        }
    }
    set_flags(slot, flags_of(slot) & ~1u);
}

// 0x004f3870: Stop on every buffer of the slot, the flag stays
void halt_sound(std::uint32_t slot) {
    if (!device_up() || slot >= kSlotCount) {
        return;
    }
#ifndef _WIN32
    g_loop_volumes[slot] = LoopVolume{};
#endif
    for (Buffer& buffer : g_slots[slot].buffers) {
        if (buffer.exists) {
            stop(buffer);
        }
    }
}

// the part of Audio_StartVoice and Audio_PlaySound after the voice has its buffer: volume, pan, frequency, Play (0x004f2fc9..0x004f31f8). False when the volume was refused.
bool start_buffer(std::uint32_t slot, std::uint32_t dup, std::int32_t left, std::int32_t right) {
    Slot& host = g_slots[slot];
    Buffer& buffer = host.buffers[dup];
    const std::uint32_t record = record_of(slot);
    if (!buffer.exists || record == 0) {
        return false;
    }
    const std::int32_t volume = volume_mb(record, left, right);
    if (volume < OT_HAL_VOLUME_SILENT || volume > 0) {   // DSERR_INVALIDPARAM: the game logs it and goes on
        std::fprintf(stderr, "game: SND_SetVolume Err: ID %u Vol %d\n", static_cast<unsigned>(slot), volume);
        return false;
    }
    buffer.volume_mb = volume;
    buffer.pan_mb = pan_mb(record, left, right);
    buffer.frequency = frequency_of(record);
    apply(host, buffer);
    play(host, buffer, (flags_of(slot) & 2) != 0);
    if (retail<std::uint8_t>(record + 0x2c) != 0) {
        set_flags(slot, flags_of(slot) | 1u);
    }
    return true;
}

// 0x004f3200
void start_voice(std::uint32_t voice, std::int32_t left, std::int32_t right) {
    if (!device_up()) {
        return;
    }
    const std::uint32_t slot = voice & 0xffff;
    const std::uint32_t dup = (voice >> 16) & 7;
    if (slot >= kSlotCount) {
        return;
    }
    left = (std::max)(left, 0);
    right = (std::max)(right, 0);
    if (left < 1 && right < 1) {
        stop_sound(slot);
        return;
    }
    start_buffer(slot, dup, left, right);
}

// 0x004f2e20
std::uint32_t play_sound(const Args& args) {
    if (args.size() < 3 || !device_up() || args[0] >= kIdCount) {
        return 0xffffffffu;
    }
    const std::uint32_t record = retail<std::uint32_t>(kIdMap + 4 * args[0]);
    if (record == 0) {
        return 0xffffffffu;
    }
    const std::uint32_t slot = retail<std::uint32_t>(record + 0x30);
    if (slot == 0xffffffffu || slot >= kSlotCount) {
        return 0xffffffffu;
    }
    Slot& host = g_slots[slot];
    if (host.sound == 0 || !host.buffers[0].exists) {
        return 0xffffffffu;   // a slot of a session image: its pointers belong to a heap this program does not have, nothing to play
    }
    std::uint32_t dup = 0;
    std::uint32_t voice = slot;
    if (retail<std::uint32_t>(kControlMode) != 0) {
        stop_sound(slot);
    } else {
        dup = 1;
        bool free = !host.buffers[1].exists;
        while (!free) {
            if (!busy(host.buffers[dup])) {   // a finished duplicate is released and made again
                stop(host.buffers[dup]);
                host.buffers[dup] = Buffer{};
                retail<std::uint32_t>(slot_address(slot) + 4 * dup) = 0;
                free = true;
                break;
            }
            if (++dup >= 8) {
                return 0xffffffffu;
            }
            free = !host.buffers[dup].exists;
        }
        // DuplicateSoundBuffer: the same samples, the original's settings
        Buffer& made = host.buffers[dup];
        made = host.buffers[0];
        made.exists = true;
        made.voice = 0;
        made.cursor = 0;
        retail<std::uint32_t>(slot_address(slot) + 4 * dup) = token(slot, dup);
        voice = (dup << 16) | slot;
    }
    const std::int32_t left = (std::max)(static_cast<std::int32_t>(args[1]), 0);
    const std::int32_t right = (std::max)(static_cast<std::int32_t>(args[2]), 0);
    if (left < 1 && right < 1) {
        stop_sound(voice);
        return voice;
    }
    report("PlaySound", args[0], args[1], args[2]);
    start_buffer(slot, dup, left, right);
    return voice;
}

#ifdef _WIN32
// True when the call must not reach the retail code (see "vehicles that share a sound"): a quieter call than the tick's loudest, or a silent one on a voice that is playing
bool shared_voice_skips(std::uint32_t slot, std::uint32_t dup, std::int32_t left, std::int32_t right) {
    const std::uint32_t record = record_of(slot);
    if (record == 0 || retail<std::uint8_t>(record + 0x2c) == 0) {
        return false;   // a one-shot: nothing to keep playing
    }
    SharedVoice& shared = g_shared[slot];
    const std::uint64_t stamp = tick_stamp();
    if (shared.pending && shared.stamp != stamp) {
        stop_sound(slot);   // nobody wanted the voice for a whole tick: the stop it was asked for happens now (the state is cleared with it)
    }
    if (shared.stamp != stamp) {
        shared.stamp = stamp;
        shared.best = -1;
    }
    const std::int32_t level = (std::max)((std::max)(left, 0), (std::max)(right, 0));
    if (level <= shared.best) {
        return true;
    }
    shared.best = level;
    if (level > 0) {
        shared.pending = false;
        return false;
    }
    if ((flags_of(slot) & 1) == 0) {
        return false;   // already stopped: retail's stop changes nothing
    }
    shared.pending = true;
    Buffer& buffer = g_slots[slot].buffers[dup];
    if (buffer.exists) {
        buffer.volume_mb = OT_HAL_VOLUME_SILENT;
        apply(g_slots[slot], buffer);
    }
    return true;
}
#endif

void apply_voice_volume(std::uint32_t slot, std::uint32_t dup, std::int32_t left, std::int32_t right) {
    if ((flags_of(slot) & 1) == 0) {
        start_voice(slot, left, right);
    }
    if (left < 1 && right < 1) {
        stop_sound(slot);
        return;
    }
    Slot& host = g_slots[slot];
    Buffer& buffer = host.buffers[dup];
    const std::uint32_t record = record_of(slot);
    if (!buffer.exists || record == 0) {
        return;
    }
    const std::int32_t volume = volume_mb(record, left, right);
    if (volume < OT_HAL_VOLUME_SILENT || volume > 0) {
        std::fprintf(stderr, "game: SND_SetVolume Err: ID %u Vol %d\n", static_cast<unsigned>(slot), volume);
        return;
    }
    buffer.volume_mb = volume;
    buffer.pan_mb = pan_mb(record, left, right);
    apply(host, buffer);
}

// 0x004f34b0
void set_voice_volume(const Args& args) {
    if (args.size() < 3 || !device_up()) {
        return;
    }
    const std::uint32_t slot = args[0] & 0xffff;
    const std::uint32_t dup = (args[0] >> 16) & 7;
    if (slot >= kSlotCount) {
        return;
    }
    report("SetVoiceVolume", args[0], args[1], args[2]);
    const auto left = (std::max)(static_cast<std::int32_t>(args[1]), 0);
    const auto right = (std::max)(static_cast<std::int32_t>(args[2]), 0);
    if (vehicle_sound_fix()) {
#ifdef _WIN32
        if (shared_voice_skips(slot, dup, left, right)) {
            return;
        }
#else
        const auto record = record_of(slot);
        if (record != 0 && retail<std::uint8_t>(record + 0x2c) != 0) {
            auto& request = g_loop_volumes[slot];
            const auto level = (std::max)(left, right);
            if (level > request.level) {
                request = {dup, left, right, level};
            }
            return;
        }
#endif
    }
    apply_voice_volume(slot, dup, left, right);
}

// 0x004f3690: the frequency comes from the record's pitch, which the sound front stores before it calls
void set_voice_frequency(const Args& args) {
    if (args.empty() || !device_up()) {
        return;
    }
    const std::uint32_t slot = args[0] & 0xffff;
    const std::uint32_t dup = (args[0] >> 16) & 7;
    if (slot >= kSlotCount || !g_slots[slot].buffers[dup].exists || record_of(slot) == 0) {
        return;
    }
    report("SetVoiceFrequency", args[0]);
    g_slots[slot].buffers[dup].frequency = frequency_of(record_of(slot));
    apply(g_slots[slot], g_slots[slot].buffers[dup]);
}

// 0x004f3720: the buffer's frequency (a new buffer reports its sound's rate) as pitch units: frequency / 44100 * 60
std::uint32_t get_voice_frequency(const Args& args) {
    if (args.empty() || !device_up()) {
        return 0;
    }
    const std::uint32_t slot = args[0] & 0xffff;
    const std::uint32_t dup = (args[0] >> 16) & 7;
    if (slot >= kSlotCount || !g_slots[slot].buffers[dup].exists) {
        return 0;
    }
    const Buffer& buffer = g_slots[slot].buffers[dup];
    const std::uint32_t frequency = buffer.frequency != 0 ? buffer.frequency : g_slots[slot].rate;
    long double value = rounded(static_cast<long double>(frequency) * constant(kConstFrequencyBack));
    value = rounded(value * constant(kConstSixty));
    return static_cast<std::uint32_t>(ftol(value));
}

// 0x004f3900 and 0x004f3940
void pause_looping() {
    if (!device_up()) {
        return;
    }
    for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
        if (retail<std::uint32_t>(slot_address(slot)) != 0 && (flags_of(slot) & 1) != 0) {
            halt_sound(slot);
        }
    }
}
void resume_looping() {
    if (!device_up()) {
        return;
    }
    for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
        if ((flags_of(slot) & 1) != 0) {
            start_voice(slot, -1, -1);   // both clamp to 0: the voice stops (see the file comment)
        }
    }
}

// 0x004f2da0
void free_sound(std::uint32_t slot) {
    if (!device_up() || slot >= kSlotCount) {
        return;
    }
    stop_sound(slot);
    for (std::uint32_t dup = 0; dup < 8; ++dup) {
        Buffer& buffer = g_slots[slot].buffers[dup];
        if (buffer.exists) {
            stop(buffer);
            buffer = Buffer{};
            retail<std::uint32_t>(slot_address(slot) + 4 * dup) = 0;
        }
    }
    if (g_slots[slot].sound != 0) {
        g_audio->sound_destroy(g_slots[slot].sound);
        g_slots[slot].sound = 0;
    }
    set_flags(slot, 0);
}

// 0x004f2ad0
void free_sound_bank(const Args& args) {
    if (args.empty() || static_cast<std::int32_t>(args[0]) > 0x12) {
        return;
    }
    std::uint32_t entry = kBanks + args[0] * kBankSize;
    for (std::uint32_t k = 0; k < kEntryCount; ++k, entry += kEntrySize) {
        if (retail<std::int32_t>(entry) == -1) {
            return;
        }
        const std::uint32_t slot = retail<std::uint32_t>(entry + 0x30);
        if (slot != 0xffffffffu) {
            free_sound(slot);
            retail<std::uint32_t>(entry + 0x30) = 0xffffffffu;
            retail<std::uint32_t>(kFreeSoundSlot) = 0;
            const std::uint32_t id = retail<std::uint32_t>(entry);
            if (id < kIdCount) {
                retail<std::uint32_t>(kIdMap + 4 * id) = 0;
            }
        }
    }
}

// the bytes of a file through the program's file layer (the same lookup the game's own reads use)
std::vector<std::uint8_t> read_file(const std::string& path) {
    std::vector<std::uint8_t> bytes;
    std::uint32_t handle = 0;
    // the file layer reads the path as a C string in game memory: the scratch is a block of the guest heap
    const arena::HeapBlock name(static_cast<std::uint32_t>(path.size() + 1));
    std::memcpy(name.data(), path.c_str(), path.size() + 1);
    if (!call_file_seam(kBackendOpen, {name.address(), 0, 0}, std::string(), handle) || handle == 0xffffffffu) {
        return bytes;
    }
    std::uint32_t size = 0;
    std::uint32_t rewound = 0;
    call_file_seam(kBackendSeek, {handle, 0, 2}, std::string(), size);
    call_file_seam(kBackendSeek, {handle, 0, 0}, std::string(), rewound);
    std::uint32_t read = 0;
    if (size != 0) {
        const arena::HeapBlock buffer(size);
        std::memset(buffer.data(), 0, size);   // a short read leaves zeros, as the host vector did
        call_file_seam(kBackendRead, {handle, buffer.address(), size}, std::string(), read);
        bytes.assign(buffer.data(), buffer.data() + size);
    }
    std::uint32_t closed = 0;
    call_file_seam(kBackendClose, {handle}, std::string(), closed);
    return bytes;
}

#ifdef __linux__
// The stock Hangar light loop has a strong 60 Hz hum and a separate band of
// crackle above 2 kHz. Filter across the wrap so the seam is smoothed as well.
std::vector<std::int16_t> filter_hangar_lights(const assets::WavPcmAsset& wav) {
    constexpr int radius = 48;
    constexpr double pi = 3.14159265358979323846;
    constexpr double cutoff_hz = 900.0;
    const auto samples = wav.samples();
    const auto count = samples.size() / 2;
    const double cutoff = cutoff_hz / wav.format().samples_per_second;
    double kernel[2 * radius + 1];
    double total = 0;
    for (int tap = -radius; tap <= radius; ++tap) {
        const double window = 0.5 + 0.5 * std::cos(pi * tap / radius);
        const double sinc = tap == 0 ? 2 * cutoff : std::sin(2 * pi * cutoff * tap) / (pi * tap);
        kernel[tap + radius] = window * sinc;
        total += window * sinc;
    }
    std::vector<std::int16_t> filtered(count);
    for (std::size_t index = 0; index < count; ++index) {
        double value = 0;
        for (std::size_t tap = 0; tap < std::size(kernel); ++tap) {
            const auto at = 2 * ((index + count + tap - radius) % count);
            const auto word = std::to_integer<unsigned>(samples[at]) | (std::to_integer<unsigned>(samples[at + 1]) << 8);
            value += kernel[tap] * static_cast<std::int16_t>(word);
        }
        filtered[index] = static_cast<std::int16_t>(std::clamp(std::lround(value / total), -32768L, 32767L));
    }
    return filtered;
}
#endif

// 0x004f2b40: the first free slot, the sound made from the WAV; the slot, or -1
std::uint32_t create_buffer_from_wav(const std::string& path) {
    std::uint32_t slot = 0;
    if (retail<std::uint32_t>(slot_address(0)) != 0) {
        do {
            ++slot;
        } while (slot < kSlotCount && retail<std::uint32_t>(slot_address(slot)) != 0);
        if (slot > 0x7f) {
            std::fprintf(stderr, "game: ERROR: Too many sound effects (%s)\n", path.c_str());
            return 0xffffffffu;
        }
    }
    if (!device_up()) {
        std::fprintf(stderr, "game: ERROR: Too many sound effects (%s)\n", path.c_str());
        return 0xffffffffu;
    }
    set_flags(slot, 0);
    const std::vector<std::uint8_t> bytes = read_file(path);
    try {
        if (bytes.empty()) {
            throw assets::WavFormatError("no such file");
        }
        std::vector<std::byte> raw(bytes.size());
        std::memcpy(raw.data(), bytes.data(), bytes.size());
        const assets::WavPcmAsset wav = assets::WavPcmAsset::parse(std::move(raw), path);
        OtHalSoundDesc desc{};
        desc.format.sample_rate = wav.format().samples_per_second;
        desc.format.channels = wav.format().channels;
        desc.format.bits_per_sample = wav.format().bits_per_sample;
        desc.data = wav.samples().data();
        desc.size = static_cast<std::uint32_t>(wav.sample_size());
#ifdef __linux__
        std::vector<std::int16_t> filtered;
        if (path == "audio/humlights1.wav" && desc.format.sample_rate == 44100 &&
            desc.format.channels == 1 && desc.format.bits_per_sample == 16 && desc.size >= 2 * 48) {
            filtered = filter_hangar_lights(wav);
            desc.data = filtered.data();
        }
#endif
        Slot& host = g_slots[slot];
        host = Slot{};
        host.sound = g_audio->sound_create(&desc);
        host.rate = desc.format.sample_rate;
        host.buffers[0].exists = true;
        retail<std::uint32_t>(slot_address(slot)) = token(slot, 0);
        report("  sound_create", slot, desc.format.sample_rate, desc.size);
        if (log_calls()) {
            std::fprintf(stderr, "audio: slot %u: %s\n", static_cast<unsigned>(slot), path.c_str());
        }
    } catch (const std::exception& problem) {
        std::fprintf(stderr, "game: ERROR!!!!!! Loading WAV file %s (%s)\n", path.c_str(), problem.what());
        return 0xffffffffu;
    }
    return slot;
}

// 0x004f2960
void load_sound_bank(const Args& args) {
    if (args.empty() || static_cast<std::int32_t>(args[0]) > 0x12) {
        return;
    }
    const auto text = [](std::uint32_t address) { return std::string(arena::host<const char>(address)); };
    std::uint32_t entry = kBanks + args[0] * kBankSize;
    for (std::uint32_t k = 0; k < kEntryCount; ++k, entry += kEntrySize) {
        if (retail<std::int32_t>(entry) == -1) {
            return;
        }
        if (text(entry + 0xc) == text(kNoName)) {
            continue;
        }
        const std::uint32_t slot = create_buffer_from_wav(text(kPathPrefix) + text(entry + 0xc) + text(kPathSuffix));
        retail<std::uint32_t>(entry + 0x30) = slot;
        // a failed load (slot -1) writes below the table, as retail does
        const std::uint32_t at = slot_address(0) + kSlotSize * slot;
        retail<std::uint32_t>(at + 0x24) = entry;
        const std::uint32_t id = retail<std::uint32_t>(entry);
        if (id < kIdCount) {
            retail<std::uint32_t>(kIdMap + 4 * id) = entry;
        }
        if (retail<std::uint8_t>(entry + 0x2c) != 0) {
            retail<std::uint32_t>(at + 0x20) = retail<std::uint32_t>(at + 0x20) | 2u;
        }
    }
}

// 0x004f27c0: the device, and the tables cleared (the sound is up when the HAL's audio is)
void audio_init() {
    if (g_audio->init() == 0) {
        retail<std::uint32_t>(kDevice) = 0;
        return;
    }
    for (std::uint32_t k = 0; k < kIdCount; ++k) {
        retail<std::uint32_t>(kIdMap + 4 * k) = 0;
    }
    for (std::uint32_t k = 0; k < kSlotCount * kSlotSize / 4; ++k) {
        retail<std::uint32_t>(kSlots + 4 * k) = 0;
    }
    for (Slot& slot : g_slots) {
        slot = Slot{};
    }
#ifdef _WIN32
    for (SharedVoice& shared : g_shared) {
        shared = SharedVoice{};
    }
#else
    for (LoopVolume& request : g_loop_volumes) {
        request = LoopVolume{};
    }
#endif
    retail<std::uint32_t>(kDevice) = 0xa0d10001u;
}

}  // namespace

bool has_audio_seam(std::uint32_t address) {
    for (const std::uint32_t known : {kAudioInit, kLoadSoundBank, kFreeSoundBank, kPlaySound, kSetVoiceVolume, kSetVoiceFrequency, kGetVoiceFrequency, kStopSound,
                                      kPauseLooping, kResumeLooping}) {
        if (known == address) {
            return true;
        }
    }
    return false;
}

bool call_audio_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string&, std::uint32_t& result) {
    result = 0;
    switch (address) {
    case kAudioInit:
        audio_init();
        return true;
    case kLoadSoundBank:
        load_sound_bank(args);
        return true;
    case kFreeSoundBank:
        free_sound_bank(args);
        return true;
    case kPlaySound:
        result = play_sound(args);
        return true;
    case kSetVoiceVolume:
        set_voice_volume(args);
        return true;
    case kSetVoiceFrequency:
        set_voice_frequency(args);
        return true;
    case kGetVoiceFrequency:
        result = get_voice_frequency(args);
        return true;
    case kStopSound:
        if (!args.empty()) {
            stop_sound(args[0]);
        }
        return true;
    case kPauseLooping:
        pause_looping();
        return true;
    case kResumeLooping:
        resume_looping();
        return true;
    default:
        return false;
    }
}

}  // namespace opentony::kitdll

#ifndef _WIN32
void opentony::kitdll::flush_standalone_loop_volumes() {
    if (!vehicle_sound_fix() || !device_up()) {
        return;
    }
    for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
        const auto request = g_loop_volumes[slot];
        if (request.level >= 0) {
            g_loop_volumes[slot] = LoopVolume{};
            apply_voice_volume(slot, request.duplicate, request.left, request.right);
        }
    }
}
#endif

void opentony::kitdll::set_standalone_audio(const OtHalAudioBackend* audio) {
    g_audio = audio != nullptr ? audio : &kSilent;
}

#ifdef _WIN32
// The HAL's audio service of the program (already initialised by the program, or null for no sound). Audio_Init (0x004f27c0) asks it to start.
extern "C" __declspec(dllexport) void __cdecl ot_standalone_set_audio_hal(const OtHalAudioBackend* audio) {
    opentony::kitdll::set_standalone_audio(audio);
}
#endif
