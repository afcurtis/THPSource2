#pragma once

#include "screen_kit.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10g: the options screen (options.cpp, 0x00481200..0x00489490, with the level information window of videowin.cpp at 0x0045b0a0 and the movie list's helper of
// vidmenu.cpp at 0x004cea70) from the disassembly: the front end's OPTIONS screen. One window (OptionsMenu) holds the main list, three sub lists and the pages it
// opens one at a time; the window's state (0x14 .. 0x1f) says which one is showing:
//   0x14 the main list (Player Controls, Display Controls, Sound levels, Movies, Saved data, Cheats, High scores, Gap checklist, Display options, Back)
//   0x15 the cheats page            0x16 the display options list (trick tips, score display)    0x17 the movies list (cinematics, game and music credits)
//   0x18 the gap checklist          0x19 the high scores                                         0x1a the movie list (cinematics)
//   0x1b the sound levels page      0x1c the saved data list (load, save, load replay)           0x1d the memory card page    0x1e the replay load page
//   0x1f the controller page
// The window works on retail memory like the other screens (the window base, the menu toolkit, the font code run natively); every other class's constructor, the
// engine's sprite, model, sound, card and pad calls are seams reported as services.
//
// OptionsMenu (0xa00 bytes, vtable 0x005193c4): a Window. +0x0ac the background   +0x0b0 the video background   +0x0b4 and +0x200 two Fonts (mainf, s2font2)
//   +0x34c, +0x350, +0x354, +0x358 four MenuLists (main, display options, movies, saved data)   +0x35c and +0x360 two MenuProperties (the rows' style and the
//   title's)   +0x364, +0x4e4, +0x664, +0x7e4 four arrays of 0x60 row slots (one for each list)   +0x4e8 and +0x974 the trick tips row, +0x4ec and +0x978 the score
//   display row   +0x964 and +0x968 the two arrow sprites   +0x96c the state   +0x970 the state wanted   +0x97c, +0x980, +0x984, +0x988, +0x98c, +0x990, +0x994 the
//   pages (gap checklist, high scores, movie list, sound levels, memory card, replay load, controller)   +0x998, +0x99c, +0x9a0 three ModelViews (memory card,
//   boombox, controller)   +0x9a4 the cheats page   +0x9a8..+0x9b4 four prompt windows   +0x9b8 the models' view, +0x9cc their light rig.
constexpr std::uint32_t kVtOptionsMenu = 0x005193c4;
constexpr std::uint32_t kOptionsMenuSize = 0xa00;
constexpr std::uint32_t kVtVideoWindow = 0x00518b30;
constexpr std::uint32_t kVtGapChecklist = 0x005193e4;
constexpr std::uint32_t kVtHighScores = 0x00519404;
constexpr std::uint32_t kVtMovieList = 0x00519424;
constexpr std::uint32_t kVtSoundLevels = 0x00519444;
constexpr std::uint32_t kVtCardPage = 0x00519464;
constexpr std::uint32_t kVtReplayPage = 0x00519484;
constexpr std::uint32_t kVtControlsMenu = 0x005194a4;
constexpr std::uint32_t kVtCheatsPage = 0x005194c4;

