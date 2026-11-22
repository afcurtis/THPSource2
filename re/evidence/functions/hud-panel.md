# The in-game HUD and the trick score stack (panel.cpp, physics.cpp)

Status: native port of the HUD (`panel.cpp`: 40 listed functions and the five Ghidra does not list) and of the trick score
stack with the skater's take-off and landing, replayed against retail forced calls in a running Warehouse level (Phase
4.10b; method in [frontend-text-layer.md](frontend-text-layer.md#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/hud_panel.{hpp,cpp}` (the score stack, take-off and landing) and `src/runtime/hud_display.cpp` (the
display); fixtures `re/evidence/fixtures/hud-calls-*.txt`
Capture tooling: `re/gdb/probes/frontend_calls.py` with the groups in `frontend_specs_panel.py` (setup, stubs, the score
stack) and `frontend_specs_panel_draw.py` (the display, construction, take-off and landing, the untouched live state);
`re/gdb/opentony/fcall.py` gained the `masks` stub option

The HUD is drawn by `Panel_Display` once a frame for every skater. Every skater object embeds a **CPanelData at
`skater + 0x168`** (`panel` below): its first word points back at the skater, and it owns the score, the trick stack and the
text objects the HUD draws with. The trick stack is the list of tricks of the combo being built; the stack's points are
what the HUD shows, and landing or bailing turns them into the score or throws them away.

## The trick stack

Up to 200 entries of 0x28 bytes at `panel + 0x7b0`, the count at `panel + 0x26f0` (it is also `skater + 0x2858`):

| Offset | Meaning |
|---|---|
| `+0x00` | the trick name (a string pointer) |
| `+0x04` | its points (the point timer adds to the newest entry) |
| `+0x08` | a countdown (0x12 when pushed) |
| `+0x0c` | the landing quality when the entry was closed (0 until then) |
| `+0x10` | kind: 1 ends a chain, 2 is a spin or gap entry |
| `+0x14` | the caller's flag (0: the trick does not count its repeats) |
| `+0x18` | how many times the trick has been done (from the trick history at `skater + 0x3214`) |
| `+0x1c` | 1 when it was done switch |
| `+0x20` | the caller's extra word |
| `+0x24` | the message node showing it (0 until shown) |

**Scoring** (`TrickScore_ComputeStackSum`, `0x0048cbc0`): for every entry, its points times `(chain multiplier + 2)`, halved,
times the repeat percentage (100, 75, 50, 25, 10 for the first to the fifth and later repeat: the table at `0x00536168`,
indexed by the history count minus one and capped at 4), over 100; summed; times 150/100 for a Perfect landing and
75/100 for a Sloppy one, but only when the landing quality (`skater + 0x306c`) is non-zero. The chain multiplier is the
table at `0x0053617c` (0, 1, 2, 4, 6, 10, then `0x00536190 + 4 * value - 0x14` from six) indexed by the sum of the closing
values of the entry's chain (`TrickScore_ChainValue`: from the entry after the previous chain's end to the entry of kind
1 that ends it). **The landed value** (`TrickScore_ComputeLandedValue`) multiplies the sum by the stack length table at
`0x00536194` (0, 2, 4 ... 26 for 0 to 13 entries, then `n + 0x1c - 0xe`) and halves it. All divisions truncate towards zero.

**Pushing** (`TrickScore_PushEntry`, `0x0048e3b0`): a full stack drops its oldest entry; the points are multiplied by ten
in the tenfold mode (`0x0056b760`) and by 120/100 when the skater is switch; an empty stack first banks the previous
combo's total into the score; the new entry is filled by `TrickScore_InitEntry`; the previous entry is closed with the
landing quality (kind 0), or a new kind-1 entry takes it; the trick history is asked how often the trick was done (a
service), and the text is rebuilt. **Landing** (`TrickScore_CommitLandedStack`, `0x0048e680`) adds the landed value to the
combo total, copies the stack's names when it is the best combo yet, awards the level's trick-gap goal when an entry name
matches (case-insensitively), and empties the stack; **bailing** (`TrickScore_RollbackStack`) records the negated value in
the replay highlight, restarts the zoom effect and removes the entries from the trick history. **The point timer**
(`TrickScore_AdvancePointTimer`, `physics.cpp`) adds its step to `skater + 0x2f54`, bails the skater when
`(profile * 113 + 2000) * limit / 10000` is exceeded, and otherwise adds a share of the step to the newest entry.

