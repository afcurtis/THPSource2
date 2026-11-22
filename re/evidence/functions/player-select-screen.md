# Player-select screen (playsel.cpp)

Status: ported and replayed against retail forced calls (Phase 4.10f)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slice: `player-select-screen`. Native code: `src/runtime/player_select_{window,panels,view,load,dispatch}.cpp` (methods of `PlayerSelect`, `player_select.hpp`, which derives from
`ScreenKit`). The window classes of `player_ui.cpp` this screen builds are in [player-select-window-classes.md](player-select-window-classes.md).

Read first: [create-select-screen.md](create-select-screen.md) (the 4.10e1 slab and the forced-call pattern), [menu-toolkit.md](menu-toolkit.md) and
[frontend-window-base.md](frontend-window-base.md).

## What the screen does

`PlayerSelect_Run` (`0x004a5f60`) is the "choose your skater" screen every game mode shows (`mode` is the caller: 1, 2, 3 and 10 are the single-player kinds, 5, 7, 8 and 9
the two-player ones). It loads five fonts, builds the select window (`0x4dc` bytes, vtable `0x005195a4`) and runs frames until the update answers 1 (go on) or 2 (back).
The window holds a skater view (the 3D model with its stat bars), a bio window, page dots, a name, seven prompt windows, a header (two players) or a medal window
(single player), the main menu window and the memory-card load window, and has three states (`+0xac`):

| State | Meaning | Pad |
|---|---|---|
| 0 | choose a skater | left and right step through the 20 characters and the custom entry `0x14`, skipping `0x10` and the characters that are not available (`SkaterView_StepSelection`); confirm accepts (a locked skater: the refusal sound `0x92`); back goes to the previous player or leaves (answer 2) |
| 1 | the main menu | continue (1: next player or finish), appearance (the next costume, or for a custom skater the editor, exit code `0xc`), buy stats (`0xf`), buy tricks (`0x10`), skate shop (`3`); back returns to state 0 |
| 2 | load a custom skater from the memory card | the load window's own state machine: wait for the card, pick a skater (`LoadWindow_LoadSkater`), or go back |

`PlayerSelectWindow_Update` first spins the skater with the shoulder buttons (`SkaterView_Rotate` by `-30` / `+30`), then runs the state. `PlayerSelectWindow_UpdateFrame` (virtual slot 1)
makes the active player's skater current, shows the name (ellipsized to `0xe6` pixels; a custom skater's is a text-table string) and refreshes the view; `PlayerSelectWindow_Draw`
(called directly by the loop, not through the vtable) hides everything and shows what the state needs.

## Functions (72)

| Group | Addresses | Functions |
|---|---|---|
| callbacks and run | `0x004a1fe0` (record), `0x004a2000` (exit), `0x004a2010` (`*result = run(*argument)`), `0x004a5f60` | `PlayerSelect_InitCallbacks`, `_ExitCallback`, `_LookupSlot` (the screen call), `_Run` |
| the window | `0x004a2030`..`0x004a2bc0`, `0x004a2cb0`..`0x004a36e0`, `0x004a5f50` | construct, delete, initialiser (2.7 KB), destroy, set-prompts-hidden, draw, update-frame, the three state updates, next-player, update, the menu's choice, the selection save and restore |
| the bio window | `0x004a3830`..`0x004a3a60`, `0x004a3af0`, `0x004a3bd0`, `0x004a3e00`, `0x004a3e40` | construct, delete, destroy, draw, update (rebuilds the lines when the character changed), the two scroll limits |
| page dots, header, main menu | `0x004a5450`..`0x004a5510`, `0x004a5690`, `0x004a55c0`; `0x004a56b0`..`0x004a57e0`, `0x004a5880`; `0x004a5890`..`0x004a5d80` | |
| the skater view | `0x004a3e80`, `0x004a4360`..`0x004a5390`, `0x004a61f0`, `0x004a6260`, `0x004a6e60`..`0x004a6ea0` | construct (the camera, the lights, ten sprites, the model view), destroy, stat sprites, draw (the viewport, the skater render, the face), load-skater (slot 4), update, create-object, the packet rewrite `0x004a5080`, the selection steps, and the embedded spool state's slot, free, busy, flag and reset methods |
| the load window | `0x004a6f50`..`0x004a78f0` | construct, delete, enter, destroy, step, draw, update, the career image's save and restore, load-skater, is-loading |

## What is native and what is a seam

