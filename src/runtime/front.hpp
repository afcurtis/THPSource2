#pragma once

#include "screen_kit.hpp"
#include "guest_view.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10i: front.cpp, the in-level front end: the pause menu, the end-of-run menus and the screens between a level and the next (the statistics, the high-score name entry,
// the unlock messages, the horse letters), with the old CMenu class they are made of, and the level loader that brings a level up (Front_LoadGame and the player loading
// under it). The global state is a handful of statics at 0x00561xxx (the menus, the status, the animation counters), the pad edge bytes and the career/competition globals;
// everything the screens reach (Mess_* text, sound, save-card, replay and pad services, the engine's per-frame functions, and the screens other files own: the competition,
// the career, the level loader's resource queue) is a seam reported by its retail address.
//
// CMenu (vtable 0x00518a00; its base class vtable is 0x005184b4):
//   +0x00 vtable   +0x04 byte justification   +0x06 byte the current line   +0x09 byte the number of entries   +0x0c x   +0x10 y of the first line   +0x14 line spacing
//   entry e (e = 0..14) is spread over 0x18 bytes at +0x18*e:  +0x18 name pointer  +0x1c word current scale  +0x1e word target scale  +0x20 word scale when selected
//   +0x22 word normal scale  +0x24 byte extra height  +0x25 byte shown  +0x26 byte can be chosen  +0x27..+0x29 selected colour  +0x2a..+0x2c normal colour  +0x2d byte fade
namespace front {

constexpr std::uint32_t kVtCMenu = 0x00518a00;
constexpr std::uint32_t kMenuEntries = 15;

constexpr std::uint32_t kStatus = 0x00561bfc;              // GStatus: which screen the front end is on
constexpr std::uint32_t kGotNewStatus = 0x005616d8;
constexpr std::uint32_t kStatusNames = 0x0052be50;         // pointer per status
constexpr std::uint32_t kGameMode = 0x00533f38;            // LevelEventGameModeSelector
constexpr std::uint32_t kPlayerCount = 0x00533f3c;         // GNumberOfPlayers
constexpr std::uint32_t kPlayer = 0x0056a858;              // Player (the first skater); +4 is the second
constexpr std::uint32_t kPlayer2 = 0x0056a85c;
constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kSimTick = 0x0056e31c;             // SimulationPublicTick
constexpr std::uint32_t kAnimScale = 0x0056865c;           // AnimationTimeScale
constexpr std::uint32_t kAnimClock = 0x0056ad64;           // AnimationClock

constexpr std::uint32_t kMenuYesNo = 0x00561c1c;
constexpr std::uint32_t kMenuEnd = 0x00561c20;
constexpr std::uint32_t kMenuReplayFull = 0x00561c24;
constexpr std::uint32_t kMenuReplayShort = 0x00561c28;
constexpr std::uint32_t kMenuReplayComp = 0x00561c2c;
constexpr std::uint32_t kMenuHeat = 0x00561c30;
constexpr std::uint32_t kMenuPlayer = 0x00561c34;
constexpr std::uint32_t kMenuTime = 0x00561c38;
constexpr std::uint32_t kMenuSound = 0x00561c3c;
constexpr std::uint32_t kMenuPaused = 0x00561c40;
constexpr std::uint32_t kMenuRestart = 0x00561c44;

}  // namespace front

