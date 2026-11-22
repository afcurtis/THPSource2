# The shell, its small classes and the network lobby: `frontend2.cpp`, `netmenu.cpp` and the helpers around them (Phase 4.10i2)

Source: `frontend2.cpp` (`0x00452e00..0x00454c00`), `netmenu.cpp` (`0x0047cbb0..0x0047fc80`), the pre-load progress object (`frontpre.cpp`), the text window (`textwin.cpp`) and the helpers that
no screen file owns (`menu.cpp` pad poll, `panel.cpp` balance meters, `rfront.cpp`, `screentools.cpp`, `tapes.cpp` effect object, `shellutils.cpp`). Native: `src/runtime/front.hpp` +
`front_shell.cpp` + `front_main.cpp` (`FrontScreens`), `shell_helpers.{hpp,cpp}` (`ShellHelpers`), `netmenu.hpp` + `netmenu{,_update,_state,_summary}.cpp` (`NetMenu`); all derive from
`ScreenKit`. 99 functions in seven slices (`front-shell-helpers` 10, `front-shell-loop` 1, `front-preload-screen` 12, `shell-small-classes` 34, `text-window` 15, `netmenu-lobby` 16,
`netmenu-windows` 11). The plan listed 89; the neighbour scan found ten more (among them the `0x00452e00 -> 0x00452e10` and `0x00454c80 -> 0x00454c90` jump thunks).

## What the code is

**`FrontEnd_Main` (`0x00452ff0`) is the shell's screen loop.** One global (`kScreen`, a number from 0 to 23) names the screen to show; every screen is a function that runs its own loop and
reports how it was left, and `FrontEnd_Main` picks the next screen from that answer. The loop ends when the window is closed (the quit flag) or the main menu answers quit.

| Screen | Name | Screen | Name |
|---|---|---|---|
| 0 | main menu | 12 | create-a-skater (appearance only) |
| 1 | skater select | 13 | create equipment |
| 2 | level select | 14 | create-a-skater save |
| 3 | equipment select | 15 | stats editor |
| 4 | create-a-skater | 16 | trick select |
| 5 | level editor | 17 | options |
| 6 | play the game (`Front_LaunchGameLevel`) | 18 | credits |
| 7, 8 | competition players / difficulty (kept for the table; the jump table sends them to the default) | 19 | multiplayer handicap |
| 9 | multiplayer select | 20 | video menu |
| 10 | career select | 21, 22 | network lobby (first / second entry) |
| 11 | create-a-skater select | 23 | controller menu |

The main menu answers `0x20..` through a byte table at `0x00454014` to twelve outcomes: 0x20 starts a free-skate game (mode 2) at the skater select, 0x21 the career, 0x22 the level
editor, 0x23 create-a-skater, 0x26 the level select, 0x27 the network lobby (two players), 0x28 a game in mode 3 at the skater select, 0x29 the equipment select, 0x2a the demo, 0x2b the
options, `0xff` quits (it logs and closes the database on the way out) and anything else is ignored. The **demo** (outcome 8) loads the recorded game header (`Replay_LoadHeader`) and plays it back with the selection it was recorded with.

**Everything a level leaves behind is a request in memory.** `LaunchGame` (`0x0046a8d0`) returns when the level ends; the level's *start-screen request* (`0x00561b9c`: 1 level select, 2 skater
select, 6 play again, 7 credits) tells the shell where to go next, and the options screen leaves four more requests (controller menu, video menu, credits track, video-test) that
`FrontEnd_Main` reads after the screen returns. The replays script those as writes under the screens' names.

**The pre-load object** (`Pie_*`, 0x34 bytes, vtable `0x00518a04`) starts the spooler on the file of one of the player-spool table's entries and counts frames until it is ready; the
table is walked in 8-byte steps until the id matches or the next entry's name is empty. **The text window** (`TextWin_*`, a doubly linked list through `+0x48/+0x4c` under the head
cell `0x0056db70`) holds up to eight lines of text on a panel; its draw puts every window's packet on the viewport list at the depth of the first viewport (or the front when the
viewport is the main one).

