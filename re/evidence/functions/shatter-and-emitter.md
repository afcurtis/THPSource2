# The shatter and its effect object (shatter.cpp, emitter)

Status: confirmed by retail forced calls (2,987 cases of the shatter and the emitter, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004add10`, `0x004ade60`, `0x004ade80`, `0x004adee0`, `0x004ae020`, `0x004ae090`, `0x004ae730`, `0x004aeef0`, `0x004aef70`, `0x004af0b0`, `0x004af190`, `0x004af1a0`, `0x004af1c0`, `0x004af630` (14 functions) (slice `shatter-and-emitter`)
Native: `src/runtime/bit_kit.hpp`, `bit_shatter.cpp`; replay `src/runtime/bit_retail_calls_test.cpp`; probe `re/gdb/probes/bit_specs4.py` (group `bsh`); fixtures `re/evidence/fixtures/bit-calls-bsh*.txt`

## What is here

The shatter turns the model of an object into flying pieces. A face is transformed through the GTE, its colour found, and then it is either cut into pieces (the triangle halved at a random point of its longest
edge, down to a depth) or sprayed as shards of glass; each piece is an *emitter*, a chunk bit with a vtable of its own that carries the model's colours and a velocity away from the shatter's centre.

- **Shatter_Corner** (cdecl): a vertex of the face through the GTE (`rtps` with the rotation and translation of the shatter) into whole units shifted back to Q12. **Shatter_Colour** (cdecl): the face's colour in
  the three bytes at `0x0056c53c`, either the packed colour itself or the mean of the three or four colours of the region's table that the face indexes.
- **Shatter_Face(item, face, depth, ..., only marked, clear, quota)**: answers 0 (not shattered), 1 (pieces) or 2 (glass). **Shatter_Triangle** (cdecl): with depth left the triangle is halved at a random
  eighth of its longest edge and each half goes on, at the depth less one, to be made one piece; the per-level scratch frame is `scratch + 0x2e0 + (level & 7) * 0x30`. **Shatter_Item(item, only marked,
  clear)** offers every face of the item's model to Shatter_Face; a fifth of them (while fewer than six pieces have been made) are cut finer.
- **Shatter_Glass** (cdecl): `count` glass shards sprayed from the face's edge midpoint down to the floor the zone query finds, kept to the burst nearest the camera. **Shatter_ForgetBurst** (and its thunk
  `0x004af190`, `0x004af1a0`): the nearest burst is forgotten. **Shatter_BurstSound**: the burst's sound once, then the flag is cleared.
- **Emitter**: `Emitter_Construct` (thiscall, five arguments) chooses the spin and the speed, `Emitter_SetVelocity` gives the piece its position and a velocity away from the wind's origin (a random part of
  the way) plus the wind and a fall, `Emitter_Move` (fastcall) lets the piece fall and spin, its smoke trail follows it, its corners are placed, and at the end of its life it dies; the destructors restore the
  vtable and run the chunk's.

## Method

Fake items with a model (faces, vertices, colour tables) and fake bits in the slab; the GTE register window is captured with the call. The zone collision query, the sound, the allocator, the trail's
constructor and the random numbers are stubs whose scripted values the native replays in order.

## Lessons

(1) The Ghidra decompile of `Shatter_Triangle` gives the wrong role to a stack argument: the depth is arg 11 minus one, which only the assembly shows. (2) `Shatter_Item` with a long model can run past the
harness's watchdog; those cases (2 in 40) are skipped. (3) The two functions at `0x004af190` and `0x004af1a0` are the burst thunks: the unlisted pair of the milestone's 175 that the cases cover through
`Shatter_ForgetBurst`.