**The text** (`Panel_RefreshTrickText`, `0x0048ddc0`): the names are joined with " + " into at most three lines (the line
buffers are `panel + 0x26f4`, 0x64 bytes each, filled from the last), a repeated trick is prefixed with its count times
180, the Perfect or Sloppy tag is appended, and a line wider than the viewport's limit (0xdc pixels in the split layouts,
0x1e0 otherwise, less the tag's width) starts the next line; the font's width is a service.

## The display

`Panel_Display` picks a layout from the skater's player number plus twice the viewport mode (0 to 7; anything else
asserts "Obsolete viewport mode"): the viewport mode 0 and 1 give the single-player layout, with a variant for the remote
player of a network game; the others place the score, the timer, the trick points and names, the switch and balance icons
and, in the lower half of a split screen, flat shaded divider bars. The draw functions work through virtual methods of
the engine's sprites, the fonts and the text effects; the probe gives their objects fake vtables so each draw is a
reported call carrying the object.

| Layout (player + 2 * viewport) | Trick points | Trick names | Score | Timer |
|---|---|---|---|---|
| 0, 1 | (0x100, 0xe0) | (0x100, 0xcd) | (0x19, 0x2d) (remote: 0x166) | (0x100, 0x30) |
| 2 | (0x180, 0xe0) | (0x180, 0xcd) | (0x166, 0x2d) | (0x132, 0x30) in versus mode 2 |
| 3 | (0x80, 0xe0) | (0x80, 0xcd) | (0x19, 0x2d) | (0x100, 0x30) |
| 4 | (0x100, 0xe0) | (0x100, 0xcd) | (0x19, h/2 + 0x19) | (0x1c4, h/2 + 0x19) |
| 5 | (0x100, 0xe0 - h/2) | (0x100, 0xcd - h/2) | (0x19, 0x23) | (0x1c4, 0x23) |
| 6 | (0x80, 0xea) | (0x80, 0xd7) | (0x70, h/2 + 0x1e) | (0x37, h/2 + 0x19) |
| 7 | (0x180, 0xef - h/2) | (0x180, 0xda - h/2) | (0x114, 0x23) | (0x1c9, 0x1e) |

(h is the screen height, `0x00527d54`.) The score is the sprite pair `0x0056b73c` and `0x0056b710` plus the score font;
the points of the last landing flow into it in steps (25, 100, 1000, 2500 by size) each frame, the special meter is a
gouraud quad (`Panel_DrawGouraudPoly`) below it.

## Functions

### The trick score stack and the skater side (22 functions, 839 retail calls)

| Address | Name | Retail calls replayed |
|---|---|---:|
| `0x0048caf0` | `TrickScore_SumEntryValues` | 40 |
| `0x0048cb10` | `TrickScore_ChainValueIfFirst` | 40 |
| `0x0048cb60` | `TrickScore_ChainValue` | 40 |
| `0x0048cbc0` | `TrickScore_ComputeStackSum` | 40 |
| `0x0048cd40` | `TrickScore_ComputeLandedValue` | 40 |
| `0x0048d6d0` | `Trick_ResetScoreState` | 40 |
| `0x0048d710` | `TrickScore_AdjustCurrentStackEntry` | 40 |
| `0x0048d7a0` | `TrickScore_CommitForSkater` | 40 |
| `0x0048d7b0` | `Panel_Bail` | 40 |
| `0x0048d960` | `TrickScore_InitEntry` | 40 |
| `0x0048ddc0` | `Panel_RefreshTrickText` | 40 |
| `0x0048e320` | `Panel_ShowStackText` | 40 |
| `0x0048e3b0` | `TrickScore_PushEntry` | 39 |
| `0x0048e620` | `TrickScore_ExpireEntries` | 40 |
| `0x0048e680` | `TrickScore_CommitLandedStack` | 40 |
| `0x0048e8a0` | `TrickScore_RollbackStack` | 40 |
| `0x0048f5f0` | `Trick_TakeOff` | 30 |
| `0x0048f720` | `TrickScore_AdvancePointTimer` | 40 |
| `0x0048f7c0` | `Trick_LandPending` | 30 |
| `0x0048f800` | `Trick_RegisterObject` | 30 |
| `0x0048f8a0` | `Trick_LandingQuality` | 40 |
| `0x0048f8f0` | `Trick_Land` | 30 |

