// opentony_movie_compare.exe (Phase 5.3b): the proof that the FFmpeg path plays what Bink plays.
//
// A 32-bit program for Wine that loads the game's own binkw32.dll as the reference decoder and the HAL's FFmpeg source
// (src/hal/ffmpeg/movie_source.cpp) as the replacement, opens the same Bink file with both and compares them frame by frame.
//
//   video BINKW32 FILE [MAX_FRAMES] [FORMAT]   the pictures: Bink's BinkCopyToBuffer against convert_to_pixels, FORMAT 32 or 565
//   yuv   BINKW32 FILE FRAME OUT_PREFIX        write Bink's RGB and FFmpeg's planes of one frame (to fit the colour conversion)
//   audio FILE                                 print the audio stream's format and length (the sound is compared in audio_compare)
//   sound BINKW32 FILE [OUT.pcm]               the sound: what Bink writes into a DirectSound buffer (a stand-in that records it) against read_audio
//
// Output: one `key value` line per result, so tests/test_movie_compare.py can read it.
#include "ffmpeg/movie_source.h"

#include "video.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace opentony::hal;

// binkw32.dll (RAD Game Tools, 2000): the exports used here, stdcall, found by their decorated names.
struct Bink {
    HMODULE module = nullptr;
    void* (__stdcall* open)(const char*, std::uint32_t) = nullptr;
    int (__stdcall* do_frame)(void*) = nullptr;
    int (__stdcall* copy_to_buffer)(void*, void*, int, unsigned, unsigned, unsigned, std::uint32_t) = nullptr;
    void (__stdcall* next_frame)(void*) = nullptr;
    void (__stdcall* close)(void*) = nullptr;
    int (__stdcall* set_sound_on_off)(void*, int) = nullptr;

    bool load(const char* path) {
        module = LoadLibraryA(path);
        if (module == nullptr) return false;
        const auto get = [&](const char* name, auto& target) {
            target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(GetProcAddress(module, name));
            return target != nullptr;
        };
        return get("_BinkOpen@8", open) && get("_BinkDoFrame@4", do_frame) && get("_BinkCopyToBuffer@28", copy_to_buffer) &&
               get("_BinkNextFrame@4", next_frame) && get("_BinkClose@4", close) && get("_BinkSetSoundOnOff@8", set_sound_on_off);
    }
};

// The header of an open Bink file (binkw.h, struct BINK).
struct BinkHeader {
    std::uint32_t width, height, frames, frame_number, last_frame_number, frame_rate, frame_rate_divisor;
};

constexpr std::uint32_t kSurface32 = 3;       // BINKSURFACE32: B, G, R, X bytes
constexpr std::uint32_t kSurface565 = 10;     // BINKSURFACE565
constexpr std::uint32_t kCopyAll = 0x80000000u;

int usage() {
    std::fprintf(stderr, "usage: movie_compare video|yuv|audio ...\n");
    return 2;
}

struct Diff {
    std::uint64_t pixels = 0;
    std::uint64_t different = 0;
    int max_channel = 0;
    std::uint64_t channel_sum = 0;
};

// compares two frames of `format`; the alpha byte of 32-bit pixels is not compared (Bink writes X, the HAL writes 255)
void compare_frame(const std::uint8_t* a, const std::uint8_t* b, int width, int height, int pitch, std::uint32_t format, Diff& diff) {
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            int delta[3];
            if (format == kSurface32) {
                const std::uint8_t* p = a + row * pitch + column * 4;
                const std::uint8_t* q = b + row * pitch + column * 4;
                for (int c = 0; c < 3; ++c) delta[c] = std::abs(p[c] - q[c]);
            } else {
                std::uint16_t p, q;
                std::memcpy(&p, a + row * pitch + column * 2, 2);
                std::memcpy(&q, b + row * pitch + column * 2, 2);
                delta[0] = std::abs((p & 31) - (q & 31));
                delta[1] = std::abs(((p >> 5) & 63) - ((q >> 5) & 63));
                delta[2] = std::abs((p >> 11) - (q >> 11));
            }
            ++diff.pixels;
            const int worst = std::max({delta[0], delta[1], delta[2]});
            if (worst != 0) ++diff.different;
            diff.max_channel = std::max(diff.max_channel, worst);
            diff.channel_sum += static_cast<std::uint64_t>(delta[0] + delta[1] + delta[2]);
        }
    }
}

