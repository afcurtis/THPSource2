# The three screen loops: `MainMenu_Main`, `CardScreen_Main` and `Credits_Run` (Phase 4.10i3)

Source: `mainmenu.cpp` (`0x0046aee0..0x0046c600`) and `credits.cpp` (`0x00426430`). Native: `src/runtime/mainmenu.{hpp,cpp}` + `mainmenu_screens.cpp` (`MainMenu`), `credits.{hpp,cpp}` (`Credits`). Probe:
`re/gdb/probes/frontend_specs_loops.py` (groups `mloop`, `cloop`); fixtures `re/evidence/fixtures/menu-calls-loops-*.txt`; replay `src/runtime/menu_retail_calls_test.cpp`.
Slice `frontend-screen-loops` (`active` since 4.10c, closed here): five functions, `MainMenu_Main` (`0x0046aef0`) and its wrapper (`0x0046aee0`), `CardScreen_Main` (`0x0046c460`) and its
wrapper (`0x0046be90`), `Credits_Run` (`0x00426430`).

## What the loops are

Every screen loop of the front end has the same frame: *tick* (`Game_FrameTick` `0x004f7ce0` advances the simulation clock `0x0056e31c`), *poll* (`0x004f5ff0` rewrites the pad bytes), the
engine's per-frame calls (`0x004dae00`, `0x00418150`, `0x004304b0`, `0x00489a10`...), the screen's own tests, its update and draw, a vertical sync (`0x004e8740`, `0x004c9300`) and a wait until two ticks
have passed since the last frame (`0x00568624`), then `0x00430510` and `0x00466c00`. What differs is how a loop ends:

| Loop | Ends when | Answer |
|---|---|---|
| `MainMenu_Main` | the quit key is down (`0x004e41b0(0x10)` or the flag `0x006a1415`: `0xff`), the title window's poll takes a pad press (the wheel entry that pad picks, `-1` is a refusal and the loop goes on), or the quit combination `Front_QuitCombo` is held | the wheel entry, `0xff`, or `-1` |
| `CardScreen_Main` | the scan window's result word (`+0xd54`) is 1 or 2, or the quit combination | `-1` only for 2 (`setne al; dec eax`), else 0 |
| `Credits_Run` | a confirm, back or other-button edge of pad 0, or the timeline passing its end | nothing |

The title window is built by the loop (0x368 bytes, the wheel and its arrow, three prompt windows, the card window when the scan flag is up) and deleted on the way out; the card loop builds the
card window (0xd60 bytes); the roll builds its manager (0xff4 bytes) and the background sprite object in its own stack frame.

## How the loops are replayed

The 4.10c port could only be unit tested because the forced-call stubs could not change what the loops wait on (the clock and the pad). Since 4.10e a stub can write memory (`writes`): here
`Game_FrameTick` writes the clock (a step of one to seven ticks, now and then a jump of hundreds to thousands) and the input poll writes the six pad edge bytes (zero, except that on the case's chosen
frame one of them is 1); the quit tests answer from a frame counter. A case therefore runs one to nine frames and ends the way it was scripted. The native code applies the same scripted writes
after the two calls (`MainMenu::frame_tick`/`poll_input`, `Credits::frame_tick`/`poll_input`) the way `ScreenKit::game_frame_tick` already did.

The retail constructors and destructors run inside the forced call, so the windows have their real vtables: the native code runs the wheel, the sprite elements and the card window natively
(`MainMenu::dispatch_named`, offered to `WindowBase::set_dispatch`) and reports the prompt windows (stub-built) and the engine's per-frame hooks as services. Hooks of the real classes are
engine functions (`0x004dd140` title window, `0x004dcf80` wheel, `0x004dc9f0` card window, `0x004dca60` text elements, `0x004dcb30` sprite elements) and are stubbed as `element_hook`.

## What the replay found

* **`CardScreen_Main` returned the wrong answer.** The port (written from the decompile) returned `-1` unless the scan finished with 2; the disassembly (`xor eax,eax; cmp edi,2; setne al; dec eax`)
  returns `-1` *only* for 2. The unit test had the same inversion.
* **The wheel's arrow is a real sprite element** (`SpriteScreenElement`, built by `0x004acbe0` with its own sprite object), so the window base's calls on it run the element code, not a service:
  its update is the element update, its draw reaches the sprite object's draw, its deleting destructor deletes the sprite object first.
* **Credits mode 2 is undefined.** The roll names its two text files in a two-entry local array indexed by the mode; the check `mode in 0..1` only logs, and mode 2 reads the stack leftover
  after the array (a stack address). The probe keeps the mode in 0..1.
* A loop waits for the clock to pass the last frame's clock plus two; a random pair of clocks makes it wait up to a hundred thousand ticks (a stub call each): the cases keep the last clock within a
  few ticks of the clock.
* The stack objects of `Credits_Run` (the background sprite object, the manager's parse buffer) are reported at fixed scratch addresses (`stack_to`), like the strings the other screens build in locals.
