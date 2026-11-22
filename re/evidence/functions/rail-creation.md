# The rail set-up (Rail_CreateRails and the CRail constructor)

Status: confirmed by retail forced calls (446 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004aa5d0` (`Rail_CreateRails`), `0x004aa8c0` (`Rail_CreateRuntimeRecord`, the CRail constructor), `0x004629f0` (`M3dColij_FlushCache`)
Native: `src/runtime/rail_kit.hpp`, `src/runtime/rail_create.cpp`, `src/runtime/collision_kit.hpp`, `src/runtime/collision_cache.cpp`; replay `src/runtime/rail_retail_calls_test.cpp`;
probe `re/gdb/probes/rail_specs_build.py` (group `rlc`); fixtures `re/evidence/fixtures/rail-calls-rlc*.txt`

## What is here

`Rail_CreateRails()` (cdecl, no arguments) makes the runtime records of a level's rails (see `rail-nodes.md` for the node table, the trigger word and the lookup table):

1. `NumNodes` must be positive and the first node must not be a rail (assertions); the collision cache is flushed (`M3dColij_FlushCache`: both cached model slots -1, both counts 0, the first word of each of
   the 20 sixteen-byte cache entries at `0x00567a70..0x00567bb0` cleared).
2. Every type-10 or type-11 node becomes type 10, `SetupLookup` numbers the rail nodes, and then **in node order** every rail node that is still a type-10 node starts a record.
   The head of its chain is found by walking back through the lookup table (entry -> the entry that leads to it): the walk stops at a node with no predecessor, at a node whose trigger word carries the
   done bit, or when it comes round to the node it started from. The record is a `CClass_OpNew(0x28)` object built by `Rail_CreateRuntimeRecord(head)`. The lookup buffer is freed (`Mem_Delete`) at the end.

`Rail_CreateRuntimeRecord(node)` (thiscall, `ret 4`) is the CRail constructor. It links the record at the head of the list, checks the node (assertions: in range, type 10 or 11), takes the kind
(`+5` = low four bits of the trigger word) and the initial state (`+4` = 0 when bit 6 of the word is set, else 1) from the first node, starts the bounding box at `+0x7fffffff` / `-0x80000000`, and follows the chain:
every node reached is made a type-11 node (so the set-up does not start a second record there), its position widens the box, and **when the byte `0x005614c4` is set** each segment (a to b) is tested against the world:

- c = the point a quarter of the way from a to b, 10 units up (`y - 0xa000`); d = the horizontal direction across the segment (`Math_NormalizeFixed12` of b - a with y = 0, turned to `(z, y, -x)`), 10 units long.
- Stage 1: two horizontal zone queries across the segment at c (c + d to c - d, then the reverse). A hit (a wall beside the rail) ends the chain.
- Stage 2 (no hit): d doubled; two vertical queries 30 units down at c - d and c + d. The chain ends only if **both** find floor.
- Stage 3 (still not ended): d doubled again, c moved 20 units down, two long horizontal queries (c + d to c - d and back). **No hit in either** ends the chain.
- Where the chain ends this way, the next node's trigger word gets bit 15 (`or byte [word + 1], 0x80`) so no walk goes on through it; otherwise the walk goes on with the next node (`Rail_NextNode` is
  called once more on it, its answer unused) and stops when the next node is the first node again.
- `0x00567c7c` and `0x00567c78` are set to 1 before the tests and `0x00567c7c` cleared after (the collision layer's "queried by the world" flags).

## Method

Forced calls on generated state (`P410_GROUPS=rlc`): the node table of `rail-nodes.md` plus the set-up's services as stubs (the decoder, the link reader, `CClass_OpNew` handing out 16 records of the slab, `Mem_Delete`, and the
two collision queries answering from a scripted list: nothing, everything or a mix). The accessors, `Math_NormalizeFixed12` (halve until inside +-0x6883, the integer root of the sum of squares of the 16-bit
components, `(c << 12) / length`, the x axis for a zero length), and the vector operators are the real retail code in the capture and the port repeats them. A rail walk ends only on a node without a
link or on its first node, so the generator builds rails as paths with forward links (a branch may skip ahead, a path may end at the head of a later path) and as isolated rings, and gives every rail node a trigger
word other than `0xffff` (a chain ending would write through a null pointer).

## Lessons

(1) The decompile's `[esp+X]` offsets move with every pending push: tracking each local as `X - pending` made the three-stage test legible; reading it as one block did not. (2) The second query of a
pair is the same line reversed; the zone query's three answers (the body, the slope, the height) are consumed even when only the body is used. (3) A thiscall constructor with one argument returns `this` in
`eax` (`ret 4`).
