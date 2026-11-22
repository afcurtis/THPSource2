# The trick-list screen (trickwin.cpp / tricksel.cpp)

Status: ported natively over retail memory and replayed against retail forced calls (Phase 4.10e2); the decision logic was ported and unit tested in 4.7b and is
retail-checked in the same replay (slice `trick-menu-logic`).
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/trick_screens.hpp` and `trick_screens_{screen,menu,pages,input}.cpp` (class `TrickScreens`, built on `ScreenKit`, `screen_kit.{hpp,cpp}`); the clean
decision model is `src/runtime/trick_menu.{hpp,cpp}` and `trick_database.{hpp,cpp}` (4.7b). Tests: `trick_screens_retail_calls_test.cpp` (the replay),
`trick_screens_test.cpp` (unit), `tests/test_trick_screens.py`.

The screen lets the player browse the tricks their character knows by page (manual, grind, lip, air and special), buy the ones they have points for, and bind tricks to
button combinations. It is a modal loop: `TrickMenu_Run` (`0x004c0240`) loads six fonts (`s2font1/2/3`, `s2menu1`, `ltisymbol`, `s2skata2`), constructs the screen
window (`0x004bf870`, `TrickScreen`, 0x258 bytes), which owns the trick menu (`0x004c0430`, `TrickMenu`, 0x2158 bytes) and the panel that shows the messages
(`0x004c3dd0`, `TrickPanel`, 0xdc bytes), then runs `Game_FrameTick`, input polling, the window's update and draw methods and the frame limiter until the update returns
1 or 2, and unloads the fonts.

## Functions (33 runtime, 7 decision)

Three vtables: `TrickScreen` `0x00519830`, `TrickMenu` `0x00519850`, `TrickPanel` `0x00519870`.

| Address | Name | |
|---|---|---|
| `0x004bf860` | `TrickScreen_ScreenCall` | `*result = TrickMenu_Run()` |
| `0x004bf870`, `0x004bfa90`, `0x004bfab0` | `TrickScreen_Construct`, `_ScalarDelete`, `_Destroy` | the window: the background, the preview skater, the menu |
| `0x004bfc30`, `0x004bfd30` | `TrickScreen_LoadFont`, `_SetupScene` | a font by name; the preview scene (the skater model, from the player spool or `CustomSkater_*` for a created skater) |
| `0x004c0120`, `0x004c0200`, `0x004c0230` | `TrickScreen_Draw`, `_UpdateFrame`, `_Step` | draw, the vtable frame hook (a bare return) and one step of the loop |
| `0x004c0240` | `TrickMenu_Run` | the loop |
| `0x004c0430`, `0x004c0e90`, `0x004c0eb0` | `TrickMenu_Construct`, `_ScalarDelete`, `_Destroy` | the menu: six fonts, the manager (`Tricks_InitializeManager`), the binding block, arrows, panel, headers, prompts and the warning box |
| `0x004c1080`, `0x004c13c0` | `TrickMenu_Draw`, `_UpdateFrame` | the five states' drawing; the frame hook |
| `0x004c13d0` | `TrickMenu_BuildCategoryList` | the page list (ids 5, 6, 7, 9, 10, 11: title, manual, grind, air, special, Continue) |
| `0x004c2000`, `0x004c21a0` | `TrickMenu_RebuildComboList`, `_BuildPageList` | the combination list, the trick rows of the page |
| `0x004c2700`, `0x004c29d0` | `TrickMenu_RequirementsMet`, `_HighlightFirst` | the character's requirement flags; the first row |
| `0x004c2a10`, `0x004c2c10` | `TrickMenu_BuildSpecialCombos`, `_BuildNormalCombos` | the combination rows (special table: 36 records at `0x00540e30`; ordinary: `0x00540cb8`) |
| `0x004c2ed0`, `0x004c3050`, `0x004c33f0`, `0x004c3a80`, `0x004c3ca0` | `TrickMenu_UpdateCategories`, `_UpdateCombos`, `_UpdateTricks`, `_UpdateBuyDialog`, `_UpdateWarning` | the five state handlers, reached by a tail `jmp` from the update's jump table (Ghidra merged them into `TrickMenu_Update`: five "overlaps") |
| `0x004c3d30` | `TrickMenu_Update` | the state dispatcher |
| `0x004c3dd0`, `0x004c40b0`, `0x004c40d0`, `0x004c40e0`, `0x004c4300` | `TrickPanel_Construct`, `_ScalarDelete`, `_Destroy`, `_UpdateFrame`, `_ShowMessage` | the panel |

The seven decision functions (slice `trick-menu-logic`, native `trick_menu.cpp` through adapters):

| Function | What it decides |
|---|---|
| `0x004c3350` TrickMenu_CategoryMask | page id 6/7/8/9/10 -> class mask `0x200`/`0x100`/`0x800`/`0x4000`/`0x8000` (manual, grind, lip, air, special); anything else asserts |
| `0x004c1310` TrickMenu_ButtonGlyphs | button codes 1..12 -> glyph characters `G H E F J I K L B D C A` in the symbol font; other codes print nothing |
| `0x004c2890` TrickMenu_SortByCost | rows in stable ascending order of the tricks' cost (`+0x22`) |
| `0x004c2800` TrickMenu_AssignedRecord | the record bound to the highlighted combination (slot `block[combo]` on ordinary pages, the special slot whose combination matches on the special page) |
| `0x004c3cf0` TrickMenu_SpecialSlotsFull | no trick on the row and all five special slots used |
| `0x004c36d0` TrickMenu_BindSelected | ordinary: assign the slot. Special: a record sits on one special combination only (refuse: sound `0x92`, message 2); slots full (refuse: sound `0x92`, message 1) |
| `0x004c38f0` TrickMenu_UnbindSelected | clear the binding; on the special page retail passes the marker `0xff` (not -1), so the combination stays recorded on a slot whose trick is empty (asserts "Special trick should have been removed" only if that fails) |

## What is native and what is a seam

The window base, the menu toolkit (lists, rows, styles, text), the font code, the skater manager and the confirm/text boxes run natively on retail memory, as in 4.10e1.
The trick database logic runs through the clean 4.7b model: `database_view` builds a `TrickDatabase` from the retail manager's memory, the binding block is copied to a
vector and written back, and `report_assertions` maps the model's diagnostics ("Bad trick index" to the assert at `0x0052531c`, "Special trick should have been removed" to
`0x005418dc`) to retail's assert service. Seams, reported as services with the arguments retail passed:

- the card and career layers: `Tricks_InitializeManager` (`0x004bb4f0`), `TrickManager_Destructor` (`0x004bb790`), `TrickDatabase_ApplyUnlockState` (`0x004bbd70`),
  `Career_CheatUnlocked` (`0x00416940`), `Career_IsTrickPurchased` (`0x00416070`), `TrickConfig_ConsumePointsAndSetBit` (`0x00416140`);
- the points counter: its constructor `0x004a1440` and update `0x004a1540`;
- the engine and frame plumbing (`Game_FrameTick`, `Db_*`, `Pad_ClearTriggers`, `PollHost`, the sounds).

## Findings

- **Ghidra drops arguments and `ecx`**: the decompile of the constructors and the row builders is missing every `sprintf` argument and `this`; the disassembly gave them.
  `TrickPanel_Construct` takes seven stack arguments (the decompile shows five).
- **Jump tables confuse linear disassembly**: the five state handlers were found by `objdump --start-address` at each table target; Ghidra lists them inside
  `TrickMenu_Update` (the 4.10e2 curation adds the names; `tony ghidra sync` reports the five as overlaps).
- **Fake rows need slot 9**: the combination rows' text is set through the row's virtual slot 9 (`0x004ad020`); a fake row whose slot is not stubbed ran the loader.
- **A highlighted blank row has no record**: the buy dialog's generator must highlight a real row (a null record crashes retail).
- **Special tables are long**: the 36-record special table runs past `0x00540ec4`; the capture's static range had to cover it to the terminator.
- **Loop bounds**: `TrickMenu_RequirementsMet` scans the 20 skater rows (the loop tests `ecx` against the end after the increment); a 19-row native loop passed the
  smoke capture and failed two full-capture cases.
- **Same seed**: re-captured functions merged into an older capture must use the same `P410_SEED`, otherwise the base data (glyph tables, fonts) differs.

## Validation

`re/gdb/probes/frontend_calls.py` with the groups `trkscr` (38 specs) and `trkloop` (2 specs, the loop and the screen step); specs in `frontend_specs_trick.py` on a
0x30000-byte slab (the 4.10e1 slab layout extended for the trick group). Each function is called in the running game on generated legal state (the manager's tricks, the
binding block, the three lists with their widgets, styles, arrows, panel and warning box), with the seams above stubbed. Fixtures:
`re/evidence/fixtures/trkscr-calls-trkscr.txt` (1,140 cases, 30 per function) and `trkscr-calls-trkloop.txt` (40); all replay with no mismatch; the replay is `trick_screens_retail_calls_test.cpp` (an adapter row per function).
