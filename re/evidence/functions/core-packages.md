# The PKR2 package wrappers, the module loader and the explosion object

Status: confirmed by retail forced calls (8,400 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004e9150..0x004e9b80` (PKR2 compression and package header), `0x004ebf10..0x004ebf80` (a stream object), `0x004ac020..0x004ac330` (the relocatable-module
loader), `0x00448860..0x00448ad0` (`exp.cpp`), `0x00448c60..0x00448cf4` (the file head block and a name hash); includes the unlisted `PKR2_CompressedSize` (`0x004e9630`),
`PKR2_BestMethod` (`0x004e9670`), `FileIO_HashName` (`0x00448cb0`), `RelocatableModule_Destroy` (`0x004ac060`) and `RelocatableModule_Empty` (`0x004ac1b0`)
Native: `src/runtime/core_kit.hpp`, `src/runtime/core_pkr.cpp`; replay `src/runtime/core_retail_calls_test.cpp`; probe `re/gdb/probes/core_specs_pkr.py` (group `pkr`);
fixtures `re/evidence/fixtures/core-calls-pkr*.txt`

## PKR2 wrappers

A PKR2 package entry (0x28 bytes) names a compression **method** at `+0x20` (-2 none, 0 byte-count run-length "BIBD", 1 two-byte-count run-length "WIBD", 2 zlib), the stored size
at `+0x28` and the compressed size at `+0x2c`. The method table is at `0x0054b408` (compress: `PKR2_CompressBIBD`, `...WIBD`, `...Zlib`) and `0x0054b414` (decompress).

- `PKR2_CompressBIBD` / `WIBD(source, length, &size)` allocate `2 * length` / `3 * length` bytes with `CRT_malloc`, emit `(count, byte)` pairs (a run past 255 / 65535 is cut and
  the next pair starts at 1), then copy the result into a block of its exact size, release the work buffer and answer the block (0 when an allocation fails).
- `PKR2_CompressZlib` allocates `3 * length / 2` (signed division), calls zlib's compress with **a pointer to its own argument slot** as the length (the service writes the used length there),
  and maps the status: -5 and -4 print an error and answer 0, any other non-zero status prints the status, 0 allocates the exact block and copies.
- `PKR2_DecompressBIBD` / `WIBD(source, length, size, name)` format `name` into a stack local (a service), allocate `size`, and fill runs: a run that would pass `size` is cut and
  the rest of the buffer is filled with that run's byte. `PKR2_DecompressZlib` maps the status to four messages.
- `PKR2_CompressResource(entry, data, release)` calls the method's wrapper (an unknown method below -2 prints a message and becomes "none"); a wrapper that answers 0 turns the entry
  into "none" (`+0x20 = -2`, `+0x2c = +0x28`) and returns `data`; otherwise `data` is released when asked and the compressed block is returned.
- `PKR_PreLoad(name)` keeps the loaded package's name at `0x006a7578` (0x100 bytes) and its two tables at `0x006a7678` (0x28-byte entries) and `0x006a767c` (0x30-byte files):
  a package that is already loaded answers 1; another one is unloaded first; the header is `'PKR2'` plus counts at `+8` and `+0xc`; a bad header or a stream that will not open
  clears the tables. `PKR_UnLoad` resets the name to the empty string and releases the tables.

## Module loader, explosion object, head block

- `RelocatableModule_HashName`: a bitwise CRC (rotate left, polynomial `0xedb88320`, the byte shifted **arithmetically**) of the name; `FileIO_HashName` is the same over the lower-cased name
  with a logical shift. `RelocatableModule_LoadPair(name, argument)` looks the hash up in the list at `0x56b89c`, else allocates a 0x34-byte node, loads `name.bin` and `name.rel`
  (the same local buffer, "bin" overwritten by "rel"), relocates the code block with the table, releases the table and calls the block's entry point.
  `RelocatableModule_ApplyRelocations(table, base)`: a list of `(offset | kind)` words ended by -1; kind 0 adds the base to a word, kind 1 sets the low half of a word to the high half of
  `(next word + 0x8000 + base)`, kind 2 adds the base to the low half, kind 3 to a jump target (26 bits).
- `Exp_HitEnvItem` looks the item up in the environment list, asserts its region is loaded and reads its model description (a table per region at `0x56d43c`, stride `0x44`):
  if bit 1 of the description's first word is set the item shatters as a whole (the byte test is `ax >> 24` of a 16-bit register, always 0, so only the whole-item argument 0xffff does anything:
  `Shatter_Item(obj, 1, 1)`), else it shatters a face when one is given. `Exp_Construct` builds the explosion object (thiscall, seven stack arguments, base class constructor first).
- `FileIO_LoadHed` allocates the directory block (size rounded up to 0x800) once; `FileIO_UnloadHed` releases it.

## Lessons

(1) A name recorded as the text of a service call must be ASCII (the probe decodes it); a name the hash reads can be anything. (2) A capture whose first allocation fails would let the stubbed
zlib write through a null pointer: only the second allocation fails. (3) The relocation base must be the module's own block, or the targets leave the captured memory.
