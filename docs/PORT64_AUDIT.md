# 5.6 audit (milestone 11a): typed structs and the 64-bit standalone

Status: **11a done** (commit `29d5cf4` of the OpenTony history): decisions (section 1), layout asserts and inventory (section 2), D9 merges and the checks (section 4).
**11b s1 done** (the arena and guest heap design, section 5); 11b s2+ is the worklist in section 5.4.
**11c done** (section 6); **11d s1 done** (the production surface of `RetailMemory` pinned, section 7); 11d s2, the backend split, is the next milestone (Opus 5.5 high): start at section 7.3.
The 32-bit build, the fixtures and the 14 gated scenarios stay the oracle through 11b-11d.

## 0. What the code does today (measured 2026-11-08)

- **Game memory is one 32-bit address space.** The standalone is linked at 0x00400000 and ends in a zero-filled section
  that reserves the retail image (to 0x029de000) and the area the retail process's zones lived in (to 0x0a100000;
  `src/hooks/kits/standalone_spike.cpp`). The Kits read and write it through `RetailMemory`
  (`src/runtime/retail_memory.hpp`) at retail addresses. In live mode an access is `*(T*)address`.
- **Pointers inside game memory are retail addresses.** `re/types` has 129 types (124 fixed layouts, 98 with a size).
  59 of them hold pointers, 168 pointer fields in all. Retail also does pointer arithmetic and compares pointers into
  arrays, and images, fixtures and state blocks all store these words as they are.
- **The C runtime heap is the host's heap.** `crt_new` (`standalone_seams.cpp`, the CRT_malloc / operator new seams)
  and the surface allocator (`standalone_surfaces.cpp`) return `malloc` pointers cut to 32 bits. This works only
  because the process is 32-bit.
- **Host code turns guest words into host pointers in 93 places**, as `reinterpret_cast<...>(uintptr_t(word))` casts,
  all in `src/hooks/kits/standalone_*.cpp`, `kit_dll.cpp` and `movie_*.cpp`. The Kits in `src/runtime` do it in only
  one place, `RetailMemory::find`.
- **Scratch window.** Stack locals of ported functions live at `kScratchBase` 0x7ff00800. Live, `alias_live` redirects
  that window to a host buffer.
- **Floating point.** `math_kit.hpp` emulates the x87 precision control explicitly (`Precision`, `round_to`). It
  computes in `long double` and runs `fsin`/`fcos`/`fpatan` as inline x87 instructions. The native replays already run
  as x86-64 on Linux and match the fixtures, so the Kits' arithmetic is proven on a 64-bit host with an 80-bit
  `long double`.
- **Kit include graph** (who includes whom among `*_kit.hpp`):
  - Leaves: `math`, `collision`, `heap`, and every Kit not named below.
  - `core` -> `math`; `m3d`, `rail` -> `collision`, `math`; `mmu` -> `heap`.
  - `camera`, `video`, `world` -> `collision`, `core`, `math`; `skater` -> `collision`, `math`, `rail`.
  - `render`, `bit` -> `collision`, `core`, `m3d`, `math`; `createobj` -> `collision`, `render`.
- **Raw accesses per Kit** (`.u8/.u16/.u32/.set*` calls, non-test sources), the larger ones: skater 3.7k, level* 3.2k,
  world 2.6k, render 2.1k, bit 1.5k, camera 1.4k, core 1.3k, front* 1.3k, gen* 1.1k, create 1.1k. The total is about
  40k.

## 1. Decisions

**D1. Handle scheme: a guest arena. Every pointer inside game memory stays a 32-bit guest address.**
- The 64-bit standalone reserves one 4 GiB range of address space (reserved, committed on demand). A host pointer is
  `arena_base + guest_address`; the 32-bit builds keep `arena_base = 0`.
- Nothing is committed below 0x00010000, so a null guest pointer still faults.
- Images, fixtures, state blocks and the comparisons are unchanged, because the bytes are the same.
- The address the retail code works with is the handle. A `pointer<T>` field becomes `GuestPtr<T>` (a `u32` wrapper),
  not a host pointer.
- Rejected:
  - *Native 64-bit pointers in structs* would change the layout of 59 types and break every image and fixture.
  - *Per-type handle tables* would need translation on every image load, and they would break retail's pointer
    arithmetic and array compares.

**D2. Host objects that retail keeps in game memory become host handles.** These are files, DirectDraw surfaces, HAL
textures, sound buffers and the window. Each stays a `u32` from a host table, never a host pointer, as the
`SurfaceStore` handle already is. Every allocation retail makes through the CRT comes from a **guest heap inside the
arena**: `crt_new` and the surface pixel allocator. A `Lock` answers a guest address, so the Kits keep writing pixels
through memory. The 5.6 note "surface pixel writes go through a store service" is therefore not needed.

**D3. `RetailMemory` splits into a production backend and a fixture backend, behind one accessor class.**
- The flat arena backend (base, size, optional guard) is the production path.
- The sparse region backend, which faults outside its regions, is built only by the fixture loader and the replays.
- The backend choice is the one branch `live_` already costs; there are no virtual calls per access.
- This meets 5.6's "no `RetailMemory` in a production path" without touching the ~40k Kit call sites.
- **Built in 11d s2 (section 7.4):** the choice is per compilation, not per access: `OPENTONY_GUEST_MEMORY_FLAT=1` on the DLLs' runtime
  archive leaves the arena backend alone (no `live_` branch either); every other target is the fixture build (sparse plus arena).

**D4. Typed structs are generated views, not overlays.**
- A generator turns `re/types` into `src/runtime/guest_layouts_generated.hpp`. Each fixed layout gets
  `kSize`/`kOffset` constants and explicit-width accessors over (memory, `GuestPtr`). A `pointer<T>` field becomes a
  `GuestPtr<T>`.
- `re/types` stays the single source of truth.
- Views work over both backends, and they avoid host struct packing and aliasing questions.

**D5. Toolchain: the x86-64 standalone uses the pinned llvm-mingw (`x86_64-w64-mingw32`), not clang-cl.**
- MinGW keeps an 80-bit x87 `long double` on x86-64; the MSVC ABI makes it 64-bit, which would silently change
  `math_kit`'s results.
- Assert `LDBL_MANT_DIG == 64` in `math_kit.hpp`.

**D6. `kTextureClut` (0x0056c540) and the CLUT words of texture records: do not emulate retail stack addresses.**
- These words are retail's ESP at a call inside the 8-bit loader (0x00456a70). They depend on the OS stack placement
  and caller frame sizes, not on the game. The frames are equal with them different.
- Under D1, the port's locals have arena addresses anyway.
- The mask class `stack-address-clut` becomes permanent. A future zone-comparison gate masks the same class of word in
  texture records (Open items, "Standalone, outside `.data`", first line, closes with this decision).

**D7. The scratch window stays a guest range at `kScratchBase` through 11d.**
- The arena covers 0x7ff00800, so `alias_live` is not needed in 64-bit.
- Turning it into real (guest) stack frames is **not** required for 64-bit. It would move every local the fixtures
  map, so it is deferred; re-entrancy stays fixed per native (own query record) as today.

**D8. Staging order.**
D1 means **the Kits need no change to run on 64-bit**, so the 64-bit standalone comes before typed structs:
- **11b, the first 64-bit target: the whole standalone on x86-64, `boot-menu` first, then all 14 gated scenarios.**
  Work:
  - Build the arena and the guest heap, and port the 93 host casts to `arena_base + word`.
  - Turn host objects into handles (D2).
  - Load the image into the arena instead of the reserving section.
  - Add an x86-64 standalone build with the D5 toolchain and gate it next to the 32-bit one.
- **11c-11d: typed views (D4), Kit by Kit, leaf first along the include graph.** Each step is gated by the 32-bit and
  64-bit scenarios plus the fixtures.
  1. `math`, `collision`, `heap`.
  2. `core`, `m3d`, `rail`, `mmu`.
  3. `camera`, `video`, `world`, `skater`, `render`, `bit`.
  4. `createobj`.
  5. The independent Kits and non-Kit runtime files, smallest first.
- With 11b in place, 11c-11d are a refactor with a working oracle and can be cut short without blocking Phase 5's
  done-when.

**D9. Duplicate layouts (done in 11a).**
- **Collision cache slots.** `SColCache`, `CollisionModelCacheEntry` and `RuntimeCollisionModelCacheEntry` described the same 20-slot array at
  0x00567a70. They disagreed: `CollisionModelCacheEntry` had model at +0 and model_data at +4. The trace-validated natives
  (`collision_cache.cpp`, `collision_query.cpp`) use item +0, model +4, face_count +8, first face index +0xc, so `SColCache` (the name bound in
  `re/symbols`) is canonical and the other two are aliases. `CollisionModelCacheEntry` was wrong.
- **Camera object.** `CCamera` and `RuntimeSkaterCameraObject` were two partial views of the same 1652-byte object (same offsets and widths,
  different names). `CCamera` is canonical: its two skater links now point at `RuntimeGameplaySkater`, and the alias description lists the
  skater-side names. `RuntimeGameplaySkater`'s camera field points at `CCamera`.
- `tony types verify` is valid (129 types, 988 fields; 10 duplicate fields gone).

## 2. Inventory of recovered layouts (generated)

One row per type in `re/types`. "Asserted" says what `src/runtime/guest_layouts_generated.hpp` checks at compile time, in the 32-bit and the 64-bit
builds: `mirror` = a packed mirror struct with the size and every field offset; `constants and bounds` = offsets and `offset + size <= type size`
only (the type has overlay groups, a field of unknown width, or no fields).

<!-- inventory:begin (scripts/guest_layouts.py --inventory writes this block) -->

