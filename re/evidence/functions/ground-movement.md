# Ground movement / orientation

Status: supported static and runtime dataflow. A clean Warehouse run captured idle, Left, and Right at the same `0x0049f0e5` callsite while all directional records remained grounded (`physics_state == 0`). The turn update, 12-bit Y rotation, basis mapping, and fixed-point projection are identified; collision/surface handling still prevents claiming one universal closed-form X/Z equation.

Branch: `re/ground-movement`

Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`

## Result

The normal grounded path is:

```text
Left/Right action record
    -> player+0x2ccc controller pointer
    -> controller+0x80 (Left) / +0x90 (Right)
    -> 0x00493370
    -> signed turn-angle accumulator player+0x3144
       (Left decreases, Right increases; player+0x3148 mirrors it)
    -> 0x0049b010 prephysics correction using the basis copied from the prior matrix
    -> 0x004967b6: position += velocity*dt + correction*dt^2/2
    -> 0x00496360 in grounded state 0x00496550
    -> 0x0049b500(angle & 0xfff, 1, 0), before the ordinary movement query
    -> 0x004e80e0 / 0x004e7de0 fixed-point Y rotation and 0x004e3130 matrix multiply
    -> player+0x2e58..+0x2e68 orientation matrix
    -> 0x0049c7d0 refreshes integer basis player+0x30f4..+0x3114
    -> later grounded correction/projection and collision fallbacks
    -> live X/Y/Z candidate
    -> 0x0049f0e5
    -> 0x00496060
```

The supported semantic is “Left/Right changes a signed turn angle which rotates the movement basis.” `player+0x3144` is not promoted to an absolute heading. The code also does not support calling `player+0x4c/+0x50/+0x54` a direct Left/Right vector: those fields are velocity/motion state and are modified by several other paths.

For a C++ recreation, the supported grounded abstraction is therefore:

```cpp
// +0x3144 is an accumulator, not a proven absolute world heading.
// Positions/velocities are Q16; short matrix and basis entries are Q12.
int32_t base_turn = player.tuning_29b7 == 0 ? 0x3c
                  : player.tuning_29b7 == 1 ? 0x78 : 0xb4;
int32_t turn_step = (base_turn * 0x100 * frame_scale_q8) >> 8;
int32_t turn_cap = player.fast_turn ? 0x5a000 : 0x2d000;

if (grounded) {
    // The original tests Left first; a simultaneous press takes this branch.
    if (left_held) {
        turn_accum = std::max(turn_accum - turn_step, -turn_cap);
    } else if (right_held) {
        turn_accum = std::min(turn_accum + turn_step, turn_cap);
    } else {
        int32_t decay = player.fast_turn ? (turn_accum >> 1)
                                         : (turn_accum >> 2);
        turn_accum -= decay;
        if (((turn_accum + 0x800) & 0xfffff000) == 0)
            turn_accum = 0;
    }
    turn_mirror = turn_accum;
}

// The frame wrapper first copied the old matrix into the basis.  The
// pre-physics correction after the turn update therefore still uses that
// old basis.  The ordinary state-0 dispatcher then integrates position:
//
//   correction_58 = prephysics_correction(old_basis)
//   position     += velocity_4c * dt + correction_58 * dt^2 / 2
//
// followed by the turn-derived matrix refresh below.  Collision and
// surface passes can subsequently alter the candidate and correction.
integrate_ground_candidate(position, velocity_4c, correction_58,
                           frame_scale_q8,
                           (frame_scale_q8 * frame_scale_q8) >> 8);

int32_t angle12 = (frame_scale_q8 * (turn_accum >> 12)) >> 8;
angle12 &= 0xfff;
rotate_short_matrix_about_y(matrix, angle12); // Q12 sin/cos, M <- M * R_y
basis = {
    matrix.column(2), // player +0x30f4..+0x30fc
    matrix.column(0), // player +0x3100..+0x3108
    matrix.column(1), // player +0x310c..+0x3114
};
// Ordinary state 0 passes param_3 == 1 to 0x0049b500.  That second phase
// transforms velocity through the saved pre-frame matrix and rescales it to
// the old integer magnitude before the ordinary movement query; state-2 /
// special state-1 passes zero.
if (ordinary_state0)
    velocity_4c = rotate_and_rescale_velocity(velocity_4c,
                                               saved_old_matrix,
                                               angle12,
                                               0);
correction = grounded_projection_and_collision(velocity_4c, basis, surface);
position = commit_candidate(position, history, correction);
```

The turn constants and branches above are from `0x00493370`; `frame_scale_q8` is runtime `DAT_0056865c`. The remaining surface/collision decisions are branch-shaped rather than one universal equation, so the recreation should preserve them as explicit data-driven stages.

## B010 producer inputs and profile ownership

The next unresolved boundary is now instrumented rather than represented by a
single caller-supplied boolean. The new `tony-ground-motion-probe [COUNT]`
breakpoint enters `0x0049b010` once per selected-player call and records the
complete raw input set: all sixteen `player+0x2ccc` 16-byte profile slots,
controller axes, animation state/frame, `+0x2e78/+0x2e7c`,
`+0x2f2c/+0x2dc8`, `+0x2dd8/+0x2dd4/+0x2df8`, response vector, basis, and the
`+0x3118/+0x3128/+0x312c` surface-side values. It also evaluates the exact
indexed lookup used by B010, but labels its result only as a raw local-profile
lookup.

The lookup is:

```text
index = player+0x2cc4
if DAT_00533f38 == 7:
    index ^= DAT_0056a854
local_value = *(int *)(0x0056a3d8 + index * 4)
profile_gate = (controller+0x10 != 0) || (local_value != 0)
```

This is an outer B010 eligibility gate. The `controller+0x10` byte also
selects scale `8` instead of `4` in the animation-state `2/3` correction
branches. A nonzero local value causes B010 to return after its first branch,
so it suppresses the later manual push block. The table is the per-player
**Autokick** option (`AutoKickSetting`, default On); see
[autokick-option.md](autokick-option.md) for the menu, default and publication
chain. The `+0x10` slot's binding name remains unpromoted.

Static image cross-references close the local-table writer chain enough to
target it directly:

```text
0x00413f30 / store PCs 0x00413f39, 0x00413f40
    initialize source flags at 0x0055fc2c[index] / 0x0055fc34[index]
