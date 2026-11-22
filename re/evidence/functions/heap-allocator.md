# The game's heap (mem.cpp)

Status: confirmed by retail forced calls (7,000 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0046e7f0..0x0046f52f` (19 functions: `AddToFreeList`, `Mem_Init`, `Mem_NewX`, `Bang`, `Mem_NewTop`, `Mem_DeleteX`, `Mem_ShrinkX`,
`Mem_GetBlockSizeX`, `Mem_GetFree`, `Mem_Copy`, `Mem_CopyBytes`, `Mem_Swap`, `Mem_SuperDebugUsage`, `Mem_NoCheck`, `SuperMem_CheckForLeaks`,
`FileIO_AllocLoadedBuffer`, `Mem_Delete`, `Mem_Shrink`, `Mem_GetBlockSize`)
Native: `src/runtime/heap_kit.hpp`, `src/runtime/heap.cpp`; replay `src/runtime/heap_retail_calls_test.cpp`; probe `re/gdb/probes/mem_specs.py`
(group `mem`); fixtures `re/evidence/fixtures/heap-calls-*.txt`

## Layout

The heap is two zones of the executable's memory (`0x0056a97c`: begin and end of zone 0, `0x0056a984`: of zone 1), each cut into blocks. A block is an
8-byte header followed by its payload:

```text
+0   dword   size << 4 | tag      the payload size in bytes (28 bits) and, in the low nibble, the zone of an allocated block (0 or 1);
                                  a FREE block's nibble is whatever junk was in that memory before (see "Junk nibbles")
+4   dword   next free block      free blocks only: the list of the zone is ordered by address, 0 ends it
+8   ...     payload             a fresh block is filled with 0x33333333 (the whole requested size)
```

Globals: `0x0056a964 + 4 * zone` the head of the zone's free list; `0x0056a96c + 4 * zone` the bytes handed out (payload plus 8 per block; zone 1's word
is `0x0056a970`); `0x0056a98c` the low-memory flag (zone 1's use has reached the limit); `0x0056a990` the limit (`98 * (end - begin) / 100` of zone 1,
set by `Mem_Init`); `0x0056a994` an allocation counter (31 bits).

## What each function does

- `Mem_Init` makes each zone one free block (`size = end - begin - 8`; asserts `begin + 8 < end`), logs, and sets the limit.
- `Mem_NewX(size, zone, flag, placement)`: zone 0 is `Mem_NewTop`. Otherwise the size is rounded up to 4 (asserts: not 0, below 2^28). Without a
  placement it takes the **first** free block of the zone that is large enough; a block that is more than 8 bytes larger is cut at its front (the
  rest is added back with `AddToFreeList`), otherwise the whole block is taken. With nothing large enough it answers 0, or stops in `Bang` when `flag`
  is set and the zone is 1. With a placement address it takes the first free block that reaches past the end of the requested range and cuts it in three
  (before, the new block, after; the tail goes back to the list); when the block does not reach past the range ("unhandled placement new case"
  assertion) it hands out the whole block. Zone 1 allocations update the low-memory flag.
- `Mem_NewTop(size)`: the **last** free block of zone 1 that is large enough, cut at its end (the new block is the tail); zone 1's use word is
  updated and so is the flag. No block that fits: `Bang`.
- `Mem_DeleteX(payload)`: the zone is the tag nibble of the header (asserts it is 0 or 1; otherwise nothing happens); the use counter drops by the size
  plus 8 and the block goes back with `AddToFreeList`.
- `Mem_ShrinkX(payload, size)`: when `size` is more than 8 bytes below the block's size the tail becomes a free block; asserts: size not above the
  block's, a multiple of 4, zone 0 or 1.
- `AddToFreeList(block, zone)`: inserts in address order; a block that touches the one before it, the one after it or both is merged with them.
- `Mem_GetFree(zone)`: `end - used - begin` (any signed byte is accepted: the words around the table are read for a zone outside 0..1).
- The remaining functions are the `Mem_Copy` family (dwords then the last `count & 3` bytes; a negative count copies `count & 3` bytes; alignment
  asserted), `Mem_Swap` (bytes, count asserted below 1,500,000), the empty debug hooks, and the four wrappers other code calls
  (`FileIO_AllocLoadedBuffer` asks `Mem_NewX` for 4 bytes less, at least 4, and adds 0x10 to a placement; `Mem_Delete`, `Mem_Shrink`,
  `Mem_GetBlockSize` shift the pointer by 4).
- `Bang` prints through `0x004e8730` (a PSX stub that does nothing) and spins for ever: no forced call can return from it, so it is not replayed. The
  port reports the print and returns.

## Junk nibbles (a quirk the replay pins)

A free block's header keeps whatever tag nibble the memory had when the block was cut: `Mem_NewX` and `Mem_ShrinkX` give the tail the old nibble of the
word at the cut (`(~old) & 0xf` xored into a value whose nibble is 0xf), `Mem_Init` keeps the nibble of the first word of the zone. `AddToFreeList`
adds the headers of the blocks it merges (`prev & ~0xf` + `block` + `0x80` ...) and xors the nibbles back in, so two junk nibbles that add up to 16 or
more carry one into the size. The port computes the same 32-bit expressions; the probe fills the words a cut can land on with `0x33333333`, zero, all
ones and random words so every nibble case is exercised.

## Replay

`re/gdb/probes/mem_specs.py` builds a random but consistent two-zone heap in a scratch slab (blocks of 4-byte multiples, no two free blocks
side by side, junk nibbles in the free headers, the use counters exact or perturbed, a zone-1 limit around the use) and pokes the globals window
`0x0056a964..0x0056a99b`; the assertion routine and `printf` are the only stubs. Requests are chosen so that `Bang` is never reached (it hangs).
Cases: `AddToFreeList` 500, `Mem_Init` 500, `Mem_NewX` 500, `Mem_NewTop` 500, `FileIO_AllocLoadedBuffer` 500, `Mem_DeleteX` 400, `Mem_Delete` 400,
`Mem_ShrinkX` 400, `Mem_Shrink` 400, `Mem_GetBlockSizeX` 400, `Mem_GetBlockSize` 400, and 300 each of `Mem_GetFree`, `Mem_Copy`, `Mem_CopyBytes`,
`Mem_Swap`, `Mem_SuperDebugUsage`, `Mem_NoCheck`, `SuperMem_CheckForLeaks`; return value, every written byte and the ordered service calls (the
assertion and log stubs) match for all of them. gcov of `heap.cpp`: 270 of 273 lines; the three left are the two `Bang` calls.
