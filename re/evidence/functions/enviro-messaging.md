# Enviro-item messaging: TRG_SendVisible and the enviro branches of SendKill (Phase 4.4c)

Status: confirmed for crate (type 2 and 12) targets by a retail observation; the power-up,
light and two-player forms are confirmed from disassembly only.
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004c77f0` (`TRG_SendVisible`), `0x004c7a00` (`TRG_SendKillFromNode`),
`0x004b1ef0` (`Spool_FindEnviroItem`), the command dispatcher sites `0x004c6f3e` (opcode `0x0d`)
and `0x004c6f58`/`0x004c6f6c` (opcodes `0x0b`/`0x0c`)
Native: `LevelTriggerState::on_visible`, `on_kill`, `set_object_flags` in
`src/trg/level_trigger_state.{hpp,cpp}`

"Enviro items" are the level's static scenery pieces that scripts can hide and destroy
(crates, breakables, doors' models). A TRG node of type 2, 9, 12 or 14 names one through the link
key stored with the node; `Spool_FindEnviroItem(key)` returns the item record and its status word
sits at `+0x04`. The native trigger state keeps one object record per node and the scene registry
turns its word into the entity's visibility, so the item itself is not a second object.

## SendVisible (`0x004c77f0`, opcode 0x0d)

The command carries one u16 (`value`). The hidden mask is `0x41` when there is one player, or
when no current skater exists; with two players it is `0x40` when the current skater is the
second player's and `0x0001` otherwise. For each linked node:

| Node type | Effect on the word |
| --- | --- |
| 2, 9, 12, 14 | resolve the enviro item; `value != 0`: `word &= ~mask`; `value == 0`: `word \|= mask` |
| 5 (power-up) | the same, applied to the word at `+0x04` of the power-up object with that node |
| 501 (light) | byte `+0x4a` of the light record: `value != 0` sets 1, `value == 0` clears it in one-player games |
| anything else | asserts "SendVisible to non-crate" |

So **show clears the hidden bits and hide sets them**. (The earlier trigger-state model left the
word unchanged for a zero command; the disassembly and the trace below both show it is set.)

## Kill (`0x004c7a00`, opcodes 0x0b and 0x0c)

Mode 0 (opcode `0x0b`) and mode 1 (opcode `0x0c`, a hit). For a linked enviro node (types 2 and 9
here; the type-12 path goes through the same key resolution in `SendVisible` only):

- mode 1: `Exp_HitEnvItem(item, 0, 0xffff)`, which explodes the crate and sets bit 0 of its word;
- mode 0: `word |= 0x41`, hiding it without the explosion.

Types 10 and 11 (rails) call `Rail_ClearState`, types 500 and 501 (lights) `FUN_00403e40`, and
type 1 is the platform and baddy path documented in `platform-object.md`.

## Retail observation

A GDB probe (`build/p44c/enviro_trace.py`, untracked) logged the first 40 calls of a Warehouse
load: 39 `SendVisible` (value 1 and value 0, 19 each with 13 crates and one type-12 node, plus one
value-0 call over 10 crates) and one hit-kill over one crate, touching 543 items in total. The
distinct word transitions are in `re/evidence/fixtures/enviro-warehouse.txt`
(`scripts/make_enviro_trace_fixture.py`):

| Command | Before | After |
| --- | --- | --- |
| show (value 1) | `0x0000`, `0x0020`, `0x8041`, `0x8061` | `0x0000`, `0x0020`, `0x8000`, `0x8020` |
| hide (value 0) | `0x0000`, `0x0020`, `0x8000`, `0x8020` | `0x0041`, `0x0061`, `0x8041`, `0x8061` |
| hit (mode 1) | `0x0020` | `0x0021` |

`level_trigger_state_test` replays all nine transitions through `LevelTriggerState`
(`set_object_flags` seeds the captured word) and `tests/test_enviro_messaging.py` checks the
fixture against the same rules.

## Not modelled

- Lights (type 501): the visibility byte and the kill helper are not represented in the trigger
  state.
- The two-player mask, the power-up target, kill mode 0 on a crate and the rail kills were not
  observed in a retail trace; they follow the disassembly.
- The explosion `Exp_HitEnvItem` performs (debris, sounds) is presentation.
