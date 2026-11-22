# The title screen and the memory-card scan screen (mainmenu.cpp)

Status: native port of the title ("main menu") screen's window, its wheel and the memory-card scan window, replayed against retail
forced calls; the screens' loops were ported and unit tested in Phase 4.10c and replayed in Phase 4.10i3 ([screen-loops.md](screen-loops.md); method in
[frontend-text-layer.md](frontend-text-layer.md#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/mainmenu.{hpp,cpp}` and `mainmenu_screens.cpp`; fixture `re/evidence/fixtures/menu-calls-main.txt`
Capture tooling: `re/gdb/probes/frontend_calls.py` with the group `mainmenu` of `frontend_specs_mainmenu.py`

`mainmenu.cpp` is not the three functions the attribution table lists: they are the asserting ones. The file runs from
`0x0046aee0` (the wrapper of the screen's function) to `0x0046c460` (the card scan screen's loop): the title window class
(constructor `0x0046b0a0`, 1.1 KB), the wheel class and the card window class, and the two loops, 25 functions, 3 of them
vtable-only (`0x0046b690`, `0x0046ba50`, `0x0046c120`) and 3 small thunks (`0x0046be60`, `0x0046be80`, `0x0046be90`) Ghidra does not list.

## The title window (0x368 bytes, vtable 0x00518d78, a Window)

+0xac and +0x20c two embedded fonts (s2font1 and s2font2, copied from the loaded fonts), +0x1f8 the state (1 while the card
scan window is up), +0x200 / +0x360 / +0x364 three prompt windows (the engine builds them from bitmap names), +0x204 the
background sprite object (raw `operator new`, the file opened by the engine, a three-argument constructor), +0x208 the wheel, +0x35c
the card window. The constructor builds the wheel (`0x0046b870`) and the prompts and, when the byte at `0x0052cb3c` says a scan is
pending, the card window. `Draw` hides the prompts during a scan and draws the card window, the window and the background; `Update`
(virtual slot 1) updates the card window first (and clears both pads' triggers); `Poll` (called by the loop, not virtual) reads
the pads: a confirm edge on pad `p` while the wheel stands on entry `i` picks the word at `0x00534508 + 4 * (3 * i + p)` (-1 plays
the "no" sound and goes on), and during a scan it waits for the card window's result.

## The wheel (0xd0 bytes, vtable 0x00518d98)

A sprite (+0xac) drawn rotated by an angle (+0xb0, 4096 to the turn) toward a target (+0xb4); seven entries in rows of three words
at `0x00534504` (the angle, then what pad 0 and pad 1 choose); +0xcc the selected entry, remembered in the byte `0x00533e98`
between visits; +0xbc the entry nearest the angle and +0xc0 a flag, which together decide how the palette is tinted. `Update`
steps the angle toward the target by 0x80 (further than 0x258 away), 0x40 (further than 0x40) or exactly, wrapping at 0x1000, and a
confirm edge of either pad moves the selection by one with the sound 0xe9. `Tint` copies the palette and brightens one slice of
eight colours (a bright green-blue band for the highlighted slice) or halves the colours of a range when the second pad is
missing, then hands the buffer to the sprite.

## The memory-card scan window (0xd60 bytes, vtable 0x00518db8)

An embedded font (+0xb0), the engine's text effect object (+0x1fc, a service), a text box (+0xd50) and the state (+0xd48 what the effect
reports, +0xd4c the screen's own: 9 reading, 0xb done, +0xd54 the finished flag, +0xd58 a countdown, +0xd5c a timeout). `Update` polls
the effect through a pointer to +0xd48, starts the read when the effect goes idle (the formatted text is written to a buffer of the
original's frame and handed to the text box), and later copies the card's 0x1dc4 bytes to `0x00568a68`; `Draw` ticks the countdown.

## The loops

`MainMenu_Main` (`0x0046aef0`), `CardScreen_Main` (`0x0046c460`) and the two wrappers that store their result through a pointer
(`0x0046aee0`, `0x0046be90`) loop until the player answers. Phase 4.10c could only port them from the disassembly and unit test them (`menu_test.cpp`: the quit path, the wheel's pick):
a forced-call stub could not change what they wait on. **Phase 4.10i3 replays them** (slice `frontend-screen-loops`, complete) with stubs that write the clock and the pad edge
bytes; see [screen-loops.md](screen-loops.md).

Fakes used by the probe: the title and card windows get fake vtables whose slot 7 is the window base's `element_hook` (the real classes'
hook, `0x004dd140`, `0x004dcf80`, is an engine function that reads debug keys); the card window's real vtable is used in some `Poll` cases
so the native dispatch runs `CardWindow_Delete`.