**The network lobby** (`NetMenu_*`, 0xa00 bytes) is a window with five lists (the main page, the found games, the host/join form, the settings and the game page), seven chat lines of 0x38
bytes at `+0x734` with their kinds at `+0x718`, the chat message at `+0x884`, the typed text at `+0x8bc`, a status line at `+0xb0` and fonts at `+0x1dc/+0x328/+0x474/+0x5c0`. Its
state (`+0xf0`) runs 0/1 the first page, 2/3 the host/join form, 4 starting the host, 5 the settings page, 6/7 waiting for the other player, 8..0xb the game search and the join, 0xc/0xd the
connected lobby (chat) and 0xe connected. `NetMenu_Main` (`0x0047cbb0`) builds the lists from four 24-byte description tables (`0x0056ad30`, `0x0056ae38`, `0x00535908`, `0x00535968`) and
runs the update, the draw and the back button until the screen is left; it returns the next shell screen.

## Findings that are not in the code

* **The decompile misleads about which pointer is the answer.** `PlayerSelect` (`0x004a2010`) is called with `(&mode, &answer)`: the *second* pointer receives the answer, the first is an
  input. `EquipSelect` (`0x00444590`) takes `(&{mode, active player}, &answer)`: the first argument is an 8-byte struct. The first capture of `FrontEnd_Main` hung because the answer was read
  from the wrong pointer (the screen then reported the same answer for ever, the loop never left the skater select). In/out pointer roles must be read from the stack offsets in the
  disassembly, not from the decompile; the probe's `deref` (record the input) and `blob` (record a struct) options and the native `engine(..., argument)` text carry them.
* **The decompile drops `ecx`.** `FUN_004dc370` is a thiscall on an edit-box item; `NetMenu` virtual calls on items and lists lose their object in the decompile and must be read from the
  call sites (`mov ecx, [esi+...]; mov edx,[ecx]; call [edx+n]`). The MenuList centre argument of the found-games list differs from the other four lists (`0x60, 0x74, 0x1f0, 100, 0`).
