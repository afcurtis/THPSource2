// THPSource2 launcher settings: PARTYMOD's partymod.ini schema ([Window], [Graphics], [Miscellaneous], [Gamepad] MovementStick; ported from PARTYMOD's
// config/main.c, MIT licence, see THIRD_PARTY_NOTICES.md) plus the launcher's own [Launcher] section. A partymod.ini can be read as it is.
//
// The key and pad bindings are not kept here: the game reads them from THawk2.ini ([KEYBOARD], [JOYPAD]) and its own Options menu writes them there, so that
// file stays the one place they live (bindings.hpp). PARTYMOD's [Keybinds] and [Gamepad] sections are read only to import them.
#pragma once

#include "ini.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace thpsource2 {

constexpr int kDetailMaxDistance = 1000;   // Settings::detail_distance at its most: OT_DETAIL_DISTANCE=max
constexpr int kFogOffDistance = 1000;      // Settings::fog_distance with no fog: OT_FOG_DISTANCE=off

enum class WindowMode { Windowed = 0, Borderless = 1, Fullscreen = 2 };
enum class Theme { Dark = 0, Light = 1, System = 2 };   // [Launcher] Theme = dark | light | system (system follows the OS's light or dark setting)
enum class MovementStick { None = -1, Left = 0, Right = 1 };   // PARTYMOD's controllerStick values

// A hotkey the host watches while the game runs ([Hotkeys] of THPSource2.ini; OT_HOTKEY_<NAME> = "key,mods,pad,hold", standalone_platform.cpp): a keyboard key and / or a
// set of controller buttons held together (a combination), either of which fires it. Not the game's own bindings: these are not in [KEYBOARD] / [JOYPAD].
enum class Hotkey { Screenshot = 0, Retry, Quit, Fullscreen };
constexpr std::size_t kHotkeyCount = 4;
constexpr std::uint32_t kHotkeyCtrl = 1, kHotkeyAlt = 2, kHotkeyShift = 4;   // Binding::mods
constexpr int kHotkeyPadBits = 17;   // the pad mask's bits: SDL's gamepad button numbers 0..14, then 15 the left trigger and 16 the right (OtHalGamepadButton and the standalone's triggers)
struct HotkeyBinding {
    std::uint8_t key = 0;        // the game's key code (DirectInput; 0 = none), the codes of [KEYBOARD]
    std::uint32_t mods = 0;      // kHotkeyCtrl | kHotkeyAlt | kHotkeyShift, held with the key
    std::uint32_t pad = 0;       // controller buttons that must all be held (bit mask, kHotkeyPadBits wide; 0 = none)
    int hold_ms = 0;             // how long the key or the buttons must stay down (0 = on the press, at most 10000)
    bool operator==(const HotkeyBinding&) const = default;
    bool bound() const { return key != 0 || pad != 0; }
};
struct HotkeyInfo {
    const char* label;     // the name the launcher shows
    const char* ini_name;  // the prefix of its [Hotkeys] entries
    const char* env_name;  // OT_HOTKEY_<this>
    const char* note;      // what it does
};
extern const std::array<HotkeyInfo, kHotkeyCount> kHotkeys;
HotkeyBinding default_hotkey(Hotkey which);
// The name of one controller button of the pad mask (0..16) and of a whole combination ("Back + Start", "None")
const char* hotkey_pad_bit_name(int bit);
std::string hotkey_pad_name(std::uint32_t mask);
// The value of OT_HOTKEY_<NAME>
std::string hotkey_env_value(const HotkeyBinding& binding);

inline constexpr int kHudSizeMin = 50;
inline constexpr int kHudSizeMax = 150;
inline constexpr int kHudMarginMax = 10;

// The HUD size (percent) Auto gives a picture  lines tall (0 = not known: 1080). The game lays its 2D layer out as a part of the picture, so it is the same
// fraction of the screen at 1080p and at 4K, and on a big display at a normal distance the same fraction is too large: up to 1080 lines it is the game's, beyond that it shrinks
// with the square root of the height (1440 lines 87%, 2160 lines 70%), no smaller than 70%.
int auto_hud_size(int picture_height);

