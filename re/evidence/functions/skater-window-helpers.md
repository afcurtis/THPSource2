# The skater windows' shared helpers (commonfe.cpp)

Status: native port of `commonfe.cpp` (0x00418200..0x0041945f, 38 functions: 32 Ghidra lists, six vtable-only methods), replayed
against retail forced calls in the running game (Phase 4.10d; method in [frontend-text-layer.md](frontend-text-layer.md#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/skater_window_helpers.{hpp,cpp}`; fixtures `re/evidence/fixtures/skwin-calls-skhelp.txt`
Capture tooling: `re/gdb/probes/frontend_calls.py` with the `skhelp` group of `frontend_specs_skhelp.py` (on the menu group's scratch slab);
`re/gdb/opentony/fcall.py` gained `(i, None, fn)` pokes (a service that fills an array) and ran pokes before `fill`

`commonfe.cpp` holds the widgets the create-a-skater pages (see [skater-windows.md](skater-windows.md)) are built from, on top of the 4.10c menu
toolkit ([menu-toolkit.md](menu-toolkit.md)). Slot numbers are the toolkit's method numbers (0 delete, 1 position, 2 draw, 3 highlight, 4 value,
5 step, 6 nothing, 7 engine hook, 8 set text, 9 set column).

## The classes

| Class (vtable) | Size | Base | What it is |
|---|---|---|---|
| ColourText (`0x00518528`) | 0x7c | MenuText | a text item with a colour swatch picked from a preset table |
| StepperRow (`0x0051854c`) | 0x100 | MenuControlB | up to six words in a row, one lit |
| NumberRow (`0x0051856c`) | 0x10c | MenuRow | two columns, the second a number between a minimum and a maximum |
| ModelView (`0x00518594`) | 0x40 | ScreenElement | renders one PSX model (a static record at +0x2c or an animated one at +0x30) |
| InitialsWindow (`0x005185b4`) | 0xb8 | Window | three one-letter text elements at +0xac, +0xb0, +0xb4 |

ColourText: `+0x54..+0x56` swatch RGB, `+0x58` selected preset, `+0x5c` preset count, `+0x60` the preset table (rows of hue,
saturation, value; the editor's is `0x00525c00`, 36 rows), `+0x64` custom hue, `+0x68` / `+0x6c` custom saturation and value
factors, `+0x70` / `+0x74` arrow sprites, `+0x78` the arrow blink timer (30 frames). The swatch is the preset's colour (preset 0 is the
custom hue with saturation and value 0x80) with saturation and value scaled by the factors, converted by `Color_HsvToRgb` (4.9); the
value goes through a fixed-point chain whose roundings are in `ColourText_Swatch` (`0x00418720`): `((v << 20) / 128 >> 10) * (factor << 20 >> 10)`
divided by 512, times five over four.

StepperRow: `+0xf4` count, `+0xfc` style, words at `+0x31 + 0x20 * i` (a space is appended to all but the last), `+0x10` the lit word, `+0x14`
the summed width, `+0x18` the tallest height. Stepping never lands on word 0 (the label).

NumberRow: `+0x100` value, `+0x104` / `+0x108` minimum and maximum, `+0xd8` / `+0xec` the second column's offset and style. The value text is
set through the row's virtual method 9 (the native code runs the toolkit's own method for a toolkit row and reports a service for a fake row).

## The plain helpers

- `CommonFe_LightDefaults` (`0x00418200`): the front end skater's light rig: thirteen words from `0x0053dc1c`, then the words at `+0x16`, `+0x1c`,
  `+0x22` halved, `+0x18`, `+0x1e`, `+0x24` doubled, the dwords at `+0x28`, `+0x2c`, `+0x30` times 3/2, and constants at `+0x0c`, `+0x0e`, `+0x10`.
- `CommonFe_ColourIndices` (`0x004182e0`): two colour indices from the byte tables at `0x0055faf8..`: the alternate pair
  (`+0x0055fb36`, `+0x0055fb38`) when `byte[0x0055faf8 + b] == a`, otherwise `(0x0055fafa + a, 0x0055fb0e + a)`.
- `CommonFe_FindRgbsChunk` (`0x00418330`): the data of the first chunk tagged `RGBs` (0x73424752) in a (tag, size, data) list ended by -1.
- `CommonFe_BusiestMaterial` (`0x00418360`): for a non-custom character, the model of the first part whose slot byte is 0x10, its materials
  (PSX call `0x004b4ad0`) and the one with the highest palette score (`0x004b4e00`); a custom skater answers 0.
- `CommonFe_LoadFaceBitmap` (`0x00418480`): the texture-set entry two colour indices name (`0x004547f0`), its name with the third character
  replaced by `g`, loaded as the skater's bitmap between the spooler's `ProcessPending` and `Finalize`.
- `CommonFe_ReplayLevelFlag` (`0x00419420`): byte `0x00560080`, or the level's flag at `0x0053c240 + level * 0x1ac` with game mode 1.

## Findings worth keeping

- Colours read from a byte are pushed as dwords with whatever the caller's register held above the byte: the probe masks the three `frame_box`
  colour arguments with 0xff (`masks`), the replay passes the bytes.
- The model view's per-frame rotation is a 16-bit add of `+0x28` (static record) or 0x1e (animated record) to the record's angle at `+0x16`.
- `ModelView_Destroy` clears the first byte of the PSX region record (`0x0056d428 + region * 0x44`) after `PSX_ClearRegion(region, 1, 0)`.
- Retail asserts here only log; execution continues, so cases with a null light pointer, an out-of-range preset index and a number outside its
  limits are replayed (the assertion call is part of the compared service list).

## Open points

None for this slice. Not generated: a two-character texture-set name in `CommonFe_LoadFaceBitmap` (the byte after the `g` is stack garbage).
