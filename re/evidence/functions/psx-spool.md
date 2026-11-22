# The PSX region slots, scene materials and spool queue: `spool.cpp` (Phase 4.11a)

Source: `spool.cpp` (`0x004b1ac0..0x004b5010`, 45 functions, 4 of them unlisted by Ghidra). Native: `src/runtime/psx_spool.hpp` + `psx_spool.cpp` (slots, materials, environments,
queue), `psx_spool_anim.cpp` (animation tables, model files, material selection), `psx_spool_tex.cpp` (texture references). Probe: `re/gdb/probes/psx_specs.py`,
`psx_specs_regions.py`, `psx_specs_anim.py` (groups `psxmat psxreg psxq psxbits psxanim psxtex psxsave`, run by `frontend_calls.py`); fixtures
`re/evidence/fixtures/psx-calls-*.txt`; test `src/runtime/psx_spool_retail_calls_test.cpp` (`tests/test_psx_spool.py`).

The model beneath the loaded game world: up to 20 *regions* (a loaded `.psx` file image plus the pointers the parser publishes into it), the scene *materials* the regions share,
a ring of 20 queued loads and the two *environment* slots. `PSX_ParseEnvironment` (`0x004b2450`, native since Phase 4.5 on a different model, `src/assets/psx_runtime.cpp`) is the
parser; here it is the one seam the code reaches that publishes state (see "Seams").

## Functions

