# The custom skaters: the definition holder, the loading of the definition file and the creation of a skater object (create.cpp, custmgr.cpp)

Status: confirmed by retail forced calls (300 cases of the holder, 492 of the custom skaters, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0042f880`, `0x0042f8b0`, `0x0042f8d0` (3 functions) (slice `custom-definition-holder`), `0x004236d0`, `0x00423800`, `0x00423b50`, `0x0042f950`, `0x0042fa10` (5 functions) (slice `custom-skaters-runtime`)
Native: `src/runtime/createobj_kit.hpp`, `createobj_def.cpp`; replay `src/runtime/createobj_retail_calls_test.cpp`; probe `re/gdb/probes/cobj_specs8.py` (group `cdef`, which takes the services of `cobj_specs4`, `cobj_specs6` and
`cobj_specs7`) on the scene of `cobj_world.py`; fixtures `re/evidence/fixtures/createobj-calls-cdef*.txt`

## What is here

**The definition holder** (vtable `0x00518848`) keeps the four custom skaters' objects at +4..+0x10, the region slot of each at +0x1c..+0x28 (-1 for none), the file image at +0x18 and a word at +0x14.
`CustomDefinition_Construct` clears the objects, the image and the word and sets the slots to -1; `CustomDefinition_Destroy` deletes each skater object through `MySkater__Delete` (clearing the pointer) and
frees the file image; the deleting form also frees the holder.

**`CustomSkater_LoadDefinitionPsx(slot, unused, index)`** (a thiscall on the holder) opens `sk2def.psx`, relocates the definition's pointers for `index` (`PSX_RelocateDefinitionPointers`), takes a free region
slot, publishes the image as that slot's raw region (`0x0056d440 + slot * 0x44`), parses the environment and names the region `sk2def<index>`. **`CustomSkaters_LoadAll`** (a fastcall on the holder) does
this for every custom skater that exists (the valid byte of the record, +0x26 of the 0x80-byte records from `0x0056a488`): it opens the file once, allocates four palette buffers (0x9000 each),
makes the texture-set manager the front end keeps (`0x00561cb4`) if there is none, copies the file image for each skater (`Mem_GetBlockSize`, `FileIO_AllocLoadedBuffer`, `Mem_Copy`), publishes the copy as a region and builds the
skater object with `CustomSkater_Create`, then frees the file and the manager it made.

**`CustomSkater_Create(slot, index, spooling, palette buffer, manager)`** (cdecl) builds the skater object of a custom record: the texture manager (the caller's, or one made for the call and kept in
`0x0056a87c`), the record's first texture set (asserting it names a file whose character at +0xf, the skin tone digit, is not `0`), the object and its part database (`MySkater__Construct`), the tone, the record's colours
(`CustomSkater_LoadColours`), the spooling flag, the region (`MySkater__AttachRegion`), the head and face set, the torso, legs and shoes sets, the board preview, the release of the part database and, when
the skater does not spool, the freeing of the palette buffers. **`CustomSkaters_InitAll`** initialises the record of every selection record that is a custom skater (flag 2 of the record's first byte, at most four)
with a trick manager made for the call. **`CustomSkaters_FreeAll(count, skaters)`** pumps the spooler and, when it is idle, frees the palette buffers of the skaters.

## Method

`cdef` is the integration of the whole chain: the slab holds the scene of `cobj_world.py`, and the group installs the stubs of the groups below it. The part manifest has one texture group name (the name the scene's
texture sets carry), so every part is variant 0 of its group and a restitch leaves all 19 parts visible; the part database's `FindBodyGroup` is emulated by the stub from the database's own group names (the stub reads
`this` from ecx). The records, the selection records and the globals are captured windows. A skater the holder owns is owned once: a second delete would find the base class's vtable.

## Lessons

(1) A callee that the harness stubs for one group (`TextureSet_FindBodyGroup`, needed as a service by the texture application) is a service in every group that calls it, including inside functions that
used to run it inline: the native calls it as a service everywhere and each group's stub answers for its scene. (2) The replay's message for a native exception now says where the native's service calls part from
the retail ones, which turned a null-pointer exception 500 calls in into a one-line diagnosis.
