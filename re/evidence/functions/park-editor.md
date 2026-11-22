# The park editor (Phases 4.11d and 4.11e)

Source: `editorglobal.cpp` (`0x004326a0..0x00432fff`, 14 functions) and the small-method half of `leveleditor.cpp` (`0x00433090..0x00433fff` and `0x00436520..0x004395a0`, 50 functions): 64 functions
and four that Ghidra did not list (`0x004331d0` the table of commands, `0x00433200`, the thunks `0x00433210` and `0x00437850`), all native. Native: `src/runtime/level_editor.hpp` (`leveleditor::LevelEditor`) with `level_editor_global.cpp`, `level_editor_load.cpp`, `level_editor_ui.cpp`, `level_editor_camera.cpp`,
`level_editor_cursor.cpp`. Probes: `re/gdb/probes/editor_specs.py` (groups `edg` the maps and the park name, `edm` the park menu, `edl` load, discard and save, `edr` the editor loop and its command table),
`editor_specs_view.py` (`edc` the camera, `edu` the interface and the piece library), `editor_specs_cursor.py` (`edk` the cursor), run by `frontend_calls.py`; fixtures
`re/evidence/fixtures/leveleditor-calls-*.txt`; test `src/runtime/level_editor_retail_calls_test.cpp` (`tests/test_park_editor.py`). Slices `park-editor-global`, `park-menu`, `park-editor-loop`,
`park-editor-hud`, `park-editor-camera`, `park-editor-cursor`.

What the code is: the editor of the park maker. `CustomPark_ProcessState` is the state machine of the park menu (the 50 parks on the disc, the memory card, the saved parks); `CustomPark_LoadBuffer`
builds the generator object (4.11c) for the park the editor holds; `LevelEditor_Run` is the editor itself, one frame after another. The editor's pieces are four objects: the **interface** (the panel, the
piece library's selection, the budget bars and the message), the **camera**, the **cursor** (the piece or group in hand, its footprint, the base indicator) and the editor object the loop makes (0x1dc bytes,
constructed by `0x004334d0`, 4.11e), which owns the other three. Everything is a computation on memory plus services, so the validation is the 4.8 forced-call pattern again: the retail function is called inside
the running game on a state the probe pokes, the port replays the same bytes, and the return value, every written byte and the ordered service calls are compared.

## The d/e boundary

