#pragma once

#include "career.hpp"
#include "guest_view.hpp"
#include "competition.hpp"
#include "screen_kit.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10h: the goals, game-mode and competition screens, from the disassembly: competition.cpp's object and its displays (0x00419600..0x0041bff3), the level
// information window's methods (videowin.cpp, 0x0045b3a0..0x0045b8a0), the level picture window, the type 500 node's record and the light editor that share the address
// range of goalwin.cpp (0x0045b950..0x0045d010), the loading screen and the goals checklist screen (0x0045d010..0x0045e4e7), the medal spawner (medals.cpp, 0x0046e550),
// the tape constructor (tapes.cpp, 0x004b8fc0) and the tag game's helpers (tag.cpp, 0x004b84c0..0x004b8fb0). The windows and the competition work on retail memory like the other
// screens (the window base, the menu toolkit, the font code and the skater manager run natively); the engine's text (Mess_*), sound, sprite, movie and draw calls are seams
// reported as services by their retail address.
//
// The competition object (CCompetition, the global at 0x0055fe28; every method is called with it as `this`):
//   +0x00 vtable (0x005184b4 while constructing, 0x00518738)   +0x04 eight participants of 0x14 bytes, +0xb8 five judges of 8 bytes (competition.hpp)
//   +0xa4 humans   +0xa8 the judges' display counter   +0xac the standings counter   +0xb0 the end counter   +0xb4, +0xb6, +0xb7 display flags (sound played, average shown, counted up)
//   +0xec ranking order (8 bytes)   +0xf4 heat   +0xf5 participant up   +0xf6 previous   +0xf8 mask of the level whose medal was just won
//   +0x104 prize money   +0x108, +0x10c, +0x110 the three medal objects   +0x114 the marquee text   +0x164 the marquee scroll   +0x168 the start sound played
//   +0x169 the ending video is playing   +0x16a the prize money was paid
namespace goals {

constexpr std::uint32_t kPrizeFlagA = 0x00560080;      // (the competition's human mode flag lives in competition.hpp as kHumanMode)
constexpr std::uint32_t kFinalVideo = 0x00525780;      // which ending video: 1 the three golds, 2 the three medals
constexpr std::uint32_t kPlayerEntry = 0x00561c1c;     // the pause/career menu object whose line the competition sets
constexpr std::uint32_t kEntryName = 0x0052e410;       // pointer variable: the name of the menu line ("yes")
constexpr std::uint32_t kContestLevelMasks = 0x0055fe1c;   // three dwords: 1 << level of each contest level
constexpr std::uint32_t kMedalTotals = 0x0056a41c;     // dword per character: bit per level whose medal screen was seen
constexpr std::uint32_t kEndFlagA = 0x00561bf0;        // byte set when the three golds were won
constexpr std::uint32_t kEndFlagB = 0x00561b9c;
constexpr std::uint32_t kEndFlagC = 0x00561cdc;
constexpr std::uint32_t kEndFlagD = 0x00561ca0;
constexpr std::uint32_t kEndSettle = 0x00561b74;       // set once the final medal has settled
constexpr std::uint32_t kEndNext = 0x00534110;         // the screen to go on to after the video
constexpr std::uint32_t kEndVideoIndex = 0x0056a94c;   // the video the career plays next
constexpr std::uint32_t kHeatCounter = 0x00525774;

constexpr std::uint32_t kMessFont = 0x0056a9a0;
constexpr std::uint32_t kRendererPointer = 0x00560fd4;
constexpr std::uint32_t kSeatTable = 0x00518638;       // eight dwords: which human sits where (-1 for a computer rider)
constexpr std::uint32_t kHumanSlots = 0x00568a2c;      // the manager slot of each human seat
constexpr std::uint32_t kTickCounter = 0x0056e31c;
constexpr std::uint32_t kVtCompetition = 0x00518738;
constexpr std::uint32_t kVtParticipantDead = 0x0051873c;
constexpr std::uint32_t kVtJudgeDead = 0x00518740;

}  // namespace goals

class GoalScreens final : public ScreenKit {
public:
    GoalScreens(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters), comp_(memory, services), career_(memory, services, skaters) {}

