#!/usr/bin/env python3
"""Turn the ndjson of build/m10e/script_size_trace.py into the fixture of src/runtime/trick_script_size_trace_test.cpp.

Retail's TrickScript_CommandSize (0x004bf6c0) and TrickScript_ReadWord (0x004be3c0), every distinct call of a level load
(the walk of the player tables' scripts makes tens of thousands; the traced facts are the bytes at the command and the answer):

  # comment
  S <hex> <delta>          TrickScript_CommandSize: the 0x40 bytes at the command (fewer at the end of readable memory),
                           and the returned address minus the command
  W <hex> <ax> <advance>   TrickScript_ReadWord: the four bytes at the stream pointer, the word returned (AX), and how far
                           the pointer moved

usage: make_trick_script_fixture.py OUT.txt TRACE.ndjson [TRACE.ndjson ...]
"""

import json
import sys
from pathlib import Path


PER_GROUP = 8


def main(out_path, trace_paths):
    rows = set()
    counts = {}
    for path in trace_paths:
        for line in open(path):
            if not line.startswith("{"):
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue  # a truncated last line
            if row.get("f") == "size":
                rows.add(("S", row["window"], f"{row['delta']:x}"))
            elif row.get("f") == "word":
                rows.add(("W", row["window"], f"{row['eax']:x}", f"{row['advance']:x}"))
            elif row.get("t") == "stop":
                for name, n in row["counts"].items():
                    counts[name] = counts.get(name, 0) + n
    # the bytes behind the opcode differ in nearly every call, so keep a few windows of each (function, opcode, answer)
    kept, groups = [], {}
    for row in sorted(rows):
        key = (row[0], row[1][:2], row[2] if row[0] == "S" else "")
        groups[key] = groups.get(key, 0) + 1
        if groups[key] <= PER_GROUP:
            kept.append(row)
    opcodes = sorted({row[1][:2] for row in kept if row[0] == "S"})
    lines = ["# retail TrickScript_CommandSize / TrickScript_ReadWord, distinct calls; see scripts/make_trick_script_fixture.py",
             f"# opcodes: {' '.join(opcodes)}"]
    lines += [" ".join(row) for row in kept]
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(out_path, len(kept), "of", len(rows), "distinct calls;", counts, "opcodes", len(opcodes))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2:])