struct Settings {
    // [Window] (PARTYMOD). ResolutionX/Y 0 = automatic: the largest whole multiple of 640 x 480 for a window, the desktop's size for fullscreen.
    int resolution_x = 0;
    int resolution_y = 0;
    bool windowed = false;     // fullscreen, as PARTYMOD's default and the first run of a new file
    bool borderless = false;
    int frame_rate = 60;       // OpenTony: 30 (the game's), 60 or 120; reaches the game as OT_FRAME_RATE (empty at 30)
    int vsync = 0;            // OpenTony: -1 = the driver's default, 0 = off, 1 = on

    // [Window] (THPSource2): the picture's shape. 0 follows the window or display, 1 the original 4:3, then widescreen shapes (a 5 from an older file, the 4:3 picture
    // stretched to fill, is read as 0: the launcher no longer offers it; OT_ASPECT=stretch still works by hand)
    int aspect = 0;
    bool fov_adjust = true;        // widen the view with the picture (Hor+, the height is kept); off: the 4:3 view's field of view for every shape
    bool hud_anchor = true;        // in a level, a wide picture's HUD goes to the picture's edges (score and special meter left, switch icon right) instead of the 4:3 middle; OT_HUD_ANCHOR
    int hud_size = 0;              // the in-level HUD's size: 0 Auto (from the picture's height, auto_hud_size), else a percent, kHudSizeMin..kHudSizeMax; OT_HUD_SCALE
    int hud_margin = 0;            // percent (0..kHudMarginMax) of the picture kept clear at the edges the HUD is anchored to, for a TV that overscans; OT_HUD_MARGIN. Not on a
                                   // launcher page: set [Window] HudMargin in the file by hand

    // [Graphics] (PARTYMOD, with THPSource2's additions). A new settings file gets the Enhanced preset (apply_preset 0); PARTYMOD's own are the ones it ships.
    bool use_psx_textures = false; // the PlayStation textures of the level files, not the PC port's replacement bitmaps (PARTYMOD: on)
    bool psx_colour = true;        // PlayStation-style colour (texel x colour x 2) for the PlayStation's own textures, not the PC port's gamma curve
    int fog_distance = kFogOffDistance;   // 100 PSX, 150 PC, 275 Dreamcast, 500 max, kFogOffDistance none
    int detail_distance = kDetailMaxDistance;   // how far objects keep their detailed model and stay drawn, percent of the game's (100 its own, 200, 400, kDetailMaxDistance the most); OT_DETAIL_DISTANCE
    int texture_filter = 1;        // 0 sharp, 1 smooth
    int anisotropy = 16;           // texture detail for the 3D world's smooth textures: 0 off, 1 mip maps (trilinear), 2, 4, 8 or 16 taps; unused with sharp textures
    bool sharp_hud = true;         // with smooth textures: menus, fonts and the HUD keep whole texels instead of being blurred when magnified; unused with sharp textures
    bool true_colour = true;       // the PC port's 24-bit bitmaps are drawn in all 24 bits, not cut to 15 (Data\TrueColour, made from the unpacked game files); unused with PSX textures
    bool upscaled_textures = false; // the upscaled set (Data\Upscaled, made by "Upscale textures...") is drawn; needs smooth textures with texture detail (mip maps)
    int internal_resolution = 2;   // 0 PSX (512 x 240), 1 PC (640 x 480), 2 match the window (a file from before the list was cut to three, with 3 or 4, reads as 2)
    int msaa = 4;                  // 0 off, 2, 4 or 8 samples
    bool smooth_cutouts = true;    // with multisampling: fences and leaves (cut-out textures) get smooth edges (alpha-to-coverage)
    int supersample = 200;         // x100: 100 off, 150, 200, 300, 400 times the internal resolution's size; PSX and PC ignore it (the backend lowers a factor the GPU cannot hold)
    int scaling = 0;               // how the picture is scaled to the window: 0 smooth, 1 sharp (whole pixels, soft edges), 2 nearest, 3 CRT (scanlines, soft columns, a faint mask); only at the PSX and PC render resolutions (Native always sends smooth)
    bool crt_curve = false;        // the picture bent as on a curved tube, with rounded corners; with any output and any render resolution. On in Original PlayStation only
    bool psx_dither = false;       // the PlayStation's 4 x 4 dither and 15-bit colour over the finished picture; only offered at the PSX render resolution, off in every preset (G7); not part of a preset's match
    int ambient_occlusion = 2;     // soft darkening where surfaces meet (0 off, 1 low, 2 high): High in Enhanced, off in the originals; not part of a preset's match. A launcher file from before the option (no key) reads 0