### The panel: display, messages, construction (29 functions, 1562 retail calls)

| Address | Name | Retail calls replayed |
|---|---|---:|
| `0x0048ad50` | `Panel_LoadRuntimeSprites` | 30 |
| `0x0048b100` | `Panel_Uninit` | 30 |
| `0x0048b2b0` | `Panel_NoOp` | 40 |
| `0x0048b2c0` | `Panel_AllocFramePrimitive` | 37 |
| `0x0048b430` | `Panel_PlaceFrameQuadWide` | 35 |
| `0x0048b540` | `Panel_PlaceFrameQuad` | 40 |
| `0x0048b620` | `Panel_DrawFlatShadedPoly` | 40 |
| `0x0048b760` | `Panel_DrawGouraudPoly` | 40 |
| `0x0048b8e0` | `DisplayTapesSkate` | 84 (44 on the live state) |
| `0x0048ba70` | `GraffitiColor` | 40 |
| `0x0048bae0` | `DisplayScore` | 84 (44 on the live state) |
| `0x0048bef0` | `Panel_DrawSwitchIcon` | 84 (44 on the live state) |
| `0x0048c0b0` | `Panel_DrawBalanceIcon` | 84 (44 on the live state) |
| `0x0048c1a0` | `Panel_DisplayTrickPoints` | 84 (44 on the live state) |
| `0x0048c520` | `DisplayHolding` | 84 (44 on the live state) |
| `0x0048c760` | `Panel_ViewportOffset` | 40 |
| `0x0048c8d0` | `Panel_DisplayLevelMessage` | 84 (44 on the live state) |
| `0x0048ca00` | `Panel_ReplayInstructions` | 84 (44 on the live state) |
| `0x0048cd80` | `Panel_DisplayTrickNames` | 84 (44 on the live state) |
| `0x0048d030` | `Panel_NoOp2` | 40 |
| `0x0048d040` | `Panel_ObsoleteViewportMode` | 40 |
| `0x0048d090` | `Panel_Display` | 84 (44 on the live state) |
| `0x0048d660` | `Panel_SetPlayerNumber` | 40 |
| `0x0048d7c0` | `Panel_SkateLetterCollected` | 40 |
| `0x0048d830` | `Panel_TapeCollected` | 40 |
| `0x0048d8a0` | `Panel_CollectibleCounted` | 40 |
| `0x0048d990` | `Panel_Construct` | 30 |
| `0x0048e950` | `Panel_ShowStealsMessage` | 40 |
| `0x0048e9c0` | `CPanelData__OutOfBounds` | 40 |

## What the disassembly and the replays found

- **Five functions of the range are not in Ghidra's list** because nothing reaches them: `0x0048b2c0`
  (`Panel_AllocFramePrimitive`, a textured quad builder that scales the vertices it finds in the arena slot, so it is
  unused), `0x0048d030` (empty), `0x0048d040` (an assertion that always fails), `0x0048d830` (`Panel_TapeCollected`) and
  `0x0048e620` (`TrickScore_ExpireEntries`). Ghidra had not even disassembled them, so `tony ghidra sync` now disassembles a
  tracked address that holds no instruction before creating its function (this also created 20 functions named by
  earlier milestones that sync had silently skipped: the PKR2 compressors and decompressors, the `Vec3s` operators and
  a few text-layer methods). The PKR2 zlib decompressor pulled eleven C allocator-callback calls (`z_stream.zalloc` at
  `+0x20`, `zfree` at `+0x24`, in `0x004fb380`, `0x004fb3d0`, `0x004fd870`, `0x004fe6f0`) into the decompiled set; they are
  recorded as reviewed non-COM sites in `re/config/hal_boundary.yml`.
- **`CPanelData` is not a separate object: it is `skater + 0x168`**, and the decompile's `param_1` is sometimes the panel
  and sometimes the skater (`Panel_DisplayTrickNames` is thiscall on the panel and takes the skater as its first stack
  argument; its neighbours take the skater and read `+ 0x6b0` for what is `panel + 0x548`). The disassembly's `ecx` was the only
  reliable guide.
