# The palette slots and the palette cache (pal.cpp, d3dtex.cpp)

Status: confirmed by retail forced calls (920 cases of the palette functions, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0048a0b0`, `0x0048a320`, `0x0048a360`, `0x0048a3b0`, `0x0048a400`, `0x0048a420`, `0x0048a490`, `0x0048a500`, `0x0048a590`, `0x0048a6f0`, `0x0048a700`, `0x0048a7e0`, `0x0048aad0`, `0x0048ab00`, `0x004da460`, `0x004da4b0`, `0x004da4d0`, `0x004da550`, `0x004da580`, `0x004da590`, `0x004da620`, `0x004da680`, `0x004da780` (23 functions) (slice `render-palette`)
Native: `src/runtime/render_kit.hpp`, `render_palette.cpp`; replay `src/runtime/render_retail_calls_test.cpp`; probe `re/gdb/probes/render_specs2.py` (groups `rpal`, `rpc`); fixtures `re/evidence/fixtures/render-calls-rpal*.txt`, `render-calls-rpc*.txt`

## What is here

**The slots** (`pal.cpp`). The PSX video memory holds a strip of up to 256 palettes of 16 colours and a strip of up to 256 palettes of 256 colours; `Pal_Init` takes the counts, asks the VRAM
allocator for room for both strips, marks every slot free (flag byte 1) and clears the strips' first rows to colour `0x80`. `GetFree16Slot` and `GetFree256Slot` take the first free slot (the assertion
fires when there is none), `Free16Slot` gives one back and `Pal_Cleanup` returns both strips. Every palette the PSX data supplies is also kept as a node of the *source lookup* (a doubly linked list
at `0x0056b2f0`, 0x1c bytes of bookkeeping per node in the byte counter at `0x0056b704`): `Palette_AllocSourceLookupNode`, `RemovePaletteEntry`, `Palette_FindSourceLookupNode` and
`Palette_ClearSourceLookup` (every node nobody uses releases its cache record and its slot). `Palette_CreateFromPsxColors(key, colours, flags)` makes a lookup node for a palette of the PSX data (flag 1: 16 colours, flag 2: 256; both or neither asserts), makes the colours ready (magenta,
`0x7c1f`, is the transparent colour; the others get the opaque bit unless the palette is transparent), takes a slot and sends the colours to VRAM.
`Get16SlotNumber` and `Get256SlotNumber` answer 0.

**The cache** (`d3dtex.cpp`). The D3D texture layer keeps a converted copy of every palette's colours (the 15-bit colours in the pixel format of the device: the bit counts and shifts of each component
are at `0x006a04f8..0x006a0514`) in a record of a doubly linked list (`0x006a0524`..): `Palette_AllocCacheRecord`, `Palette_FreeCacheRecord`, `D3DTEX_FreePaletteEntry`,
`Palette_FreeAllCacheRecords`, `Palette_CacheSource` (the colours the record was made from, at +0xc), `Palette_ConvertColors` (a new record with a converted copy), `Palette_ConvertInPlace` (a non-zero
colour gets the opaque bit first), `Palette_SetCacheColours` (converts, re-uploads every texture that uses the record, keeps the converted colours) and `Palette_ReloadTexture` (the record's colour
pointer is swapped for one re-upload; flag 4 swaps two pairs of shorts of the texture). `Pal_CopyToMemory` and `Pal_StoreFromMemory` (`0x0048aad0`, `0x0048ab00`) are the palette object's thin
wrappers over `Palette_CacheSource` and `Palette_SetCacheColours` (eight or 128 dwords, by the palette's flag).

## Method

The probe pokes the tables (free flags with a mix of 0 and 1, the counts, the strip positions), the cache list and the pixel format of the device (a 555, 565, 4444 or 8888 surface, or random
bit counts), and colour arrays with magenta (the transparent colour), black and the opaque bit scattered among them. The VRAM allocator answers its position through two pointer arguments (a poke), and
the CRT allocator is replayed from scripted values.

## Lessons

(1) The slot allocator is called with eight arguments including two out pointers; the stub pokes the positions and the replay scripts the same values. (2) The conversion of a colour depends on the
device's bit counts, so the generator varies them; with a fixed 555 surface most branches of the conversion are never reached.
