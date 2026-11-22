# Simulation-time callback producer

Status: confirmed; the model reproduces every recorded boundary and entry
clock, and strict retail and native replays use it (Phase 4.2)

The Warehouse runtime trace identifies the multimedia timer callback at
`0x004dace0` as the producer path that feeds the simulation-time accumulator.
The callback is registered by the timer setup path at `0x004dac88` and runs on
Wine's `TIME_MMSysTimeThread`, separate from the gameplay thread.

At callback entry the observed stack is:

```text
[esp+0x00] callback return address
[esp+0x04] callback argument 0
[esp+0x08] callback argument 1
[esp+0x0c] timer state pointer = 0x006a05a0
```

The callback reads the interval from `timer_state + 0x04`, accumulates it in
`timer_state + 0x0c`, updates the floating accumulators at `0x006a0590` and
`0x006a0598` when the pause gates permit it, and reaches the integer stores:

```text
0x004dad58  -> DAT_0056e31c
0x004dad68  -> DAT_0056e320
```

The callback-final-store probe at `0x004dad68` remains available for direct
callback characterization. Normal recording instead samples the atomic
`timer_state + 0x0c` counter at deterministic gameplay boundaries. Because the
callback increments that counter before storing its floating accumulators, a
boundary can observe an in-flight delivery. The sampler therefore compares the
simulation accumulator at adjacent boundaries, defers such a counter tick as
pending, and records only completed deliveries with logical boundary counters.
The sampled raw counter and pending count are retained as consistency metadata.
New recordings also retain the callback-owned initial timer state in the
header.

Strict replay now owns a `TimerReplayService`. It applies exactly the recorded
delivery events through the same transition model at their recorded causal
phases (`physics_entry`, `timer_update`, the simulation-clock read, and the
timing-producer reads), publishes only after actual deliveries, and leaves
`DAT_0056e320` to the retail player load/store chain. The asynchronous callback
is suppressed at entry with a process-local ABI-preserving return patch, so it
cannot add an unrecorded delivery. Producer-phase samples separately assert the
modeled clock stream and timing-ring inputs without making volatile process
clock fields part of the player snapshot comparison.

The offline `advance_timer` model in `src/camera/camera_timing.hpp` mirrors the
callback's state transition and x87-style double-to-integer publication. Its
fixture covers repeated 16 ms deliveries and both pause gates. A fresh 257-frame
idle Warehouse recording now strict-replays with every canonical player frame
matching; repeated long recordings and action-bearing fixtures remain required
before this slice is considered complete.

The `0x004f5ff0` path remains a separate millisecond-clock helper and is not the
callback boundary that produced the observed Warehouse simulation-time values.

## Where the deliveries fall in a frame (Phase 4.2)

The gameplay loop (`0x0046a44b..0x0046a48d`) runs the timing update
`0x0046a0f0` and then `0x0046a1a0`, which calls the present path
`0x0042ffc0 -> 0x004d0bc0` at `0x0046a216` before tail-jumping to the object
update `0x00466c00` that reaches `Skater_PhysicsFrame`. `0x004d0bc0` is the
frame limiter:

```text
while SimulationPublicTick < DAT_0056e840 + 2: WINMAIN_Sleep(0)   ; 0x004d0bd2
DAT_0056e840 = SimulationPublicTick                              ; 0x004d0bfb
```

The only other `DAT_0056e840` writer is the level reset at `0x00468e2b`. So
the deliveries a frame waits for arrive between the timing update and physics
entry. The in-process recorder samples at `0x0046a0f0` entry and at physics
entry; `tony/capture.py` (`_normalize_timer_samples`) keeps the physics-entry
sample's deliveries under the timing-update label, which is why every
recorded frame carries two timing-update samples: `post_physics 0,
timer_update 0, timer_update N, clock_read 0`.

This also explains the GDB retail replay stall that was handed over from
Phase 4.1. The replay applied the whole timing-update phase at `0x0046a0f0`,
before the present, so `DAT_0056e840` latched a tick later than retail did,
and a later limiter needed more ticks than the recording holds. In
warehouse-idle this happened at frame 3, after the frame-2 startup hitch of
17 deliveries.

The replay now treats boundary samples chronologically (`TimerReplayService`
keeps one cursor per frame):

- The `0x0046a0f0` visit closes only its own sample (`max_segments=1`).
- While the game thread spins a delivery site that the lockstep clock also
  uses (`Game_FrameTick`, `WINMAIN_Sleep` and the two call-free tick loads), a
  site hit a second time since the last physics boundary receives the next
  recorded delivery of the open segment, published as the callback would.
- Physics entry closes whatever is still open before it; every closed segment
  still checks its recorded start and end state.
- A limiter still spinning after 20000 repeated hits with nothing left to
  deliver is reported as a timer divergence rather than a hang.

Results with the unchanged in-process recordings:

| Recording | Frames | Deliveries | Handed to limiters | GDB retail replay |
|---|---|---|---|---|
| warehouse-idle | 256 | 545 | 528 | 256/256 |
| warehouse-straight | 256 | 531 | 531 | 256/256 |
| warehouse-turn | 256 | 531 | 531 | 256/256 |
| warehouse-ollie-land | 256 | 532 | 532 | 256/256 |
| warehouse-turn-ollie | 256 | 531 | 531 | 256/256 |

warehouse-turn-ollie first stopped at frame 85 on `raw_physics_words[235]`
(`+0x312c`), `0x00000c2f` recorded against `0x76700c2f` replayed. That is the
16-bit surface component whose high half retail never initializes (Phase 3,
tony/capture.py's portable scope); the replay comparator now masks the same
high half, and the full run then matches every other byte of all 256 frames.

## Callback count, interval and pause gates across recordings

An offline check (`build/p42/timer_stats.py`) closes every recording's
boundary samples in the replay's order and compares the modelled
SimulationTime with the value recorded at each physics entry:

| Recordings | Frames | Deliveries | Per frame | Mismatches |
|---|---|---|---|---|
| 5 Warehouse scenarios | 1,280 | 2,670 | 2 or 3; one 17 (idle startup) | 0 |
| 3 repeated idle captures, 1024 frames each | 3,072 | 6,393 | 938 x 2, 85 x 3, frame 0 none, identical in all three | 0 |

The same check also compares the model's state with every zero-delivery
sample right after its boundary closes. That covers 11,935 samples across the
eight recordings, and all match exactly: both accumulators bit-exact as raw
doubles, `SimulationPublicTick`, `SimulationTime` and the millisecond counter.
So the initial state plus the ordered deliveries reproduce every observed
integer and floating clock output.

Every delivery has the 16 ms interval and both pause gates were open in every
sample. The 2-or-3 pattern is the limiter's arithmetic, not scheduling noise:
a delivery adds `16 * 0.001 * 60 = 0.96` to the public accumulator, the
limiter waits for two whole ticks, and the 0.04 shortfall per delivery costs
one extra delivery about every 12.5 frames (85 of 1,023, 21 of 255). Only a
frame that overruns the limiter (the idle startup hitch) receives more.

The permanent fixture `re/evidence/fixtures/timer-warehouse-idle.json` keeps
the warehouse-idle boundary segments and entry clocks; `tests/
test_timer_replay.py` replays it with and without limiter-style early
deliveries and reproduces all 256 entry clocks and the 545 deliveries.
