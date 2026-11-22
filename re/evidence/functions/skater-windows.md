# The create-a-skater pages: personal information, appearance and stats (perwin.cpp, appwin.cpp, statwin.cpp)

Status: native port of the three pages' functions (35 functions: perwin 6, appwin 14, statwin 15), replayed against retail forced calls in the
running game (Phase 4.10d; method in [frontend-text-layer.md](frontend-text-layer.md#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/skater_windows.hpp` with `skater_windows_{personal,appearance,sync,stats,dispatch}.cpp` (the shared widgets are
[skater-window-helpers.md](skater-window-helpers.md)); fixtures `re/evidence/fixtures/skwin-calls-{skpers,skapp,skstat}.txt`
Capture tooling: `re/gdb/probes/frontend_calls.py` with the groups `skpers`, `skapp` and `skstat` of `frontend_specs_skwin.py` (the shared slab, seeds
and stubs, and the personal page), `frontend_specs_skapp.py` and `frontend_specs_skstat.py`; `frontend_calls.py` now sets
`unwind-on-signal on` (a faulting forced call no longer leaves gdb inside the dummy frame; `P410_NOUNWIND=1` shows the faulting `eip`) and
`fcall.py` reports `eip` and `esp` of a faulted call

The editor loop (4.10e) builds one page at a time, polls it every frame (0 stay, 1 go on, 2 go back), updates and draws it. Each page is a
Window of the 4.10a base holding items of the 4.10c menu toolkit and of [skater-window-helpers.md](skater-window-helpers.md). The pages work on
the **custom skater record**, 0x80 bytes at `0x0056a488 + slot * 0x80` (`slot = SkaterManager_CurrentCustomSlot`):

| Offset | Meaning |
|---|---|
| `+0x00` / `+0x10` | name / hometown (0x10 bytes each) |
| `+0x20` | age (0x06..) |
| `+0x21` | spare points (signed) |
| `+0x22` | "initial revision" flag (the stat page asserts it) |
| `+0x23` | height, 0x46..0x91 in steps of 5 |
| `+0x24` / `+0x25` | stance 0..2 / style 0..1 |
| `+0x27..+0x2a` | the four colour indices (the swatches) |
| `+0x2c + 4 * i` | ten stat points |
| `+0x54` / `+0x58` | gender flag (1 halves the weight) / a word |
| `+0x64 + 4 * i` | seven texture-set ids: skin tone, then the shirt, pants, ... the appearance page's `+0xb4..+0xcc` |

## The personal-information page (perwin.cpp)

`+0x1fc` the list, `+0x200 + 4 * i` the rows (0 name, 1 hometown, 2 age (a real NumberRow), 3 stance, 4 style, 5 height, 6 next, 7 weight),
`+0x384` the style (its font is also the rows' second-column style), `+0x388` / `+0x38c` the arrow sprites, `+0x390` the NameEntry, `+0x394` the state
(0 list, 1 typing), `+0x398..+0x3a8` five marker items, `+0xac` the "name matched a preset" byte.

- Poll: typing, the keyboard answers 1 (enter: the highlighted row 0 stores the name, row 1 the hometown; any other row asserts `No valid entry`)
  or 2 (cancel); browsing, confirm (`0x0056b029`) answers 1 on the list's -1 (nothing highlighted or an end row), opens the keyboard on rows 0 and 1
  (`NameEntry_SetName`, `NameEntry_SetPrompt(text, 0x100, 0x3c)`), otherwise navigates; back (`0x0056aff9`) answers 2.
- Update: age into the record; left / right on row 3 steps the stance (wrapping 0..2), row 4 the style (0..1), row 5 the height by 5 (clamped to
  0x46..0x91); the sound `0x90` plays for rows 2..5 and for up / down.
- Draw: the name and hometown are shortened to 0xa5 pixels (`Font_Ellipsize` on a local copy) into their rows' second columns; the stance and
  style markers show; the weight is `height^2 * 180 / 10000` (retail multiplies by the reciprocal `0x68db8bad`), halved when the gender flag is 1.
  **The two assertion messages of the marker switches are swapped in retail** (a bad stance reports "Bad style" and the reverse).
- `PersonalPage_ApplyPresetName` (`0x00429cf0`): the typed name against 24 preset rows at `0x00526bb0` (0x80 bytes: name, hometown, age at +0x20, ...),
  case-insensitively (`0x004c9f50`, native here: both-null is equal); a match sets the age row to the preset's.

## The appearance page (appwin.cpp)

`+0xac` the current texture-set id, `+0xb0` the skin tone (the digit in the skin texture's name, minus '1'), `+0xb4..+0xcc` seven ids, `+0xd0`
the scrolling list, `+0xd4 + 4 * i` twelve items (rows 0xb, 8, 9, 1, 0xa, 2, 3 with fake-able vtables; swatches 4, 5, 6, 7; a heading text -1),
`+0x254` / `+0x258` the two styles (fonts `s2menu1.fnt` and `s2skata2.fnt`, orange, right-aligned and centred), `+0x25c` / `+0x260` the arrow
sprites (`s2arrow6.bmp`, `s2arrow7.bmp`), `+0x264` the editor object that owns the palettes.

- Update: left / right on the highlighted row: 0xb steps the tone (0..2, then re-roots the id chain), 8 and 9 and 1 step the body textures, 0xa / 2 / 3
  the other parts; each applies `TextureSet_ApplyBodyGroup` (`0x004208a0`, a service), then `TextureSet_ApplyPreviewGroups` (`0x00422d80`) and
  `AppearancePage_Refresh`. Row 5's swatch copies its index to row 6's.
- Refresh: swatches are shown when their part has a second texture (or, for the shirt and pants, no skin tone); a shown swatch sets its kind's palette
  shift from its colour (`CreatePalette_SetShift`, 4.9 native) and takes the palette's average colour back (`CreatePalette_AverageHsv`); every kind's
  palette is rebuilt (`CreatePalette_UpdateAnimation`), the engine's palette read and store being the 4.9 seams.
- Camera (`0x0042ae30`): the camera offset depends on the highlighted row (ids 1/5/6/9, 2/3, 4/8, 7/10 and the default), the viewport rectangle is
  scaled by the screen size (`x * w >> 9`, `y * h / 240`).
- `AppearancePage_LoadPreset` (`0x0042ba00`): a custom skater whose name is a preset's takes the preset's hometown, age, stance, style, height, colours and
  the seven ids and re-applies them.
- `0x0042b4c0` (Ghidra lists 16 bytes there) is padding after a jump table, and `0x0042be40..0x0042c6ff` is cresave.cpp (4.10e): neither is in scope.

## The stat page and its points block (statwin.cpp)

The page's window (`+0x490` list of ten sliders, `+0x494 + 4 * i` the sliders' text parts (the dots are 0x18 before), `+0x4bc` the heading,
`+0x61c` the points block, `+0x620` the confirmation dialog, `+0x624` its answer) is built by statedit (4.10e); `StatPage_Build` (`0x004b6560`)
fills it. The points block (vtable `0x0051975c`, built by `StatPoints_Construct`) holds two counters, two texts and a sprite, the ten stat bytes being
edited at `+0xbc` and the character's points at `+0xc8`.

- Poll: with a dialog up, `0x00426310(edge, -1)` answers 1 (apply: the sliders and block take the character's stats, flag byte `+0x22` cleared for a
  custom skater, the stored answer returned) or 2 (dismissed); without one, confirm on the list's -1 returns 1 when nothing changed and the character
  is not a flagged custom skater, otherwise opens `0x00425fa0` with the message `StatPoints_Text` made; back returns 2 or opens the dialog the same way.
  The confirm block falls through into the back block when both edges are set (retail does not return in between).
- `StatPoints_Text` formats `Are you sure you want to exit?`, `Use initial %d point%s?`, `%s&your %d unused point%s will be lost.` or
  `Spend $%d on %d point%s?` depending on whether the character is a flagged custom skater.
- `StatPage_ResetSliders` / `StatPoints_Adjust` move stat bytes between the two preset arrays at `0x00539df8` and `0x00539e04` (index `0x00539e10 + 4 * stance`;
  only stances 0 and 1 have an array: the table's third entry points at unrelated data).
- `Career_GetPointCost` (`0x00415820`) is the 4.10c `option_limit` seam in the probe, so `StatPoints_Update` reports it as that service.

## Validation notes

- Windows are given a fake vtable whose per-frame hook (slot 7) is a reported service (`element_hook`), as for the 4.10c screens; the toolkit's
  `set_extra_dispatch` lets a native call through a vtable run the pages' and helpers' own methods when an object has the real vtable.
- The scratch locals retail keeps on the stack (the three words of a swatch's colour) are a scratch region the native code adds outside every
  replayed region (`0x7ff00000`).
- Cases where retail itself faults (an invalid texture-set chain, ...) are dropped by the capture; the replay compares only completed calls.

## Open points

None for this slice. The pages' own constructors and destructors for the personal and stat windows (creedit.cpp, statedit.cpp) and the editor loop are 4.10e.
