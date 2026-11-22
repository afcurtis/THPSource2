// Replays retail forced calls (Phase 4.10i) through the native front end (front.cpp: the CMenu class, the menus and screens between a level and the next, the level loader):
//
//   opentony_front_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the f... groups) holds the retail memory regions the ported code touches and, for
// every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp.
#include "front.hpp"
#include "netmenu.hpp"
#include "shell_helpers.hpp"
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
    frontend::FrontScreens front;
    frontend::ShellHelpers helpers;
    frontend::NetMenu net;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc), front(mem, svc, fonts, window, menu, skaters),
          helpers(mem, svc, fonts, window, menu, skaters), net(mem, svc, fonts, window, menu, skaters) {
        net.set_loop_seams(true);
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using FrontAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define F c.front
#define H c.helpers
#define N c.net

const std::map<std::uint32_t, FrontAdapter>& adapters() {
    static const std::map<std::uint32_t, FrontAdapter> table = {
        // ---- the CMenu class ----
        {0x0044b640, {"CMenu__new", 0xffffffff, false, A(F.cmenu_new(self, S(a[0]), S(a[1]), a[2], a[3], a[4], S(a[5])))}},
        {0x0044b6f0, {"CMenu__destruct", 0xffffffff, false, A(F.cmenu_destruct(self, a[0]))}},
        {0x0044b710, {"CMenu__delete", 0, false, V(F.cmenu_delete(self))}},
        {0x0044b720, {"CMenu__CentreY", 0, false, V(F.cmenu_centre_y(self))}},
        {0x0044b770, {"CMenu__SetLine", 0, false, V(F.cmenu_set_line(self, a[0]))}},
        {0x0044b7b0, {"CMenu__Reset", 0, false, V(F.cmenu_reset(self))}},
        {0x0044b800, {"CMenu__FindEntry", 0xffffffff, false, A(F.cmenu_find_entry(self, a[0]))}},
        {0x0044b890, {"CMenu__AddEntry", 0, false, V(F.cmenu_add_entry(self, a[0]))}},
        {0x0044b920, {"CMenu__EntryOn", 0, false, V(F.cmenu_entry_on(self, a[0]))}},
        {0x0044b940, {"CMenu__EntryOff", 0, false, V(F.cmenu_entry_off(self, a[0]))}},
        {0x0044b960, {"CMenu__IsEntryOn", 0xff, false, A(F.cmenu_is_entry_on(self, a[0]))}},
        {0x0044b980, {"CMenu__Enable", 0, false, V(F.cmenu_enable(self, a[0]))}},
        {0x0044b9a0, {"CMenu__Disable", 0, false, V(F.cmenu_disable(self, a[0]))}},
        {0x0044b9c0, {"CMenu__IsEntryShown", 0xff, false, A(F.cmenu_is_shown(self, a[0]))}},
        {0x0044b9e0, {"CMenu__SetEntryHeight", 0, false, V(F.cmenu_set_height(self, a[0], a[1]))}},
        {0x0044ba00, {"CMenu__ChoiceIs", 0xffffffff, false, A(F.cmenu_choice_is(self, a[0]))}},
        {0x0044ba20, {"CMenu__PositionOf", 0, false, V(F.cmenu_position_of(self, a[0], a[1], a[2]))}},
        {0x0044ba90, {"CMenu__Display", 0, false, V(F.cmenu_display(self))}},
        {0x0044bbc0, {"CMenu__Update", 0, false, V(F.cmenu_update(self))}},
        // ---- the small screens ----
        {0x0044b180, {"Front_DemoFont_Construct", 0, false, V(F.demo_font_construct())}},
        {0x0044b1f0, {"Front_SetStatus", 0, false, V(F.set_status(a[0], a[1]))}},
        {0x0044b240, {"Front_PrintDemo", 0, false, V(F.print_demo())}},
        {0x0044b320, {"Front_DemoModeBlink", 0, false, V(F.demo_mode_blink())}},
        {0x0044b3a0, {"Front_DisplayScreen", 0, false, V(F.display_screen(a[0]))}},
        {0x0044b470, {"RestartCompFrig", 0, false, V(F.restart_comp_frig())}},
        {0x0044b570, {"Horse_DisplayWords", 0, false, V(F.horse_display_words(a[0]))}},
        {0x0044bde0, {"Front_ClearScreen", 0, false, V(F.clear_screen())}},
        {0x0044be50, {"Front_Init", 0, false, V(F.init())}},
        {0x0044c650, {"Front_DeleteMenu", 0, false, V(F.delete_menu(a[0]))}},
        {0x0044c670, {"Front_DeleteMenus", 0, false, V(F.delete_menus())}},
        {0x0044c6e0, {"Front_HintNone", 0, false, V(F.hint_none())}},
        {0x0044c6f0, {"Front_HintOne", 0, false, V(F.hint_one())}},
        {0x0044c700, {"Front_HintText", 0, false, V(F.hint_text(a[0], a[1]))}},
        {0x0044c710, {"Front_PrintGoBack", 0, false, V(F.print_go_back(a[0], S(a[1]), S(a[2]), a[3], a[4], a[5], a[6], a[7], a[8], a[9], S(a[10])))}},
        {0x0044ca60, {"Front_Display", 0, false, V(F.display())}},
        {0x0044dd80, {"Front_DrawPausedBitmap", 0, false, V(F.draw_paused_bitmap())}},
        {0x0044dda0, {"Front_PollButtons", 0, false, V(F.poll_buttons_to(a[0], a[1], a[2], a[3]))}},
        {0x0044dfa0, {"CommonSwitchOnMenuCode", 0, false, V(F.common_switch_on_menu_code())}},
        {0x0044e0f0, {"Front_SwitchOnEndLevel", 0, false, V(F.switch_on_end_level())}},
        {0x0044e3f0, {"Front_ResetScoreSlide", 0, false, V(F.reset_score_slide())}},
        {0x0044e420, {"Front_SwitchOnEndHeat", 0, false, V(F.switch_on_end_heat())}},
        {0x0044e4e0, {"Front_SwitchOnEndingScreen", 0, false, V(F.switch_on_ending_screen())}},
        {0x0044e540, {"Front_SwitchOnEndLevelMenu", 0, false, V(F.switch_on_level_menu())}},
        {0x0044e5c0, {"Front_SwitchOnTimeSelect", 0, false, V(F.switch_on_time_select())}},
        {0x0044e630, {"Front_HorseMessage", 0, false, V(F.horse_message(a[0], a[1], a[2], a[3]))}},
        {0x0044e7e0, {"Front_PrintHorseLetters", 0, false, V(F.print_horse_letters())}},
        {0x0044e950, {"Front_SelectPlayReplay", 0, false, V(F.select_play_replay())}},
        {0x0044e9c0, {"Front_SwitchOnVideoScreen", 0, false, V(F.switch_on_video_screen())}},
        {0x0044ea70, {"Front_ReplayRestart", 0, false, V(F.replay_restart())}},
        {0x0044eac0, {"Front_Update", 0, false, V(F.update())}},
        // ---- the level loader ----
        {0x004521c0, {"Skater_LoadPlayerResources", 0, false, V(F.skater_load_player_resources(a[0]))}},
        {0x00452390, {"PSX_ShareAnimationRegion", 0, false, V(F.psx_share_animation_region(a[0], a[1], a[2]))}},
        {0x00452430, {"Front_WaitForButton", 0xffffffff, false, A(F.wait_for_button(a[0]))}},
        {0x004524a0, {"Front_LoadGame", 0, false, V(F.load_game())}},
        {0x00452a80, {"Front_CheckUnlocks", 0xff, false, A(F.check_unlocks(a[0]))}},
        {0x00452d00, {"Front_UnlockContinue", 0, false, V(F.unlock_continue())}},
        {0x00452d30, {"Front_QuitCombo", 0xffffffff, false, A(F.quit_combo())}},
        {0x00452dc0, {"Front_QuitComboReset", 0xff, false, A((F.quit_combo_reset(), 0u))}},
        {0x00452dd0, {"Front_QuitComboSet", 0, false, V(F.quit_combo_set())}},
        {0x00452de0, {"Front_ReplayPlaying", 0xffffffff, false, A(F.replay_playing())}},
        // ---- the shell (frontend2.cpp) ----
        {0x00452e40, {"Front_GetUnlockFlags", 0, false, V(F.get_unlock_flags(a[0]))}},
        {0x00452ec0, {"Front_ReselectAfterUnlockChange", 0, false, V(F.reselect_after_unlock_change(a[0]))}},
        {0x00452fe0, {"Front_ShellNothing", 0, false, V(F.shell_nothing())}},
        {0x00452ff0, {"FrontEnd_Main", 0, false, V(F.frontend_main())}},
        {0x00454130, {"Front_ReplaySwapSkatersIn", 0, false, V(F.replay_swap_in())}},
        {0x00454300, {"Front_ReplaySwapSkatersOut", 0, false, V(F.replay_swap_out())}},
        {0x004544a0, {"Front_LaunchGameLevel", 0, false, V(F.launch_game_level(a[0], a[1]))}},
        {0x00454710, {"Front_StartShellMusic", 0, false, V(F.start_shell_music())}},
        {0x00454760, {"Front_SyncSkaterSlots", 0, false, V(F.sync_skater_slots())}},
        {0x004547f0, {"Player_GetTextureSetEntry", 0xffffffff, false, A(F.texture_set_entry(a[0], a[1]))}},
        {0x004ac3f0, {"Front_ClearGVideoRestartData", 0, false, V(F.clear_gvideo_restart_data())}},
        // ---- the pre-load progress screen, the menu pad poll, the panel meters, the text window and the one-liners (shell_helpers) ----
        {0x00454930, {"Pie_Construct", 0xffffffff, false, A(H.pie_construct(self))}},
        {0x00454950, {"Pie_Delete", 0xffffffff, false, A(H.pie_delete(self, a[0]))}},
        {0x00454970, {"Pie_Destruct", 0, false, V(H.pie_destruct(self))}},
        {0x00454980, {"Pie_Load", 0xffffffff, false, A(H.pie_load(self, a[0]))}},
        {0x00454a60, {"Pie_Update", 0xffffffff, false, A(H.pie_update(self))}},
        {0x00454c80, {"SlideTable_Clear", 0, false, V(H.clear_slide_table())}},
        {0x00454c90, {"SlideTable_ClearBody", 0, false, V(H.clear_slide_table())}},
        {0x00454cb0, {"Vector_ForEach", 0, false, V(H.iterate_vector(a[0], a[1], a[2], a[3]))}},
        {0x00454ce0, {"Front_Stub_00454CE0", 0, false, V((void)0)}},
        {0x00454cf0, {"Front_Stub_00454CF0", 0, false, V((void)0)}},
        {0x00454d00, {"Front_Stub_00454D00", 0, false, V((void)0)}},
        {0x00454d10, {"GameFmv_Play", 0, false, V(H.play_game_fmv(a[0]))}},
        {0x0046f550, {"Menu_PadPoll", 0xffffffff, false, A(H.menu_pad_poll(S(a[0]), a[1], a[2]))}},
        {0x0046f650, {"Menu_PadPollAny", 0, false, A(H.menu_pad_poll_any())}},
        {0x00473c00, {"MenuF80_Construct", 0xffffffff, false, A(H.base_construct_f80(self))}},
        {0x00473c20, {"MenuBase_Delete", 0xffffffff, false, A(H.base_delete(self, a[0]))}},
        {0x00473c40, {"MenuBase_Destruct", 0, false, V(H.base_destruct(self))}},
        {0x0048ab20, {"Panel_Line", 0, false, V(H.panel_line(S(a[0]), S(a[1]), S(a[2]), S(a[3]), a[4]))}},
        {0x0048abf0, {"Panel_BalanceLR", 0, false, V(H.panel_balance_lr(S(a[0]), S(a[1]), S(a[2]), S(a[3]), S(a[4]), a[5], a[6]))}},
        {0x0048ac60, {"Panel_BalanceUD", 0, false, V(H.panel_balance_ud(S(a[0]), S(a[1]), S(a[2]), S(a[3]), S(a[4]), a[5], a[6]))}},
        {0x004ac370, {"Pad_RepeatDirection", 0xffffffff, false, A(H.pad_repeat_direction())}},
        {0x004ac420, {"Front_Stub_004AC420", 0, false, V((void)0)}},
        {0x004acf20, {"Screen_LineUnguarded", 0, false, V(H.line_unguarded(S(a[0]), S(a[1]), S(a[2]), S(a[3]), a[4]))}},
        {0x004acfe0, {"Front_Stub_004ACFE0", 0, false, V((void)0)}},
        {0x004266a0, {"Credits_AllocZeroed", 0xffffffff, false, A(H.credits_alloc_zeroed(a[0]))}},
        {0x004266d0, {"Credits_Free", 0, false, V(H.credits_free(a[0]))}},
        {0x0044aa70, {"Font_StringWidth", 0xffffffff, false, A(H.font_string_width(self, a[0]))}},
        {0x00452e00, {"Front_SetHandicapNamesJump", 0, false, V(H.set_handicap_names())}},
        {0x00452e10, {"Front_SetHandicapNames", 0, false, V(H.set_handicap_names())}},
        {0x004b0b20, {"ShellUtils_Tail", 0, false, V(H.shell_utils_tail())}},
        // ---- shellutils.cpp: the 8-bit BMP loader and the 2D screen primitives (shell_bitmap.cpp) ----
        {0x004afd40, {"Load8BitBMP2", 0xffffffff, false, A(H.load_8bit_bmp(a[0], a[1], a[2], a[3], a[4]))}},
        {0x004affe0, {"Load8BitBMP2_File", 0xffffffff, false, A(H.load_8bit_bmp_file(a[0], a[1], a[2], a[3], a[4]))}},
        {0x004b0060, {"Bmp8_UploadTall", 0, false, V(H.bmp8_upload_tall(a[0]))}},
        {0x004b0320, {"Bmp8_Upload", 0, false, V(H.bmp8_upload(a[0]))}},
        {0x004b0580, {"Clut_SetSemiTransparent", 0, false, V(H.clut_set_semi_transparent(a[0], a[1]))}},
        {0x004b0600, {"Screen_FillRect", 0, false, V(H.screen_fill_rect(a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x004b0800, {"Screen_SetDrawArea", 0, false, V(H.screen_set_draw_area(a[0], a[1], a[2], a[3]))}},
        {0x004b0950, {"Screen_ResetPrimitives", 0, false, V(H.screen_reset_primitives())}},
        {0x004b0970, {"Screen_DrawTexturedRect", 0, false, V(H.screen_draw_textured_rect(a[0], a[1], a[2], a[3], a[4]))}},
        // ---- netmenu.cpp: the network lobby ----
        {0x0047cbb0, {"NetMenu_Main", 0xffffffff, false, A(N.run(a[0]))}},
        {0x0047cda0, {"NetMenu_Return0", 0, false, V(N.no_op())}},
        {0x0047cdb0, {"NetMenu_IsReady", 0xffffffff, false, A(N.is_ready(self))}},
        {0x0047cdc0, {"NetMenu_RefreshGames", 0, false, V(N.refresh_games(self))}},
        {0x0047ce60, {"NetMenu_Construct", 0xffffffff, false, A(N.construct(self))}},
        {0x0047e210, {"NetMenu_SetItemTextures", 0, false, V(N.set_item_textures(a[0], a[1], S(a[2])))}},
        {0x0047e240, {"NetMenu_Delete", 0xffffffff, false, A(N.delete_window(self, a[0]))}},
        {0x0047e260, {"NetMenu_Destruct", 0, false, V(N.destruct(self))}},
        {0x0047e3c0, {"NetMenu_EnableArrows", 0, false, V(N.enable_arrows(self))}},
        {0x0047e3e0, {"NetMenu_AddChat", 0, false, V(N.add_chat(self, a[0], a[1]))}},
        {0x0047e4e0, {"NetMenu_UpdateLobby", 0, false, V(N.update_lobby(self))}},
        {0x0047e770, {"NetMenu_Update", 0, false, V(N.update(self))}},
        {0x0047f1e0, {"NetMenu_ConfirmPressed", 0xff, false, A(N.confirm_pressed(self))}},
        {0x0047f240, {"NetMenu_ReturnFalse", 0xff, false, A(N.return_false())}},
        {0x0047f250, {"NetSummary_Construct", 0xffffffff, false, A(N.summary_construct(self))}},
        {0x0047f770, {"NetSummary_Delete", 0xffffffff, false, A(N.summary_delete(self, a[0]))}},
        {0x0047f790, {"NetSummary_Destruct", 0, false, V(N.summary_destruct(self))}},
        {0x0047f820, {"NetSummary_Return0", 0, false, V(N.no_op())}},
        {0x0047f830, {"NetSummary_IsReady", 0xffffffff, false, A(N.summary_ready(self))}},
        {0x0047f840, {"NetSummary_DrawPlayer", 0, false, V(N.summary_draw_player(self, a[0]))}},
        {0x0047f930, {"NetSummary_Draw", 0, false, V(N.summary_draw(self))}},
        {0x0047fac0, {"NetMenu_ReturnTrue", 0xff, false, A(N.return_true())}},
        {0x0047fad0, {"NetMenu_BackPressed", 0xff, false, A(N.back_pressed())}},
        {0x0047fb20, {"NETMENU_InitSummary", 0, false, V(N.init_summary(0))}},
        {0x0047fc00, {"NetMenu_SummaryUpdate", 0, false, V(N.summary_update())}},
        {0x0047fc20, {"NetMenu_CloseSummary", 0, false, V(N.close_summary(a[0]))}},
        {0x0047fc70, {"NetMenu_DefaultHostName", 0, false, V(N.default_host_name())}},
        {0x004b9320, {"Effect_Delete", 0xffffffff, false, A(H.effect_delete(self, a[0]))}},
        {0x004b9340, {"Effect_Destruct", 0, false, V(H.effect_destruct(self))}},
        {0x004b93a0, {"Effect_Kill", 0, false, V(H.effect_kill(self))}},
        {0x004b93b0, {"Effect_SetExtra", 0, false, V(H.effect_set_extra(self, a[0], a[1]))}},
        {0x004b93d0, {"Effects_SetBusy", 0, false, V(H.set_effects_busy())}},
        {0x004b93e0, {"Effect_FreeChild", 0, false, V(H.effect_free_child(self))}},
        {0x004b9400, {"Effect_SetFrames", 0, false, V(H.effect_set_frames(self, a[0]))}},
        {0x004b9420, {"Effect_Move", 0, false, V(H.effect_move(self))}},
        {0x0047fd30, {"TRG_AllocObjectStorage", 0xffffffff, false, A(H.credits_alloc_zeroed(a[0]))}},
        {0x0047fd60, {"Object_Free", 0, false, V(H.credits_free(a[0]))}},
        {0x0047fd70, {"TRG_AllocObjectStorage2", 0xffffffff, false, A(H.credits_alloc_zeroed(a[0]))}},
        {0x0047fda0, {"PSX_FreeObjectRecordArray", 0, false, V(H.credits_free(a[0]))}},
        {0x0047fdb0, {"PSX_InitObjectRecord", 0, false, A(H.object_record_init(self))}},
        {0x0047fe00, {"PSX_ObjectRecord_Delete", 0xffffffff, false, A(H.object_record_delete(self, a[0]))}},
        {0x0047fe20, {"PSX_DestroyObjectRecord", 0, false, V(H.object_record_destruct(self))}},
        {0x004ba7a0, {"TextWin_Construct", 0xffffffff, false, A(H.textwin_construct(self, a[0], S(a[1]), S(a[2]), S(a[3]), S(a[4])))}},
        {0x004ba8a0, {"TextWin_Delete", 0xffffffff, false, A(H.textwin_delete(self, a[0]))}},
        {0x004ba8c0, {"TextWin_Destruct", 0, false, V(H.textwin_destruct(self))}},
        {0x004ba940, {"TextWin_SetRect", 0, false, V(H.textwin_set_rect(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004baa50, {"TextWin_SetColors", 0, false, V(([&] { const std::uint32_t cols[15] = {a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14]}; H.textwin_set_colors(self, cols); })())}},
        {0x004baac0, {"TextWin_SetOwner", 0, false, V(H.textwin_set_owner(self, a[0]))}},
        {0x004baad0, {"TextWin_Clear", 0, false, V(H.textwin_clear(self))}},
        {0x004bab00, {"TextWin_SetLine", 0, false, V(H.textwin_set_line(self, S(a[0]), a[1]))}},
        {0x004bab70, {"TextWin_PushLine", 0, false, V(H.textwin_push_line(self, a[0]))}},
        {0x004babc0, {"TextWin_InsertLine", 0, false, V(H.textwin_insert_line(self, a[0]))}},
        {0x004bac00, {"TextWin_SetLineMarked", 0, false, V(H.textwin_set_line_marked(self, S(a[0]), a[1]))}},
        {0x004bac70, {"TextWin_PushLineMarked", 0, false, V(H.textwin_push_line_marked(self, a[0]))}},
        {0x004bacc0, {"TextWin_InsertLineMarked", 0, false, V(H.textwin_insert_line_marked(self, a[0]))}},
        {0x004badd0, {"TextWin_Last", 0xffffffff, false, A(H.textwin_last())}},
        {0x004badf0, {"TextWin_DrawAll", 0, false, V(H.textwin_draw_all())}},
        {0x004bae70, {"Data_XorDecrypt", 0, false, V(H.xor_decrypt(a[0], a[1]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "front retail calls");
}
