# Equipment-select screen (equipsel.cpp)

Status: ported and replayed against retail forced calls (Phase 4.10f2)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slice: `equipment-select-screen`. Native code: `src/runtime/equip_select_{window,panels,racks}.cpp` (methods of `EquipSelect`, `equip_select.hpp`, which derives from
`ScreenKit`).

Read first: [player-select-screen.md](player-select-screen.md) (the model: a screen class on `ScreenKit`, native virtual dispatch, constructors as seams),
[create-select-screen.md](create-select-screen.md), [menu-toolkit.md](menu-toolkit.md) and [frontend-window-base.md](frontend-window-base.md).

## What the screen does

`EquipSelect_Run` (`0x004445b0`) is the skate shop's **decks and trucks** screen. `mode` 1 is the career, where a deck costs the character's cash; the other modes only choose.
It loads three fonts (`s2font2`, `s2font1`, `s2menu1`), builds the window (`0x3f4` bytes, vtable `0x00518914`) and runs frames until the window's step answers: the back edge
of the player's pad (with no dialog open) ends it with `-1`, and a chosen deck sets the window's done flag (the answer stays `1`). The window has two pages (`+0xac`): 0 the decks
and 1 the trucks.

| Part | Class | What it shows |
|---|---|---|
| `EquipView` (`0x50` bytes) | holds two racks and four arrows | the camera and fog; the page's rack takes the pad's moves |
| `DeckRack` (`0x6e0`) | the five decks round the front, set after set | up and down move between sets (one set per character sharing the character's deck logo; a custom skater has its own set of 20 and one for each of the first 13 characters); left and right turn the rack one slot. Each deck is a `DeckItem` whose picture is spooled in a few at a time |
| `TruckRack` (`0x2f8`) | the truck model | left and right change the style (three), with a spin, rise, turn and sink state machine |
| `NameWindow` (`0x21c`) | the deck's name, sliding out and in | the name, `unlock with <character>` (another character's deck) or `unlock in career mode` |
| `CashWindow` (`0x36c`) | `cash` and `cost` counters | the character's cash and the highlighted deck's price (`---` for another character's) |
| `StatDots` (`0x48`) | four rows of dots | the stat of the last owned deck; for a deck still to buy, the dots it would add |

A deck is named by a **set** (a character index: each character has a set of eight decks) and an **index** in the set; the current choice is in the globals `0x561cc4`
(set), `0x561cc8` (index) and `0x561ccc` (truck), the character who buys in `0x561c68`, the player's pad in `0x561c7c`. Ownership is a bit of the career record's `+0x34`
(`career::kRecClassMask`), the cash is its `+8`. **The price** is 250 for every deck the character owns (`5 x owned x 50`); a custom skater (row flag 2) gets the first eight free and pays
`(5 x owned - 45) x 50`; a deck it owns costs nothing.

`EquipWindow_Update` (virtual slot 1) in order: the dialog's answer (`ConfirmBox_Poll`: 1 pays and buys, anything else refuses), the four directions (left, right, up, down:
`EquipView_Move` 2, 0, 1, 3), the page switch (the toggle edge, or confirm on the trucks page), and on the decks page a confirm or the alternate edge: an owned deck is equipped
(the player's and the character's saved set, index and the screen is done), an unowned deck of the character's own with the cash opens `Spend $%d on deck?`, anything else refuses
(sound `0x92`); then the name window and the logo of the highlighted set slide.

## Functions (83)

| Group | Addresses | Functions |
|---|---|---|
| callbacks, loop, helpers | `0x00444510`..`0x00444970`, `0x00445810`, `0x004458b0`, `0x004468a0` | `Equip_ColoursMatch`, `EquipSelect_NoOp`, `_InitCallbacks`, `_ExitCallback`, `_ScreenCall`, `_Run`, `Equip_DeckPrice`, `_BuyDeck`, `_OwnsDeck`, `_IsCurrentCharacter`, `_CanAfford`, `_AllocZeroed`, `_Free`, `_DeckStat`, `_CountOwnedDecks`, `_CharacterShortName` |
| the window | `0x00444980`..`0x00445550` | construct (1.5 KB), scalar delete, destroy, draw, update (1 KB), slot 4, step |
| stat dots, cash window, name window | `0x004455c0`..`0x00445780`, `0x00445920`..`0x00445e70`, `0x00446490`..`0x004466d0` | construct, delete, destroy, draw / update |
| the view | `0x00445ea0`..`0x00446180`, `0x00446420`..`0x00446470` | construct, delete, destroy, update, draw (the camera rectangle), move, set page, clear flag |
| the racks | `0x004468e0`..`0x00446990`, `0x004469f0`..`0x00447730`, `0x00447a90`, `0x00448230`..`0x00448790` | the base (slide), the deck rack (construct, destroy, build sets, update, draw, move, turn, scroll), the truck rack |
| a deck's picture, the spool | `0x00447ae0`..`0x00447f40`, `0x00448110`, `0x004481c0` | the item (construct, data, free, wanted, update, priority) and the spool queue (construct, reset, clear, insert, begin, start next, poll, verify) |

## What is native and what is a seam

The window base, the menu toolkit (the stat dots are `MenuDots`), the font code, the skater manager and the career records run natively on the same memory. A virtual call on a
child runs natively when the child is of a window-base class or one of this screen's own (`EquipSelect::dispatch`) and is reported as a service otherwise.

