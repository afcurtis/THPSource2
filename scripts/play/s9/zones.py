"""usage: python scripts/play/s9/zones.py A.heap B.heap  -> the game's zone-1 blocks (from 0x04f00030) of two session_N.heap files side by side, to the first difference"""
import struct
import sys
from pathlib import Path


def region(path):
    data = Path(path).read_bytes()
    at = 0
    while at + 8 <= len(data):
        base, size = struct.unpack_from("<2I", data, at)
        if base == 0x04F00000:
            return data[at + 8: at + 8 + size]
        at += 8 + size
    raise SystemExit(f"{path}: no region at 04f00000")


def blocks(zone, start=0x04F00030, end=0x054DC020, limit=4000):
    out = []
    at = start
    while at < end and len(out) < limit:
        hdr = struct.unpack_from("<I", zone, at - 0x04F00000)[0]
        size = hdr >> 4
        out.append((at, hdr & 0xF, size, zone[at - 0x04F00000 + 8: at - 0x04F00000 + 8 + 16]))
        if size == 0:
            break
        at += 8 + size
    return out


a, b = blocks(region(sys.argv[1])), blocks(region(sys.argv[2]))
for k, (x, y) in enumerate(zip(a, b)):
    same = x[:3] == y[:3]
    print(f"{'  ' if same else '!!'} {x[0]:08x} tag {x[1]:x} size {x[2]:7x} {x[3].hex()}   |   {y[0]:08x} tag {y[1]:x} size {y[2]:7x} {y[3].hex()}")
    if not same and k > 3:
        for z in zip(a[k + 1:k + 6], b[k + 1:k + 6]):
            print(f"   {z[0][0]:08x} {z[0][2]:7x} {z[0][3].hex()}   |   {z[1][0]:08x} {z[1][2]:7x} {z[1][3].hex()}")
        break
