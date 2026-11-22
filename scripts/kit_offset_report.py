#!/usr/bin/env python3
"""Per-Kit worklist for 11c-11d (docs/PORT64_AUDIT.md section 4): how many raw retail-memory accesses and distinct literal offsets each group of
src/runtime sources has. A group is the file-name prefix (skater_*, world_*, ...). Output goes to build/guest/kit_offsets.md; it is a worklist, not a gate.

    python scripts/kit_offset_report.py
"""

from __future__ import annotations

import collections
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ACCESS = re.compile(r"\.(?:u8|s8|u16|s16|u32|s32|set8|set16|set32)\(")
OFFSET = re.compile(r"\+\s*(0x[0-9a-fA-F]+)\b")
OUTPUT = ROOT / "build/guest/kit_offsets.md"


def main() -> int:
    groups: dict[str, dict] = collections.defaultdict(lambda: {"files": 0, "accesses": 0, "offsets": set()})
    for path in sorted((ROOT / "src/runtime").glob("*.cpp")):
        if path.name.endswith("_test.cpp"):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        group = groups[path.stem.split("_")[0]]
        group["files"] += 1
        group["accesses"] += len(ACCESS.findall(text))
        group["offsets"].update(OFFSET.findall(text))
    ordered = sorted(groups.items(), key=lambda item: -item[1]["accesses"])
    lines = ["| Group | Files | Raw accesses | Distinct `+ 0x..` offsets |", "|---|---|---|---|"]
    for name, group in ordered:
        lines.append(f"| {name} | {group['files']} | {group['accesses']} | {len(group['offsets'])} |")
    total = sum(group["accesses"] for group in groups.values())
    lines.append(f"| **total** | {sum(group['files'] for group in groups.values())} | {total} | |")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:14]))
    print(f"... written to {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
