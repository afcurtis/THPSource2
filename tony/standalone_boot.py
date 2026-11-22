"""The cold boot of the standalone against a retail session recording (milestone 9, PHASE_5_SUMMARY.md "9 result").

`opentony_spike.exe` with `OT_SPIKE_CALL=boot` starts from the static data alone (no session image): the C++ initializers, `Game_Startup`'s work and
`main` (`src/hooks/kits/standalone_boot.cpp`), driven by a session scenario of `re/scenarios/sessions` through the standalone's recorder
(`src/hooks/kits/standalone_session.cpp`: the hooked probe's variables, trace format and key injection). This module runs the retail game on the same
scenario (hooks off, the probe) and the standalone, and compares the two `session.trace` files block by block, as `tony session parity` compares two retail
variants: the same deliveries and polls in every record, and every 64 KB block of `.data` equal outside the process-identity blocks (24 and 587, which every
session comparison leaves out) and the masks below.

    python -m tony.standalone_boot [SESSION] [--keep-retail] [--dump A[,B]]

The masks are the scenario's and 7a's (`session_scenarios.DEFAULT_MASKS`) plus STANDALONE_MASKS, applied to both runs (the retail reference is recorded
with them).
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from . import session_scenarios as ss
from . import static_data
from .common import headless_wine_env, resolve
from .hooks import _wine_z_path
from .nocd import nocd_executable

OUT = Path("build/standalone-boot")

# 11b: `--arch 64` runs the x86-64 build (build/hooks/kits64, `tony hooks build --arch 64`) against the same retail reference runs; its runs and results are kept
# apart from the 32-bit build's ("-x64"). Set once by the command, read by everything below and by tony.standalone_warehouse.
ARCH = 32


def set_arch(arch) -> None:
    global ARCH
    ARCH = int(arch)


def kits_dir() -> Path:
    return resolve("build/hooks/kits64" if ARCH == 64 else "build/hooks/kits")


def arch_suffix() -> str:
    return "-x64" if ARCH == 64 else ""

# The session scenarios whose cold boot matches retail (9a). `tony gates full` runs `tony standalone parity` on each; a name is added here when its scenario matches
# (boot-create-skater: milestone 10, once the 3D model draw ran on retail memory; the two careers: 10g s22, once the loading screens drew their text).
GATED_SESSIONS = ("boot-menu", "boot-options", "boot-create-skater", "boot-career-hangar", "boot-career-end-run", "boot-park-editor", "boot-saved-data")
# Sessions only the standalone runs: the hooked build (`tony session parity`, the retail process with natives hooked in) has never walked the park editor, so the hooked gates leave it out
STANDALONE_ONLY_SESSIONS = ("boot-park-editor", "boot-saved-data")

# Words the standalone does not hold as retail does, by class. Each is the same in every retail run; none is game state.
STANDALONE_MASKS = {
    # the C runtime's own state in the executable's .data: the signal-action table (0x0055d920, 0x0055d930), the stdio FILE records (0x0055dee0..), the
    # time-zone names and rules (0x0055e150..). The host's C runtime keeps its own (8c decided the same for two words of 7c's frame)
    "c-runtime": ["55d920:4", "55d930:18", "55dee0:6c", "55e150:b8"],
    # pointers to DirectDraw / Direct3D wrapper objects in the Wine heap; the standalone has no DirectX (the create-skater scenario masks the same cells
    # against the heap shift of 7a)
    "directx-objects": ["55dce8:4", "55dd0c:8", "55dd28:4", "55dd44:4", "55dd5c:4"],
    # pointers into the C runtime heap (the D3D model buffers, the bound texture and its descriptor, the texture-record and palette-cache lists, and 10f: the
    # replay level's record pointer at 0x00562074 once a level has loaded, and 10g s22: the loading screen's check-mark sprite at 0x00562070, the same
    # allocation): 7c's class, there compared as null or not
    "c-runtime-heap": ["6a0070:4", "6a0078:4", "6a0080:4", "6a04c0:4", "6a04c8:4", "6a051c:10", "562074:4", "562070:4"],
    # D3D_SaveScreenshot's next number: the first SCRnnnnn.bmp the game directory does not have yet (F9 takes the scenario's screenshot); a property of the
    # directory, which the standalone does not share (its screenshots are OT_VIDEO_SHOTS)
    "screenshot-number": ["56e844:4"],
    # 10g s25: kTextureClut, the CLUT word of the texture record of the face Shatter_Face last read (0x0056c540). Retail's 8-bit loader (0x00456a70) ORs bits of its caller's
    # stack address into the CLUT words of the records it builds (the shattered pane's record: 8 in retail, 0 here; Open items "Standalone, outside .data"), and Shatter_Face
    # copies that word here. The record itself is zone memory, which no state block covers; this is its one copy inside .data
    "stack-address-clut": ["56c540:4"],
    # 12 s10: kFileName (0x0056dd60), the name TRG_LoadFile was last given: a pointer to the caller's buffer, a stack local in retail (0x0022fc88 in the park editor's level load) and a
    # block of the scratch window here (0x7ff00800). Nothing reads it after the load; it is a dead address, like the CLUT word's stack bits
    "dead-stack-pointer": ["56dd60:4"],
}


def masks(document: dict[str, Any]) -> str:
    extra = [m for group in STANDALONE_MASKS.values() for m in group]
    return ",".join([ss.DEFAULT_MASKS, *document.get("masks", []), *extra])


def retail_label(name: str) -> str:
    return f"{name}-standalone-retail"


def standalone_label(name: str) -> str:
    return f"{name}-standalone{arch_suffix()}"


def clean_saves() -> None:
    """Saves and custom parks a scenario made in the game folder: each run starts without them (retail and the standalone share the folder, and the main menu reads the card)."""

    folder = nocd_executable().parent
    for pattern in ("*.SAV", "*.PRK"):
        for path in folder.glob(pattern):
            path.unlink()


def run_retail(name: str, document: dict[str, Any], dump: str = "") -> bool:
    import os

    clean_saves()

    previous = os.environ.get("OPENTONY_SESSION_MASK")
    os.environ["OPENTONY_SESSION_MASK"] = masks(document)
    try:
        return ss.session_run(retail_label(name), document, hooks_spec="none", dump=dump or 0)
    finally:
        if previous is None:
            os.environ.pop("OPENTONY_SESSION_MASK", None)
        else:
            os.environ["OPENTONY_SESSION_MASK"] = previous


def run_standalone(name: str, document: dict[str, Any], dump: str = "", extra: dict[str, str] | None = None) -> tuple[bool, str]:
    """The cold boot on the scenario, from retail's working directory (the game directory: its paths are in .data); output in the run directory."""

    clean_saves()
    out = ss.run_dir(standalone_label(name))
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    env = headless_wine_env()
    env.update({
        "WINEDEBUG": "-all",
        "OT_SPIKE_CALL": "boot",
        # a Windows path: Wine takes a Unix path with no drive as one on the current drive (here D:) and then does not match the case of every directory, so
        # NEWBMP/s2bgd0.bmp (the 640 x 480 main-menu background; the extracted directory is `newbmp`) was not found, which retail's package lookup does
        "OPENTONY_DATA_DIR": _wine_z_path(resolve("build/assets/all-pkr/files")),
        "OPENTONY_LOG_DIR": _wine_z_path(out),
        "OPENTONY_SESSION_TRACE": str(document["deliveries"]),
        "OPENTONY_SESSION_EVERY": str(document.get("every", 2)),
        "OPENTONY_SESSION_SKIP_MOVIES": "1",
        "OPENTONY_SESSION_MASK": masks(document),
        "OPENTONY_SESSION_KEYS": ss.keys_spec(document),
    })
    if dump:
        env["OPENTONY_SESSION_DUMP"] = dump
    env.update(extra or {})
    command = ["wine", str(kits_dir() / "opentony_spike.exe"), str(static_data.ensure_image()),
               str(kits_dir() / "opentony_standalone.dll"), "-", str(resolve("build/s5/empty.tsv"))]
    result = subprocess.run(command, cwd=nocd_executable().parent, env=env, capture_output=True, text=True, timeout=900, check=False)
    text = (result.stdout or "") + (result.stderr or "")
    (out / "run.txt").write_text(text)
    return (out / "session.done").exists(), text


