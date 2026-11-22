"""T1 census: run levels headless (Wine, no window) with the texture dump on.

python scripts/play/t1/census.py DUMPDIR FRAMES MODE LEVEL [LEVEL ...]      MODE: psx (OT_PSX_TEXTURES=1) or pc (0)
  -> DUMPDIR/<key>.png, index.tsv, seen.tsv, volatile.tsv; build/t1/runs/<mode>-L<level>.txt (the run's output) and one line a run on stdout
Runs go one after another: the index is read at the start and rewritten at the end of a run.
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/t1"), exist_ok=True)  # outputs stay in build/
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.expanduser("~/THPSource2"))
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402

dump = Path(sys.argv[1])
frames = int(sys.argv[2])
mode = sys.argv[3]
levels = [int(a) for a in sys.argv[4:]]
dump.mkdir(parents=True, exist_ok=True)
runs = Path((os.path.expanduser("~/THPSource2") + "/build/t1/runs"))
runs.mkdir(parents=True, exist_ok=True)
for level in levels:
    d = runs / f"{mode}-L{level}"
    d.mkdir(parents=True, exist_ok=True)
    spec = d / "scenario.txt"
    cap = d / "standalone.otcap"
    actions = "".join(f"action {a}\n" for a in os.environ.get("CENSUS_ACTIONS", "").split(";") if a)   # "0x1000 20 1400;0x40 100 8": a pad mask, its first frame, how long
    spec.write_text(f"level {level}\nframes {frames}\nsha256 f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c\n{actions}")
    cap.unlink(missing_ok=True)
    extra = {
        "OT_SCENARIO": sw._wine_z_path(spec),
        "OT_SCENARIO_OUT": sw._wine_z_path(cap),
        "OT_TEXTURE_DUMP": sw._wine_z_path(dump),
        "OT_TEXTURE_DUMP_TAG": f"{mode}:L{level}",
        "OT_PSX_TEXTURES": "1" if mode == "psx" else "0",
    }
    for pair in os.environ.get("CENSUS_ENV", "").split():
        key, value = pair.split("=", 1)
        extra[key] = value
    document = {"name": f"t1{mode}{level}", "deliveries": 4000 + 8 * frames, "every": 2, "keys": [], "masks": []}
    start = time.time()
    finished, text = sb.run_standalone(f"t1-{mode}-{level}", document, extra=extra)
    seconds = time.time() - start
    (d.parent / f"{mode}-L{level}.txt").write_text(text)
    lines = [line for line in text.splitlines() if line.startswith("texdump:")]
    print(f"{mode} level {level}: finished={finished} {seconds:.0f} s | {sw.fatal_line(text)[:200]} | " + " | ".join(l[8:] for l in lines[-3:]), flush=True)
