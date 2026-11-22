#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10e"  # outputs stay in build/ (scripts/play/README.md)
# dis2.sh ADDR COUNT: like m10/bytes.sh but reads across module-file boundaries
cd ~/THPSource2
python3 - "$1" "$2" <<'EOF'
import sys, pathlib, subprocess
addr, count = int(sys.argv[1], 16), int(sys.argv[2], 16 if sys.argv[2].startswith("0x") else 10)
mods = sorted((int(p.stem.split("_")[1], 16), p) for p in pathlib.Path("match/original/modules").glob("text_*.bin"))
out = bytearray()
a = addr
while len(out) < count:
    for base, p in mods:
        sz = p.stat().st_size
        if base <= a < base + sz:
            chunk = p.read_bytes()[a - base: a - base + count - len(out)]
            out += chunk
            a += len(chunk)
            break
    else:
        break
pathlib.Path("build/m10e/dis2.bin").write_bytes(bytes(out))
subprocess.run(["objdump", "-D", "-b", "binary", "-mi386", "-M", "intel", f"--adjust-vma={addr:#x}", "build/m10e/dis2.bin"])
EOF
