# The D3D model layer: start-up, fog tables, sprite quads and the lit packets (d3dmodel.cpp, d3d.cpp)

Status: confirmed by retail forced calls (60 cases of the start-up and the geometry packets, 40 of the fog builder, 80 of the sprite quad builder, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004d11d0`, `0x004d2da0` (2 functions) (slice `render-d3d`), `0x004d3f50` (1 functions) (slice `d3d-fog-tables`), `0x004d7990` (1 functions) (slice `d3d-sprite-quads`)
Native: `src/runtime/render_kit.hpp`, `render_d3d.cpp`; replay `src/runtime/render_retail_calls_test.cpp`; probe `re/gdb/probes/render_specs6.py` (groups `rd3d`, `rd3dr`); fixtures `re/evidence/fixtures/render-calls-rd3d*.txt`, `render-calls-p24-rd3d*.txt`

## What is here

**`D3DMODEL_Startup`** clears the 60000-byte pushback table and stores the 126 face depth offsets the model renderer pushes polygons back by (a table generated from the decompiled stores, in
the order the function makes them).

**`D3D_SetupFog(colour)`** builds the four 256 x 256 fog tables of a fog colour (alpha in the top byte, then red, green and blue; `0x0059d340`, `0x005dd740`, `0x0061d82c`, `0x0065fc60`) with
`__ftol` of x87 products, and does nothing when the colour is the same as last time. At alpha 2 the three colour channels follow the row alone. Only the entries the replay's memory holds are
written: the captured window holds rows 0, 1, 2, 100, 254 and 255 of each table (1 KB each), so a case compares those rows; the other rows are not part of the evidence.

**`D3D_BuildSpriteQuad`** (a thiscall on the sprite list; the stack carries the ordering table and an x and a y offset as floats) writes a quad of four vertices for each rectangle of the list
(0x10 bytes each: left, top, right, bottom as integers) with the sprite's texture word, in the screen's pixels, and links it into the table in front, or behind for a sprite whose word is not 0.9.
One 0xc0-byte packet per rectangle goes to the render arena. The vertices' colour words at +0x28, +0x44, +0x60 and +0x7c are `0xffffffff`.

**`M3D_SubmitGeometryPackets(packets, count, light matrix, reset)`** lights the vertices of a model: each packet (8 bytes: a normal of three shorts and a flag word) is lit through the light
matrix (the frame's at `0x006a3e80`, or the caller's when it gives one) and the colour matrix plus the background colour of the GTE, clamped to 1.0 and stored as a colour (red / green / blue
bytes of 255 times the result, truncated) into the array at `0x0057e888`. Flag 1 also appends the colour to the list at the cursor `0x0058f324`; flag 2 takes the colour from the table at `0x0056e874`
by index instead. A call without a light matrix, or with the reset byte set, rewinds the cursor to the table.

## Method

All of these run in the *render window* of the game, where the x87 control word is 24-bit; a forced call in the debugger runs at the process default of 53 bits. The probe therefore captures the
fog builder, the sprite quad and the packets twice: at 53 bits (fixtures `render-calls-rd3d*.txt`, the native computing at 53 bits) and through a trampoline that sets the 24-bit control word around the call (`rd3dr`, fixtures named
`p24-`, the native computing at 24 bits; the replay takes the precision from the `-p24-` in the fixture name). The trampoline for `D3D_BuildSpriteQuad` keeps its return address in eax because a
thiscall needs ecx for `this`. The sprite list and its rectangles, the light matrix, the colour list and the ordering table sit in the slab, the table's far word (+0x7ff8) is a separate small window.

## Lessons

(1) A filtered disassembly dropped `or ecx, 0xffffffff`, which made four stores of the sprite quad look like copies of a neighbouring word; the unfiltered listing showed the truth. (2) The shared
24-bit trampoline of the math probes clobbers ecx; a thiscall needs a trampoline of its own. (3) A forced call at 53 bits rounds the lighting differently from the render window in some cases, which is why every float function has a capture at each precision and the
native follows the precision of the fixture.
