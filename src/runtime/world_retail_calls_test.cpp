// Replays retail forced calls (Phase 4.15c) through the native world-object layer (world_*.cpp: the object base class, the trigger loader's classes, the mechanisms, cars, power-ups,
// platforms and trick objects):
//
//   opentony_world_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of world_specs*.py) holds the retail memory regions the ported code touches and, for
// every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "retail_calls_replay.hpp"
#include "world_kit.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::worldkit;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    WorldKit world;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), world(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
        if (!mem.contains(0x00568638, 4) && !mem.contains(0x0056881c, 0x40)) {
            mem.add_region(0x00568638, 4);                       // the assertion location (line, file name) the trig.cpp macros leave: TRG_DecodeNodePosition stamps it
            mem.add_region(0x0056881c, 0x40);
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using WorldAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.world

const std::map<std::uint32_t, WorldAdapter>& adapters() {
    static const std::map<std::uint32_t, WorldAdapter> table = {
        // ---- the object base class (world_object.cpp)
        {0x0047fe30, {"Object_SelectNamedPsxRegion", 0, false, V(G.select_psx_region(self, a[0]))}},
        {0x0047fe90, {"CItem__Flash", 0, false, V(G.item_flash(self, S(a[0]), a[1], a[2], a[3]))}},
        {0x0047ffc0, {"CItem__FlashUpdate", 0, false, V(G.item_flash_update(self))}},
        {0x00480060, {"Object_Construct", 0xffffffff, false, A(G.object_construct(self))}},
        {0x00480140, {"Object_ScalarDelete", 0xffffffff, false, A(G.object_scalar_delete(self, a[0]))}},
        {0x00480160, {"Object_Destruct", 0, false, V(G.object_destruct(self))}},
        {0x00480720, {"Object_DestructThunk", 0, false, V(G.object_destruct(self))}},
        {0x004801c0, {"Object_RecordDestructThunk", 0, false, V(G.record_destruct_thunk(self))}},
        {0x00480240, {"TRG_ReadFixedPosition", 0xffffffff, false, A(G.read_fixed_position(self, a[0]))}},
        {0x004802c0, {"TRG_ReadObjectParameters", 0xffffffff, false, A(G.read_object_parameters(self, a[0]))}},
        {0x004803e0, {"Object_SetStateBit", 0xffffffff, false, A(G.set_state_bit(self, a[0], a[1]))}},
        {0x00480420, {"Object_WantSprite", 0, false, V(G.want_sprite(self))}},
        {0x00480430, {"Object_ReleaseSprite", 0, false, V(G.release_sprite(self))}},
        {0x00480460, {"Object_SpriteUpdate", 0, false, V(G.sprite_update(self))}},
        {0x004805d0, {"Object_MarkDyingIfIdle", 0, false, V(G.mark_dying_if_idle(self))}},
        {0x004805f0, {"Object_IsDying", 0xffffffff, false, A(G.is_dying(self))}},
        {0x00480600, {"Object_AlwaysTrue", 0xffffffff, false, A(G.always_true())}},
        {0x00480610, {"Animated_Construct", 0xffffffff, false, A(G.animated_construct(self))}},
        {0x00480660, {"Animated_ScalarDelete", 0xffffffff, false, A(G.animated_scalar_delete(self, a[0]))}},
        {0x00480680, {"Animated_Destruct", 0, false, V(G.animated_destruct(self))}},
        {0x00480c30, {"CSuper_FrameReached", 0xffffffff, false, A(G.frame_reached(self, a[0], S(a[1])))}},
        {0x00480d90, {"Skater_MergeModelParts", 0, false, V(G.merge_model_parts(self, a[0], S(a[1]), a[2], S(a[3])))}},
        {0x00480f50, {"Animation_UpdatePoseFrame", 0, false, V(G.update_pose_frame(self, a[0]))}},
        {0x0046dd60, {"Skater_Update", 0, false, V(G.skater_update(self))}},
        {0x00480fd0, {"Ob_ClearInputFlags", 0, false, V(G.object_clear_input_flags(a[0]))}},
        {0x00481110, {"Ob_QuickAI", 0, false, V(G.object_quick_ai(a[0]))}},
        {0x00481150, {"Ob_MaybeUnSuspendOrCull", 0, false, V(G.object_maybe_unsuspend())}},
        // ---- the script object, the baddy and the type-0xcb baddy (world_baddy.cpp)
        {0x00401000, {"ScriptObject_ClearFlag", 0, false, V(G.script_object_clear_flag(a[0]))}},
        {0x00401030, {"ScriptObject_SetFlag", 0, false, V(G.script_object_set_flag(a[0]))}},
        {0x00401100, {"ScriptObject_ScalarDelete", 0xffffffff, false, A(G.script_object_scalar_delete(self, a[0]))}},
        {0x00401120, {"ScriptObject_Destruct", 0, false, V(G.script_object_destruct(self))}},
        {0x004011d0, {"Object_AddSpin", 0, false, V(G.add_spin(self))}},
        {0x004011e0, {"M3dUtils_InBetween", 0, false, V(G.assertion(a[0], a[1], 0))}},
        {0x00401240, {"Object_EmptyHook_00401240", 0, false, V(G.empty_hook())}},
        {0x00401250, {"Object_EmptyHook_00401250", 0, false, V(G.empty_hook())}},
        {0x00401260, {"Object_EmptyHook_00401260", 0, false, V(G.empty_hook())}},
        {0x00401270, {"Baddy_Construct", 0xffffffff, false, A(G.baddy_construct(self))}},
        {0x004012d0, {"Baddy_ScalarDelete", 0xffffffff, false, A(G.baddy_scalar_delete(self, a[0]))}},
        {0x004012f0, {"Baddy_Destruct", 0, false, V(G.baddy_destruct(self))}},
        {0x00401410, {"Baddy_StepPhysics", 0, false, V(G.step_physics(self))}},
        {0x00402f80, {"Object_AiViewOfPlayer", 0xffffffff, false, A(G.ai_view_of_player(self, a[0], a[1], a[2]))}},
        {0x00402fc0, {"Object_AiViewByPlayer", 0xffffffff, false, A(G.ai_view_by_player(self, a[0], a[1], a[2]))}},
        {0x00403000, {"TRG_ConstructSubtype0x00cb", 0xffffffff, false, A(G.construct_subtype_0xcb(self, a[0], a[1]))}},
        {0x004030b0, {"Subtype0x00cb_ScalarDelete", 0xffffffff, false, A(G.subtype_0xcb_scalar_delete(self, a[0]))}},
        {0x004030d0, {"Subtype0x00cb_Destruct", 0, false, V(G.subtype_0xcb_destruct(self))}},
        {0x00403400, {"TRG_ApplyScriptFieldWordThunk", 0xffffffff, false, A(G.apply_script_field_word_thunk(self, a[0]))}},
        // ---- the trigger file (world_trig.cpp)
        {0x004c4b10, {"Trig_ClearTrigMenu", 0, false, V(G.trig_clear_menu())}},
        {0x004c4b30, {"Trig_DeleteCommandPoints", 0, false, V(G.trig_delete_command_points())}},
        {0x004c4b80, {"Trig_ResetCPCollisionFlags", 0, false, V(G.trig_reset_collision_flags(a[0]))}},
        {0x004c4ba0, {"Trig_ResetCPExecutedFlags", 0, false, V(G.trig_reset_executed_flags(a[0]))}},
        {0x004c4bd0, {"Trig_TriggerCommandPoint", 0xffffffff, false, A(G.trig_trigger_command_point(a[0]))}},
        {0x004c4c50, {"Trig_FindRestartByName", 0, false, V(G.trig_find_restart_by_name(a[0]))}},
        {0x004c4d10, {"FudgeAngles", 0, false, V(G.fudge_angles(a[0]))}},
        {0x004c4e30, {"Trig_ExecuteRestart", 0, false, V(G.trig_execute_restart())}},
        {0x004c5100, {"Trig_DeleteTrigFile", 0, false, V(G.trig_delete_file())}},
        {0x004c5130, {"TRG_LoadFile", 0, false, V(G.trg_load_file(a[0]))}},
        {0x004c5380, {"Trig_InitDefaultFile", 0, false, V(G.trig_init_default_file())}},
        {0x004c5420, {"TRG_FindObjectPreambleFlag", 0xff, false, A(G.trg_find_preamble_flag(a[0], a[1]))}},
        {0x004c5440, {"TRG_FindObjectPreambleEnd", 0xffffffff, false, A(G.trg_find_preamble_end(a[0]))}},
        {0x004c5ac0, {"TRG_FindCommandPointLink", 0xffffffff, false, A(G.trg_find_command_point_link(a[0]))}},
        {0x004c5d70, {"TRG_SkipAlignedCString", 0xffffffff, false, A(G.trg_skip_aligned_cstring(a[0]))}},
        {0x004c5d90, {"Trig_AllocateDualBuffer", 0, false, V(G.trig_allocate_dual_buffer())}},
        {0x004c5da0, {"Trig_RemoveDualBuffer", 0, false, V(G.trig_remove_dual_buffer())}},
        {0x004c7ea0, {"Trig_ScanRestart", 0, false, V(G.trig_scan_restart())}},
        {0x004c8050, {"TRG_ExecuteAutoexec", 0, false, V(G.trg_execute_autoexec())}},
        {0x004c84d0, {"TRG_CreateLinkRecord", 0xffffffff, false, A(G.trg_create_link_record(a[0], a[1], a[2]))}},
        {0x004c8550, {"TRG_GetNodeLinks", 0xffffffff, true, A(G.trg_get_node_links(a[0]))}},
        {0x004c8650, {"TRG_DecodeNodePosition", 0xffffffff, true, A(G.trg_decode_node_position(a[0], a[1]))}},
        {0x004c8f10, {"Utils_StartFade", 0, false, V(G.start_fade(a[0], a[1]))}},
        {0x004c8f30, {"Utils_StopFade", 0, false, V(G.stop_fade())}},
        // ---- the trigger-record writers (world_cmd.cpp)
        {0x004c87c0, {"TrgWriter_Construct", 0xffffffff, false, A(G.cmd_construct(self))}},
        {0x004c87e0, {"TrgWriter_ScalarDelete_004C87E0", 0xffffffff, false, A(G.cmd_scalar_delete(self, a[0]))}},
        {0x004c88c0, {"TrgWriter_ScalarDelete_004C88C0", 0xffffffff, false, A(G.cmd_scalar_delete(self, a[0]))}},
        {0x004c8a50, {"TrgWriter_ScalarDelete_004C8A50", 0xffffffff, false, A(G.cmd_scalar_delete(self, a[0]))}},
        {0x004c8bf0, {"TrgWriter_ScalarDelete_004C8BF0", 0xffffffff, false, A(G.cmd_scalar_delete(self, a[0]))}},
        {0x004c8c60, {"TrgWriter_ScalarDelete_004C8C60", 0xffffffff, false, A(G.cmd_scalar_delete(self, a[0]))}},
        {0x004c8d00, {"TrgWriter_ScalarDelete_004C8D00", 0xffffffff, false, A(G.cmd_scalar_delete(self, a[0]))}},
        {0x004c8dd0, {"TrgWriter_ScalarDelete_004C8DD0", 0xffffffff, false, A(G.cmd_scalar_delete(self, a[0]))}},
        {0x004c8e80, {"TrgWriter_ScalarDelete_004C8E80", 0xffffffff, false, A(G.cmd_scalar_delete(self, a[0]))}},
        {0x004c8800, {"TrgWriter_Destruct_004C8800", 0, false, V(G.cmd_destruct(self))}},
        {0x004c88e0, {"TrgWriter_Destruct_004C88E0", 0, false, V(G.cmd_destruct(self))}},
        {0x004c8a70, {"TrgWriter_Destruct_004C8A70", 0, false, V(G.cmd_destruct(self))}},
        {0x004c8c10, {"TrgWriter_Destruct_004C8C10", 0, false, V(G.cmd_destruct(self))}},
        {0x004c8c80, {"TrgWriter_Destruct_004C8C80", 0, false, V(G.cmd_destruct(self))}},
        {0x004c8d20, {"TrgWriter_Destruct_004C8D20", 0, false, V(G.cmd_destruct(self))}},
        {0x004c8df0, {"TrgWriter_Destruct_004C8DF0", 0, false, V(G.cmd_destruct(self))}},
        {0x004c8ea0, {"TrgWriter_Destruct_004C8EA0", 0, false, V(G.cmd_destruct(self))}},
        {0x004c8810, {"TrgWriter_Size", 0xffffffff, false, A(G.cmd_size(self))}},
        {0x004c8870, {"TrgNode_Construct", 0xffffffff, false, A(G.node_construct(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x004c88f0, {"TrgNode_Write", 0xffffffff, false, A(G.node_write(self, a[0]))}},
        {0x004c8a20, {"TrgLink_Construct", 0xffffffff, false, A(G.link_construct(self, a[0]))}},
        {0x004c8a80, {"TrgLink_SetEnds", 0, false, V(G.link_set_ends(self, a[0], a[1]))}},
        {0x004c8ab0, {"TrgLink_Write", 0xffffffff, false, A(G.link_write(self, a[0]))}},
        {0x004c8bd0, {"TrgEnd_Construct", 0xffffffff, false, A(G.end_construct(self))}},
        {0x004c8c20, {"TrgEnd_Write", 0xffffffff, false, A(G.end_write(self, a[0]))}},
        {0x004c8c30, {"TrgCommandC_Construct", 0xffffffff, false, A(G.command_c_construct(self, a[0]))}},
        {0x004c8c90, {"TrgCommandC_Write", 0xffffffff, false, A(G.command_c_write(self, a[0]))}},
        {0x004c8cd0, {"TrgCommandE_Construct", 0xffffffff, false, A(G.command_e_construct(self, a[0], a[1], a[2]))}},
        {0x004c8d30, {"TrgCommandE_Write", 0xffffffff, false, A(G.command_e_write(self, a[0]))}},
        {0x004c8da0, {"TrgCommandF_Construct", 0xffffffff, false, A(G.command_f_construct(self, a[0], a[1]))}},
        {0x004c8e00, {"TrgCommandF_Write", 0xffffffff, false, A(G.command_f_write(self, a[0]))}},
        {0x004c8e50, {"TrgCommandG_Construct", 0xffffffff, false, A(G.command_g_construct(self, a[0]))}},
        {0x004c8eb0, {"TrgCommandG_Write", 0xffffffff, false, A(G.command_g_write(a[0]))}},
        {0x004c8ed0, {"TrgCommandNone_Write", 0xffffffff, false, A(G.command_none_write())}},
        {0x004c8ee0, {"Editor_ClearWordsThunk", 0, false, V(G.clear_editor_words())}},
        {0x004c8ef0, {"Editor_ClearWords", 0, false, V(G.clear_editor_words())}},
        // ---- the trick objects (world_trick.cpp)
        {0x004bd700, {"TrickObject_DeleteAll", 0, false, V(G.trick_delete_all())}},
        {0x004bd720, {"TrickObject_DeleteAllPreserve", 0, false, V(G.trick_delete_all_preserve())}},
        {0x004bd740, {"TRG_IsGoalObjectNode", 0xffffffff, false, A(G.trg_is_goal_object_node(a[0]))}},
        {0x004bd760, {"TrickObject_Construct", 0xffffffff, false, A(G.trick_object_construct(self, a[0], a[1]))}},
        {0x004bd920, {"TrickObject_ScalarDelete", 0xffffffff, false, A(G.trick_object_scalar_delete(self, a[0]))}},
        {0x004bd940, {"TrickObject_Destruct", 0, false, V(G.trick_object_destruct(self))}},
        {0x004bd980, {"TrickObject_ActivateForObject", 0, false, V(G.trick_object_activate_for_object(a[0], a[1], a[2]))}},
        {0x004bdbd0, {"TrickObject_Take", 0, false, V(G.trick_object_take(a[0]))}},
        {0x004bdc40, {"TrickObject_ActivateGoalObject", 0, false, V(G.trick_object_activate_goal_object(a[0]))}},
        {0x004bdd00, {"TrickObject_Update", 0, false, V(G.trick_object_update())}},
        {0x004bdea0, {"TrickObject_LineRender", 0, false, V(G.trick_object_line_render(a[0], a[1], a[2], a[3], a[4]))}},
        {0x004be0b0, {"TrickObject_DrawBox", 0, false, V(G.trick_object_draw_box(a[0], a[1]))}},
        {0x004be340, {"TrickObject_EmptyHook", 0, false, V(G.empty_hook())}},
        {0x004be350, {"TrickObject_OfferAll", 0, false, V(G.trick_object_offer_all())}},
        // ---- the skater object (world_skater.cpp)
        {0x0046c5f0, {"CBruce__BoardOff", 0, false, V(G.board_off(self))}},
        {0x0046c630, {"CBruce__BoardOn", 0, false, V(G.board_on(self))}},
        {0x0046c720, {"Skater_Construct", 0xffffffff, false, A(G.skater_construct(self))}},
        {0x0046da70, {"CBruce__BloodSplat", 0, false, V(G.blood_splat(self))}},
        {0x0046e080, {"CBruce__HandleWater", 0, false, V(G.handle_water(self))}},
        {0x0046d7a0, {"Skater_ProbeGround", 0, false, V(G.probe_ground(self))}},
        {0x0046d2e0, {"Skater_Shade", 0, false, V(G.skater_shade(self, a[0], a[1], a[2], a[3]))}},
        {0x0046d160, {"Skater_ApplyShadeLighting", 0, false, V(G.skater_apply_shade_lighting(self))}},
        {0x0046c670, {"Skater_ResetOrientationRows", 0, false, V(G.reset_orientation_rows(self))}},
        {0x0046cbe0, {"Skater_ScalarDelete", 0xffffffff, false, A(G.skater_scalar_delete(self, a[0]))}},
        {0x0046cc00, {"Skater_DestroyPanelMembers", 0, false, V(G.skater_destroy_panel_members(self))}},
        {0x0046cca0, {"Skater_Destruct", 0, false, V(G.skater_destruct(self))}},
        {0x0046cdf0, {"Skater_NameHash", 0xffffffff, false, A(G.name_hash(a[0], a[1]))}},
        {0x0046ce20, {"Skater_NameFind", 0xffffffff, false, A(G.name_find(self, a[0], a[1]))}},
        {0x0046ce60, {"Skater_NameReset", 0, false, V(G.name_reset(self))}},
        {0x0046ce70, {"Skater_NameAdd", 0xffffffff, true, A(G.name_add(self, a[0], a[1]))}},
        {0x0046cf20, {"Skater_NameRelease", 0, false, V(G.name_release(self, a[0], a[1]))}},
        {0x0046cfe0, {"Skater_NameCount", 0xffffffff, false, A(G.name_count(self, a[0], a[1]))}},
        {0x0046d010, {"Skater_StepToward", 0xffffffff, false, A(WorldKit::step_toward(a[0], a[1], a[2]))}},
        {0x0046d040, {"Skater_FadeLightColour", 0, false, V(G.fade_light_colour(self))}},
        {0x0046d1d0, {"Skater_DistanceToLine", 0xffffffff, false, A(G.distance_to_line(a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x0046d220, {"Skater_LineIntersection", 0, false, V(G.line_intersection(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]))}},
        {0x0046d6e0, {"Skater_InputProfilePrep", 0, false, V(G.input_profile_prep(self))}},
        {0x0046d750, {"Skater_MarkDyingAndVisible", 0, false, V(G.mark_dying_and_visible(self))}},
        {0x0046d770, {"Skater_EmptyHook", 0, false, V(G.empty_hook())}},
        {0x0046d780, {"Skater_AlwaysTrue3", 0xffffffff, false, A(G.always_true())}},
        {0x0046d790, {"Skater_EmptyHook1", 0, false, V(G.empty_hook())}},
        {0x0046d940, {"Player_BindHookPacket", 0, false, V(G.player_bind_hook_packet(self, a[0]))}},
        {0x0046d970, {"CBruce__FlipToVelocity", 0xffffffff, false, A(G.flip_to_velocity(self))}},
        {0x0046dd20, {"Skater_RequestBloodSplat", 0, false, V(G.request_blood_splat(self, a[0], a[1]))}},
        {0x0046e1d0, {"SFX_Grunt", 0, false, V(G.skater_grunt())}},
        {0x0046e3f0, {"Droplet_ScalarDelete", 0xffffffff, false, A(G.droplet_scalar_delete(self, a[0]))}},
        {0x0046e410, {"Droplet_Destruct", 0, false, V(G.droplet_destruct(self))}},
        {0x0046e460, {"Droplet_EmptyHook", 0, false, V(G.empty_hook())}},
        {0x0046e470, {"Droplet_Update", 0, false, V(G.droplet_update(self))}},
        {0x0046e4f0, {"Droplet_CheckedUpdate", 0, false, V(G.droplet_checked_update(self))}},
        {0x004138d0, {"Car_AlwaysTrue", 0xffffffff, false, A(G.always_true())}},
        {0x004573f0, {"Image_NothingTwo", 0, false, V(G.empty_hook())}},
        {0x00459bd0, {"Window_Nothing", 0, false, V(G.empty_hook())}},
        // ---- the platform, power-up and traffic methods (world_extra.cpp)
        {0x0049f340, {"Platform_ScalarDelete", 0xffffffff, false, A(G.platform_scalar_delete(self, a[0]))}},
        {0x0049f360, {"Platform_ConstructAt", 0xffffffff, false, A(G.platform_construct_at(self, a[0]))}},
        {0x0049f440, {"Platform_Destruct", 0, false, V(G.platform_destruct(self))}},
        {0x004a07d0, {"Platform_EmptyHook", 0, false, V(G.empty_hook())}},
        {0x004a81d0, {"Powerup_ScalarDelete", 0xffffffff, false, A(G.powerup_scalar_delete(self, a[0]))}},
        {0x004a81f0, {"Powerup_Destruct", 0, false, V(G.powerup_destruct(self))}},
        {0x004a8250, {"maybe_destroy_model", 0, false, V(G.powerup_kill(self))}},
        {0x004a8260, {"Powerup_SetFields", 0, false, V(G.powerup_set_fields(self, a[0], a[1]))}},
        {0x004a8280, {"Powerup_AllowSprite", 0, false, V(G.powerup_allow_sprite())}},
        {0x004a8290, {"Powerup_Release", 0, false, V(G.powerup_release(self))}},
        {0x004a82c0, {"CPowerUp__SetNode", 0, false, V(G.powerup_set_node(self, a[0]))}},
        {0x004a82e0, {"Powerup_MarkDying", 0, false, V(G.powerup_mark_dying(self))}},
        {0x00401520, {"TRG_RunScriptStream", 0, false, V(G.trg_run_script_stream(self, a[0]))}},
        {0x004015e0, {"TRG_ExecuteBaddyScriptOpcode", 0xffffffff, false, A(G.base_script_command(self, a[0]))}},
        {0x004014a0, {"TRG_SkipConditionalBlock", 0, false, V(G.skip_conditional_block(self))}},
        {0x004027f0, {"TRG_ApplyScriptFieldWord", 0, false, V(G.base_script_field(self, a[0]))}},
        {0x00402bb0, {"TRG_GetScriptVariable", 0xffff, false, A(G.base_script_variable(self, a[0]))}},
        {0x004a1270, {"CPlatform__GetScriptVariable", 0xffffffff, false, A(G.platform_script_variable(self, a[0]))}},
        {0x00403410, {"TRG_ApplyScriptCommand", 0xffffffff, false, A(G.script_object_command(self, a[0]))}},
        {0x004a1060, {"CPlatform__ApplyScriptField", 0, false, V(G.platform_script_field(self, a[0]))}},
        {0x004a07e0, {"CPlatform__ExecuteScriptCommand", 0xffffffff, false, A(G.platform_script_command(self, a[0]))}},
        {0x0049f4c0, {"CPlatform__Bounce", 0, false, V(G.platform_bounce(self, a[0], a[1]))}},
        {0x0049f780, {"CPlatform__BouncyAI", 0, false, V(G.platform_bouncy_ai(self))}},
        {0x0049fef0, {"CPlatform__Update", 0, false, V(G.platform_update(self))}},
        {0x004a0360, {"CPlatform__Hit", 0xffffffff, false, A(G.platform_hit(self, a[0]))}},
        {0x004031a0, {"TRG_UpdateScriptObject", 0, false, V(G.script_object_update(self))}},
        {0x00466c10, {"LevelEvent_InitializeAndDispatch", 0, false, V(G.level_event_dispatch())}},
        {0x004c4620, {"TrickTips_Update", 0, false, V(G.tips_update())}},
        {0x004c4950, {"TrickTips_Show", 0, false, V(G.tips_show(self, a[0]))}},
        {0x004c4420, {"TrickTips_Display", 0, false, V(G.tips_display())}},
        {0x004044d0, {"CBit_EmptyHook", 0, false, V(G.empty_hook())}},
        {0x004044e0, {"CBit_EmptyHook1", 0, false, V(G.empty_hook())}},
        {0x004057e0, {"CBit_EmptyHook2", 0, false, V(G.empty_hook())}},
        {0x004a03e0, {"CPlatform__DoPhysics", 0, false, V(G.platform_do_physics(self))}},
        {0x004a8300, {"CPowerUp__DoPhysics", 0, false, V(G.powerup_do_physics(self))}},
        {0x004013b0, {"CBaddy__Die", 0, false, V(G.baddy_die(self))}},
        {0x004013d0, {"CBaddy__SendDeathPulse", 0, false, V(G.baddy_send_death_pulse(self))}},
        {0x004a86f0, {"CPowerUp__TakeEffect", 0, false, V(G.powerup_take_effect(self, a[0]))}},
        {0x004a8620, {"CPowerUp__CheckAge", 0, false, V(G.powerup_check_age(self))}},
        {0x004a8ac0, {"Powerup_Update", 0, false, V(G.powerup_update(self))}},
        {0x00412ef0, {"Traffic_Check", 0, false, V(G.traffic_check(a[0]))}},
        {0x00413000, {"TRG_ProcessTrafficInteraction", 0, false, V(G.traffic_interaction(a[0], a[1], a[2], a[3]))}},
        {0x004128a0, {"TRG_DeleteTrafficObject", 0xffffffff, false, A(G.traffic_scalar_delete(self, a[0]))}},
        {0x004128c0, {"TRG_DestroyTrafficObject", 0, false, V(G.traffic_destruct(self))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "world layer retail forced calls");
}
