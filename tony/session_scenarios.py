"""Keyed whole-session scenarios and their parity gate (Phase 5.1s).

A session scenario starts the retail game at boot and drives it with scripted keys
(`OPENTONY_SESSION_KEYS`, docs/HOOKS.md "Session probe"); the in-process probe hashes the writable
image every frame. `tony session parity NAME` runs the variants of `tony hooks parity` (retail twice,
all native, all native under the stress clock, fault control) and compares the traces block by block.
Definitions live in `re/scenarios/sessions/*.yml`, one file per scenario.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import struct
import subprocess
import time
from pathlib import Path
from typing import Any

from . import hooks
from .capture import _capture_desktop_spec, _wine_path
from .common import ROOT, headless_wine_command, headless_wine_env, load_yaml, resolve
from .display import HeadlessDisplay, terminate_process
from .nocd import nocd_executable

SESSION_ROOT = ROOT / "re/scenarios/sessions"
RUN_ROOT = Path("build/sessions/runs")
PARITY_ROOT = Path("build/sessions/parity")

# DirectInput scan codes a scenario may name instead of a hex number
KEYS = {
    "esc": 0x01, "enter": 0x1C, "space": 0x39, "up": 0xC8, "down": 0xD0, "left": 0xCB, "right": 0xCD,
    "f9": 0x43, "tab": 0x0F, "backspace": 0x0E,
}

# Cells that hold no game state. The assertion bookkeeping (line, file name), the stack leftovers that
# render_scene.cpp documents as unspecified in retail, and the mouse record's heap pointers.
DEFAULT_MASKS = "568638:4,56881c:40,563a2a:e,563b22:2,563b04:4,6a3f48:8"

# The other cells that hold a Wine-heap address, for the session matrix (retail against native, both in Wine): the five
# wrapper pointers in block 3 (0x0055dce8..0x0055dd5c) and, in block 23, 0x006a0080, the three words at 0x006a04c0 and
# the four at 0x006a051c. Their values follow the heap's base, which moves with the length of the process's environment
# and command line (the repository's path, the invoking shell), so a matrix passed or failed with where the repository
# lives; they were masked only in the sessions that had tripped on them. A heap moved on purpose (the same retail run
# started by its Z:\ path instead of D:\) changes these cells and nothing else in the five sessions. Not in DEFAULT_MASKS:
# the standalone comparison adds its own to those, and the recorders hold 32 (`kMaxMasks`).
HEAP_ADDRESS_MASKS = "55dce8:4,55dd0c:8,55dd28:4,55dd5c:4,6a0080:4,6a04c0:c,6a051c:10,6a7674:8"

# Blocks of process-identity cells (the DirectInput/DirectDraw wrapper pointers, the CRT heap handles);
# any Kit hook shifts the Wine heap, so these differ by address once native code has run.
PROCESS_IDENTITY_BLOCKS = {24, 587}

# 7a: the Wine heap moves with the length of the environment block, 32 bytes per step of it, and so do the cells
# that keep heap addresses (five pointers at 0x0055dce8..0x0055dd5c in block 3, two in block 23, the wrapper
# pointers of blocks 24 and 587; `build/s3/exp_pad.py` swept the length and block 3's hash was a step function of it).
# The runs of one matrix differ in four variables: the hooks selection (`none`, `all`, `fault:NAME`), the run
# directory (the label), the stress switch (present or not) and the dump delivery. Before 7a nothing equalised
# them, so a matrix passed or failed by where the steps fell relative to the invoking shell's own environment.
# `pad_environment` makes the four add up to one fixed size.
ENV_PAD = "OPENTONY_ENV_PAD"
ENV_VARYING = ("OPENTONY_HOOKS", "OPENTONY_LOG_DIR", "OPENTONY_HOOK_STRESS", "OPENTONY_SESSION_DUMP")
ENV_VARYING_BYTES = 360  # the four variables and the pad together, in characters with each entry's `=` and NUL


def pad_environment(env: dict[str, str]) -> None:
    used = sum(len(name) + len(env[name]) + 2 for name in ENV_VARYING if name in env)
    fill = ENV_VARYING_BYTES - used - len(ENV_PAD) - 2
    if fill < 0:
        raise SessionError(f"the varying environment is {used} characters, more than the {ENV_VARYING_BYTES} the pad allows")
    env[ENV_PAD] = "x" * fill


class SessionError(RuntimeError):
    pass


def scenario_files() -> list[Path]:
    return sorted(SESSION_ROOT.glob("*.yml"))


def validate_session(document: Any, path: str = "session") -> list[str]:
    errors: list[str] = []
    if not isinstance(document, dict):
        return [f"{path}: must be a mapping"]
    if document.get("version") != 1:
        errors.append(f"{path}: version must be 1")
    if not isinstance(document.get("description"), str):
        errors.append(f"{path}: description must be a string")
    if not isinstance(document.get("deliveries"), int) or document["deliveries"] <= 0:
        errors.append(f"{path}: deliveries must be a positive integer")
    keys = document.get("keys")
    if not isinstance(keys, list):
        return errors + [f"{path}: keys must be a list"]
    last = -1
    for index, event in enumerate(keys):
        where = f"{path}: keys[{index}]"
        if not isinstance(event, dict) or set(event) != {"poll", "key", "hold"}:
            errors.append(f"{where}: needs exactly poll, key and hold")
            continue
        if not isinstance(event["poll"], int) or event["poll"] < last:
            errors.append(f"{where}: poll must be an integer, not before the previous key")
        else:
            last = event["poll"]
        if not isinstance(event["hold"], int) or event["hold"] < 1:
            errors.append(f"{where}: hold must be a positive integer")
        if event["key"] not in KEYS and not (isinstance(event["key"], int) and 0 < event["key"] < 256):
            errors.append(f"{where}: unknown key {event['key']!r}")
    masks = document.get("masks", [])
    if not isinstance(masks, list) or not all(isinstance(m, str) and re.fullmatch(r"[0-9a-f]+:[0-9a-f]+", m) for m in masks):
        errors.append(f"{path}: masks must be a list of 'hexaddress:hexlength' strings")
    return errors


def load_session(name: str) -> dict[str, Any]:
    path = SESSION_ROOT / f"{name}.yml"
    if not path.is_file():
        raise SessionError(f"no session scenario {name!r} in {SESSION_ROOT}")
    document = load_yaml(path)
    errors = validate_session(document, path.name)
    if errors:
        raise SessionError("; ".join(errors))
    document["name"] = name
    return document


def keys_spec(document: dict[str, Any]) -> str:
    """The `OPENTONY_SESSION_KEYS` value of a scenario."""
    return ",".join(
        f"{event['poll']}:{KEYS.get(event['key'], event['key']):x}:{event['hold']}" for event in document["keys"]
    )


def run_dir(label: str) -> Path:
    return resolve(RUN_ROOT) / label


def session_run(label: str, document: dict[str, Any], variant: hooks.ParityVariant | None = None, *,
                clock: str = "lockstep-fast", timeout: float = 240.0, dump: int = 0,
                hooks_spec: str | None = None, extra_env: dict[str, str] | None = None) -> bool:
    """Run one keyed session; True when the probe finished (`session.done`).

    `extra_env` adds variables after the padding of the parity matrix (7b: the function-level oracle's script); a run that passes none has the environment it always had."""
    variant = variant or hooks.ParityVariant("retail", "none")
    out = run_dir(label)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    directory = hooks.default_game_dir()
    if not hooks.staged_current(directory):
        hooks.stage(directory)

    env = headless_wine_env(None)
    spec = hooks_spec if hooks_spec is not None else variant.hooks
    if spec != "ini":  # "ini": the selection comes from opentony.ini, the environment would override it
        env["OPENTONY_HOOKS"] = spec
    env.update(
        OPENTONY_CLOCK=clock,
        OPENTONY_LOG_DIR=hooks._wine_z_path(out),
        OPENTONY_SESSION_TRACE=str(document["deliveries"]),
        OPENTONY_SESSION_EVERY=str(document.get("every", 2)),
        OPENTONY_SESSION_SKIP_MOVIES="1",
        OPENTONY_SESSION_MASK=os.environ.get("OPENTONY_SESSION_MASK") or ",".join(
            [DEFAULT_MASKS, HEAP_ADDRESS_MASKS, *document.get("masks", [])]),
        WINEDLLOVERRIDES=hooks._merge_overrides(env.get("WINEDLLOVERRIDES"), "dinput=n,b"),
    )
    spec_keys = keys_spec(document)
    if spec_keys:
        env["OPENTONY_SESSION_KEYS"] = spec_keys
    if variant.stress_ms:
        env["OPENTONY_HOOK_STRESS"] = str(variant.stress_ms)
    # always set, so that a run that dumps the image has the same environment (and heap layout) as one that does not
    env["OPENTONY_SESSION_DUMP"] = str(dump or 99999999)
    pad_environment(env)
    if extra_env:
        env.update(extra_env)

    exe = nocd_executable()
    cfg = load_yaml("re/config/wine.yml")["wine"]
    display = HeadlessDisplay(cfg, env)
    # the wrapper of tony.capture, but it ends when the probe writes session.done and has no 130 s cap
    script = (
        'desktop="$1"; output="$2"; limit="$3"; shift 3; '
        'if [ "$1" = wine ]; then shift; fi; '
        'wine explorer "$desktop" "$@" & desktop_pid=$!; ticks=0; '
        'while [ ! -e "$output" ] && kill -0 "$desktop_pid" 2>/dev/null; do '
        'sleep 0.1; ticks=$((ticks + 1)); '
        '[ "$ticks" -lt "$limit" ] || { kill "$desktop_pid" 2>/dev/null || true; exit 124; }; done; '
        'kill "$desktop_pid" 2>/dev/null || true; wait "$desktop_pid" 2>/dev/null || true; exit 0'
    )
    done = out / "session.done"
    command = ["sh", "-c", script, "session-run", _capture_desktop_spec(), str(done), str(int(timeout * 10)),
               "wine", _wine_path(exe, env)]
    process = None
    try:
        process = display.popen(headless_wine_command(command), cwd=exe.parent, env=env)
        process.wait(timeout=timeout + 30)
    except subprocess.TimeoutExpired:
        pass
    finally:
        if process is not None and process.poll() is None:
            terminate_process(process)
        display.close()
        subprocess.run(["wineserver", "-k"], env=env, check=False, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    return done.exists()


def load_trace(label: str):
    """(block size, deliveries per record, regions, [(delivery, poll, block hashes)])."""
    data = (run_dir(label) / "session.trace").read_bytes()
    if data[:8] != b"OTSESS1\0":
        raise SessionError(f"{label}: bad session.trace magic")
    block_size, every, nblocks, nregions = struct.unpack_from("<4I", data, 8)
    regions = [struct.unpack_from("<2I", data, 24 + 8 * i) for i in range(nregions)]
    offset = 24 + 8 * nregions
    stride = (2 + nblocks) * 4
    records = []
    while offset + stride <= len(data):
        words = struct.unpack_from(f"<{2 + nblocks}I", data, offset)
        records.append((words[0], words[1], words[2:]))
        offset += stride
    return block_size, every, regions, records


def block_address(index: int, block_size: int, regions) -> int:
    for base, size in regions:
        count = (size + block_size - 1) // block_size
        if index < count:
            return base + index * block_size
        index -= count
    return 0


def blocks_differing(a, b) -> dict[int, list[int]]:
    """block -> the records where it differs."""
    out: dict[int, list[int]] = {}
    for k in range(min(len(a[3]), len(b[3]))):
        for i, (x, y) in enumerate(zip(a[3][k][2], b[3][k][2], strict=True)):
            if x != y:
                out.setdefault(i, []).append(k)
    return out


def hook_stats(label: str) -> dict[str, int]:
    path = run_dir(label) / "opentony.stats"
    if not path.is_file():
        return {}
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"^hook (\S+) .*? native_calls=(\d+)", path.read_text(), re.MULTILINE)}


