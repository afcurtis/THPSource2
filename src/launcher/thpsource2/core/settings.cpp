#include "settings.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace thpsource2 {

namespace {
constexpr const char* kWindow = "Window";
constexpr const char* kGraphics = "Graphics";
constexpr const char* kGamepad = "Gamepad";
constexpr const char* kLauncher = "Launcher";
constexpr const char* kGame = "Game";
constexpr const char* kSound = "Sound";
constexpr const char* kHotkeysSection = "Hotkeys";
}  // namespace

const std::array<HotkeyInfo, kHotkeyCount> kHotkeys = {{
    {"Screenshot", "Screenshot", "SCREENSHOT", "Saves a PNG to the Screenshots folder."},
    {"Retry", "Retry", "RETRY", "Restarts the current run. In a level only."},
    {"Quit to launcher", "Quit", "QUIT", "Closes the game and returns to the launcher."},
    {"Toggle fullscreen", "Fullscreen", "FULLSCREEN", "Switches between windowed and fullscreen."},
}};

HotkeyBinding default_hotkey(Hotkey which) {
    HotkeyBinding binding;
    switch (which) {
    case Hotkey::Screenshot:
        binding.key = 0x42;   // F8
        break;
    case Hotkey::Retry:
        binding.key = 0x3f;   // F5
        break;
    case Hotkey::Quit:
        binding.pad = (1u << 7) | (1u << 8);   // L3 + R3 (the two stick clicks)
        binding.hold_ms = 1000;
        break;
    case Hotkey::Fullscreen:
        binding.key = 0x1c;   // Enter
        binding.mods = kHotkeyAlt;
        break;
    }
    return binding;
}

const char* hotkey_pad_bit_name(int bit) {
    static const char* const kNames[kHotkeyPadBits] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB",
                                                       "D-pad up", "D-pad down", "D-pad left", "D-pad right", "LT", "RT"};
    return bit >= 0 && bit < kHotkeyPadBits ? kNames[bit] : "?";
}

std::string hotkey_pad_name(std::uint32_t mask) {
    std::string text;
    for (int bit = 0; bit < kHotkeyPadBits; ++bit) {
        if ((mask & (1u << bit)) != 0) {
            text += (text.empty() ? "" : " + ") + std::string(hotkey_pad_bit_name(bit));
        }
    }
    return text.empty() ? "None" : text;
}

std::string hotkey_env_value(const HotkeyBinding& binding) {
    // an unbound hotkey is "0,0,0,0", not an empty value: the launcher's empty value unsets the variable, and the game then keeps its own default (F8 for the screenshot)
    return std::to_string(binding.key) + "," + std::to_string(binding.mods) + "," + std::to_string(binding.pad) + "," + std::to_string(binding.hold_ms);
}

// the multiples are of each level's own fog distance (the PlayStation's, OT_FOG_DISTANCE 100)
const std::array<FogOption, 5> kFogOptions = {{
    {"PlayStation (1x)", 100},
    {"PC (1.5x)", 150},
    {"Dreamcast (2.75x)", 275},
    {"Far (5x)", 500},
    {"Off (no fog)", kFogOffDistance},
}};

const std::array<const char*, 4> kDetailOptions = {"Original", "2x", "4x", "Maximum"};
const std::array<int, 4> kDetailValues = {100, 200, 400, kDetailMaxDistance};
const std::array<const char*, 3> kInternalResolutionOptions = {"PlayStation (512 x 240)", "PC (640 x 480)", "Native"};
const std::array<const char*, 2> kTextureFilterOptions = {"Sharp", "Smooth"};
const std::array<const char*, 6> kAnisotropyOptions = {"Off", "Trilinear", "2x", "4x", "8x", "16x"};
const std::array<int, 6> kAnisotropyValues = {0, 1, 2, 4, 8, 16};
const std::array<const char*, 5> kSupersampleOptions = {"Off", "1.5x", "2x", "3x", "4x"};
const std::array<int, 5> kSupersampleValues = {100, 150, 200, 300, 400};
const std::array<const char*, 5> kAspectOptions = {"Auto", "4:3", "16:9", "16:10", "21:9"};
const std::array<const char*, 4> kScalingOptions = {"Clean", "Sharp pixels", "Nearest pixels", "CRT"};
const std::array<const char*, 3> kAmbientOcclusionOptions = {"Off", "Low", "High"};
const std::array<const char*, 3> kSwitchOptions = {"Off", "On", "Game setting"};
const std::array<const char*, 13> kLevelNames = {"Hangar", "School II", "Marseille", "New York City", "Venice Beach", "Skate Street", "Philadelphia",
                                                 "The Bullring", "Chopper Drop", "Skatepark Heaven", "Downhill Jam", "Skate Park", "Warehouse"};
const std::array<const char*, 20> kSkaterNames = {"Tony Hawk", "Bob Burnquist", "Steve Caballero", "Kareem Campbell", "Rune Glifberg", "Eric Koston", "Bucky Lasek",
                                                  "Rodney Mullen", "Chad Muska", "Andrew Reynolds", "Geoff Rowley", "Elissa Steamer", "Jamie Thomas",
                                                  "Custom Skater 1", "Custom Skater 2", "Custom Skater 3", "Custom Skater 4", "Officer Dick", "Private Carrera",
                                                  "Spider-Man"};

bool skater_is_custom(int skater) { return skater >= 13 && skater <= 16; }

bool speed_run_level(int level) { return level == 0 || level == 1 || level == 3 || level == 4 || level == 6; }

int skater_style_count(int skater) { return skater == 0 ? 3 : skater == 19 ? 4 : 2; }

