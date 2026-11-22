#!/usr/bin/env python3
"""Reduce a retail SendVisible / SendKillFromNode trace (Phase 4.4c probe
build/p44c/enviro_trace.py: TRG_SendVisible 0x004c77f0, TRG_SendKillFromNode
0x004c7a00, Spool_FindEnviroItem 0x004b1ef0) to the distinct crate-word
transitions it observed.

Fixture layout, one record per line:

  # comment
  C <kind> <argument> <calls> <targets by node type: type:count,...>
  T <kind> <argument> <word before> <word after> <occurrences>

`kind` is visible (argument = the command's value) or kill (argument = the mode).
Words are the enviro item's +0x04 status word before and after the call.
"""

import collections
import json
import sys
from pathlib import Path


def main(trace_path, out_path):
    records = [json.loads(line) for line in open(trace_path)]
    calls = {}
    for record in records:
        calls.setdefault(record["call"], []).append(record)
    shapes = collections.Counter()
    transitions = collections.Counter()
    for group in calls.values():
        head = next(r for r in group if r["t"] == "call")
        exit_ = next((r for r in group if r["t"] == "exit"), None)
        if exit_ is None:
            continue
        types = collections.Counter(t[1] for t in head["targets"])
        shapes[(head["kind"], head["arg"], tuple(sorted(types.items())))] += 1
        for item in (r for r in group if r["t"] == "item"):
            if item["ptr"]:
                after = exit_["after"].get(str(item["ptr"]))
                transitions[(head["kind"], head["arg"], item["flags"], after)] += 1
    lines = ["# retail enviro-item word transitions (Phase 4.4c); see scripts/make_enviro_trace_fixture.py"]
    for (kind, arg, types), count in sorted(shapes.items(), key=str):
        lines.append(f"C {kind} {arg} {count} " + ",".join(f"{t}:{n}" for t, n in types))
    for (kind, arg, before, after), count in sorted(transitions.items(), key=str):
        lines.append(f"T {kind} {arg} {before} {after} {count}")
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(f"{len(shapes)} call shapes, {len(transitions)} transitions -> {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: make_enviro_trace_fixture.py TRACE.ndjson OUT.txt")
    main(sys.argv[1], sys.argv[2])
