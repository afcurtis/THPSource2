// opentony_movie_player.exe (Phase 5.3b): plays the game's movies and music through the standalone seams, with no Bink.
//
//   opentony_movie_player.exe <opentony_standalone.dll> <out-dir> startup NAME...   PCMovie_PlayStartupMedia(`\NAME.STR;1`) for each
//   opentony_movie_player.exe <opentony_standalone.dll> <out-dir> fullscreen NAME...   the same with the control mode 0 of a level (the bails
//                                                                                     video after a medal): the full screen path, SetTexture at the end
//   opentony_movie_player.exe <opentony_standalone.dll> <out-dir> music N...        start_music of `LTIXnn.DAT`, run to its end
//   opentony_movie_player.exe <opentony_standalone.dll> <out-dir> clip NAME FRAMES  a level-select clip (looping, one BinkMovieUpdate per tick)
//
// Like opentony_spike.exe it is linked at the retail base with a zero-filled section over the retail address range, so the natives and
// seams find their globals where they expect them; unlike the spike it needs no retail data, because the movie natives read none. The
// HAL is a recording one: the movie backend keeps the frame target and writes `frames.txt` (one FNV-1a hash of the B, G and R bytes of
// every decoded frame, with the frame size) and the audio backend writes `audio.pcm` (what the game would have heard, 16-bit
// interleaved) and `audio.txt` (its format). tests/test_movie_compare.py compares those with Bink's own decode.
#include "../kit_abi.h"

#include "audio.h"
#include "movie.h"
#include "video.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

#pragma clang section bss = ".retail"
alignas(4096) std::uint8_t g_retail_reserve[0x25c0000];
#pragma clang section bss = ""

constexpr std::uint32_t kRetailBase = 0x00400000u;
constexpr std::uint32_t kControlMode = 0x006a6964u;  // the windowed set-up: pictures go to a texture
constexpr std::uint32_t kStartMusic = 0x004e7260u;   // AudioGame::start_music(name)
constexpr std::uint32_t kXaDone = 0x004e73e0u;       // xa_done()
constexpr std::uint32_t kXaStop = 0x004e7370u;
constexpr std::uint32_t kStartupMedia = 0x004e5ec0u;
constexpr std::uint32_t kStartBinkMovie = 0x004e6590u;
constexpr std::uint32_t kCreateBinkSurface = 0x004e6020u;
constexpr std::uint32_t kBinkMovieUpdate = 0x004e6e50u;
constexpr std::uint32_t kCloseBinkMovie = 0x004e6da0u;
constexpr std::uint32_t kGameFrameTick = 0x004f7ce0u;
constexpr std::uint32_t kScratch = 0x00580000u;      // names for the calls (inside the zero-filled retail range)

void log_line(const char* line) {
    std::printf("kit: %s\n", line);
    std::fflush(stdout);
}

// ---- recording HAL ---------------------------------------------------------------------------------------------------------
struct Recording {
    std::string directory;
    std::FILE* frames = nullptr;
    std::FILE* pcm = nullptr;
    std::vector<std::uint8_t> target;
    std::int32_t width = 0, height = 0;
    std::uint32_t format = 0;
    std::uint64_t presented = 0;  // frames decoded into the target
    std::uint64_t shown = 0;      // present calls
    std::uint64_t pcm_bytes = 0;
    std::uint32_t pcm_rate = 0;
    std::uint16_t pcm_channels = 0;
} g;

std::uint64_t hash_bgr(const std::uint8_t* pixels, int width, int height, int pitch) {
    std::uint64_t h = 1469598103934665603ull;
    for (int row = 0; row < height; ++row) {
        const std::uint8_t* line = pixels + static_cast<std::ptrdiff_t>(row) * pitch;
        for (int column = 0; column < width; ++column) {
            for (int c = 0; c < 3; ++c) {
                h ^= line[column * 4 + c];
                h *= 1099511628211ull;
            }
        }
    }
    return h;
}

