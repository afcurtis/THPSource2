# Platform objects: type-1 subtype 0x0192 (Phase 4.4c)

Status: confirmed for the constructor, the update, the physics step of type 0, the bounce
simulation and its collision-query use, the scale/model/move-to commands the traces reach, and
the `Ob_QuickAI` frame loop, by retail replays (see Validation); physics types 1 and 2, the
move-to steering of types 1 and 2, and the commands no trace reaches are confirmed from
disassembly and unit tests only.
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0049f250` (constructor), `0x0049fef0` (update, vtable slot `+0x0c`),
`0x0049f780` (`CPlatform__BouncyAI`), `0x0049f4c0` (`CPlatform__Bounce`), `0x004a0360`
(`CPlatform__Hit`, slot `+0x18`), `0x004a07e0` (command handler, slot `+0x1c`), `0x004a1060`
(field handler, slot `+0x20`), `0x004a1270` (variable handler, slot `+0x24`), `0x004a03e0`
(`CPlatform__DoPhysics`), `0x004a0710` (set move-to target), `0x004a12d0`
(`Platform_HandleBounce`), `0x00481110` (`Ob_QuickAI`), `0x00480fd0` (`Ob_ClearInputFlags`),
`0x00481150` (`Ob_MaybeUnSuspendOrCull`), `0x004f5f90`, `0x004f5fc0`, `0x004ca8f0`,
`0x004c9880` (`Utils_TurnTowards`), `0x004e80e0`
Native: `src/trg/platform_object.cpp`, `src/trg/object_script.{hpp,cpp}`,
`src/trg/world_objects.{hpp,cpp}`, `src/trg/script_world.{hpp,cpp}`

Platforms are the scripted movers, doors, breakable crates and knock-about props of the levels:
351 nodes in the 32 shipped levels (Warehouse 41, Hangar 64). They run the same bytecode as the
0xcb objects (`object-script-vm.md`) with their own virtual table at `0x005194f8`:

| Slot | Address | Meaning |
| --- | --- | --- |
| `+0x00` | `0x0049f340` | destructor: leave the game-object list, stop the sound at `+0x204` |
| `+0x08` | `0x004013b0` | `CBaddy__Die` (shared with 0xcb) |
| `+0x0c` | `0x0049fef0` | per-frame update |
| `+0x18` | `0x004a0360` | `CPlatform__Hit(hit points)` |
| `+0x1c` | `0x004a07e0` | command handler, falls back to the base handler `0x004015e0` |
| `+0x20` | `0x004a1060` | field handler, falls back to `0x004027f0` |
| `+0x24` | `0x004a1270` | variable handler; only id `0x2200` differs (asserts "removed") |

## Object life cycle and frame order

`TRG_ConstructSubtype0x0192` runs the base constructors, then stores the level context at
`+0x1d8`, pushes the object on the head of the **game-object** list (`GameObjectListHead`
`0x0056af40`, not the baddy list), rewrites the state as `(state & ~0x02) | 0x111`, and sets
`+0x1f2 = 0x192`, `+0x1a0 = 0x20`, `+0x208 = -1`, active byte `+0x16c = 1`. List flags stay `0x12`
(suspend-when-far). `TRG_CreateObject` applies the node options exactly as for 0xcb: option 2
starts the object awake, otherwise it is parked on the suspended list.

The frame code at `0x00469e60` runs, in this order: `Ob_ClearInputFlags` on the baddy and
game-object lists (keep only bit 0 of `+0x68`), `Ob_AI` on the skater list, `Ob_AI` on the
pickup list, **`Ob_QuickAI` on the game-object list**, `Ob_QuickAI` on the trigger-script list,
`Ob_MaybeUnSuspendOrCull`, then `Ob_AI` on the baddy list.

- `Ob_QuickAI` updates every live object with no distance test and no far-away parking. It reads
  the next pointer one step ahead. A dead object gets one frame with list flag `0x80`, then its
  destructor runs.
- `Ob_MaybeUnSuspendOrCull` walks the suspended list; an object flagged suspend-when-far
  (`+0x6a & 2`) whose `Utils_CrapDist` to the nearest player is `<= 0x32c8` is un-suspended.
  This is how parked 0xcb and platform objects wake when the skater approaches. (4.4b left this
  step out.)

## Update (`0x0049fef0`)

1. For each axis with a non-zero scale frame counter (`+0x1e4/+0x1e6/+0x1e8`): add the per-frame
   step (`+0x1ea/+0x1ec/+0x1ee`) to the scale short (`+0x28/+0x2a/+0x2c`) and decrement.
2. Zero the acceleration (`+0x58..+0x60`). Latch `+0x68` into `+0x174`; if bit 0 is set, count it
   in `+0x170` and clear it.
3. Flag `0x10` of `+0x178` (set by command `0x4309`): run `CPlatform__BouncyAI` and return.
4. Otherwise: an inactive object does nothing; with no wait the script runs from `+0x17c`; with a
   wait that is `<= +0x1dc` the wait, `+0x1dc` and `+0x1e0` are cleared.
5. Flag `0x01` (a move-to target is pending): steer (below). Otherwise, for physics type 1 only,
   ease the roll angle `+0x18` back to zero with `+0x7a`.
6. If `+0x16e` is set, add the world vector `0x0056b770` to the acceleration.
7. `CPlatform__DoPhysics`.
8. If `+0x208` is not `-1` and `DAT_0056a8d4 % +0x208 == 0`, refresh the positional sound
   `SFX_ModifyPos(+0x204, position)`.

The platform wait is **timed in simulation ticks**: the constructor sets state bit `0x100`, so
`0x4280` stores the raw count in `+0x1c0` and starts `+0x1dc = 0`, `+0x1e0 = SimulationTime`.
`DoPhysics` then runs one physics step per two ticks of `SimulationTime` elapsed since `+0x1e0`,
counting `+0x1dc` up to the wait. (The 0xcb wait is instead counted down by the frame time.)

### Physics (`0x004a03e0`), type `+0x1a4` (attribute 2)

- **Type 0**: velocity = acceleration + velocity, each axis inside `[-0x800, 0x800]` becomes 0;
  position += velocity; the three angles add their angular velocity and wrap to 12 bits; angular
  velocity += angular acceleration with `[-1, 1]` snapped to 0.
- **Types 1 and 2**: velocity += acceleration; `v -= v >> count` with the bytes at `+0x6c`; snap
  inside `[-0x800, 0x800]`; position += velocity; angles += angular velocity, masked; angular
  velocity += acceleration, decayed by the bytes at `+0x7c`, snapped inside `[-1, 1]`.
- Any other type asserts "unknown type in platform physics".

Shipped scripts use type 0 (348 objects) and type 2 (3 objects); type 1 appears in none.

### Move-to (flag 1)

`+0x1c4` holds the target, set by commands `0x4220/0x4221/0x4222` through `FUN_004a0710` (a target
that differs from the current one is stored and raises flag 1). Attribute 0 (`+0x1a0`, default
`0x20`) is both the arrival distance and the speed. With `d = length(position - target)`
(`0x004ca8f0`: components `>> 12`, truncated to 16 bits, squared, summed, x87 square root):

- `d <= attribute 0`: position = target; types 0 and 1 also zero the velocity; flag 1 clears.
- type 0: velocity = `Camera_DirectionFromAnglesRaw(look angles to the target, attribute 0)`.
- types 1 and 2: `turn = 0x1fe0 / |target - position|`, clamped to `[-0x100, 0x100]` with 1..3 raised
  to 4; `Utils_TurnTowards` turns the two look angles into the angular accelerations
  (`(delta * turn) >> 8`, arithmetic); angle 0 is reset to 0; the acceleration becomes the look
  direction scaled by attribute 0. Type 1 also banks: with flag `0x20` set the roll follows the
  yaw error clamped to `+-0x400`.

## Commands (`0x004a07e0`) and fields (`0x004a1060`)

| Word | Effect |
| --- | --- |
| `0x4205` | die, then yield (the base handler continues) |
| `0x4220` | target = aligned u32 x3, each `<< 12` |
| `0x4221` | target = position of a node (id or variable); an undecodable node asserts and gives 0 |
| `0x4222` | target = position + aligned u32 x3 `<< 12` |
| `0x4300` | back up the cursor and yield: halt |
| `0x4301` | state bit 0 set, die, `Shatter_Item(object, 0, 0)`, yield |
| `0x4302` | `+0x20c..+0x214` = three u16 `<< 12 >> 1`; flag `0x04` |
| `0x4303` | skip a word |
| `0x4304` | scale all three axes by `amount` percent over `frames` frames |
| `0x4305` | state bit `0x08` on or off |
| `0x4306..0x4308` | the same for X, Y or Z only |
| `0x4309` | `+0x1f2 = kind`; flag `0x10` on for non-zero; the three model extents (`+0x1f4/6/8`) become the differences of the model box words |
| `0x4507..0x4509` | object sound start / positional start / stop, stored at `+0x204/+0x208` |
| `0x2123` | byte `+0x16e` |
| `0x2124`, `0x212f` | model index (direct or by checksum) and its model-flag `0x10` copied to state bit `0x20` |
| `0x2127`, `0x2128` | angular velocity / acceleration from three operands |
| `0x2134` | velocity from three signed operands `<< 12` |
| `0x2137` | the three angles from three operands |

Scale animation: the step is `(amount << 12) / 100` when `frames` is 0 and
`(amount << 12) / (frames * 100)` otherwise (signed, toward zero, low 16 bits); the first change
is applied at once and `frames` more follow.

## Bouncy props (flag 0x10)

30 of Warehouse's 41 platforms, and many props elsewhere, are created inert with a
script that runs `0x212f` (model) and `0x4309` (kind 1 to 5) and then loops. From then on the
update runs `CPlatform__BouncyAI` only, which does nothing until the prop is hit.

- `CPlatform__Bounce(object, skater, skater velocity)` (`0x0049f4c0`), reached from
  `Platform_HandleBounce` (the skater's swept box, expanded by `0x14000`, touches a model box of a
  flagged prop with state bit `0x100`; it sets state bit `0x20` on every such prop each call) and
  from a kill message (`TRG_SendKillFromNode`, which first runs the node's link list). It starts the
  flight: flag `0x20`, state bit `0x20`, `+0x1f0 = 0`; draws `Rnd(0x1000)` into `+0x1fe`,
  `Rnd(2)*0x800 + angle - 0x400` into `+0x1fc` and `+0x200`; then by kind picks a sound, grunt and
  vibration and sets the velocity from the skater (kind 1 and 3: horizontal velocity with
  `-0x40000` or `-0x20000` up depending on the skater's horizontal speed `< 0x28`; kinds 2, 4, 5:
  1.5x horizontal velocity and `-0x50000` up).
- `CPlatform__BouncyAI` (`0x0049f780`): counts down `+0x1fa` and dies at 0; while flying it spins
  towards the target orientation (`+0x1fc..`; angular velocity `(target - angle) >> 5`, pushed to
  at least `0x28` while more than `0x28` away), builds the rotation matrix
  (`FUN_004e80e0`: identity, then Y, X, Z rotations, each a matrix product with `>> 12`) and takes
  the vertical reach of the model box from its middle row, `sum(|m1j| * extent_j) >> 1`. The
  velocity gets `+0x8000` added to Y, decays by `>> 6` per axis and snaps; the object probes the
  level with a line from its position to position + velocity and, when that is clear and
  moving up, with a vertical line through the end point. Up to three probes per update. A hit
  reflects the velocity (`v -= 2 * (n . v) * n`, x87 fixed-point), counts a bounce, plays a
  sound for the first five, and on the first bounce re-rolls the target angles with `Rnd`. After
  `0x12` bounces the prop settles: angles 0 and 2 take the target, flag `0x20` clears and the
  lifetime is set to 10 frames.

## Hit (`0x004a0360`)

Counts `+0x16f`. An object with attribute 1 set and positive hit points at `+0xb4` loses the
damage; at 0 or below it dies and `Shatter_Item(object, 0, 1)` runs, otherwise the item flashes.
`KillInList` mode 1 passes the object's own hit points as the damage, so a hit always finishes
an armed object.

## Not modelled

- `Shatter_Item` (the debris spawner; it draws `Rnd(5)` per model face) and the sound, grunt and
  vibration calls are services; their effects are not native.
- The retail collision query used by the bounce simulation is not native for props: the level
  runtime approximates it with the conservative segment test of `PsxCollisionWorld` until the
  moving-prop-collision slice (paused for Phase 5).
- The retail `SimulationTime` unit is not derived; the level runtime uses milliseconds.

## Validation

- `platform_object_retail_trace_test` replays retail platform updates and `CPlatform__Bounce`
  calls from their raw entry images: the same commands and cursors, variable reads, Rnd draws,
  collision queries (line and result), side effect calls and every modelled object field.
  Recorded with a GDB probe (`build/p44c/platform_trace.py`, untracked) that teleports the
  skater onto props and replaces the skater's velocity at each bounce with one of eight values:

  | Fixture | Updates | Bounces | Flying updates | Collision queries | Rnd draws |
  | --- | --- | --- | --- | --- | --- |
  | `platform-object-warehouse.txt` (SKWARE_T) | 5,389 | 31 (kinds 1 and 5) | 1,859 | 3,578 | 453 |
  | `platform-object-hangar.txt` (SKHAN_T) | 1,631 | 13 (kind 3) | 335 | 598 | 91 |

  All match. They cover flights of up to 18 bounces, the settle at the 19th, the 10-frame
  lifetime death, the death pulse (`exec_list`) and the sounds of kinds 1, 3 and 5. The grunt is
  `SFX_Grunt` (`0x0046e1d0`): a surface sound for some surfaces, then `Rnd(3)` picks sound
  `0x1c + draw`; the first replay diverged on exactly that draw.
- Kinds 2 and 4 never bounced in a trace (their props stayed parked in Warehouse; Hangar has only
  kind 3). They are pinned by `platform_object_test` from the disassembly, along with the launch
  formulas of the traced kinds.
- The retail Rnd generator was checked separately: 114 recorded draws, each sequence started from
  its recorded entry state, agree with `RetailRandom` (`retail_random_test` pins one).
- `opentony_trg_scripts` runs every platform script of the 32 shipped levels (351 objects, 600
  frames, bounces forced) with no assertion and no fault.
- `platform_object_test` pins scale, move-to, shatter, halt, hit, both physics types and the wait
  form from the disassembly.
