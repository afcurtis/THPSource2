// Replays retail forced calls (Phase 4.14b) through the native save-card screen (mmu_screens.cpp: the prompt group, the screen's constructor and destructor, the file list, the state machine,
// the browse and display functions):
//
//   opentony_mmu_screens_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the group `mmui` of mmu_specs_ui.py) holds the retail memory regions the ported code touches and, for
// every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "create_color.hpp"
#include "create_screens.hpp"
#include "create_spool.hpp"
#include "menu_toolkit.hpp"
#include "mmu_screens.hpp"
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
    frontend::CreateScreens create;
    frontend::MmuScreens screens;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc), color(mem, svc), spool(mem, svc),
          helpers(mem, svc, fonts, window, menu, color), create(mem, svc, fonts, window, menu, skaters, spool, helpers),
          screens(mem, svc, fonts, window, menu, skaters, create, spool) {
        menu.set_extra_dispatch([this](std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) { return helpers.dispatch(object, slot, args); });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using UiAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define R c.screens

const std::map<std::uint32_t, UiAdapter>& adapters() {
    static const std::map<std::uint32_t, UiAdapter> table = {
        {0x00476560, {"MmuPrompts_Construct", 0xffffffff, false, A(R.prompts_construct(self))}},
        {0x004769a0, {"MmuPrompts_ScalarDelete", 0xffffffff, false, A(R.prompts_delete(self, a[0]))}},
        {0x004769d0, {"MmuPrompts_Destroy", 0, false, V(R.prompts_destroy(self))}},
        {0x00476a30, {"MmuPrompts_Set", 0, false, V(R.prompts_set(self, static_cast<std::uint8_t>(a[0]), a[1]))}},
        {0x00476b00, {"MmuScreen_Construct", 0xffffffff, false, A(R.screen_construct(self, a[0], a[1], a[2]))}},
        {0x00477910, {"MmuScreen_ScalarDelete", 0xffffffff, false, A(R.screen_delete(self, a[0]))}},
        {0x00477940, {"MmuScreen_Destroy", 0, false, V(R.screen_destroy(self))}},
        {0x00477b40, {"MmuScreen_RebuildList", 0, false, V(R.rebuild_list(self))}},
        {0x004780d0, {"Front_SaveCardStateMachine", 0, false, V(R.state_machine(self))}},
        {0x00478f00, {"MmuScreen_Browse", 0, false, V(R.browse(self))}},
        {0x004792f0, {"MmuScreen_ShowBanner", 0, false, V(R.show_banner(self))}},
        {0x004793a0, {"MmuScreen_Display", 0, false, V(R.display(self))}},
        {0x00479f60, {"MmuScreen_Draw", 0, false, V(R.draw(self, a[0], a[1]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "save-card screen retail forced calls");
}
