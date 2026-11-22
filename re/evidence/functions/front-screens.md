# The in-level front end: `front.cpp` (Phase 4.10i)

Source: `front.cpp` (`0x0044b180..0x00452e00`). Native: `src/runtime/front.hpp` and `front_{menu,display,screens,states,update,update_late,update_screens,load}.cpp`
(`FrontScreens`, derived from `ScreenKit`). 60 functions in five slices (`front-menu-class` 19, `front-screens` 18, `front-status-changes` 12, `front-update` 1,
`front-level-loader` 10).

## What the file is

`front.cpp` is the front end *inside a level*: the pause menu, the end-of-run menus, the statistics and high-score screens, the replay and save questions, the competition
screens between heats, the unlock announcements and the level loader that brings a level up. Everything is driven by one global, the **status** (`GStatus`, `0x00561bfc`, a
number from 0 to 0x37; `statusNames` at `0x0052be50`): `Front_Update` (`0x0044eac0`, 13,872 bytes) polls the pads and runs the status's logic, `Front_Display`
(`0x0044ca60`, 4,737 bytes) draws it, and the `Front_SwitchOn...` functions change it (`Front_SetStatus`, `0x0044b1f0`, logs the change and flags it new). Both big functions are
jump tables over the status (a byte table maps the status to a handler, a dword table holds the handlers; Ghidra merges the handlers into one function each).

The menus are the **`CMenu` class** (vtable `0x00518a00`, base class `0x005184b4`), an older sibling of the menu toolkit: fifteen fixed entries of 0x18 bytes laid over the object, the name of
entry *e* at `+0x18*(e+1)`, its scales and flags at `+0x18*e + 0x1c..0x2d` (the layout is in `front.hpp`). Menus are created by `Front_Init` (nine of them, kept in the pointer
variables `0x00561c1c..0x00561c3c`) and the pause menu / restart menu by `Front_Update` (`0x00561c40`, `0x00561c44`).

## The status numbers

