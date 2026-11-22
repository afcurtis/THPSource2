# Trick database, character records and per-player key tables (trickdb.cpp)

Status: trace-validated against retail (Phase 4.7b); the alternate-stream branch was forced, see "Exclusions".
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/trick_database.{hpp,cpp}`, generated `src/runtime/trick_combo_tables.cpp`
Fixture: `re/evidence/fixtures/trickdb-warehouse.txt` (generator `scripts/make_trickdb_trace_fixture.py`)
Replay: `src/runtime/trick_database_retail_trace_test.cpp`; hand-built cases `trick_database_test.cpp`

This page supersedes the "Per-player key table", "Trick record" and "Runtime character/config consumer"
sections of [bin-runtime.md](bin-runtime.md) wherever they differ. Every statement below was either read
from a decompile checked against the disassembly or confirmed by the retail trace.

## What the code does

```text
TRICKS.BIN section 5 (GAllTricks)
  -> Tricks_InitializeManager 0x004bb4f0  (walks the descriptors)
  -> TrickDatabase_ParseRecord 0x004bb7e0  (one record from a command stream)
  -> TrickDatabase_AddOrMerge 0x004bba50   -> TrickDatabase: at most 0x80 records of 0x28 bytes

TRICKS.BIN section 0 (one s16 stream offset per character) / section 6 (the alternate stream)
  -> TrickConfig_InitializeCharacter 0x004170d0
       -> TrickDatabase_ApplyUnlockState 0x004bbd70      unlock bitmap at record +0xac
       -> TrickDatabase_ApplyCharacterBindings 0x004bbf00 binding block at record +0xcc
  -> TrickTable_BuildForPlayer 0x004bcf00 -> the key table the recogniser walks (4.7a)
```

The manager is **transient**. `TrickRuntime_BuildPlayerTables` (`0x004bd1e0`) allocates one (`0x140c`
bytes), builds the player table(s) from it and deletes it again; the frontend builds further managers
when it initialises a character. The database itself is never stored: what lasts is the two per-player
tables and the character records. A manager that finished `Tricks_LoadBinary` first (the loader returns 1
only while `TricksBinaryBase` is null) owns the loaded buffer (`+0x1408`) and unloads it when it dies;
`TrickScript_Initialize` (`0x00492de0`) does exactly that when it was the first loader.

### The descriptor walk (section 5)

Each descriptor is `[n][n words][source][descriptor]` with `n` in 1..4 (retail asserts otherwise). The
**last** of the `n` words is the type: 1 and 2 -> class `0x2000` (air), 9 -> `0x100` (grind), 10 ->
`0x200` (manual), 12 -> `0x800` (lip); anything else asserts "Unrecognized trick type" and gets class 0.
The next descriptor starts `n + 3` words on; a zero `n` ends the list. `source` is an offset into the
loaded image and is both the record's key and the script's address.

### Record layout (0x28 bytes, at manager `+0x04 + i * 0x28`)

```text
+0x00  name, NUL-terminated (at most 31 characters)
+0x20  u16 source offset (the lookup key)
+0x22  u16 cost (the 'Q' token; 0x7b when a script has none)
+0x24  u16 runtime class flags
+0x26  u16 raw descriptor word
```

### ParseRecord (0x004bb7e0): what retail really does

* The command stream is walked with `TrickScript_CommandSize` (`0x004bf6c0`, now native as
  `trick_command_size`; all 105 scripts walk to their `0x07` terminator).
* Tokens: `0x0b` (vertical tab) and `'P'` carry a name, `'Q'` a cost word, `'X'` flag bits. The first
  `0x0b` name is kept unless a `'P'` name follows, which replaces it (only the first `'P'` counts).
  `'Q'` records the cost (more than one prints "Cost listed n times"), `'X'` ORs a signed word into the
  flags. The name token is processed **when it is met**, so an `'X'` earlier in the stream can change
  how it is cleaned.
* **`FUN_004b8340` lowercases the name in place in the loaded TRICKS.BIN buffer**, before copying it.
  Every trick name in the database is lower case and the buffer the game keeps differs from the file by
  the upper-case letters of the 105 names (`M` line of the fixture). Anything that hashes or compares the
  raw buffer to the file sees that.
* `TrickDatabase_CleanName` (`0x004bb9d0`) drops `{` and `}` and, for the lip class (`0x800`), a leading
  `fs ` or `bs ` (only at offset 0 or 1). Earlier notes and the first native code thought the final
  source character was skipped; the trace showed it is processed like any other (the terminating test
  compares against the string length, not length - 1).

### AddOrMerge (0x004bba50)

Looks the source offset up (`0x004bc300`); a new record copies the name and stores
`flags = class | X bits`, cost, source, descriptor. An existing record keeps its first name (a differing
second name only asserts); a second listing that is special (`0x8000`) sets `0x8000` in both the flags
and the descriptor. After either, the descriptor bits are folded into the flags: `0x800` sets `0x800`;
`0x4000` clears `0x800` and sets `0x4000`; `0x8000`, `0x2000`, `0x1000` set themselves. Capacity is
0x80 records (asserts, then overflows the manager in retail; native throws).

Shipped data: 105 records, 54 of them special (`0x8000`); classes `0x100` 19, `0x200` 17, `0x4000` 11,
the rest are combinations of class and special bits. Costs are 0 to 5000, plus one trick at 20000.

## Character records (0x104 bytes each at 0x00568a6c + character * 0x104)

```text
+0x08  s32 points left          +0x34  u32 class mask (1, or 0x3ff for a custom-skater row, selection flag 0x02)
+0x38  ten cost bytes (input)   +0xac  0x80-bit unlock bitmap (four dwords)
+0xcc  the 0x35-byte binding block:
         +0x00..0x2a  record index (or 0xff) for each of the 43 ordinary combination slots
         +0x2b..0x2f  record index of each of the five special slots
         +0x30..0x34  the special-combination index each special slot is bound to
