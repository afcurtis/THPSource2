# The trigger file (trig.cpp): the `_TRG` image, the command points and the restart nodes

Status: confirmed by retail forced calls (5,919 cases of the trigger file layer, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004c4b10`, `0x004c4b30`, `0x004c4b80`, `0x004c4ba0`, `0x004c4bd0`, `0x004c4c50`, `0x004c4d10`, `0x004c4e30`, `0x004c5100`, `0x004c5130`, `0x004c5380`, `0x004c5420`, `0x004c5440`,
`0x004c5ac0`, `0x004c5d70`, `0x004c5d90`, `0x004c5da0`, `0x004c7ea0`, `0x004c8050`, `0x004c84d0`, `0x004c8550`, `0x004c8650`, `0x004c8f10`, `0x004c8f30` (slice `world-trigger-file`)
Native: `src/runtime/world_kit.hpp`, `world_trig.cpp`; replay `src/runtime/world_retail_calls_test.cpp`; probe `re/gdb/probes/world_specs_trig.py` (group `wtr`); fixtures
`re/evidence/fixtures/world-calls-wtr*.txt`

## What is here

- **The image.** A level's trigger file is a `_TRG` image (`0x4752545f`, version 2) read by `TRG_LoadFile` (`0x004c5130`): the name plus `.trg` is built in a local, `FileIO_OpenResource` answers the size,
  a block of that size is allocated and loaded, and the header is checked (three assertions). At `+8` a word counts the records, at `+0xc` a table of offsets follows; the loader turns each offset into a
  pointer (`0x0056e210` is the table, `0x0056e214` the count, `0x0056e20c` the image) and shrinks the block to the end of the terminating record (type `0xff`), keeping the size in `0x0056dcb0`. A level with
  no file gets the default image of `0x004c5380`: one restart node named "ack" and the terminator.
- **Records and links.** `TRG_DecodeNodePosition` (`0x004c8650`) finds the position of a record by its type: types `0x1f4` and `0x1f5` have it at `(record + 5) & ~3`, types 1 and 7 after a word list and a
  `0xff`-terminated byte list, type 5 after a word list of `+4` words, and types 3, 8, 10, 11, 13, `0x3e8` and `0x3e9` after a word list of `+2` words; it writes three dwords (whole units shifted left 12) to
  its out vector and answers the address after them (the orientation words, then for a node its name at `+6`). Any other type asserts. `TRG_GetNodeLinks` (`0x004c8550`) answers the address of a record's
  links (`+6` for type 1, `+4` for type 5, `+2` for types 2, 3, 6, 8..14 and the `0x3e8` pair; types 4 and 7 and the rest assert).
- **Command points.** Each is `{+0 the stream, +4 collision flags, +5 executed flags, +0xa the record index, +0xc the id, +0x10 the next in its bucket, +0x14 the next in the list}`, in the list at
  `0x0056e220` and in the bucket `id & 0xff` of the 256 at `0x0056dd64`. `TRG_CreateLinkRecord` adds one at the head of both, `Trig_TriggerCommandPoint` runs the stream of the first point with the id whose
  executed bit for the current owner (`byte(counter + 1)`, the counter at owner `+0x2cc4`) is clear, `Trig_ResetCPCollisionFlags` / `Trig_ResetCPExecutedFlags` clear bits across the list and
  `Trig_DeleteCommandPoints` frees them (the byte count `0x0056e224` drops by `0x1c` for each).
- **Restart nodes.** `Trig_ScanRestart` collects the names of the nodes (type 8): up to 19 restart names (`0x0056e164`) and up to 20 "Ho_" home names (`0x0056e1b8`), with the first restart name as the home
  when the level has none and the flag at `0x005614c4` is set. `Trig_FindRestartByName` finds a node by name (case-insensitive) and keeps its index in `0x005477ec`; `Trig_ExecuteRestart` puts every skater
  at that node (position, heading, the copies the camera keeps), runs its command stream, and in a two-player game (not mode 7) moves the head skater of the list by a hundred times its front vector
  and then, if there is a node named "RE_2P", puts it (position, heading) there instead. `FudgeAngles` rebuilds a skater's two view vectors from its heading minus a half turn. `TRG_ExecuteAutoexec` runs the records of type `0xf` (two players) or `4`.

## Method

The image is built in the slab the way the loader leaves it (a table of pointers to records of 0x60 bytes); `TRG_LoadFile` is given the file form (offsets) by its stubbed loader (the size is the
`FileIO_OpenResource` answer and the bytes are scripted into the block). Two skaters (only the fields the restart code touches) are chained as the player record's list. The command-stream interpreter, the net
ring, the allocators and `Mem_Shrink` are stubs; the GTE-free vector helpers, the rotation about y and the name comparison run as retail code.

## Lessons

(1) `TRG_LoadFile` pushes a register it never set (the caller's `ebx`) as the second argument of `FileIO_OpenResource`; the probe records only the name. (2) `TRG_GetNodeLinks` and
`TRG_DecodeNodePosition` fall out of an assertion with whatever the assertion routine left in eax (and `TRG_DecodeNodePosition` indexes past the table for an out-of-range index and may fault): the replay
skips the return value after an assertion. (3) A restart index that is not a node makes `Trig_ExecuteRestart` read a garbage pointer: the generator always names a node.
