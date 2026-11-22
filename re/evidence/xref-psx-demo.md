# PSX demo symbols matched onto PC functions

Status: inferred (structurally supported methods), provisional (`callgraph` only)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Reference: emoluvjd2/thps2-demo-decomp `1050a327` (no blanket license — facts only)
Addresses: `re/xref/psx-demo.yml`

## Observation

The PSX demo decomp publishes 1,646 function symbols (from the demo's debug symbols)
grouped into 98 translation units, plus reconstructed bodies (1,232 parsed here for call
edges and string literals). The PC build shares the game code but not the instruction set.

## Method (`tony xref psx`)

1. **Anchors** — 167 community names equal to a PSX symbol (unique on both sides).
2. **Per-file alignment** — for each unit with PC assert attribution (and, iteratively,
   spans bracketed by already-matched functions), a weighted LCS aligns PSX link order with
   PC link order, scored on size ratio (PC ≈ 0.79× MIPS), call-edge consistency and shared
   strings. A pair is accepted only with ≥ 1 call edge whose callee is matched on both sides
   (`align+callgraph`) or a shared string (`align+strings`); order-only pairs are accepted
   only when both link-order neighbours are matched pairs (`align+sandwich`).
3. **Call-graph voting** — ordered callee segments and unique unmatched callers of matched
   pairs vote for new pairs (`callgraph`).
4. **Unit guard** — a non-anchor pair is rejected when the PC function is assert-attributed
   to a different file, or when its nearest attributed neighbours put it outside the PSX
   unit in (≈ alphabetical) link order. Header units (`*_H`) are exempt.

## Validation

`tony xref psx --holdout`: 30% of anchors hidden, 4 seeds (200 hidden trials):
139 correct, 3 wrong, 58 unmatched — **precision 97.9%**. By method: `align+callgraph`
72/75, `align+sandwich` 15/15, `align+strings` 3/3, `callgraph` 49/49. Without the unit
guard the call-graph stage was ~90% precise; the guard removed visible errors such as
`DoWallRidePhysics` in car code and `GetNBitsUnsigned` inside `pc\thstub.cpp`.

Independent agreement with curated OpenTony analysis:

- `0x004524a0` `Front_LoadGame` and `0x004c4e30` `Trig_ExecuteRestart` — OpenTony's only
  two names that coincide with PSX spellings — are matched to the same addresses.
- `0x00468b30` OpenTony `AnimationClock_Update` (confirmed: three-delta smoothing of the
  global time scale) ↔ PSX `FPS_Display`, whose reconstructed body "updates the game time
  scale from a rolling three-frame average". The original name is `FPS_Display`.
- The M3dColij collision family, `CCamera::AI` (`Camera_Update`), `FrontEnd2_Main`,
  `M3d_RenderSetup` and others agree with OpenTony's curated meanings.

## Result

444 PC functions receive a PSX symbol (167 anchor, 169 align+callgraph, 14 sandwich,
4 strings → `inferred`; 90 callgraph → `provisional`).

## Open questions / falsifiers

- `0x0041c2d0` OpenTony `Game_MainLoop` ↔ PSX `MySkater::Init` (CREATE, provisional).
  OpenTony's own evidence leaves open whether this is a mode-specific loop; a
  create-a-skater attribution would explain that. Check with a create-a-skater run.
- Held-out anchors are biased toward functions the community already cared about; true
  precision on the rest may be lower, which is why `callgraph`-only pairs stay provisional.
- PC-only code (D3D, DirectInput, networking) has no PSX counterpart by design.