def compare(name: str) -> dict[str, Any]:
    retail = ss.load_trace(retail_label(name))
    standalone = ss.load_trace(standalone_label(name))
    same_shape = len(retail[3]) == len(standalone[3])
    clock = [k for k, (a, b) in enumerate(zip(retail[3], standalone[3])) if a[0] != b[0] or a[1] != b[1]]
    diff = ss.blocks_differing(retail, standalone)
    real = {b: r for b, r in diff.items() if b not in ss.PROCESS_IDENTITY_BLOCKS}
    rows = [{"block": b, "address": f"{ss.block_address(b, retail[0], retail[2]):08x}", "first_record": r[0],
             "first_delivery": retail[3][r[0]][0], "records": len(r)} for b, r in sorted(real.items(), key=lambda kv: kv[1][0])]
    return {"records": [len(retail[3]), len(standalone[3])], "clock_mismatches": len(clock), "blocks": len(retail[3][0][2]) if retail[3] else 0,
            "differing": rows, "match": same_shape and not clock and not real}


def parity(name: str, *, keep_retail: bool = False, dump: str = "") -> dict[str, Any]:
    """Retail and the standalone on one scenario, compared; written to `build/standalone-boot/NAME/result.json`.

    `status` is "match", "diverge" (both runs finished and differ) or "failed" (a run left no session.done; `detail` says which)."""

    document = ss.load_session(name)
    out = resolve(OUT) / f"{name}{arch_suffix()}"
    out.mkdir(parents=True, exist_ok=True)
    result: dict[str, Any]
    if not (keep_retail and (ss.run_dir(retail_label(name)) / "session.done").exists()) and not run_retail(name, document, dump):
        result = {"session": name, "status": "failed", "detail": "retail run failed (no session.done)"}
    else:
        done, text = run_standalone(name, document, dump)
        if done:
            result = compare(name)
            result["session"] = name
            result["status"] = "match" if result["match"] else "diverge"
        else:
            result = {"session": name, "status": "failed",
                      "detail": "standalone run failed (no session.done)\n" + "\n".join(text.splitlines()[-15:])}
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def print_result(result: dict[str, Any]) -> None:
    name = result["session"]
    if result["status"] == "failed":
        print(f"{name}: {result['detail']}")
        return
    print(f"{name}: records {result['records'][0]} / {result['records'][1]}, deliveries or polls differ in {result['clock_mismatches']}, "
          f"{len(result['differing'])} of {result['blocks']} blocks differ outside the identity blocks")
    for row in result["differing"]:
        print(f"  block {row['block']:3d} @ {row['address']}: from record {row['first_record']} (delivery {row['first_delivery']}), {row['records']} records")
    print(f"{name}: {result['status'].upper()}")