int fog_option_index(int distance) {
    for (std::size_t k = 0; k + 1 < kFogOptions.size(); ++k) {
        if (distance <= kFogOptions[k].distance) {
            return static_cast<int>(k);
        }
    }
    return static_cast<int>(kFogOptions.size() - 1);
}

int anisotropy_option_index(int value) {
    for (std::size_t k = 0; k < kAnisotropyValues.size(); ++k) {
        if (kAnisotropyValues[k] == value) {
            return static_cast<int>(k);
        }
    }
    return static_cast<int>(kAnisotropyValues.size() - 1);
}

int detail_option_index(int value) {
    for (std::size_t k = 0; k < kDetailValues.size(); ++k) {
        if (kDetailValues[k] == value) {
            return static_cast<int>(k);
        }
    }
    return static_cast<int>(kDetailValues.size() - 1);
}

bool frame_rate_judders(int frame_rate, int vsync, float refresh_hz) {
    if (vsync == 0 || refresh_hz <= 0.0f || frame_rate <= 0) {
        return false;
    }
    const float multiple = refresh_hz / static_cast<float>(frame_rate);
    const float nearest = std::round(multiple);
    // a display at 119.88 or 59.94 Hz counts as 120 or 60 (within 1 Hz of a multiple)
    return nearest < 1.0f || std::abs(refresh_hz - nearest * static_cast<float>(frame_rate)) > 1.0f;
}

int supersample_option_index(int value) {
    for (std::size_t k = 0; k < kSupersampleValues.size(); ++k) {
        if (kSupersampleValues[k] == value) {
            return static_cast<int>(k);
        }
    }
    return 0;
}

WindowMode Settings::window_mode() const {
    if (!windowed) {
        return WindowMode::Fullscreen;
    }
    return borderless ? WindowMode::Borderless : WindowMode::Windowed;
}

void Settings::set_window_mode(WindowMode mode) {
    windowed = mode != WindowMode::Fullscreen;
    borderless = mode == WindowMode::Borderless;
}

Settings default_settings() {
    return Settings{};
}

const std::array<const char*, kPresetCount> kPresetNames = {"Enhanced", "Original PC", "Original PlayStation"};

void copy_preset_options(const Settings& from, Settings& to) {
    to.aspect = from.aspect;
    to.fov_adjust = from.fov_adjust;
    to.hud_anchor = from.hud_anchor;
    to.hud_size = from.hud_size;
    to.grind_meter = from.grind_meter;
    to.music_now_playing = from.music_now_playing;
    to.frame_rate = from.frame_rate;
    to.internal_resolution = from.internal_resolution;
    to.msaa = from.msaa;
    to.smooth_cutouts = from.smooth_cutouts;
    to.supersample = from.supersample;
    to.texture_filter = from.texture_filter;
    to.anisotropy = from.anisotropy;
    to.sharp_hud = from.sharp_hud;
    to.scaling = from.scaling;
    to.crt_curve = from.crt_curve;
    to.psx_colour = from.psx_colour;
    to.use_psx_textures = from.use_psx_textures;
    to.fog_distance = from.fog_distance;
    to.detail_distance = from.detail_distance;
    to.psx_dither = from.psx_dither;
    to.ambient_occlusion = from.ambient_occlusion;
    to.true_colour = from.true_colour;
    to.upscaled_textures = from.upscaled_textures;
    to.camera_stick = from.camera_stick;
    to.autokick = from.autokick;
    to.vibration = from.vibration;
    to.default_camera = from.default_camera;
    to.play_movies = from.play_movies;
}

namespace {
// the preset options as compared: the fog by the option it shows (a distance beyond the last is Off), every other value as it is
auto preset_options(const Settings& s) {
    return std::make_tuple(s.aspect, s.fov_adjust, s.hud_anchor, s.hud_size, s.grind_meter, s.music_now_playing, s.frame_rate, s.internal_resolution, s.msaa,
                           s.smooth_cutouts, s.supersample, s.texture_filter, s.anisotropy, s.sharp_hud, s.scaling, s.crt_curve, s.psx_colour, s.use_psx_textures,
                           fog_option_index(s.fog_distance), s.detail_distance, s.psx_dither, s.ambient_occlusion, s.true_colour, s.upscaled_textures, s.camera_stick,
                           s.autokick, s.vibration, s.default_camera, s.play_movies);
}

// The keys of THPSource2.ini that hold the preset options, with their sections: a custom preset in Config\Presets.ini keeps these under its own section
struct PresetKey {
    const char* section;
    const char* key;
};
constexpr PresetKey kPresetKeys[] = {
    {kWindow, "FrameRate"},          {kWindow, "Aspect"},           {kWindow, "HudAnchor"},          {kWindow, "HudSize"},         {kWindow, "FovAdjust"},
    {kGraphics, "PSXColor"},         {kGraphics, "MSAA"},           {kGraphics, "SmoothCutouts"},    {kGraphics, "Supersample"},   {kGraphics, "Scaling"},         {kGraphics, "CrtCurve"},
    {kGraphics, "UsePSXTextures"},   {kGraphics, "FogDistance"},    {kGraphics, "DetailDistance"},   {kGraphics, "TextureFilter"}, {kGraphics, "Anisotropy"},
    {kGraphics, "SharpHud"},         {kGraphics, "UpscaledTextures"}, {kGraphics, "TrueColour"},     {kGraphics, "PSXDither"},     {kGraphics, "AmbientOcclusion"},
    {kGraphics, "InternalResolution"}, {kGamepad, "CameraStick"},   {kGame, "Autokick"},             {kGame, "Vibration"},         {kGame, "DefaultCamera"},
    {kGame, "GrindMeter"},           {kSound, "MusicNowPlaying"},   {kLauncher, "PlayMovies"},
};
}  // namespace

bool same_preset_options(const Settings& a, const Settings& b) { return preset_options(a) == preset_options(b); }

