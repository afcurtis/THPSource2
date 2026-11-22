# Skater select / load / save screen (cresel.cpp)

Status: ported and replayed against retail forced calls (Phase 4.10e2)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slice: `create-select-screen` (12 functions). Native code: `src/runtime/create_screens_select.cpp` (methods of `CreateScreens`, `create_screens.hpp`); the
helpers every screen shares are in `src/runtime/screen_kit.{hpp,cpp}` (moved there from `create_screens_stat.cpp` in 4.10e2).

Read first: [create-editor-ui.md](create-editor-ui.md) (the other create-a-skater screens, the slab and the stubs this screen's capture reuses),
[menu-toolkit.md](menu-toolkit.md) and [frontend-window-base.md](frontend-window-base.md).

## What the screen does

The select screen is the career menu's list of the four custom skater slots (`0x0042f100`, `CreateSelect_Run`). It is a window (`0x7e4` bytes, vtable
`0x00518828`) holding two lists: the slots ("Custom Skater Roster": three rows and "back") and the skaters of the memory card to load from ("Choose a skater
to load": three rows and "back"). Its state machine (`CreateSelect_Update`, `0x0042e9c0`; the window's state is at `+0x7b0`, the state the next frame enters at
`+0x7b4`) has seven states:

| State | Meaning | Pad |
|---|---|---|
| 0 | the slots list | confirm picks the slot (the skater manager's custom slot is set, answer 1) or the back row (answer 2); square on an occupied slot asks "Do you want to delete ...?" (state 4); triangle loads into the slot: an empty slot goes to the card at once (state 2), an occupied one asks "Are you sure?" first (state 1); back answers 2 |
| 1 | the overwrite question (a `ConfirmBox`) | yes: the card load (state 2); no: back to state 0 |
| 2 | load from the card | the card screen (`0x0047ac60`) answers 1 (a skater was chosen: state 3) or 2 (back to state 0) |
| 3 | the skaters list | confirm copies the chosen skater into the slot (`CustomSave_CopySlot`) and shows "has been loaded" (state 5), or goes back to the card (the back row); back commits the image and returns to the card |
| 4 | the delete question | yes: `CustomSkater_Init` clears the slot (a fresh custom skater built with a new trick manager), "Delete successful" (state 5); no: state 0 |
| 5 | a message box | confirm: save to the card (`Mmu_SetupCareerSaveBuffer`, state 6) |
| 6 | save to the card | the card screen answers 1 (state 0) or 2 (a message, state 5) |

`CreateSelect_UpdateFrame` (virtual slot 1, `0x0042e180`) rebuilds the rows' texts ("Skater 2: Name", "Skater 3: <--EMPTY-->") with `sprintf`, ellipsizes
them to the screen, moves the lists with the pad and answers the dialog; `CreateSelect_Draw` (slot 2, `0x0042df60`) shows the warning lines and the prompt windows
the state needs.

## Functions (12)

| Address | Name | |
|---|---|---|
| `0x0042cd20` | `CreateSelect_InitCallbacks` | the screen record: exit and run callbacks (two stores), the same shape as `0x0042be40` and `0x004b5ba0` |
| `0x0042cd40` | `CreateSelect_ExitCallback` | a bare return |
| `0x0042cd50` | `CreateSelect_ScreenCall` | `*result = CreateSelect_Run()` |
| `0x0042cd60` | `CreateSelect_Construct` | 4.1 KB: the working copy of the save image, the background, three fonts, three styles, the arrows, both lists and their rows, four warning lines, five prompt windows |
| `0x0042dda0`, `0x0042ddc0` | `CreateSelect_ScalarDelete`, `_Destroy` | |
| `0x0042df60` | `CreateSelect_Draw` | vtable-only (Ghidra listed no function) |
| `0x0042e180` | `CreateSelect_UpdateFrame` | vtable-only |
| `0x0042e7d0`, `0x0042e860` | `CreateSelect_UpdateLoad`, `_UpdateSave` | the card states |
| `0x0042e9c0` | `CreateSelect_Update` | the pad |
| `0x0042f100` | `CreateSelect_Run` | the loop |

`CustomSave_CommitImage` (`0x0042ef30`) and `CustomSave_CopySlot` (`0x0042ef50`) sit in the middle of the file; they are native since 4.9 (`create_records.cpp`,
`create-custom-records`) and this screen reports its calls to them as services.

## What is native and what is a seam

The window base, the menu toolkit (the lists, rows and styles), the font code, the skater manager (`SetCustomSlot`, `SelectCharacterChecked`) and the
confirm box of 4.10e1 run natively on the same memory. **The memory-card layer is a seam**, as 4.8, 4.9 and 4.10e1 treated the other services: `Mmu_SetupCareerLoadBuffer`
(`0x0047a6c0`), `Mmu_SetupCareerSaveBuffer` (`0x0047a0b0`), the card screen's poll (`0x0047ac60`), its finish (`0x0047ae10`) and its draw (`0x0047ace0`) are reported
with the arguments retail passed and a scripted answer. The calls into `CustomSkater_Init`, `Tricks_InitializeManager`, `CustomSave_*`, the screen's `Mem_Copy` of the
save image and the frame plumbing (`Game_FrameTick`, `Db_*`, `Pad_ClearTriggers`, `PollHost`) are services too.

A dialog is the confirm box of 4.10e1: its virtual draw, update and delete run natively when the object is a confirm box and are reported otherwise
(`CreateScreens::dialog_call`).

## Findings

- **Ghidra listed six of the twelve**: the callback record and its exit callback (`0x0042cd20`, `0x0042cd40`) and the two vtable slots (`0x0042df60`, `0x0042e180`)
  had no function, and the plan's start list was wrong about the file's end. `0x0042f2f0..0x0042f880` is **crowd.cpp** (the stadium crowd's sounds: `Crowd_Update`, `Crowd_Boo`,
  five reaction functions and an initialiser at `0x0042f2e0`), not cresel's; `0x0042f880..0x0042f950` is the custom definition holder `0x0042f880` (constructor, `0x0042f8b0`
  deleting destructor, `0x0042f8d0` destructor) that `custmgr.cpp` and the editors use. Both are left as planned work (`re/slices/crowd-audio.yml`).
