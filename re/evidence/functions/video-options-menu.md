# The video options screen of vidmenu.cpp

Status: confirmed by retail forced calls (2,750 cases of the video menu, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004cdf60`/`0x004cdf70`, `0x004cdfa0`/`0x004cdfb0`, `0x004ce020`, `0x004ce030`, `0x004ce260`, `0x004ce280`, `0x004ce290`, `0x004ce2a0`, `0x004cea50` (slice `video-options-menu`; the rows' own
methods `0x004cebb0`, `0x004cec70`, `0x004ced80`, `0x004cefb0` are `video-options-window`, and the destructor `0x004cea70` is `options-level-pages`)
Native: `src/runtime/video_menu.cpp` (members of `OptionsScreen`, `src/runtime/options.hpp`); replay `src/runtime/options_retail_calls_test.cpp`; probes `re/gdb/probes/video_specs_menu.py` (groups `vmn`,
`vmnrun`); fixtures `re/evidence/fixtures/opt-calls-vmn*.txt`

## What is here

- **The start-up tables** (`0x004cdf70`, `0x004cdfb0`; each behind a one-instruction `jmp` thunk at `0x004cdf60` / `0x004cdfa0`). The first copies the default mode (`0x0052ea20`) and column (`0x0052e498`) to the
  screen's current mode (`0x0056e7ac`, `0x0056e7b0`) and clears the gamma step word (`0x0056e7b4`). The second builds the four option records the rows are made from (three dwords each, from `0x0056e7c4`): the
  renderer (text from `0x0052ea2c`, kept at `0x00548564`), the resolution (`0x0052ea30`, `0x0056e788`), the gamma (`0x0052ea34`, `0x00548570`) and the back row (`0x0052eb10`, `0x0056e7ac`); the renderer choice
  (`0x0056e7e4`) and the gamma step (`0x0056e7d8`) are cleared.
- **The gamma call** (`0x004ce260`): `fild [0x56e7d8]; fmul [0x519910]; fadd [0x51890c]; fstp` into the argument of `0x004d4120`. The forced call runs at the process default of 53 bits, so the port computes in
  doubles and rounds once to a float by the store.
- **The loop** (`0x004ce030`, with `0x004ce020` storing its answer through the second argument): the front-end flag (`0x0056114c`) is cleared, the renderer state refreshed (six engine calls, the menu flag
  `0x0056e7b8` set; when `0x004f2700` answers false it clears `0x00548568` and `0x0056e7c0`), the window built (0x3cc bytes) and the pads cleared; then, per frame: the clock tick, the engine's six per-frame
  calls, the pads cleared again once a button was seen, the pad poll (`0x004cefb0`, which stores -1, 0 or +1 in the loop's local and answers whether a button was pressed; the loop then calls the no-op
  `0x004ce280` with the choice), the window's update (virtual slot 1, which sets the done flag at `+0xe4`), and, unless finished, the window's draw (`0x004cec70`), the two flushes and the wait until the clock has
  moved two ticks. The window is deleted through its virtual destructor. A choice other than -1 logs "VIDMENU: Save Configuration", calls the two engine routines `0x004cf300` and `0x004dbe60`, switches the renderer (`0x004f6d20` with 4 when the renderer word
  `0x0056e7c0` is 0 or 8 otherwise, and the renderer choice at `0x0056e7e4`) and calls `0x004dbd90`; -1 logs "VIDMENU: Restore Old Configuration" and calls `0x004cf030` (the call the loop starts with).
- **The window** (`0x004ce2a0`, vtable `0x00519914`, 0x3cc bytes): the window base, the engine object at `+0xb0`, the rows' font at `+0xe8` and the captions' font at `+0x23c`; the background picture
  `lti\vidbk.bmp` (`+0x238`); four sprite halves (`lti\vidwin1a`, `1b`, `2a`, `2b`) at `+0x3bc..+0x3c8` added as children; two arrows (`s2arrow6.bmp` at `+0x3b4` and `s2arrow7.bmp` at `+0x3b8`: byte
  `+0xa` is 2 and 0, byte `+0xb` is 1 for both); the rows' font loaded from
  `mainf` and the captions' from `s2font2`, each copied field by field (the long tail copy is a 0x100-byte loop) and restyled; three prompt windows (`ltiBack.bmp`, `ltiSelecta.bmp`, `ltiaccept.bmp`) added as children; the rows' style
  (`MenuProperties`, `+0x3b0`); the mode names (`0x004ced80`); the list (`+0x388`, at 0x56,0x42, 500 x 240) with its four rows (`+0x38c..+0x398`, one per option record, each added to the list, given the flag word
  `0x2000` at `+4` and set to its option by `0x004cebb0`); and the rows' places (the last two at 0x56, 0x81 and 0x56, 0x8d). The constructor answers the window.
- `0x004ce290` answers whether the done flag is set; `0x004ce280` is a one-argument no-op; `0x004cea50` is the deleting destructor (it calls `0x004cea70`, then `operator delete` when bit 0 of the flag is set).

## Method

The screen lives on the options probe slab (`frontend_specs_opt.py`, 0x2b000 bytes) extended by the screen's own globals (`0x0056e780..0x0056e800`, the software-renderer word at `0x00548568`) and its read-only
data (the gamma constants, the default texts, the log lines and picture names). The window's constructor runs with the real list, row and properties constructors (native since 4.10a); every other engine call
(the picture elements, the arrows, the font loader, the prompt windows, the mode names, the option setter) is a stub whose answer is a block of the probe's arena. The loop's group (`vmnrun`) stubs the
window's constructor and virtual calls as seams: its update writes the done flag (the stub's write is scripted, like the clock's), the pad poll pokes a direction through the loop's local and answers a
button, so both exits (a choice and -1) are reached.

## Lessons

(1) The pad poll is a thiscall with a pointer to the loop's stack local: the stub records the pointer as the fixed scratch address, and the port keeps the local there. The poke is scripted before the answer.
(2) The constructor calls the mode-name builder twice, once before the list exists and once after the rows are placed; both calls are services here, so only their order and arguments are compared.
