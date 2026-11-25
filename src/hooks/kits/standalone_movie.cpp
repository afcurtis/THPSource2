// The Bink seams of a standalone program (Phase 5.3b): the movie and music wrappers around binkw32.dll that the natives of
// src/runtime/audio_movie.cpp call (re/symbols/seams.yml, category bink), implemented on the HAL's FFmpeg decoder
// (src/hal/ffmpeg/movie_source.h) and the HAL movie and audio services (src/hal/movie.h, src/hal/audio.h). No Bink is loaded.
//
// What a seam stands for is the retail wrapper's contract as the natives use it, read from its code (0x004e6590 StartBinkMovie,
// 0x004e6020 CreateBinkSurface, 0x004e6890 NextBinkFrame, 0x004e6da0 CloseBinkMovie, 0x004e6e50 BinkMovieUpdate, 0x004e71d0 the
// music thread) and not a copy of its Direct3D and DirectDraw work, which the HAL owns:
//  * StartBinkMovie(name, music, startup): the name is a PlayStation path (`\ATVILOGO.STR;1`) or a music file (`LTIX05.DAT`); the
//    wrapper keeps the part after the last backslash, turns `.STR...` into `.DAT`, and looks in the movie or the music directory.
//    It opens the file, stores the handle in BinkMovie (0x006a6ce4), and answers AL.
//  * NextBinkFrame(): decodes and shows the next picture, then 0 after the last frame (or, for a looping clip in texture mode, back to the
//    first); the natives only test the handle for zero and pass it on, so the handle here is a counter, not a pointer.
//  * The music runs on a thread (`_beginthread(PCMOVIE_StreamThread)`) that feeds the sound until the file ends or the natives set the
//    stop flag (0x0054b0a8), then clears the playing flag; here the thread is a host thread that does the same on a HAL audio stream.
//  * The Bink imports the natives report by name (`win32_BinkWait`, `win32_BinkService` ...) are answered here too.
//
// Pacing is the standalone's lockstep clock (standalone_seams.cpp): a frame is due when the clock has run its duration since the
// movie started, and a wait that is not over moves the clock on by a millisecond, as a sleep would. A real-time clock arrives with 5.5.
#include "kit_bridge.hpp"
#include "standalone_music.hpp"
#include "standalone_options.hpp"
#include "seam_addresses_generated.hpp"

#include "ffmpeg/movie_source.h"
#include "audio.h"
#include "movie.h"
#include "video.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "guest_arena.hpp"

namespace opentony::kitdll {
namespace {

using Args = std::vector<std::uint32_t>;

template <typename T>
volatile T& retail(std::uint32_t address) {
    return *arena::host<volatile T>(address);
}

// retail globals the wrappers keep (audio_game.hpp has the ones the natives read)
constexpr std::uint32_t kMoviePlaying = 0x006a6ce0;   // a movie or the music thread is running
constexpr std::uint32_t kMovieStopping = 0x006a6ce2;  // the music thread was parked (pause)
constexpr std::uint32_t kMovieHandle = 0x006a6ce4;    // BinkMovie
constexpr std::uint32_t kMovieWindow = 0x006a6d08;
constexpr std::uint32_t kThreadFlag = 0x0054b0a8;     // the music thread must end
constexpr std::uint32_t kControlMode = 0x006a6964;    // non-zero: the windowed set-up, pictures go to a texture, not the screen
constexpr std::uint32_t kMovieTextureU = 0x006a6bd8;  // the picture's share of the movie texture, as floats (audio_movie.cpp's kMovieTexture, kMovieTexture2)
constexpr std::uint32_t kMovieTextureV = 0x006a6bdc;

struct Hal {
    const OtHalMovieBackend* movie = nullptr;
    const OtHalAudioBackend* audio = nullptr;
} g_hal;

// One open movie or music file: retail has a single Bink handle (BinkMovie).
struct Player {
    std::unique_ptr<hal::MovieSource> source;
    bool music = false;
    bool startup = false;
    bool surface = false;
    bool loop = false;
    std::uint32_t frame = 1;          // BINK.FrameNum, 1 based
    std::uint32_t frames = 0;
    std::uint64_t started_us = 0;     // lockstep clock when the first frame became due
    OtHalHandle stream = 0;
    std::uint64_t audio_frames_written = 0;
    bool audio_ended = false;
    std::uint32_t handle = 0;
    std::int32_t bink_volume = 32768;   // the last BinkSetVolume (0..32768, linear); the player's master and music or movie levels are applied on top
};

// OT_MOVIE_LOG=1: every step of the music and movie player on stderr (what starts, what opens a stream, pauses, volumes, the music thread's start and end)
void movie_log(const char* format, ...) {
    static const bool on = std::getenv("OT_MOVIE_LOG") != nullptr;
    if (!on) return;
    char text[256];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    std::fprintf(stderr, "movie: %s\n", text);
}

std::mutex g_lock;  // the music thread and the game thread both reach the player
Player g_player;
std::uint32_t g_handles = 0;
std::atomic<bool> g_thread_running{false};

std::string utf8_of(const std::wstring& text) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes > 0 ? bytes : 0), '\0');
    if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