    // [Gamepad] (PARTYMOD)
    MovementStick movement_stick = MovementStick::Left;
    // the camera stick: the stick that does not move the skater swings the camera round the skater and springs back (src/runtime/free_look.hpp)
    bool camera_stick = true;      // on for a file without the key: the stick did nothing before
    int camera_mode = 0;           // 0 spring back, 1 stay (not offered until the game has it)
    int camera_sensitivity = 5;    // 1 to 9 (5 is the middle of the slider and the game's base reach)
    bool camera_invert_x = false;
    bool camera_invert_y = false;
    // SDL's DirectInput gamepad driver, for older controllers that are neither XInput nor a known HID pad. Off: on some PCs its device scan held the launcher's start
    // and the game's for about 20 s each (2026-11-20; Xbox, PlayStation and Switch pads use the other drivers). The launcher reads it before SDL starts; SDL_JOYSTICK_DIRECTINPUT
    bool directinput_pads = false;

    // [Game] (THPSource2). Autokick and vibration are the game's controller options, set when it starts; 2 leaves them to the game's own menu
    int autokick = 1;              // 0 off, 1 on, 2 leave
    int vibration = 1;
    // (every level and skater has no switch: a quick start opens them itself and cannot write the save, so PLAY keeps the career as it was earned)
    bool unlock_cheats = false;    // every cheat listed as earned (switch them on in Options > Cheats)
    bool unlock_movies = false;    // every movie of the Options > Movies list playable
    int default_camera = 0;        // the camera each level starts on: 0 the game's first position, 1 the second (what its view button switches to)
    bool quick_start = false;      // start in quick_level as quick_skater, skipping the menus; never stored: the Quick Start page's buttons set it for one launch
    int quick_mode = 0;            // 0 free skate, 1 single session, 2 goals speed run, 3 competition session (the button that started it)
    int quick_level = 12;          // the game's level numbers: 0 Hangar .. 12 Warehouse
    int quick_skater = 0;          // the game's skater rows 0..19
    int quick_look = 0;            // the skater's style: 0 A, 1 B, 2 C, 3 D (a skater without it keeps the style it has)
    bool max_stats = false;        // quick start only: every skater's stats at 10
    std::string quick_trick_set;   // quick start only: the trick set (Config\TrickSets.ini, trick_sets.hpp) every skater gets; empty: each skater's own tricks
    std::string quick_tricks;      // the chosen set as the game takes it (106 hex digits, trick_block_hex); never stored: the launch sets it from the set
    bool speed_run_cash = false;   // the Goals Speed Run ends at all ten goals, the 100% one (every cash icon) too; off: at the other nine
    bool show_fps = false;
    bool hide_hud = false;         // the in-level HUD is not drawn (screenshots and video)
    bool grind_meter = false;      // a balance bar is drawn while a skater grinds
    Theme theme = Theme::System;   // the launcher's own colours (System > Appearance): following the system (the default; dark when the system cannot say), dark or light. [Launcher] Theme
    std::string initials;          // score initials: exactly three of A to Z or a space, or empty (leave the game's own); every score entry starts from them
    // [Sound] (THPSource2): the game's own Options > Sound levels, 0..10 as it shows them; the game starts with these every launch, whatever it was left at
    int music_level = 5;           // the game's stock music level
    int sfx_level = 10;            // and its stock effects level
    // the custom soundtrack (src/hooks/kits/standalone_music.cpp): the player's own Bink files in <install>\Mods\Music play in levels in place of the game's songs
    bool custom_music = false;
    bool music_shuffle = false;           // a random order, a new one each time round; off: the folders and files in name order
    bool music_include_default = false;   // the game's own songs are in the playlist too
    std::string music_folders_off;        // the packs switched off: folder names relative to Mods\Music ("." its own files), '|' between them
    bool music_now_playing = true;        // [Sound] MusicNowPlaying, a HUD setting: a new song's band and title come up at the bottom left, the game's own songs too (Enhanced)
    // [Hotkeys] (THPSource2): indexed by Hotkey. Screenshot F8; Retry F5; Quit to the launcher L3 + R3 held a second; Fullscreen Alt+Enter
    std::array<HotkeyBinding, kHotkeyCount> hotkeys = {default_hotkey(Hotkey::Screenshot), default_hotkey(Hotkey::Retry), default_hotkey(Hotkey::Quit), default_hotkey(Hotkey::Fullscreen)};
    bool pause_on_focus_loss = true;    // [Hotkeys] PauseOnFocusLoss: the pause menu comes up when the window loses the focus (in a level)