int video(int argc, char** argv) {
    if (argc < 4) return usage();
    Bink bink;
    if (!bink.load(argv[2])) {
        std::printf("error cannot load %s\n", argv[2]);
        return 1;
    }
    const std::string file = argv[3];
    const int limit = argc > 4 ? std::atoi(argv[4]) : 0;
    const std::uint32_t format = argc > 5 && std::atoi(argv[5]) == 565 ? kSurface565 : kSurface32;
    std::FILE* hashes = argc > 6 ? std::fopen(argv[6], "w") : nullptr;  // one hash per Bink frame, for the player test
    const std::uint32_t hal_format = format == kSurface565 ? OT_HAL_PIXEL_RGB565 : OT_HAL_PIXEL_ARGB8888;

    void* handle = bink.open(file.c_str(), 0);
    if (handle == nullptr) {
        std::printf("error BinkOpen failed\n");
        return 1;
    }
    bink.set_sound_on_off(handle, 0);
    const BinkHeader* header = static_cast<const BinkHeader*>(handle);
    std::string error;
    auto source = MovieSource::open(file, true, false, error);
    if (!source) {
        std::printf("error %s\n", error.c_str());
        return 1;
    }
    const MovieInfo& info = source->info();
    std::printf("bink_size %ux%u frames %u rate %u/%u\n", header->width, header->height, header->frames, header->frame_rate,
                header->frame_rate_divisor);
    std::printf("ffmpeg_size %dx%d frames %d rate %d/%d\n", info.width, info.height, info.frames, info.fps_num, info.fps_den);
    if (static_cast<int>(header->width) != info.width || static_cast<int>(header->height) != info.height) {
        std::printf("result SIZE_MISMATCH\n");
        return 1;
    }

    const int width = info.width, height = info.height;
    const int bytes = format == kSurface565 ? 2 : 4;
    const int pitch = width * bytes;
    std::vector<std::uint8_t> reference(static_cast<std::size_t>(pitch) * height), ours(reference.size());
    Diff total;
    std::uint32_t frames = 0, frames_different = 0;
    std::uint32_t total_frames = header->frames;
    if (limit > 0) total_frames = std::min<std::uint32_t>(total_frames, static_cast<std::uint32_t>(limit));
    bool ffmpeg_ended_early = false;
    for (std::uint32_t frame = 1; frame <= total_frames; ++frame) {
        bink.do_frame(handle);
        std::fill(reference.begin(), reference.end(), 0);
        bink.copy_to_buffer(handle, reference.data(), pitch, static_cast<unsigned>(height), 0, 0, format | kCopyAll);
        if (hashes != nullptr && format == kSurface32) {  // the frame as opentony_movie_player records it: FNV-1a of the B, G and R bytes
            std::uint64_t h = 1469598103934665603ull;
            for (std::size_t i = 0; i < reference.size(); i += 4) {
                for (int c = 0; c < 3; ++c) {
                    h ^= reference[i + static_cast<std::size_t>(c)];
                    h *= 1099511628211ull;
                }
            }
            std::fprintf(hashes, "%u %016llx\n", frame - 1, static_cast<unsigned long long>(h));
        }
        VideoFrame picture;
        if (!source->next_video(picture)) {
            ffmpeg_ended_early = true;
            break;
        }
        std::fill(ours.begin(), ours.end(), 0);
        convert_to_pixels(picture, hal_format, ours.data(), pitch);
        Diff one;
        compare_frame(reference.data(), ours.data(), width, height, pitch, format, one);
        if (one.different != 0) ++frames_different;
        total.pixels += one.pixels;
        total.different += one.different;
        total.channel_sum += one.channel_sum;
        total.max_channel = std::max(total.max_channel, one.max_channel);
        ++frames;
        if (frame < header->frames) bink.next_frame(handle);
    }
    VideoFrame extra;
    const bool ffmpeg_has_more = limit == 0 && !ffmpeg_ended_early && source->next_video(extra);
    bink.close(handle);
    if (hashes != nullptr) std::fclose(hashes);
    std::printf("frames_compared %u\n", frames);
    std::printf("frames_different %u\n", frames_different);
    std::printf("pixels %llu\n", static_cast<unsigned long long>(total.pixels));
    std::printf("pixels_different %llu\n", static_cast<unsigned long long>(total.different));
    std::printf("max_channel_difference %d\n", total.max_channel);
    std::printf("mean_channel_difference %.6f\n", total.pixels != 0 ? static_cast<double>(total.channel_sum) / (3.0 * static_cast<double>(total.pixels)) : 0.0);
    std::printf("ffmpeg_ended_early %d\nffmpeg_extra_frames %d\n", ffmpeg_ended_early ? 1 : 0, ffmpeg_has_more ? 1 : 0);
    std::printf("result %s\n", total.different == 0 && !ffmpeg_ended_early && !ffmpeg_has_more ? "IDENTICAL" : "DIFFERENT");
    return 0;
}

