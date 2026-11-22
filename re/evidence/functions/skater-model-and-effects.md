# The skater's model parts, rail events, effects and the jump (physics.cpp, second batch)

Status: confirmed by retail forced calls (5,487 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0048ec00`, `0x0048fbe0`, `0x0048fc90`, `0x0048fd60`, `0x00490ae0`, `0x00490c70`, `0x00490e30`, `0x00493f40`, `0x00495af0`, `0x00494210`, `0x00499710`, `0x0049a280`, `0x0049c330`, `0x0049c970`, `0x004aaf70` (15 functions; slices
`skater-model-parts`, `skater-rail-events`, `skater-effects`, `skater-orientation-and-sounds`, `skater-ollie`, `skater-contact-and-rails`)
Native: `src/runtime/skater_kit.hpp`, `skater_parts.cpp`, `skater_events.cpp`, `skater_transform.cpp`, `skater_sounds.cpp`, `skater_ollie.cpp`, `skater_swept.cpp`, `skater_rail_physics.cpp`; replay `src/runtime/skater_retail_calls_test.cpp`; probes
`re/gdb/probes/phys_specs_parts.py` (group `php`), `phys_specs_events.py` (group `phe`), `phys_specs_big.py` (group `phz`); fixtures `re/evidence/fixtures/skater-calls-phys*.txt`

## What is here

- **The model parts.** The model of a region is a table of model records (`PsxModelTablesByRegion`, 0x44 bytes a region; the model count is the word before the table, two state bytes sit at `+0x2a` and `+0x2b`
  of the region's record). A model has its vertex count at `+2` and its vertices from `+0x1c`, eight bytes each (three shorts and a flag byte at `+6`; the bits `0x12` keep a vertex out of every change).
  `Skater_FindPartIndex(part)` is the position of a part number in the skater's order of parts (19 bytes at `+0x150`, in use only when `+0x14c` is set). `Skater_OffsetPartVertices(amount)` moves the third short
  of every vertex of the three parts in `+0x2cd0` / `+0x2cd4` / `+0x2cd8` by the change of a per-region byte. `Skater_Fatten(amount, undo, kind)` scales the x and z (and for part 15 the y) of every vertex by a
  percentage taken from a per-part table (`0x00536950`, `0x00536978` for kind 1 with 0x78, `0x005369a0` for kind 2 with 0xfa): `(table[part] * (|amount| - 100) * 10) / 100 + 1000` per mille, or its inverse
  `1000000 / that` when undoing. The earlier kind in force is undone first; undoing a kind 2 uses kind 1's table (a retail quirk: both recursive calls pass kind 1). It does nothing for a player whose character
  record (`0x0053a514`, 0x170 bytes) has bit 8 set.
- **The rail events.** A rail node (type `0xb`) links to at most two further nodes; one of type `0xe` or `0xc` among the first two is a trick object. `Skater_RegisterRailTrickObject(node)` looks the object's identifier up
  as an environment item, registers it with the trick layer and (in the game mode 1) activates it for the skater; with no such link it goes back along the rail (`Rail_PrevNodeQuick`, in the game mode 8 only)
  until none is left or the walk is back at the start. `Skater_CompleteRailGap` does the same walk from `+0x2ea0`, finds the script object (type `0xc9`) with the identifier, takes the gap's number and applies
  and completes its definition (`Gap_ApplyDefinitionState` is a thiscall that pops its four arguments). `Skater_EnterHandplant` takes off when the skater is on the ground, then plays the handplant (gap, animation
  `0x22`, state 8, the sound), puts the skater at the handplant position, registers the rail object and stops the sideways speed.
- **The effects.** `Skater_SpawnTagGlass` throws nine pieces of glass (new objects, handed the skater and the direction `(0, 0x19000, 0)`) while fewer than 250 effects exist; `CBruce__DoTerrainEffects` makes six puffs of
  dust in the colour of the surface (kinds 3, 5, 6) for a fast skater and rumbles the pad in proportion to the speed.
- **The orientation.** `CBruce__TempTransform` turns the orientation by a rotation about the skater's centre (the pending trick spin, `+0x2c88`, `+0x2cac`, `+0x2cb4`) and by the balance lean (`+0x2f14`); both go
  through the GTE a column at a time (the rotation registers hold the old matrix, `RTIR` multiplies a column of the new rotation). `Skater_UprightCorrection` turns a skater that leans over by a fixed angle
  (0xb or -0xb about z) when the dot product of its up vector with the sideways vector is beyond +-41.
- **The sounds.** `CBruce__HandleContinuousSounds` keeps the voices of the grind start (`+0x308c`), the rail grind (`+0x3090`), the surface roll (`+0x3094`) and the wheels (`+0x3098`): started when the state
  calls for it, tuned by the speed (the integer root of the squared velocity times 64 over `((speed stat + 500) * 0x2d000) / 0x118`) and stopped when the state ends.
- **The contact with the world.** `Skater_UpdateSweptContactState` is the move of the tick: the position advances by the velocity over the time scale and half the acceleration over the step (`0x00568804`);
  a line from the old position to the new one (at least 30 units long, 100 while bailing) is cast into the world, and a surface it hits puts the skater there (the position commit is a service), remembers the normal
  and may land the skater (state 0) with its basis rebuilt from the normal; a second line, 70 units up the skater up vector and 58 down, finds the ground: close enough the skater stands on it, else it takes off
  (state 1). The pull of gravity goes to the **acceleration** (`+0x58`), not the velocity - the decompile hides which of two locals a vector add acts on; only the stack offsets show it.
- **The rail.** `Skater_UpdateRailPhysics` slides the skater along the segment of the rail (`+0x2ea0`, the node, and the next one the rail layer gives): the velocity is projected onto the segment (the first tick
  adds the pull and a push along it), the position along the segment (`(position - start) / direction`, on the largest axis, in Q12 units plus the speed) says whether to stay (move by velocity and acceleration),
  go on to the next segment (up to five a tick, only when the rail does not turn too sharply: the dot product of the flat directions is at least 0xa49) or go back to the node before. Then the balance bar
  (`+0x2ef0` position, `+0x2ef4` speed: both scaled by the rail stat, the pad pushing it back, noise from the random numbers), a bail when the bar passes 4000 (or a drop onto a ledge, found with two short queries
  to the side), the lean of the board and a query for a wall in the way. `Rail_Update` chooses the rail: for every rail record whose box meets the box the skater moved through, the segment service gives the
  distance and nearest point of each segment; the segment the skater moves across best (and is above or may land on) becomes `+0x2ea0`, and four short queries across it tell whether a wall or nothing lies
  at its sides (`+0x2bfc`).
- **The jump.** `Skater_PrePhysicsOllie` charges the jump while the button is down (`+0x2de8` up to 15 less a twentieth of two stats), makes it on the release (the take-off, the impulse up from a polynomial of the
  stats and the charge - one set of constants when the surface under the skater is flat and another on a slope -, the trick decided from the first two press events of the pad history: ollie, nollie, fakie ollie,
  boneless or no comply) and lands the trick when the skater touches the ground again.

## Method

Forced calls on generated state (`P410_GROUPS=php,phe,phz`): the live player-one skater with random legal fields, the level's node table of `rail_specs.py` for the rail walks (forward links only: a link back
makes a rail walk run for ever in retail), a model table redirected into the slab for the parts, and stubs for everything outside physics.cpp: the trigger layer, the sound and effect layers, the animation
player, the trick layer (`Trick_TakeOff`, `Trick_Land`, `TrickInput_MatchHistory`, `TrickInput_ConsumeRecord`, `Skater_ApplyTurnVelocityPhase`) and the stat machinery of `Skater_ProfileValue` (the real code,
with its cheat and tag queries stubbed). A service whose effect on the skater the function acts on (`Trick_TakeOff` sets `+0x3064`) is a `writes=` stub whose value the case chooses and the port takes as the
service's answer (`engine_answer`).

## Lessons

(1) `Gap_ApplyDefinitionState` has `ret 0x10`: the stub must be a thiscall that pops four arguments (a cdecl stub left the caller's stack 16 bytes off and crashed the game thread after the first call that
used it). (2) A model's vertex flag byte (`+6`) and the clip words are read as bytes and shorts of the same eight-byte record. (3) The decompile of `Skater_Fatten` shows both undo calls with the same arguments;
only the asm shows that the argument that picks the table is 1 in both. (4) Several constants of the ollie are line numbers passed to the animation requests (`0x226c`...): they are ignored, like the unused second
argument of `Animation_RequestStart`. (5) A generator that links a rail node to a lower node (or makes an "object" node a rail node) makes `Rail_PrevNodeQuick` loop for ever; the watchdog then ends the
whole capture.