    // ---- the competition object (competition.cpp) ----
    std::uint32_t comp_construct_global();                                                           // 0x004195b0
    void comp_init_if_active(std::uint32_t self);                                                    // 0x004195e0
    void comp_init(std::uint32_t self);                                                              // 0x00419600
    void comp_destroy_medals(std::uint32_t self);                                                    // 0x00419f10
    void comp_win_lose_logic(std::uint32_t self, std::uint32_t result, std::uint32_t pressed);       // 0x0041a040
    void comp_end_game_logic(std::uint32_t self, std::uint32_t result, std::uint32_t pressed);       // 0x0041a610
    std::uint32_t comp_construct(std::uint32_t self);                                                // 0x0041ad40
    std::uint32_t comp_delete(std::uint32_t self, std::uint32_t flag);                               // 0x0041add0
    std::uint32_t comp_participant_destroy(std::uint32_t self);                                      // 0x0041adf0
    std::uint32_t comp_participant_construct(std::uint32_t self);                                    // 0x0041ae00, 0x0041ae20
    std::uint32_t comp_judge_destroy(std::uint32_t self);                                            // 0x0041ae10
    std::uint32_t comp_destroy(std::uint32_t self);                                                  // 0x0041ae30
    std::uint32_t comp_participant_delete(std::uint32_t self, std::uint32_t flag);                   // 0x0041aea0
    std::uint32_t comp_judge_delete(std::uint32_t self, std::uint32_t flag);                         // 0x0041aec0
    void comp_reset_marquee(std::uint32_t self);                                                     // 0x0041b540

    // ---- the competition's displays ----
    void comp_win_lose_display(std::uint32_t self, std::int32_t medal);                              // 0x00419da0
    void comp_medals_display(std::uint32_t self);                                                    // 0x0041a270
    void comp_results_display(std::uint32_t self);                                                   // 0x0041a780
    void comp_marquee_display(std::uint32_t self);                                                   // 0x0041af40
    void comp_end_game_display(std::uint32_t self);                                                  // 0x0041b590
    void comp_start_display(std::uint32_t self);                                                     // 0x0041b8b0
    void comp_prize_money_display(std::uint32_t self, std::int32_t cash);                            // 0x0041ba50

    // ---- medals.cpp, tapes.cpp ----
    std::uint32_t medal_create(std::int32_t type, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t scale, std::int32_t speed);   // 0x0046e550
    std::uint32_t tape_construct(std::uint32_t self, std::uint32_t type, std::uint32_t position, std::uint32_t target, std::int32_t scale, std::uint32_t arg5,
                                 std::uint32_t colour, std::uint32_t arg7, std::uint32_t flag);                                               // 0x004b8fc0

    // ---- the level information window's methods (videowin.cpp; its constructor is OptionsScreen::video_construct) ----
    std::uint32_t video_delete(std::uint32_t self, std::uint32_t flag);                              // 0x0045b3a0
    void video_destroy(std::uint32_t self);                                                          // 0x0045b3c0
    void video_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                             // 0x0045b470
    void video_update(std::uint32_t self);                                                           // 0x0045b750
    void video_set_next(std::uint32_t self, std::int32_t level);                                     // 0x0045b890
    bool video_level_available(std::int32_t level, std::int32_t mode);                               // 0x0045b8a0

    // ---- the level picture window (goalwin.cpp) ----
    std::uint32_t levelpic_construct(std::uint32_t self, std::int32_t level, std::uint32_t mode, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                     std::int32_t start_y);                                           // 0x0045b950
    std::uint32_t levelpic_delete(std::uint32_t self, std::uint32_t flag);                           // 0x0045bd30
    void levelpic_destroy(std::uint32_t self);                                                       // 0x0045bd50
    void levelpic_update(std::uint32_t self);                                                        // 0x0045be00
    void levelpic_lock_text(std::uint32_t self);                                                     // 0x0045c050
    void levelpic_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                          // 0x0045c1e0
    void levelpic_select(std::uint32_t self, std::int32_t level, std::int32_t speed);                // 0x0045c360

    // ---- the TRG type 500 node's record ----
    std::uint32_t light_record_construct(std::uint32_t self, std::uint32_t position, std::uint32_t word, std::uint32_t reach, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                         std::uint32_t b7, std::uint32_t b8, std::uint32_t b9, std::uint32_t b10, std::uint32_t b11, std::uint32_t b12);   // 0x0045c390
    std::uint32_t light_record_delete(std::uint32_t self, std::uint32_t flag);                       // 0x0045c420
    void light_record_destroy(std::uint32_t self);                                                   // 0x0045c440
    void light_record_update(std::uint32_t self);                                                    // 0x0045c4a0

    // ---- the light editor ----
    std::uint32_t light_editor_construct(std::uint32_t self, std::uint32_t record, std::uint32_t font, std::int32_t x, std::int32_t y);   // 0x0045c5e0
    std::uint32_t light_editor_delete(std::uint32_t self, std::uint32_t flag);                       // 0x0045cc60
    void light_editor_destroy(std::uint32_t self);                                                   // 0x0045cc80
    void light_editor_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                      // 0x0045cd10

