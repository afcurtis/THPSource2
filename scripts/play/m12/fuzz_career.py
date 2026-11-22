"""fuzz_career.py [FIRST_SEED [COUNT [POLLS]]]: random keys through the career path of the standalone alone (no retail): the real menus, not a scenario.

The first keys are boot-career-hangar's (Enter at polls 500..1100: main menu -> Career -> skater -> level select -> the Hangar). From poll 1300 on, random keys: movement
(arrows held), jump / grind / grab / flip (Space, V, C, B), spins, pause (P, Esc), camera (O), Enter and the arrows as menu keys, so it also walks the pause menu, the end-of-run
screens (statistics, goals, high-score name entry) and the unlock screen once the level's timer ends. POLLS (default 9000) is how far the walk goes; the level's 2-minute
timer ends near poll 8700 in the Hangar.

Env: LEVELKEYS=1 leaves the Enter walk to the level select as it is (the default: the career's first level, the Hangar); WALL=SECONDS ends a hang (default 600); XTR_NAME=value
is passed to the program as NAME=value (XTR_OT_INPUT_TRACE=1 ...). One line per seed: finished, the stop, the polls reached. Output: build/m12/career/sSEED/output.txt."""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m12"), exist_ok=True)  # outputs stay in build/
import os
import random
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, os.path.expanduser("~/THPSource2"))
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402

first = int(sys.argv[1]) if len(sys.argv) > 1 else 1
count = int(sys.argv[2]) if len(sys.argv) > 2 else 4
polls = int(sys.argv[3]) if len(sys.argv) > 3 else 9000
root = Path((os.path.expanduser("~/THPSource2") + "/build/m12/career"))
DIK = {"up": 0xC8, "down": 0xD0, "left": 0xCB, "right": 0xCD, "enter": 0x1C, "space": 0x39, "esc": 0x01, "p": 0x19, "o": 0x18, "v": 0x2F, "c": 0x2E, "b": 0x30,
       "pad7": 0x47, "pad9": 0x49, "pad1": 0x4F, "pad3": 0x51, "tab": 0x0F, "backspace": 0x0E, "y": 0x15, "n": 0x31}
wall = int(os.environ.get("WALL", "600"))
_run = subprocess.run
subprocess.run = lambda *a, **k: _run(*a, **{**k, "timeout": wall})
for seed in range(first, first + count):
    rng = random.Random(seed * 104729 + 3)
    keys = [{"poll": p, "key": DIK["enter"], "hold": 5} for p in (500, 600, 700, 800, 900, 1000, 1100)]
    at = 1300
    while at < polls:
        r = rng.random()
        if r < 0.40:   # movement: an arrow held a while
            key, hold = rng.choice(["up", "up", "up", "left", "right", "down"]), rng.randrange(6, 70)
        elif r < 0.60:   # tricks and stance / spins
            key, hold = rng.choice(["space", "space", "v", "c", "b", "pad7", "pad9", "pad1", "pad3"]), rng.randrange(2, 25)
        elif r < 0.68:   # pause and camera
            key, hold = rng.choice(["p", "esc", "o"]), rng.randrange(2, 6)
        else:   # menu keys
            key, hold = rng.choice(["up", "down", "enter", "enter", "left", "right", "esc", "space", "y", "n", "backspace"]), rng.randrange(2, 6)
        keys.append({"poll": at, "key": DIK[key], "hold": hold})
        at += rng.randrange(8, 45)
    keys.sort(key=lambda k: k["poll"])
    d = root / f"s{seed}"
    d.mkdir(parents=True, exist_ok=True)
    (d / "keys.txt").write_text("".join(f"{k['poll']}:{k['key']:x}:{k['hold']}\n" for k in keys))
    document = {"name": f"fuzzcareer{seed}", "deliveries": 2 * polls + 4000, "every": 2000, "keys": keys, "masks": []}
    extra = {"OT_RESOLVER_TRACE": "1"}
    extra.update({k[4:]: v for k, v in os.environ.items() if k.startswith("XTR_")})
    try:
        finished, text = sb.run_standalone(f"m12-fuzzcareer{seed}", document, extra=extra)
    except subprocess.TimeoutExpired as e:   # a hang: keep what it printed
        finished = False
        out = e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        err = e.stderr.decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or "")
        text = out + err + "\nTIMEOUT\n"
    (d / "output.txt").write_text(text)
    reached = re.findall(r"session: (\d+) deliveries, (\d+) polls", text)
    print(f"career seed {seed}: finished={finished} polls={reached[-1][1] if reached else '?'}{' HANG' if 'TIMEOUT' in text else ''} {sw.fatal_line(text)[:260]}", flush=True)
