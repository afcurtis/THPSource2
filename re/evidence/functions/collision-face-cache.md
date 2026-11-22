# The collision model cache and the query generation (m3dcolij.cpp)

Status: confirmed by retail forced calls (600 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00462490` (`M3D_AdvanceCollisionQueryGeneration`), `0x00463580` (`M3dColij_CacheCanTake`), `0x00463690` (`M3dColij_CacheRemoveOldest`); `M3dColij_FlushCache` (`0x004629f0`) is in `rail-creation.md`
Native: `src/runtime/collision_kit.hpp`, `src/runtime/collision_cache.cpp`; replay `src/runtime/rail_retail_calls_test.cpp`; probe `re/gdb/probes/rail_specs_build.py` (group `rlc`, the cache generators);
fixtures `re/evidence/fixtures/rail-calls-rlcB.txt`

## What is here

- **The query generation** (`0x00533ab4`, a 16-bit counter that the collision queries stamp on the objects they have visited, at `+6`). `M3D_AdvanceCollisionQueryGeneration()` adds one; when the counter
  wraps to 0 it is set to 1 instead and the stamps of every object of the environment list (`0x0056db28`) and of the game list (`0x0056af40`) are cleared, so no object looks as if it had already been visited
  (the objects are linked through `+0x20`).
- **The model cache** is a ring of 20 entries of 0x10 bytes from `0x00567a70` (the item at `+0`, the first of its faces at `+0xc`) over a ring of 500 faces. `0x00533ab8` is the oldest entry (-1: the cache is
  empty) and `0x00533abc` the first face of that entry; `0x00567c88` is the next entry to fill and `0x00567c8c` the next face to write.
- `M3dColij_CacheCanTake(faces)` (cdecl, answers in `al`) says whether `faces` more fit. It asserts that the oldest entry's first face is the cache's first face and that `faces` is at most 500; wraps the
  entry index at 20; says no when the next entry is the oldest one (the ring of entries is full); otherwise the faces must fit between the write position and the oldest face - when the oldest face
  is below the write position and the new faces do not fit before 500 the write position goes back to 0 (stored), and the answer is yes when it is then still above the oldest face.
- `M3dColij_CacheRemoveOldest()` (cdecl) asserts that there is an oldest entry and that its first face is the cache's first face, clears the entry's item, and makes the next entry (wrapping at 20) the
  oldest one with its first face; when that is the next entry to fill the cache is empty again (both slots -1, both counts 0).

## Method

Forced calls on generated cache states (`P410_GROUPS=rlc`, `P410_ONLY=462490,463580,463690`): random entry indices, counts, face positions and sizes in and out of range (the assertions are service
calls), wrap cases for the generation counter with short object lists of the slab. The empty cache case of `CacheRemoveOldest` writes below the captured window in retail (entry -1), so the generator keeps an
oldest entry for it and only lets the assertion fire on a legal index.

## Lessons

(1) `al` is the whole answer of `CacheCanTake`: the upper bytes of eax are whatever the compare left, so the replay compares only the low byte. (2) Both functions test the same invariant through the same
assertion string (the oldest entry owns the first face), so one scripted state exercises both messages.