def session_variants() -> list[hooks.ParityVariant]:
    variants = hooks.default_parity_variants()
    # The stress clock is a thread, which shifts the Wine heap even with every hook retail, so the
    # stress run is compared with a retail run that has it too.
    variants.insert(2, hooks.ParityVariant("retail-stress", "none", stress_ms=3))
    # the shipped selection: every row that is not `default: "off"` (5.1t)
    variants.insert(4, hooks.ParityVariant("native-default", "default"))
    return variants


LABEL_WIDTH = 24


def session_label(name: str, variant: hooks.ParityVariant) -> str:
    """The run directory of a variant. Fixed width: the directory is the game's log directory, so its path is converted to
    a wide string on the Wine heap for every file the probe opens, and a longer name moves the heap (7a)."""

    return f"{name}-{variant.label.split(':')[0][:LABEL_WIDTH].ljust(LABEL_WIDTH, '_')}"


def session_variant_run(name: str, variant: hooks.ParityVariant, *, attempts: int = 2) -> bool:
    """One run of the matrix (the unit of work of a gate run); True when the probe finished. What the
    comparison needs stays in the run directory."""

    document = load_session(name)
    return any(session_run(session_label(name, variant), document, variant) for _ in range(attempts))


def session_compare(name: str) -> tuple[list[dict[str, Any]], bool]:
    """Compare the runs of `name` that are on disk (`session_variant_run`) and write `parity.json`."""

    off = {h["name"] for h in hooks.load_manifest()["hooks"] if str(h.get("default", "")).lower() == "off"}
    rows: list[dict[str, Any]] = []
    traces: dict[str, Any] = {}
    noise: set[int] = set()
    for variant in session_variants():
        label = session_label(name, variant)
        finished = (run_dir(label) / "session.done").exists()
        row: dict[str, Any] = {"variant": variant.label, "hooks": variant.hooks}
        if not finished:
            row.update(result="ERROR", ok=False, detail="no session.done (crash or hang)")
            rows.append(row)
            continue
        traces[variant.label] = load_trace(label)
        per_hook = hook_stats(label)
        reached = sorted(h for h, c in per_hook.items() if c)
        row.update(native_calls=sum(per_hook.values()), hooks_reached=reached,
                   off_reached=sorted(h for h in reached if h in off), records=len(traces[variant.label][3]))
        reference = traces.get("retail-stress" if variant.label.endswith("stress") else "retail-a")
        if variant.label == "retail-a":
            row.update(result="REFERENCE", ok=True, detail="")
        elif reference is None:
            row.update(result="ERROR", ok=False, detail="no reference run")
        elif variant.label == "retail-stress":
            row.update(result="REFERENCE", ok=True, detail="stress clock, every hook retail")
        else:
            diff = blocks_differing(reference, traces[variant.label])
            same_length = len(reference[3]) == len(traces[variant.label][3])
            if variant.label == "retail-b":
                noise = set(diff) | PROCESS_IDENTITY_BLOCKS
                row.update(result="MATCH" if same_length else "DIVERGE", ok=same_length,
                           detail=f"{len(noise)} masked blocks {sorted(noise)[:8]}")
            else:
                real = {b: r for b, r in diff.items() if b not in noise}
                equal = not real and same_length
                detail = ""
                if not equal and real:
                    first = min(real.items(), key=lambda kv: kv[1][0])
                    base = block_address(first[0], reference[0], reference[2])
                    detail = f"{len(real)} blocks differ, first block {first[0]} @ {base:08x} record {first[1][0]}"
                elif not equal:
                    detail = f"records {len(reference[3])} vs {len(traces[variant.label][3])}"
                expect_equal = variant.expect_equal
                faulted = variant.hooks.partition(":")[2] if variant.hooks.startswith("fault:") else ""
                if faulted and not per_hook.get(faulted):
                    # 7a: a fault in a hook the session never runs cannot change anything. Before the environment was
                    # padded these runs "diverged" anyway (the longer `fault:NAME` moved the Wine heap), which was the
                    # artifact, not a control. The run is held to the rule of the real variants and says so.
                    expect_equal = True
                    detail = (detail + "; " if detail else "") + f"control not exercised: {faulted} has no native call in this session"
                row.update(result="MATCH" if equal else "DIVERGE", ok=equal == expect_equal, detail=detail)
        rows.append(row)
    out = resolve(PARITY_ROOT) / name
    out.mkdir(parents=True, exist_ok=True)
    (out / "parity.json").write_text(json.dumps(rows, indent=2) + "\n")
    return rows, all(row["ok"] for row in rows)


