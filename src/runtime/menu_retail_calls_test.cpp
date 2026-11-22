// Replays retail forced calls (Phase 4.10c) through the native menu toolkit, the title screen and the credits roll:
//
//   opentony_menu_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the menu, entry_draw, mainmenu and credits
// groups) holds the retail memory regions the ported code touches and, for every case, the state, the arguments and what the retail
// function did; see retail_calls_replay.hpp for the format and the comparison.
#include "credits.hpp"
#include "mainmenu.hpp"
#include "menu_toolkit.hpp"
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
    frontend::MainMenu main;
    frontend::Credits credits;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), main(mem, svc, fonts, window, menu),
          credits(mem, svc, fonts, window, menu) {
        // the wheel of the title window is a child of a real class: its methods run natively when the window base calls them
        window.set_dispatch([this](const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args) { return main.dispatch_named(name, object, args); });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using MenuAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define M c.menu
#define W c.main
#define K c.credits

const std::map<std::uint32_t, MenuAdapter>& adapters() {
    static const std::map<std::uint32_t, MenuAdapter> table = {
        // ---- menu.cpp: controls, style, text, rows ----
        {0x0046f680, {"MenuControl_Construct", 0xffffffff, false, A(M.control_construct(self, a[0]))}},
        {0x0046f6a0, {"MenuControl_Delete", 0xffffffff, false, A(M.control_delete(self, a[0]))}},
        {0x0046f6c0, {"MenuControl_Destroy", 0, false, V(M.control_destroy(self))}},
        {0x0046f6d0, {"MenuControl_SetHighlighted", 0, false, V(M.control_set_highlighted(self, a[0]))}},
        {0x0046f6e0, {"MenuControl_SetValue", 0, false, V(M.control_set_value(self, a[0]))}},
        {0x0046f6f0, {"MenuControl_LogNavigate", 0, false, V(M.control_log_navigate(self))}},
        {0x0046f710, {"MenuControl_Nothing", 0, false, V((void)0)}},
        {0x00470530, {"MenuControl_NoOp2", 0, false, V((void)0)}},
        {0x00470540, {"MenuControl_NoOp", 0, false, V((void)0)}},
        {0x0046f720, {"MenuControlB_Construct", 0xffffffff, false, A(M.control_b_construct(self, a[0]))}},
        {0x0046f740, {"MenuControlB_Delete", 0xffffffff, false, A(M.control_b_delete(self, a[0]))}},
        {0x0046f760, {"MenuControl_GetId", 0xffffffff, false, A(M.control_id(self))}},
        {0x0046f770, {"MenuProperties_Construct", 0xffffffff, false, A(M.properties_construct(self))}},
        {0x0046f820, {"MenuProperties_Delete", 0xffffffff, false, A(M.properties_delete(self, a[0]))}},
        {0x0046f840, {"MenuProperties_Destroy", 0, false, V(M.properties_destroy(self))}},
        {0x0046f890, {"MenuText_Construct", 0xffffffff, false, A(M.text_construct(self, a[0], a[1], a[2]))}},
        {0x0046f910, {"MenuText_Delete", 0xffffffff, false, A(M.text_delete(self, a[0]))}},
        {0x0046f930, {"MenuText_Destroy", 0, false, V(M.text_destroy(self))}},
        {0x0046f940, {"MenuItem_SetPosition", 0, false, V(M.item_set_position(self, S(a[0]), S(a[1])))}},
        {0x0046f960, {"MenuText_SetText", 0, false, V(M.text_set_text(self, a[0]))}},
        {0x0046fa60, {"MenuText_Draw", 0, false, V(M.text_draw(self))}},
        {0x0046fc50, {"MenuRow_Construct", 0xffffffff, false, A(M.row_construct(self, a[0], a[1], a[2], S(a[3]), a[4], a[5]))}},
        {0x0046fd70, {"MenuRow_Delete", 0xffffffff, false, A(M.row_delete(self, a[0]))}},
        {0x0046fd90, {"MenuRow_SetColumn", 0, false, V(M.row_set_column(self, a[0], S(a[1]), a[2], a[3]))}},
        {0x0046fe80, {"MenuRow_Destroy", 0, false, V(M.row_destroy(self))}},
        {0x0046fe90, {"MenuRow_Draw", 0, false, V(M.row_draw(self))}},
        // ---- dots, sliders, scaled text ----
        {0x004701d0, {"MenuDots_Construct", 0xffffffff, false, A(M.dots_construct(self, a[1], a[2], a[3]))}},
        {0x00470280, {"MenuDots_Delete", 0xffffffff, false, A(M.dots_delete(self, a[0]))}},
        {0x004702a0, {"MenuDots_Destroy", 0, false, V(M.dots_destroy(self))}},
        {0x004702b0, {"MenuDots_Draw", 0, false, V(M.dots_draw(self, S(a[0]), S(a[1])))}},
        {0x00470550, {"MenuDots_SetRange", 0, false, V(M.dots_set_range(self, a[0], S(a[1])))}},
        {0x00470630, {"MenuSlider_Construct", 0xffffffff, false, A(M.slider_construct(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x004706c0, {"MenuSlider_Delete", 0xffffffff, false, A(M.slider_delete(self, a[0]))}},
        {0x004706e0, {"MenuSlider_Destroy", 0, false, V(M.slider_destroy(self))}},
        {0x00470710, {"MenuAlloc_Thunk", 0xffffffff, false, A(M.operator_new_thunk(a[0]))}},
        {0x00470720, {"MenuFree_Thunk", 0, false, V(M.operator_delete_thunk(a[0]))}},
        {0x00470730, {"MenuSlider_Draw", 0, false, V(M.slider_draw(self))}},
        {0x004707e0, {"MenuSlider_Adjust", 0, false, V(M.slider_adjust(self, a[0]))}},
        {0x00473450, {"MenuSlider_SubDelete", 0xffffffff, false, A(M.slider_sub_delete_thunk(self, a[0]))}},
        {0x00473460, {"MenuSlider_SubRefresh", 0, false, V(M.slider_sub_thunk_b(self))}},
        {0x004708d0, {"MenuTextScaled_Construct", 0xffffffff, false, A(M.text_scaled_construct(self, a[0], a[1], a[2], a[3]))}},
        {0x00470910, {"MenuTextScaled_Delete", 0xffffffff, false, A(M.text_scaled_delete(self, a[0]))}},
        {0x00470930, {"MenuTextScaled_Destroy", 0, false, V(M.text_scaled_destroy(self))}},
        {0x00470940, {"MenuTextScaled_Draw", 0, false, V(M.text_scaled_draw(self))}},
        // ---- lists ----
        {0x00470a30, {"MenuList_DrawBox", 0, false, V(M.list_draw_box(self))}},
        {0x00470a70, {"MenuList_SetArrows", 0, false, V(M.list_set_arrows(self, a[0], a[1], a[2]))}},
        {0x00470ab0, {"MenuList_HighlightedId", 0xffffffff, false, A(M.list_highlighted_id(self))}},
        {0x00470b20, {"MenuList_Highlight", 0, false, V(M.list_highlight(self, a[0]))}},
        {0x00470bd0, {"MenuList_HighlightFirst", 0, false, V(M.list_highlight_first(self))}},
        {0x00470c60, {"MenuList_Construct", 0xffffffff, false, A(M.list_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3]), a[4], a[5], a[6], a[7]))}},
        {0x00470cf0, {"MenuList_Delete", 0xffffffff, false, A(M.list_delete(self, a[0]))}},
        {0x00470d10, {"MenuList_Destroy", 0, false, V(M.list_destroy(self))}},
        {0x00470d20, {"MenuList_Layout", 0, false, V(M.list_layout(self))}},
        {0x00470ec0, {"MenuList_Draw", 0, false, V(M.list_draw(self))}},
        {0x00470f50, {"MenuList_SnapAll", 0, false, V(M.list_snap_all(self))}},
        {0x00470f80, {"MenuList_Navigate", 0, false, V(M.list_navigate(self, a[0]))}},
        {0x00471050, {"MenuList_Add", 0, false, V(M.list_add(self, a[0]))}},
        // ---- scroll lists ----
        {0x004710d0, {"ScrollList_Construct", 0xffffffff, false, A(M.scroll_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3]), a[4], a[5], a[6], a[7]))}},
        {0x00471180, {"ScrollList_Delete", 0xffffffff, false, A(M.scroll_delete(self, a[0]))}},
        {0x004711a0, {"ScrollList_Destroy", 0, false, V(M.scroll_destroy(self))}},
        {0x004711b0, {"ScrollList_Layout", 0, false, V(M.scroll_layout(self))}},
        {0x00471370, {"ScrollList_Draw", 0, false, V(M.scroll_draw(self))}},
        {0x00471550, {"ScrollList_Follow", 0, false, V(M.scroll_follow(self))}},
        {0x00471650, {"ScrollList_Navigate", 0, false, V(M.scroll_navigate(self, a[0]))}},
        {0x00471750, {"ScrollList_Add", 0, false, V(M.scroll_add(self, a[0]))}},
        // ---- grids ----
        {0x00471800, {"MenuGrid_Construct", 0xffffffff, false, A(M.grid_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3]), a[4], a[5], a[6]))}},
        {0x004718c0, {"MenuGrid_Delete", 0xffffffff, false, A(M.grid_delete(self, a[0]))}},
        {0x004718e0, {"MenuGrid_Destroy", 0, false, V(M.grid_destroy(self))}},
        {0x00471930, {"MenuGrid_Layout", 0, false, V(M.grid_layout(self))}},
        {0x00471b40, {"MenuGrid_Draw", 0, false, V(M.grid_draw(self))}},
        {0x00471b80, {"MenuGrid_SnapAll", 0, false, V(M.grid_snap_all(self))}},
        {0x00471bb0, {"MenuGrid_Navigate", 0, false, V(M.grid_navigate(self, a[0]))}},
        {0x00471c80, {"MenuGrid_Add", 0, false, V(M.grid_add(self, a[0]))}},
        {0x00471cf0, {"MenuGrid_HighlightedId", 0xffffffff, false, A(M.grid_highlighted_id(self))}},
        {0x00471d70, {"MenuGrid_Highlight", 0, false, V(M.grid_highlight(self, a[0]))}},
        // ---- the name entry and the wrapper ----
        {0x00471e30, {"NameEntry_Construct", 0xffffffff, false,
                      A(M.name_entry_construct(self, a[0], S(a[1]), a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]))}},
        {0x004727d0, {"NameEntry_Delete", 0xffffffff, false, A(M.name_entry_delete(self, a[0]))}},
        {0x004727f0, {"NameEntry_Destroy", 0, false, V(M.name_entry_destroy(self))}},
        {0x00472920, {"NameEntry_GetName", 0xffffffff, false, A(M.name_entry_name(self))}},
        {0x00472930, {"NameEntry_SetName", 0, false, V(M.name_entry_set_name(self, a[0]))}},
        {0x00472a70, {"NameEntry_SetPrompt", 0, false, V(M.name_entry_set_prompt(self, a[0], a[1], a[2]))}},
        {0x00472b50, {"NameEntry_Draw", 0, false, V(M.name_entry_draw(self, a[0], a[1]))}},
        {0x00472cc0, {"NameEntry_HandleInput", 0xffffffff, false, A(M.name_entry_handle_input(self, a[0]))}},
        {0x00472ef0, {"MenuWrapper_Construct", 0xffffffff, false, A(M.wrapper_construct(self, a[0], a[1], S(a[2]), a[3], a[4]))}},
        {0x00473240, {"MenuWrapper_Delete", 0xffffffff, false, A(M.wrapper_delete(self, a[0]))}},
        {0x00473260, {"MenuWrapper_DestroyRoot", 0, false, V(M.wrapper_destroy_root(self))}},
        {0x00473270, {"MenuWrapper_SubDelete", 0xffffffff, false, A(M.wrapper_sub_delete(self, a[0]))}},
        {0x00473290, {"MenuWrapper_Destroy", 0, false, V(M.wrapper_destroy(self))}},
        {0x00473330, {"MenuWrapper_TestComplete", 0, false, V(M.wrapper_test_complete(self))}},
        {0x00473380, {"MenuWrapper_Poll", 0xff, false, A(M.wrapper_poll(self))}},
        {0x00473220, {"MenuWrapper_Nothing", 0, false, V(M.nothing())}},
        {0x00473230, {"MenuWrapper_False", 0xff, false, A(M.false_result())}},
        {0x00473470, {"Mess_StaticInit", 0, false, V(M.statics_init())}},
        {0x00473480, {"Mess_FontConstruct", 0, false, V(M.mess_font_construct())}},
        {0x00473490, {"Mess_FontAtexit", 0, false, V(M.mess_font_atexit())}},
        {0x004734a0, {"Mess_FontDestroy", 0, false, V(M.mess_font_destroy())}},
        // ---- mainmenu.cpp ----
        {0x0046aee0, {"MainMenu_RunWrapper", 0, false, V(W.run_wrapper(a[1]))}},
        {0x0046aef0, {"MainMenu_Main", 0xffffffff, false, A(W.run())}},
        {0x0046b0a0, {"MainMenuWindow_Construct", 0xffffffff, false, A(W.window_construct(self))}},
        {0x0046b4f0, {"MainMenuWindow_Delete", 0xffffffff, false, A(W.window_delete(self, a[0]))}},
        {0x0046b510, {"MainMenuWindow_Destroy", 0, false, V(W.window_destroy(self))}},
        {0x0046b5d0, {"MainMenuWindow_Draw", 0, false, V(W.window_draw(self))}},
        {0x0046b690, {"MainMenuWindow_Update", 0, false, V(W.window_update(self))}},
        {0x0046b720, {"MainMenuWindow_Poll", 0xff, false, A(W.window_poll(self, a[0]))}},
        {0x0046b870, {"MainMenuWheel_Construct", 0xffffffff, false, A(W.wheel_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x0046b9a0, {"MainMenuWheel_Delete", 0xffffffff, false, A(W.wheel_delete(self, a[0]))}},
        {0x0046b9c0, {"MainMenuWheel_Destroy", 0, false, V(W.wheel_destroy(self))}},
        {0x0046ba50, {"MainMenuWheel_Update", 0, false, V(W.wheel_update(self))}},
        {0x0046bce0, {"MainMenuWheel_Draw", 0, false, V(W.wheel_draw(self, S(a[0]), S(a[1])))}},
        {0x0046bd20, {"MainMenuWheel_PaletteAlloc", 0, false, V(W.wheel_palette_alloc(self))}},
        {0x0046bd70, {"MainMenuWheel_Tint", 0, false, V(W.wheel_tint(self))}},
        {0x0046be60, {"CardScreen_Register", 0, false, V(W.card_register(a[0]))}},
        {0x0046be80, {"CardScreen_Nothing", 0, false, V(W.card_nothing())}},
        {0x0046be90, {"CardScreen_RunWrapper", 0, false, V(W.card_run_wrapper(a[1]))}},
        {0x0046c460, {"CardScreen_Main", 0xffffffff, false, A(W.card_run())}},
        {0x0046bea0, {"CardWindow_Construct", 0xffffffff, false, A(W.card_construct(self))}},
        {0x0046c080, {"CardWindow_Delete", 0xffffffff, false, A(W.card_delete(self, a[0]))}},
        {0x0046c0a0, {"CardWindow_Destroy", 0, false, V(W.card_destroy(self))}},
        {0x0046c120, {"CardWindow_Update", 0, false, V(W.card_update(self))}},
        {0x0046c3d0, {"CardWindow_Draw", 0, false, V(W.card_draw(self, S(a[0]), S(a[1])))}},
        {0x0046c450, {"CardWindow_Result", 0xffffffff, false, A(W.card_result(self))}},
        // ---- credits.cpp ----
        {0x00426430, {"Credits_Run", 0, false, V(K.run(a[0]))}},
        {0x004266e0, {"Credits_LoadText", 0xffffffff, false, A(K.load_text(self, a[0]))}},
        {0x00426b10, {"Credits_Unload", 0, false, V(K.unload(self))}},
        {0x00426b50, {"Credits_AdvanceTimeline", 0xff, false, A(K.advance_timeline(self))}},
        {0x00426bc0, {"Credits_UpdateRecords", 0, false, V(K.update_records(self))}},
        {0x00426c00, {"Credits_ParseLine", 0xffffffff, true, A(K.parse_line(self, a[0]))}},
        {0x00426c80, {"Credits_ParseTag", 0xffffffff, true, A(K.parse_tag(self, a[0]))}},
        {0x00426d90, {"CreditRecord_Destroy", 0, false, V(K.record_destroy(self))}},
        {0x00426da0, {"CreditRecord_SetOffset", 0xffffffff, false, A(K.record_set_offset(self, a[0]))}},
        {0x00426db0, {"CreditRecord_Nothing", 0, false, V(K.record_nothing())}},
        {0x00426dc0, {"CreditsFontA_Init", 0, false, V(K.statics_a_init())}},
        {0x00426dd0, {"CreditsFontA_Construct", 0, false, V(K.statics_a_construct())}},
        {0x00426de0, {"CreditsFontA_Atexit", 0, false, V(K.statics_a_atexit())}},
        {0x00426df0, {"CreditsFontA_Destroy", 0, false, V(K.statics_a_destroy())}},
        {0x00426e10, {"CreditsFontB_Init", 0, false, V(K.statics_b_init())}},
        {0x00426e20, {"CreditsFontB_Construct", 0, false, V(K.statics_b_construct())}},
        {0x00426e30, {"CreditsFontB_Atexit", 0, false, V(K.statics_b_atexit())}},
        {0x00426e40, {"CreditsFontB_Destroy", 0, false, V(K.statics_b_destroy())}},
        {0x00426e60, {"CreditText_Construct", 0xffffffff, false, A(K.text_construct(self, a[0], a[1]))}},
        {0x00426ee0, {"CreditText_Delete", 0xffffffff, false, A(K.text_delete(self, a[0]))}},
        {0x00426f00, {"CreditText_Destroy", 0, false, V(K.text_destroy(self))}},
        {0x00426f10, {"CreditText_Update", 0, false, V(K.text_update(self, S(a[0])))}},
        {0x00426f50, {"CreditBitmap_Construct", 0xffffffff, false, A(K.bitmap_construct(self, a[0]))}},
        {0x00426fe0, {"CreditBitmap_Delete", 0xffffffff, false, A(K.bitmap_delete(self, a[0]))}},
        {0x00427000, {"CreditBitmap_Destroy", 0, false, V(K.bitmap_destroy(self))}},
        {0x00427050, {"CreditBitmap_Update", 0, false, V(K.bitmap_update(self, S(a[0])))}},
        {0x00427080, {"CreditMusic_Construct", 0xffffffff, false, A(K.music_construct(self, a[0], a[1]))}},
        {0x004270b0, {"CreditMusic_Delete", 0xffffffff, false, A(K.music_delete(self, a[0]))}},
        {0x004270d0, {"CreditMusic_BaseDestroy", 0, false, V(K.music_base_destroy(self))}},
        {0x004270e0, {"CreditMusic_Update", 0, false, V(K.music_update(self, S(a[0])))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "menu retail calls");
}