| Type | File | Kind | Size | Fields | Pointers | Asserted |
|---|---|---|---|---|---|---|
| CCamera | camera.yml | fixed_layout | 1652 | 91 | 6 | constants and bounds |
| CollisionCandidateCellSourceHeader | collision.yml | fixed_layout | 12 | 3 | 0 | mirror |
| CollisionCandidateHeadArrayEntry | collision.yml | fixed_layout | 4 | 1 | 1 | mirror |
| CollisionFaceAabbRecord | collision.yml | fixed_layout | 28 | 7 | 0 | mirror |
| CollisionFaceCache | collision.yml | fixed_layout | 14000 | 1 | 0 | mirror |
| CollisionFacePrefix | collision.yml | fixed_layout | 16 | 6 | 0 | mirror |
| CollisionModelCache | collision.yml | fixed_layout | 320 | 1 | 0 | mirror |
| CollisionModelCacheEntry | collision.yml | alias of SColCache | | | | |
| CollisionModelDataHeader | collision.yml | fixed_layout | 28 | 7 | 0 | mirror |
| CollisionNormalRecord | collision.yml | fixed_layout | 8 | 4 | 0 | mirror |
| CollisionVertexRecord | collision.yml | fixed_layout | 8 | 4 | 0 | mirror |
| CollisionZoneGridRecord | collision.yml | fixed_layout | 1632 | 9 | 1 | mirror |
| LinkedCollisionObjectElement | collision.yml | fixed_layout | 76 | 8 | 2 | mirror |
| LinkedCollisionObjectListLinks | collision.yml | fixed_layout | 56 | 5 | 2 | mirror |
| LinkedCollisionObjectPrefix | collision.yml | fixed_layout | 36 | 7 | 1 | mirror |
| RetailTimerState | timing.yml | fixed_layout | 20 | 5 | 0 | mirror |
| RuntimeAnimatedObject | trg.yml | fixed_layout | unrecovered | 23 | 0 | mirror |
| RuntimeAnimationDecompressedChannelFrame | animation.yml | fixed_layout | 12 | 1 | 0 | mirror |
| RuntimeAnimationLinkPoseRecord | animation.yml | fixed_layout | 12 | 1 | 0 | mirror |
| RuntimeAnimationObjectPoseState | animation.yml | fixed_layout | unrecovered | 9 | 6 | mirror |
| RuntimeAnimationObjectRenderState | animation.yml | fixed_layout | unrecovered | 2 | 0 | constants and bounds |
| RuntimeAnimationObjectState | animation.yml | fixed_layout | unrecovered | 15 | 0 | mirror |
| RuntimeAnimationPoseJoint | animation.yml | fixed_layout | 24 | 4 | 0 | mirror |
| RuntimeAnimationRecord | animation.yml | fixed_layout | 8 | 2 | 0 | mirror |
| RuntimeAnimationTable | animation.yml | fixed_layout | unrecovered | 2 | 0 | constants and bounds |
| RuntimeAudioBankRecord | audio.yml | fixed_layout | 40 | 3 | 2 | mirror |
| RuntimeBaddyObject | trg.yml | fixed_layout | unrecovered | 36 | 0 | mirror |
| RuntimeBitBlood | bit.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeBitChunk | bit.yml | fixed_layout | unrecovered | 13 | 0 | mirror |
| RuntimeBitEmitter | bit.yml | fixed_layout | unrecovered | 7 | 0 | mirror |
| RuntimeBitFlat | bit.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeBitFrag | bit.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeBitFt4 | bit.yml | fixed_layout | unrecovered | 11 | 0 | mirror |
| RuntimeBitGLine | bit.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeBitGLineBit | bit.yml | fixed_layout | unrecovered | 24 | 0 | mirror |
| RuntimeBitGPolyline | bit.yml | fixed_layout | unrecovered | 10 | 0 | mirror |
| RuntimeBitGenPoly | bit.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeBitGlassPiece | bit.yml | fixed_layout | unrecovered | 17 | 0 | mirror |
| RuntimeBitGlow | bit.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeBitLinked | bit.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeBitMotionBlur | bit.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeBitNonRendered | bit.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeBitObject | bit.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeBitPixel | bit.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeBitPolyline | bit.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeBitQuad | bit.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeBitRibbon | bit.yml | fixed_layout | unrecovered | 7 | 0 | mirror |
| RuntimeBitSimpleAnim | bit.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeBitSmokeGenerator | bit.yml | fixed_layout | unrecovered | 11 | 0 | mirror |
| RuntimeBitSmokeTrail | bit.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeBitSpark | bit.yml | fixed_layout | unrecovered | 16 | 0 | mirror |
| RuntimeBitSpecial | bit.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeBitWobbly | bit.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeBufferedStreamObject | core.yml | fixed_layout | unrecovered | 5 | 1 | mirror |
| RuntimeCameraPointPayload | trg.yml | fixed_layout | unrecovered | 2 | 0 | mirror |
| RuntimeCameraPointRegistry | trg.yml | fixed_layout | 144 | 2 | 0 | mirror |
| RuntimeCameraQuaternion | camera.yml | fixed_layout | 20 | 5 | 0 | mirror |
| RuntimeCameraRig | camera.yml | fixed_layout | 180 | 27 | 0 | mirror |
| RuntimeCareerSaveImage | career-runtime.yml | fixed_layout | 7680 | 16 | 0 | constants and bounds |
| RuntimeCareerSelectUpdateObject | auto-runtime.yml | fixed_layout | unrecovered | 2 | 0 | mirror |
| RuntimeCareerSkaterRecord | career-runtime.yml | fixed_layout | 260 | 3 | 0 | mirror |
| RuntimeCdParksLabelTable | text.yml | fixed_layout | unrecovered | 2 | 2 | mirror |
| RuntimeCharacterTrickConfig | bin.yml | fixed_layout | 260 | 5 | 0 | mirror |
| RuntimeClippedVertex | collision.yml | fixed_layout | 8 | 4 | 0 | mirror |
| RuntimeCollisionFaceCacheRecord | psx.yml | fixed_layout | 28 | 7 | 0 | mirror |
| RuntimeCollisionModelCacheEntry | psx.yml | alias of SColCache | | | | |
| RuntimeCollisionObject | collision.yml | fixed_layout | 48 | 8 | 1 | mirror |
| RuntimeCollisionQueryRecord | physics.yml | alias of SLineInfo | | | | |
| RuntimeCreateAppearanceContext | bin.yml | fixed_layout | unrecovered | 10 | 5 | constants and bounds |
| RuntimeCreateScreensEditorObject | auto-runtime.yml | fixed_layout | unrecovered | 19 | 0 | mirror |
| RuntimeCreateScreensEditorPersonalObject | auto-runtime.yml | fixed_layout | unrecovered | 10 | 0 | mirror |
| RuntimeCreateScreensSaveObject | auto-runtime.yml | fixed_layout | unrecovered | 11 | 0 | mirror |
| RuntimeCreateScreensSelectObject | auto-runtime.yml | fixed_layout | unrecovered | 12 | 0 | mirror |
| RuntimeCreateScreensStatObject | auto-runtime.yml | fixed_layout | unrecovered | 13 | 0 | mirror |
| RuntimeCreateScreensUiBoxObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeCreateScreensUiConfirmObject | auto-runtime.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeCreateTextureManagerPrefix | bin.yml | fixed_layout | 1032 | 2 | 1 | mirror |
| RuntimeCreateTextureRecord | bin.yml | fixed_layout | 28 | 5 | 0 | mirror |
| RuntimeCreateobjDefDefinitionObject | auto-runtime.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeCreateobjMiscTrickObject | auto-runtime.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeCreateobjObjectSkaterObject | auto-runtime.yml | fixed_layout | unrecovered | 16 | 0 | mirror |
| RuntimeCreateobjScreenKeyconfigObject | auto-runtime.yml | fixed_layout | unrecovered | 24 | 0 | mirror |
| RuntimeCreateobjSmallSpoolerObject | auto-runtime.yml | fixed_layout | unrecovered | 9 | 0 | mirror |
| RuntimeCreateobjSmallTextureObject | auto-runtime.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeCreateobjTextureApplyObject | auto-runtime.yml | fixed_layout | unrecovered | 10 | 0 | mirror |
| RuntimeCreditsBitmapObject | auto-runtime.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeCreditsBitmapRecord | text.yml | fixed_layout | 16 | 2 | 1 | mirror |
| RuntimeCreditsMusicMarker | text.yml | fixed_layout | 24 | 4 | 0 | mirror |
| RuntimeCreditsMusicObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeCreditsTextManager | text.yml | fixed_layout | unrecovered | 10 | 2 | mirror |
| RuntimeCreditsTextObject | auto-runtime.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeCreditsTextRecord | text.yml | fixed_layout | 68 | 3 | 1 | mirror |
| RuntimeCullBox | collision.yml | fixed_layout | 48 | 8 | 0 | mirror |
| RuntimeCustomParkCell | custom-park.yml | fixed_layout | 16 | 2 | 0 | mirror |
| RuntimeCustomParkDiskCell | custom-park.yml | fixed_layout | 8 | 3 | 0 | mirror |
| RuntimeCustomParkDiskHeader | custom-park.yml | fixed_layout | 12 | 3 | 0 | mirror |
| RuntimeCustomParkDiskItemRecord | custom-park.yml | fixed_layout | 36 | 6 | 0 | constants and bounds |
| RuntimeCustomParkItemRecord | custom-park.yml | fixed_layout | 44 | 8 | 0 | mirror |
| RuntimeD3DPolygonCommandHeader | renderer.yml | fixed_layout | unrecovered | 4 | 1 | constants and bounds |
| RuntimeD3DPolygonRecord | renderer.yml | fixed_layout | 48 | 6 | 2 | constants and bounds |
| RuntimeD3DPrimitiveDispatchTable | renderer.yml | fixed_layout | 1024 | 1 | 1 | mirror |
| RuntimeEditorCommandBase | trg.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeEditorCommandC | trg.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeEditorCommandE | trg.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeEditorCommandF | trg.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeEditorCommandG | trg.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeEditorEndCommand | trg.yml | fixed_layout | unrecovered | 2 | 0 | mirror |
| RuntimeEditorLinkCommand | trg.yml | fixed_layout | unrecovered | 8 | 0 | mirror |
| RuntimeEditorNodeCommand | trg.yml | fixed_layout | unrecovered | 8 | 0 | mirror |
| RuntimeEquipCashWindow | equip.yml | fixed_layout | 876 | 11 | 3 | mirror |
| RuntimeEquipDeckItem | equip.yml | fixed_layout | 40 | 12 | 2 | mirror |
| RuntimeEquipDeckRack | equip.yml | fixed_layout | 1760 | 24 | 4 | mirror |
| RuntimeEquipDeckSet | equip.yml | fixed_layout | 92 | 4 | 1 | mirror |
| RuntimeEquipNameWindow | equip.yml | fixed_layout | 540 | 9 | 1 | mirror |
| RuntimeEquipRackBase | equip.yml | fixed_layout | 20 | 6 | 1 | mirror |
| RuntimeEquipSpoolNode | equip.yml | fixed_layout | 8 | 2 | 2 | mirror |
| RuntimeEquipSpoolQueue | equip.yml | fixed_layout | 24 | 6 | 4 | mirror |
| RuntimeEquipStatDots | equip.yml | fixed_layout | 72 | 6 | 2 | mirror |
| RuntimeEquipTruckRack | equip.yml | fixed_layout | 760 | 15 | 1 | mirror |
| RuntimeEquipView | equip.yml | fixed_layout | 80 | 7 | 2 | mirror |
| RuntimeEquipWindow | equip.yml | fixed_layout | 1012 | 13 | 7 | mirror |
| RuntimeExplosionObject | core.yml | fixed_layout | unrecovered | 11 | 2 | mirror |
| RuntimeExplosionParticle | core.yml | fixed_layout | 16 | 4 | 0 | mirror |
| RuntimeFont | font.yml | fixed_layout | 332 | 14 | 1 | mirror |
| RuntimeFontDiskGlyphRecord | font.yml | fixed_layout | 16 | 4 | 0 | mirror |
| RuntimeFontGlyphEntry | font.yml | fixed_layout | 8 | 4 | 1 | mirror |
| RuntimeFontGlyphImage | font.yml | fixed_layout | 92 | 6 | 0 | mirror |
| RuntimeFrontElement | frontend.yml | fixed_layout | 40 | 10 | 1 | mirror |
| RuntimeFrontMenuCmenuObject | auto-runtime.yml | fixed_layout | unrecovered | 7 | 0 | mirror |
| RuntimeFrontMenuProperties | frontend.yml | fixed_layout | 380 | 13 | 1 | mirror |
| RuntimeFrontendEffectsEffectObject | auto-runtime.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeFrontendEffectsFallingObject | auto-runtime.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeFrontendEffectsZoomObject | auto-runtime.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeFrontendWindowElementObject | auto-runtime.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeFrontendWindowObject | auto-runtime.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeFrontendWindowSpriteObject | auto-runtime.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeFrontendWindowTextObject | auto-runtime.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeGameplaySkater | player.yml | fixed_layout | 13624 | 268 | 11 | mirror |
| RuntimeGameplayTrickScoreState | bin.yml | fixed_layout | unrecovered | 7 | 2 | mirror |
| RuntimeGapDefinition | gap-runtime.yml | fixed_layout | 44 | 5 | 0 | mirror |
| RuntimeGenGap | gen-runtime.yml | fixed_layout | unrecovered | 20 | 0 | mirror |
| RuntimeGenIn | gen-runtime.yml | fixed_layout | unrecovered | 14 | 0 | mirror |
| RuntimeGenOut | gen-runtime.yml | fixed_layout | unrecovered | 20 | 0 | mirror |
| RuntimeGenPoint | gen-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeGenPolygon | gen-runtime.yml | fixed_layout | unrecovered | 10 | 0 | mirror |
| RuntimeGenPolygonList | gen-runtime.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeGeneratedGapMember | custom-park.yml | fixed_layout | 88 | 21 | 4 | mirror |
| RuntimeGeneratedParkPiece | custom-park.yml | fixed_layout | 568 | 20 | 4 | mirror |
| RuntimeGoalsCompetitionCompObject | auto-runtime.yml | fixed_layout | unrecovered | 12 | 0 | mirror |
| RuntimeGoalsDisplaysCompObject | auto-runtime.yml | fixed_layout | unrecovered | 20 | 0 | mirror |
| RuntimeGoalsLightsLightObject | auto-runtime.yml | fixed_layout | unrecovered | 19 | 0 | mirror |
| RuntimeGoalsWindowsLevelpicObject | auto-runtime.yml | fixed_layout | unrecovered | 26 | 0 | mirror |
| RuntimeGoalsWindowsVideoObject | auto-runtime.yml | fixed_layout | unrecovered | 13 | 0 | mirror |
| RuntimeGteRegisterFile | math.yml | fixed_layout | 288 | 21 | 0 | mirror |
| RuntimeHandicapPanelObject | auto-runtime.yml | fixed_layout | unrecovered | 9 | 0 | mirror |
| RuntimeHandicapWindowObject | auto-runtime.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeHeapBlockHeader | heap.yml | fixed_layout | 8 | 2 | 1 | mirror |
| RuntimeHeapGlobals | heap.yml | fixed_layout | 56 | 8 | 1 | mirror |
| RuntimeHeapZoneBounds | heap.yml | fixed_layout | 8 | 2 | 0 | mirror |
| RuntimeInputActionStateRecord | input.yml | fixed_layout | 16 | 5 | 0 | mirror |
| RuntimeIntVector3 | math.yml | fixed_layout | 12 | 3 | 0 | mirror |
| RuntimeLevelEditorCameraObject | auto-runtime.yml | fixed_layout | unrecovered | 19 | 0 | mirror |
| RuntimeLevelEditorCursorObject | auto-runtime.yml | fixed_layout | unrecovered | 48 | 0 | mirror |
| RuntimeLevelEditorGlobalMenuObject | auto-runtime.yml | fixed_layout | unrecovered | 14 | 0 | mirror |
| RuntimeLevelEditorObjectObject | auto-runtime.yml | fixed_layout | unrecovered | 39 | 0 | mirror |
| RuntimeLevelEditorUiObject | auto-runtime.yml | fixed_layout | unrecovered | 33 | 0 | mirror |
| RuntimeLevelEventState | trg.yml | variable_record | unrecovered | 12 | 0 | no (variable record) |
| RuntimeLevelGenMapsFreeObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeLevelGenPiecesBuildObject | auto-runtime.yml | fixed_layout | unrecovered | 8 | 0 | mirror |
| RuntimeLevelSelectClassesBannerObject | auto-runtime.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeLevelSelectClassesGlobeObject | auto-runtime.yml | fixed_layout | unrecovered | 8 | 0 | mirror |
| RuntimeLevelSelectClassesMenuObject | auto-runtime.yml | fixed_layout | unrecovered | 8 | 0 | mirror |
| RuntimeLevelSelectClassesNameObject | auto-runtime.yml | fixed_layout | unrecovered | 8 | 0 | mirror |
| RuntimeLightSet | math.yml | fixed_layout | 32 | 2 | 0 | mirror |
| RuntimeListObject | core.yml | fixed_layout | unrecovered | 25 | 2 | mirror |
| RuntimeM3DTransformedVertex | renderer.yml | fixed_layout | 28 | 7 | 0 | mirror |
| RuntimeMainmenuScreensCardObject | auto-runtime.yml | fixed_layout | unrecovered | 15 | 0 | mirror |
| RuntimeMainmenuScreensWheelObject | auto-runtime.yml | fixed_layout | unrecovered | 11 | 0 | mirror |
| RuntimeMainmenuWindowObject | auto-runtime.yml | fixed_layout | unrecovered | 20 | 0 | mirror |
| RuntimeMatrixTransform | math.yml | fixed_layout | 32 | 2 | 0 | mirror |
| RuntimeMenuListsGridObject | auto-runtime.yml | fixed_layout | unrecovered | 14 | 0 | mirror |
| RuntimeMenuListsListObject | auto-runtime.yml | fixed_layout | unrecovered | 16 | 0 | mirror |
| RuntimeMenuListsScrollObject | auto-runtime.yml | fixed_layout | unrecovered | 22 | 0 | mirror |
| RuntimeMenuScreensNameObject | auto-runtime.yml | fixed_layout | unrecovered | 14 | 0 | mirror |
| RuntimeMenuScreensWrapperObject | auto-runtime.yml | fixed_layout | unrecovered | 11 | 0 | mirror |
| RuntimeMenuToolkitControlObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeMenuToolkitDotsObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeMenuToolkitPropertiesObject | auto-runtime.yml | fixed_layout | unrecovered | 12 | 0 | mirror |
| RuntimeMenuToolkitRowObject | auto-runtime.yml | fixed_layout | unrecovered | 17 | 0 | mirror |
| RuntimeMenuToolkitSliderObject | auto-runtime.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeMenuToolkitTextObject | auto-runtime.yml | fixed_layout | unrecovered | 12 | 0 | mirror |
| RuntimeMmuDisplayName | save-runtime.yml | fixed_layout | 33 | 1 | 0 | mirror |
| RuntimeMmuFileRecord | save-runtime.yml | fixed_layout | 148 | 3 | 1 | mirror |
| RuntimeMmuPromptGroup | save-runtime.yml | fixed_layout | 520 | 3 | 2 | mirror |
| RuntimeMmuSaveHeader | save-runtime.yml | fixed_layout | 512 | 7 | 0 | mirror |
| RuntimeMmuSaveManager | save-runtime.yml | fixed_layout | 2892 | 10 | 2 | constants and bounds |
| RuntimeMmuScreen | save-runtime.yml | fixed_layout | 3536 | 23 | 13 | mirror |
| RuntimeNetMouseEditObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeNetmenuSummaryObject | auto-runtime.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeOptionsControlsCheatsObject | auto-runtime.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeOptionsControlsObject | auto-runtime.yml | fixed_layout | unrecovered | 12 | 0 | mirror |
| RuntimeOptionsGapsObject | auto-runtime.yml | fixed_layout | unrecovered | 14 | 0 | mirror |
| RuntimeOptionsMenuObject | auto-runtime.yml | fixed_layout | unrecovered | 22 | 0 | mirror |
| RuntimeOptionsMenuStepObject | auto-runtime.yml | fixed_layout | unrecovered | 13 | 0 | mirror |
| RuntimeOptionsPagesCardObject | auto-runtime.yml | fixed_layout | unrecovered | 2 | 0 | mirror |
| RuntimeOptionsPagesMovieObject | auto-runtime.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeOptionsPagesReplayObject | auto-runtime.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeOptionsPagesSoundObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeOptionsScoresObject | auto-runtime.yml | fixed_layout | unrecovered | 10 | 0 | mirror |
| RuntimePaletteCacheRecord | texture.yml | fixed_layout | 24 | 6 | 5 | mirror |
| RuntimePaletteRecord | texture.yml | fixed_layout | unrecovered | 5 | 1 | mirror |
| RuntimePcSystemVideoObject | auto-runtime.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimePcTextureRecord | texture.yml | fixed_layout | 44 | 14 | 6 | mirror |
| RuntimePkrFileRecord | fileio.yml | fixed_layout | 48 | 5 | 0 | mirror |
| RuntimePkrPackageHeader | core.yml | fixed_layout | 16 | 3 | 0 | mirror |
| RuntimePlatformObject | trg.yml | fixed_layout | unrecovered | 46 | 0 | mirror |
| RuntimePlayerHookPacketRecord | animation.yml | fixed_layout | 8 | 4 | 0 | mirror |
| RuntimePlayerHookPacketTemplate | animation.yml | fixed_layout | 20 | 2 | 0 | mirror |
| RuntimePlayerSelectLoadObject | auto-runtime.yml | fixed_layout | unrecovered | 10 | 0 | mirror |
| RuntimePlayerSelectPanelsBioObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimePlayerSelectPanelsDotsObject | auto-runtime.yml | fixed_layout | unrecovered | 2 | 0 | mirror |
| RuntimePlayerSelectPanelsHeaderObject | auto-runtime.yml | fixed_layout | unrecovered | 2 | 0 | mirror |
| RuntimePlayerSelectPanelsMenuObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimePlayerSelectUiCounterObject | auto-runtime.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimePlayerSelectUiFlagObject | auto-runtime.yml | fixed_layout | unrecovered | 2 | 0 | mirror |
| RuntimePlayerSelectUiMedalObject | auto-runtime.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimePlayerSelectUiPromptObject | auto-runtime.yml | fixed_layout | unrecovered | 2 | 0 | mirror |
| RuntimePlayerSelectViewObject | auto-runtime.yml | fixed_layout | unrecovered | 21 | 0 | mirror |
| RuntimePlayerSelectWindowObject | auto-runtime.yml | fixed_layout | unrecovered | 13 | 0 | mirror |
| RuntimePlayerSpoolEntry | psx.yml | fixed_layout | 40 | 6 | 1 | mirror |
| RuntimePlayerSpoolManager | psx.yml | fixed_layout | 2576 | 4 | 1 | mirror |
| RuntimePowerupObject | trg.yml | fixed_layout | 256 | 47 | 4 | mirror |
| RuntimePreEntry | pre.yml | variable_record | unrecovered | 4 | 0 | no (variable record) |
| RuntimePreManager | pre.yml | fixed_layout | 324 | 2 | 1 | mirror |
| RuntimePsxBlockmapCellSourceHeader | psx.yml | fixed_layout | 12 | 3 | 0 | mirror |
| RuntimePsxColourPulseInterval | psx.yml | fixed_layout | 4 | 4 | 0 | mirror |
| RuntimePsxColourPulseList | psx.yml | fixed_layout | unrecovered | 5 | 0 | constants and bounds |
| RuntimePsxDrawAreaPrimitive | math.yml | fixed_layout | 16 | 3 | 0 | mirror |
| RuntimePsxFaceHeader | psx.yml | fixed_layout | 4 | 2 | 0 | mirror |
| RuntimePsxFaceRecord | psx.yml | fixed_layout | unrecovered | 7 | 1 | mirror |
| RuntimePsxGeometryRecord | psx.yml | fixed_layout | 8 | 4 | 0 | mirror |
| RuntimePsxMaterialRecord | texture.yml | fixed_layout | 44 | 7 | 4 | mirror |
| RuntimePsxModelPacketHeader | psx.yml | fixed_layout | unrecovered | 14 | 0 | constants and bounds |
| RuntimePsxObjectRecord | psx.yml | fixed_layout | 76 | 19 | 2 | constants and bounds |
| RuntimePsxPostModelTagHeader | psx.yml | fixed_layout | 8 | 2 | 0 | mirror |
| RuntimePsxRect | math.yml | fixed_layout | 8 | 4 | 0 | mirror |
| RuntimePsxRegionSlot | psx.yml | fixed_layout | 68 | 14 | 8 | mirror |
| RuntimePsxRgbColourEntry | psx.yml | fixed_layout | 4 | 1 | 0 | mirror |
| RuntimePsxRgbColourTable | psx.yml | fixed_layout | unrecovered | 1 | 0 | constants and bounds |
| RuntimePsxSlotArrays | psx.yml | fixed_layout | unrecovered | 8 | 8 | mirror |
| RuntimePsxSpoolQueueEntry | psx.yml | fixed_layout | 17 | 5 | 0 | mirror |
| RuntimePsxTextureWibItem | psx.yml | fixed_layout | 16 | 4 | 0 | mirror |
| RuntimePsxTextureWibList | psx.yml | fixed_layout | unrecovered | 4 | 0 | constants and bounds |
| RuntimeQ12Matrix | math.yml | fixed_layout | 18 | 1 | 0 | mirror |
| RuntimeRailLookupEntry | trg.yml | fixed_layout | 4 | 2 | 0 | mirror |
| RuntimeRelocatableModuleNode | core.yml | fixed_layout | 52 | 5 | 4 | mirror |
| RuntimeRenderAnimModelObject | auto-runtime.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeRenderImageBmpObject | auto-runtime.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeRenderImageObject | auto-runtime.yml | fixed_layout | unrecovered | 15 | 0 | mirror |
| RuntimeRenderImageSlicedObject | auto-runtime.yml | fixed_layout | unrecovered | 18 | 0 | mirror |
| RuntimeRenderImageSpriteObject | auto-runtime.yml | fixed_layout | unrecovered | 13 | 0 | mirror |
| RuntimeRenderState | core.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeReplayBitReader | replay.yml | fixed_layout | 12 | 3 | 1 | mirror |
| RuntimeReplayHeaderFields | replay.yml | fixed_layout | 56 | 9 | 0 | mirror |
| RuntimeReplayHighlightRange | replay.yml | fixed_layout | 8 | 2 | 0 | mirror |
| RuntimeReplayPlaybackHighlightSlot | replay.yml | fixed_layout | 8 | 3 | 0 | mirror |
| RuntimeReplayPlaybackState | replay.yml | fixed_layout | unrecovered | 11 | 4 | mirror |
| RuntimeReplayStreamBuffer | replay.yml | fixed_layout | 524288 | 2 | 0 | constants and bounds |
| RuntimeResourceStream | fileio.yml | fixed_layout | 56 | 7 | 4 | mirror |
| RuntimeScreenPrimitive | core.yml | fixed_layout | 28 | 15 | 0 | constants and bounds |
| RuntimeShellHelpersEffectObject | auto-runtime.yml | fixed_layout | unrecovered | 16 | 0 | mirror |
| RuntimeShellHelpersPieObject | auto-runtime.yml | fixed_layout | unrecovered | 5 | 0 | mirror |
| RuntimeShellHelpersTextwinObject | auto-runtime.yml | fixed_layout | unrecovered | 35 | 0 | mirror |
| RuntimeShortTransform | math.yml | fixed_layout | 24 | 2 | 0 | mirror |
| RuntimeShortVector3 | math.yml | fixed_layout | 6 | 3 | 0 | mirror |
| RuntimeSkaterCameraObject | player.yml | alias of CCamera | | | | |
| RuntimeSkaterMovementState | physics.yml | fixed_layout | unrecovered | 100 | 1 | mirror |
| RuntimeSkaterTrickScriptState | bin.yml | fixed_layout | unrecovered | 29 | 5 | mirror |
| RuntimeSkaterWindowHelpersColourObject | auto-runtime.yml | fixed_layout | unrecovered | 18 | 0 | mirror |
| RuntimeSkaterWindowHelpersInitialsObject | auto-runtime.yml | fixed_layout | unrecovered | 1 | 0 | mirror |
| RuntimeSkaterWindowHelpersModelObject | auto-runtime.yml | fixed_layout | unrecovered | 8 | 0 | mirror |
| RuntimeSkaterWindowHelpersNumberObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeSkaterWindowHelpersStepperObject | auto-runtime.yml | fixed_layout | unrecovered | 13 | 0 | mirror |
| RuntimeSkaterWindowsAppearanceObject | auto-runtime.yml | fixed_layout | unrecovered | 14 | 0 | mirror |
| RuntimeSkaterWindowsPersonalObject | auto-runtime.yml | fixed_layout | unrecovered | 15 | 0 | mirror |
| RuntimeSkaterWindowsStatsPointsObject | auto-runtime.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeSkaterWindowsStatsStatObject | auto-runtime.yml | fixed_layout | unrecovered | 12 | 0 | mirror |
| RuntimeSkaterWindowsSyncAppearanceObject | auto-runtime.yml | fixed_layout | unrecovered | 4 | 0 | mirror |
| RuntimeSoundBufferDescriptor | audio.yml | fixed_layout | 36 | 4 | 1 | mirror |
| RuntimeSoundDescriptionRecord | audio.yml | fixed_layout | 52 | 4 | 0 | mirror |
| RuntimeSourcePaletteLookupNode | texture.yml | fixed_layout | 24 | 5 | 3 | mirror |
| RuntimeTextureSetManagerPrefix | bin.yml | fixed_layout | 26120 | 2 | 0 | mirror |
| RuntimeTextureSetRecord | bin.yml | fixed_layout | 68 | 9 | 1 | mirror |
| RuntimeTrickDescriptor | bin.yml | variable_record | unrecovered | 5 | 0 | no (variable record) |
| RuntimeTrickInputHistory | bin.yml | fixed_layout | unrecovered | 6 | 0 | mirror |
| RuntimeTrickInputHistoryEntry | bin.yml | fixed_layout | 8 | 3 | 0 | mirror |
| RuntimeTrickKeyTableEntry | bin.yml | variable_record | unrecovered | 5 | 0 | no (variable record) |
| RuntimeTrickManagerPrefix | bin.yml | fixed_layout | 5132 | 3 | 1 | mirror |
| RuntimeTrickObject | trg.yml | fixed_layout | 24 | 8 | 3 | mirror |
| RuntimeTrickPendingStartRecord | bin.yml | fixed_layout | 16 | 4 | 0 | mirror |
| RuntimeTrickPointStack | bin.yml | fixed_layout | unrecovered | 4 | 0 | constants and bounds |
| RuntimeTrickPointStackEntry | bin.yml | fixed_layout | 40 | 1 | 0 | mirror |
| RuntimeTrickRecord | bin.yml | fixed_layout | 40 | 5 | 0 | mirror |
| RuntimeTrickScreensInputPanelObject | auto-runtime.yml | fixed_layout | unrecovered | 8 | 0 | mirror |
| RuntimeTrickScreensInputUpdateObject | auto-runtime.yml | fixed_layout | unrecovered | 9 | 0 | mirror |
| RuntimeTrickScreensMenuObject | auto-runtime.yml | fixed_layout | unrecovered | 13 | 0 | mirror |
| RuntimeTrickScreensScreenObject | auto-runtime.yml | fixed_layout | unrecovered | 9 | 0 | mirror |
| RuntimeTricksSectionPointers | bin.yml | fixed_layout | unrecovered | 7 | 7 | mirror |
| RuntimeTriggerLinkRecord | trg.yml | fixed_layout | 24 | 10 | 3 | mirror |
| RuntimeTriggerObjectHeader | trg.yml | fixed_layout | unrecovered | 13 | 2 | mirror |
| RuntimeTriggerRailRecord | trg.yml | fixed_layout | 40 | 8 | 3 | mirror |
| RuntimeTriggerScriptObject0xab | trg.yml | fixed_layout | 204 | 7 | 3 | mirror |
| RuntimeTriggerSubtype0x00cb | trg.yml | fixed_layout | 500 | 45 | 8 | mirror |
| RuntimeTriggerSubtype0x0192 | trg.yml | fixed_layout | 536 | 21 | 3 | mirror |
| RuntimeTriggerTrafficObject | trg.yml | fixed_layout | 488 | 24 | 4 | mirror |
| RuntimeTypeTaggedListNode | core.yml | fixed_layout | unrecovered | 4 | 1 | mirror |
| RuntimeVideoShortsObject | auto-runtime.yml | fixed_layout | unrecovered | 3 | 0 | mirror |
| RuntimeViewportRecord | core.yml | fixed_layout | 164 | 2 | 0 | mirror |
| RuntimeViewportRect | core.yml | fixed_layout | 24 | 6 | 0 | mirror |
| RuntimeWaveFormatEx | audio.yml | fixed_layout | 18 | 7 | 0 | mirror |
| SColCache | collision.yml | fixed_layout | 16 | 4 | 2 | mirror |
| SFaceCache | collision.yml | fixed_layout | 28 | 7 | 0 | mirror |
| SLineInfo | collision.yml | fixed_layout | 144 | 24 | 2 | mirror |

