"""F1.5B sweeps: python scripts/play/fr/sweep.py SWEEP  (combo | manual | rail | gap_ds | gap_sd | all)
Runs trick.py for every (rate, variant) in parallel (4 workers, retried: two runs cleaning the saves at one instant can collide), then prints one signature per run.
Signature: the distinct physics states in order, the trick buttons (2a0c) seen, the trick score words reached (2bc), the animation ids in order (f6, collapsed)."""
import random, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__import__("os").path.expanduser("~/THPSource2"))
PY = str(ROOT / ".tools/venv/bin/python")
OUT = ROOT / "build/fr"

SCEN = {
    # events in ticks (1/60 s) from the scenario start; the 30 Hz scenarios of re/scenarios doubled
    "combo": ("up:40:240;cross:88:16;square+left:124:16;circle:152:16", 440),
    "manual": ("up:40:56;up:100:6;down:116:40", 320),
    "rail": ("up:40:408;right:228:40;triangle:262:166;cross:270:16", 508),
}


def variants(sweep):
    """-> list of (label, events, total, shifts per rate)"""
    if sweep in SCEN:
        ev, total = SCEN[sweep]
        return [(sweep, ev, total, {"30": [-4, -2, 0, 2, 4], "60": list(range(-4, 5))})]
    gaps = {"30": list(range(0, 32, 2)), "60": list(range(0, 32))}
    out = []
    for rate, gl in gaps.items():
        for g in gl:
            # up is held from tick 40; the ollie is pressed at 88 for 16 ticks (the takeoff is about tick 106); in the air the first button of the pair at 112 (held 6 ticks), the second g ticks after it
            if sweep == "gap_ds":      # a direction, then a button
                ev = f"up:40:240;cross:88:16;left:112:6;square:{112 + g}:6"
            else:                       # a button, then a direction
                ev = f"up:40:240;cross:88:16;square:112:6;left:{112 + g}:6"
            out.append((f"{sweep}_g{g}", ev, 300, {rate: [0]}))
    return out


def one(job):
    rate, label, ev, total, shift = job
    time.sleep(random.random() * 6)
    name = f"{label}s{shift}" if shift else label
    path = OUT / f"trk_{name}_{rate}.txt"
    for _ in range(4):
        path.unlink(missing_ok=True)
        subprocess.run([PY, str(OUT / "trick.py"), rate, label, ev, str(total), str(shift)], cwd=ROOT, capture_output=True, text=True)
        if path.exists():
            return rate, label, shift, path
        time.sleep(random.random() * 5)
    return rate, label, shift, None


def signature(path):
    if path is None:
        return "NO RESULT"
    states, tricks, scores, anims = [], [], [], []
    for line in path.read_text().split("\n"):
        p = line.split()
        if len(p) < 9:
            continue
        s = int(p[5])
        if not states or states[-1] != s:
            states.append(s)
        w = {a.split("=")[0]: int(a.split("=")[1]) for a in p[8:] if "=" in a}
        if w.get("2a0c") and w["2a0c"] not in tricks:
            tricks.append(w["2a0c"])
        if w.get("2bc") and w["2bc"] not in scores:
            scores.append(w["2bc"])
        if w.get("f6") is not None and (not anims or anims[-1] != w["f6"]):
            anims.append(w["f6"])
    return f"states {''.join(map(str, states))} tricks {tricks} pts {scores} anims {anims}"


if __name__ == "__main__":
    sweeps = ["combo", "manual", "rail", "gap_ds", "gap_sd"] if sys.argv[1] == "all" else [sys.argv[1]]
    jobs = []
    for sw in sweeps:
        for label, ev, total, shifts in variants(sw):
            for rate, sl in shifts.items():
                for s in sl:
                    jobs.append((rate, label, ev, total, s))
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(one, jobs))
    for rate, label, shift, path in sorted(results, key=lambda r: (r[1].split("_g")[0], int(r[1].split("_g")[1]) if "_g" in r[1] else 0, r[0], r[2])):
        print(f"{label:14s} rate {rate} shift {shift:+d}: {signature(path)}")
