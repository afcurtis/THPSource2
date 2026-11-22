# Trick-manager and engine leftovers: the trick tables, a record bank, the singleton, two string helpers and two small game helpers

Status: confirmed by retail forced calls (400 cases of the trick functions, 1,100 of the engine ones, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00416050`, `0x004bb770`, `0x004bb790`, `0x004bd1e0` (4 functions) (slice `trick-runtime-leftovers`), `0x004b0b50`, `0x004b0b90`, `0x004b0bb0`, `0x004b8340`, `0x004b8360`, `0x004b8380`, `0x004b83b0`, `0x004c43b0`, `0x004c43e0`, `0x004c43f0`, `0x004c4400` (11 functions) (slice `small-engine-leftovers`)
Native: `src/runtime/createobj_kit.hpp`, `createobj_misc.cpp`; replay `src/runtime/createobj_retail_calls_test.cpp`; probe `re/gdb/probes/cobj_specs2.py` (group `cmisc`); fixtures
`re/evidence/fixtures/createobj-calls-cmisc*.txt`

## What is here

**Tricks.** `TrickConfig_FindByCharacter(n)` is the address of a character's career record, `0x00568a6c + n * 0x104`. The trick manager (vtable `0x00519828`, 0x140c bytes) has a destructor
`TrickManager_Destructor`, which unloads the trick scripts when its flag at +0x1408 is set and restores the base vtable, and a deleting form. `TrickRuntime_BuildPlayerTables` makes a manager for the length of the
call and asks it to build the trick script table of player one from the pointer at `0x00568a50`, and of player two (when two are playing) from `0x00568a4c`; it deletes the manager through its vtable.

**A record bank** (`0x004b0b50`, vtable `0x00519734`) is eight cleared triples (+4..+0x63) and twenty cleared words from +0x68, with its destructor and deleting form. **The singleton** at `0x0056db98`
(vtable `0x00519890`) has a constructor that only sets the vtable, a thunk that builds the global one, a destructor and a deleting form.

**Helpers.** `String_ToLowerInPlace` turns A..Z into a..z (a signed compare, so bytes above 0x7f are left alone); `Char_IsBlank` answers whether a byte is a space or a tab;
`LevelEvent_SelectPlayers` copies the two player pointers (`0x0056a858`, `0x0056a85c`) to `0x0056db5c` and clears the selector at `0x0056db64`; `Mess_AddViewportMessage(player, message)` queues a message at the
player's place in the viewport (`Panel_ViewportOffset(mode*2 + the player's short at +0x170, &x, &y)`, then `Mess_AddMessage(message, format, 0, x, y)`), unless the player is on the other machine of a network game.

## Method

The objects sit in the slab. The manager of `TrickRuntime_BuildPlayerTables` is the first block the arena hands out: the generator pokes its vtable there, and the stub of the manager's initialiser answers that block.
The vtable's first slot is read from a static window (`READ_ONLY`) and decides what the virtual delete runs (the native checks the slot against the addresses it ports and reports any other as a service).
`Panel_ViewportOffset` writes through two pointers into the caller's frame: the stub pokes them and records the values for the replay, the pointers being a fixed scratch address.

## Lessons

(1) A class pointer read from a vtable in static memory has to be in a captured window, or the replay cannot take the same branch as the retail code. (2) The singleton thunk (`0x004c43b0`) passes a fixed
object in ecx and jumps into the constructor, so its replay needs no `this`.
