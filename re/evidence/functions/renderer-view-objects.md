# The start of a view and the objects of a frame (m3d.cpp)

Status: confirmed by retail forced calls (730 cases of the six view and object functions, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0045e8e0`, `0x0045ef80`, `0x0045f530`, `0x004604f0`, `0x00460ca0`, `0x004610f0` (6 functions) (slice `render-view-objects`)
Native: `src/runtime/render_kit.hpp`, `render_view.cpp`, `render_scene.cpp`; replay `src/runtime/render_retail_calls_test.cpp`; probes `re/gdb/probes/render_specs5.py`, `render_specs7.py`, `render_specs8.py` (groups `rview`, `rbgd`, `robj`, `rssi`, `rmdl`); fixtures `re/evidence/fixtures/render-calls-rview*.txt`, `-rbgd*`, `-robj*`, `-rssi*`, `-rmdl*`

## What is here

**The view** (`M3D_BeginRenderView(view, edges, buckets)`, `M3d_RenderSetupCamera(view)`). The viewport edges record (left, bottom, right, top shorts, the depth at +0xa, the half-width at +0xc) is turned
into the projection constants (half width +0xe, centre +0x10 / +0x12) with the pixel aspect words (`0x00563a6c`, `0x00563a70`); the four frustum planes (eight shorts each, `0x00563a90..0x00563adc`:
the normals and their offsets, from square roots of the edges); the frame's light and colour matrices (the view's matrix through the GTE applied to the plane normals, `0x005620e8` and `0x005620c0`);
and the aspect-scaled copy of the view's transposed matrix at +0x74. `M3D_BeginRenderView` also advances the animation clock (`0x00563af0`, the tick count at `0x00564370`), opens the view's ordering
table with a rectangle (a primitive linked at the head of the buckets, depth -1.0 when they are the current buffer's), updates the colour pulses (three service calls) and the texture wibble (two) of the active regions, and keeps the negated shorts at +0x3a / +0x3c / +0x3e of the view
(`0x005620b0..b4`) and the player's height relative to the camera (`0x005620b6`); `M3d_RenderSetupCamera` does only the projection part for the camera that renders single objects.

**The sky** (`M3d_RenderBackground(list)`). Each background object of the list that is not culled has the view's matrix turned by its own rotation (and scale) into the matrix at `0x00563a18`
(a column at a time through the GTE; an object with no rotation and no scale uses the view's matrix as it is), takes its region's colour table and is submitted through the background model
submitters with the depth word `0x00563a54` at 0x7fff.

**The objects** (`M3D_RenderObjects(list)`). The list is culled against the view; an environment list also scrolls the textures of its two active regions. For every object: the flags word
(`0x00564358`) is built from bits 7 and 10 of the object's flags and, when bit 3 of its second flag byte is set, from the word at +0x30; a skater (bit 1) goes to `M3D_RenderModel` and, when bit 3 of
its flags is set, is rendered a second time with a fixed colour word (it depends on the level) written to +0x24 and the byte at +0xdc cleared; any other object looks up its model through the region's table and follows the model's rougher-model link (+0x1a) while the object's distance exceeds the limit its model carries (+0x18, in
units of the viewport's scale over 191, and a negative limit scales by a frame factor; an object beyond the roughest model is not drawn). It is then turned into the view by its own rotation and
scale (as the sky is), lit through the lights of its matrix (or the level's table from level 10 on, except 13), and submitted unrotated, "variant" (a close, plain model) or normally, with the
object's three depth-offset bytes (+0x1c, +0x1d, +0x1e, times 32) pushed into the frame's depth words around the submission.

**The item and the skater** (`RenderSuperItemShadow`, `M3D_RenderModel`). Both draw an item of several models, one per bone of its region, with the object's matrix at +0x118 (the first, fourth and
seventh shorts negated for a mirrored object) and the pose of its bones: the item's position relative to the view and its matrix times the view's go to `0x00563b10` (with the bone's pose entry at
`0x00564340`) and every model is submitted through `M3D_SetGeometryState` (the first with 2, one named by the object's mask at +0x148 or marked in its first byte with 4). `M3D_RenderModel` is the skater's version: it also lights each bone (the lights of
the object's matrix, or the level's table), submits its geometry packets with the object's matrix times the bone's, picks `RenderModelInSuperFast` when the frame asks, marks the two bones a
skater names (+0x2cd4, +0x2cd8) with 0x100, and, for a bone that is not named and an object with a shade or a byte at +0xdc, turns the bone's last vector by the object's matrix.

## Method

The slab holds the view record, the viewport edges, the buckets and primitives, up to three objects of 0x160 bytes (the skater's object is much larger, the fields at +0x2cd4 are poked too), a model
table per region with up to four models linked by +0x1a, and the pose entries. Pose, matrix, lights and positions are random; the planes are zeroed in 60% of the cases so that the culling lets objects
through. The GTE register file is plain memory of the replay, so matrices and rotations are compared as bytes. In the tests of `M3D_RenderObjects` the callees `M3D_RenderModel`, `M3D_SubmitStateGeometry`, `M3D_SubmitStateGeometryVariant`,
`RenderModelNonRotated`, `M3D_ApplyTextureWib` and `M3D_SubmitGeometryPackets` are stubs recorded with their arguments (the object's position record is the stack argument of two of them);
in the tests of `M3D_RenderModel` the stubs are `M3D_SetGeometryState`, `RenderModelInSuperFast`, the animation pose and `M3D_SubmitGeometryPackets`, whose light matrix is recorded as a blob, so the
matrix a skater's bones are lit with is compared too.

## Lessons

(1) Three words of the frame are stack leftovers in retail and the replay treats them as unspecified: the fourth word of the object's position (`0x00563b04`), the two bytes between the matrix and
the translation of the item's copy at `0x00563b10`, and fourteen bytes of the matrix copy at `0x00563a18` when the object is turned. (2) The decompiler hid an `and esi, 0xfffffffd` in the flags of
`M3D_RenderObjects`; the disassembly is the authority wherever the two disagree. (3) A probe that zeroes the planes with a wide `put` also zeroes the camera pointer at `0x005620e0`, which lies between
them; the pointer is poked after. (4) The level-of-detail limits use a division by 191 on a sign-extended product, written by the compiler as a multiplication by `0xab8f69e3`; the native uses the
division and truncates the quotient to 16 bits only on the negative-limit path.
