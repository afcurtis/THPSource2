// Replays retail forced calls (Phase 4.10f3) through the native career select, handicap and multi-player select screens (carsel.cpp, handicap.cpp, multisel.cpp):
//
//   opentony_select_screens_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the carwin, carloop, han... groups) holds the retail memory regions the
// ported code touches and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp.
#include "career_select.hpp"
#include "handicap.hpp"
#include "level_select.hpp"
#include "multi_select.hpp"
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
    frontend::CareerSelect career;
    frontend::HandicapScreen handicap;
    frontend::MultiSelect multi;
    frontend::LevelSelect level;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc),
          career(mem, svc, fonts, window, menu, skaters), handicap(mem, svc, fonts, window, menu, skaters), multi(mem, svc, fonts, window, menu, skaters),
          level(mem, svc, fonts, window, menu, skaters) {
        // the screen loops are replayed with their window's methods as seams (see ScreenKit::set_loop_seams)
        career.set_loop_seams(true);
        handicap.set_loop_seams(true);
        multi.set_loop_seams(true);
        level.set_loop_seams(true);
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using SelAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define CAR c.career
#define HAN c.handicap
#define MUL c.multi
#define LEV c.level

// The handicap constructors leave the address of a local buffer in the name element: the replay skips those words.
Unspecified handicap_gaps(Ctx& c) { return c.handicap.unspecified(); }

const std::map<std::uint32_t, SelAdapter>& adapters() {
    static const std::map<std::uint32_t, SelAdapter> table = {
        // ---- carsel.cpp: the career select screen ----
        {0x004173d0, {"CareerSelect_InitCallbacks", 0, false, V(CAR.init_callbacks(a[0]))}},
        {0x004173f0, {"CareerSelect_ExitCallback", 0, false, V(CAR.exit_callback())}},
        {0x00417400, {"CareerSelect_ScreenCall", 0, false, V(CAR.screen_call(a[0], a[1]))}},
        {0x00417410, {"CareerSelect_Construct", 0xffffffff, false, A(CAR.construct(self))}},
        {0x00417aa0, {"CareerSelect_ScalarDelete", 0xffffffff, false, A(CAR.scalar_delete(self, a[0]))}},
        {0x00417ac0, {"CareerSelect_Destroy", 0, false, V(CAR.destroy(self))}},
        {0x00417bf0, {"CareerSelect_Draw", 0, false, V(CAR.draw(self, a[0], a[1]))}},
        {0x00417c90, {"CareerSelect_Update", 0, false, V(CAR.update(self))}},
        {0x00417ce0, {"CareerSelect_UpdateSave", 0xffffffff, false, A(CAR.update_save(self))}},
        {0x00417d70, {"CareerSelect_UpdateLoad", 0xffffffff, false, A(CAR.update_load(self))}},
        {0x00417e30, {"CareerSelect_Poll", 0xffffffff, false, A(CAR.poll(self))}},
        {0x00417f90, {"CareerSelect_Run", 0xffffffff, false, A(CAR.run())}},
        {0x00418140, {"Front_KeyTable", 0xffffffff, false, A(CAR.key_table())}},
        {0x00418150, {"Front_UpdateMenuSound", 0, false, V(CAR.update_menu_sound())}},
        // ---- handicap.cpp: the handicap screen ----
        {0x00454e80, {"Handicap_InitCallbacks", 0, false, V(HAN.init_callbacks(a[0]))}},
        {0x00454ea0, {"Handicap_ExitCallback", 0, false, V(HAN.exit_callback())}},
        {0x00454eb0, {"Handicap_ScreenCall", 0, false, V(HAN.screen_call(a[0], a[1]))}},
        {0x00454ec0, {"HandicapPlayer_Construct", 0xffffffff, false,
                      A((HAN.reset_unspecified(), HAN.panel_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3]), S(a[4])))), handicap_gaps}},
        {0x00455410, {"HandicapPlayer_ScalarDelete", 0xffffffff, false, A(HAN.panel_delete(self, a[0]))}},
        {0x00455430, {"HandicapPlayer_Destroy", 0, false, V(HAN.panel_destroy(self))}},
        {0x004554f0, {"HandicapPlayer_Poll", 0xff, false, A(HAN.panel_poll(self))}},
        {0x00455590, {"HandicapPlayer_Update", 0, false, V(HAN.panel_update(self))}},
        {0x00455690, {"HandicapPlayer_Draw", 0, false, V(HAN.panel_draw(self, S(a[0]), S(a[1])))}},
        {0x004558c0, {"HandicapWindow_Construct", 0xffffffff, false, A((HAN.reset_unspecified(), HAN.window_construct(self))), handicap_gaps}},
        {0x00455d20, {"HandicapWindow_ScalarDelete", 0xffffffff, false, A(HAN.window_delete(self, a[0]))}},
        {0x00455d40, {"HandicapWindow_Destroy", 0, false, V(HAN.window_destroy(self))}},
        {0x00455e10, {"HandicapWindow_Draw", 0, false, V(HAN.window_draw(self, a[0], a[1]))}},
        {0x00455e90, {"HandicapWindow_Step", 0xffffffff, false, A(HAN.window_step(self))}},
        {0x00455f00, {"Handicap_Run", 0xffffffff, false, A(HAN.run())}},
        // ---- multisel.cpp: the multi-player select screen ----
        {0x0047ba30, {"MultiSelect_InitCallbacks", 0, false, V(MUL.init_callbacks(a[0]))}},
        {0x0047ba50, {"MultiSelect_ExitCallback", 0, false, V(MUL.exit_callback())}},
        {0x0047ba60, {"MultiSelect_ScreenCall", 0, false, V(MUL.screen_call(a[0], a[1]))}},
        {0x0047ba70, {"MultiSelect_Construct", 0xffffffff, false, A(MUL.construct(self))}},
        {0x0047c290, {"MultiSelect_ScalarDelete", 0xffffffff, false, A(MUL.scalar_delete(self, a[0]))}},
        {0x0047c2b0, {"MultiSelect_Destroy", 0, false, V(MUL.destroy(self))}},
        {0x0047c3e0, {"MultiSelect_Draw", 0, false, V(MUL.draw(self, a[0], a[1]))}},
        {0x0047c460, {"MultiSelect_Update", 0, false, V(MUL.update(self))}},
        {0x0047c4a0, {"MultiSelect_Poll", 0xffffffff, false, A(MUL.poll())}},
        {0x0047c550, {"MultiSelect_SelectedId", 0xffffffff, false, A(MUL.selected_id(self))}},
        {0x0047c560, {"MultiSelect_Run", 0xffffffff, false, A(MUL.run())}},
        // ---- levelsel.cpp: the level select screen ----
        {0x004588c0, {"LevelSelect_InitCallbacks", 0, false, V(LEV.init_callbacks(a[0]))}},
        {0x004588e0, {"LevelSelect_ExitCallback", 0, false, V(LEV.exit_callback())}},
        {0x004588f0, {"LevelSelect_ScreenCall", 0, false, V(LEV.screen_call(a[0], a[1]))}},
        {0x00458900, {"LevelSelect_Main", 0xffffffff, false, A(LEV.run())}},
        {0x00458b90, {"LevelSelect_Construct", 0xffffffff, false, A(LEV.construct(self))}},
        {0x004594e0, {"LevelSelect_ScalarDelete", 0xffffffff, false, A(LEV.scalar_delete(self, a[0]))}},
        {0x00459500, {"LevelSelect_Destroy", 0, false, V(LEV.destroy(self))}},
        {0x004595e0, {"LevelSelect_Draw", 0, false, V(LEV.draw(self))}},
        {0x00459650, {"LevelSelect_Update", 0, false, V(LEV.update(self))}},
        {0x00459be0, {"LevelSelect_ChangeLevel", 0, false, V(LEV.change_level(self, S(a[0])))}},
        {0x00459d90, {"LevelSelect_Step", 0xff, false, A(LEV.step(self, a[0]))}},
        {0x00459db0, {"LevelBanner_Construct", 0xffffffff, false, A(LEV.banner_construct(self, S(a[0]) & 0xff, S(a[1]), S(a[2]), S(a[3]), S(a[4])))}},
        {0x0045a0a0, {"LevelBanner_ScalarDelete", 0xffffffff, false, A(LEV.banner_delete(self, a[0]))}},
        {0x0045a0c0, {"LevelBanner_Destroy", 0, false, V(LEV.banner_destroy(self))}},
        {0x0045a120, {"Level_CopyNameWord", 0xffffffff, false, A(LEV.copy_name_word(a[0], a[1], (a[2] & 0xff) != 0))}},
        {0x0045a1b0, {"LevelGlobe_Construct", 0xffffffff, false, A(LEV.globe_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x0045a3e0, {"LevelGlobe_ScalarDelete", 0xffffffff, false, A(LEV.globe_delete(self, a[0]))}},
        {0x0045a400, {"LevelGlobe_Destroy", 0, false, V(LEV.globe_destroy(self))}},
        {0x0045a490, {"LevelGlobe_Update", 0, false, V(LEV.globe_update(self))}},
        {0x0045a5a0, {"LevelGlobe_Draw", 0, false, V(LEV.globe_draw(self, S(a[0]), S(a[1])))}},
        {0x0045a6b0, {"LevelGlobe_SetLevel", 0, false, V(LEV.globe_set_level(self, S(a[0])))}},
        {0x0045a6e0, {"LevelName_Construct", 0xffffffff, false, A(LEV.name_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x0045a920, {"LevelName_ScalarDelete", 0xffffffff, false, A(LEV.name_delete(self, a[0]))}},
        {0x0045a940, {"LevelName_Destroy", 0, false, V(LEV.name_destroy(self))}},
        {0x0045a9a0, {"LevelName_SetLevel", 0, false, V(LEV.name_set_level(self, S(a[0])))}},
        {0x0045aa40, {"LevelName_Update", 0, false, V(LEV.name_update(self))}},
        {0x0045ab50, {"LevelName_Refresh", 0, false, V(LEV.name_refresh(self))}},
        {0x0045abc0, {"ModeMenu_Construct", 0xffffffff, false, A(LEV.menu_construct(self, a[0], S(a[1]), S(a[2])))}},
        {0x0045aeb0, {"ModeMenu_ScalarDelete", 0xffffffff, false, A(LEV.menu_delete(self, a[0]))}},
        {0x0045aed0, {"ModeMenu_Destroy", 0, false, V(LEV.menu_destroy(self))}},
        {0x0045af60, {"ModeMenu_HighlightPlay", 0, false, V(LEV.menu_highlight_play(self))}},
        {0x0045af70, {"ModeMenu_Draw", 0, false, V(LEV.menu_draw(self, S(a[0]), S(a[1])))}},
        {0x0045b080, {"ModeMenu_Result", 0xffffffff, false, A(LEV.menu_result(self, a[0]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "select screens retail calls");
}
