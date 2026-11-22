#pragma once

#include "collision_kit.hpp"
#include "math_kit.hpp"

#include <cstdint>

namespace opentony::runtime::m3d {

// Phase 4.15a: the M3D transform and collision-culling helpers at 0x004f39f0..0x004f5f4f. They are the small matrix and GTE register helpers the culling of the linked collision objects and the model
// transforms are built from: copies and transposes of 3x3 matrices of Q12 shorts, loads of the rotation and translation registers, multiplications of a matrix by the rotation or light matrix a column
// at a time, and the ordering of a box's corners. Like the rest of Phase 4 they work on the retail memory at the retail addresses (the GTE register file is memory at 0x006a3e00).

namespace layout {

constexpr std::uint32_t kMatrixBytes = 18;                 // nine shorts, row major

}  // namespace layout

class M3dKit final : public collision::CollisionKit {
public:
    M3dKit(RetailMemory& memory, RetailServices& services) : CollisionKit(memory, services), math_(memory, services) {}

    std::uint32_t rotation_from_angles(std::uint32_t angles, std::uint32_t matrix);        // 0x004f5390 M3D_RotationMatrixFromAngles (cdecl: the same call as Math_RotationMatrixFromAngles)
    void set_background_colour(std::uint32_t vector);                                      // 0x004f50d0 GTE_SetBackgroundColour: three dwords copied to BK
    void set_translation(std::uint32_t vector);                                            // 0x004f50f0 GTE_SetTranslationQ12: a Q12 vector in whole units to TR
    std::uint32_t mul_div_truncated(std::int32_t a, std::int32_t b, std::int32_t c);       // 0x004f5f10 Math_MulDivTruncated: a * b / c through the x87, truncated
    void set_identity_matrix(std::uint32_t matrix);                                        // 0x004f5430 Matrix_SetIdentityQ12
    void transpose_matrix(std::uint32_t source, std::uint32_t destination);                // 0x004f53e0 Render_TransposeMatrixQ12
    void copy_matrix(std::uint32_t source, std::uint32_t destination);                     // 0x004f5730 Matrix_CopyQ12
    void load_rotation_and_rotate(std::uint32_t matrix);                                   // 0x004f56c0 GTE_LoadRotationAndRotateIR: the matrix into RT, then RTIR
    void load_rotation_and_translation(std::uint32_t matrix);                              // 0x004f4fe0 GTE_LoadRotationAndTranslation: nine shorts into RT, three shorts after them into TR
    void multiply_by_rotation(std::uint32_t source, std::uint32_t destination);            // 0x004f5460 Matrix_MultiplyByRotationColumns: RT times each column of a matrix
    void multiply_by_light(std::uint32_t source, std::uint32_t destination);               // 0x004f5780 Matrix_MultiplyByLightColumns: the light matrix times each column of a matrix
    void order_corners(std::uint32_t low, std::uint32_t high, std::uint32_t flags);        // 0x004f4050 Box_OrderCorners: per axis the lower corner first; the extents into the RT diagonal
    void corners_to_units(std::uint32_t box, std::uint32_t low, std::uint32_t high, std::uint32_t flags);   // 0x004f40d0 Box_CornersToUnits

    // ---- the culling of the linked collision objects (0x004f3f30, 0x004f4130, 0x004f4240, 0x004f4940) ----
    std::uint32_t project_box_corners(std::uint32_t box, std::uint32_t camera);            // 0x004f3f30 Camera_ProjectBoxCorners: the two corners through RTPS, the nearer depth >> 2 (at most 0xfff)
    void object_corners_in_box(std::uint32_t model, std::int32_t low_x, std::int32_t low_y, std::int32_t low_z, std::int32_t high_x, std::int32_t high_y, std::int32_t high_z,
                               std::int32_t object_x, std::int32_t object_y, std::int32_t object_z, std::uint32_t flags, std::uint32_t out_low, std::uint32_t out_high);   // 0x004f4130 (cdecl, 13 arguments)
    std::uint32_t box_overlap_test(const std::int32_t (&a)[15]);                           // 0x004f4240 Box_ObjectOverlapsBox (cdecl, 15 arguments)
    void cull_objects_in_box(std::uint32_t objects, std::uint32_t unused, std::uint32_t box, std::int32_t generation);   // 0x004f4940 Collision_CullObjectsAgainstBox

    // ---- the model transforms (0x004f4b00, 0x004f5160, 0x004f5540, 0x004f5a70, 0x004f5ca0) ----
    std::uint32_t transform_vertices(std::uint32_t model, std::uint32_t out, std::int32_t limit, std::uint32_t offset);   // 0x004f4b00 Collision_TransformLinkedModelVertices
    void apply_hook_transform(std::uint32_t vector, std::uint32_t transform, std::uint32_t lights);   // 0x004f5160 M3D_ApplyHookTransform (cdecl, three arguments)
    void scale_object_matrix(std::uint32_t model, std::uint32_t matrix);                   // 0x004f5540 Collision_ScaleLinkedObjectMatrix: each column scaled by a factor of the model record
    void compose_transforms(std::uint32_t out, std::uint32_t outer, std::uint32_t inner);  // 0x004f5a70 Transform_ComposeQ12 (cdecl): a 3x3 and a translation each
    void compose_with_transpose(std::uint32_t out, std::uint32_t outer, std::uint32_t inner);   // 0x004f5ca0 Transform_ComposeWithTransposeQ12

    // ---- the collision of a line with the linked objects (0x004f43e0, 0x004f4c50, 0x00463e50) ----
    void cull_linked_objects(std::uint32_t objects, std::uint32_t unused, std::uint32_t box, std::int32_t generation);   // 0x004f43e0 Collision_CullLinkedObjects (cdecl)
    void test_linked_model_faces(std::uint32_t model, std::uint32_t vertices, std::uint32_t info, std::uint32_t object);   // 0x004f4c50 Collision_TestLinkedModelFaces (cdecl)
    void test_linked_object_faces(std::uint32_t object, std::uint32_t info);               // 0x00463e50 Collision_TestLinkedObjectFaces (cdecl)
    void cull_objects_to_view(std::uint32_t objects, std::uint32_t mask);                  // 0x004f39f0 (cdecl): the objects outside the view volume get bit 15 of their flags
    std::uint32_t line_to_sphere(std::uint32_t start, std::uint32_t end, std::uint32_t out, std::uint32_t list, std::uint32_t skip, std::int32_t scale);   // 0x00464200 M3dColij_LineToSphere (cdecl)

    mathkit::MathKit& math() { return math_; }

private:
    using Word = std::uint32_t;
    mathkit::MathKit math_;
};

}  // namespace opentony::runtime::m3d
