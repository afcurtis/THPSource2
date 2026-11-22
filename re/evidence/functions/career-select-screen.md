# Career select screen (carsel.cpp)

Status: ported and replayed against retail forced calls (Phase 4.10f3)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slice: `career-select-screen`. Native code: `src/runtime/career_select.{hpp,cpp}` (methods of `CareerSelect`, which derives from `ScreenKit`).

Read first: [create-select-screen.md](create-select-screen.md) (the model: a screen built on the window base, the menu toolkit and `ScreenKit`), [menu-toolkit.md](menu-toolkit.md) and
[frontend-window-base.md](frontend-window-base.md).

## What the screen does

`CareerSelect_Run` (`0x00417f90`) is frontend screen 10 (`CAREER_SELECT`), the screen the first entry of the main menu's wheel opens (the 4.10a harness reaches it with
Enter on the title). It loads `carsel.pre` and three fonts (`mainf`, `s2font2`, `s2tricks`), builds the window (`0x4dc` bytes, vtable `0x00518508`), unloads the pre file
again and runs the shared frame loop (`ScreenKit::frame_loop`: tick, three engine calls, `Front_UpdateMenuSound`, flip, pad update, then the poll) until the poll answers 1
or 2 (or the engine asks to leave). It answers `0` (a choice was made) or `-1` (back).

The window holds a menu list of three text rows built from the `mainf` font: **Continue career** (id 3), **New career** (id 0) and **Load career** (id 1), with two arrow
sprites and three prompt windows (select, accept, back). While the byte at `0x0052cb3d` is set, the Continue row's byte at `+0xc` is cleared.

| Window field | Meaning |
|---|---|
| `+0xac` | the background sprite (`s2bgd4.bmp`) |
| `+0xb0`, `+0x1fc` | two Font objects (`mainf`, `s2font2`) |
| `+0x348`, `+0x34c` | the list and its style (a MenuProperties copied from `+0xb0`) |
| `+0x350` | 0x60 row slots (three used) |
| `+0x4d0`, `+0x4d4` | the arrow sprites (`s2arrow2.bmp`, `s2arrow1.bmp`) |
| `+0x4d8` | the state: -1 choosing, 1 loading from the card, 2 saving to it |

`CareerSelect_Poll` (`0x00417e30`) is the whole of the screen's input. In the card states it hands over to `UpdateLoad` (state 1: the card layer's answer 1 ends the screen, 2
cancels) or `UpdateSave` (state 2: 1 or 2 end the step, answer 0); any other state comes back unchanged (the retail jump falls out with the state in `eax`). While choosing:
a move of either up or down edge on pad 0 plays sound `0x90`; **confirm on pad 0** (only pad 0) plays `0x91` and acts on the highlighted row's id through a jump table:

| id | choice | what happens | answer |
|---|---|---|---|
| 0 | New career | clears the flag at `0x0052cb3d`, `Career_ResetSaveImage` | 1 |
| 1 | Load career | `Mmu_SetupCareerLoadBuffer(1, 1, 0)`, state 1 | 0 |
| 2 | (save) | `Mmu_SetupCareerSaveBuffer(0x12, 0x12, 1)`, state 2: no row has this id | 0 |
| 3 | Continue career | nothing | 1 |
| other | | the assertion "Unrecognized menu option" | 0 |

Back (pad 0) plays `0x93`, clears the edge and answers 2.

`CareerSelect_Draw` (`0x00417bf0`) lets the memory-card screens (`0x0047ace0`) draw instead of the window while a card step runs (state 1 or 2), then draws the
background. `CareerSelect_Update` (`0x00417c90`) moves the list (list slots 3, the pad edge, 5, 4) and updates the window, in the choosing state only.

## Two helpers every screen's loop calls

`Front_UpdateMenuSound` (`0x00418150`) sits in this range though it is the front end's: while a voice is playing (`0x0052cb38` is not -1) its pan (`0x00561c84`) moves toward
the target (`0x00561c94`) by `0x88` a frame, clamped; when it has arrived at a target of zero the voice is stopped (`0x004ad9f0`); the pan, scaled by the word at `0x0052be48`
and divided by 510 with the compiler's multiply-by-reciprocal, goes to `Audio_UpdatePositionalSound` (`0x004ad950`, twice as left and right) with `0x0052be44` set to
`0x3fff` for the call. `Front_KeyTable` (`0x00418140`) returns the address `0x00525500`, the 400-byte XOR key the memory-card scrambler (`0x004bae70`: `data[i] ^= key[i % 400]`) applies to saved data.

## Functions (14)

| Address | Name | Replayed |
|---|---|---|
| `0x004173d0` | `CareerSelect_InitCallbacks` | yes |
| `0x004173f0` | `CareerSelect_ExitCallback` | yes |
| `0x00417400` | `CareerSelect_ScreenCall` | yes |
| `0x00417410` | `CareerSelect_Construct` | yes |
| `0x00417aa0` | `CareerSelect_ScalarDelete` | yes |
| `0x00417ac0` | `CareerSelect_Destroy` | yes |
| `0x00417bf0` | `CareerSelect_Draw` | yes |
| `0x00417c90` | `CareerSelect_Update` | yes |
| `0x00417ce0` | `CareerSelect_UpdateSave` | yes |
| `0x00417d70` | `CareerSelect_UpdateLoad` | yes |
| `0x00417e30` | `CareerSelect_Poll` | yes |
| `0x00417f90` | `CareerSelect_Run` | yes |
| `0x00418140` | `Front_KeyTable` | yes |
| `0x00418150` | `Front_UpdateMenuSound` | yes |

The two jump-table targets `0x00417ef0` and `0x00417f50` are labels inside `CareerSelect_Poll` that nothing else refers to (not functions).

## Validation

Forced calls in the running game (`re/gdb/probes/frontend_calls.py` with the groups `carwin` and `carloop` of `frontend_specs_carsel.py`; method in
[frontend-text-layer.md](frontend-text-layer.md#validation-method)): the window is built by hand in the scratch slab with real text rows, a fake list and fake pool objects
whose virtual methods are reported services; the constructor and the loop run for real in the arena. The loop's window methods (poll, update, draw, delete) are seams.
Fixtures `re/evidence/fixtures/carsel-calls-{carwin,carloop}.txt`.

What the smoke captures found (all fixed before the full runs): the first font's red byte is `0x40`, not `0x80`; the loop constructs the window natively (only the four
per-frame methods are seams); the shared sound stub had to be switched off for the function under test.
