"""zd22.py A.heap B.heap [ADDRESS]: the differing word ranges of the region holding ADDRESS (default 0x091e0020, the capture host's zones) in two session_N.heap files"""
import struct
import sys
from pathlib import Path


def region(path, address):
    data = Path(path).read_bytes()
    at = 0
    while at + 8 <= len(data):
        base, size = struct.unpack_from("<2I", data, at)
        if base <= address < base + size:
            return base, data[at + 8: at + 8 + size]
        at += 8 + size
    raise SystemExit(f"{path}: no region holds {address:08x}")


address = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x091E0020
(base_a, a), (base_b, b) = region(sys.argv[1], address), region(sys.argv[2], address)
print(f"regions {base_a:08x}+{len(a):x} / {base_b:08x}+{len(b):x}")
n = min(len(a), len(b)) // 4 * 4
i, shown = 0, 0
while i < n and shown < 60:
    if a[i:i + 4] != b[i:i + 4]:
        j = i
        while j < n and a[j:j + 4] != b[j:j + 4]:
            j += 4
        wa = " ".join(f"{w:08x}" for w in struct.unpack_from(f"<{min((j - i) // 4, 6)}I", a, i))
        wb = " ".join(f"{w:08x}" for w in struct.unpack_from(f"<{min((j - i) // 4, 6)}I", b, i))
        print(f"{base_a + i:08x} +{j - i:x}  a={wa}  b={wb}")
        shown += 1
        i = j
    else:
        i += 4
