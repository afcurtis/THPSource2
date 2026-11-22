// The custom soundtrack of the standalone (docs/LAUNCHER.md, "Custom soundtrack"; THPS2 docs/post_phase_5/fableideas.md, idea 9). Host-side only: no ported function
// changes. The game keeps choosing its tracks as it always does (redbook_next, 0x004abcb0: the pause menu's TRACK line, a song's end, a level's start); when it asks
// the music seam for one of the level songs (LTIX16..LTIX31) during a level, the seam plays the playlist's next file in its place. Menus, the credits and the
// level's own ambience (LTIX00..09, the music with the CD music level at 0) keep the game's files. With OT_CUSTOM_MUSIC unset the game's songs play as they
// always do; the one thing that runs is the song's name (OT_MUSIC_NOW_PLAYING), shown for the game's own songs from a built-in track list too.
#pragma once

#include <cstdint>
#include <string>

namespace opentony::kitdll {

struct MusicChoice {
    bool replaced = false;   // false: the game's own file
    bool disc = false;       // `file` is one of the game's music files by name (LTIX20.DAT), to be found where the game's are; else a full path
    std::string file;
};

// StartBinkMovie(name, music = 1, ...): what to open instead of the game's music file `name` (standalone_movie.cpp)
MusicChoice music_choice(const std::string& name);

// The player's file music_choice gave last could not be opened or decoded (not a Bink file FFmpeg reads: a damaged file, Bink 2): it is passed over from now on,
// and with every one of the player's songs failed the game's own music plays (standalone_movie.cpp, when the open fails)
void music_open_failed(const std::string& file);

// The level loop (Game_LevelLoop, 0x0046a3a0) is running: the resolver marks its call (kit_dll.cpp)
void music_level_running(bool running);
constexpr std::uint32_t kMusicLevelLoop = 0x0046a3a0;
struct MusicLevelScope {
    explicit MusicLevelScope(std::uint32_t address) : on(address == kMusicLevelLoop) {
        if (on) music_level_running(true);
    }
    ~MusicLevelScope() {
        if (on) music_level_running(false);
    }
    MusicLevelScope(const MusicLevelScope&) = delete;
    MusicLevelScope& operator=(const MusicLevelScope&) = delete;
    bool on;
};

// Every input poll: the song's name, once a new song has started, through the game's own message layer (standalone_platform.cpp)
void music_each_poll();

}  // namespace opentony::kitdll
