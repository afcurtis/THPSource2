# The script object, the baddy classes and the assertion routine (the head of the executable)

Status: confirmed by retail forced calls (5,600 cases of the script / baddy layer and the assertion routine, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00401000`, `0x00401030`, `0x00401100`, `0x00401120`, `0x004011d0`, `0x004011e0`, `0x00401240`, `0x00401250`, `0x00401260`, `0x00401270`, `0x004012d0`, `0x004012f0`, `0x00401410`,
`0x00402f80`, `0x00402fc0`, `0x00403000`, `0x004030b0`, `0x004030d0`, `0x00403400` (slice `world-script-baddy-classes`)
Native: `src/runtime/world_kit.hpp`, `world_baddy.cpp`; replay `src/runtime/world_retail_calls_test.cpp`; probe `re/gdb/probes/world_specs_baddy.py` (groups `wbd`, `wasr`); fixtures
`re/evidence/fixtures/world-calls-wbd*.txt`, `world-calls-wasr*.txt`

## What is here

- **The script object** (vtable `0x0051836c`, list head `0x0055f6b0`, count word `0x0055f6b4`, the object's id a word at `+0xc8`): `0x00401000` / `0x00401030` find the script object by id and clear / set
  bit 0 of its flags; the destructor (`0x00401120`) leaves the list, asserts the count is not zero, counts one less and runs the object base destructor.
- **The baddy base class** (vtable `0x00518388`, count `0x0055f6b8`): the constructor (`0x00401270`) runs the animated object's constructor and clears `+0x1c0..+0x1cc`, sets the two voices to -1 and a
  turn word to `0x80`; the destructor stops the voice (`+0x1d0`) and runs the animated destructor. `0x00401410` is the movers' physics step: the two frame scalars (`0x0056af38` / `0x0056af3c`) are copied into
  `+0x80..+0x84`, the velocity (`+0x4c`) gains the push (`+0x58`), decays by shifts (the bytes at `+0x6c`), goes through the dead zone and is added to the position; the spin (`+0x70`) is added to the
  angles (`+0x14`, masked to 12 bits), gains its own push and decays (`+0x7c`) and snaps near zero.
- **The type-0xcb baddy** (vtable `0x005183b0`, list `0x0055f6bc`): its constructor reads the position and angles from a trigger record (two chained readers), keeps the owner (`0x0056a954`), the stream
  pointer after the parameters and the link word; its destructor leaves the list, stops its voice, deletes its sprite and each child of its table (virtual delete), frees the table and runs the baddy
  destructor. `0x00402f80` / `0x00402fc0` ask the AI query `0x004c9fd0` with the player record as target or as viewer (0 while the player is dying); `0x00403400` jumps into the script field setter.
- **The assertion routine** (`0x004011e0`): when the condition's low byte is zero it formats the message into a 0x200-byte local (`vsnprintf` of the format and the caller's argument list) and logs it with
  the file name and line the assertion macro left in `0x0056881c` / `0x00568638`. Every other function reports a failing assertion by the format string's address (the port's `check`).

## Method

The object classes use the same fake object as `world_specs.py`; the script objects are a chain of three in the slab; the type-0xcb baddy's table of children and its sprites are fake objects whose vtable is
the retail sprite class's. The assertion routine has a group of its own because every other group stubs it; there the formatter and the log line are the stubs (the buffer and the argument list are stack
locals, recorded as fixed scratch addresses, and the format string is the call's text).

## Lessons

(1) The movers' step reads its decay counts from the bytes at `+0x6c` and `+0x7c` *after* the earlier components were written, as the in-place vector operators do. (2) The assertion routine's second
parameter list is a pointer into its caller's frame (the `...` of the macro), which the replay never reads.