int OT_HAL_CALL movie_open(int32_t width, int32_t height, uint32_t format, uint32_t) {
    if (format != OT_HAL_PIXEL_ARGB8888) return 0;
    g.width = width;
    g.height = height;
    g.format = format;
    g.target.assign(static_cast<std::size_t>(width) * height * 4, 0);
    std::fprintf(g.frames, "open %d %d\n", width, height);
    return 1;
}
int OT_HAL_CALL movie_lock(OtHalMovieFrame* frame) {
    frame->pixels = g.target.data();
    frame->pitch = g.width * 4;
    frame->width = g.width;
    frame->height = g.height;
    frame->format = g.format;
    return 1;
}
void OT_HAL_CALL movie_unlock() {  // a frame has been decoded into the target
    std::fprintf(g.frames, "%llu %016llx\n", static_cast<unsigned long long>(g.presented),
                 static_cast<unsigned long long>(hash_bgr(g.target.data(), g.width, g.height, g.width * 4)));
    ++g.presented;
}
void OT_HAL_CALL movie_present(const OtHalRect*) { ++g.shown; }
void OT_HAL_CALL movie_close() { std::fprintf(g.frames, "close\n"); }

const OtHalMovieBackend kMovie = {"recording", &movie_open, &movie_lock, &movie_unlock, &movie_present, &movie_close, nullptr};

OtHalHandle OT_HAL_CALL stream_open(const OtHalPcmFormat* format) {
    g.pcm_rate = format->sample_rate;
    g.pcm_channels = format->channels;
    return 1;
}
uint32_t OT_HAL_CALL stream_write(OtHalHandle, const void* data, uint32_t size) {
    std::fwrite(data, 1, size, g.pcm);
    g.pcm_bytes += size;
    return size;
}
uint32_t OT_HAL_CALL stream_queued(OtHalHandle) { return 0; }  // the recording device plays instantly
void OT_HAL_CALL stream_volume(OtHalHandle, int32_t) {}
void OT_HAL_CALL stream_pause(OtHalHandle, int) {}
void OT_HAL_CALL stream_close(OtHalHandle) {}

OtHalAudioBackend make_audio() {
    OtHalAudioBackend audio = {};
    audio.name = "recording";
    audio.stream_open = &stream_open;
    audio.stream_write = &stream_write;
    audio.stream_queued = &stream_queued;
    audio.stream_set_volume = &stream_volume;
    audio.stream_pause = &stream_pause;
    audio.stream_close = &stream_close;
    return audio;
}

