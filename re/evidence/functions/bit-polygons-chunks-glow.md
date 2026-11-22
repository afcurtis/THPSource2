# The general polygon, chunk, glow, flat, linked and pixel bits (bit.cpp)

Status: confirmed by retail forced calls (6,600 cases of the polygon, chunk and glow classes, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00404af0`, `0x00404bd0`, `0x00404bf0`, `0x00404cb0`, `0x004051c0`, `0x00405390`, `0x004053b0`, `0x004053d0`, `0x004054b0`, `0x00405520`, `0x004056c0`, `0x004057f0`, `0x00405810`, `0x00405990`, `0x00405a70`, `0x00405aa0`, `0x00405b20`, `0x00405b50`, `0x00405b90`, `0x00405c40`, `0x00405d20`, `0x00405d60`, `0x00405e30`, `0x00405e50`, `0x00405ec0`, `0x00405ef0`, `0x00405f10`, `0x00405f30`, `0x00405f70`, `0x00405f90`, `0x00405fb0`, `0x00405fd0`, `0x00405ff0` (33 functions) (slice `bit-polygons-chunks-glow`)
Native: `src/runtime/bit_kit.hpp`, `bit_core.cpp`, `bit_glow.cpp`; replay `src/runtime/bit_retail_calls_test.cpp`; probe `re/gdb/probes/bit_specs.py` (groups `bit2`, `bit3`); fixtures `re/evidence/fixtures/bit-calls-bit[23]*.txt`

## What is here

- **General polygon** (`CGenPoly`, list `0x0055f724`): a bit with three or four short corners; the two constructors differ only in the number of corners they copy.
- **Chunk bit** (`CChunkBit`, list `0x0055f6c0`): a flying triangle. The constructor takes three short corners, derives the normal and gives the bit a velocity along it (the unit vector is the M3D one,
  so the 4.14 layer is used); `SetRGB` sets the colour and three random shades of it; `SetTexture` sets the page, the clut and three texture coordinates. `CalculateWorldCoords` turns the four corners
  (three shorts each, `+0x3c`, `+0x44`, `+0x4c`, `+0x54`) into world coordinates through the GTE with the rotation of the bit's angles (`+0x8c`) and its position (`rtv0tr` per corner).
- **Glow** (`CGlow`, list `0x0055f6e8`): `+0x3c` the sections (radius, colour; 8 bytes each), `+0x40` the fringes, `+0x44` the number of sections, `+0x48` the number of fringes, `+0x4c` the centre colour,
  `+0x50` the step angle, `+0x58` the mask of lit spokes. The general constructor takes `2 * points` sections and `fringes` fringes of that many bands in one block; the simple constructor makes eight sections
  and one fringe, all alike. The setters write the radius of every section, the width of every band of one fringe (asserting the fringe), the colours of the centre, of every section, of every band of one
  fringe, of one spoke (a section and the band of every fringe in line with it), and `Reduce` lowers every colour. The wobbly glow is a simple glow with a random phase and period per section.
- **Flat, linked and pixel bits** (`CFlatBit` list `0x0055f6f0`, `CLinked2EndedBit` list `0x0055f720`, `CPixel` list `0x0055f6e4`): constructors, destructors and deleting destructors.

## Method

The slab holds fake bits as before; a glow case lays the section and fringe arrays out after the bit with random radii. The allocator, the block-size query and the random numbers are stubs. The GTE is the
MathKit's in-memory register file (`0x006a3e00`), captured with the call: the native must leave the screen and MAC registers of `rtv0tr` as retail does.

## Lessons

(1) A glow with no points divides by zero in retail (SIGFPE), so the generator never makes one. (2) The glow setters assert their fringe / spoke argument and carry on (the assertion routine only logs), so a
case with an illegal index still has to agree byte for byte; the generator uses a legal index most of the time. (3) `CalculateWorldCoords` goes through the GTE registers in memory, so its cases compare the
register window (vector, screen and MAC words) as well as the bit.
