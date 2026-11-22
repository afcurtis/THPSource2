# The start-up and clean-up sequences (init.cpp)

Status: confirmed by retail forced calls (2,700 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00458270..0x004588b7` (9 functions: `Init_KillAll`, `DeleteList`, `Init_Cleanup`, `Init_ResetMemory`, `Init_Restart`, `Init_ForGame`, `Init_DebugStuff`, `Init_AtStart`, `Init_AtEnd`)
Native: `src/runtime/core_kit.hpp`, `src/runtime/core_init.cpp`; replay `src/runtime/core_retail_calls_test.cpp`; probe `re/gdb/probes/core_specs_init.py` (group `ini`);
fixtures `re/evidence/fixtures/core-calls-ini*.txt`

## What is here

`init.cpp` is a set of fixed sequences of calls into the rest of the game plus the resets of a few globals. Every callee is a service reported by its address
(`engine_<address>`, cdecl, with the arguments the code pushes), so a replay checks the order and the arguments of about fifty calls and the memory the functions write.

- `DeleteList(head)` walks a list through the next pointer at `+0x20`, reading the next object **before** deleting the current one, and deletes each object through its virtual
  destructor (slot 0, argument 1) unless bit 5 of its flag byte at `+0x6a` is set.
- `Init_KillAll` sweeps seven lists in the order player record `0x56a960`, `CameraList`, baddies, game objects, trigger scripts, power-ups, active objects, then **checks** the heads in
  a different order (the trigger-script list before the game objects) with seven assertions ("Leftover protected members of ...", lines `0x106..0x10c`); a destructor is expected to
  unlink its object, so a probe whose fake destructors do nothing sees the assertions fire (and a probe with empty lists sees them pass). Then it clears the trick objects, the rails, the
  bit effects and the messages.
- `Init_Cleanup(mode)`: the viewport pointer and the loading flag are reset, the sound, movie and music are stopped, `Init_KillAll` runs; mode 0 is the **hard** clean-up (the log line,
  the reverb, the two skater handles at `0x568648`, the three objects at `0x56b73c / 0x56b710 / 0x56b70c`, the PSX regions, the list of bit resources at `0x56db38` (each node released,
  next read first), the trigger file, the trick data, the sound banks, the menus, the palette), mode 1 drops the environment regions, mode 2 clears bit 10 of the environment objects'
  flag word and keeps the trick objects. All modes then reseed the random generator (`Utils_InitialRand(0x12a6cc58)`), clear the trigger menu, reset the fade and the pads and set
  `0x535af8 = 13000`, `0x533f90 = 0x533f94 = 0x7fffffff`.
- `Init_ResetMemory(bytes)` lays the two heap zones out in the 6 MB block `0x562018`: zone 0 `[base, base + bytes)` and zone 1 `[base + bytes, base + 0x5dc000)`, starts the heap
  and clears seven usage counters.
- `Init_Restart` is `Init_Cleanup(0)` followed by the database reset, the spooler, the title timeout (`0x52be4c = 0x14`) and the palette (`Pal_Init(0x100, 0x50)`);
  `Init_ForGame` resets the viewport pointer and ends in a jump (the last call is a tail call). `Init_AtStart` allocates the 6 MB block, **clears it** (`rep stosd`) and starts
  every subsystem; `Init_AtEnd` shuts them down and releases the block.
- The PlayStation library stubs the retail code also calls (`0x004e86f0..0x004e8d0f`) answer 0 or nothing and touch no memory (4.14a): the port omits them.

## Method

Forced calls on generated state (`P410_GROUPS=ini`): fake list objects with a two-slot vtable in a scratch slab, the globals listed above poked with random words. Every callee is a
stub; the 6 MB block is a real block taken from the C runtime once (the replay does not hold it, so the port skips the clearing when the block is not mapped).

## Lessons

(1) The order of the leftover assertions is not the order of the sweeps: the replay reported it at once. (2) `Init_Restart` and `Init_Cleanup` are called with arguments `Init_Restart(0, 0x10)`
that the function ignores. (3) A `rep stosd` over a block the capture does not hold cannot be compared: say so rather than hide it.