    // ---- the loading screen and the goals checklist ----
    void loading_show(std::uint32_t file, std::uint32_t lines, std::uint32_t mode);                  // 0x0045d010
    void loading_begin_progress(std::int32_t length_frames, std::int32_t y, std::int32_t width, std::int32_t height);   // 0x0045d5e0
    void loading_end_progress();                                                                     // 0x0045d680
    void loading_update_progress();                                                                  // 0x0045d690
    void loading_tick(std::uint32_t flag);                                                           // 0x0045d760
    void loading_copy_frame();                                                                       // 0x0045d7a0
    void loading_start(std::uint32_t mode_flag);                                                     // 0x0045d830
    void goals_run();                                                                                // 0x0045db40

    // ---- the tag game's helpers (tag.cpp) ----
    void tag_show_banners();                                                                         // 0x004b84c0
    void tag_swap_roles();                                                                           // 0x004b8500
    void tag_check_caught();                                                                         // 0x004b8560
    void tag_push_triangle(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2, std::int32_t x3, std::int32_t y3, std::int32_t key);   // 0x004b8610
    void tag_rotated_line(std::int32_t cx, std::int32_t cy, std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2, std::uint32_t angle, std::uint32_t colour);   // 0x004b8720
    void tag_rotated_triangle(std::int32_t cx, std::int32_t cy, std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2, std::int32_t x3, std::int32_t y3,
                              std::uint32_t angle, std::uint32_t colour);                              // 0x004b8800
    void tag_draw_arrow(std::int32_t cx, std::int32_t cy, std::int32_t shaft, std::int32_t length, std::int32_t half_width, std::uint32_t angle, std::uint32_t colour);   // 0x004b8930
    void tag_pointer_to_skater(std::uint32_t skater, std::uint32_t other, std::int32_t cx, std::int32_t cy);   // 0x004b8990
    void tag_draw_pointers();                                                                        // 0x004b8a90
    void tag_versus_update();                                                                        // 0x004b8b40
    void tag_player_box(std::uint32_t skater, std::int32_t x, std::int32_t y, std::uint32_t r, std::uint32_t g, std::uint32_t b, std::int32_t wins, std::int32_t losses,
                        std::int32_t streak, std::int32_t unused, std::int32_t state);              // 0x004b8ca0
    void tag_draw_boxes();                                                                           // 0x004b8ef0
    bool tag_flag_one();                                                                             // 0x004b8fa0
    bool tag_flag_two();                                                                             // 0x004b8fb0

    // The virtual methods of the window-base classes, run natively: offered to the menu toolkit (set_extra_dispatch) and to the window base (set_dispatch, by service
    // name) so that a child of a real class is not reported as a service. The per-frame hook (slot 7) is never dispatched.
    bool dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args);
    bool dispatch_named(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args);

private:
    // the Mess_* text calls the displays make (seams, by retail address)
    void mess_delete_all() { engine(0x00473650); }
    void mess_scale(std::uint32_t scale) { engine(0x004734f0, {scale}); }
    void mess_rgb(std::uint32_t r, std::uint32_t g, std::uint32_t b) { engine(0x00473580, {r, g, b, 0}); }
    void mess_justify(std::uint32_t mode) { engine(0x004734b0, {mode}); }
    // Mess_DrawText(x, y, text, 0): the text is a string in memory (a global text) or a line built in a local (kScratchLine)
    void mess_text(std::int32_t x, std::int32_t y, std::uint32_t text) {
        engine(0x004739e0, {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), text, 0}, m_.cstring(text, 0x100));
    }
    void mess_line(std::int32_t x, std::int32_t y, const std::string& line) {
        engine(0x004739e0, {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), scratch_line(line), 0}, line);
    }
    std::uint32_t text_var(std::uint32_t variable) const { return guest::read_scalar<std::uint32_t>(m_, variable); }    // a global pointer to a string
    std::uint32_t player0() const { return guest::read_scalar<std::uint32_t>(m_, career::kPlayers); }
    void play(std::uint32_t sound) { sfx(sound); }
    void menu_line_yes();                                                                            // CMenu__FindEntry + CMenu__SetLine of the "yes" line
    bool replay_level_flag() const;                                                                  // 0x00419420
    void clear_pads_and_messages();                                                                  // Pad_ClearAll, the triggers, the actuators, Mess_DeleteAll
    void pad_scan();                                                                                 // 0x004e02d0
    // a loading screen's Font object (a stack local in retail): live the services are given the object, the replay the scratch address the probe recorded for it
    std::uint32_t font_at(std::uint32_t font) const { return s_.live() ? font : kScratchLine; }

    competition::Competition comp_;
    career::Career career_;
};

}  // namespace opentony::runtime::frontend
