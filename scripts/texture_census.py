#!/usr/bin/env python3
"""The full texture census (texture-upscaling plan, T3): every run of a recipe, headless under Wine with the texture dump on, then one merged dump.

    python scripts/texture_census.py OUT_DIR [--recipe re/config/texture_census.yml] [--jobs N] [--only REGEX] [--merge-only]

The recipe (`re/config/texture_census.yml`) lists runs. Each run is one of
  * a level run: `level`, optional `skater` and `look`, `frames`, optional `actions` (OT_SCENARIO: the scenario bootstrap passes the menus and launches the level), or
  * a session run: `keys` and `deliveries` (the keyed session of re/scenarios/sessions/*.yml, from the boot: menus, screens, the park editor),
always with `textures: pc|psx` (OT_PSX_TEXTURES), for a session optionally `undrawn: world` (a key only its store holds and no run draws is a 3D texture: the park
editor's pieces; by default such a key of a session is 2D, scripts/texture_classes.py), and the recipe's `env` (unlocks; a run's `unlock` replaces OT_UNLOCK for that run). Each run dumps into OUT_DIR/runs/<name>/ (OT_TEXTURE_DUMP, tagged with the run's
name) and its output goes to OUT_DIR/runs/<name>.txt; runs go side by side on isolated Wine workers (tony/workers.py). The merge then writes OUT_DIR/<key>.png (one per key),
OUT_DIR/index.tsv (the dump's columns; use, wrap and mod2x are the union over runs, the tag is the first run that had the key), OUT_DIR/seen.tsv (run, key, draw/walk) and
OUT_DIR/runs.tsv (one line a run: name, kind, textures, level, skater, look, finished, seconds, keys, keys no earlier run had, the scenario's skater line, undrawn).

A run whose folder already has `done.txt` is not run again (delete the folder to redo it), so a stopped census resumes.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

DEFAULT_RECIPE = ROOT / "re" / "config" / "texture_census.yml"
USE = {"world": 1, "2d": 2, "both": 3, "unknown": 0}
WRAP = {"wrap": 1, "clamp": 2, "both": 3, "unknown": 0}
MOD = {"yes": 1, "no": 2, "both": 3, "unknown": 0}


def name_of(bits: int, table: dict[str, int]) -> str:
    return next(name for name, value in table.items() if value == bits)


def load_recipe(path: Path) -> tuple[dict, list[dict]]:
    """(the recipe's defaults, its runs with the defaults applied); checks the fields a run may have."""
    import yaml

    document = yaml.safe_load(path.read_text())
    if document.get("version") != 1:
        raise SystemExit(f"{path}: version 1 expected")
    defaults = {"frames": document.get("frames", 600), "env": dict(document.get("env") or {})}
    runs, names = [], set()
    allowed = {"name", "level", "skater", "look", "frames", "actions", "keys", "deliveries", "textures", "undrawn", "unlock", "note"}
    for run in document["runs"]:
        unknown = set(run) - allowed
        if unknown:
            raise SystemExit(f"{path}: run {run.get('name')}: unknown fields {sorted(unknown)}")
        if run["name"] in names or not re.fullmatch(r"[A-Za-z0-9_.-]+", run["name"]):
            raise SystemExit(f"{path}: run name {run['name']!r} repeated or not [A-Za-z0-9_.-]")
        names.add(run["name"])
        if run.get("textures") not in ("pc", "psx"):
            raise SystemExit(f"{path}: run {run['name']}: textures must be pc or psx")
        if run.get("undrawn", "world") not in (("world",) if "level" in run else ("world", "2d")):
            raise SystemExit(f"{path}: run {run['name']}: undrawn must be world or 2d, and only a session may say 2d")
        if ("level" in run) == ("keys" in run):
            raise SystemExit(f"{path}: run {run['name']}: a run has a level or keys, not both and not neither")
        runs.append({"frames": defaults["frames"], **run})
    return defaults, runs


def undrawn_of(run: dict) -> str:
    """The class of a key only this run's store holds, if no run draws it: world for a level or skater run, a session's `undrawn` (2d by default)."""
    return run.get("undrawn", "2d") if "keys" in run else "world"


def kind_of(run: dict) -> str:
    if "keys" in run:
        return "session"
    return "skater" if "skater" in run or "look" in run else "level"


def scenario_spec(run: dict, sha256: str) -> str:
    """A level run's OT_SCENARIO file (standalone_scenario.cpp reads it; tony standalone warehouse writes the same facts but skater and look)."""
    lines = [f"level {run['level']}", f"frames {run['frames']}", f"sha256 {sha256}"]
    if "skater" in run:
        lines.append(f"skater {run['skater']}")
    if "look" in run:
        lines.append(f"look {run['look']}")
    lines += [f"action {a}" for a in run.get("actions", [])]
    return "\n".join(lines) + "\n"


def run_one(run: dict, env_extra: dict[str, str], out: Path) -> tuple[bool, float, str]:
    """One run, in this process (OPENTONY_WORKER, if set, isolates it): (finished, seconds, the stop line)."""
    from tony import capture
    from tony import standalone_boot as sb
    from tony import standalone_warehouse as sw

    dump = out / "runs" / run["name"]
    shutil.rmtree(dump, ignore_errors=True)
    dump.mkdir(parents=True)
    extra = {
        "OT_TEXTURE_DUMP": sw._wine_z_path(dump),
        "OT_TEXTURE_DUMP_TAG": run["name"],
        "OT_PSX_TEXTURES": "1" if run["textures"] == "psx" else "0",
        **{key: str(value) for key, value in env_extra.items()},
    }
    if "unlock" in run:   # the run's own OT_UNLOCK (the Skate Shop runs own every deck)
        extra["OT_UNLOCK"] = str(run["unlock"])
    if "keys" in run:
        document = {"name": f"tc-{run['name']}", "deliveries": run["deliveries"], "every": 2, "keys": run["keys"], "masks": []}
    else:
        spec = dump / "scenario.txt"
        cap = dump / "census.otcap"
        spec.write_text(scenario_spec(run, capture.SUPPORTED_BUILD_SHA256))
        extra["OT_SCENARIO"] = sw._wine_z_path(spec)
        extra["OT_SCENARIO_OUT"] = sw._wine_z_path(cap)
        if run["frames"] > 1200:   # the capture buffer holds about 2,500 frames by default; T1's 2,400-frame runs asked for 200 MB
            extra["OT_SCENARIO_MAPPING_MB"] = str(run["frames"] // 12 + 16)
        document = {"name": f"tc-{run['name']}", "deliveries": 4000 + 8 * run["frames"], "every": 2, "keys": [], "masks": []}
    start = time.time()
    finished, text = sb.run_standalone(f"tc-{run['name']}", document, extra=extra)
    seconds = time.time() - start
    (out / "runs" / f"{run['name']}.txt").write_text(text)
    (dump / "census.otcap").unlink(missing_ok=True)
    stop = sw.fatal_line(text)
    skater = next((line for line in text.splitlines() if line.startswith("scenario: skater ")), "")
    if finished and (dump / "index.tsv").exists():
        (dump / "done.txt").write_text(f"{seconds:.1f}\n{skater}\n")
    return finished, seconds, stop or skater


def _child(run_name: str, recipe: Path, out: Path) -> int:
    defaults, runs = load_recipe(recipe)
    run = next(r for r in runs if r["name"] == run_name)
    finished, seconds, stop = run_one(run, defaults["env"], out)
    print(f"{run_name}: finished={finished} {seconds:.0f} s {stop}", flush=True)
    return 0 if finished else 1


def run_all(runs: list[dict], recipe: Path, out: Path, jobs: int, first_worker: int = 0) -> None:
    from tony import workers

    todo = [r for r in runs if not (out / "runs" / r["name"] / "done.txt").exists()]
    print(f"{len(runs)} runs, {len(runs) - len(todo)} done already, {len(todo)} to run on {jobs} workers", flush=True)
    free = workers.worker_ids(first_worker + jobs)[first_worker:]
    lock = __import__("threading").Lock()

    from tony import session_scenarios as ss
    from tony import standalone_boot as sb

    def go(run: dict) -> None:
        with lock:
            worker = free.pop(0)
        try:
            env = workers.worker_environ(worker)
            command = [sys.executable, str(Path(__file__).resolve()), str(out), "--recipe", str(recipe), "--child", run["name"]]
            # Some session runs write session.done and then never exit (boot-menu, two players: seen with the dump on, 11-17). The dump is complete once
            # session.done is there, so the worker's Wine is ended 15 s later; the child then returns as usual.
            marker = ss.run_dir(sb.standalone_label(f"tc-{run['name']}")) / "session.done"
            marker.unlink(missing_ok=True)
            child = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT)
            started, done_at = time.time(), None
            while child.poll() is None:
                time.sleep(1)
                if done_at is None and marker.exists():
                    done_at = time.time()
                if (done_at is not None and time.time() - done_at > 15) or time.time() - started > 1200:
                    workers.kill_prefix(worker)
                    done_at = float("inf")
            stdout, stderr = child.communicate()
            line = (stdout.strip().splitlines() or [f"{run['name']}: no output: {stderr.strip()[-300:]}"])[-1]
            print(f"[{worker}] {line}", flush=True)
        finally:
            workers.kill_prefix(worker)
            with lock:
                free.append(worker)

    with ThreadPoolExecutor(jobs) as pool:
        list(pool.map(go, todo))


def merge(runs: list[dict], out: Path) -> None:
    rows: dict[str, list] = {}
    seen_lines: list[str] = []
    summary = ["# name\tkind\ttextures\tlevel\tskater\tlook\tfinished\tseconds\tkeys\tnew\tskater_line\tundrawn"]
    for run in runs:
        dump = out / "runs" / run["name"]
        done = dump / "done.txt"
        index = dump / "index.tsv"
        finished = done.exists()
        keys_here: set[str] = set()
        new = 0
        if index.exists():
            for line in index.read_text().splitlines():
                if not line or line[0] == "#":
                    continue
                cols = line.split("\t")
                key = cols[0]
                keys_here.add(key)
                if key not in rows:
                    new += 1
                    rows[key] = [cols[1], cols[2], cols[3], cols[4], USE[cols[5]], WRAP[cols[6]], MOD[cols[7]], cols[8] == "1", run["name"]]
                    target = out / f"{key}.png"
                    if not target.exists():
                        shutil.copyfile(dump / f"{key}.png", target)
                else:
                    row = rows[key]
                    row[4] |= USE[cols[5]]
                    row[5] |= WRAP[cols[6]]
                    row[6] |= MOD[cols[7]]
                    row[7] = row[7] or cols[8] == "1"
        if (dump / "seen.tsv").exists():
            seen_lines += [line for line in (dump / "seen.tsv").read_text().splitlines() if line]
        info = done.read_text().splitlines() if finished else ["", ""]
        summary.append("\t".join(str(v) for v in (run["name"], kind_of(run), run["textures"], run.get("level", ""), run.get("skater", ""), run.get("look", ""),
                                                   int(finished), info[0], len(keys_here), new, info[1] if len(info) > 1 else "", undrawn_of(run))))
    header = "# key\twidth\theight\tformat\talpha\tuse\twrap\tmod2x\tvolatile\ttag"
    body = [f"{k}\t{r[0]}\t{r[1]}\t{r[2]}\t{r[3]}\t{name_of(r[4], USE)}\t{name_of(r[5], WRAP)}\t{name_of(r[6], MOD)}\t{int(r[7])}\t{r[8]}" for k, r in sorted(rows.items())]
    (out / "index.tsv").write_text("\n".join([header, *body]) + "\n")
    (out / "seen.tsv").write_text("\n".join(seen_lines) + "\n")
    (out / "runs.tsv").write_text("\n".join(summary) + "\n")
    print(f"merged {len(runs)} runs: {len(rows)} keys -> {out}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out", type=Path)
    parser.add_argument("--recipe", type=Path, default=DEFAULT_RECIPE)
    parser.add_argument("--jobs", type=int, default=0, help="Wine workers (default: the gates' count, min(cores / 2, RAM / 1.5 GB))")
    parser.add_argument("--only", default="", help="only the runs whose name matches this regular expression (the merge still takes every finished run)")
    parser.add_argument("--merge-only", action="store_true")
    parser.add_argument("--first-worker", type=int, default=0, help="the first Wine worker id (another census or the gates may hold the low ones)")
    parser.add_argument("--child", default="", help=argparse.SUPPRESS)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.child:
        return _child(args.child, args.recipe, out)
    _, runs = load_recipe(args.recipe)
    if not args.merge_only:
        chosen = [r for r in runs if re.search(args.only, r["name"])] if args.only else runs
        jobs = args.jobs
        if jobs <= 0:
            from tony import gates

            jobs = gates.default_jobs()
        start = time.time()
        run_all(chosen, args.recipe, out, jobs, args.first_worker)
        print(f"wall time {time.time() - start:.0f} s", flush=True)
    merge(runs, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