0x00487c30 / store PCs 0x00487d27, 0x00487d45
    derive those source flags from profile-record +0x24c/+0x248, word +0x10
0x00413c10 / store PC 0x00413c49
    copy 0x0055fc2c[index] into 0x0056a3d8[index]
0x0049b010
    consume the indexed 0x0056a3d8 value
```

`tony-ground-motion-profile-probe [COUNT]` records these five store sites,
including the profile-record object/index and the value about to be written.
This makes the remaining question empirical: whether the Warehouse player’s
local value is changed by profile setup, profile selection, or a later runtime
mode update.

The B010 rearm inputs are similarly explicit. The first random return site at
`0x0049b1c4` supplies the `+0xaa` threshold seed; the second at `0x0049b416`
supplies the `+0xdc` seed. The resulting stores are:

```text
cooldown +0x2f2c = 0x14
threshold +0x2dc8 = ((roll + bias) * 0x2d000) / 0x118
animation rate +0x108 = 0x14000
event pending +0x30a8 = 1
```

The `0x0049b1c4` path can request animation `1` with reason `0x2570`; the
later `0x0049b416` path uses reason `0x25e5`. The no-animation side of the
first gate still rearms only `+0x2f2c`. `tony-ground-motion-writers [COUNT]`
records both the twelve correction component stores and these cooldown,
threshold, animation, and event stores; with `--correction` or `--control` it
can be narrowed to one group. The two random return sites are included in
the `--control` group.

This B010 rearm pair is distinct from the outer per-frame threshold refresh
in `0x0049e680`. That refresh calls `FUN_0048f3a0(3)` at `0x0049eb25` for the
`+0xaa` decay path and at `0x0049eae9` for the `+0xdc` replacement path. The
nearby `0x0049e831` draw has argument `5` and feeds a frame-local scalar at
`+0x2f3c`; it is not a `+0x2dc8` threshold source.

These probes do not change game state. A deterministic Warehouse replay should
arm the producer, profile, and writer probes together with the existing frame,
position-commit, and collision probes. That trace can then distinguish a
profile/stat eligibility change from a surface/collision modification of the
candidate without treating the action-script impulse path as ordinary steering.

## Action handoff

The input mapping is already established in [input.md](input.md):

- Left: global mask `0x8000`, DirectInput scan `203`, action record `0x0056b078`.
- Right: global mask `0x2000`, DirectInput scan `205`, action record `0x0056b088`.

The skater constructor at `0x0046c720` stores the controller object at `player+0x2ccc`. For the first player its controller base is `0x0056aff8`, so its `+0x80` and `+0x90` bytes are the Left and Right action records respectively:

```text
player+0x2ccc -> 0x0056aff8
0x0056aff8 + 0x80 = 0x0056b078  Left
0x0056aff8 + 0x90 = 0x0056b088  Right
```

## Grounded turn state

`0x00493370` is called by the per-frame skater physics wrapper `0x0049e680`, before the physics dispatcher. In the grounded state (`player+0x30b8 == 0`, also shared with state 7), it reads the controller bytes above and applies a frame-scaled signed increment to `player+0x3144`.

The relevant operation is:

```text
i3 = base_turn * 0x100 * DAT_0056865c >> 8

Left:  player+0x3144 = clamp(player+0x3144 - i3)
Right: player+0x3144 = clamp(player+0x3144 + i3)