void apply_preset(Settings& s, int preset, bool upscaled_available) {
    const bool enhanced = preset == 0, psx = preset == 2;
    // Display: Enhanced the window's own shape, the wide view and the HUD at the edges, 60 fps (every display holds it) and the song title; the originals 4:3, the
    // 4:3 view and HUD at the game's own size and 30 fps; none of them shows the balance meter (a player can tick it)
    s.aspect = enhanced ? 0 : 1;
    s.fov_adjust = enhanced;
    s.hud_anchor = enhanced;
    s.hud_size = enhanced ? 0 : 100;
    s.grind_meter = false;
    s.music_now_playing = enhanced;
    s.frame_rate = enhanced ? 60 : 30;
    // Graphics: Enhanced everything at its most, with the PC's sharper textures (the upscaled set once it has been made); Original PC as it ran (640 x 480, PC
    // colour and fog); Original PlayStation the console on a TV (512 x 240, its own textures unfiltered, its colour, fog and dither, through the CRT)
    s.internal_resolution = enhanced ? 2 : psx ? 0 : 1;
    s.msaa = enhanced ? 4 : 0;
    s.smooth_cutouts = enhanced;
    s.supersample = enhanced ? 200 : 100;
    s.texture_filter = psx ? 0 : 1;
    s.anisotropy = enhanced ? 16 : 0;
    s.sharp_hud = enhanced;
    s.scaling = psx ? 3 : 0;
    s.crt_curve = psx;
    s.psx_colour = preset != 1;
    s.use_psx_textures = psx;
    s.fog_distance = enhanced ? kFogOffDistance : psx ? 100 : 150;
    s.detail_distance = enhanced ? kDetailMaxDistance : 100;
    s.psx_dither = psx;
    s.ambient_occlusion = enhanced ? 2 : 0;
    s.true_colour = enhanced;
    s.upscaled_textures = enhanced && upscaled_available;
    // Gameplay: neither original had the camera stick, and their own menus keep autokick and vibration; none of them plays the intro movies
    s.camera_stick = enhanced;
    s.autokick = enhanced ? 1 : 2;
    s.vibration = enhanced ? 1 : 2;
    s.default_camera = 0;
    s.play_movies = false;
}

int preset_of(const Settings& settings, bool upscaled_available) {
    for (int preset = 0; preset < kPresetCount; ++preset) {
        Settings values = settings;
        apply_preset(values, preset, upscaled_available);
        if (same_preset_options(values, settings)) {
            return preset;
        }
    }
    return -1;
}

bool preset_name_valid(std::string_view name) {
    if (name.empty() || name.size() > kPresetNameMax || trim(name) != name || name.front() == ';' || name.front() == '#') {
        return false;
    }
    if (std::any_of(kPresetNames.begin(), kPresetNames.end(), [&](const char* builtin) { return equal_nocase(builtin, name); })) {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](char c) { return c == '[' || c == ']' || c == '\r' || c == '\n'; });
}

std::vector<CustomPreset> read_presets(const IniFile& ini) {
    std::vector<CustomPreset> presets;
    for (const std::string& section : ini.sections()) {
        if (!preset_name_valid(section)) {
            continue;
        }
        // the preset's keys over a file of defaults, read as THPSource2.ini is: a key it lacks is the default, a value out of range is corrected the same way
        IniFile file;
        write_settings(file, default_settings());
        for (const PresetKey& key : kPresetKeys) {
            if (const auto value = ini.get(section, key.key)) {
                file.set(key.section, key.key, *value);
            }
        }
        presets.push_back(CustomPreset{section, read_settings(file)});
    }
    return presets;
}

IniFile write_presets(const std::vector<CustomPreset>& presets) {
    IniFile ini = IniFile::parse(
        "; THPSource2's own presets (the launcher's Presets list, under Enhanced, Original PC and Original PlayStation). One section a preset, holding the keys of\r\n"
        "; THPSource2.ini that a preset sets.\r\n");
    for (const CustomPreset& preset : presets) {
        if (!preset_name_valid(preset.name)) {
            continue;
        }
        IniFile file;
        write_settings(file, preset.values);
        for (const PresetKey& key : kPresetKeys) {
            ini.set(preset.name, key.key, file.get_string(key.section, key.key));
        }
    }
    return ini;
}

