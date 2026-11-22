"""Phase 4.9 probe: force-call retail create-a-skater functions.

Run inside the game (level loaded) under GDB, e.g. through build/p44c/run_gdb_level.py::

    P49_PROBE_DIR=re/gdb/probes P49_OUT=out.ndjson P49_GROUPS=color P49_CASES=40 \\
        python build/p44c/run_gdb_level.py re/gdb/probes/create_calls.py

Like career_calls.py (Phase 4.8) it stops at the Nth physics frame, but the state the functions work
on lives in one scratch slab taken from the game's own allocator, not in the career globals: the
editor's objects (palettes, texture databases, part tables) are laid out there by the case
generators.  For every function in the selected groups it runs P49_CASES forced calls and writes
NDJSON: a `base` record with the region images, then one `case` record per call (pokes, arguments,
return value, written bytes, ordered service calls, the values the stubs returned).
scripts/make_career_calls_fixture.py turns that into a fixture the native replay test consumes
(the fixture format is shared with Phase 4.8).
"""

import json
import os
import random
import sys

import gdb

sys.path.insert(0, os.environ.get("P49_PROBE_DIR", (os.path.expanduser("~/THPSource2") + "/re/gdb/probes")))

import create_specs  # noqa: E402
import create_specs_color  # noqa: E402,F401  (registers the colour specs)

from opentony.breakpoint import TonyBreakpoint  # noqa: E402
from opentony.fcall import ForcedCalls, write_chunks  # noqa: E402
from opentony.memory import mem  # noqa: E402

for _optional in ("create_specs_textures", "create_specs_records", "create_specs_spool", "create_specs_parts",
                  "create_specs_stitch", "create_specs_palette"):
    try:
        __import__(_optional)
    except ModuleNotFoundError as exc:
        if exc.name != _optional:
            raise

OUT = os.environ.get("P49_OUT", (os.path.expanduser("~/THPSource2") + "/build/p49/calls.ndjson"))
GROUPS = [g for g in os.environ.get("P49_GROUPS", "color").split(",") if g]
CASES = int(os.environ.get("P49_CASES", "40"))
SEED = int(os.environ.get("P49_SEED", "1"))
FRAME = int(os.environ.get("P49_FRAME", "5"))
ONLY = [int(x, 16) for x in os.environ.get("P49_ONLY", "").split(",") if x]
PHYSICS_FRAME = 0x0049E680
ALLOC_LOADED_BUFFER = 0x0046F490

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
    if os.environ.get("P49_TRACE"):
        with open(OUT + ".trace", "a") as fh:
            fh.write(text + "\n")


def merge(items, gap=0x40):
    items = sorted(items)
    merged = []
    for base, size in items:
        if merged and base <= merged[-1][0] + merged[-1][1] + gap:
            end = max(merged[-1][0] + merged[-1][1], base + size)
            merged[-1][1] = end - merged[-1][0]
        else:
            merged.append([base, size])
    return [(b, s) for b, s in merged]


def run():
    rng = random.Random(SEED)
    slab_bytes = max(create_specs.SLAB_BYTES.get(g, 0) for g in GROUPS) or 0x1000
    slab = ForcedCalls.call(ALLOC_LOADED_BUFFER, (slab_bytes, 1, 1, 0))
    trace(f"slab {slab:#x} bytes {slab_bytes:#x}")
    regions = []
    for g in GROUPS:
        extra = create_specs.EXTRA_REGIONS.get(g)
        # a group names the windows of the slab it touches (snapshots are the slow part of a case);
        # without any the whole slab is captured
        regions += extra(slab) if extra else [(slab, slab_bytes)]
    static = merge([r for g in GROUPS for r in create_specs.READ_ONLY.get(g, [])], gap=0)
    fc = ForcedCalls(merge(regions, gap=0))
    # the slab starts as whatever the allocator left: zero it, then seed the groups' fixed content
    zero = bytes(slab_bytes)
    write_chunks(slab, zero)
    env = create_specs.Env(rng, slab, slab_bytes, {})
    for g in GROUPS:
        seed = create_specs.SEEDS.get(g)
        if seed:
            for address, data in seed(env, rng).items():
                write_chunks(address, data)
    base = fc.snapshot()
    frozen = {b: mem.bytes(b, s) for b, s in static}
    env.base = {**base, **frozen}
    trace("snapshot taken")
    create_specs.install_stubs(fc, env)
    for g in GROUPS:
        install = getattr(create_specs, "GROUP_STUBS", {}).get(g)
        if install:
            install(fc, env)
    trace("stubs installed")
    with open(OUT, "w") as out:
        whole = {**base, **frozen}
        emit(out, {"t": "base", "regions": [[f"{b:08x}", whole[b].hex()] for b in sorted(whole)]})
        for spec in create_specs.select(GROUPS, ONLY):
            emitted = 0
            for n in range(CASES):
                env.begin_case()
                try:
                    args, pokes = spec.gen(env, n)
                except create_specs.Skip:
                    continue
                except Exception as exc:  # noqa: BLE001
                    gdb.write(f"{spec.name} case {n}: generator failed: {exc!r}\n")
                    continue
                trace(f"call {spec.name} #{n} args={args} pokes={len(pokes)}")
                try:
                    this = spec.this(env) if callable(spec.this) else spec.this
                    record = fc.run(spec.address, args, pokes=pokes, base=base, this=this)
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
gdb.write(f"p49 create probe armed: groups={GROUPS} cases={CASES} frame={FRAME} -> {OUT}\n")
