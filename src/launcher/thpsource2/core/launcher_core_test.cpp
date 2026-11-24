// The THPSource2 launcher's portable core (src/launcher/thpsource2/core): INI editing, PARTYMOD's settings schema, the game's binding rules, the disc-image
// reader and the import of a game folder. The retail tables are synthetic here (no game data); OPENTONY_THAWK2_EXE names a real THawk2.exe to check the table reader on it.
#include "launcher/layout.hpp"
#include "launcher/music_library.hpp"
#include "launcher/scores.hpp"
#include "bindings.hpp"
#include "game_files.hpp"
#include "ini.hpp"
#include "iso9660.hpp"
#include "settings.hpp"
#include "trick_sets.hpp"
#include "update_check.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace thpsource2;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(condition)                                                                 \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

void test_ini() {
    IniFile ini = IniFile::parse("; top comment\r\n[VIDEO]\r\nRESOLUTION=640x480\r\n\r\n[keyboard]\r\nJUMP = PAD2,SPACE \r\nDebug=0\t; a comment\r\n");
    CHECK(ini.get_string("Keyboard", "jump") == "PAD2,SPACE");
    CHECK(ini.get_int("KEYBOARD", "Debug", 7) == 0);
    CHECK(ini.get_int("KEYBOARD", "Missing", 7) == 7);
    ini.set("KEYBOARD", "JUMP", "PAD5");
    ini.set("VIDEO", "GAMMA", "0");
    ini.set("New", "Key", "1");
    const std::string text = ini.text();
    CHECK(text.find("; top comment\r\n") == 0);
    CHECK(text.find("JUMP=PAD5\r\n") != std::string::npos);
    CHECK(text.find("RESOLUTION=640x480\r\nGAMMA=0\r\n") != std::string::npos);   // a new key goes after the section's last entry
    CHECK(text.find("Debug=0\t; a comment") != std::string::npos);                 // other lines are kept as they were
    CHECK(text.find("[New]\r\nKey=1\r\n") != std::string::npos);
    CHECK(IniFile::parse(text).get_string("video", "gamma") == "0");
}

// PARTYMOD's shipped partymod.ini (Reference/PartyMod), the part the settings read: the settings layout stays compatible with it
const char* const kPartyModIni =
    "[Window]\nResolutionX=0\nResolutionY=0\nWindowed=0\nBorderless=0\n\n[Graphics]\nUsePSXTextures=1\nFogDistance=150\nTextureFilter=0\nInternalResolution=0\n\n"
    "[Miscellaneous]\nDebug=0\t; enables debug output\n\n[Keybinds]\nOllie=90\nGrab=94\nFlip=92\nGrind=96\n"
    "SpinLeft=89\nSpinRight=91\nNollie=95\nSwitch=97\nPause=19\nUp=26\nDown=22\nLeft=4\nRight=7\nCameraUp=12\nViewToggle=18\n\n[Gamepad]\nPause=6\nViewToggle=4\n"
    "Grind=3\nGrab=1\nOllie=0\nFlip=2\nSpinLeft=9\nSpinRight=10\nNollie=22\nSwitch=21\nUp=11\nDown=12\nRight=14\nLeft=13\nMovementStick=0\n";

