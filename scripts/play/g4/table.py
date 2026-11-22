"""Compare the sweeps: per level and fog, the arena high-water mark (bytes), and the swapped / dropped peaks, at 100 (the plain sweep), 200, 400 and max."""

import re
from pathlib import Path

root = Path((__import__("os").path.expanduser("~/THPSource2") + "/build/g4"))
files = {"100": "summary.txt", "200": "summaryd200.txt", "400": "summaryd400.txt", "max": "summarydmax.txt"}
data = {}
for label, name in files.items():
    for line in (root / name).read_text().splitlines():
        tag, rest = line.split(": ", 1)
        tag = re.sub(r"^d(200|400|max)", "", tag)
        arena = re.search(r"arena high (\d+) of (\d+)", rest)
        sw = re.search(r"swapped=(\d+) dropped=(\d+)", rest)
        seen = re.search(r"seen=(\d+)", rest)
        data.setdefault(tag, {})[label] = (int(arena.group(1)), int(arena.group(2)), int(sw.group(1)), int(sw.group(2)), int(seen.group(1)))
print(f"{'run':8} " + " ".join(f"{k:>26}" for k in files))
for tag in sorted(data, key=lambda t: (int(re.match(r'L(\d+)', t).group(1)), t)):
    cells = []
    for k in files:
        a, cap, s, dr, seen = data[tag][k]
        cells.append(f"{a // 1000:4d}K {100 * a // cap:2d}% sw{s:3d} dr{dr:3d} n{seen:3d}")
    print(f"{tag:8} " + " ".join(f"{c:>26}" for c in cells))
