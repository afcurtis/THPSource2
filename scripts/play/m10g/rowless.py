"""The functions a retail Hangar frame runs (from the first Game_GameplayUpdate on) that the standalone cannot call: in scope, no standalone row and no hook row, whatever their status."""
import re
import sys
from pathlib import Path

ROOT = Path(__import__("os").path.expanduser("~/THPSource2"))
sys.path.insert(0, str(ROOT / "scripts"))
import phase5_progress as p  # noqa: E402

hits = []
for line in (ROOT / "re/evidence/fixtures/hangar-load-coverage.txt").read_text().splitlines():
    if line and not line.startswith("#"):
        seq, phase, address = line.split()
        hits.append((int(seq), int(phase), int(address, 16)))
functions = (ROOT / "re/native/functions.yml").read_text()
status = {}
for m in re.finditer(r"- address: 0x([0-9a-f]+)\n    name: (\S+)\n(?:    .*\n)*?    status: (\S+)", functions):
    status[int(m.group(1), 16)] = (m.group(2), m.group(3))
rows = set()
for name in ("standalone_natives.yml", "standalone_natives_manual.yml"):
    rows |= p.addresses((ROOT / "re/config" / name).read_text(), r"address: 0x([0-9a-f]+)")
hooks = p.addresses((ROOT / "re/config/native_hooks.yml").read_text(), r"(?:canonical|address): 0x([0-9a-f]+)")
seams = p.addresses((ROOT / "re/symbols/seams.yml").read_text(), r"address: 0x([0-9a-f]+)")
scope = (ROOT / "re/symbols/scope.yml").read_text()
ranges = [(int(a, 16), int(b, 16)) for a, b in re.findall(r"first: 0x([0-9a-f]+), last: 0x([0-9a-f]+)", scope)]
scoped_out = p.addresses(scope, r"- \{address: 0x([0-9a-f]+), category")
gameplay = next(seq for seq, _, a in hits if status.get(a, ("", ""))[0] == "Game_GameplayUpdate")
by_status = {}
for seq, _, a in hits:
    if seq < gameplay or a in rows or a in hooks or a in seams or a in scoped_out or any(lo <= a <= hi for lo, hi in ranges):
        continue
    name, st = status.get(a, ("?", "none"))
    by_status.setdefault(st, []).append(f"{name} {a:#x}")
total = sum(len(v) for v in by_status.values())
print(f"{total} functions of the first play frames have no row")
for st, names in sorted(by_status.items()):
    print(f"  {st} ({len(names)}): " + ", ".join(names))
