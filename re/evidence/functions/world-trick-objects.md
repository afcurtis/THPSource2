# The trick objects (trickob.cpp)

Status: confirmed by retail forced calls (3,240 cases of the trick object layer, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004bd700`, `0x004bd720`, `0x004bd740`, `0x004bd760`, `0x004bd920`, `0x004bd940`, `0x004bd980`, `0x004bdbd0`, `0x004bdc40`, `0x004bdd00`, `0x004bdea0`, `0x004be0b0`, `0x004be340`, `0x004be350`
(slice `world-trick-objects`)
Native: `src/runtime/world_kit.hpp`, `world_trick.cpp`; replay `src/runtime/world_retail_calls_test.cpp`; probe `re/gdb/probes/world_specs_trick.py` (groups `wtk`, `wtk2`); fixtures
`re/evidence/fixtures/world-calls-wtk*.txt`

## What is here

A trigger file marks some of its records (type `0xc` and `0xe`) as the things a skater can touch to score or to paint: in the tag game (mode 8) the level's objects change colour as the skaters touch them.
Each becomes a trick object (vtable `0x0051982c`) in the list at `0x0056db90`: `{+4 the model's name checksum, +8 the record index (a word), +0xa activated, +0xb the colour (1 for the first player, 2 for the
other), +0xc the strength (a word), +0x10 the next object, +0x14 the model item it painted}`.

- **Construction** (`0x004bd760`, two arguments: the record index and a kind below 3): three assertions (index below the record count, kind below 3, record type `0xc` or `0xe`), the object is linked at the
  head, and the checksum is the dword after the record's link words (aligned to four). The deleting destructor (`0x004bd920`) and the destructor (`0x004bd940`) unlink it; `0x004bd700` deletes every object
  (the head is read again after each delete) and `0x004bd720` walks the list taking the next object first.
- **Activation** (`0x004bd980`, the model, the skater and a strength): the model's checksum finds the object; in the tag game a model painted by the other player is stolen if the new strength is not
  smaller than the old one (the net game is told, the "steals" message shown on the skater's panel, the sound played once), in the goal game (mode 1) the goal is marked; then the colour/strength are
  recorded for the replay recorder, the links of the record are followed (another trick object is activated through `0x004bdc40`, anything else gets a trigger pulse) and the object is marked activated
  with the model. `0x004bdc40` (`ActivateGoalObject`) finds the model item of a record's object by its checksum and paints it the last activation's colour and strength; `0x004bdbd0` hands the item to
  the owner in the tag game. `0x004be350` offers every model of the environment to the objects at the start of a round (with a random strength above 1000).
- **The flicker** (`0x004bdd00`): while a round is being set up (`0x0056db8c`) the painted items flash through a table of masks by their address and the frame counter (`0x0056e320`); otherwise, in the tag
  game, the colour of an item pulses with the strength it was painted with (the period is about `strength * 64 / 1000` half-steps); elsewhere it pulses a fixed triangle wave.
- **The lines** (`0x004bdea0`, five arguments: x0, y0, x1, y1 and a colour word): the map's outline of the object boxes. The endpoints are scaled (the compiler's multiplications by magic constants,
  reproduced exactly), ordered, clipped to the 512x240 screen (the integer line clipping with `idiv`), scaled again to the display and linked into the ordering table as a primitive of 0x14 bytes at
  the primitive cursor (`0x00560fd0`, limited by `0x00568628`). `0x004be0b0` draws the four sides of an object's model box.

## Method

The image is the trigger file's (records of types `0xc`/`0xe` with link words and a checksum, plus a few others); the trick objects, three model items, a skater, the display with its ordering-table cell
and the primitive buffer are in the slab. The delete-all pair has a group of its own: the objects' deleting destructor (slot 0 of the vtable) is a function under test in the other group, so there it is a
stub that scripts the new head of the list. The model items, the sound, the net game, the replay recorder and the trigger pulse are stubs; `TRG_GetNodeLinks` and the class delete run as retail code.

## Lessons

(1) `TrickObject_DeleteAll` re-reads the head after each delete because the destructor unlinks the object; the stub writes the next object into the head, and the native port takes the stub's answer as the new
head. (2) `0x004bdc40` asserts that the item exists and then writes to it: a null item is a retail crash, so the probe never makes one. (3) The flicker's phase uses the *address* of the item shifted right
two bits as the index into the mask table, so it depends on where the item was allocated.
