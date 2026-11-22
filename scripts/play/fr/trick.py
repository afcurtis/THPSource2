"""F1.5B: a scripted Warehouse run with tricks, at 30 and 60, logged per delivery with extra skater words.
python scripts/play/fr/trick.py RATE NAME EVENTS [FRAMES_TICKS]
EVENTS: "buttons:start_tick:len_tick;..." in game ticks (1/60 s) from the scenario start, buttons joined by + (up, cross, square, left ...).
At 30 fps a frame is 2 ticks (events are rounded down to even ticks); at 60 it is 1.
Output: build/fr/trk_NAME_RATE.txt, one row per delivery: delivery sim x y z state vy ay then the words."""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/fr"), exist_ok=True)  # outputs stay in build/
import os, re, sys
from pathlib import Path
sys.path.insert(0, _os.path.expanduser("~/THPSource2"))
from tony import standalone_boot as sb
from tony import standalone_warehouse as sw
from tony import capture
from tony.levels import LEVEL_NAMES

rate, name, events = sys.argv[1], sys.argv[2], sys.argv[3]
total = int(sys.argv[4]) if len(sys.argv) > 4 else 400
k = 2 if rate == "30" else 1                      # ticks per frame
shift = int(sys.argv[5]) if len(sys.argv) > 5 else 0   # every event this many ticks later (a phase test); the output name gets it
if shift:
    name = f"{name}s{shift}"
WORDS = "2a8,16c,178,2ac,2bc,2bd8,2a0c,2e80,2c68,29c8,f6,f4,2de8"
root = Path((_os.path.expanduser("~/THPSource2") + "/build/fr/run")); root.mkdir(parents=True, exist_ok=True)
spec = root / f"trk_{name}_{rate}.txt"; cap = root / f"trk_{name}_{rate}.otcap"
lines = [f"level {LEVEL_NAMES['warehouse']}", f"frames {total // k}", f"sha256 {capture.SUPPORTED_BUILD_SHA256}"]
for ev in events.split(";"):
    if not ev:
        continue
    buttons, start, length = ev.split(":")
    mask = 0
    for b in buttons.split("+"):
        mask |= capture.ACTION_MASKS[b]
    lines.append(f"action 0x{mask:x} {max(0, int(start) + shift) // k} {max(1, int(length) // k)}")
spec.write_text("\n".join(lines) + "\n")
cap.unlink(missing_ok=True)
extra = {"OT_SCENARIO": sw._wine_z_path(spec), "OT_SCENARIO_OUT": sw._wine_z_path(cap), "OT_FRAME_RATE": "" if rate == "30" else rate,
         "OT_DELIVERY_LOG": "1:2000", "OT_DELIVERY_WORDS": WORDS, "OT_DELIVERY_GWORDS": os.environ.get("FR_GWORDS", "")}
doc = {"name": f"trk-{name}-{rate}", "deliveries": 20000, "every": 2, "keys": [], "masks": []}
fin, text = sb.run_standalone(f"fr-trk-{name}-{rate}", doc, extra=extra)
rows = []
for m in re.finditer(r"delivery (\d+): (\S+) in \S+ \[poll (\d+), sim (\d+), skater (-?\d+) (-?\d+) (-?\d+) state (-?\d+) vy (-?\d+) ay (-?\d+)((?: [wg] [0-9a-f]+=-?\d+)*)\]", text):
    w = " ".join(re.findall(r"[wg] ([0-9a-f]+=-?\d+)", m.group(11)))
    rows.append(" ".join([m.group(1), m.group(4), m.group(5), m.group(6), m.group(7), m.group(8), m.group(9), m.group(10), w, f"poll={m.group(3)}"]))
Path(f"{_os.path.expanduser('~/THPSource2')}/build/fr/trk_{name}_{rate}.txt").write_text("\n".join(rows) + "\n")
print(rate, name, len(rows), "rows", "done" if fin else "NOT FINISHED")
