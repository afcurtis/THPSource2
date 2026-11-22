# The custom skater object: its part database, material checksums, textures and regions (create.cpp)

Status: confirmed by retail forced calls (1,280 cases of the 28 functions, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0041f1e0`, `0x0041f350`, `0x0041f370`, `0x0041f4b0`, `0x0041f7c0`, `0x0041f900`, `0x0041fc80`, `0x0041ff50`, `0x004200b0`, `0x004200d0`, `0x00420420`, `0x004206c0`, `0x004208a0`, `0x00420c10`, `0x00420fa0`, `0x00421410`, `0x00421460`, `0x00421690`, `0x00421910`, `0x00421ba0`, `0x00422020`, `0x004221a0`, `0x00422340`, `0x00422b30`, `0x00422d80`, `0x00422e30`, `0x00422ec0`, `0x00423be0` (28 functions) (slice `create-skater-objects`)
Native: `src/runtime/createobj_kit.hpp`, `createobj_object.cpp`, `createobj_texture.cpp`; replay `src/runtime/createobj_retail_calls_test.cpp`; probes `re/gdb/probes/cobj_world.py` (the shared scene), `cobj_specs3.py` (group `cmat`),
`cobj_specs4.py` (`cpart`), `cobj_specs6.py` (`capp`), `cobj_specs7.py` (`cskt`, `cweld`); fixtures `re/evidence/fixtures/createobj-calls-{cmat,cpart,capp,cskt,cweld}*.txt`

## What is here

The editor's custom skater is a `MySkater` object (vtable `0x00518770`, 0x31c bytes) that owns a *part database* (0x208c bytes, vtable `0x0051876c`), a region of its own in the PSX region table (`0x0056d428 +
slot * 0x44`; +0x14 the model table, +0x18 the raw region, +0x38 the part count) and nine texture slots. The fields the code reads: +4 the spooling flag, +8 "needs scale", +0xc the build type, +0x18 the
definition index, +0x1c + 4 * i the animation index of every part slot, +0x68 the skin tone, +0x6c the board checksum, +0x70..0x7c the four body group words, +0x84 the build region, +0x88 the palette buffer
(nine pages of 0x1000 bytes, +0x8c), +0x1f8 the second leg material, +0x1fc / +0x27c the material checksums and their count, +0x280 the part database, +0x284 the record slot, +0x288 + 0x10 * slot the nine bitmap names
(`NONE` when empty) and +0x308 the board preview name.

**The part database** (`Create_SpoolTextures`, 0x41f1e0): the name `sk2def<index>` finds the region (`PSX_FindLoadedRegionByName`); `MySkater__ProcessSpoolerEntry` reads the part manifest (a service that fills a
local array of name pointers and another of texture names, and the count into the database) and gives every part its name, its body group (`Part_GroupIndex`) and the index of its texture name among the group's
names (a new name is appended; at most 16 a group); `MySkater__Free2` reads the animation manifest and gives every part the position of its name in it (0x13 names; `String_FindNoCase` is a service);
`PartDatabase_BuildChecksums` collects the texture checksums of every part's model, at most five a part.

**The checksums.** `Part_CollectChecksums(region, model, list, count)` walks the faces of a model: each face names a texture record (+0x10) and, when its flag 0x20 is set, a palette (+0x1c); the
region's trailing table of materials (`PSX_FindTrailingTables`) must hold a material of that texture (and palette); the checksum is appended once (at most 0x20). `CreatePart_SelectMaterialChecksum` picks the
biggest or smallest texture (width and height as bytes, in the picture memory) among those of the parts of a name; `CreatePart_FindMaterialChecksum` the one texture of a name that the parts of
two other names do not use; `CreatePart_SelectSlotChecksum` maps the nine slots to those lookups; `MySkater__FindBoardChecksum` collects the build region's checksums and, for the board part (animation 0x10),
takes the one that uses most palette (`PSX_ScoreMaterialPaletteUsage`).

**The textures.** `CreatePart_ApplyTexture` puts a bitmap in a slot unless the slot already names it (the digit at offset 7 is the skin tone + '1', unless it is '0'): when the skater spools, the request goes to the
spooler queue (`Spool_Add`), otherwise the bitmap is loaded at once (`Skater_LoadBitmap`, twice for the leg slot 2, which also records the second material) and the palette handle, colour preset and animation of
the slot follow. `CreatePart_ApplyPreviewTexture` does the board's preview the same way (letter `s` or `l` at offset 4). `CreatePart_AttachLoadedBitmap` is what the spooler runs when a bitmap arrives.
`TextureSet_ApplyBodyGroup(kind, set id, tone)` applies a texture set to the slots of a kind (0 head and face, 1 torso, 4 legs, 5 shoes, 6 board preview), resetting the texture names when the set's body group is
another than the skater's; `TextureSet_ApplyPreviewGroups` applies the torso, legs and shoes sets.

**Regions.** `MySkater__AttachRegion` finds a free slot, names the region `CUSTOM<index>`, copies the source region's record into it, clears its tables and gives it an empty model table
(`Collision_InitModelCacheSlot`: 0x13 pointers, the count before them); `MySkater__ResetTextureNames` empties the texture names and restitches the parts (`MySkater__StitchParts`: the parts the body group words select
are made visible, a part map is built, the stitcher copies and welds); `MySkater__AssignModelPartOrder`, `MySkater__ApplyBodyScale` (the custom record's scale byte clamped to 0x5a..0x7d, the three scale shorts
of the object and the region's flag) and `CustomSkater_BuildObject` give a game object the region, the part order and the scale. `MySkater__ReleasePartDatabase` finds the board checksum, counts the materials
it uses up (the usage short at +0x10 of the scene material) and deletes the part database. `MySkater__Free` (destructor), `MySkater__ScalarDelete` and `MySkater__Delete` release the build region's 0x13 models
and tables, count the materials down, delete the database and the palette buffers.

## Method

The probe's scene (`cobj_world.py`) is a part database of 19 parts, a source region with 19 models whose faces use eight materials, the same models in a build region, the scene materials, three texture sets,
fake skaters and a pose; the region records are two or three free records of the game's table. Engine services (the PSX region and material queries, the manifest reader, the texture-set lookups, the file
system's sub-directory, the palette functions of a slot) are stubs that report their arguments; the ones that answer are scripted by the generator. The natives of earlier slices (the part map, the spool queue,
the stitch log / copy / reorder, the GTE) run inline. A group of the scene has its own windows; the arena of the allocator is captured only as far as the code writes.

## Lessons

(1) A stub on a retail address replaces the function for every caller in the capture, so a fake vtable slot must be a function nothing in the flow calls: the first choice, `MySkater__ClassifyPart`, was called
by `MySkater__BuildPartMap` and made the map garbage. (2) A stitch needs all 19 slots filled (`CopyModels` asserts a part count of 0x13): a scene in which some parts are hidden crashes the reorder on a null model, so
every part of the scene is variant 0 of group 0 and the first body group word is 0. (3) `Skater_Fatten` pops its own arguments (stdcall): a cdecl stub on it corrupted the caller's stack and the crash showed as a jump to
address 2. (4) Names built in a local (`skin..._d.bmp` with the digit patched at offset 7) are read before the NUL by the compare, so the generator's names are always at least eight characters.
