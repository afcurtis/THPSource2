"""Run the standalone alone (no retail) on levels: python scripts/play/m12/levels_spin.py [LEVEL ...]  -> one line per level: frames recorded, the stop.

A scenario of `level N`, 64 idle frames; the output of each run is under build/m12/levels/LN. Needs the DLLs built (scripts/play/m10/gen.sh)."""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m12"), exist_ok=True)  # outputs stay in build/
import sys
from pathlib import Path

sys.path.insert(0, _os.path.expanduser("~/THPSource2"))
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402

root = Path((_os.path.expanduser("~/THPSource2") + "/build/m12/levels"))
levels = [int(a) for a in sys.argv[1:]] or [1, 2, 3, 4, 6, 7, 8, 9, 10]
frames = 64
for level in levels:
    d = root / f"L{level}"
    d.mkdir(parents=True, exist_ok=True)
    spec = d / "scenario.txt"
    cap = d / "standalone.otcap"
    spec.write_text(f"level {level}\nframes {frames}\nsha256 f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c\n")
    cap.unlink(missing_ok=True)
    extra = {"OT_SCENARIO": sw._wine_z_path(spec), "OT_SCENARIO_OUT": sw._wine_z_path(cap)}
    document = {"name": f"level{level}", "deliveries": 4000 + 8 * frames, "every": 2, "keys": [], "masks": []}
    finished, text = sb.run_standalone(f"m12-level{level}", document, extra=extra)
    (d / "output.txt").write_text(text)
    fatal = sw.fatal_line(text)
    print(f"level {level}: finished={finished} {fatal[:300]}", flush=True)