4.12 put the 14 functions at `0x004334d0..0x0043651f` (the editor object's constructor and destructor, its update and draw, the budget helper `0x00435f60`, the large mode functions) in 4.11e and the
rest in 4.11d. The call graph held: the functions here call the generator, the generator objects, the fonts and the renderer, never the large update and draw (`0x00433cb0`, `0x00433a40`), which `LevelEditor_Run`
reaches only as services. The one stretch of 4.11d code that is user interface (`LevelEditor_UiDraw`, `LevelEditor_UiUpdate`, 1.0 KB and 0.6 KB) stayed in 4.11d: it works on the interface object's own fields.
Nothing moved. 4.11e inherits the 14 listed functions, all the objects' constructors, and has the service seams below to bind.

## Services and the dispatcher

Every routine outside these 64 functions is reached through `LevelEditor::engine` / `engine_value` (the arguments, `this` first, and a text where the routine takes a string): the replay reports it by name
(`engine_<address>`) and returns the scripted result; a host that sets `set_dispatch` gets the address and the arguments and runs the real routine. Virtual calls (`virtual_call`) read the target from the object's vtable.
The allocator, `Mem_Delete`, the logger and the assertion keep their 4.9 names (`alloc`, `mem_delete`, `printf_log`, `assert`), the class allocator is `operator_new`. A service that writes through a pointer argument
(the engine's rotation and matrix routines, the file loader, the buffer parser) has its bytes scripted a word at a time (the probe's `fill` or `pokes`), and the port writes them where the pointer says.
Locals that retail keeps on the stack are built at fixed scratch addresses (`0x7ff009xx`, `0x7ff00axx`); `fcall.py` records a stack pointer argument as the scratch address of its argument index (a
dictionary `stack_to` now), so a call that is handed several locals can be compared. The CRT `sprintf` and `sscanf` that format names and lines are done natively.

## Object layouts

*Park menu* (`ParkMenu_Construct`): +0 vtable, +4 the delete menu, +8..+0x14 its texts, +0x18 the label, +0x58 the step, +0x5c the mode (0 the parks on the disc, 1 the memory card, 4 the saved parks), +0x60 saving,
+0x64 the result of the state machine (0 waiting, 1 done, 2 the choice was not a park), +0x68 the player, +0x6c the park buffer and +0x70 its size, +0x74 the list wrapper, +0x78 the labels' file and +0x7c the 50 labels.
The labels are read from `CDParks.txt`: each is the text after a `$` up to the end of its line, which the loader terminates in place.

*Camera*: +0 the quarter, +4 the swing in progress and +8 the swing asked for (a flick of the turn button adds 0x400), +0xc four yaw angles and +0x1c four pitch values (one per quarter), +0x2c..+0x34 the
point it looks at, +0x3c the state (1 steady, 2 turning), +0x40 the turn button is held, +0x41 the angle was changed, +0x44/+0x48 the last look direction. `EditorCamera_View` builds three angles on the stack
(the negated pitch, the negated angle, zero) and an offset vector `(0, 0, c)` with `c` one of four constants per quarter, has the engine make the rotation (`0x004e80e0`) and carry the offset through it
(`0x004e85a0`), and adds the target scaled by 1/4096. `EditorCamera_Update` hands it the globals `0x0055f7fc` (matrix) and `0x0055f7cc` (position) and keeps the position 30 units clear of the park's edges.

*Cursor* (0x15f0 bytes): four elements of 0x94 bytes at +0 (+0x4c model or -1, +0x50 turn, +0x54..+0x68 the place in the park, +0x6c..+0x74 the offset in the group, +0x78..+0x84 the size, +0x88..+0x90
the animated pose), then +0x250 the model in hand, +0x254 the turn, +0x258/+0x260 the cell, +0x25c the layer, +0x264/+0x26c the footprint, +0x270..+0x278 where the cursor is, +0x27c..+0x288 the extent of
the group, +0x28c..+0x2d8 the swing of the view, +0x2dc the generator, +0x2ec the base indicator is on, and from +0x2f0 forty object records of 0x4c bytes (the indicator's cells, linked in rows).

*Interface and piece library*: the interface keeps the panel position (+0, +4, +8), the layout (+0x10), the set's line and name (+0x14, +0x3c), the piece's line and name (+0x74, +0x9c), the panel
elements (+0x134, +0x13c, +0x144..+0x15c, +0x160), the selection (+0x164 which of the library's four code columns, +0x168 the library's kind, +0x16c the category, +0x170 the piece, +0x174 and +0x178 how many pieces and
categories, +0x17c and +0x180 where the category begins and how many it has), the font object (+0x184, with +0x188/+0x18c its shadow offsets), the budget bar (+0x41c..+0x42c) and the message (+0x430..+0x454).
The library is two tables in the executable: pieces of 0x25 bytes at `0x00528820` (four code bytes, a 0x20-byte name, flags: 0x80 hides the piece in the library of kind 4) and categories of 0x22 bytes at
`0x00529e38` (the first piece's code, a name, the code of the last piece used at +0x21, and the next category's first code at +0x22 ends it).

## Slice `park-editor-global` and `park-menu`

`EditorMaps_Alloc` / `EditorMaps_Free` (the editor's cell rows, 60 row pointers over a 0x3840-byte block, and ten item records, once; assertions "map already in memory" / "freed"), `EditorGlobal_Command`
(0 the editor, 1 make and throw away the park, 2 unload its PSX region, 3 save it), `EditorObjects_SetShade` (two colour bytes of the base objects move by 0x7f when the shade flag changes),
`EditorGlobal_SetParkName`. `ParkMenu_Construct` makes the list wrapper through the menu toolkit (a service); modes 0 and 4 differ in where the 50 indices come from. `CustomPark_ProcessState` is a direct
transcription: virtual calls on the list wrapper (slot 2 "done", slot 3 "the choice", slot 1 "reset") are reported services; loading reads `park%d.prk` with the file layer, saving writes it with the card
layer (`PCinit`, `PCcreate`, a seek, `PCwrite`, a close); the saved parks list copies 0x1e00 bytes of the editor's rows.

## Slice `park-editor-loop`

`CustomPark_LoadBuffer` makes the generator object with the three states of the map (1 the statics are set up, 3 the saved buffer is parsed into them, otherwise the variant of the last park is
reused), then finalizes the region and, when asked, builds the nodes of the rails; `CustomPark_Discard` is the same followed by the destructor; `LevelEditor_Run` is the frame loop (`Game_FrameTick`, input, the
editor's update, draw, flip, the clock copy, the quit combination).

## Slice `park-editor-hud`

`LevelEditor_UiDraw` writes a 7-word polygon (a darkened strip) and a 4-word header into the render arena inline (`0x00560fd0` the next byte, `0x00568628` the end), links the header into the renderer's list,
draws the lines with the font's virtual method and the budget bar with the rectangle routine. The bar's height is the fullest of three budgets (the piece limit, the 0x8c0 nodes, the zone room), drawn green below
0x77, yellow below 0x8f and red and blinking above. The selection (`SelectManager__*`) walks the two tables above; `SelectManager__SetCategory` keeps, for each category, the last piece used.

## Slice `park-editor-camera`, `park-editor-cursor`

See the layouts. Notes on the cursor: `EditorCursor_Update` centres the group on the cursor, keeps it inside the park, sets the pose of every element for the quarter (four formulae), links the elements
through their +0x20 words, lays out the base indicator (`EditorCursor_Indicator`) and marks the gap pieces; `EditorCursor_Place` refuses unless the budgets allow it, the footprint is level and free and the
colours of the neighbours match (`0x00444450` turns a piece id for a side, `0x00444510` compares two), then asks the generator to place the piece and updates the cell's layer count and, for a solid block, the
layers above it; `EditorCursor_RemoveSelected` tries once and, when nothing went, once more forcing solid blocks.

## What the decompiler got wrong

Every one of these showed up as a replay mismatch (or while reading the assembly of a function whose first cases did not cover a branch) and was settled from the disassembly (`objdump -d -M intel` of
`THawk2.exe`; the helper is `build/p411d/fn.py`): `0x00437dc0` also stores `[+0x2c0]` and `[+0x2d4]` (not in the decompile); `0x00439130` sets the indicator flag `[+0x2ec]` to 1 before its assertion
(the decompile shows it only on the other branch); `0x00439540` restores the polygon flag only inside its branch; `EditorCamera_Update` hands `0x00437580` the matrix `0x0055f7fc` and the position `0x0055f7cc`
(the decompile mixed the order up); `0x00437580` takes its angle as a dword and masks it with 0xfff; the jump tables of `0x00437dc0`, `0x00437310`, `0x00437580` and `0x00437650` hide the cases of the four
quarters; the decompile of `0x00438610` is right about the loop that empties the layers above a tall piece (the counter lives in a register), which the first captures did not show because every cell
reference they started from was already 0xffff (a stale reference in the cell is what makes the writes visible). `0x00438400` returns a float in `st0`: the probe puts a breakpoint on its `ret`, turns
`st0` into the float's bits in `eax`, and returns through `fstp st(0); ret` (a forced call has to leave the x87 stack empty or the ninth call returns the indefinite NaN).

## Preserved quirks

`SelectManager__SetLibrary` tests the *stored* second index for -1, not its argument; the budget bar divides by the limits without a guard (a zero limit would trap in retail and here); the base indicator's
assertion allows eight cells per side and then lays records out in rows of eight (`+0x2f0 + (row * 8 + column) * 0x4c`), so a footprint of nine would run its rows together; `EditorCursor_RemoveSelected`
hands `RemoveTop` a force flag whose upper 24 bits are stack leftovers (the callee masks them). Dead code kept out of the port: the constructor of the park menu turns mode 1 into 4 before its switch, so the
switch's case 1 never runs; `EditorCursor_SetModel` keeps minima of the elements' offsets that start at zero and cannot go below it because the elements' offsets were just cleared.

## Validation

Captured in the running game (the Warehouse level) by forced calls on made-up state; all cases replay with no mismatch. Counts, gcov line coverage and the committed sample are in the slice manifests.
Limits: the loop's clock words (`0x0056e31c`, `0x00568624`) are the live clock, which a timer thread moves while the call runs, so the replay does not compare them; the dispatcher paths of `engine` are not
exercised by the replay (a host binds them); a footprint past the map (a null row pointer in retail) and a text without its terminator are not generated.


# Phase 4.11e: the editor object and its loop

Source: the first half of `leveleditor.cpp`, `0x004334d0..0x0043651f`, 14 functions (12.1 KB), all native: `level_editor_object.cpp` (construct, destruct, the interface's constructor, update, draw, the budget),
`level_editor_menus.cpp` (the pad, the gap adjustment, placing and removing) and `level_editor_states.cpp` (the menus' states and the editor's modes). Probe: `re/gdb/probes/editor_specs_obj.py` (groups `eoc`
construct/destruct/budget, `eod` update and draw, `eom` pad, menus, modes, placing, removing). Slices `park-editor-object`, `park-editor-frame`, `park-editor-pad`, `park-editor-menus`.

**The object** (0x1dc bytes; it is also a Font: its own font object starts at +0x38 and the vtable at +0 is `0x00518908`, whose only slot is `0x004dd390`, called first thing by the pad). +4/+8 the theme
and version a load or "new park" asks for, +0xc/+0xd/+0xe the requests to rebuild the park (loaded, new, another theme), +0x10/+0x14 what the buffer parser reads out, +0x18/+0x1c the load and save buffers, +0x2e
"save first", +0x30 the menu wrapper on the screen, +0x34 the name entry, +0x184 the gap bitmap, +0x188 the state of the menus (0 none, 1 main, 2 save, 3 load, 4 saved parks, 5 leave, 6 theme, 7 size, 8 confirm,
9 scores, 10 save a park, 11 load a park, 12 save before leaving, 13 gap menu, 14 name, 15 points, 16 type, 17 leave the gap menu), +0x18c the state to come back to, +0x190 the park menu, +0x194 the mode (0 a piece
in hand, 1 an item, 2 and 3 a menu over them, 4 adjusting a gap, 5 the pad is out), +0x198 the mode to come back to, +0x19c the item being placed, +0x1a0/+0x1a4 the item and end under the cursor, +0x1a8 the interface,
+0x1ac the park, +0x1b0 the region's object records (`0x0056d438 + region * 0x44`), +0x1b4 the camera, +0x1b8 the cursor, +0x1bc the view rectangle, +0x1d4 the region slot, +0x1d8 the pad's repeat delay (4 frames).
The interface (0x458 bytes, made by `0x00435fa0`) gets two Font objects (the second a copy of the first, 0x14b bytes of the font record minus two bytes at +0x22), the arrow bitmaps, seven panel windows
and the help text.

**Menus.** A menu is a wrapper (`0x00472ef0`: texts, numbers, count, font, flag) over a list; the states build their arrays on the stack. The probe records the strings the text pointers name (joined by 0x1f) and the
numbers (after a 0x1e): `fcall.py` got `strings=(i, count)` for this, and a poke whose pointer argument is null now writes nothing. The wrappers, the park menu, the name entry and the gap bitmap are fake objects in the
slab whose virtual methods are unreached retail entry points (`0x004ad9b0`... as in 4.11d, with their own argument counts here).

**What the decompiler got wrong or hid** (all settled from the disassembly, `build/p411e/fn.py`): left and right share a register, so do up and down, and the second wins (`mov ecx,2`, not `or`): a held right with
left held is 2, not 3; `0x00434750` case 8 calls the list's `MenuList_Highlight` and slot 2 on the new wrapper's inner object, not the wrapper; the ids array of the type menu is the static table `0x005286fc`, not a
local; `0x00434750` case 2 writes the eight score records `"AAA "` + a zero dword at `0x0056a1fc` (decompile: a loop over `0x0056a1ff`); the pad-out test is a dword at `0x0056b144`; `0x00433cb0` reads the park's width and
depth of version v at `0x005283a4 + 8 * v`; the label passed to `SetLabel` is a six-byte structure (dword + word) whose upper half of the second dword is stack garbage (masked in the stub); `0x00433a40`'s
`EditorCursor_Scaled` is the real function (it returns a float in `st0` which the draw subtracts 2.0 from: native `cursor_scaled`).

**Preserved quirks.** In the type prompt a type number not in the table of four reads past the local array (the port prints an empty string; retail reads stack garbage; not generated). `menu_wrapper`'s
`operator_new` answering 0 is not generated (retail would crash on the next dereference).

Validation: captured in the running game by forced calls on made-up state; see the slice manifests for counts and coverage.
