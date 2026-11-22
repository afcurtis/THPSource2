// THPSource2.exe: the player's front door to OpenTony (docs/LAUNCHER.md). It finds or imports the game's files (an installed game, the disc, or a
// disc image), has opentony.exe unpack them once, keeps the settings (PARTYMOD's partymod.ini schema) and the game's own key and pad bindings, both in the one
// file Config\THPSource2.ini, and starts opentony.exe (in Build\) with them. opentony.exe keeps doing what it did: the first-run reads, the run logs, the hang
// watchdog. The install folder's layout is src/launcher/layout.hpp.
//
// UI: SDL3 and Dear ImGui (SDL_Renderer backend). The settings model is ported from PARTYMOD's configurator (MIT licence; THIRD_PARTY_NOTICES.md).
//
// The launcher sits in the install folder with SDL3.dll in Build\ beside the other programs: SDL3.dll is delay-loaded (-Wl,--delayload=SDL3.dll), and main()
// points the DLL search at Build\ before the first SDL call. That is why SDL_MAIN_HANDLED: SDL_main.h's WinMain would call SDL3.dll before main runs.
#include "launcher/layout.hpp"
#include "launcher/music_library.hpp"
#include "launcher/scores.hpp"

#include "bindings.hpp"
#include "game_files.hpp"
#include "ini.hpp"
#include "platform.hpp"
#include "png.hpp"
#include "settings.hpp"
#include "trick_sets.hpp"
#include "true_colour.hpp"
#include "update_task.hpp"
#include "upscale_command.hpp"
#include "upscale_setup.hpp"
#include "upscale_task.hpp"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <atomic>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;
using namespace thpsource2;

namespace {

constexpr const char* kTitle = "THPSource2";
constexpr const char* kVersion = "v0.7";
constexpr const char* kRepoUrl = "https://github.com/afcurtis/THPSource2";
// the pages the launcher sends the player to, all under kRepoUrl (tests/test_launcher.py checks that): the bug report form (.github/ISSUE_TEMPLATE/bug.md) and
// the newest release. The update notice opens the second and never an address that came over the network (core/update_check.hpp)
constexpr const char* kIssueUrl = "https://github.com/afcurtis/THPSource2/issues/new?template=bug.md";
constexpr const char* kLatestReleaseUrl = "https://github.com/afcurtis/THPSource2/releases/latest";

// the text colours that differ between the dark and the light theme (set_theme_colours)
ImVec4 kGood = ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
ImVec4 kBad = ImVec4(1.0f, 0.45f, 0.40f, 1.0f);
ImVec4 kNote = ImVec4(0.95f, 0.78f, 0.35f, 1.0f);
ImVec4 kDim = ImVec4(0.62f, 0.64f, 0.68f, 1.0f);
const ImVec4 kAccent = ImVec4(0.89f, 0.40f, 0.09f, 1.0f);   // the icon's orange
const ImVec4 kAccentHover = ImVec4(0.96f, 0.50f, 0.16f, 1.0f);
const ImVec4 kAccentActive = ImVec4(0.78f, 0.33f, 0.06f, 1.0f);
const ImVec4 kGo = ImVec4(0.18f, 0.52f, 0.25f, 1.0f);   // the Quick Start button and its page's start buttons
const ImVec4 kGoHover = ImVec4(0.23f, 0.62f, 0.31f, 1.0f);
const ImVec4 kGoActive = ImVec4(0.14f, 0.43f, 0.20f, 1.0f);
// the High Scores button and the new-high-score banner: gold, apart from PLAY's orange and Quick Start's green; its text is dark
const ImVec4 kGold = ImVec4(0.93f, 0.72f, 0.14f, 1.0f);
const ImVec4 kGoldHover = ImVec4(0.98f, 0.80f, 0.26f, 1.0f);
const ImVec4 kGoldActive = ImVec4(0.80f, 0.60f, 0.08f, 1.0f);
const ImVec4 kGoldText = ImVec4(0.10f, 0.07f, 0.01f, 1.0f);

// The headings' font (Segoe UI Semibold, when the system has it)
ImFont* g_heading_font = nullptr;

void dim_wrapped(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool operator==(const Bindings& a, const Bindings& b) {
    return a.keys == b.keys && a.pad == b.pad;
}

std::string settings_text(const Settings& s) {
    IniFile ini;
    write_settings(ini, s);
    return ini.text();
}

// ---------------------------------------------------------------- background work

struct Task {
    enum class Kind { None, Import, Prepare, Play, Upscale };
    Kind kind = Kind::None;
    std::thread thread;
    std::mutex mutex;
    std::vector<std::string> log;
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> game_started{false};
    ImportProgress progress;
    UpscaleProgress upscale;   // Kind::Upscale (upscale_task.hpp)
    bool ok = false;           // set by the worker before `finished`
    std::string error;
    long exit_code = 0;

    void add(const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex);
        log.push_back(line);
        if (log.size() > 4000) {
            log.erase(log.begin(), log.begin() + 1000);
        }
    }
    std::vector<std::string> lines() {
        std::lock_guard<std::mutex> lock(mutex);
        return log;
    }
};

// ---------------------------------------------------------------- dialogs (SDL calls back on any thread)

enum class Pick { None, GameFolder, DiscImage, Destination };

struct DialogSlot {
    std::mutex mutex;
    Pick purpose = Pick::None;
    std::string path;
    bool ready = false;
};
DialogSlot g_dialog;

void SDLCALL on_dialog(void* userdata, const char* const* files, int) {
    std::lock_guard<std::mutex> lock(g_dialog.mutex);
    g_dialog.purpose = static_cast<Pick>(reinterpret_cast<std::intptr_t>(userdata));
    g_dialog.path = files != nullptr && files[0] != nullptr ? files[0] : "";
    g_dialog.ready = true;
}

// ---------------------------------------------------------------- the app

struct Capture {
    bool active = false;
    bool pad = false;
    std::size_t action = 0;
    std::size_t slot = 0;
    std::string message;
    int hotkey = -1;                // >= 0: the capture is for that hotkey of the Hotkeys tab (a key with its modifiers, or a combination of controller buttons)
    std::uint32_t pad_mask = 0;     // a combination being pressed: every button seen
    std::uint32_t pad_held = 0;     // and those still down; the combination is taken when the last one comes up
};

bool is_modifier_scancode(SDL_Scancode code) {
    return code == SDL_SCANCODE_LCTRL || code == SDL_SCANCODE_RCTRL || code == SDL_SCANCODE_LALT || code == SDL_SCANCODE_RALT || code == SDL_SCANCODE_LSHIFT ||
           code == SDL_SCANCODE_RSHIFT;
}

void apply_theme(bool light);   // the colours of the dark or the light theme (below)

class App {
public:
    SDL_Window* window = nullptr;
    bool quit = false;
    std::string page;   // --page: the tab (or "setup") shown first; the screenshot mode of the tests uses it
    std::string controls_page;   // the Controls tab's page to show first (gamepad, keyboard, hotkeys)
    bool play_requested = false;   // --play: start the game at once with the saved settings (a desktop shortcut that skips the launcher's page)
    float scale = 1.0f;            // the display's content scale: every size in the layout is multiplied by it
    SDL_Texture* logo = nullptr;   // the header's picture (resource 2), or none
    bool update_check_allowed = true;   // false for --no-update-check and a --screenshot: the launcher makes no web request

    void init(const fs::path& install_dir) {
        where = opentony::layout::Layout{install_dir};
        config = IniFile::load(where.ini());
        settings = read_settings(config);
        if (config.get_int("Launcher", "SettingsVersion", 0) < 2 && upscaled_set_usable()) {
            settings.upscaled_textures = true;   // a new settings file is Enhanced, which uses the upscaled textures when they have been made
        }
        saved_settings = settings;
        trick_sets = read_trick_sets(IniFile::load(trick_sets_file()));
        saved_trick_sets = trick_sets;
        presets = read_presets(IniFile::load(presets_file()));
        refresh_display_modes();
        if (!settings.game_dir.empty()) {
            load_game(path_from_utf8(settings.game_dir));
        } else if (check_game_folder(where.game(), where.unpacked(), false).usable()) {
            load_game(where.game());   // the files the setup imported (Data\Game)
        }
        screen = check.usable() && page != "setup" ? Screen::Main : Screen::Setup;
        destination = utf8_of(where.game());
        last_run = last_session_line(where.sessions());
        if (settings.check_updates && update_check_allowed) {
            start_update_check();
        }
    }

    void shutdown() {
        if (screen == Screen::Main && (settings_dirty() || bindings_dirty() || tricks_dirty())) {
            save();   // a box still being typed in when the window closed
        }
        task.progress.cancel = true;
        task.upscale.cancel = true;   // the census stops its runs, the pipeline its batch
        if (task.thread.joinable()) {
            task.thread.join();   // an import stops at the cancel; the launcher cannot be closed while the game runs (its window is hidden then)
        }
    }

    // A hotkey's key (with the modifiers held) or controller combination. Escape cancels either; a modifier alone is not a key; the buttons are taken when the last one is released
    bool hotkey_capture_event(const SDL_Event& event) {
        HotkeyBinding& binding = settings.hotkeys[static_cast<std::size_t>(capture.hotkey)];
        if (!capture.pad) {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    capture.active = false;
                } else if (!is_modifier_scancode(event.key.scancode)) {
                    const std::uint8_t code = key_code_from_scancode(event.key.scancode);
                    if (code == 0) {
                        capture.message = std::string(SDL_GetScancodeName(event.key.scancode)) + " can't be used. Try another key.";
                    } else {
                        binding.key = code;
                        binding.mods = ((event.key.mod & SDL_KMOD_CTRL) != 0 ? kHotkeyCtrl : 0u) | ((event.key.mod & SDL_KMOD_ALT) != 0 ? kHotkeyAlt : 0u) |
                                       ((event.key.mod & SDL_KMOD_SHIFT) != 0 ? kHotkeyShift : 0u);
                        capture.active = false;
                    }
                }
                return true;
            }
            return event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT;
        }
        int bit = -1;
        bool down = false;
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
            bit = event.gbutton.button >= 0 && event.gbutton.button < 15 ? event.gbutton.button : -1;   // SDL3's numbering is the standalone's (OtHalGamepadButton)
            down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            bit = event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 15 : 16;
            if (event.gaxis.value > 20000) {
                down = true;
            } else if (event.gaxis.value < 8000) {
                down = false;
            } else {
                return true;
            }
        } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
            capture.active = false;
            return true;
        }
        if (bit >= 0) {
            if (down) {
                capture.pad_mask |= 1u << bit;
                capture.pad_held |= 1u << bit;
            } else {
                capture.pad_held &= ~(1u << bit);
                if (capture.pad_held == 0 && capture.pad_mask != 0) {
                    binding.pad = capture.pad_mask;
                    capture.active = false;
                    nav_back_at = SDL_GetTicks() + 400;
                }
            }
            return true;
        }
        return event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
    }

    // events the binding capture takes before ImGui sees them; true when consumed
    bool capture_event(const SDL_Event& event) {
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            SDL_OpenGamepad(event.gdevice.which);
            return false;
        }
        if (!capture.active) {
            return false;
        }
        if (capture.hotkey >= 0) {
            return hotkey_capture_event(event);
        }
        if (!capture.pad && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            const std::uint8_t code = key_code_from_scancode(event.key.scancode);
            if (code == 0 || !tables || !key_bindable(*tables, code)) {
                capture.message = std::string(SDL_GetScancodeName(event.key.scancode)) + " can't be used. Try another key.";
                return true;
            }
            assign_key(bindings, capture.action, capture.slot, code);
            capture.active = false;
            return true;
        }
        if (!capture.pad && (event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT)) {
            return true;
        }
        if (capture.pad) {
            int button = -2;
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                button = pad_button_from_sdl(event.gbutton.button);
                if (button == -1) {
                    capture.message = "The d-pad is for movement. Try another button.";
                    return true;
                }
            } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event.gaxis.value > 20000) {
                if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                    button = 6;
                } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                    button = 7;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                capture.active = false;
                return true;
            }
            if (button >= 0) {
                assign_pad(bindings, capture.action, button);
                capture.active = false;
                nav_back_at = SDL_GetTicks() + 400;   // the button that bound is still held: the menu waits before it reads the pad again
                return true;
            }
            return event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP;
        }
        return false;
    }

    void frame() {
        const bool light_now = settings.theme == Theme::Light || (settings.theme == Theme::System && system_theme_light());
        if (light_now != theme_light) {
            theme_light = light_now;
            apply_theme(theme_light);
        }
        poll_dialog();
        poll_task();
        if (play_requested && screen == Screen::Main && !task.running) {
            play_requested = false;
            start_play();
        }
        ImGuiIO& io = ImGui::GetIO();
        if (capture.active && capture.pad) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        } else if (SDL_GetTicks() >= nav_back_at) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("##root", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        // the content is one column, at most kMaxContentWidth wide and centred: on a large window the form is not left behind in a corner (a child, not a wider window
        // padding, so that the rules ImGui draws across a tab bar and a separator stay inside it)
        const float available = ImGui::GetContentRegionAvail().x;
        const float column = std::min(available, px(kMaxContentWidth));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - column) * 0.5f);
        ImGui::BeginChild("##column", ImVec2(column, 0), ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        header();
        if (screen == Screen::Setup) {
            setup_screen();
        } else {
            main_screen();
        }
        task_popup();
        if (screen != Screen::Setup) {
            upscale_popup();
        }
        capture_popup();
        ImGui::EndChild();
        ImGui::End();
        autosave();
    }

    static constexpr float kMaxContentWidth = 1180.0f;   // the widest the page's column grows (before the display's scale)

    bool light() const { return theme_light; }
    bool game_running() const { return task.kind == Task::Kind::Play && task.running && task.game_started; }