<!-- inventory:end -->

## 3. Risks to watch in 11b

- **The live guard.** `RetailMemory::make_live` asks `probe_` (`IsBadReadPtr`-style) per access when debugging. The
  arena guard is a range check plus the reservation.
- **The upper half of every pointer-sized value the host passes.** HAL callbacks, Win32 APIs and FILE* must never be
  stored into guest memory. Grep for `static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>`; the two allocators
  are the known sites.
- **Wine x86-64 (checked):** Wine 11.0 with a 64-bit prefix (`system32/kernel32.dll` is PE32+), and the pinned
  llvm-mingw ships `x86_64-w64-mingw32-clang++`. Both are present.

## 4. How the audit is kept honest, and what the checks found

1. **Layout asserts (done).** `scripts/guest_layouts.py` generates `src/runtime/guest_layouts_generated.hpp` from `re/types`: sizes, field
   offsets and, for 108 layouts, a packed mirror struct with `static_assert`s on its size and every offset (`GuestPtr<T>`, from
   `src/runtime/guest_ptr.hpp`, for pointers). The other 16 get offsets and `offset + size <= type size` bounds (overlay groups, a field of unknown
   width, or no fields). `tests/test_guest_layouts.py` checks that the header and the inventory above are current, that every sized layout has its
   constants, that the header compiles with the host g++, the 32-bit llvm-mingw and the 64-bit llvm-mingw, and that a changed size fails to compile.
   `python scripts/guest_layouts.py` and `--inventory` regenerate after a change to `re/types`.
