"""G4 step 1: census over the levels at fog 150 (the PC default) and fog off. python scripts/play/g4/sweep.py [FRAMES] [LEVEL ...]
Runs scripts/play/g4/census.py four at a time and prints one summary line per run (build/g4/summary.txt)."""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/g4"), exist_ok=True)  # outputs stay in build/
import os
import random
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(os.path.expanduser("~/THPSource2"))
PY = str(ROOT / ".tools/venv/bin/python")
frames = int(sys.argv[1]) if len(sys.argv) > 1 else 700
levels = [int(a) for a in sys.argv[2:]] or list(range(13))


def run(job):
    level, fog = job
    tag = f"{os.environ.get('SWEEP_TAG', '')}L{level}_{fog}"
    time.sleep(random.random() * 6)
    for _ in range(3):
        extra = (["OT_FOG_DISTANCE=off"] if fog == "off" else ["OT_FOG_DISTANCE=150"]) + os.environ.get("SWEEP_VARS", "").split()
        subprocess.run([PY, str(ROOT / "scripts/play/g4/census.py"), tag, str(level), str(frames), *extra], cwd=ROOT, capture_output=True, text=True)
        out = ROOT / f"build/g4/census/{tag}/output.txt"
        if out.exists() and "census:" in out.read_text():
            break
        time.sleep(random.random() * 5)
    return tag, summarise(out)


def summarise(path):
    if not path.exists():
        return "no output"
    rows = []
    for line in path.read_text().splitlines():
        if line.startswith("census:"):
            r = dict(re.findall(r"(\w+)=(\S+)", line))
            rows.append(r)
    rows = [r for r in rows if int(r["seen"]) > 0]
    if not rows:
        return "no views"

    def tot(k):
        return sum(int(r[k]) for r in rows)

    def peak(k):
        return max(int(r[k]) for r in rows)

    def pair(k, i):
        return [int(r[k].split("/")[i]) for r in rows]

    dmins = [int(r["dist"].split("..")[0]) for r in rows if int(r["chained"]) > 0]
    dmaxs = [int(r["dist"].split("..")[1]) for r in rows if int(r["chained"]) > 0]
    mindrops = [int(r["mindrop"]) for r in rows if int(r["dropped"]) > 0]
    return (f"views={len(rows)} per view peak: seen={peak('seen')} chained={peak('chained')} neg={peak('neg')} swapped={peak('swapped')} dropped={peak('dropped')} | "
            f"chg x2 {max(pair('chg2', 0))} (revived {max(pair('chg2', 1))}), x4 {max(pair('chg4', 0))} ({max(pair('chg4', 1))}), max {max(pair('chgmax', 0))} ({max(pair('chgmax', 1))}) | "
            f"dist {min(dmins) if dmins else '-'}..{max(dmaxs) if dmaxs else '-'} mindrop {min(mindrops) if mindrops else '-'} maxdraw {peak('maxdraw')} scale {rows[0]['scale']} factor {rows[0]['factor']} "
            f"limits {rows[0]['limits']} arena high {max(int(r['arena'].split('/')[0]) for r in rows)} of {rows[0]['arena'].split('/')[1]}")


jobs = [(level, fog) for level in levels for fog in ("150", "off")]
with ThreadPoolExecutor(4) as pool:
    results = list(pool.map(run, jobs))
text = "\n".join(f"{tag}: {line}" for tag, line in results)
(ROOT / f"build/g4/summary{os.environ.get('SWEEP_TAG', '')}.txt").write_text(text + "\n")
print(text)
