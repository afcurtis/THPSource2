"""replay_play.py SCRIPT [POLLS] [NAME]: replays an OT_INPUT_LOG script (scripts/input_log_to_script.py of a player's input.txt) in a headless standalone run with the
player's options (quick start level 3, skater 1, single session, PSX textures and colour, everything unlocked). Output: build/m12/replay/NAME/output.txt; the last line is the stop.
Env: XTR_NAME=value is passed on as NAME=value (e.g. XTR_OT_QUICK_MODE=free); WALL=SECONDS (default 600)."""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m12"), exist_ok=True)  # outputs stay in build/
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, os.path.expanduser("~/THPSource2"))
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402

script = Path(sys.argv[1]).resolve()
polls = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
name = sys.argv[3] if len(sys.argv) > 3 else script.stem
wall = int(os.environ.get("WALL", "600"))
sb.set_arch(64)
_run = subprocess.run
subprocess.run = lambda *a, **k: _run(*a, **{**k, "timeout": wall})
extra = {
    "OT_INPUT_LOG": "Z:" + str(script).replace("/", "\\"),
    "OT_QUICK_START": "3,1",
    "OT_QUICK_MODE": "single",
    "OT_UNLOCK": "levels,skaters,cheats",
    "OT_PSX_TEXTURES": "1",
    "OT_PSX_COLOR": "1",
    "OT_RESOLVER_TRACE": "1",
}
extra.update({k[4:]: v for k, v in os.environ.items() if k.startswith("XTR_")})
document = {"name": f"replay{name}", "deliveries": 2 * polls + 4000, "every": 2000, "keys": [], "masks": []}
try:
    finished, text = sb.run_standalone(f"m12-replay{name}", document, extra=extra)
except subprocess.TimeoutExpired as e:
    out = e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    err = e.stderr.decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or "")
    finished, text = False, out + err + "\nTIMEOUT\n"
d = Path((os.path.expanduser("~/THPSource2") + "/build/m12/replay")) / name
d.mkdir(parents=True, exist_ok=True)
(d / "output.txt").write_text(text)
reached = re.findall(r"session: (\d+) deliveries, (\d+) polls", text)
stop = sw.fatal_line(text) or next((line.strip() for line in text.splitlines() if line.startswith("CRASH")), "")
print(f"replay {name}: finished={finished} polls={reached[-1][1] if reached else '?'} {stop[:300]}", flush=True)