bool write_file(const std::string& path, const void* data, std::size_t size) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;
    const bool ok = std::fwrite(data, 1, size, file) == size;
    std::fclose(file);
    return ok;
}

int yuv(int argc, char** argv) {
    if (argc < 6) return usage();
    Bink bink;
    if (!bink.load(argv[2])) return 1;
    const std::string file = argv[3];
    const int wanted = std::atoi(argv[4]);
    const std::string prefix = argv[5];
    void* handle = bink.open(file.c_str(), 0);
    if (handle == nullptr) return 1;
    bink.set_sound_on_off(handle, 0);
    const BinkHeader* header = static_cast<const BinkHeader*>(handle);
    std::string error;
    auto source = MovieSource::open(file, true, false, error);
    if (!source) return 1;
    const int width = static_cast<int>(header->width), height = static_cast<int>(header->height);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 4);
    VideoFrame picture;
    for (int frame = 1; frame <= wanted; ++frame) {
        bink.do_frame(handle);
        if (!source->next_video(picture)) return 1;
        if (frame == wanted) bink.copy_to_buffer(handle, rgb.data(), width * 4, static_cast<unsigned>(height), 0, 0, kSurface32 | kCopyAll);
        if (frame < static_cast<int>(header->frames)) bink.next_frame(handle);
    }
    write_file(prefix + ".bgrx", rgb.data(), rgb.size());
    write_file(prefix + ".y", picture.y, static_cast<std::size_t>(picture.stride_y) * height);
    write_file(prefix + ".u", picture.u, static_cast<std::size_t>(picture.stride_uv) * ((height + 1) / 2));
    write_file(prefix + ".v", picture.v, static_cast<std::size_t>(picture.stride_uv) * ((height + 1) / 2));
    std::printf("wrote %s.{bgrx,y,u,v} %dx%d\n", prefix.c_str(), width, height);
    bink.close(handle);
    return 0;
}

int audio_info(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string error;
    auto source = MovieSource::open(argv[2], false, true, error);
    if (!source) {
        std::printf("error %s\n", error.c_str());
        return 1;
    }
    const MovieInfo& info = source->info();
    std::int64_t total = 0;
    std::vector<std::int16_t> block(4096 * static_cast<std::size_t>(std::max(info.audio_channels, 1)));
    while (const std::size_t got = source->read_audio(block.data(), 4096)) total += static_cast<std::int64_t>(got);
    std::printf("audio_rate %d\naudio_channels %d\naudio_frames %lld\n", info.audio_rate, info.audio_channels, static_cast<long long>(total));
    return 0;
}

