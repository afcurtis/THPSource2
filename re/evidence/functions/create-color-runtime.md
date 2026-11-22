# Create-a-skater colour math and palettes (create_color.cpp)

Status: native port of the colour and palette functions, replayed against retail forced calls
(Phase 4.9; method in [create-skater-runtime.md](create-skater-runtime.md#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/create_color.{hpp,cpp}` (hue shift of palettes), `src/runtime/create_palette.{hpp,cpp}`
(recolouring a part), fixtures `re/evidence/fixtures/create-calls-color.txt` and `create-calls-palette.txt`

The editor lets the player recolour clothes and skin. It does not edit the engine's 15-bit palette in
place: it keeps a copy of each palette as hue / saturation / value entries, shifts that, and writes the
rounded result back.

## Number formats

| Value | Format |
|---|---|
| hue | degrees `<< 20` (a full turn is `0x16800000`); a negative hue gets one turn added |
| saturation, value | `0..1 << 20`; the converter clamps them to `0..0xfffff` |
| saved palette entry | 16 bytes: hue, saturation, value, alpha word (`0x8000` = the colour's alpha bit) |
| engine colour | 15 bits `r | g << 5 | b << 10`, bit 15 = alpha; a red of 0 becomes 1 (0 is "transparent") |

## Functions

| Address | Name | Native | Notes |
|---|---|---|---|
| `0x00423c90` / `cb0` | `Color_Min3` / `Color_Max3` | `min3`, `max3` | low bytes only; the arguments are full words |
| `0x00423cd0` | `Color_RgbToHsv` | `rgb_to_hsv` | writes value, saturation, then hue; black writes zeros |
| `0x00423ea0` | `Color_HsvToRgb` | `hsv_to_rgb` | sector table of six; a hue still negative after the turn asserts, an out-of-range sector asserts and writes nothing |
| `0x0041f0c0` | `Color_PresetHsv` | `preset_hsv` | preset 0 = none (all -1), else row n of 12 bytes at `0x00525c00` |
| `0x00424280` | `Palette_SavedColour` | `saved_colour`, `palette_hsv` | hue in degrees, saturation and value 0..255 |
| `0x00424330` | `Palette_ShiftHsv` | `shift_palette` | see below |
| `0x004241a0` | `Create_RememberPalette` | `remember_palette` | engine palette to saved entries |
| `0x004226e0` | `Bitmap_HueLookup` | `bitmap_hue` | table at `0x00525e10` of (name, hue) pairs, case-insensitive |
| `0x00422790` | `CreatePart_AverageHsv` | `average_hsv` | see below |
| `0x00422a80` | `CreatePart_SetHueShift` | `set_shift` | |
| `0x00422c10` | `CreatePart_ApplyColourPreset` | `apply_preset` | kinds 1, 2, 3, 6 only |
| `0x004214f0` | `CreatePart_UpdateTextureAnimation` | `update_animation` | |
| `0x00422460` | `CreatePart_SetPaletteHandle` | `set_palette_handle` | |

## What the replays found

- `Palette_SavedColour(n)` returns entry **n - 1** (entry 0 for n <= 0): the retail loop walks `n` entries
  and keeps the one it read before the last step. The decompile hid it; the callers depend on it (the
  average walks `0..255`, so it never sees entry 255 and sees entry 0 twice).
- `Palette_ShiftHsv` adds `dh << 20` to the hue and scales saturation and value by `(d << 20 + rounding) >> 17`
  times the entry's `>> 10`; only the first `limit` entries are shifted (the pelvis palette is shifted over
  `0x80` when the half flag is set). Only the low twelve bits of `dh`, `ds`, `dv` matter: the retail caller
  `CreatePart_UpdateTextureAnimation` passes 16-bit words with stale upper halves, which the `<< 20` shifts out.
- `CreatePart_AverageHsv` counts hues below 90 and from 270 to 360 degrees; when both exceed 32 the palette
  straddles the seam and the hue is averaged around -180 degrees instead. Kinds 2, 3 and 6 then look the
  skater's own texture set up and let `Bitmap_HueLookup` override the hue of a few named bitmaps.
- `CreatePart_SetHueShift` stores the preset's saturation and value, not the average's: the average's own
  are computed and dropped.
- The engine is a service. `palette_read` hands back the engine's colours (scripted, one dword per call
  value) and `palette_store` reports the 15-bit colours it was given (as the call's text); `find_material`
  stands for `PSX_FindSceneMaterial`.

## Validation

Replays: 480 colour cases (`Color_*`, `Palette_*` including the stored colours byte for byte) and 360 recolour
cases, with the assertion paths (`Bad hue`, `Unknown color sector`, `No texture database`) exercised. Changing one
byte of a recorded write or of a recorded palette store makes the replay fail
(`tests/test_create.py::test_replay_has_teeth`).
