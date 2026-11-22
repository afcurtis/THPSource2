# The bail, the rumble, the pad history and the tricks.bin loader (physics.cpp)

Status: confirmed by retail forced calls (4,000 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0048fb20`, `0x0048ffc0`, `0x004904d0`, `0x004916d0`, `0x00491c10`, `0x00491da0..0x00491f40`, `0x00492a90..0x00492e90` (16 functions; slices `skater-bail-and-pad-history` and `tricks-bin-loader`)
Native: `src/runtime/skater_kit.hpp`, `skater_trick.cpp`, `skater_tricks_load.cpp`; replay `src/runtime/skater_retail_calls_test.cpp`; probes `re/gdb/probes/phys_specs_trick.py` (group `pht`),
`phys_specs_load.py` (group `phl`); fixtures `re/evidence/fixtures/skater-calls-pht*.txt`, `skater-calls-phl*.txt`

## What is here

- `CBruce__Bail(script, cause)` (ret 8) is the fall. It clears the trick/script state (a long list of fields), counts the bail (`+0x302c`), asks `NetGame_IsRemotePlayer` and, outside the versus mode 9, tells the netgame
  layer (`NetGame_SetLocalFlag(0x100000, 1)`), runs `CBruce__Trick_Bail`, sets `+0x2f64 = 1`, resets the trick buffers, goes to state 1 (`StateTransition(1, 0x715)`) and starts the bail script `script` of
  the state-script table (`TrickStateScriptOffsets`; when `script` is 0 and the speed `sqrt(v.v) * 64` is under `0x2d000`, script 1) and runs it once (`TrickScript_Execute`).
- `CBruce__Trick_Bail` zeroes the combo counters, lets the crowd react (to the landed value of the score panel at `+0x168`, or to the speed >> 12 when nothing landed), tells the panel, and rumbles
  (`CBruce__Vibrate(8, 1, 0x96)`): the pad of the skater's player (`+0x2cc4`, xored with `0x56a854` in the mode 7) when its setting in `0x56a3e0` is on and no movie plays.
- `Skater_BailWhenFlipping` bails with the animation `+0x29f0` when a flip phase says the board comes round wrong, or when the script flag is clear; not when already bailing or when `0x56a890` locks bailing.
- The pad history is 32 entries of 8 bytes at `+0x2a14` (button, pressed, two pad bytes, time) with the head at `+0x2b14`: `TrickInput_PressTime(button, skip)` looks back from the entry before the head (31 entries;
  the head itself is never read) for the `skip`-th pressed entry of the button and answers its time, -1 for none; `TrickInput_PressedTogether(a, b, window)` and `TrickInput_BothRecent` build on it with assertions
  on the button numbers (at most 0x10).
- `Tricks_LoadBinary` loads `tricks.bin` once (`FileIO_OpenLoad`) and points seven globals at the offsets in its header (seven signed shorts, each added to the file's start); `UnloadGTricks` releases it.
  `TrickScript_Initialize` loads it, resets the extra animation list (`0x005365ac`: the fixed list ends with -1, the extra animations follow behind its end mark, at most 300), lets the runtime build the
  player tables and parses player one's (and player two's) table: records of (button count 1..4, that many buttons, a stream offset, one more word) ended by a count of 0; every `1` command of a stream registers
  the animation it names. The command sizes (`TrickScript_CommandSize`) and the word reader are services of the interpreter.

## Method

Forced calls on the live skater (as in `skater-state-helpers.md`) with the bail's services stubbed (the crowd, the panel and its landed value, the rumble, the movie test, the netgame queries, the script
interpreter); `TrickScript_Start` is the real retail code (it is also native in the 4.7a layout; the port keeps a retail-memory copy). The loader group uses a fake tricks.bin in the slab and stubs for the file
loader, `Mem_Delete`, the word reader (the record is the pointer the local holds: `deref=[0]`), the command size and the skater manager.

## Lessons

(1) A stub's `result` callable gets the arguments after `stack_to` mapping, so a stub that must read through a pointer to a stack local uses `deref` (the record is the pointer) and the raw argument for its
answer. (2) `FUN_00492c70` answers in `al` (upper bytes are leftovers of the assertion call). (3) A `head` outside 0..31 would make `TrickInput_PressTime` loop for ever: the generator keeps it in range.