// ---- Bink's sound, recorded ------------------------------------------------------------------------------------------------------
//
// binkw32.dll 1.x gives its decoded sound to nobody but a sound system, so the reference is taken where Bink hands it over: a stand-in
// IDirectSound whose streaming buffer is memory. Bink refills it behind the play cursor; the stand-in plays instantly (the cursor jumps
// to the end of what has been unlocked) and appends each byte it passes to the recording, which is therefore the sound in play order.
// Only the calls Bink makes are implemented.
struct FakeBuffer {
    void* const* vtable;
    std::vector<std::uint8_t> ring;
    std::uint32_t rate = 0, channels = 0, bits = 0;
    bool primary = false;
    std::uint64_t written = 0;  // bytes unlocked, counted from the first byte of the stream
    std::uint64_t played = 0;   // bytes passed to the recording
    std::uint32_t lock_offset = 0;
    std::uint32_t status = 0;
    std::uint32_t gaps = 0;  // locks that did not continue where the last one ended
    std::uint32_t locks = 0;
    std::vector<std::uint8_t> recording;
};
struct FakeDevice {
    void* const* vtable;
};

FakeBuffer g_stream;  // the one streaming buffer Bink creates
FakeBuffer g_primary;
FakeDevice g_device;
extern void* const g_buffer_vtable[21];
extern void* const g_device_vtable[11];

constexpr HRESULT kOk = 0;
constexpr HRESULT kInvalid = static_cast<HRESULT>(0x80070057);

void drain(FakeBuffer* b, std::uint64_t most = ~0ull) {  // "play" what is unlocked, at most `most` bytes
    if (b->ring.empty()) return;
    for (; b->played < b->written && most != 0; --most) {
        b->recording.push_back(b->ring[b->played % b->ring.size()]);
        ++b->played;
    }
}

HRESULT __stdcall ds_query(void* self, const GUID*, void** out) {
    *out = self;
    return kOk;
}
ULONG __stdcall ds_addref(void*) { return 1; }
ULONG __stdcall ds_release(void*) { return 1; }

HRESULT __stdcall dev_create_buffer(FakeDevice*, const std::uint32_t* desc, FakeBuffer** out, void*) {
    const bool primary = (desc[1] & 1u) != 0;
    std::fprintf(stderr, "CreateSoundBuffer size %u flags %08x bytes %u\n", desc[0], desc[1], desc[2]);
    FakeBuffer* b = primary ? &g_primary : &g_stream;
    b->vtable = g_buffer_vtable;
    b->primary = primary;
    if (!primary) {
        b->ring.assign(desc[2], 0);
        const std::uint16_t* format = reinterpret_cast<const std::uint16_t*>(static_cast<std::uintptr_t>(desc[4]));
        b->channels = format[1];
        b->rate = *reinterpret_cast<const std::uint32_t*>(format + 2);
        b->bits = format[7];
    }
    *out = b;
    return kOk;
}
HRESULT __stdcall dev_get_caps(FakeDevice*, std::uint32_t* caps) {
    if (caps == nullptr || caps[0] < 0x60) return kInvalid;
    std::memset(caps + 1, 0, 0x60 - 4);
    caps[1] = 0x00000f1fu;
    caps[2] = 100;
    caps[3] = 200000;
    caps[4] = 1;
    return kOk;
}
HRESULT __stdcall dev_ok(void*) { return kOk; }
HRESULT __stdcall dev_ok1(void*, void*) { return kOk; }
HRESULT __stdcall dev_ok2(void*, void*, void*) { return kOk; }
HRESULT __stdcall dev_dup(FakeDevice*, FakeBuffer*, FakeBuffer**) { return kInvalid; }
HRESULT __stdcall dev_speaker(FakeDevice*, std::uint32_t* config) {
    if (config != nullptr) *config = 4;
    return kOk;
}

