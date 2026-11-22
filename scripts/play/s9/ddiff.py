"""usage: python scripts/play/s9/ddiff.py A B [--max N] [--from ADDR] [--to ADDR] [--zones A_LOW,A_HIGH,B_LOW,B_HIGH]

A and B are .data images at 0x00524000 (a session_N.img, a standalone dump) or `static` (the file image: .data's initialised bytes, then zeros).
Prints the differing word ranges with the nearest named global. --zones compares a word that points into each image's zones as an offset (pointer normalisation);
`auto` reads the zone bounds at 0x0056a97c from each image.
"""
import argparse
import bisect
import struct
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[3]
BASE = 0x00524000
SIZE = 0x024B7E3C


def load(spec):
    if spec != "static":
        return Path(spec).read_bytes()
    img = (ROOT / "build/static/thawk2_static.img").read_bytes()
    count = struct.unpack_from("<I", img, 20)[0]
    out = bytearray(SIZE)
    at = 24 + 8 * count
    for i in range(count):
        va, size = struct.unpack_from("<2I", img, 24 + 8 * i)
        if BASE <= va < BASE + SIZE:
            out[va - BASE: va - BASE + size] = img[at: at + size]
        at += size
    return bytes(out)


def names():
    g = []
    for x in yaml.safe_load((ROOT / "re/symbols/globals.yml").read_text())["globals"]:
        a = x["address"]
        g.append((int(a, 16) if isinstance(a, str) else int(a), x["name"]))
    g.sort()
    return g


def zones(img):
    w = struct.unpack_from("<4I", img, 0x0056A97C - BASE)
    low, high = min(w[0], w[2]), max(w[1], w[3])
    return (low, high) if low and high > low else (0, 0)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--max", type=int, default=80)
    p.add_argument("--from", dest="lo", default="0")
    p.add_argument("--to", dest="hi", default="ffffffff")
    p.add_argument("--zones", default="")
    args = p.parse_args()
    a, b = load(args.a), load(args.b)
    n = min(len(a), len(b)) // 4
    wa = struct.unpack(f"<{n}I", a[: 4 * n])
    wb = struct.unpack(f"<{n}I", b[: 4 * n])
    za = zb = (0, 0)
    if args.zones == "auto":
        za, zb = zones(a), zones(b)
    elif args.zones:
        v = [int(x, 16) for x in args.zones.split(",")]
        za, zb = (v[0], v[1]), (v[2], v[3])

    def norm(w, z):
        return (w - z[0] + 0x5A5A0000) & 0xFFFFFFFF if z[0] <= w < z[1] else w

    lo, hi = int(args.lo, 16), int(args.hi, 16)
    g = names()
    keys = [x[0] for x in g]
    ranges = []
    i = 0
    while i < n:
        addr = BASE + 4 * i
        if lo <= addr < hi and norm(wa[i], za) != norm(wb[i], zb):
            j = i
            while j < n and norm(wa[j], za) != norm(wb[j], zb):
                j += 1
            ranges.append((i, j))
            i = j
        else:
            i += 1
    total = sum(j - i for i, j in ranges)
    print(f"{len(ranges)} ranges, {total} words differ")
    for i, j in ranges[: args.max]:
        addr = BASE + 4 * i
        k = bisect.bisect_right(keys, addr) - 1
        sym = f"{g[k][1]}+{addr - g[k][0]:#x}" if k >= 0 else "?"
        show = min(j - i, 4)
        print(f"  {addr:08x} +{4 * (j - i):<6x} {sym:<40} a={' '.join(f'{w:08x}' for w in wa[i:i + show])}  b={' '.join(f'{w:08x}' for w in wb[i:i + show])}")


if __name__ == "__main__":
    main()
