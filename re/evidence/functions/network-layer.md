# The two-player network layer and the PC mouse of the menus (Phase 4.13b2)

Source: `pc\netgame.cpp` and the mouse / text-entry code that shares its address range, `0x004dbd90..0x004e1370`. Native: `src/runtime/net_game.hpp` (`NetGame`, a `PcKit`) with `net_mouse.cpp`,
`net_state.cpp`, `net_play.cpp`, `net_update.cpp`. **74 functions** in seven slices (`pc-mouse-cursor` 5, `pc-menu-widgets` 16, `netgame-state-queries` 15, `netgame-level-and-career` 6,
`netgame-flags-and-queues` 19, `netgame-handshake` 7, `netgame-update-and-send` 6).

The phase plan called this range "the netgame query surface" with single-player answers. Walking it in the disassembly showed what it really is: the whole **two-player layer of the PC game**
(a state machine that handshakes two machines through DirectPlay, sends the game's state as packets and drains the other machine's) plus the **PC mouse of the menus**. Every network function
returns at once while the flag `0x006a3b21` is clear, so single-player behaviour is "nothing happens"; the port still reproduces the whole layer, because the flag is just a byte and the
forced calls can set it. DirectPlay itself (`0x004daf10..0x004dbd90`), the message dispatcher (`0x004e0dd0`) and the senders of the custom level and skater are out of scope (`scope.yml`
"directplay") and are services here.

## What the code is

**The mouse.** Globals `0x006a13e0..0x006a1438`. `Mouse_Track(x, y, buttons)` scales the poll into a 320 x 240 menu space (`x << 9 / displayWidth`, `y * 240 / displayHeight`), clears the *released* byte
and raises *changed*; a button word that goes from non-zero to zero sets *released*. The menu widgets (`Slider_PickByMouse`, `MenuRows_PickByMouse`) hit-test the pointer against rectangles
(`0x004dbf90`, a service) and feed the result to the pad as "pending buttons" (`0x8000` left, `0x2000` right, `0x40` confirm). The key-name table at `0x0054a3a8` (rows `{key, name, replacement}`)
maps the names of the bindings. The park list (`NetMenu_ParkListLoad`) searches `*.prk`, cuts every name at its **first** dot (the call is `strchr`, found by the replay) and at 31 characters.

**The handshake.** Two words, this machine's step (`0x006a3a90`) and the other's (`0x006a18b0`), run through 0..0x16: 2 versions agree, 5/6 hold, 7 options, 8/9 agree and exchange the park
and skater, 0xe-0x11 loading, 0x12 playing (latency decides how often to send), 0x14/0x15 game over, 0x16 ended. `NetGame_Update` (`0x004e02d0`, 2.7 KB) runs the step's actions (the table at
`0x004e0d78`), sends the state when due (`0x006a3df8`, every `0x006a3d20` ticks) or retries (`0x006a3df4`), drains up to 31 messages (201 after steps 8 and 13), copies the remote skater's
score and flags in and decides when a silent or quit game is over (120 / 180 / 300 ticks by game type). The flag words (`0x006a3a9c` / `0x006a18bc`: 0x100 tagged, 0x200 in-game, 0x2000 pause
request, 0x20000 pause agreed, 0x80000 end of run ...) are set and cleared by `NetGame_SetLocalFlag` / `SetRemoteFlag`. `NetGame_SendState` builds the type 5 packet (`0x006a1840`, 100 bytes) from
the game's globals.

## Findings

1. **The range is not a query surface.** Of the 74 listed functions 21 are the mouse and its widgets, 40 are the network state, its flags, queues, tables and
   one-line queries, and 13 are the handshake (match start and end, the waiting loops, the senders, the per-frame update and the message handlers). The mechanical scope rule ("no caller outside the range") had tagged 31 more functions of the range as DirectPlay internals; they stay out of scope and are services.
