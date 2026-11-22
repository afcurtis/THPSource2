# The credits roll (credits.cpp)

Status: native port of the roll's manager, its three record classes and its static fonts, replayed against retail forced calls;
the roll's loop was ported in Phase 4.10c and replayed in Phase 4.10i3 ([screen-loops.md](screen-loops.md); method in [frontend-text-layer.md](frontend-text-layer.md#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/credits.{hpp,cpp}`; fixture `re/evidence/fixtures/menu-calls-credits.txt`
Capture tooling: `re/gdb/probes/frontend_calls.py` with the group `credits` of `frontend_specs_credits.py`

The attribution gave `credits.cpp` seven functions; the file is 0x00426430..0x004270f0: 30 functions (four vtable-only methods and the
static initialiser thunks Ghidra does not list). The functions after `0x004271a0` that the Ghidra list shows in the same range
(`CreateEditor` and its constructor) belong to the create-a-skater editor.

## The roll

`Credits_Run` takes a pointer to a mode (0 credits, 1 music credits), loads two fonts, builds the manager from `credits.txt` or
`music.txt` and loops: advance the timeline, update the records, draw the background sprite, wait two ticks, until a pad edge (`0x0056b029`,
`0x0056aff9`, `0x0056b0d9`) or the end of the roll. The manager (0xff4 bytes) lives in the loop's stack frame: +0 the scroll position (fixed
point, the draw divides by four), +4 up to 1000 record pointers, +0xfa4 how many, +0xfa8 the total height, +0xfac the tick of the last
advance. The timeline moves by the ticks since the last call (four times as many while the fast-forward byte `0x0056b0a8` is set) and
ends when the position passes the total height.

**The file** is parsed a line at a time. A line starts with tags: `@B` plus one character (a bitmap: the rest of the line is the file),
`@F` plus two (the second font), `@M` and two digits separated by one character (a music marker); the text runs to the carriage return,
at most 0x31 characters; a line starting with `#` ends the file (that line is dropped). The tag's handler is picked through a byte table
over `B..m` at `0x00426d58` (upper and lower case); an unknown tag asserts. `LoadText` copies the two global fonts (`0x00560d08`,
`0x00560e58`) from `s2font2.fnt` and `s2font1.fnt` and sets their alignment and scale, opens the file through the engine, builds one record
per line and places each at the running offset (`SetOffset` returns the record's height).

**Records** (vtable at +0, +4 the offset, +8 the height): *text* (0x44 bytes, the text copied at +0xc, the font at +0x40, height the font's
extent plus 2; an empty line measures the string `?`), drawn at x = 0x100 when it is on screen (`0 < offset - position + height` and
`offset - position < 0x100`); *bitmap* (0x10 bytes, a sprite of the file, height the sprite's plus 2), drawn the same way; *music marker* (0x18 bytes,
two track numbers, a state): at 256 rows away it fades the music, then waits for the current music to stop and starts its track.

## Validation

Forced calls on a manager and records built in a scratch slab: the file is poked at the start of the allocation arena (the first allocation
`LoadText` makes, whose size the stubbed file opener answers), the records are allocated from the arena as the original does, and
the records' virtual methods go through fake vtables or, in some cases, their real ones (so the native dispatch runs the records' own
methods). The static initialiser thunks end in tail jumps to the font constructor and destructor: calling them straight from the
debugger needed `fcall.py` to return a stub through a real `ret` (the debugger steps over a breakpoint at the dummy-call return address).

`Credits_Run` was ported from the disassembly and unit tested in 4.10c; 4.10i3 replays it ([screen-loops.md](screen-loops.md)) with the clock and the pad edge bytes scripted. Its manager and
background sprite object are in its stack frame in retail: the services that receive them record fixed scratch addresses (`stack_to`) and the native frame is outside the compared memory.
