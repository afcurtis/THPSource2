"""usage: python scripts/play/s9/arena.py A.heap B.heap [FROM [TO]] -> the zone words 0x04f00000.. side by side (8 words a row), with floats where a word looks like one"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from zones import region  # noqa: E402

a, b = region(sys.argv[1]), region(sys.argv[2])
start = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x04f00000
end = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x04f00500


def show(word):
    f = struct.unpack("<f", struct.pack("<I", word))[0]
    if 0x3a000000 <= word <= 0x46000000 and abs(f) >= 0.01:
        return f"{f:8g}"
    return f"{word:08x}"


for at in range(start, end, 32):
    o = at - 0x04f00000
    wa = struct.unpack_from("<8I", a, o)
    wb = struct.unpack_from("<8I", b, o)
    mark = " " if wa == wb else "*"
    print(f"{at:08x}{mark} a " + " ".join(show(w) for w in wa))
    if wa != wb:
        print(f"          b " + " ".join(show(w) for w in wb))
