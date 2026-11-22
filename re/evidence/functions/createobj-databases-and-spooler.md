# The texture database, the texture-set manager and the bitmap spooler (cre_tex.cpp, create.cpp)

Status: confirmed by retail forced calls (600 cases of the database and manager, 820 of the spooler, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0041d790`, `0x0041d7b0`, `0x0041d7d0`, `0x0041db50`, `0x0041dbc0`, `0x0041dbe0` (6 functions) (slice `create-texture-database`), `0x00422b10`, `0x00422b20`, `0x00423b10`, `0x00423bd0`, `0x00424550`, `0x004245a0`, `0x004245c0`, `0x00424610`, `0x00424630` (9 functions) (slice `create-spooler`)
Native: `src/runtime/createobj_kit.hpp`, `createobj_small.cpp`; replay `src/runtime/createobj_retail_calls_test.cpp`; probe `re/gdb/probes/cobj_specs.py` (group `cdb`) and `cobj_specs6.py` (group `capp`, the spooler's pump); fixtures `re/evidence/fixtures/createobj-calls-cdb*.txt`, `createobj-calls-capp*.txt`

## What is here

**The texture database** (`TextureDatabase_*`, vtable `0x00518764`) is the sorted list of texture entries the create-a-skater editor keeps: the count at +0x404 and the entry pointers from +4. The constructor
empties it, the destructor frees every entry (the count is read again after each free) and restores the base vtable `0x005184b4`; the deleting form frees the object when bit 0 of its flag is set.

**The texture-set manager** (`TextureSetManager_*`, vtable `0x00518768`, 0x6a10 bytes) holds the set records and, at +0x6608, the database. Its constructor builds the base, the database and the set count and then
asks the cretex loader (an earlier slice, a service here) to read the list `0x00525a0c`; its destructor destroys the database and restores the base.

**The part database's destructor** `PartDatabase_Destroy` (vtable `0x0051876c`) clears the PSX region the database was built from (`PSX_ClearRegion(region, 1, 0)`, a service) and restores the base vtable;
its deleting form frees the object. (The constructor is `0x0041f1e0`, which Ghidra also names `Create_SpoolTextures`; it belongs to the skater slice.)

**The spooler** (`0x005601d0`, vtable `0x00518774`) is the queue that loads a skater's texture bitmaps a few at a time. It keeps 64 entries of 0x2c bytes from +8 (name at +4, slot +0x18, free flag +0x1c, three
request words); the constructor and destructor both mark every entry free (`Spooler_Clear`: flag 1) and clear the busy byte at +4. `Spooler_Pump` (`0x00424630`, which Ghidra names `Create_SpoolTextures` too)
is one step: with a load running it pumps the player spool and, when the spool says the bitmap is ready, takes the buffer, attaches it to the skater that asked (the entry's request word C) through
`CreatePart_AttachLoadedBitmap`, frees the buffer and looks for the next request (`Spool_FindFree` compacts the queue and answers the first free index, so 0 means the queue is empty); with nothing running it
copies the front entry to +0xb08, marks the entry free, resets the player spool, queues the entry's file (`PlayerSpool_QueueResource(name, 1, 1, -1)`) and forces its finalisation. It answers 1 when the spooler is
idle and nothing is queued. It switches the file system's sub-directory to `skatedit\` for the step and restores the caller's (kept in a 16-byte local). The thunks `Create_SpoolTexturesThunk`,
`Create_SpoolerUpdate` and `Create_SpoolerUpdateThunk` call the pump, the clear and the constructor on the global spooler, and `Create_ResetSpool` resets the player spool and then clears the queue.

## Method

The probe takes a slab from the game's allocator and lays the objects out in it; the spooler is the game's own global, so its window (0xb40 bytes) is captured. The player spool object, the file system's
sub-directory and the bitmap services are stubs that report their arguments (and `this` for a method of an engine object); the ones that return a value (the ready flag, the bitmap buffer, the queue handle)
answer from the generator. A sub-directory string that lives in the callee's frame is recorded as a fixed scratch address (`stack_to`), and the text of the call is the string.

## Lessons

(1) The clock at `0x0056e31c` ticks on a timer thread, so a window that covers it needs the timer callback held (`0x004dace0`), or every case differs in that one word. (2) A thiscall stub with a zero-argument
list still records `this`, which is how the player spool's methods are told apart from each other on the same object.