// The media folder as UTF-8, the encoding FFmpeg takes a file name in on Windows (MovieSource::open). It was read and tested through the ANSI calls, so an install
// path with a letter outside ASCII (C:\Users\José\...) reached FFmpeg in the code page's bytes and no game song, intro movie or level select clip opened.
std::string directory_of(const char* environment, const std::vector<std::string>& relative) {
    std::wstring root;
    const std::wstring name(environment, environment + std::strlen(environment));
    wchar_t value[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(name.c_str(), value, MAX_PATH);
    if (length != 0 && length < MAX_PATH) {
        root = value;
    } else {
        wchar_t module[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, module, MAX_PATH);
        root = module;
        root = root.substr(0, root.find_last_of(L"\\/"));
    }
    if (!root.empty() && root.back() != L'\\' && root.back() != L'/') root += L'\\';
    for (const std::string& candidate : relative) {
        const std::wstring path = root + std::wstring(candidate.begin(), candidate.end());
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return utf8_of(path) + "\\";
    }
    return utf8_of(root);
}

// The file a wrapper call names. `\ATVILOGO.STR;1` -> ATVILOGO.DAT, `LTIX05.DAT` -> LTIX05.DAT, in the movie or the music directory
// (OPENTONY_MEDIA_DIR is the install's media root: `med\Movies` and `data\Music` under it, as on the disc and in an install).
std::string media_path(const std::string& name, bool music) {
    std::string base = name.substr(name.find_last_of('\\') == std::string::npos ? 0 : name.find_last_of('\\') + 1);
    std::string upper = base;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper.find(".STR") != std::string::npos) {
        base = base.substr(0, base.find('.')) + ".DAT";
    }
    static const std::string movies = directory_of("OPENTONY_MEDIA_DIR", {"med\\Movies", "Movies", "SETUP\\med\\Movies"});
    static const std::string tracks = directory_of("OPENTONY_MEDIA_DIR", {"data\\Music", "Music", "SETUP\\data\\Music"});
    return (music ? tracks : movies) + base;
}

std::uint64_t clock_us() { return standalone_clock_micros(); }

// the time the movie has run, and when frame number `frame` (1 based) is due
bool frame_due(const Player& player) {
    if (player.source == nullptr || player.source->info().fps_num <= 0) return true;
    const hal::MovieInfo& info = player.source->info();
    const std::uint64_t due = (static_cast<std::uint64_t>(player.frame) - 1) * static_cast<std::uint64_t>(info.fps_den) * 1000000ull /
                              static_cast<std::uint64_t>(info.fps_num);
    return clock_us() - player.started_us >= due;
}

// Bink volume (0..32768, linear) to HAL millibels
std::int32_t millibels(std::int32_t volume) {
    if (volume <= 0) return OT_HAL_VOLUME_SILENT;
    if (volume >= 32768) return 0;
    return std::max<std::int32_t>(OT_HAL_VOLUME_SILENT, static_cast<std::int32_t>(std::lround(2000.0 * std::log10(volume / 32768.0))));
}

