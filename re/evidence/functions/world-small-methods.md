# The small methods of the platform, the power-up and the traffic object

Status: confirmed by retail forced calls (3,500 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0049f340`, `0x0049f360`, `0x0049f440`, `0x004a07d0`, `0x004a81d0`, `0x004a81f0`, `0x004a8250`, `0x004a8260`, `0x004a8280`, `0x004a8290`, `0x004a82c0`, `0x004a82e0`, `0x004a8b4c`,
`0x004128a0`, `0x004128c0` (slice `world-small-methods`; `0x004a8b4c` through its entry point `Powerup_Update`, `0x004a8ac0`)
Native: `src/runtime/world_kit.hpp`, `world_extra.cpp`; replay `src/runtime/world_retail_calls_test.cpp`; probes `re/gdb/probes/world_specs_extra.py` (groups `wpp`, `wpk`), `world_specs_glow.py` (group `wpg`);
fixtures `re/evidence/fixtures/world-calls-wp*.txt`

## What is here

- **The platform** (vtable `0x005194f8`, list `0x0056af40`): the destructor (`0x0049f440`) leaves the list, stops its voice (`+0x204`, set back to -1) and runs the baddy destructor; the deleting
  destructor (`0x0049f340`) adds the object free. `0x0049f360` makes a platform at a position given as three dwords (the trigger-record constructor `0x0049f250` is the other way to make one): the baddy
  constructor, the region from `0x0056e218`, the list add, the flag word set to `(flags & ~2) | 0x111`, the animation `0x192` and the voice cleared.
- **The power-up** (vtable `0x00519684`, list `0x0056b830`): `0x004a81f0` leaves the list, releases its sprite and its glow (`0x004a8290`: the sprite release and a virtual delete of the child at `+0xc8`)
  and runs the object base destructor. `0x004a8250` (`maybe_destroy_model`) shows the power-up (`+0x41`) and jumps through slot 1 of its vtable (the kill method); `0x004a82e0` marks it dying and does
  the same unless it already is. `0x004a8260` sets the value word (`+0x5c`) and flag byte (`+0x6d`), `0x004a82c0` the node it came from, `0x004a8280` the sprite flag.
- **Powerup_Update and its tail** (`0x004a8ac0` and `0x004a8b4c`): the age check (a service), then by the distance to the skater (`+0xb8`, or, in a game of two, the distance to the second player) against
  the range at `0x00563a3c`: within it the glow block runs, outside it the kill method. The glow block (`0x004a8b4c`, which Ghidra lists as a function of its own) is a *tail* of Powerup_Update: it
  starts with the power-up in ebp, pops the registers and the exception frame Powerup_Update pushed and returns for it, so it cannot be called alone; it is replayed through Powerup_Update. It runs the
  power-up's physics (a service), allows sprites, makes the glow object on first use (a `0x5c`-byte sprite, set up and coloured by the power-up's type: three colour schemes, alternating by part), places it at
  the power-up and pulses its size with `sin(150 * frame)` times the amplitude at `+0xce`, plus the base at `+0xcc`.
- **The traffic object** (vtable `0x005184e0`): the destructor (`0x004128c0`) counts one car less (`0x0055faec`), leaves the baddy list and runs the baddy destructor; `0x004128a0` is its deleting
  destructor.

## Method

A fake object (0x230 bytes, all captured) and two fake sprites per group; the power-up's kill method is slot 1 of its vtable, `Powerup_Release` (`0x004a8290`), a function under test in `wpp`, so the two
functions that reach it through the vtable (`0x004a8250`, `0x004a82e0`) are in a group of their own (`wpk`) where it is a stub. The glow group stubs the glow's methods and the power-up's age check and physics.

## Lessons

(1) A Ghidra function can be the middle of another: `0x004a8b4c` starts after the `ret` of `0x004a8ac0` but is reached by its `jbe` and leaves by its epilogue; a forced call at its address crashes in the
epilogue's `pop`s, so it is replayed through its entry point. (2) A fake object's call through a retail vtable reaches the real function when that function is under test in the same group: the stub for the
slot's target is skipped silently, so the case runs retail code the port models as a service.