void test_settings() {
    const Settings partymod = read_settings(IniFile::parse(kPartyModIni));
    CHECK(!partymod.windowed && !partymod.borderless && partymod.window_mode() == WindowMode::Fullscreen);
    CHECK(!partymod.use_psx_textures && partymod.fog_distance == 150 && partymod.texture_filter == 1 && partymod.internal_resolution == 2);   // no SettingsVersion: PARTYMOD's graphics values are not read as choices
    CHECK(partymod.movement_stick == MovementStick::Left);
    const Settings fresh = read_settings(IniFile{});
    CHECK(!fresh.windowed && fresh.window_mode() == WindowMode::Fullscreen && fresh.vsync == 0 && fresh.frame_rate == 60 && fresh.music_now_playing && fresh.pause_on_focus_loss && fresh.game_dir.empty());

    Settings s;
    s.set_window_mode(WindowMode::Borderless);
    s.resolution_x = 320;   // below 640 x 480: PARTYMOD's saveSettings raises it
    s.resolution_y = 1080;
    s.fog_distance = 275;
    s.internal_resolution = 2;
    s.movement_stick = MovementStick::Right;
    s.game_dir = "C:\\Games\\THPS2";
    s.vsync = 0;
    s.frame_rate = 60;
    IniFile ini;
    write_settings(ini, s);
    const Settings back = read_settings(ini);
    CHECK(back.window_mode() == WindowMode::Borderless && back.resolution_x == 640 && back.resolution_y == 1080);
    CHECK(back.fog_distance == 275 && back.internal_resolution == 2 && back.movement_stick == MovementStick::Right && back.vsync == 0);
    CHECK(back.game_dir == "C:\\Games\\THPS2");
    CHECK(fog_option_index(100) == 0 && fog_option_index(150) == 1 && fog_option_index(200) == 2 && fog_option_index(500) == 3 && fog_option_index(9999) == 4);

    const auto env = launch_environment(back, "");
    auto value = [&env](const char* name) {
        for (const auto& [key, text] : env) {
            if (key == name) {
                return text;
            }
        }
        return std::string("<absent>");
    };
    CHECK(value("OT_WINDOW_MODE") == "borderless");
    CHECK(value("OT_WINDOW_SIZE") == "640x1080");
    CHECK(value("OT_VSYNC") == "0");
    CHECK(back.frame_rate == 60 && value("OT_FRAME_RATE") == "60");
    {   // F2D: 120 round-trips, any other value is 30
        Settings s120 = s;
        s120.frame_rate = 120;
        IniFile ini120;
        write_settings(ini120, s120);
        CHECK(read_settings(ini120).frame_rate == 120);
        const auto env120 = launch_environment(s120, "");
        bool found = false;
        for (const auto& kv : env120) { found = found || (kv.first == "OT_FRAME_RATE" && kv.second == "120"); }
        CHECK(found);
        ini120.set_int("Window", "FrameRate", 144);
        CHECK(read_settings(ini120).frame_rate == 30);
    }
    // the judder warning: vertical sync on or the driver's default, a refresh that is not a whole multiple of the rate; 119.88 counts as 120; off or unknown never warns
    CHECK(frame_rate_judders(120, 1, 144.0f) && frame_rate_judders(120, -1, 144.0f) && frame_rate_judders(60, 1, 144.0f) && frame_rate_judders(120, 1, 60.0f));
    CHECK(!frame_rate_judders(120, 0, 144.0f) && !frame_rate_judders(120, 1, 120.0f) && !frame_rate_judders(120, 1, 119.88f) && !frame_rate_judders(120, 1, 240.0f));
    CHECK(!frame_rate_judders(60, 1, 120.0f) && frame_rate_judders(30, 1, 144.0f) && !frame_rate_judders(30, 1, 60.0f) && !frame_rate_judders(120, 1, 0.0f));
    CHECK(launch_environment(fresh, "").size() > 0);
    CHECK(value("OT_PAD_STICK") == "right");
    {   // the camera stick: on by default (also for a file without the key), the stick that does not move the skater, sensitivity and invert
        const Settings blank = read_settings(IniFile{});
        CHECK(blank.camera_stick && blank.camera_mode == 0 && blank.camera_sensitivity == 5 && !blank.camera_invert_x && !blank.camera_invert_y);
        const auto env_of = [](const Settings& s, const char* name) {
            for (const auto& [key, text] : launch_environment(s, "")) {
                if (key == name) {
                    return text;
                }
            }
            return std::string("<absent>");
        };
        Settings c = default_settings();
        CHECK(env_of(c, "OT_FREELOOK") == "spring" && env_of(c, "OT_FREELOOK_STICK") == "right" && env_of(c, "OT_FREELOOK_SENS") == "5" && env_of(c, "OT_FREELOOK_INVERT").empty());
        c.movement_stick = MovementStick::Right;
        c.camera_sensitivity = 9;
        c.camera_invert_y = true;
        CHECK(env_of(c, "OT_FREELOOK_STICK") == "left" && env_of(c, "OT_FREELOOK_SENS") == "9" && env_of(c, "OT_FREELOOK_INVERT") == "y");
        c.camera_invert_x = true;
        CHECK(env_of(c, "OT_FREELOOK_INVERT") == "xy");
        c.movement_stick = MovementStick::None;
        CHECK(env_of(c, "OT_FREELOOK_STICK") == "right");
        c.camera_mode = 1;
        CHECK(env_of(c, "OT_FREELOOK") == "stay");
        IniFile file;
        write_settings(file, c);
        const Settings back = read_settings(file);
        CHECK(back.camera_stick && back.camera_mode == 1 && back.camera_sensitivity == 9 && back.camera_invert_x && back.camera_invert_y);
        c.camera_stick = false;
        CHECK(env_of(c, "OT_FREELOOK").empty() && env_of(c, "OT_FREELOOK_SENS").empty() && env_of(c, "OT_FREELOOK_INVERT").empty());
    }
    CHECK(value("OT_INTERNAL_RESOLUTION") == "window");
    CHECK(value("OT_WINDOW_SIZE") != "<absent>");
    Settings automatic;
    CHECK(launch_environment(automatic, "")[1].second.empty());   // automatic size: OT_WINDOW_SIZE is unset (opentony.exe's OT_WINDOW_SCALE=auto applies)

    // the enhanced defaults of a new file, and what each reaches the game as
    const Settings enhanced = default_settings();
    CHECK(enhanced.aspect == 0 && enhanced.fov_adjust && !enhanced.use_psx_textures && enhanced.psx_colour && enhanced.fog_distance == kFogOffDistance && enhanced.texture_filter == 1 &&
          enhanced.internal_resolution == 2 && enhanced.msaa == 8 && enhanced.supersample == 400 && preset_of(enhanced) == 0 && enhanced.scaling == 0 && enhanced.autokick == 1 && enhanced.vibration == 1 && !enhanced.quick_start &&
          enhanced.music_now_playing && !enhanced.grind_meter && enhanced.frame_rate == 60 && enhanced.vsync == 0);
    Settings game = enhanced;
    game.aspect = 2;
    game.fog_distance = kFogOffDistance;
    game.internal_resolution = 1;   // PC: the output looks exist at the original sizes only
    game.msaa = 8;
    game.scaling = 1;
    game.autokick = 2;
    game.vibration = 0;
    game.unlock_cheats = game.unlock_movies = true;
    game.default_camera = 1;
    game.music_level = 3;
    game.quick_start = true;
    game.quick_level = 12;
    game.quick_skater = 19;
    game.quick_mode = 1;
    game.quick_look = 3;
    game.show_fps = true;
    game.hide_hud = true;
    game.directinput_pads = true;
    game.grind_meter = true;
    game.game_dir = "C:\\Games\\THPS2";
    const auto genv = launch_environment(game, "C:\\Games\\THPS2\\Screenshots");
    auto gvalue = [&genv](const char* name) {
        for (const auto& [key, text] : genv) {
            if (key == name) {
                return text;
            }
        }
        return std::string("<absent>");
    };
    CHECK(gvalue("OT_ASPECT") == "16:9" && gvalue("OT_HUD") == "<absent>" && gvalue("OT_FOG_DISTANCE") == "off" && gvalue("OT_INTERNAL_RESOLUTION") == "pc");
    CHECK(gvalue("SDL_JOYSTICK_DIRECTINPUT").empty());   // on: unset, SDL's default
    CHECK(gvalue("OT_MSAA") == "8" && gvalue("OT_SCALING") == "sharp" && gvalue("OT_AUTOKICK").empty() && gvalue("OT_VIBRATION") == "0");
    CHECK(gvalue("OT_UNLOCK") == "cheats,movies" && gvalue("OT_QUICK_START") == "12,19" && gvalue("OT_FPS") == "1" && gvalue("OT_PSX_COLOR") == "1");
    CHECK(gvalue("OT_QUICK_MODE") == "single" && gvalue("OT_QUICK_LOOK") == "3" && gvalue("OT_HIDE_HUD") == "1" && gvalue("OT_GRIND_METER") == "1");
    CHECK(gvalue("OT_SCREENSHOTS") == "C:\\Games\\THPS2\\Screenshots");
    CHECK(gvalue("OT_VEHICLE_SOUND_FIX") == "1" && gvalue("OT_RETAIL_FIXES") == "1");
    CHECK(gvalue("OT_TRICK_TIPS") == "0");   // trick tips start off every launch; no setting
    CHECK(gvalue("OT_CAMERA") == "2" && gvalue("OT_MUSIC_LEVEL") == "3" && gvalue("OT_SFX_LEVEL") == "10" && default_settings().music_level == 5);
    CHECK(launch_environment(enhanced, "")[0].first == "OT_WINDOW_MODE" && launch_environment(enhanced, "")[1].first == "OT_WINDOW_SIZE");
    {
        // screenshots are always on: an old file's Screenshots=0 is not read, and the folder is always passed on
        const Settings old = read_settings(IniFile::parse("[Game]\nScreenshots=0\n"));
        int passed = 0;
        for (const auto& [key, text] : launch_environment(old, "C:\\Games\\THPS2\\Screenshots")) {
            if (key == "OT_SCREENSHOTS") {
                passed += text == "C:\\Games\\THPS2\\Screenshots" ? 1 : 100;
            }
        }
        CHECK(passed == 1);
        const auto plain = launch_environment(enhanced, "");
        for (const auto& [key, text] : plain) {
            if (key == "OT_QUICK_START" || key == "OT_QUICK_MODE" || key == "OT_QUICK_LOOK" || key == "OT_UNLOCK" || key == "OT_FPS" || key == "OT_CAMERA" || key == "OT_HIDE_HUD" || key == "OT_GRIND_METER" ||
                key == "OT_SPEED_RUN_CASH") {
                CHECK(text.empty());   // off: unset, so nothing of an earlier run leaks
            }
        }
    }
    // the quick start's four buttons: the mode word, and the speed run's 100% switch only with a speed run
    {
        const auto env_of = [](const Settings& s, const char* name) {
            for (const auto& [key, text] : launch_environment(s, "")) {
                if (key == name) {
                    return text;
                }
            }
            return std::string("<absent>");
        };
        Settings quick = enhanced;
        quick.quick_start = true;
        quick.quick_level = 0;
        quick.speed_run_cash = true;
        quick.quick_mode = 0;
        CHECK(env_of(quick, "OT_QUICK_MODE").empty() && env_of(quick, "OT_SPEED_RUN_CASH").empty());
        quick.quick_mode = 2;
        CHECK(env_of(quick, "OT_QUICK_MODE") == "speedrun" && env_of(quick, "OT_SPEED_RUN_CASH") == "1");
        quick.speed_run_cash = false;
        CHECK(env_of(quick, "OT_SPEED_RUN_CASH").empty());
        quick.quick_mode = 3;
        quick.speed_run_cash = true;
        CHECK(env_of(quick, "OT_QUICK_MODE") == "competition" && env_of(quick, "OT_SPEED_RUN_CASH").empty());
        // the speed run's levels: the five with goals of their own
        int levels = 0;
        for (int level = 0; level < 13; ++level) {
            levels += speed_run_level(level) ? 1 : 0;
        }
        CHECK(levels == 5 && speed_run_level(0) && speed_run_level(1) && speed_run_level(3) && speed_run_level(4) && speed_run_level(6));
        CHECK(!speed_run_level(2) && !speed_run_level(5) && !speed_run_level(7) && !speed_run_level(12) && !speed_run_level(13));
    }
    // the in-level HUD's layout: anchored and sized Auto by default (the game's size up to 1080 lines, smaller beyond), a size or a margin of the player's
    CHECK(enhanced.hud_anchor && enhanced.hud_size == 0 && enhanced.hud_margin == 0);
    CHECK(auto_hud_size(0) == 100 && auto_hud_size(720) == 100 && auto_hud_size(1080) == 100 && auto_hud_size(1440) == 85 && auto_hud_size(2160) == 70 && auto_hud_size(4320) == 70);
    {
        const auto hud_value = [](const Settings& s, const char* name, int display_height) {
            for (const auto& [key, text] : launch_environment(s, "", display_height)) {
                if (key == name) {
                    return text;
                }
            }
            return std::string("<absent>");
        };
        CHECK(hud_value(enhanced, "OT_HUD_ANCHOR", 0) == "1" && hud_value(enhanced, "OT_HUD_SCALE", 0) == "100" && hud_value(enhanced, "OT_HUD_MARGIN", 0) == "0");
        CHECK(hud_value(enhanced, "OT_HUD_SCALE", 2160) == "70");   // Auto: the display's height when the window's size is not set
        Settings sized = enhanced;
        sized.resolution_x = 1920;
        sized.resolution_y = 1080;
        CHECK(hud_value(sized, "OT_HUD_SCALE", 2160) == "100");     // ... the set window's when it is
        sized.hud_anchor = false;
        sized.hud_size = 80;
        sized.hud_margin = 5;
        CHECK(hud_value(sized, "OT_HUD_ANCHOR", 0) == "0" && hud_value(sized, "OT_HUD_SCALE", 2160) == "80" && hud_value(sized, "OT_HUD_MARGIN", 0) == "5");
        sized.hud_size = 500;
        sized.hud_margin = 90;
        CHECK(hud_value(sized, "OT_HUD_SCALE", 0) == std::to_string(kHudSizeMax) && hud_value(sized, "OT_HUD_MARGIN", 0) == std::to_string(kHudMarginMax));
        IniFile hud_file;
        write_settings(hud_file, sized);
        const Settings hud_back = read_settings(hud_file);
        CHECK(!hud_back.hud_anchor && hud_back.hud_size == kHudSizeMax && hud_back.hud_margin == kHudMarginMax);
        CHECK(read_settings(IniFile::parse("[Window]\nHudSize=10\n")).hud_size == kHudSizeMin && read_settings(IniFile::parse("[Window]\n")).hud_anchor);   // an old file: the defaults
    }
    // the options survive a save and a load; a file from before they did anything gets the new defaults for the three that changed meaning
    IniFile saved;
    write_settings(saved, game);
    const Settings reloaded = read_settings(saved);
    CHECK(reloaded.aspect == 2 && reloaded.msaa == 8 && reloaded.scaling == 1 && reloaded.autokick == 2 && reloaded.vibration == 0);
    // the stretched 4:3 (Aspect=5) is no longer offered: an older file's is Auto, and the game is told the window's shape
    const Settings old_stretch = read_settings(IniFile::parse("[Window]\nAspect=5\n"));
    const auto old_env = launch_environment(old_stretch, "");
    CHECK(old_stretch.aspect == 0 &&
          std::any_of(old_env.begin(), old_env.end(), [](const auto& entry) { return entry.first == "OT_ASPECT" && entry.second == "window"; }));
    CHECK(reloaded.unlock_cheats && reloaded.unlock_movies && !reloaded.quick_start && reloaded.quick_level == 12 && reloaded.quick_skater == 19 &&
          reloaded.quick_mode == 1 && reloaded.quick_look == 3);
    CHECK(reloaded.hide_hud && !default_settings().hide_hud);
    CHECK(reloaded.directinput_pads && !default_settings().directinput_pads);
    {
        bool off = false;   // off by default: its device scan can hold the start for 20 s
        for (const auto& [key, text] : launch_environment(default_settings(), "")) {
            off = off || (key == "SDL_JOYSTICK_DIRECTINPUT" && text == "0");
        }
        CHECK(off);
    }
    CHECK(reloaded.grind_meter && !default_settings().grind_meter);
    // debug mode: off by default, stored in [Launcher], and the only thing that sets OPENTONY_DEBUG (opentony.exe keeps every run and dumps textures only then)
    {
        const auto debug_value = [](const Settings& s) {
            for (const auto& [key, value] : launch_environment(s, "", 1080, "")) {
                if (key == "OPENTONY_DEBUG") {
                    return value;
                }
            }
            return std::string("(unset)");
        };
        Settings on = default_settings();
        on.debug_mode = true;
        IniFile file;
        write_settings(file, on);
        CHECK(!default_settings().debug_mode && debug_value(default_settings()).empty() && debug_value(on) == "1");
        CHECK(read_settings(file).debug_mode && !read_settings(IniFile::parse("[Launcher]\n")).debug_mode);
    }
    // the update notice: the check is on for a new file or an old one without the key, and the player's choice and the version they dismissed survive a save
    {
        CHECK(default_settings().check_updates && default_settings().skipped_update.empty());
        CHECK(read_settings(IniFile::parse("[Launcher]\n")).check_updates && read_settings(IniFile{}).check_updates);
        Settings chosen = default_settings();
        chosen.check_updates = false;
        chosen.skipped_update = "v0.8";
        IniFile file;
        write_settings(file, chosen);
        const Settings back = read_settings(IniFile::parse(file.text()));
        CHECK(!back.check_updates && back.skipped_update == "v0.8");
        CHECK(read_settings(IniFile::parse("[Launcher]\nCheckForUpdates=1\n")).check_updates && !read_settings(IniFile::parse("[Launcher]\nCheckForUpdates=0\n")).check_updates);
    }
    // score initials: three of A to Z or a space, upper-cased; anything else is empty and the variable stays set-but-empty so nothing leaks between runs
    {
        CHECK(normalize_initials("afc") == "AFC" && normalize_initials("A C") == "A C" && normalize_initials("AB ") == "AB " && normalize_initials("\"AB \"") == "AB ");
        CHECK(normalize_initials("").empty() && normalize_initials("AB").empty() && normalize_initials("ABCD").empty() && normalize_initials("A1C").empty() &&
              normalize_initials("A\tC").empty() && normalize_initials("\xc3\x84""BC").empty());
        const auto initials_env = [](const Settings& s) {
            for (const auto& [key, text] : launch_environment(s, "")) {
                if (key == "OT_INITIALS") {
                    return text;
                }
            }
            return std::string("<absent>");
        };
        CHECK(default_settings().initials.empty() && initials_env(default_settings()).empty());
        Settings named = default_settings();
        named.initials = "afc";
        CHECK(initials_env(named) == "AFC");
        named.initials = "AB ";   // a trailing space survives the file
        IniFile initials_file;
        write_settings(initials_file, named);
        CHECK(read_settings(initials_file).initials == "AB " && initials_env(read_settings(initials_file)) == "AB ");
        named.initials = "A1C";
        CHECK(initials_env(named).empty());
        write_settings(initials_file, named);
        CHECK(read_settings(initials_file).initials.empty());
        CHECK(read_settings(IniFile::parse("[Game]\nInitials=AFC\n")).initials == "AFC" && read_settings(IniFile::parse("[Game]\nInitials=XXXXX\n")).initials.empty() &&
              read_settings(IniFile::parse("[Game]\n")).initials.empty());
    }
    CHECK(reloaded.unlock_movies && reloaded.default_camera == 1 && reloaded.music_level == 3 && reloaded.sfx_level == 10);
    CHECK(reloaded.internal_resolution == 1 && reloaded.fog_distance == kFogOffDistance);
    const Settings placeholder = read_settings(IniFile::parse("[Graphics]\nUsePSXTextures=1\nFogDistance=150\nTextureFilter=0\nInternalResolution=0\n"));
    CHECK(!placeholder.use_psx_textures && placeholder.texture_filter == 1 && placeholder.internal_resolution == 2);   // the old placeholders' values were not choices
    const Settings partymod_choice = read_settings(IniFile::parse("[Launcher]\nSettingsVersion=2\n[Graphics]\nUsePSXTextures=1\nInternalResolution=0\n"));
    CHECK(partymod_choice.use_psx_textures && partymod_choice.internal_resolution == 0);

    // hotkeys: F8 screenshot, F5 retry, L3 + R3 held a second quits, Alt+Enter fullscreen; they round-trip, an unbound one is "0,0,0,0" (never empty: the game would keep
    // its own default), a file without the section gets the defaults, and out-of-range numbers are held to what the game reads
    {
        const auto hotkey_env = [](const Settings& s, const char* name) {
            for (const auto& [key, text] : launch_environment(s, "")) {
                if (key == name) {
                    return text;
                }
            }
            return std::string("<absent>");
        };
        CHECK(hotkey_env(enhanced, "OT_HOTKEY_SCREENSHOT") == "66,0,0,0" && hotkey_env(enhanced, "OT_HOTKEY_RETRY") == "63,0,0,0");
        CHECK(hotkey_env(enhanced, "OT_HOTKEY_QUIT") == "0,0,384,1000" && hotkey_env(enhanced, "OT_HOTKEY_FULLSCREEN") == "28,2,0,0");
        CHECK(hotkey_env(enhanced, "OT_PAUSE_ON_FOCUS_LOSS") == "1");   // on for a new file
        CHECK(hotkey_pad_name(384) == "L3 + R3" && hotkey_pad_name(80) == "Back + Start" && hotkey_pad_name(0) == "None" && hotkey_pad_name((1u << 15) | (1u << 16)) == "LT + RT");
        Settings keys = enhanced;
        keys.hotkeys[0] = HotkeyBinding{};
        keys.hotkeys[1] = HotkeyBinding{0x13, kHotkeyCtrl | kHotkeyShift, 1u << 3, 250};
        keys.pause_on_focus_loss = true;
        CHECK(hotkey_env(keys, "OT_HOTKEY_SCREENSHOT") == "0,0,0,0" && hotkey_env(keys, "OT_HOTKEY_RETRY") == "19,5,8,250" && hotkey_env(keys, "OT_PAUSE_ON_FOCUS_LOSS") == "1");
        IniFile file;
        write_settings(file, keys);
        const Settings again = read_settings(file);
        CHECK(again.hotkeys == keys.hotkeys && again.pause_on_focus_loss);
        keys.pause_on_focus_loss = false;
        IniFile off_file;
        write_settings(off_file, keys);
        CHECK(!read_settings(off_file).pause_on_focus_loss);   // a saved choice is kept
        const Settings none = read_settings(IniFile::parse("[Game]\nScreenshots=1\n"));
        CHECK(none.hotkeys == enhanced.hotkeys && none.pause_on_focus_loss);
        const Settings wild = read_settings(IniFile::parse("[Hotkeys]\nRetryKey=999\nRetryMods=99\nRetryPad=9999999\nRetryHold=-5\nQuitKey=0\nQuitMods=7\n"));
        CHECK(wild.hotkeys[1].key == 255 && wild.hotkeys[1].mods == 7 && wild.hotkeys[1].pad == (1u << kHotkeyPadBits) - 1 && wild.hotkeys[1].hold_ms == 0);
        CHECK(wild.hotkeys[2].mods == 0);   // modifiers with no key mean nothing
    }

    // texture detail: 16x by default; the env value per option; sharp textures pass 0 (nothing to filter); the key round-trips and a bad or missing one gets 16
    CHECK(enhanced.anisotropy == 16);
    {
        Settings detail = enhanced;
        const auto anisotropy_env = [](const Settings& s) {
            for (const auto& [key, text] : launch_environment(s, "")) {
                if (key == "OT_ANISOTROPY") {
                    return text;
                }
            }
            return std::string("<absent>");
        };
        CHECK(anisotropy_env(detail) == "16");
        const char* const expected[] = {"0", "trilinear", "2", "4", "8", "16"};
        for (std::size_t k = 0; k < kAnisotropyValues.size(); ++k) {
            detail.anisotropy = kAnisotropyValues[k];
            CHECK(anisotropy_env(detail) == expected[k]);
            CHECK(anisotropy_option_index(detail.anisotropy) == static_cast<int>(k));
            IniFile file;
            write_settings(file, detail);
            CHECK(read_settings(file).anisotropy == kAnisotropyValues[k]);
        }
        detail.anisotropy = 8;
        detail.texture_filter = 0;   // sharp: nearest, so no mip maps whatever the row says; the row's value is kept for when it is smooth again
        CHECK(anisotropy_env(detail) == "0");
        IniFile sharp;
        write_settings(sharp, detail);
        CHECK(read_settings(sharp).anisotropy == 8);
        CHECK(read_settings(IniFile::parse("[Launcher]\nSettingsVersion=2\n[Graphics]\nTextureFilter=1\n")).anisotropy == 16);   // a file made before the key existed
        CHECK(read_settings(IniFile::parse("[Graphics]\nAnisotropy=3\n")).anisotropy == 16);                                      // not one of the options
        CHECK(read_settings(IniFile::parse("[Graphics]\nAnisotropy=-1\n")).anisotropy == 16);
        CHECK(read_settings(IniFile::parse("[Graphics]\nAnisotropy=32\n")).anisotropy == 16);
        CHECK(read_settings(IniFile::parse("[Graphics]\nAnisotropy=0\n")).anisotropy == 0);
    }

    // supersampling and smooth cut-outs: off and on by default; env per option; PSX and PC sizes pass 1; keys round-trip and bad values fall back
    {
        CHECK(enhanced.supersample == 400 && enhanced.smooth_cutouts);
        const auto env_of = [](const Settings& s, const char* name) {
            for (const auto& [key, text] : launch_environment(s, "")) {
                if (key == name) {
                    return text;
                }
            }
            return std::string("<absent>");
        };
        Settings sample = enhanced;
        const char* const expected[] = {"1", "1.5", "2", "3", "4"};
        for (std::size_t k = 0; k < kSupersampleValues.size(); ++k) {
            sample.supersample = kSupersampleValues[k];
            CHECK(env_of(sample, "OT_SUPERSAMPLE") == expected[k]);
            CHECK(supersample_option_index(sample.supersample) == static_cast<int>(k));
            IniFile file;
            write_settings(file, sample);
            CHECK(read_settings(file).supersample == kSupersampleValues[k]);
        }
        sample.supersample = 200;
        sample.internal_resolution = 1;   // PC: ignores it
        CHECK(env_of(sample, "OT_SUPERSAMPLE") == "1");
        sample.internal_resolution = 0;
        CHECK(env_of(sample, "OT_SUPERSAMPLE") == "1");
        sample.internal_resolution = 2;
        CHECK(env_of(sample, "OT_SUPERSAMPLE") == "2");
        // the output looks (Sharp, Nearest, CRT) exist at the PlayStation and PC sizes only: Native sends smooth whatever is stored, and the stored choice stays
        sample.scaling = 3;
        CHECK(env_of(sample, "OT_SCALING") == "smooth" && sample.scaling == 3 && env_of(sample, "OT_SUPERSAMPLE") == "2");
        sample.internal_resolution = 1;
        CHECK(env_of(sample, "OT_SCALING") == "crt");
        sample.internal_resolution = 2;
        sample.scaling = 0;
        CHECK(env_of(enhanced, "OT_ALPHA_TO_COVERAGE") == "1");
        sample.msaa = 0;   // nothing to extend without multisampling
        CHECK(env_of(sample, "OT_ALPHA_TO_COVERAGE") == "0");
        sample.msaa = 4;
        sample.smooth_cutouts = false;
        CHECK(env_of(sample, "OT_ALPHA_TO_COVERAGE") == "0");
        IniFile cutouts;
        write_settings(cutouts, sample);
        CHECK(!read_settings(cutouts).smooth_cutouts);
        CHECK(read_settings(IniFile::parse("[Graphics]\nSupersample=250\n")).supersample == 100);   // not one of the options
        CHECK(read_settings(IniFile::parse("[Graphics]\nSupersample=-1\n")).supersample == 100);
        CHECK(read_settings(IniFile::parse("[Launcher]\nSettingsVersion=2\n")).smooth_cutouts);   // a file made before the keys existed
        Settings preset = enhanced;
        preset.supersample = 300;
        apply_preset(preset, 1);
        CHECK(preset.supersample == 100 && !preset.smooth_cutouts);
        apply_preset(preset, 2);
        CHECK(preset.supersample == 100 && !preset.smooth_cutouts);
        apply_preset(preset, 0);
        CHECK(preset.supersample == 400 && preset.smooth_cutouts);
    }

    // sharp menus and HUD: on by default, only with smooth textures; key round-trips; a file without it gets the default; presets
    {
        const auto hud_env = [](const Settings& s) {
            for (const auto& [key, text] : launch_environment(s, "")) {
                if (key == "OT_HUD_FILTER") {
                    return text;
                }
            }
            return std::string("<absent>");
        };
        Settings hud = enhanced;
        CHECK(hud.sharp_hud && hud_env(hud) == "sharp");
        hud.sharp_hud = false;
        CHECK(hud_env(hud) == "smooth");
        IniFile file;
        write_settings(file, hud);
        CHECK(!read_settings(file).sharp_hud);
        hud.sharp_hud = true;
        hud.texture_filter = 0;
        CHECK(hud_env(hud) == "smooth");
        write_settings(file, hud);
        CHECK(read_settings(file).sharp_hud);
        CHECK(read_settings(IniFile::parse("[Launcher]\nSettingsVersion=2\n[Graphics]\nTextureFilter=1\n")).sharp_hud);
        apply_preset(hud, 1);
        CHECK(!hud.sharp_hud);
        apply_preset(hud, 2);
        CHECK(!hud.sharp_hud);
        apply_preset(hud, 0);
        CHECK(hud.sharp_hud);
    }

    // G4: object detail distance. The default (a new file, a file without the key) is the most; the game's own is 100; it reaches the game as OT_DETAIL_DISTANCE
    {
        const auto detail_env = [](const Settings& s) {
            for (const auto& [key, text] : launch_environment(s, "")) {
                if (key == "OT_DETAIL_DISTANCE") {
                    return text;
                }
            }
            return std::string("<absent>");
        };
        Settings detail = enhanced;
        CHECK(detail.detail_distance == kDetailMaxDistance && detail_env(detail) == "max");
        detail.detail_distance = 200;
        CHECK(detail_env(detail) == "200");
        detail.detail_distance = 100;
        CHECK(detail_env(detail) == "100");
        IniFile file;
        write_settings(file, detail);
        CHECK(read_settings(file).detail_distance == 100);
        detail.detail_distance = 400;
        write_settings(file, detail);
        CHECK(read_settings(file).detail_distance == 400);
        CHECK(read_settings(IniFile::parse("[Launcher]\nSettingsVersion=2\n[Graphics]\nTextureFilter=1\n")).detail_distance == kDetailMaxDistance);
        CHECK(read_settings(IniFile::parse("[Launcher]\nSettingsVersion=2\n[Graphics]\nDetailDistance=250\n")).detail_distance == kDetailMaxDistance);   // not one of the options
        CHECK(detail_option_index(100) == 0 && detail_option_index(200) == 1 && detail_option_index(400) == 2 && detail_option_index(kDetailMaxDistance) == 3);
        apply_preset(detail, 1);
        CHECK(detail.detail_distance == 100);
        apply_preset(detail, 2);
        CHECK(detail.detail_distance == 100);
        apply_preset(detail, 0);
        CHECK(detail.detail_distance == kDetailMaxDistance);
    }

    // the presets: Enhanced 16x, the original PC game and the PlayStation look off (nearest, no mips)
    {
        Settings preset = enhanced;
        preset.anisotropy = 4;
        apply_preset(preset, 1);
        CHECK(preset.anisotropy == 0 && preset.texture_filter == 1 && preset.msaa == 0 && preset.internal_resolution == 1);
        apply_preset(preset, 2);
        CHECK(preset.anisotropy == 0 && preset.texture_filter == 0 && preset.use_psx_textures && preset.fog_distance == 100);
        apply_preset(preset, 0);
        CHECK(preset.anisotropy == 16 && preset.texture_filter == 1 && preset.msaa == 8 && preset.supersample == 400 && preset.internal_resolution == 2);
        CHECK(!preset.use_psx_textures && preset.psx_colour && preset.fog_distance == kFogOffDistance);   // Enhanced is everything at its most, with the PC's sharper textures
        // the preset the values match, or none; a setting no preset holds (the window, sound, unlocks) does not change the match
        for (int k = 0; k < kPresetCount; ++k) {
            Settings named = enhanced;
            apply_preset(named, k);
            CHECK(preset_of(named) == k);
            named.windowed = true;
            named.resolution_x = 1280;
            named.vsync = 1;
            named.music_level = 2;
            named.unlock_cheats = true;
            named.show_fps = true;
            named.hide_hud = true;
            named.camera_sensitivity = 9;
            CHECK(preset_of(named) == k);
            named.aspect = 3;
            CHECK(preset_of(named) == -1);
        }
        preset.msaa = 2;
        CHECK(preset_of(preset) == -1);
        preset.msaa = 8;
        preset.fog_distance = 900;   // shown as Off: still Enhanced
        CHECK(preset_of(preset) == 0);
        game.max_stats = true;
        CHECK(launch_environment(game, "")[0].first == "OT_WINDOW_MODE");
        for (const auto& [key, text] : launch_environment(game, "")) {
            if (key == "OT_MAX_STATS") {
                CHECK(text == "1");
            }
        }
    }
}