| Address | Name | What it does |
|---|---|---|
| `0x004b1ac0` | `Spool_Init` | reset the queue flags and indices, every slot, the BITS list, the 512 material buckets, the environment list and slots |
| `0x004b1bb0` | `PSX_FindFreeRegionSlot` | first slot with an empty name; asserts when all 20 are taken (returns -1) |
| `0x004b1c20` *(unlisted)* | `PSX_SaveRegionImage` | copy the first `0x7530` bytes of a slot's raw image into a new allocation, kept at `0x0056d978 + slot * 4` |
| `0x004b1c70` *(unlisted)* | `PSX_RestoreRegionImage` | copy it back, free it, clear the cell (asserts when there is no copy) |
| `0x004b1d10` | `PSX_GetModelPointer` | model of a placed object (slot at `+0x1f`, index at `+0x1a`); asserts the slot is usable and has a model table |
| `0x004b1de0` | `PSX_FindModelIndexByChecksum` | linear search of a slot's checksum table, as many entries as the model table's count word |
| `0x004b1f40` | `PSX_GetModelNameChecksum` | the checksum of a placed object's model |
| `0x004b1f70` | `PSX_AllocSceneMaterial` | allocate `0x2c` bytes, link at the head of `bucket[checksum & 0x1ff]`, add `0x30` to the byte counter |
| `0x004b1fd0` | `PSX_UnlinkMaterial` | unlink, free, subtract `0x30` |
| `0x004b2030` | `PSX_FindSceneMaterial` | walk the bucket's chain |
| `0x004b2060`, `0x004b2080` | `PSX_MaterialIteratorBegin/Next` | walk every material (chain, then the following buckets) |
| `0x004b20f0` | `PSX_FindTrailingTables` | walk the `tag, size, data` chunks after the header to the `-1` tag; the tables start after it |
| `0x004b2120` | `PSX_DecrementMaterialUsage` | subtract one from the use count of every material pointer a region's table holds |
| `0x004b2390` | `PSX_FreeUnusedMaterials` | unlink and free every material with a zero use count (release its texture, drop its palette node's count) |
| `0x004b2440` | `PSX_NothingA` | `ret` |
| `0x004b2ac0` | `PSX_AttachEnvironment` | link a region's object chain in front of the attached list, store the current tables for the environment index |
| `0x004b2b70` | `PSX_LoadTextureReferences` | for each texture record of a region: prepare the texture, set up the material's rectangle words and flags |
| `0x004b2f70` | `PSX_ClearTextureReferences` | undo it, newest record first (the loaded flag of each material) |
| `0x004b3090` | `PSX_DetachEnvironment` | unlink the chain of one environment slot from the attached list |
| `0x004b3230` | `PSX_FindLoadedRegionByName` | first slot whose name equals (case-insensitive) |
| `0x004b3270` | `PSX_ClearNamedRegion` | clear that region (and free materials) when it is usable |
| `0x004b32b0` | `Spool_ClearAllPSXs` | drain the queue, clear the environments, every region not marked kept, free materials |
| `0x004b32f0` | `PSX_ClearRegion` | release one region: material counts, object chain, slot fields, environment detach, raw image |
| `0x004b3460` *(unlisted)* | `Spool_ClearUnusedRegions` | clear every loaded region that is neither attached nor kept (does not free materials per region) |
| `0x004b34b0` | `Spool_ClearEnvironmentRegions` | clear both environment regions, free materials, cursor 0 |
| `0x004b34f0` | `BITS_ParseNamedResourceTag` | link a tag's animation list into the BITS list, replace each frame's table index with its material pointer and bump four of its bytes |
| `0x004b3680` | `BITS_FindNamedResource` | find an animation by name in that list: eight characters, bit 5 of each ignored |
| `0x004b3750` | `PSX_SpoolQueueCopyName` | write two strings one after the other into the entry at the write index |
| `0x004b37a0` | `PSX_QueueResource` | the slot of a loaded or queued region of that name, else claim a slot and queue the file |
| `0x004b3990` | `Spool_GetEnvIndex` | which environment slot holds a region (0, 1 or -1) |
| `0x004b39b0` | `PSX_Spooler` | one step of the loader (below) |
| `0x004b3df0` | `PSX_DrainSpooler` | run the spooler and `FileIO_Continue` until nothing is loading and the current entry is idle |
| `0x004b3e20` *(unlisted)* | `PSX_SpoolBusy` | a file is loading or the current queue entry is active |
| `0x004b3e50` | `Spool_StripAnim` | compact a skater model region: colour table, `-1`, checksums, counts move down over the dropped animation data |
| `0x004b40a0` | `PSX_BuildAnimationTables` | keep only the wanted animations of a loaded animation file, repack the hierarchy table |
| `0x004b43e0` | `Skater_LoadModelFile` | load `<name>.psx` for a skater slot: relocate, claim a slot, parse, strip |
| `0x004b4640` | `PSX_InitFreeSlot` | claim a free slot and clear its fields |
| `0x004b46a0` | `PSX_LoadAnimationResource` | the animation file `<name>.psx` as a region of state 2 with its animation tables built |
| `0x004b47a0` | `PlayerSpool_LoadPartManifest` | open `<name>.psh` and hand it to the part-manifest parser |
| `0x004b4ad0` | `PSX_CollectModelMaterials` | the checksums of the materials a model's faces name, without repeats |
| `0x004b4e00` | `PSX_ScoreMaterialPaletteUsage` | the number of palette entries a material's texture uses |
| `0x004b4e70` | `PSX_SelectMaterialForModel` | the board material of a skater: custom skaters take theirs from the part tables, the others pick the material with the most used palette entries |
| `0x004b5010` | `PSX_RelocateDefinitionPointers` | add a value to the material references, the first words of the two table kinds and the palette word of every texture record of a freshly loaded image |
| `0x004b50a0` | `PlayerSpool_ModelSlot` | the model slot of a skater: five per character, plus one and the custom slot index for a custom skater |

(`0x004b2450` `PSX_ParseEnvironment`, `0x004b4940` `PlayerSpool_ParsePartsManifest` and `0x004b1ef0` `Spool_FindEnviroItem` are native on other models and not part of this
page; `0x004b50e0..0x004b5a00` (`spooler.cpp`, the per-player queue) is the rest of 4.11a.)

## The slot table (`0x0056d428`, 20 slots of `0x44` bytes)

`+0x00` name (9 characters and a NUL; empty = free) · `+0x0a` state (0 unusable, 1 loaded, 2 animation resource) · `+0x0b` kept · `+0x0c` model-name checksum table (the first
trailing table) · `+0x10` published objects · `+0x14` model pointer table (its count word is the word before it) · `+0x18` raw file image · `+0x1c` animation tables · `+0x20` animation
hierarchy · `+0x24` hooks · `+0x28` colour (RGBS) table · `+0x2c` texture wib · `+0x30` colour pulse · `+0x38` model count (u16) · `+0x3b` textures in RAM · `+0x3c` textures shrunk /
heap flag the spooler stores · `+0x3d` heap type · `+0x40` file size.

## A region image's trailing tables

`header +4` is the offset of the chunk area; `+8` is a count of `0x24`-byte records at `+0xc`, and the model count follows them. Chunks are `tag, size, data`, ended by the tag
`-1`. Right after it (`PSX_FindTrailingTables`): the model-name checksums, then `M` and `M` material references (checksums in a fresh file, pointers once parsed: the relocation adds
a per-skater value to them), then `A` and `A` nine-word entries, `B` and `B` entries of `0x81` words, `C` and `C` offsets of the texture records. A texture record holds the flags
(bit 0: tiled), the colour count (`0x10` or `0x100`, otherwise the assertion `Bad value %d for NumColours`), the palette offset, the index of its material, the width (u16 at `+0x10`)
and the height (u16 at `+0x12`).

## Scene materials (`0x2c` bytes)

`+0x00..` rectangle bytes written by the texture loader · `+0x02` the palette node's first halfword · `+0x10` use count (u16) · `+0x12` loaded · `+0x13` tiled · `+0x14` texture ·
`+0x18` checksum · `+0x1c` palette lookup node (its use count is the u16 at `+4`) · `+0x20/+0x22` rectangle halfwords · `+0x24/+0x28` hash chain next/previous. The hash table is
512 heads at `0x0056cc18`; the iterator state is `0x0056d418` (next node) and `0x0056d424` (bucket). The walk can run on into the iterator's own cell at the end; native reads the same
cells.

## The spooler (`PSX_QueueResource` / `PSX_Spooler`)

The queue is 20 entries of `0x11` bytes at `0x0056d9c8`: `"<name>.psx"` ending at `+0x0c` at the latest (a nine-character name overwrites the active byte: retail quirk, replayed), `+0x0d`
active, `+0x0e` kind (0 plain, 1 environment, 2 animation), `+0x0f` slot, `+0x10` a byte stored in the slot's `+0x3c` when loading starts. `0x0056db30` is the read index,
`0x0056db34` the write index, `0x0056db48` the mode: **0** open the file (size from `FileIO_OpenResource`), pick the slot (an environment takes a free environment slot, or evicts
the one under the cursor), allocate the raw buffer on the slot's heap and start the read, return the slot; **1** wait while `Loading` (`0x005615d0`) is non-zero; **2** parse
(`PSX_ParseEnvironment`), attach an environment and store its trailing tables, deactivate the entry, advance the read index modulo 20 and return 1 on the wrap (the quotient).

## Ghidra traps found (disassembly decided each)

* Four functions are unlisted: `0x004b1c20`, `0x004b1c70`, `0x004b3460`, `0x004b3e20` (the 4.12 count of 41 for `spool.cpp` was therefore 45; `0x004b2a7c..0x004b2ab3` is the jump
  table of `PSX_ParseEnvironment`, not code).
* `PSX_LoadTextureReferences`: `Options_ReturnZeroG` (8 arguments, cdecl) and `FUN_004e8770` are empty functions, so the rectangle words (`+0x20` and the low nibble
  of `+6`) come from an uninitialised local: the replay does not compare them (`unspecified()`), the second argument's low half at `+0x22` it does. The `0x200` bit of the tpage word
  comes from the second argument (the decompile was right; reading the `mov cx,[edi]` before it as the source was wrong: the replay of 12 smoke cases told them apart).
* `PSX_ClearNamedRegion` calls `PSX_ClearRegion(region, 1, 0)`: the decompile showed three arguments to `FindLoadedRegionByName`.
* `PSX_Spooler` returns the slot, `Loading`, 0, or the quotient of the index advance depending on the path; the bad-mode path returns what the assertion routine leaves.

## Validation

The probe replaces the whole region world (slots, buckets, queue, environments, region images, materials, objects) with a made-up but legal one in a scratch slab, pokes a random state
per case and calls the retail function. Services are stubbed by address and reported in order with their arguments: the assertion (message address), the allocator, `Mem_Delete`,
`Mem_` virtual deletes, texture release, palette lookup, file open/load/continue, the region parser, `FileIO_OpenLoad`, the cheat query, `D3DTex_PrepareTexture`,
`D3DTex_CountUsedPaletteEntries`, the model roll. The stubbed parser publishes the words of a block in the image (`+0x5c`) into the slot; the replay's `set_parse_hook` does the same.

3,759 cases were replayed with no mismatch (fixtures `psx-calls-{mat,reg,queue,bits,anim,tex,save}.txt`: 1,047 + 1,159 + 460 + 120 + 767 + 200 + 6), every one of the 45 functions at least six times (most
60 to 120 times). gcov of the native code against the fixtures: `psx_spool.cpp` 490 of 493 lines, `psx_spool_anim.cpp` 322 of 322, `psx_spool_tex.cpp` 82 of 82; the three lines no case reaches are the null-pointer
guards of the name compare (no caller passes null) and an iterator exit the bucket arithmetic cannot reach.

**Exclusions:** `PSX_ParseEnvironment` is a seam (above); the rectangle words of `PSX_LoadTextureReferences` that come from an uninitialised local are not compared (`+0x20`, the low byte of `+6`); cases whose
retail run faults are skipped (a null raw image after a failed assertion, a model index below zero, the walk of a non-BITS image by `BITS_ParseNamedResourceTag`), so the assertion paths that are
followed by a fault are reached only up to the assertion; a fault in the skipped cases is retail's, not the port's.
