# The skater stats and the motion handlers of physics.cpp

Status: confirmed by retail forced calls (1,849 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0048f090`, `0x0048f1d0`, `0x0048f3a0`, `0x004993f0`, `0x004995d0`, `0x00497bb0`, `0x0049d2f0`, `0x0049d480`, `0x0049d740`, (with `0x00493e10`, `0x00494090`, `0x00494180` of
`skater-state-helpers.md`) (slices `skater-stats` and `skater-motion-handlers`)
Native: `src/runtime/skater_kit.hpp`, `skater_stats.cpp`, `skater_motion.cpp`, `skater_damp.cpp`, `skater_rail.cpp`; replay `src/runtime/skater_retail_calls_test.cpp`; probes
`re/gdb/probes/phys_specs_stats.py` (group `phk`), `phys_specs_hit.py` (group `phq`); fixtures `re/evidence/fixtures/skater-calls-phk*.txt`, `skater-calls-phq*.txt`

## What is here

- **The stats.** A skater has 11 stats (0 air time / 1 ... 10); `Skater_ProfileValue(stat)` gives one in tenths of a level: with one player the character's base level (a signed byte of its trick configuration at
  `+0x38`) times ten plus the skater's own bonus (a short at `+0x29f8 + 2 * stat`); with more players the multi-player table's level (`0x0053a4fc`, 0x170 bytes a character) plus the player's boost
  (`0x0056a2f0`) plus 2, times ten, at least 30; `0x0056b7f4` (a level override) replaces both with its tenfold. The cheats or the power-up bits of `+0x2f70` add 30 % of a table entry; the stat's limit caps it;
  a skater facing the other way than its stance loses a share through stat 6; the bail lock (`0x0056a890`) adds a fixed amount up to a limit; stat 6 itself is capped at 100; the tag mode lets
  `Tag_StatModifier` change it; the cheat 12 makes stat 4 equal 250. `Skater_InitStats` starts the levels at 1, raises the three levels (bytes at `+0x29b4..+0x29b6`) to the best deck its character owns
  (`Skater_DeckRecord(set, deck)`: eight decks a set, twelve more for a set with bit 1) and fills the bonus points (10 per level above the first, spread over the slots by a fixed pattern).
- `Skater_EnterFootplantState`, `CBruce__MaybeManual` and `Skater_DampResponseVector` are 32-bit integer arithmetic over those stats: the compiler's multiply-by-reciprocal sequences (`0x66666667`
  = /10 or /20, `0x68db8bad` = /10000, `0x51eb851f` = /100, `0xea0ea0eb` = /280) all give the truncating quotient; the port writes the division. Their intermediates wrap at 32 bits.
- `Skater_UpdateSpecialBasisState` and `Skater_RebuildSidewaysBasis` rebuild the orientation basis with the GTE's outer product (`Math_CrossFixed12`: the first vector on the diagonal of the rotation matrix registers,
  the second in IR1..3) and `Math_NormalizeFixed12` (halve until inside +-0x6883, the GTE squares the 16-bit components, `(c << 12) / sqrt(sum)`, the x axis for a zero length).
- `Skater_CollideWithOtherPlayer` (two skaters closer than 60 units): `mine` / `theirs` are the dot products of the two velocities with the direction to the other skater; both moving in: the other bails; only mine
  moving in: the slower one bails; neither: this one. `CBruce__CheckForTriggers` runs a zone broadphase along the line from 16 units behind the previous position to the new one with the skater recorded as the
  trigger owner (`0x0056a954`); its function-local static (`0x0056b780`, guard byte `0x0056b7e4`) registers a destructor with `atexit` on the first call.

## Method

As in `skater-state-helpers.md`. The stat tables and the character/deck records of the exe are mutable windows poked with random small values (a stat that gets large would make the footplant's divisor 0, which
faults in retail; such cases end in a retail error and are skipped); the trick configuration record, the cheats and the tag modifier are stubs. The collision group (`phq`) puts a fake second skater in the slab
(0x3538 bytes) because the bail writes its fields.

## Lessons

(1) The decompile's expression trees hide the order of the `Skater_ProfileValue` calls: the footplant calls it 7 times (2, 0, 2, 0, 2, 2, 2) and the replay compares the service calls in order. (2) The 32-bit
wrap matters for the products before a division (`imul` then `idiv`), so the port multiplies as unsigned. (3) `Math_Vector3Normalize` divides z by the already divided y (a retail quirk the core layer found).
(4) `CBruce__MaybeManual` tests the manual state (`+0x2dd4`) before it starts the queued trick script and acts on it afterwards: the second half is reachable only because `TrickScript_Execute` can set
the field. The probe's stub for that service writes the field (a value the case chooses, or the field as it was) and records the value, and the port takes the service's answer (`engine_answer`, the field as it was when the
host has none), so the manual's balance update is replayed (250 cases of the last capture) instead of being dead code.
