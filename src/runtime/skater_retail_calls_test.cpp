// Replays retail forced calls (Phase 4.15a) through the native skater layer (skater_pose.cpp: the small skater functions of physics.cpp):
//
//   opentony_skater_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of phys_specs*.py) holds the retail memory regions the ported code touches and, for every
// case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "retail_calls_replay.hpp"
#include "skater_kit.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::skaterkit;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    SkaterKit skater;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), skater(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, 0x5000);                    // (the stack locals of the retail functions; the air physics keeps its own at +0x4000)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using SkaterAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.skater

const std::map<std::uint32_t, SkaterAdapter>& adapters() {
    static const std::map<std::uint32_t, SkaterAdapter> table = {
        {0x004aaf70, {"Rail_Update", 0, false, V(G.rail_update(a[0]))}},
        {0x00494210, {"Skater_UpdateRailPhysics", 0, false, V(G.update_rail_physics(self))}},
        {0x00499710, {"Skater_UpdateSweptContactState", 0, false, V(G.update_swept_contact_state(self))}},
        {0x0049a280, {"Skater_PrePhysicsOllie", 0, false, V(G.pre_physics_ollie(self))}},
        {0x0049c330, {"Skater_UprightCorrection", 0, false, V(G.upright_correction(self))}},
        {0x0049c970, {"CBruce__HandleContinuousSounds", 0, false, V(G.handle_continuous_sounds(self))}},
        {0x0048ec00, {"CBruce__TempTransform", 0, false, V(G.temp_transform(self))}},
        {0x0048fbe0, {"Skater_FindPartIndex", 0xffffffff, false, A(G.find_part_index(self, a[0]))}},
        {0x0048fc90, {"Skater_OffsetPartVertices", 0, false, V(G.offset_part_vertices(self, a[0]))}},
        {0x0048fd60, {"Skater_Fatten", 0, false, V(G.fatten(self, a[0], a[1], a[2]))}},
        {0x00490ae0, {"Skater_RegisterRailTrickObject", 0, false, V(G.register_rail_trick_object(self, a[0]))}},
        {0x00490c70, {"Skater_CompleteRailGap", 0, false, V(G.complete_rail_gap(self))}},
        {0x00490e30, {"Skater_EnterHandplant", 0, false, V(G.enter_handplant(self))}},
        {0x00493f40, {"Skater_SpawnTagGlass", 0, false, V(G.spawn_tag_glass(self))}},
        {0x00495af0, {"CBruce__DoTerrainEffects", 0, false, V(G.do_terrain_effects(self))}},
        {0x0048eb70, {"Animation_PingPong", 0xffffffff, false, A(G.ping_pong(S(a[0]), S(a[1]), S(a[2])))}},
        {0x0048f590, {"Skater_RestoreSavedPose", 0, false, V(G.restore_saved_pose(self))}},
        {0x0048f5d0, {"Skater_SetPanelNumber", 0, false, V(G.set_panel_number(self, a[0]))}},
        {0x004900b0, {"CBruce__StateTransition", 0, false, V(G.state_transition(self, a[0]))}},
        {0x004902e0, {"CBruce__MaybeFlipAnimationTransition", 0, false, V(G.flip_animation_transition(self))}},
        {0x004904c0, {"CBruce__ResetTrickBuffers", 0, false, V(G.reset_trick_buffers(self))}},
        {0x00490610, {"Skater_RemoveNormalComponent", 0, false, V(G.remove_normal_component(a[0], a[1], a[2]))}},
        {0x00490680, {"Skater_ProjectOntoPlaneKeepLength", 0, false, V(G.damp_to_ground_plane(a[0], a[1], a[2]))}},
        {0x0049c7d0, {"CBruce__FRUVectorsFromRotMatrix", 0, false, V(G.fru_from_rotation_matrix(self))}},
        {0x0049c850, {"Skater_PublishOrientationBasis", 0, false, V(G.publish_orientation_basis(self))}},
        {0x0049e430, {"CBruce__HandleFlips", 0, false, V(G.handle_flips(self))}},
        {0x0049e480, {"CBruce__HandleTimer", 0, false, V(G.handle_timer(self))}},
        {0x004903f0, {"Animation_RequestStart", 0, false, V(G.animation_request_start(self, a[0]))}},
        {0x00490420, {"Animation_RequestStartDefaultEnd", 0, false, V(G.animation_request_start_default_end(self, a[0], a[1]))}},
        {0x00490450, {"Animation_RequestRange", 0, false, V(G.animation_request_range(self, a[0], a[1], a[2]))}},
        {0x00490480, {"Animation_RequestFull", 0, false, V(G.animation_request_full(self, a[0], a[1], a[2], a[3]))}},
        {0x0048fb20, {"CBruce__Trick_Bail", 0, false, V(G.trick_bail(self))}},
        {0x0048ffc0, {"CBruce__Vibrate", 0, false, V(G.vibrate(self, a[0], a[1], a[2]))}},
        {0x004904d0, {"CBruce__Bail", 0, false, V(G.bail(self, a[0], a[1]))}},
        {0x004916d0, {"Skater_BailWhenFlipping", 0, false, V(G.bail_when_flipping(self))}},
        {0x00491c10, {"Skater_StartGrindTrick", 0, false, V(G.grind_trick_start(self, a[0]))}},
        {0x00490ef0, {"Skater_StartGrind", 0, false, V(G.start_grind(self, a[0]))}},
        {0x00491da0, {"TrickInput_PressTime", 0xffffffff, false, A(G.pad_press_time(self, a[0], a[1]))}},
        {0x00491e50, {"TrickInput_PressedTogether", 0xffffffff, false, A(G.pad_pressed_with(self, a[0], a[1], a[2]))}},
        {0x00491f40, {"TrickInput_BothRecent", 0xffffffff, false, A(G.pad_both_recent(self, a[0], a[1], a[2]))}},
        {0x00492a90, {"Tricks_LoadBinary", 0xffffffff, false, A(G.tricks_load_binary())}},
        {0x00492b20, {"UnloadGTricks", 0, false, V(G.unload_gtricks())}},
        {0x00492b60, {"TrickScript_ResetExtraAnimations", 0, false, V(G.reset_extra_animations())}},
        {0x00492ba0, {"TrickScript_RegisterAnimation", 0, false, V(G.register_animation(a[0]))}},
        {0x00492c70, {"TrickScript_IsKnownAnimation", 0xff, false, A(G.animation_in_lists(a[0]))}},
        {0x00492d10, {"TrickScript_ParseStream", 0, false, V(G.parse_stream(a[0]))}},
        {0x00492d50, {"TrickScript_ParseTable", 0, false, V(G.parse_table(a[0]))}},
        {0x00492de0, {"TrickScript_Initialize", 0, false, V(G.trick_script_initialize())}},
        {0x0048f090, {"Skater_DeckRecord", 0xffffffff, false, A(G.deck_record(a[0], a[1]))}},
        {0x0048f1d0, {"Skater_InitStats", 0, false, V(G.init_stats(self))}},
        {0x0048f3a0, {"Skater_ProfileValue", 0xffffffff, false, A(G.profile_value(self, a[0]))}},
        {0x00494020, {"Skater_AdvancePointTimerWhenMoved", 0, false, V(G.advance_point_timer_when_moved(self))}},
        {0x00496280, {"Skater_AnimationCompletionCheck", 0, false, V(G.animation_completion_check(self))}},
        {0x004962f0, {"Skater_Nothing", 0, false, V(G.nothing())}},
        {0x00496300, {"Skater_ClearTrickFlags", 0, false, V(G.clear_trick_flags(self))}},
        {0x0049a1b0, {"Skater_PlayTakeoffSound", 0, false, V(G.play_takeoff_sound(a[0]))}},
        {0x0049a1e0, {"Skater_PlayLandingSound", 0, false, V(G.play_landing_sound(a[0]))}},
        {0x0049c8d0, {"Skater_UpdateOrientationAngles", 0, false, V(G.orientation_angles(self))}},
        {0x0049d8a0, {"Skater_BlockedPhysicsReset", 0, false, V(G.blocked_physics_reset(self))}},
        {0x0049e570, {"Skater_UpdateNollieFlip", 0, false, V(G.update_nollie_flip(self))}},
        {0x0049e680, {"Skater_PhysicsFrame", 0, false, V(G.physics_frame(self))}},
        {0x00496550, {"Skater_DoGroundPhysics", 0, false, V(G.do_ground_physics(self))}},
        {0x00497f40, {"Skater_DoPhysicsInAir", 0, false, V(G.do_air_physics(self))}},
        {0x00497aa0, {"Skater_AirCollisionRecovery", 0, false, V(G.air_recovery_for_hit(self, a[0]))}},
        {0x00497df0, {"Skater_UpdateAirBasis", 0, false, V(G.update_air_basis(self))}},
        {0x004be450, {"TrickScript_Execute", 0, false, V(G.trick_script_execute(self))}},
        {0x00496360, {"Skater_GroundSurfaceResponseStep", 0, false, V(G.ground_surface_response(self))}},
        {0x004a12d0, {"Platform_HandleBounce", 0, false, V(G.platform_bounce(a[0]))}},
        {0x0049b500, {"Skater_ApplyTurnVelocityPhase", 0, false, V(G.apply_turn_velocity_phase(self, a[0], a[1], a[2]))}},
        {0x0049c060, {"Skater_ApplySurfaceResponseCorrection", 0, false, V(G.surface_response_correction(self, a[0]))}},
        {0x00496060, {"Skater_PositionWritePath", 0, false, V(G.position_write_path(self, a[0], a[1], a[2]))}},
        {0x0049bad0, {"Skater_ApplyCollisionResponse", 0, false, V(G.collision_response(self, a[0], a[1], a[2]))}},
        {0x0049d080, {"Skater_RebuildOrientationRecovery", 0, false, V(G.rebuild_orientation_recovery(self))}},
        {0x004956f0, {"Skater_ExitGroundCollisionRecovery", 0, false, V(G.exit_ground_collision_recovery(self))}},
        {0x00491780, {"Skater_AirCollisionOrientationRecovery", 0, false, V(G.air_collision_recovery(self, a[0], a[1]))}},
        {0x004914d0, {"Skater_LandingCleanup", 0, false, V(G.landing_cleanup(self))}},
        {0x00495cc0, {"Skater_GroundPostCollision", 0, false, V(G.ground_post_collision(self))}},
        {0x004957c0, {"Skater_GroundedBounceProbe", 0xffffffff, false, A(G.grounded_bounce_probe(self, a[0], a[1], a[2]))}},
        {0x00497960, {"Skater_AirLandingOrientationRecovery", 0, false, V(G.air_landing_orientation_recovery(self, a[0], a[1]))}},
        {0x00493370, {"Skater_ActionPhysicsStep", 0, false, V(G.action_physics_step(self))}},
        {0x00492f20, {"Skater_UpdateHeading", 0, false, V(G.update_heading(self))}},
        {0x0049b010, {"Skater_UpdateAcceleration", 0, false, V(G.update_acceleration(self))}},
        {0x00492120, {"TrickInput_DerivePattern", 0xffffffff, false, A(G.derive_trick_pattern(self))}},
        {0x00491c90, {"TrickInput_RecordState", 0, false, V(G.record_trick_state(self, a[0], a[1]))}},
        {0x00492190, {"TrickInput_RefreshHistory", 0, false, V(G.refresh_trick_history(self))}},
        {0x00492560, {"TrickInput_MatchHistory", 0xffffffff, false, A(G.match_trick_history(self, a[0], a[1], a[2], a[3], a[4]))}},
        {0x00492030, {"TrickInput_ConsumeRecord", 0, false, V(G.consume_trick_record(self, a[0], a[1]))}},
        {0x00492290, {"TrickScript_QueueStart", 0, false, V(G.queue_trick_start(self, a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x00492400, {"TrickScript_StartPending", 0, false, V(G.start_pending_trick(self))}},
        {0x004925e0, {"TrickInput_SelectSequence", 0, false, V(G.select_trick_sequence(self))}},
        {0x00492ea0, {"TrickScript_Update", 0, false, V(G.trick_script_update(self))}},
        {0x0049df00, {"Skater_UpdateBrakeMode", 0, false, V(G.update_brake_mode(self))}},
        {0x0049db80, {"Skater_PhysicsDispatcher", 0, false, V(G.physics_dispatcher(self))}},
        {0x0049dad0, {"Skater_GroundPreparation", 0, false, V(G.ground_preparation(self))}},
        {0x00490730, {"Skater_OuterFloorRecoveryCheck", 0, false, V(G.outer_floor_recovery(a[0], a[1], a[2], a[3]))}},
        {0x004624d0, {"M3D_InitCollisionQuery", 0, false, V(G.init_collision_query(a[0]))}},
        {0x004628f0, {"M3D_PrepareCollisionObjectList", 0, false, V(G.prepare_collision_objects(a[0], a[1]))}},
        {0x004638d0, {"M3D_CollisionCell", 0, false, V(G.collision_cell(a[0], a[1]))}},
        {0x00462a20, {"M3D_CollisionFaceTest", 0, false, V(G.collision_face_test(a[0], a[1], a[2]))}},
        {0x00463d50, {"M3D_CommitCollisionSurface", 0xffffffff, false, A(G.commit_collision_surface(a[0]))}},
        {0x0048ea80, {"M3D_ReadSurfaceFlags", 0xffffffff, false, A(G.read_surface_flags(a[0]))}},
        {0x00466090, {"M3D_QueryZoneCollision", 0xffffffff, false, A(G.query_zone_collision(a[0], a[1]))}},
        {0x004660b0, {"M3D_ZoneCollisionBroadphase", 0, false, V(G.zone_collision_broadphase(a[0], a[1]))}},
        {0x0049d9c0, {"Skater_RunGroundedBounceRecovery", 0, false, V(G.run_grounded_bounce_recovery(self))}},
        {0x00493e10, {"Skater_RebuildSidewaysBasis", 0, false, V(G.rebuild_sideways_basis(self))}},
        {0x00494090, {"CBruce__HandleGrindOffSound", 0, false, V(G.grind_off_sound(self))}},
        {0x00494180, {"CBruce__LeaveRail", 0, false, V(G.leave_rail(self))}},
        {0x004993f0, {"Skater_EnterFootplantState", 0, false, V(G.enter_footplant_state(self))}},
        {0x004995d0, {"Skater_UpdateSpecialBasisState", 0, false, V(G.update_special_basis_state(self))}},
        {0x00497bb0, {"CBruce__MaybeManual", 0, false, V(G.maybe_manual(self, a[0], a[1]))}},
        {0x004f53b0, {"Math_SqrtTruncated", 0xffffffff, false, A(G.sqrt_truncated(a[0]))}},
        {0x0049d480, {"Skater_DampResponseVector", 0, false, V(G.damp_response_vector(self))}},
        {0x0049d2f0, {"Skater_CollideWithOtherPlayer", 0, false, V(G.collide_with_other_player(self))}},
        {0x0049d740, {"CBruce__CheckForTriggers", 0, false, V(G.check_for_triggers(self))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "skater layer retail forced calls");
}