* **`0x005252f0` is a single space, not an empty string.** The lobby clears its status line and its chat lines by `sprintf`-ing that string, so the status becomes `" "` (one byte
  `0x20` followed by the old terminator's position) and the draw condition (`status[0] != 0`) still holds. The first native port wrote an empty string and the replay of the found-games page
  caught it (the draw call and the byte were missing).
* **The keyboard line editor fills the typed buffer** (`0x004df370(buffer)` returns with the text in place); the probe stubs it with `fill=` and the replay applies the scripted bytes as two dwords
  at `+0x8bc` before sending the chat line. The chat line is formatted into the last slot as `"%s: %s"` (speaker, text) with the speaker from the host or the player name, or as a bare message.
* **Chat lines scroll in 0x38-byte slots and the format arguments are `%s` of arbitrary pointers** (the speaker names are 0x40 bytes of the network layer's tables); the garbage-filled
  generator state left the typed text unterminated in the first net2 capture, which made the native `cstring(…, 0x40)` fault where retail's `sprintf` would have run on into the next field. The
  generator now terminates the chat message and the typed text (retail's keyboard handler caps them); the capture was redone.
* **The window base's destroy walks 32 child slots** (`+0x28`, count `+0xa8`); fake windows need zeroed slots and valid style vtables or the forced call crashes in a destructor.
* **Harness breakpoints inside `FrontEnd_Main` break gdb.** The harness that brought the game to the level leaves breakpoints in `FrontEnd_Main` (the main-menu answer, the screen automation); a
  forced call of the shell runs through them and one that creates breakpoints from a stop handler raises gdb's `finish_step_over` internal error. The probe disables them first
  (`silence_harness`).
* **The CRT's `sprintf` runs for real** in the pie, net and twin groups (it is a leaf with no side effects but its output), with the format string and the arguments recorded as the call's text.
* **One captured case carried a write the code does not make** (the first `fmain2` pass wrote `0x1800` into the simulation clock `0x0056e31c` and its neighbour in case 0; the same inputs
  captured again wrote nothing). It was a real-time artifact of the game's timer thread running while the first forced call was suspended in gdb, not shell behaviour; the whole pass was
  captured again and the fixture is the clean one.
* **Three groups of functions in the 4.10i list belonged to other milestones.** The shatter effect, the relocation helpers and the redbook/VAB loaders were listed here because their
  addresses sit between shell files; `re/xref/milestones.yml` moved them to 4.15d, 4.14c and 4.13b.
* **The bitmap loader (`frontend-bitmap-load`, real function starts `0x004afd40`, `0x004afd8c`, `0x004b035e`) is not in this milestone.** It was done in 4.10i3 ([bitmap-loader.md](bitmap-loader.md)) together with the screen loops ([screen-loops.md](screen-loops.md)).

## Seams

Everything the engine owns is a reported call with the arguments retail passed: the career and equipment screens `FrontEnd_Main` starts (`0x00417400`, `0x004588f0`, `0x0042cd50`, `0x0042be70`,
`0x004b5ba0`, `0x004bf860`, `0x0047ba60`, `0x00454eb0`, `0x00481200`, `0x004a2010`, `0x00444590`, `0x004271a0`, `0x00426430`, `0x0046aee0`), the lobby (`NetMenu_Main`), the level (`LaunchGame`,
`Init_Restart`, `Db_Init`), the skater manager (`SkaterManager_*` thiscalls on `0x0056c580`), the spooler and the sound and fade calls. The lobby's network layer (`0x004dd980`, `0x004ddb70`,
`0x004dde10`, `0x004de040`, ...) is a set of seams of its own (reported by address); the lobby window's lists, items and fonts are virtual calls reported by slot (`list_v3/4/5`,
`item_v7/v8`, `font_draw`). Same-file calls the port makes natively: the whole `NetMenu` state machine (`update`, `update_lobby`, `add_chat`, `refresh_games`, the summary window), the
`FrontScreens` shell helpers and the `ShellHelpers` methods.

## Probe

`re/gdb/probes/frontend_specs_shell.py` (groups `fe2 fmain`; `FrontEnd_Main` runs every screen as a stub whose answers come from a script that steers the loop back to the main menu
after a bounded number of screens; the level launch, the options flags and the lobby's results are scripted writes), `frontend_specs_shell2.py` (`pie shl twin`),
`frontend_specs_net.py` (`net1 net2 net3 net4`; the lobby window is a fake object with fake lists, items and fonts built by `window_world`, the network layer's statics poked, the pads'
edge bytes poked). Stubs of the functions under test are left out by the `P410_GROUPS` selection; one stub per address.

## Results

All 99 functions replay with no mismatch: **8,585 forced-call cases** (return value, every written byte and the ordered service and virtual calls compared). Fixtures
`front-calls-shell-{fe2,fmain,fmain2,pie,shl,shl2,twin}.txt` and `front-calls-net{1,2,3,4}.txt` (1,500 + 200 + 400 + 1,800 + 1,980 + 30 + 750 and 600 + 735 + 550 + 40 cases;
`shl2` is `Effects_SetBusy` alone: the first `shl` pass produced no case for it because the flag's address was missing from the group's regions, which the probe reports as a failed case).

| Slice | Functions | Cases |
|---|---:|---:|
| `front-shell-helpers` (`fe2`) | 10 | 1,500 |
| `front-shell-loop` (`fmain`, `FrontEnd_Main`) | 1 | 600 |
| `front-preload-screen` (`pie`) | 12 | 1,800 |
| `shell-small-classes` (`shl`) | 34 | 2,010 |
| `text-window` (`twin`) | 15 | 750 |
| `netmenu-lobby` (`net1`, `net2`, `net4`) | 16 | 1,375 |
| `netmenu-windows` (`net3`) | 11 | 550 |

Line coverage of the native code by the fixtures (gcov, `-O0`): `front_shell` 177/177, `front_main` 356/368, `shell_helpers` 456/464, `netmenu` 206/206, `netmenu_update` 196/198,
`netmenu_state` 291/308, `netmenu_summary` 150/151 (1,832 of 1,872 lines, 97.9%). What the replays do not reach: `FrontEnd_Main`'s level-editor "test the park" path (the editor test flag
stays clear in the generated states) and two `new_database` repairs, the lobby's join-accepted transition (`NetMenu_Update` state 0xb when the join answer is true) and the direct-IP form
branches, the settings list's mode choices 7 and 9, and a few shell-helper branches that need states retail does not produce (the spool table walked past its first entry, the signed-modulo
idiom on a positive repeat count, a text window linked after another). Those lines are ported from the objdump and covered only by reading; `shell_test.cpp` covers the text-window list
unlinking from the middle.

The captures were done twice for the shell loop (a second `fmain2` pass with the lobby's result widened to every screen number, which reached the stats editor, trick select, multiplayer
select and handicap, create-save and the controller menu) and the net2 capture was redone after the unterminated-text fix; `FrontEnd_Main` was captured with 400 cases per pass at most
because every case runs several screens.
