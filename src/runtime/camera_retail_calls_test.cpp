// Replays retail forced calls (Phase 4.15b) through the native camera layer (camera_*.cpp: the quaternion math and the CCamera methods of camera.cpp):
//
//   opentony_camera_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of cam_specs.py) holds the retail memory regions the ported code touches and, for every
// case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "camera_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::camerakit;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    CameraKit camera;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), camera(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
        if (!mem.contains(0x7ff0f000, 0x10)) {
            mem.add_region(0x7ff0f000, 0x200);                   // Camera_ApplyEffects' frame (camera_effects.cpp)
        }
        if (!mem.contains(0x00568638, 4) && !mem.contains(0x0056881c, 0x40)) {
            mem.add_region(0x00568638, 4);                       // the assertion location (line, file name) camera.cpp's checks leave
            mem.add_region(0x0056881c, 0x40);
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using CameraAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.camera

std::array<std::uint32_t, 5> quaternion(const Args& a, std::size_t first) {
    return {a[first], a[first + 1], a[first + 2], a[first + 3], a[first + 4]};
}

const std::map<std::uint32_t, CameraAdapter>& adapters() {
    static const std::map<std::uint32_t, CameraAdapter> table = {
        {0x004a9650, {"Camera_ComposeTransformQ12", 0xffffffff, false, A(G.compose(a[0], a[1], a[2]))}},
        {0x004a9750, {"Camera_QuaternionFromRotationVector", 0, false, V(G.from_rotation_vector(a[0], a[1]))}},
        {0x004a9820, {"Camera_QuaternionAboutX", 0, false, V(G.about_x(a[0], S(a[1])))}},
        {0x004a9870, {"Camera_QuaternionAboutY", 0, false, V(G.about_y(a[0], S(a[1])))}},
        {0x004a98c0, {"Camera_QuaternionAboutZ", 0, false, V(G.about_z(a[0], S(a[1])))}},
        {0x004a9910, {"Camera_TransformToMatrixQ12", 0, false, V(G.to_matrix(a[0], a[1]))}},
        {0x004a9a00, {"Camera_MatrixToTransformQ12", 0, false, V(G.from_matrix(a[0], a[1]))}},
        {0x004a9bf0, {"Camera_SlerpTransformQ12", 0, false, V(G.slerp(quaternion(a, 0), quaternion(a, 5), S(a[10]), a[11]))}},
        {0x00410610, {"Camera_FollowTarget", 0, false, V(G.follow_target(self))}},
        {0x004113f0, {"Camera_Mode2", 0, false, V(G.mode_2(self))}},
        {0x004103e0, {"Camera_BuildTripodRotation", 0, false, V(G.build_tripod_rotation(self))}},
        {0x0040c1a0, {"CCamera__ResetOrientation", 0, false, V(G.reset_orientation(self))}},
        {0x0040be70, {"Camera_CommitViewportEffects", 0, false, V(G.commit_viewport(self))}},
        {0x00410c40, {"Camera_ResetAndSettle", 0, false, V(G.reset_and_settle(self))}},
        {0x00410c90, {"Camera_DeathMode", 0, false, V(G.death_mode(self))}},
        {0x00410f70, {"Camera_PointMode", 0, false, V(G.point_mode(self))}},
        {0x00410bb0, {"Camera_StartCompetitionIntro", 0, false, V(G.start_competition_intro(self))}},
        {0x0040f200, {"Camera_DrawDebugBars", 0, false, V(G.draw_debug_bars())}},
        {0x00411a00, {"CameraRig_Update", 0, false, V(G.rig_update(self))}},
        {0x00411c00, {"CameraRig_BuildMatrix", 0, false, V(G.rig_build_matrix(self))}},
        {0x00411ce0, {"CameraRig_Blend", 0, false, V(G.rig_blend(a[0], a[1], a[2], a[3]))}},
        {0x0040e090, {"Camera_SmoothAndValidate", 0, false, V(G.smooth_and_validate(self, a[0]))}},
        {0x0040f270, {"Camera_HandlePad", 0, false, V(G.handle_pad(self))}},
        {0x0040f850, {"Camera_Update", 0, false, V(G.update(self))}},
        {0x0040c370, {"Camera_ApplyEffects", 0, false, V(G.apply_effects(self))}},
        {0x004103b0, {"Camera_AngleDelta12", 0xffffffff, false, A(CameraKit::angle_delta(a[0], a[1]))}},
        {0x0040e000, {"Camera_QuaternionConstruct", 0xffffffff, false, A(G.quaternion_construct(self, a[0], a[1], a[2], a[3]))}},
        {0x0040e030, {"Camera_QuaternionScalarDelete", 0xffffffff, false, A(G.quaternion_scalar_delete(self, a[0]))}},
        {0x0040e050, {"Camera_QuaternionDestruct", 0, false, V(G.quaternion_destruct(self))}},
        {0x0040e060, {"Camera_QuaternionCopyConstruct", 0xffffffff, false, A(G.quaternion_copy_construct(self, a[0]))}},
        {0x004117f0, {"CameraRig_Construct", 0, false, V(G.rig_construct(self))}},
        {0x004119e0, {"CameraRig_Destruct", 0, false, V(G.rig_destruct(self))}},
        {0x00411880, {"CameraRig_ScalarDelete", 0xffffffff, false, A(G.rig_scalar_delete(self, a[0]))}},
        {0x004118a0, {"CameraRig_ConstructFromRecord", 0xffffffff, false, A(G.rig_construct_from_record(self, a[0]))}},
        {0x00411c50, {"CameraRig_CopyMatrix", 0, false, V(G.rig_copy_matrix(self, a[0]))}},
        {0x00411c70, {"CameraRig_GetPosition", 0, false, V(G.rig_get_position(self, a[0]))}},
        {0x00411ca0, {"CameraRig_GetTarget", 0, false, V(G.rig_get_target(self, a[0]))}},
        {0x00411ec0, {"CameraPoint_Destruct", 0, false, V(G.point_destruct(self))}},
        {0x00411ed0, {"CameraPoint_ScalarDelete", 0xffffffff, false, A(G.point_scalar_delete(self, a[0]))}},
        {0x00411ef0, {"CameraPoints_Clear", 0, false, V(G.points_clear())}},
        {0x00411f00, {"CameraPoints_ClearAndResetTuning", 0, false, V(G.points_clear_and_reset_tuning())}},
        {0x0040b650, {"SkaterCamera_Construct", 0xffffffff, false, A(G.construct(self, a[0], a[1]))}},
        {0x0040bc00, {"Camera_Destruct", 0, false, V(G.destruct(self))}},
        {0x0040bbe0, {"Camera_ScalarDelete", 0xffffffff, false, A(G.scalar_delete(self, a[0]))}},
        {0x0040bce0, {"Camera_SetOverrides", 0, false, V(G.set_overrides(self, a[0]))}},
        {0x0040bd00, {"Camera_SetOverrideA", 0, false, V(G.set_override_a(self, a[0]))}},
        {0x0040bd10, {"Camera_SetOverrideB", 0, false, V(G.set_override_b(self, a[0]))}},
        {0x0040bd20, {"Camera_SetWindowWidth", 0, false, V(G.set_window_width(self, a[0]))}},
        {0x0040bd30, {"Camera_SetWindowHeight", 0, false, V(G.set_window_height(self, a[0]))}},
        {0x0040c070, {"Camera_PushOverrides", 0, false, V(G.push_overrides(self))}},
        {0x0040c0e0, {"Camera_PopOverrides", 0, false, V(G.pop_overrides(self))}},
        {0x0040c130, {"Camera_PushOverrideB", 0, false, V(G.push_override_b(self))}},
        {0x0040c170, {"Camera_PopOverrideB", 0, false, V(G.pop_override_b(self))}},
        {0x0040be40, {"Camera_FlagObjectsForShake", 0, false, V(G.flag_objects_for_shake(a[0], a[1], a[2]))}},
        {0x0040bd40, {"Camera_ApplyShake", 0, false, V(G.apply_shake(self, a[0], a[1]))}},
        {0x0040f1a0, {"Camera_SetBlendTarget", 0, false, V(G.set_blend_target(self, a[0], a[1]))}},
        {0x0040f1f0, {"Camera_GetBlendValue", 0xffffffff, false, A(G.blend_value(self))}},
        {0x0040f130, {"Camera_ClearFlag419", 0, false, V(G.clear_flag_419(self))}},
        {0x004113e0, {"CCamera__InCompetitionIntro", 0xff, false, A(G.in_competition_intro(self))}},
        {0x0040be60, {"Camera_ShakeHook", 0, false, V((void)0)}},
        {0x0040f0d0, {"Camera_Hook_F0D0", 0, false, V((void)0)}},
        {0x0040f0e0, {"Camera_Hook_F0E0", 0, false, V((void)0)}},
        {0x0040f0f0, {"Camera_Hook_F0F0", 0, false, V((void)0)}},
        {0x0040f100, {"Camera_Hook_F100", 0, false, V((void)0)}},
        {0x0040f110, {"Camera_Hook_F110", 0, false, V((void)0)}},
        {0x0040f120, {"Camera_Hook_F120", 0, false, V((void)0)}},
        {0x0040f140, {"Camera_Hook_F140", 0, false, V((void)0)}},
        {0x0040f150, {"Camera_Hook_F150", 0, false, V((void)0)}},
        {0x0040f160, {"Camera_Hook_F160", 0, false, V((void)0)}},
        {0x0040f170, {"Camera_Hook_F170", 0, false, V((void)0)}},
        {0x0040f180, {"Camera_Hook_F180", 0, false, V((void)0)}},
        {0x0040f190, {"Camera_Hook_F190", 0, false, V((void)0)}},
        {0x004117a0, {"CameraRig_Hook_7A0", 0, false, V((void)0)}},
        {0x004117b0, {"CameraRig_Hook_7B0", 0, false, V((void)0)}},
        {0x004117c0, {"CameraRig_Hook_7C0", 0, false, V((void)0)}},
        {0x004117d0, {"CameraRig_Hook_7D0", 0, false, V((void)0)}},
        {0x004117e0, {"CameraRig_Hook_7E0", 0, false, V((void)0)}},
        {0x00411cc0, {"CameraRig_Hook_CC0", 0, false, V((void)0)}},
        {0x00411cd0, {"CameraRig_Hook_CD0", 0, false, V((void)0)}},
        {0x0040ed70, {"Camera_TuningThunk_0040ED80", 0, false, V(G.tune_0040ed80())}},
        {0x0040ed80, {"Camera_Tuning_0040ED80", 0, false, V(G.tune_0040ed80())}},
        {0x0040eda0, {"Camera_TuningThunk_0040EDB0", 0, false, V(G.tune_0040edb0())}},
        {0x0040edb0, {"Camera_Tuning_0040EDB0", 0, false, V(G.tune_0040edb0())}},
        {0x0040edd0, {"Camera_TuningThunk_0040EDE0", 0, false, V(G.tune_0040ede0())}},
        {0x0040ede0, {"Camera_Tuning_0040EDE0", 0, false, V(G.tune_0040ede0())}},
        {0x0040ee00, {"Camera_TuningThunk_0040EE10", 0, false, V(G.tune_0040ee10())}},
        {0x0040ee10, {"Camera_Tuning_0040EE10", 0, false, V(G.tune_0040ee10())}},
        {0x0040ee30, {"Camera_TuningThunk_0040EE40", 0, false, V(G.tune_0040ee40())}},
        {0x0040ee40, {"Camera_Tuning_0040EE40", 0, false, V(G.tune_0040ee40())}},
        {0x0040ee60, {"Camera_TuningThunk_0040EE70", 0, false, V(G.tune_0040ee70())}},
        {0x0040ee70, {"Camera_Tuning_0040EE70", 0, false, V(G.tune_0040ee70())}},
        {0x0040ee90, {"Camera_TuningThunk_0040EEA0", 0, false, V(G.tune_0040eea0())}},
        {0x0040eea0, {"Camera_Tuning_0040EEA0", 0, false, V(G.tune_0040eea0())}},
        {0x0040eec0, {"Camera_TuningThunk_0040EED0", 0, false, V(G.tune_0040eed0())}},
        {0x0040eed0, {"Camera_Tuning_0040EED0", 0, false, V(G.tune_0040eed0())}},
        {0x0040eef0, {"Camera_TuningThunk_0040EF00", 0, false, V(G.tune_0040ef00())}},
        {0x0040ef00, {"Camera_Tuning_0040EF00", 0, false, V(G.tune_0040ef00())}},
        {0x0040ef20, {"Camera_TuningThunk_0040EF30", 0, false, V(G.tune_0040ef30())}},
        {0x0040ef30, {"Camera_Tuning_0040EF30", 0, false, V(G.tune_0040ef30())}},
        {0x0040ef50, {"Camera_TuningThunk_0040EF60", 0, false, V(G.tune_0040ef60())}},
        {0x0040ef60, {"Camera_Tuning_0040EF60", 0, false, V(G.tune_0040ef60())}},
        {0x0040ef80, {"Camera_TuningThunk_0040EF90", 0, false, V(G.tune_0040ef90())}},
        {0x0040ef90, {"Camera_Tuning_0040EF90", 0, false, V(G.tune_0040ef90())}},
        {0x0040efb0, {"Camera_TuningThunk_0040EFC0", 0, false, V(G.tune_0040efc0())}},
        {0x0040efc0, {"Camera_Tuning_0040EFC0", 0, false, V(G.tune_0040efc0())}},
        {0x0040efe0, {"Camera_TuningThunk_0040EFF0", 0, false, V(G.tune_0040eff0())}},
        {0x0040eff0, {"Camera_Tuning_0040EFF0", 0, false, V(G.tune_0040eff0())}},
        {0x0040f010, {"Camera_TuningThunk_0040F020", 0, false, V(G.tune_0040f020())}},
        {0x0040f020, {"Camera_Tuning_0040F020", 0, false, V(G.tune_0040f020())}},
        {0x0040f040, {"Camera_TuningThunk_0040F050", 0, false, V(G.tune_0040f050())}},
        {0x0040f050, {"Camera_Tuning_0040F050", 0, false, V(G.tune_0040f050())}},
        {0x0040f070, {"Camera_TuningThunk_0040F080", 0, false, V(G.tune_0040f080())}},
        {0x0040f080, {"Camera_Tuning_0040F080", 0, false, V(G.tune_0040f080())}},
        {0x0040f0a0, {"Camera_TuningThunk_0040F0B0", 0, false, V(G.tune_0040f0b0())}},
        {0x0040f0b0, {"Camera_Tuning_0040F0B0", 0, false, V(G.tune_0040f0b0())}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "camera layer retail forced calls");
}
