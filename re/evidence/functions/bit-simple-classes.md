# The special, non-rendered, textured-quad and quad bit classes (bit.cpp)

Status: confirmed by retail forced calls (5,994 cases of the simple bit classes, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00403fc0`, `0x00403fe0`, `0x00404000`, `0x00404020`, `0x00404040`, `0x00404060`, `0x00404080`, `0x004040b0`, `0x004040d0`, `0x00404150`, `0x00404170`, `0x004041c0`, `0x00404210`, `0x00404220`, `0x00404250`, `0x004042d0`, `0x00404390`, `0x004043d0`, `0x00404460`, `0x00404490`, `0x004044f0`, `0x00404590`, `0x004045e0`, `0x00404600`, `0x00404620`, `0x004046f0`, `0x00404810`, `0x00404840`, `0x00404860`, `0x004048c0` (30 functions) (slice `bit-simple-classes`)
Native: `src/runtime/bit_kit.hpp`, `bit_core.cpp`; replay `src/runtime/bit_retail_calls_test.cpp`; probe `re/gdb/probes/bit_specs.py` (groups `bit1`, `bit2`); fixtures `re/evidence/fixtures/bit-calls-bit[12]*.txt`

## What is here

Four small classes derive from the base and nothing else: the special display (`CSpecialDisplay`, list `0x0055f710`, its virtual slot 2 is drawn by `Bit_Display`), the non-rendered bit (`CNonRenderedBit`,
list `0x0055f704`, it only moves), the textured-quad bit (`CFT4Bit`, the base of most visible bits) and the quad (`CQuadBit`, list `0x0055f708`). Each has a constructor, a destructor (which restores its
vtable, leaves its list and runs the parent's) and a deleting destructor.

- **CFT4Bit.** It adds the transparency decay (`+0x3c`), the scale decay (`+0x3e`), the colour word (`+0x40`; bit 24 set means semi-transparent), the animation (`+0x44`, the frame entry at `+0x48`), the
  flags (`+0x4c`), the frame count (`+0x4d`), the frame (`+0x4e`), the fraction (`+0x4f`), the speed (`+0x50`) and the scale (`+0x52`). `SetAnim` looks an animation up by name and takes its frame count;
  `SetAnimIndex` selects one of the four animations `Bit_Init` looked up (asserting the index); `SetFrame` asserts the frame and that an animation is set. `IncFrame` adds the speed, scaled by the
  animation time scale, to the 8.8 clock (frame byte, fraction byte); `IncFrameWithWrap` wraps to frame 0 past the last frame; `ChooseRandomFrame` picks a random frame. `Shrink` and `Fade` take the scale and the colour down by their
  decays and answer 1 (optionally killing the bit) when they reach zero.
- **CQuadBit.** Its four corners are short vectors at `+0x3c`; `SetTextureIndex` / `SetTexture` set the texture record (asserting both the index and the frame), `FlagOn` ors flag bits,
  `SetSemiTransparent` / `SetTransparency` set the colour. `OrientUsing(normal, centre, half-width, half-height)` places the corners in the plane perpendicular to a short normal.

## Method

Fake bits as in the list slice, plus a pool of animation records for the lookup service. The lookups, allocator and random numbers are stubs. A zero-length axis makes `OrientUsing` divide by zero in retail
(SIGFPE in the forced call); those cases end in an error and are skipped (about one in five of the orient cases), the native never sees them.

## Lessons

(1) The 8.8 animation clock is advanced by `(speed * timescale) >> 8` in integers (the time scale is a dword global), the frame being the high byte of the sum as a *signed* byte; `IncFrameWithWrap` compares
that signed frame with the frame count and resets only the frame byte (the fraction keeps its new value). (2) Most
setters assert their argument; the assertion routine only logs, so a case with an out-of-range index continues with the written bytes the same on both sides.
