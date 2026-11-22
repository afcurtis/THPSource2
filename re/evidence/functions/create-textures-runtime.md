# Create-a-skater texture database and texture sets (cre_tex.cpp, cre_texset.cpp)

Status: native port of 9 functions, replayed against retail forced calls, including a replay of the real
`CRETEX.BIN` (Phase 4.9; method in [create-skater-runtime.md](create-skater-runtime.md#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/create_textures.{hpp,cpp}`; fixtures `re/evidence/fixtures/create-calls-textures.txt` and
`create-calls-cretex.txt`

## Memory model

One 0x6a10-byte manager object (`DAT_00561cb4` / `DAT_0056a87c` point at it):

```text
+0x0000  set holder: +0 vtable, 0x180 set records of 0x44 bytes from +4, the set count at +0x6604
+0x6608  texture database: +0 vtable, 0x100 pointers to 0x1c-byte entries from +4, the entry count at +0x404
```

The set functions take the holder (the start of the object) as `this`; `TextureDatabase_Find` and `_Allocate`
take the database (`+0x6608`).

```text
database entry (0x1c)    +0 index  +8 name[16] (lower case)  +0x18 flag byte
set record (0x44)        +0 id (= its index)  +4 root set id or -1  +8 name[16]  +0x18 display name[24]
                         +0x30 four entry pointers  +0x40 texture count (< 5)  +0x41 has-second-texture
                         +0x42 flag  +0x43 group (0 hair/face, 1 clothes, 4 legwear, 5 socks, 6 shoes)
```

## CRETEX.BIN

Six sections of NUL-separated strings, each ended by an empty string (`TextureSetManager_LoadCretex`
builds 369 sets from the real 20,460-byte file):

| Section | Group | Record | Notes |
|---|---|---|---|
| 1 | 0 | name, texture, second texture or empty, display name, flag byte | the digit at name 7 of the texture is the skin tone; a new digit starts a new root; the second texture must have `0` at position 7 ("Cap should not have skin tone") |
| 2 | 1 | name, flag byte, three texture names (the third may be empty), one skipped string, display name | every set also gets `handopn1.bmp`; a nonzero flag chains it to the group root and marks its first texture's database entry |
| 3 | - | name, value byte, ... | lower-cased in the file; the value byte becomes the has-second-texture byte of the legwear sets with that name |
| 4 | 4 | name, texture, display name | root id always -1 |
| 5, 6 | 5, 6 | texture, display name | the name is the literal `DUMMY` |
| tail | 0 | three sets the code adds itself: `slick`/`connor`, `caphelmet`/`hudson` (two textures), `afrowhite`/`earl` | flag 1; their root is the root of the last group with skin tone 2 |

## What the replays found

- The decompile dropped the **root of section 4** (the assembly stores `-1`); the real file happened to agree
  with a carry-over guess, a synthetic file did not. Synthetic files (random flags, optional strings,
  upper-case section 3 names) are part of the fixture for that reason.
- `TextureSet_Commit` asserts "Too many textures" (not "texture sets") when the table is full, and re-reads
  the count after the copy: a record written at slot 0x180 lands on the count itself and the new count is that
  record's id plus one. Replayed with the table at 0x17f, 0x180 and 0x100.
- `TextureSet_FindByGroup` returns -1 when nothing matches (the decompile showed no return). `TextureSet_FindRoot`
  returns the index it found, or the set's own index after the "Could not find root" assertion.
- Records are built in locals and copied whole, so the bytes a local never set (the gap after a name's NUL, the
  unused texture pointer slots, stack words inside a database entry) hold stack garbage. The replay does not
  compare them (`Adapter::unspecified`); the native port reports their addresses.
- `TextureDatabase_Find` lower-cases a copy of the name, scans the sorted list until it passes the name, and on a
  miss inserts a new entry in order through `TextureDatabase_Allocate` (which lower-cases the entry it is given).
  Allocation is a service (`FileIO_AllocLoadedBuffer`).

## Validation

622 texture-database and set cases and 16 `LoadCretex`/full-table cases: the real file (369 sets and its
texture database entries, every service call and every written byte) and seven synthetic files.
`src/runtime/create_test.cpp` additionally loads the real file through the generated tables and checks the
result. Gaps: a full database (0x100 entries) is not exercised, and a second texture shorter than eight
characters reads a stale local buffer in retail, so synthetic files avoid it.