// The stream's volume: what the game asked for; a movie's sound also follows the game's music level (the CD music is scaled by the game itself)
void apply_stream_volume_locked(Player& player) {
    if (player.stream == 0 || g_hal.audio == nullptr || g_hal.audio->stream_set_volume == nullptr) return;
    const std::int32_t asked = millibels(player.bink_volume);
    const std::int32_t volume = asked <= OT_HAL_VOLUME_SILENT || player.music ? asked : std::max<std::int32_t>(OT_HAL_VOLUME_SILENT, asked + movie_volume_offset_mb());
    g_hal.audio->stream_set_volume(player.stream, volume);
}

// Keeps the HAL stream about half a second ahead of its device. Called from BinkService and from the music thread.
void feed_audio_locked(Player& player) {
    if (player.stream == 0 || g_hal.audio == nullptr || player.audio_ended) return;
    const hal::MovieInfo& info = player.source->info();
    const std::uint32_t bytes_per_frame = static_cast<std::uint32_t>(info.audio_channels) * 2u;
    const std::uint32_t target = static_cast<std::uint32_t>(info.audio_rate) / 2u * bytes_per_frame;
    std::vector<std::int16_t> block(1024u * static_cast<std::size_t>(info.audio_channels));
    while (g_hal.audio->stream_queued(player.stream) < target) {
        const std::size_t got = player.source->read_audio(block.data(), 1024);
        if (got == 0) {
            player.audio_ended = true;
            return;
        }
        g_hal.audio->stream_write(player.stream, block.data(), static_cast<std::uint32_t>(got) * bytes_per_frame);
        player.audio_frames_written += got;
    }
}

void close_locked() {
    movie_log("close (handle %u, stream %u)", g_player.handle, static_cast<unsigned>(g_player.stream));
    if (g_player.stream != 0 && g_hal.audio != nullptr) g_hal.audio->stream_close(g_player.stream);
    if (g_player.surface && g_hal.movie != nullptr) g_hal.movie->close();
    g_player = Player();
    retail<std::uint32_t>(kMovieHandle) = 0;
}

std::uint32_t start_movie(const Args& args, const std::string& text) {
    const std::string name = !text.empty() ? text : (args.empty() || args[0] == 0 ? std::string() : std::string(arena::host<const char>(args[0])));
    const bool music = args.size() > 1 && (args[1] & 0xff) != 0;
    const bool startup = args.size() > 2 && (args[2] & 0xff) != 0;
    std::lock_guard<std::mutex> lock(g_lock);
    close_locked();
    // the custom soundtrack (standalone_music.cpp): a level song may be the player's file, or another of the game's
    const MusicChoice custom = music ? music_choice(name) : MusicChoice();
    const std::string path = !custom.replaced ? media_path(name, music) : custom.disc ? media_path(custom.file, true) : custom.file;
    std::string error;
    // a music file is read for its sound; a movie for its picture and its sound
    std::unique_ptr<hal::MovieSource> source = hal::MovieSource::open(path, !music, true, error);
    if (source == nullptr) {
        std::fprintf(stderr, "game: StartBinkMovie(%s): %s\n", name.c_str(), error.c_str());
        if (custom.replaced && !custom.disc) {
            music_open_failed(custom.file);   // the player's song: passed over from now on (the game asks for its next track)
        }
        return 0;
    }
    g_player.source = std::move(source);
    g_player.music = music;
    g_player.startup = startup;
    const hal::MovieInfo& info = g_player.source->info();
    g_player.frames = static_cast<std::uint32_t>(std::max(info.frames, 0));
    g_player.frame = 1;
    g_player.started_us = clock_us();
    // the clip loops in texture mode when it is small (retail: 0x004e6841 sets 0x006a6cfd only on that path)
    g_player.loop = !music && retail<std::uint32_t>(kControlMode) != 0 && info.height < 200;
    g_player.handle = ++g_handles;
    retail<std::uint32_t>(kMovieHandle) = g_player.handle;
    movie_log("start %s (%s, handle %u)", path.c_str(), music ? "music" : "movie", g_player.handle);
    return 1;
}

