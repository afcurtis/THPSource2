# Frontend capture harness (Phase 4.10a)

Status: working; reaches the main menu headlessly, pulses pad (keyboard) edges, logs every drawn element and records
replayable cases of the window base. Used by [frontend-window-base.md](frontend-window-base.md) and by every later 4.10
sub-milestone that needs a screen.
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Tooling: `re/gdb/probes/frontend_trace.py` (the probe), `scripts/make_frontend_trace_fixture.py` (cases to fixtures),
`re/gdb/probes/frontend_calls.py` (forced calls, see [frontend-text-layer.md](frontend-text-layer.md)); scratch drivers
`build/p410a/run_front.{py,sh}` (untracked, shown below)

## Go / no-go (the plan's mid-session check)

**Go.** The existing automation (`tony-skip-movies`, the keyboard-edge helpers of `docs/CAPTURE_INPROC.md` M7) already gets
the retail game through its logos and intro movies and into the frontend; the one addition 4.10a needed was *not* forcing
the main-menu result (`tony-frontend-play`) so the game stays on the menu. A probe sourced through `debug_game` with
`level=None` reaches `Shell Screen: 0 - MAIN_MENU` at about the tenth input tick and draws it indefinitely; the probe's
first record shows the screen id, then one `tick` record per input poll with every element draw, sprite draw and
message call in between. With case recording on, 500 ticks take about 4 to 5 minutes (every window call is a breakpoint).

## Recipe: park at the main menu, log frames, record cases

```bash
# build/p410a/run_front.sh (scratch): headless game under GDB, movies skipped, probe sourced, run to the probe's stop
export FE_TICKS=500                  # stop after this many input ticks
export FE_CASES=1                    # also record replayable window-base cases
export FE_OUT=build/p410a/trace_menu.ndjson
export FE_KEYS=keys.json             # optional: [{"tick": 80, "scan": 28, "press": 6}]  (DirectInput scan codes)
python build/p410a/run_front.py re/gdb/probes/frontend_trace.py      # = debug_game(level=None, gdb_commands=[tony-skip-movies, source PROBE, continue])
python scripts/make_frontend_trace_fixture.py build/p410a/trace_menu.ndjson \
    re/evidence/fixtures/frontend-calls-trace-mainmenu.txt --limit 40
# then the ordinary replay: build/native/opentony_frontend_retail_calls_test re/evidence/fixtures/frontend-calls-trace-mainmenu.txt
```

`run_front.py` is a ten-line wrapper: `SimpleNamespace(level=None, headless=True, unmute=True, gdb_commands=["tony-skip-movies",
"source PROBE", "continue"], gdb_batch=True, headless_launch=True, ...)` passed to `tony.commands.debug_game`.

## The seam format (what later screens replay against)

NDJSON, one record per line, written by `frontend_trace.py`:

| `t` | Contents |
|---|---|
| `screen` | the frontend screen id each time `FrontEnd_Main`'s screen log (`0x0045326b`) sees a new one (0 = `MAIN_MENU`) |
| `tick` | an input-poll tick (`0x004e41b0`): the tick number and the scan codes held this tick |
| `font` | one `Font_Draw`: the font image (0x4c bytes), x, y, the string, the fifth argument |
| `mess` | one `Mess_*` call: name, ECX, four stack words |
| `elem` | one window / text element / sprite element draw: the object, x, y and, for text, its string and font |
| `frame` | one `Mess_Display` (the HUD message frame boundary) |
| `const` | a read-only global the replay needs (the empty string text elements compare with) |
| `case` | one call of a window-base function: arguments, the object's bytes on entry and exit, and its ordered virtual-method and engine calls (taken at the original's call instructions, with the object each was made on) |

`case` records are the replayable ones: `make_frontend_trace_fixture.py` writes them in the forced-call fixture format, with
the entry image as the starting memory, so `frontend_retail_calls_test` replays them like any other fixture (324 natural
calls from the main menu and 306 from the career select screen). A later screen adds its own entries to `CASE_ENTRIES` / `CASE_SITES` in the probe (its update and
draw functions and the call instructions where they reach virtual methods) and an adapter row in the test.

## Keys

`FE_KEYS` writes `0x80` into the game's `KeyboardState` (`0x006a43e4`) for `press` input ticks starting at `tick`, and `0`
afterwards: the game's own DirectInput poll then sees a clean edge. Scan codes: `0x1c` Enter (confirm), `0x01` Esc (back),
`0xc8`/`0xd0` up/down, `0xcb`/`0xcd` left/right.

## What it reached

| Run | Keys | Screens (`Shell Screen: n - NAME`) | Window-base calls recorded | Fixture (calls kept, replayed) |
|---|---|---|---:|---|
| 1 | none, 500 ticks | `0 - MAIN_MENU` at tick 10 | 5,782 (1,687 element updates, 1,206 window updates, 1,206 window draws, 962 sprite draws, 721 text draws) | `frontend-calls-trace-mainmenu.txt`, 324, 0 mismatches |
| 2 | Enter held 6 ticks from tick 80 | `0 - MAIN_MENU`, then `10 - CAREER_SELECT` at tick 81 | 8,962 (2,683 / 1,800 / 1,800 / 1,355 / 1,324) | `frontend-calls-trace-careerselect.txt`, 306, 0 mismatches |

Both runs record every text element as empty: the main menu and the career select screen label their entries with sprites,
so no `Font_Draw` is reached from either (the text layer was validated with forced calls for that reason). One 500-tick run with
case recording takes about 4 to 5 minutes. The first menu item (the one the cursor starts on) opens `CAREER_SELECT`; the
other screens are not walked yet (gap 2).

## Honest gaps

1. **Only keyboard edges**: the frontend also reads the pad through the action mask (`0x006a3f1c`); `tony-action-edge` pulses it, but
   the probe does not yet (the menus react to the keyboard path).
2. **No named "park at screen X" command**: the probe walks by scripted keys from the main menu; a table of key scripts per
   screen (title, select player, options) is the next piece (4.10c onwards need it for their own screens).
3. **Frame boundary**: the probe counts input ticks, not displayed frames; `Mess_Display` marks HUD frames only (never called in
   the frontend), so a displayed frame in the frontend is "all `window_draw` calls between two ticks".
