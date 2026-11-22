# The transient-bit lists and the bit base class (bit.cpp)

Status: confirmed by retail forced calls (3,400 cases of the bit lists and the base class, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00403680`, `0x00403730`, `0x00403760`, `0x00403bd0`, `0x00403c70`, `0x00403ca0`, `0x00403d30`, `0x00403dc0`, `0x00403df0`, `0x00403e10`, `0x00403e40`, `0x00403ea0`, `0x00403ec0`, `0x00403f00`, `0x00403f50`, `0x00403f70`, `0x00403f90` (17 functions) (slice `bit-lists-and-base`)
Native: `src/runtime/bit_kit.hpp`, `bit_core.cpp`; replay `src/runtime/bit_retail_calls_test.cpp`; probe `re/gdb/probes/bit_specs.py` (group `bit1`); fixtures `re/evidence/fixtures/bit-calls-bit1*.txt`

## What is here

Every short-lived effect of the game (sparks, smoke, glass, dust clouds, glows, the flying pieces of a shatter) is a "bit": a heap object whose first dword is a vtable and which sits in one of thirteen
singly-headed doubly-linked lists (`0x0055f6c0..0x0055f724`, each list a head pointer; the bit's `+4` is the previous bit and `+8` the next). The base class (`CBit`, vtable `0x005183d8`) lays down the
age (`+0xc`), the lifetime (`+0xe`), the position (`+0x10`), velocity (`+0x1c`) and acceleration (`+0x28`) as Q12 dwords, three friction shift bytes (`+0x34..0x36`), the dead flag (`+0x37`), the protected
flag (`+0x3a`) and the type byte (`+0x3b`).

- **Init.** `Bit_Init` zeroes the bit count (`0x0055f72c`) and the thirteen heads, sets the primitive mode word to `0xe1000620`, looks up the four quick animations (a table of four names, the
  textured bits select one by index), the buttons animation and the shadow material (checksum `0x2a3b2550`). The count is zeroed here, not by `Bit_DeleteAll`.
- **Deletion.** `DeleteBitList` walks a list reading the next link *before* it calls the deleting destructor (slot 0 with argument 1) of every bit that is not protected; `Bit_DeleteAll` does this for the
  thirteen lists and asserts that nothing (no protected bit and no live count) is left. `RemoveDeadBits` / `Bit_RemoveDeadBits` delete only the bits whose dead flag is set.
- **Allocation.** `CBit_New` takes a zeroed block from the loader's heap (asserting the size is below `0x200`) and adds its size to the heap byte count (`0x0055f728`); `CBit_Delete` subtracts the size the
  heap reports and frees it. Both are services of the replay (the heap), the byte count is compared.
- **The base methods.** `CBit_AttachTo` / `CBit_DeleteFrom` link a bit in as the head of / unlink it from a list (and keep the count); `CBit_Die` sets the dead flag (asserting the bit is not protected);
  `CBit_SetPos` copies a position; `CBit_SetRandomVelocity` gives the bit a random velocity in a cube; `CBit_Move` is `position += velocity; velocity += acceleration; velocity -= velocity >> shift`
  with an arithmetic shift per axis; `Bit_ReduceRGB` lowers the three colour bytes of a word, saturating at zero. The constructor, deleting destructor and destructor of the base are in the same file.

## Method

The probe builds up to a dozen fake bits (0x100 bytes each, a fake vtable whose slot 0 is the stub `0x004ad950`, a deleting destructor that only records its call, and whose other slots are the bare `ret`
at `0x004ad9a0`) in the slab and points the list heads at them. The heap, the random numbers, the resource lookups and the virtual deleting destructors are stubs whose scripted values the native side
replays in order; the replay compares the return value, every written byte and the ordered service calls.

## Lessons

(1) `Bit_Init` zeroes the bit counter itself; a native that left the count to its callers differs only in that dword. (2) `DeleteBitList` must read the next pointer before the call: the stub destructor does not
free anything, but a random `next` poked by a case is what shows the order. (3) The deleting destructor is a vtable call; the stub is installed once per address however many slots use it.

## Empty virtual methods (10g session 17)

`0x004044d0` (`CBit_EmptyHook`), `0x004044e0` (`CBit_EmptyHook1`) and `0x004057e0` (`CBit_EmptyHook2`) are a lone `ret` each. The first fills the Move slot (slot 1) of every bit class that does not move and a dozen other slots of the
bit vtables (0x005183d8 onwards), so `Bit_Move` calls it on every frame once such a bit lives. All are `WorldKit::empty_hook`, replayed over 30 forced calls each (`world-calls-bemA.txt`, `world-calls-bemB.txt`, `world-calls-bemC.txt`, group `bem` of `re/gdb/probes/world_specs_tips.py`).
