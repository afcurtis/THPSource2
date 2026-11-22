# The keyboard bindings screen and the career loading screen

Status: confirmed by retail forced calls (880 cases of the bindings screen, 80 of the loading screen, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0041c2d0`, `0x0041c490`, `0x0041c4a0`, `0x0041c4b0`, `0x0041cda0`, `0x0041cdc0`, `0x0041ceb0`, `0x0041d060`, `0x0041d0f0`, `0x0041d4f0`, `0x0041d580`, `0x0041d6f0` (12 functions) (slice `keyboard-bindings-screen`), `0x00415b30` (1 functions) (slice `career-loading-screen`)
Native: `src/runtime/createobj_kit.hpp`, `createobj_screen.cpp`; replay `src/runtime/createobj_retail_calls_test.cpp`; probe `re/gdb/probes/cobj_specs5.py` (groups `cscr` and `kcs`); fixtures
`re/evidence/fixtures/createobj-calls-cscr*.txt`

## What is here

**The bindings screen** (vtable `0x00518744`, 0x444 bytes; `Game_MainLoop` at `0x0041c2d0` is its game state and `Create_LoadPSH` at `0x0041c4b0` its constructor, both names being Ghidra's) lets the player
rebind the keyboard and the pads. It is built on the window base (`Window_Construct(0, 0, -1, -1)`) with a pie, two fonts (mainf at +0x100, s2font2 at +0x254, each filled from the font record by a field copy that
leaves the two padding bytes at +0x22 alone), the background `lti\conbk.bmp` at +0x250, six corner pictures (`lti\conwin[a-f].bmp`), two arrows (`s2arrow6/7.bmp`), four prompts (back, reset, select, accept),
the style object (`MenuProperties`, whose fields are mainf's moved up four bytes), the list at +0x3a0 and fifteen rows from +0x3a4. The bindings table (15 rows of 0x14 bytes from `0x005600a4`: the label
at +0, for row 0 the list of page titles at +4, a pointer to the row's two key bytes at +8 and one to its pad button cell at +0xc) is the game's; `0x005600a0` is the page.
`KeyConfig_SetRow(row, delta)` writes the texts of a row through the row's virtual method 9: row 0 is the page title (the delta turns the page, wrapping), page 0 shows the names of the row's two keys
(`PCInput_KeyName`; the delta moves the selected column 0..2) and the other pages the pad button as a number (`-?-` for -1). `KeyConfig_Assign(row, column, key)` binds a key: the function keys (0x3b..0x44,
0x57..0x66) refuse with the error sound, otherwise the row that had the key takes the old one. `KeyConfig_SetButton(row, value)` does the same for a pad button (-1 clears). `KeyConfig_Poll` reads the pads' edges:
a confirm or back sets the done flag (+0xfc) with the answer 1 or -1, only in the first column. `KeyConfig_Update` is the frame: the blink counter (modulo 20), the list's steps, the window and the background
sprite. The loop of `Game_MainLoop` clears the front-end flag, runs the frame functions, polls the screen, steps its window (slot 1 of its vtable, `0x0041d0f0`) until it is done, then saves the bindings (or
reloads them when the screen was left with back) and answers the screen's answer.

**The step** (`KeyConfig_Step` 0x0041d0f0, `CreateObjKit::keyconfig_step`; the method the standalone stopped on, 12 s7 seed 4) runs once a frame. With the mouse on (`0x006a13fc`) a click on the reset prompt (+0xc4,
slot 7) resets the bindings (`PCInput_ResetBindings`) with a sound, and the row under the pointer (`MenuList_HighlightedId`, then slot 7 of that row) selects the column the mouse chose (`0x006a1410`). It reads
the first pad button (`PCInput_FirstJoystickButton`; the frame before is kept at +0xb8) and the first key down (`PCInput_FirstKeyDown(0)`). On a pad page (+0xb0 not 0) the column is 0, Backspace clears the
row's button and a new button binds to it. On the keyboard page a key with a selected column goes through a table of 208 entries: Escape, up and down leave the column, the arrows leave it when one of
two pad edges is set, Enter does nothing, Backspace clears the key (`KeyConfig_Assign(row, column, 0)`), any other key is assigned and every row's text is redone. Rows 11 to 14 have one column (column 1
becomes 2; two pad edges leave it). The selected cell blinks: every fifth frame after the tenth it is drawn blank through the row's slot 9, before that the row is redone. Two pad edges turn the row's value down
and up (`KeyConfig_SetRow(row, -1 / 1)`), two more leave the column with a sound; when anything changed every row's text is redone and rows 11 and up are enabled on the keyboard page only. The step ends in
`Window_Update`. Group `kcs` captures it as retail code with `KeyConfig_SetRow`, `SetButton` and `Assign` as retail callees (group `cscr` keeps stubbing it inside `Game_MainLoop`).

**The career loading screen** (`Career_LoadingScreen(mode)`) loads the goal font, builds two backgrounds and the prompt buttons the mode asks for (1 back, 2 play, any other both), then draws two frames of the level's
checklist: the level's name and "Checklist", ten goal lines each with a check mark sprite when `Career_Got` says the goal is reached (drawn through the sprite quad builder, a float offset), the goal's high-score
text and its amount (`$%d`), and the background.

## Method

The bindings table is rewritten by the seed to point into the slab (labels, titles, key bytes, button cells) so that everything the screen reads is a captured window; the level table of the loading screen likewise
(three levels, ten goals). The engine objects the constructor builds are stubbed constructors that hand out arena blocks (the stub's answer is the block the allocation just before it returned); the list, the rows
and the style get a fake vtable whose slots are retail functions the harness stubs, so the virtual calls are reported with their object (`this`) and arguments. Locals of the loading screen (its Font object and the
line it formats) are mapped to scratch addresses (`stack_to`). The loop of `Game_MainLoop` is run with a pad edge already set, so it ends after one frame; the clock stub advances `0x0056e31c` by 1..3 per call.

## Lessons

(1) A stub's `text=0` is falsy and records nothing: the index of the first argument is `text=True`. (2) The stubbed `Game_FrameTick` writes the clock, so the loading screen must read the scripted value back
exactly as the loops do, though it never waits on the clock. (3) The decompiler's idea of `Create_LoadPSH` as a fastcall hides that the caller allocates the block; the constructor is called with it in ecx.
