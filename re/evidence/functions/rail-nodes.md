# The rail nodes and the rail records (rail.cpp)

Status: confirmed by retail forced calls (5,400 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004a9f70..0x004aa8a0` and `0x004aaf10..0x004aaf67` (18 functions: `TRG_ReadRailTriggerWord`, `Rail_Type`, `Rail_FlagsPointer`, `Rail_IsRailNode`, `Rail_GetLink`, `Rail_NumLinks`, `Rail_NextNode`,
`Rail_PrevNode`, `Rail_PrevNodeQuick`, `Rail_SetState`, `Rail_ClearState`, `Rail_ActivateNode`, `GetIndexOfNode`, `SetupLookup`, `Rail_Init`, `Rail_DeleteAll`, `CRail_destruct`, `CRail_destroy`)
Native: `src/runtime/rail_kit.hpp`, `src/runtime/rail_nodes.cpp`; replay `src/runtime/rail_retail_calls_test.cpp`; probe `re/gdb/probes/rail_specs.py` (group `rln`);
fixtures `re/evidence/fixtures/rail-calls-rln*.txt`

## What is here

A rail is a chain of **type-10 / type-11 nodes** of the level's trigger file. The node table is `OffsetList` (`0x0056e210`, a pointer to an array of node pointers) with `NumNodes` (`0x0056e214`) entries;
the first word of a node record is its type. Two services of the trigger-file sub-milestone read the rest of a node: `TRG_DecodeNodePosition(out, node)` writes the node's position (three Q12 dwords) to `out`
and returns the address after it, and `TRG_GetNodeLinks(node)` returns the node's link table (a word count, then the linked node numbers).

- **The trigger word** of a rail node is the word at the address `TRG_DecodeNodePosition` returns, **or six bytes further on when `CurrentLevel` (`0x0056a898`) is 10 or more and not 13**.
  `0xffff` reads as 0 (`TRG_ReadRailTriggerWord`) and as "no word" (`Rail_FlagsPointer` returns 0). Bits: 0..3 the kind of rail (`Rail_Type`), 4..5 a mask of the player counts the rail is used
  in (0: all), 6 "the rail starts switched off", 15 "done" (a chain is cut before a node that carries it).
- `Rail_IsRailNode(node)`: type 10 or 11, and the player mask is 0 or shares a bit with `GNumberOfPlayers` (`0x00533f3c`).
- `Rail_GetLink(node, index)` has three assertions (node below `NumNodes`; index below the node's link count; the linked node in the table) and reads the link even when one fails.
  `Rail_NextNode(node)` is the **last** link that is a rail node whose trigger word has no done bit (the loop does not stop at the first); 0 when none.
- `Rail_PrevNode(node)` scans the nodes **from 1** for a rail node whose next node is `node`; `Rail_PrevNodeQuick(node, start)` walks forward from `start` until a node's next is `node`
  (returns that node) or the walk reaches 0 or `start` again (0). A walk that enters a cycle not containing `start` never ends.
- The **lookup table** (`0x0056b858`, 8,000 bytes from `FileIO_AllocLoadedBuffer`, count at `0x0056b85c`): `SetupLookup` numbers the rail nodes in node order from entry 1 (word 0: the node,
  word 1: the entry that leads to it, the *last* node in order that links to it wins), with an assertion that fewer than 2,000 fit. `GetIndexOfNode` searches from entry 1 and asserts when the node is missing.
- The **runtime records** (`RailRuntimeListHead` `0x0056b860`) are 0x28-byte objects, vtable `0x005196a4` (slot 0 = `CRail_destruct`): +4 state byte (0 off, 1 on), +5 kind, +6 first node (word),
  +8 / +0x14 the minimum / maximum of the chain's positions, +0x20 / +0x24 the previous / next record. `Rail_SetState(node, state)` sets the state byte of the record whose chain (followed with `Rail_NextNode`
  from the record's first node, until 0 or back at the first node) holds `node`; `Rail_ClearState` / `Rail_ActivateNode` pass 0 / 1. `Rail_Init` destroys every record through its virtual
  destructor until the head is empty; `Rail_DeleteAll` does the same reading each next pointer first. `CRail_destroy` unlinks a record and leaves it with the base vtable `0x005184b4`;
  `CRail_destruct(flags)` also deletes the block (`CClass_OpDelete`) when bit 0 of the flags is set.

## Method

Forced calls on generated state (`P410_GROUPS=rln`, level 12): a 32-slot node table in a scratch slab (the pointer array is the seed, so a case pokes only the nodes it uses, and only the bytes that differ from the
base), the globals above, the node decoder, the link reader, the lookup buffer allocator and the object delete as stubs with scripted answers (the decoder's position is a `fill=` of the caller's stack
local, recorded as a scripted block of three dwords). Everything else, the accessors calling each other and the record destructors, is the real retail code. Graphs: random links for the single-step accessors; forward-only
links (and a back edge to the start, or isolated rings) wherever a function walks a chain, so no retail walk can run for ever.

## Lessons

(1) Pokes are the slow part of a forced call under Wine: the first version poked 259 runs per case and ran at 70 cases a minute; a seed for the fixed pointers, the nodes a case uses and a "only the bytes
that differ from the base" filter made it about four times faster. (2) `Rail_NextNode` picks the **last** qualifying link, not the first. (3) `Rail_SetState` compares with the full dword argument but the chain
nodes with 16-bit values: the probe passes small node numbers. (4) The assertion routine only logs; every assertion path reads the table entry anyway, so the probe keeps all 32 slots readable.
