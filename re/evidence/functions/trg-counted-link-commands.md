# TRG link commands: operand-free, targets come from the source node

Status: confirmed (corrects an earlier "inline counted node list" reading)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004c5dc0`, `0x004c5c70`, `0x004c5b60`, `0x004c7c50`, `0x004c8550`

## Observation

Commands `0x0003`, `0x0004`, `0x0005`, `0x000a`, `0x000b` and `0x000c` have
**no operand in the command stream**. Only `0x000d` consumes one following
`u16`. Each of these commands acts on the *source node's serialized link list*,
the `u16 count` + `u16 node[count]` block that precedes the checksum in a type-6
node. The dispatcher at `0x004c5dc0` obtains that list from
`TRG_GetNodeLinks` (`0x004c8550`) and hands the resulting pointer to the helper.

- `0x004c5b60` (signal helper, command `0x000a`) receives the link-list pointer,
  reads the `u16` count, and asserts (`M3dUtils_InBetween(count != 0, msg)`)
  that it is non-zero. For each index it looks up the node; only node types 1
  (baddy) and 7 are forwarded, to `0x004c5b30` once for the traffic list
  (`0x0055f6bc`) and once for the baddy list (`0x0056af40`). Other node types
  are skipped silently.
- `0x004c5c70` (activate/suspend helper, commands `0x0004`/`0x0005`) takes the
  opcode as its second argument, asserts it is 4 or 5, and iterates the same
  link list with the same type 1/7 filter.
  - Command **5 = suspend**: calls `0x004c5c00(list, node)` on the traffic
    list `0x0055f6bc` and the baddy list `0x0056af40`; that walks the list and
    calls `0x004802f0` (`CBody::Suspend`), which asserts the object is not
    already suspended, moves it onto the suspended list `0x0056af44` and sets
    flag bit 0 of `+0x6a`.
  - Command **4 = unsuspend/activate**: calls `0x004c5c40(node)`, which walks
    the suspended list `0x0056af44` and calls `0x004803b0`
    (`CBody::UnSuspend`), which moves a suspended object back and clears the
    flag. The earlier native code and Python names had 4 and 5 the wrong way
    round; thps2-tools (`SendActivate`/`SendSuspend`) and the PSX demo symbol
    map agree with the disassembly.
- The skip walker at `0x004c7c50`, used when a non-matching `0x0094` block is
  skipped, advances by 2 bytes for `0x0003/4/5/a/b/c` and by 4 for `0x000d`.
  This independently fixes the stream width: there is no inline list to skip.
- The only other caller of the signal helper is the baddy-script interpreter at
  `0x0040243c`, which passes a link list it looked up itself
  (`0x004c8550` result).

### Empty link lists

The count check is an assert (`M3dUtils_InBetween`, message string at
`0x00547a8c`); the loop after it is a countdown that exits immediately for a
zero count (`0x004c5bb1` `jle`), so if the assert is not fatal nothing is
signalled. Native reconstruction delivers an empty target list to the service
and performs no action. Whether the retail assert aborts is not established
here.

## History: why the earlier reading was wrong

The first reconstruction claimed `0x0004/0x0005/0x000a` read an inline
`u16 count` + node list from the stream, and that `0x0003` "executes the
source node's own counted list". That miscounted the stream width for every
shipped script using these opcodes: 13 of the 32 shipped `.TRG` files failed to
decode or dispatch natively, and the Python decoder in `tony/assets.py`
mis-tokenised the same streams. After correction all 32 files dispatch to the
`0xffff` terminator and agree with the independent `thps2-tools` disassembler
(see `trg-command-table.md`).

## Native reconstruction

`TriggerRuntime::dispatch()` reads no operand for `0x0004/0x0005/0x000a`; it
takes `file_.links(source_node)` and passes it to `on_suspend_activate()` or
`on_signal()`. `skip_operands()` treats those opcodes as one-word commands.
`0x000b/0x000c` (kill) also use the source links; `0x000d` reads one `u16`.

Regression coverage:

- `test_link_commands_take_targets_from_source_node_links` (links `{1,2}`
  delivered unchanged to suspend, activate and signal).
- `test_link_command_conditional_skip_is_operand_free` (bare opcodes inside a
  skipped `0x0094` block, plus a link-less source delivering an empty list).
- `test_link_commands_update_serialized_link_targets` (state effects).
- `tests/test_assets.py::test_decode_trg_link_commands_are_operand_free`.

## Open questions

The higher-level gameplay meaning of the target object state after
suspend/activate remains outside this slice; width, source ownership, target
filter and dispatch order are fixed by the helper control flow.
