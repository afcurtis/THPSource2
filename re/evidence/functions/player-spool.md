# The player spool manager, the skater view's spool state and the file layer's neighbours (Phase 4.11a2)

Sources: `spooler.cpp` (`0x004b50e0..0x004b5b6d`, 22 functions, 5 of them unlisted by Ghidra), `player_spool.cpp` (`0x004a62b0..0x004a6e5f`, 11 functions), the rest of `pre.cpp`
(`0x004a8f90..0x004a9640`, 11 functions, 5 unlisted) and the run of `fileio.cpp`'s neighbours (`0x00448dd0..0x0044a060`, 14 functions, 2 unlisted). Native:
`src/runtime/player_spooler.{hpp,cpp}`, `skater_spool.{hpp,cpp}`, `pre_manager.{hpp,cpp}`, `file_loader.{hpp,cpp}`. Probes: `re/gdb/probes/pspool_specs.py` (group `pspl`),
`pspool_specs_view.py` (`pskv`), `pspool_specs_file.py` (`prem`, `fiol`, `fade`, `fonto`), run by `frontend_calls.py`; fixtures `re/evidence/fixtures/pspool-calls-{spooler,view,pre,fileio,fade,font}.txt`;
test `src/runtime/player_spool_retail_calls_test.cpp` (`tests/test_player_spool.py`).

## The player spool manager (`spooler.cpp`)

One 0xa10-byte object, its pointer kept at `0x0056db4c`, vtable `0x00519738` (slot 0 is the scalar deleting destructor `0x004b5240`). The select screens queue the `.psh` manifest and the
`.psx` model of a skater through it and step it from their update loop. Entry `i` starts at `manager + 4 + i * 0x28`:

| Offset | Meaning |
|---|---|
| `+0x00` | processed (the file was read, or the region parsed) |
| `+0x01` | the stored copy of the name (a `.psx` name loses its extension: the dot is overwritten with a NUL) |
| `+0x14` | kind: 0 a raw file kept in the buffer, 1 a PSX region owned by the PSX spooler |
| `+0x18` | the region slot `PSX_QueueResource` answered (-1 none) |
| `+0x1c` | the buffer the file was read into |
| `+0x20` | the heap (0 or 1; any other value asserts "Unrecognized heap" and is stored anyway) |
| `+0x24` | the size to reserve (-1: the file's own size; a pad smaller than the file asserts) |

The manager holds `+0xa04` the number of entries queued, `+0xa08` the entry being loaded and `+0xa0c` the mode (0 idle, 1 loading, 2 waiting to be finalised). The byte at `0x005615dc` says the manager is
stepping the file layer (`FileIO_Continue` asserts it is clear).

| Address | Name | What it does |
|---|---|---|
| `0x004b50e0` | `PlayerSpool_Create` | asserts none exists, allocates 0xa10 bytes, constructs, keeps the pointer |
| `0x004b5190` | `PlayerSpool_Destroy` | calls the destructor through the vtable (flag 1) and clears the global |
| `0x004b51b0` | `PlayerSpool_RingOrder` | `out[0..n)` = start, start + 1, start - 1, start + 2, ... with wrap-around (the order the select screen prefers to load skaters in) |
| `0x004b5200` | `PlayerSpool_Construct` | vtable, counters 0, every entry's processed 0, buffer 0, handle -1 |
| `0x004b5240`, `0x004b5260` | `PlayerSpool_ScalarDelete`, `_Destruct` | destructor: vtable, `Reset`, base vtable; bit 0 of the flags frees the object |
| `0x004b52b0` | `PlayerSpool_Reset` | finish a load in progress (`ProcessPending`), release all 64 entries, counters and mode 0 |
| `0x004b5300` | `PlayerSpool_ProcessPending` | wait for the current entry (the PSX spooler's drain or `FileIO_SyncResource`), advance, mode 2 |
| `0x004b5350` | `PlayerSpool_Finalize` | mode 2 -> 1, start the next load |
| `0x004b5370` | `PlayerSpool_QueueResource` | stores the name, finds the extension; `force_raw` (second argument) makes any name a raw file, otherwise `psx` / `PSX` makes it a region; returns the index |
| `0x004b5580` | `PlayerSpool_ForceFinalize` | mode 1, start the first load |
| `0x004b55a0` | `PlayerSpool_IsIdle` | current index == count |
| `0x004b55c0` | `PlayerSpool_Pump` | mode 1: one `FileIO_Continue` (and `PSX_Spooler` for a region), and when `Loading` is clear advance and start the next entry |
| `0x004b5620` | `PlayerSpool_LoadQueuedItem` | a region: `PSX_QueueResource(name, 2, 1, 1)`; a file: open, size checks, reserve, `FileIO_LoadResource` |
| `0x004b57d0` | `PlayerSpool_AdvanceEntry` | processed 1, current + 1; at the end mode 0 and the stepping byte clear |
| `0x004b5810`, `0x004b5830` | `PlayerSpool_HasResource`, `_Acquire` | by name: queued? / processed (asserts it was queued) |
| `0x004b58a0` | `PlayerSpool_EntryReady` | processed, and (kind 1 or a buffer exists) |
| `0x004b5920`, `0x004b5990` | `PlayerSpool_TakeBufferNamed`, `_TakeBuffer` | hand over the buffer, clear it and the processed byte (asserts kind 0) |
| `0x004b5a00` | `PlayerSpool_ReleaseEntry` | a file: free the buffer; a region: `PSX_ParseEnvironment(handle)`, `PSX_ClearNamedRegion(name)`, drain, handle -1 (the processed byte stays) |
| `0x004b5b00` | `PlayerSpool_FindEntry` | exact name match among the queued entries; with the flag the entry must be processed |

Traps: the name copy is `sprintf(entry, "%s", name)` with no bound (a name over 18 characters runs into the kind word); a name without a dot is scanned past its end (the assertion
`irregular file name` fires at each position); queueing into a full queue (64) writes the name over the counters. The probe keeps clear of all three.

## The skater view's spool state (`player_spool.cpp`)

The select screen's skater view embeds a 0x794-byte state block (`this` of these functions; `PlayerSelect` calls them with `view + kSpool`). Fields: `+0x00 + 4i` the loaded `.psx` file of skater i,
`+0x54 + 4i + 0x50j` the index of part j's name in the manifest's list, `+0x644` / `+0x648` / `+0x64c` the manager entries of the `.psx`, the `.psh` and a third (always -1), `+0x650` the skater being loaded,
`+0x654 + 4i` model loaded, `+0x6a4 + 4i` preview object made, `+0x6f4 + 4i` the preview object, `+0x744 + 4i` the costume, `+0x794` the owner (the view; it holds the part-name lists at
`+0x8a0 / +0x920` (animation parts) and `+0x924 + 0x84 i / +0x9a4 + 0x84 i` (model parts)).

| Address | Name | What it does |
|---|---|---|
| `0x004a62b0` | `SkaterSpool_SetSlotBuffer` | free the skater's old file, keep the new one |
| `0x004a6340` | `SkaterSpool_Preview` | the preview object |
| `0x004a63b0` | `SkaterSpool_SlotUsable` | file in memory, or the skater's row flag bit 1 (custom) |
| `0x004a6440` | `SkaterSpool_InstallModel` | clear the PSX region (usage, materials, palette, free), copy the file into a fresh block, `PSX_ParseEnvironment` |
| `0x004a6560` | `SkaterSpool_ResolvePartIndices` | part name -> position in the manifest's list, stored as a dword (the part window reads the low byte) |
| `0x004a6720` | `SkaterSpool_ApplyPartIndices` | 19 calls to the part window with (j, index) |
| `0x004a67a0` | `SkaterSpool_LoadedCount` | skaters with a file in memory |
| `0x004a67c0` | `SkaterSpool_Begin` | reset the manager, nothing queued or loaded, costumes from the players |
| `0x004a6880` | `SkaterSpool_Update` | restart when the costume changed, pump, finish loads when busy, queue the next skater when not |
| `0x004a68f0` | `SkaterSpool_FinishLoads` | take the `.psx` (size <= 0x10800, packet tables, refresh the owner when it is the current skater), take the `.psh` (parse the manifest, resolve the indices) |
| `0x004a6bf0` | `PlayerSpool_LoadResources` | reset, order the skaters by distance from the one on show, free the models of the thirteen furthest (positions 7 to 19 of the order) when more than six are held, queue the first unloaded one within six (`.psh` raw, then `.psx` as a region of 0x10800 bytes on heap 1), make its preview object, kick the manager |

## `pre.cpp` leftovers

The PRE manager object is 0x144 bytes (vtable `0x005196a0`; sixteen buffer pointers at `+4`, sixteen 16-byte names at `+0x44`; global `0x0056b834`). `PRE_RegisterBuffer` keeps a buffer in the first
free slot under a name (three assertions before: buffer, name, not already there; one after: a slot is free). `PRE_Find` stops at the first free slot. `PRE_Dump` logs through `0x004cf850` (the
probe's stub records the format address only). Four functions at `0x004a9610..0x004a9640` return zero or nothing. The ten functions at `0x004a9650..0x004a9bf0` that the 4.12 attribution gave to 4.11a are the camera's
quaternion math (`Camera_*Q12`): moved to 4.15b in `re/xref/milestones.yml`.

## The file layer's neighbours (`0x00448dd0..0x0044a060`)

* `FileIO_OpenLoad` (`0x00448dd0`): `OpenResource`, size in 1..1,999,999, heap 0/1, `AllocLoadedBuffer`, `LoadResource`, `SyncResource`, shrink the block to the file's size rounded to 4. `0x00448f40` stores a word nothing reads.
* `FileIO_FinishRead` (`0x004493f0`, unlisted): close the handle (the word at `0x00561560` as a signed 16-bit value), assert it worked, clear `Loading` and the pending word.
* `FileIO_Continue` (`0x004494b0`): returns at once when the other loader owns the layer; otherwise `Loading` set: when the bytes read equal the size, close (assert), clear, update the loading screen; else one backend read of the whole size, a failure
  counts a retry and draws two error bars (a rectangle built in a local `{0x1e5, 0x12, 10, 10}` and the same with the second short + 0x100: the probe records the stack address as a fixed scratch address and the bytes as the call's text).
* The fade (`0x004497e0..0x004499b0`): three 16.16 colour channels with their steps, a level byte (a weaker fade does not replace a stronger one), a layer, frames left and a flash flag. `Fade_Draw` writes two packets at the draw list's cursor
  (a translucent rectangle scaled by the screen size / 512 and / 240 through the multiply-by-0x88888889 divide, then a mode packet `0xe1000620` fade down / `0xe1000640` flash) and links them on the head of its layer.
* The font object (`0x00449ca0`, `0x00449cb0`, `0x0044a060`): vtable `0x005189f4` and a -1 word at +0x3c; the scalar destructor leaves the base vtable and frees on bit 0.
* `FontTest_Loop` (`0x00449b40`, unlisted): a leftover test screen that draws "Hello world" with `mainf.fnt` on a blue field, double buffering by flipping the draw area's y between 0 and 0x100, until the exit flag `0x0056b0d9` is set; the up and down flags move the text by 0x10.

## Validation

The probe makes up the manager, the state block, the owner and the PRE manager in a scratch slab and pokes the game's own file-layer, fade and flag words, pokes a legal random state per case, calls the retail function, and records the return value, every
written byte and the ordered service calls; the native replay starts from the same bytes. Services are stubbed by address and reported with their arguments: the allocators, the file backend, the PSX region calls, the skater manager, the file-name builder, the
manifest parser, the preview object's constructor, virtual calls through fake vtables, logging, the loading screen. Scripted values (a file's size, the progress word `FileIO_Continue` leaves, a read result, the frame counter and flags of the test screen) are recorded and replayed in order.

8,400 forced-call cases were replayed with no mismatch (fixtures `pspool-calls-{spooler,view,pre,fileio,fade,font}.txt`: 2,860 + 1,430 + 1,430 + 1,000 + 1,200 + 480), every one of the 58 functions at least 130 times (`FileIO_*`: 250, `Fade_*`: 200, `FontTest_Loop` and the font object: 120). gcov of the native code against the fixtures: `player_spooler.cpp` 225 of 226 lines (the one line is the guard that stops a scan of a name without a dot, which retail runs on through memory), `skater_spool.cpp` 167 of 167, `pre_manager.cpp` 63 of 63, `file_loader.cpp` 237 of 237. The replay has teeth: `tests/test_player_spool.py` flips a retail write and the replay fails.

**Exclusions:** the allocator, the backend, the PSX spooler and region parser (native on other models) and the engine calls of the test screen are services; the traps above are not generated; a null model file in `SkaterSpool_InstallModel` would fault inside `Mem_Copy` in retail and is not generated.
