# FNT font disk and runtime path

Status: confirmed file read, font record construction, slot lookup, and text
consumer
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0044ada0`, `0x0044aea0`, `0x00449cd0`, `0x0044b000`,
`0x0044b0a0`, `0x0046a770`, `0x0046a8d0`

The executable carries 26 `.FNT` assets. `S2TRICKS.FNT` is 9104 bytes, with
SHA-256 `e4193af0c8136f01f3f9eb3145bb90239fde3f77958b9f10b4e816734330ba73`.

## Disk format and allocation

`0x0044ada0` is the game-owned loader. It obtains the file size through
`0x00449030`, rejects files at or below `0x28` bytes, allocates an exact-size
buffer, reads/synchronizes it with `0x00449230`/`0x00449660`, and passes the
buffer to `0x0044aea0`. The loader then frees the temporary file buffer.

`0x00449cd0` constructs one runtime font object in a `0x14c` allocation. The
first file word is a glyph/record count. The parser consumes a 16-byte record
for each entry, followed by a 0x20-byte 16-colour table and packed glyph data.
The exact glyph-record semantic names are not assigned, but the width and
pointer behavior are stable:

```text
FNT file
  +0x00 u32 record_count
  +0x04 record_count * 0x10-byte records
  +...  0x20-byte colour table
  +...  packed glyph data

runtime font +0x34 -> (record_count + 1) entries, stride 0x08
  entry +0x00 -> 0x5c-byte glyph/image runtime object
  entry +0x04/+0x05/+0x06 -> compact source/atlas bytes
runtime font +0x38 -> record_count
runtime font +0x3c -> packed atlas dimensions/format word
```

For the `S2TRICKS.FNT` witness, the count is 59, the record table occupies
`0x04..0x3b3`, the 0x20-byte colour table begins at `0x3b4`, and packed glyph
data begins at `0x3d4`. Record 0 is `(5, 14, 14, 19)` as four little-endian
words. The runtime entry stores the low bytes of record `+0x0c`, `+0x04`, and
`+0x08` at entry `+0x04`, `+0x05`, and `+0x06`; the full record `+0x00` is
used with entry `+0x05` to advance the packed-data cursor, including the
observed even-byte alignment. This is a direct file-offset-to-entry witness,
while the editor's original glyph metric names remain open.

Each glyph object is allocated through the normal game heap as a 0x5c-byte
image object. Its setup writes the decoded image width/height, bpp mode, and
atlas-format word, then creates the associated PC texture record through the
same image/texture helpers used by the bitmap path. The extra entry is a
blank glyph built from record 0's dimensions (see Phase 4.5 below). The font manager
has eight slots: `0x0044aea0` stores the runtime pointer in
`DAT_0056161c[slot]` and the name in the 0x10-byte name table beginning at
`DAT_0056163c`.

## Lookup and consumer

`0x0044b000(name)` performs the bounded eight-slot name lookup (byte-for-byte
compare of the 16-byte name slots) and returns the runtime font pointer. `0x0044b0a0(name)` performs the same lookup, releases
the font's image/texture objects, and clears the slot. This makes the slot
table and lifetime contract explicit rather than treating `.FNT` as a static
blob.

The front-end/game consumer at `0x0046a770` loads `s2tricks.fnt`, looks it up,
and copies the runtime record fields at `+0x04` through `+0x4a` into the text
rendering globals (`DAT_005619e0` onward), including packed dimensions and the
glyph table. `0x0046a8d0` repeats the same load/lookup/copy path after a level
load. Other menu and HUD code uses the same `Font_Find` boundary.

The glyph image prefix used by this path is also bounded: `0x00456d10`
allocates/initializes a 0x5c-byte object, `0x00456e10` writes image width at
`+0x40`, image height at `+0x42`, slice dimensions at `+0x4e/+0x50`, maps
4/8/16-bpp input to the image bpp mode at `+0x58`, and stores the font's
packed atlas word at `+0x5a`. The texture decoder at `0x004d98d0` receives the
glyph data and its entry width/height before the image object is built. This
proves the FNT glyph -> image -> PC texture ownership edge without assigning
the Direct3D texture implementation's internal fields.

The native recreation in `src/assets/fnt_asset.*` preserves the same bounded
handoff: it copies the 16-byte disk records and 0x20-byte palette table,
retains the packed glyph-data tail, materializes the `record_count + 1` entry
table with the three observed low-byte copies, and reports the `0x14c` font,
`0x08` entry, and `0x5c` glyph-image allocation sizes. The sentinel entry is
kept as a value-owned blank record; graphics-library texture pointers and
atlas decoding remain backend-owned.

`FntRuntimeManager` now supplies the remaining slot/lifetime boundary. It
keeps eight value-owned fonts under the bounded, exact-match name table,
supports both direct file bytes and a copy of a payload returned by
`PreRuntimeManager::find_embedded()`, and releases the complete font on
unload. `text_view()` exposes only the fields the downstream text consumer is
shown to copy: the disk record count, packed atlas/format word, and the
count-plus-sentinel entry span. This makes the temporary PRE payload safe to
free after construction, as in the retail `Font_Load` path.

The native FNT regression exercises the direct parser, PRE-embedded
`S2TRICKS.FNT` handoff, exact-name lookup/unload, sentinel entry, glyph offsets,
malformed count and truncated glyph rejection, and the eight-slot capacity. Exact palette upload,
glyph texture decoding, and the Direct3D object remain presentation/backend
seams because their executable-side semantics are not recovered here.

The proven path is therefore:

```text
S2TRICKS.FNT / menu FNT
  -> 0x0044ada0 file open/read
  -> 0x0044aea0 / 0x00449cd0 runtime font + glyph objects
  -> DAT_0056161c[slot], DAT_0056163c names
  -> 0x0044b000 lookup
  -> text-rendering dimensions/glyph consumer