player+0x3148 = player+0x3144
```

For the normal grounded branch, `base_turn` is selected exactly as:

```text
+0x29b7 == 0: 0x3c
+0x29b7 == 1: 0x78
otherwise:    0xb4
```

Then `turn_step = (base_turn * 0x100 * DAT_0056865c) >> 8`. The normal cap is `0x2d000`; the fast branch sets `+0x2e78 = 1`, doubles the step, and uses cap `0x5a000`. Left subtracts the step and clamps at the negative cap; Right adds the step and clamps at the positive cap. With no direction, the low-lean grounded path decays the accumulator by `turn >> 2` (or `turn >> 1` in the fast branch) and snaps values within the low 12-bit bucket to zero. `+0x2e7c` is set when an active turn branch is taken. These are turn-state operations, not world-space displacement.

`0x00492f20` consumes the mirrored `+0x3148` value for steering/pose response, but the orientation-to-basis operation used by the grounded physics path is the separate `0x00496360` call below.

## Grounded analog/lean target branch

The selected unresolved unit for this session is the no-direction analog
branch of 0x00493370, reached when the signed byte at player+0x31a1
has magnitude at least 0x1a. The serialized Ghidra decompilation is
build/ghidra/decomp/ground-turn-lean.c; the relevant state is static-confirmed
and the native boundary is tested, while the analog frame-to-position join
remains open.

For raw grounded states 0 and 7, the function first clears
player+0x2e7c, selects +0x2d000 or +0x5a000 as its limit, and resolves
the action bytes at controller +0x80/+0x90. Left has priority over Right.
When neither direction is active and abs((signed char)+0x31a1) >= 0x1a, it
performs this fixed-point response:

~~~text
product = limit * sign_extended_byte(+0x31a1)
target  = clamp((product + (product < 0 ? 0x7f : 0)) >> 7,
                -limit, limit)
denom   = (unsigned)(limit >> 12) / 2

if (turn < target) {
    +0x2e7c = 1
    turn += (((target - turn) >> 12) * step) / denom + step
    turn = min(turn, target)
} else if (target < turn) {
    +0x2e7c = 1
    turn -= (((turn - target) >> 12) * step) / denom + step
    turn = max(turn, target)
}
~~~

The 0x7f correction is applied before the arithmetic shift only for a
negative product, preserving the retail truncate-toward-zero result at
non-canonical fixed-point boundaries. The 0x1a comparison is strict on the
inside (0x19 still takes release decay) and inclusive on the analog side.
The native implementation preserves 32-bit product/add/subtract wrapping and
signed division before the final branch clamp. It also reports +0x2e7c as a
branch-selection flag: a repeated Right press at the positive cap still sets
the flag even though +0x3144 remains unchanged.

The final output handoff is deliberately split by ownership:

| Retail output | Immediate consumer | Scope of this slice |
|---|---|---|
| +0x3144 target/turn accumulator | 0x00496360 -> 0x0049b500, then grounded motion | reconstructed |
| +0x3148 = +0x3144 | 0x00492f20 steering/pose helper and later grounded reads | published, helper remains shared |
| +0x2e7c active target/turn flag | 0x0049b010 correction gate | reconstructed as raw flag |
| +0x2e78 wide-limit/brake flag | 0x0049b010 wide-turn side effect | caller-selected limit |
| +0x30b8 physics state | 0x0049db80 dispatcher | unchanged by this branch |

Thus the branch produces no ground-to-air, ground-to-rail, or special-state
request. The physics dispatcher continues on its existing raw state 0/7
ground path and consumes the updated target/basis inputs; state transition
ownership stays with the dispatcher and its state-specific handlers.

### Analog lean joined to the orientation and commit

A headless Warehouse GDB run (2026-09-28, 120 physics frames, profile
`+0x29b7 == 1`, `DAT_0056865c = 640`) set the controller's normalized
horizontal axis byte (controller `+0x149`) at the entry of `0x0046d6e0`, the
routine that copies it into skater `+0x31a1`: `+0x20` for frames 31-70 and
`-0x20` for frames 71-109. `0x20` is at or above the `0x1a` analog threshold
and below the `0x29` digital Left/Right threshold, so the digital records stay
clear (every sample shows `+0x80 == +0x90 == 0`) and only the analog branch can
respond. Every record carries one frame ordinal.

| frame | `+0x31a1` | `+0x3144` | `+0x2e7c` | `0x0049b500` angle | observation |
|---|---:|---|---:|---:|---|
| 31 | `0x20` | `0 -> 46080` | 1 | 27 | target `(0x2d000 * 0x20) >> 7 = 46080` reached in one weighted step |
| 32-37 | `0x20` | `46080` | 0 | 27 | at target: no branch selected |
| 71 | `-0x20` | `46080 -> -46080` | 1 | -30 | negative target, same arithmetic |
| 110-112 | 0 | `-46080 -> -34560 -> -25920 -> -19440` | 0 | -23, -18, -13 | release decay `turn -= turn >> 2` |

At frame 31 the matrix at `+0x2e58` goes from
`[-4096,0,-6; 0,-4096,0; 6,0,-4096]` to
`[-4092,0,-175; 0,-4096,0; 174,0,-4092]`, the velocity phase turns `+0x4c`
from `(290, 0, 195101)` into `(8339, 0, 194898)`, and the `0x0049f0e5` commit
sees the new basis `+0x30f4 = (-175, 0, -4092)`. Frame 95 (left lean on a
tilted surface, `[-4093,0,155; -14,-4081,-349; -155,349,-4079]`, angle -30)
publishes `[-4081,0,342; -31,-4081,-348; -343,349,-4067]` and basis
`(342, -348, -4067)`. Both frames are native fixtures in
`src/runtime/ground_turn_test.cpp`: `GroundTurn::update`, `angle12`,
`q12_apply_ground_yaw`, `retail_basis_from_matrix` and
`q12_rotate_ground_velocity` reproduce them exactly.

Frames 47-94 called `0x0049b500` with a nonzero angle without changing the
matrix. `0x0049b500` returns immediately unless `angle != 0 && +0x2f64 == 0`;
those frames follow the landing from the ramp at frame 46, and `+0x2f64` was
not sampled, so the gate owner is not asserted here.

### State-1/2 half of the same producer

The same run left the ramp at frame 38 with the lean still at `0x20`. In state
1 the accumulator went `46080 -> 163840`, and after it was cleared
(frame 46 entry `0`, the last state-1 frame before landing) it went
`0 -> 153600`. Both values are the grounded tail's
arithmetic with the in-air parameters:

```text
limit = 0xa0000, step = (DAT_0056865c * 0xa000) >> 8   (0x64000 while the
                                                      +0x40/+0x60 records are held)
