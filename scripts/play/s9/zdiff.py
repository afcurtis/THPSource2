"""usage: python scripts/play/s9/zdiff.py A.heap B.heap -> differing byte ranges of the zone region (0x04f00000), with a text preview"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from zones import region  # noqa: E402

a, b = region(sys.argv[1]), region(sys.argv[2])
i, n, shown = 0, min(len(a), len(b)), 0
while i < n and shown < 40:
    if a[i] != b[i]:
        j = i
        while j < n and (a[j] != b[j] or a[j + 1:j + 9] != b[j + 1:j + 9]):
            j += 1
        text = lambda d: "".join(chr(c) if 32 <= c < 127 else "." for c in d[i:min(j, i + 48)])
        print(f"{0x04f00000 + i:08x} +{j - i:x}\n   a {a[i:i + 24].hex()} {text(a)}\n   b {b[i:i + 24].hex()} {text(b)}")
        shown += 1
        i = j
    else:
        i += 1
