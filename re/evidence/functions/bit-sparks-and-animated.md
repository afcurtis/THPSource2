# The sparks, animated bits, ribbons and glass (bit.cpp)

Status: confirmed by retail forced calls (8,400 cases of the spark and animated classes, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00406070`, `0x00406090`, `0x004060b0`, `0x00406160`, `0x004061c0`, `0x004061e0`, `0x00406200`, `0x00406300`, `0x00406320`, `0x00406330`, `0x004063e0`, `0x00406490`, `0x004064b0`, `0x00406560`, `0x00406570`, `0x004065c0`, `0x00406660`, `0x00406680`, `0x00406690`, `0x00406710`, `0x00406730`, `0x00406740`, `0x00406780`, `0x00406870`, `0x00406890`, `0x00406940`, `0x00406960`, `0x00406980`, `0x004069a0`, `0x00406b10`, `0x00406b30`, `0x00406bf0`, `0x00406c30`, `0x00406c60`, `0x00406d00`, `0x00406db0`, `0x00406dd0`, `0x00406de0`, `0x00406e30`, `0x00407010`, `0x00407030`, `0x00407050` (42 functions) (slice `bit-sparks-and-animated`)
Native: `src/runtime/bit_kit.hpp`, `bit_spark.cpp`; replay `src/runtime/bit_retail_calls_test.cpp`; probe `re/gdb/probes/bit_specs2.py` (group `bit4`); fixtures `re/evidence/fixtures/bit-calls-bit4*.txt`

## What is here

- **Spark settings.** `Bit_SetSparkTrajectory` / `Bit_SetSparkTrajectoryCone` store the angle triple the sparks fly along and the three angle ranges they spread in; `Bit_CalculateSparkVelocity(out, magnitude)`
  is the direction of the trajectory with each angle moved by a random part of the cone (the sine and cosine tables are MathKit's), scaled to the magnitude; `Bit_SetSparkSize` asserts the size is below 16;
  the next two set the colour and the colour fade the next sparks get.
- **Spark** (`CSpark`, a pixel bit): the constructor takes position, speed, gravity and life. `Move`: gravity on the height with an optional floor, the position follows the velocity which decays by an
  eighth, the colour fades by its rates, and at the end of its life the spark dies.
- **Animated flat bits.** `CSimpleAnim` (constructors by name and by index; `Move` runs the animation once and dies at its last frame, or loops back to its first or its last frame), `CMotionBlur` (position,
  velocity, animation, scale, scale decay, tint decay; `Move` shrinks and fades it and kills it when either is done) with `CMotionBlurImmortal` (the same `Move` that kills nothing), and `CFrag` (a random
  velocity in a cube, gravity and a random life).
- **Ribbon.** `CRibbon` is a ring of points and a bit for each pair of them (`CRibbonBit`); `SetTint` and `SetScale` write every bit, `SetPos` pushes a position into the ring and sets the two ends of each
  bit from the ring's points a bit's length apart. `CSmokeTrail` is a ribbon of two points per bit that fades from a colour to black along its length; once it is released its bits fade and it dies when all
  are black.
- **Glass.** `CGlassBit` has three corners scattered around a position, a velocity, a floor and fading colours; `Move` lets the corners fall with gravity 10000 on the height, bounces a corner that goes under the
  floor up (twice at most, halving the horizontal speed), fades by 4 a frame once its life is over and dies when black.

## Method

Fake bits and animation records in the slab; the allocator, block-size query, resource lookup, random numbers and the virtual deleting destructors of the bits the ribbons own are stubs. Ribbon cases link up to a
dozen child bits whose destructor calls are replayed in order.

## Lessons

(1) Where the destruct paths walk a child list the stub destructors are scripted calls: their order is part of the comparison. (2) A ribbon is an object plus an array of bits and a ring of points (`(points - 1) * bits + 1` of them): the probe's `fake_ribbon` lays the whole structure out in the slab, with the bits in the
slots after the ribbon's, so the setters, `SetPos`, the destructors and the smoke trail's `Move` all see a consistent ribbon.
