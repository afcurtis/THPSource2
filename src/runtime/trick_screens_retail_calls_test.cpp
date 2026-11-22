// Replays retail forced calls (Phase 4.10e2) through the native trick-list screen: the screen window and its loop (tricksel.cpp), the trick menu's
// construction, drawing, lists and pad (tricksel.cpp and trickwin.cpp) and the cash panel (trickwin.cpp):
//
//   opentony_trick_screens_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the trkscr and trkloop groups) holds the retail memory regions the
// ported code touches and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp.
#include "create_color.hpp"
#include "create_screens.hpp"
#include "create_spool.hpp"
#include "menu_toolkit.hpp"
#include "retail_calls_replay.hpp"
#include "skater_manager.hpp"
#include "skater_window_helpers.hpp"
#include "trick_screens.hpp"

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
    frontend::CreateScreens create_screens;
    frontend::TrickScreens screens;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc), color(mem, svc), spool(mem, svc),
          helpers(mem, svc, fonts, window, menu, color), create_screens(mem, svc, fonts, window, menu, skaters, spool, helpers),
          screens(mem, svc, fonts, window, menu, skaters, helpers, create_screens) {
        // the screen loop is replayed with its window's step, update, draw and delete as seams (see ScreenKit::set_loop_seams)
        screens.set_loop_seams(true);
        // the rows of the personal page are NumberRows (the 4.10d helpers): the toolkit offers their virtual calls to the helpers first
        menu.set_extra_dispatch([this](std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) {
            return helpers.dispatch(object, slot, args);
        });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using TrickAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define R c.screens

const std::map<std::uint32_t, TrickAdapter>& adapters() {
    static const std::map<std::uint32_t, TrickAdapter> table = {
        // ---- tricksel.cpp: the screen ----
        {0x004bf860, {"TrickScreen_ScreenCall", 0, false, V(R.screen_call(a[0], a[1]))}},
        {0x004bf870, {"TrickScreen_Construct", 0xffffffff, false, A(R.screen_construct(self))}},
        {0x004bfa90, {"TrickScreen_ScalarDelete", 0xffffffff, false, A(R.screen_delete(self, a[0]))}},
        {0x004bfab0, {"TrickScreen_Destroy", 0, false, V(R.screen_destroy(self))}},
        {0x004bfc30, {"TrickScreen_LoadFont", 0, false, V(R.screen_load_font(self))}},
        {0x004bfd30, {"TrickScreen_SetupScene", 0, false, V(R.screen_setup_scene(self))}},
        {0x004c0120, {"TrickScreen_Draw", 0, false, V(R.screen_draw(self, S(a[0]), S(a[1])))}},
        {0x004c0200, {"TrickScreen_UpdateFrame", 0, false, V(R.screen_update_frame(self))}},
        {0x004c0230, {"TrickScreen_Step", 0xffffffff, false, A(R.screen_step(self))}},
        {0x004c0240, {"TrickMenu_Run", 0xffffffff, false, A(R.run())}},
        // ---- the trick menu ----
        {0x004c0430, {"TrickMenu_Construct", 0xffffffff, false, A(R.menu_construct(self, S(a[0]), S(a[1])))}},
        {0x004c0e90, {"TrickMenu_ScalarDelete", 0xffffffff, false, A(R.menu_delete(self, a[0]))}},
        {0x004c0eb0, {"TrickMenu_Destroy", 0, false, V(R.menu_destroy(self))}},
        {0x004c1080, {"TrickMenu_Draw", 0, false, V(R.menu_draw(self, S(a[0]), S(a[1])))}},
        {0x004c13c0, {"TrickMenu_UpdateFrame", 0, false, V(R.menu_update_frame(self))}},
        {0x004c13d0, {"TrickMenu_BuildCategoryList", 0, false, V(R.build_category_list(self))}},
        {0x004c2000, {"TrickMenu_RebuildComboList", 0, false, V(R.rebuild_combo_list(self))}},
        {0x004c21a0, {"TrickMenu_BuildPageList", 0, false, V(R.build_page_list(self))}},
        {0x004c2700, {"TrickMenu_RequirementsMet", 0xff, false, A(R.requirements_met(S(a[0]), a[1]) ? 1 : 0)}},
        {0x004c2800, {"TrickMenu_AssignedRecord", 0xffffffff, false, A(R.assigned_record(self))}},
        {0x004c2890, {"TrickMenu_SortByCost", 0, false, V(R.sort_by_cost(self, S(a[0])))}},
        {0x004c29d0, {"TrickMenu_HighlightFirst", 0, false, V(R.highlight_first(self))}},
        {0x004c2a10, {"TrickMenu_BuildSpecialCombos", 0, false, V(R.build_special_combos(self))}},
        {0x004c2c10, {"TrickMenu_BuildNormalCombos", 0, false, V(R.build_normal_combos(self))}},
        {0x004c2ed0, {"TrickMenu_UpdateCategories", 0xffffffff, false, A(R.update_categories(self))}},
        {0x004c3050, {"TrickMenu_UpdateCombos", 0xffffffff, false, A(R.update_combos(self))}},
        {0x004c1310, {"TrickMenu_ButtonGlyphs", 0, false, V(R.button_glyphs(a[0], a[1]))}},
        {0x004c3350, {"TrickMenu_CategoryMask", 0xffffffff, false, A(R.category_mask(S(a[0])))}},
        {0x004c33f0, {"TrickMenu_UpdateTricks", 0xffffffff, false, A(R.update_tricks(self))}},
        {0x004c36d0, {"TrickMenu_BindSelected", 0, false, V(R.bind_selected(self, S(a[0])))}},
        {0x004c38f0, {"TrickMenu_UnbindSelected", 0, false, V(R.unbind_selected(self))}},
        {0x004c3a80, {"TrickMenu_UpdateBuyDialog", 0xffffffff, false, A(R.update_buy_dialog(self))}},
        {0x004c3ca0, {"TrickMenu_UpdateWarning", 0xffffffff, false, A(R.update_warning(self))}},
        {0x004c3cf0, {"TrickMenu_SpecialSlotsFull", 0xff, false, A(R.specials_full(self) ? 1 : 0)}},
        {0x004c3d30, {"TrickMenu_Update", 0xffffffff, false, A(R.menu_update(self))}},
        // ---- the panel ----
        {0x004c3dd0, {"TrickPanel_Construct", 0xffffffff, false, A(R.panel_construct(self, a[0], a[1], a[2], S(a[3]), S(a[4])))}},
        {0x004c40b0, {"TrickPanel_ScalarDelete", 0xffffffff, false, A(R.panel_delete(self, a[0]))}},
        {0x004c40d0, {"TrickPanel_Destroy", 0, false, V(R.panel_destroy(self))}},
        {0x004c40e0, {"TrickPanel_UpdateFrame", 0, false, V(R.panel_update_frame(self))}},
        {0x004c4300, {"TrickPanel_ShowMessage", 0, false, V(R.panel_show_message(self, S(a[0]), a[1]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "trick screen retail calls");
}