    // [Launcher] (THPSource2)
    std::string game_dir;          // UTF-8; empty until the setup has found the game's files
    bool play_movies = false;      // opentony.exe --movies (off in every preset)
    bool debug_mode = false;       // [Launcher] DebugMode: every run is logged and kept in full (input log, texture dump, run history); off, only the last session is kept and a crash asks what happened
    bool check_updates = true;     // [Launcher] CheckForUpdates: when the launcher opens it asks GitHub for the newest release (core/update_check.hpp); off, it never touches the network
    std::string skipped_update;    // [Launcher] SkippedUpdate: the release tag ("v0.8") whose notice the player dismissed; a newer release shows its notice again

    WindowMode window_mode() const;
    void set_window_mode(WindowMode mode);
};

Settings default_settings();
// Score initials as the game takes them: upper-cased, exactly three characters each A to Z or a space; anything else is empty (a value in quotes, as the settings file
// stores it so that a trailing space survives, is unquoted first)
std::string normalize_initials(std::string_view text);
// The presets (the launcher footer's Presets list): one set of the options that make the game's look and feel. The Display tab's picture shape, field of view, HUD
// layout and size, balance meter, song title and frame rate; every Graphics option; the camera stick, autokick, vibration, starting camera and intro movies. The rest is
// the person's and their machine's and no preset touches it: the window's mode and size, V-Sync, the HUD's visibility, the FPS counter, sound, unlocks, initials,
// controls, hotkeys, trick sets.
// 0 Enhanced (everything at its most at 60 fps, with the upscaled textures once they exist), 1 Original PC (the game as it ran), 2 Original PlayStation (the console on a TV:
// 512 x 240, its textures, fog and dither, the CRT). A new settings file is Enhanced (default_settings() holds its values, with no upscaled set).
constexpr int kPresetCount = 3;
extern const std::array<const char*, kPresetCount> kPresetNames;
// upscaled_available: Data\Upscaled holds a set, which Enhanced turns on
void apply_preset(Settings& settings, int preset, bool upscaled_available = false);
// The built-in preset whose options `settings` holds, or -1
int preset_of(const Settings& settings, bool upscaled_available = false);
// The preset options from one Settings onto another, and whether two hold the same (the fog compared by the option it shows)
void copy_preset_options(const Settings& from, Settings& to);
bool same_preset_options(const Settings& a, const Settings& b);
// A preset the person saved and named (Config\Presets.ini, one section a preset with THPSource2.ini's keys for the preset options); only its preset options mean anything
struct CustomPreset {
    std::string name;
    Settings values;
};
constexpr std::size_t kPresetNameMax = 32;
// 1..kPresetNameMax characters, no surrounding spaces, no brackets, not a built-in's name (in any case)
bool preset_name_valid(std::string_view name);
std::vector<CustomPreset> read_presets(const IniFile& ini);
IniFile write_presets(const std::vector<CustomPreset>& presets);
Settings read_settings(const IniFile& ini);
void write_settings(IniFile& ini, const Settings& settings);

