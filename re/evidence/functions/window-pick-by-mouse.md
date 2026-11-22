# Pick-by-mouse: slot 7 of the window-graph classes

Status: modeled (read from the disassembly; `NetMouse_HitRect`, the rectangle test they all end in, is checked against retail by `tony.native_oracle`; the methods themselves are not yet)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004dc480`, `0x004dc9f0`, `0x004dca60`, `0x004dcb30`, `0x004dcb70`, `0x004dcc20` (6 functions)
Native: `src/runtime/net_mouse.cpp` (`NetGame::menu_list_pick`, `element_pick`, `text_element_pick`, `sprite_element_pick`, `image_pick`, `prompt_window_pick`), `re/config/standalone_natives_manual.yml`

## What is here

Seven vtable slots of the window-graph classes (`MenuList`, `ScreenElement`/`Window`/`OptionsMenu`, `TextScreenElement`, `SpriteScreenElement`, `PromptWindow`; slot 4 of `SlicedImage2`) answer
"is the pointer over this object?" for the mouse-driven menus. The linker put them between the DirectPlay wrapper and the netgame functions (`0x004dbd90..0x004dcc90` holds the mouse layer of
the menus: the pointer tracking, the cursor sprites, the rectangle test and these), so `scope.yml` filed four of them under "netgame internals, unreachable"; they are not: every menu screen reaches
them through its vtable (milestone 7c, `Options_Run`). Two (`0x004dc480`, `0x004dcc20`) are in no symbol table of the earlier tools at all, because nothing calls them directly.

Each ends in `NetMouse_HitRect(x, y, width, height)` (`0x004dbf90`): the pointer (`0x006a13f0`, `0x006a13f4`, the 320 x 240 menu space) inside the rectangle with its edges, signed compares, a byte
answer. The list picks highlight an enabled child when the pointer has moved; the prompt window tests its box, two buttons and two arrow pairs. A button release over an object with a key sends the key.

## Method and what is not shown

The bodies were transcribed from the disassembly (`src/runtime/net_mouse.cpp`). `tony.native_oracle` runs `NetMouse_HitRect` in the retail process and in the standalone on ten pointer/rectangle
pairs (edges, a negative origin, a negative width, a pointer left of the screen as a signed dword) and compares the answers. The six methods need window objects with children and the mouse state a
screen leaves; no oracle builds those yet, and the Options screen's frames do not exercise a highlight (a pointer moved onto a menu line changed only the cursor sprite in the 8a input check).

## The main menu's three (milestone 9)

Addresses: `0x004dbfd0`, `0x004dcf80`, `0x004dd140`. Native: `NetGame::hit_circle`, `main_menu_wheel_pick`, `main_menu_window_pick` (`src/runtime/net_mouse.cpp`; rows `NetMouse_HitCircle`,
`MainMenuWheel_PickByMouse`, `MainMenuWindow_PickByMouse` in `re/config/standalone_natives_manual.yml`).

The cold boot of milestone 9 reached them through the main menu's vtables; no symbol table had any of the three, and `scope.yml` filed `0x004dbfd0` under "netgame internals, unreachable" for the
same reason as the four above (it lies in the menus' mouse layer between the DirectPlay wrapper and the netgame).
- `0x004dbfd0` (cdecl: x, y, radius): is the pointer inside the circle? The x87 square root of the squared integer offsets from the pointer, compared with the radius.
- `0x004dcf80`, slot 7 of the main menu's wheel: with no turn pending, over the wheel (a circle of 150 around (255, 128)) each of seven 50 x 50 hot spots marks the pointer over an item and, on a
  release, asks for that turn (the front spot also presses the confirm button 0x40); with a turn pending, one step (the left or right button) every five polls. Answers whether the pointer is over the
  wheel.
- `0x004dd140`, slot 7 of the main menu's window: on a release over its wheel (+0x200, the wheel's own slot 7) the cursor is shown; answers 0.

Checked only at the session level: the `boot-menu` scenario, cold-booted in the standalone, equals retail's recording in every block of `.data` (`python -m tony.standalone_boot`). The pointer stays
at rest in that scenario, so the hot spots and the turn steps are not exercised.

## 12 s7: the methods the career fuzz reached (trace-validated)

Addresses: `0x004dc530`, `0x004dc5c0`, `0x004dc630`, `0x004dc640`, `0x004dc8a0`, `0x004dc8d0`, `0x004dc9d0`, `0x004dcea0`, `0x004dceb0`, `0x004dd170`, `0x004dd180`. Native: `NetGame::grid_pick`,
`button_pick`, `column_list_pick`, `right_button_pick`, `arrow_pair_pick`, `arrow_children_pick` (`src/runtime/net_mouse.cpp`); the constant answers (`xor al, al`, `ret 4`) are rows of the replay table.

`build/m12/fuzz_career.py` (random keys through the real menus of a career) stopped on `vcall_004dc530` (the save-card screen's item grid, `Front_SaveCardStateMachine`) and `vcall_004dd180` (the
equipment window, `EquipWindow_Update`): slot 7 of window classes nothing had ported, because the earlier sessions reached only the classes of the main menu, the options and the prompt windows.
`python build/m10g/vtscan17.py 5183d8 51a800` lists the slots left (see the session's note in PHASE_5_SUMMARY.md). All eleven share one shape: a rectangle test (`NetMouse_HitRect`, a service here as in
`Slider_PickByMouse`), the mouse flags (`0x006a13f8`, `0x006a13fc`..`0x006a13fe`), `0x006a1408` (the last id), `0x006a1410` (the list row) and the pending-button store (`0x004e4190`, 16 bits).
`0x004dc630` is a jump to `0x004dc5c0`; `0x004dc8a0` calls it and sends right on a release; `0x004dc640` and `0x004dc8d0` are the bar-with-arrows classes (`0x004dc7b0`'s siblings).

Probe: group `mouse` of `re/gdb/probes/net_specs.py` (a fake vtable whose slots all go to one stubbed entry, `FAKE_PICK` = `0x004ad9d0`, answering 1 in 60 % of the calls; the window objects with their
arrows, the item grid; the pointer, the release and changed flags and the pending-key word `0x006a696c` tilted towards the branches). 300 retail calls each, fixture `net-calls-pickA.txt` (the
first 30 of each committed): every case replays with the return value, the written bytes and the ordered service calls equal. Mutants: `build/m12/mut_pick.py`, `build/m12/mutate_net.py`.

Left: `0x004dc190` (the text entry control's update, the network name entry), `0x004dd390` (the park editor's pointer frame) and the unported slots outside this range (see PHASE_5_SUMMARY.md).
