# The trigger-record writers (trig.cpp)

Status: confirmed by retail forced calls (10,800 cases of the writer classes, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004c87c0`..`0x004c8ef0` (the base class, the node, link, end and four command writers; thirty-six functions) (slice `world-trigger-writers`)
Native: `src/runtime/world_kit.hpp`, `world_cmd.cpp`; replay `src/runtime/world_retail_calls_test.cpp`; probe `re/gdb/probes/world_specs_cmd.py` (group `wcm`); fixtures
`re/evidence/fixtures/world-calls-wcm*.txt`

## What is here

A small class family describes one record of a `_TRG` file each and writes its bytes into a buffer (the level editor saves parks this way). The base class (vtable `0x00519894`, constructor `0x004c87c0`)
keeps the record's size at `+8` and the number of bytes the writer did not need at `+0xc`; `0x004c8810` answers the size and asserts it is set. Every class has the same destructor (the vtable becomes the plain
base `0x005184b4`) and a deleting destructor that calls `CClass_OpDelete` when bit 0 of its flag argument is set (eight of each).

| class (vtable) | record | constructor | writer |
| --- | --- | --- | --- |
| node (`0x005198a0`, size `0x26`) | type 8 | `0x004c8870` (seven fields at `+0x10..+0x28`) | `0x004c88f0` |
| link (`0x005198ac`, size `0x1a`) | type `0xa` | `0x004c8a20`, ends set by `0x004c8a80` | `0x004c8ab0` |
| end (`0x005198b8`, size 2) | `0xff` | `0x004c8bd0` | `0x004c8c20` |
| command C (`0x005198c4`, size `0xc`) | `0xc` | `0x004c8c30` | `0x004c8c90` |
| command E (`0x005198d0`, size `0x16`) | 6 | `0x004c8cd0` | `0x004c8d30` |
| command F (`0x005198dc`, size `0x10`) | 6 | `0x004c8da0` | `0x004c8e00` |
| command G (`0x005198e8`, size `0x10`) | 6 | `0x004c8e50` | `0x004c8eb0` (answers 6), `0x004c8ed0` answers 0 |

The node writer turns the editor's grid cell (x, y, z, rotation) into a record: the position `(x - width/2) * scale + 0xaa`, `-10 - stride * y`, `(height/2 - z) * scale - 0xaa`, the rotation as twelve
bits, a name formatted into the record (a gate name from the cell's gate number, or "RE_2P" / a default) and a trailing marker; it logs the cell and the record's position. The link writer pads to a
word boundary and writes the two ends, the three shorts of the linked node and its type (types `0xd..0xf` go through the gate number table at `0x005286f0`), and asserts the size it computed against the one
it was given. `0x004c8ef0` (with its jump thunk `0x004c8ee0`) clears nine words of the editor's state at `0x0056e230`.

## Method

A writer object (0x40 bytes), the record a link points at and the buffer (at an address that is aligned, even or odd in turn) are in the slab; the editor's grid scale and size are globals. The formatter
(`sprintf`, called from two sites of the node writer) is stubbed at its call sites and writes eight scripted bytes; `CClass_OpDelete` runs as retail code with its `Mem_Delete` call site stubbed.

## Lessons

(1) The three writers that pad test the buffer's alignment with different masks (`3`, `2`, `2`): the node and command C pad to four bytes, the others to two. (2) The node writer's name is *formatted*
over bytes the next stores overwrite, so only the first six of the eight scripted bytes survive in the record.