2. **Inventory (done).** Section 2.
3. **Kit offsets (done as a worklist).** `python scripts/kit_offset_report.py` writes `build/guest/kit_offsets.md`: raw accesses and distinct
   literal offsets per source group (skater 3726 / 369, level 3159 / 224, world 2637 / 325, render 2059 / 103, bit 1502 / 127, camera 1403 / 154,
   core 1334 / 103; about 40k accesses in all). It does not map an offset to a type, because the base type is only known at the call site; 11c does
   that Kit by Kit, with the 32-bit scenarios and the fixtures as the oracle.
4. **File formats (done, no exception).** `src/assets`, `src/trg`, `src/camera` and `src/collision` read scalars at explicit offsets with
   explicit widths (`read_i16` and friends, `memcpy` of a scalar); the only pointer casts are `Bytef*` for zlib. No host struct is overlaid on file
   bytes. The packed structs under `src/capture/win32` and `src/hooks/hal` are the 32-bit capture/host side, not retail formats.
5. **D9 merges (done)** above; `tony types verify` is valid.

Left for 11b (not audit work): everything in section 1 D1-D3 and D5-D8 is a decision, not code. 11b builds the guest arena, the guest heap and the
handle tables for host objects, then the x86-64 standalone.

## 5. 11b: the guest arena and the guest heap