def parity_command(args) -> int:
    """`tony standalone parity [NAME...]`: 0 when every session matches, 1 when one diverges, 2 when a run failed."""

    set_arch(args.arch)
    names = args.names or [path.stem for path in ss.scenario_files()]
    results = []
    for name in names:
        result = parity(name, keep_retail=args.keep_retail, dump=args.dump)
        print_result(result)
        results.append(result)
    if any(r["status"] == "failed" for r in results):
        return 2
    return 0 if all(r["status"] == "match" for r in results) else 1


def register(subparsers) -> None:
    standalone = subparsers.add_parser("standalone", help="the standalone executable against retail (milestones 9 and 9a)")
    sub = standalone.add_subparsers(dest="standalone_command", required=True)
    p = sub.add_parser("parity", help="cold-boot session scenarios in the standalone and in retail and compare the traces block by block")
    p.add_argument("names", nargs="*", help="scenario names of re/scenarios/sessions (default: all)")
    p.add_argument("--keep-retail", action="store_true", help="reuse the retail reference run when it is there")
    p.add_argument("--dump", default="", help="also dump both images at these deliveries (A or A,B)")
    p.add_argument("--arch", choices=("32", "64"), default="32", help="the 32-bit build (default) or the x86-64 one (milestone 11b)")
    p.set_defaults(func=parity_command)
    from .standalone_warehouse import register as register_warehouse

    register_warehouse(sub)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python -m tony.standalone_boot", description=__doc__.splitlines()[0])
    parser.add_argument("session", nargs="?", default="boot-menu")
    parser.add_argument("--keep-retail", action="store_true", help="reuse the retail reference run when it is there")
    parser.add_argument("--dump", default="", help="also dump both images at these deliveries (A or A,B)")
    args = parser.parse_args(argv)
    result = parity(args.session, keep_retail=args.keep_retail, dump=args.dump)
    print_result(result)
    return {"match": 0, "diverge": 1}.get(result["status"], 2)


if __name__ == "__main__":
    sys.exit(main())