HRESULT __stdcall buf_get_caps(FakeBuffer* b, std::uint32_t* caps) {
    caps[1] = 0x00010000u | 0x00000080u;
    caps[2] = static_cast<std::uint32_t>(b->ring.size());
    caps[3] = 0;
    caps[4] = 0;
    return kOk;
}
std::uint64_t g_credit = ~0ull;  // bytes the device may still play (unlimited outside `sound`)
int g_trace = 0;  // OT_DS_TRACE=N prints the first N buffer calls: what Bink asks of its sound output
#define OT_TRACE(...)                      \
    do {                                   \
        if (g_trace > 0) {                 \
            --g_trace;                     \
            std::fprintf(stderr, __VA_ARGS__); \
        }                                  \
    } while (0)

HRESULT __stdcall buf_position(FakeBuffer* b, std::uint32_t* play, std::uint32_t* write) {
    // time passes, at the rate of the sound: the device plays what the frames Bink has decoded so far are worth (faster, and Bink
    // finds its sound short and writes silence; all at once, and the ring looks both full and empty)
    const std::uint64_t before = b->played;
    drain(b, std::min<std::uint64_t>(8192, g_credit));
    if (g_credit != ~0ull) g_credit -= b->played - before;
    OT_TRACE("position: written %llu played %llu\n", static_cast<unsigned long long>(b->written), static_cast<unsigned long long>(b->played));
    const std::uint32_t size = static_cast<std::uint32_t>(b->ring.empty() ? 1 : b->ring.size());
    if (play != nullptr) *play = static_cast<std::uint32_t>(b->played % size);
    if (write != nullptr) *write = static_cast<std::uint32_t>((b->played + 1024) % size);  // the safe distance ahead of the cursor
    return kOk;
}
HRESULT __stdcall buf_get_format(FakeBuffer* b, std::uint16_t* format, std::uint32_t size, std::uint32_t* written) {
    if (size < 18 || format == nullptr) return kInvalid;
    format[0] = 1;
    format[1] = static_cast<std::uint16_t>(b->channels);
    *reinterpret_cast<std::uint32_t*>(format + 2) = b->rate;
    *reinterpret_cast<std::uint32_t*>(format + 4) = b->rate * b->channels * b->bits / 8;
    format[6] = static_cast<std::uint16_t>(b->channels * b->bits / 8);
    format[7] = static_cast<std::uint16_t>(b->bits);
    format[8] = 0;
    if (written != nullptr) *written = 18;
    return kOk;
}
HRESULT __stdcall buf_get_long(FakeBuffer*, std::int32_t* v) {
    *v = 0;
    return kOk;
}
HRESULT __stdcall buf_get_dword(FakeBuffer* b, std::uint32_t* v) {
    *v = b->rate;
    return kOk;
}
HRESULT __stdcall buf_status(FakeBuffer* b, std::uint32_t* v) {
    *v = b->status;
    return kOk;
}
HRESULT __stdcall buf_lock(FakeBuffer* b, std::uint32_t offset, std::uint32_t bytes, void** p1, std::uint32_t* n1, void** p2,
                           std::uint32_t* n2, std::uint32_t flags) {
    const std::uint32_t size = static_cast<std::uint32_t>(b->ring.size());
    if (size == 0) return kInvalid;
    if (flags & 2u) bytes = size;   // DSBLOCK_ENTIREBUFFER
    if (offset >= size) offset = 0;  // an offset equal to the size means the start
    if (bytes > size) return kInvalid;
    b->lock_offset = offset;
    ++b->locks;
    OT_TRACE("lock offset %u bytes %u flags %u (written %llu played %llu)\n", offset, bytes, flags,
             static_cast<unsigned long long>(b->written), static_cast<unsigned long long>(b->played));
    const std::uint32_t first = std::min(bytes, size - offset);
    *p1 = b->ring.data() + offset;
    *n1 = first;
    if (p2 != nullptr) *p2 = bytes > first ? b->ring.data() : nullptr;
    if (n2 != nullptr) *n2 = bytes - first;
    return kOk;
}
HRESULT __stdcall buf_unlock(FakeBuffer* b, void*, std::uint32_t n1, void*, std::uint32_t n2) {
    OT_TRACE("unlock %u + %u\n", n1, n2);
    if (b->locks == 1 && b->written == 0 && n1 + n2 == b->ring.size()) return kOk;  // Bink clears the whole ring first: not sound
    const std::uint32_t size = static_cast<std::uint32_t>(b->ring.size());
    const std::uint32_t at = static_cast<std::uint32_t>(b->written % size);
    if (b->lock_offset != at) {  // the sound was written somewhere other than the end of the last write
        ++b->gaps;
        b->written += (b->lock_offset + size - at) % size;
    }
    b->written += n1 + n2;
    return kOk;
}
HRESULT __stdcall buf_play(FakeBuffer* b, std::uint32_t, std::uint32_t, std::uint32_t flags) {
    b->status = 1u | ((flags & 1u) != 0 ? 4u : 0u);
    return kOk;
}
HRESULT __stdcall buf_stop(FakeBuffer* b) {
    b->status = 0;
    return kOk;
}
HRESULT __stdcall buf_set_position(FakeBuffer*, std::uint32_t) { return kOk; }
HRESULT __stdcall buf_ok1(FakeBuffer*, void*) { return kOk; }
HRESULT __stdcall buf_ok2(FakeBuffer*, std::int32_t) { return kOk; }
HRESULT __stdcall buf_initialize(FakeBuffer*, void*, void*) { return kOk; }
HRESULT __stdcall buf_restore(FakeBuffer*) { return kOk; }

