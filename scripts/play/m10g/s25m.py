import struct, sys
from pathlib import Path
R = "build/sessions/runs/warehouse-idle-standalone"
n = sys.argv[1]
ITEMS = [0x97a4aa8, 0x97a6c34, 0x97a6e94, 0x97a78f8, 0x97a79dc, 0x97a7ac0, 0x97a7cd4]
def region(path, address):
    data = Path(path).read_bytes(); at = 0
    while at + 8 <= len(data):
        base, size = struct.unpack_from("<2I", data, at)
        if base <= address < base + size:
            return base, data[at + 8: at + 8 + size]
        at += 8 + size
    raise SystemExit("no region")
for tag, d in (("retail", R + "-retail"), ("standalone", R)):
    base, heap = region(f"{d}/session_{n}.heap", 0x097a6c34)
    def h(a): return struct.unpack_from("<H", heap, a - base)[0]
    def s(a): return struct.unpack_from("<i", heap, a - base)[0]
    print(tag)
    for item in ITEMS:
        print("  ", hex(item), "flags4", hex(h(item+4)), "gen6", hex(h(item+6)), "pos", [s(item+k)>>12 for k in (8,12,16)], "ang", [hex(h(item+k)) for k in (0x14,0x16,0x18)], "model", hex(h(item+0x1a)))