- **The decompile drops the arguments of thiscalls and the registers a callee ignores**: `FallingTextEffect_ClearClip` is
  called with the effect in `ecx` but takes no argument; `FallingTextEffect_ClearClipFor` is stdcall with the viewport
  index; the font's draw method takes `(x, y, string, 0)` and the string is usually a buffer on the stack, so the replay
  records the font, `x`, `y` and the string's text, not its address.
- **`Mess_AddMessage` is handed the viewport offsets as 16-bit words pushed as dwords**: the upper halves are stack garbage
  (the probe masks them, a new `masks` option of `fcall.py`).
- **The text builder can overrun its line buffers**: it asserts "Too long" when a line is 100 or more characters and
  writes it anyway (a line of up to 0x180 bytes is copied into a 0x64-byte slot). The port copies the same bytes.
- **`Panel_DisplayLevelMessage` and `Panel_ShowStealsMessage` read the viewport offsets from two stack words even when the
  layout index is not 0 to 7** (the offset function asserts and writes nothing): the values are stack garbage, so the
  probe never generates an index above 7 for them.
- **`Panel_DrawGouraudPoly`'s colour arguments are the second corner pair first** (`p5 p6 p7` for the right corners, `p8 p9
  p10` for the left): the decompile's parameter order was right, the byte layout of the primitive had to be checked against it.
- **The 8-bit multiplies for colour cycling carry the quotient's upper half** (`Mess_SetRGB`'s first argument in
  `DisplayTapesSkate` is `(time / 20 & 0xffff0000) | (time % 20) * 12`): zero for any time under 22 minutes.

## Validation

**Forced calls** (the 4.9 / 4.10a pattern): `frontend_calls.py` runs in the Warehouse level, at the fifth physics frame, on the
live player-one skater. A case pokes a random legal state into the skater (the stack, the landing quality, the effects, the
timer, the special meter, the flags), the HUD globals (game mode, players, viewport, replay state, pause), the level's goal
table and a scratch slab (trick names, a gap record, message nodes, the level message list, fake fonts), calls the retail
function, and records its return value, every byte it wrote (the skater, the globals, the arena and the bucket heads)
and the ordered services it reached. Every virtual method of a sprite, a font or a text effect, and every function outside
`panel.cpp`, is stubbed; the sprites, fonts and effects of the skater have their vtable words poked to the fake ones. The
natural variants run the draw functions with only the vtables redirected, at the fifth and the four hundredth frame.
`hud_retail_calls_test.cpp` replays every case: 2401 cases, 51 functions, no mismatch.

**Teeth**: `tests/test_hud_panel.py` changes one written byte, one call argument or one return value of a recorded case and
requires the replay to fail.

## Honest gaps

1. **What a draw looks like is not checked here**: a sprite or font draw is a reported call. The glyph and sprite
   drawing is the text layer's (4.10a) and the engine's; `tony hal video shadow` records the draws of a real frame.
2. **The natural frames are two idle frames of Warehouse** (the skater standing at the start); the random states cover the
   rest. A recorded run with a long combo would add the multi-line text and the Perfect landing through the whole path.
3. **Not generated**: null pointers the original dereferences after its assertion (a null frame or polygon, a missing sprite),
   a layout index above 7 for the level and steals messages, a goal list longer than the table.
4. **Seven functions have no direct caller** (`0x0048b2c0`, `0x0048b430`, `0x0048b540`, `0x0048d030`, `0x0048d040`,
   `0x0048d830`, `0x0048e620`): they are replayed as forced calls only, and whether a script or a pointer reaches them is unknown.
5. **Not ported**: `CBruce__TempTransform` (`0x0048ec00`), `M3D_ReadSurfaceFlags` (already native), `0x0048eb70` and the
   three vtable methods at `0x0048ea50`, `0x0048eb10`, `0x0048eb40`: they sit between `panel.cpp` and `physics.cpp` and are
   not HUD code. The player-select window classes of `player_ui.cpp` (`0x004a1670` to `0x004a2100`) are the planned slice
   `player-select-window-classes` of 4.10f.