std::uint32_t create_surface() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_player.source == nullptr) return 0;
    const hal::MovieInfo& info = g_player.source->info();
    if (info.has_audio && g_hal.audio != nullptr && g_hal.audio->stream_open != nullptr) {
        OtHalPcmFormat format = {static_cast<std::uint32_t>(info.audio_rate), static_cast<std::uint16_t>(info.audio_channels), 16};
        g_player.stream = g_hal.audio->stream_open(&format);
        movie_log("stream %u opened (%d Hz, %d channels)", static_cast<unsigned>(g_player.stream), info.audio_rate, info.audio_channels);
        g_player.bink_volume = 32768;
        apply_stream_volume_locked(g_player);
        feed_audio_locked(g_player);
    }
    if (g_player.music || !info.has_video) return 1;
    if (g_hal.movie != nullptr) {
        const std::uint32_t flags = g_player.startup ? OT_HAL_MOVIE_FULLSCREEN : OT_HAL_MOVIE_OVERLAY;
        if (!g_hal.movie->open(info.width, info.height, OT_HAL_PIXEL_ARGB8888, flags)) return 0;
        g_player.surface = true;
        if (!g_player.startup) {
            // retail's surface is a power-of-two texture the picture fills part of: the two words are the picture's share of it, the u and v the game's quad (draw_movie_window,
            // 0x004e6f00) puts on its far corners. The HAL's texture is the picture's size, so the share is all of it.
            retail<std::uint32_t>(kMovieTextureU) = 0x3f800000u;   // 1.0f
            retail<std::uint32_t>(kMovieTextureV) = 0x3f800000u;
        }
    }
    return 1;
}

void present_locked() {
    if (g_hal.movie == nullptr || !g_player.surface) return;
    // the full screen movies fill the 640x480 render target (retail doubles 320x240 into the primary surface)
    // on a wider picture the 4:3 movie sits in the middle of the render target (the whole width when the picture is stretched)
    const Display& screen = display();
    const std::int32_t width = screen.stretch ? screen.width : 640;
    const OtHalRect target = {(screen.width - width) / 2, 0, width, 480};
    g_hal.movie->present(g_player.startup ? &target : nullptr);
}

// 0x004e6890
std::uint32_t next_frame() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_player.source == nullptr) return 0;
    if (!g_player.music && g_player.source->info().has_video) {
        hal::VideoFrame picture;
        if (g_player.source->next_video(picture) && g_hal.movie != nullptr && g_player.surface) {
            OtHalMovieFrame target = {};
            if (g_hal.movie->lock_frame(&target)) {
                hal::convert_to_pixels(picture, target.format, target.pixels, target.pitch);
                g_hal.movie->unlock_frame();
            }
            // texture mode has no DrawFullScreenMovie: the picture is shown where it was decoded
            if (g_player.startup && retail<std::uint32_t>(kControlMode) != 0) present_locked();
        }
    }
    if (g_player.frame >= g_player.frames) {
        if (!g_player.loop) return 0;
        g_player.source->rewind();
        g_player.frame = 1;
        g_player.started_us = clock_us();
        return 1;
    }
    ++g_player.frame;
    return 1;
}

// BinkWait: non-zero while the next frame is not due. A loop that spins on it (the startup movies) has no frame tick to move the clock,
// so `spin` moves it on by a millisecond, as the sleep of a real wait would.
std::uint32_t wait(bool spin) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_player.source == nullptr || frame_due(g_player)) return 0;
    if (spin) standalone_advance_clock(1000);
    return 1;
}

void service() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_player.source != nullptr) feed_audio_locked(g_player);
}

// the music thread (0x004e71d0): feed the sound until the file ends or the stop flag is set, then clear the flags
void music_thread() {
    movie_log("music thread starts (stop flag %u)", static_cast<unsigned>(retail<std::uint8_t>(kThreadFlag)));
    while (retail<std::uint8_t>(kThreadFlag) == 0) {
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (g_player.source == nullptr) {
                retail<std::uint8_t>(kThreadFlag) = 1;
            } else {
                feed_audio_locked(g_player);
                const bool drained = g_player.stream == 0 || g_hal.audio == nullptr || g_hal.audio->stream_queued(g_player.stream) == 0;
                if (g_player.audio_ended && drained) retail<std::uint8_t>(kThreadFlag) = 1;
            }
        }
        Sleep(3);
    }
    if (retail<std::uint8_t>(kMovieStopping) == 0) {
        std::lock_guard<std::mutex> lock(g_lock);
        close_locked();
        retail<std::uint32_t>(kMovieWindow) = 0;
    }
    retail<std::uint8_t>(kThreadFlag) = 0;
    retail<std::uint8_t>(kMoviePlaying) = 0;
    g_thread_running = false;
    movie_log("music thread ends");
}

