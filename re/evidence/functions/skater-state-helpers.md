# The small skater functions of physics.cpp: pose, state, animation requests and handlers

Status: confirmed by retail forced calls (6,600 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0048eb70`, `0x0048f590..0x0048f5d0`, `0x004900b0..0x00490680`, `0x00493e10`, `0x00494020`, `0x00494090`, `0x00494180`, `0x00496280..0x00496300`, `0x0049a1b0..0x0049a1e0`,
`0x0049c7d0..0x0049c8d0`, `0x0049d8a0`, `0x0049e430..0x0049e570`, `0x004f53b0` (29 functions; slices `skater-pose-and-state` and `skater-small-handlers`)
Native: `src/runtime/skater_kit.hpp` and `skater_pose.cpp`, `skater_misc.cpp`, `skater_rail.cpp`; replay `src/runtime/skater_retail_calls_test.cpp`; probes `re/gdb/probes/phys_specs.py` (group `phs`),
`phys_specs_misc.py` (group `phm`); fixtures `re/evidence/fixtures/skater-calls-phs*.txt`, `skater-calls-phm*.txt`

## What is here

A skater (`CBruce`) is an object of 0x3538 bytes; these functions work on it at the retail addresses (`self` = `ecx`). The fields they use:

| offset | meaning |
|---|---|
| `+0x08` | position (x, y, z; Q12) |
| `+0xd8` | flags: bit 1 = the skater faces the other way (fakie) |
| `+0xf4` / `+0xf6` | frame and number of the animation being played (shorts) |
| `+0x108` | animation speed (`0x10000` = 1.0) |
| `+0x2e58` | the orientation matrix as nine shorts (right column `+0x2e58 / 5e / 64`, up `+0x2e5a / 60 / 66`, forward `+0x2e5c / 62 / 68`) |
| `+0x30f4` | the same basis as nine integers: forward `+0x30f4`, right `+0x3100`, up `+0x310c` |
| `+0x30b8` / `+0x30c0` | state / previous state (1 air, 2 ground, 5 grind, 8 handplant ...) |

- `CBruce__StateTransition(state)` (ret 8: a second argument nobody reads) counts air/ground changes in `+0x3044`, saves the forward vector scaled by `0x32c8` (first and last component negated) when the
  skater leaves the ground, and, leaving a grind (5) for the air, turns the right and up vectors a quarter: `(right, up) = (-up, right)` when "the animation is `0x23`" equals "the fakie bit is set",
  otherwise `(up, -right)`. It always republishes the orientation shorts and moves the state.
- The four `Animation_Request*` functions call `CBruce__MaybeFlipAnimationTransition` (a half-turn animation past frame 15 or a pending request toggles the fakie bit; a pending orientation flip negates the six
  basis components and the shorts that go with them), set the speed to 1.0 and call `Animation_Start(this, animation, from, to, blend)` with `-1` for what they do not take. Each takes one argument more than it
  uses (`ret 8 / 0xc / 0x10 / 0x14`).
- `CBruce__HandleTimer` ends a run: not in the modes 2 and 4; the time left (or a fixed 100 with no time limit) less the animation clock, negative (or the host flag in a netgame) ends it; in the career a level
  flag says whether to play the end sound; in the versus mode 9 the follower must be on the ground; the netgame layer is asked (`NetGame_PauseAgreement`) before `LevelEvent_InitializeAndDispatch`.
- `Skater_BlockedPhysicsReset`: a blocked skater's sideways speed (`+0x58`, `+0x60`) is cleared, its velocity damped by the animation clock, and the animation speed raised by `count * 0x5555 / 3` for every distinct
  pad press in the last 20 ticks of the 32-entry history (`+0x2a14`, head `+0x2b14`).
- `Animation_PingPong(low, high, rate)` (`0x0048eb70`): `(clock / (60 / rate)) % (2 * (high - low))`, folded back at `high - low`, plus `low`.
- `Math_SqrtTruncated` (`0x004f53b0`) is an x87 `fsqrt` of the 64-bit value, truncated by `_ftol`: the root of a positive argument, 0 otherwise.
- The rest are small field copies and sound/animation choices; see the names table.

## Method

Forced calls on the live player-one skater of the probe's Warehouse run (all 0x3538 bytes captured, a case pokes random legal fields), a few globals and a scratch slab. The animation player, the panel, the sound,
the score timer and rollback, the angle converter and the netgame queries are stubs; the GTE (a window of the register file) is real because lengths go through it. The functions that call each other
(`Animation_Request*` -> `MaybeFlipAnimationTransition`, `StateTransition` -> `PublishOrientationBasis`) are the real retail code.

## Lessons

(1) A `ret N` function needs N/4 stack arguments pushed by the forced call, whether it reads them or not; the four animation requests take 2, 3, 4 and 5. (2) `StateTransition`'s second argument is dead.
(3) The decompile hides a `rep movs` string copy before every assertion; reading the listing without those lines makes the control flow legible. (4) Several results are 16-bit in `eax` with stack garbage above
(`FUN_00492c70` answers in `al`): the adapter masks the return.