def session_parity(name: str, *, attempts: int = 2) -> tuple[list[dict[str, Any]], bool]:
    """The serial gate: every run of the matrix in turn, then the comparison (`tony gates` runs them side by side)."""

    load_session(name)
    for variant in session_variants():
        session_variant_run(name, variant, attempts=attempts)
    return session_compare(name)


def session_list(_args=None) -> int:
    for path in scenario_files():
        document = load_yaml(path)
        errors = validate_session(document, path.name)
        status = "INVALID " + "; ".join(errors) if errors else f"{document['deliveries']} deliveries, {len(document['keys'])} keys"
        print(f"{path.stem:<28} {status}  {document.get('description', '')}")
    return 0


def session_run_command(args) -> int:
    document = load_session(args.name)
    label = args.label or f"{args.name}-run"
    started = time.time()
    finished = session_run(label, document, hooks.ParityVariant("run", args.hooks), clock=args.clock,
                           dump=args.dump)
    print(f"{label}: {'done' if finished else 'FAILED (no session.done)'} in {time.time() - started:.0f}s, "
          f"trace {run_dir(label) / 'session.trace'}")
    return 0 if finished else 1


def print_session_rows(name: str, rows: list[dict[str, Any]]) -> None:
    print(f"\n{name}")
    print(f"{'Variant':<24} {'Hooks':<26} {'Result':<10} {'OK':<4} {'native calls':>13} {'hooks':>6} {'off rows':>9}")
    for row in rows:
        print(f"{row['variant']:<24} {row['hooks']:<26} {row['result']:<10} {'yes' if row['ok'] else 'NO':<4} "
              f"{row.get('native_calls', 0):>13} {len(row.get('hooks_reached', [])):>6} "
              f"{len(row.get('off_reached', [])):>9}")
        if row["detail"]:
            print(f"    {row['detail']}")
    native = next((row for row in rows if row["variant"] == "native-all"), None)
    if native and native.get("off_reached"):
        print("default-off rows reached: " + ", ".join(native["off_reached"]))


