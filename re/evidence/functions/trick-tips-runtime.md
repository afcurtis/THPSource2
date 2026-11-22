# In-game trick tips (tricktips.cpp)

Status: trace-validated against retail (Phase 4.7b), with controlled input; see "Driven input".
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/trick_tips.{hpp,cpp}`, generated `src/runtime/trick_tip_table.cpp`
Fixture: `re/evidence/fixtures/tricktips-warehouse.txt` (generator `scripts/make_trick_tips_fixture.py`)
Replay: `src/runtime/trick_tips_retail_trace_test.cpp`; hand-built cases `trick_tips_test.cpp`

The tips are the short hints ("TIP: HOLD JUMP TO CROUCH AND GO FASTER") that slide onto the screen during
play. `CTrickTipMonitor` (276 bytes, `re/xref/types.yml`) is one static object at `0x0056db98`; its methods
are thiscall on that address and a few globals around it (`0x0056a40c` tip masks, `0x0056a46c` tips option).

## Functions

| Address | Name | Role |
|---|---|---|
| `0x004c4570` | TrickTips_Reset | level start (`0x004691f9`): zero the counters, set the byte at `+0x110` to 1 |
| `0x004c4620` | TrickTips_Update | once per frame (`0x0046a042`): watch the skater's trick counters and pick a tip |
| `0x004c4950` | TrickTips_Show | show tip n: `ret 4`, thiscall on the monitor |
| `0x004c48f0` | TrickTips_MaskBank | mask word of tip n: 0 below `0x20`, 1 below `0x40`, an assert past that |
| `0x004c4420` | TrickTips_Display | per frame from the HUD (`0x0048d18d`): draw the panel and count the timer down |

### TrickTips_Update

Runs only when `FUN_00419420()` and `Video_Playing()` are false, `ReplayVideoRestartActive` is 0, the
**game mode (`LevelEventGameModeSelector`) is 1** and `0x0055fd78` is below 4. A plain headless launch
of a level starts in mode 2, where the monitor never runs; the capture forces mode 1 by rewriting the
argument of `0x004544a0` (as the power-up capture did for career mode).

Each call: copy the watched skater pointer (`0x0056a960`) into `+0x64`; on the very first frame
(`+0x58 == 0`) show the first table entry whose state dword is 0 and whose bit is not yet in the session
mask; increment the frame counter and the "since reset" counter; then look at nine counters in the skater
image. For each the object keeps the highest value seen (`+0xa4 ... +0xd0`) and, for some, how many times
it rose:

| Skater word | Highest (+) | Rises (+) | Effect |
|---|---|---|---|
| `+0x3048` | `0xc0` | | none (tracked) |
| `+0x303c` | `0xb4` | `0xe8` | resets the since-reset counter; otherwise that counter resets after `0x258` frames |
| `+0x3054` | `0xcc` | `0x100` | third rise clears the count and shows **tip 3** unless `+0x3058` has risen three times |
| `+0x3050` | `0xc8` | `0xfc` | none |
| `+0x3058` | `0xd0` | `0x104` | none (gates tip 3) |
| `+0x3040` | `0xb8` | `0xec` | fifth rise shows **tip 2** |
| `+0x302c` | `0xa4` | `0xd8` | wraps at 3, no tip |
| `+0x3030` | `0xa8` | `0xdc` | wraps at 3, no tip |
| `+0x3038` | `0xb0` | `0xe4` | third rise shows **tip 5** |

A separate counter (`+0x10c`) counts frames one of the two held flags (`0x0056b098`, `0x0056b204`) stays
set; above `0x96` frames it shows **tip 1** and restarts. All comparisons are signed 32-bit.

### TrickTips_Show

Ignored for a negative tip. After the first frame a tip is shown only when 600 frames have passed since the
previous one (`+0x5c`, which is updated even when the tip then turns out to be blocked). Then: the tips
option (`0x0056a46c`) must be non-zero; the level's "never again" mask (`0x0053c288 + (level * 0x6b + bank) * 4`)
must not hold the tip; the tip is marked seen (`0x0056a40c[bank]`) **before** the next test; if it is already
in the remembered mask (`0x0056a414[bank]`) nothing is shown; if it was seen before the heading is
"REMEMBER:" and the tip joins the remembered mask, otherwise "TIP:". The three text lines come from the tip
table and the timer is set to 200.

### Tip table (generated from the executable)

22 rows of five dwords at `0x00518b98`: a state word (0, 1 or 2; the first-frame search takes only the
zeros), a second word, and three line pointers. `scripts/generate_trick_tip_table.py` emits them with the
text and `tests/test_trick_tip_table.py` fails if the executable disagrees. Only tips 0, 1, 2, 3 and 5 are
raised by the monitor; the rest are shown nowhere in this code (other callers, or unused).

### TrickTips_Display

Nothing happens while the heading pointer (`+0x90`) is 0. Otherwise the x position starts at 20, slides in
over the first ten frames (`200 - timer < 10`: `40 * (200 - timer) - 380`) and out over the last ten
(`timer < 10`: `x + 40 * timer - 400`), is masked to 16 bits, and the calls are: `Mess_SetTextJustify(1)`,
`Mess_SetRGB(0x80, 0x80, 0, 0)`, `Mess_SetScale(0xdc)`, `Mess_DrawPanelText(x, 100, heading, 0)`, then for each
non-empty line `Mess_GetScale(line, 0)` and `Mess_DrawPanelText(x, 100 + n * (width * 20 / 220), line, 0)` for
n = 1, 2, 3 (the compiler reuses the `GetScale` argument words as the last two `DrawPanelText` arguments). The
timer then drops by one and the panel clears itself at zero. `Mess_GetScale` is a seam: its return value comes
from the trace.

## Retail trace

`build/p47b/tips_trace.py` (GDB) records each `TrickTips_Update` call (the 0x114-byte object and the four
tip-mask dwords before and after, every gate value, the nine skater counters, the tip table states and the
level's masks), each `TrickTips_Display` (object before and after and the `Mess_*` calls with arguments and
returns) and the level-start reset.

| Measure | Result |
|---|---|
| Monitor calls recorded (Warehouse, game mode 1) | 890 |
| Monitor calls replayed from their raw entry image (fixture) | 517 |
| Display frames replayed, with their Mess calls | 827 (8,270 drawing calls) |
| Level-start resets replayed | 3 |
| Tips raised and shown | tip 0 (first frame) x4, tips 3, 2, 5, 1 (and tip 1 again as REMEMBER: twice) |
| Tip requests that showed nothing | 10 (600-frame window, already remembered, blocked on the level, option off) |
| Gate skips (game mode 2, level state 4) | 4 |
| `ctest` checks | 1,347 |

Changing an exit-image word or a drawing argument makes the replay fail (`tests/test_trick_tips.py`).

## Driven input

A natural run raises only the first-frame tip (and the counters barely move in a few hundred idle frames),
so the capture also **pokes inputs** the monitor reads: the skater counters, the held flag, its own frame
counters, the level mask, the tips option, the game mode and the level state (`build/p47b/make_pokes.py`).
Every poke is a write to an input; the recorded outputs are whatever retail does with it. Each recorded call
is replayed from its own raw entry image, so a poke changes only what that call starts from.

## Memory form (10g session 17)

`TrickTips_Update`, `TrickTips_Show` and `TrickTips_Display` also exist over the retail memory (`WorldKit::tips_update` / `tips_show` / `tips_display`, `src/runtime/world_extra.cpp`), written from the machine code and
replayed word for word against forced calls of the retail functions (`re/gdb/probes/world_specs_tips.py`, groups `tpu`/`tpv`, `tps` and `tpd`, fixtures `world-calls-tp*.txt`). What the disassembly adds to the model above:
Show asserts the tip below 0x16 (line 0x158) and, for a tip of 0x40 or more, asserts again (line 0xc0) and **goes on with the tip number as the mask bank**; Update asks the pause check and then
`Video_Playing` (the second only when the first answers 0) before it reads anything of its own. `TrickTips_MaskBank` (0x4c48f0) is called only from Update's first-frame search (for tips below 22: bank 0) and
has no native of its own.

## Exclusions

* `FUN_00419420()` (a pause/fade check) and `Video_Playing()` are seams whose return is taken from the trace;
  they were always 0.
* The second bank of the masks (tips 32..63) does not exist in the table (22 tips) and was never exercised.
* Where other code raises tips, and what the `+0x04`/`+0x08` table words mean, is open; the monitor itself only
  reads the state dword.
