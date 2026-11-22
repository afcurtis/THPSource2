# Create-a-skater screens (create_ui.cpp, creedit.cpp, cresave.cpp, statedit.cpp and the stat window's constructor)

Status: ported and replayed against retail forced calls (Phase 4.10e1)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slice: `create-editor-ui` (55 functions). The memory-card selection screen `cresel.cpp` and the trick list (`trickwin.cpp`, `tricksel.cpp`) are
Phase 4.10e2: slices `create-select-screen` and `trick-menu-runtime`.

Read first: [create-skater-runtime.md](create-skater-runtime.md) (the records and palettes these windows edit), [skater-windows.md](skater-windows.md)
(the three pages the editor holds), [menu-toolkit.md](menu-toolkit.md) and [frontend-window-base.md](frontend-window-base.md).

## What the screens do

The custom skater editor (`creedit.cpp`) is a window of four pages (0 personal information, 1 appearance, 2 stats, 3 tricks) around a spinning skater.
`CreateEditor_Run` (`0x00428420`) makes the active player's character the custom skater, loads `create.pre` and six fonts, builds the editor and runs
frames until `CreateEditor_Update` answers 1 (finished) or 2 (back out), then unloads and, unless the caller passed the mode flag, initialises the
skater (spare points, trick configuration, `Career_SaveCustomAppearance`). `CreateEditor_Update` polls the current page; 1 goes to the next page and the
last page's 1 finishes, 2 goes back and the first page's 2 leaves. `CreateEditor_UpdateFrame` (the window's per-frame method, vtable slot 1, found
by the starts scan: Ghidra listed no function there) spins the skater with the pad's two spin buttons (not on the trick page, not while a name is typed),
follows the texture spool, rebuilds the name text and shows the current page's window and buttons.

The save-skater screen (`cresave.cpp`, `CreateSave_Run` `0x0042cb80`) offers "save the skater into the career file" (the memory-card save, state 2)
and "continue without saving" (a warning box, state 1: yes leaves, no returns to the list). The stat editor (`statedit.cpp`, `StatEdit_Run`
`0x004b8090`) holds the stat window (`statwin.cpp`, whose constructor and destructor 4.10d did not reach) over a spinning skater, either a character
from its model files or a custom skater from `CustomSkater_Create`. `create_ui.cpp` is the help text box (`TextBox`) and the confirm box
(`ConfirmBox`: a text box with a two-item list); `Text_Layout` (4.9) lays the text out.

## Functions (55)

| File | Functions |
|---|---|
| `create_ui.cpp` (11) | `TextBox_Construct`, `_ScalarDelete`, `_Destroy`, `_Draw`; `ConfirmBox_Construct`, `_ScalarDelete`, `_Destroy`, `_Highlight`, `_Draw`, `_Update`, `_Poll` |
| `creedit.cpp` (17) | `CreateEditor_ScreenCall`, `_Construct`, `_ScalarDelete`, `_Destroy`, `_SetupScene`, `_LoadFonts`, `_SetPage`, `_UpdateFrame`, `_ChangeBuild`, `_Draw`, `_Update`, `_ShowButtons`, `_Run`; the personal page's `Construct`, `Delete`, `BuildList`, `Destroy` |
| `cresave.cpp` (13) | `CreateSave_InitCallbacks`, `_ExitCallback`, `_ScreenCall`, `_Construct`, `_ScalarDelete`, `_Destroy`, `_Draw`, `_Update`, `_Step`, `_DrawList`, `_DrawBox`, `_MmuScreenThunk`, `_Run` |
| `statedit.cpp` and the stat window (13) | `StatEdit_ScreenCall`, `_Construct`, `_ScalarDelete`, `_Destroy`, `_LoadFonts`, `_SetupScene`, `_Draw`, `_UpdateSkater`, `_Step`, `_Run`; `StatPage_Construct`, `_ScalarDelete`, `_Destroy` |
| `appwin.cpp` (1) | `AppearancePage_HighlightFirst` (`0x0042b4c0`, 16 bytes: `mov ecx,[ecx+0xd0]; jmp MenuList_HighlightFirst`; 4.10d took it for padding after a jump table) |

Native code: `src/runtime/create_screens.hpp` (layouts and the class), `create_screens_ui.cpp`, `create_screens_stat.cpp` (also the helpers every screen
shares and the stat window), `create_screens_save.cpp`, `create_screens_editor.cpp` (also the personal page).

## What is native and what is a seam

The window base (4.10a), the menu toolkit (4.10c), the font code, the skater manager (4.8) and the 4.10d helpers (`CommonFe_LightDefaults`, `NumberRow`) run
natively on the same memory as retail. Everything else a screen calls is a service reported with the arguments retail passed (`engine_XXXXXXXX`, the
object first for a thiscall; a value such as an allocation, a font or a poll answer is scripted): the page classes of other files (the three pages' polls,
loads and updates, `AppearancePage_Construct`, `StatPoints_Construct`, `StatPage_Build`, `TrickMenu_Construct`/`_Update`), the engine's model
(`MySkater__*`, `CustomSkater_Create`, `PSX_*`), render (`M3D_*`), sprite (`D3DSprite_*`) and spool calls, the memory-card screen (`Mmu_*`) and the
frame plumbing (`Game_FrameTick`, `Db_*`, `Pad_ClearTriggers`, `PollHost`). The calling conventions of all of them were read from the callees' `ret N`
(`build/p410e/rets.py`): `CustomSkater_SaveColours` (`0x00422640`) is a thiscall with one argument, which a first stub got wrong and which showed as
a jump to `0x2` after the stub left the argument on the stack.

