// The player's options of the standalone (docs/LAUNCHER.md, "What the game reads"): what THPSource2.exe passes in the environment, applied to the game's own
// memory at the places the game itself would set it. Nothing is set and nothing changes when the variables are absent, which is how every scenario and gate runs.
#pragma once

#include <cstdint>
#include <vector>

namespace opentony::kitdll {

// The display the game is told it has. The program's video (standalone_video.cpp) decides it from the window and OT_ASPECT and hands it over before the boot
// (ot_standalone_set_display); the default is retail's 640 x 480.
struct Display {
    std::int32_t width = 640;       // the game's screen words (0x029da394 / 0x029da398): wider than 4:3, the game widens its own view (M3dInit_SetResolution, 0x00464690)
    std::int32_t height = 480;
    std::int32_t hud_width = 640;   // the width the 2D layer (HUD, menus, text) keeps its shape for, centred; equal to `width` when it is stretched with the screen
    bool stretch = false;           // the picture fills the window whatever its shape (only the pointer's mapping needs to know)
};
const Display& display();

// The quick start's modes (OT_QUICK_MODE): unset a free skate; single, speedrun (a Goals Speed Run, standalone_challenge.cpp), competition (a Competition Session)
enum class QuickMode { FreeSkate, Single, SpeedRun, Competition };

struct GameOptions {
    float fog_scale = 0.0f;        // OT_FOG_DISTANCE: > 0 replaces VideoFogYonScale (the file's FOGSCALE percentage / 100 stops at 5; "off" is far beyond every level)
    bool fog_off = false;
    int detail_distance = 100;     // OT_DETAIL_DISTANCE: percent (200, 400 ...) or "max" of the distance at which objects take a rougher model or are dropped (runtime/host_options.hpp); 100 the game's
    bool fov_fixed = false;        // OT_FOV=fixed: no widening of the view on a wide picture (the 4:3 view's horizontal angle for every shape); unset or "adjust": Hor+
    bool psx_textures = false;     // OT_PSX_TEXTURES=1: the PlayStation textures of the level files, not the PC port's replacement bitmaps
    bool psx_colour = false;       // OT_PSX_COLOR=1: a texture that is the PlayStation's own is drawn the PlayStation's way, texel x colour x 2 (drawkit::Presentation)
    int autokick = -1;             // OT_AUTOKICK 0 | 1; -1 leaves the game's (on)
    int vibration = -1;            // OT_VIBRATION 0 | 1
    bool unlock_levels = false;    // OT_UNLOCK: a comma list of levels, skaters, cheats
    bool unlock_skaters = false;
    bool unlock_cheats = false;
    bool unlock_decks = false;     // OT_UNLOCK has decks: every character owns every deck of its Skate Shop set (the texture census reaches every deck picture)
    bool unlock_movies = false;    // OT_UNLOCK has movies: the Options > Movies list plays every movie (runtime/host_options.hpp)
    int camera = 0;                // OT_CAMERA=2: each level starts on the game's second camera position (runtime/host_options.hpp)
    int music_level = -1;          // OT_MUSIC_LEVEL / OT_SFX_LEVEL: the Options > Sound levels (0..10 as the menu shows them) the game starts with, whatever a save holds; -1 (unset) the game's
    int sfx_level = -1;
    int trick_tips = -1;           // OT_TRICK_TIPS 0 | 1: Options > Display Options > Trick Tips at the start of every launch, whatever a save holds; -1 (unset) the game's (on)
    int quick_level = -1;          // OT_QUICK_START=<level>[,<skater>]: straight into a free skate (OT_QUICK_MODE: another mode) of that level
    int quick_skater = -1;
    QuickMode quick_mode = QuickMode::FreeSkate;   // OT_QUICK_MODE=single | speedrun | competition
    bool speed_run_cash = false;   // OT_SPEED_RUN_CASH=1: the speed run ends at all ten goals, the 100% goal (every cash icon) too; unset at the other nine
    int quick_look = -1;           // OT_QUICK_LOOK=0..3: the skater's style A to D; -1 the game's
    int frame_rate = 30;           // OT_FRAME_RATE: 30 (the game's, unset), 60 (F1: levels; the limiter waits one tick, the clock is exact in realtime) or 120 (F2: half a tick, the sub-tick clock)
    bool max_stats = false;       // OT_MAX_STATS=1: every skater's ten stats at 10 when the quick start launches its level
    bool has_quick_tricks = false;   // OT_QUICK_TRICKS=<106 hex digits>: the launcher's trick set, the 0x35-byte binding block every skater gets when the quick start
    std::uint8_t quick_tricks[0x35] = {};   // launches its level (src/launcher/thpsource2/core/trick_sets.hpp); only with OT_QUICK_START
    bool has_initials = false;    // OT_INITIALS=XYZ: the score entry starts from these three letters (A to Z or a space) instead of the game's remembered ones
    char initials[4] = {'A', 'A', 'A', 0};
};
const GameOptions& game_options();

// A movie's sound follows the game's music level (Options > Sound), as an attenuation in millibels added to its HAL volume (audio.h: gain = 10 ^ (mb / 2000))
std::int32_t movie_volume_offset_mb();                    // (standalone_movie.cpp)

void read_freelook_options();   // the free look's switches (standalone_freelook.cpp), read once with the options
void freelook_each_poll(bool connected, const std::int16_t axes[4]);   // every input poll, after the scripted input: player one's look stick (axes: lx ly rx ry)

void options_after_load();      // the boot, after WINMAIN_LoadOptions has read the options file
// The calls that put the options to retail's defaults (Options_Init: the music at half, trick tips on) or to a save's (Career_ApplyLoadedSave): the launcher's go back after them
constexpr bool is_options_reset(std::uint32_t address) { return address == 0x00413f70 || address == 0x004165d0; }
void options_after_options_reset();
void options_after_input();     // the boot, after PCInput_Initialize has reset the controller options
void options_each_poll();       // every input poll: the values a save file or a menu may have put back

// The quick start (OT_QUICK_START): until the level it asks for has been launched, the resolver shows every call here first, as it shows them to a scenario
// (standalone_scenario.cpp). `before` may answer the call itself (true, the answer in `result`); `live` maps a Kit address to where the memory really is.
// After the launch only the calls a Goals Speed Run or a Competition Session follows are shown (options_watch_call).
bool options_watch_calls();
bool options_watch_call(std::uint32_t address);
bool options_before_call(std::uint32_t address, std::vector<std::uint32_t>& args, const char* caller, std::uint32_t (*live)(std::uint32_t), std::uint32_t& result);
void options_before_bindings();   // the poll's bindings are about to read the keys: the rules screen's PLAY is pressed here
void options_after_call(std::uint32_t address, const std::vector<std::uint32_t>& args, std::uint32_t (*live)(std::uint32_t));
// The player's skater (0..19, the select screen's row; -1 keeps the slot's) and look (0..3; a look the row has no model for, or one still locked, is left alone),
// as the select screen would set them: the quick start and a scenario's `skater` / `look` lines call it at the level select. Returns the look the slot holds after it.
int options_choose_skater(int skater, int look);

// The Goals Speed Run and the Competition Session (standalone_challenge.cpp; docs/LAUNCHER.md, "Quick start"): the quick start's two career sessions.
//   challenge_prepare_level   at the quick start's level select, before the level loads: the session's mode and level are set up (the level made a contest for a
//                             competition, the judging and clock options of host_options.hpp)
//   challenge_watches         the resolver call a session follows: Game_PrepareVideoMasksWrapper (0x00469900), which every run's object set-up calls before it reads
//                             which goals and cash the career has
//   challenge_before_call     that call: the career record is made ready for the run (a speed run starts with no goals, a competition with every goal and the cash)
//   challenge_each_poll       every input poll: the speed run's clock and its end; the competition's heats and result
//   challenge_shutdown        the program ends (the quick start's Quit): a result not yet written is
void challenge_prepare_level();
bool challenge_watches(std::uint32_t address);
void challenge_before_call(std::uint32_t address);
void challenge_each_poll();
void challenge_shutdown();
std::uint32_t quick_front_mode();   // the shell's game mode for the quick start's mode: 1 career, 2 free skate, 3 single session
bool speed_run_level(int level);    // the level has goals a speed run can finish: Hangar, School II, New York City, Venice Beach, Philadelphia

// PCInput_ActuatorOn(port, duration, motor, strength) / PCInput_ActuatorOff on the HAL's pad
void options_rumble(std::uint32_t port, std::uint32_t duration, std::uint32_t motor, std::uint32_t strength);
void options_rumble_stop();

}  // namespace opentony::kitdll
