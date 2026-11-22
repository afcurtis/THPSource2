# Skater position write path

Status: complete matching assembly; subsystem analysis in progress

The Ghidra body for `0x00496060–0x00496276` is instruction-aligned. Its only
proposal risk was `unknown-adjacent`; static bytes show that the following
`0x00496276–0x00496280` range is ten NOP padding bytes, so the function was
split explicitly without absorbing that padding.

The function uses `ECX` as the player/skater object, receives proposed X/Y/Z
words on the stack, and returns with `ret 0x0c`. The field at object offset
`0x3200` is therefore behaviorally resolved as a direct-commit gate: when it is
nonzero, the function writes those values directly to offsets `+0x08`,
`+0x0c`, and `+0x10`. The selected ordinary grounded and air-to-landing paths
take the zero/collision branch.

## The `+0x3200` gate: writer, lifetime and consumer (Phase 4.2)

The whole executable has exactly three accesses to `+0x3200` (a scan of every
`[reg+0x3200]` operand in `.text`):

| Address | Function | Access |
|---|---|---|
| `0x00495a74` | `Skater_GroundedBounceProbe` (`0x004957c0`) | `mov [esi+0x3200], 1` |
| `0x00496069` | `0x00496060` (this function) | read, the direct-commit test |
| `0x0049f224` | `Skater_PhysicsFrame` (`0x0049e680`) | `mov [ebp+0x3200], esi` with `esi == 0` |

The only setter is the bounce probe's successful second query: when the
normal-offset verification line from the restored previous position misses
(`[esp+0xac] == 0` at `0x00495a5e`), `0x00495a62..0x00495a80` writes the
directional endpoint straight to `+0x08/+0x0c/+0x10`, raises `+0x3200`, and
returns 1. `Skater_RunGroundedBounceRecovery` (`0x0049d9c0`), which calls the
probe, never touches the field, so the gate survives its restore of the saved
position.

`Skater_PhysicsFrame` clears the gate at `0x0049f224`, the last store before
its epilogue, after `0x0049e570`. `esi` is zeroed at `0x0049f09d` and is the
zero operand of the preceding compares (`cmp [ebp+0x30b8], esi` is the
state-0 test at `0x0049f1b5`). The gate therefore lives for the rest of one
physics frame.

Its consumer is the frame's outer commit at `0x0049f0be..0x0049f0e5`: unless
`DAT_0056a86c != 0`, `+0x2e90 != 0` or state 8, retail takes the live position
as the proposal, restores `+0xbc` into the live position, and re-commits
through `0x00496060`. In a frame where the bounce probe fired, that commit is
a direct store of the bounce endpoint instead of a seven-candidate collision
re-test from the previous position.

Native: `PlayerState::position_commit_bypass()` models the gate.
`commit_position` honours it; the bounce probe's endpoint write in
`PlayerPhysicsFrame::step` raises it; the end of the same step clears it
(`player_state_test.cpp` covers both branches). The native frame has no
separate outer-commit stage yet, so the gate's only effect today is on
commits later in the same frame. The five Warehouse strict native replays
stay 256/256 with it in place.

When `+0x3200` is zero, the function saves and sets global `0x00567c7c`, builds
a local collision-query structure, and repeatedly calls `0x004624d0` and
`0x00466090`. A zero result at local stack offset `0x7c` accepts the current
candidate. It tests seven combinations in this order: proposed XYZ; current X;
current Z; current Y; current Y/Z; current X/Y; and current X/Z. If none is
accepted it falls back to current XYZ. The selected position is committed and
the global guard restored. Exact role names for the two callees and result
field remain inferred.

The matching module expresses the complete 534-byte function as reviewed NASM;
it contains no `incbin`. The module and full PE remain byte-identical. The
compact [player-frame integration fixture](../fixtures/player-frame-integration.json)
records the fixed-point caller values and the native candidate ordinals used
by the two selected frame paths.