void test_sha256() {
    CHECK(sha256_bytes("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_bytes("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string million(1000000, 'a');
    CHECK(sha256_bytes(million.data(), million.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Synthetic retail tables in the shape of the real ones: names by code, the defaults of the game's layout, its reading order.
RetailTables synthetic_tables() {
    RetailTables t;
    for (int code = 0; code < 256; ++code) {
        t.key_names[code] = code == 0 ? "-?-" : "K" + std::to_string(code);
    }
    t.key_names[3] = "2 ";       // the retail name with a trailing space
    t.key_names[0x39] = "SPACE";
    t.key_names[0x1c] = "RETURN";
    t.key_names[0x19] = "P";
    t.key_names[0x01] = "ESC";
    t.key_names[0xdf] = "K13";   // a second code with the name of code 13: only the first is bindable
    // kActions order: JUMP GRIND GRAB KICK SPINLEFT NOLLIE SPINRIGHT SWITCH PAUSE CAMERA UP DOWN LEFT RIGHT
    const std::uint8_t defaults[kActionCount][3] = {{80, 0x39, 0x1c}, {72, 47, 0}, {75, 46, 0}, {77, 48, 0}, {71, 0, 0}, {79, 0, 0}, {73, 0, 0},
                                                    {81, 0, 0}, {74, 0x19, 0x01}, {78, 24, 0}, {200, 17, 0}, {208, 31, 0}, {203, 30, 0}, {205, 32, 0}};
    for (std::size_t a = 0; a < kActionCount; ++a) {
        for (std::size_t k = 0; k < 3; ++k) {
            t.default_keys[a][k] = defaults[a][k];
        }
    }
    t.default_pad = {1, 4, 2, 0, 3, 6, 5, 7, 9, 8};
    // the game reads JUMP GRIND GRAB KICK SPINLEFT NOLLIE SPINRIGHT SWITCH UP DOWN LEFT RIGHT PAUSE CAMERA
    t.load_order = {0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 8, 9};
    return t;
}

bool same_keys(const Bindings& a, const Bindings& b) {
    for (std::size_t action = 0; action < kActionCount; ++action) {
        std::vector<std::uint8_t> x, y;
        for (std::size_t k = 0; k < kKeysPerAction; ++k) {
            if (a.keys[action][k] != 0) x.push_back(a.keys[action][k]);
            if (b.keys[action][k] != 0) y.push_back(b.keys[action][k]);
        }
        std::sort(x.begin(), x.end());
        std::sort(y.begin(), y.end());
        if (x != y) {
            return false;
        }
    }
    return a.pad == b.pad;
}

void test_bindings() {
    const RetailTables t = synthetic_tables();
    CHECK(!key_bindable(t, 0) && !key_bindable(t, 3) && !key_bindable(t, 0xdf) && key_bindable(t, 13) && key_bindable(t, 0x39));

    // a file without bindings reads as the defaults; the defaults write and read back unchanged
    Bindings defaults = read_bindings(IniFile{}, t);
    CHECK(same_keys(defaults, default_bindings(t)));
    CHECK(find_conflicts(defaults, t).empty());
    IniFile ini;
    write_bindings(ini, defaults, t);
    CHECK(ini.get_string("KEYBOARD", "JUMP") == "K80,SPACE,RETURN");
    CHECK(ini.get_string("KEYBOARD", "GRIND") == "K72,K47");   // as the game writes it
    CHECK(ini.get_int("JOYPAD", "PAUSE", -5) == 9);
    std::vector<std::string> problems;
    CHECK(same_keys(read_bindings(ini, t, &problems), defaults) && problems.empty());

    // a spare slot left out keeps the default key in the game: reading "GRIND=K16" gives K16 plus GRIND's second default key
    IniFile partial = IniFile::parse("[KEYBOARD]\nGRIND=K16\n");
    const Bindings partial_read = read_bindings(partial, t);
    CHECK(partial_read.keys[1][0] == 16 && partial_read.keys[1][1] == 47);
    // a code no key produces (0x5a) is inert: the reader leaves it out
    CHECK(read_bindings(IniFile::parse("[KEYBOARD]\nGRIND=K16,K90\n"), t).keys[1][1] == 0);

    // moving PAUSE's default key P (slot 1) to JUMP, which the game reads earlier: only slot 1 passes the game's "taken" check
    Bindings moved = defaults;
    assign_key(moved, 0, 0, 0x19);   // JUMP gets P in its first slot ...
    CHECK(moved.keys[8][0] == 74 && moved.keys[8][1] == 0x01);   // ... and P leaves PAUSE
    CHECK(find_conflicts(moved, t).empty());
    IniFile moved_ini;
    write_bindings(moved_ini, moved, t);
    const std::string jump = moved_ini.get_string("KEYBOARD", "JUMP");
    CHECK(jump.substr(jump.find(',') + 1, 2) == "P,");   // P is written in slot 1, where PAUSE's default has it
    problems.clear();
    CHECK(same_keys(read_bindings(moved_ini, t, &problems), moved) && problems.empty());

    // a written key in a slot the check refuses makes the game reset everything; the reader says so
    IniFile refused = IniFile::parse("[KEYBOARD]\nJUMP=P,K80,K80\n");
    problems.clear();
    CHECK(same_keys(read_bindings(refused, t, &problems), defaults) && problems.size() == 1);

    // one key on two actions; no key; a pad button twice
    Bindings bad = defaults;
    bad.keys[1][0] = 80;
    CHECK(!find_conflicts(bad, t).empty());
    bad = defaults;
    bad.keys[4] = {0, 0, 0};
    CHECK(!find_conflicts(bad, t).empty());
    bad = defaults;
    bad.pad[1] = bad.pad[0];
    CHECK(!find_conflicts(bad, t).empty());
    IniFile pad_reset = IniFile::parse("[JOYPAD]\nJUMP=4\n");   // GRIND's default is 4 too
    problems.clear();
    CHECK(same_keys(read_bindings(pad_reset, t, &problems), defaults) && problems.size() == 1);
    Bindings pad = defaults;
    assign_pad(pad, 0, 4);
    CHECK(pad.pad[0] == 4 && pad.pad[1] == -1 && find_conflicts(pad, t).empty());

    // SDL scancodes and buttons
    CHECK(key_code_from_scancode(44) == 0x39);   // SDL_SCANCODE_SPACE -> DIK_SPACE
    CHECK(key_code_from_scancode(90) == 0x50);   // SDL_SCANCODE_KP_2 -> DIK_NUMPAD2
    CHECK(pad_button_from_sdl(0) == 1 && pad_button_from_sdl(2) == 0 && pad_button_from_sdl(11) == -1);
}

void test_moved_default_key() {
    RetailTables t = synthetic_tables();
    Bindings b = default_bindings(t);
    b.keys[2] = {77, 0, 0};   // Grab and Flip swap their first keys
    b.keys[3] = {75, 0, 0};
    CHECK(find_conflicts(b, t).empty());
    // Grab = keypad 6 is KICK's default (slot 0), which the game reads later: Grab's spare slot 1 can neither repeat it nor keep its default C, so it
    // holds an inert filler; the file reads back to exactly these bindings
    IniFile ini;
    write_bindings(ini, b, t);
    const std::string grab = ini.get_string("KEYBOARD", "GRAB");
    CHECK(grab.rfind("K77,", 0) == 0 && grab != "K77,K77");
    std::vector<std::string> problems;
    CHECK(same_keys(read_bindings(ini, t, &problems), b) && problems.empty());
}

// ---------------------------------------------------------------- a disc image built here

void put32(std::vector<std::uint8_t>& s, std::size_t at, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) {
        s[at + k] = static_cast<std::uint8_t>(v >> (8 * k));
        s[at + 4 + (3 - k)] = static_cast<std::uint8_t>(v >> (8 * k));   // both-endian
    }
}

void record(std::vector<std::uint8_t>& sector, std::size_t& at, const std::string& name, std::uint32_t extent, std::uint32_t size, bool directory) {
    const std::size_t length = 33 + name.size() + ((33 + name.size()) % 2);
    sector[at] = static_cast<std::uint8_t>(length);
    put32(sector, at + 2, extent);
    put32(sector, at + 10, size);
    sector[at + 25] = directory ? 2 : 0;
    sector[at + 32] = static_cast<std::uint8_t>(name.size());
    std::memcpy(sector.data() + at + 33, name.data(), name.size());
    at += length;
}

// sectors: 16 primary descriptor, 17 terminator, 18 root, 19 SETUP, 20 DATA, 21 THAWK2.EXE, 22.. ALL.PKR
std::vector<std::vector<std::uint8_t>> build_iso(const std::string& exe, const std::string& pkr) {
    std::vector<std::vector<std::uint8_t>> sectors(24, std::vector<std::uint8_t>(2048, 0));
    auto& pvd = sectors[16];
    pvd[0] = 1;
    std::memcpy(pvd.data() + 1, "CD001", 5);
    pvd[6] = 1;
    std::size_t at = 156;
    record(pvd, at, std::string(1, '\0'), 18, 2048, true);
    sectors[17][0] = 255;
    std::memcpy(sectors[17].data() + 1, "CD001", 5);
    at = 0;
    record(sectors[18], at, std::string(1, '\0'), 18, 2048, true);
    record(sectors[18], at, std::string(1, '\1'), 18, 2048, true);
    record(sectors[18], at, "SETUP", 19, 2048, true);
    at = 0;
    record(sectors[19], at, std::string(1, '\0'), 19, 2048, true);
    record(sectors[19], at, std::string(1, '\1'), 18, 2048, true);
    record(sectors[19], at, "DATA", 20, 2048, true);
    at = 0;
    record(sectors[20], at, std::string(1, '\0'), 20, 2048, true);
    record(sectors[20], at, std::string(1, '\1'), 19, 2048, true);
    record(sectors[20], at, "THAWK2.EXE;1", 21, static_cast<std::uint32_t>(exe.size()), false);
    record(sectors[20], at, "ALL.PKR;1", 22, static_cast<std::uint32_t>(pkr.size()), false);
    std::memcpy(sectors[21].data(), exe.data(), exe.size());
    std::memcpy(sectors[22].data(), pkr.data(), std::min<std::size_t>(pkr.size(), 2048));
    if (pkr.size() > 2048) {
        std::memcpy(sectors[23].data(), pkr.data() + 2048, pkr.size() - 2048);
    }
    return sectors;
}

void write_image(const fs::path& path, const std::vector<std::vector<std::uint8_t>>& sectors, std::uint32_t size, std::uint32_t offset) {
    std::ofstream out(path, std::ios::binary);
    for (const auto& sector : sectors) {
        std::vector<std::uint8_t> raw(size, 0);
        std::memcpy(raw.data() + offset, sector.data(), 2048);
        out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    }
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void test_disc_image(const fs::path& scratch) {
    const std::string exe = "MZ not the real game";
    std::string pkr(3000, 'p');
    pkr[2999] = 'q';
    const auto sectors = build_iso(exe, pkr);
    const struct { const char* name; std::uint32_t size, offset; } layouts[] = {{"plain.iso", 2048, 0}, {"mode1.bin", 2352, 16}, {"mode2.img", 2352, 24}};
    for (const auto& layout : layouts) {
        const fs::path image_path = scratch / layout.name;
        write_image(image_path, sectors, layout.size, layout.offset);
        std::string error;
        auto image = DiscImage::open(image_path, error);
        CHECK(image != nullptr);
        if (!image) {
            std::fprintf(stderr, "  %s: %s\n", layout.name, error.c_str());
            continue;
        }
        const auto found = image->find("setup/Data/all.pkr");
        CHECK(found && !found->directory && found->size == 3000 && found->name == "ALL.PKR");
        CHECK(!image->find("SETUP/MISSING"));
        const fs::path out = scratch / (std::string(layout.name) + ".pkr");
        CHECK(found && image->extract(*found, out, {}) && read_file(out) == pkr);

        // the import of a disc image: the plan, and the refusal of a THawk2.exe of no supported build (after the copy of the exe only)
        const ImportPlan plan = plan_import(image_path);
        CHECK(plan.error.empty() && plan.from_image && plan.items.size() == 2 && plan.total_bytes() == exe.size() + pkr.size());
        ImportProgress progress;
        std::string import_error;
        const fs::path game = scratch / (std::string(layout.name) + ".game");
        CHECK(!run_import(plan, game, progress, import_error) && import_error.find("neither the Activision Value") != std::string::npos);
        CHECK(read_file(game / "THawk2.exe") == exe && !fs::exists(game / "ALL.PKR"));
    }
    std::string not_an_image;
    CHECK(DiscImage::open(scratch / "plain.iso.pkr", not_an_image) == nullptr && !not_an_image.empty());
}

void test_folder_import(const fs::path& scratch) {
    // the disc layout (SETUP\data, SETUP\med\Movies), any case
    const fs::path disc = scratch / "disc";
    fs::create_directories(disc / "Setup" / "DATA" / "Music");
    fs::create_directories(disc / "Setup" / "med" / "movies");
    std::ofstream(disc / "Setup" / "DATA" / "thawk2.EXE") << "exe";
    std::ofstream(disc / "Setup" / "DATA" / "all.pkr") << "pkr!";
    std::ofstream(disc / "Setup" / "DATA" / "Music" / "LTIX01.DAT") << "music";
    std::ofstream(disc / "Setup" / "med" / "movies" / "intro.bik") << "movie";
    const ImportPlan plan = plan_import(disc);
    CHECK(plan.error.empty() && !plan.from_image && plan.items.size() == 4 && plan.total_bytes() == 3 + 4 + 5 + 5);
    bool music = false, movies = false;
    for (const ImportItem& item : plan.items) {
        music = music || item.target == "Music/LTIX01.DAT";
        movies = movies || item.target == "med/Movies/intro.bik";
    }
    CHECK(music && movies);
    CHECK(!plan_import(scratch / "nothing-here").error.empty());

    const GameCheck missing = check_game_folder(scratch / "nothing-here", scratch / "unpacked");
    CHECK(!missing.usable() && missing.summary() == "ALL.PKR is missing");
    const GameCheck wrong = check_game_folder(disc / "Setup" / "DATA", scratch / "unpacked");
    CHECK(!wrong.exe.empty() && !wrong.pkr.empty() && !wrong.exe_supported() && !wrong.usable() && wrong.music);
    CHECK(wrong.build == GameBuild::Unknown && wrong.summary().find("neither") != std::string::npos);

    // the builds by their SHA-256: the Value re-release and the original 2000 release's unprotected executable run, the 2000 disc's own is named
    CHECK(build_of_sha256(kCanonicalSha256) == GameBuild::ValueRerelease && build_of_sha256(kOriginal2000Sha256) == GameBuild::Original2000);
    CHECK(build_of_sha256(kProtectedDiscSha256) == GameBuild::Original2000Protected && build_of_sha256(std::string(64, '0')) == GameBuild::Unknown);
    CHECK(build_supported(GameBuild::ValueRerelease) && build_supported(GameBuild::Original2000));
    CHECK(!build_supported(GameBuild::Original2000Protected) && !build_supported(GameBuild::Unknown));
    CHECK(build_problem(GameBuild::Original2000, kOriginal2000Sha256).empty());
    CHECK(build_problem(GameBuild::Original2000Protected, kProtectedDiscSha256).find("copy-protected") != std::string::npos);
}

void test_real_tables() {
    const char* path = std::getenv("OPENTONY_THAWK2_EXE");
    if (path == nullptr || *path == '\0') {
        std::printf("launcher_core_test: OPENTONY_THAWK2_EXE not set, the retail table reader is checked on synthetic tables only\n");
        return;
    }
    std::ifstream in(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string error;
    const auto tables = read_retail_tables(bytes, error);
    CHECK(tables.has_value());
    if (!tables) {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return;
    }
    CHECK(tables->key_names[0x39] == "SPACE" && tables->key_names[0x50] == "PAD2" && tables->key_names[3] == "2 ");
    CHECK(!key_bindable(*tables, 3) && key_bindable(*tables, 0x39));
    CHECK(tables->default_keys[0][0] == 0x50 && tables->default_keys[0][1] == 0x39 && tables->default_keys[0][2] == 0x1c);   // JUMP=PAD2,SPACE,RETURN
    CHECK(tables->default_pad[0] == 1 && tables->default_pad[3] == 0 && tables->default_pad[8] == 9 && tables->default_pad[9] == 8);
    CHECK(tables->load_order[8] == 10 && tables->load_order[12] == 8);   // UP is read ninth, PAUSE thirteenth
    const Bindings defaults = default_bindings(*tables);
    CHECK(find_conflicts(defaults, *tables).empty());
    IniFile ini;
    write_bindings(ini, defaults, *tables);
    std::vector<std::string> problems;
    CHECK(same_keys(read_bindings(ini, *tables, &problems), defaults) && problems.empty());
}

// OPENTONY_DISC_IMAGE: a Tony Hawk's Pro Skater 2 PC disc (game/THPS2.img, the Value re-release in raw Mode 2 sectors, or the original 2000 disc in raw
// Mode 1): the import finds the game's files and the THawk2.exe it copies out is a known build
void test_real_disc(const fs::path& scratch) {
    const char* path = std::getenv("OPENTONY_DISC_IMAGE");
    if (path == nullptr || *path == '\0') {
        std::printf("launcher_core_test: OPENTONY_DISC_IMAGE not set, the disc-image reader is checked on built images only\n");
        return;
    }
    const ImportPlan plan = plan_import(path);
    CHECK(plan.error.empty() && plan.from_image);
    bool pkr = false, music = false, movies = false;
    for (const ImportItem& item : plan.items) {
        pkr = pkr || (item.target == "ALL.PKR" && item.size > 100000000);
        music = music || item.target.rfind("Music/", 0) == 0;
        movies = movies || item.target.rfind("med/Movies/", 0) == 0;
    }
    CHECK(pkr && music && movies);
    std::string error;
    auto image = DiscImage::open(path, error);
    const auto exe = image ? image->find("SETUP/data/THawk2.exe") : std::nullopt;
    CHECK(exe.has_value());
    if (exe) {
        const fs::path out = scratch / "THawk2.exe";
        CHECK(image->extract(*exe, out, {}) && build_of_sha256(sha256_file(out)) != GameBuild::Unknown);
    }
    std::printf("launcher_core_test: disc image %s: %zu files, %llu bytes to import\n", path, plan.items.size(), static_cast<unsigned long long>(plan.total_bytes()));
}

// The custom soundtrack: the folder scan (src/launcher/music_library.hpp, the game's playlist reads the same), its settings and what reaches the game
void test_music(const fs::path& scratch) {
    namespace music = opentony::music;
    const fs::path root = scratch / "Music";
    fs::create_directories(root / "2001-THPS3-Trax");
    fs::create_directories(root / "Mine" / "Deeper");
    fs::create_directories(root / "Empty");
    const auto touch = [](const fs::path& file) { std::ofstream(file, std::ios::binary) << "BIKf"; };
    touch(root / "2001-THPS3-Trax" / "Motorhead-Ace.bik");
    touch(root / "2001-THPS3-Trax" / "afi-boydestroyed.BIK");
    touch(root / "2001-THPS3-Trax" / "aa-afi.img.wpc");   // album art: not a song
    std::ofstream(root / "2001-THPS3-Trax" / "Pack.INI", std::ios::binary)
        << "\xef\xbb\xbf[PackInfo]\r\nName=Tony Hawk's Pro Skater 3\r\n\r\n[AFI-BoyDestroyed]\r\nband=AFI\r\ntrack_title=The Boy Who Destroyed the World\r\n"
           "[Motorhead-Ace]\r\nband = Motorhead \r\ntrack_title=Ace of Spades\r\n";
    touch(root / "Loose Song.bik");
    touch(root / "Mine" / "Deeper" / "b.bik");
    touch(root / "Mine" / "Deeper" / "A.bik");
    touch(root / "Empty" / "notes.txt");
    const std::vector<music::Folder> folders = music::scan(root);
    CHECK(folders.size() == 3);
    if (folders.size() == 3) {
        CHECK(folders[0].name == "." && folders[0].tracks.size() == 1 && folders[0].tracks[0].title == "Loose Song" && folders[0].tracks[0].band.empty());
        CHECK(folders[1].name == "2001-THPS3-Trax" && folders[1].pack_name == "Tony Hawk's Pro Skater 3" && folders[1].tracks.size() == 2);
        if (folders[1].tracks.size() == 2) {
            CHECK(folders[1].tracks[0].band == "AFI" && folders[1].tracks[0].title == "The Boy Who Destroyed the World");   // the section matched whatever the case
            CHECK(folders[1].tracks[1].band == "Motorhead" && folders[1].tracks[1].title == "Ace of Spades");
        }
        CHECK(folders[2].name == "Mine/Deeper" && folders[2].tracks.size() == 2 && folders[2].tracks[0].title == "A" && folders[2].tracks[1].title == "b");
    }
    CHECK(music::scan(scratch / "no such folder").empty());
    CHECK(music::split_list(" Mine/Deeper | . ||") == std::vector<std::string>({"Mine/Deeper", "."}) && music::join_list({"a", "b c"}) == "a|b c");
    CHECK(music::is_off(music::split_list("a|."), ".") && !music::is_off(music::split_list("a|."), "b"));

    // the playlist moved by the game's own walk: its level songs 16..30 (31 has no length), the next with a length or the one before, wrapping
    const auto walk = [](int from, int direction) {
        int index = from;
        for (int k = 0; k < 32; ++k) {
            index = (index + direction + 32) % 32;
            if (index >= 16 && index <= 30) {
                return index;
            }
        }
        return -1;
    };
    std::mt19937 random(7);
    {
        music::Playlist list(4, false, random);
        CHECK(list.request(23, walk, random) == 0 && list.moved() && list.place() == 0);   // the first request (a random first song) starts the list
        CHECK(list.request(24, walk, random) == 1 && list.moved());                       // a song ended, or TRACK: on
        CHECK(list.request(25, walk, random) == 2);
        CHECK(list.request(24, walk, random) == 1 && list.moved());                       // TRACK's left: back
        CHECK(list.request(24, walk, random) == 1 && !list.moved());                      // the same track again: the same song
        CHECK(list.request(25, walk, random) == 2 && list.request(26, walk, random) == 3);
        CHECK(list.request(27, walk, random) == 0);                                       // past the end: the top again
        CHECK(list.request(26, walk, random) == 3);                                       // back past the top: the end
        CHECK(list.request(30, walk, random) == 0);                                       // a jump (a new level's start): on
        CHECK(list.request(16, walk, random) == 1);                                       // the game's own wrap 30 -> 16 is a step on
        CHECK(list.request(30, walk, random) == 0);                                       // and 16 -> 30 a step back
        // a song that cannot be played is passed over: one on, the game's place kept (its next track is still a step on)
        CHECK(list.advance(random) == 1 && list.moved() && list.advance(random) == 2 && list.advance(random) == 3 && list.advance(random) == 0);
        CHECK(list.request(16, walk, random) == 1);
    }
    {
        // shuffled: every song once a round, a new order each round, never the same song twice at the join
        music::Playlist list(20, true, random);
        int index = 16;
        std::size_t last = 99;
        for (int round = 0; round < 25; ++round) {
            std::vector<bool> seen(20, false);
            for (int k = 0; k < 20; ++k) {
                index = walk(index, 1);
                const std::size_t entry = list.request(index, walk, random);
                CHECK(entry < 20 && !seen[entry] && entry != last);
                seen[entry] = true;
                last = entry;
            }
        }
        music::Playlist one(1, true, random);
        CHECK(one.request(20, walk, random) == 0 && one.request(21, walk, random) == 0 && one.request(20, walk, random) == 0);
    }

    Settings s = default_settings();
    CHECK(!s.custom_music && !s.music_shuffle && !s.music_include_default && s.music_now_playing && s.music_folders_off.empty());   // the song title: on, as Enhanced
    s.custom_music = true;
    s.music_shuffle = true;
    s.music_include_default = true;
    s.music_now_playing = false;
    s.music_folders_off = "2001-THPS3-Trax|.";
    IniFile ini;
    write_settings(ini, s);
    const Settings back = read_settings(IniFile::parse(ini.text()));
    CHECK(back.custom_music && back.music_shuffle && back.music_include_default && !back.music_now_playing && back.music_folders_off == "2001-THPS3-Trax|.");
    const auto value = [](const std::vector<std::pair<std::string, std::string>>& env, const char* name) {
        for (const auto& [key, text] : env) {
            if (key == name) {
                return text;
            }
        }
        return std::string("<absent>");
    };
    const auto on = launch_environment(s, "", 0, "C:\\Games\\THPS2\\Mods\\Music");
    CHECK(value(on, "OT_CUSTOM_MUSIC") == "1" && value(on, "OT_MUSIC_DIR") == "C:\\Games\\THPS2\\Mods\\Music" && value(on, "OT_MUSIC_FOLDERS_OFF") == "2001-THPS3-Trax|.");
    CHECK(value(on, "OT_MUSIC_SHUFFLE") == "1" && value(on, "OT_MUSIC_DEFAULT") == "1" && value(on, "OT_MUSIC_NOW_PLAYING").empty());
    // off (or no folder to read): every switch is unset, so nothing of an earlier run leaks and the game plays its own music
    for (const auto& env : {launch_environment(default_settings(), "", 0, "C:\\Games\\THPS2\\Mods\\Music"), launch_environment(s, "")}) {
        for (const char* name : {"OT_CUSTOM_MUSIC", "OT_MUSIC_DIR", "OT_MUSIC_FOLDERS_OFF", "OT_MUSIC_SHUFFLE", "OT_MUSIC_DEFAULT"}) {
            CHECK(value(env, name).empty());
        }
    }
    // the song's name is a HUD setting: it follows its own tick box with the custom soundtrack off, and is unset (retail) when ticked off
    CHECK(value(launch_environment(default_settings(), "", 0, "C:\\Games\\THPS2\\Mods\\Music"), "OT_MUSIC_NOW_PLAYING") == "1");
    CHECK(value(launch_environment(default_settings(), ""), "OT_MUSIC_NOW_PLAYING") == "1");   // on for a new file (Enhanced)
    CHECK(value(launch_environment(s, ""), "OT_MUSIC_NOW_PLAYING").empty());
}

// The score file (src/launcher/scores.hpp, high scores plan H3): format, append, read, the board's views and OT_SCORES_FILE
void test_scores(const fs::path& scratch) {
    namespace sc = opentony::scores;
    // the file's names are the launcher's lists, and table 13 is the park editor's
    CHECK(sc::kLevelNames.size() == kLevelNames.size() + 1 && std::string(sc::kLevelNames[13]) == "Custom park");
    for (std::size_t k = 0; k < kLevelNames.size(); ++k) {
        CHECK(std::string(sc::kLevelNames[k]) == kLevelNames[k]);
    }
    CHECK(sc::kSkaterNames.size() == kSkaterNames.size());
    for (std::size_t k = 0; k < kSkaterNames.size(); ++k) {
        CHECK(std::string(sc::kSkaterNames[k]) == kSkaterNames[k]);
    }

    // a line, and back
    sc::Record r = sc::make_record(sc::Mode::Single, 12, 0, "A C", 48210, 0);
    r.date = "2026-11-15 21:04:33";
    CHECK(r.level_name == "Warehouse" && r.skater_name == "Tony Hawk");
    CHECK(sc::format_line(r) == "2026-11-15 21:04:33\tsingle\t12\tWarehouse\t0\tTony Hawk\tA C\t48210");
    const auto back = sc::parse_line(sc::format_line(r));
    CHECK(back && back->date == r.date && back->mode == sc::Mode::Single && back->level == 12 && back->skater == 0 && back->initials == "A C" && back->score == 48210);
    CHECK(sc::parse_line(sc::format_line(r) + "\r"));   // a line end an editor made
    sc::Record tabbed = r;
    tabbed.skater_name = "Tab\tName";
    CHECK(sc::format_line(tabbed).find("Tab Name") != std::string::npos);   // a field never holds a separator
    CHECK(sc::make_record(sc::Mode::Career, 13, 99, "AFC", 1, 0).level_name == "Custom park" &&
          sc::make_record(sc::Mode::Career, 13, 99, "AFC", 1, 0).skater_name == "Unknown skater");
    const sc::Record now = sc::make_record(sc::Mode::Career, 0, 1, "AFC", 1, std::time(nullptr));
    CHECK(now.date.size() == 19 && now.date[4] == '-' && now.date[10] == ' ' && now.date[13] == ':');
    // bad lines: no such mode, level out of range, not a number, too few columns
    CHECK(!sc::parse_line("2026-11-15 21:04:33\tfree\t12\tWarehouse\t0\tTony Hawk\tAFC\t1"));
    CHECK(!sc::parse_line("2026-11-15 21:04:33\tsingle\t14\tWarehouse\t0\tTony Hawk\tAFC\t1"));
    CHECK(!sc::parse_line("2026-11-15 21:04:33\tsingle\t12\tWarehouse\t0\tTony Hawk\tAFC\t12a"));
    CHECK(!sc::parse_line("2026-11-15 21:04:33\tsingle\t12"));
    CHECK(!sc::parse_line(""));

    // a new file: the header lines, then one line a run; appending keeps every earlier line
    const fs::path file = scratch / "Saves" / sc::kFileName;
    fs::create_directories(file.parent_path());
    CHECK(sc::load(file).missing);
    const auto run = [](sc::Mode mode, int level, const char* initials, std::int64_t score, const char* date) {
        sc::Record x = sc::make_record(mode, level, 3, initials, score, 0);
        x.date = date;
        return x;
    };
    CHECK(sc::append(file, run(sc::Mode::Single, 12, "AFC", 1000, "2026-11-15 10:00:00")));
    CHECK(sc::append(file, run(sc::Mode::Career, 12, "AFC", 3000, "2026-11-15 10:05:00")));
    {
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.rfind(sc::header_text(), 0) == 0);
        CHECK(text.find("# THPSource2 scores v1\ndate\tmode\tlevel\tlevel_name\tskater\tskater_name\tinitials\tscore\ttime_ms\tjudged\tmedal\n") == 0);
        CHECK(std::count(text.begin(), text.end(), '\n') == 4);
    }
    // a bad line in the middle (the player's edit, with no line end) is skipped and counted, the next append starts a line of its own
    { std::ofstream(file, std::ios::binary | std::ios::app) << "not a run"; }
    CHECK(sc::append(file, run(sc::Mode::Single, 12, "BOB", 3000, "2026-11-15 10:10:00")));
    CHECK(sc::append(file, run(sc::Mode::Single, 1, "AFC", 500, "2026-11-15 10:15:00")));
    CHECK(sc::append(file, run(sc::Mode::Single, 12, "AFC", 2000, "2026-11-15 10:20:00")));
    sc::File loaded = sc::load(file);
    CHECK(!loaded.missing && loaded.bad_lines == 1 && loaded.records.size() == 5);
    CHECK(loaded.records.size() == 5 && loaded.records[2].initials == "BOB" && loaded.records[2].order == 2);

    // the views: best of a level (ties: the earlier run first), by mode, best per level, recent
    const auto top = sc::best(loaded.records, 12, std::nullopt, 10);
    CHECK(top.size() == 4 && top[0].score == 3000 && top[0].mode == sc::Mode::Career && top[1].initials == "BOB" && top[2].score == 2000 && top[3].score == 1000);
    CHECK(sc::best(loaded.records, 12, std::nullopt, 2).size() == 2);
    const auto single = sc::best(loaded.records, 12, sc::Mode::Single, 10);
    CHECK(single.size() == 3 && single[0].initials == "BOB");
    CHECK(sc::best(loaded.records, 12, sc::Mode::Career, 10).size() == 1);
    CHECK(sc::best(loaded.records, 5, std::nullopt, 10).empty());
    const auto per_level = sc::best_per_level(loaded.records, std::nullopt);
    CHECK(per_level.size() == 2 && per_level[0].level == 1 && per_level[1].level == 12 && per_level[1].score == 3000);
    const auto latest = sc::recent(loaded.records, std::nullopt, 3);
    CHECK(latest.size() == 3 && latest[0].date == "2026-11-15 10:20:00" && latest[2].initials == "BOB");
    CHECK(sc::recent(loaded.records, sc::Mode::Career, 20).size() == 1);
    // the new high scores: the session's runs that are now the best of their level (the five runs above: level 12 best is run 1, level 1's run 3)
    CHECK(sc::new_bests(loaded.records, 5).empty());   // a session that added nothing
    CHECK(sc::new_bests(loaded.records, 4).empty());   // 2000 on level 12 is not the best
    {
        const auto fresh = sc::new_bests(loaded.records, 3);   // runs 3 (level 1, its only run) and 4 (level 12, not its best): one new high score
        CHECK(fresh.size() == 1 && fresh[0].level == 1 && fresh[0].score == 500);
        const auto both = sc::new_bests(loaded.records, 0);
        CHECK(both.size() == 2 && both[0].level == 1 && both[1].level == 12 && both[1].score == 3000);
        std::vector<sc::Record> tied = loaded.records;
        sc::Record tie = run(sc::Mode::Single, 12, "ZED", 3000, "2026-11-15 10:30:00");
        tie.order = tied.size();
        tied.push_back(tie);
        CHECK(sc::new_bests(tied, 5).empty());   // a tie goes to the earlier run
    }

    // every run of the session that just ended, record or not, and where each stands on its level (the "Run recorded" callout and the page's This session list)
    {
        const auto session = sc::session_runs(loaded.records, 3);
        CHECK(session.size() == 2 && session[0].level == 1 && session[1].level == 12 && session[1].score == 2000);
        CHECK(sc::session_runs(loaded.records, 5).empty());
        const sc::Placing only = sc::placing(loaded.records, session[0]);   // level 1's only run
        CHECK(only.rank == 1 && only.of == 1);
        const sc::Placing third = sc::placing(loaded.records, session[1]);   // 3000, 3000, then 2000, then 1000
        CHECK(third.rank == 3 && third.of == 4);
        CHECK(sc::board_of(session[1]) == sc::Board::Scores);
    }

    // an empty file gets the header lines too; columns the reader does not know are passed by, and a reordered column line is read by name
    const fs::path empty = scratch / "empty.tsv";
    { std::ofstream touch(empty, std::ios::binary); }
    CHECK(sc::load(empty).records.empty() && !sc::load(empty).missing && sc::load(empty).bad_lines == 0);
    CHECK(sc::append(empty, run(sc::Mode::Career, 0, "AFC", 7, "2026-11-15 11:00:00")));
    CHECK(sc::load(empty).records.size() == 1);
    const sc::File later = sc::parse_text(
        "# THPSource2 scores v2\r\nscore\tcombo\tmode\tlevel\tinitials\tdate\r\n120\t9\tcareer\t3\tZZ \t2027-01-01 00:00:00\r\n\r\n# a remark\r\n77\t1\tsingle\t13\tQQQ\t\r\n");
    CHECK(later.bad_lines == 0 && later.records.size() == 2);
    CHECK(later.records.size() == 2 && later.records[0].score == 120 && later.records[0].level == 3 && later.records[0].initials == "ZZ " &&
          later.records[0].level_name == "New York City" && later.records[0].skater == -1 && later.records[1].level_name == "Custom park");
    // v1's order without a column line, and extra columns at the end
    const sc::File bare = sc::parse_text("2026-11-15 21:04:33\tsingle\t12\tWarehouse\t0\tTony Hawk\tAFC\t48210\textra\n");
    CHECK(bare.records.size() == 1 && bare.records[0].score == 48210);
    CHECK(sc::thousands(48210) == "48,210" && sc::thousands(999) == "999" && sc::thousands(1234567) == "1,234,567" && sc::thousands(0) == "0");

    // the speed runs and the competitions: their lines, an old file's column line given the new columns once, and their boards
    {
        sc::Record speed = sc::make_record(sc::Mode::SpeedRun, 0, 0, "AFC", 912400, 0);
        speed.date = "2026-11-20 18:02:10";
        speed.time_ms = 245320;
        CHECK(sc::format_line(speed) == "2026-11-20 18:02:10\tspeedrun\t0\tHangar\t0\tTony Hawk\tAFC\t912400\t245320\t\t");
        const auto speed_back = sc::parse_line(sc::format_line(speed));
        CHECK(speed_back && speed_back->mode == sc::Mode::SpeedRun && speed_back->time_ms == 245320 && speed_back->judged == -1);
        CHECK(!sc::parse_line("2026-11-20 18:02:10\tspeedrun\t0\tHangar\t0\tTony Hawk\tAFC\t912400"));   // a speed run without its time
        sc::Record comp = sc::make_record(sc::Mode::Competition, 3, 0, "AFC", 151230, 0);
        comp.judged = 1786;
        comp.medal = "gold";
        const auto comp_back = sc::parse_line(sc::format_line(comp));
        CHECK(comp_back && comp_back->mode == sc::Mode::Competition && comp_back->judged == 1786 && comp_back->medal == "gold" && comp_back->score == 151230);
        CHECK(sc::run_time(245320) == "4:05.32" && sc::run_time(59999) == "0:59.99" && sc::run_time(3600000) == "60:00.00" && sc::judged_text(1786) == "178.6");

        const fs::path old = scratch / "old.tsv";
        { std::ofstream(old, std::ios::binary) << "# THPSource2 scores v1\ndate\tmode\tlevel\tlevel_name\tskater\tskater_name\tinitials\tscore\n"
                                                  "2026-11-15 21:04:33\tsingle\t0\tHangar\t0\tTony Hawk\tAFC\t48210\n"; }
        CHECK(sc::append(old, run(sc::Mode::Career, 0, "AFC", 9000, "2026-11-20 10:00:00")));   // a score run: no new column line
        speed.time_ms = 300000;
        CHECK(sc::append(old, speed));
        speed.time_ms = 200000;
        CHECK(sc::append(old, speed));
        sc::Record cash = speed;
        cash.mode = sc::Mode::SpeedRunCash;
        cash.time_ms = 400000;
        CHECK(sc::append(old, cash));
        CHECK(sc::append(old, comp));
        comp.judged = 1500;
        comp.score = 999999;
        comp.medal = "none";
        CHECK(sc::append(old, comp));
        std::ifstream in(old, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::size_t lines = 0;
        for (std::size_t at = text.find("date\tmode"); at != std::string::npos; at = text.find("date\tmode", at + 1)) {
            ++lines;
        }
        CHECK(lines == 2);   // the old column line, and the new one once
        const sc::File both = sc::load(old);
        CHECK(both.bad_lines == 0 && both.records.size() == 7);
        const auto fastest = sc::best(both.records, 0, sc::Board::SpeedRuns, 10);
        CHECK(fastest.size() == 2 && fastest[0].time_ms == 200000 && fastest[1].time_ms == 300000);
        CHECK(sc::best(both.records, 0, sc::Board::SpeedRunsCash, 10).size() == 1);
        const auto judged = sc::best(both.records, 3, sc::Board::Competitions, 10);
        CHECK(judged.size() == 2 && judged[0].judged == 1786 && judged[1].judged == 1500);   // by the total, not the trick score
        // the score board: the career and single runs only, never a speed run or a competition's heat
        const auto scored = sc::best(both.records, 0, std::nullopt, 10);
        CHECK(scored.size() == 2 && scored[0].score == 48210);
        CHECK(sc::best(both.records, 3, sc::Board::Scores, 10).empty());
        CHECK(sc::recent(both.records, std::nullopt, 20).size() == 2 && sc::recent(both.records, sc::Board::SpeedRuns, 20).size() == 2);
    }

    // the launcher passes the file's place on, and an empty one records nothing
    const auto value = [](const std::vector<std::pair<std::string, std::string>>& env, const char* name) {
        for (const auto& [key, text] : env) {
            if (key == name) {
                return text;
            }
        }
        return std::string("<absent>");
    };
    CHECK(value(launch_environment(default_settings(), "", 0, "", "C:\\Games\\THPS2\\Saves\\highscores.tsv"), "OT_SCORES_FILE") == "C:\\Games\\THPS2\\Saves\\highscores.tsv");
    CHECK(value(launch_environment(default_settings(), ""), "OT_SCORES_FILE").empty());
    // the game's tables from the file: always, whenever there is a file (no setting); an old file's ScoresInGame=0 is ignored
    CHECK(value(launch_environment(default_settings(), "", 0, "", "C:\\x\\highscores.tsv"), "OT_SCORES_IN_GAME") == "1");
    CHECK(value(launch_environment(default_settings(), ""), "OT_SCORES_IN_GAME").empty());
    IniFile old_ini;
    old_ini.set("Game", "ScoresInGame", "0");
    CHECK(value(launch_environment(read_settings(old_ini), "", 0, "", "C:\\x\\highscores.tsv"), "OT_SCORES_IN_GAME") == "1");

    // Upscaled textures (texture-upscaling T5): off by default; the folder only when ticked, a set exists and texture detail is on; round trip; presets
    Settings up = default_settings();
    CHECK(!up.upscaled_textures);
    CHECK(value(launch_environment(up, "", 0, "", "", "C:\\x\\Data\\Upscaled"), "OT_TEXTURE_PACK").empty());
    up.upscaled_textures = true;
    up.texture_filter = 1;
    up.anisotropy = 16;
    CHECK(value(launch_environment(up, "", 0, "", "", "C:\\x\\Data\\Upscaled"), "OT_TEXTURE_PACK") == "C:\\x\\Data\\Upscaled");
    CHECK(value(launch_environment(up, "", 0, "", "", ""), "OT_TEXTURE_PACK").empty());   // no set
    Settings no_detail = up;
    no_detail.anisotropy = 0;
    CHECK(value(launch_environment(no_detail, "", 0, "", "", "C:\\x\\Data\\Upscaled"), "OT_TEXTURE_PACK").empty());
    Settings sharp = up;
    sharp.texture_filter = 0;
    CHECK(!upscaled_textures_usable(sharp) && upscaled_textures_usable(up));
    IniFile up_ini;
    write_settings(up_ini, up);
    CHECK(read_settings(up_ini).upscaled_textures && up_ini.get_string("Graphics", "UpscaledTextures") == "1");
    Settings preset = up;
    apply_preset(preset, 0, true);
    CHECK(preset.upscaled_textures && preset_of(preset, true) == 0 && preset_of(preset, false) == -1);   // Enhanced uses the set once it has been made
    apply_preset(preset, 0, false);
    CHECK(!preset.upscaled_textures && preset_of(preset, false) == 0);
    apply_preset(preset, 1);
    CHECK(!preset.upscaled_textures);
    preset.upscaled_textures = true;
    apply_preset(preset, 2);
    CHECK(!preset.upscaled_textures);
    // True-colour textures (graphics-2 G5): on by default, used with the PC textures only, after the upscaled folder in the pack; round trip; Enhanced on, the originals off
    Settings tc = default_settings();
    CHECK(tc.true_colour && true_colour_usable(tc));
    CHECK(value(launch_environment(tc, "", 0, "", "", "", "C:\\x\\Data\\TrueColour"), "OT_TEXTURE_PACK") == "C:\\x\\Data\\TrueColour");
    CHECK(value(launch_environment(tc, "", 0, "", "", "", ""), "OT_TEXTURE_PACK").empty());
    Settings tc_up = up;
    tc_up.true_colour = true;
    CHECK(value(launch_environment(tc_up, "", 0, "", "", "C:\\x\\Data\\Upscaled", "C:\\x\\Data\\TrueColour"), "OT_TEXTURE_PACK") == "C:\\x\\Data\\Upscaled|C:\\x\\Data\\TrueColour");
    tc_up.upscaled_textures = false;
    CHECK(value(launch_environment(tc_up, "", 0, "", "", "C:\\x\\Data\\Upscaled", "C:\\x\\Data\\TrueColour"), "OT_TEXTURE_PACK") == "C:\\x\\Data\\TrueColour");
    tc.use_psx_textures = true;
    CHECK(!true_colour_usable(tc) && value(launch_environment(tc, "", 0, "", "", "", "C:\\x\\Data\\TrueColour"), "OT_TEXTURE_PACK").empty());
    tc.use_psx_textures = false;
    tc.true_colour = false;
    IniFile tc_ini;
    write_settings(tc_ini, tc);
    CHECK(!read_settings(tc_ini).true_colour && tc_ini.get_string("Graphics", "TrueColour") == "0");
    IniFile no_key;
    CHECK(read_settings(no_key).true_colour);   // a file without the key gets the default
    apply_preset(tc, 0);
    CHECK(tc.true_colour && preset_of(tc) == 0);
    apply_preset(tc, 1);
    CHECK(!tc.true_colour);
    tc.true_colour = true;
    apply_preset(tc, 2);
    CHECK(!tc.true_colour);
    // ambient occlusion (graphics-2 G8): High by default and in Enhanced, off in the originals, three values, round trip, clamped, its own environment variable
    Settings ao = default_settings();
    CHECK(ao.ambient_occlusion == 2 && value(launch_environment(ao, "", 0, "", "", "", ""), "OT_AMBIENT_OCCLUSION") == "high");
    ao.ambient_occlusion = 2;
    CHECK(value(launch_environment(ao, "", 0, "", "", "", ""), "OT_AMBIENT_OCCLUSION") == "high");
    ao.ambient_occlusion = 1;
    CHECK(value(launch_environment(ao, "", 0, "", "", "", ""), "OT_AMBIENT_OCCLUSION") == "low");
    IniFile ao_ini;
    write_settings(ao_ini, ao);
    CHECK(read_settings(ao_ini).ambient_occlusion == 1 && ao_ini.get_string("Graphics", "AmbientOcclusion") == "1");
    CHECK(read_settings(IniFile::parse("[Launcher]\nSettingsVersion=2\n[Graphics]\nAmbientOcclusion=9\n")).ambient_occlusion == 2);
    CHECK(read_settings(IniFile{}).ambient_occlusion == 2 && read_settings(IniFile::parse("[Launcher]\nSettingsVersion=2\n")).ambient_occlusion == 0);   // a new file is Enhanced; an older launcher file keeps off
    for (int preset = 0; preset < 3; ++preset) {
        ao.ambient_occlusion = 1;
        apply_preset(ao, preset);
        CHECK(ao.ambient_occlusion == (preset == 0 ? 2 : 0));
    }
    apply_preset(ao, 0);
    ao.ambient_occlusion = 0;
    CHECK(preset_of(ao) == -1 && preset_of(default_settings()) == 0);   // part of a preset's match
    // the presets' display and gameplay options, the custom presets' file, and the light theme
    {
        Settings d = default_settings();
        CHECK(preset_of(d) == 0);
        apply_preset(d, 1);
        CHECK(d.aspect == 1 && !d.fov_adjust && !d.hud_anchor && d.hud_size == 100 && !d.grind_meter && !d.music_now_playing && d.frame_rate == 30 && preset_of(d) == 1);
        CHECK(!d.camera_stick && d.autokick == 2 && d.vibration == 2 && !d.play_movies && !d.psx_colour && d.fog_distance == 150 && d.internal_resolution == 1);
        apply_preset(d, 2);
        CHECK(d.aspect == 1 && !d.grind_meter && d.frame_rate == 30 && preset_of(d) == 2);
        CHECK(!d.camera_stick && d.scaling == 3 && d.psx_dither && d.internal_resolution == 0 && d.use_psx_textures && d.texture_filter == 0);
        apply_preset(d, 0);
        CHECK(d.aspect == 0 && d.fov_adjust && d.hud_anchor && d.hud_size == 0 && d.frame_rate == 60 && d.music_now_playing && !d.grind_meter && preset_of(d) == 0);
        CHECK(d.camera_stick && d.autokick == 1 && !d.play_movies);
        d.frame_rate = 120;
        CHECK(preset_of(d) == -1);   // the frame rate is part of the match
        d.frame_rate = 60;
        d.aspect = 3;
        CHECK(preset_of(d) == -1);
        // names: not empty, not a built-in's, no brackets or surrounding spaces
        CHECK(preset_name_valid("My look") && !preset_name_valid("") && !preset_name_valid("enhanced") && !preset_name_valid("Original PlayStation") &&
              !preset_name_valid(" a") && !preset_name_valid("[a]") && !preset_name_valid(std::string(kPresetNameMax + 1, 'x')));
        // the custom presets' file: the preset options round-trip, nothing else is kept, a key a preset lacks is the default, a bad name is skipped
        Settings mine = default_settings();
        apply_preset(mine, 2);
        mine.frame_rate = 120;
        mine.fog_distance = 275;
        mine.music_level = 1;   // not a preset option
        const std::vector<CustomPreset> saved = {{"Telly", mine}, {"Enhanced", default_settings()}};
        const IniFile file = write_presets(saved);
        CHECK(file.get_string("Telly", "FrameRate") == "120" && file.get_string("Telly", "Scaling") == "3" && !file.get("Telly", "MusicLevel"));
        const std::vector<CustomPreset> back = read_presets(IniFile::parse(file.text()));
        CHECK(back.size() == 1 && back[0].name == "Telly" && same_preset_options(back[0].values, mine) && back[0].values.music_level == default_settings().music_level);
        Settings onto = default_settings();
        copy_preset_options(back[0].values, onto);
        CHECK(same_preset_options(onto, mine) && preset_of(onto) == -1 && onto.music_level == default_settings().music_level);
        const std::vector<CustomPreset> partial = read_presets(IniFile::parse("[Half]\nAspect=1\n"));
        CHECK(partial.size() == 1 && partial[0].values.aspect == 1 && partial[0].values.msaa == default_settings().msaa);
        CHECK(default_settings().theme == Theme::System);
        Settings light = default_settings();
        light.theme = Theme::Light;
        IniFile light_ini;
        write_settings(light_ini, light);
        CHECK(read_settings(light_ini).theme == Theme::Light && light_ini.get_string("Launcher", "Theme") == "light" && read_settings(IniFile{}).theme == Theme::System);
        Settings dark = default_settings();
        dark.theme = Theme::Dark;
        IniFile dark_ini;
        write_settings(dark_ini, dark);
        CHECK(read_settings(dark_ini).theme == Theme::Dark && dark_ini.get_string("Launcher", "Theme") == "dark" && read_settings(IniFile::parse("[Launcher]\nTheme=bogus\n")).theme == Theme::System);
        write_settings(dark_ini, default_settings());
        CHECK(dark_ini.get_string("Launcher", "Theme") == "system");
    }
    // CRT scaling and the PlayStation dither (graphics-2 G7): the fourth Scaling value, the dither only at the PSX size, off in every preset, round trip
    Settings crt = default_settings();
    CHECK(!crt.psx_dither && value(launch_environment(crt, "", 0, "", "", "", ""), "OT_PSX_DITHER") == "0");
    crt.scaling = 3;
    CHECK(value(launch_environment(crt, "", 0, "", "", "", ""), "OT_SCALING") == "smooth");   // Native: always Clean
    crt.internal_resolution = 1;
    CHECK(value(launch_environment(crt, "", 0, "", "", "", ""), "OT_SCALING") == "crt");
    crt.psx_dither = true;
    crt.internal_resolution = 2;
    CHECK(value(launch_environment(crt, "", 0, "", "", "", ""), "OT_PSX_DITHER") == "0");   // not at the window's size
    crt.internal_resolution = 0;
    CHECK(value(launch_environment(crt, "", 0, "", "", "", ""), "OT_PSX_DITHER") == "1");
    IniFile crt_ini;
    write_settings(crt_ini, crt);
    CHECK(read_settings(crt_ini).psx_dither && read_settings(crt_ini).scaling == 3 && crt_ini.get_string("Graphics", "PSXDither") == "1");
    CHECK(read_settings(IniFile::parse("[Launcher]\nSettingsVersion=2\n[Graphics]\nScaling=9\n")).scaling == 3);
    CHECK(!read_settings(IniFile{}).psx_dither);
    for (int preset = 0; preset < 3; ++preset) {
        crt.psx_dither = preset != 2;
        apply_preset(crt, preset);
        CHECK(crt.psx_dither == (preset == 2) && (crt.scaling == 3) == (preset == 2));   // the PlayStation's look is the console on a TV
        CHECK(crt.crt_curve == (preset == 2));   // the curved screen with it, and with it only
    }
    // the curved screen is its own option: any output and any render resolution carry it, it round-trips, and a file without the key reads it off
    {
        Settings curve = default_settings();
        CHECK(!curve.crt_curve && value(launch_environment(curve, "", 0, "", "", "", ""), "OT_CRT_CURVE") == "0");
        curve.crt_curve = true;
        for (const int resolution : {0, 1, 2}) {
            for (const int look : {0, 1, 2, 3}) {
                curve.internal_resolution = resolution;
                curve.scaling = look;
                CHECK(value(launch_environment(curve, "", 0, "", "", "", ""), "OT_CRT_CURVE") == "1");
            }
        }
        IniFile curve_ini;
        write_settings(curve_ini, curve);
        CHECK(read_settings(curve_ini).crt_curve && curve_ini.get_string("Graphics", "CrtCurve") == "1" && !read_settings(IniFile{}).crt_curve);
        // and it is part of a preset's match
        Settings psx_preset = default_settings();
        apply_preset(psx_preset, 2);
        CHECK(preset_of(psx_preset) == 2);
        psx_preset.crt_curve = false;
        CHECK(preset_of(psx_preset) == -1);
    }
    crt.psx_dither = false;
    CHECK(preset_of(crt) == -1);   // part of a preset's match
    CHECK(opentony::layout::Layout{"C:/THPS2"}.scores() == fs::path("C:/THPS2") / "Saves" / "highscores.tsv");
}

// The Quick Start's trick sets: the combinations' names and pages, the labels, the file, and (with the game's TRICKS.BIN, the path given as the first argument or
// OPENTONY_TRICKS_BIN) the catalog: every skater's own set and the block the game takes
void test_trick_sets(const char* tricks_bin) {
    const auto& combos = trick_combos();
    CHECK(combos[0].key == "Up+Grind" && combos[0].label == "\xE2\x86\x91 + Grind" && combos[0].page == TrickPage::Lip);
    CHECK(combos[4].key == "Up,Up+Grind" && combos[4].label == "\xE2\x86\x91, \xE2\x86\x91 + Grind" && combos[4].page == TrickPage::Grind);
    CHECK(combos[6].key == "Up+Flip" && combos[6].page == TrickPage::Flip);
    CHECK(combos[7].key == "UpRight+Flip" && combos[7].label == "\xE2\x86\x97 + Flip");
    CHECK(combos[14].key == "Up+Grab" && combos[14].page == TrickPage::Grab);
    CHECK(combos[22].key == "Up,Up+Flip" && combos[29].key == "Left,Left+Grab");
    int pages[kTrickPageCount] = {};
    for (const TrickCombo& combo : combos) {
        ++pages[static_cast<int>(combo.page)];
    }
    CHECK(pages[0] == 12 && pages[1] == 12 && pages[2] == 4 && pages[3] == 2 && pages[4] == 0);
    const auto& specials = special_trick_combos();
    CHECK(specials[0].key == "Up,Right+Grind" && specials[12].key == "Up,Right+Flip" && specials[35].key == "Left,Down+Grab");
    CHECK(find_special_combo("right,down+grab") >= 0 && find_trick_combo("UP+FLIP") == 6 && find_trick_combo("Up+Ollie") == -1);
    CHECK(trick_label("kickflip to indy") == "Kickflip to Indy");
    CHECK(trick_label("fs shove it") == "FS Shove It");
    CHECK(trick_label("the 900") == "The 900");
    CHECK(trick_label("180 rock n roll") == "180 Rock n Roll");
    CHECK(trick_label("beni f-flip. crooks") == "Beni F-Flip. Crooks");
    CHECK(trick_label("to") == "To");
    CHECK(trick_set_name_valid("My tricks") && !trick_set_name_valid("") && !trick_set_name_valid(" a") && !trick_set_name_valid("a]") &&
          !trick_set_name_valid(";a") && !trick_set_name_valid(std::string(41, 'a')));

    TrickSet set;
    set.name = "Mine";
    set.combos[6] = "kickflip";
    set.specials[1] = {"Right,Down+Grab", "the 900"};
    TrickSet other;
    other.name = "Second";
    TrickSet bad;
    bad.name = "bad]";
    const std::vector<TrickSet> sets = {set, other, bad};
    const IniFile file = write_trick_sets(sets);
    const std::vector<TrickSet> back = read_trick_sets(IniFile::parse(file.text()));
    CHECK(back.size() == 2 && back[0] == set && back[1] == other);
    CHECK(file.text().find("[Mine]\r\nUp+Grind=\r\n") != std::string::npos && file.text().find("Up+Flip=kickflip\r\n") != std::string::npos);
    CHECK(file.text().find("Special2=Right,Down+Grab: the 900\r\n") != std::string::npos);
    const std::vector<TrickSet> edited = read_trick_sets(IniFile::parse("[Hand]\nup+flip = Kickflip\nSpecial1 = right,down+grab : The 900\n"));
    CHECK(edited.size() == 1 && edited[0].combos[6] == "kickflip");
    CHECK(edited[0].specials[0].combo == "Right,Down+Grab" && edited[0].specials[0].trick == "the 900");

    // the set chosen on the Quick Start page is stored; the block reaches the game only with a quick start (PLAY never passes it)
    Settings quick = default_settings();
    quick.quick_trick_set = "Mine";
    IniFile stored;
    write_settings(stored, quick);
    CHECK(read_settings(stored).quick_trick_set == "Mine" && stored.get_string("Game", "QuickTrickSet") == "Mine");
    quick.quick_tricks = std::string(106, 'f');
    const auto tricks_env = [](const Settings& s) {
        for (const auto& [key, text] : launch_environment(s, "")) {
            if (key == "OT_QUICK_TRICKS") {
                return text;
            }
        }
        return std::string("missing");
    };
    CHECK(tricks_env(quick).empty());
    quick.quick_start = true;
    CHECK(tricks_env(quick) == quick.quick_tricks);
    quick.quick_tricks = "ff";
    CHECK(tricks_env(quick).empty());

    std::string path = tricks_bin != nullptr ? tricks_bin : "";
    if (const char* given = std::getenv("OPENTONY_TRICKS_BIN"); given != nullptr && *given != '\0') {
        path = given;
    }
    std::string error;
    CHECK(!TrickCatalog::load("no such file", error) && !error.empty());
    const auto catalog = path.empty() ? std::nullopt : TrickCatalog::load(path, error);
    if (!catalog) {
        std::printf("launcher_core_test: no TRICKS.BIN (%s), the trick catalog is not checked\n", path.empty() ? "none given" : error.c_str());
        return;
    }
    CHECK(catalog->tricks().size() == 105);
    const int kickflip = catalog->find_trick("Kickflip");
    const int the_900 = catalog->find_trick("the 900");
    const int handplant = catalog->find_trick("handplant");
    const int bluntslide = catalog->find_trick("bluntslide");
    const int casper = catalog->find_trick("casper");
    const int overturn = catalog->find_trick("overturn");
    CHECK(kickflip >= 0 && the_900 >= 0 && handplant >= 0 && bluntslide >= 0 && casper >= 0 && overturn >= 0);
    CHECK(catalog->tricks()[static_cast<std::size_t>(the_900)].special && catalog->tricks()[static_cast<std::size_t>(the_900)].kind == TrickKind::Grab);
    CHECK(catalog->tricks()[static_cast<std::size_t>(casper)].kind == TrickKind::Manual);
    CHECK(catalog->fits(6, kickflip) && catalog->fits(14, kickflip) && !catalog->fits(0, kickflip) && !catalog->fits(6, the_900));
    CHECK(catalog->fits(0, handplant) && !catalog->fits(4, handplant) && catalog->fits(4, bluntslide) && !catalog->fits(6, bluntslide));
    const int grab_special = find_special_combo("Right,Down+Grab");
    const int grind_special = find_special_combo("Down,Left+Grind");
    CHECK(catalog->fits_special(grab_special, the_900) && !catalog->fits_special(grind_special, the_900) && catalog->fits_special(grab_special, casper));
    CHECK(catalog->fits_special(grind_special, overturn) && !catalog->fits_special(grab_special, overturn) && !catalog->fits_special(grab_special, kickflip));
    // Tony Hawk's own set as a new career starts it: the 900 on Right, Down + Grab, three specials, Kickflip on Left + Flip
    const TrickSet hawk = catalog->skater_set(0);
    CHECK(hawk.combos[12] == "kickflip" && hawk.combos[23].empty());
    CHECK(hawk.specials[1].combo == "Right,Down+Grab" && hawk.specials[1].trick == "the 900" && hawk.specials[3].combo.empty());
    std::vector<std::string> problems;
    const auto block = catalog->block(hawk, &problems);
    CHECK(problems.empty() && block[12] == kickflip && block[30] == 0xff && block[42] == 0xff);
    CHECK(block[0x2c] == the_900 && block[0x31] == grab_special && block[0x2e] == 0xff && block[0x34] == 0xff);
    CHECK(trick_block_hex(block).size() == 106);
    for (int skater = 0; skater < 20; ++skater) {   // every skater's own set goes into a block whole
        problems.clear();
        (void)catalog->block(catalog->skater_set(skater), &problems);
        CHECK(problems.empty());
    }
    // what does not fit is left out, with a line each
    TrickSet wrong;
    wrong.combos[0] = "kickflip";       // a flip on a lip combination
    wrong.combos[1] = "no such trick";
    wrong.specials[0] = {"Right,Down+Grab", "the 900"};
    wrong.specials[1] = {"Up,Down+Grab", "the 900"};          // the same special twice
    wrong.specials[2] = {"Right,Down+Grab", "sacktap"};       // the same combination twice
    wrong.specials[3] = {"Right,Down+Grind", "the 900"};      // a grab special on a Grind combination
    wrong.specials[4] = {"Up,Left+Flip", ""};                 // no trick
    problems.clear();
    const auto left_out = catalog->block(wrong, &problems);
    CHECK(problems.size() == 6 && left_out[0] == 0xff && left_out[1] == 0xff);
    CHECK(left_out[0x2b] == the_900 && left_out[0x2c] == 0xff && left_out[0x30] == grab_special && left_out[0x31] == 0xff);
}

// The update notice's rules (core/update_check.hpp): what is a version, which is newer, and which redirect is believed
void test_update_check() {
    namespace up = thpsource2::update;
    const std::string repo = "https://github.com/afcurtis/THPSource2";
    // versions: a v, one to four numbers; anything else is no version
    CHECK(up::parse_version("v0.8") && up::parse_version("V1.2.3") && up::parse_version("0.10") && up::parse_version("7") && up::parse_version("1.2.3.4"));
    CHECK(!up::parse_version("") && !up::parse_version("v") && !up::parse_version("v.") && !up::parse_version("0.") && !up::parse_version(".8") && !up::parse_version("0..8"));
    CHECK(!up::parse_version("v0.8-rc1") && !up::parse_version("v0.8 ") && !up::parse_version("vv0.8") && !up::parse_version("1.2.3.4.5") && !up::parse_version("0.x") &&
          !up::parse_version("1234567") && !up::parse_version("-1") && !up::parse_version("v0.8/../x"));
    CHECK(*up::parse_version("v0.8") == *up::parse_version("0.8.0") && !(*up::parse_version("v0.8") == *up::parse_version("0.8.1")));
    // newer is numeric, part by part: 0.10 is after 0.9, never before
    CHECK(up::is_newer("v0.8", "v0.7") && up::is_newer("v0.10", "v0.9") && up::is_newer("v1.0", "v0.99") && up::is_newer("v0.7.1", "v0.7") && up::is_newer("v0.7.0.1", "v0.7"));
    CHECK(!up::is_newer("v0.7", "v0.7") && !up::is_newer("v0.7.0", "v0.7") && !up::is_newer("v0.6", "v0.7") && !up::is_newer("v0.9", "v0.10"));
    CHECK(!up::is_newer("nightly", "v0.7") && !up::is_newer("v0.8", "dev") && !up::is_newer("", "") && !up::is_newer("v0.8-beta", "v0.7"));
    // the redirect: this repository's release page, a version for a tag, nothing else is believed
    CHECK(up::tag_from_redirect(repo + "/releases/tag/v0.8", repo) == std::optional<std::string>("v0.8"));
    CHECK(up::tag_from_redirect(repo + "/releases/tag/v0.8\r\n", repo) == std::optional<std::string>("v0.8"));   // as curl's write-out and the pipe leave it
    CHECK(up::tag_from_redirect("https://GitHub.com/AFCurtis/thpsource2/releases/tag/v1.0.2", repo) == std::optional<std::string>("v1.0.2"));   // GitHub's own capitals
    CHECK(up::tag_from_redirect(repo + "/releases/tag/v0.8", repo + "/") == std::optional<std::string>("v0.8"));
    CHECK(!up::tag_from_redirect("", repo) && !up::tag_from_redirect(repo + "/releases", repo) && !up::tag_from_redirect(repo + "/releases/latest", repo) &&
          !up::tag_from_redirect(repo + "/releases/tag/", repo) && !up::tag_from_redirect(repo + "/releases/tag/v0.8-rc1", repo) &&
          !up::tag_from_redirect(repo + "/releases/tag/v0.8/extra", repo) && !up::tag_from_redirect(repo + "/releases/tag/v0.8%20x", repo) &&
          !up::tag_from_redirect(repo + "/releases/tag/v0.8?x=1", repo) && !up::tag_from_redirect(repo + "/releases/tag/nightly", repo));
    CHECK(!up::tag_from_redirect("https://github.com/someone/else/releases/tag/v9.9", repo) && !up::tag_from_redirect("https://evil.example/afcurtis/THPSource2/releases/tag/v9.9", repo) &&
          !up::tag_from_redirect(repo + "-fork/releases/tag/v9.9", repo) && !up::tag_from_redirect("http://github.com/afcurtis/THPSource2/releases/tag/v9.9", repo));
    CHECK(!up::tag_from_redirect(repo + "/releases/tag/v0.8", ""));
}


// The release the update installs: the GitHub API's answer, the notes made plain, the checksum file, the addresses and the script
void test_update_release() {
    namespace up = thpsource2::update;
    const std::string repo = "https://github.com/afcurtis/THPSource2";
    const std::string json =
        R"({"url":"https://api.github.com/repos/afcurtis/THPSource2/releases/1","tag_name":"v0.8","draft":false,"author":{"login":"afcurtis","body":"not this"},)"
        R"("body":"## What's new\r\n\r\n* **Faster** menus — see [the README](https://example.com/x#y)\r\n* `OT_X` is gone 😀\r\n\r\n\r\n\r\nThanks é","assets":[)"
        R"({"name":"thpsource2-win64.zip.sha256","size":87},{"name":"thpsource2-win64.zip","size":8156075,"browser_download_url":"https://evil.example/x.zip"}]})";
    const std::optional<up::Release> release = up::parse_release(json, "v0.8");
    CHECK(release && release->tag == "v0.8" && release->zip_size == 8156075);
    CHECK(release && release->notes == "What's new\n\n- Faster menus - see the README\n- OT_X is gone\n\nThanks \xc3\xa9");
    CHECK(!up::parse_release(json, "v0.9") && !up::parse_release("", "v0.8") && !up::parse_release("[]", "v0.8") && !up::parse_release(R"({"tag_name":"v0.8"} x)", "v0.8") &&
          !up::parse_release(R"({"tag_name":"v0.8","body":"unterminated)", "v0.8") && !up::parse_release(R"({"tag_name":"v0.8","body":"\q"})", "v0.8"));
    const std::optional<up::Release> bare = up::parse_release(R"({"tag_name":"v0.8"})", "v0.8");   // no notes, no assets: still the release
    CHECK(bare && bare->notes.empty() && bare->zip_size == 0);
    CHECK(!up::parse_release(std::string(300, '[') + std::string(300, ']'), "v0.8"));   // nested too deep to be a release
    CHECK(up::plain_notes("# One\n## Two\ntext\r\n> quoted\n![shot](a.png) and [a](b) [c](d)\n+ plus\n  * indented").find("](") == std::string::npos);
    CHECK(up::plain_notes("a\n\n\n\n\nb") == "a\n\nb" && up::plain_notes("\n\n") .empty() && up::plain_notes("**bold** __x__ ~~y~~") == "bold x y");
    CHECK(up::plain_notes(std::string(500, 'x') + "\n" + std::string(500, 'y'), 600) == std::string(500, 'x') + "\n...");
    const std::string hash = "73c05042bb96cff977a5e53fd953bcfb91d453de906293ae87add109f7ef82da";
    CHECK(up::checksum_from_file(hash + "  thpsource2-win64.zip\n", up::kPackageName) == std::optional<std::string>(hash));
    CHECK(up::checksum_from_file(hash + " *thpsource2-win64.zip\r\n", up::kPackageName) == std::optional<std::string>(hash));
    CHECK(up::checksum_from_file("73C05042BB96CFF977A5E53FD953BCFB91D453DE906293AE87ADD109F7EF82DA  thpsource2-win64.zip", up::kPackageName) == std::optional<std::string>(hash));
    CHECK(!up::checksum_from_file(hash + "  other.zip\n", up::kPackageName) && !up::checksum_from_file(hash.substr(1) + "  thpsource2-win64.zip", up::kPackageName) &&
          !up::checksum_from_file("zz" + hash.substr(2) + "  thpsource2-win64.zip", up::kPackageName) && !up::checksum_from_file(hash + "thpsource2-win64.zip", up::kPackageName) &&
          !up::checksum_from_file("", up::kPackageName));
    CHECK(up::api_url(repo, "v0.8") == "https://api.github.com/repos/afcurtis/THPSource2/releases/tags/v0.8" && up::api_url("http://elsewhere/x/y", "v0.8").empty());
    CHECK(up::asset_url(repo + "/", "v0.8", up::kPackageName) == repo + "/releases/download/v0.8/thpsource2-win64.zip");
    const std::string script = up::apply_script("v0.8");
    CHECK(script.find("@TAG@") == std::string::npos && script.find("@PACKAGE@") == std::string::npos && script.find("updated to v0.8") != std::string::npos && script.find("thpsource2-win64.zip") != std::string::npos);
    CHECK(script.find("Build /MIR") != std::string::npos && script.find("Licenses /MIR") != std::string::npos && script.find("failed.txt") != std::string::npos);
    // the player's folders are named only in the comment that promises to leave them alone, and only Build and Licenses are mirrored
    CHECK(script.find("Saves", script.find("Saves") + 1) == std::string::npos && script.find("Mods", script.find("Mods") + 1) == std::string::npos);
    std::size_t mirrors = 0;
    for (std::size_t at = script.find("/MIR"); at != std::string::npos; at = script.find("/MIR", at + 1)) {
        ++mirrors;
    }
    CHECK(mirrors == 2 && script.find("\r\n") != std::string::npos && script.find("\n") == script.find("\r\n") + 1);
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path scratch = fs::temp_directory_path() / ("thpsource2_core_test_" + std::to_string(std::rand()));
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    test_ini();
    test_settings();
    test_update_check();
    test_update_release();
    test_sha256();
    test_bindings();
    test_moved_default_key();
    test_music(scratch);
    test_scores(scratch);
    test_disc_image(scratch);
    test_folder_import(scratch);
    test_real_tables();
    test_real_disc(scratch);
    test_trick_sets(argc > 1 ? argv[1] : nullptr);
    fs::remove_all(scratch);
    if (g_failures != 0) {
        std::fprintf(stderr, "launcher_core_test: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("launcher_core_test: ok\n");
    return 0;
}
