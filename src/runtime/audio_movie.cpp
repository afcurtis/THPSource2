#include "auto_views.hpp"
#include "audio_game.hpp"
#include "seam_addresses_generated.hpp"

#include <bit>
#include <cstdio>

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

namespace {

// Bink and the movie window, which the HAL's movie backend replaces: services named by their retail address (or, for the import slots, by the Win32 / Bink name)
constexpr std::uint32_t kStartBinkMovie = seam::bink::StartBinkMovie;      // (name, music, startup) -> success in AL
constexpr std::uint32_t kCreateBinkSurface = seam::bink::CreateBinkSurface;   // () -> non-zero when the surface exists
constexpr std::uint32_t kCloseBinkMovie = seam::bink::CloseBinkMovie;
constexpr std::uint32_t kBinkMovieUpdate = seam::bink::BinkMovieUpdate;
constexpr std::uint32_t kNextBinkFrame = seam::bink::NextBinkFrame;       // () -> non-zero while there are frames
constexpr std::uint32_t kSetupFullScreenMovie = seam::bink::SetupFullScreenMovie;
constexpr std::uint32_t kDrawFullScreenMovie = seam::bink::DrawFullScreenMovie;
constexpr std::uint32_t kBinkSetUp = seam::hal::Game_FrameTick;           // called after a movie or the music starts
constexpr std::uint32_t kMovieReset = seam::hal::D3D_ClearBuffers;
constexpr std::uint32_t kMusicThread = seam::bink::PCMOVIE_StreamThread;         // the music thread's entry (PCMOVIE_StreamThread)
constexpr std::uint32_t kBeginThread = seam::crt::CRT_beginthread;         // _beginthread(entry, stack, argument)
constexpr std::uint32_t kExit = seam::crt::CRT_exit;
constexpr std::uint32_t kReportError = seam::hal::Debug_ReportError;         // (code, file, line)
constexpr std::uint32_t kErrorHandler = seam::hal::D3D_Shutdown;
constexpr std::uint32_t kUpdateActions = 0x00489a10;       // Input_UpdateActionStates
constexpr std::uint32_t kFirstKeyDown = 0x004e41d0;
constexpr std::uint32_t kFirstJoystickButton = 0x004e4260;
constexpr std::uint32_t kClearPadTriggers = 0x004e4290;

constexpr std::uint32_t kLogStopping = 0x0054b298;
constexpr std::uint32_t kLogStartFmv = 0x0054b278;
constexpr std::uint32_t kLogPlayFmv = 0x0054b254;
constexpr std::uint32_t kLogXaPlay = 0x0054b2cc;
constexpr std::uint32_t kPcMovieFile = 0x0054b198;
constexpr std::uint32_t kControlMode = 0x006a6964;         // non-zero: the windowed set-up (no full screen movie surface)
constexpr std::uint32_t kPrimitiveHead = 0x00560fd0;       // the primitive pool's next free record, and the ordering table
constexpr std::uint32_t kOrderingTable = 0x00560fd4;
constexpr std::uint32_t kMovieTexture = 0x006a6bd8;        // the movie's two texture coordinates
constexpr std::uint32_t kMovieTexture2 = 0x006a6bdc;
constexpr std::uint32_t kDisplaySurface = 0x029da3c0;

constexpr std::uint32_t kMessageQuit = 0x12;
constexpr std::uint32_t kMessageKeyDown = 0x100;

}  // namespace