std::uint32_t seam_start(const Args& args, const std::string& text) { return start_movie(args, text); }
std::uint32_t seam_create_surface(const Args&, const std::string&) { return create_surface(); }
std::uint32_t seam_next_frame(const Args&, const std::string&) { return next_frame(); }

std::uint32_t seam_close(const Args&, const std::string&) {
    std::lock_guard<std::mutex> lock(g_lock);
    close_locked();
    return 0;
}

// 0x004e6e50: a clip played a frame per game frame; the end stops it through the game's own stop function
std::uint32_t seam_update(const Args&, const std::string&) {
    if (wait(false) != 0) return 0;
    if (next_frame() != 0) return 0;
    call_native_or_seam(0x004e7140, {});  // PCMOVIE_StopFMV
    return 0;
}

// The full screen path of PCMovie_PlayStartupMedia (control mode 0: a movie the game plays from a level, the bails video after a medal) ends
// as retail's does, with D3DDevice->SetTexture(0, NULL) (vtable slot 0x23) on the IDirect3DDevice7 at 0x029da3c0. A standalone program has no
// Direct3D: WINMAIN_InitDirect3D never ran, the word is 0, and the call read the vtable through a null pointer (GitHub report, v0.7.1: a crash
// at the end of the bails video). The first such movie gets a stand-in device, as the 32-bit HAL has one (video_hal.cpp): a guest object whose
// vtable answers D3D_OK in every slot. It is made here and not at boot so the guest heap of every other path stays as it was.
constexpr std::uint32_t kD3DDevice = 0x029da3c0;
constexpr std::uint32_t kDeviceSlots = 49;                      // IDirect3DDevice7's methods
constexpr std::uint32_t kStandInDeviceMethod = 0x0000d3d7;      // the slots' target: below the null guard, so never code; a seam (kEntries)

std::uint32_t seam_stand_in_device_method(const Args&, const std::string&) { return 0; }  // D3D_OK

void ensure_device() {
    if (retail<std::uint32_t>(kD3DDevice) != 0) return;
    const std::uint32_t object = arena::heap_allocate(4 + 4 * kDeviceSlots);
    if (object == 0) return;
    retail<std::uint32_t>(object) = object + 4;
    for (std::uint32_t slot = 0; slot < kDeviceSlots; ++slot) {
        retail<std::uint32_t>(object + 4 + 4 * slot) = kStandInDeviceMethod;
    }
    retail<std::uint32_t>(kD3DDevice) = object;
    movie_log("full screen movie: a stand-in Direct3D device at %08x", static_cast<unsigned>(object));
}

std::uint32_t seam_setup_full_screen(const Args&, const std::string&) {  // the HAL owns the screen set-up
    ensure_device();
    return 0;
}

std::uint32_t seam_draw_full_screen(const Args&, const std::string&) {
    std::lock_guard<std::mutex> lock(g_lock);
    present_locked();
    return 0;
}

// _beginthread(entry, stack, argument): the one thread the natives start is the music thread
std::uint32_t seam_begin_thread(const Args& args, const std::string&) {
    if (args.empty() || args[0] != seam::bink::PCMOVIE_StreamThread) return 0;
    if (g_thread_running.exchange(true)) {
        movie_log("begin thread: the music thread is still running, none started");
        return 0;
    }
    std::thread(music_thread).detach();
    return 1;
}

// PCMOVIE_StreamThread (0x004e71d0) is the entry _beginthread is given; a program that calls it directly runs the music loop on the calling thread until it is told to end
std::uint32_t seam_stream_thread(const Args&, const std::string&) {
    music_thread();
    return 0;
}

struct Entry {
    std::uint32_t address;
    std::uint32_t (*implementation)(const Args&, const std::string&);
};