```

Limits: the exact palette upload and per-glyph texture coordinate semantics
remain shared graphics-library details; the asset loader, runtime allocation,
slot ownership, and consumer boundary are independently supported.

## Phase 4.5 corrections and retail trace validation

Decompiled from the exact build and traced in retail (Warehouse and Hangar;
probe `build/p45/font_trace.py`, fixtures `re/evidence/fixtures/fnt-*.txt`,
generator `scripts/make_font_trace_fixture.py`, replay
`src/assets/fnt_retail_trace_test.cpp`). Twenty `Font_Create` calls replayed.

**Callers pass no atlas format.** `Font_Load(name)` (`0x0044ada0`) takes only a
name and `Font_Create(buffer, name)` (`0x0044aea0`) only the buffer and name. The
word `Font_Parse` (`0x00449cd0`) stores at font `+0x3c` and hands every glyph as
its CLUT position comes from the 16-colour palette allocator:
`(pal16x >> 4 & 0x3f) | (GetFree16Slot() + pal16y) << 6`
(`FntRuntimeFont::clut_word`). All 20 loads matched it (`0x40`, `0x80`,
`0x1640`, `0x1680`, ... with `pal16x = pal16y = 0`); the high half of the value
the decompiler shows is a decompiler artefact.

**Record layout.** A record is four u32 words. Word 0 is the row stride in 16-bit
units; word 1's low byte is the row count (entry `+5`); word 3's low byte is the
pixel width (entry `+4`); word 2's low byte (entry `+6`) is a third metric.
Each glyph's pixels are `2 * stride * rows` bytes, plus 2 when `stride * rows`
is odd, and follow one another after the 0x20-byte palette. Across all 20 traced
loads every `FontGlyph_ConstructImage` call (`0x00456d10`) received exactly that
cursor, width and height, the glyph image object's `+0x40` and `+0x42` fields
equal entry bytes 4 and 5, and the last glyph ends exactly at the end of the
file. The native parser now records each glyph's offset and size and rejects a
file whose glyphs would run past its end (retail would read on).

**The sentinel is a copy of record 0.** The extra entry's bytes `+4/+5/+6` are
copied from file offsets `0x10`, `0x08` and `0x0c` (record 0's words 3, 1 and 2),
not left zero, and its image is built from a `0x12`-filled buffer of record 0's
`stride * rows * 2` bytes with record 0's width and height. The earlier
"trailing header values" description and the native zero entry were wrong.

**Lookup semantics.** `Font_Find` (`0x0044b000`) and `Font_Unload`
(`0x0044b0a0`) compare the 16-byte name slots byte for byte; the earlier
"case-insensitive" claim was wrong. `Font_Create` takes the first slot whose font
pointer is null and never checks for a duplicate, so loading a name twice fills two
slots. Unloading runs the font's deleting destructor and clears the pointer but
**leaves the name in the table**; `Font_Find` returns the first slot whose name
matches, so after such an unload it returns "no font" even if a later duplicate
is loaded, and a name that was never loaded asserts (non-fatal) and returns
garbage read past the table. Native `FntRuntimeManager` reproduces the first
three behaviours and returns null for the last.

**Sources.** Retail loads fonts from a PRE-embedded copy when the container is
loaded (`mainf.fnt` 12,808 bytes, `s2font2.fnt` 3,396, `time.fnt` 1,916, ...) and
from the loose `data\*.FNT` otherwise (`s2font1.fnt` 18,848 and `s2skata1.fnt`
8,588); the same name can be a different file. The replay finds each retail
buffer by size and CRC-32 among the 26 loose fonts and the 54 PRE-embedded
fonts. All 80 parse strictly except `data/LEVSEL.FNT`, whose records ask for more
glyph pixels than the file holds and which no string in `THawk2.exe` names; it is
recorded as a known-invalid file in the test.

**Trace shape.** Both levels create the same ten fonts in the same order. The
frontend loads `mainf` and `s2font2` (slots 0 and 1) and unloads them; `s2font1`
and `s2skata1` reuse slots 0 and 1 and are unloaded; the level then loads `time`,
`score`, `s2tricks`, `ingame1`, `s2font1` and `s2skata1` into slots 0 to 5 and
unloads the last two. Fifteen `Font_Find` and `Font_Unload` calls are interleaved,
and the native manager answers every one the same way.
