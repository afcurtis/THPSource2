// Replays retail forced calls (Phase 4.10e1) through the native create-a-skater screens: the help text box and confirm box (create_ui.cpp), the custom
// skater editor and its personal page (creedit.cpp), the save-skater screen (cresave.cpp), the stat editor (statedit.cpp) and the stat window's
// constructor and destructor (statwin.cpp):
//
//   opentony_create_screens_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the crui, crsave, crstat and credit groups) holds the retail
// memory regions the ported code touches and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp.
#include "create_color.hpp"
#include "create_screens.hpp"
#include "create_spool.hpp"
#include "menu_toolkit.hpp"
#include "retail_calls_replay.hpp"
#include "skater_manager.hpp"
#include "skater_window_helpers.hpp"

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
    create::CreateColor color;
    create::CreateSpool spool;
    frontend::SkaterWindowHelpers helpers;
    frontend::CreateScreens screens;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc), color(mem, svc),
          spool(mem, svc), helpers(mem, svc, fonts, window, menu, color), screens(mem, svc, fonts, window, menu, skaters, spool, helpers) {
        // the screen loops are replayed with their window's step, update, draw and delete as seams (see CreateScreens::set_loop_seams)
        screens.set_loop_seams(true);
        // the personal page's list holds NumberRows (the 4.10d helpers): the toolkit offers their virtual calls to the helpers first
        menu.set_extra_dispatch([this](std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) {
            return helpers.dispatch(object, slot, args);
        });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using CrAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define R c.screens

const std::map<std::uint32_t, CrAdapter>& adapters() {
    static const std::map<std::uint32_t, CrAdapter> table = {
        // ---- create_ui.cpp ----
        {0x00425a40, {"TextBox_Construct", 0xffffffff, false, A(R.box_construct(self, a[0], a[1], S(a[2]), S(a[3])))}},
        {0x00425b40, {"TextBox_ScalarDelete", 0xffffffff, false, A(R.box_delete(self, a[0]))}},
        {0x00425b60, {"TextBox_Destroy", 0, false, V(R.box_destroy(self))}},
        {0x00425b70, {"TextBox_Draw", 0, false, V(R.box_draw(self))}},
        {0x00425fa0, {"ConfirmBox_Construct", 0xffffffff, false, A(R.confirm_construct(self, a[0], a[1], S(a[2]), S(a[3])))}},
        {0x004261f0, {"ConfirmBox_ScalarDelete", 0xffffffff, false, A(R.confirm_delete(self, a[0]))}},
        {0x00426210, {"ConfirmBox_Destroy", 0, false, V(R.confirm_destroy(self))}},
        {0x004262a0, {"ConfirmBox_Highlight", 0, false, V(R.confirm_highlight(self, a[0]))}},
        {0x004262c0, {"ConfirmBox_Draw", 0, false, V(R.confirm_draw(self))}},
        {0x00426300, {"ConfirmBox_Update", 0, false, V(R.confirm_update(self))}},
        {0x00426310, {"ConfirmBox_Poll", 0xffffffff, false, A(R.confirm_poll(self, S(a[0]), S(a[1])))}},
        // ---- creedit.cpp ----
        {0x004271a0, {"CreateEditor_ScreenCall", 0, false, V(R.editor_screen_call(a[0], a[1]))}},
        {0x004271c0, {"CreateEditor_Construct", 0xffffffff, false, A(R.editor_construct(self, a[0], a[1]))}},
        {0x004277e0, {"CreateEditor_ScalarDelete", 0xffffffff, false, A(R.editor_delete(self, a[0]))}},
        {0x00427800, {"CreateEditor_Destroy", 0, false, V(R.editor_destroy(self))}},
        {0x00427920, {"CreateEditor_SetupScene", 0, false, V(R.editor_setup_scene(self))}},
        {0x00427be0, {"CreateEditor_LoadFonts", 0, false, V(R.editor_load_fonts(self))}},
        {0x00427dc0, {"CreateEditor_SetPage", 0, false, V(R.editor_set_page(self, S(a[0])))}},
        {0x00427e40, {"CreateEditor_UpdateFrame", 0, false, V(R.editor_update_frame(self))}},
        {0x004280d0, {"CreateEditor_ChangeBuild", 0, false, V(R.editor_change_build(self))}},
        {0x004281e0, {"CreateEditor_Draw", 0, false, V(R.editor_draw(self, S(a[0]), S(a[1])))}},
        {0x004282d0, {"CreateEditor_Update", 0xffffffff, false, A(R.editor_update(self))}},
        {0x004283f0, {"CreateEditor_ShowButtons", 0, false, V(R.editor_show_buttons(self, a[0]))}},
        {0x00428420, {"CreateEditor_Run", 0xffffffff, false, A(R.editor_run(a[0], a[1]))}},
        {0x004288a0, {"PersonalPage_Construct", 0xffffffff, false, A(R.personal_construct(self, S(a[0]), S(a[1])))}},
        {0x00428c80, {"PersonalPage_Delete", 0xffffffff, false, A(R.personal_delete(self, a[0]))}},
        {0x00428ca0, {"PersonalPage_BuildList", 0, false, V(R.personal_build_list(self))}},
        {0x004292d0, {"PersonalPage_Destroy", 0, false, V(R.personal_destroy(self))}},
        {0x0042b4c0, {"AppearancePage_HighlightFirst", 0, false, V(R.appearance_highlight_first(self))}},
        // ---- cresave.cpp ----
        {0x0042be40, {"CreateSave_InitCallbacks", 0, false, V(R.save_init_callbacks(a[0]))}},
        {0x0042be60, {"CreateSave_ExitCallback", 0, false, V(R.save_exit_callback())}},
        {0x0042be70, {"CreateSave_ScreenCall", 0, false, V(R.save_screen_call(a[0], a[1]))}},
        {0x0042be80, {"CreateSave_Construct", 0xffffffff, false, A(R.save_construct(self))}},
        {0x0042c6f0, {"CreateSave_ScalarDelete", 0xffffffff, false, A(R.save_delete(self, a[0]))}},
        {0x0042c710, {"CreateSave_Destroy", 0, false, V(R.save_destroy(self))}},
        {0x0042c860, {"CreateSave_Draw", 0, false, V(R.save_draw(self, S(a[0]), S(a[1])))}},
        {0x0042c910, {"CreateSave_Update", 0, false, V(R.save_update(self))}},
        {0x0042c940, {"CreateSave_Step", 0xffffffff, false, A(R.save_step(self))}},
        {0x0042cb50, {"CreateSave_DrawList", 0, false, V(R.save_draw_list(self))}},
        {0x0042cb60, {"CreateSave_DrawBox", 0, false, V(R.save_draw_box(self))}},
        {0x0042cb70, {"CreateSave_MmuScreenThunk", 0, false, V(R.save_mmu_thunk())}},
        {0x0042cb80, {"CreateSave_Run", 0xffffffff, false, A(R.save_run())}},
        // ---- cresel.cpp ----
        {0x0042cd20, {"CreateSelect_InitCallbacks", 0, false, V(R.select_init_callbacks(a[0]))}},
        {0x0042cd40, {"CreateSelect_ExitCallback", 0, false, V(R.select_exit_callback())}},
        {0x0042cd50, {"CreateSelect_ScreenCall", 0, false, V(R.select_screen_call(a[0], a[1]))}},
        {0x0042cd60, {"CreateSelect_Construct", 0xffffffff, false, A(R.select_construct(self))}},
        {0x0042dda0, {"CreateSelect_ScalarDelete", 0xffffffff, false, A(R.select_delete(self, a[0]))}},
        {0x0042ddc0, {"CreateSelect_Destroy", 0, false, V(R.select_destroy(self))}},
        {0x0042df60, {"CreateSelect_Draw", 0, false, V(R.select_draw(self, S(a[0]), S(a[1])))}},
        {0x0042e180, {"CreateSelect_UpdateFrame", 0, false, V(R.select_update_frame(self))}},
        {0x0042e7d0, {"CreateSelect_UpdateLoad", 0xffffffff, false, A(R.select_update_load(self))}},
        {0x0042e860, {"CreateSelect_UpdateSave", 0xffffffff, false, A(R.select_update_save(self))}},
        {0x0042e9c0, {"CreateSelect_Update", 0xffffffff, false, A(R.select_update(self))}},
        {0x0042f100, {"CreateSelect_Run", 0xffffffff, false, A(R.select_run())}},
        // ---- statedit.cpp and the stat window ----
        {0x004b5ba0, {"StatEdit_ScreenCall", 0, false, V(R.stat_screen_call(a[0], a[1]))}},
        {0x004b5bb0, {"StatPage_Construct", 0xffffffff, false, A(R.stat_page_construct(self, S(a[0]), S(a[1])))}},
        {0x004b6060, {"StatPage_ScalarDelete", 0xffffffff, false, A(R.stat_page_delete(self, a[0]))}},
        {0x004b6080, {"StatPage_Destroy", 0, false, V(R.stat_page_destroy(self))}},
        {0x004b7430, {"StatEdit_Construct", 0xffffffff, false, A(R.stat_construct(self))}},
        {0x004b77b0, {"StatEdit_ScalarDelete", 0xffffffff, false, A(R.stat_delete(self, a[0]))}},
        {0x004b77d0, {"StatEdit_Destroy", 0, false, V(R.stat_destroy(self))}},
        {0x004b7960, {"StatEdit_LoadFonts", 0, false, V(R.stat_load_fonts(self))}},
        {0x004b7b50, {"StatEdit_SetupScene", 0, false, V(R.stat_setup_scene(self))}},
        {0x004b7f40, {"StatEdit_Draw", 0, false, V(R.stat_draw(self, S(a[0]), S(a[1])))}},
        {0x004b8020, {"StatEdit_UpdateSkater", 0, false, V(R.stat_update_skater(self))}},
        {0x004b8080, {"StatEdit_Step", 0xffffffff, false, A(R.stat_step(self))}},
        {0x004b8090, {"StatEdit_Run", 0xffffffff, false, A(R.stat_run())}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "create screen retail calls");
}
