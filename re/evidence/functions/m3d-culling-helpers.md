# The M3D transform and culling helpers (0x004f3f30..0x004f5f10)

Status: confirmed by retail forced calls (5,900 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00463e50`, `0x00464200`, `0x004f39f0`, `0x004f3f30`, `0x004f4050..0x004f4240`, `0x004f43e0`, `0x004f4940`, `0x004f4b00`, `0x004f4c50`, `0x004f4fe0..0x004f5f10` (27 functions; slices `m3d-matrix-helpers`, `m3d-culling-boxes`, `m3d-model-transforms`, `m3d-line-collision`, `m3d-view-cull`)
Native: `src/runtime/m3d_kit.hpp`, `m3d_matrix.cpp`, `m3d_boxes.cpp`, `m3d_transform.cpp`, `m3d_collision.cpp`; replay `src/runtime/m3d_retail_calls_test.cpp`; probes `re/gdb/probes/m3d_specs.py` (group `m3d`), `m3d_specs_hit.py` (group `m3h`);
fixtures `re/evidence/fixtures/m3d-calls-*.txt`

## What is here

The code between the collision primitives and the model renderer is built from small functions over 3x3 matrices of Q12 shorts and the GTE register file (memory at `0x006a3e00`; the layout is in
`math_kit.hpp`). None of them reaches a service, so a replay compares every register store.

- **Matrix helpers**: identity (`0x1000` on the diagonal), copy, transpose (word by word in that order, so an aliased source and destination give a partly transposed result), the rotation registers loaded from a
  matrix (`GTE_LoadRotationAndRotateIR` follows with RTIR, `GTE_LoadRotationAndTranslation` loads three shorts after the nine into TR), the background colour and the translation (`>> 12`), the rotation matrix or the
  light matrix times each column of a matrix (`RTIR` / `LCIR`; the rotation version writes each column before reading the next, the light version reads all nine words first), and `Math_MulDivTruncated`
  (`a * b / c` through `fild / fimul / fidiv` at the process's 53-bit precision, truncated by `_ftol`).
- **Boxes**: `Box_OrderCorners(low, high, flags)` puts the lower value of each axis in `low` and toggles flag bit 0, 1 or 2 for every axis it swapped, then puts the extents on the diagonal of the rotation
  registers (`D1`, `D2`, `D3`); `Box_CornersToUnits` first brings a Q12 box to whole units and clears the flags. `Box_ObjectCornersRelativeToBox` moves a model's box (packed as a high and a low short per axis at
  `+0xc..+0x14`) by the object's position, makes it 2 units larger each way and mirrors the flagged axes about the culling box. `Box_ObjectOverlapsBox` (15 arguments) answers from the signs of the GTE's outer
  products (`op0`) of the extents with the offsets, after checking the order of the corners. `Collision_CullObjectsAgainstBox` walks a null-terminated array of objects: an object that is solid (flag word at `+4`
  not both `0x41` bits) and not marked (`0x20`) whose generation (short at `+6`) differs from the current one is tested; one that does not overlap, or is not solid, or is marked, is tagged with the generation.
- **Projection**: `Camera_ProjectBoxCorners` puts the two corners through `RTPS` relative to the camera and keeps the screen words and depths in the record (+0x18 / +0x1c / +0x20 / +0x24 / +0x28 / +0x2c).
- **Transforms**: `Collision_TransformLinkedModelVertices` sends the vertices of a linked model (count at `+2`, four shorts each from `+0x1c`) through `LCIRTR` and builds the clip word
  (bit 2 / 1 / 0 for a negative x / y / z, bit 3 when z is over the limit, bit 10 / 9 for a positive x / y; the answer is the AND of all of them with `0x60f`); it reads one vertex past the end. `M3D_ApplyHookTransform` is
  `RT * V0 + TR` then the light matrix times IR plus the background; `Collision_ScaleLinkedObjectMatrix` scales each column by a short of the model record (`GPF12`); the two `Transform_Compose*` functions multiply
  twelve-short transforms through `RTV0..2` and `RTIRTR`.

## The collision of a line with the linked objects (`m3d_collision.cpp`)

- `Collision_CullLinkedObjects(objects, _, box, generation)` is `Collision_CullObjectsAgainstBox` for a chain of objects (linked at `+0x20`). An object turned from the upright (the angles at `+0x14`) gets the culling box
  turned into its frame: the corners go through the transposed rotation matrix (`Render_TransposeMatrixQ12` on an aliased matrix, the quirk of `m3d-matrix-helpers`) and `RTIR`, the corner order flags are
  worked out again (`Box_OrderCorners`) and kept for the objects after it; a scaled object (bit 1 of `+5`) has the corners of its model scaled by the shorts at `+0x28..+0x2c` through the x87
  (`fild / fimul / fmul 1/4096 / _ftol`: truncated products).
- `Collision_TestLinkedModelFaces(model, vertices, info, object)`: the faces of a model (16 bytes each after the vertices and the normals; the size in the high half of the first word, the flags in the last) tested
  against the line `info`. A face counts when its flags are not in the mask `0x00567a60`, contain everything outside the mask `0x00567a68` and are not just `0x10000`; the outer product of two of its edges says which
  side the line starts on (a face with bit 4 of its size word has only one side), the light matrix - the line's direction in the first row, its length plus 2 in the last place - times the face's normal gives
  the distance along the line; the nearest face before the start becomes the hit (its object, face, normal and index are kept; a trigger face, flag `0x20000`, only marks its zone). The point hit is
  `info[0..2]` moved by the nearest distance over the length (`GPL0`).
- `Collision_TestLinkedObjectFaces(object, info)` puts the object's model into the line's frame - an upright unscaled object by an offset, any other through its rotation and scale and the light matrix, the
  colour matrix of the GTE receiving the result - runs `Collision_TransformLinkedModelVertices`, and tests the faces when the vertices are not all clipped away. A trigger face hit runs the zone's commands
  (`Trig_TriggerCommandPoint`, with the environment index from `Spool_GetEnvIndex`).
- `M3dColij_LineToSphere(start, end, out, list, skip, scale)`: the unit direction of the line (the vector shifted right by 12, its integer root, the vector shifted left by 12 and divided by the root) goes on the
  GTE diagonal and in the light matrix's first row; for each object whose box (centre +- radius, the short at `+0xae` times `scale`) meets the line's box, the outer product with the vector to the centre gives
  its distance from the line and the light matrix its position along it; the nearest sphere the line enters (or whose centre is within the radius of the line's end or start) is the hit, and the answer is
  the object with the point hit at `out`.
- `Render_FlagObjectsOutsideView(objects, mask)` (`0x004f39f0`) marks the objects of a chain that the view does not show (bit 15 of the flag word): an object whose flags meet the mask is out at once; the others are
  tested in floating point - the position relative to the camera, in whole units as floats, against a high and a low limit on each axis; the bounding sphere (the radius of the model in whole units, scaled by
  the smallest scale short for scaled objects, and by a float for bit 1) against six planes (three Q12 shorts each, an offset on the first two); and, for an object that is neither scaled nor turned and whose
  model has more than ten faces, the eight corners of the box of the model against the planes (out when all eight are beyond one). The x87 runs at the precision of the caller: the render window uses 24 bits,
  a forced call 53; the port rounds at the precision of its math kit and only the 53-bit case is replayed.

## Method

Forced calls with the matrices and boxes in a scratch slab and the GTE register file as a window poked with random words each case (a register an operation reads without writing shows up at once). The model
tables of `Collision_CullObjectsAgainstBox` (`0x0056d43c`, 0x44 bytes a region) point into the slab.

## Lessons

(1) The decompile names GTE registers by address (`DAT_006a3e10`...): the rotation matrix rows are `0x6a3e10 + 6 * row`, the vectors V0..V2 `0x6a3e58 + 8 * k`, the translation `0x6a3ea0`, the background `0x6a3e48`, the
light matrix `0x6a3e80`. (2) A function that reads its input words into registers before the first GTE call and stores after the last (`Matrix_MultiplyByLightColumns`) differs from one that stores each result
at once (`Matrix_MultiplyByRotationColumns`) only when source and destination overlap; the probe aliases them on purpose.