- **A decompile hides a screen's second list**: `CreateSelect_Construct` builds two lists with the same shape (a scaled title, three rows with ids 0 to 2 and the back row,
  id `-3`); only the title text and the array they go in (`+0x498`, `+0x61c`) differ.
- **Statements the decompile drops**: every `sprintf` and `ConfirmBox_Construct` argument list (the format is a text-table pointer, not a literal), the pad index of
  `0x0046f550`, the asserts' message arguments, and the `Mem_Copy` arguments' order (`dest, source, size`).
- **The ConfirmBox positions**: the delete question is centred (`fill height / 2`), the "Are you sure?" one `0x14` lower.

## Validation

`re/gdb/probes/frontend_calls.py` with the groups `crsel` and `crselloop` (specs in `frontend_specs_select.py`, on the 4.10e1 slab: `frontend_specs_screens.py`). Each function is
called in the running game on a random legal state in the scratch slab: a window built by hand (real text rows for the lists' rows, so the toolkit's own method sets
their text; fake lists, prompts and children whose methods are services; a real confirm box where a state needs a dialog), constructors run for real in the arena, the loop
with its four per-frame calls as seams. Fixtures: `re/evidence/fixtures/crscreen-calls-crsel.txt` (620 cases), `crscreen-calls-crselloop.txt` (40); all replay with no mismatch; the replay is
`create_screens_retail_calls_test.cpp` (the 4.10e1 binary, with an adapter row per function).

The first capture of `CreateSelect_UpdateFrame` crashed retail ten times: a fake style object's font was garbage, and the update ellipsizes the rows' text with it.
The generator now uses real `MenuProperties` for the three styles (the same objects the rows use, as in the real screen).
