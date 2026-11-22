// Replays retail forced calls (Phase 4.10b) through the native HUD code:
//
//   opentony_hud_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the panel group) holds
// the retail memory regions the ported code touches (the live skater, the globals) and, for every case, the state,
// the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "hud_panel.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    hud::Panel panel;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), panel(mem, svc) {}
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using HudAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack
// arguments, `self` is `this` for a thiscall function.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }

const std::map<std::uint32_t, HudAdapter>& adapters() {
    static const std::map<std::uint32_t, HudAdapter> table = {
        // ---- the score stack (panel.cpp) ----
        {0x0048caf0, {"TrickScore_SumEntryValues", 0xffffffff, false, A(c.panel.sum_entry_values(self))}},
        {0x0048cb60, {"TrickScore_ChainValue", 0xffffffff, false, A(c.panel.chain_value(self, S(a[0])))}},
        {0x0048cb10, {"TrickScore_ChainValueIfFirst", 0xffffffff, false, A(c.panel.chain_value_if_first(self, S(a[0])))}},
        {0x0048cbc0, {"TrickScore_ComputeStackSum", 0xffffffff, false, A(c.panel.compute_stack_sum(self))}},
        {0x0048cd40, {"TrickScore_ComputeLandedValue", 0xffffffff, false, A(c.panel.compute_landed_value(self))}},
        {0x0048d960, {"TrickScore_InitEntry", 0, false, V(c.panel.init_entry(self, a[0], a[1], a[2]))}},
        {0x0048e3b0, {"TrickScore_PushEntry", 0, false, V(c.panel.push_entry(self, a[0], S(a[1]), S(a[2]), a[3], a[4]))}},
        {0x0048d710, {"TrickScore_AdjustCurrentStackEntry", 0, false, V(c.panel.adjust_current_entry(a[0], S(a[1])))}},
        {0x0048e620, {"TrickScore_ExpireEntries", 0, false, V(c.panel.expire_entries(self))}},
        {0x0048e680, {"TrickScore_CommitLandedStack", 0, false, V(c.panel.commit_landed_stack(self))}},
        {0x0048d7a0, {"TrickScore_CommitForSkater", 0, false, V(c.panel.commit_for_skater(a[0]))}},
        {0x0048e8a0, {"TrickScore_RollbackStack", 0, false, V(c.panel.rollback_stack(self))}},
        {0x0048d7b0, {"Panel_Bail", 0, false, V(c.panel.bail_for_skater(a[0]))}},
        {0x0048ddc0, {"Panel_RefreshTrickText", 0, false, V(c.panel.refresh_text(self))}},
        {0x0048e320, {"Panel_ShowStackText", 0, false, V(c.panel.show_stack_text(self))}},
        // ---- the display (panel.cpp) ----
        {0x0048b2b0, {"Panel_NoOp", 0, false, V((void)0)}},
        {0x0048b2c0, {"Panel_AllocFramePrimitive", 0xffffffff, false, A(c.panel.alloc_frame_primitive(a[0]))}},
        {0x0048b430, {"Panel_PlaceFrameQuadWide", 0, false, V(c.panel.place_frame_quad_wide(S(a[0]), S(a[1]), a[2], a[3]))}},
        {0x0048b540, {"Panel_PlaceFrameQuad", 0, false, V(c.panel.place_frame_quad(S(a[0]), S(a[1]), a[2], a[3]))}},
        {0x0048b620, {"Panel_DrawFlatShadedPoly", 0xffffffff, false,
                      A(c.panel.draw_flat_poly(S(a[0]), S(a[1]), S(a[2]), S(a[3]), a[4], a[5], a[6], S(a[7])))}},
        {0x0048b760, {"Panel_DrawGouraudPoly", 0xffffffff, false,
                      [](Ctx& c, const Args& a, std::uint32_t) -> std::uint32_t {
                          const std::uint32_t colours[6] = {a[4], a[5], a[6], a[7], a[8], a[9]};
                          return c.panel.draw_gouraud_poly(S(a[0]), S(a[1]), S(a[2]), S(a[3]), colours, S(a[10]));
                      }}},
        {0x0048b8e0, {"DisplayTapesSkate", 0, false, V(c.panel.display_tapes())}},
        {0x0048ba70, {"GraffitiColor", 0, false, V(c.panel.graffiti_color(a[0]))}},
        {0x0048bae0, {"DisplayScore", 0, false, V(c.panel.display_score(a[0], a[1], a[2]))}},
        {0x0048bef0, {"Panel_DrawSwitchIcon", 0, false, V(c.panel.draw_switch_icon(a[0], a[1], a[2]))}},
        {0x0048c0b0, {"Panel_DrawBalanceIcon", 0, false, V(c.panel.draw_balance_icon(a[0], a[1], a[2]))}},
        {0x0048c1a0, {"Panel_DisplayTrickPoints", 0, false, V(c.panel.display_trick_points(a[0], a[1], a[2]))}},
        {0x0048c520, {"DisplayHolding", 0, false, V(c.panel.display_holding(a[0], a[1], a[2]))}},
        {0x0048c760, {"Panel_ViewportOffset", 0, false, V(c.panel.viewport_offset(a[0], a[1], a[2]))}},
        {0x0048c8d0, {"Panel_DisplayLevelMessage", 0, false, V(c.panel.display_level_message(a[0], a[1], a[2]))}},
        {0x0048ca00, {"Panel_ReplayInstructions", 0, false, V(c.panel.replay_instructions())}},
        {0x0048cd80, {"Panel_DisplayTrickNames", 0, false, V(c.panel.display_trick_names(self, a[0], a[1], a[2]))}},
        {0x0048d030, {"Panel_NoOp2", 0, false, V((void)0)}},
        {0x0048d040, {"Panel_ObsoleteViewportMode", 0, false, V(c.panel.obsolete_viewport_mode())}},
        {0x0048d090, {"Panel_Display", 0, false, V(c.panel.display())}},
        {0x0048d660, {"Panel_SetPlayerNumber", 0, false, V(c.panel.set_player_number(a[0], a[1]))}},
        {0x0048d6d0, {"Trick_ResetScoreState", 0, false, V(c.panel.reset_trick_state(a[0]))}},
        {0x0048d7c0, {"Panel_SkateLetterCollected", 0xffffffff, false, A(c.panel.skate_letter_collected(S(a[0])))}},
        {0x0048d830, {"Panel_TapeCollected", 0, false, V(c.panel.tape_collected(S(a[0])))}},
        {0x0048d8a0, {"Panel_CollectibleCounted", 0, false, V(c.panel.collectible_counted())}},
        {0x0048e950, {"Panel_ShowStealsMessage", 0, false, V(c.panel.show_steals_message(self))}},
        {0x0048e9c0, {"CPanelData__OutOfBounds", 0, false, V(c.panel.message_out_of_bounds(self))}},
        // ---- construction ----
        {0x0048d990, {"Panel_Construct", 0, false, V(c.panel.construct(self, a[0]))}},
        {0x0048ad50, {"Panel_LoadRuntimeSprites", 0, false, V(c.panel.load_runtime_sprites(self))}},
        {0x0048b100, {"Panel_Uninit", 0, false, V(c.panel.uninit())}},
        // ---- the skater side of the score (physics.cpp) ----
        {0x0048f720, {"TrickScore_AdvancePointTimer", 0, false, V(c.panel.advance_point_timer(self, S(a[0])))}},
        {0x0048f690, {"Gap_ApplyDefinitionState", 0, false, V(c.panel.apply_gap(self, a[0], S(a[1]), S(a[2]), a[3]))}},
        {0x0048f5f0, {"Trick_TakeOff", 0, false, V(c.panel.take_off(self))}},
        {0x0048f8f0, {"Trick_Land", 0, false, V(c.panel.land(self))}},
        {0x0048f800, {"Trick_RegisterObject", 0, false, V(c.panel.register_trick_object(self, a[0]))}},
        {0x0048f7c0, {"Trick_LandPending", 0, false, V(c.panel.land_pending(self))}},
        {0x0048f8a0, {"Trick_LandingQuality", 0xffffffff, false, A(c.panel.landing_quality(self))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "hud retail calls");
}
