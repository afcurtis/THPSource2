#pragma once

#include "career.hpp"
#include "screen_kit.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10f: the player-select screen (playsel.cpp) and the window classes it builds (player_ui.cpp and the classes next to it), from the disassembly.
// Retail memory is the model, as everywhere in the front end: the window base, the menu toolkit, the font code, the skater manager and the career records
// run natively on the same memory; everything else (the engine's sprite, model, spool and render calls, the memory-card screen) is a seam reported as a
// service with the arguments retail passed.
//
// Window classes (all derive from Window, 0xac bytes: the element and child table of frontend_window.hpp), with their vtables:
//   ScoreCounter (0xbc bytes, 0x00519520): +0x0ac the target value   +0x0b0 the value shown (it moves half way to the target each frame)   +0x0b4 the
//     "$%6d" text element   +0x0b8 the text's font.  Slots: 0 delete, 1 update (0x004a15a0), 2 draw (0x004a1560).
//   MedalWindow (0xb8 bytes, 0x00519540): children 0 and 1 are two ScoreCounters (the character's two totals), child 2 the frame sprite; +0x0ac, +0x0b0,
//     +0x0b4 are the gold, silver and bronze medal sprites (owned, not children: the draw places them).  Slots: 0 delete, 1 update, 2 draw (0x004a19f0).
//   PromptWindow (0xb0 bytes, 0x00519560): child 0 the button sprite, child 1 the caption text (also at +0x0ac).  Slot 3 is the width.
//   TextFlagElement (a TextScreenElement of 0x34 bytes, 0x00519584): +0x30 the slide shift (1 fast, 2 slow).
//   PlayerSelectWindow (0x4dc bytes, 0x005195a4): +0x0ac the state (0 skaters, 1 menu, 2 load)   +0x0b0 the background sprite   +0x0b4 / +0x200 / +0x34c three
//     Font objects (s2font2, s2font1, s2font3)   +0x498 whether the player may continue to the menu (a second player)   +0x49c the number of players
//     +0x4a0 the SkaterView   +0x4a4 the MedalWindow   +0x4a8 the BioWindow   +0x4ac the PlayerHeader   +0x4b0 the PlayerMenu   +0x4b4 the name text
//     +0x4b8 the LoadWindow   +0x4bc..+0x4d4 seven PromptWindows (select, blank, blank, rotate, blank, accept, back)   +0x4d8 the PageDots.
//   BioWindow (0x244 bytes, 0x005195c4), SkaterView (0x13dc bytes, 0x005195e4), PageDots (0xb0, 0x00519604), PlayerHeader (0xb0, 0x00519624),
//     PlayerMenu (0x240, 0x00519644), LoadWindow (0x248, 0x00519664): described where their methods are.
constexpr std::uint32_t kVtScoreCounter = 0x00519520;
constexpr std::uint32_t kVtMedalWindow = 0x00519540;
constexpr std::uint32_t kVtPromptWindow = 0x00519560;
constexpr std::uint32_t kVtTextFlag = 0x00519584;
constexpr std::uint32_t kVtPlayerSelect = 0x005195a4;
constexpr std::uint32_t kVtBioWindow = 0x005195c4;
constexpr std::uint32_t kVtSkaterView = 0x005195e4;
constexpr std::uint32_t kVtPageDots = 0x00519604;
constexpr std::uint32_t kVtPlayerHeader = 0x00519624;
constexpr std::uint32_t kVtPlayerMenu = 0x00519644;
constexpr std::uint32_t kVtLoadWindow = 0x00519664;

