# Level select screen (levelsel.cpp)

Status: ported and replayed against retail forced calls (Phase 4.10f3)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slice: `level-select-screen`. Native code: `src/runtime/level_select.{hpp,cpp}` (the window, methods of `LevelSelect`, which derives from `ScreenKit`) and
`src/runtime/level_select_classes.cpp` (the banner, globe, name window and mode menu, also methods of `LevelSelect`).

Read first: [career-select-screen.md](career-select-screen.md) (the same model), [menu-toolkit.md](menu-toolkit.md).

## What the screen does

`LevelSelect_Main` (`0x00458900`) is frontend screen 2 (`LEVEL_SELECT`): a globe turns to the level the player picks (left and right), confirm chooses it, and in the career
a menu (Play, Saved Parks, Pre-made parks) comes before the level starts. It loads six fonts (`mainf`, `s2font3`, `s2font2`, `s2font1`, `ingame1`, `s2tricks`), builds the
window (`0x628` bytes, vtable `0x00518a90`) and loops: the step (`0x00459d90`, which also writes the answer), then the per-frame engine calls, the window's update and draw,
and the clock wait, until the step answers done or the engine asks to leave.

The level table is at `0x0053c1f8`, `0x1ac` bytes a level: `+0` the name, `+0x40` a pointer to the location string, `+0x48` a byte (non-zero: no tapes to count), `+0x70` and
`+0x74` the globe's longitude and latitude. The chosen level is `0x00561ce4` (13 is the custom park option); the game mode is `0x00561c74`.

| Class (vtable) | Size | What it holds |
|---|---|---|
| `LevelSelect` (`0x00518a90`) | `0x628` | two banners (`+0xac`, `+0xb0`), five prompt windows (`+0xb8`: select, accept, back, goals, blank), name window (`+0xcc`), globe (`+0xd0`), two information windows (`+0xd4`, `+0xd8`, 4.10h), mode menu (`+0xdc`), the goal percentage prompt (`+0xe0`), the background (`+0xe4`), four fonts, state (`+0x618`), park menu (`+0x61c`), done (`+0x620`), answer (`+0x624`) |
| `LevelBanner` (`0x00518ab0`) | `0x22c` | a player's face and name, cut into two lines by `Level_CopyNameWord` (ends a line with "..." when it does not fit) |
| `LevelGlobe` (`0x00518ad0`) | `0xa4` | a PSX model of the globe and a crosshair; latitude and longitude turn toward their targets the short way round the 4096-step circle |
| `LevelName` (`0x00518af0`) | `0x210` | two flagged texts (name, location) that slide out before a new level's strings go in |
| `ModeMenu` (`0x00518b10`) | `0x240` | a scroll list of four rows: Play (1), the one at id 2, Saved Parks (3), Pre-made parks (4) |

The window's state is 0 choosing the level, 1 the mode menu, 2 the park menu. `LevelSelect_Update` is that state machine; `LevelSelect_ChangeLevel` steps the chosen level
over the table (skipping what the game mode hides) and tells the globe and the name window.

**What retail leaves uninitialised.** `LevelName_Construct` never sets the shown level (`+0x204`), and its first `LevelName_SetLevel` measures the name of that level. In the
game the block is a recycled arena block; the port keeps the read and, in fresh memory, shows level 0. The probe seeds a leftover valid level number (a wild number would read
wild memory, which the native port refuses).

## Functions (33)

| Address | Name | Replayed |
|---|---|---|
| `0x004588c0` | `LevelSelect_InitCallbacks` | yes |
| `0x004588e0` | `LevelSelect_ExitCallback` | yes |
| `0x004588f0` | `LevelSelect_ScreenCall` | yes |
| `0x00458900` | `LevelSelect_Main` | yes |
| `0x00458b90` | `LevelSelect_Construct` | yes |
| `0x004594e0` | `LevelSelect_ScalarDelete` | yes |
| `0x00459500` | `LevelSelect_Destroy` | yes |
| `0x004595e0` | `LevelSelect_Draw` | yes |
| `0x00459650` | `LevelSelect_Update` | yes |
| `0x00459be0` | `LevelSelect_ChangeLevel` | yes |
| `0x00459d90` | `LevelSelect_Step` | yes |
| `0x00459db0` | `LevelBanner_Construct` | yes |
| `0x0045a0a0` | `LevelBanner_ScalarDelete` | yes |
| `0x0045a0c0` | `LevelBanner_Destroy` | yes |
| `0x0045a120` | `Level_CopyNameWord` | yes |
| `0x0045a1b0` | `LevelGlobe_Construct` | yes |
| `0x0045a3e0` | `LevelGlobe_ScalarDelete` | yes |
| `0x0045a400` | `LevelGlobe_Destroy` | yes |
| `0x0045a490` | `LevelGlobe_Update` | yes |
| `0x0045a5a0` | `LevelGlobe_Draw` | yes |
| `0x0045a6b0` | `LevelGlobe_SetLevel` | yes |
| `0x0045a6e0` | `LevelName_Construct` | yes |
| `0x0045a920` | `LevelName_ScalarDelete` | yes |
| `0x0045a940` | `LevelName_Destroy` | yes |
| `0x0045a9a0` | `LevelName_SetLevel` | yes |
| `0x0045aa40` | `LevelName_Update` | yes |
| `0x0045ab50` | `LevelName_Refresh` | yes |
| `0x0045abc0` | `ModeMenu_Construct` | yes |
| `0x0045aeb0` | `ModeMenu_ScalarDelete` | yes |
| `0x0045aed0` | `ModeMenu_Destroy` | yes |
| `0x0045af60` | `ModeMenu_HighlightPlay` | yes |
| `0x0045af70` | `ModeMenu_Draw` | yes |
| `0x0045b080` | `ModeMenu_Result` | yes |

What is not part of the screen: the information windows at `0x0045b0a0` and `0x0045b950` and the park menu belong to 4.10h; the 4.12 sizing's `0x004588c0` range is exactly this
file.

## Validation

Forced calls in the running game (groups `levwin`, `levloop`, `levban`, `levglobe`, `levname`, `levmenu` of `frontend_specs_levsel.py`): objects built by hand in the scratch slab
with fake pool objects whose virtual methods are reported services, the toolkit's real rows, the level table as the game has it, the heap-allocated location strings captured
with it. Every class another function constructs (the information windows, the other windows' constructors) is a seam reported as a service; each class is replayed on its own
group. **The loop's frame waits for the clock with a bare spin on it, which a forced call cannot satisfy**, so the loop is replayed on the frames that end it: the step is a
seam that writes an answer through its pointer and answers done on most frames. The prompt windows are arena blocks (the stubbed constructor hands back what the caller
allocated) so the constructor's virtual calls on them land on real objects. Fixtures `re/evidence/fixtures/levsel-calls-*.txt`.