class FrontScreens final : public ScreenKit {
public:
    FrontScreens(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the CMenu class (0x0044b640..0x0044bde0) ----
    std::uint32_t cmenu_new(std::uint32_t self, std::int32_t x, std::int32_t y, std::uint32_t justify, std::uint32_t scale_a, std::uint32_t scale_b, std::int32_t spacing);   // 0x0044b640
    std::uint32_t cmenu_destruct(std::uint32_t self, std::uint32_t flag);                                  // 0x0044b6f0
    void cmenu_delete(std::uint32_t self);                                                                 // 0x0044b710
    void cmenu_centre_y(std::uint32_t self);                                                               // 0x0044b720
    void cmenu_set_line(std::uint32_t self, std::uint32_t line);                                           // 0x0044b770
    void cmenu_reset(std::uint32_t self);                                                                  // 0x0044b7b0
    std::uint32_t cmenu_find_entry(std::uint32_t self, std::uint32_t name);                                // 0x0044b800
    void cmenu_add_entry(std::uint32_t self, std::uint32_t name);                                          // 0x0044b890
    void cmenu_entry_on(std::uint32_t self, std::uint32_t name);                                           // 0x0044b920
    void cmenu_entry_off(std::uint32_t self, std::uint32_t name);                                          // 0x0044b940
    bool cmenu_is_entry_on(std::uint32_t self, std::uint32_t name);                                        // 0x0044b960
    void cmenu_enable(std::uint32_t self, std::uint32_t name);                                             // 0x0044b980
    void cmenu_disable(std::uint32_t self, std::uint32_t name);                                            // 0x0044b9a0
    bool cmenu_is_shown(std::uint32_t self, std::uint32_t name);                                           // 0x0044b9c0
    void cmenu_set_height(std::uint32_t self, std::uint32_t name, std::uint32_t height);                   // 0x0044b9e0
    bool cmenu_choice_is(std::uint32_t self, std::uint32_t name);                                          // 0x0044ba00
    void cmenu_position_of(std::uint32_t self, std::uint32_t name, std::uint32_t out_x, std::uint32_t out_y);   // 0x0044ba20
    void cmenu_display(std::uint32_t self);                                                                // 0x0044ba90
    void cmenu_update(std::uint32_t self);                                                                 // 0x0044bbc0

    // ---- the small screens and helpers ----
    void demo_font_construct();                                                                            // 0x0044b180
    void set_status(std::uint32_t line, std::uint32_t status);                                             // 0x0044b1f0
    void print_demo();                                                                                     // 0x0044b240
    void demo_mode_blink();                                                                                // 0x0044b320
    void display_screen(std::uint32_t file);                                                               // 0x0044b3a0
    void restart_comp_frig();                                                                              // 0x0044b470
    void horse_display_words(std::uint32_t counts);                                                        // 0x0044b570
    void clear_screen();                                                                                   // 0x0044bde0
    void init();                                                                                           // 0x0044be50
    void delete_menu(std::uint32_t variable);                                                              // 0x0044c650
    void delete_menus();                                                                                   // 0x0044c670
    void hint_none() {}                                                                                    // 0x0044c6e0 (the console button hints are gone from the PC build)
    void hint_one() {}                                                                                     // 0x0044c6f0
    void hint_text(std::uint32_t, std::uint32_t) {}                                                        // 0x0044c700
    void print_go_back(std::uint32_t player, std::int32_t x, std::int32_t y, std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t steals,
                       std::uint32_t losses, std::uint32_t streak, std::uint32_t total, std::int32_t result);   // 0x0044c710
    void display();                                                                                        // 0x0044ca60
    void draw_paused_bitmap();                                                                             // 0x0044dd80

    // ---- the status changes and the state helpers (0x0044dda0..0x0044ea70) ----
    struct Buttons {
        std::uint32_t accept = 0;      // a select press (either pad)
        std::uint32_t back = 0;        // a back press
        std::uint32_t any = 0;         // any press at all
        std::uint32_t select = 0;      // a select press on pad 1 or 2
    };
    Buttons poll_buttons();
    void poll_buttons_to(std::uint32_t accept, std::uint32_t back, std::uint32_t any, std::uint32_t start);    // 0x0044dda0
    void common_switch_on_menu_code();                                                                     // 0x0044dfa0
    void switch_on_end_level();                                                                            // 0x0044e0f0
    void reset_score_slide();                                                                              // 0x0044e3f0
    void switch_on_end_heat();                                                                             // 0x0044e420
    void switch_on_ending_screen();                                                                        // 0x0044e4e0
    void switch_on_level_menu();                                                                           // 0x0044e540
    void switch_on_time_select();                                                                          // 0x0044e5c0
    void horse_message(std::uint32_t first, std::uint32_t second, std::uint32_t counter, std::uint32_t sound);   // 0x0044e630
    void print_horse_letters();                                                                            // 0x0044e7e0
    void select_play_replay();                                                                             // 0x0044e950
    void switch_on_video_screen();                                                                         // 0x0044e9c0
    void replay_restart();                                                                                 // 0x0044ea70
    void update();                                                                                         // 0x0044eac0

    // ---- the level loader (0x004521c0..0x00452dff) ----
    void skater_load_player_resources(std::uint32_t player);                                               // 0x004521c0
    void psx_share_animation_region(std::uint32_t region, std::uint32_t source, std::uint32_t raw);        // 0x00452390
    std::uint32_t wait_for_button(std::uint32_t skip);                                                     // 0x00452430
    void load_game();                                                                                      // 0x004524a0
    bool check_unlocks(std::uint32_t resume);                                                              // 0x00452a80
    void unlock_continue();                                                                                // 0x00452d00
    bool quit_combo();                                                                                     // 0x00452d30
    void quit_combo_reset() { guest::write_scalar<std::uint8_t>(m_, 0x00561cbc, 0); }                                                    // 0x00452dc0
    void quit_combo_set() { guest::write_scalar<std::uint8_t>(m_, 0x00561cbc, 1); }                                                      // 0x00452dd0
    bool replay_playing() { return guest::read_scalar<std::int16_t>(m_, 0x0056b8d4) > 0 && guest::read_scalar<std::uint8_t>(m_, 0x00561990) == 0; }                     // 0x00452de0

    // ---- frontend2.cpp: the shell that runs the screens (0x00452e40..0x004548ff) ----
    void get_unlock_flags(std::uint32_t out);                                                              // 0x00452e40
    void reselect_after_unlock_change(std::uint32_t old);                                                  // 0x00452ec0
    void shell_nothing() {}                                                                                // 0x00452fe0
    void frontend_main();                                                                                  // 0x00452ff0
    void replay_swap_in();                                                                                 // 0x00454130
    void replay_swap_out();                                                                                // 0x00454300
    void launch_game_level(std::uint32_t level, std::uint32_t mode);                                       // 0x004544a0
    void start_shell_music();                                                                              // 0x00454710
    void sync_skater_slots();                                                                              // 0x00454760
    std::uint32_t texture_set_entry(std::uint32_t set, std::uint32_t deck);                                // 0x004547f0
    void clear_gvideo_restart_data();                                                                      // 0x004ac3f0

private:
    // the shell's helpers
    std::uint32_t screen_call(std::uint32_t address, std::vector<std::uint32_t> args, std::string text = {});
    std::uint32_t local_argument(std::uint32_t value);
    void new_database();
    void delete_database();
    // the Mess_* text calls the screens make (seams, by retail address)
    void mess_delete_all() { engine(0x00473650); }
    void mess_scale(std::uint32_t scale) { engine(0x004734f0, {scale}); }
    void mess_rgb(std::uint32_t r, std::uint32_t g, std::uint32_t b) { engine(0x00473580, {r & 0xff, g & 0xff, b & 0xff, 0}); }
    void mess_justify(std::uint32_t mode) { engine(0x004734b0, {mode & 0xff}); }
    void mess_sort_mode(std::uint32_t mode) { engine(0x00473510, {mode}); }
    void mess_text(std::int32_t x, std::int32_t y, std::uint32_t text) {
        engine(0x004739e0, {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), text, 0}, m_.cstring(text, 0x100));
    }
    void mess_line(std::int32_t x, std::int32_t y, const std::string& line) {
        engine(0x004739e0, {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), scratch_line(line), 0}, line);
    }
    // a formatted line written to a global buffer (sprintf into a static array)
    void sprintf_to(std::uint32_t buffer, const std::string& text);
    void play(std::uint32_t sound, std::uint32_t volume = 0x2000) { s_.note(kSvcSfx, {sound, volume, 0}); }
    // a name through a pointer cell (the menu line names are pointer variables)
    std::uint32_t str(std::uint32_t cell) const { return guest::read_scalar<std::uint32_t>(m_, cell); }
    bool names_equal(std::uint32_t a, std::uint32_t b, std::uint32_t limit) const;
    bool replay_level_flag() const;                                                                        // 0x00419420
    void virtual_delete_menu(std::uint32_t menu);                                                          // slot 0 of a menu: the deleting destructor
    std::uint32_t competition_line() { return str(replay_level_flag() ? 0x0052e554 : 0x0052e550); }
    bool menu_choice(std::uint32_t variable, std::uint32_t cell) { return cmenu_choice_is(guest::read_scalar<std::uint32_t>(m_, variable), str(cell)); }
    void find_line(std::uint32_t variable, std::uint32_t cell) { set_menu_line(guest::read_scalar<std::uint32_t>(m_, variable), str(cell)); }
    std::uint32_t make_menu(std::uint32_t variable, std::uint32_t scale_a);
    void update_late(const Buttons& buttons, bool& leave, std::uint32_t& sound);
    void update_screens(const Buttons& buttons, bool& leave, std::uint32_t status);
    // Input_UpdateActionStates: the probe's stub releases the four held buttons the waits poll (scripted values, written in order; live, the update wrote them itself)
    void input_update_action_states() {
        engine(0x00489a10);
        if (s_.live()) {
            return;
        }
        for (std::uint32_t at : {0x0056b028u, 0x0056b194u, 0x0056b0d8u, 0x0056b244u}) {
            guest::write_scalar<std::uint8_t>(m_, at, static_cast<std::uint8_t>(static_cast<std::uint32_t>(s_.value(engine_name(0x00489a10)))));
        }
    }
    void display_statistics();
    void display_horse_title();
    void display_horse_game();
    void display_high_scores();
    // the horse message with the texts given as values too (a text built in the caller's frame is recorded as the scratch address)
    void horse_message(std::uint32_t first, std::uint32_t second, std::uint32_t counter, std::uint32_t sound, const std::string& first_text, const std::string& second_text);
    void set_menu_line(std::uint32_t menu, std::uint32_t name) { cmenu_set_line(menu, cmenu_find_entry(menu, name)); }
};

}  // namespace opentony::runtime::frontend