const Entry kEntries[] = {
    {seam::bink::SetupFullScreenMovie, &seam_setup_full_screen},
    {seam::bink::DrawFullScreenMovie, &seam_draw_full_screen},
    {seam::bink::CreateBinkSurface, &seam_create_surface},
    {seam::bink::StartBinkMovie, &seam_start},
    {seam::bink::NextBinkFrame, &seam_next_frame},
    {seam::bink::CloseBinkMovie, &seam_close},
    {seam::bink::BinkMovieUpdate, &seam_update},
    {seam::bink::PCMOVIE_StreamThread, &seam_stream_thread},
    {seam::crt::CRT_beginthread, &seam_begin_thread},
    {kStandInDeviceMethod, &seam_stand_in_device_method},
};

}  // namespace

// The texture of the open in-game clip (the level select's video box), current, for the draw path to put on the quad the game queued; 0 when no such clip is open
std::uint32_t movie_overlay_texture() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_hal.movie == nullptr || g_hal.movie->texture == nullptr || !g_player.surface || g_player.startup || g_player.music) return 0;
    return g_hal.movie->texture();
}

bool has_movie_seam(std::uint32_t address) {
    for (const Entry& entry : kEntries) {
        if (entry.address == address) {
            return true;
        }
    }
    return false;
}

bool call_movie_seam(std::uint32_t address, const Args& args, const std::string& text, std::uint32_t& result) {
    for (const Entry& entry : kEntries) {
        if (entry.address == address) {
            result = entry.implementation(args, text);
            return true;
        }
    }
    return false;
}

// The Bink imports and the few Win32 calls of the movie natives, reported by name (`win32_BinkWait`): see the file comment.
bool call_movie_service(const std::string& name, const Args& args, std::uint32_t& result) {
    result = 0;
    if (name == "win32_BinkWait") {
        result = wait(true);
    } else if (name == "win32_BinkService") {
        service();
    } else if (name == "win32_BinkPause") {
        std::lock_guard<std::mutex> lock(g_lock);
        movie_log("pause %u (stream %u)", args.size() > 1 ? static_cast<unsigned>(args[1] & 0xff) : 0u, static_cast<unsigned>(g_player.stream));
        if (g_player.stream != 0 && g_hal.audio != nullptr && args.size() > 1) g_hal.audio->stream_pause(g_player.stream, (args[1] & 0xff) != 0);
    } else if (name == "win32_BinkSetVolume") {
        std::lock_guard<std::mutex> lock(g_lock);
        movie_log("volume %d (stream %u)", args.size() > 1 ? static_cast<int>(args[1]) : -1, static_cast<unsigned>(g_player.stream));
        if (g_player.stream != 0 && g_hal.audio != nullptr && args.size() > 1) {
            g_player.bink_volume = static_cast<std::int32_t>(args[1]);
            apply_stream_volume_locked(g_player);
        }
    } else if (name == "win32_BinkSetPan") {
        // centred: the HAL streams are not panned
    } else if (name == "win32_Sleep") {
        Sleep(args.empty() ? 0 : args[0]);
        result = retail<std::uint8_t>(kMoviePlaying);  // the natives store the answer in the playing flag: the thread's progress
    } else if (name == "win32_GetCursorPos") {
        // the HAL's mouse (standalone_platform.cpp). Until the pointer has been used it rests where X puts it on a new display, its centre, which is where it is in the
        // retail sessions (a 640 x 480 Xvfb display; the main-menu image's pointer is 256, 120 in the 512 x 240 menu space)
        if (!args.empty()) {
            platform_cursor(args[0]);
        }
        result = 1;
    } else if (name == "win32_PeekMessageA" || name == "win32_TranslateMessage" || name == "win32_DispatchMessageA") {
        result = 0;  // no window messages: the HAL window delivers events through the input service
    } else {
        return false;
    }
    return true;
}

}  // namespace opentony::kitdll

extern "C" __declspec(dllexport) void __cdecl ot_standalone_set_movie_hal(const OtHalMovieBackend* movie, const OtHalAudioBackend* audio) {
    opentony::kitdll::g_hal.movie = movie;
    opentony::kitdll::g_hal.audio = audio;
}