The window base, the menu toolkit (the main menu's list and rows, the load window's list, scaled title and rows), the font code, the skater manager (selection, costumes, the
custom skaters' load) and the career records run natively on the same memory. A virtual call on a child runs natively when the child is of a window-base class or one of
this screen's own (`PlayerSelect::dispatch`) and is reported as a service otherwise.

**Seams** (reported by name with the arguments retail passed): the big classes' constructors as the select window's initialiser calls them (`engine_004a3e80` view,
`0x004a6f50` load window, `0x004a5450` dots, `0x004a56b0` header, `0x004a1670` medal window, `0x004a3830` bio, `0x004a5890` menu; each is replayed on its own),
the seven prompt windows (`ScreenKit::prompt`), the pre-file manager (`0x004a9510`, `0x004a90f0`, `0x004a9330`), the spool's per-character functions of `player_spool.cpp`
(`0x004a63b0` loaded?, `0x004a6340` the face, `0x004a6440`, `0x004a6720`, `0x004a67c0`, `0x004a62b0`, `0x004a6880`, the spool manager's `0x004b52b0`, `0x004b5300`, `0x004b5350`),
the model and animation resources (`PSX_LoadAnimationResource`, the part manifest, `PSX_InitFreeSlot`, the region clears), the render calls (`M3D_BeginRenderView`,
`M3d_BuildTransform`, `M3D_RenderObjects`, `M3d_RenderCleanup`), the custom skater build and face bitmap (`0x00423be0`, `0x00418480`, `0x00418360`), the memory-card screen
(`0x0047a6c0`, `0x0047ac60`, `0x0047ae10`, `0x0047ace0`), `Pack_NewFree` (`0x004de150`), the palette set-up, the fades and the frame plumbing (`Game_FrameTick` ...).

## Findings

- **Seventy-two functions here (ninety-two with the window classes), not the plan's 37 + 17**: the plan's `playsel.cpp (37)` was a count of listed functions. The file has two ranges,
  `0x004a2bc0..0x004a62b0` and `0x004a6e60..0x004a78f0` (`player_spool.cpp` sits between them), and nine functions Ghidra did not list: seven vtable-only methods (`0x004a3110`,
  `0x004a3bd0`, `0x004a48e0`, `0x004a5690`, `0x004a5d80`, `0x004a7730`, `0x004a77e0`) and the screen's callback record and exit callback (`0x004a1fe0`, `0x004a2000`); the window classes
  add four more (see the other page). Curated names and `tony ghidra sync` created them (the inventory grew by 13).
- **A decompile drops the `ecx` of nearly every method here**: `FUN_004a35e0()` is `mov ecx,[ecx+0x4b8]; jmp 0x004a7590` (the load window's step on the window's `+0x4b8`), the select
  window calls `0x004a5880` on its header (`+0x4ac`, which must exist when a second player is choosing) and `0x004a6e90`/`0x004a61f0` work on the spool state embedded in the view at
  `+0xe0`. **Read the call sites in the objdump.**
- **Array slots in the initialiser**: its 32-word local array of pages (added to the window in order at the end) is filled by index: name text `[7]`, dots `[8]`, header or medal
  window `[9]`, bio `[10]`, menu `[11]` (the decompile names five locals; the stack slot differs by one when a `push` is pending).
- **Row layout**: the main menu's rows have ids 1 to 5 (Continue, Appearance, Buy stats, Buy tricks or Edit tricks, Skate shop; the career adds Buy stats and Buy tricks, a single-player screen outside
  the career adds Edit tricks, a two-player one neither); the load window's list holds a scaled title (id `-2`, at `+0xb8`), four rows with ids 0 to 3 (`+0xbc..+0xc8`) and the back row (id `-1`, at `+0xcc`).
- **`SkaterView_StepSelection` can loop for ever** when no character is available (the original does too); the replay's states always have one.
- **Real elements' per-frame hook** (slot 7 of the window `0x004dc9f0`, text `0x004dca60`, sprite `0x004dcb30`) is an engine function (the mouse-cursor hit test); the probe reports all three as the
  service `element_hook`, as the fake vtables' slot 7, so a real child updated inside a test replays.
- **The load window's constructor keeps both arrows' `sprite_init`, the main menu's does not**; the faces' sprite constructor takes `(file, 0x40, 0x40, 0)`, the arrows' `(file, 1, 0, 0)`.
- **`Mem_Copy` is real in the load group**: retail's `SkaterManager_LoadCustomIntoActive` and the window's image save and restore copy through it, so the group does not stub it and the career image
  (`0x00568a68`, `0x1dc4` bytes) is a captured region.
- **Bugs found while porting**: the first capture of the view's destructor crashed retail because the 4.10e1 seed filled the object slots with fake-vtable pointers (a spool slot pointing at the vtable
  table is "an object" whose vtable is code): the 4.10f seed zeroes them.

## Validation

`re/gdb/probes/frontend_calls.py` with the groups `sel`, `bio`, `view`, `load` and `selloop` (specs `frontend_specs_playsel_{sel,bio,view}.py`, the slab, seeds and stubs in
`frontend_specs_playsel.py`). Each function is called in the running game on a random legal state in the scratch slab: windows built by hand (fake pool objects whose virtual methods are
services, real text, sprite and menu-row elements where a width or a text is computed, a fake list, the skater view and the load window as slab objects), constructors run for real in the arena.
Fixtures `re/evidence/fixtures/plsel-calls-{sel,bio,view,load,selloop}.txt` (880 + 800 + 557 + 400 + 40 = 2,677 cases; three `SkaterView_LoadSkater` states made retail fault and were skipped) and the
window classes' `plsel-calls-uiw.txt` (953), 3,630 cases in all, replay with no mismatch through `player_select_retail_calls_test.cpp` (an adapter row per function); the pytest wrapper
`tests/test_player_select.py` also flips one recorded value and requires the replay to fail. `player_select_test.cpp` pins six behaviours (the counter's counting, the flag element's slide, the view's
skipping, the menu's answers, the bio's scroll limits, the prompt's width).
