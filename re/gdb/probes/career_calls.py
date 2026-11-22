"""Phase 4.8 probe: force-call retail career / skater-manager / competition functions.

Run inside the game (level loaded) under GDB, e.g. through build/p44c/run_gdb_level.py::

    P48_PROBE_DIR=re/gdb/probes P48_OUT=out.ndjson P48_GROUPS=goals P48_CASES=40 \\
        python build/p44c/run_gdb_level.py re/gdb/probes/career_calls.py

At the Nth physics frame (P48_FRAME) it snapshots every region the native code touches, stubs the
outside services (assert, sound, messages, random numbers, movie state ...), then for every
function in the selected groups runs P48_CASES forced calls on randomized legal state and writes
NDJSON: a `base` record with the region images, then one `case` record per call (pokes, arguments,
return value, written bytes, ordered service calls, the values the stubs returned).
scripts/make_career_calls_fixture.py turns that into a fixture the native replay test consumes.
"""

import json
import os
import random
import sys

import gdb

sys.path.insert(0, os.environ.get("P48_PROBE_DIR", (os.path.expanduser("~/THPSource2") + "/re/gdb/probes")))

import career_specs  # noqa: E402
import career_specs_more  # noqa: E402,F401  (registers more specs)
import career_specs_skater  # noqa: E402,F401
import career_specs_competition  # noqa: E402,F401
import career_state as cs  # noqa: E402

from opentony.breakpoint import TonyBreakpoint  # noqa: E402
from opentony.fcall import ForcedCalls  # noqa: E402
from opentony.memory import mem  # noqa: E402

OUT = os.environ.get("P48_OUT", (os.path.expanduser("~/THPSource2") + "/build/p48/calls.ndjson"))
GROUPS = [g for g in os.environ.get("P48_GROUPS", "goals").split(",") if g]
CASES = int(os.environ.get("P48_CASES", "40"))
SEED = int(os.environ.get("P48_SEED", "1"))
FRAME = int(os.environ.get("P48_FRAME", "5"))
ONLY = [int(x, 16) for x in os.environ.get("P48_ONLY", "").split(",") if x]
PHYSICS_FRAME = 0x0049E680

state = {"frames": 0, "done": False}


def emit(out, record):
    out.write(json.dumps(record, sort_keys=True) + "\n")
    out.flush()


class Frame(TonyBreakpoint):
    def on_hit(self, ctx):
        if state["done"]:
            return
        state["frames"] += 1
        if state["frames"] < FRAME:
            return
        state["done"] = True
        try:
            run()
        finally:
            self.should_stop = True


def trace(text):
    if os.environ.get("P48_TRACE"):
        with open(OUT + ".trace", "a") as fh:
            fh.write(text + "\n")


def run():
    rng = random.Random(SEED)
    regions = cs.regions(GROUPS)
    static = cs.read_only_regions(GROUPS)
    trace(f"regions {len(regions)} bytes {sum(s for _, s in regions)}; read-only {len(static)}")
    fc = ForcedCalls(regions)
    if os.environ.get("P48_TRACE") == "2":
        fc.trace = trace
    base = fc.snapshot()
    frozen = {b: mem.bytes(b, s) for b, s in static}
    trace("snapshot taken")
    env = career_specs.Env(rng, {**base, **frozen})
    career_specs.install_stubs(fc, env)
    trace("stubs installed")
    with open(OUT, "w") as out:
        whole = {**base, **frozen}
        emit(out, {"t": "base", "regions": [[f"{b:08x}", whole[b].hex()] for b in sorted(whole)]})
        for spec in career_specs.select(GROUPS, ONLY):
            emitted = 0
            for n in range(CASES):
                env.begin_case()
                try:
                    args, pokes = spec.gen(env, n)
                except career_specs.Skip:
                    continue
                except Exception as exc:  # noqa: BLE001
                    gdb.write(f"{spec.name} case {n}: generator failed: {exc!r}\n")
                    continue
                trace(f"call {spec.name} #{n} args={args} pokes={len(pokes)}")
                try:
                    record = fc.run(spec.address, args, pokes=pokes, base=base, this=spec.this)
                except (RuntimeError, KeyError) as exc:
                    gdb.write(f"{spec.name} case {n}: {exc}\n")
                    fc.restore(base)   # resynchronise memory with the base snapshot
                    continue
                trace(f"  done ret={record['ret']} err={record.get('error')}")
                record["name"] = spec.name
                record["group"] = spec.group
                emit(out, {"t": "case", **record})
                emitted += 1
            gdb.write(f"{spec.name}: {emitted} cases\n")
        emit(out, {"t": "end"})


Frame(PHYSICS_FRAME, internal=True)
gdb.write(f"p48 career probe armed: groups={GROUPS} cases={CASES} frame={FRAME} -> {OUT}\n")