target = (0xa0000 * 0x20) >> 7 = 163840
0      + ((163840 >> 12) * 102400) / 80 + 102400 = 153600
46080  + ((117760 >> 12) * 102400) / 80 + 102400 = 184320 -> clamp 163840
```

Static structure (`build/ghidra/decomp/3370.c`): the state-0/7 and the other
states' setup blocks both fall into one shared tail at `0x00493a9e`
(`jmp 0x493a9e` from `0x004939b9`), which applies Left, then Right, then either
the `>> 2`/`>> 1` release decay (`|lean| < 0x1a`) or the analog target, and
mirrors `+0x3144` into `+0x3148` at `0x00493cdd`. For states other than 0/7,
no direction with `|lean| < 0x14` first clears `+0x3144` (`0x00493990`). The
native `update_in_air_orientation_accumulator` now reuses the shared tail with
those parameters (`base_turn_override = 0xa0`) and mirrors the result.

### The rest of the non-grounded branch (Phase 4.2)

`Skater_PhysicsFrame` calls `0x00493370` on every frame: `0x0049e939..
0x0049e97d` is straight-line and every earlier path converges on `0x0049e939`.
Its `else` branch therefore runs for every state except 0/7, not only 1/2
(native used to skip states 3-6 and 8; Warehouse only visits 0, 1 and 2).

- **Spin gate.** In state 1/2 a nonzero `+0x2e80` spin count clears `+0x3144`
  and returns before the direction logic, the `0x00493cdd` mirror and the
  heading update; every other state zeroes `+0x2e80`. Native honours the
  gate. The count's producer (the `+0x51`/`+0x71` press latches, the `+0x3068`
  phase, the `+0x306c` half-spin count and `0x0048ddc0`) moved to the paused
  `air-spin-controls` slice.
- **Records.** Unless the `DAT_0056b7f0` cheat is on, the NOLLIE (`+0x40`) and
  SWITCH (`+0x60`) records steer like Left/Right. With the cheat on they do
  not, and the branch clears the `+0x2c80` action gate; a nonzero `+0x2c80`
  discards every direction. `DAT_0056b7f0`'s only writer, `0x004afa93`, XOR-
  toggles it when `DAT_0056c578 == 0x20c49738`: a checksummed cheat code,
  default 0 and 0 in every Warehouse frame.
- **Fast step.** `DAT_0056b038 || DAT_0056b058` select `0x64000` instead of
  `0xa000`. Those are controller 0's `+0x40/+0x60` records (controller base
  `0x0056aff8`, `+0x148 = 0x0056b140`), whichever player is updating; for
  player 0 they are its own records.
- **Re-press window.** Every direction-free frame stamps `+0x3060 =
  AnimationClock`; a direction pressed while `AnimationClock - +0x3060 < 8` is
  ignored (the stamp does not move while it is held). The records are ORed in
  again after the window, so they bypass it. The grounded branch zeroes
  `+0x3060`.
- **`+0x2e84`.** The state-1 block after `0x00492f20` produces the value
  `0x00497f40` consumes at `0x004983cb`/`0x00498459`. Native computes the same
  value at the consumer (`compute_in_air_orientation_angle`); only the
  `+0x29b0 != 0` target-relative branch is not modelled.

A live retail trace (Warehouse, analog lean 0x20, record bytes written at
`0x00493370` entry, `DAT_0056865c = 640`, AnimationClock +5 per frame)
confirms each branch. It takes off at frame 45:

| Frame | Input | Clock - stamp | `+0x3144` in -> out | Branch |
|---|---|---|---|---|
| 45-46 | Left | - (stamp 0 from the ground) | -179160 -> -281560 | ordinary 0xa000 step |
| 47 | NOLLIE | 5 | 163840 -> -655360 | fast step to the clamp; record bypasses the window |
| 49 | none | - | -655360 -> -296960, stamp 239 | stamp, then the lean target |
| 51 | Left | 5 | -51200 -> 117760 | Left ignored; lean target |
| 52 | Left | 10 | 117760 -> 15360 | Left applies |
| 53 | Left (landed) | - | stamp -> 0 | grounded branch |

(The first two rows are from the first run of the probe; the others from the
second, whose schedule placed the injections in the air.) Frames 47, 49, 51
and 52 are native fixtures in `player_state_test.cpp`; the five Warehouse
strict native replays stay 256/256.

The five Warehouse strict native replays (`build/scenarios/*/retail.otrec`,
recaptured 2026-09-28) match 256/256 frames both before and after this change.

## Exact orientation writer

The grounded function `0x00496550` calls `0x00496360` after its initial motion/collision preparation. `0x00496360` reads the signed accumulator and, when the grounded `+0x2d90` correction path is inactive, calls:

```text
turn_units = (player+0x3144) >> 12
angle12    = (DAT_0056865c * turn_units) >> 8
0x0049b500(angle12, 1, 0)
```

The `0x0049b500` writer is the strongest orientation evidence:

1. It masks the angle to 12 bits (`angle & 0xfff`).
2. `0x004e80e0` receives the angle as the Y component of a zero X/Z angle vector. Its X and Z inputs are zero on this path, so the nontrivial helper is `0x004e7de0`.
3. `0x004e7de0` computes Q12 `sin`/`cos` with x87 `fsin`/`fcos` after converting `angle12` to radians as `angle12 * (2*pi / 4096)`. It builds the signed-Q12 matrix

   ```text
   R_y = [  cos   0   sin ]
         [   0  4096   0 ]
         [ -sin   0   cos ]
   ```

   and the three `0x004e3130` calls compute `M <- M * R_y` using `(a*b) >> 12` products.
4. The rotated short matrix is stored at `player+0x2e58..+0x2e68`. `0x0049c7d0` exposes its columns as integer basis fields:

   ```text
   basis_0 = matrix column 2 -> player+0x30f4/+0x30f8/+0x30fc
   basis_1 = matrix column 0 -> player+0x3100/+0x3104/+0x3108
   basis_2 = matrix column 1 -> player+0x310c/+0x3110/+0x3114
   ```

For ordinary grounded state 0, `0x00496360` passes `param_3 == 1` (`!bVar1`) to `0x0049b500`; the state-2 / special state-1 path passes zero. Therefore the normal grounded call takes the function's second phase as well: after the matrix rotation it transforms `player+0x4c/+0x50/+0x54` through the saved matrix at `+0x2e38` and rescales the result using the pre/post vector magnitudes. That velocity update is complete before `0x00496550` builds the ordinary movement line, so it affects that frame's collision candidate and the following frame's position add. The first matrix rotation and basis write remain the direct orientation evidence; the velocity phase is kept separate in the C++ core because its saved-matrix transform is not itself the orientation candidate.

The second phase's supported fixed-point shape is:

```text
phase        = R_y((angle12 - param_4) & 0xfff)
local_phase  = transpose(saved_old_matrix) * phase
effective    = saved_old_matrix * local_phase
rotated_v    = effective * velocity_4c

old_ratio = (sqrt(dot_q12(velocity_4c, velocity_4c)) * 0x40) >> 8
new_ratio = (sqrt(dot_q12(rotated_v, rotated_v)) * 0x40) >> 8
if (new_ratio > 0)
    velocity_4c = rotated_v * old_ratio / new_ratio
```

The matrix products and vector transforms use the same Q12 short/integer
helpers as the orientation writer. A first-Left runtime sample with saved
matrix `[-4096,0,-6; 0,-4096,0; 6,0,-4096]` and velocity `(282,0,192408)`
therefore yields `(-2066,0,192364)` at this phase boundary; the later ground
surface/collision code can change that value before the final commit.

## Basis to grounded movement

The basis is consumed in the same per-frame grounded pipeline rather than being an isolated animation value. The ordinary state-0 path has four ordered stages around the turn refresh:

1. `0x0049b010` uses the basis copied at the start of `0x0049e680` to build the temporary correction at `player+0x58/+0x5c/+0x60`.
2. `0x00496550` integrates the live position at `0x004967b6`.
3. `0x00496360` calls `0x0049b500`, rotating the short matrix from the updated turn accumulator and completing its response phase before the ordinary movement line.
4. `0x0049c7d0` refreshes the integer basis from that matrix for ground-follow, projection, and collision handling.

This ordering matters: the prephysics correction uses the basis copied before the turn writer, while the ordinary movement line uses the response and basis published by `0x0049b500`. The refreshed basis also controls the subsequent correction/collision work and the next frame's integration.

`0x0049b010`, called before the dispatcher by `0x0049e680`, uses `player+0x30f4/+0x30f8/+0x30fc` to write the temporary correction vector at `player+0x58/+0x5c/+0x60` in grounded branches. `0x00496550` also uses the basis after its collision work. The exact helper arithmetic is:

```text
dot_q12(a, b) = trunc_toward_zero((a.x*b.x + a.y*b.y + a.z*b.z) / 4096)
mul_q12(a, b) = trunc_toward_zero(a*b / 4096)

lateral = dot_q12(player+0x4c, basis_1)
player+0x4c -= mul_q12(lateral, basis_1)

forward = dot_q12(player+0x4c, basis_0)
projection = mul_q12(forward, basis_0)
if (+0x2d94 == 0 && surface_gate != 0)
    correction -= frame_scale_q8 * mul_q12(8, projection) >> 8
```

The helpers at `0x004f5f90` and `0x004f5fc0` perform the divisions by `4096`; their common `fistp` path sets x87 round-toward-zero. This is the smallest supported movement formula: the current motion vector is projected/scaled against the rotated basis, producing a world-space correction vector. `0x0049bad0` and the surrounding `0x00496550` collision code can additionally project velocity, rotate/normalize the surface basis, and construct collision-adjusted candidates. That is why the final X/Z delta is not a pure `sin/cos` expression of Left/Right.

### Ordinary grounded position integration

The direct position add is in `0x00496550`, at the call to `0x004ca9f0` at `0x004967b6`. At that point `ebx` is `player+0x08`, so the vector helper updates all three live position words. The vector passed to it is assembled from two terms:

```text
dt      = DAT_0056865c                 // Q8 frame scale
dt2     = DAT_00568804 = (dt * dt) >> 8

accel0  = wrap32(correction_58 * dt2)
accel1  = accel0 >> 8
accel   = accel1 / 2                   // x86 signed idiv, toward zero

vel0    = wrap32(velocity_4c * dt)
vel     = vel0 >> 8

position += accel + vel
```

The three helper stages are visible immediately before `0x004967b6`:

```text
correction_58 -> 0x004cac30(* DAT_00568804)
               -> 0x004cacd0(>> 8)
               -> 0x004cac90(/ 2)
velocity_4c   -> 0x004cac30(* DAT_0056865c)
               -> 0x004cacd0(>> 8)
sum           -> 0x004cabb0
position      -> 0x004ca9f0 at 0x004967b6
```

`DAT_00568804` is written by the frame-scale update at `0x00468c96` as `(DAT_0056865c * DAT_0056865c) >> 8`. The multiply helpers use the low 32-bit x86 product; the shifts are arithmetic and the divisor stage is signed integer division. In a C++ recreation, use explicit 32-bit wrapping or widened intermediates followed by a defined wrap before shifting so signed-overflow UB does not change the result.

This is the supported grounded X/Y/Z candidate formula before the later collision fallbacks. It also explains why `player+0x4c` is not itself the position delta: it is first scaled by `dt`, while `player+0x58` supplies the acceleration/correction half-step.

`0x00496550` contains direct calls to `0x00496060` for intermediate collision candidates. After the dispatcher returns, the outer wrapper reaches the observed final commit at `0x0049f0e5`.

The final wrapper sequence is explicit in `0x0049e680`:

```text
current live X/Y/Z = player+0x08/+0x0c/+0x10
player+0x08/+0x0c/+0x10 = history +0xbc/+0xc0/+0xc4
call 0x00496060(current live X, current live Y, current live Z)
```

At `0x00496060`, the shared commit receives X/Y/Z in that order and writes the three live position words (`+0x08`, `+0x0c`, `+0x10`). With `player+0x3200 == 0`, it first collision-tests the candidate and tries component fallbacks if necessary; with `+0x3200 != 0`, it writes directly. Thus `0x0049f0e5 -> 0x00496060` is a collision-safe commit, not just a blind assignment.

## Dynamic Warehouse corroboration

The clean corrected run is `build/debug/sessions/ground-orient11/orient.trace.ndjson`. It was started in Warehouse through the Free Skate path with the level-launch override, then recorded an idle prefix followed by short Left and Right holds. The directional records were filtered to the same callsite and state:

| phase | records | action mask | callsite | physics state | first X/Y/Z argument | last X/Y/Z argument | argument delta |
|---|---:|---:|---|---:|---|---|---|
| idle prefix | 350 | `0` | `0x0049f0e5` | `0` | `(365.1250, -90.0625, 170.6875)` | — | — |
| Left | 8 | `0x8000` | `0x0049f0e5` | `0` | `(365.2778, -90.0625, 274.7060)` | `(362.4892, -89.6207, 294.9813)` | `(-2.7886, +0.4418, +20.2753)` |
| Right | 9 | `0x2000` | `0x0049f0e5` | `0` | `(352.8268, -87.6475, 313.3138)` | `(342.5837, -79.0211, 333.8102)` | `(-10.2431, +8.6264, +20.4964)` |

Both controlled phases stay on the grounded state-0 dispatcher path and alter the horizontal X/Z commit arguments. Their starting positions are intentionally sequential rather than reset-identical, so the total deltas are not a paired magnitude comparison. The trace does establish the action-to-orientation sign and the orientation-to-commit correlation in one reproducible callsite/state window.

The orientation fields captured alongside those commits are the strongest runtime candidates:

| field | idle | Left window | Right window | supported interpretation |
|---|---:|---:|---:|---|
| `player+0x3144` | `0` | `-30720` … `-184320` | `-2085` … `+184320` | signed turn accumulator; Left decreases, Right increases |
| `player+0x3148` | `0` | equals `+0x3144` | equals `+0x3144` | mirror used by steering/pose code |
| `player+0x2e58..+0x2e68` | stable initial basis | changes as Left angle accumulates | changes as Right angle accumulates | 3x3 short matrix updated by the angle writer |
| `player+0x30f4..+0x3114` | fixed basis | changes with the matrix | changes with the matrix | integer/fixed-point basis consumed by grounded correction |

The matrix entries and basis words are Q12. Representative commit-adjacent samples from the same run:

```text
Left  turn=-30720  basis_0=(+44,    0, -4096)  ~= (+0.0107,  0.0000, -1.0000)
Left  turn=-184320 basis_0=(+978,   0, -4096)  ~= (+0.2388,  0.0000, -1.0000)
Right turn=-2085   basis_0=(+2151,-344, -3470) ~= (+0.5251, -0.0840, -0.8472)
Right turn=+184320 basis_0=(+1038,-676, -2677) ~= (+0.2534, -0.1650, -0.6536)
```

The short matrix and fixed basis are not merely pose snapshots: the static writers and consumers above show that they feed the correction vector and the candidate passed to the shared position commit. The position samples also show that X/Z changes continue while the turn sign changes, but the world-space result is affected by inherited velocity and collision/surface projection.

As a sign/order check, the first Left sample records `turn_accum = -0x7800`. With the ordinary `0x78` turn branch at `DAT_0056865c = 0x100`, `angle12 = -8`; applying the Q12 `R_y(-8)` matrix to the preceding short matrix predicts the observed first-turn entries (`row_0.z = +44`, `row_2.x = -45`). This ties the measured accumulator to the measured basis rotation, rather than only correlating both with the input mask.

The trace footer says `complete: false` because the debugger was stopped after collection, but all 17 controlled position records are present and all are state 0 at the target callsite. The later seven state-2 records were excluded.

## Fresh B010-to-commit replay

The stronger replay is `build/debug/sessions/ground-motion-final3/ground.trace.ndjson`.
It forced Warehouse, used `render_present` as the frame clock, and injected short
post-poll action-mask windows so that the directional phases were reproducible.
The raw keyboard-to-mask mapping is established separately in
`re/evidence/functions/input.md`; the injections here are only the deterministic
runtime mechanism used to hold the already identified masks.

The three clean windows are all on the same grounded callsite and state:

| phase | render frames | action mask | B010 records | position commits | physics records | physics state |
|---|---:|---:|---:|---:|---:|---:|
| idle | 1–9 | `0` | 18 | 9 | 9 | `0` |
| Left | 10–39 | `0x8000` | 44 | 30 | 30 | `0` |
| Right | 80–109 | `0x2000` | 30 | 30 | 30 | `0` |

Every target position record is the `0x0049f0e5 -> 0x00496060` path. The
directional input therefore changes the turn/basis state while the native
grounded producer is still running, rather than selecting a different physics
state or commit callsite.

The producer-side profile inputs were constant in all three clean windows:
`local_profile_lookup = {mode: 1, player_index: 0, lookup_index: 0, value: 1}`,
`profile_gate = true`, profile slot `+0x10 = 0`, and
`correction_before_raw = (0, 0, 0)`. Thus this replay does not attribute the
Left/Right difference to a profile-selection transition. The profile and
control writer probes still show correction/control stores during the same
grounded windows, so the later producer stages remain part of the result.

The orientation and commit arguments move together:

| phase | turn accumulator | basis column 0 | first → last commit argument | candidate delta (X/Y/Z) |
|---|---|---|---|---|
| Left, frame 10 → 39 | `-35760 → -184320` | `(-0.06226, 0.55762, 0.82739) → (0.93457, 0.00928, 0.35474)` | `(419.342743, -5.960754, 660.143799) → (403.552780, -1.802307, 665.339386)` | `(-15.789963, +4.158447, +5.195587)` |
| Right, frame 80 → 109 | `+30720 → +184320` | `(0.86792, 0, 0.49585) → (0.24268, 0, 0.96875)` | `(319.960281, -2.386719, 618.510513) → (309.478394, -1.824219, 596.463715)` | `(-10.481888, +0.562500, -22.046799)` |

The absolute starting positions are sequential, not reset-identical, so these
total deltas are not a paired magnitude comparison. They do establish the
reproducible chain `action mask -> 0x00493370 turn accumulator -> 0x0049b500
basis refresh -> grounded X/Z commit argument`. Per-frame samples also show
that the result is not a direct lateral add: inherited `velocity_4c`, the
orientation basis, later correction/projection, and collision-safe commit all
contribute. For example, the Left frame-30 net commit delta is approximately
`(-1.081528, +0.363831, +0.391174)` while its captured `velocity_4c` is
`(-0.988373, +0.225769, +0.197632)` and the later correction is
`(-0.004257, +0.030487, +0.045029)`.

The same replay completed 200 shared collision queries while the player was in
state 0: 48 hits and 152 misses, with changing surface normals. This confirms
that active surface/collision handling is present in the grounded window, but
the earlier records were collected before the probe gained frame and action
mask fields, so they cannot yet be joined one-to-one to each directional commit.
The next targeted replay should use the updated collision probe for that join;
it is the remaining runtime evidence gap, not evidence that the turn or basis
chain is unresolved.

## Ordinary B010 correction branch

The selected unresolved unit for this run is the ordinary scale-1 correction
branch in `0x0049b010`, beginning at the `0x0049b2c1` gate. Its ownership is
limited to the transient acceleration write; it does not classify surfaces,
query collision, or request a physics state. The recovered predicate is:

```text
AutoKickSetting[player] != 0 || controller+0x10 held   (autokick-option.md)
physics lock and ground-motion mode gates allow the producer
ordinary grounded state == 0
turn correction gate is open
animation state is not 2, 3 or 0x5e
response_speed_metric > 0x4e20 || basis_y < 0x1f4
response_speed_metric < ground_motion_threshold
physics state != 2
```

When all predicates pass, the function writes the three signed 32-bit words
at `+0x58/+0x5c/+0x60` as `-basis(+0x30f4)` at scale 1 (stores
`0x0049b309/0x0049b30c/0x0049b30f`). The speed comparison is strict against
`+0x2dc8`. The basis-Y test (`0x0049b2c9`, strict) applies only while the
metric is at or below `0x4e20`: `0x0049b2c1` jumps straight to the threshold
test when the metric is greater. (An earlier revision of this note listed the
two as a conjunction; the native model already implemented the disjunction.) The native boundary keeps the
profile/lock/mode values caller-owned and preserves the low word of the
32-bit basis product before exposing the signed result.

The deterministic fixtures in `src/runtime/ground_motion_test.cpp` close the
branch edges without inventing the unresolved profile/stat owner:

| fixture | result |
| --- | --- |
| metric `0x4e20`, threshold `0x10000` | scale-1 write occurs |
| metric equals threshold `0x10000` | no write; strict threshold test |
| basis Y `0x1f3` | scale-1 write occurs |
| basis Y `0x1f4` | no write; strict slope/basis test |
| correction gate closed | no write |

The same test exercises the signed low-word write with a synthetic overflow
input. That fixture validates the PE32 arithmetic contract only; it is not a
claim about the retail range of the Q12 basis values.

### Ordinary B010 correction joined to its position candidate

A headless Warehouse GDB run (2026-09-28, 120 gameplay frames, default
settings) armed one frame counter at `Skater_PhysicsFrame` and probes at the
four B010 correction writers, the return from B010 (`0x0049e9a6`), the
`0x004967b6` position add and the `0x0049f0e5` commit, all filtered to the
live player. Every record carries the same frame ordinal, which closes the
join the earlier trace could not make.

- 26 B010 correction writes: 25 on the ordinary scale-1 branch
  (`0x0049b309`) and one animation-`0x5e` scale-8 write (`0x0049b2b3`).
- In all 26 frames `+0x58` is unchanged from B010's return to `0x004967b6`,
  and the vector added there equals
  `((correction * DAT_00568804) >> 8) / 2 + ((velocity_4c * DAT_0056865c) >> 8)`
  exactly (0 mismatches).
- The `0x0049f0e5` commit argument equals position + that vector in 19 of the
  26 frames; in the other 7 a later ground-follow/collision pass moves Y/Z
  (for example frame 33: `+6080` in Y), which belongs to the collision
  boundary rather than to B010.

Frame 17 is kept as a native fixture in `src/runtime/ground_motion_test.cpp`:

| field | retail value |
|---|---|
| AutoKickSetting / physics state / animation at the write | `1` / `0` / `0` |
| speed metric (`EDI`) / `+0x2dc8` threshold | `74240` / `190886` |
| basis `+0x30f4` | `(-6, 0, -4096)` |
| correction before → after B010 | `(0, 0, -365)` → `(6, 0, 4096)` |
| velocity `+0x4c` | `(111, 0, 74268)` |
| `DAT_0056865c` / `DAT_00568804` | `640` / `1600` (slow headless GDB frame) |
| position before → `0x0049f0e5` argument | `(23931313, -5902336, 12858710)` → `(23931608, -5902336, 13057180)` |

`apply_ground_motion()` reproduces the write and
`PlayerState::integrated_position(640)` reproduces the commit argument.

A controlled A/B of the same launch with `AutoKickSetting[0]` forced to 0 at
the first gameplay frame produced **no** B010 correction writes in 120 frames
and the skater never moved (Z stayed `11186176`, final speed 0), against 26
writes and a final Z of `58283707` with the default On. The runtime therefore
confirms the static owner in `autokick-option.md`: the B010 outer gate is the
Autokick option.

## Ground-physics mode-1 transition

The selected unresolved unit for this run is the `case 1` branch of
`FUN_0049df00`. It owns the ground-motion mode
transition and the raw animation-control handoff; it does not own the
surface/material predicate, the audio/script callees, or general physics-state
dispatch.

The static decompilation at `build/ghidra/decomp/ground-physics.c:107-128`
establishes this exact predicate and order:

```text
advance = !surface_allows_brake ||
          speed_metric < (signed animation_frame * 0x1000) + slope_threshold
if (advance) {
    old_start = *(u16 *)(player + 0x114)
    *(u8  *)(player + 0x107) = 0
    *(u8  *)(player + 0x100) = 0xff
    *(i16 *)(player + 0x114) = (signed char)*(player + 0x101)
    *(char *)(player + 0x101) = (char)old_start
    *(i32 *)(player + 0x2df8) = 3
    if (surface_allows_brake)
        play surface-selected service / update object +0x30b0
}
*(i32 *)(player + 0x2f2c) = 2
return
```

The comparison is strict: equality leaves `+0x2df8` at `1`, while the
surface-failure side of the `||` advances regardless of speed and suppresses
the surface-eligible service. The byte/short exchange is also significant:
`+0x114` contributes only its low byte to `+0x101`, and the old `+0x101` byte
is sign-extended when restored to `+0x114`. This is recorded as a raw handoff,
not as a new animation-selection policy; the animation session remains the
consumer of the resulting control fields.

The native `update_ground_physics()` result now reports this handoff alongside
mode `1 -> 3`, the caller-visible `animation_transition` service gate, and the
unconditional cooldown `2`. `src/runtime/ground_physics_test.cpp` covers the
equal boundary (`speed_metric == slope_threshold`), the strict-below side,
the low-byte/sign-extension exchange (`0x1234` and `0x82`), and the
surface-failure bypass. Confidence is static-confirmed and native-tested.

### The brake predicate, services and animation requests (2026-09-28)

`FUN_0049df00` is `Skater_UpdateBrakeMode` (its default case asserts
"Illegal Brake mode %d"). The predicate earlier called `surface_allows_brake`
is not a surface/material test: `0x0049df4e..0x0049df90` compute it from
input alone.

```text
brake = (controller+0xb0 Down && !controller+0x80 Left && !controller+0x90 Right)
     || (signed skater+0x31a2 > 0 && skater+0x31a1 == 0)
```

The native field is now `brake_requested`, with the test in
`ground_brake_requested()` (`src/runtime/ground_brake.hpp`).

Services after the transitions, as data in `GroundPhysicsResult`:

| transition | retail call | native report |
|---|---|---|
| mode 1 -> 3 while braking | `Rnd(0x80)` (`0x004c9340`), then `SFX_PlayX(roll < 0x40 ? 0xb1 : 0xb0, 0x2000, 0)` (`0x0049e25a`) | `services.random_release_sound`, `ground_brake_release_sound(roll)` |
| mode 1 -> 3 while braking | `FUN_00431260` on the skater's embedded dust emitter (`ECX = skater+0x2c14`; `dust.cpp`, asserts "Unsupported dust type") with `(skater+0x30b0, 2)` | `services.dust`, `dust_type == 2` |
| mode 5 -> 6 while braking, frame > 4 | `SFX_PlayX(2, 0x2000, 0)` (`0x0049e2e7`) | `services.fixed_release_sound`, `fixed_sound_id == 2` |

`SFX_PlayX` and `Rnd` are the PSX-demo names (`re/symbols/imported.yml`,
provisional/inferred); the other dust call site (`0x0049ec8e`, in
`Skater_PhysicsFrame`) uses type 1.

Animation requests, as `GroundAnimationRequest` data. Each wrapper takes a
trailing source-line tag (`0x2c6a`, `0x2c70`, `0x2cb8`, `0x2cba`, `0x2cc4` —
`physics.cpp` line numbers, like the state-transition reasons) that it does
not forward: `0x004903f0` is `ret 8` and always passes start frame 0 to
`Animation_Start`.

| transition | request |
|---|---|
| mode 0 -> 5 | `Start(0x31)` unless animation `0x5d`/`0x5f` plays |
| mode 0 -> 1 | `Start(0x83)` unless animation `0x5d`/`0x5f` plays |
| mode 3 -> 4 (`+0x107` set) | `Range(8, 0x13, 0)`, or `Full(8, 0x13, 0x1a, 0x13)` while `+0x2dd8` is set |
| mode 6 -> 4 (`+0x107` set) | `Start(0)` |

Mode 5 -> 6 performs the same raw `+0x107/+0x100/+0x114/+0x101` exchange as
mode 1 -> 3 (`0x0049e29c..0x0049e2c2`); the native port had omitted it and now
reports it.

### Brake mode 1 joined to its animation, sound and dust requests

A headless Warehouse GDB run (2026-09-28) let autokick run for 20 physics
frames, rescaled `+0x4c` to magnitude `0x48000` at frame 20 (a **controlled
injection**: on flat Warehouse ground autokick stops pushing near the
`+0x2dc8` threshold, below the `0x30000` high-speed cut-off) and then held
Down (action mask `0x4000`) from frame 22 to 95. Every record carries one
frame ordinal:

| frame | observation |
|---|---|
| 22 | Down read at `+0xb0`; mode `0 -> 1`; wrapper `Start(0x83, 0x2c70)` -> `Animation_Start(0x83, 0, -1, -1)`; `+0x4c` `(427, 0, 287768) -> (362, 0, 242806)` |
| 23-28 | mode 1, `v -= (v >> 4) * DAT_0056865c >> 8` each frame (`DAT_0056865c` = 640) |
| 28 | mode `1 -> 3`; `Rnd(0x80)` = 4 -> `SFX_PlayX(0xb1, 0x2000, 0)`; dust `ECX = skater+0x2c14`, args `(0, 2)`; exchange `+0x100 = 0xff`, `+0x101 = 0`, `+0x114 = 21` |
| 29-34 | animation `0x83` plays backwards to frame 0 |
| 34 | speed ~40329 below the `0xa000` threshold: state request `(7, 0x2c56)`, `+0x4c` `(0, 4320, 40097) -> (0, 0, 0)` |
| 35 | `+0x107` set: wrapper `Range(8, 0x13, 0, 0x2cb8)` -> `Animation_Start(8, 0x13, 0, -1)`; mode `3 -> 4` |
| 36-95 | mode 4, physics state 7 while Down is held |
| 96 | Down released: state request `(0, 0x2c21)`, mode `4 -> 0` |

Frames 22, 28, 34, 35 and 96 are native fixtures in
`src/runtime/ground_physics_test.cpp`, including the exact decelerated `+0x4c`
words. That fixture exposed a native bug: `update_ground_physics()` passed the
animation frame where retail reads `+0x31a2` (`0x0049e033`) to scale the brake
term, so any nonzero animation frame suppressed braking. It now passes the
vertical lean.

## Native reference core

The supported arithmetic is implemented in the focused native reference core:
[ground_movement.hpp](../../../src/ground_movement.hpp) and
[ground_movement.cpp](../../../src/ground_movement.cpp). `step_grounded`
preserves the recovered order through the prephysics correction, initial
position integration, turn-derived matrix/basis refresh, ordinary velocity
phase, optional grounded projection, and the candidate resolver standing in
for the geometry-dependent portion of `0x00496060`. The resolver boundary is
intentional: the trace and disassembly establish the commit call and its
component fallbacks, but not one portable Warehouse geometry query that can be
embedded in this small core.

## What is and is not established

Established:

- `player+0x2ccc` is the controller pointer used by the skater physics code.
- Controller `+0x80/+0x90` are the grounded Left/Right reads.
- `0x00493370` applies opposite signed updates to `player+0x3144` and mirrors the result to `+0x3148`.
- `0x00496360` consumes `+0x3144` in the normal grounded function.
- `0x0049b500` performs the fixed-point 12-bit angle/Y-trigonometric matrix rotation.
- `player+0x2e58..+0x2e68` and `+0x30f4..+0x3114` are the resulting orientation/basis storage used by grounded correction code.
- The short matrix is Q12, and `+0x30f4..+0x3114` are sign-extended Q12 basis columns, not 16.16 position vectors.
- Left and Right both reach `0x0049f0e5 -> 0x00496060` with `physics_state == 0` and produce different X/Z arguments.
- The Warehouse runtime probe records Left and Right at that same callsite with opposite `+0x3144` trajectories and corresponding matrix/basis changes.

Not promoted:

- `+0x3144` is a signed turn-angle accumulator, not proven to be an absolute world heading.
- `+0x3148` is a mirrored steering/pose value, not an independent orientation vector.
- `+0x4c/+0x50/+0x54` are not direct Left/Right displacement fields.
- No single closed-form X/Z equation is claimed: inherited velocity, surface basis, and collision correction all contribute to the final candidate. The faithful recreation target is the fixed-point action/turn/matrix/projection pipeline, not a direct `X += left/right` shortcut.

Open:

- The Autokick-off low-speed decay path at `0x0049d630`, reached when
  `AutoKickSetting` is off, has no name, native model or slice yet; no
  Warehouse scenario runs with Autokick off.
