"""vx22.py A.heap B.heap: the zone words that differ in the vertex area (0x05000000..), read as 0x1c-byte vertices (x y z rhw colour u v), with both colours"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "s9"))
from zones import region  # noqa: E402

a, b = region(sys.argv[1]), region(sys.argv[2])
base = 0x04f00000
lo = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x05000000
hi = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x05200000
n = min(len(a), len(b), hi - base)
diffs = [i for i in range(lo - base, n, 4) if a[i:i + 4] != b[i:i + 4]]
print(len(diffs), "differing words")
seen = set()
rows = 0
for i in diffs:
    # the colour word of a vertex: the word after rhw; assume the differing word is the colour
    start = i - 0x10
    if start in seen:
        continue
    seen.add(start)
    x, y, z, w, ca, uu, vv = struct.unpack_from("<ffffIff", a, start)
    cb = struct.unpack_from("<I", b, start + 0x10)[0]
    if rows < 60:
        print(f"{base + start:08x} x={x:8.1f} y={y:8.1f} z={z:.5f} uv=({uu:.3f},{vv:.3f}) retail={ca:08x} standalone={cb:08x}")
    rows += 1
print(rows, "vertices")