#define OT_SLOT(f) reinterpret_cast<void*>(&f)
void* const g_device_vtable[11] = {OT_SLOT(ds_query),  OT_SLOT(ds_addref),  OT_SLOT(ds_release), OT_SLOT(dev_create_buffer),
                                   OT_SLOT(dev_get_caps), OT_SLOT(dev_dup), OT_SLOT(dev_ok2),    OT_SLOT(dev_ok),
                                   OT_SLOT(dev_speaker),  OT_SLOT(dev_ok1), OT_SLOT(dev_ok1)};
void* const g_buffer_vtable[21] = {OT_SLOT(ds_query),    OT_SLOT(ds_addref),     OT_SLOT(ds_release),  OT_SLOT(buf_get_caps),
                                   OT_SLOT(buf_position), OT_SLOT(buf_get_format), OT_SLOT(buf_get_long), OT_SLOT(buf_get_long),
                                   OT_SLOT(buf_get_dword), OT_SLOT(buf_status),    OT_SLOT(buf_initialize), OT_SLOT(buf_lock),
                                   OT_SLOT(buf_play),    OT_SLOT(buf_set_position), OT_SLOT(buf_ok1),   OT_SLOT(buf_ok2),
                                   OT_SLOT(buf_ok2),     OT_SLOT(buf_ok2),       OT_SLOT(buf_stop),    OT_SLOT(buf_unlock),
                                   OT_SLOT(buf_restore)};
#undef OT_SLOT

