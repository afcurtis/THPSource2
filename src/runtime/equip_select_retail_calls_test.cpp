// Replays retail forced calls (Phase 4.10f2) through the native equipment-select screen (equipsel.cpp):
//
//   opentony_equip_select_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the eqw, eqs, eqr... groups) holds the retail memory regions the
// ported code touches and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp.
#include "equip_select.hpp"
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
    frontend::EquipSelect screen;
    explicit Ctx(RetailMemory memory)
        : mem(std::move(memory)), fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc),
          screen(mem, svc, fonts, window, menu, skaters) {
        // the screen loop is replayed with its window's constructor as a seam (see ScreenKit::set_loop_seams)
        screen.set_loop_seams(true);
        // a child of one of the window-base or this screen's own classes runs its method natively (the others are reported as services)
        window.set_dispatch([this](const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args) { return screen.dispatch_named(name, object, args); });
        menu.set_extra_dispatch([this](std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) { return screen.dispatch(object, slot, args); });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using EqAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define R c.screen

const std::map<std::uint32_t, EqAdapter>& adapters() {
    static const std::map<std::uint32_t, EqAdapter> table = {
        // ---- the callbacks, the loop and the deck helpers ----
        {0x00444510, {"Equip_ColoursMatch", 0xff, false, A(R.colours_match(S(a[0]), S(a[1])))}},
        {0x00444550, {"EquipSelect_NoOp", 0, false, V(R.no_op())}},
        {0x00444560, {"EquipSelect_InitCallbacks", 0, false, V(R.init_callbacks(a[0]))}},
        {0x00444580, {"EquipSelect_ExitCallback", 0, false, V(R.exit_callback())}},
        {0x00444590, {"EquipSelect_ScreenCall", 0, false, V(R.screen_call(a[0], a[1]))}},
        {0x004445b0, {"EquipSelect_Run", 0xffffffff, false, A(R.run(S(a[0]), S(a[1])))}},
        {0x00444810, {"Equip_DeckPrice", 0xffffffff, false, A(R.deck_price(S(a[0]), S(a[1])))}},
        {0x00444880, {"Equip_BuyDeck", 0, false, V(R.buy_deck(S(a[0]), S(a[1])))}},
        {0x004448c0, {"Equip_OwnsDeck", 0xff, false, A(R.owns_deck(S(a[0]), S(a[1])))}},
        {0x004448f0, {"Equip_IsCurrentCharacter", 0xff, false, A(R.is_current_character(S(a[0])))}},
        {0x00444900, {"Equip_CanAfford", 0xff, false, A(R.can_afford(S(a[0]), S(a[1])))}},
        {0x00444940, {"Equip_AllocZeroed", 0xffffffff, false, A(R.alloc_zeroed(a[0]))}},
        {0x00444970, {"Equip_Free", 0, false, V(R.free_block(a[0]))}},
        {0x00445810, {"Equip_DeckStat", 0xffffffff, false, A(R.deck_stat(a[0], S(a[1]), S(a[2])))}},
        {0x004458b0, {"Equip_CountOwnedDecks", 0xffffffff, false, A(R.count_owned_decks())}},
        {0x004468a0, {"Equip_CharacterShortName", 0, false, V(R.character_short_name(S(a[0]), a[1]))}},
        // ---- the window ----
        {0x00444980, {"EquipWindow_Construct", 0xffffffff, false, A(R.window_construct(self))}},
        {0x00444ff0, {"EquipWindow_ScalarDelete", 0xffffffff, false, A(R.window_delete(self, a[0]))}},
        {0x00445010, {"EquipWindow_Destroy", 0, false, V(R.window_destroy(self))}},
        {0x004450a0, {"EquipWindow_Draw", 0, false, V(R.window_draw(self))}},
        {0x004450e0, {"EquipWindow_Update", 0, false, V(R.window_update(self))}},
        {0x00445540, {"EquipWindow_Slot4", 0, false, V(R.window_slot4())}},
        {0x00445550, {"EquipWindow_Step", 0xff, false, A(R.window_step(self, a[0]))}},
        // ---- the stat dots, the cash window ----
        {0x004455c0, {"StatDots_Construct", 0xffffffff, false, A(R.dots_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004456a0, {"StatDots_ScalarDelete", 0xffffffff, false, A(R.dots_delete(self, a[0]))}},
        {0x004456c0, {"StatDots_Destroy", 0, false, V(R.dots_destroy(self))}},
        {0x00445740, {"StatDots_Draw", 0, false, V(R.dots_draw(self, S(a[0]), S(a[1])))}},
        {0x00445780, {"StatDots_Update", 0, false, V(R.dots_update(self))}},
        {0x00445920, {"CashWindow_Construct", 0xffffffff, false, A(R.cash_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x00445cf0, {"CashWindow_ScalarDelete", 0xffffffff, false, A(R.cash_delete(self, a[0]))}},
        {0x00445d10, {"CashWindow_Destroy", 0, false, V(R.cash_destroy(self))}},
        {0x00445d80, {"CashWindow_Update", 0, false, V(R.cash_update(self))}},
        {0x00445e70, {"CashWindow_SetVisible", 0, false, V(R.cash_set_visible(self, a[0]))}},
        // ---- the view, the name window ----
        {0x00445ea0, {"EquipView_Construct", 0xffffffff, false, A(R.view_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004460a0, {"EquipView_ScalarDelete", 0xffffffff, false, A(R.view_delete(self, a[0]))}},
        {0x004460c0, {"EquipView_Destroy", 0, false, V(R.view_destroy(self))}},
        {0x00446150, {"EquipView_Update", 0, false, V(R.view_update(self))}},
        {0x00446180, {"EquipView_Draw", 0, false, V(R.view_draw(self, S(a[0]), S(a[1])))}},
        {0x00446420, {"EquipView_Move", 0, false, V(R.view_move(self, a[0]))}},
        {0x00446440, {"EquipView_SetPage", 0, false, V(R.view_set_page(self, a[0]))}},
        {0x00446470, {"EquipView_ClearFlag", 0, false, V(R.view_clear_flag(self, S(a[0]), S(a[1])))}},
        {0x00446490, {"NameWindow_Construct", 0xffffffff, false, A(R.name_construct(self, S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x00446650, {"NameWindow_ScalarDelete", 0xffffffff, false, A(R.name_delete(self, a[0]))}},
        {0x00446670, {"NameWindow_Destroy", 0, false, V(R.name_destroy(self))}},
        {0x004466d0, {"NameWindow_Update", 0, false, V(R.name_update(self, S(a[0]), S(a[1])))}},
        // ---- the racks ----
        {0x004468e0, {"RackBase_Init", 0xffffffff, false, A((R.rack_init(self), self))}},
        {0x00446900, {"RackBase_Slot2", 0, false, V(R.rack_slot2())}},
        {0x00446910, {"RackBase_Slot3", 0, false, V(R.rack_slot3())}},
        {0x00446920, {"RackBase_ScalarDelete", 0xffffffff, false, A(R.rack_delete(self, a[0]))}},
        {0x00446940, {"RackBase_Destroy", 0, false, V(R.rack_destroy(self))}},
        {0x00446950, {"RackBase_SetActive", 0, false, V(R.rack_set_active(self, a[0]))}},
        {0x00446990, {"RackBase_Step", 0, false, V(R.rack_step(self))}},
        {0x004469f0, {"DeckRack_Construct", 0xffffffff, false, A(R.deck_rack_construct(self))}},
        {0x00446c20, {"DeckRack_ScalarDelete", 0xffffffff, false, A(R.deck_rack_delete(self, a[0]))}},
        {0x00446c40, {"DeckRack_Destroy", 0, false, V(R.deck_rack_destroy(self))}},
        {0x00446d80, {"DeckRack_BuildSets", 0, false, V(R.deck_rack_build_sets(self))}},
        {0x00446fb0, {"DeckRack_Update", 0, false, V(R.deck_rack_update(self))}},
        {0x00447340, {"DeckRack_Draw", 0, false, V(R.deck_rack_draw(self, a[0], a[1]))}},
        {0x00447440, {"DeckRack_Move", 0, false, V(R.deck_rack_move(self, a[0]))}},
        {0x004474b0, {"DeckRack_Turn", 0, false, V(R.deck_rack_turn(self, a[0]))}},
        {0x00447730, {"DeckRack_Scroll", 0, false, V(R.deck_rack_scroll(self, S(a[0])))}},
        {0x00447a90, {"DeckRack_ClearFlag", 0, false, V(R.deck_rack_clear_flag(self, S(a[0]), S(a[1])))}},
        {0x00448230, {"TruckRack_Construct", 0xffffffff, false, A(R.truck_construct(self))}},
        {0x00448480, {"TruckRack_ScalarDelete", 0xffffffff, false, A(R.truck_delete(self, a[0]))}},
        {0x004484a0, {"TruckRack_Destroy", 0, false, V(R.truck_destroy(self))}},
        {0x00448540, {"TruckRack_Update", 0, false, V(R.truck_update(self))}},
        {0x00448740, {"TruckRack_Draw", 0, false, V(R.truck_draw(self, a[0], a[1]))}},
        {0x00448790, {"TruckRack_Move", 0, false, V(R.truck_move(self, a[0]))}},
        // ---- a deck's picture and the spool queue ----
        {0x00447ae0, {"DeckItem_Construct", 0xffffffff, false, A(R.item_construct(self, a[0], a[1], a[2]))}},
        {0x00447b70, {"DeckItem_FreeThunk", 0, false, V(R.item_free_thunk(self))}},
        {0x00447b80, {"DeckItem_SetData", 0, false, V(R.item_set_data(self, a[0]))}},
        {0x00447be0, {"DeckItem_Free", 0, false, V(R.item_free(self))}},
        {0x00447c00, {"DeckItem_SetWanted", 0, false, V(R.item_set_wanted(self, a[0], S(a[1])))}},
        {0x00447c20, {"DeckItem_Update", 0, false, V(R.item_update(self))}},
        {0x00447cf0, {"DeckItem_PriorityLessEqual", 0xff, false, A(R.item_priority_le(self, a[0]))}},
        {0x00447d10, {"DeckSpool_Construct", 0xffffffff, false, A(R.spool_construct(self, a[0]))}},
        {0x00447d30, {"DeckSpool_Reset", 0, false, V(R.spool_reset(self))}},
        {0x00447d70, {"DeckSpool_ClearQueue", 0, false, V(R.spool_clear_queue(self))}},
        {0x00447e00, {"DeckSpool_Insert", 0, false, V(R.spool_insert(self, a[0]))}},
        {0x00447e80, {"DeckSpool_Begin", 0, false, V(R.spool_begin(self))}},
        {0x00447f40, {"DeckSpool_StartNext", 0, false, V(R.spool_start_next(self))}},
        {0x00448110, {"DeckSpool_Poll", 0xff, false, A(R.spool_poll(self))}},
        {0x004481c0, {"DeckSpool_Verify", 0, false, V(R.spool_verify(self, a[0]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "equip select retail calls");
}