// 0x00454d30: the next (or a chosen) FMV in the game's movie list; a positive step skips to the next entry of type 0x60 and wraps to the third
void AudioGame::change_fmv_track(std::int32_t delta) {
    if (global_word(m_, audio::kMovieActive) != 0) {
        return;
    }
    constexpr std::uint32_t kRow = 0x0052cfa2;                 // the type word of the first row; every row is 36 bytes
    std::int32_t index;
    if (delta < 0) {
        index = -1 - delta;
    } else {
        index = global_int(m_, audio::kMovieCurrent);
        std::int32_t next = index + 1;
        const std::int32_t count = global_int(m_, audio::kMovieCount);
        set_global_word(m_, audio::kMovieCurrent, static_cast<std::uint32_t>(next));
        if (next < count) {
            while (half_at(m_, kRow + 36u * static_cast<std::uint32_t>(next)) != 0x60) {
                ++next;
                if (next >= count) {
                    break;
                }
            }
            set_global_word(m_, audio::kMovieCurrent, static_cast<std::uint32_t>(next));
        }
        if (next == count) {
            set_global_word(m_, audio::kMovieCurrent, 2);
        }
    }
    const std::uint32_t row = kRow + 36u * static_cast<std::uint32_t>(index);
    set_global_byte(m_, audio::kMovieIndex, static_cast<std::uint32_t>(index));
    set_global_half(m_, 0x00561d34, half_at(m_, row));
    set_global_half(m_, 0x00561dc0, half_at(m_, row + 2));
    set_global_word(m_, 0x00561db4, half_at(m_, row + 4));
    set_global_word(m_, 0x00561d30, word_at(m_, row + 6));
    set_global_word(m_, 0x00561d2c, word_at(m_, row + 10));
    set_global_byte(m_, 0x00561db8, byte_at(m_, row + 14));
    set_global_word(m_, 0x00561cf8, word_at(m_, row + 22));
}

// 0x004e7090: play the current game FMV with the startup media loop
std::uint32_t AudioGame::play_game_fmv() {
    log_line(kLogPlayFmv);
    const std::uint32_t name = movie_name(global_byte(m_, audio::kMovieIndex));
    xa_stop();
    engine(kMovieReset);
    return play_startup_media(name);
}

// 0x004e70e0: start the current game FMV in the background
void AudioGame::start_fmv() {
    log_line(kLogStartFmv);
    const std::uint32_t name = movie_name(global_byte(m_, audio::kMovieIndex));
    if ((engine_value(kStartBinkMovie, {name, 0, 0}, m_.cstring(name, 0x100)) & 0xff) != 0) {
        if (engine_value(kCreateBinkSurface) == 0) {
            engine(kCloseBinkMovie);
        } else {
            engine(kBinkSetUp);
        }
    }
}

// The wait both stop functions share: the music thread is told to end and the caller sleeps until it has
void AudioGame::stop_movie_or_music(bool close_when_idle) {
    if (global_byte(m_, audio::kMoviePlaying) == 0) {
        if (close_when_idle) {
            engine(kCloseBinkMovie);
        }
        return;
    }
    if (global_byte(m_, audio::kMovieStopping) == 0) {
        set_global_byte(m_, audio::kMovieThreadFlag, 1);
        do {
            set_global_byte(m_, audio::kMoviePlaying, win32("Sleep", {10}));          // (the scripted value is what the stub wrote to the flag)
        } while (global_byte(m_, audio::kMoviePlaying) != 0);
        log_line(kLogStopping);
        return;
    }
    engine(kCloseBinkMovie);
    set_global_byte(m_, audio::kMovieStopping, 0);
    set_global_byte(m_, audio::kMoviePlaying, 0);
    set_global_byte(m_, audio::kMovieThreadFlag, 0);
    set_global_word(m_, audio::kMovieWindow, 0);
    log_line(kLogStopping);
}

// 0x004e7140
void AudioGame::stop_fmv() { stop_movie_or_music(true); }

// 0x004e7370
void AudioGame::xa_stop() { stop_movie_or_music(false); }

// 0x004e71c0
void AudioGame::movie_update() {
    if (global_byte(m_, audio::kMoviePlaying) == 0) {
        engine(kBinkMovieUpdate);
    }
}

