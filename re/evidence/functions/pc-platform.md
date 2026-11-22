# The PC platform layer: the pad, the PC input bindings, the options file, the custom park files and the render glue (Phase 4.13a)

Source: `pad.cpp` (`0x004894a0..0x00489ec0`), `pc\pcinput.cpp` (`0x004e3e80..0x004e5720` outside the DirectInput wrapper), `pc\winmain.cpp` (`0x004f6660..0x004f86f0` and the video options at
`0x004cf030..0x004cf420`), the video options window (`vidmenu`, `0x004cebb0..0x004cf030`), `pc\thstub.cpp` and the PC file backends (`0x004e74f0..0x004e7c50`, `0x004f6140..0x004f6400`),
the game-logic helpers of `pc\d3dfunc.cpp`, `d3dpoly.cpp`, `d3dtex.cpp`, `d3dtimer.cpp` and the SDX mouse wrapper's bounds. Native: `src/runtime/pad_input.{hpp,cpp}` (`PadInput`),
`src/runtime/pc_system.{hpp,cpp}` (`PcSystem`) and `src/runtime/pc_platform.hpp` (the service conventions, the C runtime pieces `crt::format`, `crt::scan_int`, `crt::Strtok`, `crt::equal_nocase`).
**85 functions** in nine slices (`pad-input` 13, `pc-input-keys` 13, `pc-input-bindings` 4, `pc-winmain-options` 11, `video-options-window` 5, `pc-custom-park-files` 8, `pc-render-glue` 16,
`pc-texture-glue` 9, `pc-timers-and-small` 7), 3 functions tagged out of scope, 2 re-homed to 4.15e (`d3d-fog-tables`, `d3d-sprite-quads`, both `planned`).

## What the code is

**The control system (`pad.cpp`)** keeps two 0x16c-byte player blocks from `0x0056aff8`: twenty 16-byte *action records* (`+0` held, `+1` pressed latch, `+4` frames held, `+8` frames released,
`+0xc` frames since the latch), the rumble records at `+0x154..`, a table of four record pointers at `+0x15c` and, for player 0 only, the four analog bytes at `0x0056b140`. `Input_UpdateActionStates`
(per frame) polls the devices (`0x00489cc0`, a jump to `PCInput_Poll`), stops the rumble of a pad that cannot rumble, scales each analog pad's four samples with `Pad_Analog` (dead zone of 32 around
the centre, -127..127, outside it the attract-mode count `0x0056b2e4` is cleared) and advances the sixteen records of each player from that player's half of the pending mask (`0x006a3f1c`): the eight
buttons, four directions (a mask bit or the stick past +-40) and four more buttons. A press (`Input_AdvanceActionState`) resets the attract timer through `0x00466b60`. In the head-to-head mode
(`0x00533f3c == 1` with a player count) `Pad_Remap` / `Pad_Restore` swap the left/right and the +0x40 / +0x60 records so that the players swap sides.