```

`TrickConfig_InitializeCharacter` zeroes the record, stores the cost bytes (retail adds the character's
stat bonuses first; the native function takes the final bytes) and the class mask, then applies unlock
state (sets every named trick's bit at zero cost) and bindings from the character's key stream
(`section0 + 2 * character`, an s16 offset from the image start). `ApplyCharacterBindings` fills the block
with `0xff`, binds every non-special entry whose button list equals a built-in combination to the record's
index, then binds special entries (descriptor `0x8000`) through `TrickConfig_AssignSpecialSlot`, which
keeps a record on at most one of the five slots (`Career_SpecialTrickAssigned`, `0x00416340`, is the
"already bound" test and is now native as `trick_binding_special_assigned`). Retail also runs it for the
alternate block (`0x0056a690`) from the section-6 stream with argument 1.

### The two combination tables (generated from the executable)

`0x00540cb8`: 30 ordinary combinations of six words (count, three buttons, class mask, slot kind).
`0x00540e30`: 36 special combinations of the same shape. Button codes are the pad's (1 up, 2 down,
3 left, 4 right ... see the 4.7a notes); both tables are generated by
`scripts/generate_trick_combo_tables.py` and `tests/test_trick_combo_tables.py` fails if they drift.
`FUN_004bca80`/`FUN_004bca10`/`FUN_004bcaa0` are plain accessors over the ordinary table.

## The per-player key table (TrickTable_BuildForPlayer 0x004bcf00)

1. `BuildPlayerReturnCombos` reads the player's key stream into fixed six-word entries
   (count, buttons, `[4]` overwritten with the record's flags, `[5]` source). A four-button entry loses
   its last button to the flags word (asserts "Too many buttons"; no shipped entry has four).
2. Passes in this order, each: generic entries whose flags match the mask and which are **not** ordinary
   combinations (`AppendFilteredEntries`), then the bound ordinary combinations of that mask for slot
   kinds 0..3 (`AppendBuiltInCombos`): masks `0x1000`, `0x800`, then the five special slots
   (`AppendSpecialCombos`), then `0x2000`, `0x4000`, `0x300`.
3. A zero count ends the table, which must fit the 0x400-byte destination (section 3 or 4 of the image).

An entry is `count, buttons..., source, descriptor`; the recogniser (4.7a, `trick_recognition.cpp`)
walks exactly this. Which stream and block a player gets is chosen by `TrickTable_BuildForPlayer` from the
selected skater (`Skater_GetSlotRecord`, `Skater_GetResourceSlot`, `FUN_004b0cd0`) and is **not** ported:
the native function takes the stream offset and block as inputs, and the replay checks that the recorded
offset is the character's section-0 stream and the recorded block is that character's initialised block.

## Retail trace

`build/p47b/db_trace.py` (GDB) recorded the manager after every `Tricks_InitializeManager`, every
`TrickConfig_InitializeCharacter` (full 0x104-byte record) and every `TrickTable_BuildForPlayer` call
(stream pointer, binding block, scratch combo table, finished table) in Warehouse (game start and level
load), then again with the selected character overridden (3, 7, 11, 15, 19) and with the alternate
stream forced.

| Measure | Result |
|---|---|
| Manager images replayed (distinct: 1) | 22 |
| Records compared (name, source, cost, flags, descriptor) | 105 |
| Loaded buffer vs native lowercased copy | identical |
| Character records replayed byte for byte | 20 of 20 (incl. the four 0x3ff custom-skater rows 13-16) |
| Alternate binding block | identical |
| Player tables replayed | 7 (characters 0, 3, 7, 11, 15, 19 and the alternate stream), 1,049 words |
| Script command walks (`trick_command_size`) | 105 of 105 end on 0x07 |
| `ctest` checks | 1,315 |

Changing any recorded record field, character-record byte, table word or the alternate block makes the
replay fail (`tests/test_trick_database.py`).

## Exclusions

* Created-skater tables (`Skater_GetSlotRecord != 0`, `Skater_MapEnabledIndex`, `FUN_004b1570`) and the
  second-player build were not recorded; the builder is the same function, only its inputs differ.
* The alternate-stream branch was reached by forcing the selector, not by a natural create-a-skater
  session; its `ApplyUnlockState` variant (argument 1) was never called by retail and is unit tested only.
* `ConsumePointsAndSetBit` with a non-zero cost (buying a trick in the menu) is unit tested only.
* `TrickRuntime_BuildPlayerTables` itself (allocate, build, free) and the manager destructors
  (`0x004bb770`, `0x004bb790`) are ownership plumbing with no native counterpart.