// 0x004e6e70: pause or resume the movie; the music thread is parked (the caller waits until it stopped) or started again
void AudioGame::movie_pause(std::uint32_t paused) {
    const std::uint32_t handle = global_word(m_, audio::kMovieHandle);
    if (handle == 0) {
        return;
    }
    win32("BinkPause", {handle, paused & 0xff});
    if (global_byte(m_, audio::kMoviePlaying) == 0) {
        return;
    }
    const bool parked = global_byte(m_, audio::kMovieStopping) != 0;
    if ((paused & 0xff) != 0) {
        if (parked) {
            return;
        }
        set_global_byte(m_, audio::kMovieStopping, 1);
        set_global_byte(m_, audio::kMovieThreadFlag, 1);
        do {
            set_global_byte(m_, audio::kMoviePlaying, win32("Sleep", {10}));          // (the scripted value is what the stub wrote to the flag)
        } while (global_byte(m_, audio::kMoviePlaying) != 0);
        set_global_byte(m_, audio::kMoviePlaying, 1);
        return;
    }
    if (!parked) {
        return;
    }
    set_global_byte(m_, audio::kMovieStopping, 0);
    set_global_byte(m_, audio::kMovieThreadFlag, 0);
    engine(kBeginThread, {kMusicThread, 0, 0});
    engine(kBinkSetUp);
}

// 0x004e7260: start streaming a music file; false when the movie interface could not open it
std::uint32_t AudioGame::start_music(std::uint32_t name) { return start_music_text(m_.cstring(name, 0x100), name); }

std::uint32_t AudioGame::start_music_text(const std::string& text, std::uint32_t name_argument) {
    xa_stop();
    engine(kBinkSetUp);
    set_global_byte(m_, audio::kMoviePlaying, 1);
    const std::uint32_t opened = engine_value(kStartBinkMovie, {name_argument, 1, 0}, text) & 0xff;
    if (opened == 0) {
        set_global_byte(m_, audio::kMoviePlaying, 0);
        return 0;
    }
    if (engine_value(kCreateBinkSurface) == 0) {
        engine(kCloseBinkMovie);
        set_global_byte(m_, audio::kMoviePlaying, 0);
        return 0;
    }
    set_global_byte(m_, audio::kMovieThreadFlag, 0);
    engine(kBeginThread, {kMusicThread, 0, 0});
    engine(kBinkSetUp);
    return global_byte(m_, audio::kMoviePlaying);
}

// 0x004e72d0: the music volume of the two sides, sent to Bink as one level ((left + right) * 64) with the pan centred
void AudioGame::set_xa_volume(std::int32_t left, std::int32_t right) {
    const std::uint32_t handle = global_word(m_, audio::kMovieHandle);
    if (handle == 0) {
        return;
    }
    std::int32_t volume = static_cast<std::int32_t>(static_cast<std::uint32_t>(left + right) << 15);
    volume = (volume + ((volume >> 31) & 0x1ff)) >> 9;
    win32("BinkSetVolume", {handle, static_cast<std::uint32_t>(volume)});
    win32("BinkSetPan", {global_word(m_, audio::kMovieHandle), 0x8000});
}

// 0x004e7310: play track `track` of group `group` (file LTIX<group * 8 + track>.DAT)
void AudioGame::xa_play(std::int32_t group, std::int32_t track) {
    const std::int32_t index = static_cast<std::int32_t>(static_cast<std::uint32_t>(track) + static_cast<std::uint32_t>(group) * 8u);
    char number[16];
    std::snprintf(number, sizeof number, "%02i", index);
    const std::string file = m_.cstring(0x0054b2fc, 0x20) + number + m_.cstring(0x0054b230, 0x20);
    log_line(kLogXaPlay);
    start_music_text(file, kStackLocal);
}

