# Frontend text layer: fonts, messages and text effects (fonttools.cpp, mess.cpp, texteffects.cpp)

Status: native port of the three files, every function replayed against retail calls (Phase 4.10a; method in
[Validation method](#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/frontend_text.{hpp,cpp}` (fonts), `frontend_mess.{hpp,cpp}` (messages),
`frontend_effects.{hpp,cpp}` (text effects); fixtures `re/evidence/fixtures/frontend-calls-{font,mess,effects}.txt`
Capture tooling: `re/gdb/probes/frontend_calls.py` and `frontend_specs_{common,font,mess,effects}.py`
(see also [frontend-window-base.md](frontend-window-base.md) and [frontend-capture-harness.md](frontend-capture-harness.md))

Everything the frontend, the HUD and the pause menu print goes through this layer: a **Font** object (one per loaded
`.FNT`), a **mess font** (one global Font the HUD messages use) and **text effects** that bind to a font and bend how
its glyphs are drawn. This milestone ports all of it over the original's own memory layout (`RetailMemory`, the 4.8
pattern), so a captured retail image replays byte for byte.

## Objects

**Font** (0x14c bytes, built by `Font_Parse` in 4.5):

| Offset | Field |
|---|---|
| `+0x00` | vtable (`0x005189f4`: deleting destructor, `Font_Draw`) |
| `+0x04`, `+0x08` | horizontal alignment (0 left, 1 centre, 2 right), vertical alignment (0 top row, 1 middle, 2 bottom, 3 top) |
| `+0x0c` | extra advance per glyph |
| `+0x14`, `+0x18`, `+0x1c` | colour (dwords); `+0x48..+0x4a` the colour saved by an escape code |
| `+0x21`, `+0x24`, `+0x28` | shadow flag (byte) and shadow offset |
| `+0x2c`, `+0x30` | monospace flag, scale (`0x1000` = 1.0) |
| `+0x34`, `+0x38`, `+0x3c` | glyph table, glyph count (the table has one more record, the sentinel), CLUT slot (-1 = none) |
| `+0x40` | bound text effect (the object whose callbacks `Font_Draw` calls) |
| `+0x44`, `+0x4b` | character mapping (0 letters, 1 digits and colon, 2 lower case) and the 256-byte character to glyph index table (0xff = no glyph) |

A glyph record is 8 bytes: sprite object, advance, bottom row, top row. The glyph sprite is the engine's: `Font_Draw`
sets its scale (`+0x0c`), mode (`+6`, 1 for the glyph, 2 for its shadow), colour bytes (`+7..+9`) and shadow flag (`+4`)
and calls its virtual draw method (vtable `+0xc`).

**Mess font and messages.** The mess font is a Font image at `0x0056a9a0` (name at `0x0056aaec`, loaded flag at
`0x0056ab0c`). Messages are a doubly linked list (head at `0x0056ab10`) of 0x1c-byte nodes; each runs a program of
0x20-byte steps that interpolates scale, colour and position over the step's duration, then jumps to the next step
(a signed step count in the step's last byte; 0 deletes the message, 0x7f holds for ever). Node and step layouts are
in `frontend_mess.hpp`.

**Text effects** (vtables `0x005197b8` base, `0x005197dc` falling, `0x00519800` zoom): an effect bound to a Font gets
three virtual calls per glyph from `Font_Draw`: position (`+0x14`, `&x, &y`), colour (`+0x18`, `&r, &g, &b`) and
"after the glyph" (`+0x20`). The falling effect drips blood; the zoom effect scales the font through seven states.

## Functions

### Fonts (17 functions, 678 retail calls)

| Address | Name | Retail calls replayed |
|---|---|---:|
| `0x0044a070` | `Font_Draw` | 39 |
| `0x0044a4d0` | `Font_TextWidth` | 39 |
| `0x0044a540` | `Font_TextExtent` | 40 |
| `0x0044a570` | `Font_TextTop` | 40 |
| `0x0044a5c0` | `Font_TextHeight` | 40 |
| `0x0044a620` | `Font_GlyphIndex` | 40 |
| `0x0044a860` | `Font_EscapeClass` | 40 |
| `0x0044a8e0` | `Font_ApplyEscape` | 40 |
| `0x0044aaa0` | `Font_CharAdvance` | 40 |
| `0x0044ab70` | `Font_SetRgb` | 40 |
| `0x0044ab90` | `Font_SetEffect` | 40 |
| `0x0044aba0` | `Font_GetEffect` | 40 |
| `0x0044abb0` | `Font_SetMapping` | 40 |
| `0x0044abe0` | `Font_Ellipsize` | 40 |
| `0x0044ad30` | `Font_FreeGlyphs` | 40 |
| `0x0044ad80` | `Font_ClearTable` | 40 |
| `0x0044af90` | `Font_IsLoaded` | 40 |

### Messages (20 functions, 600 retail calls)

| Address | Name | Retail calls replayed |
|---|---|---:|
| `0x004734b0` | `Mess_SetTextJustify` | 30 |
| `0x004734f0` | `Mess_SetScale` | 30 |
| `0x00473500` | `Mess_GetScale` | 30 |
| `0x00473510` | `Mess_SetSortMode` | 30 |
| `0x00473580` | `Mess_SetRGB` | 30 |
| `0x004735b0` | `Mess_DrawPanelText` | 30 |
| `0x004735d0` | `Mess_DeleteMessage` | 30 |
| `0x00473650` | `Mess_DeleteAll` | 30 |
| `0x00473690` | `Mess_Exists` | 30 |
| `0x004736c0` | `Mess_DeleteByText` | 30 |
| `0x004736f0` | `Mess_AddMessage` | 30 |
| `0x00473730` | `Mess_AllocNode` | 30 |
| `0x00473760` | `Mess_Update` | 30 |
| `0x004737c0` | `Mess_Display` | 30 |
| `0x004739d0` | `Mess_TextWidth` | 30 |
| `0x004739e0` | `Mess_DrawText` | 30 |
| `0x00473a00` | `Mess_LoadFont` | 30 |
| `0x00473b80` | `Mess_UnloadFont` | 30 |
| `0x00473be0` | `Mess_SetCurrentFont` | 30 |
| `0x00473bf0` | `Mess_RetiredNotice` | 30 |

### Text effects (35 functions, 1050 retail calls)

| Address | Name | Retail calls replayed |
|---|---|---:|
| `0x004b94a0` | `TextEffect_Construct` | 30 |
| `0x004b94c0` | `TextEffect_Delete` | 30 |
| `0x004b94e0` | `TextEffect_Destroy` | 30 |
| `0x004b94f0` | `TextEffect_Bind` | 30 |
| `0x004b95a0` | `TextEffect_Unbind` | 30 |
| `0x004b9610` | `TextEffect_PositionDefault` | 30 |
| `0x004b9690` | `TextEffect_ColourDefault` | 30 |
| `0x004b9710` | `TextEffect_GlyphDefault` | 30 |
| `0x004b9720` | `TextEffect_Disable` | 30 |
| `0x004b9730` | `FallingTextEffect_Construct` | 30 |
| `0x004b9750` | `FallingTextEffect_Delete` | 30 |
| `0x004b9770` | `FallingTextEffect_Destroy` | 30 |
| `0x004b9780` | `FallingTextEffect_SetBlood` | 30 |
| `0x004b9790` | `FallingTextEffect_Init` | 30 |
| `0x004b9820` | `FallingTextEffect_Update` | 30 |
| `0x004b9860` | `FallingTextEffect_GlyphDone` | 30 |
| `0x004b9ac0` | `FallingTextEffect_ClearClip` | 30 |
| `0x004b9ae0` | `FallingTextEffect_ClearClipFor` | 30 |
| `0x004b9b30` | `FallingTextEffect_Position` | 30 |
| `0x004b9c80` | `FallingTextEffect_Colour` | 30 |
| `0x004b9d50` | `FallingTextEffect_SetScale` | 30 |
| `0x004b9d60` | `FallingTextEffect_Bind` | 30 |
| `0x004b9de0` | `FallingTextEffect_Finished` | 30 |
| `0x004b9e00` | `FallingTextEffect_BloodUpdate` | 30 |
| `0x004b9ef0` | `FallingTextEffect_CreateSprite` | 30 |
| `0x004b9fb0` | `FallingTextEffect_DestroySprite` | 30 |
| `0x004ba020` | `ZoomTextEffect_Construct` | 30 |
| `0x004ba040` | `ZoomTextEffect_Delete` | 30 |
| `0x004ba060` | `ZoomTextEffect_Destroy` | 30 |
| `0x004ba070` | `ZoomTextEffect_Bind` | 30 |
| `0x004ba430` | `ZoomTextEffect_Init` | 30 |
| `0x004ba450` | `ZoomTextEffect_Update` | 30 |
| `0x004ba480` | `ZoomTextEffect_Colour` | 30 |
| `0x004ba760` | `ZoomTextEffect_Finished` | 30 |
| `0x004ba780` | `ZoomTextEffect_Start` | 30 |

Not ported (named or catalogued only): `0x00473480` and `0x00473490` (C++ static-initialiser stubs: they register an
exit handler), `0x00473c00`..`0x00473c40` (a small class with vtable `0x00518f80` between `mess.cpp` and `mmu.cpp`: not part
of the message layer), `0x004b9420` (a vector effect that sits before `texteffects.cpp` and uses `Math_Vector3s*`),
`0x004b9900` (data inside a function gap).

## What the disassembly and the replays found

- **Ghidra's function list was missing 15 of these 72 functions.** A method that is only reachable through a vtable
  never gets a function of its own, and the "distance to the next function" sizes the plan used swallowed it: the
  attributed `Mess_Display`, for example, sat next to `Mess_Exists`, `Mess_TextWidth` and a second retired notice that no
  list showed. Every function here was found by walking vtables and the gaps between listed functions in the
  disassembly. Giving them curated names and running `tony ghidra sync` made Ghidra create them.
- **`Font_Draw` is two passes.** The first measures: each glyph's pixel position (12-bit fixed point), the tallest
  glyph and the highest top row, then lets the bound effect adjust the position. The second applies alignment (a
  jump table for the four vertical modes) and draws. A character without a glyph is a space: the first glyph's
  advance less a quarter of it, computed by the x87 at 24-bit precision (a float multiply, truncated); native does the
  same in single precision. At most 64 characters are kept (the original keeps four 64-entry arrays on the stack and runs
  them into each other beyond that, so the native port refuses); a vertical alignment above 3 reads an uninitialised
  stack slot (no caller sets one).
- **The decompile lost a store in `Font_ApplyEscape`**: the `[` branch also saves the blue byte (`+0x4a`); the replays
  (three `[` cases among 40) show it.
- **`Font_Ellipsize` is odd on purpose or by accident**: it measures a copy of the string with `...` appended, but
  what it stores in place has three dots, so the measured text has six. The early exit "string plus dots already
  fits" can only ever be taken when the dots are free. It loops for ever when six dots are wider than the limit
  (the first capture hung on exactly that, so the generator now keeps limits above 240 pixels).
- **Mapping 2 is not a copy of mapping 0**: lower-case letters map to glyphs `0x31..0x4a`, after the sentinel, so a
  font that uses it needs more than `0x4a` glyphs (a fake glyph table with only `0x32` crashed the capture at a
  null sprite, which is how this showed up).
- **`Font_IsLoaded` returns 0 or `0xffffff00` when nothing matches** (the last string comparison's result with its low
  byte cleared); the low byte is the answer and the replay compares it.
- **The message layer writes a 16-bit scale and a 16-bit sort mode next to each other** (`0x0056ab14`, `0x0056ab16`):
  `Mess_SetSortMode` writes only the upper word. `Mess_Display` stores the interpolated scale as a word, reads the
  dword back and masks it before `Mess_SetScale` multiplies by 16. A flashing message cycles its red channel
  as `(SimulationTime % 20) * 12`; `Mess_DrawPanelText` ignores its fourth argument (the interpolated value
  `Mess_Display` computes for it is dropped). A stacked message is offset down by `16 * (number of stacked messages) - 16`, and each
  further stacked message by 0x20 less.
- **The simulation clock is live during a forced call** (`0x0056e320`, advanced by a multimedia timer thread). The
  probe pokes it at the start of every message case and holds the timer callback off while the case runs (a stub
  that returns from `0x004dace0`).
- **The zoom effect's state machine is in its `Bind` override**: the owner calls bind every frame, which interpolates
  the font scale through seven states (state 1 grows to the level's target in 5 frames, 2 settles to the held scale
  in 5, 3 holds it 30, 4 shrinks to nothing in 10, 7 is zero; 5 and 6 fade the colour and are entered by `Start`).
  The held scale is `0x940` (game level kind above 1 and game mode not 7) or `0xbc0`; the target is `0x1000` on level
  kind 1. Divisions by 5 and 10 are compiled to multiplications by `0x66666667`; they truncate toward zero.
- **The falling effect drips only while a glyph's countdown is negative**: its position callback pushes the glyph
  down by `-countdown << 13` (blood) or `countdown² << 12` pixels (12-bit fixed), stopping at the floor
  (`screen height + 0x1e`); after a glyph is drawn the blood sprite is drawn under it, clipped to its cell (the
  clip rectangle call order depends on a video flag at `0x029da3a0`).

## Validation method

Forced calls, the 4.8 / 4.9 pattern (`re/gdb/opentony/fcall.py`, now with `this=True`, `pokes=` and `deref=` stub
options): the retail game is stopped at a physics frame of Warehouse, one scratch slab is taken from the game's
allocator and fake fonts, sprites, message lists and effect objects are laid out in it with random legal state;
each original function is *called in the running game* and what it returned, which bytes it wrote and which outside
services it reached, in order, are recorded. Virtual calls are made on fake vtables whose slots hold the entry
points of retail functions that nothing in these paths reaches (the `SFX_*` stubs at `0x004ad9b0`..`0x004ada10`),
stubbed per slot while a case runs, so a glyph sprite's draw, an effect's callbacks and a destructor are reported as
service calls on the object they were made on. The callbacks that change what they are handed by pointer (an effect
moving a glyph, recolouring it) are stubs that write chosen values through the pointers and record them, so the
replay can script the same values. The native port replays each case from the same bytes and must produce the same
return value, the same written bytes and the same ordered calls; changing one recorded value makes the replay fail
(`tests/test_frontend_text.py::test_replay_has_teeth_*`).

Run it: `build/p410a/run_calls.sh GROUP CASES OUT` (scratch, untracked) wraps
`python build/p44c/run_gdb_level.py re/gdb/probes/frontend_calls.py` with `P410_GROUPS=font|mess|window|effects`,
then `scripts/make_career_calls_fixture.py --curated`. To add a function: a spec in `frontend_specs_*.py`, an adapter
row in `frontend_retail_calls_test.cpp`.

## Honest gaps

1. **The states are generated, not played.** The main menu draws no text through `Font_Draw` (every text element
   it creates is empty), so a natural capture of the frontend does not reach this layer; forced calls on random
   states do. `Mess_Display` is replayed end to end through the real `Font_Draw` over fake glyph sprites.
2. **Services are seams**: glyph sprite draws and destructors, the effect callbacks, the clip rectangle, `operator new`
   and the sprite constructor, the allocator and `Font_Load` / `Font_Unload` (by name) are stubbed and only their calls
   are compared.
3. **Not generated** because the original reads uninitialised stack or loops for ever: strings above 63 characters in
   `Font_Draw`, a vertical alignment above 3, ellipsis limits below the width of six dots, a message step of zero duration
   (a division by zero), null output pointers on an enabled effect.
4. **The zoom effect and the falling effect have no natural capture**: neither runs in the main menu or in Warehouse.
   `FallingTextEffect_GlyphDone` uses a global (`0x029da3a0`) whose meaning is guessed ("clip order flag").