2. **The packet descriptor is a stack local with three garbage dwords.** `NetGame_SendState` and `SendString` hand DirectPlay `{3 leftover words, size, pointer}` built on the stack (and, for the chat packet,
   the packet right behind it). The probe stubs the two `call 0x004db350` instructions as call sites (`stub_site`), records the 20-byte descriptor (and the packet) as the call's text, blanks the leftover
   words and the stack pointer, and the native port writes the same bytes.
3. **`0x00501ab0` is `strchr`, not `strrchr`.** `ParkListLoad` cuts `my.park.PRK` at `my`; the first port used the last dot and the replay found it in the first capture.
4. **Stashing and restoring the career globals are not symmetric:** ten globals are saved and cleared, nine restored (`0x0056b7ec` is not).
5. **Tail jumps into services:** `NetGame_Reset` ends in `jmp 0x004dd980` (the session start), `OnSystemMessage` in `jmp 0x004deff0`, `TextEdit_Destruct` in `jmp 0x0046fe80`; the stub returns through the harness's
   trampoline when the function is forced-called directly.
6. **A stub that allocates:** `NetGame_OnCustomLevelMessage` calls `EditorMaps_Alloc` only when the editor image pointer is null; the stub's scripted write of `0x005614cc` is replayed by the native port.
7. **Loops are replayed with the clock and the Escape key scripted:** the frame tick advances the clock 1-3 ticks per call (a `writes=` stub), the key poll answers "down" after a scripted number of passes and
   the message poll answers "none" after a scripted number of polls.
8. **An argument that is a byte or a word pushed as a dword** (`SetPendingButtons`, the flag setters' on/off byte) is masked in the stub (`masks=`) and the port truncates the same way.

## Validation

Retail forced calls on generated state in the running game (a level loaded, `re/gdb/probes/frontend_calls.py` with the groups `mouse`, `nets`, `netp` of `net_specs.py`, `net_specs_state.py`, `net_specs_play.py`): the probe makes a legal
state in the game's own globals and a scratch slab (two skaters, the cursor sprites, a text entry control, a slider row, a file list, a message, the editor image cells), calls the retail function and records the return value,
every written byte and the ordered service calls; `net_retail_calls_test.cpp` starts from the same bytes and compares all three. **15,398 cases, 0 mismatches** in the full captures: `mouse` 4,200 (200 per function),
`nets` 8,100 (200 per function; 300 more of `NetTable_Take` after the first capture's wild head indices were removed), `netp` 3,098 (200 per function, 600 of `NetGame_Update`, 300 of `NetGame_OnSystemMessage` after the first
capture lacked the `"unknown"` text in the read-only tables). gcov of the replay over the full captures: `net_mouse.cpp` 217 / 219, `net_state.cpp` 367 / 367, `net_play.cpp` 308 / 308, `net_update.cpp` 286 / 289 lines.
The uncovered lines: the refused division by a zero display size in `Mouse_Track` (retail would fault), the lambda that reports the park list's allocation (an artefact of gcov: the allocation itself is replayed), the
two defaults that give a local player with a zero score field a score of 0x1e in the update (no generated state has one) and one `break` of the update's message loop: **derived from the disassembly only**.

The committed fixtures `re/evidence/fixtures/net-calls-*.txt` keep the first 30 cases of each capture per function (2,220 cases); they replay with no mismatch and a changed written byte or a changed argument of the DirectPlay
send makes the replay fail (`tests/test_net_game.py`). Gates: `ctest` 152/152, `pytest -q` 584 passed + 7 skipped, `tony verify --all` byte-identical.

## Not ported (and why)

The 31 functions of the range tagged `directplay` in `scope.yml` (the hit tests `0x004dbf90` / `0x004dbfd0`, the session start `0x004dd980`, the receive handlers `0x004e0dd0..0x004e1250`'s callees, the senders
`0x004df420`, `0x004e00c0`, `0x004e0180`, `0x004e0210`, ...); the vtable-only methods of the lobby window and its controls between `0x004dc190` and `0x004dd780` are the netmenu classes of 4.10i2.
