"""fuzz_to_yml.py SEED...: build/m12/fuzz/sSEED/scenario.txt -> re/scenarios/hangar-fuzzSEED.yml (an untracked file: remove it after the run, the scenario tests want only the gated ones)."""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m12"), exist_ok=True)  # outputs stay in build/
import sys
sys.path.insert(0, _os.path.expanduser("~/THPSource2"))
from tony import capture

names = {v: k for k, v in capture.ACTION_MASKS.items()}
for seed in sys.argv[1:]:
    lines = open(f"{_os.path.expanduser('~/THPSource2')}/build/m12/fuzz/s{seed}/scenario.txt").read().splitlines()
    frames = int(next(l for l in lines if l.startswith("frames")).split()[1])
    events = []
    for l in lines:
        if l.startswith("action"):
            _, mask, start, hold = l.split()
            events.append((int(start), names[int(mask, 16)], "press"))
            events.append((int(start) + int(hold), names[int(mask, 16)], "release"))
    events.sort(key=lambda e: (e[0], e[2] != "release"))
    out = ["version: 1", "level: hangar", f"frames: {frames}", "", "input:"]
    for frame, action, state in events:
        out += [f"  - frame: {frame}", f"    action: {action}", f"    state: {state}"]
    out += ["", "forensics:", "  - all", "", "expect:", "  retail: match", ""]
    open(f"{_os.path.expanduser('~/THPSource2')}/re/scenarios/hangar-fuzz{seed}.yml", "w").write("\n".join(out))
    print(seed, len(events), "events")