**Seams** (reported by name with the arguments retail passed): the constructors of the screen's classes as another class's initialiser calls them (the window's calls for the dots
`0x004455c0`, the name window `0x00446490`, the cash window `0x00445920` and the view `0x00445ea0`; the view's for the racks `0x004469f0` and `0x00448230`; the run loop's for the window
`0x00444980`; each is replayed on its own), the prompt windows (`ScreenKit::prompt`), `ScoreCounter_Construct` / `_SetValue` and `TextFlagElement_Construct` of `player_ui.cpp`, the confirm
box's constructor and poll, `Player_GetTextureSetEntry` (`0x004547f0`: a pointer to the deck's entry; the stats, the file stem and the name are read natively from it), the file
calls the spool makes (`0x00449030`, `0x00449230`, `0x00449660`, `0x004494b0`), the model and render calls (`M3D_BeginRenderView`, `M3D_RenderObjects`, `M3d_RenderCleanup`, the camera, fog and
rectangle fills), the spool regions (`PSX_QueueResource`, `PSX_DrainSpooler`, `PSX_ClearRegion`), the vector constructor and destructor of the object records (`0x00500400`, `0x0050030c`),
the texture swap (`0x00457e10`) and the frame plumbing (`Game_FrameTick` ...).

## Findings

- **Eighty-three functions, not the plan's 68**: 68 were Ghidra-listed (the `18 + 50` of the two ranges); fifteen were not: the exit-callback-like empty function `0x00444550` and the
  callback record's `0x00444560`, `0x00444580`, the window's update `0x004450e0` and slot 4 `0x00445540`, the stat dots' draw and update `0x00445740`, `0x00445780`, the view's update and draw
  `0x00446150`, `0x00446180`, the rack base's two empty slots `0x00446900`, `0x00446910`, the deck rack's update `0x00446fb0`, the truck rack's slot 3 `0x00448790`, the spool's check
  `0x004481c0` and the thunk `0x00447b70` Ghidra lists but nothing refers to. `0x00445850` looked like a function start (a 16-byte-aligned address after a `ret`) but is a case label of the stat
  switch `0x00445810`; `0x00448830` (a `Spool_FindEnviroItem` wrapper) is `exp.cpp`'s and was left out. Names and `tony ghidra sync` created the unlisted functions.
- **A decompile drops the `ecx` of nearly every method and one stack argument of several callers**: `0x0046f550(0, 0, 6)` (pad edges) is followed by `add esp, 0xc` after four pushes, so the
  fourth push (the player) becomes the second argument of `ConfirmBox_Poll(box, edge, player)` (`ret 8`); `FUN_00447ae0`'s `held` argument is a leftover push of the caller (the same
  trap as 4.10f's `Skater_CustomSlotIndex`).
- **Retail's assertions write the source file and line into globals** (`0x56881c`, `0x568638`) before each `M3dUtils_InBetween`; only a failing one is reported, so the native code passes
  the message only. The deck rack asserts the slot, the deck value and the set's deck count; the spool asserts its state and its file calls (`file %s not found`).
- **A string retail builds in a local and hands to a service** (the logo's file name `s2logo%s.bmp`, the dialog's text, the rectangle of `0x004e8730`) is recorded with the address of the
  caller's stack frame, which changes with every call. `fcall.py` gained `stack_to=`: such an argument is recorded as the fixed scratch address the native code builds the same string at
  (`ScreenKit`'s `kScratchLine`), while the recorded text still comes from the real pointer.
- **The fake racks and dialog need their own vtables**: a rack's slot 3 takes one argument and the confirm box's slot 2 none, but the shared `VT_ANY` has the sprite draw (three arguments) and the
  child draw (two) there, and a stub that pops the wrong number of bytes corrupts the next return (the lesson of 4.10c). The probe adds `VT_RACK` and `VT_DIALOG` with their own stubs
  (`rack_v3`, `dialog_v1`, `dialog_v2`).
- **The deck table is `0x53a3b8` rows and the entries `Player_GetTextureSetEntry` returns**: a row's `+0x44` points at the file stem of the set's first deck (`s2dBi01a.bmp`: the characters at
  `+3` and `+4` name the deck brand; characters with the same brand share one rack), its flags `+0x15c` say custom (2) and no logo (`0x22`). The real stems were read, not guessed.
- `DeckRack_Update`'s slot search compares the shown angle with the five slots' angles (`0x52cb58`, a pair of the angle and the texture-set id of that slot's picture) half a slot either side,
  wrapping the last slot's range by a full turn; the rack scrolls by one deck when the slot at the front changes, and by one set when the height crosses a 2000 band.
- `0x00446180` scales the camera rectangle from the 512 x 240 layout with `x * width >> 9` and `y * height / 240` (the divide by `imul 0x88888889`, `shr 7`).

## Validation

Forced calls in the running game (`frontend_calls.py` groups `eqh`, `eqwin`, `eqpan`, `eqview`, `eqrack`, `eqloop`; slab, seeds and stubs in `frontend_specs_eqsel.py`, generators in
`frontend_specs_eqsel_cases.py`), replayed by `equip_select_retail_calls_test.cpp` over the fixtures `re/evidence/fixtures/eqsel-calls-*.txt`: see the slice manifest for the counts. The
fixtures were captured with the same seed per group; a recapture of one function replaces its cases only (`P410_ONLY`, same seed).

Not generated because retail faults or reads stack garbage: a null child where the code dereferences it after its assertion (a null sprite in a rack's marker, a null item in a set's
list), a deck index above the set's item list, a spool node whose item is null. States are generated, not played (no natural walk of the equipment screen: 4.10i); `tony hal video shadow`
would check what the frames look like.
