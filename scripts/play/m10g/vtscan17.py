import pathlib, re, struct, sys
root = pathlib.Path(__import__("os").path.expanduser("~/THPSource2"))
natives = set()
for f in ("re/config/standalone_natives.yml", "re/config/standalone_natives_manual.yml"):
    natives |= {int(a, 16) for a in re.findall(r"address: (0x[0-9a-f]+)", (root / f).read_text())}
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
mods = sorted((int(p.stem.split("_")[1], 16), p) for p in (root / "match/original/modules").glob("*.bin"))
def read(addr, n):
    for base, p in mods:
        d = p.read_bytes()
        if base <= addr < base + len(d):
            return d[addr - base: addr - base + n]
    return b""
data = read(lo, hi - lo)
seen = {}
for i in range(0, len(data) - 3, 4):
    v = struct.unpack_from("<I", data, i)[0]
    if 0x401000 <= v < 0x4f5000 and v not in natives:
        seen.setdefault(v, []).append(lo + i)
for v in sorted(seen):
    print("%08x slots %s first bytes %s" % (v, ",".join("%x" % a for a in seen[v][:4]), read(v, 12).hex()))
