// Replays retail forced calls (Phase 4.14c) through the native core layer (core_utils.cpp: utils.cpp and the vector helpers; core_pkr.cpp: the relocatable-module loader, the PKR2 wrappers and
// exp.cpp; core_init.cpp: init.cpp; core_main.cpp: main.cpp):
//
//   opentony_core_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of core_specs*.py) holds the retail memory regions the ported code touches and, for every
// case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "core_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::core;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    CoreKit core;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), core(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using CoreAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.core

const std::map<std::uint32_t, CoreAdapter>& adapters() {
    static const std::map<std::uint32_t, CoreAdapter> table = {
        // ---- utils.cpp ----
        {0x004c9300, {"Pause", 0, false, V(G.pause())}},
        {0x004c9320, {"Utils_InitialRand", 0, false, V(G.initial_rand(a[0]))}},
        {0x004c9390, {"Utils_RotateY", 0, false, V(G.rotate_y(a[0], a[1], S(a[2])))}},
        {0x004c9400, {"Utils_RotateYQ12", 0, false, V(G.rotate_y_q12(a[0], a[1], S(a[2])))}},
        {0x004c9470, {"Utils_CalcUnit", 0, false, V(G.unit_vector(a[0], a[1]))}},
        {0x004c9500, {"Camera_DirectionFromAnglesRaw", 0, false, V(G.direction_from_angles(a[0], S(a[1]), a[2]))}},
        {0x004c9590, {"Utils_Dist", 0xffffffff, false, A(G.distance(a[0], a[1]))}},
        {0x004c95e0, {"Utils_XZDist", 0xffffffff, false, A(G.distance_xz(a[0], a[1]))}},
        {0x004c96d0, {"Utils_VectorCosineQ12", 0xffffffff, false, A(G.cosine_between(a[0], a[1], a[2]))}},
        {0x004c9770, {"Camera_BuildLookAngles", 0xffffffff, false, A(G.look_angles(a[0], a[1], a[2]))}},
        {0x004c9880, {"Utils_TurnTowards", 0, false, V(G.turn_towards(a[0], a[1], a[2], a[3], a[4], a[5], S(a[6])))}},
        {0x004c9cb0, {"Utils_ClearExplosionStack", 0, false, V(G.clear_explosion_stack())}},
        {0x004c9f50, {"CaseInsensitiveBoundedNameEqual", 0xffffffff, false, A(G.name_equal(a[0], a[1], a[2]))}},
        {0x004c9fd0, {"CPlatform__AI", 0xffffffff, false, A(G.platform_ai(a[0], a[1], S(a[2]), S(a[3]), S(a[4])))}},
        {0x004ca0a0, {"Camera_AcosRatioToAngle12", 0xffffffff, false, A(G.acos_ratio_to_angle(S(a[0])))}},
        {0x004ca0c0, {"Utils_LineOfSight", 0xffffffff, false, A(G.line_of_sight(a[0], a[1]))}},
        {0x004ca140, {"Utils_GetGroundHeight", 0xffffffff, false, A(G.ground_height(a[0], S(a[1]), S(a[2])))}},
        {0x004ca1e0, {"Utils_KillObjectsInBox", 0xffffffff, false, A(G.kill_objects_in_box(a[0], a[1], a[2], a[3]))}},
        {0x004ca270, {"RuntimeObject_ApplyBoundsToLists", 0xffffffff, false, A(G.apply_bounds_to_lists(a[0], a[1]))}},
        {0x004ca2d0, {"RuntimeObject_UpdateVisibilityBounds", 0, false, V(G.update_visibility_bounds(a[0], a[1], a[2], a[3]))}},
        {0x004ca690, {"Utils_CalculateSpatialAttenuation", 0xffffffff, false, A(G.spatial_attenuation(a[0], S(a[1]), S(a[2])))}},
        {0x004ca890, {"Utils_Pulse", 0xffffffff, false, A(G.pulse(S(a[0]), S(a[1]), S(a[2])))}},
        {0x004ca8d0, {"Utils_LimitRange", 0, false, V(G.limit_range(a[0], S(a[1]), S(a[2])))}},
        {0x004ca940, {"Math_Vector3SquaredLength", 0xffffffff, false, A(G.vector_squared_length(self))}},
        {0x004ca980, {"Math_Vector3Normalize", 0, false, V(G.vector_normalize(self))}},
        {0x004ca9b0, {"Math_Vector3DeadZone", 0, false, V(G.vector_dead_zone(self))}},
        {0x004cabb0, {"Math_Vector3AddOut", 0xffffffff, false, A(G.vector_add_out(a[0], a[1], a[2]))}},
        {0x004cabf0, {"Vector3_Subtract", 0xffffffff, false, A(G.vector_subtract_out(a[0], a[1], a[2]))}},
        {0x004cac30, {"Math_Vector3MulScalarOut", 0xffffffff, false, A(G.vector_mul_out(a[0], a[1], a[2]))}},
        {0x004cac60, {"Math_ScalarMulVector3Out", 0xffffffff, false, A(G.scalar_mul_out(a[0], a[1], a[2]))}},
        {0x004cac90, {"Math_Vector3DivScalarOut", 0xffffffff, false, A(G.vector_div_out(a[0], a[1], a[2]))}},
        {0x004cacd0, {"Math_Vector3ShiftRightOut", 0xffffffff, false, A(G.vector_shift_right_out(a[0], a[1], a[2]))}},
        {0x004cad00, {"Math_Vector3ShiftLeftOut", 0xffffffff, false, A(G.vector_shift_left_out(a[0], a[1], a[2]))}},
        {0x004cad30, {"Math_Vector3NegateOut", 0xffffffff, false, A(G.vector_negate_out(self, a[0]))}},
        {0x004cad60, {"Math_Vector3sMask12", 0xffffffff, false, A(G.vector_mask12(self))}},
        // ---- the relocatable-module loader, the PKR2 wrappers, the stream object, exp.cpp and the head file ----
        {0x004ac020, {"RelocatableModule_HashName", 0xffffffff, false, A(G.relocation_hash(a[0]))}},
        {0x004ac0b0, {"RelocatableModule_Nothing", 0, false, V(G.nothing())}},
        {0x004ac0c0, {"RelocatableModule_LoadPair", 0, false, V(G.load_module_pair(a[0], a[1]))}},
        {0x004ac2e0, {"RelocatableModule_ApplyRelocations", 0, false, V(G.apply_relocations(a[0], a[1]))}},
        {0x004e9150, {"PKR2_CompressBIBD", 0xffffffff, false, A(G.compress_bibd(a[0], S(a[1]), a[2]))}},
        {0x004e9200, {"PKR2_CompressWIBD", 0xffffffff, false, A(G.compress_wibd(a[0], S(a[1]), a[2]))}},
        {0x004e92c0, {"PKR2_CompressZlib", 0xffffffff, false, A(G.compress_zlib(a[0], S(a[1]), a[2]))}},
        {0x004e9390, {"PKR2_DecompressBIBD", 0xffffffff, false, A(G.decompress_bibd(a[0], S(a[1]), a[2], a[3]))}},
        {0x004e9480, {"PKR2_DecompressWIBD", 0xffffffff, false, A(G.decompress_wibd(a[0], S(a[1]), a[2], a[3]))}},
        {0x004e9570, {"PKR2_DecompressZlib", 0xffffffff, false, A(G.decompress_zlib(a[0], a[1], a[2], a[3]))}},
        {0x004e96b0, {"PKR2_CompressResource", 0xffffffff, false, A(G.compress_resource(a[0], a[1], a[2]))}},
        {0x004e9780, {"PKR_PreLoad", 0xffffffff, false, A(G.pkr_preload(a[0]))}},
        {0x004e9930, {"PKR_Log", 0, false, V(G.nothing())}},
        {0x004e9b80, {"PKR_UnLoad", 0, false, V(G.pkr_unload(a[0]))}},
        {0x004ebf10, {"Stream_Construct", 0xffffffff, false, A(G.stream_construct(self))}},
        {0x004ebf50, {"Stream_ScalarDelete", 0xffffffff, false, A(G.stream_scalar_delete(self, a[0]))}},
        {0x004ebf70, {"Stream_Destruct", 0, false, V(G.stream_destruct(self))}},
        {0x00448860, {"Exp_HitEnvItem", 0, false, V(G.exp_hit_env_item(a[0], a[1], a[2]))}},
        {0x00448990, {"Exp_Construct", 0xffffffff, false, A(G.exp_construct(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x00448aa0, {"Exp_ScalarDelete", 0xffffffff, false, A(G.exp_scalar_delete(self, a[0]))}},
        {0x00448ac0, {"Exp_Destruct", 0, false, V(G.exp_destruct(self))}},
        {0x00448ad0, {"Exp_Move", 0, false, V(G.exp_move(self))}},
        {0x00448c60, {"FileIO_LoadHed", 0, false, V(G.load_head())}},
        {0x00448c90, {"FileIO_UnloadHed", 0, false, V(G.unload_head())}},
        // ---- init.cpp ----
        {0x004584d0, {"DeleteList", 0, false, V(G.delete_list(a[0]))}},
        {0x00458270, {"Init_KillAll", 0, false, V(G.init_kill_all())}},
        {0x00458500, {"Init_Cleanup", 0, false, V(G.init_cleanup(a[0]))}},
        {0x00458690, {"Init_ResetMemory", 0, false, V(G.init_reset_memory(a[0]))}},
        {0x004586f0, {"Init_Restart", 0, false, V(G.init_restart())}},
        {0x00458730, {"Init_ForGame", 0, false, V(G.init_for_game())}},
        {0x00458750, {"Init_DebugStuff", 0, false, V(G.nothing())}},
        {0x00458760, {"Init_AtStart", 0, false, V(G.init_at_start())}},
        {0x00458840, {"Init_AtEnd", 0, false, V(G.init_at_end())}},
        // ---- main.cpp ----
        {0x00466b60, {"ResetMainDemoTimer", 0, false, V(G.reset_demo_timer())}},
        {0x00466b70, {"CClass_OpNew", 0xffffffff, false, A(G.class_new(a[0]))}},
        {0x00466bb0, {"CClass_OpDelete", 0, false, V(G.class_delete(a[0]))}},
        {0x00466be0, {"CalcPolyBufferEnd", 0, false, V(G.calc_poly_buffer_end())}},
        {0x00466c00, {"PollHost", 0, false, V(G.poll_host())}},
        {0x00466de0, {"Display_DrawDimOverlay", 0, false, V(G.draw_dim_overlay())}},
        {0x004674d0, {"Skater_LoadBitmap", 0, false, V(G.load_skater_bitmap(a[0], a[1]))}},
        {0x00467610, {"FlipPrimitives", 0, false, V(G.flip_primitives())}},
        {0x00467c90, {"M3D_RenderStage", 0, false, V(G.render_stage(a[0], a[1])), [](Ctx& c) -> Unspecified { return c.core.scrap(); }}},
        {0x00468350, {"Display_AddQuad", 0, false, V(G.add_quad(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]))}},
        {0x004684f0, {"Display_AddRect", 0, false, V(G.add_rect(a[0], a[1], a[2], a[3]))}},
        {0x00468520, {"Display", 0, false, V(G.display()), [](Ctx& c) -> Unspecified { return c.core.scrap(); }}},
        {0x00468b00, {"FPS_Init", 0, false, V(G.fps_init())}},
        {0x00468b30, {"AnimationClock_Update", 0, false, V(G.animation_clock_update())}},
        {0x00468cf0, {"PlayAway_Init", 0, false, V(G.play_away_init())}},
        {0x00468e90, {"Skater_SetupAppearance", 0, false, V(G.setup_appearance(a[0], a[1], a[2]))}},
        {0x004691e0, {"SkaterObjects_Create", 0, false, V(G.create_skater_objects())}},
        {0x00469400, {"Replay_ApplyLoadedHeaderAndRestart", 0xffffffff, false, A(G.replay_apply_loaded_header())}},
        {0x004695a0, {"Game_PrepareVideoMasks", 0, false, V(G.prepare_video_masks())}},
        {0x00469700, {"Game_ConfigureReplayStreams", 0, false, V(G.configure_replay_streams())}},
        {0x00469900, {"Game_PrepareVideoMasksWrapper", 0xffffffff, false, A(G.prepare_video_masks_wrapper())}},
        {0x00469910, {"Game_Init", 0, false, V(G.game_init())}},
        {0x004699f0, {"Game_InputUpdate", 0, false, V(G.game_input_update())}},
        {0x00469d30, {"CheckSkatersStop", 0, false, V(G.check_skaters_stop())}},
        {0x00469a30, {"Skater_UpdateAnimationObjects", 0, false, V(G.skater_update_animation_objects())}},
        {0x00469de0, {"Game_GameplayUpdate", 0, false, V(G.game_gameplay_update())}},
        {0x0046a0f0, {"Game_Display", 0, false, V(G.game_display()), [](Ctx& c) -> Unspecified { return c.core.scrap(); }}},
        {0x0046a1a0, {"MainLoop_End", 0, false, V(G.main_loop_end())}},
        {0x0046a250, {"Level_Teardown", 0, false, V(G.level_teardown())}},
        {0x0046a3a0, {"Game_LevelLoop", 0, false, V(G.game_level_loop()), [](Ctx& c) -> Unspecified { return c.core.scrap(); }}},
        {0x0046a500, {"IntroMovies", 0, false, V(G.intro_movies())}},
        {0x0046a580, {"LogoScreen", 0, false, V(G.logo_screen())}},
        {0x0046a600, {"Game_MessageScreen", 0, false, V(G.message_screen(a[0], a[1]))}},
        {0x0046a770, {"main", 0xffffffff, false, A(G.main_entry())}},
        {0x0046a8d0, {"LaunchGame", 0, false, V(G.launch_game(a[0]))}},
        // ---- unlisted functions of the same files ----
        {0x004ca620, {"Utils_IsEnvironmentObject", 0xffffffff, false, A(G.is_environment_object(a[0]))}},
        {0x004ca650, {"Utils_ShuffleShorts", 0, false, V(G.shuffle_shorts(a[0], S(a[1])))}},
        {0x004c9c90, {"Utils_FlagListObjects", 0, false, V(G.flag_list_objects(a[0]))}},
        {0x004c9fb0, {"Utils_StringCopy", 0, false, V(G.copy_c_string(a[0], a[1]))}},
        {0x00448cb0, {"FileIO_HashName", 0xffffffff, false, A(G.file_name_hash(a[0]))}},
        {0x004ac1b0, {"RelocatableModule_Empty", 0, false, V(G.nothing())}},
        {0x004ac060, {"RelocatableModule_Destroy", 0, false, V(G.module_destroy())}},
        {0x004e9630, {"PKR2_CompressedSize", 0xffffffff, false, A(G.compressed_size(a[0], S(a[1]), S(a[2])))}},
        {0x004e9670, {"PKR2_BestMethod", 0xffffffff, false, A(G.best_method(a[0], S(a[1]), a[2]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "core layer retail forced calls");
}