int sound(int argc, char** argv) {
    if (argc < 4) return usage();
    HMODULE module = LoadLibraryA(argv[2]);
    if (module == nullptr) return 1;
    const auto open = reinterpret_cast<void* (__stdcall*)(const char*, std::uint32_t)>(GetProcAddress(module, "_BinkOpen@8"));
    const auto set_system = reinterpret_cast<int(__stdcall*)(void*, std::uint32_t)>(GetProcAddress(module, "_BinkSetSoundSystem@8"));
    const auto open_ds = GetProcAddress(module, "_BinkOpenDirectSound@4");
    const auto do_frame = reinterpret_cast<int(__stdcall*)(void*)>(GetProcAddress(module, "_BinkDoFrame@4"));
    const auto next_frame = reinterpret_cast<void(__stdcall*)(void*)>(GetProcAddress(module, "_BinkNextFrame@4"));
    const auto service = reinterpret_cast<int(__stdcall*)(void*)>(GetProcAddress(module, "_BinkService@4"));
    const auto close = reinterpret_cast<void(__stdcall*)(void*)>(GetProcAddress(module, "_BinkClose@4"));
    const auto set_video = reinterpret_cast<int(__stdcall*)(void*, int)>(GetProcAddress(module, "_BinkSetVideoOnOff@8"));
    if (!open || !set_system || !open_ds || !do_frame || !next_frame || !service || !close) {
        std::printf("error binkw32.dll lacks an export\n");
        return 1;
    }
    g_device.vtable = g_device_vtable;
    if (std::getenv("OT_DS_TRACE") != nullptr) g_trace = std::atoi(std::getenv("OT_DS_TRACE"));
    set_system(reinterpret_cast<void*>(open_ds), static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_device)));
    std::fprintf(stderr, "sound system set; opening %s\n", argv[3]);
    void* handle = open(argv[3], 0);
    std::fprintf(stderr, "BinkOpen returned %p\n", handle);
    if (handle == nullptr) {
        std::printf("error BinkOpen failed\n");
        return 1;
    }
    if (set_video != nullptr) set_video(handle, 0);  // the picture is compared by `video`
    const BinkHeader* header = static_cast<const BinkHeader*>(handle);
    const std::uint32_t frames = header->frames;
    // the sound one frame is worth, in bytes of the buffer's format
    const std::uint64_t bytes_per_frame = static_cast<std::uint64_t>(g_stream.rate) * g_stream.channels * (g_stream.bits / 8) *
                                          header->frame_rate_divisor / std::max<std::uint32_t>(header->frame_rate, 1);
    g_credit = 0;
    for (std::uint32_t frame = 1; frame <= frames; ++frame) {
        do_frame(handle);
        g_credit += bytes_per_frame;
        for (int i = 0; i < 8; ++i) service(handle);  // each question about the cursor lets the device play a little
        if (frame < frames) next_frame(handle);
    }
    g_credit = ~0ull;
    for (int i = 0; i < 64; ++i) service(handle);  // let the tail out
    close(handle);
    drain(&g_stream);

    std::string error;
    auto source = MovieSource::open(argv[3], false, true, error);
    if (!source) {
        std::printf("error %s\n", error.c_str());
        return 1;
    }
    const MovieInfo& info = source->info();
    const std::size_t channels = static_cast<std::size_t>(std::max(info.audio_channels, 1));
    std::vector<std::int16_t> ours;
    std::vector<std::int16_t> block(4096 * channels);
    while (const std::size_t got = source->read_audio(block.data(), 4096)) {
        ours.insert(ours.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(got * channels));
    }

    const std::vector<std::uint8_t>& raw = g_stream.recording;
    std::vector<std::int16_t> theirs(raw.size() / 2);
    if (!raw.empty()) std::memcpy(theirs.data(), raw.data(), theirs.size() * 2);
    if (argc > 4) write_file(argv[4], raw.data(), raw.size());
    if (argc > 5) write_file(argv[5], ours.data(), ours.size() * 2);

    // the decoder's own floats, to see how Bink reaches 16 bits from them: how many samples each conversion gets right
    std::vector<float> floats;
    if (auto again = MovieSource::open(argv[3], false, true, error)) {
        std::vector<float> chunk(4096 * channels);
        while (const std::size_t got = again->read_audio_float(chunk.data(), 4096)) {
            floats.insert(floats.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got * channels));
        }
    }
    if (argc > 6) write_file(argv[6], floats.data(), floats.size() * sizeof(float));
    const std::size_t limit = std::min(floats.size(), theirs.size());
    struct Conversion {
        const char* name;
        int (*apply)(float);
    };
    static const Conversion conversions[] = {
        {"round(x*32768)", [](float x) { return static_cast<int>(std::lrintf(std::clamp(x * 32768.0f, -32768.0f, 32767.0f))); }},
        {"trunc(x*32768)", [](float x) { return static_cast<int>(std::clamp(x * 32768.0f, -32768.0f, 32767.0f)); }},
        {"floor(x*32768)", [](float x) { return static_cast<int>(std::floor(std::clamp(x * 32768.0f, -32768.0f, 32767.0f))); }},
        {"round(x*32767)", [](float x) { return static_cast<int>(std::lrintf(std::clamp(x * 32767.0f, -32768.0f, 32767.0f))); }},
        {"trunc(x*32767)", [](float x) { return static_cast<int>(std::clamp(x * 32767.0f, -32768.0f, 32767.0f)); }},
    };
    for (const Conversion& conversion : conversions) {
        std::size_t wrong = 0, off_by_more = 0;
        for (std::size_t i = 0; i < limit; ++i) {
            const int d = std::abs(conversion.apply(floats[i]) - theirs[i]);
            wrong += d != 0;
            off_by_more += d > 1;
        }
        std::printf("conversion %-16s samples_wrong %zu of %zu, off_by_more_than_1 %zu\n", conversion.name, wrong, limit, off_by_more);
    }
    std::printf("bink_sound rate %u channels %u bits %u samples %zu stream_buffer %zu locks %u gaps %u\n", g_stream.rate, g_stream.channels,
                g_stream.bits, theirs.size(), g_stream.ring.size(), g_stream.locks, g_stream.gaps);
    std::printf("ffmpeg_sound rate %d channels %d samples %zu\n", info.audio_rate, info.audio_channels, ours.size());
    // compare the common prefix and report what each side has beyond it
    const std::size_t common = std::min(ours.size(), theirs.size());
    std::size_t different = 0;
    int worst = 0;
    double square = 0;
    std::size_t first = common;
    std::size_t histogram[4] = {0, 0, 0, 0};  // |difference| 0, 1, 2, more
    for (std::size_t i = 0; i < common; ++i) {
        const int d = ours[i] - theirs[i];
        ++histogram[std::min(std::abs(d), 3)];
        if (d != 0) {
            ++different;
            if (first == common) first = i;
            worst = std::max(worst, std::abs(d));
            square += static_cast<double>(d) * d;
        }
    }
    std::size_t nonzero_tail = 0;
    for (std::size_t i = common; i < theirs.size(); ++i) nonzero_tail += theirs[i] != 0;
    std::printf("samples_compared %zu\nsamples_different %zu\nmax_difference %d\nrms_difference %.6f\nfirst_difference %lld\n", common, different,
                worst, common != 0 ? std::sqrt(square / static_cast<double>(common)) : 0.0, first == common ? -1ll : static_cast<long long>(first));
    std::printf("difference_histogram equal %zu off_by_1 %zu off_by_2 %zu more %zu\n", histogram[0], histogram[1], histogram[2], histogram[3]);
    std::printf("bink_extra_samples %zu nonzero %zu\nffmpeg_extra_samples %zu\n", theirs.size() - common, nonzero_tail, ours.size() - common);
    // what Bink plays beyond the length of the file is its looping of the track, not part of the sound
    std::printf("result %s\n", different == 0 && theirs.size() >= ours.size() ? "IDENTICAL" : "DIFFERENT");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string mode = argv[1];
    if (mode == "video") return video(argc, argv);
    if (mode == "yuv") return yuv(argc, argv);
    if (mode == "audio") return audio_info(argc, argv);
    if (mode == "sound") return sound(argc, argv);
    return usage();
}
