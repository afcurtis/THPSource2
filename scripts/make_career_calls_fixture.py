#!/usr/bin/env python3
"""Turn the NDJSON of re/gdb/probes/career_calls.py into a replay fixture.

usage: make_career_calls_fixture.py IN.ndjson OUT.txt [--title TEXT] [--curated]

With --curated each case carries the curated name of its function (re/symbols/functions.yml) instead of
the probe's spec name, so a fixture reads the same as the slice manifests (Phase 4.9 uses this).

One `R base hex` line per retail memory region, then one `C ...` line per forced call:

  C name=<function> fn=<hex> this=<hex|-> args=<hex,...> pokes=<addr=hex;...> ret=<hex|->
    writes=<addr=hex;...> calls=<name,arg,...[@texthex];...> values=<name=v,v;...>

`pokes` put the game in the case's state, `ret` and `writes` are what the retail function did, `calls`
the outside services it reached in order (arguments in hex, message text after `@`), `values` what its
stubbed services returned. src/runtime/career_retail_calls_test.cpp replays the file.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


def call_token(call: dict) -> str:
    token = ",".join([call["name"], *(f"{a & 0xFFFFFFFF:x}" for a in call.get("args", []))])
    if call.get("text"):
        token += "@" + call["text"].encode("latin-1", "replace").hex()
    return token


def curated_names() -> dict[int, str]:
    import yaml

    path = Path(__file__).resolve().parents[1] / "re/symbols/functions.yml"
    return {entry["address"]: entry["name"] for entry in yaml.safe_load(path.read_text())["functions"]}


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    title = "Retail forced calls (Phase 4.8)"
    if "--title" in sys.argv:
        title = sys.argv[sys.argv.index("--title") + 1]
        args = [a for a in args if a != title]
    names = curated_names() if "--curated" in sys.argv else {}
    src, dst = args[0], args[1]
    lines = [f"# {title}"]
    skipped = 0
    counts: dict[str, int] = {}
    with open(src) as fh:
        for raw in fh:
            rec = json.loads(raw)
            if rec["t"] == "base":
                for base, hexdata in rec["regions"]:
                    lines.append(f"R {base} {hexdata}")
            elif rec["t"] == "case":
                if "error" in rec or rec["ret"] is None:
                    skipped += 1
                    continue
                rec["name"] = names.get(rec["fn"], rec["name"])
                pokes = ";".join(f"{a}={h}" for a, h in sorted(rec["pokes"].items()))
                writes = ";".join(f"{a}={h}" for a, h in rec["writes"])
                calls = ";".join(call_token(c) for c in rec["calls"])
                values = ";".join(f"{k}=" + ",".join(f"{v:x}" for v in vs) for k, vs in sorted(rec["values"].items()))
                this = "-" if rec["this"] is None else f"{rec['this']:x}"
                lines.append(
                    f"C name={rec['name']} fn={rec['fn']:x} this={this} args={','.join(f'{a:x}' for a in rec['args'])} "
                    f"pokes={pokes} ret={rec['ret']:x} writes={writes} calls={calls} values={values}"
                )
                counts[rec["name"]] = counts.get(rec["name"], 0) + 1
    with open(dst, "w", newline="\n") as out:
        out.write("\n".join(lines) + "\n")
    print(f"{sum(counts.values())} cases, {skipped} skipped -> {dst}")
    for name, n in sorted(counts.items()):
        print(f"  {name}: {n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
