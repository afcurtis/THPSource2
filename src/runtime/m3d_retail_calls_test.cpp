// Replays retail forced calls (Phase 4.15a) through the native M3D helpers (m3d_matrix.cpp: the small matrix and GTE register helpers at 0x004f4050..0x004f5f10):
//
//   opentony_m3d_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of m3d_specs*.py) holds the retail memory regions the ported code touches and, for every
// case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "m3d_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::m3d;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    M3dKit kit;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), kit(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using M3dAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.kit

const std::map<std::uint32_t, M3dAdapter>& adapters() {
    static const std::map<std::uint32_t, M3dAdapter> table = {
        {0x004f5390, {"M3D_RotationMatrixFromAngles", 0xffffffff, false, A(G.rotation_from_angles(a[0], a[1]))}},
        {0x004f50d0, {"GTE_SetBackgroundColour", 0, false, V(G.set_background_colour(a[0]))}},
        {0x004f50f0, {"GTE_SetTranslationQ12", 0, false, V(G.set_translation(a[0]))}},
        {0x004f5f10, {"Math_MulDivTruncated", 0xffffffff, false, A(G.mul_div_truncated(S(a[0]), S(a[1]), S(a[2])))}},
        {0x004f5430, {"Matrix_SetIdentityQ12", 0, false, V(G.set_identity_matrix(a[0]))}},
        {0x004f53e0, {"Render_TransposeMatrixQ12", 0, false, V(G.transpose_matrix(a[0], a[1]))}},
        {0x004f5730, {"Matrix_CopyQ12", 0, false, V(G.copy_matrix(a[0], a[1]))}},
        {0x004f56c0, {"GTE_LoadRotationAndRotateIR", 0, false, V(G.load_rotation_and_rotate(a[0]))}},
        {0x004f4fe0, {"GTE_LoadRotationAndTranslation", 0, false, V(G.load_rotation_and_translation(a[0]))}},
        {0x004f5460, {"Matrix_MultiplyByRotationColumns", 0, false, V(G.multiply_by_rotation(a[0], a[1]))}},
        {0x004f5780, {"Matrix_MultiplyByLightColumns", 0, false, V(G.multiply_by_light(a[0], a[1]))}},
        {0x004f4050, {"Box_OrderCorners", 0, false, V(G.order_corners(a[0], a[1], a[2]))}},
        {0x004f40d0, {"Box_CornersToUnits", 0, false, V(G.corners_to_units(a[0], a[1], a[2], a[3]))}},
        {0x004f3f30, {"Camera_ProjectBoxCorners", 0xffffffff, false, A(G.project_box_corners(a[0], a[1]))}},
        {0x004f4130, {"Box_ObjectCornersRelativeToBox", 0, false, V(G.object_corners_in_box(a[0], S(a[1]), S(a[2]), S(a[3]), S(a[4]), S(a[5]), S(a[6]), S(a[7]), S(a[8]), S(a[9]), a[10], a[11], a[12]))}},
        {0x004f4240, {"Box_ObjectOverlapsBox", 0xffffffff, false, [](Ctx& c, const Args& a, std::uint32_t) -> std::uint32_t {
             std::int32_t v[15];
             for (std::size_t k = 0; k < 15; ++k) {
                 v[k] = S(a.at(k));
             }
             return c.kit.box_overlap_test(v);
         }}},
        {0x004f4940, {"Collision_CullObjectsAgainstBox", 0, false, V(G.cull_objects_in_box(a[0], a[1], a[2], S(a[3])))}},
        {0x004f4b00, {"Collision_TransformLinkedModelVertices", 0xffffffff, false, A(G.transform_vertices(a[0], a[1], S(a[2]), a[3]))}},
        {0x004f5160, {"M3D_ApplyHookTransform", 0, false, V(G.apply_hook_transform(a[0], a[1], a[2]))}},
        {0x004f5540, {"Collision_ScaleLinkedObjectMatrix", 0, false, V(G.scale_object_matrix(a[0], a[1]))}},
        {0x004f39f0, {"Render_FlagObjectsOutsideView", 0, false, V(G.cull_objects_to_view(a[0], a[1]))}},
        {0x004f43e0, {"Collision_CullLinkedObjects", 0, false, V(G.cull_linked_objects(a[0], a[1], a[2], S(a[3])))}},
        {0x004f4c50, {"Collision_TestLinkedModelFaces", 0, false, V(G.test_linked_model_faces(a[0], a[1], a[2], a[3]))}},
        {0x00464200, {"M3dColij_LineToSphere", 0xffffffff, false, A(G.line_to_sphere(a[0], a[1], a[2], a[3], a[4], S(a[5])))}},
        {0x00463e50, {"Collision_TestLinkedObjectFaces", 0, false, V(G.test_linked_object_faces(a[0], a[1]))}},
        {0x004f5a70, {"Transform_ComposeQ12", 0, false, V(G.compose_transforms(a[0], a[1], a[2]))}},
        {0x004f5ca0, {"Transform_ComposeWithTransposeQ12", 0, false, V(G.compose_with_transpose(a[0], a[1], a[2]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "M3D helper retail forced calls");
}
