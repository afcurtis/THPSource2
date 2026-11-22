// Replays retail forced calls (Phase 4.10f) through the native player-select screen (playsel.cpp) and the window classes of player_ui.cpp:
//
//   opentony_player_select_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the uiw, plsel... groups) holds the retail memory regions the
// ported code touches and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp.
#include "player_select.hpp"
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
    frontend::PlayerSelect screen;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc),
          screen(mem, svc, fonts, window, menu, skaters) {
        // the screen loop is replayed with its window's step, update, draw and delete as seams (see ScreenKit::set_loop_seams)
        screen.set_loop_seams(true);
        // a child of one of the window-base or this screen's own classes runs its method natively (the others are reported as services)
        window.set_dispatch([this](const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args) { return screen.dispatch_named(name, object, args); });
        menu.set_extra_dispatch([this](std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) { return screen.dispatch(object, slot, args); });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using PsAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define R c.screen

const std::map<std::uint32_t, PsAdapter>& adapters() {
    static const std::map<std::uint32_t, PsAdapter> table = {
        // ---- the window classes (player_ui.cpp) ----
        {0x004a1440, {"ScoreCounter_Construct", 0xffffffff, false, A(R.counter_construct(self, a[0], a[1], S(a[2]), S(a[3])))}},
        {0x004a1510, {"ScoreCounter_ScalarDelete", 0xffffffff, false, A(R.counter_delete(self, a[0]))}},
        {0x004a1530, {"ScoreCounter_Destroy", 0, false, V(R.counter_destroy(self))}},
        {0x004a1540, {"ScoreCounter_SetValue", 0, false, V(R.counter_set_value(self, S(a[0])))}},
        {0x004a1560, {"ScoreCounter_Draw", 0, false, V(R.counter_draw(self, S(a[0]), S(a[1])))}},
        {0x004a15a0, {"ScoreCounter_Update", 0, false, V(R.counter_update(self))}},
        {0x004a1670, {"MedalWindow_Construct", 0xffffffff, false, A(R.medal_construct(self, a[0], S(a[1]), S(a[2]), S(a[3]), S(a[4])))}},
        {0x004a1880, {"MedalWindow_ScalarDelete", 0xffffffff, false, A(R.medal_delete(self, a[0]))}},
        {0x004a18a0, {"MedalWindow_Destroy", 0, false, V(R.medal_destroy(self))}},
        {0x004a19f0, {"MedalWindow_Draw", 0, false, V(R.medal_draw(self, S(a[0]), S(a[1])))}},
        {0x004a1b00, {"MedalWindow_Update", 0, false, V(R.medal_update(self))}},
        {0x004a1b40, {"PromptWindow_Construct", 0xffffffff, false, A(R.prompt_construct(self, a[0], a[1], a[2], S(a[3]), S(a[4]), S(a[5]), S(a[6])))}},
        {0x004a1dd0, {"PromptWindow_ScalarDelete", 0xffffffff, false, A(R.prompt_delete(self, a[0]))}},
        {0x004a1df0, {"PromptWindow_Destroy", 0, false, V(R.prompt_destroy(self))}},
        {0x004a1e00, {"PromptWindow_GetWidth", 0xffffffff, false, A(R.prompt_width(self))}},
        {0x004a1eb0, {"PromptWindow_SetString", 0, false, V(R.prompt_set_string(self, a[0]))}},
        {0x004a1ed0, {"TextFlagElement_Construct", 0xffffffff, false, A(R.flag_construct(self, a[0], a[1], a[2], S(a[3]), S(a[4]), S(a[5]), S(a[6])))}},
        {0x004a1f20, {"TextFlagElement_ScalarDelete", 0xffffffff, false, A(R.flag_delete(self, a[0]))}},
        {0x004a1f40, {"TextFlagElement_Destroy", 0, false, V(R.flag_destroy(self))}},
        {0x004a1f50, {"TextFlagElement_Update", 0, false, V(R.flag_update(self))}},
        {0x004a1fe0, {"PlayerSelect_InitCallbacks", 0, false, V(R.init_callbacks(a[0]))}},
        {0x004a2000, {"PlayerSelect_ExitCallback", 0, false, V(R.exit_callback())}},
        {0x004a2010, {"PlayerSelect_ScreenCall", 0, false, V(R.screen_call(a[0], a[1]))}},
        {0x004a2030, {"PlayerSelect_SaveSelection", 0, false, V(R.save_selection(self))}},
        {0x004a2040, {"PlayerSelect_RestoreSelection", 0, false, V(R.restore_selection(self))}},
        // ---- the select window (playsel.cpp) ----
        {0x004a2050, {"PlayerSelectWindow_Construct", 0xffffffff, false, A(R.select_construct(self, S(a[0])))}},
        {0x004a20e0, {"PlayerSelectWindow_ScalarDelete", 0xffffffff, false, A(R.select_delete(self, a[0]))}},
        {0x004a2100, {"PlayerSelectWindow_Init", 0, false, V(R.select_init(self, S(a[0])))}},
        {0x004a2bc0, {"PlayerSelectWindow_Destroy", 0, false, V(R.select_destroy(self))}},
        {0x004a2cb0, {"PlayerSelectWindow_SetPromptsHidden", 0, false, V(R.select_set_prompts_hidden(self, a[0]))}},
        {0x004a2ce0, {"PlayerSelectWindow_Draw", 0, false, V(R.select_draw(self))}},
        {0x004a3110, {"PlayerSelectWindow_UpdateFrame", 0, false, V(R.select_update_frame(self))}},
        {0x004a3230, {"PlayerSelectWindow_UpdateSkaters", 0xffffffff, false, A(R.select_update_skaters(self))}},
        {0x004a34b0, {"PlayerSelectWindow_UpdateMenu", 0xffffffff, false, A(R.select_update_menu(self))}},
        {0x004a35e0, {"PlayerSelectWindow_UpdateLoad", 0xffffffff, false, A(R.select_update_load(self))}},
        {0x004a35f0, {"PlayerSelectWindow_NextPlayer", 0xff, false, A(R.select_next_player(self))}},
        {0x004a36e0, {"PlayerSelectWindow_Update", 0xffffffff, false, A(R.select_update(self))}},
        {0x004a5f50, {"PlayerMenu_GetChoice", 0xffffffff, false, A(R.menu_choice(self))}},
        {0x004a5f60, {"PlayerSelect_Run", 0xffffffff, false, A(R.run(a[0]))}},
        // ---- the bio window, the page dots, the header, the main menu window ----
        {0x004a3830, {"BioWindow_Construct", 0xffffffff, false, A(R.bio_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004a3a40, {"BioWindow_ScalarDelete", 0xffffffff, false, A(R.bio_delete(self, a[0]))}},
        {0x004a3a60, {"BioWindow_Destroy", 0, false, V(R.bio_destroy(self))}},
        {0x004a3af0, {"BioWindow_Draw", 0, false, V(R.bio_draw(self, S(a[0]), S(a[1])))}},
        {0x004a3bd0, {"BioWindow_Update", 0, false, V(R.bio_update(self))}},
        {0x004a3e00, {"BioWindow_ScrollUp", 0, false, V(R.bio_scroll_up(self))}},
        {0x004a3e40, {"BioWindow_ScrollDown", 0, false, V(R.bio_scroll_down(self))}},
        {0x004a5450, {"PageDots_Construct", 0xffffffff, false, A(R.dots_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004a54f0, {"PageDots_ScalarDelete", 0xffffffff, false, A(R.dots_delete(self, a[0]))}},
        {0x004a5510, {"PageDots_Destroy", 0, false, V(R.dots_destroy(self))}},
        {0x004a5690, {"PageDots_Update", 0, false, V(R.dots_update(self))}},
        {0x004a55c0, {"PageDots_Draw", 0, false, V(R.dots_draw(self, S(a[0]), S(a[1])))}},
        {0x004a56b0, {"PlayerHeader_Construct", 0xffffffff, false, A(R.header_construct(self, a[0], S(a[1]), S(a[2]), S(a[3]), S(a[4])))}},
        {0x004a57b0, {"PlayerHeader_ScalarDelete", 0xffffffff, false, A(R.header_delete(self, a[0]))}},
        {0x004a57d0, {"PlayerHeader_Destroy", 0, false, V(R.header_destroy(self))}},
        {0x004a57e0, {"PlayerHeader_Update", 0, false, V(R.header_update(self))}},
        {0x004a5890, {"PlayerMenu_Construct", 0xffffffff, false, A(R.menu_construct(self, a[0], a[1], S(a[2]), S(a[3])))}},
        {0x004a5ca0, {"PlayerMenu_ScalarDelete", 0xffffffff, false, A(R.menu_delete(self, a[0]))}},
        {0x004a5cc0, {"PlayerMenu_Destroy", 0, false, V(R.menu_destroy(self))}},
        {0x004a5d80, {"PlayerMenu_Draw", 0, false, V(R.menu_draw(self, S(a[0]), S(a[1])))}},
        // ---- the skater view and the spool state ----
        {0x004a3e80, {"SkaterView_Construct", 0xffffffff, false, A(R.view_construct(self, a[0], a[1], S(a[2]), S(a[3]), S(a[4]), S(a[5])))}},
        {0x004a4360, {"SkaterView_ScalarDelete", 0xffffffff, false, A(R.view_delete(self, a[0]))}},
        {0x004a4380, {"SkaterView_Destroy", 0, false, V(R.view_destroy(self))}},
        {0x004a44f0, {"SkaterView_BuildSprites", 0, false, V(R.view_build_sprites(self))}},
        {0x004a4630, {"SkaterView_Draw", 0, false, V(R.view_draw(self, S(a[0]), S(a[1])))}},
        {0x004a48e0, {"SkaterView_LoadSkater", 0, false, V(R.view_load_skater(self))}},
        {0x004a4bc0, {"SkaterView_Update", 0, false, V(R.view_update(self))}},
        {0x004a4e40, {"SkaterView_CreateObject", 0, false, V(R.view_create_object(self))}},
        {0x004a5080, {"PlayerSelect_CopyPacketTables", 0, false, V(R.view_copy_packet(a[0]))}},
        {0x004a61f0, {"SkaterView_InitSlots", 0xffffffff, false, A((R.view_init_slots(self), self))}},
        {0x004a6260, {"SkaterView_FreeSlots", 0, false, V(R.view_free_slots(self))}},
        {0x004a6e60, {"SkaterView_SpoolBusy", 0xffffffff, false, A(R.view_spool_busy(self))}},
        {0x004a6e90, {"SkaterView_SetSpoolFlag", 0, false, V(R.view_set_spool_flag(self, a[0]))}},
        {0x004a6ea0, {"SkaterView_ResetSpool", 0, false, V(R.view_reset_spool(self))}},
        // ---- the load window ----
        {0x004a6f50, {"LoadWindow_Construct", 0xffffffff, false, A(R.load_construct(self, S(a[0]), S(a[1])))}},
        {0x004a7460, {"LoadWindow_ScalarDelete", 0xffffffff, false, A(R.load_delete(self, a[0]))}},
        {0x004a7480, {"PlayerSelectWindow_EnterLoad", 0, false, V(R.load_enter(self))}},
        {0x004a74c0, {"LoadWindow_Destroy", 0, false, V(R.load_destroy(self))}},
        {0x004a7590, {"LoadWindow_Step", 0xffffffff, false, A(R.load_step(self))}},
        {0x004a7730, {"LoadWindow_Draw", 0, false, V(R.load_draw(self, S(a[0]), S(a[1])))}},
        {0x004a77e0, {"LoadWindow_Update", 0, false, V(R.load_update(self))}},
        {0x004a7890, {"LoadWindow_SaveImage", 0, false, V(R.load_save_image(self))}},
        {0x004a78b0, {"LoadWindow_RestoreImage", 0, false, V(R.load_restore_image(self))}},
        {0x004a78d0, {"LoadWindow_LoadSkater", 0, false, V(R.load_load_skater(self, a[0]))}},
        // ---- the small methods of the other classes ----
        {0x004a5380, {"SkaterView_IsCustomSelected", 0xffffffff, false, A(R.view_is_custom(self))}},
        {0x004a44b0, {"SkaterView_SetMode", 0, false, V(R.view_set_mode(self, a[0]))}},
        {0x004a44d0, {"SkaterView_Rotate", 0, false, V(R.view_rotate(self, a[0]))}},
        {0x004a5390, {"SkaterView_StepSelection", 0, false, V(R.view_step_selection(self, S(a[0])))}},
        {0x004a4890, {"SkaterView_Refresh", 0, false, V(R.view_refresh(self))}},
        {0x004a5340, {"PlayerSelect_NextCostume", 0, false, V(R.next_costume())}},
        {0x004a5880, {"PlayerSelectWindow_SlideIn", 0, false, V(R.header_slide_in(self))}},
        {0x004a5d70, {"PlayerMenu_HighlightFirst", 0, false, V(R.menu_highlight_first(self))}},
        {0x004a78f0, {"LoadWindow_IsLoading", 0xff, false, A(R.load_is_loading(self))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "player select retail calls");
}
