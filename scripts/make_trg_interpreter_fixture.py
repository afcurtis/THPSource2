#!/usr/bin/env python3
"""Turn a retail trace of TRG_InterpretCommandStream 0x004c5dc0 (Milestone 10b probe,
build/m10/trg_trace.py) into the compact text fixture consumed by
src/trg/trg_interpreter_retail_trace_test.cpp.

Fixture layout, one record per line, in call-start order (nested calls follow their caller):

  # comment
  C <n> <depth> <index> <mode> <stream> <caller>
                          one call of the interpreter: the retail sequence number, the nesting depth
                          (0 = called from outside the interpreter's own callees), the source node index
                          (or 0xffff), the mode argument, the stream address and the return address
  S <hex>                 the stream bytes (a 0x600-byte window, shorter at the end of readable memory)
  R <address> <hex>       a tracked region at entry (fixed globals, the camera and skater fields)
  D <address> <hex>       static data from the executable's file image, not traced (with --static-data: the gap
                          definition table ActiveGapDefinitionTable points at, for a call whose opcode 0xc9 went on to
                          scan it, i.e. called Video_Playing from 0x004c6539); retail's copy may differ where the game
                          wrote it since the start
  c <callee> <site> <ecx> <edx> <arg0> .. <arg5> [| <hex> ..]
                          the interpreter's body called <callee> (address) from <site>; after the bar,
                          the bytes behind pointer arguments (RuntimeObject_ApplyBoundsToLists and
                          _UpdateVisibilityBounds: the two 12-byte boxes; TRG_CreateScriptObject: the
                          6-byte parameter block)
  r <callee> <eax> <edx> [<hex>]
                          that call returned; the bytes behind a returned pointer for
                          TRG_FindCommandPointLink (0x20), TRG_GetNodeLinks (0x40),
                          TRG_DecodeNodePosition (0x0c) and TrickConfig_FindByCharacter (0x100)
  w <address>:<old>:<new> ...
                          the interpreter's own writes since the previous event (words, or single
                          bytes at a region tail), tracked regions only
  k <address>:<old>:<new> ...
                          what the previous callee wrote into the tracked regions, nested calls
                          included (replayed by a harness that stubs the callee)
  v <hex>                 (TRG_ParseNodes trace, 10e) the 12 bytes TRG_DecodeNodePosition wrote through its first
                          argument, right after its r line
  x <eax>                 the exit (the last own writes come before it as a w line)
  Z                       end of the call

A callee entry with no matching return was skipped by the debugging harness (PSX_DrainSpooler). The
generator checks that every call's events nest properly and that every 'w' old value equals the last
value seen for that address in the same call.
"""

import json
import sys
from pathlib import Path


def fmt_changes(changes):
    return " ".join(f"{a:x}:{old}:{new}" for a, old, new in changes)


GAP_TABLE_POINTER = 0x0056DD58
GAP_SCAN_SITE = 0x004C6539          # the return address of 0xc9's Video_Playing call
GAP_RECORD = 0x2C


def static_reader(directory):
    """read(address, size) from the executable's section images (match/original/modules)."""
    images = []
    for path in Path(directory).glob("*.bin"):
        base = int(path.stem.split("_")[1], 16)
        images.append((base, path.read_bytes()))

    def read(address, size):
        out = b""
        while size:
            for base, data in images:
                if base <= address < base + len(data):
                    part = data[address - base:address - base + size]
                    out, address, size = out + part, address + len(part), size - len(part)
                    break
            else:
                raise SystemExit(f"static data at {address:#x} is in no section image of {directory}")
        return out
    return read


def gap_table(read, entry):
    pointer = entry.get(f"{GAP_TABLE_POINTER:08x}")
    if pointer is None:
        raise SystemExit("a gap scan without the gap table pointer in the entry state")
    base = int.from_bytes(bytes.fromhex(pointer)[:4], "little")
    size = 0
    while int.from_bytes(read(base + size + 4, 2), "little") != 0xFFFF:
        size += GAP_RECORD
    return base, read(base, size + GAP_RECORD)


