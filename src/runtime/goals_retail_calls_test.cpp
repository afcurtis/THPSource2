// Replays retail forced calls (Phase 4.10h) through the native goals, game-mode and competition screens (competition.cpp, medals.cpp, tapes.cpp, videowin.cpp, goalwin.cpp, tag.cpp):
//
//   opentony_goals_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the goal... groups) holds the retail memory regions the ported code touches and, for
// every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp.
#include "goals.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    frontend::FontTools fonts;
    frontend::WindowBase window;
    frontend::MenuToolkit menu;
    skater::SkaterManager skaters;
    frontend::GoalScreens goals;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc),
          goals(mem, svc, fonts, window, menu, skaters) {
        // children of the window-base classes run their methods natively
        window.set_dispatch([this](const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args) { return goals.dispatch_named(name, object, args); });
        menu.set_extra_dispatch([this](std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) { return goals.dispatch(object, slot, args); });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using GoalAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.goals

const std::map<std::uint32_t, GoalAdapter>& adapters() {
    static const std::map<std::uint32_t, GoalAdapter> table = {
        // ---- competition.cpp: the object and its logic ----
        {0x004195b0, {"Competition_ConstructGlobal", 0xffffffff, false, A(G.comp_construct_global())}},
        {0x004195e0, {"Competition_InitIfActive", 0, false, V(G.comp_init_if_active(competition::kObject))}},
        {0x00419600, {"Competition_Init", 0, false, V(G.comp_init(self))}},
        {0x00419f10, {"Competition_DestroyMedals", 0, false, V(G.comp_destroy_medals(self))}},
        {0x0041a040, {"Competition_WinLoseLogic", 0, false, V(G.comp_win_lose_logic(self, a[0], a[1]))}},
        {0x0041a610, {"Competition_EndGameLogic", 0, false, V(G.comp_end_game_logic(self, a[0], a[1]))}},
        {0x0041ad40, {"Competition_Constructor", 0xffffffff, false, A(G.comp_construct(self))}},
        {0x0041add0, {"Competition_DeletingDestructor", 0xffffffff, false, A(G.comp_delete(self, a[0]))}},
        {0x0041adf0, {"Competition_ParticipantDestructor", 0xffffffff, false, A(G.comp_participant_destroy(self))}},
        {0x0041ae00, {"Competition_ParticipantConstructor", 0, false, V(G.comp_participant_construct(self))}},
        {0x0041ae10, {"Competition_JudgeDestructor", 0xffffffff, false, A(G.comp_judge_destroy(self))}},
        {0x0041ae20, {"Competition_JudgeConstructor", 0, false, V(G.comp_participant_construct(self))}},
        {0x0041ae30, {"Competition_Destructor", 0, false, V(G.comp_destroy(self))}},
        {0x0041aea0, {"Competition_ParticipantDeletingDestructor", 0xffffffff, false, A(G.comp_participant_delete(self, a[0]))}},
        {0x0041aec0, {"Competition_JudgeDeletingDestructor", 0xffffffff, false, A(G.comp_judge_delete(self, a[0]))}},
        {0x0041b540, {"Competition_ResetMarquee", 0, false, V(G.comp_reset_marquee(self))}},
        // ---- competition.cpp: the displays ----
        {0x00419da0, {"Competition_WinLoseDisplay", 0, false, V(G.comp_win_lose_display(self, S(a[0])))}},
        {0x0041a270, {"Competition_MedalsDisplay", 0, false, V(G.comp_medals_display(self))}},
        {0x0041a780, {"Competition_ResultsDisplay", 0, false, V(G.comp_results_display(self))}},
        {0x0041af40, {"Competition_MarqueeDisplay", 0, false, V(G.comp_marquee_display(self))}},
        {0x0041b590, {"Competition_EndGameDisplay", 0, false, V(G.comp_end_game_display(self))}},
        {0x0041b8b0, {"Competition_StartDisplay", 0, false, V(G.comp_start_display(self))}},
        {0x0041ba50, {"Competition_PrizeMoneyDisplay", 0, false, V(G.comp_prize_money_display(self, S(a[0])))}},
        // ---- medals.cpp, tapes.cpp ----
        {0x0046e550, {"Medal_Create", 0xffffffff, false, A(G.medal_create(S(a[0]), S(a[1]), S(a[2]), S(a[3]), S(a[4]), S(a[5])))}},
        {0x004b8fc0, {"Tape_Construct", 0xffffffff, false, A(G.tape_construct(self, a[0], a[1], a[2], S(a[3]), a[4], a[5], a[6], a[7]))}},
        // ---- videowin.cpp ----
        {0x0045b3a0, {"VideoWindow_ScalarDelete", 0xffffffff, false, A(G.video_delete(self, a[0]))}},
        {0x0045b3c0, {"VideoWindow_Destroy", 0, false, V(G.video_destroy(self))}},
        {0x0045b470, {"VideoWindow_Draw", 0, false, V(G.video_draw(self, S(a[0]), S(a[1])))}},
        {0x0045b750, {"VideoWindow_Update", 0, false, V(G.video_update(self))}},
        {0x0045b890, {"VideoWindow_SetNext", 0, false, V(G.video_set_next(self, S(a[0])))}},
        {0x0045b8a0, {"VideoWindow_LevelAvailable", 0xff, false, A(G.video_level_available(S(a[0]), S(a[1])))}},
        // ---- goalwin.cpp: the level picture window ----
        {0x0045b950, {"LevelPic_Construct", 0xffffffff, false, A(G.levelpic_construct(self, S(a[0]), a[1], S(a[2]), S(a[3]), S(a[4]), S(a[5])))}},
        {0x0045bd30, {"LevelPic_ScalarDelete", 0xffffffff, false, A(G.levelpic_delete(self, a[0]))}},
        {0x0045bd50, {"LevelPic_Destroy", 0, false, V(G.levelpic_destroy(self))}},
        {0x0045be00, {"LevelPic_Update", 0, false, V(G.levelpic_update(self))}},
        {0x0045c050, {"LevelPic_SetLockText", 0, false, V(G.levelpic_lock_text(self))}},
        {0x0045c1e0, {"LevelPic_Draw", 0, false, V(G.levelpic_draw(self, S(a[0]), S(a[1])))}},
        {0x0045c360, {"LevelPic_Select", 0, false, V(G.levelpic_select(self, S(a[0]), S(a[1])))}},
        // ---- the type 500 node's record ----
        {0x0045c390, {"TRG_InitializeExtendedRecord", 0xffffffff, false,
                      A(G.light_record_construct(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]))}},
        {0x0045c420, {"TRG_ExtendedRecord_ScalarDelete", 0xffffffff, false, A(G.light_record_delete(self, a[0]))}},
        {0x0045c440, {"TRG_ExtendedRecord_Destroy", 0, false, V(G.light_record_destroy(self))}},
        {0x0045c4a0, {"TRG_ExtendedRecord_Update", 0, false, V(G.light_record_update(self))}},
        // ---- the light editor ----
        {0x0045c5e0, {"LightEditor_Construct", 0xffffffff, false, A(G.light_editor_construct(self, a[0], a[1], S(a[2]), S(a[3])))}},
        {0x0045cc60, {"LightEditor_ScalarDelete", 0xffffffff, false, A(G.light_editor_delete(self, a[0]))}},
        {0x0045cc80, {"LightEditor_Destroy", 0, false, V(G.light_editor_destroy(self))}},
        {0x0045cd10, {"LightEditor_Draw", 0, false, V(G.light_editor_draw(self, S(a[0]), S(a[1])))}},
        // ---- the loading screen and the goals checklist ----
        {0x0045d010, {"LoadingScreen_Show", 0, false, V(G.loading_show(a[0], a[1], a[2]))}},
        {0x0045d5e0, {"LoadingScreen_BeginProgress", 0, false, V(G.loading_begin_progress(S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x0045d680, {"LoadingScreen_EndProgress", 0, false, V(G.loading_end_progress())}},
        {0x0045d690, {"LoadingScreen_UpdateProgress", 0, false, V(G.loading_update_progress())}},
        {0x0045d760, {"LoadingScreen_Tick", 0, false, V(G.loading_tick(a[0]))}},
        {0x0045d7a0, {"LoadingScreen_CopyFrame", 0, false, V(G.loading_copy_frame())}},
        {0x0045d830, {"LoadingScreen_Start", 0, false, V(G.loading_start(a[0]))}},
        {0x0045db40, {"GoalsScreen_Run", 0, false, V(G.goals_run())}},
        // ---- tag.cpp ----
        {0x004b84c0, {"Tag_ShowBanners", 0, false, V(G.tag_show_banners())}},
        {0x004b8500, {"Tag_SwapRoles", 0, false, V(G.tag_swap_roles())}},
        {0x004b8560, {"Tag_CheckCaught", 0, false, V(G.tag_check_caught())}},
        {0x004b8610, {"Tag_PushTriangle", 0, false, V(G.tag_push_triangle(S(a[0]), S(a[1]), S(a[2]), S(a[3]), S(a[4]), S(a[5]), S(a[6])))}},
        {0x004b8720, {"Tag_RotatedLine", 0, false, V(G.tag_rotated_line(S(a[0]), S(a[1]), S(a[2]), S(a[3]), S(a[4]), S(a[5]), a[6], a[7]))}},
        {0x004b8800, {"Tag_RotatedTriangle", 0, false, V(G.tag_rotated_triangle(S(a[0]), S(a[1]), S(a[2]), S(a[3]), S(a[4]), S(a[5]), S(a[6]), S(a[7]), a[8], a[9]))}},
        {0x004b8930, {"Tag_DrawArrow", 0, false, V(G.tag_draw_arrow(S(a[0]), S(a[1]), S(a[2]), S(a[3]), S(a[4]), a[5], a[6]))}},
        {0x004b8990, {"Tag_PointerToSkater", 0, false, V(G.tag_pointer_to_skater(a[0], a[1], S(a[2]), S(a[3])))}},
        {0x004b8a90, {"Tag_DrawPointers", 0, false, V(G.tag_draw_pointers())}},
        {0x004b8b40, {"Tag_VersusUpdate", 0, false, V(G.tag_versus_update())}},
        {0x004b8ca0, {"Tag_PlayerBox", 0, false, V(G.tag_player_box(a[0], S(a[1]), S(a[2]), a[3], a[4], a[5], S(a[6]), S(a[7]), S(a[8]), S(a[9]), S(a[10])))}},
        {0x004b8ef0, {"Tag_DrawScoreBoxes", 0, false, V(G.tag_draw_boxes())}},
        {0x004b8fa0, {"Tag_PlayerOneFlag", 0xff, false, A(G.tag_flag_one())}},
        {0x004b8fb0, {"Tag_PlayerTwoFlag", 0xff, false, A(G.tag_flag_two())}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "goals retail calls");
}