Decisions worth knowing:

- **The three loops are replayed with seams.** A loop calls its window's step, update, draw and deleting destructor through the window's vtable. A window
  retail built holds real children (text and sprite elements, the personal and stat pages), whose update, draw and destructor retail would run, while the
  window base reports a child's virtual call as a service. So with `CreateScreens::set_loop_seams(true)` (the replay does) the loops report those four
  calls as services (`engine_0042c940` and its neighbours), and the methods are replayed on their own on windows with fake children. A capture of the
  loops reaches the clock wait too: a stub option `writes=[(address, size, fn)]` in `re/gdb/opentony/fcall.py` lets the stubbed `Game_FrameTick` move
  `SimulationPublicTick` on, and the generator starts the clock two ticks behind the last one so the wait is a few calls.
- **`PersonalPage_BuildList` skips `TrickConfig_FindByCharacter`** (`0x00416050`, the call whose result retail drops): it is pure and its only effect
  could be an assertion on an invalid character, which the generator never builds.
- **The assertion branches of `TextBox_Construct`** (a null text or font) are not generated: retail goes on to read through the null pointer.
- **The editor skater's `+0x6c` material** is reached only by forced calls of `CreateEditor_SetupScene`; the constructors build the skater object from
  zeroed arena memory, so their material is always null.

## Findings

- **A bug in the 4.9 `Text_Layout`** (`0x00425ca0`, `CreateSpool::layout`): a single word wider than the box is stored on a line of its own and retail then
  moves past it (`src = after`); the port did not, and looped on the word until memory ran out. No 4.9 capture had a word that wide; this one's
  first case did. Fixed, with an overload that measures candidate lines with the font code instead of the `text_width` service (the screens' fonts are
  native).
- **Functions 4.10d missed:** `AppearancePage_HighlightFirst` (above) and the stat window's constructor `0x004b5bb0` (1.2 KB), deleting destructor
  `0x004b6060` and destructor `0x004b6080`, which sit before `statwin.cpp`'s first listed function. The screen callback `0x004b5ba0` (stores
  `StatEdit_Run`'s result) sits there too. Ghidra listed no function for 7 of the 55 (vtable-only methods, the callback records); `tony ghidra sync`
  created them from the curated names.
- **A screen callback record** (`CreateSave_InitCallbacks`, `0x0042be40`) is two stores: `[record] = exit callback`, `[record + 0xc] = run callback`;
  `0x00426400` (credits.cpp) and `0x0042cd20` (cresel.cpp) are the same function for their screens.
- **A stub on a menu fake vtable slot** collided with a real function: the menu group's fake grid vtable slot is `0x0046f480`, which is
  `SuperMem_CheckForLeaks`. Two stubs at one address corrupt the stack (one stub rewrites `eip` and the other then stops on the rewritten state).
- **The costume of the active slot** must be one the character has, or `Skater_SelectModelName` asserts and returns null, which `StatEdit_SetupScene` then
  copies from; the generator picks it from the row table. The active slot of a screen is the player's own pad (`gActivePlayer`), not an arbitrary slot.

## Validation

`re/gdb/probes/frontend_calls.py` with the groups `crui`, `crsave`, `crstat`, `credit` and `crloop` (specs in `frontend_specs_screens.py`, the slab,
seeds and stubs, and `frontend_specs_screens_cases.py`, the generators). Each function is called in the running game on a random legal state in a
scratch slab of 0x24000 bytes: windows are built by hand with fake children whose virtual methods are services, constructors run for real in an arena
whose blocks start with a fake vtable (an object a stubbed constructor "builds" is a legal child), and the replay
(`create_screens_retail_calls_test.cpp`) compares the return value, every written byte and the ordered service calls. Fixtures:
`re/evidence/fixtures/crscreen-calls-{crui,crsave,crstat,credit,crloop}.txt`. Unit tests: `create_screens_test.cpp`.

## What 4.10e2 can reuse

- The slab, seeds, stub table and generators above; a new group registers with `register(group)` and adds its stubs to `GROUP_STUBS`.
- The loop seams and the `writes` stub option (the `cresel` screens loop on the pad and the clock too).
- `0x0042cd20` / `0x0042cd40` / `0x0042cd50` are cresel's callback record, its exit callback and its run wrapper (`*result = FUN_0042f100()`);
  `0x0042f880` (the 0x2c-byte definition holder's constructor, called by the editor and the stat editor before `CustomSkater_LoadDefinitionPsx`), and
  probably `0x0042f8b0` / `0x0042f8d0` (its deleting destructor and destructor), sit just before `custmgr.cpp`'s first assertion and are most likely that
  file's, not cresel's; check when naming them.
