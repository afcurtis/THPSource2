# The part stitcher: construction and weld (create.cpp, the stitcher)

Status: confirmed by retail forced calls (120 cases of the stitcher and the weld, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00424900`, `0x00424a60`, `0x00424a80`, `0x00425100`, `0x004252c0`, `0x004254c0` (6 functions) (slice `create-stitch-weld`)
Native: `src/runtime/createobj_kit.hpp`, `createobj_weld.cpp`; replay `src/runtime/createobj_retail_calls_test.cpp`; probe `re/gdb/probes/cobj_specs7.py` (groups `cweld`, `cskt`) on the scene of `cobj_world.py`; fixtures
`re/evidence/fixtures/createobj-calls-cweld*.txt`

## What is here

The stitcher joins the parts of a custom skater: vertices on the seam between two parts come in pairs, a *stitch* vertex (flag bit 0) on one part and a *ghost* (bit 1, whose x names the stitch vertex it mirrors in
eighths) on the other. Its pieces (`PartStitcher_LogStitches`, `CopyModels`, `ReorderStitches`, `AddStitch`, the part map) are earlier slices (`create-stitch`); this slice is the rest:

**`PartStitcher_Construct(source region, build region, map)`** (a thiscall; vtable `0x00518778`) asserts its three arguments and then logs the source region's stitches into a 0x1e0-byte local (one running
count per part), copies the visible source models into the build region by the part map, and reorders the ghosts by the change of the numbering; `PartStitcher_Finish` is empty. `PartStitcher_Destroy` restores
the base vtable and `PartStitcher_ScalarDelete` also frees the object.

**`PartStitcher_Weld(name, source, build, map)`** makes an animated object (`TRG_AllocObjectStorage(0x168)` and `Animated_Construct`, both services; the object is 0 when the storage is), checks that the
region `name` is loaded, lets the object select that region and welds two pairs of build parts, 0 with 1 and 2 with 3, then deletes the object through its vtable. **`PartStitcher_WeldPair(object, source, build, a, b, map)`**
takes the pose of the object (`Animation_GetDecompressedPose`, a service: 24 bytes a bone, nine shorts of rotation and three of translation) and finds, through the map, the source parts of the build
parts `a` and `b`. For every vertex of the source part of `a` and every vertex of the source part of `b` it loads the rotation and translation of the first part's bone, sets the vertex, runs the GTE's `RT`, then
does the same for the second, and when all three coordinates of the two results are less than 10 apart it makes the pair a stitch: `PartStitcher_AddStitch(build, vertex a, vertex b, normal a, normal b)` on the
*build* models, whose vertices run parallel to the source's. The GTE is plain memory (`0x006a3e00`), so its registers are a captured window and the matrices and results are compared as bytes.

## Method

The scene's models draw their vertex positions from a pool of ten points, and the pose's rotations are the identity with small off-diagonal terms and translations of at most four units, so that vertices of
two parts can meet and the weld is exercised. The part map of a stitch is a permutation of the 19 slots (every part visible); the
build region's models are copies of the source's. The animated object is a fake: a block in the slab whose first word points to a vtable whose first slot is a retail function nothing else in the flow calls
(`0x00463580`), which the harness stubs, so the virtual delete is a reported call.

## Lessons

(1) The weld's `ecx` (the stitcher object, possibly 0) is passed to every callee unused; only the pose's object matters. (2) A hidden part leaves its build slot empty and `CopyModels` then asserts a part count
below 0x13; the reorder that follows dereferences the null model. The real flow never gets there because the body groups of a valid skater leave exactly 19 parts visible. (3) The weld's inner branch needs two vertices
less than 10 apart on all three axes after the transform, so the generator draws positions from a small pool of points; the coverage run of the captures shows whether the branch is reached.