class PlayerSelect final : public ScreenKit {
public:
    PlayerSelect(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the score counter (player_ui.cpp) ----
    std::uint32_t counter_construct(std::uint32_t self, std::uint32_t value, std::uint32_t font, std::int32_t x, std::int32_t y);   // 0x004a1440
    std::uint32_t counter_delete(std::uint32_t self, std::uint32_t flag);                    // 0x004a1510
    void counter_destroy(std::uint32_t self);                                                // 0x004a1530
    void counter_set_value(std::uint32_t self, std::int32_t value);                          // 0x004a1540
    void counter_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                   // 0x004a1560
    void counter_update(std::uint32_t self);                                                 // 0x004a15a0

    // ---- the medal window ----
    std::uint32_t medal_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x004a1670
    std::uint32_t medal_delete(std::uint32_t self, std::uint32_t flag);                      // 0x004a1880
    void medal_destroy(std::uint32_t self);                                                  // 0x004a18a0
    void medal_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                     // 0x004a19f0
    void medal_update(std::uint32_t self);                                                   // 0x004a1b00

    // ---- the prompt window ----
    std::uint32_t prompt_construct(std::uint32_t self, std::uint32_t file, std::uint32_t text, std::uint32_t font, std::int32_t x, std::int32_t y,
                                   std::int32_t start_x, std::int32_t start_y);              // 0x004a1b40
    std::uint32_t prompt_delete(std::uint32_t self, std::uint32_t flag);                     // 0x004a1dd0
    void prompt_destroy(std::uint32_t self);                                                 // 0x004a1df0
    std::uint32_t prompt_width(std::uint32_t self);                                          // 0x004a1e00
    void prompt_set_string(std::uint32_t self, std::uint32_t text);                          // 0x004a1eb0

    // ---- the flagged text element ----
    std::uint32_t flag_construct(std::uint32_t self, std::uint32_t text, std::uint32_t font, std::uint32_t flag, std::int32_t x, std::int32_t y,
                                 std::int32_t start_x, std::int32_t start_y);                // 0x004a1ed0
    std::uint32_t flag_delete(std::uint32_t self, std::uint32_t flag);                       // 0x004a1f20
    void flag_destroy(std::uint32_t self);                                                   // 0x004a1f40
    void flag_update(std::uint32_t self);                                                    // 0x004a1f50

    // ---- the screen callbacks and the selection globals ----
    void init_callbacks(std::uint32_t record);                                               // 0x004a1fe0
    void exit_callback();                                                                    // 0x004a2000
    void screen_call(std::uint32_t argument, std::uint32_t result);                          // 0x004a2010
    void save_selection(std::uint32_t self);                                                 // 0x004a2030
    void restore_selection(std::uint32_t self);                                              // 0x004a2040
    std::int32_t run(std::uint32_t mode);                                                    // 0x004a5f60

    // ---- the select window (playsel.cpp) ----
    std::uint32_t select_construct(std::uint32_t self, std::int32_t mode);                   // 0x004a2050
    std::uint32_t select_delete(std::uint32_t self, std::uint32_t flag);                     // 0x004a20e0
    void select_init(std::uint32_t self, std::int32_t mode);                                 // 0x004a2100
    void select_destroy(std::uint32_t self);                                                 // 0x004a2bc0
    void select_set_prompts_hidden(std::uint32_t self, std::uint32_t hidden);                // 0x004a2cb0
    void select_draw(std::uint32_t self);                                                    // 0x004a2ce0
    void select_update_frame(std::uint32_t self);                                            // 0x004a3110
    std::uint32_t select_update_skaters(std::uint32_t self);                                 // 0x004a3230
    std::uint32_t select_update_menu(std::uint32_t self);                                    // 0x004a34b0
    std::uint32_t select_update_load(std::uint32_t self);                                    // 0x004a35e0
    bool select_next_player(std::uint32_t self);                                             // 0x004a35f0
    std::uint32_t select_update(std::uint32_t self);                                         // 0x004a36e0
    std::uint32_t menu_choice(std::uint32_t menu);                                           // 0x004a5f50

    // ---- the bio window ----
    std::uint32_t bio_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x004a3830
    std::uint32_t bio_delete(std::uint32_t self, std::uint32_t flag);                        // 0x004a3a40
    void bio_destroy(std::uint32_t self);                                                    // 0x004a3a60
    void bio_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                       // 0x004a3af0
    void bio_update(std::uint32_t self);                                                     // 0x004a3bd0
    void bio_mouse_hook(std::uint32_t self);                                                 // 0x004dcf00 (slot 7 of the bio window: the mouse over the scroll bar presses up or down)
    void bio_scroll_up(std::uint32_t self);                                                  // 0x004a3e00
    void bio_scroll_down(std::uint32_t self);                                                // 0x004a3e40

    // ---- the page dots ----
    std::uint32_t dots_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x004a5450
    std::uint32_t dots_delete(std::uint32_t self, std::uint32_t flag);                       // 0x004a54f0
    void dots_destroy(std::uint32_t self);                                                   // 0x004a5510
    void dots_update(std::uint32_t self);                                                    // 0x004a5690
    void dots_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                      // 0x004a55c0

    // ---- the header ----
    std::uint32_t header_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x004a56b0
    std::uint32_t header_delete(std::uint32_t self, std::uint32_t flag);                     // 0x004a57b0
    void header_destroy(std::uint32_t self);                                                 // 0x004a57d0
    void header_update(std::uint32_t self);                                                  // 0x004a57e0

    // ---- the main menu window ----
    std::uint32_t menu_construct(std::uint32_t self, std::uint32_t two_player, std::uint32_t font, std::int32_t x, std::int32_t y);   // 0x004a5890
    std::uint32_t menu_delete(std::uint32_t self, std::uint32_t flag);                       // 0x004a5ca0
    void menu_destroy(std::uint32_t self);                                                   // 0x004a5cc0
    void menu_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                      // 0x004a5d80

    // ---- the skater view (and the spool state embedded in it) ----
    std::uint32_t view_construct(std::uint32_t self, std::uint32_t multi, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                 std::int32_t start_y);                                      // 0x004a3e80
    std::uint32_t view_delete(std::uint32_t self, std::uint32_t flag);                       // 0x004a4360
    void view_destroy(std::uint32_t self);                                                   // 0x004a4380
    void view_build_sprites(std::uint32_t self);                                             // 0x004a44f0
    void view_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                      // 0x004a4630
    void view_load_skater(std::uint32_t self);                                               // 0x004a48e0
    void view_update(std::uint32_t self);                                                    // 0x004a4bc0
    void view_create_object(std::uint32_t self);                                             // 0x004a4e40
    void view_copy_packet(std::uint32_t packet);                                             // 0x004a5080
    void view_init_slots(std::uint32_t spool);                                               // 0x004a61f0
    void view_free_slots(std::uint32_t spool);                                               // 0x004a6260
    [[nodiscard]] bool view_spool_busy(std::uint32_t spool);                                 // 0x004a6e60
    void view_set_spool_flag(std::uint32_t spool, std::uint32_t value);                      // 0x004a6e90
    void view_reset_spool(std::uint32_t spool);                                              // 0x004a6ea0

    // ---- the load window ----
    std::uint32_t load_construct(std::uint32_t self, std::int32_t x, std::int32_t y);        // 0x004a6f50
    std::uint32_t load_delete(std::uint32_t self, std::uint32_t flag);                       // 0x004a7460
    void load_enter(std::uint32_t self);                                                     // 0x004a7480
    void load_destroy(std::uint32_t self);                                                   // 0x004a74c0
    std::uint32_t load_step(std::uint32_t self);                                             // 0x004a7590
    void load_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                      // 0x004a7730
    void load_update(std::uint32_t self);                                                    // 0x004a77e0
    void load_save_image(std::uint32_t self);                                                // 0x004a7890
    void load_restore_image(std::uint32_t self);                                             // 0x004a78b0
    void load_load_skater(std::uint32_t self, std::uint32_t custom);                         // 0x004a78d0

    // The virtual methods of the window-base classes and of this screen's own classes, run natively: offered to the menu toolkit (set_extra_dispatch)
    // and to the window base (set_dispatch, by service name) so that a child of a real class is not reported as a service.
    bool dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args);
    bool dispatch_named(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args);

    // ---- the small methods of the other classes the select window calls ----
    [[nodiscard]] bool view_is_custom(std::uint32_t view);                                   // 0x004a5380
    void view_set_mode(std::uint32_t view, std::uint32_t mode);                              // 0x004a44b0
    void view_rotate(std::uint32_t view, std::uint32_t angle);                               // 0x004a44d0
    void view_step_selection(std::uint32_t view, std::int32_t step);                         // 0x004a5390
    void view_refresh(std::uint32_t view);                                                   // 0x004a4890
    void next_costume();                                                                     // 0x004a5340
    void header_slide_in(std::uint32_t header);                                              // 0x004a5880
    void menu_highlight_first(std::uint32_t menu);                                           // 0x004a5d70
    [[nodiscard]] bool load_is_loading(std::uint32_t load);                                  // 0x004a78f0

private:
    // The width of a window child (virtual slot 3): a sprite or text element of the window base answers itself, anything else is reported
    std::uint32_t child_width(std::uint32_t child);
    void child_draw(std::uint32_t child, std::int32_t x, std::int32_t y) {
        vcall_item(kElementDraw, child, 2, {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)});
    }
};

}  // namespace opentony::runtime::frontend
