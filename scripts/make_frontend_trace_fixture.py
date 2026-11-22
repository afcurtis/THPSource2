#!/usr/bin/env python3
"""Turn the `case` records of re/gdb/probes/frontend_trace.py (a natural frontend run) into a replay fixture.

usage: make_frontend_trace_fixture.py IN.ndjson OUT.txt [--limit N] [--title TEXT]

The output is the forced-call fixture format of scripts/make_career_calls_fixture.py (see src/runtime/retail_calls_replay.hpp),
so src/runtime/frontend_retail_calls_test.cpp replays it with no new test code: one `R` line per object the cases touch (zeroed,
except the read-only empty string), and per case the entry image as pokes, the bytes the retail function changed, and the ordered
calls it made through virtual methods or the engine.  --limit keeps at most N cases per (function, calls shape), evenly spread.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


def curated_names() -> dict[int, str]:
    import yaml

    path = Path(__file__).resolve().parents[1] / "re/symbols/functions.yml"
    return {entry["address"]: entry["name"] for entry in yaml.safe_load(path.read_text())["functions"]}


def call_token(call: dict) -> str:
    args = [call["this"], *call["args"]]
    token = ",".join([call["name"], *(f"{a & 0xFFFFFFFF:x}" for a in args)])
    if call.get("text"):
        token += "@" + call["text"].encode("latin-1", "replace").hex()
    return token


def diff_runs(before: bytes, after: bytes, base: int) -> list[str]:
    runs = []
    i = 0
    while i < len(before):
        if before[i] != after[i]:
            j = i
            while j < len(before) and before[j] != after[j]:
                j += 1
            runs.append(f"{base + i:08x}={after[i:j].hex()}")
            i = j
        else:
            i += 1
    return runs


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    limit = None
    title = "Natural frontend frames (Phase 4.10a)"
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])
        args = [a for a in args if a != str(limit)]
    if "--title" in sys.argv:
        title = sys.argv[sys.argv.index("--title") + 1]
        args = [a for a in args if a != title]
    src, dst = args[0], args[1]
    names = curated_names()
    consts, cases = [], []
    for line in open(src):
        record = json.loads(line)
        if record["t"] == "const":
            consts.append(record)
        elif record["t"] == "case" and "out" in record:
            cases.append(record)
    if limit is not None:
        groups: dict[tuple, list] = {}
        for case in cases:
            shape = (case["fn"], tuple(c["name"] for c in case["calls"]) if len(case["calls"]) < 3 else ("many", len(case["calls"]) > 8))
            groups.setdefault(shape, []).append(case)
        cases = []
        for group in groups.values():
            step = max(1, len(group) // limit)
            cases.extend(group[::step][:limit])
        cases.sort(key=lambda c: c["tick"])
    # the regions: every object image, every string, every constant (merged when they overlap)
    spans = []
    for case in cases:
        spans.append((case["this"], len(bytes.fromhex(case["in"]))))
        if case.get("str_ptr"):
            spans.append((case["str_ptr"], 0x80))
    for const in consts:
        spans.append((const["address"], len(bytes.fromhex(const["bytes"]))))
    spans.sort()
    merged: list[list[int]] = []
    for address, size in spans:
        if merged and address <= merged[-1][0] + merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], address + size - merged[-1][0])
        else:
            merged.append([address, size])
    lines = [f"# {title}"]
    for address, size in merged:
        data = bytearray(size)
        for const in consts:
            raw = bytes.fromhex(const["bytes"])
            if address <= const["address"] < address + size:
                data[const["address"] - address:const["address"] - address + len(raw)] = raw
        lines.append(f"R {address:08x} {bytes(data).hex()}")
    counts: dict[str, int] = {}
    for case in cases:
        before, after = bytes.fromhex(case["in"]), bytes.fromhex(case["out"])
        pokes = [f"{case['this']:08x}={case['in']}"]
        if case.get("str_ptr"):
            text = (case.get("str") or "").encode("latin-1") + b"\0"
            pokes.append(f"{case['str_ptr']:08x}={text.hex()}")
        name = names.get(case["fn"], case["name"])
        lines.append(
            f"C name={name} fn={case['fn']:x} this={case['this']:x} args={','.join(f'{a:x}' for a in case['args'])} "
            f"pokes={';'.join(pokes)} ret=0 writes={';'.join(diff_runs(before, after, case['this']))} "
            f"calls={';'.join(call_token(c) for c in case['calls'])} values="
        )
        counts[name] = counts.get(name, 0) + 1
    Path(dst).write_text("\n".join(lines) + "\n", newline="\n")
    print(f"{sum(counts.values())} cases -> {dst}")
    for name, n in sorted(counts.items()):
        print(f"  {name}: {n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
