"""realdiff.py A B [--max N]: ddiff's ranges minus the ones equal after normalisation and the masked ones (tony.standalone_boot.masks)"""
import re, subprocess, sys
sys.path.insert(0, ".")
from tony import standalone_boot as sb
masks = [(int(m.split(":")[0], 16), int(m.split(":")[1], 16)) for m in sb.masks({}).split(",")]
out = subprocess.run([".tools/venv/bin/python", "scripts/play/s9/ddiff.py", sys.argv[1], sys.argv[2], "--max", "100000", "--zones", "auto"], capture_output=True, text=True).stdout
shown = 0
for line in out.splitlines():
    m = re.match(r"\s+([0-9a-f]{8}) \+([0-9a-f]+)\s+(\S+)\s+a=(.*?)  b=(.*)$", line)
    if not m:
        continue
    addr, size = int(m.group(1), 16), int(m.group(2), 16)
    if m.group(4) == m.group(5):
        continue
    if any(addr >= b and addr + size <= b + n for b, n in masks):
        continue
    print(line)
    shown += 1
print(shown, "unmasked differing ranges")
