// Replays retail forced calls (Phase 4.10g) through the native options screen (options.cpp, videowin.cpp, vidmenu.cpp):
//
//   opentony_options_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the opt... groups) holds the retail memory regions the ported code touches and, for
// every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp.
#include "options.hpp"
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
    frontend::OptionsScreen options;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc),
          options(mem, svc, fonts, window, menu, skaters) {
        // the screen loop is replayed with its window's methods as seams (see ScreenKit::set_loop_seams)
        options.set_loop_seams(true);
        // children of the window-base classes run their methods natively
        window.set_dispatch([this](const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args) { return options.dispatch_named(name, object, args); });
        menu.set_extra_dispatch([this](std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) { return options.dispatch(object, slot, args); });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using OptAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. B(expr): it returns a byte.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define OPT c.options

const std::map<std::uint32_t, OptAdapter>& adapters() {
    static const std::map<std::uint32_t, OptAdapter> table = {
        // ---- the options window ----
        {0x00481200, {"Options_ScreenCall", 0, false, V(OPT.screen_call(a[0], a[1]))}},
        {0x00481210, {"OptionsMenu_Construct", 0xffffffff, false, A(OPT.menu_construct(self))}},
        {0x00482450, {"Options_CheatsAvailable", 0xff, false, A(OPT.cheats_available())}},
        {0x00482460, {"OptionsMenu_ScalarDelete", 0xffffffff, false, A(OPT.menu_delete(self, a[0]))}},
        {0x00482480, {"OptionsMenu_Destroy", 0, false, V(OPT.menu_destroy(self))}},
        {0x004826f0, {"OptionsMenu_SetPrompts", 0, false, V(OPT.set_prompts(self, a[0], a[1]))}},
        {0x00482770, {"OptionsMenu_Sync", 0, false, V(OPT.sync(self))}},
        {0x00482870, {"OptionsMenu_Apply", 0, false, V(OPT.apply(self))}},
        {0x00482950, {"OptionsMenu_Draw", 0, false, V(OPT.menu_draw(self, S(a[0]), S(a[1])))}},
        {0x00482a40, {"OptionsMenu_Update", 0, false, V(OPT.menu_update(self))}},
        {0x00482e60, {"OptionsMenu_Step", 0xffffffff, false, A(OPT.menu_step(self))}},
        {0x00483ef0, {"Options_Run", 0xffffffff, false, A(OPT.run())}},
        // ---- the level information window ----
        {0x0045b0a0, {"VideoWindow_Construct", 0xffffffff, false, A(OPT.video_construct(self, a[0], a[1], S(a[2]), S(a[3]), S(a[4]), S(a[5])))}},
        // ---- the gap checklist ----
        {0x004840b0, {"GapChecklist_Construct", 0xffffffff, false, A(OPT.gaps_construct(self, a[0], S(a[1]), S(a[2]), S(a[3]), S(a[4])))}},
        {0x00484ab0, {"GapChecklist_LevelLocked", 0xff, false, A(OPT.gaps_locked(S(a[0])))}},
        {0x00484af0, {"GapChecklist_ScalarDelete", 0xffffffff, false, A(OPT.gaps_delete(self, a[0]))}},
        {0x00484b10, {"GapChecklist_Destroy", 0, false, V(OPT.gaps_destroy(self))}},
        {0x00484c10, {"GapChecklist_Update", 0, false, V(OPT.gaps_update(self))}},
        {0x004850a0, {"GapChecklist_StripBrackets", 0, false, V(OPT.gaps_strip_brackets(a[0]))}},
        {0x004850f0, {"GapChecklist_Draw", 0, false, V(OPT.gaps_draw(self, S(a[0]), S(a[1])))}},
        {0x00485290, {"GapChecklist_SortRows", 0, false, V(OPT.gaps_sort(self, S(a[0]), S(a[1])))}},
        {0x004853b0, {"GapChecklist_SortAll", 0, false, V(OPT.gaps_sort_all(self))}},
        // ---- the high scores ----
        {0x00485400, {"HighScores_Construct", 0xffffffff, false, A(OPT.scores_construct(self, a[0], S(a[1]), S(a[2]), S(a[3]), S(a[4])))}},
        {0x00485c90, {"HighScores_ScalarDelete", 0xffffffff, false, A(OPT.scores_delete(self, a[0]))}},
        {0x00485cb0, {"HighScores_Destroy", 0, false, V(OPT.scores_destroy(self))}},
        {0x00485d80, {"HighScores_Update", 0, false, V(OPT.scores_update(self))}},
        {0x00485f90, {"HighScores_Draw", 0, false, V(OPT.scores_draw(self, S(a[0]), S(a[1])))}},
        // ---- the movie list ----
        {0x004860d0, {"MovieList_Construct", 0xffffffff, false, A(OPT.movie_construct(self, a[0], a[5], a[6]))}},
        {0x004866e0, {"MovieList_ScalarDelete", 0xffffffff, false, A(OPT.movie_delete(self, a[0]))}},
        {0x00486700, {"MovieList_Destroy", 0, false, V(OPT.movie_destroy(self))}},
        {0x00486790, {"MovieList_Update", 0, false, V(OPT.movie_update(self))}},
        {0x004867a0, {"MovieList_Draw", 0, false, V(OPT.movie_draw(self, S(a[0]), S(a[1])))}},
        {0x00486830, {"MovieList_Step", 0xffffffff, false, A(OPT.movie_step(self))}},
        {0x00486910, {"MovieList_Unlocked", 0xff, false, A(OPT.movie_unlocked(a[0], S(a[1])))}},
        // ---- the sound levels page ----
        {0x00486b10, {"SoundLevels_Construct", 0xffffffff, false, A(OPT.sound_construct(self, a[0], a[5], a[6]))}},
        {0x00486e80, {"SoundLevels_ScalarDelete", 0xffffffff, false, A(OPT.sound_delete(self, a[0]))}},
        {0x00486ea0, {"SoundLevels_Destroy", 0, false, V(OPT.sound_destroy(self))}},
        {0x00486f80, {"SoundLevels_Update", 0, false, V(OPT.sound_update(self))}},
        {0x00487130, {"SoundLevels_Draw", 0, false, V(OPT.sound_draw(self, S(a[0]), S(a[1])))}},
        {0x004871e0, {"SoundLevels_Step", 0xffffffff, false, A(OPT.sound_step(self))}},
        {0x00487280, {"SoundLevels_HasSelection", 0xff, false, A(OPT.sound_has_selection(self))}},
        {0x004872a0, {"SoundLevels_NoSelection", 0xff, false, A(OPT.sound_no_selection(self))}},
        // ---- the memory card and replay pages ----
        {0x004872b0, {"CardPage_Construct", 0xffffffff, false, A(OPT.card_construct(self, a[0]))}},
        {0x00487330, {"CardPage_ScalarDelete", 0xffffffff, false, A(OPT.card_delete(self, a[0]))}},
        {0x00487350, {"CardPage_Destroy", 0, false, V(OPT.card_destroy(self))}},
        {0x00487360, {"CardPage_Update", 0, false, V(OPT.card_update(self))}},
        {0x00487370, {"CardPage_Draw", 0, false, V(OPT.card_draw(self, S(a[0]), S(a[1])))}},
        {0x00487390, {"CardPage_Step", 0xffffffff, false, A(OPT.card_step())}},
        {0x004873b0, {"ReplayPage_Construct", 0xffffffff, false, A(OPT.replay_construct(self))}},
        {0x00487410, {"ReplayPage_ScalarDelete", 0xffffffff, false, A(OPT.replay_delete(self, a[0]))}},
        {0x00487430, {"ReplayPage_Destroy", 0, false, V(OPT.replay_destroy(self))}},
        {0x00487440, {"ReplayPage_Update", 0, false, V(OPT.card_update(self))}},
        {0x00487450, {"ReplayPage_Draw", 0, false, V(OPT.card_draw(self, S(a[0]), S(a[1])))}},
        {0x00487470, {"ReplayPage_Step", 0xffffffff, false, A(OPT.card_step())}},
        // ---- the controller page ----
        {0x00487490, {"ControlsMenu_Construct", 0xffffffff, false, A(OPT.controls_construct(self, a[0], a[1], a[6], a[7]))}},
        {0x00487c10, {"ControlsMenu_ScalarDelete", 0xffffffff, false, A(OPT.controls_delete(self, a[0]))}},
        {0x00487c30, {"ControlsMenu_Destroy", 0, false, V(OPT.controls_destroy(self))}},
        {0x00487dc0, {"ControlsMenu_Update", 0, false, V(OPT.controls_update(self))}},
        {0x00487dd0, {"ControlsMenu_Draw", 0, false, V(OPT.controls_draw(self, S(a[0]), S(a[1])))}},
        {0x00487f60, {"ControlsMenu_ControlText", 0, false, V(OPT.controls_text(a[0], a[1], a[2]))}},
        {0x00488160, {"ControlsMenu_Step", 0xffffffff, false, A(OPT.controls_step(self))}},
        {0x00488410, {"ControlsMenu_WrapValue", 0, false, V(OPT.controls_clamp(a[0], S(a[1]), S(a[2])))}},
        {0x00488480, {"ControlsMenu_ValueUnique", 0xff, false, A(OPT.controls_unique(self, a[0]))}},
        {0x004884c0, {"ControlsMenu_FixDuplicates", 0, false, V(OPT.controls_fix(self))}},
        {0x00488590, {"ControlsMenu_Selectable", 0xff, false, A(OPT.controls_selectable(self))}},
        {0x004885c0, {"ControlsMenu_NotSelectable", 0xff, false, A(!OPT.controls_selectable(self))}},
        // ---- the cheats page ----
        {0x004885d0, {"CheatsPage_Construct", 0xffffffff, false, A(OPT.cheats_construct(self, a[0], a[5], a[6]))}},
        {0x00488d50, {"CheatsPage_ScalarDelete", 0xffffffff, false, A(OPT.cheats_delete(self, a[0]))}},
        {0x00488d70, {"CheatsPage_Destroy", 0, false, V(OPT.cheats_destroy(self))}},
        {0x00488e30, {"CheatsPage_Update", 0, false, V(OPT.cheats_update(self))}},
        {0x00488e40, {"CheatsPage_Draw", 0, false, V(OPT.cheats_draw(self, S(a[0]), S(a[1])))}},
        {0x004891a0, {"CheatsPage_Step", 0xffffffff, false, A(OPT.cheats_step(self))}},
        {0x00489240, {"CheatsPage_Refresh", 0, false, V(OPT.cheats_refresh(self))}},
        {0x004892a0, {"CheatsPage_Store", 0, false, V(OPT.cheats_store(self))}},
        {0x00489350, {"CheatsPage_Switchable", 0xff, false, A(OPT.cheats_switchable(self))}},
        {0x00489390, {"CheatsPage_OnBack", 0xff, false, A(OPT.cheats_on_back(self))}},
        // ---- the video menu ----
        {0x004cea70, {"VideoMenu_Destroy", 0, false, V(OPT.video_menu_destroy(self))}},
        {0x004cdf60, {"VideoMenu_InitStateThunk", 0, false, V(OPT.video_menu_init_state())}},
        {0x004cdf70, {"VideoMenu_InitState", 0, false, V(OPT.video_menu_init_state())}},
        {0x004cdfa0, {"VideoMenu_InitOptionsThunk", 0, false, V(OPT.video_menu_init_options())}},
        {0x004cdfb0, {"VideoMenu_InitOptions", 0, false, V(OPT.video_menu_init_options())}},
        {0x004ce020, {"VideoMenu_RunInto", 0, false, V(OPT.video_menu_run_into(a[1]))}},
        {0x004ce030, {"VideoMenu_Run", 0xffffffff, false, A(OPT.video_menu_run())}},
        {0x004ce260, {"VideoMenu_ApplyGamma", 0, false, V(OPT.video_menu_apply_gamma())}},
        {0x004ce280, {"VideoMenu_NoOp", 0, false, V((void)0)}},
        {0x004ce290, {"VideoMenu_IsDone", 0xffffffff, false, A(OPT.video_menu_is_done(self))}},
        {0x004ce2a0, {"VideoMenu_Construct", 0xffffffff, false, A(OPT.video_menu_construct(self))}},
        {0x004cea50, {"VideoMenu_ScalarDelete", 0xffffffff, false, A(OPT.video_menu_delete(self, a[0]))}},
        // ---- the stubs: functions that return at once (zero or nothing) ----
        {0x00485280, {"Options_ReturnZeroA", 0xffffffff, false, A(0)}},
        {0x004860c0, {"Options_ReturnZeroB", 0xffffffff, false, A(0)}},
        {0x004893b0, {"Options_ReturnZeroC", 0xffffffff, false, A(0)}},
        {0x004893c0, {"Options_NothingA", 0, false, V((void)0)}},
        {0x004893d0, {"Options_ReturnZeroD", 0xffffffff, false, A(0)}},
        {0x004893e0, {"Options_ReturnZeroE", 0xffffffff, false, A(0)}},
        {0x004893f0, {"Options_ReturnZeroF", 0xffffffff, false, A(0)}},
        {0x00489400, {"Options_NothingB", 0, false, V((void)0)}},
        {0x00489410, {"Options_NothingC", 0, false, V((void)0)}},
        {0x00489420, {"Options_NothingD", 0, false, V((void)0)}},
        {0x00489430, {"Options_NothingE", 0, false, V((void)0)}},
        {0x00489440, {"Options_ReturnZeroG", 0xffffffff, false, A(0)}},
        {0x00489450, {"Options_NothingF", 0, false, V((void)0)}},
        {0x00489460, {"Options_ReturnZeroH", 0xffffffff, false, A(0)}},
        {0x00489470, {"Options_ReturnZeroI", 0xffffffff, false, A(0)}},
        {0x00489480, {"Options_NothingG", 0, false, V((void)0)}},
        {0x00489490, {"Options_NothingH", 0, false, V((void)0)}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "options retail calls");
}
