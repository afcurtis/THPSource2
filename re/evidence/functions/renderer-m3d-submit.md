# The scene renderer's small functions (m3d.cpp, m3dinit.cpp)

Status: confirmed by retail forced calls (760 cases of the nineteen small functions, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00461a50`, `0x00461a90`, `0x00461ad0`, `0x00461b10`, `0x00461b50`, `0x00461b70`, `0x00461bb0`, `0x00461bf0`, `0x00461c90`, `0x00461cc0`, `0x00461cf0`, `0x00462170`, `0x004645f0`, `0x00464690`, `0x00464710`, `0x00464a80`, `0x004657c0`, `0x00466030`, `0x00466080` (19 functions) (slice `render-m3d-submit`)
Native: `src/runtime/render_kit.hpp`, `render_m3d.cpp`; replay `src/runtime/render_retail_calls_test.cpp`; probe `re/gdb/probes/render_specs5.py` (group `rm3d`); fixtures `re/evidence/fixtures/render-calls-rm3d*.txt`

## What is here

**The submitters.** A model is handed to the model-state submission (`0x004d14d0`, an earlier slice) with the flags the scene keeps: `0x005620a4` (the flags every submitted model gets),
`0x0056437c` / `0x00564380` (a pending flag for the second submission of a pair, which adds `0x200`, and the value it moves on to) and `0x00564378` (the mirror flag, `8`).
`M3D_SubmitStateGeometry`, `M3D_SubmitStateGeometryVariant` and `RenderModelNonRotated` are the same call with the pair flag; `RenderBackgroundModel` and `RenderBackgroundModelNonRotated` add the
background flag `0x10`; `M3D_SetGeometryState(model, flags)` ORs in 1, `0x200` while a pair is pending and 8 for a mirrored model; `RenderModelInSuperFast` is the fast path with 1.

**The transform and the wibble.** `M3d_BuildTransform(model)` builds the matrix at +0x118 from the model's angles (or from the matrix it was given with the first column's signs flipped) and
scales it when the model asks. `uWibble` and `vWibble` read the amplitude (high nibble) and phase step (low nibble) of the wibble record's byte +2 / +3 and look the offset up in a 16 x 64 sine table
(`0x00563b3c`, built by `M3dInit_InitAtStart`); `M3D_ApplyTextureWib(region)` scrolls and wobbles the texture coordinates of the faces named by the region's wibble table (per list: the object and its
model the faces belong to, the u and v offsets added with a carry rule that must converge), `M3D_PrepareTextureWib` marks each list as scrolling only in u or only in v when none of its faces'
coordinates reach into the upper nibble, and `M3D_UpdateColourPulse(region)` steps each colour pulse with the clock and writes the colour between the current and the next one through the GTE.

**The start-up.** `M3dInit_InitAtStart` sets the projection ratio of the window, the fog range (near `0x1770`, range `0x800`), the sine table and the sky word; `M3D_WindowRatio` reads the window's
size from the platform layer and keeps the 4:3 correction ratios; `M3D_SetFogRange` demands a power of two for the range and keeps the shift that brings it to `0x1000`; `M3D_MatrixAngles` takes the
three angles of a rotation matrix with the fixed-point arc tangent; `Blockmap_ClearRuntimeGrid(index)` clears a grid with a non-positive index (a count and 0x190 words) and `Blockmap_ClearGrid0` is the
call with 0.

## Method

The probe pokes the scene flags and the globals above and calls the small submitters with a model address; the model-state submission is a service recorded with its arguments. For the wibble
functions it builds a region with wibble lists, objects and models in the slab. The sine-table builder, the colour pulses and the matrix angles are compared through the GTE and the table memory.

## Lessons

(1) `M3D_ApplyTextureWib` hung in an unbounded carry loop on random wibble data in the first probe: the generator now guarantees that the loop converges. (2) The fog range write at `0x0056209c` needs the
window to start at `0x00562090`; windows that were too wide captured the assertion globals and made every case differ. (3) A blockmap grid is `index * 0x660` bytes: the window covers the grid and the
rows it clears, nothing else.
