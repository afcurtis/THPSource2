# Trick recognition and the TRICKS.BIN script interpreter (Phase 4.7a)

Status: confirmed for the per-frame update, the button-history recognition chain, the delayed-start
queue and every script opcode the retail captures exercised (see Validation); the branches no capture
reached are disassembly-derived and unit tested, and are listed under Not traced.
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00492ea0` (`TrickScript_Update`), `0x00492190` (`TrickInput_RefreshHistory`),
`0x00492120` (`TrickInput_DerivePattern`), `0x00491c90` (`TrickInput_RecordState`),
`0x00492560` (`TrickInput_MatchHistory`), `0x00492030` (history consume), `0x004925e0`
(`TrickInput_SelectSequence`), `0x00492290` (`TrickScript_QueueStart`), `0x00492400`
(`TrickScript_StartPending`), `0x00491b80` (`TrickScript_Start`), `0x004924f0` (release query),
`0x004be450` (`TrickScript_Execute`), `0x004be3b0`/`0x004be3c0`/`0x004be3e0` (byte, word and frame-operand
helpers)
Native: `src/runtime/trick_script.{hpp,cpp}` (interpreter, `trick_script_execute`),
`src/runtime/trick_recognition.cpp` (`trick_script_update` and the chain under it),
`src/runtime/retail_image.hpp` (the raw skater image)

This page extends [bin-runtime.md](bin-runtime.md), which recovered the file format, the per-player key
table and the opcode table. Three entries of that table were wrong (see Corrections); the rest holds.

## One frame of trick code

`TrickScript_Update(skater)` runs, in order:

1. `TrickInput_RefreshHistory`: derive the composite direction pattern (`0x00492120`) and publish button
   states 1-8 (pattern equals the state number) and 9, 10, 11, 12, 14, 16 (controller bytes `+0x20`,
   `+0x10`, `+0x30`, `+0x00`, `+0x50`, `+0x70`) through `TrickInput_RecordState`. A state that changed
   writes the next 8-byte ring record `{action, pressed, u32 clock}` at `+0x2a14 + 8*index` and advances
   the write index at `+0x2b14` modulo 32; the per-button latches are at `+0x2b18`.
2. `TrickInput_SelectSequence`, unless `+0x2f64` (script blocked) is nonzero: walk the player's generated
   key table and queue the first entry the history satisfies.
3. `TrickScript_StartPending`: start the oldest queued script when none is active.
4. If `+0x29c8` is set, `TrickScript_Execute`.

Direction pattern: `index = +0xa0 + 2*(+0xb0) + 4*(+0x80) + 8*(+0x90)` (controller bytes Up, Down, Left,
Right), with 1/2 or-ed in when the signed lean byte `+0x31a2` is below/above
`+-0x28` and 4/8 for `+0x31a1`. `DAT_005369c8[index]` = `0 1 2 2 3 5 7 7 4 6 8 8 4 6 8 8`. So Up is
pattern 1, Down 2, Left 3, Right 4, Left+Up 5, Right+Up 6, Left+Down 7, Right+Down 8. Button numbers:
9 Circle, 10 Square, 11 Cross, 12 Triangle, 14 L2, 16 R2 (controller records `+0x20`, `+0x10`, `+0x30`,
`+0x00`, `+0x50`, `+0x70`).

## Selection (`0x004925e0`)

The key table entry is `[count][code * count][stream-relative s16][flags u16]`; a zero count ends it. Flags:
low 10 bits = the longest age (in clock ticks) a press may have, `0x0400` = ignore buttons 5-8 while
matching, `0x2000` = run in any physics state (otherwise only raw states 1, 2 and 3), `0x4000` = the
"delayed" grind form, `0x8000` = only while the special meter (`+0x2f70`) is above 0xfff.

`TrickInput_MatchHistory(start, direction, age, stop, skip)` walks the ring backwards from `start`
(`direction` 0 includes `start`), ignores empty records (and buttons 5-8 when `skip`), returns -1 as soon as
a record is older than `age`, and returns the first record whose pressed byte is nonzero. Two forms:

- **Direction pattern then button** (count 2, first code 1-8, second not 1-8): find the newest press and the
  one before it, and accept `pattern + button` in either order, or the pattern still held (the live
  `DerivePattern` equal to the first code) with the button pressed. Unless the flags carry `0x2000` the
  matched presses are consumed (`0x00492030` zeroes the newest record with that action and state).
- **Any other sequence**: the newest press must be the last code and the one before the second last (or the
  swapped order when the two are of different kinds); each earlier code then has to follow in the history.
  A complete match queues and returns, ending the walk, whereas the first form continues to the next entry.
  For a one-code entry the "second last" code is the count word itself (retail reads it that way).

`TrickScript_QueueStart(script, action, timestamp, flag4000, flag2000, flag8000)` does nothing when the
ten-entry queue is full. Action 12 (Triangle) stores the request in `+0x29dc/+0x2be0/+0x29e4` for the grind
path (with `0x4000` the expiry is `timestamp + 0x3c`, clamped to `clock - 1`). Other entries without `0x2000`
append `{script, clock, action, timestamp}` to the queue at `+0x2b38` (count `+0x2bd8`, head `+0x2bdc`).
With `0x2000` the script is stored directly (`+0x29e0`, `+0x2be4`) unless physics state 2 or a script mode
(`+0x2dd4`) is active, calling `CBruce__MaybeManual(0, 1)` from states 0 and 7.

`TrickScript_StartPending` needs no active script (`+0x2c68`), no block (`+0x2f64`) and a queued entry,
drops the whole queue outside raw states 1-3, discards entries older than `0xb4` ticks, and otherwise
records the selected action and time (`+0x2a0c`, `+0x2a10`), clears history records pressed before that
time, and calls `TrickScript_Start`, which resets the cursor state (`+0x29cc` script pointer, `+0x2c68 = 1`,
`+0x29c8 = 1`, `+0x108 = 0x10000`, name `"Trick"` at `+0x29c4`, the gosub/loop pointers and counters).

## Interpreter corrections to bin-runtime.md

- `0x43` branches when bit `0x02` of `+0xd8` is **set** (the table said clear).
- `0x16` is not a rewind: it returns to the frame loop with the cursor already past the opcode. (`0x1a` and
  `0x2c` rewind one byte, `0x0d`/`0x10`/`0x11` three.)
- `0x3a` branches when bit 1 of `+0xd8` differs from byte `+0x163`.
- `0x1e` calls `CBruce__Bail(0, 0)` (`0x004904d0`), not `0x004904d0(0,0)` "state reset"; it recursively
  restarts and re-executes a state script, so it is a service seam here.
- `0x31` clears `+0x2f64/+0x2c10`, sets `+0x2f2c = 4`, picks one of two animation requests from `+0x2dd8` and
  the controller byte `+0x10`, zeroes the motion vector and then falls through into `0x32`'s call.
  `0x32` calls `0x0046c630` and `0x33` calls `0x0046c5f0`.
- `0x49` ends by clearing `+0x2c68` like `0x20`, after moving the position by `0x1e * (+0x310c)`.
- An undefined command (bytes `0x04-0x06`, `0x08`, `0x00` and `>= 0x5a`) logs, fails the assertion at
  `0x004011e0` and clears `+0x2c68`, then continues with the next byte.

## Services (seams)

Calls the trick code makes to code outside 4.7a are reported in order to a services object, with the
retail stack words. In the retail replay each is a seam: the call and its arguments are compared, and its
own effect on the skater image is applied from the trace. They are Animation requests (`0x004903f0`,
`0x00490420`, `0x00490450`, `0x00490480`), `CBruce__StateTransition` (`0x004900b0`),
`TrickScore_AdvancePointTimer` (`0x0048f720`), `CBruce__Bail`, `CBruce__Trick_TakeOff` (`0x0048f5f0`),
`CBruce__Trick_Land` (`0x0048f8f0`), `Gap_ApplyDefinitionState` (`0x0048f690`), the board calls, `SFX_PlayX`
(`0x004ad620`), `CBruce__Vibrate` (`0x0048ffc0`), the rail restart (`0x00490c70`), `0x0046dd20`, `0x00431260`,
`CBruce__MaybeManual` (`0x00497bb0`), `Rnd` (`0x004c9340`) and the assertion at `0x004011e0` (a passing
check is invisible, a failing one is reported). Their bodies belong to later slices (animation, scoring
`trick-score-stack`, sound).

## Validation

Method: a GDB probe (`build/p47/trick_trace.py`, untracked scratch) hooks `TrickScript_Update` for the
primary skater in Warehouse and logs the whole 0x3538-byte skater image and the controller record at entry,
at the depth-0 `TrickScript_Execute` entry and return, and at exit, plus every service call with its stack
words, its image before and after, and its return value. Calls a service makes internally (including
`CBruce__Bail` restarting and re-running a script) are opaque. `scripts/make_trick_trace_fixture.py` writes
the compact fixture `re/evidence/fixtures/trick-warehouse.txt`; `src/runtime/trick_retail_trace_test.cpp`
replays each update from its raw entry image and requires, word for word over all 0x3538 bytes:

1. the image at the Execute entry (or at the exit when no script ran) after refresh, selection and
   pending start,
2. the same service calls with the same arguments in the same order,
3. the image at the Execute return and at the update exit,

and then replays every Execute again on its own from retail's Execute-entry image. A service's own effect on
the skater is applied from the trace (a seam), so its body is not part of this claim. Changing one recorded
service argument makes the replay fail (`tests/test_trick_script.py`).

Result over four Warehouse recordings (3,363 updates, 849 of them kept in the fixture: every update that ran
a script, made a call or changed trick state, plus every 25th quiet one): 625 script executions and 198
service calls replay exactly, 0 mismatches, 40 distinct opcodes. The recordings were a natural ollie
(hold Cross, then a flip; 900 frames), a forced launch (state 2 with an ollie-like upward speed, controlled
injection, see below) cycling all eight direction patterns with Square and Circle and every other button
(1,250 frames plus a partial second run), and a targeted run of double-tap and special-meter sequences
(500 frames) that retail declined to start, which the replay reproduces as non-events. Tricks that started:
Kickflip, HeelFlip, Pop Shove It, FS Shove It, Varial Kickflip, Kickflip to Indy, Stalefish, Tailgrab,
Crossbone, Nosegrab, Indy Nosebone, Judo, and two state scripts started by `Bail` and the landing.

Findings that changed the native code or the notes: the three corrected opcodes above, the ring position of
the one-code "second last" code (retail reads the count word), that a pattern-then-button match continues to
the next table entry while a longer sequence ends the walk, that `TrickScript_QueueStart`'s fourth
argument is the *timestamp* of the matched press (the queue stores it at `+0x0c`), and that a wait opcode
(`0x0d`, `0x10`, `0x11`, `0x1a`, `0x2c`) leaves the cursor on itself but `0x16` does not.

Controlled injection: the natural ollie needs about 100 frames of hold, launch and landing per trick and
the capture runs at a few frames per second under Wine and GDB, so the forced launch writes `+0x30b8 = 2`,
`+0x50 = -93552` and lifts `+0xc` by 150 units, then keeps state 2 until a script starts. Every image the
replay checks is a real retail state at a real retail call boundary; only the way the skater got there is
scripted.

## Not traced

The following are ported from the disassembly and covered only by hand-built cases (or not at all); none
of the four recordings reached them:

- Opcodes `0e 10 11 17 18 19 1a 1b 1c 1e 1f 22 23 24-28 29 2a 2b 2c 34 35 3a 3b 3f 41 43 48 4a 4d 4f 50 52-59`
  (grind, manual, plant and spin scripts: the recordings started none of them). `0x42` and `0x49` are
  ported (`0x49` is unit tested) and `0x21` (basis rotation from a sine table and the game's matrix
  register) is **not** ported: it raises `TrickScriptFault`.
- Selection of three-code sequences and of `0x2000`, `0x4000` and `0x8000` entries: no recording
  queued one, so only the pattern-then-button branch and rejections are retail-validated; the rest is
  unit tested.
- The vector math of `0x47`/`0x4b` uses the existing exact-integer dot product while retail computes it
  through the x87 at 24-bit precision; the traced values agree, borderline cases are unverified.
- Manual and grind starts are made elsewhere (`+0x29e0` and the action-12 request are consumed by the
  physics frame), and `TrickScript_QueueStart`'s grind branch was recorded without a rail.
- The service bodies (`Bail`, `TakeOff`, `Land`, the point timer, animation and sound requests) belong
  to later slices; `trick-score-stack` should take `0x0048f720`, `0x0048d710`, `0x0048cbc0`,
  `0x0048cd40` and `0x0048e680`.
