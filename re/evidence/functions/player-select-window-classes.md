# Player-select window classes (player_ui.cpp and the neighbouring window classes)

Status: ported and replayed against retail forced calls (Phase 4.10f)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slice: `player-select-window-classes` (20 functions). Native code: `src/runtime/player_select_ui.cpp` (methods of `PlayerSelect`, `player_select.hpp`); the
screen that builds these windows is in [player-select-screen.md](player-select-screen.md).

Read first: [frontend-window-base.md](frontend-window-base.md) (the element, window, text and sprite classes every class here is built from) and
[create-select-screen.md](create-select-screen.md) (the slab and the forced-call pattern).

## The classes

All four derive from the window base of Phase 4.10a (`screentools.cpp`); the vtable address says which class an object is.

| Class | Vtable | Size | What it is |
|---|---|---|---|
| `ScoreCounter` | `0x00519520` | `0xbc` | a window with one `"$%6d"` text element that counts towards a target value (`+0xac` target, `+0xb0` shown, `+0xb4` the text, `+0xb8` its font) |
| `MedalWindow` | `0x00519540` | `0xb8` | two score counters (the character's two totals, from the career record's words `+4` and `+8`), the frame (`s2win4.bmp`) and three medal sprites it places itself |
| `PromptWindow` | `0x00519560` | `0xb0` | a button picture and its caption: the engine maps the file name and the caption to the platform's button art |
| `TextFlagElement` | `0x00519584` | `0x34` | a text element whose slide speed is a shift (1 fast, 2 slow) from `+0x30` |

### Functions (20)

| Address | Name | |
|---|---|---|
| `0x004a1440`, `0x004a1510`, `0x004a1530` | `ScoreCounter_Construct`, `_ScalarDelete`, `_Destroy` | |
| `0x004a1540` | `ScoreCounter_SetValue` | the target, capped at `999999` (signed compare) |
| `0x004a1560` | `ScoreCounter_Draw` | slot 2: forces the font's draw argument to 1 around the window's draw |
| `0x004a15a0` | `ScoreCounter_Update` | slot 1 (vtable-only): the shown value moves half way to the target (`(d >> 1) + shown`) and lands on it when it is within one; `"$%6d"`, or `"$   ---"` for a negative target; **it calls `ScreenElement_Update` on itself, not the window's update, so the text child is not updated** |
| `0x004a1670`, `0x004a1880`, `0x004a18a0` | `MedalWindow_Construct`, `_ScalarDelete`, `_Destroy` | the three medal sprites are not children: the destructor deletes them |
| `0x004a19f0` | `MedalWindow_Draw` | slot 2 (vtable-only): one medal for each contest level the character won (gold `0x400`, else silver `0x800`, else bronze `0x1000` of the record's level mask), `0x44` pixels apart, and the gap is left for a level with no medal |
| `0x004a1b00` | `MedalWindow_Update` | the two counters' targets, then the window's update |
| `0x004a1b40`, `0x004a1dd0`, `0x004a1df0` | `PromptWindow_Construct`, `_ScalarDelete`, `_Destroy` | the caption is centred on the font's tallest glyph: `y += Font_TextTop >> 1` |
| `0x004a1e00` | `PromptWindow_GetWidth` | slot 3 (vtable-only): picture width plus caption width plus four, each asked of the child through its virtual slot 3 |
| `0x004a1eb0` | `PromptWindow_SetString` | |
| `0x004a1ed0`, `0x004a1f20`, `0x004a1f40` | `TextFlagElement_Construct`, `_ScalarDelete`, `_Destroy` | the flag argument is a **byte** pushed as a dword |
| `0x004a1f50` | `TextFlagElement_Update` | slot 1 (vtable-only): see Findings |

## Findings

- **Four of the 20 had no Ghidra function**: `ScoreCounter_Update`, `MedalWindow_Draw`, `PromptWindow_GetWidth` and `TextFlagElement_Update` are vtable-only
  methods (the plan listed 17 functions and called them "player_ui.cpp (5)"; the real file range starts at the counter, `0x004a1440`, and the five
  functions of the counter class sit before the medal window). They are named, `tony ghidra sync` created them.
- **`TextFlagElement_Update` has a bug retail keeps**: when the y distance is not zero and its shifted step rounds to nothing, the y step falls back to **half of the
  x distance**, not of the y distance (the compiler reused the register). The port keeps it.
- **`PromptWindow_Construct` is a seam in every other screen**: the menu code of 4.10c and the screens of 4.10e report it as the service `prompt_window_construct`
  (`ScreenKit::prompt`); the select screen's initialiser still does. Here the real constructor is replayed on its own, with `FUN_004dc050` (the file name to the
  platform's picture), `FUN_004dc0b0` (the caption) and `FUN_004dcbc0` (register with the engine) as services.
- **The counter's arguments are `(value, font, x, y)`**: the medal window builds its two counters with `(0, font, 0xa6, 0x0e)` and `(0, font, 0xa6, 0x1a)`; the 4.10d
  windows (`skater-windows`) still report the same constructor as the service `engine_004a1440`.
- **A virtual call on a child of a real class runs natively**: `WindowBase::set_dispatch` and `PlayerSelect::dispatch` (`player_select_dispatch.cpp`) run the
  methods of the window-base classes and of this screen's own classes; any other object is reported as a service as before. The per-frame hook (slot 7 of a
  real vtable, an engine function) is always a service.

## Validation

`re/gdb/probes/frontend_calls.py` with the group `uiw` (specs in `frontend_specs_playsel_cases.py`, the slab and stubs in `frontend_specs_playsel.py`, which
reuses the 4.10e1 slab and adds room for the select screen's larger objects). Each function is called in the running game on a random legal state in the
scratch slab (a window built by hand; real sprite and text elements where the width is computed; fake pool objects whose virtual methods are reported services;
constructors run for real in the arena). Fixture: `re/evidence/fixtures/plsel-calls-uiw.txt`; the replay is `player_select_retail_calls_test.cpp` (an adapter row per
function). 953 cases replay with no mismatch (40 a function; seven cases made retail fault, two `PromptWindow_Construct` with a null font and five `PromptWindow_GetWidth` with a missing child,
and are skipped, not replayed).
