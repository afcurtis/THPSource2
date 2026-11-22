// Replays retail forced calls (Phase 4.13b2) through the native network layer (net_*.cpp: the mouse and its widgets, the network state, flags and queues, the handshake and the
// per-frame update):
//
//   opentony_net_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of net_specs.py) holds the retail memory regions the ported code touches and, for
// every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "net_game.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::pcplatform;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    NetGame net;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), net(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using NetAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.net

const std::map<std::uint32_t, NetAdapter>& adapters() {
    static const std::map<std::uint32_t, NetAdapter> table = {
        // ---- the mouse and the pointer-driven widgets ----
        {0x004dbd90, {"Mouse_LoadCursors", 0, false, V(G.mouse_load_cursors())}},
        {0x004dbe60, {"Mouse_FreeCursors", 0, false, V(G.mouse_free_cursors())}},
        {0x004dbea0, {"MENUPC_DrawMouseCursor", 0, false, V(G.mouse_draw_cursor())}},
        {0x004dbee0, {"Mouse_Track", 0, false, V(G.mouse_track(a[0], a[1], a[2]))}},
        {0x004dc020, {"Mouse_EnableCursors", 0, false, V(G.mouse_enable(a[0]))}},
        {0x004dc050, {"NetKeys_TranslateName", 0xffffffff, false, A(G.translate_key_name(a[0]))}},
        {0x004dc090, {"Text_DotsToSpaces", 0, false, V(G.dots_to_spaces(a[0]))}},
        {0x004dc0b0, {"NetGame_BlankText", 0xffffffff, false, A(G.blank_text())}},
        {0x004dc0c0, {"NetMenu_BannerStyle", 0, false, V(G.banner_style())}},
        {0x004dc100, {"TextEdit_Construct", 0xffffffff, false, A(G.edit_construct(self, a[0], a[1], a[2], a[3]))}},
        {0x004dc160, {"TextEdit_DeletingDestructor", 0xffffffff, false, A(G.edit_delete(self, a[0]))}},
        {0x004dc180, {"TextEdit_Destruct", 0, false, V(G.edit_destruct(self))}},
        {0x004dc190, {"TextEdit_Update", 0, false, V(G.text_edit_update(self))}},
        {0x004dc370, {"TextEdit_SetBuffer", 0, false, V(G.edit_set_buffer(self, a[0], a[1], a[2]))}},
        {0x004dd390, {"Mouse_PointerFrame", 0xff, false, A(G.pointer_frame())}},
        {0x004dc3a0, {"NetMenu_ParkListLoad", 0, false, V(G.file_list_load(self, a[0], a[1]))}},
        {0x004dc620, {"NetMenu_Nothing", 0, false, V()}},
        {0x004dc530, {"MenuGrid_PickByMouse", 0xff, false, A(G.grid_pick(self))}},
        {0x004dc5c0, {"Button_PickByMouse", 0xff, false, A(G.button_pick(self))}},
        {0x004dc630, {"Button_PickByMouseJump", 0xff, false, A(G.button_pick(self))}},
        {0x004dc640, {"ColumnList_PickByMouse", 0xff, false, A(G.column_list_pick(self))}},
        {0x004dc8a0, {"RightButton_PickByMouse", 0xff, false, A(G.right_button_pick(self))}},
        {0x004dc8d0, {"ArrowBar_PickByMouse", 0xff, false, A(G.arrow_pair_pick(self))}},
        {0x004dc9d0, {"NetMenu_FalseA", 0xff, false, A(0)}},
        {0x004dcea0, {"NetMenu_NothingArg", 0, false, V()}},
        {0x004dceb0, {"NetMenu_FalseB", 0xff, false, A(0)}},
        {0x004dd170, {"NetMenu_FalseC", 0xff, false, A(0)}},
        {0x004dd180, {"ArrowChildren_PickByMouse", 0xff, false, A(G.arrow_children_pick(self))}},
        {0x004dc7b0, {"Slider_PickByMouse", 0xff, false, A(G.slider_pick(self))}},
        {0x004dc9e0, {"NetMenu_MinusOne", 0xffffffff, false, A(0xffffffffu)}},
        {0x004dcb00, {"MenuWidget_SetColour", 0, false, V(G.widget_set_colour(self, a[0], a[1], a[2]))}},
        {0x004dcbc0, {"NetKeys_CodeByName", 0, false, V(G.key_code_by_name(self, a[0]))}},
        {0x004dd520, {"MenuRows_PickByMouse", 0xff, false, A(G.pick_row(self))}},
        {0x004dd6d0, {"MenuWidget_SetTint", 0, false, V(G.set_tint(self, a[0], a[1], a[2]))}},
        // ---- the network state, its flags and queues ----
        {0x004dd890, {"NetGame_Reset", 0, false, V(G.net_reset(a[0]))}},
        {0x004ddb70, {"NetGame_Close", 0, false, V(G.net_close())}},
        {0x004ddbe0, {"NetGame_Nothing", 0, false, V()}},
        {0x004ddc00, {"NetGame_LoadSettings", 0, false, V(G.load_settings())}},
        {0x004ddce0, {"NetGame_SaveSettings", 0, false, V(G.save_settings())}},
        {0x004dddb0, {"NetGame_ResolveSessionName", 0xffffffff, false, A(G.resolve_name(a[0]))}},
        {0x004dde10, {"NetGame_Connect", 0xff, false, A(G.connect(a[0], a[1]))}},
        {0x004ddf30, {"Skater_RegisterPlayer", 0, false, V(G.register_player(a[0]))}},
        {0x004de000, {"NetGame_ClearPlayers", 0, false, V(G.clear_players())}},
        {0x004de010, {"NetGame_IsRemotePlayer", 0xff, false, A(G.is_remote_player(a[0]))}},
        {0x004de040, {"NetGame_IsHost", 0xff, false, A(G.is_host())}},
        {0x004de060, {"NetGame_IsGuest", 0xff, false, A(G.is_guest())}},
        {0x004de0a0, {"NetGame_PauseState", 0xff, false, A(G.pause_state(a[0], a[1]))}},
        {0x004de150, {"NetGame_ModeUsesNet", 0xff, false, A(G.game_mode_runs_net())}},
        {0x004de170, {"NetGame_AbortRequested", 0xff, false, A(G.abort_requested())}},
        {0x004de180, {"NetGame_SetCustomLevel", 0, false, V(G.set_custom_level(S(a[0]), a[1]))}},
        {0x004de270, {"NetGame_RestoreLevel", 0, false, V(G.restore_level())}},
        {0x004de2b0, {"NetGame_LoadCustomLevel", 0xff, false, A(G.load_custom_level())}},
        {0x004de300, {"NetGame_GuestValue", 0xffffffff, false, A(G.tint_or(a[0]))}},
        {0x004de320, {"NetGame_StashCareerState", 0, false, V(G.stash_career_state())}},
        {0x004de420, {"NetGame_RestoreCareerState", 0, false, V(G.restore_career_state())}},
        {0x004de4d0, {"NetGame_SetLocalFlag", 0, false, V(G.set_local_flag(a[0], a[1]))}},
        {0x004de500, {"NetGame_SetRemoteFlag", 0, false, V(G.set_remote_flag(a[0], a[1]))}},
        {0x004de570, {"NetGame_SetLocalState", 0, false, V(G.set_local_state(a[0]))}},
        {0x004de5c0, {"NetGame_TagAcknowledged", 0, false, V(G.tag_acknowledged(a[0]))}},
        {0x004de5f0, {"NetGame_SpawnHitSparks", 0, false, V(G.spawn_hit_sparks())}},
        {0x004de7c0, {"NetGame_PlayerTagged", 0, false, V(G.player_tagged(a[0], a[1], a[2]))}},
        {0x004de9c0, {"NetGame_InputReset", 0, false, V(G.input_reset())}},
        {0x004df030, {"NetGame_DrawStatusText", 0, false, V(G.status_text_draw())}},
        {0x004df160, {"NetGame_NothingLog", 0, false, V()}},
        {0x004df1d0, {"NetRing_Pop", 0xffffffff, false, A(G.ring_pop(a[0], a[1]))}},
        {0x004df220, {"NetRing_Remove", 0, false, V(G.ring_remove(a[0], a[1]))}},
        {0x004df250, {"NetRing_Push", 0, false, V(G.ring_push(a[0]))}},
        {0x004df280, {"NetRing_Clear", 0, false, V(G.ring_clear(a[0]))}},
        {0x004df2c0, {"NetGame_OnPoint", 0, false, V(G.on_point(a[0], a[1], a[2], a[3]))}},
        {0x004df300, {"NetTable_Add", 0, false, V(G.table_add(a[0], a[1], a[2]))}},
        {0x004dfa00, {"NetTable_Take", 0xffffffff, false, A(G.table_take(a[0], a[1], a[2]))}},
        {0x004dfda0, {"NetTable_Remove", 0xff, false, A(G.table_remove(a[0], a[1], a[2]))}},
        {0x004e00b0, {"NetGame_NothingB", 0, false, V()}},
        {0x004e0170, {"NetGame_SendStateIfFive", 0, false, V(G.sync_if_five(a[0]))}},
        // ---- the handshake and the per-frame update ----
        {0x004deb00, {"NetGame_BeginMatch", 0, false, V(G.begin_match())}},
        {0x004deb50, {"NetGame_EndMatch", 0, false, V(G.end_match())}},
        {0x004dece0, {"NetGame_PauseAgreement", 0xff, false, A(G.pause_agreement())}},
        {0x004dee50, {"NetGame_GameOver", 0, false, V(G.disconnected())}},
        {0x004defc0, {"NetGame_EndRun", 0, false, V(G.end_run())}},
        {0x004df370, {"NetGame_SendString", 0, false, V(G.send_string(a[0]))}},
        {0x004df6d0, {"NetGame_SendState", 0, false, V(G.send_state())}},
        {0x004dfe50, {"NetGame_WaitForOptions", 0, false, V(G.wait_for_options())}},
        {0x004dffb0, {"NetGame_WaitForLoading", 0, false, V(G.wait_for_loading())}},
        {0x004e02d0, {"NetGame_Update", 0, false, V(G.update())}},
        {0x004e1230, {"NetGame_OnEventMessage", 0, false, V(G.on_event_message(a[0]))}},
        {0x004e1250, {"NetGame_OnCustomLevelMessage", 0, false, V(G.on_custom_level(a[0]))}},
        {0x004e1370, {"NetGame_OnSystemMessage", 0, false, V(G.on_system_message(a[0], a[1]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "network layer retail forced calls");
}