Settings read_settings(const IniFile& ini) {
    const Settings d;
    Settings s;
    s.resolution_x = ini.get_int(kWindow, "ResolutionX", d.resolution_x);
    s.resolution_y = ini.get_int(kWindow, "ResolutionY", d.resolution_y);
    s.windowed = ini.get_bool(kWindow, "Windowed", d.windowed);
    s.borderless = ini.get_bool(kWindow, "Borderless", d.borderless);
    s.vsync = ini.get_int(kWindow, "VSync", d.vsync);
    if (s.vsync < -1 || s.vsync > 1) {
        s.vsync = -1;
    }
    s.frame_rate = ini.get_int(kWindow, "FrameRate", d.frame_rate);
    if (s.frame_rate != 60 && s.frame_rate != 120) {
        s.frame_rate = 30;   // anything else is 30
    }
    // A file from before these options did anything holds their placeholder defaults, not choices: it gets the new defaults for the three that changed
    const bool chosen = ini.get_int(kLauncher, "SettingsVersion", 0) >= 2;
    s.aspect = ini.get_int(kWindow, "Aspect", d.aspect);
    s.aspect = s.aspect >= 0 && s.aspect < static_cast<int>(kAspectOptions.size()) ? s.aspect : d.aspect;   // 5, the old 4:3 stretched, is Auto
    s.hud_anchor = ini.get_bool(kWindow, "HudAnchor", d.hud_anchor);
    s.hud_size = ini.get_int(kWindow, "HudSize", d.hud_size);
    s.hud_size = s.hud_size == 0 ? 0 : std::clamp(s.hud_size, kHudSizeMin, kHudSizeMax);
    s.hud_margin = std::clamp(ini.get_int(kWindow, "HudMargin", d.hud_margin), 0, kHudMarginMax);
    s.fov_adjust = ini.get_bool(kWindow, "FovAdjust", d.fov_adjust);
    s.psx_colour = ini.get_bool(kGraphics, "PSXColor", d.psx_colour);
    s.msaa = ini.get_int(kGraphics, "MSAA", d.msaa);
    if (s.msaa != 0 && s.msaa != 2 && s.msaa != 4 && s.msaa != 8) {
        s.msaa = d.msaa;
    }
    s.smooth_cutouts = ini.get_bool(kGraphics, "SmoothCutouts", d.smooth_cutouts);
    s.supersample = ini.get_int(kGraphics, "Supersample", d.supersample);
    if (kSupersampleValues[static_cast<std::size_t>(supersample_option_index(s.supersample))] != s.supersample) {
        s.supersample = 100;   // not one of the options: off
    }
    s.scaling = std::clamp(ini.get_int(kGraphics, "Scaling", d.scaling), 0, static_cast<int>(kScalingOptions.size()) - 1);
    s.crt_curve = ini.get_bool(kGraphics, "CrtCurve", d.crt_curve);
    s.psx_dither = ini.get_bool(kGraphics, "PSXDither", d.psx_dither);
    s.ambient_occlusion = std::clamp(ini.get_int(kGraphics, "AmbientOcclusion", chosen ? 0 : d.ambient_occlusion), 0, static_cast<int>(kAmbientOcclusionOptions.size()) - 1);
    s.use_psx_textures = chosen ? ini.get_bool(kGraphics, "UsePSXTextures", d.use_psx_textures) : d.use_psx_textures;
    s.fog_distance = ini.get_int(kGraphics, "FogDistance", d.fog_distance);
    // PARTYMOD's loadSettings falls back to 1 here, which its fog list shows as PSX (100); 150 is the value its defaults and partymod.ini use
    if (s.fog_distance <= 1) {
        s.fog_distance = 150;
    }
    // a file without the key (made before object detail existed) gets the default; a value that is not one of the options does too
    s.detail_distance = ini.get_int(kGraphics, "DetailDistance", d.detail_distance);
    if (kDetailValues[static_cast<std::size_t>(detail_option_index(s.detail_distance))] != s.detail_distance) {
        s.detail_distance = d.detail_distance;
    }
    s.texture_filter = chosen ? ini.get_int(kGraphics, "TextureFilter", d.texture_filter) : d.texture_filter;
    if (s.texture_filter < 0 || s.texture_filter >= static_cast<int>(kTextureFilterOptions.size())) {
        s.texture_filter = 0;
    }
    // a file without the key (made before texture detail existed) gets the default too; a value that is not one of the options does
    s.anisotropy = ini.get_int(kGraphics, "Anisotropy", d.anisotropy);
    if (kAnisotropyValues[static_cast<std::size_t>(anisotropy_option_index(s.anisotropy))] != s.anisotropy) {
        s.anisotropy = d.anisotropy;
    }
    s.sharp_hud = ini.get_bool(kGraphics, "SharpHud", d.sharp_hud);   // a file without the key gets the default
    s.upscaled_textures = ini.get_bool(kGraphics, "UpscaledTextures", d.upscaled_textures);
    s.true_colour = ini.get_bool(kGraphics, "TrueColour", d.true_colour);   // a file without the key gets the default
    s.internal_resolution = chosen ? ini.get_int(kGraphics, "InternalResolution", d.internal_resolution) : d.internal_resolution;
    if (s.internal_resolution < 0 || s.internal_resolution >= static_cast<int>(kInternalResolutionOptions.size())) {
        s.internal_resolution = d.internal_resolution;
    }
    const int stick = ini.get_int(kGamepad, "MovementStick", static_cast<int>(d.movement_stick));
    s.movement_stick = stick == 1 ? MovementStick::Right : stick == -1 ? MovementStick::None : MovementStick::Left;
    s.directinput_pads = ini.get_bool(kGamepad, "DirectInput", d.directinput_pads);
    s.camera_stick = ini.get_bool(kGamepad, "CameraStick", d.camera_stick);
    s.camera_mode = std::clamp(ini.get_int(kGamepad, "CameraMode", d.camera_mode), 0, 1);
    s.camera_sensitivity = std::clamp(ini.get_int(kGamepad, "CameraSensitivity", d.camera_sensitivity), 1, 9);
    s.camera_invert_x = ini.get_bool(kGamepad, "CameraInvertX", d.camera_invert_x);
    s.camera_invert_y = ini.get_bool(kGamepad, "CameraInvertY", d.camera_invert_y);
    s.autokick = std::clamp(ini.get_int(kGame, "Autokick", d.autokick), 0, 2);
    s.vibration = std::clamp(ini.get_int(kGame, "Vibration", d.vibration), 0, 2);
    s.unlock_cheats = ini.get_bool(kGame, "UnlockCheats", d.unlock_cheats);
    s.unlock_movies = ini.get_bool(kGame, "UnlockMovies", d.unlock_movies);
    s.default_camera = std::clamp(ini.get_int(kGame, "DefaultCamera", d.default_camera), 0, 1);
    s.music_level = std::clamp(ini.get_int(kSound, "MusicLevel", d.music_level), 0, 10);
    s.sfx_level = std::clamp(ini.get_int(kSound, "SfxLevel", d.sfx_level), 0, 10);
    s.custom_music = ini.get_bool(kSound, "CustomMusic", d.custom_music);
    s.music_shuffle = ini.get_bool(kSound, "MusicShuffle", d.music_shuffle);
    s.music_include_default = ini.get_bool(kSound, "MusicIncludeDefault", d.music_include_default);
    s.music_now_playing = ini.get_bool(kSound, "MusicNowPlaying", d.music_now_playing);
    s.music_folders_off = ini.get(kSound, "MusicFoldersOff").value_or(d.music_folders_off);
    // quick_start is not stored: only the Quick Start page's buttons set it, for one launch
    s.quick_mode = std::clamp(ini.get_int(kGame, "QuickMode", d.quick_mode), 0, 3);
    s.quick_level = std::clamp(ini.get_int(kGame, "QuickLevel", d.quick_level), 0, static_cast<int>(kLevelNames.size()) - 1);
    s.quick_skater = std::clamp(ini.get_int(kGame, "QuickSkater", d.quick_skater), 0, static_cast<int>(kSkaterNames.size()) - 1);   // an old -1 (the game's choice) becomes Tony
    if (skater_is_custom(s.quick_skater)) {
        s.quick_skater = 0;
    }
    s.quick_look = std::clamp(ini.get_int(kGame, "QuickLook", d.quick_look), 0, skater_style_count(s.quick_skater) - 1);
    s.max_stats = ini.get_bool(kGame, "MaxStats", d.max_stats);
    s.quick_trick_set = ini.get_string(kGame, "QuickTrickSet", d.quick_trick_set);
    s.speed_run_cash = ini.get_bool(kGame, "SpeedRunCash", d.speed_run_cash);
    s.show_fps = ini.get_bool(kGame, "ShowFPS", d.show_fps);
    s.hide_hud = ini.get_bool(kGame, "HideHUD", d.hide_hud);
    s.grind_meter = ini.get_bool(kGame, "GrindMeter", d.grind_meter);
    s.initials = normalize_initials(ini.get_string(kGame, "Initials", d.initials));
    for (std::size_t k = 0; k < kHotkeyCount; ++k) {
        const std::string name = kHotkeys[k].ini_name;
        const HotkeyBinding fallback = d.hotkeys[k];
        HotkeyBinding& binding = s.hotkeys[k];
        binding.key = static_cast<std::uint8_t>(std::clamp(ini.get_int(kHotkeysSection, (name + "Key").c_str(), fallback.key), 0, 255));
        binding.mods = static_cast<std::uint32_t>(std::clamp(ini.get_int(kHotkeysSection, (name + "Mods").c_str(), static_cast<int>(fallback.mods)), 0, 7));
        binding.pad = static_cast<std::uint32_t>(std::clamp(ini.get_int(kHotkeysSection, (name + "Pad").c_str(), static_cast<int>(fallback.pad)), 0, (1 << kHotkeyPadBits) - 1));
        binding.hold_ms = std::clamp(ini.get_int(kHotkeysSection, (name + "Hold").c_str(), fallback.hold_ms), 0, 10000);
        if (binding.key == 0) {
            binding.mods = 0;
        }
    }
    s.pause_on_focus_loss = ini.get_bool(kHotkeysSection, "PauseOnFocusLoss", d.pause_on_focus_loss);
    s.game_dir = ini.get_string(kLauncher, "GameDir", d.game_dir);
    s.play_movies = ini.get_bool(kLauncher, "PlayMovies", d.play_movies);
    s.debug_mode = ini.get_bool(kLauncher, "DebugMode", d.debug_mode);
    s.check_updates = ini.get_bool(kLauncher, "CheckForUpdates", d.check_updates);
    s.skipped_update = ini.get_string(kLauncher, "SkippedUpdate", d.skipped_update);
    const std::string theme = ini.get_string(kLauncher, "Theme", "system");
    s.theme = theme == "light" ? Theme::Light : theme == "dark" ? Theme::Dark : Theme::System;
    return s;
}