LONG WINAPI crash_filter(EXCEPTION_POINTERS* info) {
    std::printf("CRASH %08lx at %p\n", info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    std::fflush(stdout);
    ExitProcess(0xc0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: opentony_movie_player <opentony_standalone.dll> <out-dir> startup|fullscreen|music|clip NAME...\n");
        return 64;
    }
    SetUnhandledExceptionFilter(&crash_filter);
    const auto reserve_begin = reinterpret_cast<std::uintptr_t>(g_retail_reserve);
    if (reserve_begin > 0x00524000u || reserve_begin + sizeof g_retail_reserve < kRetailBase + 0x025de000u) {
        std::fprintf(stderr, "the zero-filled section does not cover the retail range\n");
        return 3;
    }
    g.directory = argv[2];
    CreateDirectoryA(g.directory.c_str(), nullptr);
    g.frames = std::fopen((g.directory + "\\frames.txt").c_str(), "w");
    g.pcm = std::fopen((g.directory + "\\audio.pcm").c_str(), "wb");
    if (g.frames == nullptr || g.pcm == nullptr) {
        std::fprintf(stderr, "cannot write into %s\n", argv[2]);
        return 65;
    }

    // The pad read (ReadControllers, 0x00489cc0, behind Input_UpdateActionStates) is 5.5's input layer, not a Bink seam. The resolver's
    // lenient mode logs each call it cannot resolve once and answers 0, so no key is ever pressed and a movie plays to its end; the
    // log lines below say what the movie loop still reaches that this program does not provide.
    _putenv("OT_RESOLVER_LENIENT=1");
    const HMODULE dll = LoadLibraryA(argv[1]);
    if (dll == nullptr) {
        std::fprintf(stderr, "cannot load %s (error %lu)\n", argv[1], GetLastError());
        return 5;
    }
    const auto attach = reinterpret_cast<OtKitAttach>(GetProcAddress(dll, OT_KIT_ATTACH_STANDALONE));
    const auto call = reinterpret_cast<std::uint32_t(__cdecl*)(std::uint32_t, const std::uint32_t*, std::uint32_t)>(
        GetProcAddress(dll, OT_STANDALONE_CALL));
    const auto set_hal = reinterpret_cast<void(__cdecl*)(const OtHalMovieBackend*, const OtHalAudioBackend*)>(
        GetProcAddress(dll, "ot_standalone_set_movie_hal"));
    if (attach == nullptr || call == nullptr || set_hal == nullptr) {
        std::fprintf(stderr, "%s lacks the standalone exports\n", argv[1]);
        return 5;
    }
    OtKitHost host{};
    host.version = OT_KIT_ABI_VERSION;
    host.build = "canonical";
    host.log = &log_line;
    std::uint32_t count = 0;
    if (attach(&host, &count) == nullptr) {
        std::fprintf(stderr, "the Kits declined to attach\n");
        return 5;
    }
    static OtHalAudioBackend audio = make_audio();
    set_hal(&kMovie, &audio);
    const std::string mode = argv[3];
    *reinterpret_cast<volatile std::uint32_t*>(kControlMode) = mode == "fullscreen" ? 0 : 1;

    int status = 0;
    for (int index = 4; index < argc; ++index) {
        char* name = reinterpret_cast<char*>(kScratch);
        if (mode == "startup" || mode == "fullscreen") {
            std::snprintf(name, 0x100, "\\%s.STR;1", argv[index]);
            std::fprintf(g.frames, "movie %s\n", argv[index]);
            const std::uint32_t arguments[1] = {kScratch};
            const std::uint32_t played = call(kStartupMedia, arguments, 1);
            std::printf("%s %s: %s, %llu frames decoded so far\n", mode.c_str(), argv[index], (played & 0xff) != 0 ? "played" : "NOT PLAYED",
                        static_cast<unsigned long long>(g.presented));
            if ((played & 0xff) == 0) status = 1;
        } else if (mode == "music") {
            std::snprintf(name, 0x100, "LTIX%02d.DAT", std::atoi(argv[index]));
            std::fprintf(g.frames, "music %s\n", name);
            const std::uint32_t arguments[1] = {kScratch};
            const std::uint32_t started = call(kStartMusic, arguments, 1);
            std::printf("music %s: %s\n", name, (started & 0xff) != 0 ? "started" : "NOT STARTED");
            if ((started & 0xff) == 0) {
                status = 1;
                continue;
            }
            for (int wait = 0; wait < 6000 && (call(kXaDone, nullptr, 0) & 0xff) == 0; ++wait) Sleep(10);  // up to a minute
            if ((call(kXaDone, nullptr, 0) & 0xff) == 0) {
                std::printf("music %s: still playing after a minute; stopping it\n", name);
                call(kXaStop, nullptr, 0);
                status = 1;
            }
        } else if (mode == "clip" && index + 1 < argc) {
            // the level-select clip: StartBinkMovie(name, 0, 0), CreateBinkSurface, then one BinkMovieUpdate per game tick
            std::snprintf(name, 0x100, "\\%s.STR;1", argv[index]);
            const int wanted = std::atoi(argv[index + 1]);
            ++index;
            std::fprintf(g.frames, "movie %s\n", argv[index - 1]);
            const std::uint32_t arguments[3] = {kScratch, 0, 0};
            const bool started = (call(kStartBinkMovie, arguments, 3) & 0xff) != 0 && call(kCreateBinkSurface, nullptr, 0) != 0;
            std::printf("clip %s: %s\n", argv[index - 1], started ? "started" : "NOT STARTED");
            if (!started) {
                status = 1;
                continue;
            }
            for (int tick = 0; tick < wanted * 8 && g.presented < static_cast<std::uint64_t>(wanted); ++tick) {
                call(kGameFrameTick, nullptr, 0);
                call(kBinkMovieUpdate, nullptr, 0);
            }
            call(kCloseBinkMovie, nullptr, 0);
        } else {
            std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
            return 64;
        }
    }
    std::fprintf(g.frames, "end %llu\n", static_cast<unsigned long long>(g.presented));
    std::fclose(g.frames);
    std::fclose(g.pcm);
    std::FILE* info = std::fopen((g.directory + "\\audio.txt").c_str(), "w");
    if (info != nullptr) {
        std::fprintf(info, "rate %u\nchannels %u\nbytes %llu\n", g.pcm_rate, static_cast<unsigned>(g.pcm_channels),
                     static_cast<unsigned long long>(g.pcm_bytes));
        std::fclose(info);
    }
    std::printf("RESULT %llu frames decoded, %llu shown, %llu audio bytes\n", static_cast<unsigned long long>(g.presented),
                static_cast<unsigned long long>(g.shown), static_cast<unsigned long long>(g.pcm_bytes));
    return status;
}
