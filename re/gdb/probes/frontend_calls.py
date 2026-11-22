"""Phase 4.10a probe: force-call retail frontend text-layer functions (fonts, messages, effects, window base).

Run inside the game (level loaded) under GDB, e.g. through build/p44c/run_gdb_level.py::

    P410_PROBE_DIR=re/gdb/probes P410_OUT=out.ndjson P410_GROUPS=color P410_CASES=40 \\
        python build/p44c/run_gdb_level.py re/gdb/probes/create_calls.py

Like career_calls.py (Phase 4.8) it stops at the Nth physics frame, but the state the functions work
on lives in one scratch slab taken from the game's own allocator, not in the career globals: the
editor's objects (palettes, texture databases, part tables) are laid out there by the case
generators.  For every function in the selected groups it runs P410_CASES forced calls and writes
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

sys.path.insert(0, os.environ.get("P410_PROBE_DIR", (os.path.expanduser("~/THPSource2") + "/re/gdb/probes")))

import create_specs  # noqa: E402  (the shared spec registry, Pokes and Env)
import frontend_specs_common  # noqa: E402

from opentony.breakpoint import TonyBreakpoint  # noqa: E402
from opentony.fcall import ForcedCalls, write_chunks  # noqa: E402
from opentony.memory import mem  # noqa: E402

# Phase 4.15b: the spec modules of the camera and the replay recorder (one list, used by both loops below)
EXTRA_MODULES = ("cam_specs", "video_specs", "video_specs_frame", "cam_specs_small", "video_specs_more", "cam_specs_modes", "video_specs_menu", "world_specs", "world_specs_baddy", "world_specs_trig", "world_specs_cmd", "world_specs_trick", "world_specs_skater", "world_specs_extra", "world_specs_skater2", "world_specs_traffic", "world_specs_glow", "world_specs_platform", "world_specs_script","bit_specs", "bit_specs2", "bit_specs3", "bit_specs4", "bit_specs5", "bit_specs6", "render_specs", "render_specs2", "render_specs3", "render_specs4", "render_specs5", "render_specs6", "render_specs7", "render_specs8", "cobj_specs", "cobj_specs2", "cobj_specs3", "cobj_specs4", "cobj_specs5", "cobj_specs6", "cobj_specs7", "cobj_specs8", "gpu_specs", "world_specs_tips")

for _optional in ("frontend_specs_font", "frontend_specs_mess", "frontend_specs_effects", "frontend_specs_window", "frontend_specs_panel", "frontend_specs_panel_draw",
                  "frontend_specs_menu", "frontend_specs_menu_items", "frontend_specs_menu_lists", "frontend_specs_menu_screens",
                  "frontend_specs_mainmenu", "frontend_specs_credits", "frontend_specs_skhelp", "frontend_specs_skwin", "frontend_specs_skapp", "frontend_specs_skstat",
                  "frontend_specs_screens", "frontend_specs_screens_cases", "frontend_specs_select", "frontend_specs_trick",
                  "frontend_specs_playsel", "frontend_specs_playsel_cases", "frontend_specs_playsel_sel", "frontend_specs_playsel_bio", "frontend_specs_playsel_view",
                  "frontend_specs_eqsel", "frontend_specs_eqsel_cases", "frontend_specs_carsel", "frontend_specs_handi", "frontend_specs_multi",
                  "frontend_specs_levsel", "frontend_specs_opt", "frontend_specs_opt_pages", "frontend_specs_opt_ctl",
                  "frontend_specs_goals", "frontend_specs_goals_cases", "frontend_specs_goals_win",
                  "frontend_specs_front", "frontend_specs_front_cases", "frontend_specs_front_cases2", "frontend_specs_front_upd", "frontend_specs_front_ld", "frontend_specs_shell", "frontend_specs_shell2", "frontend_specs_net", "frontend_specs_bmp", "frontend_specs_loops", "psx_specs", "pspool_specs", "pspool_specs_view", "pspool_specs_file", "gen_specs", "gen_specs_poly", "gen_specs_model", "levelgen_specs", "levelgen_specs_pieces", "levelgen_specs_edit", "levelgen_specs_prk", "levelgen_specs_region", "editor_specs", "editor_specs_view", "editor_specs_cursor", "editor_specs_obj", "platform_specs", "platform_specs_sys", "audio_specs", "net_specs", "net_specs_state", "net_specs_play", "math_specs", "mem_specs", "mmu_specs", "mmu_specs_mgr", "mmu_specs_misc", "mmu_specs_ui", "core_specs", "core_specs_pkr", "core_specs_init", "core_specs_main", "core_specs_main_cases", "core_specs_top", "rail_specs", "rail_specs_build", "phys_specs", "phys_specs_trick", "phys_specs_load", "phys_specs_stats", "phys_specs_misc", "phys_specs_hit", "m3d_specs", "phys_specs_parts", "phys_specs_events", "m3d_specs_hit", "phys_specs_big") + EXTRA_MODULES:
    try:
        __import__(_optional)
    except ModuleNotFoundError as exc:
        if exc.name != _optional:
            raise

OUT = os.environ.get("P410_OUT", (os.path.expanduser("~/THPSource2") + "/build/p410a/calls.ndjson"))
GROUPS = [g for g in os.environ.get("P410_GROUPS", "color").split(",") if g]
CASES = int(os.environ.get("P410_CASES", "40"))
SEED = int(os.environ.get("P410_SEED", "1"))
FRAME = int(os.environ.get("P410_FRAME", "5"))
ONLY = [int(x, 16) for x in os.environ.get("P410_ONLY", "").split(",") if x]
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
    if os.environ.get("P410_TRACE"):
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
    gdb.execute("set unwind-on-signal " + ("off" if os.environ.get("P410_NOUNWIND") else "on"))   # a faulting forced call must not leave gdb inside the dummy frame
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
    if os.environ.get("P410_STUBTRACE"):
        fc.trace = trace      # (every stub hit is written to the trace file: the last one before a failure names the callee)
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
    # shared outside services (assertion, printf, random numbers, allocation); the groups add their own
    frontend_specs_common.install_stubs(fc, env)
    installed = set()      # (modules that share one GROUP_STUBS table, as the options screens' do, must not install a group's stubs twice: two breakpoints at one address corrupt the stack)
    for g in GROUPS:
        for module in ("frontend_specs_font", "frontend_specs_mess", "frontend_specs_effects", "frontend_specs_window", "frontend_specs_panel", "frontend_specs_panel_draw",
                       "frontend_specs_menu", "frontend_specs_mainmenu", "frontend_specs_credits", "frontend_specs_skhelp", "frontend_specs_skwin", "frontend_specs_skapp", "frontend_specs_skstat",
                       "frontend_specs_screens", "frontend_specs_select", "frontend_specs_trick", "frontend_specs_playsel", "frontend_specs_playsel_bio", "frontend_specs_playsel_view",
                       "frontend_specs_eqsel", "frontend_specs_carsel", "frontend_specs_handi", "frontend_specs_multi", "frontend_specs_levsel",
                       "frontend_specs_opt", "frontend_specs_opt_pages", "frontend_specs_opt_ctl", "frontend_specs_goals", "frontend_specs_front", "frontend_specs_shell", "frontend_specs_shell2", "frontend_specs_net", "frontend_specs_bmp", "frontend_specs_loops", "psx_specs", "pspool_specs", "pspool_specs_view", "pspool_specs_file", "gen_specs", "gen_specs_poly", "gen_specs_model", "levelgen_specs", "levelgen_specs_pieces", "levelgen_specs_edit", "levelgen_specs_prk", "levelgen_specs_region", "editor_specs", "editor_specs_view", "editor_specs_cursor", "editor_specs_obj", "platform_specs", "platform_specs_sys", "audio_specs", "net_specs", "net_specs_state", "net_specs_play", "math_specs", "mem_specs", "mmu_specs", "mmu_specs_mgr", "mmu_specs_misc", "mmu_specs_ui", "core_specs", "core_specs_pkr", "core_specs_init", "core_specs_main", "core_specs_main_cases", "core_specs_top", "rail_specs", "rail_specs_build", "phys_specs", "phys_specs_trick", "phys_specs_load", "phys_specs_stats", "phys_specs_misc", "phys_specs_hit", "m3d_specs", "phys_specs_parts", "phys_specs_events", "m3d_specs_hit", "phys_specs_big") + EXTRA_MODULES:
            install = getattr(sys.modules.get(module), "GROUP_STUBS", {}).get(g)
            if install and (g, install) not in installed:
                installed.add((g, install))
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
                if os.environ.get("P410_TRACE") == "2":      # (Phase 4.11b: the pokes of the case that hangs are the last lines of the trace)
                    trace("  pokes " + json.dumps({f"{a:08x}": bytes(d).hex() for a, d in pokes.items()}))
                try:
                    this = spec.this(env) if callable(spec.this) else spec.this
                    edx = spec.edx(env) if callable(spec.edx) else spec.edx
                    via = spec.via(env) if callable(spec.via) else None
                    record = fc.run(spec.address, args, pokes=pokes, base=base, this=this, edx=edx, via=via)
                except (RuntimeError, KeyError) as exc:
                    gdb.write(f"{spec.name} case {n}: {exc}\n")
                    fc.restore(base)   # resynchronise memory with the base snapshot
                    continue
                trace(f"  done ret={record['ret']} err={record.get('error')}")
                record["name"] = spec.name
                record["group"] = spec.group
                emit(out, {"t": "case", **record})
                emitted += 1
                if "another thread" in str(record.get("error", "")):
                    # Phase 4.11b: the watchdog interrupted a call that loops for ever; the looping thread stays, so the process is of no more use: end the capture here
                    gdb.write(f"{spec.name} case {n}: the call looped (the watchdog fired): ending the capture\n")
                    emit(out, {"t": "end"})
                    return
            gdb.write(f"{spec.name}: {emitted} cases\n")
        emit(out, {"t": "end"})


Frame(PHYSICS_FRAME, internal=True)
gdb.write(f"p410a create probe armed: groups={GROUPS} cases={CASES} frame={FRAME} -> {OUT}\n")