std::string normalize_initials(std::string_view text) {
    if (text.size() == 5 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, 3);
    }
    if (text.size() != 3) {
        return {};
    }
    std::string out;
    for (const char c : text) {
        const char up = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        if (!((up >= 'A' && up <= 'Z') || up == ' ')) {
            return {};
        }
        out += up;
    }
    return out;
}

void clamp_resolution(Settings& s) {
    if (s.resolution_x != 0 && s.resolution_y != 0) {
        if (s.resolution_x < 640) {
            s.resolution_x = 640;
        }
        if (s.resolution_y < 480) {
            s.resolution_y = 480;
        }
    } else {
        s.resolution_x = 0;
        s.resolution_y = 0;
    }
}

void write_settings(IniFile& ini, const Settings& settings) {
    Settings s = settings;
    clamp_resolution(s);
    ini.set_int(kWindow, "ResolutionX", s.resolution_x);
    ini.set_int(kWindow, "ResolutionY", s.resolution_y);
    ini.set_bool(kWindow, "Windowed", s.windowed);
    ini.set_bool(kWindow, "Borderless", s.borderless);
    ini.set_int(kWindow, "VSync", s.vsync);
    ini.set_int(kWindow, "FrameRate", s.frame_rate);
    ini.set_int(kWindow, "Aspect", s.aspect);
    ini.set_bool(kWindow, "HudAnchor", s.hud_anchor);
    ini.set_int(kWindow, "HudSize", s.hud_size);
    ini.set_int(kWindow, "HudMargin", s.hud_margin);
    ini.set_bool(kWindow, "FovAdjust", s.fov_adjust);
    ini.set_bool(kGraphics, "PSXColor", s.psx_colour);
    ini.set_int(kGraphics, "MSAA", s.msaa);
    ini.set_bool(kGraphics, "SmoothCutouts", s.smooth_cutouts);
    ini.set_int(kGraphics, "Supersample", s.supersample);
    ini.set_int(kGraphics, "Scaling", s.scaling);
    ini.set_bool(kGraphics, "CrtCurve", s.crt_curve);
    ini.set_bool(kGraphics, "UsePSXTextures", s.use_psx_textures);
    ini.set_int(kGraphics, "FogDistance", s.fog_distance);
    ini.set_int(kGraphics, "DetailDistance", s.detail_distance);
    ini.set_int(kGraphics, "TextureFilter", s.texture_filter);
    ini.set_int(kGraphics, "Anisotropy", s.anisotropy);
    ini.set_bool(kGraphics, "SharpHud", s.sharp_hud);
    ini.set_bool(kGraphics, "UpscaledTextures", s.upscaled_textures);
    ini.set_bool(kGraphics, "TrueColour", s.true_colour);
    ini.set_bool(kGraphics, "PSXDither", s.psx_dither);
    ini.set_int(kGraphics, "AmbientOcclusion", s.ambient_occlusion);
    ini.set_int(kGraphics, "InternalResolution", s.internal_resolution);
    ini.set_int(kGamepad, "MovementStick", static_cast<int>(s.movement_stick));
    ini.set_bool(kGamepad, "DirectInput", s.directinput_pads);
    ini.set_bool(kGamepad, "CameraStick", s.camera_stick);
    ini.set_int(kGamepad, "CameraMode", s.camera_mode);
    ini.set_int(kGamepad, "CameraSensitivity", s.camera_sensitivity);
    ini.set_bool(kGamepad, "CameraInvertX", s.camera_invert_x);
    ini.set_bool(kGamepad, "CameraInvertY", s.camera_invert_y);
    ini.set_int(kGame, "Autokick", s.autokick);
    ini.set_int(kGame, "Vibration", s.vibration);
    ini.set_bool(kGame, "UnlockCheats", s.unlock_cheats);
    ini.set_bool(kGame, "UnlockMovies", s.unlock_movies);
    ini.set_int(kGame, "DefaultCamera", s.default_camera);
    ini.set_int(kSound, "MusicLevel", s.music_level);
    ini.set_int(kSound, "SfxLevel", s.sfx_level);
    ini.set_bool(kSound, "CustomMusic", s.custom_music);
    ini.set_bool(kSound, "MusicShuffle", s.music_shuffle);
    ini.set_bool(kSound, "MusicIncludeDefault", s.music_include_default);
    ini.set_bool(kSound, "MusicNowPlaying", s.music_now_playing);
    ini.set(kSound, "MusicFoldersOff", s.music_folders_off);
    ini.set_int(kGame, "QuickMode", s.quick_mode);
    ini.set_int(kGame, "QuickLevel", s.quick_level);
    ini.set_int(kGame, "QuickSkater", s.quick_skater);
    ini.set_int(kGame, "QuickLook", s.quick_look);
    ini.set_bool(kGame, "MaxStats", s.max_stats);
    ini.set(kGame, "QuickTrickSet", s.quick_trick_set);
    ini.set_bool(kGame, "SpeedRunCash", s.speed_run_cash);
    ini.set_bool(kGame, "ShowFPS", s.show_fps);
    ini.set_bool(kGame, "HideHUD", s.hide_hud);
    ini.set_bool(kGame, "GrindMeter", s.grind_meter);
    const std::string initials = normalize_initials(s.initials);
    ini.set(kGame, "Initials", initials.empty() ? "" : "\"" + initials + "\"");   // quoted: the file's reader trims a trailing space
    for (std::size_t k = 0; k < kHotkeyCount; ++k) {
        const std::string name = kHotkeys[k].ini_name;
        ini.set_int(kHotkeysSection, (name + "Key").c_str(), s.hotkeys[k].key);
        ini.set_int(kHotkeysSection, (name + "Mods").c_str(), static_cast<int>(s.hotkeys[k].mods));
        ini.set_int(kHotkeysSection, (name + "Pad").c_str(), static_cast<int>(s.hotkeys[k].pad));
        ini.set_int(kHotkeysSection, (name + "Hold").c_str(), s.hotkeys[k].hold_ms);
    }
    ini.set_bool(kHotkeysSection, "PauseOnFocusLoss", s.pause_on_focus_loss);
    ini.set_int(kLauncher, "SettingsVersion", 2);
    ini.set(kLauncher, "GameDir", s.game_dir);
    ini.set_bool(kLauncher, "PlayMovies", s.play_movies);
    ini.set_bool(kLauncher, "DebugMode", s.debug_mode);
    ini.set_bool(kLauncher, "CheckForUpdates", s.check_updates);
    ini.set(kLauncher, "SkippedUpdate", s.skipped_update);
    ini.set(kLauncher, "Theme", s.theme == Theme::Light ? "light" : s.theme == Theme::System ? "system" : "dark");
}

