# Options screen (options.cpp, videowin.cpp, vidmenu.cpp)

Status: ported; replay results are listed at the end (Phase 4.10g)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slices: `options-window`, `options-level-pages`, `options-media-pages`, `options-controls-cheats`, `options-null-stubs`. Native code: `src/runtime/options.hpp` and
`options_{menu,gaps,scores,pages,controls,dispatch}.cpp` (methods of `OptionsScreen`, which derives from `ScreenKit`).

Read first: [career-select-screen.md](career-select-screen.md) (the same model), [menu-toolkit.md](menu-toolkit.md) and [frontend-window-base.md](frontend-window-base.md).

## What the screen does

`Options_Run` (`0x00483ef0`) is the front end's OPTIONS screen. It loads `options.pre` and three fonts (`mainf`, `s2font2`, `s2tricks`), builds one window
(`OptionsMenu`, `0xa00` bytes, vtable `0x005193c4`), runs frames until the window's poll answers 1 (leave through a controller screen or Back) or 2 (back on the pad) or the
engine asks to leave, applies the options (`OptionsMenu_Apply`, twice when the engine asked), deletes the window, unloads the fonts and runs the leak check. It answers `0`, or
`-1` after an answer of 2.

The window holds four lists (main, display options, movies, saved data) and opens one page at a time; the **state** (`+0x96c`, asked for through `+0x970`; the update moves it
and sends the sub lists back to their first row and clears both pads' triggers) says which:

| State | Showing | Page object (slot) |
|---|---|---|
| `0x14` | the main list: Player Controls, Display Controls, Sound levels, Movies, Saved data, Cheats, High scores, Gap checklist, Display options, Back | list `+0x34c` |
| `0x15` | the cheats page | `+0x9a4` (a window child) |
| `0x16` | display options: trick tips and score display (steppers 6 and 7), Back | list `+0x350` |
| `0x17` | movies: Cinematics, Game credits, Music credits, Back | list `+0x354` |
| `0x18` | the gap checklist | `+0x97c` |
| `0x19` | the high scores | `+0x980` |
| `0x1a` | the movie list (Cinematics) | `+0x984` |
| `0x1b` | the sound levels page | `+0x988` |
| `0x1c` | saved data: Load data, Save data, Load replay, Back | list `+0x358` |
| `0x1d` | the memory card page (save or load) | `+0x98c` |
| `0x1e` | the replay load page | `+0x990` |
| `0x1f` | the controller page | `+0x994` |

`OptionsMenu_Step` (`0x00482e60`) is a jump table on the state into twelve polls. In the main list confirm opens the entry: Player Controls and Display Controls leave the
screen (answer 1) after setting a return flag (`0x005601cc` or `0x005601cd`) that `OptionsMenu_Sync` (`0x00482770`) reads next time to highlight the entry; Sound levels, High
scores and Gap checklist build their page (the slot must be empty: "Possible memory leak"), the others only move the state. Game credits and Music credits leave the screen
with `0x00561cdc` set to 1 or 2 so the next options window opens on the movies list. A card page that loaded a game sets `0x00561ce0` and the screen leaves.

The pages are separate classes (below); the window draws and updates the page of its state by virtual call with the window's place. The window's prompt windows (four, at
`+0x9a8`) are hidden or shown and lowered to y = 0xdc by `OptionsMenu_SetPrompts` (`0x004826f0`) with a mask, and three models (memory card, boombox, controller) share a camera
and light rig set up by the constructor.

## The classes

| Class (vtable) | Size | Functions |
|---|---|---|
| `OptionsMenu` (`0x005193c4`) | `0xa00` | construct, delete, destroy, set prompts, sync, apply, draw, update, step, run, screen call, cheats available |
| `VideoWindow` (`0x00518b30`, videowin.cpp) | `0x14c` | construct (the level information window: a textured-quad packet and a 64 x 64 picture for each of the 14 levels) |
| `GapChecklist` (`0x005193e4`) | `0x504` | construct, locked, delete, destroy, update (rebuild), draw (input), strip brackets, sort rows, sort all |
| `HighScores` (`0x00519404`) | `0x7ac` | construct, delete, destroy, update, draw |
| `MovieList` (`0x00519424`) | `0x238` | construct, delete, destroy, update, draw, step, unlocked |
| `SoundLevels` (`0x00519444`) | `0x244` | construct, delete, destroy, update, draw, step, has/no selection |
| `CardPage` (`0x00519464`), `ReplayPage` (`0x00519484`) | | construct, delete, destroy, update, draw, step |
| `ControlsMenu` (`0x005194a4`) | `0x25c` | construct, delete, destroy, update, draw, control text, step, wrap value, value unique, fix duplicates, selectable |
| `CheatsPage` (`0x005194c4`) | `0x288` | construct, delete, destroy, update, draw, step, refresh, store, switchable, on back |
| `VideoMenu` (`0x00519914`, vidmenu.cpp) | | destroy only (the rest belongs to the platform layer) |

Seventeen further functions in the range return at once (zero or nothing): `0x00485280`, `0x004860c0`, `0x004893b0 .. 0x00489490` (the last range of 16-byte stubs; `0x00489440` is
the texture-page call the video window makes and this build does not need).

## Notes

- Movies: the table at `*0x0052d4a8` has 0x24-byte entries (+0 non-zero, +4 the label, +8 the kind); the rows' ids count down from -1. Kind 0 is always unlocked, 1 and 4
  belong to a skater (three gold medals, or in the four enabled characters when the skater's flag is set), 2 to a level (open with the third medal), 3 never, 5 follows the first
  character's flag, 6 needs three medals on any character.
- The gap checklist reads the gap table at `0x00541990` (0x2c-byte entries; +4 is `0xfffe` for a header, +6 the id, +8 the name), shows a header only when the entry after it is
  active, and sorts each run by id; a gap found is a two-column row with its number.
- The controller table (`0x0055fc0c`, four words for each player) holds the action each button does; the page keeps the four values distinct by moving the others on.
- Eight skaters' pictures are drawn in the first row of the cheats page and the rest in the second; a flagged skater's picture is drawn only when it is the first enabled one.
- The decompile drops `ecx` and arguments throughout; the disassembly (`build/p410g/ann_opt.txt`) is the source of this page.
- Limits retail does not survive (the generated state keeps inside them): the gap checklist's row array holds 0x60 rows (more overruns into the style
  slots at `+0x4cc`), a menu string is under 32 characters (the row's `+0x50` properties pointer follows the text buffer), and a cheat's type is 0, 1 or 2 (anything else reaches an
  assert that falls through to a null row).
- The window's rows built by other classes' constructors (steppers, model views, prompt windows) are seams, but the lists lay them out through their set-position slot, so the
  probe's arena blocks start with a fake item vtable (`optloop` keeps the shared window vtable: its window is updated and drawn through the children's slots).

## Replay results (Phase 4.10g)

Forced calls of the retail functions in the running game on generated state (`re/gdb/probes/frontend_specs_opt{,_pages,_ctl}.py`), replayed by `opentony_options_retail_calls_test` against `OptionsScreen`: the return value, every byte written and the ordered service and virtual calls must match. 94 functions, 3179 cases, 0 mismatches (fixtures `re/evidence/fixtures/opt-calls-*.txt`).

| Class | Function | Address | Cases |
|---|---|---|---|
| VideoWindow | `VideoWindow_Construct` | `0x0045b0a0` | 40 |
| Options | `Options_ScreenCall` | `0x00481200` | 20 |
| Options | `Options_CheatsAvailable` | `0x00482450` | 40 |
| Options | `Options_Run` | `0x00483ef0` | 20 |
| Options | `Options_ReturnZeroA` | `0x00485280` | 20 |
| Options | `Options_ReturnZeroB` | `0x004860c0` | 20 |
| Options | `Options_ReturnZeroC` | `0x004893b0` | 20 |
| Options | `Options_NothingA` | `0x004893c0` | 20 |
| Options | `Options_ReturnZeroD` | `0x004893d0` | 20 |
| Options | `Options_ReturnZeroE` | `0x004893e0` | 20 |
| Options | `Options_ReturnZeroF` | `0x004893f0` | 20 |
| Options | `Options_NothingB` | `0x00489400` | 20 |
| Options | `Options_NothingC` | `0x00489410` | 20 |
| Options | `Options_NothingD` | `0x00489420` | 20 |
| Options | `Options_NothingE` | `0x00489430` | 20 |
| Options | `Options_ReturnZeroG` | `0x00489440` | 20 |
| Options | `Options_NothingF` | `0x00489450` | 20 |
| Options | `Options_ReturnZeroH` | `0x00489460` | 20 |
| Options | `Options_ReturnZeroI` | `0x00489470` | 20 |
| Options | `Options_NothingG` | `0x00489480` | 20 |
| Options | `Options_NothingH` | `0x00489490` | 20 |
| OptionsMenu | `OptionsMenu_Construct` | `0x00481210` | 40 |
| OptionsMenu | `OptionsMenu_ScalarDelete` | `0x00482460` | 40 |
| OptionsMenu | `OptionsMenu_Destroy` | `0x00482480` | 40 |
| OptionsMenu | `OptionsMenu_SetPrompts` | `0x004826f0` | 40 |
| OptionsMenu | `OptionsMenu_Sync` | `0x00482770` | 40 |
| OptionsMenu | `OptionsMenu_Apply` | `0x00482870` | 40 |
| OptionsMenu | `OptionsMenu_Draw` | `0x00482950` | 40 |
| OptionsMenu | `OptionsMenu_Update` | `0x00482a40` | 40 |
| OptionsMenu | `OptionsMenu_Step` | `0x00482e60` | 40 |
| GapChecklist | `GapChecklist_Construct` | `0x004840b0` | 40 |
| GapChecklist | `GapChecklist_LevelLocked` | `0x00484ab0` | 40 |
| GapChecklist | `GapChecklist_ScalarDelete` | `0x00484af0` | 40 |
| GapChecklist | `GapChecklist_Destroy` | `0x00484b10` | 40 |
| GapChecklist | `GapChecklist_Update` | `0x00484c10` | 40 |
| GapChecklist | `GapChecklist_StripBrackets` | `0x004850a0` | 40 |
| GapChecklist | `GapChecklist_Draw` | `0x004850f0` | 40 |
| GapChecklist | `GapChecklist_SortRows` | `0x00485290` | 40 |
| GapChecklist | `GapChecklist_SortAll` | `0x004853b0` | 40 |
| HighScores | `HighScores_Construct` | `0x00485400` | 40 |
| HighScores | `HighScores_ScalarDelete` | `0x00485c90` | 40 |
| HighScores | `HighScores_Destroy` | `0x00485cb0` | 40 |
| HighScores | `HighScores_Update` | `0x00485d80` | 40 |
| HighScores | `HighScores_Draw` | `0x00485f90` | 40 |
| MovieList | `MovieList_Construct` | `0x004860d0` | 40 |
| MovieList | `MovieList_ScalarDelete` | `0x004866e0` | 40 |
| MovieList | `MovieList_Destroy` | `0x00486700` | 40 |
| MovieList | `MovieList_Update` | `0x00486790` | 40 |
| MovieList | `MovieList_Draw` | `0x004867a0` | 40 |
| MovieList | `MovieList_Step` | `0x00486830` | 40 |
| MovieList | `MovieList_Unlocked` | `0x00486910` | 40 |
| SoundLevels | `SoundLevels_Construct` | `0x00486b10` | 30 |
| SoundLevels | `SoundLevels_ScalarDelete` | `0x00486e80` | 30 |
| SoundLevels | `SoundLevels_Destroy` | `0x00486ea0` | 30 |
| SoundLevels | `SoundLevels_Update` | `0x00486f80` | 30 |
| SoundLevels | `SoundLevels_Draw` | `0x00487130` | 30 |
| SoundLevels | `SoundLevels_Step` | `0x004871e0` | 30 |
| SoundLevels | `SoundLevels_HasSelection` | `0x00487280` | 30 |
| SoundLevels | `SoundLevels_NoSelection` | `0x004872a0` | 30 |
| CardPage | `CardPage_Construct` | `0x004872b0` | 30 |
| CardPage | `CardPage_ScalarDelete` | `0x00487330` | 30 |
| CardPage | `CardPage_Destroy` | `0x00487350` | 30 |
| CardPage | `CardPage_Update` | `0x00487360` | 30 |
| CardPage | `CardPage_Draw` | `0x00487370` | 30 |
| CardPage | `CardPage_Step` | `0x00487390` | 30 |
| ReplayPage | `ReplayPage_Construct` | `0x004873b0` | 30 |
| ReplayPage | `ReplayPage_ScalarDelete` | `0x00487410` | 30 |
| ReplayPage | `ReplayPage_Destroy` | `0x00487430` | 30 |
| ReplayPage | `ReplayPage_Update` | `0x00487440` | 30 |
| ReplayPage | `ReplayPage_Draw` | `0x00487450` | 30 |
| ReplayPage | `ReplayPage_Step` | `0x00487470` | 30 |
| ControlsMenu | `ControlsMenu_Construct` | `0x00487490` | 40 |
| ControlsMenu | `ControlsMenu_ScalarDelete` | `0x00487c10` | 40 |
| ControlsMenu | `ControlsMenu_Destroy` | `0x00487c30` | 40 |
| ControlsMenu | `ControlsMenu_Update` | `0x00487dc0` | 40 |
| ControlsMenu | `ControlsMenu_Draw` | `0x00487dd0` | 40 |
| ControlsMenu | `ControlsMenu_ControlText` | `0x00487f60` | 40 |
| ControlsMenu | `ControlsMenu_Step` | `0x00488160` | 40 |
| ControlsMenu | `ControlsMenu_WrapValue` | `0x00488410` | 40 |
| ControlsMenu | `ControlsMenu_ValueUnique` | `0x00488480` | 40 |
| ControlsMenu | `ControlsMenu_FixDuplicates` | `0x004884c0` | 40 |
| ControlsMenu | `ControlsMenu_Selectable` | `0x00488590` | 40 |
| ControlsMenu | `ControlsMenu_NotSelectable` | `0x004885c0` | 40 |
| CheatsPage | `CheatsPage_Construct` | `0x004885d0` | 39 |
| CheatsPage | `CheatsPage_ScalarDelete` | `0x00488d50` | 40 |
| CheatsPage | `CheatsPage_Destroy` | `0x00488d70` | 40 |
| CheatsPage | `CheatsPage_Update` | `0x00488e30` | 40 |
| CheatsPage | `CheatsPage_Draw` | `0x00488e40` | 40 |
| CheatsPage | `CheatsPage_Step` | `0x004891a0` | 40 |
| CheatsPage | `CheatsPage_Refresh` | `0x00489240` | 40 |
| CheatsPage | `CheatsPage_Store` | `0x004892a0` | 40 |
| CheatsPage | `CheatsPage_Switchable` | `0x00489350` | 40 |
| CheatsPage | `CheatsPage_OnBack` | `0x00489390` | 40 |
| VideoMenu | `VideoMenu_Destroy` | `0x004cea70` | 40 |
