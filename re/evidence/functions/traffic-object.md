# Traffic cars: type-1 subtypes 0xd5..0xdc (Phase 4.4c)

Status: confirmed for the constructor state, the road-following update, the turn, the proximity
sounds and the service calls by a retail replay (see Validation); the car-versus-skater
interaction and the video-playback branch are services and not modelled.
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00412640` (`TRG_ConstructTrafficObject`), `0x00412960`
(`TRG_UpdateTrafficObject`, vtable `0x005184e0` slot `+0x0c`), `0x004136e0`
(`TRG_CheckTrafficProximity`), `0x004128c0` (`TRG_DestroyTrafficObject`), `0x00412ef0`
(car-versus-skater interaction), `0x00413000` (its worker), `0x00465f60`
(`Math_NormalizeFixed12`), `0x004e8560` (`orig_ratan2`), `0x00412930` (wrapped angle
difference), `0x004f5f50` (sign of a 2D cross product), `0x004cde80` / `0x004cdef0` (car position
save and restore)
Native: `src/trg/traffic_object.cpp`, `ScriptObject::construct_traffic`,
`ObjectScriptVm::update_traffic`, `ScriptWorld::pulse_node`

Cars are the taxis (`0xd5`), police cars (`0xd6`), buses (`0xd7`), cable cars (`0xd8`), karts
(`0xd9`), the Marseille boat (`0xda`), bulls (`0xdb`) and gulls (`0xdc`): 40 in the shipped levels
(New York 20, Philadelphia 10, Downhill Jam 3, San Francisco 3, and one each in Bullring,
Burnside, Marseille and School II). A car runs no script; it drives along a chain of type-1 nodes.

## Construction

The constructor runs the base constructors (state `0x02`, list flags `0x12`, `+0xae = 0x80`),
installs the vtable, takes the next index from `DAT_0055faec` into `+0x1e0`, stores the subtype at
`+0x3c`, and starts the subtype's positional sound through `Audio_PlayPositionalSound`, keeping the
handle at `+0x1d0` (taxi `0x71`, bus `0x121`, cable car `0x86`, kart `0x111`, boat `0xdd`, bull
`0x145`, gull `-1`; the police car starts none and sets its refresh period `+0x1d4` to 0, every
other car sets it to 1). It pushes the car on the baddy list, sets `+0x108 = 0x10000`,
`+0x1ac = 0x34`, `+0x1ae = 0x20`, `+0xb4 = 0x3c`, reads the parameters, and stores the node index
at `+0xb0`. The node's link list must hold exactly one link (the first road node, kept at `+0x1dc`);
the car's position is that road node's position with Y lowered by `0x6e000`.

## Update (`0x00412960`)

1. If both the sound handle and period are set and `AnimationClock % period == 0`, refresh the
   sound at the car's position (the retail code scales a volume global around this call).
2. While a movie plays, restore the saved position and angles for the car's index and stop.
3. Otherwise run the skater interaction (`0x00412ef0`), then drive:
   the road segment runs from the car's road node `P0` to the node its one link names, `P1` (both
   Y lowered by `0x6e000`); `dir = normalize(P1 - P0)`. On the first segment of the frame the speed is
   `dot(dir, velocity)`, raised by `0x1000` unless it is at least `0x50000` and replaced by `0x1000`
   when negative; later segments of the same frame use the first dot and the carried overshoot.
   Velocity is `dir * speed`.
   Progress along the segment is measured on the dominant axis: `length = ((P1 - P0) / dir) << 12` and
   `travelled = ((position - P0) / dir) << 12`. One step is `(AnimationTimeScale * speed) >> 8`; in the last
   sixteenth of the segment the step loses `(AnimationTimeScale * speed) >> 12` and the velocity decays
   by `v >> 4`. If the step passes the end of the segment the car is placed on `P1`, the road node
   advances to the next link and the update repeats with the distance carried over. Otherwise the car
   advances by `dir * step`.
4. After a step that stays inside its segment the car turns: yaw target `ratan2(-dir.x, -dir.z) & 0xfff`
   and pitch target `ratan2(dir.y, 0x1000) & 0xfff`. An angle within `0x80` of its target snaps to
   it; otherwise it moves by `error >> 2` (yaw) or `error >> 3` (pitch), masked to 12 bits.
5. `TRG_CheckTrafficProximity`, then the position is saved for replay (`0x004cde80`).

`ratan2(a, b)` is `atan2(a, b)` in 4096ths of a turn through x87, truncated; `Math_NormalizeFixed12`
halves the vector until every component is within `0x6882`, then scales it to length `0x1000`
(a zero vector becomes `(0x1000, 0, 0)`).

## Proximity (`0x004136e0`)

For each player within `Utils_CrapDist < 2000` of the car, two cross-product sign tests on the
player's offset and the car's heading decide whether the player is beside it:
`(x - z) * dx + (z + x) * dz > 0` and `(z + x) * dx + (z - x) * dz > 0` (each scaled by `1/4096`).
The first such player makes the car sound its horn once (latch `+0x1e4 = 1`, nothing more while
it stays set); when no player matches the latch clears. Sounds: taxi `0x6e + Rnd(3)`, police
`0x7a`, bus `0x122 + (Rnd(2) != 0)`, cable car `0xfb`, kart `0x110 + 0xe * (Rnd(2) != 0)`,
boat and gull `-1`, bull `0x148 + (Rnd(2) != 0)`.

## Validation

- `traffic_object_retail_trace_test` replays 2,856 retail updates of New York's cars from their raw
  entry images (recorded with `build/p44c/traffic_trace.py`, untracked, which teleports the skater
  onto cars): the same position, angles, velocity, road node and latch, and the same sound
  refreshes, horn sounds, Rnd draws and service calls in order. All match, across 50 road-segment
  changes, 3 horn sounds and 2 Rnd draws (`re/evidence/fixtures/traffic-object-ny.txt`).
- `opentony_trg_scripts` drives all 40 shipped cars along their roads for 600 frames with no
  assertion and no fault.
- `traffic_object_test` pins the constructor, a first step and turn, a node crossing, every
  subtype's horn, the latch, and out-of-range and behind-the-car players.
- `level_world_test` creates New York's cars in a `LevelRuntime` and drives one.

## Not modelled

- The car-versus-skater interaction (`0x00412ef0` / `0x00413000`: swept tests, response sounds and
  effects) is a service. It ran in every traced update, and the modelled car fields still match
  retail afterwards; its other effects (on the skater, sounds, unmodelled car bytes) are not checked.
- The video-playback branch restores the saved position through a service.
- The Rnd-driven horns of buses, karts and bulls and the police and gull sounds were not observed
  in the trace; they follow the disassembly.
- The volume scaling around the positional-sound refresh is presentation.