def session_parity_command(args) -> int:
    failed = False
    for name in args.names:
        try:
            rows, ok = session_parity(name, attempts=args.attempts)
        except SessionError as exc:
            print(f"FAIL {name}: {exc}")
            failed = True
            continue
        print_session_rows(name, rows)
        failed = failed or not ok
    return 1 if failed else 0


def register(subparsers) -> None:
    session = subparsers.add_parser("session", help="keyed whole-session scenarios and their parity gate (5.1s)")
    sub = session.add_subparsers(dest="session_command", required=True)
    p = sub.add_parser("list", help="list the scenarios of re/scenarios/sessions/")
    p.set_defaults(func=session_list)
    p = sub.add_parser("run", help="run one scenario once and keep its session.trace")
    p.add_argument("name")
    p.add_argument("--hooks", default="none", help="OPENTONY_HOOKS selection (default: none, all retail)")
    p.add_argument("--clock", default="lockstep-fast", choices=("lockstep", "lockstep-fast"))
    p.add_argument("--label", help=f"run directory name under {RUN_ROOT}")
    p.add_argument("--dump", type=int, default=0, help="also write the image at this delivery (session_<N>.img)")
    p.set_defaults(func=session_run_command)
    p = sub.add_parser("parity", help="retail twice, all native, all native under stress, fault control; compare the image")
    p.add_argument("names", nargs="+")
    p.add_argument("--attempts", type=int, default=2, help="retries per run (Wine startup is flaky)")
    p.set_defaults(func=session_parity_command)
