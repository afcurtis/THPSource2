# Bit_Display: the bit lists turned into primitives (bit.cpp)

Status: confirmed by retail forced calls (1,077 cases of Bit_Display under the render window's 24-bit x87 precision, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00407310` (1 functions) (slice `bit-display`)
Native: `src/runtime/bit_kit.hpp`, `bit_display.cpp`; replay `src/runtime/bit_retail_calls_test.cpp` (the Bit_Display adapter sets both MathKit instances to 24-bit); probe `re/gdb/probes/bit_specs6.py` (group `bdp`); fixtures `re/evidence/fixtures/bit-calls-p24-bdp*.txt`

## What is here

`Bit_Display` (`0x00407310`) is the only drawing function of the bits. It walks the lists in a fixed order and for each bit projects the position through the GTE (`rtps` / `rtpt` on the camera-relative
position, shifted down from Q12), writes one or more primitives into the renderer's polygon arena and links each into the depth bucket of its z. The arena is a bump allocator (cursor `0x00560fd0`, end
`0x00568628`); a primitive that does not fit ends the whole function with 0, one that is dropped after being started gives its space back by restoring the cursor. The renderer record (`0x00560fd4`) holds the
bucket table in use (`+0x8c`) and the first one (`+0x84`); the bucket's winding value is the viewport's near plane over the bucket's depth, a float compared with the global `0x00518484`, and when the two tables
are the same the value is -1. The depth bucket is `((z - bias) >> 2) & 0xfff`. The viewport record (`0x00563a38`) supplies near (`+8`), far (`+0xa`), scale (`+0xe`) and centre (`+0x10`, `+0x12`).

The order and what each list becomes: the special displays (virtual slot 2 of each bit), the glows (a fan of quads out to the fringes, the points of two neighbouring spokes kept in two swapping buffers, the
radius scaled `* 400 / z` and the screen position clamped to -100..size+100), the gouraud polylines and polylines (a line primitive per segment through the box projector `0x004f3f30`, a service), the flat bits
(a sprite quad from the current animation frame, rotated about the screen point by its angle through `sin_q12` / `cos_q12` and scaled by `128 / z`), the glass shards (three corners through `rtpt`), the line
bits, the quads (four corners; dropped when a corner is out of the depth range), the chunks (a mode primitive each side of up to four triangles that face the camera, by `nclip`), the pixels (sparks: a one-point
primitive when the low nibble of the mode is 1, a larger one otherwise) and the linked bits (a quad between the bit's two projected points; a strip starts at a bit flagged `0x10` or after a dropped bit and each
following bit continues from the far edge of the one before; flag bits 1 and 2 choose which edges lead).

## Method

The probe lays a renderer record, a viewport record, a 0x2000-byte arena and a 4096-entry bucket table out in the slab and points the engine's globals at them; one case fills one list with fake bits (the
others are empty), a view (matrix and camera position of view 0 or 1) and the GTE constants. The function runs under the render window's 24-bit precision: the captures go through the `trampoline` of
`math_specs` with the control word `CW_PC24` (the fixtures are named `-p24-`) and the Bit_Display adapter of the test switches both MathKit instances to 24-bit. Everything written is compared: the primitives, the
bucket links, the cursor and the GTE register window.

## Lessons

(1) The depth test and the screen clamp reject most random positions: the generator keeps the points near the camera's axis and the view matrix the identity most of the time, or whole lists draw nothing.
(2) The chunk's four corners have to be on the screen together for a face to be drawn, so the generator clusters them. (3) The linked bit's edge selection is easy to invert: bit 2 of the flag byte chooses the
`f_x1, f_x2, n_x1, n_x2` order, bit 1 swaps the pairs. (4) The glow takes its header primitive before its range tests and does not give it back when it skips the glow, so the arena cursor moves although nothing is drawn; the other lists restore the cursor.