### 5.1 What s1 built (Opus 5.5)

- **Guest heap** `src/runtime/guest_heap.{hpp,cpp}` (portable, ctest `opentony_guest_heap_test`): manages a guest range, bookkeeping on the host
  side (a game overrun cannot corrupt it), 16-byte alignment, 16 unused bytes before every block (where retail's CRT keeps its header, so a
  small overrun of one block does not land in the next block's data), address-ordered first fit over coalesced free spans, a span that reaches
  the top returns to the top, pages committed through a callback in 1 MiB granules, blocks not cleared (as retail's malloc). A free of anything
  that is not a live block throws. Deterministic: the same calls give the same addresses on every machine, Wine version and environment length.
- **Arena** `src/hooks/kits/guest_arena.{hpp,cpp}` (in `opentony_standalone.dll`): the guest address map (5.2), `arena::host<T>(a)`,
  `arena::at<T>(a)` (a volatile guest word), `arena::text/chars(a)`, `arena::guest(p)` (host pointer to guest address; `MemoryFault` outside the
  arena), `arena::commit`, `arena::heap_allocate/heap_release/heap_owns`. On the 32-bit build `base()` is the constant 0 and the heap is its own
  reservation (512, 256 or 128 MB, whichever fits); on 64-bit `base()` is `OtKitHost::arena_base` and the heap is the fixed guest range below.
- **On the 32-bit build already:** `crt_new` / `crt_free` (malloc, free, operator new and delete) and the surface pixels (`standalone_surfaces.cpp`)
  come from the guest heap. The 14 gated scenarios are the evidence (5.3).
- `RetailMemory::make_live(probe, check, arena_base)`: a live access is `arena_base + address`; `alias_live` is refused unless host pointers are
  guest addresses (the 64-bit arena commits the scratch window itself, D7; `map_scratch` in `kit_dll.cpp` does that when `!arena::kFlat`).
  `retail_live_test` covers a non-zero base.
- `OtKitHost::arena_base` (`kit_abi.h`, ABI version 2); `ot_kit_attach_standalone` calls `arena::attach` before anything touches guest memory.
- The resolver calls natives through `CallTarget` (a `uintptr_t` function), not through `LiveCall::address`: a native adapter's 64-bit address was
  cut to 32 bits there and in `call_host_cdecl`.
- **x86-64 toolchain:** `cmake/toolchains/win64-llvm-mingw.cmake`; `src/hooks/kits/CMakeLists.txt` sets `OPENTONY_KITS64` and then builds only the
  standalone (DLL, spike, video DLL, replays), not `opentony_native.dll`, the Bink compare tool or the movie player. Trial configure:
  `build/m11b/x64.sh` (git-ignored) into `build/hooks/kits64`, with the i686 FFmpeg headers. **`opentony_standalone.dll` and
  `opentony_spike_video.dll` compile and link for x86-64**; the one error is the spike's crash filter (`Context->Esp`). So the remaining work is the
  silent truncations below, not compile errors: none of them warns.

### 5.2 Guest address map (both builds where the 32-bit process allows it)

| Guest range | What | 64-bit | 32-bit |
|---|---|---|---|
| 0 .. 0x00010000 | null guard | never committed | never mapped |
| 0x00400000 .. 0x029de000 | retail image (`.rdata`, `.data`, zero `.text`) | committed by the program, copied at `base + rva` | the `.retail` section |
| .. 0x0a100000 | zone block (0x04f00020 or `OT_ZONE_BLOCK`), session `.heap` regions | committed by the program | the `.retail` section |
| 0x10000000 .. 0x70000000 | guest heap (1.5 GiB, committed on demand) | `arena::kHeapBase` | a reservation wherever it lands |
| 0x7ff00000 .. 0x7ff10000 | scratch window (D7) | committed by `map_scratch` | aliased onto a buffer |
| to 0x1_0000_0000 + 64 KiB | the rest, and a guard after 4 GiB | reserved, no access | n/a |

The 64-bit program should reserve at a 4 GiB-aligned base (try `k << 32` for k = 1, 2, ...), so the low 32 bits of a faulting host address are
the guest address.

### 5.3 Evidence

`tony gates full --only standalone` on the 32-bit build with the guest heap (2026-11-08): all 14 gated scenarios MATCH (5 session scenarios, 9
Warehouse), 20/20 steps. The heap's reservation landed at 0x0a980000 in the session runs and 0x0e990000 under the capture host (512 MB each), so
every C runtime pointer moved off the Wine heap and nothing the comparisons see changed. `opentony_guest_heap_test` and `opentony_retail_live_test`
pass. Not measured yet: the heap's peak (`GuestHeap::stats`, no report printed); log it once when sizing matters.

### 5.4 Worklist for Sonnet 5.5 (11b s2 onwards, in this order; each step keeps the 32-bit gates green) - **done in s2, see 5.5**

Rules for host code: a guest word becomes a host pointer only through `arena::host/at/text/chars`; a host pointer becomes a guest word only through
`arena::guest` (never `static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p))`); a host buffer that a native or seam must see (a file
name, a read buffer, the command line) is a guest heap block, never `std::string::data()`, a host array or a DLL static; a host object retail keeps
as a number (a find handle, a stream) is an index into a host table, never the host pointer or `intptr_t` (D2).

1. **The casts.** Counts of `reinterpret_cast<T*>(static_cast<std::uintptr_t>(word))` / `reinterpret_cast<T*>(kConstant)` /
   host-to-guest truncations at s1: `standalone_host.cpp` 15/0/0, `standalone_spike.cpp` 12/0/2, `standalone_scenario.cpp` 9/0/0,
   `standalone_seams.cpp` 5/8/0, `standalone_session.cpp` 5/0/0, `standalone_platform.cpp` 5/0/0, `standalone_static_init_generated.cpp` 5/0/0
   (change its generator, `scripts/static_init_transcribe.py`, lines ~195-199), `kit_dll.cpp` 3/0/2 (`format_wants_more`, `OT_WATCH_DUMP`),
   `standalone_boot.cpp` 1/3/1, `standalone_audio.cpp` 2/0/2, `standalone_files.cpp` 2/0/0, `standalone_movie.cpp` 2/0/0. Most files have one
   `retail<T>(address)`-style template: make it `arena::at<T>`. `movie_compare.cpp` and `movie_player.cpp` stay 32-bit. Then add
   `tests/test_guest_casts.py`: no such pattern in `src/hooks/kits/standalone_*.cpp` and `kit_dll.cpp`'s standalone part, `guest_arena.*` excepted.
2. **Host buffers handed to the game:** `standalone_audio.cpp` ~615/626 (a name and a read buffer passed to the file seam), `standalone_boot.cpp`
   ~341 (`g_command_line` stored at `kCommandLine`), the spike's `restore` (`pre_name`) and `hostcheck` (`pointer(...)` of host vectors: give the
   DLL an export `ot_standalone_guest_alloc(size)` / `_free` and use guest blocks). `kHostText` in `kit_dll.cpp` is already guest scratch.
3. **Handles:** `crt_findfirst` returns the UCRT's `intptr_t` cut to 32 bits: a small table (index + 1, `-1` on failure as now). Package streams
   (`stream_number`) and the file backend's slots are already numbers. Check the audio and movie seams' handles the same way.
4. **printf family on 64-bit:** `crt_sprintf`, `crt_vsnprintf` and `call_host_cdecl` push 32-bit words at the host's `sprintf`; on x86-64 `%s`
   needs a host pointer, `%f` a double in a register, and a 32-bit `va_list` is a pointer to words in guest memory. Write a guest formatter
   (portable, under `src/runtime`, unit-tested): walk the format in guest memory; per conversion take one word (two for `%f %e %g %E %G`, `%I64`,
   `%ll`), `%s` reads a guest string, `%n` refuses; format each spec with the host's `snprintf` and the right C type; the output goes to guest
   memory. Use it on both builds (the 32-bit scenarios must stay equal), then drop `call_host_cdecl`. `kit_dll.cpp`'s `format_wants_more` reads the
   format through the arena too.
5. **The 64-bit program** (`standalone_spike.cpp` under `#if UINTPTR_MAX > 0xffffffff`): the `.retail` section only on 32-bit; reserve
   `arena::kArenaBytes` (aligned as in 5.2), commit `[kImageBase, kZonesEnd)`, copy the image to `base + rva`, place `.heap` regions at
   `base + where` (no fixed `VirtualAlloc`), `host.arena_base = base`; `peek/poke`, `restore`, `hostcheck` and `OT_SPIKE_DUMP` through the base;
   the crash filter reads `Rsp` and prints `fault - base` as the guest address; the stack walk's DLL range is 64-bit.
6. **Math:** `static_assert(LDBL_MANT_DIG == 64)` in `math_kit.hpp` (D5); x87 `fnstcw`/`fldcw` inline asm is valid on x86-64.
7. **Build and run:** `tony hooks build --arch 64` (or `build_kits64`) into `build/hooks/kits64`; FFmpeg x86-64 DLLs (`tony setup ffmpeg` builds
   i686 only today), the SDL3 x64 DLL; `tony standalone parity|warehouse NAME --arch 64` (Wine 11 runs 64-bit programs in the same prefixes;
   check once). `boot-menu` first, then the other 13; gate steps `standalone64/NAME` and `standalone64-warehouse/NAME` beside the 32-bit ones.

What to expect on 64-bit: guest heap addresses differ from the 32-bit build's (fixed base 0x10000000 against a reservation), which is fine for the
comparisons (the `c-runtime-heap` mask class covers the heap pointers in `.data`; 5.3 shows the 32-bit build already matches with heap addresses
that are not the host heap's). Integer widths are safe (Windows is LLP64: `long` stays 32 bits). Host arithmetic in `uintptr_t` on a guest
address that relied on 32-bit wrap-around would now leave the arena and hit the guard: wrap in `std::uint32_t` first.

### 5.5 What s2 built (Sonnet 5.5): the 64-bit standalone, gated

All seven steps of 5.4. `tony gates full` 135/135 (120 + `hooks-build-64` + 14 `standalone64/*`, `standalone64-warehouse/*`), 11.3 minutes; the 14 scenarios MATCH on the
x86-64 build on the first run of each (Wine 11 runs the 64-bit program in the same prefixes).

- **Casts:** every hand-written guest/host conversion in `standalone_*.cpp` and `kit_dll.cpp` is `arena::host/at/text/chars` (a scripted rewrite; the generated static
  initializers through `scripts/static_init_transcribe.py`). `tests/test_guest_casts.py` forbids the patterns (a line that really is a host address says so:
  "host address, not a guest word"). Beware the rewrite on a value that is already a host address (`mapped()` in `kit_dll.cpp` was one).
- **Host buffers:** `arena::HeapBlock` (scoped guest heap block) for the audio seam's file name and read buffer; the command line is one permanent block; the spike's
  own buffers (`restore`'s name, every `hostcheck` argument) are `Guest` blocks made through the DLL exports `ot_standalone_guest_alloc` / `_free`.
- **Handles:** `_findfirst` handles are a table index + 1 (`g_finds`, eight slots); package streams and file slots were numbers already.
- **printf:** `src/runtime/guest_format.{hpp,cpp}` (ctest `opentony_guest_format_test`) formats over argument words on both builds; `call_host_cdecl` is gone. A `%s`
  whose address is unreadable prints "(unreadable)" (`arena::readable_text`): the career levels' failing assertions (`trickdb.cpp(1335)`, retail's own) format stack
  garbage that retail reads as whatever its process held and the 64-bit arena cannot.
- **Program:** `standalone_spike.cpp` reserves 4 GiB + guard at `k << 32` (first free k; it got 0x200000000 under Wine), commits `[kImageBase, kZonesEnd)`, hands the DLL
  `arena_base`, and its crash filter prints the guest address of a fault inside the arena and DLL offsets (`llvm-nm --numeric-sort`, image base 0x180000000).
- **Tooling:** `tony hooks build --arch 64`, `tony setup ffmpeg --arch 64` (x86_64 target of the same pin, `re/config/ffmpeg.yml` `x86_64:`), `tony standalone
  parity|warehouse --arch 64` (runs and results under `-x64` / `x64/`, the retail reference shared), gate steps as above (each 64-bit step needs its 32-bit twin and reuses
  its retail run). `sdl3.dll_path("x64")` names the 64-bit SDL3 for a windowed run.
- **Not done:** the windowed 64-bit run (SDL3 x64 with `OT_VIDEO_SHOW`, sound) was never started; `movie_compare` / `movie_player` and the hooked build stay 32-bit; no
  run on real Windows (Wine 11 only); the guest heap's peak (`GuestHeap::stats`) is still not printed.

## 6. 11c: typed views, one Kit per session

### 6.1 The pattern (built in the `math` session)

- **Generated views.** `scripts/guest_layouts.py` writes `src/runtime/guest_views_generated.hpp` beside the layout constants: one `<Type>View(RetailMemory&, address)` per recovered
  fixed layout. Per field: `field()` / `set_field(v)` (explicit width; `GuestPtr<Tag>` for a pointer), `field(i)` / `set_field(i, v)` / `field_count` for an array of scalars (an index past
  the end throws `MemoryFault`), `field()` returning the nested view for an embedded layout, and `field_address()` for every field. A view is a handle: its setters are `const`.
  `src/runtime/guest_view.hpp` (hand-written) holds the loads and stores and the two public helpers `guest::read_scalar<T>` / `write_scalar<T>` for a single global or an out-parameter word that no layout contains.
- **Tested against the corpus, not against the generator.** `tests/test_guest_views.py` derives every offset and width from `re/types`, emits a C++ program that sets each field through its view
  and reads the raw bytes (and the bytes around them), and runs it; a mutation of one view offset must fail it. `tests/test_guest_layouts.py` still asserts the layouts in the 32-bit and 64-bit
  compilers; the views compile in all three targets.
- **Kit conversion rules.** The Kit's public methods keep their address arguments (the generated adapters and the fixtures pass words). Inside, a view is built over the address and the arithmetic
  is left alone. Include `guest_views_generated.hpp` from `.cpp` files only (a small `<kit>_views.hpp` shared by the Kit's sources, like `math_views.hpp`; a Kit header never includes it: the views
  are 5,000 lines and a Kit header reaches half the runtime). The Kit's address constants (`gte::kMac`) stay for the other Kits that use them, and `static_assert`s in `math_views.hpp` tie each
  to the generated layout offset. A struct the corpus does not have is recovered first: add the type to `re/types/<subsystem>.yml` with evidence from the disassembly the forced-call fixtures
  already cover, never from a guess; then `tony types verify`, `scripts/guest_layouts.py`, `scripts/guest_layouts.py --inventory`, `tony ghidra sync` last.
- **The guard.** `tests/test_typed_kits.py`: a Kit listed in `TYPED_KITS` has no `m_.u32(...)`-style call left in its sources, and no header includes the views. Add a Kit there in the same commit that converts it.
  What stays raw: `m_.cstring` / `m_.write` on a string or byte buffer.

### 6.2 Kits done

| Kit | Commit | Types added to `re/types` | Left raw |
|---|---|---|---|
| `math` (`math_gte.cpp`, `math_fixed.cpp`, `math_psx.cpp`) | 11c s1 | `math.yml`: `RuntimeGteRegisterFile` (0x120), `RuntimeQ12Matrix`, `RuntimeMatrixTransform`, `RuntimeShortVector3`, `RuntimeIntVector3`, `RuntimePsxRect`, `RuntimePsxDrawAreaPrimitive`; `RetailTimerState` already existed | the card flag and state, the frame clocks, the pause flags and the two accumulators are single globals (`read_scalar` / `write_scalar`); path buffers are strings |
| `collision` (`collision_cache.cpp`, `collision_query.cpp`, `collision_zone.cpp`; `collision_response.cpp` and `collision_recovery.cpp` had no raw access) | 11c s2 | `collision.yml`: `CollisionNormalRecord` (0x8, the face normals); `CollisionZoneGridRecord` grown to its 0x660 stride with `cells` (20 x 20 pointers at +0x20: the stride closes exactly); `SLineInfo.hit_model_index` u16 -> u32 (retail clears and stores it as a dword; `blockmap-collision.md`). Already there: `SLineInfo`, `SColCache`, `SFaceCache`, `RuntimeCollisionObject`, `CollisionModelDataHeader`, `CollisionVertexRecord`, `CollisionFacePrefix` | single globals (masks, flags, the generation, the cache cursors, the skater's surface flags) via `read_scalar` / `write_scalar`; a region's model-pointer table, a cell's item list and the trigger command arrays (arrays with no fixed length); a face's two whole words are composed from the halves the layout names (`face_word0`, `face_flags`) |
| `heap` (`heap.cpp`) | 11c s3 | `heap.yml`: `RuntimeHeapBlockHeader` (0x8: `size_and_tag` read whole, `next_free`), `RuntimeHeapZoneBounds` (0x8), `RuntimeHeapGlobals` (0x38: the free heads, the use counters, the zone table, the low-memory flag, the limit, the allocation counter, the three words nothing writes) | the payload words and bytes of `fill`, `copy`, `copy_bytes` and `swap` (`read_scalar` / `write_scalar`); the per-zone slots are unchecked helpers (`heap_views.hpp`) because `Mem_GetFree` reads the words around the tables for a zone outside 0..1 |

| `core`, first half (`core_init.cpp`, `core_pkr.cpp`, `core_utils.cpp`) | 11c s4 | `core.yml`: `RuntimeListObject` (the head the list sweeps share: vtable, visibility word, Q12 position, two angles, model index, region, next at +0x20, `list_flags` +0x68, `protect_flags` +0x6a), `RuntimeTypeTaggedListNode` (the rail list: next at +8, position +0x10, kind +0x3b, result +0x4a), `RuntimeViewportRecord` (0xa4: camera position +4, rotation +0x74), `RuntimeRelocatableModuleNode` (0x34), `RuntimePkrPackageHeader` (0x10), `RuntimeBufferedStreamObject`, `RuntimeExplosionObject`, `RuntimeExplosionParticle` (0x10). Reused: `RuntimeIntVector3`, `RuntimeShortVector3`, `RuntimeQ12Matrix`, `RuntimeGteRegisterFile`, `SLineInfo`, `RuntimePkrFileRecord` (the compression method is its payload marker word), `RuntimePsxRegionSlot`, `RuntimePsxModelPacketHeader`, `RuntimeHeapZoneBounds` | single globals (`global_word`), words of untyped buffers, out-parameters and scratch locals (`word_at`), strings and byte buffers (`byte_at`), the shorts of `Utils_ShuffleShorts`, the model-pointer table of a region (no fixed length) |
| `core`, second half (`core_display.cpp`, `core_main.cpp`) | 11c s5 | `player.yml`: `RuntimeGameplaySkater` grown by 21 fields the display and the frame read (`position` +0x8, `saved_position` +0x90, `model_matrix` +0x2e58 as `RuntimeMatrixTransform`, `draw_in_front` +0xdc, the three voice handles, the warm-up vectors `warmup_vector` +0x4c and `vector_30f4`, and words named by offset where the meaning is not recovered: `field_2f14`, `field_3210`, `field_31f0` ...). `core.yml`: `RuntimeRenderState` (the ordering tables and the arena base), `RuntimeScreenPrimitive` (0x1c: link, colour word with the channel bytes and the code overlaid, four vertices, depth), `RuntimeViewportRect` (0x18, nine from 0x00533fa8: right, bottom, left, top). Reused: `RuntimeD3DPolygonCommandHeader` (the prefix every primitive of the ordering table shares, for the screen flip), `CCamera` (`camera_mode`, `field_5b4`), `RuntimeListObject` (`next`), `RuntimeMatrixTransform`, `RuntimeQ12Matrix`, `RuntimeGteRegisterFile` | single globals (`global_word` / `global_half` / `global_byte` / `global_int` / `global_short`: about 490 of them); the words of an ordering-table bucket, the second 0x10-byte dimming command, the polygon vertices of the `0xb0` shape, the pointer object (`0x00568614`), the power-up objects' animation word, the font header copy, the replay header and stream buffers, the pad-history buffers, the config tables (`word_at` / `half_at` / `byte_at`, about 120) |
| `m3d` (`m3d_matrix.cpp`, `m3d_boxes.cpp`, `m3d_transform.cpp`, `m3d_collision.cpp`) | 11c s6 | `collision.yml`: `RuntimeCollisionObject` grown to 0x30 (`flags` +0x4, `matrix_scale` +0x28; `LinkedCollisionObjectPrefix` / `ListLinks` name the same words), `RuntimeCullBox` (0x30: two Q12 corners, the screen words, 0 / 0x1000 and the depths `Camera_ProjectBoxCorners` stores), `RuntimeClippedVertex` (0x8: the transformed vertex and its clip word). `math.yml`: `RuntimeShortTransform` (0x18: nine shorts and three of translation), `RuntimeLightSet` (0x20: light matrix, background at +0x14). `core.yml`: `RuntimeListObject.bounding_radius` +0xae (the sphere of `M3dColij_LineToSphere`). Reused: `RuntimeGteRegisterFile`, `RuntimeQ12Matrix`, `RuntimeShortVector3`, `RuntimeIntVector3`, `CollisionModelDataHeader`, `CollisionVertexRecord`, `CollisionNormalRecord`, `CollisionFacePrefix`, `SLineInfo`, `RuntimeViewportRecord` (the camera position) | single globals (`global_word` / `global_int` / `global_short`: the masks, the hit object, the best distance, the plane tables and float limits of the view cull); the null-terminated object array of `Collision_CullObjectsAgainstBox`, the environment tables of the trigger commands (`word_at`) |
| `rail` (`rail_create.cpp`, `rail_nodes.cpp`) | 11c s7 | `trg.yml`: `RuntimeTriggerRailRecord` grown from three named fields to its 0x28 (`vtable`, `rail_kind`, `bounds_minimum` / `bounds_maximum` as `RuntimeIntVector3`, `previous_rail_record`), `RuntimeRailLookupEntry` (0x4: `node`, `leads_from`). Reused: `RuntimeGteRegisterFile` (`ir`, `mac` for the normaliser), `RuntimeIntVector3` | single globals (`global_word` / `global_int` / `global_byte`); the level's node table and a node's type, trigger word and link list (`node_record`, `half_at`, `byte_at`: a node's body depends on its type); the decoded position scratch is a `RuntimeIntVector3` view |
| `mmu` (`mmu_manager.cpp`, `mmu_setup.cpp`, `mmu_screens.cpp`) | 11c s8 | `save-runtime.yml`: `RuntimeMmuSaveManager` rewritten to its 0xb4c (the old one had 16 records and a `file_sizes` array inside them; 15 records of 0x94, the 15th record's last word is the first type word: `overlay_group: record_tail`), `RuntimeMmuFileRecord` (`file_name`, `file_size`, `next_record`), `RuntimeMmuDisplayName`, `RuntimeMmuScreen` (0xdd0: the window, the font at +0xac, the card manager at +0x1f8, the widgets and state to +0xdcc), `RuntimeMmuPromptGroup` (0x208). `frontend.yml` (new): `RuntimeFrontElement` (0x28, the ScreenElement base), `RuntimeFrontMenuProperties` (0x17c: an embedded font, then the scales). `font.yml`: `RuntimeFont` +11 fields (alignment, spacing, colour, shadow, scale). `replay.yml`: `RuntimeReplayHeaderFields` +`costume`, `costume2`. Reused: the replay header for the loaded replay (`finalize_screen`, `replay_load_header`) | single globals (`global_word` / `global_byte`: the card flags, the manager pointer, the return screens, the park buffer, the replay globals, the text tables); the scripted scratch locals (the stat and find records, the 0x200-byte header, the label), the directory scan's record clear, the pad's action bytes and the text box's font pointer (`word_at` / `byte_at`); the manager's arrays by index are unchecked helpers (`file_type_at`, `save_buffer_pointer_at` ...: retail asserts the index and goes on, so a fifth buffer pointer is the first buffer size) |
| `skater`, ground physics (`skater_ground_physics.cpp`; the other `skater_*.cpp` files use the scripted-call helpers and have no raw accessor) | 11c s9 | `player.yml`: `RuntimeGameplaySkater` grown by 76 fields the ground physics reads: 65 scalars named by offset (`field_2c60` ...: the meaning is not recovered), the vectors `acceleration` +0x58, `previous_position` +0xbc, `vector_2da8`, `vector_2e00`, `vector_2e0c`, `vector_2e18`, `vector_3100`, `vector_310c`, `vector_3118`, `vector_3134` (`RuntimeIntVector3`) and `orientation_angles` +0x80 (`RuntimeShortVector3`); `warmup_vector` +0x4c renamed `velocity` (the code moves the position by it). Reused: `RuntimeIntVector3`, `RuntimeShortVector3`, `RuntimeQ12Matrix` (the model matrix's rotation), `SLineInfo` (the query record), `RuntimeListObject` (the platform walk) | single globals (`global_word` / `global_int` / `global_byte`); the scratch vectors and GTE words (`word_at` / `half_at` / `set_word_at`); the action record's pad bytes (`byte_at`: the record behind +0x2ccc has no layout yet); `0x312a` (the middle half of a dword field), the 0x20 eight-byte pad-history entries at +0x2a14 and the hit normal words of the query record at +0x78 / +0x7c (two shorts and a short plus padding) |
| `equip_select`, racks (`equip_select_racks.cpp`; `equip_select_window.cpp` and `equip_select_panels.cpp` are the rest of the Kit) | 11c s10 | `equip.yml` (new): `RuntimeEquipRackBase` (0x14: `single`, `active`, `state`, `offset`, `target`), `RuntimeEquipDeckRack` (0x6e0: the base, `first_update`, three `markers`, `picture`, `slot`, `front`, `target_angle`, `angle`, `step`, `region`, five `records`, 14 `sets`, `set_count`, `set_shown`, `set_now`, `target_y`, `y`, `spool`, `at_front`), `RuntimeEquipDeckSet` (0x5c: `owner`, `deck_count`, `saved_front`, 20 `items`), `RuntimeEquipDeckItem` (0x28: the two priority bytes, `data`, `file_name`, `data_held`, `held_back`, `wanted`, `not_wanted`, `settled`, `picture_shown`, `placeholder_set`, `texture_set`), `RuntimeEquipSpoolQueue` (0x18) and `RuntimeEquipSpoolNode`, `RuntimeEquipTruckRack` (0x2f8: the base, the caption `RuntimeFont`, `style`, `previous_style`, `motion`, `height`, `spin`, `turn`, `region`, five `records`). `psx.yml`: `RuntimePsxObjectRecord` +`field_1c` / `field_1d` / `field_1e` (the three bytes the racks clear). Reused: `RuntimePsxObjectRecord` (the five deck and two truck objects; the two half words at +4 / +6 are written as the one `object_flags` word), `RuntimeFont` | single globals (`global_word` / `global_byte`: the choice words, the pad edges, the loading flag); the slot table, the character file stems and a skater row's flag word (`word_at` / `byte_at`), the marker sprites' two bytes, the region name table; a set's deck items and the sets by index are unchecked helpers (`deck_set_at`, `set_item`: retail asserts the deck count and goes on) |
| `equip_select`, window and panels (`equip_select_window.cpp`, `equip_select_panels.cpp`; the `equip_select` Kit is typed) | 11c s11 | `equip.yml`: `RuntimeEquipWindow` (0x3f4: the ScreenElement base, `page`, the `view` / `name` / `cash` pointers, `dialog_open`, `done`, `dialog`, two embedded `RuntimeFont`s, `background`, six `prompts`, twenty `logos`), `RuntimeEquipStatDots` (0x48: four `rows`, `sprite`, three words named by offset), `RuntimeEquipCashWindow` (0x36c: two fonts, `set`, `index`, `amount`, `x`, `shown`, two `captions`, two `counters`, `frame`), `RuntimeEquipView` (0x50: `page`, two `racks`, `width`, `height`, four `arrows`, `blink`), `RuntimeEquipNameWindow` (0x21c: `text`, `font`, `x`, the shown and wanted set and index, `state`). Reused: `RuntimeFrontElement` (each widget's base: vtable, `x`, `y`, `target_x`, `target_y`, `hidden`, `arrived`), `RuntimeFont`, `RuntimeViewportRect` (the camera rectangle: right, bottom, left, top), `RuntimeIntVector3` (the camera position), `RuntimeEquipRackBase` (`single`) | single globals (`global_word` / `global_byte`: the choice words, the career flag, the screen size); the pad edge bytes and the saved-choice bytes by player (`byte_at`), the career record's cash and class mask and a skater row's flag words (`word_at`), the sprite objects' two bytes, the out-parameter words of `screen_call` / `window_step`; the logo of the shown set is an unchecked helper (`logo_at`: the choice is a character index the screen trusts) |
| `world` (twelve `world_*.cpp`: extra, object, baddy, script, trig, trig_commands, trig_nodes, skater_effects, skater, trick, cmd, traffic) | 11c s12 | `trg.yml` (types grown by `build/c11s12/conv.py`, fields named by offset, base-object offsets named from the `object::` constants): `RuntimePlatformObject`, `RuntimeBaddyObject`, `RuntimeAnimatedObject`, `RuntimeTrickObject`, `RuntimeEditor*Command` (base, node, link, end, C, E, F, G); grown: `RuntimePowerupObject`, `RuntimeTriggerTrafficObject`, `RuntimeTriggerScriptObject0xab`, `RuntimeTriggerSubtype0x00cb` / `0x0192`, `RuntimeListObject`, `RuntimeGameplaySkater` | single globals via `global_*`; any offset a function reads at two widths (or that overlaps another access) and every `VAR + CONST + 4 * k` walk stay `*_at` |
| `bit` (`bit_core`, `bit_spark`, `bit_misc`, `bit_glow`, `bit_shatter`, `bit_dust`) | 11c s12 | new `bit.yml`: `RuntimeBitObject` (the `bit::` base) and one type per class (spark, ribbon, glass piece, polyline, glow, emitter ...), fields by offset | as above |
| every other runtime source (143 files: front end, menus, career, net, render, psx_spool, texture, video, level_gen, level_editor ...) | 11c s14, s15 | s15: `auto-runtime.yml`, 105 types `Runtime<File><Prefix>Object` (the `self` of each method group of three or more in a file: `build/c11s12/spec_auto.py`; fields named by offset, evidence = the subsystem's document) | `self` typed as above; every other variable, table and scratch word stays on the helpers of `core_views.hpp` (`global_*`, `*_at`) |

Tool: `build/c11s12/conv.py SPEC [--apply]` (spec modules `spec_<kit>.py`: types, function-name rules for `self` and named variables, optional header constants; `reset.sh` / `apply.sh`). Pure mechanical rewrite: reads keep their old C++ type, so no behaviour changes; a typed field is used only when every access of that offset has one width.

Next: the big Kits (`grep -c` over `src/runtime/*.cpp` for the raw-accessor pattern ranks them). Find a Kit's raw offsets (`grep -n 'm_\.\(u\|s\|set\)\(8\|16\|32\)' src/runtime/<kit>_*.cpp`), match them to layouts `tests/test_guest_layouts.py` already asserts
(`SLineInfo`, `SColCache`, `SFaceCache`, `CollisionModelCache` ... for `collision`), and recover what is missing before typing it.

## 7. 11d: the `RetailMemory` backend split

### 7.1 What 11d s1 did (Sonnet 5.5): the surface production code uses is now small and pinned

No behaviour change (ctest 226/226, `tony gates full` unchanged, both builds). Before the split could be designed, production code had to stop reaching the sparse backend and the raw accessors in places the 11c guard did not look:

- **106 raw accessor calls** that 11c left (the inline helpers of 22 Kit headers, which `test_typed_kits.py` does not scan, and files that name the memory `m` instead of `m_`) became `guest::read_scalar<T>` / `guest::write_scalar<T>` (`build/c11d/conv_raw.py`, a balanced-parenthesis rewrite; reads keep their C++ type). The only files that name `u8()`...`set32()` are now `retail_memory.hpp`, `guest_view.hpp` and `guest_views_generated.hpp`.
- **`RetailMemory::ensure_region(base, size, probe = 1)`** replaces the twelve `if (!m_.contains(A)) { m_.add_region(A, N); }` scratch-window blocks in eleven Kit files. Same semantics (`!contains` then `add_region`, which throws when live); the call site no longer names the sparse backend.
- **`add_career_live_regions`** (a fixture loader that sat in `career.cpp`) moved to `career_tables.hpp`, beside `add_career_static_regions`.
- **Guard `tests/test_memory_surface.py`**: no raw accessor outside the three accessor-layer files; `add_region` / `regions()` only in `retail_memory.hpp`, `career_tables.hpp`, `create_tables.hpp` and `retail_calls_replay.hpp`; exactly one `RetailMemory` instance in production (`g_memory` in `kit_dll.cpp`).

### 7.2 What production code asks of `RetailMemory` today (`build/c11d/surface.py`, non-test sources; test sources in brackets)

| Method | Uses (files) | Notes |
|---|---|---|
| `u8`..`s32`, `set8`..`set32` | only through `guest::read_scalar/write_scalar` and the generated views | the one place `get`/`put` are reached |
| `cstring` | 650 (109) | NUL-terminated read, throws without a terminator |
| `write`, `copy`, `fill`, `bytes` | 80, 58, 37, 26 | block operations; `copy` never overlaps (retail's `Mem_CopyBytes`) |
| `contains` | 6 (5) [29] | validity checks that must hold on both backends |
| `ensure_region` | 12 (11) | a no-op on a backend that always holds the address |
| `live_address`, `alias_live`, `make_live` | 9, 1, 2 (all `kit_dll.cpp`) | host side: the hooked build's scratch alias and the probe; the standalone passes `arena_base` |
| `add_region`, `regions()`, `operator==` | none in the runtime Kits | fixture side only: 135 `add_region` calls in 45 test files, `regions()` and `==` in `retail_calls_replay.hpp` |

### 7.3 For the split (Opus 5.5 high)

Goal (`PHASE_5_SUMMARY.md` stack, 11d): no sparse backend in a production path; both builds pass the scenarios; the fixtures still replay. D3 stands: one accessor class name, no per-access virtual call.

Facts that shape the design:

- **Production and tests are different compilations of the same runtime sources.** The DLLs (`opentony_native.dll`, `opentony_standalone.dll`, 32 and 64 bit; `OPENTONY_KIT_LIBRARY_SOURCES` in `src/hooks/kits/CMakeLists.txt`) compile `OPENTONY_RUNTIME_SOURCES` into their own objects and do not link the static `opentony_runtime` that the ctest executables use. A per-target compile definition (a flat-only `RetailMemory` for the DLLs) therefore does not create an ODR clash, as long as no target links objects from both flavours. Check that again before relying on it.
- **A live memory is already flat** (`live_`: `arena_base_ + address`, the optional `probe_`, the scratch alias). The sparse half is `regions_` (a `std::map`), `add_region`, `regions()`, `operator==` and the `!live_` branch of `find`.
- **Fixtures need both flavours.** The replay tests (`retail_calls_replay.hpp`, 71 test files by value) need the sparse one; `retail_live_test.cpp` needs the live one inside a test executable (it maps a page below 4 GiB). So the test targets must be able to build both, and `RetailMemory` as seen by a Kit has to be one name per target.
- **Scratch window.** Stack locals of ported functions live at `kScratchBase` 0x7ff00800 (`PORT64_AUDIT.md` D7). Live on 64-bit the arena holds the window (`map_scratch`); on the hooked 32-bit build `alias_live` redirects it to a host buffer (`live_address` turns a guest address into the host one for calls into retail code). The hooked build stays until 5.6 is done, so its alias path has to survive the split or be retired with a scenario to show it is not needed.
- **Fault behaviour.** Sparse: any access outside a region throws `MemoryFault`, which several Kits catch (`career.cpp` `message`, `contains` guards). Flat: the guard is the probe or a range check on the arena. The scenarios and the replays rely on `MemoryFault` only as fail-stop, but `Font::draw` and a few Kits throw it themselves for retail's undefined behaviour: keep the type.
- **Speed.** Every guest access goes through `find`. The split must not add a branch per access to production beyond today's `live_` test (ideally remove it: flat only).

A design that fits, not binding: `template <class Backend> class BasicRetailMemory` (or a macro-selected typedef) with `FlatBackend` (base, optional guard, scratch alias) for the DLLs and `SparseBackend` (regions) plus `FlatBackend` for the test targets, `using RetailMemory = ...` per target; the generated views and `guest::read_scalar` already take `RetailMemory&` and need no change.

Verify with: `cmake --build build/native` + `ctest` (226), `tony gates full` (135 steps: the 14 scenarios on both builds, the hooked-build replays), `pytest -q tests/test_memory_surface.py` (extend it: the sparse symbols, `add_region` and `regions_`, are absent from the production DLLs, e.g. `llvm-nm` on `opentony_standalone.dll` for both architectures), and `tony hooks build` / `--arch 64` for both builds. Update `docs/PORT64_AUDIT.md` D3 and this section, `PHASE_5_SUMMARY.md` (Status, Next, stack row, log), and commit.

### 7.4 What 11d s2 built (Opus 5.5): two builds of one class

- **`retail_memory.hpp`** compiles in one of two flavours, chosen by `OPENTONY_GUEST_MEMORY_FLAT` (default 0). Flat (the production DLLs):
  the arena backend only: `arena_base + address` behind the scratch alias and the optional per-access guard; no `regions_`, `add_region`,
  `regions()`, `operator==`, `std::map`, and no `live_` member (`live()` is `constexpr true`; the memory is live from construction with
  arena base 0, and `make_live` gives it the base and guard). `ensure_region` on an address the arena does not hold is a `MemoryFault`, as
  `add_region` was live. Fixture (the default): the class as before, the sparse regions until `make_live`, then the same arena path.
- **One name, two manglings.** Each flavour sits in its own inline namespace (`opentony::runtime::flat_memory` /
  `fixture_memory`); source code still says `RetailMemory`, but a link that mixed objects of both flavours fails on undefined symbols
  instead of breaking the one-definition rule in silence. `MemoryFault` is outside, shared.
- **Build split** (`src/hooks/kits/CMakeLists.txt`): `opentony_kits_runtime` (the archive `opentony_native.dll` and
  `opentony_standalone.dll` link, both architectures) gets the define `PUBLIC`; a second archive of the same sources,
  `opentony_kits_runtime_fixture` (`EXCLUDE_FROM_ALL`), is what the 32-bit replays of `tony hooks replay32` link. The host build
  (`CMakeLists.txt`: ctest, the layer tests) never defines it. The fixture loaders of `career_tables.hpp` / `create_tables.hpp` are fenced
  with `#if !OPENTONY_GUEST_MEMORY_FLAT` (their data files are still compiled into the DLL archive; nothing links them).
- **Three guards.** Compile time: `kit_dll.cpp` `static_assert`s `!SparseRetailMemory<RetailMemory>` (a concept in the header, itself asserted true in the fixture build so it cannot pass vacuously). Build time: `tony
  hooks build` (and `--arch 64`) reads each production DLL's symbols (`llvm-nm -C`; `check_production_memory` in `tony/hooks.py`) and fails
  on any `RetailMemory` that is not `flat_memory::RetailMemory`, or on none at all. Source: `tests/test_memory_surface.py` checks the CMake
  wiring and that every sparse member of the header sits under the fixture fence (a pytest cannot read the DLLs: `gates full` runs pytest
  beside the hook build).
