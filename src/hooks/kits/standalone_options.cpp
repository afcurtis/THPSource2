// The player's options of the standalone (standalone_options.hpp; docs/LAUNCHER.md, "What the game reads").
#include "standalone_options.hpp"
#include "standalone_display.hpp"
#include "standalone_scores.hpp"

#include "kit_bridge.hpp"

#include "../../hal/audio.h"
#include "../../hal/input.h"
#include "../../runtime/draw_kit.hpp"
#include "../../runtime/frame_pacing.hpp"
#include "../../runtime/host_options.hpp"
#include "../../runtime/trick_database.hpp"
#include "guest_arena.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace opentony::kitdll {

const OtHalInputBackend* platform_input();   // standalone_platform.cpp: the HAL's input service, or null

namespace {

template <typename T>
volatile T& retail(std::uint32_t address) {
    return *arena::host<volatile T>(address);
}

constexpr std::uint32_t kVideoFogYonScale = 0x005485a0;      // float: the far plane and the depth cue's reach, as a multiple of the level's own (the options file's FOGSCALE / 100)
constexpr std::uint32_t kConfiguredTextureRoot = 0x00549f24; // byte: the PC port's replacement bitmaps (NEWTEX\<level>\) are looked for (D3DTex_OpenImage, 0x004d8770)
constexpr std::uint32_t kAutoKickSetting = 0x0056a3d8;       // two words, one a player (re/evidence/functions/autokick-option.md)
constexpr std::uint32_t kVibrationSetting = 0x0056a3e0;
constexpr std::uint32_t kAutoKickState = 0x0055fc2c;         // the controller page's copies
constexpr std::uint32_t kVibrationState = 0x0055fc34;

// the Options > Sound levels, 0..12 (shown as 0..10), and the mixer words derived from them. The audio code's use settles which is which (audio_game.hpp: 0x0052be44 scales the
// sound effects, 0x0052be48 is the CD music's volume), though career_save.cpp names the two the other way round; Options_Init starts the music at 6 and the effects at 12
constexpr std::uint32_t kSfxLevel = 0x0055fb3c;
constexpr std::uint32_t kMusicLevel = 0x0055fb40;
constexpr std::uint32_t kSfxMixer = 0x0052be44;               // level x 0x555
constexpr std::uint32_t kMusicMixer = 0x0052be48;             // level x 0x15
// the save image's copies of the two levels (Career_SerializeLiveState writes them from the levels above, Career_ApplyLoadedSave reads them back). The pause menu's Sound
// screen starts its lines from them, shows its TRACK line as "--" with the music copy at 0, and sets the music mixer from that copy as it closes (front_update_late.cpp)
constexpr std::uint32_t kSfxImage = 0x0056a2e8;
constexpr std::uint32_t kMusicImage = 0x0056a2ec;
constexpr std::uint32_t kLevelMax = 12;
// Options > Display Options > Trick Tips: the live word (Options_Init sets it to 1) and the save image's copy (career_save.cpp kLiveOptionA / kImgOptionA)
constexpr std::uint32_t kTrickTipsOption = 0x0055fcc0;
constexpr std::uint32_t kTrickTipsImage = 0x0056a46c;

constexpr std::uint32_t kPixelAspectX = 0x00563a6c;          // 4.12: the view's vertical scale (M3dInit_SetResolution, 0x00464690, sets both from the screen's size)
constexpr std::uint32_t kPixelAspectY = 0x00563a70;          // 4.12: the view's horizontal scale, and a factor of the projection distance

// the unlocks (src/runtime/career.hpp, skater_manager.hpp)
constexpr std::uint32_t kAllLevelsOpen = 0x0056a8e4;         // the game's own "all levels" switch: every level select and the career's level test read it
constexpr std::uint32_t kCheats = 0x0056a374;                // 17 entries of 4 bytes: unlocked, the character that earned it, on / off
constexpr std::uint32_t kCheatCount = 17;
constexpr std::uint32_t kInitialsImage = 0x0056a408;         // the remembered score initials in the save image (three bytes; the score tables take their letters from here)
constexpr std::uint32_t kInitialsLive = 0x0055fc5c;          // the live copy, a C string ("AAA" at start-up)
constexpr std::uint32_t kSkaterRows = 0x0053a3b8;            // SkaterModelNameTable: 20 rows of 0x170 bytes
constexpr std::uint32_t kSkaterRowStride = 0x170;
constexpr std::uint32_t kSkaterRowUnlocked = 0x158;          // dword: the character can be chosen
constexpr std::uint32_t kSkaterRowFlags = 0x15c;             // 0x40 / 0x80 / 0x100 / 0x200: the four hidden characters (Career::unlock_character, 0x00416f80)
constexpr std::uint32_t kCareerRecordCount = 20;             // kCareerRecords (above): one per Select Player row
constexpr std::uint32_t kRecDecksOwned = 0x34;               // u32: a bit per deck of the character's sponsor set the character owns (EquipSelect::owns_deck, 0x004448c0)
constexpr std::uint32_t kAllDecks = 0x000fffffu;             // a set holds at most 20 decks

// the quick start: the shell's own words (src/runtime/front_main.cpp) and the calls the scenario bootstrap uses (standalone_scenario.cpp)
constexpr std::uint32_t kFrontMode = 0x00561c74;             // the game mode the shell's screens pass on: 2 is the free skate, 3 the single session (the HUD hides the session clock in 2 and 4, and 4 is the two-player free skate)
constexpr std::uint32_t kModeSingleSession = 3;
constexpr std::uint32_t kModeFreeSkate = 2;
constexpr std::uint32_t kModeCareer = 1;                     // the speed run and the competition session (standalone_challenge.cpp)
constexpr std::uint32_t kFrontTwoPlayers = 0x00561c70;
constexpr std::uint32_t kMainMenuScreen = 0x0046aee0;
constexpr std::uint32_t kKeyDown = 0x004e41b0;
constexpr std::uint32_t kPreLoad = 0x004a90f0;
constexpr std::uint32_t kPreUnload = 0x004a9330;
constexpr std::uint32_t kLevelSelectScreen = 0x004588f0;
constexpr std::uint32_t kLevelSelectPre = 0x0052cc64;        // the level select's .pre file name
constexpr std::uint32_t kSelectCharacter = 0x004b18d0;       // SkaterManager_SelectCharacterChecked(character, player)
constexpr std::uint32_t kKeyboardState = 0x006a43e4;
constexpr std::uint8_t kConfirmScanCode = 0x10;
constexpr std::uint32_t kPrintfLog = 0x004cf850;
constexpr std::uint32_t kSummaryLogFormat = 0x00535ad4;      // NETMENU_InitSummary's log line: the rules screen is up
constexpr std::uint8_t kSummaryScanCode = 0x1c;              // Enter
constexpr std::uint32_t kControlMode = 0x006a6964;
constexpr std::uint32_t kPlayResult = 0x26;                  // the main menu's answer that leads to the level select

// the music's place (src/runtime/audio_game.hpp): the CD tracks are 4 groups of 8; the next track is found from the current group and track, skipping empty ones and
// the first two groups (index < 0x10)
constexpr std::uint32_t kXaGroup = 0x0056b888;
constexpr std::uint32_t kXaTrack = 0x0056b88c;
constexpr std::uint32_t kTrackLengths = 0x0053770c;          // 32 dwords, -1: no such track
constexpr std::uint32_t kCareerRecords = 0x00568a6c;         // per-character career record, 0x104 bytes (skater_manager.hpp)
constexpr std::uint32_t kCareerRecordStride = 0x104;
constexpr std::uint32_t kRecordStatsOffset = 0x38;           // ten signed stat bytes of a record: what the game reads (career.hpp kRecStats)
constexpr std::uint32_t kRecordBindings = 0xcc;              // the record's 0x35-byte trick binding block (trick_database.hpp kConfigBindings)
constexpr std::uint32_t kAlternateBindings = 0x0056a690;     // the alternate block: Tony Hawk in his third style (createobj_misc.cpp kAlternateBlock)
constexpr std::uint32_t kTricksImage = 0x0056a894;           // the loaded TRICKS.BIN (createobj_misc.cpp)
constexpr std::size_t kTricksImageSize = 33168;
constexpr std::uint32_t kTrickTableOne = 0x00568a50;         // PlayerOneTrickScriptTable: player one's table, built at the level's set-up (TrickTable_BuildForPlayer)
constexpr std::uint32_t kSkaterManager = 0x0056c580;
constexpr std::uint32_t kSkaterSlot = kSkaterManager + 4;    // the first player's slot entry: character, custom slot, costume
constexpr std::uint32_t kSlotCostume = 8;
constexpr std::uint32_t kRememberedCostume = 0x68;           // + character * 4

constexpr int kDetailMax = 100000;   // OT_DETAIL_DISTANCE=max: a thousand times the game's distances, beyond every level (the arena guard in render_objects is what limits it)
constexpr float kFogOff = 64.0f;   // "off": the far plane 64 times the level's own, beyond every level; the depth cue starts at the same multiple, so nothing in view fades

Display g_display;
GameOptions g_options;
bool g_options_read = false;

bool flag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

// OT_QUICK_TRICKS: the binding block as 106 hex digits (trick_block_hex). Each ordinary slot and special trick a record index below 0x80 or 0xff (none), each special
// combination one of the 36 or 0xff, a special's trick and combination both set or both empty; anything else is refused whole (the game would index past its tables)
bool parse_trick_block(const char* text, std::uint8_t (&block)[0x35]) {
    if (std::strlen(text) != 2 * sizeof block) {
        return false;
    }
    const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (std::size_t k = 0; k < sizeof block; ++k) {
        const int high = digit(text[2 * k]);
        const int low = digit(text[2 * k + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        block[k] = static_cast<std::uint8_t>(high * 16 + low);
    }
    for (std::size_t k = 0; k < 0x30; ++k) {
        if (block[k] >= 0x80 && block[k] != 0xff) {
            return false;
        }
    }
    for (std::size_t slot = 0; slot < 5; ++slot) {
        const std::uint8_t combo = block[0x30 + slot];
        if ((combo >= 36 && combo != 0xff) || ((combo == 0xff) != (block[0x2b + slot] == 0xff))) {
            return false;
        }
    }
    return true;
}

int tristate(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return -1;
    }
    return std::strcmp(value, "0") != 0 ? 1 : 0;
}

void read_options() {
    g_options_read = true;
    GameOptions& o = g_options;
    if (const char* fog = std::getenv("OT_FOG_DISTANCE"); fog != nullptr && *fog != '\0') {
        if (std::strcmp(fog, "off") == 0) {
            o.fog_off = true;
            o.fog_scale = kFogOff;
        } else {
            const long percent = std::strtol(fog, nullptr, 10);
            if (percent >= 10 && percent <= 6400) {
                o.fog_scale = static_cast<float>(percent) / 100.0f;
            }
        }
    }
    if (const char* fov = std::getenv("OT_FOV")) {
        o.fov_fixed = std::strcmp(fov, "fixed") == 0;
    }
    if (const char* detail = std::getenv("OT_DETAIL_DISTANCE"); detail != nullptr && *detail != '\0') {
        if (std::strcmp(detail, "max") == 0) {
            o.detail_distance = kDetailMax;
        } else {
            const long percent = std::strtol(detail, nullptr, 10);
            if (percent > 100 && percent <= 10000) {
                o.detail_distance = static_cast<int>(percent);
            }
        }
    }
    runtime::host_options().detail_distance = o.detail_distance;
    // a fog distance of the player's: the sky goes behind the far plane when that is beyond the game's fixed sky depth (275 and up), so far buildings are not cut off by it
    runtime::host_options().sky_behind_yon = o.fog_scale > 0.0f;
    runtime::host_options().draw_hidden_areas = o.fog_scale > 0.0f;   // the areas a level's trigger boxes hide, drawn: with fog pushed out or off they appeared block by block (NYC)
    runtime::host_options().item_reach_scale = o.fog_scale;   // cash, letters and goal items shown out to the pushed far plane, not popping in at the level's own fog distance (School II)
    runtime::host_options().retail_fixes = flag("OT_RETAIL_FIXES");   // the retail game's own mistakes corrected (host_options.hpp); the launcher always sets it
    // the polygon arena those options fill: OT_POLY_BUFFER=N (1 retail .. 16), or 4 times retail's when one of them is on (runtime/host_options.hpp); read here, once per run.
    // The retail fixes take the enlarged arena too: at retail's size several writers test only that the cursor is not past the end and then write up to 0xc0 bytes (past the
    // block, into the game's heap), and a full arena stops the program; the enlarged one is padded and drops a polygon that does not fit (RenderKit::model_allocate_polygon)
    runtime::host_options().poly_buffer_scale = runtime::poly_buffer_scale_for(
        std::getenv("OT_POLY_BUFFER"), o.detail_distance != 100 || o.fog_scale > 0.0f || runtime::host_options().retail_fixes);
    o.psx_textures = flag("OT_PSX_TEXTURES");
    o.psx_colour = flag("OT_PSX_COLOR");
    o.autokick = tristate("OT_AUTOKICK");
    o.vibration = tristate("OT_VIBRATION");
    o.trick_tips = tristate("OT_TRICK_TIPS");
    if (const char* unlock = std::getenv("OT_UNLOCK")) {
        const std::string list = std::string(",") + unlock + ",";
        o.unlock_levels = list.find(",levels,") != std::string::npos || list.find(",all,") != std::string::npos;
        o.unlock_skaters = list.find(",skaters,") != std::string::npos || list.find(",all,") != std::string::npos;
        o.unlock_cheats = list.find(",cheats,") != std::string::npos || list.find(",all,") != std::string::npos;
        o.unlock_movies = list.find(",movies,") != std::string::npos || list.find(",all,") != std::string::npos;
        o.unlock_decks = list.find(",decks,") != std::string::npos || list.find(",all,") != std::string::npos;
    }
    if (const char* quick = std::getenv("OT_QUICK_START"); quick != nullptr && *quick != '\0') {
        int level = -1, skater = -1;
        const int fields = std::sscanf(quick, "%d,%d", &level, &skater);
        if (fields >= 1 && level >= 0 && level <= 12) {
            o.quick_level = level;
            o.quick_skater = fields == 2 ? skater : -1;
            // a quick start opens every level and skater (and so every style) itself: the launcher has no switches for them. Safe for the career because a quick
            // start never writes the save file (host_options.hpp quick_play; standalone_userdata.hpp user_file_locked)
            o.unlock_levels = true;
            o.unlock_skaters = true;
        }
    }
    runtime::host_options().quick_play = o.quick_level >= 0;
    o.max_stats = flag("OT_MAX_STATS");
    if (const char* tricks = std::getenv("OT_QUICK_TRICKS"); tricks != nullptr && o.quick_level >= 0) {
        o.has_quick_tricks = parse_trick_block(tricks, o.quick_tricks);
    }
    if (const char* initials = std::getenv("OT_INITIALS"); initials != nullptr && std::strlen(initials) == 3) {
        bool valid = true;
        for (int k = 0; k < 3; ++k) {
            valid = valid && ((initials[k] >= 'A' && initials[k] <= 'Z') || initials[k] == ' ');
        }
        if (valid) {
            std::memcpy(o.initials, initials, 3);
            o.has_initials = true;
        }
    }
    if (const char* camera = std::getenv("OT_CAMERA")) {
        o.camera = std::strcmp(camera, "2") == 0 ? 2 : 1;
    }
    const auto level = [](const char* name) {
        const char* text = std::getenv(name);
        return text != nullptr && *text != '\0' ? static_cast<int>((std::clamp)(std::strtol(text, nullptr, 10), 0L, 10L)) : -1;
    };
    o.music_level = level("OT_MUSIC_LEVEL");
    o.sfx_level = level("OT_SFX_LEVEL");
    runtime::host_options().unlock_movies = o.unlock_movies;
    runtime::host_options().disable_controls = flag("OT_DISABLE_CONTROLS_MENU");
    runtime::host_options().default_camera = o.camera == 2 ? 1 : 0;
    runtime::host_options().hide_hud = flag("OT_HIDE_HUD");
    runtime::host_options().grind_meter = flag("OT_GRIND_METER");
    read_hud_layout(runtime::host_options());
    if (const char* rate = std::getenv("OT_FRAME_RATE"); rate != nullptr && *rate != '\0') {
        if (std::strcmp(rate, "60") == 0) {
            o.frame_rate = 60;
        } else if (std::strcmp(rate, "120") == 0) {
            o.frame_rate = 120;
        } else if (std::strcmp(rate, "30") != 0) {
            std::printf("options: frame rate '%s' is not supported (30, 60 or 120); staying at 30\n", rate);
            std::fflush(stdout);
        }
    }
    {
        runtime::framepacing::Pacing pacing;
        if (o.frame_rate != 30) {
            pacing.limiter_ticks = 1;
            pacing.exact_clock = true;
            pacing.count_30hz = true;
            pacing.period_ticks = o.frame_rate == 120 ? 0.5 : 1.0;
            // F2A: 120 runs on the sub-tick clock; OT_SUB_TICK=1 puts 60 on it too (a measurement switch: it must give what 60 gives on whole ticks)
            pacing.sub_tick = o.frame_rate == 120 || flag("OT_SUB_TICK");
            // F2B: at 120 a small value times the frame scale (64) keeps its fraction for the next frame; OT_CARRY=0 switches that off (a measurement switch)
            pacing.carry_fraction = o.frame_rate == 120 && !(std::getenv("OT_CARRY") != nullptr && std::strcmp(std::getenv("OT_CARRY"), "0") == 0);
        }
        runtime::framepacing::set_pacing(pacing);
    }
    read_freelook_options();
    if (const char* mode = std::getenv("OT_QUICK_MODE")) {
        o.quick_mode = std::strcmp(mode, "single") == 0        ? QuickMode::Single
                       : std::strcmp(mode, "speedrun") == 0    ? QuickMode::SpeedRun
                       : std::strcmp(mode, "competition") == 0 ? QuickMode::Competition
                                                               : QuickMode::FreeSkate;
    }
    // a speed run is a level with goals of its own (standalone_challenge.cpp speed_run_level): another level has none to finish, so it is a single session
    if (o.quick_mode == QuickMode::SpeedRun && !speed_run_level(o.quick_level)) {
        std::printf("options: quick start: level %d has no goals for a speed run; a single session instead\n", o.quick_level);
        std::fflush(stdout);
        o.quick_mode = QuickMode::Single;
    }
    runtime::host_options().speed_run = o.quick_level >= 0 && o.quick_mode == QuickMode::SpeedRun;
    o.speed_run_cash = flag("OT_SPEED_RUN_CASH");
    if (const char* look = std::getenv("OT_QUICK_LOOK"); look != nullptr && (look[0] >= '0' && look[0] <= '3' && look[1] == '\0')) {
        o.quick_look = *look - '0';
    }
    if (o.fog_scale > 0.0f || o.psx_textures || o.psx_colour || o.autokick >= 0 || o.vibration >= 0 || o.unlock_levels || o.unlock_skaters || o.unlock_cheats ||
        o.unlock_decks || o.quick_level >= 0 || o.frame_rate != 30 || o.has_initials) {
        std::printf("options: frame rate %d, fog %s, textures %s, colour %s, autokick %s, vibration %s, unlock%s%s%s%s%s, quick start %d (skater %d), initials '%s'\n", o.frame_rate,
                    o.fog_off ? "off" : o.fog_scale > 0.0f ? std::to_string(static_cast<int>(o.fog_scale * 100.0f + 0.5f)).c_str() : "the game's",
                    o.psx_textures ? "PSX" : "PC", o.psx_colour ? "PSX" : "PC", o.autokick < 0 ? "the game's" : o.autokick != 0 ? "on" : "off",
                    o.vibration < 0 ? "the game's" : o.vibration != 0 ? "on" : "off", o.unlock_levels ? " levels" : "", o.unlock_skaters ? " skaters" : "",
                    o.unlock_cheats ? " cheats" : "", o.unlock_decks ? " decks" : "",
                    !o.unlock_levels && !o.unlock_skaters && !o.unlock_cheats && !o.unlock_decks ? " nothing" : "", o.quick_level, o.quick_skater,
                    o.has_initials ? o.initials : "the game's");
        std::fflush(stdout);
    }
}

// A screen wider than 4:3. The game has a branch for one (PixelAspectY = width x 3 / (height x 4)), written for displays whose pixels are not square and never reached
// by a mode retail lists: it magnifies the view by that ratio squared across and once down. With square pixels the view should keep its height and gain at the
// sides. The projection (M3D_BeginRenderView, 0x0045e8e0) is x = (w / 2) Ay^2 X / (t Z) and y = (w / 2) Ay Ax Y / (t Z), t the view's half-angle tangent; at
// 640 x 480 both aspects are 1 and both scales 320 / t. A screen r = w / 640 times wider keeps those scales with Ax = Ay = 1 / sqrt(r). The view's planes are built
// from the same two words, so what is culled follows.
void write_wide_aspect() {
    const Display& d = g_display;
    if (d.width <= 640 || d.height != 480) {
        return;
    }
    // OT_FOV=fixed leaves the game's 4:3 words (1.0): the same horizontal angle at every width, the extra width magnifies the view and crops it above and below
    const std::uint32_t aspect = wide_pixel_aspect(d.width, game_options().fov_fixed);
    if (retail<std::uint32_t>(kPixelAspectX) != aspect || retail<std::uint32_t>(kPixelAspectY) != aspect) {
        retail<std::uint32_t>(kPixelAspectX) = aspect;
        retail<std::uint32_t>(kPixelAspectY) = aspect;
    }
}

// The unlocks, put back at every poll (a loaded save, a new career and the cheat menu rewrite them): the game's own "all levels" switch; the hidden characters as
// Career::unlock_character opens them (available, the two "locked" bits of the row cleared); every cheat listed as earned (it is still off until the player switches
// it on in Options > Cheats); every deck of the Skate Shop owned by every character (the texture census: an unowned deck shows a placeholder, its picture never loads).
void write_unlocks(const GameOptions& o) {
    if (o.unlock_levels && retail<std::uint32_t>(kAllLevelsOpen) == 0) {
        retail<std::uint32_t>(kAllLevelsOpen) = 1;
    }
    if (o.unlock_skaters) {
        for (std::uint32_t row = 0; row < 20; ++row) {
            const std::uint32_t at = kSkaterRows + row * kSkaterRowStride;
            const std::uint32_t flags = retail<std::uint32_t>(at + kSkaterRowFlags);
            if ((flags & 0x3c0u) != 0 && (retail<std::uint32_t>(at + kSkaterRowUnlocked) == 0 || (flags & 0x18u) != 0)) {
                retail<std::uint32_t>(at + kSkaterRowUnlocked) = 1;
                retail<std::uint32_t>(at + kSkaterRowFlags) = flags & 0xffffffe7u;
            }
        }
    }
    if (o.unlock_cheats) {
        for (std::uint32_t cheat = 0; cheat < kCheatCount; ++cheat) {
            if (retail<std::uint8_t>(kCheats + 4 * cheat) == 0) {
                retail<std::uint8_t>(kCheats + 4 * cheat) = 1;
            }
        }
    }
    if (o.unlock_decks) {
        for (std::uint32_t character = 0; character < kCareerRecordCount; ++character) {
            const std::uint32_t at = kCareerRecords + character * kCareerRecordStride + kRecDecksOwned;
            if ((retail<std::uint32_t>(at) & kAllDecks) != kAllDecks) {
                retail<std::uint32_t>(at) |= kAllDecks;
            }
        }
    }
}

// The launcher's score initials, put back at every poll: the game remembers the last entry in two places (the save image's copy, which the score tables take their
// letters from, and the live string a save is made from), and the entry screen's accept copies the row's letters into both. Writing here makes the launcher's letters win
// again at the next poll, so each entry starts from them and a changed one applies to that score only. The live copy is a C string: its terminator stays.
void write_initials(const GameOptions& o) {
    if (!o.has_initials) {
        return;
    }
    for (std::uint32_t k = 0; k < 3; ++k) {
        const std::uint8_t letter = static_cast<std::uint8_t>(o.initials[k]);
        if (retail<std::uint8_t>(kInitialsImage + k) != letter) {
            retail<std::uint8_t>(kInitialsImage + k) = letter;
        }
        if (retail<std::uint8_t>(kInitialsLive + k) != letter) {
            retail<std::uint8_t>(kInitialsLive + k) = letter;
        }
    }
    if (retail<std::uint8_t>(kInitialsLive + 3) != 0) {
        retail<std::uint8_t>(kInitialsLive + 3) = 0;
    }
}

// the quick start's steps, each once and in this order
struct QuickStart {
    bool key_armed = true;      // the main menu's confirm key
    bool answer_armed = true;   // the main menu's answer
    int level_select = 0;       // 0 waiting for the pre-load, 1 the screen, 2 the unload, 3 done
    bool summary_armed = true;  // the rules screen's PLAY
    bool summary_active = false;
    std::uint32_t summary_ticks = 0;
    bool finished() const { return level_select == 3 && !summary_armed && !summary_active; }
} g_quick;

// Every skater's ten stats at the maximum, 10. The game reads a stat from the skater's career record (SkaterKit::profile_value: the record's signed byte x 10, one
// player), where the row's base stats are only what a career starts from and what a point costs; writing the rows changed nothing.
void write_max_stats() {
    for (std::uint32_t character = 0; character < 20; ++character) {
        for (std::uint32_t stat = 0; stat < 10; ++stat) {
            retail<std::uint8_t>(kCareerRecords + character * kCareerRecordStride + kRecordStatsOffset + stat) = 10;
        }
    }
}

// The launcher's trick set (OT_QUICK_TRICKS): the binding block of every skater's career record (+0xcc: 30 ordinary combinations used of 43, then five special tricks
// and their five special combinations; re/evidence/functions/trick-database-runtime.md), and the alternate block Tony Hawk's third style reads. The level's object set-up
// builds the player's trick table from it (CreateObjKit::trick_table_build), after the level select; nothing is saved in a quick start.
void write_quick_tricks(const std::uint8_t (&block)[0x35]) {
    for (std::uint32_t character = 0; character < kCareerRecordCount; ++character) {
        for (std::uint32_t k = 0; k < sizeof block; ++k) {
            retail<std::uint8_t>(kCareerRecords + character * kCareerRecordStride + kRecordBindings + k) = block[k];
        }
    }
    for (std::uint32_t k = 0; k < sizeof block; ++k) {
        retail<std::uint8_t>(kAlternateBindings + k) = block[k];
    }
}

// Once the level runs (a few seconds after the quick start's last step): how many of the set's tricks player one's trick table holds, each a combination's buttons
// and the trick's script (the entries TrickTable_BuildForPlayer writes: count, buttons, source, descriptor). A line in run.txt: the set reached the game, or not.
void report_quick_tricks(const std::uint8_t (&block)[0x35]) {
    const std::uint32_t image_at = retail<std::uint32_t>(kTricksImage);
    const std::uint32_t table_at = retail<std::uint32_t>(kTrickTableOne);
    if (image_at == 0 || table_at == 0) {
        std::printf("options: quick start: the trick set: no trick table to check\n");
        std::fflush(stdout);
        return;
    }
    try {
        std::vector<std::uint8_t> image(kTricksImageSize);
        std::memcpy(image.data(), arena::host<const std::uint8_t>(image_at), image.size());
        const runtime::TrickDatabase db = runtime::TrickDatabase::build(image);
        std::vector<std::uint16_t> table(0x200);
        std::memcpy(table.data(), arena::host<const std::uint16_t>(table_at), table.size() * 2);
        const auto holds = [&](const runtime::ComboWords& combo, std::uint16_t source) {
            for (std::size_t at = 0; at < table.size() && table[at] != 0; at += table[at] + 3u) {
                const std::size_t count = table[at];
                if (count == combo[0] && at + count + 1 < table.size() && std::equal(combo.begin() + 1, combo.begin() + 1 + count, table.begin() + at + 1) &&
                    table[at + count + 1] == source) {
                    return true;
                }
            }
            return false;
        };
        int expected = 0;
        int found = 0;
        for (std::size_t k = 0; k < runtime::builtin_combos().size(); ++k) {
            if (block[k] < db.count()) {
                ++expected;
                found += holds(runtime::builtin_combos()[k], db.record(block[k]).source) ? 1 : 0;
            }
        }
        for (std::size_t slot = 0; slot < 5; ++slot) {
            const std::uint8_t record = block[0x2b + slot];
            const std::uint8_t combo = block[0x30 + slot];
            if (record < db.count() && combo < runtime::special_combos().size()) {
                ++expected;
                found += holds(runtime::special_combos()[combo], db.record(record).source) ? 1 : 0;
            }
        }
        std::printf("options: quick start: the trick set: %d of its %d tricks in player one's trick table\n", found, expected);
    } catch (const std::exception& e) {
        std::printf("options: quick start: the trick set: the trick table cannot be checked (%s)\n", e.what());
    }
    std::fflush(stdout);
}

// The skater's look, A (costume 0) or B (costume 1): the player's slot and the character's remembered costume (SkaterManager::select_costume), for the character the
// slot holds. A look the row has no model for is left alone.
void write_look(int look) {
    const std::uint32_t character = retail<std::uint32_t>(kSkaterSlot);
    if (character >= 20 || look < 0 || look > 3) {
        return;
    }
    const std::uint32_t row = kSkaterRows + character * kSkaterRowStride;
    const std::uint32_t flags = retail<std::uint32_t>(row + kSkaterRowFlags);
    if (retail<std::uint32_t>(row + static_cast<std::uint32_t>(look) * 8) == 0 || (look == 2 && (flags & 0x08u) != 0) || (look == 3 && (flags & 0x10u) != 0)) {
        return;   // no such style, or one still locked (Tony's C opens with "All skaters available")
    }
    retail<std::uint32_t>(kSkaterSlot + kSlotCostume) = static_cast<std::uint32_t>(look);
    retail<std::uint32_t>(kSkaterManager + kRememberedCostume + 4 * character) = static_cast<std::uint32_t>(look);
}

// A random level track to start the music on, not always the first. The game's next-track step moves on from the group and track it holds, so the one before the
// pick is written; with no level track on the disc (or the level-picked music) nothing changes.
void shuffle_first_song() {
    std::uint32_t usable[32];
    std::uint32_t count = 0;
    for (std::uint32_t index = 0x10; index < 32; ++index) {
        if (retail<std::int32_t>(kTrackLengths + 4 * index) != -1) {
            usable[count++] = index;
        }
    }
    if (count < 2) {
        return;
    }
    const std::uint32_t pick = usable[std::random_device{}() % count];
    const std::uint32_t before = pick - 1;
    retail<std::uint32_t>(kXaGroup) = before / 8;
    retail<std::uint32_t>(kXaTrack) = before % 8;
    std::printf("options: quick start: music starts on track %u\n", pick - 0x0f);
    std::fflush(stdout);
}

// A Goals Speed Run waits on its goals screen (the level's checklist, with Play and Back) so the player can read the goals; every other quick start presses Play itself,
// and so does a speed run under OT_SPEED_RUN_TEST (a headless check has no one to press it)
bool player_starts_run() {
    static const bool test = std::getenv("OT_SPEED_RUN_TEST") != nullptr;
    return game_options().quick_mode == QuickMode::SpeedRun && !test;
}

// Enter for the first five tests of the key since the rules screen came up, then up (standalone_scenario.cpp's summary_tick)
void summary_tick() {
    if (g_quick.summary_active && g_quick.summary_ticks < 20) {
        retail<std::uint8_t>(kKeyboardState + kSummaryScanCode) = g_quick.summary_ticks < 5 ? 0x80 : 0;
        if (++g_quick.summary_ticks == 20) {
            g_quick.summary_active = false;
        }
    }
}

void push_presentation() {
    runtime::drawkit::Presentation presentation;
    presentation.psx_colour = game_options().psx_colour;   // (reads the options, which set the HUD's layout too)
    if (g_display.width > g_display.hud_width) {
        presentation.screen_width = g_display.width;
        presentation.hud_width = g_display.hud_width;
    } else if (g_display.width != 640 || runtime::host_options().hud_layout()) {
        // (a 4:3 picture takes part when the HUD's own layout is asked for: its size and margin apply there too)
        presentation.screen_width = g_display.width;
        presentation.hud_width = g_display.width;
    }
    runtime::drawkit::set_presentation(presentation);
}

}  // namespace

const Display& display() { return g_display; }

std::int32_t movie_volume_offset_mb() {
    const std::uint32_t level = retail<std::uint32_t>(kMusicLevel);
    if (level >= kLevelMax) {
        return 0;
    }
    if (level == 0) {
        return OT_HAL_VOLUME_SILENT;
    }
    return (std::max)(static_cast<std::int32_t>(OT_HAL_VOLUME_SILENT), static_cast<std::int32_t>(std::lround(2000.0 * std::log10(static_cast<double>(level) / kLevelMax))));
}

const GameOptions& game_options() {
    if (!g_options_read) {
        read_options();
    }
    return g_options;
}

void options_after_load() {
    const GameOptions& o = game_options();
    if (o.fog_scale > 0.0f) {
        retail<float>(kVideoFogYonScale) = o.fog_scale;
    }
    if (o.psx_textures) {
        retail<std::uint8_t>(kConfiguredTextureRoot) = 0;
    }
    options_after_options_reset();
    push_presentation();
}

// The launcher's music and sound effect levels (0..10, as Options > Sound shows them; the game keeps 0..12) and trick tips are what the game starts with, every launch:
// whatever the game's sliders and Display Options were left at, in a save or not, is replaced. Retail's Options_Init (the start of main, a new game) and a loaded save
// set them themselves, so this runs after both (the call seam). The menus still change them within a run.
// The save image's copies are set too: after a loaded save they held the save's levels, so the pause menu showed those (a music level of 0 with the launcher's music
// playing, TRACK "--") and set the music mixer from them as it closed, which silenced the music (a mixer of 0 is the level's ambience at the effects volume). A quick
// start loads no save, so its copies were already the launcher's
void options_after_options_reset() {
    const GameOptions& o = game_options();
    if (o.trick_tips >= 0) {
        // the live word is the one Options > Display Options shows and sets; the tips themselves (TrickTips_Show, WorldKit::tips_show) read the save image's copy,
        // which Career_SerializeLiveState refreshes from the live word as the menu closes
        retail<std::uint32_t>(kTrickTipsOption) = static_cast<std::uint32_t>(o.trick_tips);
        retail<std::uint32_t>(kTrickTipsImage) = static_cast<std::uint32_t>(o.trick_tips);
        std::printf("options: trick tips %s\n", o.trick_tips != 0 ? "on" : "off");
        std::fflush(stdout);
    }
    if (o.music_level >= 0) {
        const std::uint32_t level = static_cast<std::uint32_t>(o.music_level) * 6 / 5;
        retail<std::uint32_t>(kMusicLevel) = level;
        retail<std::uint32_t>(kMusicMixer) = level * 0x15;
        retail<std::uint32_t>(kMusicImage) = level;
    }
    if (o.sfx_level >= 0) {
        const std::uint32_t level = static_cast<std::uint32_t>(o.sfx_level) * 6 / 5;
        retail<std::uint32_t>(kSfxLevel) = level;
        retail<std::uint32_t>(kSfxMixer) = level * 0x555;
        retail<std::uint32_t>(kSfxImage) = level;
    }
}

void write_controller_options();

void options_after_input() {
    write_controller_options();
    options_each_poll();
}

// the controller options as the Options menu's controller page would set them: the live flags and the page's copies (one word a player); once, so the menu can change them
void write_controller_options() {
    const GameOptions& o = game_options();
    for (std::uint32_t player = 0; player < 2; ++player) {
        if (o.autokick >= 0) {
            retail<std::uint32_t>(kAutoKickSetting + 4 * player) = static_cast<std::uint32_t>(o.autokick);
            retail<std::uint32_t>(kAutoKickState + 4 * player) = static_cast<std::uint32_t>(o.autokick);
        }
        if (o.vibration >= 0) {
            retail<std::uint32_t>(kVibrationSetting + 4 * player) = static_cast<std::uint32_t>(o.vibration);
            retail<std::uint32_t>(kVibrationState + 4 * player) = static_cast<std::uint32_t>(o.vibration);
        }
    }
}

void options_each_poll() {
    const GameOptions& o = game_options();
    if (runtime::host_options().quit_to_launcher) {   // the quick start's Quit, confirmed (FrontScreens::update): back to the launcher, as the Quit hotkey does
        std::printf("options: quick start: quit to the launcher\n");
        std::fflush(stdout);
        challenge_shutdown();
        scores_shutdown();
        ExitProcess(0);
    }
    write_wide_aspect();
    write_unlocks(o);
    write_initials(o);
    if (o.trick_tips >= 0) {
        // the tips read the save image's copy, the menu the live word: a save read into the image without being applied must not bring them back behind an "Off" menu
        retail<std::uint32_t>(kTrickTipsImage) = retail<std::uint32_t>(kTrickTipsOption);
    }
    scores_each_poll();   // the score file (standalone_scores.cpp): nothing without OT_SCORES_FILE
    if (o.has_quick_tricks && g_quick.level_select == 3 && !g_quick.summary_armed) {
        static int polls_in_level = 0;
        if (++polls_in_level == 60) {   // the rules screen comes after the level and its trick table are built; a second or two on
            report_quick_tricks(o.quick_tricks);
        }
    }
    challenge_each_poll();   // the speed run's clock, the competition's heats (standalone_challenge.cpp): nothing in the other modes
    if (o.quick_level >= 0 && !player_starts_run() && (g_quick.summary_active || !g_quick.summary_armed)) {
        retail<std::uint8_t>(kKeyboardState + kSummaryScanCode) = 0;   // DirectInput's real state replaces the pulsed key at every poll
        retail<std::uint8_t>(kKeyboardState + kConfirmScanCode) = 0;
    }
    if (o.fog_scale > 0.0f) {
        retail<float>(kVideoFogYonScale) = o.fog_scale;   // the Options menu's fog slider and its file stop at 500 %
    }
    if (o.psx_textures && retail<std::uint8_t>(kConfiguredTextureRoot) != 0) {
        retail<std::uint8_t>(kConfiguredTextureRoot) = 0;   // the Options menu's renderer page sets it again
    }
}

// The quick start is the scenario bootstrap's way into a level (standalone_scenario.cpp: the retail capture's own, run thousands of times) with the free skate's mode
// and the player's skater: the main menu sees its confirm key and its answer becomes "level select"; the level select is passed over, its answer the level.
// After that the game is on its own: End Run leads back to the shell as it always does.
bool options_watch_calls() { return game_options().quick_level >= 0 && !g_quick.finished(); }

bool options_watch_call(std::uint32_t address) { return options_watch_calls() || challenge_watches(address); }

void options_before_bindings() {
    if (options_watch_calls() && retail<std::uint32_t>(kControlMode) != 0) {
        summary_tick();
    }
}

int options_choose_skater(int skater, int look) {
    if (skater >= 0 && skater < 20 && retail<std::uint32_t>(kSkaterRows + static_cast<std::uint32_t>(skater) * kSkaterRowStride + kSkaterRowUnlocked) != 0) {
        call_native_or_seam(kSelectCharacter, {static_cast<std::uint32_t>(skater), 0});
    }
    write_look(look);   // after the choice: selecting the character a slot holds already leaves its costume alone
    return static_cast<int>(retail<std::uint32_t>(kSkaterSlot + kSlotCostume));
}

bool options_before_call(std::uint32_t address, std::vector<std::uint32_t>& args, const char* caller, std::uint32_t (*live)(std::uint32_t), std::uint32_t& result) {
    QuickStart& q = g_quick;
    const GameOptions& o = game_options();
    result = 0;
    if (challenge_watches(address)) {
        challenge_before_call(address);
        return false;
    }
    if (address == kKeyDown) {
        if (q.key_armed && !args.empty() && args[0] == kConfirmScanCode && caller != nullptr && std::strcmp(caller, "MainMenu_RunWrapper") == 0) {
            q.key_armed = false;
            retail<std::uint8_t>(kKeyboardState + kConfirmScanCode) = 0x80;
        }
        summary_tick();
        return false;
    }
    if (address == kPrintfLog && q.summary_armed && q.level_select == 3 && !args.empty() && args[0] == kSummaryLogFormat) {
        q.summary_armed = false;
        if (player_starts_run()) {
            return false;   // the goals screen stays up until the player presses Play
        }
        q.summary_active = true;
        q.summary_ticks = 0;
        retail<std::uint8_t>(kKeyboardState + kSummaryScanCode) = 0x80;
        return false;
    }
    if (q.level_select == 0 && address == kPreLoad && args.size() >= 2 && args[1] == kLevelSelectPre && !q.answer_armed) {
        q.level_select = 1;
        return true;
    }
    if (q.level_select == 1 && address == kLevelSelectScreen && args.size() >= 2) {
        retail<std::uint32_t>(kFrontMode) = quick_front_mode();
        retail<std::uint8_t>(kFrontTwoPlayers) = 0;
        if (o.max_stats) {
            write_max_stats();
        }
        shuffle_first_song();
        options_choose_skater(o.quick_skater, o.quick_look);
        if (o.has_quick_tricks) {
            write_quick_tricks(o.quick_tricks);   // after the choice, so nothing the select does can set the record's tricks again
        }
        challenge_prepare_level();
        *arena::host<volatile std::uint32_t>(live(args[1])) = static_cast<std::uint32_t>(o.quick_level);
        q.level_select = 2;
        static const char* const kModeNames[] = {"free skate", "single session", "goals speed run", "competition session"};
        std::printf("options: quick start: %s, level %d, skater %d, look %d%s\n", kModeNames[static_cast<int>(o.quick_mode)], o.quick_level, o.quick_skater, o.quick_look,
                    o.has_quick_tricks ? ", the launcher's trick set" : "");
        std::fflush(stdout);
        return true;
    }
    if (q.level_select == 2 && address == kPreUnload && args.size() >= 2 && args[1] == kLevelSelectPre) {
        q.level_select = 3;
        return true;
    }
    return false;
}

void options_after_call(std::uint32_t address,const std::vector<std::uint32_t>& args, std::uint32_t (*live)(std::uint32_t)) {
    QuickStart& q = g_quick;
    if (address == kMainMenuScreen && q.answer_armed && args.size() >= 2) {
        q.answer_armed = false;
        retail<std::uint8_t>(kKeyboardState + kConfirmScanCode) = 0;
        *arena::host<volatile std::uint32_t>(live(args[1])) = kPlayResult;
        // answer 0x26 goes straight to the level select, which takes the mode from here (retail's free skate sets it on the way); setting it only at the level select
        // came after the screen had read it, so every quick start was a single session
        retail<std::uint32_t>(kFrontMode) = quick_front_mode();
        retail<std::uint8_t>(kFrontTwoPlayers) = 0;
    }
}

std::uint32_t quick_front_mode() {
    switch (game_options().quick_mode) {
    case QuickMode::Single: return kModeSingleSession;
    case QuickMode::SpeedRun:
    case QuickMode::Competition: return kModeCareer;
    default: return kModeFreeSkate;
    }
}

// The PlayStation pad's two motors on a pad of today: motor 0 is the small one (on or off), motor 1 the large one with a strength of 0..255. The duration is in the
// game's units of about four frames (PARTYMOD's PCINPUT_ActuatorOn, src/input.c: duration * 66 ms).
void options_rumble(std::uint32_t port, std::uint32_t duration, std::uint32_t motor, std::uint32_t strength) {
    const OtHalInputBackend* input = platform_input();
    if (input == nullptr || input->rumble == nullptr || (port & 0xff) != 0) {
        return;
    }
    const std::uint32_t milliseconds = (std::min)((duration & 0xffff) * 66u, 2000u);
    if ((motor & 0xff) == 0) {
        input->rumble(0, 0, 0x7fff, milliseconds);
    } else {
        const std::uint32_t level = (std::min)(strength & 0xffffu, 255u);
        const auto low = static_cast<std::uint16_t>(0x3000u + level * (0xffffu - 0x3000u) / 255u);
        input->rumble(0, low, static_cast<std::uint16_t>(low / 2), milliseconds);
    }
}

void options_rumble_stop() {
    const OtHalInputBackend* input = platform_input();
    if (input != nullptr && input->rumble != nullptr) {
        input->rumble(0, 0, 0, 0);
    }
}

}  // namespace opentony::kitdll

// The display the program made: the game's screen size (640 x 480 unless the picture is wider), the width the 2D layer keeps its shape for, and whether the picture is
// stretched over the window. Before the boot.
extern "C" __declspec(dllexport) void __cdecl ot_standalone_set_display(std::int32_t width, std::int32_t height, std::int32_t hud_width, std::int32_t stretch) {
    using namespace opentony::kitdll;
    g_display.width = (std::max)(width, 320);
    g_display.height = (std::max)(height, 240);
    g_display.hud_width = (std::min)((std::max)(hud_width, 320), g_display.width);
    g_display.stretch = stretch != 0;
    push_presentation();
}
