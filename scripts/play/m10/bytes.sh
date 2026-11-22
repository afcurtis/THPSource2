#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# bytes.sh ADDR COUNT: disassemble COUNT bytes of the retail .text at ADDR (from the module bins, the one that holds ADDR)
cd ~/THPSource2
python3 - "$1" "$2" <<'EOF'
import sys, pathlib, subprocess
addr, count = int(sys.argv[1], 16), int(sys.argv[2], 16 if sys.argv[2].startswith("0x") else 10)
best = None
for p in pathlib.Path("match/original/modules").glob("text_*.bin"):
    base = int(p.stem.split("_")[1], 16)
    if base <= addr < base + p.stat().st_size:
        best = (base, p)
base, p = best
data = p.read_bytes()[addr - base: addr - base + count]
pathlib.Path("build/m10/bytes.bin").write_bytes(data)
print(f"{p} base {base:#x}, {len(data)} bytes")
subprocess.run(["objdump", "-D", "-b", "binary", "-mi386", "-M", "intel", f"--adjust-vma={addr:#x}", "build/m10/bytes.bin"])
EOF
