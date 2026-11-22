# The object base class of the world objects (ob.cpp)

Status: confirmed by retail forced calls (6,900 cases of the object base layer, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0047fe30`, `0x0047fe90`, `0x0047ffc0`, `0x00480060`, `0x00480140`, `0x00480160`, `0x004801c0`, `0x00480240`, `0x004802c0`, `0x004803e0`, `0x00480420`, `0x00480430`, `0x00480460`,
`0x004805d0`, `0x004805f0`, `0x00480600`, `0x00480610`, `0x00480660`, `0x00480680`, `0x00480720`, `0x00480c30`, `0x00480d90`, `0x00480f50` (slice `world-object-base`)
Native: `src/runtime/world_kit.hpp`, `world_object.cpp`; replay `src/runtime/world_retail_calls_test.cpp`; probe `re/gdb/probes/world_specs.py` (group `wob`); fixtures `re/evidence/fixtures/world-calls-wob*.txt`

## What is here

Every world object (the platforms, the power-ups, the baddies, the skaters, the traffic cars) starts with the same record, which `Object_Construct` (`0x00480060`) lays down after the record helper
(`0x0047fdb0`, a service here) has cleared its first part: the vtable (`0x0051938c`), a flag word (`+4`: `0x2` moves, `0x4` the colour changed, `0x400` flashing), the position as three Q12 dwords (`+8`),
three angle words (`+0x14`), the region byte (`+0x1f`), the list links (`+0x20` next, `+0x34` previous), the colour (`+0x24`), the flash state (`+0x38` saved colour with two flag bits, `+0x44` frames left,
`+0x46..+0x4a` the per-frame change of red, green and blue), the list the object waits in (`+0x64`), the state word (`+0x6a`: `0x1` suspended, `0x2` may be suspended, `0x8` wants a sprite, `0x40` dying,
`0x80` delete queued) and the sprite object (`+0xa4`).

- **Flash.** `CItem__Flash` (`0x0047fe90`, four arguments: frames and the three channels): 0 frames sets the colour at once; a negative count fades in from grey (the colour is set to `0x808080` and the
  step is `(channel - 0x80) / -frames`); a positive count fades out *to* the colour (the step is `(0x80 - channel) / frames`, the current colour is saved in `+0x38` with `0x1000000` and, if the object was
  already flashing, `0x2000000`). `CItem__FlashUpdate` (`0x0047ffc0`) adds the steps to the colour (a byte at a time, wrapping), counts the frames down and, at zero, puts the saved colour back and clears or
  keeps the flashing bit as the saved flag says.
- **Destruction.** The deleting destructors (`0x00480140`, `0x00480660`) call the destructor and, when bit 0 of the flag argument is set, `Object_Free`. The base destructor (`0x00480160`) deletes the sprite
  object through its own vtable (slot 0 with argument 1) and runs the record helper's destructor (`0x0047fe20`); the animated object's destructor (`0x00480680`) frees its four animation buffers first. The
  jump thunks at `0x004801c0` and `0x00480720` are `jmp 0x0047fe20` and `jmp 0x00480160`.
- **The sprite object.** `Object_WantSprite` (`0x00480420`) sets state bit `0x8`; `Object_SpriteUpdate` (`0x00480460`) makes the object's sprite (a `0x6c`-byte block from the display's allocator, set up
  with a texture, a mode and a size) when it is wanted and absent, sizes it from the fields at `+0x9c..+0xac` (a call with the position words when the pitch word is below -0x80) and scales it by
  `(whole - part) * 128 / whole / 6`; when the bit is clear it releases it (`0x00480430`).
- **The rest.** `TRG_ReadFixedPosition` / `TRG_ReadObjectParameters` read three dwords (shifted left 12) / three words out of a trigger record and answer the address after them (the first asserts the
  record is 4-aligned). `Object_SetStateBit` (`0x004803e0`) sets bits of the byte at `+0x86` (refusing when one is already set) or clears them. `CSuper_FrameReached` is a range test of an animation frame
  against a track, with the range's direction in `+0x112`. `Skater_MergeModelParts` gives each part name the place of the equal name in the order list. `Animation_UpdatePoseFrame` reads the links packet for
  a frame, interpolates the pose and rebuilds the transform.

## Method

A fake object (0x260 bytes, all captured), two fake sprite objects (their vtable is the retail sprite class's, whose slot 0 is a stubbed function), a record of fixed-point words, a pool of strings and two
arrays of string pointers sit in the slab. The record helper, the allocators, the sprite object's methods and the pose builders are stubs; the sprite flag at `0x005244cc` is captured.

## Lessons

(1) Every assertion in the object code first copies the file name and the line number into the globals `0x0056881c` and `0x00568638`; they are not compared (the port reports only the failing assertions).
(2) `TRG_ReadFixedPosition` and `TRG_ReadObjectParameters` answer the *advanced* pointer in eax, which the constructors chain (`ReadParameters(ReadPosition(record))`). (3) The sprite's local struct of three
words is passed by pointer to a stack local: the probe records its bytes as the call's text and the pointer as a fixed scratch address.
