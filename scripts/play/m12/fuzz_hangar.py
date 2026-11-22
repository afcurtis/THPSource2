"""fuzz_hangar.py [FIRST_SEED [COUNT [FRAMES]]]: random Hangar inputs (forward, steering, ollies, tricks) through the standalone alone, resolver trace on.

LEVEL=N picks the level. EXTRA=camera,switch,pause,menu|all adds the camera button, stance/spin buttons, pause (P, by poll) and pause-menu keys (tags get an `x`).
`frames=short` = the run never recorded all its frames (stuck paused or in a menu: not a stop, the pause menu can be left open by a random Enter).
One line per seed: the stop (FATAL or the end), and which of the air functions were resolved. Scenarios are kept as build/m12/fuzz/sSEED/scenario.txt: copy one to a
re/scenarios yml (`action` lines -> `input:` entries) to compare it with retail."""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m12"), exist_ok=True)  # outputs stay in build/
import os
import random
import re
import sys
from pathlib import Path

sys.path.insert(0, _os.path.expanduser("~/THPSource2"))
from tony import capture  # noqa: E402
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402

sb.set_arch(int(os.environ.get("ARCH", "32")))   # ARCH=64: the x86-64 program the player runs
first = int(sys.argv[1]) if len(sys.argv) > 1 else 1
count = int(sys.argv[2]) if len(sys.argv) > 2 else 8
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 600
root = Path((_os.path.expanduser("~/THPSource2") + "/build/m12/fuzz"))
# EXTRA=camera,switch,pause,menu (or EXTRA=all): inputs beyond movement and tricks; unset = the old behaviour. `menu` only acts together with `pause`.
kinds = set(os.environ.get("EXTRA", "").replace("all", "camera,switch,pause,menu").split(",")) - {""}
masks = capture.ACTION_MASKS
for seed in range(first, first + count):
    rng = random.Random(seed)
    lines = [f"level 0", f"frames {frames}", "sha256 f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c"]
    # forward for most of the run, in long holds
    at = 10
    while at < frames - 20:
        hold = rng.randrange(30, 160)
        lines.append(f"action 0x{masks['up']:x} {at} {hold}")
        at += hold + rng.randrange(0, 20)
    # steering bursts
    at = rng.randrange(20, 60)
    while at < frames - 20:
        side = rng.choice(["left", "right"])
        hold = rng.randrange(8, 70)
        lines.append(f"action 0x{masks[side]:x} {at} {hold}")
        at += hold + rng.randrange(5, 60)
    # ollies, and tricks in the air now and then
    at = rng.randrange(30, 80)
    while at < frames - 30:
        hold = rng.randrange(3, 25)
        lines.append(f"action 0x{masks['cross']:x} {at} {hold}")
        if rng.random() < 0.5:
            lines.append(f"action 0x{masks[rng.choice(['triangle', 'square', 'circle'])]:x} {at + hold + rng.randrange(2, 12)} {rng.randrange(2, 10)}")
        at += hold + rng.randrange(20, 90)
    d = root / f"s{seed}"
    d.mkdir(parents=True, exist_ok=True)
    spec = d / "scenario.txt"
    cap = d / "standalone.otcap"
    spec.write_text("\n".join(lines) + "\n")
    cap.unlink(missing_ok=True)
    extra = {"OT_SCENARIO": sw._wine_z_path(spec), "OT_SCENARIO_OUT": sw._wine_z_path(cap), "OT_RESOLVER_TRACE": "1"}
    document = {"name": f"fuzz{seed}", "deliveries": 4000 + 8 * frames, "every": 2, "keys": [], "masks": []}
    finished, text = sb.run_standalone(f"m12-fuzz{seed}", document, extra=extra)
    (d / "output.txt").write_text(text)
    fatal = sw.fatal_line(text)
    reached = [a for a in ("00497aa0", "00497df0") if re.search(r"resolve: engine_" + a, text)]
    print(f"seed {seed}: finished={finished} reached={reached} {fatal[:260]}", flush=True)