class OptionsScreen final : public ScreenKit {
public:
    OptionsScreen(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the screen call and the loop ----
    void screen_call(std::uint32_t argument, std::uint32_t result);                                  // 0x00481200
    std::int32_t run();                                                                              // 0x00483ef0

    // ---- the window ----
    std::uint32_t menu_construct(std::uint32_t self);                                                // 0x00481210
    bool cheats_available();                                                                         // 0x00482450
    std::uint32_t menu_delete(std::uint32_t self, std::uint32_t flag);                               // 0x00482460
    void menu_destroy(std::uint32_t self);                                                           // 0x00482480
    void set_prompts(std::uint32_t self, std::uint32_t changed, std::uint32_t mask);                 // 0x004826f0
    void sync(std::uint32_t self);                                                                   // 0x00482770
    void apply(std::uint32_t self);                                                                  // 0x00482870
    void menu_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                              // 0x00482950
    void menu_update(std::uint32_t self);                                                            // 0x00482a40
    std::uint32_t menu_step(std::uint32_t self);                                                     // 0x00482e60

    // ---- the level information window (videowin.cpp) ----
    std::uint32_t video_construct(std::uint32_t self, std::uint32_t level, std::uint32_t with_background, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                  std::int32_t start_y);                                              // 0x0045b0a0

    // ---- the gap checklist ----
    bool gaps_locked(std::int32_t level);                                                            // 0x00484ab0
    std::uint32_t gaps_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x004840b0
    std::uint32_t gaps_delete(std::uint32_t self, std::uint32_t flag);                               // 0x00484af0
    void gaps_destroy(std::uint32_t self);                                                           // 0x00484b10
    void gaps_update(std::uint32_t self);                                                            // 0x00484c10
    void gaps_strip_brackets(std::uint32_t text);                                                    // 0x004850a0
    void gaps_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                              // 0x004850f0
    void gaps_sort(std::uint32_t self, std::int32_t lo, std::int32_t hi);                            // 0x00485290
    void gaps_sort_all(std::uint32_t self);                                                          // 0x004853b0

    // ---- the high scores ----
    std::uint32_t scores_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x00485400
    std::uint32_t scores_delete(std::uint32_t self, std::uint32_t flag);                             // 0x00485c90
    void scores_destroy(std::uint32_t self);                                                         // 0x00485cb0
    void scores_update(std::uint32_t self);                                                          // 0x00485d80
    void scores_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                            // 0x00485f90

    // ---- the movie list ----
    bool movie_unlocked(std::uint32_t kind, std::int32_t index);                                     // 0x00486910
    std::uint32_t movie_construct(std::uint32_t self, std::uint32_t font, std::uint32_t arrow_a, std::uint32_t arrow_b);   // 0x004860d0
    std::uint32_t movie_delete(std::uint32_t self, std::uint32_t flag);                              // 0x004866e0
    void movie_destroy(std::uint32_t self);                                                          // 0x00486700
    void movie_update(std::uint32_t self);                                                           // 0x00486790
    void movie_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                             // 0x004867a0
    std::uint32_t movie_step(std::uint32_t self);                                                    // 0x00486830

    // ---- the sound levels page ----
    std::uint32_t sound_construct(std::uint32_t self, std::uint32_t font, std::uint32_t arrow_a, std::uint32_t arrow_b);   // 0x00486b10
    std::uint32_t sound_delete(std::uint32_t self, std::uint32_t flag);                              // 0x00486e80
    void sound_destroy(std::uint32_t self);                                                          // 0x00486ea0
    void sound_update(std::uint32_t self);                                                           // 0x00486f80
    void sound_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                             // 0x00487130
    std::uint32_t sound_step(std::uint32_t self);                                                    // 0x004871e0
    bool sound_has_selection(std::uint32_t self);                                                    // 0x00487280
    bool sound_no_selection(std::uint32_t self);                                                     // 0x004872a0

    // ---- the memory card page and the replay page ----
    std::uint32_t card_construct(std::uint32_t self, std::uint32_t load);                            // 0x004872b0
    std::uint32_t card_delete(std::uint32_t self, std::uint32_t flag);                               // 0x00487330
    void card_destroy(std::uint32_t self);                                                           // 0x00487350
    void card_update(std::uint32_t self);                                                            // 0x00487360, 0x00487440
    void card_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                              // 0x00487370, 0x00487450
    std::uint32_t card_step();                                                                       // 0x00487390, 0x00487470
    std::uint32_t replay_construct(std::uint32_t self);                                              // 0x004873b0
    std::uint32_t replay_delete(std::uint32_t self, std::uint32_t flag);                             // 0x00487410
    void replay_destroy(std::uint32_t self);                                                         // 0x00487430

    // ---- the controller page ----
    std::uint32_t controls_construct(std::uint32_t self, std::uint32_t player, std::uint32_t font, std::uint32_t arrow_a, std::uint32_t arrow_b);   // 0x00487490
    std::uint32_t controls_delete(std::uint32_t self, std::uint32_t flag);                           // 0x00487c10
    void controls_destroy(std::uint32_t self);                                                       // 0x00487c30
    void controls_update(std::uint32_t self);                                                        // 0x00487dc0
    void controls_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                          // 0x00487dd0
    void controls_text(std::uint32_t buffer, std::uint32_t control, std::uint32_t value);            // 0x00487f60
    std::uint32_t controls_step(std::uint32_t self);                                                 // 0x00488160
    void controls_clamp(std::uint32_t row, std::int32_t low, std::int32_t high);                     // 0x00488410
    bool controls_unique(std::uint32_t self, std::uint32_t row);                                     // 0x00488480
    void controls_fix(std::uint32_t self);                                                           // 0x004884c0
    bool controls_selectable(std::uint32_t self);                                                    // 0x00488590 (0x004885c0 is its negation)

    // ---- the cheats page ----
    std::uint32_t cheats_construct(std::uint32_t self, std::uint32_t font, std::uint32_t arrow_a, std::uint32_t arrow_b);   // 0x004885d0
    std::uint32_t cheats_delete(std::uint32_t self, std::uint32_t flag);                             // 0x00488d50
    void cheats_destroy(std::uint32_t self);                                                         // 0x00488d70
    void cheats_update(std::uint32_t self);                                                          // 0x00488e30
    void cheats_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                            // 0x00488e40
    std::uint32_t cheats_step(std::uint32_t self);                                                   // 0x004891a0
    void cheats_refresh(std::uint32_t self);                                                         // 0x00489240
    void cheats_store(std::uint32_t self);                                                           // 0x004892a0
    bool cheats_switchable(std::uint32_t self);                                                      // 0x00489350
    bool cheats_on_back(std::uint32_t self);                                                         // 0x00489390

    // ---- the video menu (vidmenu.cpp) ----
    void video_menu_destroy(std::uint32_t self);                                                     // 0x004cea70
    void video_menu_init_state();                                                                    // 0x004cdf70 (jump thunk 0x004cdf60)
    void video_menu_init_options();                                                                  // 0x004cdfb0 (jump thunk 0x004cdfa0)
    void video_menu_run_into(std::uint32_t out);                                                     // 0x004ce020
    std::int32_t video_menu_run();                                                                   // 0x004ce030
    void video_menu_apply_gamma();                                                                   // 0x004ce260
    std::uint32_t video_menu_is_done(std::uint32_t self);                                            // 0x004ce290
    std::uint32_t video_menu_construct(std::uint32_t self);                                          // 0x004ce2a0
    std::uint32_t video_menu_delete(std::uint32_t self, std::uint32_t flag);                         // 0x004cea50

    // The virtual methods of the window-base classes, run natively: offered to the menu toolkit (set_extra_dispatch) and to the window base (set_dispatch, by service
    // name) so that a child of a real class is not reported as a service. The per-frame hook (slot 7) is never dispatched.
    bool dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args);
    bool dispatch_named(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args);

private:
    // the poll of each state of the window (the targets of the step's jump table)
    std::uint32_t step_main(std::uint32_t self);                                                     // 0x00482f50
    std::uint32_t step_cheats(std::uint32_t self);                                                   // 0x004836f0
    std::uint32_t step_display(std::uint32_t self);                                                  // 0x00483370
    std::uint32_t step_movies(std::uint32_t self);                                                   // 0x004834b0
    std::uint32_t step_gaps(std::uint32_t self);                                                     // 0x00483730
    std::uint32_t step_scores(std::uint32_t self);                                                   // 0x004837e0
    std::uint32_t step_movie_list(std::uint32_t self);                                               // 0x00483890
    std::uint32_t step_sound(std::uint32_t self);                                                    // 0x00483930
    std::uint32_t step_saved(std::uint32_t self);                                                    // 0x004839c0
    std::uint32_t step_card(std::uint32_t self);                                                     // 0x00483da0
    std::uint32_t step_replay(std::uint32_t self);                                                   // 0x00483d00
    std::uint32_t step_controls(std::uint32_t self);                                                 // 0x00483e60
    // helpers shared by the polls
    bool nav_edge();
    void nav_sound();
    void drop_page(std::uint32_t self, std::uint32_t slot, std::uint32_t line);
    std::uint32_t make_page(std::uint32_t self, std::uint32_t slot, std::uint32_t line, std::uint32_t size, std::uint32_t constructor, std::vector<std::uint32_t> args);
};

}  // namespace opentony::runtime::frontend