private:
    enum class Screen { Setup, Main };
    // what the main screen shows under the header: the settings' tabs, or the Quick Start page, which takes the whole area and has three tabs of its own: the
    // Session, its Tricks (the trick sets' editor) and the High Scores (the header's gold button opens that one)
    enum class View { Settings, QuickStart, Scores };   // Scores: the High Scores page, apart from Quick Start so the footer keeps Quick Start where it is
    enum class QuickTab { Session, Tricks };
    View view = View::Settings;
    QuickTab quick_tab = QuickTab::Session;
    int quick_tab_request = -1;   // the Quick Start tab to select (a QuickTab), once
    bool speed_run_open = false;   // the Goals Speed Run was clicked: its two ways to start show under the buttons
    // after the game returns with a new high score: the header's High Scores button pulses and a callout names the score beside it, then slides away (draw_score_flash)
    bool score_flash = false;
    Uint64 score_flash_at = 0;
    std::string score_flash_text;
    int banner_kind = 0;        // the board the button opens while it flashes: board_kind
    bool banner_cash = false;   // and board_cash
    // the header's icon and title return to the settings: where they were last drawn (the hover is read a frame late), and whether the pointer is on them
    ImVec2 header_home_min = ImVec2(0, 0);
    ImVec2 header_home_max = ImVec2(0, 0);
    bool theme_light = false;   // the theme drawn now (settings.theme is applied at the start of a frame)

    // The operating system's light or dark setting, for Theme: System; asked about twice a second (a change of it is noticed then), dark when the system cannot say
    bool system_theme_light() {
        const Uint64 now = SDL_GetTicks();
        if (now - system_theme_at >= 500 || system_theme_at == 0) {
            system_theme_at = now;
            system_light = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT;
        }
        return system_light;
    }
    Uint64 system_theme_at = 0;
    bool system_light = false;

    opentony::layout::Layout where;
    IniFile config;
    Settings settings;
    Settings saved_settings;
    Screen screen = Screen::Setup;

    fs::path game_dir;
    GameCheck check;
    std::optional<RetailTables> tables;
    std::string tables_error;
    Bindings bindings;
    Bindings saved_bindings;
    std::vector<std::string> binding_problems;

    std::vector<std::pair<int, int>> display_modes;
    bool custom_resolution = false;

    // the Quick Start's trick sets (core/trick_sets.hpp): Config\TrickSets.ini as edited and as last saved, and the catalog of the game's TRICKS.BIN (unpacked by
    // the first start: until then it is tried again every few seconds)
    std::vector<TrickSet> trick_sets;
    std::vector<TrickSet> saved_trick_sets;
    // the person's own presets (Config\Presets.ini, core/settings.hpp), written the moment one is saved or deleted; the footer's Save as... name box and its error
    std::vector<CustomPreset> presets;
    char preset_name_edit[kPresetNameMax + 1] = {};
    std::string preset_name_error;
    std::string preset_delete_name;   // the preset the Delete dialog is about
    std::optional<TrickCatalog> trick_catalog;
    std::string trick_catalog_error;
    Uint64 trick_catalog_tried = 0;
    int trick_page_request = -1;   // the Edit Tricks tab to select (--page specials), once
    int trick_start_skater = 0;    // Edit Tricks' "Start from" skater
    char trick_filter[64] = {};    // the open trick list's search
    char trick_name_edit[48] = {}; // the Rename box
    std::string trick_name_error;

    // setup
    std::optional<ImportPlan> plan;
    std::string destination;
    std::string setup_message;
    bool setup_message_bad = false;

    Task task;
    Capture capture;
    Uint64 nav_back_at = 0;
    std::string status;
    bool status_bad = false;
    std::string last_run;
    // the High Scores page (the header's button): the score file as last read, what it shows, and how many runs it had before the last PLAY (the runs after are marked new)
    opentony::scores::File board;
    bool board_loaded = false;
    static constexpr int kAllLevels = -1;   // board_level: the best run of each level, best first
    static constexpr std::size_t kBoardRows = 20;
    int board_kind = 0;   // 0 Scores, 1 Speed Runs, 2 Competitions
    bool board_cash = false;   // the speed runs shown: false every goal, true 100%
    int board_level = kAllLevels;
    std::size_t board_before_play = static_cast<std::size_t>(-1);
    // the upscaled textures' panel (Graphics > Upscale textures...)
    bool upscale_panel = false;
    int upscale_scale = upscale_setup::chosen_options().scale;
    upscale_setup::State upscale_state_cached;
    bool upscale_state_valid = false;
    struct Crash {   // the crash panel (note_crash)
        bool active = false;
        std::string verdict, reason, message;
        bool message_bad = false;
        bool debug = false;          // the run was a debug-mode one: the panel keeps its manual zip and the run's folder; off, the zip is made at once
        char description[2048] = {};   // what the player says they were doing; goes into the zip as what_happened.txt
        fs::path folder, zip;
    } crash;
    std::shared_ptr<UpdateCheck> update;   // the update notice's request (start_update_check): none until one has been made

    // ------------------------------------------------------------ state

    void refresh_display_modes() {
        display_modes.clear();
        int count = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &count);
        for (int k = 0; modes != nullptr && k < count; ++k) {
            const std::pair<int, int> size = {modes[k]->w, modes[k]->h};
            if (size.first >= 640 && size.second >= 480 && std::find(display_modes.begin(), display_modes.end(), size) == display_modes.end()) {
                display_modes.push_back(size);
            }
        }
        SDL_free(modes);
        std::sort(display_modes.begin(), display_modes.end());
        const std::pair<int, int> current = {settings.resolution_x, settings.resolution_y};
        custom_resolution = settings.resolution_x != 0 && std::find(display_modes.begin(), display_modes.end(), current) == display_modes.end();
    }

    void load_game(const fs::path& folder) {
        game_dir = folder;
        trick_catalog.reset();   // read again from the new files when a page needs it
        trick_catalog_tried = 0;
        check = check_game_folder(folder, where.unpacked());
        tables = load_retail_tables(folder, where.static_image(), tables_error);
        binding_problems.clear();
        if (tables) {
            bindings = read_bindings(IniFile::load(where.ini()), *tables, &binding_problems);   // the game's [KEYBOARD] and [JOYPAD], in the same file as the settings
        } else {
            bindings = Bindings{};
        }
        saved_bindings = bindings;
    }

    bool settings_dirty() const { return settings_text(settings) != settings_text(saved_settings); }
    bool bindings_dirty() const { return tables && !(bindings == saved_bindings); }
    bool tricks_dirty() const { return trick_sets != saved_trick_sets; }
    fs::path trick_sets_file() const { return where.config() / "TrickSets.ini"; }
    fs::path presets_file() const { return where.config() / "Presets.ini"; }

    // The catalog of the game's tricks, or null while the game files are not unpacked (read again every few seconds until they are)
    const TrickCatalog* tricks_catalog() {
        const Uint64 now = SDL_GetTicks();
        if (!trick_catalog && (trick_catalog_tried == 0 || now - trick_catalog_tried > 3000)) {
            trick_catalog_tried = std::max<Uint64>(now, 1);
            trick_catalog = TrickCatalog::load(where.package_data() / "data" / "TRICKS.BIN", trick_catalog_error);
        }
        return trick_catalog ? &*trick_catalog : nullptr;
    }

    TrickSet* trick_set_named(const std::string& name) {
        for (TrickSet& set : trick_sets) {
            if (!name.empty() && equal_nocase(set.name, name)) {
                return &set;
            }
        }
        return nullptr;
    }

    std::string unused_trick_set_name(std::string base) {
        base = trim(base.substr(0, 34));
        std::string name = base;
        for (int n = 2; trick_set_named(name) != nullptr; ++n) {
            name = base + " " + std::to_string(n);
        }
        return name;
    }

    std::vector<Conflict> conflicts() const { return tables ? find_conflicts(bindings, *tables) : std::vector<Conflict>{}; }

    bool save_config() {
        std::error_code ec;
        fs::create_directories(where.config(), ec);
        if (!config.save(where.ini())) {
            set_status("Cannot write " + utf8_of(where.ini()), true);
            return false;
        }
        return true;
    }

    static constexpr const char* kBindingsWait = "Bindings not saved until the conflicts are fixed.";

    // Writes what changed: the settings, the trick sets, and the bindings unless two of them conflict (those wait until the conflict is fixed). There is no Save
    // button: autosave() calls this as soon as a change is made, and PLAY and the upscaler call it first with `always` (the settings file is written even unchanged)
    bool save(bool always = false) {
        const bool bindings_ready = bindings_dirty() && conflicts().empty();
        if (always || settings_dirty() || bindings_ready) {
            config = IniFile::load(where.ini());   // keep whatever else the file holds: the game's own sections, the player's comments
            write_settings(config, settings);
            if (bindings_ready) {
                write_bindings(config, bindings, *tables);   // the game's [KEYBOARD] and [JOYPAD], in the same file
            }
            if (!save_config()) {
                saved_settings = settings;   // not tried again every frame: the next change (or PLAY) tries again
                return false;
            }
            saved_settings = settings;
            if (bindings_ready) {
                saved_bindings = bindings;
                binding_problems.clear();
            }
        }
        if (tricks_dirty()) {
            if (!write_trick_sets(trick_sets).save(trick_sets_file())) {
                set_status("Cannot write " + utf8_of(trick_sets_file()), true);
                saved_trick_sets = trick_sets;   // not tried again every frame: the next change tries again
                return false;
            }
            saved_trick_sets = trick_sets;
        }
        if (bindings_dirty()) {
            set_status(kBindingsWait, true);
            return false;
        }
        if (status == kBindingsWait) {
            set_status("", false);
        }
        return true;
    }

    // Once a frame: a change is written when it is finished (not while a slider is dragged or a box is typed in, nor while a binding is being captured)
    void autosave() {
        if (screen != Screen::Main || ImGui::IsAnyItemActive() || capture.active) {
            return;
        }
        if (settings_dirty() || bindings_dirty() || tricks_dirty()) {
            save();
        }
    }

    bool write_presets_file() {
        std::error_code ec;
        fs::create_directories(where.config(), ec);
        if (!write_presets(presets).save(presets_file())) {
            set_status("Cannot write " + utf8_of(presets_file()), true);
            return false;
        }
        return true;
    }

    void set_status(const std::string& text, bool bad) {
        status = text;
        status_bad = bad;
    }

    // ------------------------------------------------------------ dialogs and tasks

    void open_dialog(Pick purpose) {
        void* userdata = reinterpret_cast<void*>(static_cast<std::intptr_t>(purpose));
        const std::string start = !game_dir.empty() ? utf8_of(game_dir) : utf8_of(where.root);
        if (purpose == Pick::DiscImage) {
            static const SDL_DialogFileFilter filters[] = {{"Disc images (*.iso, *.bin, *.img)", "iso;bin;img"}, {"All files", "*"}};
            SDL_ShowOpenFileDialog(on_dialog, userdata, window, filters, 2, start.c_str(), false);
        } else {
            SDL_ShowOpenFolderDialog(on_dialog, userdata, window, start.c_str(), false);
        }
    }

    void poll_dialog() {
        Pick purpose;
        std::string path;
        {
            std::lock_guard<std::mutex> lock(g_dialog.mutex);
            if (!g_dialog.ready) {
                return;
            }
            g_dialog.ready = false;
            purpose = g_dialog.purpose;
            path = g_dialog.path;
        }
        if (path.empty()) {
            return;   // cancelled
        }
        const fs::path chosen = path_from_utf8(path);
        switch (purpose) {
        case Pick::GameFolder: {
            const GameCheck found = check_game_folder(chosen, where.unpacked());
            if (found.usable()) {
                use_game_folder(chosen);
            } else if ((!found.exe.empty() && !found.exe_supported() && found.build != GameBuild::Original2000Protected) || !plan_import(chosen).error.empty()) {
                // (the original 2000 disc's protected THawk2.exe: the folder is imported, so that only the executable is left to replace)
                show_setup_message(utf8_of(chosen) + ": " + found.summary(), true);
            } else {
                prepare_import(chosen);   // a disc layout picked as a game folder: import it
            }
            break;
        }
        case Pick::DiscImage:
            prepare_import(chosen);
            break;
        case Pick::Destination:
            destination = path;
            break;
        case Pick::None:
            break;
        }
    }

    void show_setup_message(const std::string& text, bool bad) {
        setup_message = text;
        setup_message_bad = bad;
    }

    void prepare_import(const fs::path& source) {
        ImportPlan p = plan_import(source);
        if (!p.error.empty()) {
            plan.reset();
            show_setup_message(p.error, true);
            return;
        }
        show_setup_message("", false);
        plan = std::move(p);
    }

    // what [Launcher] GameDir keeps: nothing for the default Data\Game (so a moved install folder still finds its files), else the folder the player chose
    std::string stored_game_dir(const fs::path& folder) const {
        std::error_code ec;
        return fs::equivalent(folder, where.game(), ec) ? std::string() : utf8_of(folder);
    }

    void use_game_folder(const fs::path& folder) {
        settings.game_dir = stored_game_dir(folder);
        load_game(folder);
        save_settings_only();
        if (!check.prepared) {
            start_prepare(false);
        } else {
            screen = Screen::Main;
        }
    }

    void save_settings_only() {
        config = IniFile::load(where.ini());
        Settings to_save = saved_settings;
        to_save.game_dir = settings.game_dir;
        write_settings(config, to_save);
        save_config();
        saved_settings.game_dir = settings.game_dir;
    }

    fs::path opentony_exe() const { return where.build() / "opentony.exe"; }

    bool begin_task(Task::Kind kind) {
        if (task.running) {
            return false;
        }
        if (task.thread.joinable()) {
            task.thread.join();
        }
        {
            std::lock_guard<std::mutex> lock(task.mutex);
            task.log.clear();
        }
        task.kind = kind;
        task.ok = false;
        task.error.clear();
        task.exit_code = 0;
        task.finished = false;
        task.game_started = false;
        task.progress.cancel = false;
        task.progress.done = 0;
        task.progress.total = 0;
        task.running = true;
        return true;
    }

    // opentony.exe --prepare: the static data of THawk2.exe and the unpacked ALL.PKR (once)
    static bool run_prepare(Task& task, const fs::path& exe, const fs::path& root, const fs::path& game, bool force) {
        std::error_code ec;
        if (!fs::is_regular_file(exe, ec)) {
            task.error = "opentony.exe is missing from " + utf8_of(exe.parent_path());
            return false;
        }
        task.add("Unpacking the game data (first run only, about 200 MB)...");
        std::vector<std::string> arguments = {"--root", utf8_of(root), "--game-dir", utf8_of(game), "--prepare"};
        if (force) {
            arguments.push_back("--force");
        }
        std::string error;
        const long code = run_process(exe, arguments, game, {}, [&task](const std::string& line) { task.add(line); }, {}, error);
        if (code != 0) {
            task.error = code == -1 ? error : "opentony.exe --prepare stopped with exit code " + std::to_string(code);
            return false;
        }
        return true;
    }

    void start_import() {
        if (!plan || !begin_task(Task::Kind::Import)) {
            return;
        }
        const ImportPlan p = *plan;
        const fs::path target = path_from_utf8(destination);
        const fs::path exe = opentony_exe();
        const fs::path root = where.root;
        task.thread = std::thread([this, p, target, exe, root] {
            task.add("Copying " + std::to_string(p.items.size()) + " files from " + utf8_of(p.source) + " to " + utf8_of(target) + "...");
            std::string error;
            bool ok = run_import(p, target, task.progress, error);
            if (!ok) {
                task.error = error;
            } else {
                task.add("Copied.");
                ok = run_prepare(task, exe, root, target, false);
            }
            task.ok = ok;
            task.running = false;
            task.finished = true;
        });
    }

    void start_prepare(bool force) {
        if (!begin_task(Task::Kind::Prepare)) {
            return;
        }
        const fs::path game = game_dir;
        const fs::path exe = opentony_exe();
        const fs::path root = where.root;
        task.thread = std::thread([this, game, exe, root, force] {
            task.ok = run_prepare(task, exe, root, game, force);
            task.running = false;
            task.finished = true;
        });
    }

    // quick: -1 the game as usual (the PLAY button), 0 straight into a free skate, 1 a single session, 2 a goals speed run, 3 a competition (the Quick Start page's buttons)
    void start_play(int quick = -1) {
        if (!save(true) || !begin_task(Task::Kind::Play)) {
            return;
        }
        const fs::path exe = opentony_exe();
        const fs::path game = game_dir;
        std::vector<std::string> arguments = {"--root", utf8_of(where.root), "--game-dir", utf8_of(game)};
        if (settings.play_movies) {
            arguments.push_back("--movies");
        }
        board_before_play = opentony::scores::load(where.scores()).records.size();   // the runs after these are this session's (the High Scores page marks them)
        score_flash = false;
        speed_run_open = false;
        Settings launch = settings;   // the quick start is for this launch only: it is neither saved nor set by PLAY
        launch.quick_start = quick >= 0;
        launch.quick_mode = quick >= 0 ? quick : launch.quick_mode;
        launch.quick_tricks.clear();   // the trick set's block, for a quick start only (OT_QUICK_TRICKS)
        if (const TrickSet* set = trick_set_named(settings.quick_trick_set); quick >= 0 && set != nullptr && tricks_catalog() != nullptr) {
            launch.quick_tricks = trick_block_hex(trick_catalog->block(*set));
        }
        const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        std::error_code pack_ec;
        const bool pack = fs::is_regular_file(where.upscaled() / "stamp.txt", pack_ec);   // a set the pipeline made
        std::error_code colour_ec;
        const bool colour = true_colour_usable(launch) && fs::is_directory(where.package_data() / "newtex", colour_ec);   // made below when it is missing or old
        auto environment = launch_environment(launch, utf8_of(where.screenshots()), desktop != nullptr ? desktop->h : 0, utf8_of(music_root()),
                                              utf8_of(where.scores()), pack ? utf8_of(where.upscaled()) : std::string(),
                                              colour ? utf8_of(where.true_colour()) : std::string());
        const fs::path colour_source = colour ? where.package_data() : fs::path();
        const fs::path colour_out = where.true_colour();
        if (const char* given = std::getenv("OT_TEXTURE_PACK"); given != nullptr && *given != '\0') {
            for (auto& [name, value] : environment) {   // a pack named where the launcher was started (the test .cmd files) wins over the setting
                if (name == "OT_TEXTURE_PACK") {
                    value = given;
                }
            }
        }
        task.thread = std::thread([this, exe, game, arguments, environment, colour_source, colour_out] {
            std::error_code ec;
            if (!colour_source.empty() && !true_colour::is_current(colour_source, colour_out)) {
                // seconds, once: the unpacked bitmaps again in all 24 bits (a new import or a new version makes it again)
                task.add("Preparing true-colour textures...");
                const true_colour::Result made = true_colour::build(colour_source, colour_out);
                task.add("True-colour textures: " + std::to_string(made.written) + " written, " + std::to_string(made.failed) + " failed.");
            }
            if (!fs::is_regular_file(exe, ec)) {
                task.error = "The game program (opentony.exe) is missing from " + utf8_of(exe.parent_path()) + ". Reinstall THPSource2 to restore it.";
                task.exit_code = -1;   // not a crash: the game never started
            } else {
                std::string error;
                task.exit_code = run_process(exe, arguments, game, environment, [this](const std::string& line) { task.add(line); },
                                             [this] { task.game_started = true; }, error);
                task.ok = task.exit_code == 0;
                if (task.exit_code == -1) {
                    task.error = error;
                }
            }
            task.running = false;
            task.finished = true;
        });
    }

    void poll_task() {
        if (!task.finished) {
            return;
        }
        task.finished = false;
        if (task.thread.joinable()) {
            task.thread.join();
        }
        switch (task.kind) {
        case Task::Kind::Import:
            if (task.ok) {
                plan.reset();
                settings.game_dir = stored_game_dir(path_from_utf8(destination));
                load_game(path_from_utf8(destination));
                save_settings_only();
            }
            [[fallthrough]];
        case Task::Kind::Prepare:
            if (task.ok) {
                load_game(game_dir);
                if (check.usable()) {
                    screen = Screen::Main;
                    task.kind = Task::Kind::None;   // nothing left to show
                    set_status("Game files ready.", false);
                }
            }
            break;
        case Task::Kind::Play: {
            last_run = last_session_line(where.sessions());
            note_crash(last_run);
            load_scores();   // the runs this session added are marked on the High Scores page
            note_banner();
            // the game's own Options menu may have changed the bindings in the settings file
            if (tables) {
                binding_problems.clear();
                bindings = read_bindings(IniFile::load(where.ini()), *tables, &binding_problems);
                saved_bindings = bindings;
            }
            if (task.exit_code == -1) {
                set_status(task.error, true);
            } else if (task.ok) {
                set_status("", false);
            } else {
                char hex[16];
                std::snprintf(hex, sizeof hex, "%08lX", static_cast<unsigned long>(task.exit_code));
                set_status(std::string("The game stopped unexpectedly (0x") + hex + "). See System > Open logs.", true);
            }
            task.kind = Task::Kind::None;
            break;
        }
        case Task::Kind::Upscale:
            upscale_state_valid = false;
            if (task.ok && settings.texture_filter == 1 && settings.anisotropy != 0) {
                settings.upscaled_textures = true;   // made to be used: on, unless the filtering it needs is off (the checkbox is disabled then)
            }
            set_status(task.ok ? "Upscaled textures are ready."
                       : task.error.rfind("Cancelled", 0) == 0 ? task.error
                                                               : "Texture upscaling failed: " + task.error,
                       !task.ok);
            break;   // the popup stays up with the log until it is closed
        case Task::Kind::None:
            break;
        }
    }

    // ------------------------------------------------------------ drawing
    //
    // Every page is a form: a label on the left, its control on the right, and what the option does in a tooltip on either.

    float px(float value) const { return value * scale; }
    float label_width() const { return px(250.0f); }
    float control_width() const { return px(330.0f); }

    // What the item just drawn does: a tooltip while the pointer is on it
    static void tooltip(const char* text) {
        if (text == nullptr || text[0] == '\0') {
            return;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    // A section's heading: semibold, in the text colour (the accent colour is kept for what can be switched, chosen or started)
    static void section(const char* name) {
        ImGui::Spacing();
        ImGui::PushFont(g_heading_font, ImGui::GetStyle().FontSizeBase);
        ImGui::SeparatorText(name);
        ImGui::PopFont();
    }

    // Why the option just drawn is greyed, on its own line's right: no row moves when it comes and goes
    static void note_if(bool when, const char* text) {
        if (when) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kDim, "%s", text);
        }
    }

    // A choice of a few, as buttons side by side with the chosen one filled (the view of a page, not a field of a form)
    bool segmented(const char* id, int& value, const char* const* names, std::size_t count) {
        bool changed = false;
        ImGui::PushID(id);
        for (std::size_t k = 0; k < count; ++k) {
            const bool on = value == static_cast<int>(k);
            if (k != 0) {
                ImGui::SameLine(0.0f, px(4.0f));
            }
            if (on) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            if (ImGui::Button(names[k]) && !on) {
                value = static_cast<int>(k);
                changed = true;
            }
            if (on) {
                ImGui::PopStyleColor(4);
            }
        }
        ImGui::PopID();
        return changed;
    }

    // The tabs inside a tab (the Controls pages, the trick pages) are pills with no rule under them, so they read as a level below the page's own tabs
    void sub_tabs(const char* id, const std::function<void()>& pages) {
        ImGuiStyle& style = ImGui::GetStyle();
        const ImVec4 none(0, 0, 0, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, px(14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_TabBarOverlineSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Tab, none);
        ImGui::PushStyleColor(ImGuiCol_TabDimmed, none);
        ImGui::PushStyleColor(ImGuiCol_TabHovered, style.Colors[ImGuiCol_FrameBgHovered]);
        ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f));
        if (ImGui::BeginTabBar(id)) {
            pages();
            ImGui::EndTabBar();
        }
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);
    }

    // the label of a row; the control that follows goes in the right column at the control width
    void row(const char* label, const char* tip) {
        const float x = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        tooltip(tip);
        ImGui::SameLine(x + label_width());
        ImGui::SetNextItemWidth(control_width());
    }

    // the right column, for a line under a row
    void indent_to_control() { ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_width()); }

    static bool combo(const char* id, int& value, const char* const* names, std::size_t count, const char* none = nullptr) {
        bool changed = false;
        const char* shown = value >= 0 && value < static_cast<int>(count) ? names[value] : none != nullptr ? none : names[0];
        if (ImGui::BeginCombo(id, shown)) {
            for (std::size_t k = 0; k < count; ++k) {
                if (ImGui::Selectable(names[k], value == static_cast<int>(k))) {
                    value = static_cast<int>(k);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool combo_row(const char* label, const char* tip, int& value, const char* const* names, std::size_t count) {
        row(label, tip);
        ImGui::PushID(label);
        const bool changed = combo("##v", value, names, count);
        tooltip(tip);
        ImGui::PopID();
        return changed;
    }

    bool check_row(const char* label, const char* tip, bool& value) {
        row(label, tip);
        ImGui::PushID(label);
        const bool changed = ImGui::Checkbox("##v", &value);
        tooltip(tip);
        ImGui::PopID();
        return changed;
    }

    bool slider_row(const char* label, const char* tip, int& value, int low, int high, const char* format = "%d") {
        row(label, tip);
        ImGui::PushID(label);
        const bool changed = ImGui::SliderInt("##v", &value, low, high, format, ImGuiSliderFlags_AlwaysClamp);
        tooltip(tip);
        ImGui::PopID();
        return changed;
    }

    bool button(const char* label, const char* tip = nullptr, float width = 0.0f) {
        const bool pressed = ImGui::Button(label, ImVec2(width, 0));
        tooltip(tip);
        return pressed;
    }

    // The score callout and the pulse around the High Scores button after a new high score: it fades in and slides in from the right, the button pulses with a ring that
    // spreads from it, then the callout fades and slides back out and the button goes quiet.
    static constexpr double kScoreFlashMs = 10000.0;
    void draw_score_flash(ImVec2 button_min, ImVec2 button_max) {
        const double elapsed = static_cast<double>(SDL_GetTicks() - score_flash_at);
        if (elapsed >= kScoreFlashMs) {
            score_flash = false;
            return;
        }
        const float in = static_cast<float>(std::clamp(elapsed / 450.0, 0.0, 1.0));
        const float out = static_cast<float>(std::clamp((kScoreFlashMs - elapsed) / 700.0, 0.0, 1.0));
        const float fade = std::min(in, out);
        const float ease_in = 1.0f - (1.0f - in) * (1.0f - in);
        const float ease_out = 1.0f - out * out;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float rounding = ImGui::GetStyle().FrameRounding;
        // the rings: two, a half period apart, each spreading and fading
        const float period = 1400.0f;
        for (int ring = 0; ring < 2; ++ring) {
            const float phase = std::fmod(static_cast<float>(elapsed) + ring * period * 0.5f, period) / period;
            const float grow = px(3.0f) + phase * px(14.0f);
            const float alpha = (1.0f - phase) * 0.75f * fade;
            draw->AddRect(ImVec2(button_min.x - grow, button_min.y - grow), ImVec2(button_max.x + grow, button_max.y + grow), ImGui::GetColorU32(ImVec4(kGold.x, kGold.y, kGold.z, alpha)),
                          rounding + grow, px(2.0f));
        }
        // the callout, to the left of the button
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.05f);
        const ImVec2 text = ImGui::CalcTextSize(score_flash_text.c_str());
        const float pad_x = px(14.0f);
        const float pad_y = px(6.0f);
        const float height = text.y + pad_y * 2.0f;
        const float slide = (1.0f - ease_in) * px(36.0f) + ease_out * px(36.0f);
        const float right = button_min.x - px(14.0f) + slide;
        const ImVec2 min(right - text.x - pad_x * 2.0f, (button_min.y + button_max.y) * 0.5f - height * 0.5f);
        const ImVec2 max(right, min.y + height);
        draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(kGold.x, kGold.y, kGold.z, fade)), height * 0.5f);
        draw->AddText(ImVec2(min.x + pad_x, min.y + pad_y), ImGui::GetColorU32(ImVec4(kGoldText.x, kGoldText.y, kGoldText.z, fade)), score_flash_text.c_str());
        ImGui::PopFont();
    }

    void header() {
        const float size = ImGui::GetFontSize() * 2.4f;
        const float top = ImGui::GetCursorPosY();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        // the icon and the title go back to the settings from any other page; they look it on hover. Where there is no page to go back from, they open the project's
        // page on GitHub instead
        const bool home = screen == Screen::Main && view != View::Settings;
        const bool over_title = ImGui::IsMouseHoveringRect(header_home_min, header_home_max) && ImGui::IsWindowHovered() &&
                                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        const bool home_hot = home && over_title;
        const bool link_hot = !home && over_title;
        if (link_hot) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (home_hot || link_hot) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(header_home_min.x - px(8.0f), header_home_min.y - px(4.0f)), ImVec2(header_home_max.x + px(8.0f), header_home_max.y + px(4.0f)),
                                                      ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.16f)), ImGui::GetStyle().FrameRounding);
        }
        if (home) {   // a big round back button at the far left, as an app's top left has it; the logo and title move over, and the whole group goes back (drawn, the font has no arrow)
            const float d = size * 0.8f;
            ImGui::SetCursorPosY(top + (size - d) * 0.5f);
            ImGui::Dummy(ImVec2(d, d));
            const ImVec2 low = ImGui::GetItemRectMin();
            const ImVec2 centre(low.x + d * 0.5f, low.y + d * 0.5f);
            const float r = d * 0.5f, thick = px(2.8f), reach = r * 0.44f, wing = r * 0.40f;
            const ImVec4 tint = home_hot ? kAccentHover : kAccent;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddCircleFilled(centre, r, ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, home_hot ? 0.30f : 0.16f)), 48);
            draw->AddCircle(centre, r, ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, home_hot ? 0.90f : 0.55f)), 48, px(1.6f));
            const ImU32 ink = ImGui::GetColorU32(tint);
            const ImVec2 tail(centre.x + reach, centre.y), tip(centre.x - reach, centre.y);
            const ImVec2 upper(tip.x + wing, tip.y - wing), lower(tip.x + wing, tip.y + wing);
            draw->AddLine(tail, tip, ink, thick);
            draw->AddLine(tip, upper, ink, thick);
            draw->AddLine(tip, lower, ink, thick);
            for (const ImVec2& end : {tail, tip, upper, lower}) {   // round ends
                draw->AddCircleFilled(end, thick * 0.5f, ink, 12);
            }
            ImGui::SameLine(0.0f, px(14.0f));
            ImGui::SetCursorPosY(top);
        }
        if (logo != nullptr) {
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(logo)), ImVec2(size, size));
            ImGui::SameLine();
        }
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
        ImGui::SetCursorPosY(top + (size - ImGui::GetFontSize()) * 0.5f);
        if (home_hot || link_hot) {
            ImGui::TextColored(kAccentHover, "%s", kTitle);
        } else {
            ImGui::TextUnformatted(kTitle);
        }
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::SetCursorPosY(top + (size - ImGui::GetFontSize()) * 0.5f + px(4.0f));
        ImGui::TextColored(kDim, "%s", kVersion);
        header_home_min = origin;
        header_home_max = ImVec2(ImGui::GetItemRectMax().x, origin.y + size);
        if (home_hot) {
            ImGui::SetTooltip("Back to the settings.");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                view = View::Settings;
            }
        } else if (link_hot) {
            ImGui::SetTooltip("Open THPSource2 on GitHub:\n%s", kRepoUrl);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                SDL_OpenURL(kRepoUrl);
            }
        }
        if (screen == Screen::Main && view != View::Scores) {   // the score board, apart from the settings' tabs: a gold button that opens the High Scores page (hidden on that page: the back arrow leaves it)
            const float width = px(180.0f);
            const float height = ImGui::GetFrameHeight() * 1.4f;
            ImGui::SameLine();
            const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            const float y = top + (size - height) * 0.5f;
            ImGui::SetCursorPos(ImVec2(std::max(ImGui::GetCursorPosX(), right - width), y));   // placed, not SameLine's: that is the version text's line
            const ImVec2 button_min = ImGui::GetCursorScreenPos();
            const bool flashing = score_flash;
            float pulse = 0.0f;
            if (flashing) {
                pulse = 0.5f + 0.5f * std::sin(static_cast<float>(SDL_GetTicks() - score_flash_at) * 0.0085f);
            }
            const auto lift = [pulse](const ImVec4& c) { return ImVec4(c.x + (1.0f - c.x) * 0.45f * pulse, c.y + (1.0f - c.y) * 0.45f * pulse, c.z + (1.0f - c.z) * 0.30f * pulse, 1.0f); };
            ImGui::PushStyleColor(ImGuiCol_Button, lift(kGold));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, lift(kGoldHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kGoldActive);
            ImGui::PushStyleColor(ImGuiCol_Text, kGoldText);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
            const bool pressed = ImGui::Button("High Scores", ImVec2(width, height));
            ImGui::PopFont();
            ImGui::PopStyleColor(4);
            tooltip("Your best runs on each level: scores, speed runs and competitions.");
            if (flashing) {
                draw_score_flash(button_min, ImVec2(button_min.x + width, button_min.y + height));
            }
            if (pressed) {
                if (flashing) {   // the board the new score is on
                    board_kind = banner_kind;
                    board_cash = banner_cash;
                }
                open_scores();
            }
        }
        ImGui::SetCursorPosY(top + size + ImGui::GetStyle().ItemSpacing.y);
    }

    void setup_screen() {
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
        ImGui::TextUnformatted("Find your game files");
        ImGui::PopFont();
        dim_wrapped("THPSource2 runs your copy of Tony Hawk's Pro Skater 2 for PC: the Activision Value re-release, or the original 2000 release with its "
                    "unprotected THawk2.exe (the disc's own is copy-protected and cannot be read).");
        ImGui::Spacing();
        const bool busy = task.running;
        const float width = px(300.0f);
        ImGui::BeginDisabled(busy);
        if (button("Game or disc folder...", "An installed game (THawk2.exe and ALL.PKR) is used where it is. A disc, or a copy of one, is imported.", width)) {
            open_dialog(Pick::GameFolder);
        }
        ImGui::SameLine();
        if (button("Disc image...", "An .iso, .bin or .img file. It is read directly: there is nothing to mount.", width)) {
            open_dialog(Pick::DiscImage);
        }
        ImGui::EndDisabled();

        if (!setup_message.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, setup_message_bad ? kBad : kGood);
            ImGui::TextWrapped("%s", setup_message.c_str());
            ImGui::PopStyleColor();
        }

        if (plan) {
            section("Import");
            std::size_t music = 0, movies = 0;
            for (const ImportItem& item : plan->items) {
                music += item.target.rfind("Music/", 0) == 0 ? 1 : 0;
                movies += item.target.rfind("med/Movies/", 0) == 0 ? 1 : 0;
            }
            row("From", nullptr);
            ImGui::TextUnformatted(utf8_of(plan->source).c_str());
            row("Contents", nullptr);
            ImGui::Text("Game, %zu music tracks, %zu movies (%.0f MB)", music, movies, static_cast<double>(plan->total_bytes()) / 1048576.0);
            ImGui::BeginDisabled(busy);
            row("To", "The default, Data\\Game in the launcher's folder, is where the game looks first.");
            ImGui::SetNextItemWidth(control_width() * 1.4f);
            char buffer[1024];
            std::snprintf(buffer, sizeof buffer, "%s", destination.c_str());
            if (ImGui::InputText("##destination", buffer, sizeof buffer)) {
                destination = buffer;
            }
            ImGui::SameLine();
            if (button("Browse...")) {
                open_dialog(Pick::Destination);
            }
            ImGui::Spacing();
            indent_to_control();
            if (button("Import", nullptr, px(140.0f))) {
                start_import();
            }
            ImGui::SameLine();
            if (button("Cancel", nullptr, px(140.0f))) {
                plan.reset();
            }
            ImGui::EndDisabled();
        }

        if (!game_dir.empty() && !check.usable()) {
            section("Current game folder");
            ImGui::TextUnformatted(utf8_of(game_dir).c_str());
            ImGui::TextColored(kBad, "%s", check.summary().c_str());
        }
        if (check.usable() && !busy) {
            ImGui::Spacing();
            ImGui::Spacing();
            if (button("Back", nullptr, px(140.0f))) {
                screen = Screen::Main;
            }
        }
    }

    // The run's line of Logs\sessions.txt, `stamp | verdict | played Ns | folder runs\NAME | build X | reason`: a run that ended as a CRASH or a HANG puts the crash panel up
    static std::vector<std::string> session_fields(const std::string& line) {
        std::vector<std::string> fields;
        for (std::size_t from = 0, at; from <= line.size(); from = at + 3) {
            at = line.find(" | ", from);
            if (at == std::string::npos) {
                fields.push_back(line.substr(from));
                break;
            }
            fields.push_back(line.substr(from, at - from));
        }
        return fields;
    }

    void note_crash(const std::string& line) {
        crash = Crash{};
        const std::vector<std::string> fields = session_fields(line);
        if (fields.size() < 6 || (fields[1].rfind("CRASH", 0) != 0 && fields[1].rfind("HANG", 0) != 0)) {
            return;
        }
        crash.active = true;
        crash.verdict = fields[1];
        for (std::size_t k = 5; k < fields.size(); ++k) {   // the reason is the rest of the line (it may hold a " | " of its own)
            crash.reason += (k == 5 ? "" : " | ") + fields[k];
        }
        if (crash.reason.empty() || crash.reason == "no error") {
            crash.reason = "No error was recorded. The run's log has the details.";
        }
        const std::string prefix = "folder runs\\";
        if (fields[3].rfind(prefix, 0) == 0) {
            crash.folder = where.runs() / path_from_utf8(fields[3].substr(prefix.size()));
        }
        crash.debug = settings.debug_mode;
        if (!crash.debug) {
            make_report(false);   // not in debug mode: the report is ready before the player sees the panel, the box only adds to it
        }
    }

    // Zips the crashed run's folder to Logs\reports\<run>.zip, with the player's own words in it (what_happened.txt) when there are any
    bool make_report(bool show) {
        std::error_code ec;
        if (crash.folder.empty() || !fs::is_directory(crash.folder, ec)) {
            crash.message = "The run's log folder is gone.";
            crash.message_bad = true;
            return false;
        }
        const fs::path words = crash.folder / "what_happened.txt";
        if (crash.description[0] != '\0') {
            std::ofstream(words, std::ios::binary | std::ios::trunc) << crash.description << '\n';
        } else {
            fs::remove(words, ec);
        }
        const fs::path zip = where.logs() / "reports" / (crash.folder.filename().string() + ".zip");
        std::string error;
        if (!zip_folder(crash.folder, zip, error)) {
            crash.message = error;
            crash.message_bad = true;
            return false;
        }
        crash.zip = zip;
        crash.message = "Report saved: " + utf8_of(zip);
        crash.message_bad = false;
        if (show) {
            open_in_explorer(zip.parent_path());
        }
        return true;
    }

    // The panel's main button: the report zip is made and shown in Explorer, and the bug report form opens in the browser to drag it into. Nothing is sent from here:
    // the player attaches the file themselves, after seeing it. Without a run folder (gone since) the form still opens and the player can say it in words.
    void report_on_github() {
        std::error_code ec;
        const bool have_folder = !crash.folder.empty() && fs::is_directory(crash.folder, ec);
        const bool zipped = have_folder && make_report(false);   // sets crash.message to what happened to the zip
        if (zipped) {
            reveal_in_explorer(crash.zip);
        }
        if (!SDL_OpenURL(kIssueUrl)) {
            crash.message = std::string("The browser could not be opened. Report it at ") + kIssueUrl + (zipped ? " and attach the zip." : "");
            crash.message_bad = true;
        } else if (zipped) {
            crash.message = "Drag the selected zip into the issue.";
            crash.message_bad = false;
        }
    }

    // After a run that ended as a CRASH or a HANG: what stopped it, a box for what the player was doing, and the report zip
    void crash_panel() {
        ImGui::PushStyleColor(ImGuiCol_Border, kBad);
        ImGui::BeginChild("##crash", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(kBad, "The game stopped unexpectedly (%s)", crash.verdict.c_str());
        ImGui::TextUnformatted(crash.reason.c_str());
        tooltip(crash.reason.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##what_happened", "What were you doing? (optional, saved with the report when you click away, press Enter or Dismiss)", crash.description,
                                 sizeof crash.description);
        std::error_code ec;
        const bool have_folder = !crash.folder.empty() && fs::is_directory(crash.folder, ec);
        if (ImGui::IsItemDeactivatedAfterEdit() && have_folder) {
            make_report(false);   // the note goes into the report as soon as the player leaves the box: no button to find
        }
        dim_wrapped("To report this: click Report on GitHub. Your report zip is selected in Explorer and the bug report form opens in your browser; drag the zip into it. "
                    "Nothing is sent for you. The logs hold file paths from this PC, such as the install folder.");
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        const bool report = button("Report on GitHub", "Makes the report zip (the run's logs and your note), shows it in Explorer and opens the bug report form on GitHub. "
                                                       "You attach the zip yourself.");
        ImGui::PopStyleColor(4);
        if (report) {
            report_on_github();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!have_folder);
        if (button(crash.debug ? "Create report" : "Open report", "Zips the run's logs, with your note, into Logs\\reports and opens the folder.")) {
            make_report(true);
        }
        ImGui::SameLine();
        if (button("Open logs")) {
            open_in_explorer(crash.folder);
        }
        ImGui::EndDisabled();
        if (crash.debug) {
            ImGui::SameLine();
            if (button("Open notes.txt")) {
                open_in_explorer(where.notes());
            }
        }
        ImGui::SameLine();
        if (button("Dismiss", "Closes this panel. A note in the box is saved with the report first.")) {
            if (crash.description[0] != '\0' && have_folder) {
                make_report(false);
            }
            crash = Crash{};
        }
        if (!crash.message.empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(crash.message_bad ? kBad : kGood, "%s", crash.message.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ------------------------------------------------------------ updates (core/update_check.hpp, app/update_task.hpp)

    void start_update_check() {
        if (update && update->state() == UpdateCheck::State::Checking) {
            return;   // one request at a time
        }
        update = UpdateCheck::start(kRepoUrl, kVersion);
    }

    // A slim notice above the page when the newest release is newer than this launcher and the player has not dismissed that release. It only points at the release
    // page: nothing is downloaded or installed from here. Dismiss is remembered (`[Launcher] SkippedUpdate`), so the next release shows its own notice.
    void update_banner() {
        if (!settings.check_updates || !update || update->state() != UpdateCheck::State::Newer) {
            return;
        }
        const std::string latest = update->latest();
        if (latest == settings.skipped_update) {
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
        ImGui::BeginChild("##update", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kAccent, "THPSource2 %s is available", latest.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kDim, "(you have %s)", kVersion);
        ImGui::SameLine(0.0f, ImGui::GetFontSize());
        if (button("Download", "Opens the release page on GitHub in your browser.")) {
            SDL_OpenURL(kLatestReleaseUrl);
        }
        ImGui::SameLine();
        if (button("Dismiss", "Hides this notice until a newer release comes out.")) {
            settings.skipped_update = latest;   // saved at once, as every change is
        }
        dim_wrapped("To update, close the launcher, extract the new zip over this folder and start it again. Your settings, saves and game files are kept.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // The System page's line for the last check: what it found, or why it found nothing
    void update_status() {
        using State = UpdateCheck::State;
        if (!update) {
            ImGui::TextColored(kDim, "Not checked yet");
            return;
        }
        switch (update->state()) {
        case State::Checking:
            ImGui::TextColored(kDim, "Checking...");
            break;
        case State::Newer:
            ImGui::TextColored(kAccent, "%s is available (you have %s)", update->latest().c_str(), kVersion);
            break;
        case State::Current:
            ImGui::TextColored(kGood, "You have the latest version (%s)", kVersion);
            break;
        case State::NoRelease:
            ImGui::TextColored(kDim, "No published release found");
            break;
        case State::Failed:
            ImGui::TextColored(kBad, "Could not reach GitHub");
            break;
        }
    }

    void main_screen() {
        if (crash.active) {
            crash_panel();
        }
        update_banner();
        // --page: the old tab names still open the page that holds them
        static const std::pair<const char*, const char*> kAliases[] = {{"sound", "audio"}, {"game", "gameplay"}, {"files", "system"}, {"gamepad", "controls"},
                                                                         {"keyboard", "controls"}, {"hotkeys", "controls"}};
        if (page == "upscale") {   // --page upscale: the Graphics page with the upscaled textures' panel open
            page = "graphics";
            open_upscale();
        } else if (page == "scores") {   // --page scores, --page quickstart: the header buttons' pages
            page.clear();
            open_scores();
        } else if (page == "quickstart") {
            page.clear();
            open_quick(QuickTab::Session);
        } else if (page == "speedrun") {   // --page speedrun: the Session tab with the Goals Speed Run's two ways to start open
            page.clear();
            open_quick(QuickTab::Session);
            speed_run_open = true;
        } else if (page == "tricks" || page == "specials") {   // --page tricks, --page specials: the Tricks tab on its first page or its Specials
            trick_page_request = page == "specials" ? static_cast<int>(TrickPage::Special) : 0;
            page.clear();
            open_trick_editor();
        } else if (page == "reset") {   // --page reset: the System tab with the "Reset all settings?" question open
            page = "system";
            reset_dialog_request = true;
        } else if (page == "update") {   // --page update: the update notice, as for a newer release (no web request; the screenshot mode of the tests)
            update = UpdateCheck::answered(UpdateCheck::State::Newer, "v9.9");
            page = "system";   // the banner above, and the Updates section below it
        } else if (page == "crash") {   // --page crash: the crash panel, as after a crashed run with no log folder left (the screenshot mode of the tests)
            note_crash("2026-01-01 12:00:00 | CRASH (3221225477) | played 12 s | folder runs\\2026-01-01_12-00-00_CRASH-3221225477 | build 2026-01-01 | "
                       "Access violation reading 0x00000010");
            crash.message.clear();
            page.clear();
        } else if (page == "banner") {   // --page banner: the High Scores button's flash after a run, made up and a few seconds in (the screenshot mode of the tests)
            score_flash = true;
            score_flash_at = SDL_GetTicks() - 3000;
            score_flash_text = "New high score! 48,210 on Hangar";
            banner_kind = 0;
            banner_cash = false;
            page.clear();
        } else if (page == "session") {   // --page session: the High Scores page as after a run, the file's last run taken as the session's (the screenshot mode of the tests)
            load_scores();
            board_before_play = board.records.empty() ? 0 : board.records.size() - 1;
            page.clear();
            open_scores();
        }
        for (const auto& [old_name, name] : kAliases) {
            if (page == old_name) {
                if (std::string(name) == "controls") {
                    controls_page = page;
                }
                page = name;
            }
        }
        // under the page, the footer's rule, a gap, and its buttons
        const float footer = ImGui::GetFrameHeight() * 1.6f + ImGui::GetStyle().ItemSpacing.y * 3.0f + 1.0f;
        const float below = footer;
        // the page of a tab: its own scrolling area
        const auto page_area = [this, below](const char* id, void (App::*draw)()) {
            ImGui::BeginChild(id, ImVec2(0, -below), ImGuiChildFlags_AlwaysUseWindowPadding);
            (this->*draw)();
            ImGui::EndChild();
        };
        if (view == View::Scores) {   // the header's gold button opens the board: a page of its own, the footer is the settings' (Quick Start and PLAY stay)
            page_title("High Scores", "Your best runs on each level: scores (career levels and single sessions), speed runs by time, competitions by the judges' total.");
            ImGui::BeginChild("##scorespage", ImVec2(0, -below), ImGuiChildFlags_AlwaysUseWindowPadding);
            scores_page();
            ImGui::EndChild();
            // Escape goes back, unless a list or a box has it
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
                view = View::Settings;
            }
            footer_bar();
            return;
        }
        if (view == View::QuickStart) {
            page_title("Quick Start",
                       quick_tab == QuickTab::Tricks ? "One set of tricks for every skater in a Quick Start, chosen on the Session tab. PLAY and your career keep each "
                                                       "skater's own tricks."
                                                     : "Starts straight in the level, with every level and skater open. Your save file is not touched. Quit from the "
                                                       "pause menu returns here.");
            if (ImGui::BeginTabBar("##quick")) {
                const auto tab = [this, &page_area](const char* name, QuickTab which, void (App::*draw)()) {
                    const ImGuiTabItemFlags flags = quick_tab_request == static_cast<int>(which) ? ImGuiTabItemFlags_SetSelected : 0;
                    if (ImGui::BeginTabItem(name, nullptr, flags)) {
                        quick_tab = which;
                        page_area("##quickpage", draw);
                        ImGui::EndTabItem();
                    }
                };
                tab("Session", QuickTab::Session, &App::quick_start_page);
                tab("Tricks", QuickTab::Tricks, &App::tricks_page);
                quick_tab_request = -1;
                ImGui::EndTabBar();
            }
            // Escape goes back, unless a list or a box has it
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
                view = View::Settings;
            }
            footer_bar();
            return;
        }
        if (ImGui::BeginTabBar("##sections")) {
            const auto tab = [this, &page_area](const char* name, const char* key, void (App::*draw)()) {
                const ImGuiTabItemFlags flags = page == key ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(name, nullptr, flags)) {
                    page_area("##page", draw);
                    ImGui::EndTabItem();
                }
            };
            tab("Display", "display", &App::display_tab);
            tab("Graphics", "graphics", &App::graphics_tab);
            tab("Audio", "audio", &App::audio_tab);
            tab("Controls", "controls", &App::controls_tab);
            tab("Gameplay", "gameplay", &App::gameplay_tab);
            tab("System", "system", &App::system_tab);
            page.clear();
            ImGui::EndTabBar();
        }
        footer_bar();
    }

    void resolution_row() {
        const WindowMode mode = settings.window_mode();
        const char* automatic = mode == WindowMode::Windowed ? "Auto" : "Desktop";
        std::string current = automatic;
        if (custom_resolution) {
            current = "Custom";
        } else if (settings.resolution_x != 0) {
            current = std::to_string(settings.resolution_x) + " x " + std::to_string(settings.resolution_y);
        }
        const char* tip = mode == WindowMode::Windowed     ? "Auto opens the largest window that fits the desktop."
                          : mode == WindowMode::Fullscreen ? "A set resolution switches the display to it (exclusive fullscreen). Desktop keeps the desktop's."
                                                           : "Borderless covers the screen at the desktop's resolution.";
        row("Resolution", tip);
        if (ImGui::BeginCombo("##resolution", current.c_str())) {
            if (ImGui::Selectable(automatic, !custom_resolution && settings.resolution_x == 0)) {
                custom_resolution = false;
                settings.resolution_x = settings.resolution_y = 0;
            }
            for (auto it = display_modes.rbegin(); it != display_modes.rend(); ++it) {
                const auto [w, h] = *it;
                const std::string label = std::to_string(w) + " x " + std::to_string(h);
                if (ImGui::Selectable(label.c_str(), !custom_resolution && settings.resolution_x == w && settings.resolution_y == h)) {
                    custom_resolution = false;
                    settings.resolution_x = w;
                    settings.resolution_y = h;
                }
            }
            if (ImGui::Selectable("Custom", custom_resolution)) {
                custom_resolution = true;
                if (settings.resolution_x == 0) {
                    settings.resolution_x = 1280;
                    settings.resolution_y = 960;
                }
            }
            ImGui::EndCombo();
        }
        tooltip(tip);
        if (custom_resolution) {
            indent_to_control();
            const float half = (control_width() - ImGui::CalcTextSize("x").x - ImGui::GetStyle().ItemSpacing.x * 2.0f) * 0.5f;
            ImGui::SetNextItemWidth(half);
            ImGui::InputInt("##width", &settings.resolution_x, 0, 0);
            ImGui::SameLine();
            ImGui::TextUnformatted("x");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(half);
            ImGui::InputInt("##height", &settings.resolution_y, 0, 0);
            if (!ImGui::IsAnyItemActive()) {
                settings.resolution_x = std::clamp(settings.resolution_x, 640, 16384);
                settings.resolution_y = std::clamp(settings.resolution_y, 480, 16384);
            }
        }
    }

    void display_tab() {
        section("Screen");
        int mode = static_cast<int>(settings.window_mode());
        static const char* const kModes[] = {"Windowed", "Borderless", "Fullscreen"};
        if (combo_row("Window mode", nullptr, mode, kModes, 3)) {
            settings.set_window_mode(static_cast<WindowMode>(mode));
        }
        resolution_row();
        combo_row("Aspect ratio", "Auto matches the window, or the screen in borderless and fullscreen.",
                  settings.aspect, kAspectOptions.data(), kAspectOptions.size());
        ImGui::BeginDisabled(settings.aspect == 1);
        check_row("Widescreen FOV", "Field of view. On, a wider picture shows more of the level at the sides. Off, it keeps the 4:3 view and crops the top and bottom.",
                  settings.fov_adjust);
        ImGui::EndDisabled();
        note_if(settings.aspect == 1, "Not used at 4:3");

        static const char* const kRates[] = {"30 (original)", "60", "120"};
        int rate = settings.frame_rate == 120 ? 2 : settings.frame_rate == 60 ? 1 : 0;
        const char* rate_tip = "Game speed is the same at every rate. Menus run at 30.";
        if (combo_row("Frame rate", rate_tip, rate, kRates, 3)) {
            settings.frame_rate = rate == 2 ? 120 : rate == 1 ? 60 : 30;
        }
        const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        const float refresh = desktop != nullptr ? desktop->refresh_rate : 0.0f;
        if (settings.frame_rate != 30 && frame_rate_judders(settings.frame_rate, settings.vsync, refresh)) {
            ImGui::SameLine();
            ImGui::TextColored(kNote, "Uneven at %.0f Hz", refresh);
            char tip[256];
            std::snprintf(tip, sizeof tip, "Your display runs at %.0f Hz, which is not a multiple of %d, so some frames show longer than others. Turn V-Sync off, set the display to "
                          "a multiple of %d Hz, or ignore this on a G-Sync or FreeSync display.", refresh, settings.frame_rate, settings.frame_rate);
            tooltip(tip);
        }
        static const char* const kVsync[] = {"Driver default", "Off", "On"};
        int vsync = settings.vsync + 1;
        if (combo_row("V-Sync", "On caps the frame rate at the display's refresh rate.", vsync, kVsync, 3)) {
            settings.vsync = vsync - 1;
        }

        section("HUD");
        const bool wide_hud = settings.aspect != 1;
        ImGui::BeginDisabled(!wide_hud);
        static const char* const kLayouts[] = {"Centered 4:3", "Anchored to edges"};
        int layout = settings.hud_anchor ? 1 : 0;
        if (combo_row("HUD layout", "How the HUD sits on a wide picture. Anchored moves each element to the nearest edge at its original size.", layout, kLayouts, 2)) {
            settings.hud_anchor = layout == 1;
        }
        ImGui::EndDisabled();
        note_if(!wide_hud, "Needs a wide picture");
        {
            const SDL_DisplayMode* display = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
            const int height = settings.resolution_y > 0 ? settings.resolution_y : display != nullptr ? display->h : 0;
            const char* tip = "Auto is the original size up to 1080p and smaller on larger displays.";
            row("HUD scale", tip);
            bool automatic = settings.hud_size == 0;
            if (ImGui::Checkbox("Auto##hud", &automatic)) {
                settings.hud_size = automatic ? 0 : auto_hud_size(height);
            }
            tooltip(tip);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(control_width() - (ImGui::GetCursorPosX() - ImGui::GetCursorStartPos().x - label_width()));
            int shown = automatic ? auto_hud_size(height) : settings.hud_size;
            ImGui::BeginDisabled(automatic);
            if (ImGui::SliderInt("##hudsize", &shown, kHudSizeMin, kHudSizeMax, "%d%%", ImGuiSliderFlags_AlwaysClamp) && !automatic) {
                settings.hud_size = shown;
            }
            ImGui::EndDisabled();
            tooltip(tip);
        }
        bool show_hud = !settings.hide_hud;
        if (check_row("Show HUD", "Off hides the score, trick names, timer and tips, for screenshots and video.", show_hud)) {
            settings.hide_hud = !show_hud;
        }
        check_row("Grind balance meter", "The balance bar from the PlayStation version, shown while grinding.", settings.grind_meter);
        check_row("Show song title", "Shows the band and title at the bottom left when a song starts in a level, for the game's own songs and your custom ones.",
                  settings.music_now_playing);
        check_row("FPS counter", nullptr, settings.show_fps);
    }

    void graphics_tab() {
        section("Resolution and output");
        combo_row("Render resolution", "The resolution the game draws at. Native matches the window.", settings.internal_resolution, kInternalResolutionOptions.data(),
                  kInternalResolutionOptions.size());
        // the output looks scale a picture smaller than the window, so they exist at the two original sizes only: at Native the target is the window's own (or a
        // supersampled one that comes down through its own filter) and the look is always Clean. The stored choice is kept for the PlayStation and PC sizes
        const bool native = settings.internal_resolution >= 2;
        int look = native ? 0 : settings.scaling;
        ImGui::BeginDisabled(native);
        if (combo_row("Output",
                      "How the picture is scaled to the window. Clean: smooth. Sharp pixels: whole-number steps with soft edges. Nearest pixels: hard edges. CRT: scanlines and "
                      "a phosphor mask. Not used at Native.",
                      look, kScalingOptions.data(), kScalingOptions.size())) {
            settings.scaling = look;
        }
        ImGui::EndDisabled();
        note_if(native, "Needs Render resolution: PlayStation or PC");
        check_row("Curved screen",
                  "Bends the picture like the glass of a CRT tube, with rounded corners. It works with any output and any render resolution.", settings.crt_curve);
        ImGui::BeginDisabled(settings.internal_resolution != 0);
        check_row("PlayStation dither",
                  "Adds the PlayStation's 4 x 4 dither pattern and 15-bit colour, like the console's own picture. It is applied to the finished picture, not to each polygon as the "
                  "console did, so it is an approximation. Only at Render resolution PlayStation (512 x 240).",
                  settings.psx_dither);
        ImGui::EndDisabled();
        note_if(settings.internal_resolution != 0, "Needs Render resolution: PlayStation");

        section("Quality");
        ImGui::BeginDisabled(!native);
        int supersample = supersample_option_index(settings.supersample);
        if (combo_row("Supersampling", "Draws larger and scales down for the cleanest picture, at a high GPU cost: 2x is four times the pixels. Lowered automatically "
                                       "when the GPU cannot hold it. Needs Render resolution: Native.",
                      supersample, kSupersampleOptions.data(), kSupersampleOptions.size())) {
            settings.supersample = kSupersampleValues[static_cast<std::size_t>(supersample)];
        }
        ImGui::EndDisabled();
        note_if(!native, "Needs Render resolution: Native");
        static const char* const kMsaa[] = {"Off", "2x MSAA", "4x MSAA", "8x MSAA"};
        int msaa = settings.msaa == 0 ? 0 : settings.msaa == 2 ? 1 : settings.msaa == 4 ? 2 : 3;
        if (combo_row("Anti-aliasing", "Smooths the edges of polygons.", msaa, kMsaa, 4)) {
            settings.msaa = msaa == 0 ? 0 : 2 << (msaa - 1);
        }
        ImGui::BeginDisabled(settings.msaa == 0);
        check_row("Transparency anti-aliasing", "Also smooths fences, grass and other cut-out textures. Needs anti-aliasing.", settings.smooth_cutouts);
        ImGui::EndDisabled();
        note_if(settings.msaa == 0, "Needs anti-aliasing");
        combo_row("Ambient occlusion",
                  "Darkens creases and contact areas where surfaces meet, such as under ramps, along walls and around the skater's feet, to add depth to the scene. "
                  "High uses more samples than Low, at a small GPU cost.",
                  settings.ambient_occlusion, kAmbientOcclusionOptions.data(), kAmbientOcclusionOptions.size());

        section("Textures");
        static const char* const kTextureSets[] = {"PC (higher quality)", "PlayStation (lower quality)"};
        int texture_set = settings.use_psx_textures ? 1 : 0;
        if (combo_row("Texture set", "PC uses the PC version's higher-resolution textures. PlayStation uses the console's originals, which are lower resolution.",
                      texture_set, kTextureSets, 2)) {
            settings.use_psx_textures = texture_set == 1;
        }
        static const char* const kColours[] = {"PC (lighter)", "PlayStation (richer colour)"};
        int colour = settings.psx_colour ? 1 : 0;
        if (combo_row("Colour balance", "PC is the PC version's lighter gamma. PlayStation is the console's richer colour.", colour, kColours, 2)) {
            settings.psx_colour = colour == 1;
        }
        ImGui::BeginDisabled(settings.use_psx_textures);
        check_row("True-colour textures",
                  "Draws the PC textures in all 24 bits of colour instead of 15, which removes the banding in skies and soft shading. The first Play after a change takes a few "
                  "seconds to prepare them. Upscaled textures, when on, take priority.",
                  settings.true_colour);
        ImGui::EndDisabled();
        note_if(settings.use_psx_textures, "Not used with the PlayStation textures");
        combo_row("Texture filtering", "Smooth blends textures. Sharp keeps the original blocky look.", settings.texture_filter, kTextureFilterOptions.data(),
                  kTextureFilterOptions.size());
        ImGui::BeginDisabled(settings.texture_filter == 0);
        int anisotropy = anisotropy_option_index(settings.anisotropy);
        if (combo_row("Anisotropic filtering", "Keeps ground and walls sharp in the distance and at steep angles. Needs smooth filtering.", anisotropy,
                      kAnisotropyOptions.data(), kAnisotropyOptions.size())) {
            settings.anisotropy = kAnisotropyValues[static_cast<std::size_t>(anisotropy)];
        }
        ImGui::EndDisabled();
        note_if(settings.texture_filter == 0, "Needs smooth filtering");
        ImGui::BeginDisabled(settings.texture_filter == 0);
        check_row("Crisp HUD and menus", "Keeps 2D art and text sharp when scaled up. Needs smooth filtering.", settings.sharp_hud);
        ImGui::EndDisabled();
        note_if(settings.texture_filter == 0, "Needs smooth filtering");
        upscaled_rows();

        section("Distance and detail");
        static const auto fog_names = [] {
            std::array<const char*, std::tuple_size_v<std::remove_cvref_t<decltype(kFogOptions)>>> names{};
            for (std::size_t k = 0; k < kFogOptions.size(); ++k) {
                names[k] = kFogOptions[k].label;
            }
            return names;
        }();
        int fog = fog_option_index(settings.fog_distance);
        if (combo_row("Fog", "How far you see before the level fades into fog, as a multiple of each level's own fog distance (the PlayStation's).", fog, fog_names.data(), fog_names.size())) {
            settings.fog_distance = kFogOptions[static_cast<std::size_t>(fog)].distance;
        }
        int detail = detail_option_index(settings.detail_distance);
        if (combo_row("Object detail", "How far away objects keep their full model and stay drawn.", detail, kDetailOptions.data(), kDetailOptions.size())) {
            settings.detail_distance = kDetailValues[static_cast<std::size_t>(detail)];
        }
    }

    void binding_problems_box() {
        for (const std::string& problem : binding_problems) {
            ImGui::PushStyleColor(ImGuiCol_Text, kNote);
            ImGui::TextWrapped("%s", problem.c_str());
            ImGui::PopStyleColor();
        }
        for (const Conflict& conflict : conflicts()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kBad);
            ImGui::TextWrapped("%s", conflict.message.c_str());
            ImGui::PopStyleColor();
        }
    }

    bool tables_ready() {
        if (tables) {
            return true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        ImGui::TextWrapped("The controls need THawk2.exe's key table, which could not be read: %s", tables_error.c_str());
        ImGui::PopStyleColor();
        return false;
    }

    void controls_tab() {
        sub_tabs("##controls", [this] {
            const auto sub = [this](const char* name, const char* key, void (App::*draw)()) {
                const ImGuiTabItemFlags flags = controls_page == key ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(name, nullptr, flags)) {
                    ImGui::Spacing();
                    (this->*draw)();
                    ImGui::EndTabItem();
                }
            };
            sub("Controller", "gamepad", &App::gamepad_tab);
            sub("Keyboard", "keyboard", &App::keyboard_tab);
            sub("Hotkeys", "hotkeys", &App::hotkeys_tab);
            controls_page.clear();
        });
    }

    // `text` cut in the middle with "..." until it is no wider than `room` pixels (a path keeps its drive and its last folders)
    static std::string shortened_middle(const std::string& text, float room) {
        if (ImGui::CalcTextSize(text.c_str()).x <= room) {
            return text;
        }
        std::size_t keep = text.size();
        while (keep > 4) {
            --keep;
            const std::size_t head = (keep + 1) / 3, tail = keep - head;
            const std::string cut = text.substr(0, head) + "..." + text.substr(text.size() - tail);
            if (ImGui::CalcTextSize(cut.c_str()).x <= room) {
                return cut;
            }
        }
        return "...";
    }

    // A key as the player knows it: the game names the keypad's keys PAD2, PAD+ ...
    static std::string key_label(const std::string& name) {
        return name.size() > 3 && name.compare(0, 3, "PAD") == 0 ? "Numpad " + name.substr(3) : name;
    }

    void gamepad_tab() {
        if (!tables_ready()) {
            return;
        }
        int count = 0;
        SDL_JoystickID* pads = SDL_GetGamepads(&count);
        const char* name = count > 0 ? SDL_GetGamepadNameForID(pads[0]) : nullptr;
        section("Controller");
        row("Connected", "Xbox, PlayStation, Switch Pro and other SDL controllers work.");
        if (count == 0) {
            ImGui::TextColored(kDim, "No controller connected");
        } else {
            ImGui::TextColored(kGood, "%s", name != nullptr ? name : "Controller connected");
        }
        tooltip("Xbox, PlayStation, Switch Pro and other SDL controllers work.");
        SDL_free(pads);
        int stick = static_cast<int>(settings.movement_stick) + 1;
        static const char* const kSticks[] = {"D-pad only", "Left stick", "Right stick"};
        if (combo_row("Movement", "The stick that moves the skater and the menus, besides the d-pad. The other stick swings the camera (Camera stick, below).", stick, kSticks,
                      3)) {
            settings.movement_stick = static_cast<MovementStick>(stick - 1);
        }
        note_if(settings.movement_stick == MovementStick::Right, "The left stick looks around");
        combo_row("Vibration", "Game setting leaves it to the in-game options.", settings.vibration, kSwitchOptions.data(), kSwitchOptions.size());
        check_row("DirectInput support (slow)", "For an older controller that does not show up otherwise. On some PCs it makes the launcher and the game take about "
                                       "20 seconds longer to start. Takes effect the next time the launcher starts.", settings.directinput_pads);

        section("Camera stick");
        check_row("Look around", "The stick that does not move the skater swings the camera round the skater and back when let go.", settings.camera_stick);
        ImGui::BeginDisabled(!settings.camera_stick);
        slider_row("Sensitivity", "How far the camera swings at full push.", settings.camera_sensitivity, 1, 9);
        ImGui::EndDisabled();
        note_if(!settings.camera_stick, "Needs Look around");
        ImGui::BeginDisabled(!settings.camera_stick);
        check_row("Invert left-right", "Push right to look left.", settings.camera_invert_x);
        check_row("Invert up-down", "Push up to look down.", settings.camera_invert_y);
        ImGui::EndDisabled();

        section("Buttons");
        dim_wrapped("Choose a button from a list, or click Bind and press the button on the controller.");
        const auto problems = conflicts();
        if (ImGui::BeginTable("##pad", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV, ImVec2(label_width() + control_width() + px(130.0f), 0))) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, label_width() - px(10.0f));
            ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(110.0f));
            for (std::size_t a = 0; a < kPadActionCount; ++a) {
                ImGui::PushID(static_cast<int>(a));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool in_conflict = std::any_of(problems.begin(), problems.end(), [a](const Conflict& c) { return c.action_a == a || c.action_b == a; });
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(in_conflict ? kBad : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", kActions[a].label);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##button", pad_button_name(bindings.pad[a]))) {
                    for (int b = -1; b < kPadButtonCount; ++b) {
                        if (ImGui::Selectable(pad_button_name(b), bindings.pad[a] == b)) {
                            assign_pad(bindings, a, b);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TableNextColumn();
                if (button("Bind", "Press a button on the controller to bind it.", -FLT_MIN)) {
                    capture = Capture{true, true, a, 0, ""};
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (button("Reset buttons", "Puts every button above back to its default. Movement and the camera stick are not changed.")) {
            bindings.pad = tables->default_pad;
        }
        binding_problems_box();
    }

    void keyboard_tab() {
        if (!tables_ready()) {
            return;
        }
        const char* tip = "Click to change, right-click to clear. An action can have up to three keys.";
        dim_wrapped("Click a key to change it, right-click to clear it. An action can have up to three keys.");
        ImGui::Spacing();
        const auto problems = conflicts();
        if (ImGui::BeginTable("##keys", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, label_width() - px(10.0f));
            ImGui::TableSetupColumn("Primary");
            ImGui::TableSetupColumn("Secondary");
            ImGui::TableSetupColumn("Third");
            ImGui::TableHeadersRow();
            for (std::size_t a = 0; a < kActionCount; ++a) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool in_conflict = std::any_of(problems.begin(), problems.end(), [a](const Conflict& c) { return c.action_a == a || c.action_b == a; });
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(in_conflict ? kBad : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", kActions[a].label);
                for (std::size_t slot = 0; slot < kKeysPerAction; ++slot) {
                    ImGui::TableNextColumn();
                    const std::uint8_t code = bindings.keys[a][slot];
                    // a slot after an empty one is not offered: the game reads a chord up to its first empty slot
                    if (slot != 0 && bindings.keys[a][slot - 1] == 0) {
                        continue;
                    }
                    const std::string label = (code != 0 ? key_label(tables->key_names[code]) : std::string("+")) + "##k" + std::to_string(a) + "_" + std::to_string(slot);
                    if (button(label.c_str(), tip, -FLT_MIN)) {
                        capture = Capture{true, false, a, slot, ""};
                    }
                    if (code != 0 && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        assign_key(bindings, a, slot, 0);
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (button("Reset keys", "Puts every key back to its default. The hotkeys are not changed.")) {
            bindings.keys = tables->default_keys;
        }
        binding_problems_box();
    }

    std::string hotkey_key_text(const HotkeyBinding& binding) const {
        if (binding.key == 0) {
            return "None";
        }
        std::string text;
        if ((binding.mods & kHotkeyCtrl) != 0) {
            text += "Ctrl + ";
        }
        if ((binding.mods & kHotkeyAlt) != 0) {
            text += "Alt + ";
        }
        if ((binding.mods & kHotkeyShift) != 0) {
            text += "Shift + ";
        }
        if (tables && !tables->key_names[binding.key].empty()) {
            return text + key_label(tables->key_names[binding.key]);
        }
        char fallback[16];
        std::snprintf(fallback, sizeof fallback, "key 0x%02X", static_cast<unsigned>(binding.key));
        return text + fallback;
    }

    // The game's own key bindings that use the same key: the hotkey still fires, and so does the action
    std::string hotkey_overlap(const HotkeyBinding& binding) const {
        if (binding.key == 0 || binding.mods != 0) {
            return "";
        }
        for (std::size_t a = 0; a < kActionCount; ++a) {
            if (std::find(bindings.keys[a].begin(), bindings.keys[a].end(), binding.key) != bindings.keys[a].end()) {
                return kActions[a].label;
            }
        }
        return "";
    }

    // How long a hotkey is held before it fires (HotkeyBinding::hold_ms): the usual choices; a value from the settings file that is none of them is shown as it is
    static constexpr int kHoldValues[] = {0, 250, 500, 1000, 2000, 3000};
    static constexpr const char* kHoldNames[] = {"Tap", "0.25 s", "0.5 s", "1 s", "2 s", "3 s"};

    void hotkeys_tab() {
        dim_wrapped("Click a field, then press the key, or the controller buttons together. Right-click clears it. Hold is how long it must stay down.");
        ImGui::Spacing();
        if (ImGui::BeginTable("##hotkeys", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Hotkey", ImGuiTableColumnFlags_WidthFixed, label_width() - px(10.0f));
            ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("Hold", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableHeadersRow();
            for (std::size_t h = 0; h < kHotkeyCount; ++h) {
                HotkeyBinding& binding = settings.hotkeys[h];
                ImGui::PushID(static_cast<int>(h));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(kHotkeys[h].label);
                tooltip(kHotkeys[h].note);
                ImGui::TableNextColumn();
                const std::string overlap = hotkey_overlap(binding);
                const std::string key_tip = overlap.empty() ? std::string("Click, then press a key. Hold Ctrl, Alt or Shift for a combination. Right-click to clear.")
                                                            : "The game also uses this key for " + overlap + ": both happen.";
                if (!overlap.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, kNote);
                }
                if (button((hotkey_key_text(binding) + "##key").c_str(), key_tip.c_str(), -FLT_MIN)) {
                    capture = Capture{};
                    capture.active = true;
                    capture.hotkey = static_cast<int>(h);
                }
                if (!overlap.empty()) {
                    ImGui::PopStyleColor();
                }
                if (binding.key != 0 && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    binding.key = 0;
                    binding.mods = 0;
                }
                ImGui::TableNextColumn();
                if (button((hotkey_pad_name(binding.pad) + "##pad").c_str(), "Click, then press the buttons together and release. Right-click to clear.", -FLT_MIN)) {
                    capture = Capture{};
                    capture.active = true;
                    capture.pad = true;
                    capture.hotkey = static_cast<int>(h);
                }
                if (binding.pad != 0 && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    binding.pad = 0;
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                {
                    char custom[24];
                    std::snprintf(custom, sizeof custom, "%.2f s", binding.hold_ms / 1000.0);
                    int chosen = -1;
                    for (int k = 0; k < 6; ++k) {
                        chosen = binding.hold_ms == kHoldValues[k] ? k : chosen;
                    }
                    if (ImGui::BeginCombo("##hold", chosen >= 0 ? kHoldNames[chosen] : custom)) {
                        for (int k = 0; k < 6; ++k) {
                            if (ImGui::Selectable(kHoldNames[k], chosen == k)) {
                                binding.hold_ms = kHoldValues[k];
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                tooltip("How long to hold before it triggers.");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (button("Reset hotkeys", "F8 screenshot, F5 retry, Alt+Enter fullscreen, L3 + R3 held for a second to quit. The keyboard and controller bindings are not changed.")) {
            for (std::size_t h = 0; h < kHotkeyCount; ++h) {
                settings.hotkeys[h] = default_hotkey(static_cast<Hotkey>(h));
            }
        }
    }

    void audio_tab() {
        section("Volume");
        slider_row("Music", "The game's music level at every launch. It can still be changed in the game.", settings.music_level, 0, 10);
        slider_row("Sound effects", "The game's sound effects level at every launch. It can still be changed in the game.", settings.sfx_level, 0, 10);

        section("Custom soundtrack");
        check_row("Enabled", "Plays your own Bink (.bik) songs from Mods\\Music in levels, in place of the game's. Music packs can be dropped in as folders.",
                  settings.custom_music);
        ImGui::BeginDisabled(!settings.custom_music);
        check_row("Shuffle", nullptr, settings.music_shuffle);
        check_row("Include original soundtrack", nullptr, settings.music_include_default);
        if (!music_scanned) {
            rescan_music();
        }
        std::vector<std::string> off = opentony::music::split_list(settings.music_folders_off);
        std::size_t songs = 0;
        row("Folders", "Untick a folder to leave its songs out.");
        ImGui::BeginGroup();
        for (const opentony::music::Folder& folder : music_folders) {
            bool on = !opentony::music::is_off(off, folder.name);
            std::string place = folder.name == "." ? std::string("Mods\\Music") : "Mods\\Music\\" + folder.name;
            std::replace(place.begin(), place.end(), '/', '\\');
            const std::string label = (folder.pack_name.empty() ? place : folder.pack_name) + "##music:" + folder.name;
            if (ImGui::Checkbox(label.c_str(), &on)) {
                off.erase(std::remove(off.begin(), off.end(), folder.name), off.end());
                if (!on) {
                    off.push_back(folder.name);
                }
                settings.music_folders_off = opentony::music::join_list(off);
            }
            tooltip(place.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kDim, "%zu song%s", folder.tracks.size(), folder.tracks.size() == 1 ? "" : "s");
            songs += on ? folder.tracks.size() : 0;
        }
        if (music_folders.empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kDim, "No songs in Mods\\Music");
            tooltip("Put .bik songs or music pack folders in Mods\\Music, then Rescan. Until then the game plays its own music.");
        } else if (songs == 0) {
            ImGui::TextColored(kNote, "No songs selected: the game plays its own music");
        }
        if (button("Open folder")) {
            std::error_code ec;
            fs::create_directories(music_root(), ec);
            open_in_explorer(music_root());
        }
        ImGui::SameLine();
        if (button("Rescan")) {
            rescan_music();
        }
        ImGui::EndGroup();
        ImGui::EndDisabled();
    }

    // the custom soundtrack's folders (src/launcher/music_library.hpp), read when the Audio tab is first shown and on Rescan
    std::vector<opentony::music::Folder> music_folders;
    bool music_scanned = false;
    fs::path music_root() const { return where.mods() / "Music"; }
    void rescan_music() {
        music_folders = opentony::music::scan(music_root());
        music_scanned = true;
    }

    void gameplay_tab() {
        section("Skating");
        combo_row("Autokick", "Pushes the skater along without kicking. Game setting leaves it to the in-game options.", settings.autokick, kSwitchOptions.data(),
                  kSwitchOptions.size());
        static const char* const kCameras[] = {"Default (close)", "Alternate (far)"};
        combo_row("Starting camera", "The camera each level starts with. The camera button still switches between the two.", settings.default_camera, kCameras, 2);
        initials_row();

        section("Game");
        check_row("Pause when unfocused", "Opens the pause menu when you switch to another window during a level.", settings.pause_on_focus_loss);
        check_row("Intro movies", "Plays the intro movies when the game starts.", settings.play_movies);

        section("Unlocks");
        check_row("All cheats", "Every cheat can be switched on in Options > Cheats. Your career save is not changed.", settings.unlock_cheats);
        check_row("All movies", "Every movie can be played from Options > Movies. Your career save is not changed.", settings.unlock_movies);
    }

    // ------------------------------------------------------------ upscaled textures (docs/LAUNCHER.md, "Upscaled textures")

    upscale::Options upscale_options() const {
        upscale::Options o = upscale_setup::chosen_options();
        o.scale = upscale_scale;
        return o;
    }

    const upscale_setup::State& upscale_state() {
        if (!upscale_state_valid) {   // counted when the tab or the panel is drawn first, and again after a task: not every frame
            upscale_state_cached = upscale_setup::state(where.upscaler(), where.texture_dump(), where.upscaled(), upscale_options());
            upscale_state_valid = true;
        }
        return upscale_state_cached;
    }

    // the upscaled set has been made and the filtering it needs is on (Enhanced's own)
    bool upscaled_set_usable() {
        return upscale_setup::set_exists(upscale_state()) && settings.texture_filter == 1 && settings.anisotropy != 0;
    }

    void upscaled_rows() {
        const upscale_setup::State& st = upscale_state();
        const bool have = upscale_setup::set_exists(st);
        const bool detail = settings.texture_filter == 1 && settings.anisotropy != 0;
        const char* tip = !have    ? "Create the high-resolution textures first with Upscale textures..."
                          : !detail ? "Requires smooth texture filtering with anisotropic filtering, which keeps high-resolution textures from shimmering in the distance."
                                    : "Replaces the game's textures with the high-resolution set. At 4x, uses up to about 190 MB of extra video memory and adds about "
                                      "a second of loading, spread over the first moments of a level.";
        ImGui::BeginDisabled(!have || !detail);
        check_row("Upscaled textures", tip, settings.upscaled_textures);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (button("Upscale textures...", "Create, update or delete the high-resolution textures.")) {
            open_upscale();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (!have || !detail) {
            ImGui::TextColored(kNote, "%s", !have ? "Not created yet" : "Requires smooth filtering with anisotropic filtering");
        } else {
            ImGui::TextDisabled("%d textures, %.0f MB", st.upscaled, static_cast<double>(st.upscaled_bytes) / 1048576.0);
        }
    }

    // 4x for a picture of 1440 lines or more (1440p, 4K), 2x below (1080p): the resolution the launcher starts the game at, or the desktop's
    int recommended_upscale() const {
        int lines = settings.resolution_y;
        if (lines == 0) {
            const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
            lines = desktop != nullptr ? desktop->h : 1080;
        }
        return lines >= 1440 ? 4 : 2;
    }

    void open_upscale() {
        upscale_state_valid = false;
        if (!upscale_setup::set_exists(upscale_state())) {
            upscale_scale = recommended_upscale();   // no set yet: start on the size for this display
        }
        upscale_panel = true;
    }

    void upscale_popup() {
        if (upscale_panel && !ImGui::IsPopupOpen("Upscale Textures###upscale")) {
            ImGui::OpenPopup("Upscale Textures###upscale");
        }
        const float width = std::min(ImGui::GetMainViewport()->WorkSize.x * 0.8f, px(720.0f));
        ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Upscale Textures###upscale", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        namespace us = upscale_setup;
        const us::State& st = upscale_state();
        const upscale::Options options = upscale_options();
        const std::vector<us::Fetch> plan = us::download_plan(where.upscaler(), options);
        const bool have = us::set_exists(st);
        ImGui::PushTextWrapPos(width - ImGui::GetStyle().WindowPadding.x);
        ImGui::TextWrapped("Creates high-resolution versions of the game's textures on this PC. World textures are upscaled with an AI model; the HUD, fonts "
                           "and menus are enlarged pixel for pixel so they stay crisp.");
        ImGui::Spacing();
        ImGui::TextColored(kDim, "%s", "Requires a graphics card with Vulkan support. The first run takes about 10 to 15 minutes. You can cancel at any time "
                                       "and continue later.");

        section("Status");
        const auto status_row = [this](const char* label, const std::string& value) {
            ImGui::TextUnformatted(label);
            ImGui::SameLine(px(170.0f));
            ImGui::TextUnformatted(value.c_str());
        };
        status_row("Upscaler", st.tool && st.models ? "Installed" : st.tool ? "Model not installed" : "Not installed");
        status_row("Game textures", st.census      ? opentony::scores::thousands(st.dumped) + " found"
                                    : st.dumped > 0 ? opentony::scores::thousands(st.dumped) + " found (scan not finished)"
                                                    : "Not scanned yet");
        status_row("Upscaled textures", have ? opentony::scores::thousands(st.upscaled) + " (" + std::to_string(static_cast<long long>(st.upscaled_bytes >> 20)) + " MB)"
                                             : "None");

        section("Scale");
        const int recommended = recommended_upscale();
        const auto scale_option = [&](int scale, const char* label, const char* fits) {
            ImGui::RadioButton(label, &upscale_scale, scale);
            ImGui::SameLine(px(80.0f));
            ImGui::TextUnformatted(fits);
            ImGui::SameLine(px(280.0f));
            ImGui::TextColored(kDim, "about %.0f MB", static_cast<double>(us::estimate_bytes(scale)) / 1048576.0);
            if (scale == recommended) {
                ImGui::SameLine(px(400.0f));
                ImGui::TextColored(kGood, "Recommended for your display");
            }
        };
        scale_option(4, "4x", "1440p and 4K displays");
        scale_option(2, "2x", "1080p displays");

        section("Components");
        const us::Download& t = us::tool();
        const us::ModelPin* m = us::model(options.model_world);
        const auto component = [](const std::string& name, const std::string& detail) {
            ImGui::Bullet();
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kDim, "%s", detail.c_str());
        };
        component(std::string(t.name), std::string("runs the AI model on your graphics card. ") + t.licence + ".");
        if (m != nullptr) {
            component(m->author, std::string("AI model for world textures. ") + m->licence + ", for personal use.");
        }
        if (plan.empty()) {
            ImGui::TextColored(kDim, "%s", "Both are installed.");
        } else {
            ImGui::TextColored(kDim, "Downloaded from GitHub when you start (%.0f MB) and verified by SHA-256. Neither is distributed with THPSource2.",
                               static_cast<double>(us::download_bytes(plan)) / 1048576.0);
        }
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::BeginDisabled(task.running || !check.usable());
        if (button(have ? "Update" : "Start", "Downloads anything missing, scans the game for its textures, then upscales them. Work already done is kept.",
                   px(120.0f))) {
            start_upscale();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(st.upscaled == 0);
        if (button("Delete", "Deletes the upscaled textures (Data\\Upscaled). The upscaler and the scanned textures are kept, so they can be recreated "
                             "without downloading again.")) {
            std::error_code ec;
            fs::remove_all(where.upscaled(), ec);
            settings.upscaled_textures = false;
            upscale_state_valid = false;
            set_status(ec ? "Could not delete " + utf8_of(where.upscaled()) + ": " + ec.message() : "Upscaled textures deleted.", static_cast<bool>(ec));
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (button("Close", nullptr, px(120.0f))) {
            upscale_panel = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void start_upscale() {
        if (!save(true) || !begin_task(Task::Kind::Upscale)) {
            return;
        }
        upscale_panel = false;
        task.upscale.cancel = false;
        task.upscale.phase = 0;
        task.upscale.done = 0;
        task.upscale.total = 0;
        const opentony::layout::Layout install = where;
        const fs::path game = game_dir;
        const upscale::Options options = upscale_options();
        const int jobs = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1, 6);
        task.thread = std::thread([this, install, game, options, jobs] {
            task.ok = run_upscale_task(install, game, options, jobs, [this](const std::string& line) { task.add(line); }, task.upscale, task.error);
            task.running = false;
            task.finished = true;
        });
    }

    // the launcher's score initials (OT_INITIALS): on the Gameplay tab, its one place
    void initials_row() {
        {
            const char* tip = "Every high score entry starts with these three letters; a letter changed on the entry screen applies to that score only. Leave empty to "
                              "use the game's.";
            row("High score initials", tip);
            char letters[4] = {0, 0, 0, 0};
            std::memcpy(letters, settings.initials.c_str(), (std::min)(settings.initials.size(), std::size_t{3}));
            ImGui::SetNextItemWidth(px(80.0f));
            const auto upper_letters = [](ImGuiInputTextCallbackData* data) -> int {
                const ImWchar c = data->EventChar;
                if (c >= 'a' && c <= 'z') {
                    data->EventChar = static_cast<ImWchar>(c - 'a' + 'A');
                    return 0;
                }
                return (c >= 'A' && c <= 'Z') || c == ' ' ? 0 : 1;   // letters and space only
            };
            if (ImGui::InputText("##initials", letters, sizeof(letters), ImGuiInputTextFlags_CallbackCharFilter, upper_letters)) {
                settings.initials = letters;
            }
            tooltip(tip);
            if (!settings.initials.empty() && settings.initials.size() != 3) {
                ImGui::SameLine();
                ImGui::TextColored(kNote, "Needs three letters");
            }
        }
    }

    // ------------------------------------------------------------ the High Scores page (docs/LAUNCHER.md, "Scores"): Saves\highscores.tsv, which the game appends to

    void load_scores() {
        board = opentony::scores::load(where.scores());
        board_loaded = true;
    }

    // The Quick Start page on one of its tabs
    void open_quick(QuickTab tab) {
        view = View::QuickStart;
        quick_tab = tab;
        quick_tab_request = static_cast<int>(tab);
    }

    // The High Scores page (the header's button; docs/LAUNCHER.md, "Scores"): the top runs of every level or of one, on the board last
    // shown (scores, speed runs, competitions). It always opens on All.
    void open_scores() {
        if (!board_loaded) {   // read when the page first opens; the game's return reads it again (poll_task)
            load_scores();
        }
        board_level = kAllLevels;
        view = View::Scores;
    }

    // After the game returns (scores read): when the session set a new high score, the High Scores button flashes with a callout of the score (draw_score_flash); a click on
    // the button opens the board it is on. Otherwise nothing is shown.
    void note_banner() {
        namespace sc = opentony::scores;
        score_flash = false;
        const std::vector<sc::Record> fresh = sc::new_bests(board.records, board_before_play);
        if (fresh.empty()) {   // no record, but every run that was written is acknowledged: where it stands on its level
            const std::vector<sc::Record> session = sc::session_runs(board.records, board_before_play);
            if (session.empty()) {
                return;
            }
            const sc::Record& last = session.back();
            banner_kind = last.mode == sc::Mode::Competition ? 2 : sc::is_speed_run(last.mode) ? 1 : 0;
            banner_cash = last.mode == sc::Mode::SpeedRunCash;
            if (session.size() > 1) {
                score_flash_text = std::to_string(session.size()) + " runs recorded";
            } else {
                const sc::Placing where_it_stands = sc::placing(board.records, last);
                std::string what = banner_kind == 2 ? "Competition" : banner_kind == 1 ? "Speed run" : "Run";
                what += " recorded: " + (banner_kind == 2 ? sc::judged_text(last.judged) : banner_kind == 1 ? sc::run_time(last.time_ms) : sc::thousands(last.score)) + " on " +
                        last.level_name + " (#" + std::to_string(where_it_stands.rank) + " of " + std::to_string(where_it_stands.of) + ")";
                score_flash_text = what;
            }
            score_flash = true;
            score_flash_at = SDL_GetTicks();
            return;
        }
        const sc::Record& first = fresh.front();   // the score board's first, then the speed runs', the competitions'
        banner_kind = first.mode == sc::Mode::Competition ? 2 : sc::is_speed_run(first.mode) ? 1 : 0;
        banner_cash = first.mode == sc::Mode::SpeedRunCash;
        if (fresh.size() > 1) {
            score_flash_text = "New high scores on " + std::to_string(fresh.size()) + " levels";
        } else if (banner_kind == 1) {
            score_flash_text = std::string(banner_cash ? "New 100% speed run record! " : "New speed run record! ") + sc::run_time(first.time_ms) + " on " + first.level_name;
        } else if (banner_kind == 2) {
            score_flash_text = "New competition best! " + sc::judged_text(first.judged) + " on " + first.level_name;
        } else {
            score_flash_text = "New high score! " + sc::thousands(first.score) + " on " + first.level_name;
        }
        score_flash = true;
        score_flash_at = SDL_GetTicks();
    }

    // The page's title and what it is for. The way back is the footer's button (where Quick Start was), the header's icon and title, and Esc.
    void page_title(const char* name, const char* what) {
        ImGui::PushFont(g_heading_font, ImGui::GetStyle().FontSizeBase * 1.25f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        ImGui::PopFont();
        dim_wrapped(what);
        ImGui::Separator();
    }

    void scores_page() {
        namespace sc = opentony::scores;
        score_flash = false;   // the player is here: the flash has done its work
        ImGui::Spacing();
        // the board: the score runs (career levels, single sessions), the speed runs (by time; every goal, or 100%), the competitions (by the judges' total)
        static const char* const kBoards[] = {"Scores", "Speed Runs", "Competitions"};
        segmented("##boards", board_kind, kBoards, 3);
        const sc::Board kind = board_kind == 1 ? (board_cash ? sc::Board::SpeedRunsCash : sc::Board::SpeedRuns) : board_kind == 2 ? sc::Board::Competitions : sc::Board::Scores;
        ImGui::Spacing();
        // the second row: the speed runs' two kinds (nothing to choose on the other boards), the level, and at the right the file's folder
        const float row_right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        if (board_kind == 1) {
            static const char* const kSpeedKinds[] = {"All goals", "100%"};
            int cash = board_cash ? 1 : 0;
            if (segmented("##speedkind", cash, kSpeedKinds, 2)) {
                board_cash = cash == 1;
            }
            ImGui::SameLine(0.0f, px(24.0f));
        }
        {
            bool custom_park = false;
            for (const sc::Record& r : board.records) {
                custom_park = custom_park || r.level == sc::kCustomParkLevel;
            }
            const std::size_t levels = custom_park || board_level == sc::kCustomParkLevel ? sc::kLevelNames.size() : sc::kLevelNames.size() - 1;
            static const auto names = [] {   // All, then the levels: the combo's index is board_level + 1
                std::array<const char*, sc::kLevelNames.size() + 1> out{"All"};
                std::copy(sc::kLevelNames.begin(), sc::kLevelNames.end(), out.begin() + 1);
                return out;
            }();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Level");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(px(260.0f));
            int shown = board_level + 1;
            if (combo("##level", shown, names.data(), levels + 1)) {
                board_level = shown - 1;
            }
            // one level shown (from the list, or a row clicked on All): straight back to every level's best, beside the list
            if (board_level != kAllLevels) {
                ImGui::SameLine();
                if (button("Back to All", "Show the best run of every level again.")) {
                    board_level = kAllLevels;
                }
            }
        }
        {   // the scores are Saves/highscores.tsv, a plain text file: its folder, at the right of the row, with what was skipped in it
            const ImGuiStyle& style = ImGui::GetStyle();
            const float open_width = ImGui::CalcTextSize("Open Saves folder").x + style.FramePadding.x * 2.0f;
            const std::string skipped = board.bad_lines > 0 ? std::to_string(board.bad_lines) + " unreadable line" + (board.bad_lines == 1 ? "" : "s") + " skipped" : std::string();
            const float skipped_width = skipped.empty() ? 0.0f : ImGui::CalcTextSize(skipped.c_str()).x + style.ItemSpacing.x;
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), row_right - open_width - skipped_width));
            if (!skipped.empty()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(kNote, "%s", skipped.c_str());
                ImGui::SameLine();
            }
            if (button("Open Saves folder", "The scores are kept in Saves\\highscores.tsv, a plain text file that opens in a spreadsheet or a text editor.")) {
                std::error_code ec;
                fs::create_directories(where.saves(), ec);
                open_in_explorer(where.saves());
            }
        }
        ImGui::Spacing();

        {   // the runs of the session that just ended, record or not, newest first: each with where it stands on its level (the board below shows only the best)
            const std::vector<sc::Record> session = sc::session_runs(board.records, board_before_play);
            if (!session.empty()) {
                ImGui::TextColored(kDim, "This session");
                constexpr std::size_t kSessionRows = 5;
                for (std::size_t k = 0; k < session.size() && k < kSessionRows; ++k) {
                    const sc::Record& r = session[session.size() - 1 - k];
                    const sc::Placing at = sc::placing(board.records, r);
                    const bool speed_run = sc::is_speed_run(r.mode);
                    const bool competition = r.mode == sc::Mode::Competition;
                    const std::string value = competition ? sc::judged_text(r.judged) : speed_run ? sc::run_time(r.time_ms) : sc::thousands(r.score);
                    const char* kind_text = competition ? "Competition" : r.mode == sc::Mode::SpeedRunCash ? "100% speed run" : speed_run ? "Speed run" : "Run";
                    if (at.rank == 1) {
                        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                    }
                    ImGui::Text("%s: %s on %s  -  #%d of %d  -  %s, %s", kind_text, value.c_str(), r.level_name.c_str(), at.rank, at.of, r.initials.c_str(), r.skater_name.c_str());
                    if (at.rank == 1) {
                        ImGui::PopStyleColor();
                    }
                }
                if (session.size() > kSessionRows) {
                    ImGui::TextColored(kDim, "and %d earlier", static_cast<int>(session.size() - kSessionRows));
                }
                ImGui::Spacing();
            }
        }

        ImGui::BeginChild("##board_area", ImVec2(0, 0));
        bool none = true;   // nothing on this board yet (no file, or none of its runs)
        for (const sc::Record& r : board.records) {
            none = none && !sc::on_board(r, kind);
        }
        const bool all = board_level == kAllLevels;
        std::vector<sc::Record> rows;
        if (!none && all) {   // the best run of each level, best first; a row opens its level's table
            rows = sc::best_per_level(board.records, kind);
            std::stable_sort(rows.begin(), rows.end(), [kind](const sc::Record& a, const sc::Record& b) { return sc::better_on(kind, a, b); });
            if (rows.size() > kBoardRows) {
                rows.resize(kBoardRows);
            }
        } else if (!none) {
            rows = sc::best(board.records, board_level, kind, kBoardRows);
        }
        const bool ranked = true;   // (every view is a ranking since Recent went)
        const bool level_column = all;
        const bool competition = kind == sc::Board::Competitions;
        const bool speed = kind == sc::Board::SpeedRuns || kind == sc::Board::SpeedRunsCash;
        if (rows.empty()) {
            const char* empty = kind == sc::Board::Scores ? "No scores yet. Finish a single session or a career level to set one."
                                : speed                   ? "No speed runs yet. Start a Goals Speed Run from the Quick Start page."
                                                          : "No competitions yet. Start a Competition from the Quick Start page.";
            ImGui::TextColored(kDim, "%s", none ? empty : "None on this level yet.");
        } else {
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##board", 5 + (ranked ? 1 : 0) + (level_column ? 1 : 0) + (competition ? 2 : 0), flags)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                if (ranked) {
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, px(28.0f));
                }
                if (level_column) {
                    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                }
                ImGui::TableSetupColumn("Initials", ImGuiTableColumnFlags_WidthFixed, px(64.0f));
                if (competition) {
                    ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, px(64.0f));
                    ImGui::TableSetupColumn("Medal", ImGuiTableColumnFlags_WidthFixed, px(64.0f));
                }
                ImGui::TableSetupColumn(speed ? "Time" : competition ? "Best heat" : "Score", ImGuiTableColumnFlags_WidthFixed, px(90.0f));
                ImGui::TableSetupColumn("Skater", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, px(140.0f));
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(40.0f));
                ImGui::TableHeadersRow();
                int open_level = kAllLevels;
                for (std::size_t k = 0; k < rows.size(); ++k) {
                    const sc::Record& r = rows[k];
                    const bool fresh = r.order >= board_before_play;   // from the session that just ended
                    ImGui::TableNextRow();
                    if (fresh) {
                        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                    }
                    ImGui::TableNextColumn();
                    if (all) {   // the whole row is the level's link
                        ImGui::PushID(static_cast<int>(k));
                        char rank[16];
                        std::snprintf(rank, sizeof rank, "%d", static_cast<int>(k + 1));
                        if (ImGui::Selectable(rank, false, ImGuiSelectableFlags_SpanAllColumns)) {
                            open_level = r.level;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        }
                        tooltip("Show this level's top runs.");
                        ImGui::PopID();
                    } else if (ranked) {
                        ImGui::Text("%d", static_cast<int>(k + 1));
                    }
                    if (level_column) {
                        if (ranked) {
                            ImGui::TableNextColumn();
                        }
                        ImGui::TextUnformatted(r.level_name.c_str());
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.initials.c_str());
                    if (competition) {
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(sc::judged_text(r.judged).c_str());
                        ImGui::TableNextColumn();
                        std::string medal = r.medal.empty() || r.medal == "none" ? std::string("-") : r.medal;
                        medal[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(medal[0])));
                        ImGui::TextUnformatted(medal.c_str());
                    }
                    ImGui::TableNextColumn();
                    const std::string score = speed ? sc::run_time(r.time_ms) : sc::thousands(r.score);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(score.c_str()).x);
                    ImGui::TextUnformatted(score.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.skater_name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.date.c_str());
                    ImGui::TableNextColumn();
                    if (fresh) {
                        ImGui::TextUnformatted("New");
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::EndTable();
                if (open_level != kAllLevels) {
                    board_level = open_level;   // the dropdown follows
                }
            }
        }
        ImGui::EndChild();
    }

    void quick_start_page() {
        section("Level and skater");
        int level = settings.quick_level;
        combo_row("Level", nullptr, level, kLevelNames.data(), kLevelNames.size());
        settings.quick_level = level;
        row("Skater", nullptr);
        if (ImGui::BeginCombo("##skater", kSkaterNames[static_cast<std::size_t>(settings.quick_skater)])) {
            for (std::size_t k = 0; k < kSkaterNames.size(); ++k) {
                if (skater_is_custom(static_cast<int>(k))) {
                    continue;
                }
                if (ImGui::Selectable(kSkaterNames[k], settings.quick_skater == static_cast<int>(k))) {
                    settings.quick_skater = static_cast<int>(k);
                }
            }
            ImGui::EndCombo();
        }
        // the style list follows the skater: only the styles that skater has, and nothing to choose with fewer than two
        const int styles = skater_style_count(settings.quick_skater);
        settings.quick_look = std::min(settings.quick_look, styles - 1);
        if (styles >= 2) {
            static constexpr const char* kLooks[] = {"Style A", "Style B", "Style C", "Style D"};
            combo_row("Style", nullptr, settings.quick_look, kLooks, static_cast<std::size_t>(styles));
        }
        check_row("Max stats", "Every stat at 10.", settings.max_stats);
        trick_set_row();
        section("Start");
        const bool blocked = tables && bindings_dirty() && !conflicts().empty();
        indent_to_control();
        ImGui::BeginDisabled(task.running || blocked || !check.usable());
        const ImVec2 size((control_width() - ImGui::GetStyle().ItemSpacing.x) * 0.5f, ImGui::GetFrameHeight() * 1.6f);
        ImGui::PushStyleColor(ImGuiCol_Button, kGo);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kGoHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kGoActive);
        if (ImGui::Button("Free Skate", size)) {
            start_play(0);
        }
        tooltip("No timer and no goals: skate the level for as long as you like.");
        ImGui::SameLine();
        if (ImGui::Button("Single Session", size)) {
            start_play(1);
        }
        tooltip("A two-minute run for a high score.");
        indent_to_control();
        // the speed run needs a level with goals of its own (core/settings.hpp speed_run_level); a competition takes any level
        const bool goals = speed_run_level(settings.quick_level);
        if (!goals) {
            speed_run_open = false;
        }
        // the Goals Speed Run opens its two ways to start (every goal, or every goal and the cash) under the buttons; the one clicked there starts the run
        ImGui::BeginDisabled(!goals);
        if (speed_run_open) {
            ImGui::PushStyleColor(ImGuiCol_Button, kGoActive);
        }
        if (ImGui::Button("Goals Speed Run", size)) {
            speed_run_open = !speed_run_open;
        }
        if (speed_run_open) {
            ImGui::PopStyleColor();
        }
        ImGui::EndDisabled();
        tooltip(goals ? "A career run with no goals done and no time limit: the clock counts up and stops when you have the goals. Click to choose which: every "
                        "goal, or every goal and the cash. The fastest times go to High Scores."
                      : "This level has no goals of its own to finish (the competitions and the bonus levels): choose Hangar, School II, New York City, "
                        "Venice Beach or Philadelphia.");
        ImGui::SameLine();
        if (ImGui::Button("Competition", size)) {
            start_play(3);
        }
        tooltip("Three one-minute heats on this level, judged as hard as The Bullring, with the level's goals and cash hidden. The judges' total and "
                "your medal go to High Scores (Competitions).");
        if (speed_run_open) {
            indent_to_control();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kDim, "The clock stops at:");
            indent_to_control();
            if (ImGui::Button("All goals", size)) {
                settings.speed_run_cash = false;
                start_play(2);
            }
            tooltip("Every goal but the level's 100% goal. The 100% goal and the cash are not needed. Its own High Scores board (Speed Runs, All goals).");
            ImGui::SameLine();
            if (ImGui::Button("Goals and cash", size)) {
                settings.speed_run_cash = true;
                start_play(2);
            }
            tooltip("100%: every goal and every cash icon. Its own High Scores board (Speed Runs, 100%).");
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        if (blocked) {
            tooltip("Fix the binding conflicts first.");
        }
    }

    // ------------------------------------------------------------ trick sets (core/trick_sets.hpp)

    // The Quick Start page's Tricks row: the set every skater gets, or each skater's own, and the editor
    void trick_set_row() {
        static constexpr const char* kTip = "The tricks on every button combination and the specials, for whichever skater you choose. The game's own Edit Tricks "
                                            "is a career screen a quick start never reaches; PLAY and your career keep each skater's own tricks.";
        static constexpr const char* kOwn = "Each skater's own";
        const TrickCatalog* catalog = tricks_catalog();
        row("Tricks", kTip);
        const TrickSet* chosen = trick_set_named(settings.quick_trick_set);
        const float edit_width = px(120.0f);
        ImGui::BeginDisabled(catalog == nullptr);
        ImGui::SetNextItemWidth(control_width() - edit_width - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginCombo("##tricks", chosen != nullptr ? chosen->name.c_str() : kOwn)) {
            if (ImGui::Selectable(kOwn, chosen == nullptr)) {
                settings.quick_trick_set.clear();
            }
            for (const TrickSet& set : trick_sets) {
                if (ImGui::Selectable(set.name.c_str(), chosen == &set)) {
                    settings.quick_trick_set = set.name;
                }
            }
            ImGui::EndCombo();
        }
        tooltip(kTip);
        ImGui::SameLine();
        if (button("Edit Tricks...", "Make and change trick sets: the trick on each combination, grouped as the game's Edit Tricks groups them, and five specials.",
                   edit_width)) {
            open_trick_editor();
        }
        ImGui::EndDisabled();
        if (catalog == nullptr) {
            indent_to_control();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + control_width());
            ImGui::TextColored(kDim, "%s", "Start the game once first: the trick list is read from its files, which the first start unpacks.");
            ImGui::PopTextWrapPos();
        }
    }

    // Edit Tricks on the chosen set; with none chosen, a new one from the quick start's skater
    void open_trick_editor() {
        trick_start_skater = settings.quick_skater;
        if (trick_set_named(settings.quick_trick_set) == nullptr && tricks_catalog() != nullptr) {
            new_trick_set(trick_start_skater, "My tricks");
        }
        trick_name_error.clear();
        open_quick(QuickTab::Tricks);
    }

    void new_trick_set(int skater, const std::string& base) {
        TrickSet set = trick_catalog->skater_set(skater);
        set.name = unused_trick_set_name(base);
        trick_sets.push_back(set);
        settings.quick_trick_set = set.name;
    }

    static bool contains_nocase(std::string_view text, std::string_view part) {
        const auto same = [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); };
        return part.empty() || std::search(text.begin(), text.end(), part.begin(), part.end(), same) != text.end();
    }

    // A trick list: a search box (Enter takes the first match), None, then the tricks `fits` takes under a heading for each kind in `kinds`' order, A to Z. `taken`
    // marks a trick another slot holds (a special is on one slot only). Returns true when `value` (a trick's database name, "" none) changed.
    bool trick_picker(const char* id, std::string& value, const TrickCatalog& catalog, const std::function<bool(int)>& fits, const std::vector<TrickKind>& kinds,
                      const std::function<bool(int)>& taken = {}) {
        const int current = value.empty() ? -1 : catalog.find_trick(value);
        const std::string preview = value.empty() ? "None" : current >= 0 ? catalog.tricks()[static_cast<std::size_t>(current)].label : value + " (unknown)";
        bool changed = false;
        if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            const bool appearing = ImGui::IsWindowAppearing();
            if (appearing) {
                trick_filter[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool enter = ImGui::InputTextWithHint("##search", "Search", trick_filter, sizeof trick_filter, ImGuiInputTextFlags_EnterReturnsTrue);
            int first = -1;
            if (trick_filter[0] == '\0' && ImGui::Selectable("None", value.empty())) {
                value.clear();
                changed = true;
            }
            for (TrickKind kind : kinds) {
                std::vector<int> list;
                for (std::size_t t = 0; t < catalog.tricks().size(); ++t) {
                    const TrickInfo& info = catalog.tricks()[t];
                    if (info.kind == kind && fits(static_cast<int>(t)) && contains_nocase(info.label, trick_filter)) {
                        list.push_back(static_cast<int>(t));
                    }
                }
                if (list.empty()) {
                    continue;
                }
                std::sort(list.begin(), list.end(), [&](int a, int b) {
                    return catalog.tricks()[static_cast<std::size_t>(a)].label < catalog.tricks()[static_cast<std::size_t>(b)].label;
                });
                ImGui::PushStyleColor(ImGuiCol_Text, kDim);
                ImGui::SeparatorText(kTrickKindNames[static_cast<std::size_t>(kind)]);
                ImGui::PopStyleColor();
                for (int t : list) {
                    const TrickInfo& info = catalog.tricks()[static_cast<std::size_t>(t)];
                    const bool busy = taken && t != current && taken(t);
                    const std::string label = info.label + (busy ? "  (on another special)" : "") + "##" + std::to_string(t);
                    if (!busy && first < 0) {
                        first = t;
                    }
                    if (ImGui::Selectable(label.c_str(), t == current, busy ? ImGuiSelectableFlags_Disabled : 0)) {
                        value = info.name;
                        changed = true;
                    }
                    if (t == current && appearing) {
                        ImGui::SetScrollHereY();
                    }
                }
            }
            if (first < 0 && trick_filter[0] != '\0') {
                ImGui::TextColored(kDim, "No trick matches.");
            }
            if (enter && first >= 0) {
                value = catalog.tricks()[static_cast<std::size_t>(first)].name;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // A special's combination: the 36 of the game's special table, under the button they end in; `taken` marks one another slot holds
    bool special_combo_picker(const char* id, std::string& key, const std::function<bool(int)>& taken) {
        const auto& combos = special_trick_combos();
        const int current = key.empty() ? -1 : find_special_combo(key);
        const std::string preview = key.empty() ? "None" : current >= 0 ? combos[static_cast<std::size_t>(current)].label : key + " (unknown)";
        bool changed = false;
        if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable("None", key.empty())) {
                key.clear();
                changed = true;
            }
            int button = -1;
            for (std::size_t c = 0; c < combos.size(); ++c) {
                if (combos[c].button != button) {
                    button = combos[c].button;
                    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
                    ImGui::SeparatorText(button == 12 ? "Ending in Grind: grind and lip specials" : button == 10 ? "Ending in Flip" : "Ending in Grab");
                    ImGui::PopStyleColor();
                }
                const bool busy = static_cast<int>(c) != current && taken(static_cast<int>(c));
                const std::string label = combos[c].label + (busy ? "  (on another special)" : "") + "##" + std::to_string(c);
                if (ImGui::Selectable(label.c_str(), static_cast<int>(c) == current, busy ? ImGuiSelectableFlags_Disabled : 0)) {
                    key = combos[c].key;
                    changed = true;
                }
                if (static_cast<int>(c) == current && ImGui::IsWindowAppearing()) {
                    ImGui::SetScrollHereY();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void tricks_page() {
        const TrickCatalog* catalog = tricks_catalog();
        if (catalog == nullptr) {
            ImGui::Spacing();
            dim_wrapped(("The trick list is read from the game's own files, which the first start unpacks: start the game once, then come back. (" + trick_catalog_error +
                         ")").c_str());
            return;
        }
        TrickSet* set = trick_set_named(settings.quick_trick_set);
        section("Trick set");
        row("Set", "The set the Quick Start uses. Every set is kept in Config\\TrickSets.ini.");
        if (ImGui::BeginCombo("##set", set != nullptr ? set->name.c_str() : "None")) {
            for (const TrickSet& each : trick_sets) {
                if (ImGui::Selectable(each.name.c_str(), set == &each)) {
                    settings.quick_trick_set = each.name;
                }
            }
            ImGui::EndCombo();
        }
        set = trick_set_named(settings.quick_trick_set);
        indent_to_control();
        const float small = (control_width() - ImGui::GetStyle().ItemSpacing.x * 3.0f) * 0.25f;
        if (button("New", "A new set with the \"Start from\" skater's own tricks.", small)) {
            new_trick_set(trick_start_skater, "My tricks");
            set = trick_set_named(settings.quick_trick_set);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(set == nullptr);
        if (button("Copy", "A new set with this one's tricks.", small)) {
            TrickSet copy = *set;
            copy.name = unused_trick_set_name(set->name + " copy");
            trick_sets.push_back(copy);
            settings.quick_trick_set = copy.name;
            set = trick_set_named(settings.quick_trick_set);
        }
        ImGui::SameLine();
        if (button("Rename...", nullptr, small)) {
            std::snprintf(trick_name_edit, sizeof trick_name_edit, "%s", set->name.c_str());
            trick_name_error.clear();
            ImGui::OpenPopup("Rename trick set");
        }
        ImGui::SameLine();
        if (button("Delete...", nullptr, small)) {
            ImGui::OpenPopup("Delete trick set?");
        }
        ImGui::EndDisabled();
        rename_trick_set_popup(set);
        delete_trick_set_popup(set);
        set = trick_set_named(settings.quick_trick_set);
        if (set == nullptr) {
            indent_to_control();
            ImGui::TextColored(kDim, "No set: New makes one.");
            return;
        }
        row("Start from", "Put a skater's own tricks in this set, as a new career starts them: their combinations and their specials.");
        const float use_width = px(150.0f);
        ImGui::SetNextItemWidth(control_width() - use_width - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginCombo("##start", kSkaterNames[static_cast<std::size_t>(trick_start_skater)])) {
            for (std::size_t k = 0; k < kSkaterNames.size(); ++k) {
                if (!skater_is_custom(static_cast<int>(k)) && ImGui::Selectable(kSkaterNames[k], trick_start_skater == static_cast<int>(k))) {
                    trick_start_skater = static_cast<int>(k);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (button("Use their tricks...", "Every combination and special of this set becomes the skater's own. You are asked first.", use_width)) {
            ImGui::OpenPopup("Use their tricks?");
        }
        if (ImGui::BeginPopupModal("Use their tricks?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("Replace every trick of \"%s\" with %s's own?", set->name.c_str(), kSkaterNames[static_cast<std::size_t>(trick_start_skater)]);
            ImGui::TextColored(kDim, "This cannot be undone.");
            const float width = px(140.0f);
            if (ImGui::Button("Replace", ImVec2(width, 0))) {
                TrickSet own = catalog->skater_set(trick_start_skater);
                own.name = set->name;
                *set = own;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(width, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Spacing();
        ImGui::Spacing();
        sub_tabs("##trickpages", [&] {
            for (std::size_t page = 0; page < kTrickPageCount; ++page) {
                const ImGuiTabItemFlags flags = trick_page_request == static_cast<int>(page) ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(kTrickPageNames[page], nullptr, flags)) {
                    ImGui::Spacing();
                    if (static_cast<TrickPage>(page) == TrickPage::Special) {
                        specials_tab(*catalog, *set);
                    } else {
                        combos_tab(*catalog, *set, static_cast<TrickPage>(page));
                    }
                    ImGui::EndTabItem();
                }
            }
            trick_page_request = -1;
        });
    }

    void combos_tab(const TrickCatalog& catalog, TrickSet& set, TrickPage page) {
        const char* note = page == TrickPage::Flip   ? "Direction and Flip. The double taps (\xE2\x86\x91, \xE2\x86\x91) are pressed quickly. Grab tricks fit here too."
                           : page == TrickPage::Grab ? "Direction and Grab. The double taps (\xE2\x86\x91, \xE2\x86\x91) are pressed quickly. Flip tricks fit here too."
                           : page == TrickPage::Lip  ? "On the lip of a ramp: direction and Grind."
                                                     : "Double taps and Grind on a rail or ledge. The game's own Edit Tricks has no page for these two.";
        ImGui::TextColored(kDim, "%s", note);
        ImGui::Spacing();
        const std::vector<TrickKind> kinds = page == TrickPage::Flip   ? std::vector<TrickKind>{TrickKind::Flip, TrickKind::Grab}
                                             : page == TrickPage::Grab ? std::vector<TrickKind>{TrickKind::Grab, TrickKind::Flip}
                                             : page == TrickPage::Lip  ? std::vector<TrickKind>{TrickKind::Lip}
                                                                       : std::vector<TrickKind>{TrickKind::Grind};
        if (ImGui::BeginTable("##combos", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed, label_width());
            ImGui::TableSetupColumn("Trick", ImGuiTableColumnFlags_WidthFixed, control_width());
            for (std::size_t k = 0; k < kTrickCombos; ++k) {
                const TrickCombo& combo = trick_combos()[k];
                if (combo.page != page) {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(combo.label.c_str());
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(k));
                ImGui::SetNextItemWidth(-FLT_MIN);
                trick_picker("##trick", set.combos[k], catalog, [&](int t) { return catalog.fits(static_cast<int>(k), t); }, kinds);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void specials_tab(const TrickCatalog& catalog, TrickSet& set) {
        int used = 0;
        for (const TrickSet::Special& special : set.specials) {
            used += !special.combo.empty() && !special.trick.empty() ? 1 : 0;
        }
        ImGui::TextColored(kDim, "%d of %d specials. A special goes off with the special meter full; one ending in Grind takes the grind and lip specials, "
                                 "one ending in Flip or Grab the others.", used, static_cast<int>(kSpecialSlots));
        ImGui::Spacing();
        const float combo_width = px(240.0f);
        if (ImGui::BeginTable("##specials", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, px(24.0f));
            ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed, combo_width);
            ImGui::TableSetupColumn("Trick", ImGuiTableColumnFlags_WidthFixed, control_width());
            ImGui::TableSetupColumn("Clear", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            for (std::size_t slot = 0; slot < kSpecialSlots; ++slot) {
                TrickSet::Special& special = set.specials[slot];
                ImGui::PushID(static_cast<int>(slot));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(kDim, "%d", static_cast<int>(slot) + 1);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                const auto combo_taken = [&](int c) {
                    for (std::size_t other = 0; other < kSpecialSlots; ++other) {
                        if (other != slot && find_special_combo(set.specials[other].combo) == c) {
                            return true;
                        }
                    }
                    return false;
                };
                if (special_combo_picker("##combo", special.combo, combo_taken)) {
                    const int trick = catalog.find_trick(special.trick);
                    if (special.combo.empty() || (trick >= 0 && !catalog.fits_special(find_special_combo(special.combo), trick))) {
                        special.trick.clear();   // the trick does not go on the new combination's button
                    }
                }
                ImGui::TableNextColumn();
                const int combo = find_special_combo(special.combo);
                const bool grind = combo >= 0 && special_trick_combos()[static_cast<std::size_t>(combo)].button == 12;
                const auto trick_taken = [&](int t) {
                    for (std::size_t other = 0; other < kSpecialSlots; ++other) {
                        if (other != slot && catalog.find_trick(set.specials[other].trick) == t) {
                            return true;
                        }
                    }
                    return false;
                };
                ImGui::BeginDisabled(combo < 0);
                ImGui::SetNextItemWidth(-FLT_MIN);
                trick_picker("##trick", special.trick, catalog, [&](int t) { return catalog.fits_special(combo, t); },
                             grind ? std::vector<TrickKind>{TrickKind::Grind, TrickKind::Lip}
                                   : std::vector<TrickKind>{TrickKind::Flip, TrickKind::Grab, TrickKind::Manual},
                             trick_taken);
                ImGui::EndDisabled();
                if (combo < 0) {
                    tooltip("Choose the buttons first.");
                }
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(special.combo.empty() && special.trick.empty());
                if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), 0))) {
                    special = {};
                }
                tooltip("Empty this special.");
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void rename_trick_set_popup(TrickSet* set) {
        if (!ImGui::BeginPopupModal("Rename trick set", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(control_width());
        const bool enter = ImGui::InputText("##name", trick_name_edit, sizeof trick_name_edit, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (!trick_name_error.empty()) {
            ImGui::TextColored(kBad, "%s", trick_name_error.c_str());
        }
        const float width = (control_width() - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Rename", ImVec2(width, 0)) || enter) {
            const std::string name = trim(trick_name_edit);
            const TrickSet* same = trick_set_named(name);
            if (!trick_set_name_valid(name)) {
                trick_name_error = "1 to 40 characters, no [ or ], not starting with ; or #.";
            } else if (same != nullptr && same != set) {
                trick_name_error = "Another set has that name.";
            } else {
                if (set != nullptr) {
                    set->name = name;
                }
                settings.quick_trick_set = name;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(width, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void delete_trick_set_popup(const TrickSet* set) {
        if (!ImGui::BeginPopupModal("Delete trick set?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }
        ImGui::Text("Delete \"%s\"?", set != nullptr ? set->name.c_str() : "");
        ImGui::TextColored(kDim, "This cannot be undone.");
        const float width = px(140.0f);
        if (ImGui::Button("Delete", ImVec2(width, 0))) {
            const auto at = std::find_if(trick_sets.begin(), trick_sets.end(), [&](const TrickSet& each) { return &each == set; });
            if (at != trick_sets.end()) {
                const std::size_t index = static_cast<std::size_t>(at - trick_sets.begin());
                trick_sets.erase(at);
                settings.quick_trick_set = trick_sets.empty() ? std::string() : trick_sets[std::min(index, trick_sets.size() - 1)].name;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(width, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // What "Reset all to defaults" makes of the settings: every option of the six tabs at its default, as a new settings file has it. Kept, because they are not options of
    // those tabs: where the game's files are, the Quick Start page's choices (level, skater, trick set, speed run), and the hand-set HUD margin, which no page shows.
    Settings settings_reset_to_defaults() {
        Settings fresh = default_settings();
        fresh.game_dir = settings.game_dir;
        fresh.hud_margin = settings.hud_margin;
        fresh.quick_mode = settings.quick_mode;
        fresh.quick_level = settings.quick_level;
        fresh.quick_skater = settings.quick_skater;
        fresh.quick_look = settings.quick_look;
        fresh.max_stats = settings.max_stats;
        fresh.quick_trick_set = settings.quick_trick_set;
        fresh.speed_run_cash = settings.speed_run_cash;
        fresh.check_updates = settings.check_updates;     // a choice about the network is not something a reset should undo
        fresh.skipped_update = settings.skipped_update;
        // as a new file: the defaults are Enhanced (smooth textures, 16x anisotropy), which uses the upscaled textures once they have been made
        fresh.upscaled_textures = upscale_setup::set_exists(upscale_state());
        return fresh;
    }

    bool reset_dialog_request = false;   // --page reset

    bool bindings_at_defaults() const { return !tables || (bindings.pad == tables->default_pad && bindings.keys == tables->default_keys); }

    bool at_defaults() {
        return settings_text(settings) == settings_text(settings_reset_to_defaults()) && bindings_at_defaults();
    }

    // Written at once, as every change is (autosave)
    void reset_all_to_defaults() {
        settings = settings_reset_to_defaults();
        if (tables) {
            bindings.pad = tables->default_pad;
            bindings.keys = tables->default_keys;
        }
        refresh_display_modes();   // the resolution is Auto again
        set_status("", false);
    }

    void reset_all_popup() {
        if (!ImGui::BeginPopupModal("Reset all settings?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }
        ImGui::PushTextWrapPos(px(460.0f));
        ImGui::TextWrapped("Every option on the Display, Graphics, Audio, Controls, Gameplay and System pages goes back to its default, including the controller, keyboard "
                           "and hotkey bindings.");
        ImGui::Spacing();
        ImGui::TextColored(kDim, "Kept: the game's location, Quick Start choices and trick sets, the update check, saves and high scores.");
        ImGui::TextColored(kDim, "This is saved at once and cannot be undone.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const float width = px(140.0f);
        if (ImGui::Button("Reset all", ImVec2(width, 0))) {
            reset_all_to_defaults();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(width, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void system_tab() {
        section("Game files");
        {   // the folder, cut in the middle when it does not fit, with its buttons at the right of the row; the whole path is the row's help
            const std::string full = utf8_of(game_dir);
            const ImGuiStyle& style = ImGui::GetStyle();
            const float buttons = ImGui::CalcTextSize("Change...").x + ImGui::CalcTextSize("Open folder").x + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
            const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            row("Location", full.c_str());
            const float room = right - buttons - style.ItemSpacing.x - ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(shortened_middle(full, room).c_str());
            tooltip(full.c_str());
            ImGui::SameLine();
            ImGui::SetCursorPosX(right - buttons);
            ImGui::BeginDisabled(task.running);
            if (button("Change...", "Choose another installed game, disc or disc image.")) {
                screen = Screen::Setup;
            }
            ImGui::SameLine();
            if (button("Open folder##game", "Opens the game's folder in Explorer.")) {
                open_in_explorer(game_dir);
            }
            ImGui::EndDisabled();
        }
        row("Status", nullptr);
        const std::string state = !check.usable() ? check.summary() : check.prepared ? std::string("Ready") : std::string("Found, unpacked on first play");
        ImGui::TextColored(check.usable() ? kGood : kBad, "%s", state.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%s", check.music ? (check.movies ? "  Music and movies found" : "  Music found, no movies") : check.movies ? "  Movies found, no music" : "  No music or movies");
        tooltip("The game plays without its music and movies, if they are missing.");

        section("Appearance");
        static const char* const kThemes[] = {"Dark", "Light", "Follow system"};
        int theme = static_cast<int>(settings.theme);
        if (combo_row("Theme", "The launcher's colours: dark, light, or the same as Windows' light or dark mode. It does not affect the game.", theme, kThemes, 3)) {
            settings.theme = static_cast<Theme>(theme);
        }

        section("Diagnostics");
        check_row("Debug logging", "Keeps the logs of every run, and dumps the textures the game draws to Data\\TextureDump. Off keeps only the latest runs.",
                  settings.debug_mode);
        row("Logs", "Opens the folder with the logs of the latest runs (every run with Debug logging on).");
        if (button("Open logs")) {
            std::error_code ec;
            fs::create_directories(where.logs(), ec);
            open_in_explorer(where.logs());
        }
        row("Problems", "Opens the bug report form on GitHub in your browser. After a crash, the launcher has its own Report on GitHub button that zips the logs for you. "
                        "For anything else, turn on Debug logging, reproduce the problem, then zip the newest folder in Logs\\runs and attach it.");
        if (button("Report on GitHub", "Opens the bug report form in your browser.")) {
            SDL_OpenURL(kIssueUrl);
        }
        if (!last_run.empty()) {
            const std::vector<std::string> fields = session_fields(last_run);
            row("Last session", nullptr);
            std::string text = fields[0];
            if (fields.size() >= 3) {
                text += "   " + fields[1] + "   " + fields[2];
            }
            ImGui::TextColored(kDim, "%s", text.c_str());
            tooltip(last_run.c_str());
        }

        section("Updates");
        if (check_row("Check for updates",
                      "When the launcher opens, it asks GitHub for the newest release: one request to github.com that shows your IP address and nothing else. "
                      "A newer release shows a notice with a link to its page. Nothing is downloaded or installed for you.",
                      settings.check_updates) &&
            settings.check_updates) {
            start_update_check();
        }
        row("Newest release", "Looks at GitHub now, whether or not the automatic check is on.");
        const bool checking = update && update->state() == UpdateCheck::State::Checking;
        ImGui::BeginDisabled(checking);
        if (button("Check now")) {
            start_update_check();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        update_status();
        if (update && update->state() == UpdateCheck::State::Newer) {
            ImGui::SameLine();
            if (button("Download##update", "Opens the release page on GitHub in your browser.")) {
                SDL_OpenURL(kLatestReleaseUrl);
            }
        }

        section("Reset");
        row("All settings", nullptr);
        const bool defaults = at_defaults();
        ImGui::BeginDisabled(defaults);
        if (button("Reset all to defaults...", "Puts every option and binding back to its default. You are asked first.") ||
            reset_dialog_request) {
            reset_dialog_request = false;
            ImGui::OpenPopup("Reset all settings?");
        }
        ImGui::EndDisabled();
        if (defaults) {
            tooltip("Everything is already at its default.");
        }
        reset_all_popup();

        section("About");
        ImGui::Text("THPSource2 %s", kVersion);
        dim_wrapped("A way to play Tony Hawk's Pro Skater 2 on a modern PC: a decompilation, the game's code rebuilt from the original by reverse engineering, so it "
                    "can be fixed and improved while still playing like the real thing. You bring your own copy of the game; none of its files come with this. THPSource2 "
                    "is a fan project, not affiliated with or endorsed by Activision or Neversoft.");
        dim_wrapped("Based on OpenTony by Joao Matos (tritao). Thanks to PARTYMANX's PARTYMOD for the settings that inspired many of these, and to Dear ImGui and SDL3 "
                    "for the launcher itself. THPSource2 is MIT-licensed; the licences and the small print are in the Licenses folder.");
        ImGui::TextColored(kDim, "Project page:");
        ImGui::SameLine();
        ImGui::TextColored(kAccent, "%s", kRepoUrl);
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const ImVec2 low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(low.x, high.y), high, ImGui::GetColorU32(kAccent), 1.0f);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                SDL_OpenURL(kRepoUrl);
            }
        }
        tooltip("Opens the project's page on GitHub in your browser.");
    }

    // The footer's Presets: Enhanced, Original PC, Original PlayStation and the person's own, in one list. Picking one sets its options (core/settings.hpp, apply_preset:
    // the look and feel, not the window, sound, controls or unlocks), and like every change it is saved at once. The list shows the preset the options match, or
    // "Custom (not saved)". Save as... names the options as a preset of the person's own (or replaces one of that name); Delete removes the own preset shown.
    void presets_bar() {
        static constexpr const char* kTip = "A preset sets the look and feel at once: the picture, HUD and frame rate (Display), every Graphics option, and the camera "
                                            "stick, autokick, vibration, starting camera and intro movies. The window, V-Sync, sound, controls and unlocks are yours and stay.";
        static constexpr const char* kBuiltInTips[kPresetCount] = {
            "Everything at its most: the window's own shape with a wide view and the HUD at the edges, 60 fps, the song title, 4x supersampling, 8x anti-aliasing, 16x "
            "texture detail, no fog, the most object detail, high ambient occlusion, true-colour (and upscaled, once made) PC textures, the camera stick.",
            "The PC game as it ran: 640 x 480 at 4:3 and 30 fps, the original view and HUD, the PC's textures, colour and fog, no camera stick.",
            "The PlayStation on a TV: 512 x 240 at 4:3 and 30 fps, its own unfiltered textures, colour, fog and dither through the CRT with a curved screen, no camera stick."};
        const bool upscaled = upscale_setup::set_exists(upscale_state());
        const int built_in = preset_of(settings, upscaled);
        int own = -1;
        for (std::size_t k = 0; k < presets.size() && own < 0; ++k) {
            if (same_preset_options(presets[k].values, settings)) {
                own = static_cast<int>(k);
            }
        }
        const std::string shown = own >= 0 ? presets[static_cast<std::size_t>(own)].name : built_in >= 0 ? kPresetNames[static_cast<std::size_t>(built_in)] : "Custom (not saved)";
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Presets");
        tooltip(kTip);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(200.0f));
        if (ImGui::BeginCombo("##presets", shown.c_str())) {
            for (int k = 0; k < kPresetCount; ++k) {
                if (ImGui::Selectable(kPresetNames[static_cast<std::size_t>(k)], own < 0 && built_in == k)) {
                    apply_preset(settings, k, upscaled);
                }
                tooltip(kBuiltInTips[k]);
            }
            if (!presets.empty()) {
                ImGui::Separator();
            }
            for (std::size_t k = 0; k < presets.size(); ++k) {
                ImGui::PushID(static_cast<int>(k));
                if (ImGui::Selectable(presets[k].name.c_str(), own == static_cast<int>(k))) {
                    copy_preset_options(presets[k].values, settings);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        tooltip(kTip);
        ImGui::SameLine();
        if (button("Save as...", "Keeps the options a preset sets as a preset of your own, under a name you choose.")) {
            std::snprintf(preset_name_edit, sizeof preset_name_edit, "%s", own >= 0 ? presets[static_cast<std::size_t>(own)].name.c_str() : "");
            preset_name_error.clear();
            ImGui::OpenPopup("Save preset");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(own < 0);
        if (button("Delete...", own >= 0 ? "Deletes this preset of your own. The options stay as they are." : "Only a preset of your own can be deleted: pick one first.")) {
            preset_delete_name = presets[static_cast<std::size_t>(own)].name;
            ImGui::OpenPopup("Delete preset?");
        }
        ImGui::EndDisabled();
        save_preset_popup();
        delete_preset_popup();
    }

    int preset_named(const std::string& name) const {
        for (std::size_t k = 0; k < presets.size(); ++k) {
            if (equal_nocase(presets[k].name, name)) {
                return static_cast<int>(k);
            }
        }
        return -1;
    }

    void save_preset_popup() {
        if (!ImGui::BeginPopupModal("Save preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }
        ImGui::PushTextWrapPos(px(420.0f));
        ImGui::TextWrapped("Keeps the Display, Graphics and Gameplay options a preset sets. The window, V-Sync, sound, controls and unlocks are not part of it.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(px(420.0f));
        const bool enter = ImGui::InputTextWithHint("##preset_name", "Name", preset_name_edit, sizeof preset_name_edit,
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const std::string name = trim(preset_name_edit);
        const int same = preset_named(name);
        if (!preset_name_error.empty()) {
            ImGui::TextColored(kBad, "%s", preset_name_error.c_str());
        } else if (same >= 0) {
            ImGui::TextColored(kNote, "Replaces your preset \"%s\".", presets[static_cast<std::size_t>(same)].name.c_str());
        } else {
            ImGui::TextUnformatted("");
        }
        const float width = (px(420.0f) - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Save", ImVec2(width, 0)) || enter) {
            if (!preset_name_valid(name)) {
                preset_name_error = "1 to 32 characters, no [ or ], and not Enhanced, Original PC or Original PlayStation.";
            } else {
                const CustomPreset preset{name, settings};
                if (same >= 0) {
                    presets[static_cast<std::size_t>(same)] = preset;
                } else {
                    presets.push_back(preset);
                }
                if (write_presets_file()) {
                    set_status("", false);
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(width, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void delete_preset_popup() {
        if (!ImGui::BeginPopupModal("Delete preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }
        ImGui::Text("Delete your preset \"%s\"?", preset_delete_name.c_str());
        ImGui::TextColored(kDim, "The options stay as they are. This cannot be undone.");
        const float width = px(140.0f);
        if (ImGui::Button("Delete", ImVec2(width, 0))) {
            const int at = preset_named(preset_delete_name);
            if (at >= 0) {
                presets.erase(presets.begin() + at);
                write_presets_file();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(width, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void footer_bar() {
        ImGui::Separator();
        ImGui::Spacing();
        const bool blocked = tables && bindings_dirty() && !conflicts().empty();
        const float play_height = ImGui::GetFrameHeight() * 1.6f;
        const float top = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(top + (play_height - ImGui::GetFrameHeight()) * 0.5f);
        if (view == View::Settings) {   // the Presets (and Save as, Delete) are the settings': not on the High Scores or Quick Start pages
            ImGui::BeginDisabled(task.running);
            presets_bar();
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        ImGui::AlignTextToFramePadding();
        if (!status.empty()) {
            ImGui::TextColored(status_bad ? kBad : kGood, "%s", status.c_str());
            tooltip(status.c_str());
            ImGui::SameLine();
        }
        // PLAY and Quick Start take the right of the line, narrower when the window is (the Presets keep their place at the left)
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float left = ImGui::GetCursorPosX();
        const float right = left + ImGui::GetContentRegionAvail().x;
        const float play_width = std::clamp((right - left - spacing) * 0.5f, px(150.0f), px(220.0f));
        const float x = std::max(left, right - play_width * 2.0f - spacing);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
        // Quick Start, PLAY's size, in green at its left: it opens its page (on the Session tab), and again goes back to the settings
        ImGui::SetCursorPos(ImVec2(x, top));
        // on the Quick Start page the same place says where it goes: back, in the neutral colour of a button that does not start the game
        const bool on_page = view == View::QuickStart;
        if (!on_page) {
            ImGui::PushStyleColor(ImGuiCol_Button, kGo);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kGoHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kGoActive);
        }
        if (ImGui::Button(on_page ? "Back to settings" : "Quick Start", ImVec2(play_width, play_height))) {
            if (on_page) {
                view = View::Settings;
            } else {
                open_quick(QuickTab::Session);
            }
        }
        if (!on_page) {
            ImGui::PopStyleColor(3);
        }
        tooltip(on_page ? "Esc also goes back." : "Straight into a level as any skater, without touching your save.");
        ImGui::SetCursorPos(ImVec2(x + play_width + spacing, top));   // placed, not SameLine's: the status text may have moved the line
        ImGui::BeginDisabled(task.running || blocked || !check.usable());
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
        if (ImGui::Button(task.kind == Task::Kind::Play && task.running ? "RUNNING" : "PLAY", ImVec2(play_width, play_height))) {
            start_play();
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        ImGui::PopFont();
        if (blocked) {
            tooltip("Fix the binding conflicts first.");
        }
    }

    void task_popup() {
        const bool show = task.kind == Task::Kind::Import || task.kind == Task::Kind::Prepare || task.kind == Task::Kind::Upscale;
        const char* title = task.kind == Task::Kind::Import ? "Importing###task" : task.kind == Task::Kind::Upscale ? "Upscaling Textures###task" : "Preparing###task";
        if (show && !ImGui::IsPopupOpen("###task")) {
            ImGui::OpenPopup(title);
        }
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.8f, ImGui::GetMainViewport()->WorkSize.y * 0.6f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            if (task.running) {
                const std::uint64_t total = task.progress.total;
                if (task.kind == Task::Kind::Import && total != 0 && task.progress.done < total) {
                    const float fraction = static_cast<float>(static_cast<double>(task.progress.done) / static_cast<double>(total));
                    char overlay[64];
                    std::snprintf(overlay, sizeof overlay, "%.0f / %.0f MB", static_cast<double>(task.progress.done) / 1048576.0, static_cast<double>(total) / 1048576.0);
                    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), overlay);
                } else if (task.kind == Task::Kind::Upscale && task.upscale.total != 0) {
                    const std::uint64_t done = task.upscale.done, all = task.upscale.total;
                    std::string overlay;
                    if (task.upscale.phase == 1) {
                        char mb[64];
                        std::snprintf(mb, sizeof mb, "Downloading: %.0f of %.0f MB", static_cast<double>(done) / 1048576.0, static_cast<double>(all) / 1048576.0);
                        overlay = mb;
                    } else if (task.upscale.phase == 2) {
                        overlay = "Scanning the game for textures: step " + std::to_string(done) + " of " + std::to_string(all);
                    } else {
                        overlay = "Upscaling textures: " + opentony::scores::thousands(static_cast<std::int64_t>(done)) + " of " +
                                  opentony::scores::thousands(static_cast<std::int64_t>(all));
                    }
                    ImGui::ProgressBar(static_cast<float>(std::min(1.0, static_cast<double>(done) / static_cast<double>(all))), ImVec2(-FLT_MIN, 0), overlay.c_str());
                } else {
                    ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-FLT_MIN, 0), "Working...");
                }
            } else if (task.ok) {
                ImGui::TextColored(kGood, "Done");
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, kBad);
                ImGui::TextWrapped("%s", task.error.empty() ? "Failed" : task.error.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::BeginChild("##log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.3f), ImGuiChildFlags_Borders);
            for (const std::string& line : task.lines()) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            if (task.running) {
                ImGui::BeginDisabled(task.kind != Task::Kind::Import && task.kind != Task::Kind::Upscale);
                if (button("Cancel", nullptr, px(120.0f))) {
                    task.progress.cancel = true;
                    task.upscale.cancel = true;
                }
                ImGui::EndDisabled();
            } else if (button("Close", nullptr, px(120.0f))) {
                task.kind = Task::Kind::None;
                ImGui::CloseCurrentPopup();
            }
            if (!show) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void capture_popup() {
        if (capture.active && !ImGui::IsPopupOpen("Bind")) {
            ImGui::OpenPopup("Bind");
        }
        if (ImGui::BeginPopupModal("Bind", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            if (capture.hotkey >= 0 && capture.pad) {
                ImGui::Text("Press the buttons for %s together, then release", kHotkeys[static_cast<std::size_t>(capture.hotkey)].label);
                ImGui::TextColored(kDim, "Esc to cancel");
            } else if (capture.hotkey >= 0) {
                ImGui::Text("Press a key for %s", kHotkeys[static_cast<std::size_t>(capture.hotkey)].label);
                ImGui::TextColored(kDim, "Hold Ctrl, Alt or Shift for a combination. Esc to cancel.");
            } else if (capture.pad) {
                ImGui::Text("Press a button for %s", kActions[capture.action].label);
                ImGui::TextColored(kDim, "Esc to cancel");
            } else {
                ImGui::Text("Press a key for %s", kActions[capture.action].label);
                ImGui::TextColored(kDim, "Any key can be bound, Esc too. Click Cancel to stop.");
            }
            if (!capture.message.empty()) {
                ImGui::TextColored(kBad, "%s", capture.message.c_str());
            }
            ImGui::Spacing();
            if (button("Cancel", nullptr, px(120.0f))) {
                capture.active = false;
            }
            if (!capture.active) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
};


void style_ui(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 7);
    style.WindowPadding = ImVec2(18, 14);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(0, 6);
    style.TabBarBorderSize = 2.0f;
    style.FrameBorderSize = 1.0f;   // a field, a box and a button have an edge in both themes (an unticked box on the light page would otherwise vanish)
    style.ScaleAllSizes(scale);
    apply_theme(false);
}

// The colours of the dark theme (the default) or the light one; the sizes stay as style_ui set them
void apply_theme(bool light) {
    if (light) {
        ImGui::StyleColorsLight();
    } else {
        ImGui::StyleColorsDark();
    }
    ImVec4* colors = ImGui::GetStyle().Colors;
    const ImVec4 frame = light ? ImVec4(0.89f, 0.90f, 0.92f, 1.0f) : ImVec4(0.16f, 0.17f, 0.19f, 1.0f);
    const ImVec4 frame_hover = light ? ImVec4(0.83f, 0.85f, 0.89f, 1.0f) : ImVec4(0.22f, 0.23f, 0.26f, 1.0f);
    const ImVec4 frame_active = light ? ImVec4(0.77f, 0.80f, 0.86f, 1.0f) : ImVec4(0.27f, 0.28f, 0.32f, 1.0f);
    const ImVec4 line = light ? ImVec4(0.76f, 0.78f, 0.81f, 1.0f) : ImVec4(0.22f, 0.23f, 0.25f, 1.0f);
    const ImVec4 edge = light ? ImVec4(0.58f, 0.61f, 0.67f, 1.0f) : ImVec4(0.30f, 0.32f, 0.36f, 1.0f);   // the border of a field and a button: 3:1 against the page
    const ImVec4 tab = light ? ImVec4(0.86f, 0.87f, 0.90f, 1.0f) : ImVec4(0.12f, 0.125f, 0.14f, 1.0f);
    const ImVec4 tab_selected = light ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.20f, 0.21f, 0.23f, 1.0f);
    kGood = light ? ImVec4(0.10f, 0.50f, 0.16f, 1.0f) : ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
    kBad = light ? ImVec4(0.78f, 0.16f, 0.12f, 1.0f) : ImVec4(1.0f, 0.45f, 0.40f, 1.0f);
    kNote = light ? ImVec4(0.62f, 0.38f, 0.02f, 1.0f) : ImVec4(0.95f, 0.78f, 0.35f, 1.0f);
    kDim = light ? ImVec4(0.36f, 0.38f, 0.43f, 1.0f) : ImVec4(0.62f, 0.64f, 0.68f, 1.0f);
    colors[ImGuiCol_Text] = light ? ImVec4(0.09f, 0.10f, 0.12f, 1.0f) : colors[ImGuiCol_Text];
    colors[ImGuiCol_WindowBg] = light ? ImVec4(0.95f, 0.955f, 0.965f, 1.0f) : ImVec4(0.08f, 0.085f, 0.095f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = light ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.12f, 0.125f, 0.14f, 1.0f);
    colors[ImGuiCol_Border] = edge;
    colors[ImGuiCol_FrameBg] = frame;
    colors[ImGuiCol_FrameBgHovered] = frame_hover;
    colors[ImGuiCol_FrameBgActive] = frame_active;
    colors[ImGuiCol_Button] = frame;
    colors[ImGuiCol_ButtonHovered] = frame_hover;
    colors[ImGuiCol_ButtonActive] = frame_active;
    colors[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.50f);
    colors[ImGuiCol_HeaderActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.65f);
    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_CheckboxSelectedBg] = frame;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentHover;
    colors[ImGuiCol_Tab] = tab;
    colors[ImGuiCol_TabHovered] = frame_hover;
    colors[ImGuiCol_TabSelected] = tab_selected;
    colors[ImGuiCol_TabSelectedOverline] = kAccent;
    colors[ImGuiCol_TabDimmed] = tab;
    colors[ImGuiCol_TabDimmedSelected] = tab_selected;
    colors[ImGuiCol_TabDimmedSelectedOverline] = kAccent;
    colors[ImGuiCol_Separator] = line;
    colors[ImGuiCol_TableRowBgAlt] = light ? ImVec4(0.0f, 0.0f, 0.0f, 0.035f) : ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
    colors[ImGuiCol_TableHeaderBg] = light ? ImVec4(0.88f, 0.89f, 0.92f, 1.0f) : ImVec4(0.14f, 0.145f, 0.16f, 1.0f);
    colors[ImGuiCol_NavCursor] = kAccent;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, light ? 0.35f : 0.6f);
}

// The header's picture: resource 2 (app/header.png, scripts/make_launcher_icon.py) as a texture, or none
SDL_Texture* load_logo(SDL_Renderer* renderer) {
    png::Image image;
    std::string error;
    if (!png::decode(resource_bytes(2), image, error)) {
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
    if (texture != nullptr) {
        SDL_UpdateTexture(texture, nullptr, image.rgba.data(), image.width * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    }
    return texture;
}

}  // namespace

int main(int argc, char** argv) {
    // --page NAME opens a tab (display, graphics, audio, controls, gameplay, system; gamepad, keyboard and hotkeys open that page of Controls), a header button's
    // page (quickstart, scores), the Graphics tab with the upscaler's window (upscale) or the setup screen; --screenshot FILE.bmp draws a few frames, saves the
    // window's picture and quits (how the UI is checked without clicking through it)
    // --play starts the game at once with the saved settings (the launcher comes back when it ends)
    std::string page, screenshot;
    int size_w = 0, size_h = 0;               // --size W,H: the window's size (before the display's scale) for a --screenshot
    float hover_x = -1.0f, hover_y = -1.0f;   // --hover X,Y: the pointer is put there (window pixels) for a --screenshot, so a tooltip can be checked
    bool play = false;
    bool no_update_check = false;   // --no-update-check: no web request at start (a --screenshot makes none either)
    for (int k = 1; k < argc; ++k) {
        const std::string argument = argv[k];
        if (argument == "--page" && k + 1 < argc) {
            page = argv[++k];
        } else if (argument == "--size" && k + 1 < argc) {
            std::sscanf(argv[++k], "%d,%d", &size_w, &size_h);
        } else if (argument == "--hover" && k + 1 < argc) {
            std::sscanf(argv[++k], "%f,%f", &hover_x, &hover_y);
        } else if (argument == "--screenshot" && k + 1 < argc) {
            screenshot = argv[++k];
        } else if (argument == "--play") {
            play = true;
        } else if (argument == "--no-update-check") {
            no_update_check = true;
        }
    }
    // the install folder is the launcher's own; SDL3.dll is in Build\ (delay-loaded: nothing of SDL runs before this)
    const opentony::layout::Layout install = opentony::layout::from_program_directory(executable_directory());
    add_dll_directory(install.build());
    // --upscale-textures: the texture upscaler from the command line (no window; docs/LAUNCHER.md, "Upscaled textures")
    if (int upscale_exit = 0; upscale_command(install, argc, argv, upscale_exit)) {
        return upscale_exit;
    }
    SDL_SetMainReady();
    if (!read_settings(IniFile::load(install.ini())).directinput_pads) {
        SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "0");   // its device scan can hold SDL_Init for about 20 s (settings.hpp directinput_pads)
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "THPSource2: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    SDL_Window* window = SDL_CreateWindow(kTitle, static_cast<int>((size_w > 0 ? size_w : 1060) * scale), static_cast<int>((size_h > 0 ? size_h : 780) * scale),
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (renderer == nullptr) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "THPSource2", SDL_GetError(), nullptr);
        return 1;
    }
    set_window_icon(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowMinimumSize(window, static_cast<int>(880 * scale), static_cast<int>(600 * scale));
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // the launcher keeps no window layout file
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    style_ui(scale);
    const char* windows = std::getenv("WINDIR");
    const fs::path font = fs::path(windows != nullptr ? windows : "C:\\Windows") / "Fonts" / "segoeui.ttf";
    std::error_code ec;
    if (fs::is_regular_file(font, ec)) {
        io.Fonts->AddFontFromFileTTF(utf8_of(font).c_str(), 18.0f * scale);
        // the arrows of the trick lists (the diagonals are not in every Segoe UI): Segoe UI Symbol fills in what the first font lacks
        const fs::path symbols = fs::path(windows != nullptr ? windows : "C:\\Windows") / "Fonts" / "seguisym.ttf";
        if (fs::is_regular_file(symbols, ec)) {
            ImFontConfig fallback;
            fallback.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(utf8_of(symbols).c_str(), 18.0f * scale, &fallback);
        }
    } else {
        ImFontConfig config;
        config.SizePixels = 15.0f * scale;
        io.Fonts->AddFontDefault(&config);
    }
    {   // the headings' semibold (Segoe UI Semibold); without it they use the regular face
        const fs::path semibold = fs::path(windows != nullptr ? windows : "C:\\Windows") / "Fonts" / "seguisb.ttf";
        if (fs::is_regular_file(semibold, ec)) {
            g_heading_font = io.Fonts->AddFontFromFileTTF(utf8_of(semibold).c_str(), 18.0f * scale);
        }
    }
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    App app;
    app.window = window;
    app.scale = scale;
    app.logo = load_logo(renderer);
    app.page = page;
    app.play_requested = play;
    app.update_check_allowed = !no_update_check && screenshot.empty();
    app.init(install.root);

    bool hidden = false;
    int frames = 0;
    while (!app.quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (app.capture_event(event)) {
                continue;
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))) {
                app.quit = true;
            }
        }
        // while the game runs the launcher steps aside, and comes back when it ends (it reads the run's result, the crash panel and the new scores then)
        if (app.game_running()) {
            if (!hidden) {
                SDL_HideWindow(window);
                hidden = true;
            }
            SDL_Delay(100);
            continue;
        }
        if (hidden) {
            SDL_ShowWindow(window);
            SDL_RaiseWindow(window);
            hidden = false;
        }
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
            SDL_Delay(50);
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (hover_x >= 0.0f) {
            io.AddMousePosEvent(hover_x, hover_y);
        }
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        if (app.light()) {
            SDL_SetRenderDrawColor(renderer, 242, 243, 246, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 23, 25, 31, 255);
        }
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (!screenshot.empty() && ++frames == 8) {
            if (SDL_Surface* picture = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SaveBMP(picture, screenshot.c_str());
                SDL_DestroySurface(picture);
            }
            app.quit = true;
        }
        SDL_RenderPresent(renderer);
    }
    app.shutdown();
    if (app.logo != nullptr) {
        SDL_DestroyTexture(app.logo);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