int auto_hud_size(int picture_height) {
    if (picture_height <= 1080) {
        return 100;
    }
    const int percent = static_cast<int>(std::lround(100.0 * std::sqrt(1080.0 / picture_height) / 5.0)) * 5;
    return std::clamp(percent, 70, 100);
}

std::vector<std::pair<std::string, std::string>> launch_environment(const Settings& settings, const std::string& screenshots_dir, int display_height,
                                                                  const std::string& music_dir, const std::string& scores_file, const std::string& texture_pack_dir,
                                                                  const std::string& true_colour_dir) {
    Settings s = settings;
    clamp_resolution(s);
    std::vector<std::pair<std::string, std::string>> env;
    // read by the game today (src/hooks/kits/standalone_video.cpp, standalone_platform.cpp)
    const WindowMode mode = s.window_mode();
    env.emplace_back("OT_WINDOW_MODE", mode == WindowMode::Fullscreen ? "fullscreen" : mode == WindowMode::Borderless ? "borderless" : "windowed");
    env.emplace_back("OT_WINDOW_SIZE", s.resolution_x != 0 ? std::to_string(s.resolution_x) + "x" + std::to_string(s.resolution_y) : "");
    env.emplace_back("OT_VSYNC", s.vsync < 0 ? "" : std::to_string(s.vsync));
    env.emplace_back("OT_FRAME_RATE", s.frame_rate == 60 ? "60" : s.frame_rate == 120 ? "120" : "");
    env.emplace_back("OT_WINDOW_TITLE", "THPSource2");
    env.emplace_back("OT_PAD_STICK", s.movement_stick == MovementStick::Right ? "right" : s.movement_stick == MovementStick::None ? "none" : "left");
    // the free look (standalone_freelook.cpp): the stick that does not move the skater; empty variables are unset
    env.emplace_back("OT_FREELOOK", !s.camera_stick ? "" : s.camera_mode == 1 ? "stay" : "spring");
    env.emplace_back("OT_FREELOOK_STICK", s.movement_stick == MovementStick::Right ? "left" : "right");
    env.emplace_back("OT_FREELOOK_SENS", s.camera_stick ? std::to_string(std::clamp(s.camera_sensitivity, 1, 9)) : "");
    env.emplace_back("OT_FREELOOK_INVERT", !s.camera_stick ? "" : std::string(s.camera_invert_x ? "x" : "") + (s.camera_invert_y ? "y" : ""));
    env.emplace_back("SDL_JOYSTICK_DIRECTINPUT", s.directinput_pads ? "" : "0");   // SDL's own hint, read by the game's SDL input (settings.hpp directinput_pads)
    // the renderer (standalone_video.cpp, sdl_video_gl.cpp, draw_kit.cpp) and the game's own switches (standalone_options.cpp)
    static const char* const kAspects[] = {"window", "4:3", "16:9", "16:10", "21:9"};
    env.emplace_back("OT_ASPECT", kAspects[std::clamp(s.aspect, 0, 4)]);
    env.emplace_back("OT_HUD_ANCHOR", s.hud_anchor ? "1" : "0");
    env.emplace_back("OT_HUD_SCALE", std::to_string(s.hud_size == 0 ? auto_hud_size(s.resolution_y > 0 ? s.resolution_y : display_height) : std::clamp(s.hud_size, kHudSizeMin, kHudSizeMax)));
    env.emplace_back("OT_HUD_MARGIN", std::to_string(std::clamp(s.hud_margin, 0, kHudMarginMax)));
    env.emplace_back("OT_FOV", s.fov_adjust ? "adjust" : "fixed");
    env.emplace_back("OT_PSX_TEXTURES", s.use_psx_textures ? "1" : "0");
    env.emplace_back("OT_PSX_COLOR", s.psx_colour ? "1" : "0");
    env.emplace_back("OT_FOG_DISTANCE", s.fog_distance >= kFogOffDistance ? "off" : std::to_string(s.fog_distance));
    env.emplace_back("OT_DETAIL_DISTANCE", s.detail_distance >= kDetailMaxDistance ? "max" : std::to_string(s.detail_distance));
    env.emplace_back("OT_TEXTURE_FILTER", s.texture_filter == 1 ? "smooth" : "sharp");
    // texture detail applies to smooth textures only: sharp ones are nearest, which no mip map or anisotropic filter changes
    env.emplace_back("OT_ANISOTROPY", s.texture_filter != 1 || s.anisotropy == 0 ? "0" : s.anisotropy == 1 ? "trilinear" : std::to_string(s.anisotropy));
    // the 2D layer's own filter only exists with smooth textures (sharp ones are nearest already)
    env.emplace_back("OT_HUD_FILTER", s.texture_filter == 1 && s.sharp_hud ? "sharp" : "smooth");
    // the upscaled set: only with smooth textures and texture detail on (a replaced texture without mip maps shimmers when it is drawn smaller), and only once a set exists
    // and the true-colour bitmaps (no mip maps needed: they are the game's own size). The upscaled folder comes first, so its picture wins for a key both hold
    std::string pack = upscaled_textures_usable(s) && !texture_pack_dir.empty() ? texture_pack_dir : "";
    if (true_colour_usable(s) && !true_colour_dir.empty()) {
        pack += (pack.empty() ? "" : "|") + true_colour_dir;
    }
    env.emplace_back("OT_TEXTURE_PACK", pack);
    static const char* const kInternal[] = {"psx", "pc", "window"};
    env.emplace_back("OT_INTERNAL_RESOLUTION", kInternal[std::clamp(s.internal_resolution, 0, 2)]);
    env.emplace_back("OT_MSAA", std::to_string(s.msaa));
    // supersampling multiplies the window's target; the PlayStation and PC sizes ignore it. Smooth cut-outs need multisampling
    static const char* const kSupersamples[] = {"1", "1.5", "2", "3", "4"};
    env.emplace_back("OT_SUPERSAMPLE", s.internal_resolution < 2 ? "1" : kSupersamples[supersample_option_index(s.supersample)]);
    env.emplace_back("OT_ALPHA_TO_COVERAGE", s.smooth_cutouts && s.msaa != 0 ? "1" : "0");
    static const char* const kScalings[] = {"smooth", "sharp", "nearest", "crt"};
    // the output looks (Sharp, Nearest, CRT) scale a picture smaller than the window: at Native the target is the window's own size (or a supersampled one that the box
    // filter brings down), so Native is always Clean. The stored Scaling is kept for the PlayStation and PC sizes
    env.emplace_back("OT_SCALING", kScalings[s.internal_resolution >= 2 ? 0 : std::clamp(s.scaling, 0, 3)]);
    env.emplace_back("OT_CRT_CURVE", s.crt_curve ? "1" : "0");
    env.emplace_back("OT_PSX_DITHER", s.psx_dither && s.internal_resolution == 0 ? "1" : "0");   // the dither is the PlayStation's 240 lines': only at that size
    static const char* const kOcclusion[] = {"off", "low", "high"};
    env.emplace_back("OT_AMBIENT_OCCLUSION", kOcclusion[std::clamp(s.ambient_occlusion, 0, 2)]);
    env.emplace_back("OT_AUTOKICK", s.autokick == 2 ? "" : std::to_string(s.autokick));
    env.emplace_back("OT_VIBRATION", s.vibration == 2 ? "" : std::to_string(s.vibration));
    env.emplace_back("OT_VEHICLE_SOUND_FIX", "1");   // cars sharing a sound no longer restart it every frame (standalone_audio.cpp); no menu entry: there is no reason to want the stutter
    env.emplace_back("OT_RETAIL_FIXES", "1");        // the retail game's other mistakes corrected (runtime/host_options.hpp retail_fixes); no menu entry, for the same reason
    env.emplace_back("OT_TRICK_TIPS", "0");          // trick tips start off every launch (standalone_options.cpp); Options > Display Options in the game still turns them on
    std::string unlock;
    const std::pair<bool, const char*> unlocks[] = {{s.unlock_cheats, "cheats"}, {s.unlock_movies, "movies"}};   // levels and skaters: a quick start opens them itself
    for (const auto& [on, name] : unlocks) {
        if (on) {
            unlock += (unlock.empty() ? "" : ",") + std::string(name);
        }
    }
    env.emplace_back("OT_UNLOCK", unlock);
    env.emplace_back("OT_CAMERA", s.default_camera == 1 ? "2" : "");
    // the game's own Options > Sound levels (0..10) start at these every launch (standalone_options.cpp); the movies follow the music
    env.emplace_back("OT_MUSIC_LEVEL", std::to_string(std::clamp(s.music_level, 0, 10)));
    env.emplace_back("OT_SFX_LEVEL", std::to_string(std::clamp(s.sfx_level, 0, 10)));
    // the custom soundtrack (standalone_music.cpp): every switch unset while it is off, so the game plays its own music as retail does
    const bool custom = s.custom_music && !music_dir.empty();
    env.emplace_back("OT_CUSTOM_MUSIC", custom ? "1" : "");
    env.emplace_back("OT_MUSIC_DIR", custom ? music_dir : "");
    env.emplace_back("OT_MUSIC_FOLDERS_OFF", custom ? s.music_folders_off : "");
    env.emplace_back("OT_MUSIC_SHUFFLE", custom && s.music_shuffle ? "1" : "");
    env.emplace_back("OT_MUSIC_DEFAULT", custom && s.music_include_default ? "1" : "");
    env.emplace_back("OT_MUSIC_NOW_PLAYING", s.music_now_playing ? "1" : "");   // not tied to the custom soundtrack: the game's own songs are named too
    env.emplace_back("OT_QUICK_START", s.quick_start ? std::to_string(s.quick_level) + (s.quick_skater >= 0 ? "," + std::to_string(s.quick_skater) : "") : "");
    static constexpr const char* kQuickModes[] = {"", "single", "speedrun", "competition"};
    env.emplace_back("OT_QUICK_MODE", s.quick_start && s.quick_mode >= 1 && s.quick_mode <= 3 ? kQuickModes[s.quick_mode] : "");
    env.emplace_back("OT_QUICK_LOOK", s.quick_start ?std::to_string(s.quick_look) : "");
    env.emplace_back("OT_MAX_STATS", s.quick_start && s.max_stats ? "1" : "");
    // the quick start's trick set (trick_sets.hpp): the binding block every skater's career record gets before the level builds its trick table
    env.emplace_back("OT_QUICK_TRICKS", s.quick_start && s.quick_tricks.size() == 106 ? s.quick_tricks : "");
    env.emplace_back("OT_SPEED_RUN_CASH", s.quick_start && s.quick_mode == 2 && s.speed_run_cash ? "1" : "");
    env.emplace_back("OT_FPS", s.show_fps ? "1" : "");
    env.emplace_back("OT_HIDE_HUD", s.hide_hud ? "1" : "");
    env.emplace_back("OT_GRIND_METER", s.grind_meter ? "1" : "");
    env.emplace_back("OT_INITIALS", normalize_initials(s.initials));
    env.emplace_back("OT_SCORES_FILE", scores_file);   // every finished career level and single session appended (standalone_scores.cpp); not a setting: always kept
    env.emplace_back("OT_SCORES_IN_GAME", !scores_file.empty() ? "1" : "");   // the game's own tables merged with the file's runs; not a setting: always, with the file
    env.emplace_back("OT_SCREENSHOTS", screenshots_dir);   // always on: F8 always saves one
    // the hotkeys the game watches (standalone_platform.cpp), every one set so that an unbound one stays unbound; the pause on a lost focus (FrontScreens::update)
    for (std::size_t k = 0; k < kHotkeyCount; ++k) {
        env.emplace_back(std::string("OT_HOTKEY_") + kHotkeys[k].env_name, hotkey_env_value(s.hotkeys[k]));
    }
    env.emplace_back("OT_PAUSE_ON_FOCUS_LOSS", s.pause_on_focus_loss ? "1" : "");
    env.emplace_back("OPENTONY_DEBUG", s.debug_mode ? "1" : "");   // opentony.exe: the full run history, the texture dump and census (docs/LAUNCHER.md, "Debug mode")
    return env;
}

}  // namespace thpsource2
