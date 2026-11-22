# Level-object script VM, object lists and message commands (Phase 4.4b)

Status: confirmed for the interpreter (commands, operands, variables, frame update) by a
retail replay; list operations and the object loop confirmed from disassembly and partly
by the same replay (distance formula); node messaging confirmed from disassembly only
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00401520` (`TRG_RunScriptStream`), `0x004014a0` (`TRG_SkipConditionalBlock`),
`0x004015e0` (`TRG_ExecuteBaddyScriptOpcode`), `0x004027c0` (`TRG_ReadScriptOperand`),
`0x004027f0` (`TRG_ApplyScriptFieldWord`), `0x00402bb0` (`TRG_GetScriptVariable`),
`0x00403410` (`TRG_ApplyScriptCommand`), `0x004013b0` (`CBaddy__Die`), `0x004013d0`
(`CBaddy__SendDeathPulse`), `0x004031a0` (frame update, no Ghidra function),
`0x00480ff0` (`Ob_AI`), `0x004801d0`, `0x004801f0`, `0x004802f0`, `0x004803b0`,
`0x004c5b30`, `0x004c5b60`, `0x004c5c00`, `0x004c5c40`, `0x004c5c70`, `0x004c7a00`,
`0x004c7bf0`, `0x004c9620`
Native: `src/trg/object_script.{hpp,cpp}`, `src/trg/world_objects.{hpp,cpp}`,
`src/trg/script_world.{hpp,cpp}`

Type-1 TRG nodes are the level's scripted objects. This page records how the retail game
runs the 0xcb family ("script objects", 402 nodes in the shipped levels), how those objects
live on lists, and how TRG message commands reach them. The bytecode in these streams is a
different language from the command-point opcodes in `trg-command-table.md`.

## Object life cycle

1. A pulse to a type-1/type-7 node calls `TRG_SendPulse` (`0x004c58b0`) which calls
   `TRG_CreateObject` (`0x004c5460`). The 0xcb constructor pushes the new object on the head
   of `BaddyObjectListHead` and stores the node index at `+0xb0`.
2. Unless the node's option list (bytes before the position) contains option `2`, the object
   is immediately suspended (`Object_SuspendToList`). Option `4` clears list flag `0x02`
   (do not suspend when far); a type-7 node sets list flag `0x04`. Objects wait suspended for
   an activate message (`send_activate`, `0x04`).
3. Every frame `Ob_AI` walks each list (see below) and calls the object's update.
4. The update (`0x004031a0`) latches and clears the signal bit, counts down a wait, runs the
   script while it is active and calls `CBaddy__Die` once the script has finished.
5. A dead object gets one frame to show death (list flag `0x80`), then is destroyed.

## Ob_AI (`0x00480ff0`)

Per object, reading the next pointer before anything can move it:

- assert the suspended flag is clear;
- alive (`+0x6a & 0x40` clear): `+0xb8 = Utils_CrapDist(player position, object position)`, or
  the smaller of two when two players are present. If the object is not flagged
  suspend-when-far (`+0x6a & 0x02`) or the distance is `<= 0x32c8` (`DAT_00535af8`),
  run the update hooks (`Animation_Advance` if state bit `0x02` and no movie, then the virtual
  update); otherwise suspend it;
- dead but `0x80` clear: set `0x80`;
- dead and `0x80` set: run the destructor (leaves the baddy list, stops its sound).

`Utils_CrapDist` weights the largest axis by 1, the middle by 1/2 and the smallest by 1/4
(ties resolved by the retail comparison chain) and shifts right by 12. The native
`crap_distance` reproduces the `+0xb8` value retail stored in all 948 replayed updates where
the skater had not just been moved.

## List operations

Intrusive lists: next at `+0x20`, previous at `+0x34`, owner list at `+0x64`. `Remove` does not
clear the removed object's own links and calls `UnSuspend` first when the object is
suspended and the list is not the suspended list. Consequences kept in the native model:
`TRG_SuspendObjectByNode` and `SendUnSuspend` continue iterating through the object's own
next pointer after moving it, so they carry on into the list it just joined.

## Message commands

| Command | Retail | Effect |
| --- | --- | --- |
| send_signal `0x0a` | `TRG_SendSignalToNodes` `0x004c5b60` | for each linked node of type 1 or 7, set `+0x68` bit 0 on the matching object of the baddy list and of the game-object list |
| send_activate `0x04` / send_suspend `0x05` | `TRG_ApplyActivationOrSuspendList` | type 1/7 targets only; suspend walks baddy then game-object lists, activate walks the suspended list |
| send_kill `0x0b` / `0x0c` | `TRG_SendKillFromNode(node, 0/1)` | type 1: platform path, then `KillInList` on both lists (mode 0 = die, mode 1 = hit); types 2, 9, 10, 11, 500, 501 go to enviro-item, rail and light code |
| script `0x42b1` / `0x42b2` | `TRG_SendSignalToNodes` / `TRG_ExecuteCommandList` on `TRG_GetNodeLinks(n)` | signal the linked nodes / pulse each of them |
| script `0x42b3` / `0x42b4` | `SendSignalToNode` on both lists / `TRG_SendPulse(n)` | signal objects at node n / pulse node n |

`TRG_ExecuteCommandList` is simply "`TRG_SendPulse` every id in the list".

## Script language

A command word has bit `0x4000` (command, virtual slot `+0x1c`) or bit `0x2000` with `0x4000`
clear (field write, slot `+0x20`). An operand word with `0x2000` set and `0x8000` clear names a
variable (slot `+0x24`); otherwise it is an immediate. `0x4100` ends the stream and marks the
object inactive. The runner stops for the frame when a command returns 0 (yield).

Field writes (`0x004027f0`): `0x2100` `+0xb4`, `0x2101` `+0xae`, `0x2114` `+0x1bc`, `0x2115`
`+0x1be`, `0x2120` register (index word then operand), `0x2121` signed `+0x108`, `0x2122`
`+0x1ac`, `0x2125` attribute (index word, raw value word), `0x212c`/`0x212d` byte at
`+0x170`/`+0x16f`, `0x212e` skip a word, `0x212f` model checksum lookup (aligned u32),
`0x2131` state bit `0x08`, `0x2135` skip aligned u32, `0x2140..0x2142` position axis (operand
`<< 12`).

Variables (`0x00402bb0`): `0x2100`, `0x2120` register, `0x2129` random, `0x212a` own node,
`0x212b` first link of a node, `0x212c`/`0x212d` bytes, `0x2132` XZ distance to the target
(clamped to `0x1fff`), `0x2133` line of sight, `0x2136` a global word, `0x2140..0x2142`
own position `>> 12`, `0x2150..0x2152` target position `>> 12`. `0x212e` is a removed
variable and asserts.

Commands: control flow `0x4101` goto, `0x4102` goto and yield, `0x4104` label, `0x4105` label
pre-scan, `0x4106` jump to a type-1000 script point, `0x4107` yield; arithmetic `0x4110`
add / `0x4111` subtract (read-modify-write through a scratch copy of the destination's operand
words, so any writable variable works as a destination); conditionals `0x4112` greater,
`0x4113` less, `0x4114` equal, `0x4115` all flag bits set, `0x4116` no flag bits set, each
ending at `0x4120`; `0x4121` set / `0x4122` test level flag, `0x4123` test goal bit;
animation `0x4201` cycle, `0x4202` start; `0x4203`/`0x4204` clear/set state bits, `0x4205`
die, `0x4226` clear velocity, `0x4227` set/clear flag bit 2 with a byte; `0x4240` finish and
yield; `0x4280` wait (operand `<< 12` frame units), `0x4281` wait for signal; sounds `0x4290`,
`0x4291`, `0x4507..0x4509`; `0x4292` particle burst (skipped when 250 effects are live);
`0x4296`/`0x4297` empty on PC; `0x4298` camera shake; `0x429a`/`0x42a6` explosion (the PC
build reads its operands and does nothing); `0x429c` screen flash; `0x429e` camera height;
`0x42a0` power-up; `0x42c0` collect-goal counter; `0x4700` flag `0x100`. The 0xcb subclass adds
`0x4503` no-op, `0x4504` skip 20 bytes, `0x4506` copy target position, `0x429b`/`0x42a1` skip
12 bytes, `0x429f` no-op. Anything else asserts "Unknown script command" and continues.

Retail assertions (`M3dUtils_InBetween`) log and continue; the native interpreter counts them
and continues. No shipped script triggers one.

## Validation

- `opentony_trg_scripts` runs every 0xcb script and script point of all 32 shipped TRG files for
  600 frames against a varying pseudo-world: about 78,000 commands, no assertion, no fault.
- A GDB probe (`build/p44b/script_trace.py`, untracked) recorded 981 updates of the Warehouse
  objects, including a run where the skater was moved next to each object in turn.
  `re/evidence/fixtures/object-script-warehouse.txt` holds them; `scripts/make_script_trace_fixture.py`
  builds it. Each update replays from the retail entry image and must match retail in: command
  sequence with cursors, variable reads with results, direct side-effect calls in order, and
  every modelled object field. 981 of 981 match (1,677 commands, 704 variable reads, 169
  calls, 15 distinct opcodes).

## Not modelled / open

- The rest of the 0x4031a0 update (looping positional sound refresh, attached-model
  transforms) is not in the Phase 4 model; `WorldKit::script_object_update` (10g s16, retail-memory
  form, `world-calls-wruA.txt`) does all of it.
- `SendVisible` and the enviro-item, rail and light kills are hooks only.
- The 0x192 (platform) subclass, its game-object frame loop and its kill path are in
  `platform-object.md` (Phase 4.4c). That work also added `Ob_MaybeUnSuspendOrCull`
  (`0x00481150`), which wakes parked objects flagged suspend-when-far when a player comes within
  `0x32c8`; 4.4b had left it out, and `ScriptWorld::run_frame` now runs it before `Ob_AI`.
- Initial value of `+0x1e8` (sound handle) before the first sound command: retail does not set
  it in the constructor; the native model uses -1.
- Warehouse traces never execute `0x2132/0x2133`, `0x4121..0x4123`, `0x4201/0x4202`, `0x4298`,
  `0x42a0`, `0x4506..0x4509`; those are covered by unit tests and the corpus run only.
- Whether retail assertions are fatal in the shipped build is still unknown.
