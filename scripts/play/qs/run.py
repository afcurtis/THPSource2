"""A headless quick start: python scripts/play/qs/run.py NAME LEVEL MODE DELIVERIES [poll:key:hold ...] [VAR=value ...]

MODE: free, single, speedrun, competition. Keys: enter, esc, down, up, ... (tony/session_scenarios.py KEYS) at an input poll. Writes build/qs/NAME/run.txt and
prints the challenge:, options: quick start, scores: lines and the stop."""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/qs"), exist_ok=True)  # outputs stay in build/
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.expanduser("~/THPSource2"))
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402

name, level, mode, deliveries = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
keys, extra = [], {}
for arg in sys.argv[5:]:
    if "=" in arg:
        k, v = arg.split("=", 1)
        extra[k] = v
    else:
        poll, key, hold = arg.split(":")
        keys.append({"poll": int(poll), "key": key, "hold": int(hold)})
out = Path((os.path.expanduser("~/THPSource2") + "/build/qs")) / name
out.mkdir(parents=True, exist_ok=True)
scores = out / "highscores.tsv"
scores.unlink(missing_ok=True)
env = {"OT_QUICK_START": f"{level},0", "OT_QUICK_MODE": "" if mode == "free" else mode, "OT_SCORES_FILE": sw._wine_z_path(scores),
       "OT_INITIALS": "TST"}
env.update(extra)
document = {"name": name, "deliveries": deliveries, "every": 2, "keys": keys, "masks": []}
finished, text = sb.run_standalone(f"qs-{name}", document, extra=env)
(out / "run.txt").write_text(text)
for line in text.splitlines():
    if line.startswith(("challenge:", "scores:")) or "quick start" in line or "FATAL" in line or line.startswith("CRASH") or "session:" in line:
        print(line[:300])
print(f"finished={finished} stop={sw.fatal_line(text)[:300]}")
if scores.exists():
    print(scores.read_text())