// The option lists of PARTYMOD's configurator (fog distance values, internal resolution, texture filter)
struct FogOption {
    const char* label;
    int distance;
};
extern const std::array<FogOption, 5> kFogOptions;
extern const std::array<const char*, 4> kDetailOptions;   // The game, 2x, 4x, Maximum, in the order of kDetailValues
extern const std::array<int, 4> kDetailValues;            // Settings::detail_distance for each
int detail_option_index(int value);
int fog_option_index(int distance);   // PARTYMOD's update_general_page: the first option at or above the distance
extern const std::array<const char*, 3> kInternalResolutionOptions;
extern const std::array<const char*, 2> kTextureFilterOptions;
extern const std::array<const char*, 6> kAnisotropyOptions;   // Off, Mip maps, 2x .. 16x, in the order of kAnisotropyValues
extern const std::array<int, 6> kAnisotropyValues;            // Settings::anisotropy for each
int anisotropy_option_index(int value);
extern const std::array<const char*, 5> kSupersampleOptions;  // Off, 1.5x, 2x, 3x, 4x, in the order of kSupersampleValues
extern const std::array<int, 5> kSupersampleValues;           // Settings::supersample for each
int supersample_option_index(int value);
extern const std::array<const char*, 5> kAspectOptions;
extern const std::array<const char*, 4> kScalingOptions;
extern const std::array<const char*, 3> kAmbientOcclusionOptions;
extern const std::array<const char*, 3> kSwitchOptions;   // Off, On, Leave to the game's menu, in the order of Settings::autokick's values 0, 1, 2
extern const std::array<const char*, 13> kLevelNames;
bool skater_is_custom(int skater);      // rows 13..16, the four custom skaters (placeholder models until the player makes one)
// The Quick Start's Goals Speed Run takes a level with ten goals of its own: Hangar, School II, New York City, Venice Beach, Philadelphia (not the three
// competitions, not Chopper Drop and Skatepark Heaven, which have none, nor Downhill Jam, Skate Park and Warehouse, whose rows are Hangar's copied and whose
// items the game never shows). The game's DLL has the same list (standalone_challenge.cpp speed_run_level).
bool speed_run_level(int level);
int skater_style_count(int skater);     // styles the row has in retail's table (THawk2.exe): 2 (A, B), Tony 3 (C opens with "All skaters available"), Spider-Man 4
extern const std::array<const char*, 20> kSkaterNames;   // the game's skater rows, as the skater select lists them

// PARTYMOD's saveSettings: a set resolution is at least 640 x 480
void clamp_resolution(Settings& settings);

// True when the frame rate cannot be shown evenly: vertical sync is not off (on, or the driver's default, usually on) and the display's refresh rate (Hz, 0 = unknown) is not a
// whole multiple of it, so some frames stay up for one more refresh than others (120 on 144 Hz: 24 a second; 60 on 144 Hz too). A variable-refresh display does not judder, but
// the launcher cannot tell one from the refresh rate.
bool frame_rate_judders(int frame_rate, int vsync, float refresh_hz);

// The environment opentony.exe passes on to the game (docs/LAUNCHER.md, "What the game reads"). Every name the launcher owns is in the list, an empty value
// meaning "unset it", so a value of an earlier run never leaks into the next. `screenshots_dir` (UTF-8) is where F8 writes (always passed on).
//  (lines, 0 unknown) is the primary display's: the picture's height when the settings do not give a window size, for the HUD's Auto size.
// music_dir: <install>\Mods\Music, passed on when the custom soundtrack is on
// scores_file: <install>\Saves\highscores.tsv (src/launcher/scores.hpp), where the game appends every finished run; empty records nothing
// texture_pack_dir: <install>\Data\Upscaled when a set exists there (empty otherwise); OT_TEXTURE_PACK is it when "Upscaled textures" is on and usable
// true_colour_dir: <install>\Data\TrueColour when its set exists (empty otherwise); named after the upscaled folder in OT_TEXTURE_PACK ('|' between) when "True colour" is usable
std::vector<std::pair<std::string, std::string>> launch_environment(const Settings& settings, const std::string& screenshots_dir, int display_height = 0,
                                                                  const std::string& music_dir = "", const std::string& scores_file = "",
                                                                  const std::string& texture_pack_dir = "", const std::string& true_colour_dir = "");

// "Upscaled textures" takes effect: ticked, with smooth textures and texture detail on (mip maps)
// "True-colour textures" takes effect: ticked, and the PC port's bitmaps are the ones in use (the PlayStation textures are 15-bit at source)
inline bool true_colour_usable(const Settings& s) { return s.true_colour && !s.use_psx_textures; }

inline bool upscaled_textures_usable(const Settings& s) { return s.upscaled_textures && s.texture_filter == 1 && s.anisotropy != 0; }

}  // namespace thpsource2
