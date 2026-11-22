# The bit sweeps, polylines, smoke generator, line bits and small helpers (bit.cpp, bit2.cpp, blood.cpp)

Status: confirmed by retail forced calls (5,348 cases of the sweeps and line classes, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00407250`, `0x00407270`, `0x0040a740`, `0x0040a860`, `0x0040a880`, `0x0040a900`, `0x0040aa20`, `0x0040ab50`, `0x0040ab70`, `0x0040ad30`, `0x0040ae20`, `0x0040ae40`, `0x0040ae50`, `0x0040af70`, `0x0040afe0`, `0x0040b000`, `0x0040b060`, `0x0040b1a0`, `0x0040b1c0`, `0x0040b2e0`, `0x0040b300`, `0x0040b350`, `0x0040b3e0`, `0x0040b410`, `0x0040b4e0`, `0x0040b500`, `0x0040b570` (27 functions) (slice `bit-sweeps-and-lines`)
Native: `src/runtime/bit_kit.hpp`, `bit_misc.cpp`; replay `src/runtime/bit_retail_calls_test.cpp`; probe `re/gdb/probes/bit_specs3.py` (group `bit5`); fixtures `re/evidence/fixtures/bit-calls-bit5*.txt`

## What is here

- **Sweeps.** `MoveList(head)` calls slot 1 (Move) of every bit of a list whose dead flag is clear (a dead one waits for the removal sweep); `Bit_Move` does this for thirteen lists in the order pixel, flat,
  glass, general polygon, chunk, quad, glow, non-rendered, linked, polyline, gouraud polyline, line, special.
- **Polylines.** `CPolyLine(segments)` (list `0x0055f70c`) is a record of 16 bytes per segment (point and colour) with every point at the start point (three dwords at `0x0056af28`) and the colour
  `0x50ffffff` (the constructor asserts 1..99 segments and adds the block to the heap byte count); `SetSemiTransparent` sets the bit on every segment colour. `CGPolyLine` (gouraud, list `0x0055f6e0`) is the
  same with a colour for the whole line (bytes `ff ff ff 50` at `+0x50`).
- **Smoke generator.** `CSmokeGenerator(position, count, ...)` (asserts a nonzero duration) makes `count` motion-blur puffs rise from the position every frame (`Move`) and ends after its lifetime
  (`0xffff` never).
- **Line bits.** `CGLine` (list `0x0055f6cc`) holds two points (`+0x44`, `+0x50`) and two colours (`+0x3c` = `0x50808080`, `+0x40` = `0x55808080`); `CGLineBit` is a line that flies from a point of an object: a spark velocity, gravity, a random life, an end offset by the object's
  position (the hook query is a service).
- **Blood and helpers.** `Blood_Reset` deletes every blood bit and clears its count and age; the blood manager's destructors, `CClass_Destruct` (the base class: it only leaves its vtable), `Vector_Jitter`
  (each component moved by a random amount about zero, x4 / x2 / x4), the camera's small random generator (`Camera_RandSeed` sets the multiplier `0x12b9b0a1`, the increment `0x0aa2fb3f` and the seed; `Camera_Rand(limit)` is `seed = a * seed + b`, then `a` and `b` are stirred
  with the seed's signed shifts, and the answer is `((seed & 0xffff) * limit) >> 16`) and the
  camera's identity quaternion constructor call.

## Method

The slab holds fake lists of bits (the Move and deleting destructors are the stubs of the first slice), polyline records and the camera generator's state. The class delete, the quaternion constructor, the
random numbers and the hook query are stubs. The camera generator is replayed from its state words: the cases poke the two seed words and compare the new state.

## Lessons

(1) `MoveList` reads the dead flag and the next link of each bit *after* the virtual Move returns (the stub does nothing, so the order of the list walk and of the stub calls is what a case shows).
(2) The sweeps and the polylines have almost no arithmetic: nearly all the risk is in the order of the stub calls and in the heap byte count the constructors add to, so the cases carry several links per list.
(3) The camera generator is its own state (three dwords), not the game's random numbers, so the cases poke and compare those three words.