| Status | Screen | Status | Screen |
|---|---|---|---|
| 0 | playing: a pause press builds the pause menu | 0x18 / 0x19 | next player / next heat menu |
| 1 | the pause menu | 0x1a | replay menu of a competition |
| 2 | the cheat restart list ("skip to restart") | 0x24 | the competition's win/lose logic |
| 3 / 5 / 6 / 7 / 0xc / 0x11 | yes/no questions (quit, save game, save career, ...) | 0x2a | the horse game's score-setting screen |
| 4 | the controller was pulled | 0x2b | the horse game's word entry |
| 8, 9, 10 | waiting for an engine function to finish | 0x2e | the high-score entry |
| 0xb | the level goals screen | 0x2f | the sound levels menu |
| 0xd | the statistics (or the horse game's results) | 0x31, 0x36, 0x37 | "you opened a new level" announcements |
| 0xe | the end-of-run menu | 0x32 | "remember you can spend your earnings" |
| 0xf, 0x10 | the replay menus | 0x33 | the versus game's time select |
| 0x12 | waiting for a save | 0x34, 0x35 | a cheat or character was unlocked |
| 0x13 | the yes/no question put up for the replay level | 0x14, 0x16, 0x17 | the competition's heat screens |

## Findings that are not in the code

* **`Mess_SetRGB` and `Mess_SetTextJustify` keep only the low byte of their arguments** (`and eax,0xff` first). Callers build the colours with 8-bit arithmetic in registers whose upper
  bits are stack addresses or quotients (`CMenu__Display`'s pulsing colour, `Front_PrintGoBack`'s winner colour, the goals screen's), so the probe's stubs mask those arguments; the port
  passes the low bytes.
* **`CaseInsensitiveBoundedNameEqual` folds on signed bytes** (`cmp al,0x41; jl` / `cmp al,0x5a; jg`): bytes above 0x7f do not fold. The menus find entries by comparing the *contents* of the
  names, so the sound levels menu (whose entries are three static buffers rewritten every frame) is found by the buffer's current text.
* **A menu with no shown entry never finishes `CMenu__Update`**: its first loop skips hidden entries and never ends when all are hidden (the probe's generators keep one shown; the
  restart menu hangs the game if every cheat restart is a "de_" one).
* **`Front_CheckUnlocks` calls `Career_HighestOpenLevel` up to five times** in one condition and the `Career_GameCleared` of each character up to twice; the port makes the same calls in the same order
  (each is a reported service with its own scripted answer).
* **`Front_LoadGame`'s custom-skater loop runs twice** (custom skaters first, then the stock ones) so that the custom definitions are built before the stock models are loaded; the
  `SkaterSelectionRecords` flags are at `0x0053a514 + character * 0x170` (bit 0: the "steamer" sounds, bit 1: a custom skater, bit 7: not a regular character).
* **`PSX_ShareAnimationRegion` with region -1** asserts and then writes four words below the region tables (`0x0056d3f8..`): the probe's region covers them.
* **The goals screen (status 0xb) counts a reward up** while the clock runs: the running total changes by the difference between the interpolated amount and the full reward (the asm
  adds `x - reward` where the decompile shows `amount - reward`; the replay is what settles it).
* **`FUN_004dc020(1)` / `(0)` and `FUN_004dc0c0`** are the draw helpers of the end screens (a darkening layer and the pad hint); they are seams here (4.10i3 owns them).

## Seams

The Mess text calls (`0x00473650`, `0x004734f0`, `0x004734b0`, `0x00473510`, `0x00473580`, `0x004739e0`, `0x004df030`), sounds, pad and shake calls (`0x00489670`, `0x00489850`,
`0x00489a10`), the career (`Career_Got`, `Career_JustGot`, `Career_HighestOpenLevel`, `Career_GameCleared`, `Career_GotAllGaps`, `Career_UnlockCharacter`, `Career_NewCheat`,
`Career_CountUnlockedCheats`, `Career_CheatName`, `Career_UnlockCheat`), the competition (`Competition_AiScore`, `_NextHuman`, `_ResetMarquee`, `_EndGameLogic`, `_WinLoseLogic` and its displays,
`Competition_InitIfActive`), the replay and save-card layer (`Replay_PrepareHighlights`, `Replay_ClearPlaybackState`, `Replay_SaveCardBuffers`, `Mmu_*`), the name entry (`NameEntry_*`), the
falling-text effect (`FallingTextEffect_*`, `TextEffect_Unbind`), the PSX spooler and animation loaders, the script loader (`TRG_*`, `Rail_CreateRails`), the sound banks (`Audio_Open*Vab`) and the
panel bitmaps' constructors. Same-file functions the port calls natively: the CMenu methods, `Front_SetStatus`, `RestartCompFrig`, `Horse_DisplayWords`, `Front_PrintGoBack`, the button hints,
`Front_PollButtons`, `Front_SwitchOnEndHeat`, `Front_ResetScoreSlide`, `Front_SelectPlayReplay`, `Front_HorseMessage`, `Front_CheckUnlocks`, `Front_WaitForButton`, `Front_QuitCombo`. The
functions the update calls through a seam because their own replays cover them: `Front_SwitchOnEndLevel` (`0x0044e0f0`), `Front_SwitchOnVideoScreen` (`0x0044e9c0`),
`Front_CheckUnlocks` (`0x00452a80`) and `Front_UnlockContinue` (`0x00452d00`).

## Probe

`re/gdb/probes/frontend_specs_front.py` (groups `fmenu fscr fdisp fst fupd fld`, regions, seeds, stubs; the stubs of functions under test are left out through the `P410_GROUPS` selection),
`frontend_specs_front_cases.py` (the CMenu class and the small screens), `frontend_specs_front_cases2.py` (`Front_Display` and the state helpers), `frontend_specs_front_upd.py`
(`Front_Update`), `frontend_specs_front_ld.py` (the level loader). The menus are real `CMenu` objects built in the slab by `menu_image` (the layout the constructor and `AddEntry`
leave); the second player of the two-player cases is a fake player object past the slab's arena; the name entry and the PAUSED bitmap are fake objects whose virtual methods are
services; the arena starts zeroed (retail's class new zeroes its blocks) except in `fld`, where every block starts with a fake delete vtable.

## Results

All 60 functions replay with no mismatch: **10,245 forced-call cases** (return value, every written byte and the ordered service and virtual calls compared), fixtures
`front-calls-{menu,screens,display,states,update,update2,load,load2}.txt` (1,520 + 750 + 1,800 + 1,200 + 1,875 + 300 + 800 + 2,000 cases).

| Slice | Functions | Cases |
|---|---:|---:|
| `front-menu-class` | 19 | 1,520 |
| `front-screens` (small screens, `Front_Init`, `Front_Display`) | 18 | 2,550 |
| `front-status-changes` | 12 | 1,200 |
| `front-update` (`Front_Update`) | 1 | 2,175 |
| `front-level-loader` | 10 | 2,800 |

Line coverage of the native code by the fixtures (gcov, `-O0`): `front_menu` 188/190, `front_display` 379/393, `front_screens` 215/216, `front_states` 256/256, `front_update` 316/320,
`front_update_late` 565/579, `front_update_screens` 241/244, `front_load` 227/241. What the replays do not reach: the null-pointer branches of the name comparison (no menu has a null name), the horse game's "scored more than the target"
branch and the word-complete announcement ("an A" / "a B": the generated horse states never finish a word), the high-score name editor's letter wrap-around (a letter stepping past A or Z
falls to a blank), a few competition-screen menu choices that the generator's menus never put on the current line, and the custom-skater "all gold" and "all perfect" unlock checks of
`Front_CheckUnlocks` (they need a career where every custom skater is perfect). Those lines are ported from the objdump and covered only by reading; the unit tests (`front_test.cpp`) cover
the CMenu arithmetic, the status names and the button polling.

The fixtures were captured twice: the first pass (smoke runs of three cases per group, then full runs) found the probe problems listed in PHASE_4_SUMMARY.md; the second pass used generators steered
by the gcov report of the first (the second player, the character-flag windows for the custom-skater loop, the competition and horse states). `Front_Update` was thinned to three cases in
four to keep the fixture below 24 MB (the dropped quarter added four lines of coverage).
