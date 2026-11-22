# Power-ups: type-5 nodes (Phase 4.4c)

Status: confirmed for the constructor, the ageing, blinking and hover update, the pickup effects and
their service calls by a retail replay (see Validation); the fall-and-settle branch, the camera-follow
branch and the exact pickup test are not traced.
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004a7c50` (`Powerup_Construct`), `0x004a8e50` (`Powerup_CreateFromTrigger`),
`0x004a8ac0` (`Powerup_Update`, vtable slot `+0x0c`), `0x004a8300` (`CPowerUp__DoPhysics`),
`0x004a8620` (`CPowerUp__CheckAge`), `0x004a86f0` (`CPowerUp__TakeEffect`)
Native: `src/trg/powerup_object.cpp`, `ObjectScriptVm::create_powerup`, `update_powerup`,
`take_powerup`, `hide_powerup`, `die_powerup`, `ScriptWorld::pulse_node` (type-5 branch),
`ScriptWorld::collect_powerup`

A power-up is a 0x100-byte object on the pickup list (`0x0056b830`), which the frame runs before the
game-object list. `TRG_CreateObject` builds one from a type-5 node: the subtype, the position, the word
after the position (zero: drop to the ground, flag `4`) and the timer word after that. A node with a
link list sets `+0xd0` and `+0xd6` (`CPowerUp__SetNode`): the links run when the power-up is collected.

## Construction (`0x004a7c50`)

List flags `0x1a`, the node index at `+0xb0`, the timer at `+0xf0` (`0xffff`: permanent). Flag `8` throws
the power-up (`velocity.y = -0x3c000`). The subtype selects the model checksum and region (ITEMS for
money and stat pickups, SKMEDALS for medals `0x664..0x666`, the level pickup's checksum from the level)
and the spin (`+0x72 = 200` for money). Subtypes outside the dispatch (`0x13..0x17`, and any above `0x666`)
reach the `Bad powerup type` assertion, which Downhill Jam's 16 nodes of subtypes `0x15..0x17` would hit.
In modes other than career (selector `!= 1`) every pickup subtype is created with a timer of 1 (it expires
at once); in career mode the ones the goal progress bits show as already collected get the same timer.

## Update (`0x004a8ac0`)

1. `CheckAge`: `++age`; unless the timer is `0xffff` it counts down. Below `0x3c` left the bob amplitude
   is `amplitude_base * left / 60`; the power-up blinks (state bits `0x41`, through the hide slot) on
   `counter & 2` while more than `0x1e` is left and on `counter & 1` in the last `0x1e`; at zero it dies.
2. A level draw limit (`_DAT_00563a3c`) hides the power-up when too many are on screen.
3. `DoPhysics`: angles advance by `angular_velocity * AnimationTimeScale >> 8`, masked to 12 bits; a
   resting power-up bobs (`sin(phase) * amplitude`, phase advanced by `rate`); a dropped one waits for the
   ground height, falls, bounces and settles.
4. The glow emitter is created on first update (per-subtype base and swing) and refreshed each frame
   with `sin(counter * 0x96) * amplitude / 4096 + base`.

## Pickup (`0x004a86f0`)

A power-up already hidden for that player (`0x01`, or `0x40` in split-screen for the primary skater) cannot
be taken. Stat pickups (`4 5 6 10 0xf`) play sound `0x14` and show their message unless the goal stat is
already taken; `0x10` grants goal type 6; money (`0x18 0x19 0x1a`: 250, 50, 100) and the level pickup
(`0x21`) need the skater on the ground, take the skater out of its trick and land it again around a
`0x12` (or `0x13c`) sound and a pitch change. The node's links run, then the power-up dies.

## Validation

- `powerup_object_retail_trace_test` replays 2,182 retail calls on Hangar's pickups in career mode from
  their raw entry images (recorded with `build/p44c/powerup_trace.py`, untracked, which teleports the
  skater onto power-ups): 2,123 updates of subtypes `4 5 6 0xa 0xf 0x19 0x1a 0x21` and 59 take attempts.
  Every call matches on state, list flags, position, angles, velocity, home, ground, bob phase and
  amplitude, timer, age, shadow words and glow, and on each sound, pitch, money, goal and message call in
  order (25 calls, 7 expiries). The constructor is checked against the first image of all 13 power-ups
  (`re/evidence/fixtures/powerup-object-hangar.txt`).
- `opentony_trg_scripts` constructs and runs every shipped power-up node for 600 frames with no fault or
  assertion (Downhill Jam's 16 invalid subtypes are skipped, as above).
- `powerup_object_test` pins permanence, expiry, the drop, money and stat pickups.
- `level_world_test` creates Hangar's power-ups in a `LevelRuntime` and runs them.

## Not modelled

- The fall-and-settle branch (flag `4`) and thrown power-ups: no traced power-up was moving, so they
  follow the disassembly and are unit tested only.
- The camera-follow branch (`+0xf6`) and the exact line-to-sphere pickup test: the native world collects
  on a service call.
- Shatter, pickup particles and the glow's presentation are services.