**The PC bindings (`pcinput.cpp`).** A binding is a chord of up to three key codes. Fourteen pointers at `0x0054adcc` name the chord of each action (jump, grind, grab, kick, spin left / right, nollie,
switch, the four directions, pause, camera) inside one of three chord sets (the game's at `0x0054ad94`, two menu sets at `0x0054ad20` and `0x0054ad5c`; `PCInput_SetControlMode` chooses). The joystick
buttons are numbers 0..31 (-1: none) at `0x0054ae04..0x0054ae24` and `0x006a6984`. `PCInput_BuildActionMask` asks the DirectInput wrapper (`0x004ec6b0`) whether each chord is down, adds the first
stick as a d-pad (past +-500) and the joystick buttons, applies the pause latch and the one-shot `0x006a696c` word and stores the mask at `0x006a3f1c` + the active port; the mouse position (GetCursorPos)
goes to the mouse tracker. `PCInput_LoadBindings` / `SaveBindings` read and write them in the options file (JOYPAD: ten numbers, KEYBOARD: fourteen comma-separated key-name lists); a bad file (a
number below -1 or above 32, two actions on one button, an unknown key name or a key another action uses) puts the defaults back and writes them.

**The options file and the install (`winmain.cpp`).** `WINMAIN_LoadOptions` truncates a file of another version (fopen / fclose of the .INI), writes the version, reads the VIDEO section
(`VideoOptions_Load`: renderer Hardware / Software, resolution by name, gamma -15..15, texture quality, fog scale as a percentage) and the bindings; `WINMAIN_InstallSetup` finds the data, music
and movie folders (the file's INSTALL entries, else the install folder, else the disc's) by probing for `All.pkr`, `LTIX30.dat` and `Intro.dat`; `WINMAIN_BuildDisplayModeList` lists the
resolutions `WINMAIN_CanDoMode` accepts (DirectDraw listed it and two buffers' worth fit the video memory). The video options window (`VideoWin_*`) edits the rows (renderer, resolution, gamma).

**The custom parks and the file backends.** `THSTUB_SaveCustomLevel` / `Delete` / `Load` build `<install>\[!]name.PRK` (the `!` folder unless the name already starts with one) and use `PCcreate` /
`PCwrite` / `PCremove` over ten file slots (state byte 0 free, 1 package, 2 plain file; FILE pointers at `0x006a6d1c`).

**The render glue.** Small pure helpers (`D3D_BitCount`, `D3D_LowestBit`: pixel-format masks), the viewport rectangle (`D3D_SubmitViewportRect`: PlayStation 512x240 space to the screen, with an
unsigned divide by 240 and a *logical* shift for x), the begin-scene frame limiter (`D3D_BeginScene` sleeps until two simulation ticks passed), the reversed ordering-table list (`D3D_ReversePush`,
`D3D_BuildReverseCommandList`, `D3D_InitListHeap`), the gamma tables (`D3D_BuildGammaTable`: `255 * (i/255)^(1/gamma)` with the x87 `pow` intrinsic), the polygon extent test, the three solid-rectangle
wrappers, the polygon heap, the software frame copy, and the texture record glue (`D3DTex_*`: destroy all, load, prepare from a file's BMP header, create for supplied pixels, attach to a material).

## Triage of the d3d remainders (the plan's first task)

| Function | Verdict |
|---|---|
| `0x004cf9b0` DirectSound error text, `0x004cfb60` DirectDraw / Direct3D error text (2,173 bytes), `0x004d09d0` register / memory dump | out of scope (`scope.yml`, `platform-hal`): log text only, no game state |
| `0x004d3f50` `setupFog` (four 256 KB fog tables) | re-homed to 4.15e, slice `d3d-fog-tables` (planned): a byte diff of 1 MB per case does not fit the fixture format; it needs a checksum compare |
| `0x004d7990` sprite quad builder | re-homed to 4.15e, slice `d3d-sprite-quads` (planned): it runs in the 24-bit x87 render window, forced calls run at 53 bits |
| everything else in the list | ported and replayed (slices above) |

Not counted (Ghidra lists no function): `0x004895a0` (two PSX pad calls), `0x00489cc0` (the poll thunk), `0x004e4200` (wait for a key), `0x004e4240` / `0x004e4c30` (return 0),
`0x004e4b40` (a cheat-code key buffer): none has a caller, so they are dead and were not ported.

## Findings

1. **Bug-faithful behaviours the replay pinned:** `Pad_Clear` always clears *player 0's* analog bytes whatever block it clears; `Pad_ActuatorOn`'s package-state branch and `PCwrite`'s state-1 branch
   return a stack word (never generated); `PCInput_BuildActionMask` reads the one-shot word as a *dword* (`0x006a696c`, so the two bytes after it are ORed into the mask); the key-conflict check of
   `LoadBindings` compares part *k* only with bytes *other than k* of the other actions' chords.
2. **The launch helpers keep breakpoints of their own on the poll code.** A breakpoint at `PCInput_BuildActionMask` (the key-forcing helper) rewrote the Enter key when the forced call entered the
   function: 3 of 16 cases wrote a byte the native port never would. The probe disables any breakpoint at `0x004e42c0`, `0x004e4a90` or `0x004e41b0`.
3. **The simulation clock moves while a case runs.** A timer thread writes the clock words (`0x0056e31c`, `0x0056e320`, `0x006a05ac`); the probe stubs the timer callback (`0x004dace0`) for the
   duration of a case (the stub only skips the callback while a forced call is active).
4. **The x87 precision of a forced call is 53 bits** (the physics-frame breakpoint is outside the render window): the frame-rate meter and the fog percentage are double math; the gamma power is
   `powl` rounded to double. In-game the renderer runs at 24 bits (`D3DPOLY_EndScene` restores the control word), which is why the two functions that run there stay planned.
5. **`LoadBindings` is a good example of the C runtime seam:** `sprintf` / `sscanf` / `strtok` / `stricmp` run for real in retail and are reproduced natively; the options file is a service whose text
   the probe scripts. `sscanf("%d")` of an unparseable string leaves its stack local as it was, so the probe never makes one.
6. **Virtual calls on window rows** (the video options window) are reported by the slot's function address; the probe uses the unreached retail entry points of `pspool_specs.FAKE_SLOTS` for fake
   vtables, as the park editor does.

## Validation

Retail forced calls in the running game (`re/gdb/probes/frontend_calls.py` with the groups `padc`, `pcin`, `pcbind`, `winm`, `vopt`, `vwin`, `thst`, `d3dm` of `platform_specs.py` and
`platform_specs_sys.py`): a legal random state is poked into the game's own globals (windows of the real address space) and a scratch slab, the retail function is called, and the return value,
every written byte and the ordered service calls are recorded and replayed natively (`platform_retail_calls_test.cpp`). `fcall.py` gained `edx=` (a fastcall's second register argument).
The committed fixtures `platform-calls-*.txt` keep the first 30 cases of each capture per function; the full captures are in `build/p413a` (not committed). Counts are in the slice manifests.

**Result (2026-10-17):** 87 native functions, 16,100 replayed forced-call cases in the full captures (`padc` 3,900 including the pad, `pcin` 2,250, `pcbind` 600, `winm` 1,350, `vopt` 600, `vwin` 1,000, `thst` 1,600, `d3dm` 4,800), 0 mismatches; gcov
of `pad_input.cpp` 408 / 413 lines and `pc_system.cpp` 640 / 647 (the rest are guards the retail game cannot reach in a legal state: the division-by-zero throws, the clock and texture-list watchdogs, the full
file table, the `PCwrite` state-1 branch that returns a stack word). The committed fixtures hold 2,610 cases (30 per function). Late findings: (a) `THSTUB_LoadCustomLevel` returns the failed handle with only `AL`
cleared (`0xffffff00`), not 0; (b) the `D3D_BeginScene` generator must never put the last-scene tick below zero after the offset: the retail loop compares unsigned and a wrapped value waits four billion ticks (the capture hung there once).