// 0x004e6f00: add the movie's quad to the primitive pool: four vertices (the corners of the movie rectangle as floats, 0.5 deep, white), the movie's texture, linked into the ordering table's
// first bucket
void AudioGame::draw_movie_window(std::uint32_t rect) {
    if (global_word(m_, audio::kMovieHandle) == 0 || rect == 0) {
        return;
    }
    const std::uint32_t record = global_word(m_, kPrimitiveHead);
    set_global_word(m_, kPrimitiveHead, record + 0xc0);
    m_.fill(record, 0, 0xc0);
    set_word_at(m_, record + 0x10, 0x006a69f8);
    m_.fill(record + 4, 0xff, 3);
    set_byte_at(m_, record + 7, 0xb0);
    set_word_at(m_, record + 0x14, 4);
    set_word_at(m_, record + 0xc, 0xbf800000);
    // a vertex: the corner (two rectangle fields as floats), depth 0.5, 1.0, white, and the texture coordinates
    auto vertex = [&](std::uint32_t at, std::uint32_t x_field, std::uint32_t y_field, std::uint32_t u, std::uint32_t v) {
        set_word_at(m_, record + at, std::bit_cast<std::uint32_t>(static_cast<float>(short_at(m_, rect + x_field))));
        set_word_at(m_, record + at + 4, std::bit_cast<std::uint32_t>(static_cast<float>(short_at(m_, rect + y_field))));
        set_word_at(m_, record + at + 8, 0x3f000000);
        set_word_at(m_, record + at + 12, 0x3f800000);
        set_word_at(m_, record + at + 16, 0xffffffff);
        set_word_at(m_, record + at + 20, u);
        set_word_at(m_, record + at + 24, v);
    };
    vertex(0x18, 0x08, 0x0a, 0, 0);
    vertex(0x34, 0x10, 0x12, global_word(m_, kMovieTexture), 0);
    vertex(0x50, 0x20, 0x22, global_word(m_, kMovieTexture), global_word(m_, kMovieTexture2));
    vertex(0x6c, 0x18, 0x1a, 0, global_word(m_, kMovieTexture2));
    const std::uint32_t table = global_word(m_, kOrderingTable);
    set_word_at(m_, record, word_at(m_, word_at(m_, table + 0x84)));
    set_word_at(m_, word_at(m_, table + 0x84), record);
}

// 0x004e5ec0: play a startup movie file to its end (or until a key or a joystick button comes after a frame without one, or the window is closed): the pump of messages, the frame wait,
// the full screen draw and the pad
std::uint32_t AudioGame::play_startup_media(std::uint32_t name) {
    if ((engine_value(kStartBinkMovie, {name, 0, 1}, m_.cstring(name, 0x100)) & 0xff) == 0) {
        return 0;
    }
    if (engine_value(kCreateBinkSurface) == 0) {
        engine(kCloseBinkMovie);
        return 0;
    }
    engine(kBinkSetUp);
    bool keep_playing = true;
    bool waiting_for_release = true;           // the byte the retail loop keeps in a local: cleared by a frame without input
    if (global_word(m_, kControlMode) == 0) {
        engine(kSetupFullScreenMovie);
    }
    do {
        // PeekMessageA(&msg, 0, 0, 0, PM_REMOVE): the scripted values are the MSG's first two dwords (the window, the message), then the result
        s_.note("win32_PeekMessageA", {kStackLocal, 0, 0, 0, 1});
        (void)s_.value("win32_PeekMessageA");
        const std::uint32_t message = static_cast<std::uint32_t>(s_.value("win32_PeekMessageA"));
        if (s_.value("win32_PeekMessageA") != 0) {
            if (message == kMessageQuit || message == kMessageKeyDown) {
                keep_playing = false;
            }
            win32("TranslateMessage", {kStackLocal});
            win32("DispatchMessageA", {kStackLocal});
            continue;
        }
        win32("BinkService", {global_word(m_, audio::kMovieHandle)});
        if (win32("BinkWait", {global_word(m_, audio::kMovieHandle)}) != 0) {
            continue;
        }
        if (engine_value(kNextBinkFrame) == 0) {
            keep_playing = false;
        }
        if (global_word(m_, kControlMode) == 0) {
            engine(kDrawFullScreenMovie);
        }
        engine(kUpdateActions);
        if (engine_value(kFirstKeyDown, {0}) == 0 && static_cast<std::int32_t>(engine_value(kFirstJoystickButton)) < 0) {
            waiting_for_release = false;
        } else if (!waiting_for_release) {
            break;
        }
    } while (keep_playing);
    if (global_word(m_, kControlMode) == 0) {
        const std::uint32_t error = virtual_call(global_word(m_, kDisplaySurface), 0x23, {0, 0});
        if (error != 0) {
            engine(kReportError, {error, kPcMovieFile, 0x49b});
            engine(kErrorHandler, {1});
            engine(kExit, {error});
        }
    }
    engine(kCloseBinkMovie);
    engine(kClearPadTriggers);
    return 1;
}

}  // namespace opentony::runtime::pcplatform