def main(trace_paths, out_path, limit=None, static_data=None):
    read = static_reader(static_data) if static_data else None
    calls = []
    for path in trace_paths:
        for line in open(path):
            if not line.startswith("{"):
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue  # a truncated last line
            if row["t"] == "call":
                calls.append((path, row))
    calls.sort(key=lambda item: (item[0], item[1]["n"]))
    lines = ["# retail TRG_InterpretCommandStream trace (Milestone 10b); see scripts/make_trg_interpreter_fixture.py"]
    counts = {"calls": 0, "events": 0, "callees": 0, "writes": 0}
    seen = set()
    for path, row in calls:
        if limit and counts["calls"] >= limit:
            break
        counts["calls"] += 1
        lines.append(
            f"C {row['n']} {row['depth']} {row['index']:x} {row['mode']:x} {row['stream']:x} {row['caller']:x}")
        lines.append(f"S {row['stream_bytes']}")
        last = {}
        for addr, data in sorted(row["entry"].items(), key=lambda kv: int(kv[0], 16)):
            lines.append(f"R {addr} {data}")
            base = int(addr, 16)
            raw = bytes.fromhex(data)
            for off in range(0, len(raw) - len(raw) % 4, 4):
                last[base + off] = raw[off:off + 4].hex()
            for off in range(len(raw) - len(raw) % 4, len(raw)):
                last[base + off] = raw[off:off + 1].hex()
        if read and any(ev["e"] == "c" and ev["site"] == GAP_SCAN_SITE for ev in row["events"]):
            base, raw = gap_table(read, row["entry"])
            lines.append(f"D {base:08x} {raw.hex()}")
        open_calls = []
        for ev in row["events"]:
            kind = ev["e"]
            counts["events"] += 1
            if kind == "c":
                counts["callees"] += 1
                args = " ".join(f"{a:x}" for a in ev["args"])
                tail = (" |" + "".join(f" {p or '-'}" for p in ev["pointees"])) if ev.get("pointees") else ""
                lines.append(f"c {ev['a']:x} {ev['site']:x} {ev['ecx']:x} {ev['edx']:x} {args}{tail}")
                open_calls.append(ev["a"])
            elif kind == "r":
                # drop skipped entries (no return seen) that precede this return
                while open_calls and open_calls[-1] != ev["a"]:
                    open_calls.pop()
                assert open_calls, (row["n"], hex(ev["a"]))
                open_calls.pop()
                tail = f" {ev['data']}" if ev.get("data") else ""
                lines.append(f"r {ev['a']:x} {ev['eax']:x} {ev['edx']:x}{tail}")
            elif kind == "v":
                lines.append(f"v {ev['data'] or '-'}")
            elif kind in ("w", "x", "cw"):
                changes = ev.get("w") or []
                for addr, old, new in changes:
                    if addr in last and last[addr] != old:
                        raise SystemExit(f"call {row['n']}: write at {addr:x} saw {old}, last value {last[addr]}")
                    last[addr] = new
                    counts["writes"] += kind != "cw"
                if kind == "x":
                    if changes:
                        lines.append("w " + fmt_changes(changes))
                    lines.append(f"x {ev['eax']:x}")
                elif changes:
                    lines.append(("w " if kind == "w" else "k ") + fmt_changes(changes))
            else:
                raise SystemExit(f"unknown event {kind}")
        lines.append("Z")
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(out_path, counts)


if __name__ == "__main__":
    args = sys.argv[1:]
    static = None
    if len(args) > 1 and args[0] == "--static-data":
        static, args = args[1], args[2:]
    if len(args) < 2:
        raise SystemExit("usage: make_trg_interpreter_fixture.py [--static-data DIR] OUT.txt TRACE.ndjson [TRACE.ndjson ...]")
    main(args[1:], args[0], static_data=static)
