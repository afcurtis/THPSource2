"""The Warehouse recordings in the standalone (milestone 10f, PHASE_5_SUMMARY.md).

`tony hooks parity` runs the nine Warehouse scenarios of `re/scenarios` in the retail process; this module runs one of them in the standalone and compares the two:

    python -m tony.standalone_warehouse [NAME...] [--keep-retail]        (or `tony standalone warehouse`)

Retail: the in-process capture (docs/CAPTURE_INPROC.md; hooks off, the lockstep clock) records the scenario's physics frames, and the session probe, loaded beside it,
hashes `.data` every second delivery (`session.trace`, as `tony standalone parity` has it). Standalone: `opentony_spike.exe` cold-boots, `standalone_scenario.cpp` does what
the capture DLL does at the retail code (the frontend bootstrap, the recorded input, the physics-frame recorder) and writes the same `.otcap`, while the session recorder
writes its own `session.trace`. Two comparisons, over what both runs reached:

* the frames: each recorded physics frame's input, player before and after, and timing snapshot (`compare_recordings`' "snapshots" scope), the first differing field named;
* the state blocks: every 64 KB block of `.data` at every record both runs made, outside the process-identity blocks and the masks of `tony.standalone_boot`, with the
  deliveries and polls compared as `tony standalone parity` does. A divergence is found by its first record and the caller of its writes (PHASE_5_SUMMARY.md, "Finding a
  divergence's caller").

The standalone ends on a FATAL (the first native or seam it has none of) or when the scenario's frames are recorded; both are results, so the comparison covers the common
prefix and the stop is named: `status` is "match" (every frame and every common record equal, the standalone recorded all of them), "stopped" (equal as far as it went, then a
FATAL: `detail` has the line) or "diverge" (something differs).
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import sys
from pathlib import Path
from typing import Any

from . import capture
from . import session_scenarios as ss
from . import standalone_boot as sb
from .common import resolve
from .hooks import _wine_z_path, hooks_environment, runtime_dir
from .scenarios import load_scenario, scenario_capture, scenario_files

OUT = Path("build/standalone-warehouse")

# The Warehouse scenarios whose recording the standalone matches (10g s25, 10h s1-s2). `tony gates full` runs `tony standalone warehouse` on each; a name is added here when its scenario MATCHes.
GATED_WAREHOUSE = ("warehouse-idle", "warehouse-rail", "warehouse-combo", "warehouse-manual", "warehouse-ollie-land",
                    "warehouse-spin", "warehouse-straight", "warehouse-turn", "warehouse-turn-ollie")

FRAME_FIELDS_SHOWN = 6   # differing frames listed in a report


def deliveries_for(scenario: dict[str, Any]) -> int:
    """A limit no run reaches: the retail load is ~500 deliveries and a frame a few more; the standalone stops on its own at the last frame."""

    return 4000 + 8 * int(scenario["frames"])


def scenario_spec(scenario: dict[str, Any]) -> str:
    """The text `OT_SCENARIO` reads (src/hooks/kits/standalone_scenario.cpp)."""

    lines = [f"level {capture_level(scenario)}", f"frames {scenario['frames']}", f"sha256 {capture.SUPPORTED_BUILD_SHA256}"]
    for action, start, hold in capture._scenario_action_intervals(scenario):
        lines.append(f"action 0x{capture.ACTION_MASKS[action]:x} {start} {hold}")
    return "\n".join(lines) + "\n"


def capture_level(scenario: dict[str, Any]) -> int:
    from .levels import LEVEL_NAMES

    return LEVEL_NAMES.get(scenario["level"], 0)


def paths(name: str) -> dict[str, Path]:
    base = resolve(OUT) / name
    own = base / "x64" if sb.ARCH == 64 else base   # 11b: the 64-bit build's outputs beside the 32-bit ones; the retail recording is shared
    return {"dir": own, "retail": base / "retail.otrec", "standalone": own / "standalone.otcap", "spec": own / "scenario.txt", "result": own / "result.json"}


def document_for(name: str, scenario: dict[str, Any]) -> dict[str, Any]:
    """The session document `tony.standalone_boot` runs a cold boot from: no keys (the scenario drives the frontend), a delivery limit nothing reaches."""

    return {"name": name, "deliveries": deliveries_for(scenario), "every": 2, "keys": [], "masks": []}


def run_retail(name: str, scenario: dict[str, Any], dump: str = "") -> bool:
    """The retail capture of the scenario with the session probe beside it: `retail.otrec` and the run directory's `session.trace`.

    The probe's movie skip stays off: the capture DLL skips the same two entry points and checks their bytes first, so a patched entry would fail its install."""

    p = paths(name)
    p["dir"].mkdir(parents=True, exist_ok=True)
    label = sb.retail_label(name)
    out = ss.run_dir(label)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    saved = {key: os.environ.get(key) for key in ("OPENTONY_SESSION_TRACE", "OPENTONY_SESSION_EVERY", "OPENTONY_SESSION_MASK", "OPENTONY_SESSION_DUMP")}
    try:
        with hooks_environment("none", "lockstep-fast"):
            os.environ["OPENTONY_SESSION_TRACE"] = str(deliveries_for(scenario))
            os.environ["OPENTONY_SESSION_EVERY"] = "2"
            os.environ["OPENTONY_SESSION_MASK"] = sb.masks({})
            if dump:
                os.environ["OPENTONY_SESSION_DUMP"] = dump
            code = scenario_capture(argparse.Namespace(name=name, output=str(p["retail"]), frames=None, backend="inproc", force=True))
            trace = runtime_dir() / "session.trace"
            if trace.is_file():
                shutil.copyfile(trace, out / "session.trace")
            for image in runtime_dir().glob("session_*.*"):   # the dumps of --dump: build/s9/ddiff.py reads them
                shutil.copyfile(image, out / image.name)
    finally:
        for key, value in saved.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
    return code == 0 and (out / "session.trace").is_file()


def zone_block(name: str) -> int | None:
    """Where the retail run has the game's memory zones: its image at delivery 2 holds the zone bounds (Mem_Init, 0x0056a97c). The capture host starts the game another way
    than the session runs do, so its Wine heap puts the block elsewhere (0x091e0020, not 0x04f00020), and the state blocks hash a word inside the zones as an offset."""

    image = ss.run_dir(sb.retail_label(name)) / "session_2.img"
    if not image.is_file():
        return None
    with image.open("rb") as handle:
        handle.seek(0x0056a97c - 0x00524000)
        low = struct.unpack("<I", handle.read(4))[0]
    return low or None


def dump_spec(dump: str) -> str:
    """Delivery 2 is always dumped (the zone block is read from it); a requested one follows."""

    return "2" if not dump or dump == "2" else f"2,{dump.split(',')[0]}"


def run_standalone(name: str, scenario: dict[str, Any], dump: str = "") -> tuple[bool, str]:
    """The standalone on the scenario: (finished, the run's output). `finished` is session.done, which the recorder writes after the last frame."""

    p = paths(name)
    p["dir"].mkdir(parents=True, exist_ok=True)
    p["spec"].write_text(scenario_spec(scenario))
    p["standalone"].unlink(missing_ok=True)
    extra = {"OT_SCENARIO": _wine_z_path(p["spec"]), "OT_SCENARIO_OUT": _wine_z_path(p["standalone"])}
    block = zone_block(name)
    if block is not None:
        extra["OT_ZONE_BLOCK"] = f"{block:x}"
    # the level's music: retail's capture opens it once its movie skip retires (the first gameplay frame), and so does the standalone, through the FFmpeg
    # DLLs of `tony setup ffmpeg` (the number of frame ticks the track change takes depends on the open)
    from . import ffmpeg
    from .nocd import nocd_executable
    target = "x86_64" if sb.ARCH == 64 else "i686"   # a 64-bit process loads the 64-bit FFmpeg DLLs (`tony setup ffmpeg --arch 64`)
    if (ffmpeg.install_dir(target) / "bin" / "avutil-60.dll").is_file():
        extra["OPENTONY_FFMPEG_DIR"] = _wine_z_path(ffmpeg.install_dir(target) / "bin")
        extra["OPENTONY_MEDIA_DIR"] = _wine_z_path(nocd_executable().parent.parent)   # the install root: data\Music and med\Movies under it
    return sb.run_standalone(name, document_for(name, scenario), dump=dump, extra=extra)


def fatal_line(text: str) -> str:
    """The run's stop: the FATAL line, or the host's own complaint."""

    for line in text.splitlines():
        if "FATAL" in line:
            return line.strip()
    return ""


# ---------------------------------------------------------------- the frames

def standalone_frames(path: Path, directory: Path) -> tuple[int, list[dict[str, Any]], str]:
    """(frames recorded, their records, a decode complaint): a stopped run's capture is partial, so a copy that says it is complete at the count it has is decoded."""

    if not path.is_file():
        return 0, [], "no capture was written"
    data = bytearray(path.read_bytes())
    fields = list(capture.HEADER_STRUCT.unpack_from(data))
    # fields: magic, version, header_size, config_offset, config_size, initial_offset, initial_size, data_offset, mapping_size, bytes_used, frame_count, frame_limit, status, ...
    count = fields[10]
    if count == 0:
        return 0, [], ""
    fields[11] = count
    fields[12] = capture.STATUS_COMPLETE
    capture.HEADER_STRUCT.pack_into(data, 0, *fields)
    # the config's frame limit follows the header's
    prefix = list(capture.CONFIG_PREFIX_STRUCT.unpack_from(data, fields[3]))
    prefix[2] = count
    capture.CONFIG_PREFIX_STRUCT.pack_into(data, fields[3], *prefix)
    patched = directory / "standalone.partial.otcap"
    patched.write_bytes(bytes(data))
    out = directory / "standalone.otrec"
    try:
        capture.convert_capture(patched, out, force=True)
        return count, [r for r in capture._recording_records(out) if r.get("type") == "frame"], ""
    except capture.CaptureDecodeError as exc:
        return count, [], str(exc)


def compare_frames(retail: Path, frames: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """The frames the standalone recorded against the retail recording's: the first differing field of each differing frame ("snapshots" scope,
    less the never-initialised high half of raw_physics_words[235], which retail fills from whatever its stack held)."""

    def prepared(record: dict[str, Any]) -> dict[str, Any]:
        return capture._without_uninitialized_padding(capture._comparison_frame(record, "snapshots"))

    reference = [prepared(r) for r in capture._recording_records(retail) if r.get("type") == "frame"]
    rows = []
    for index, record in enumerate(frames):
        if index >= len(reference):
            break
        difference = capture._first_difference(reference[index], prepared(record))
        if difference is not None:
            path, left, right = difference
            rows.append({"frame": index, "path": "/".join(str(p) for p in path), "retail": left, "standalone": right})
    return rows


# ---------------------------------------------------------------- the state blocks

def compare_blocks(name: str) -> dict[str, Any]:
    """`.data` blocks over the records both runs made: where each differing block first differs, and whether the deliveries and polls agree."""

    retail = ss.load_trace(sb.retail_label(name))
    standalone = ss.load_trace(sb.standalone_label(name))
    common = min(len(retail[3]), len(standalone[3]))
    clock = [k for k in range(common) if retail[3][k][0] != standalone[3][k][0] or retail[3][k][1] != standalone[3][k][1]]
    first_clock = None
    if clock:
        k = clock[0]
        first_clock = {"record": k, "retail": [retail[3][k][0], retail[3][k][1]], "standalone": [standalone[3][k][0], standalone[3][k][1]]}
    clipped = (retail[0], retail[1], retail[2], retail[3][:common])
    differing = {b: r for b, r in ss.blocks_differing(clipped, (standalone[0], standalone[1], standalone[2], standalone[3][:common])).items() if b not in ss.PROCESS_IDENTITY_BLOCKS}
    rows = [{"block": b, "address": f"{ss.block_address(b, retail[0], retail[2]):08x}", "first_record": r[0], "first_delivery": retail[3][r[0]][0],
             "first_poll": retail[3][r[0]][1], "records": len(r)} for b, r in sorted(differing.items(), key=lambda kv: kv[1][0])]
    return {"records": [len(retail[3]), len(standalone[3])], "common": common, "clock_mismatches": len(clock), "first_clock": first_clock, "blocks": len(retail[3][0][2]) if retail[3] else 0,
            "differing": rows}


# ---------------------------------------------------------------- the command

def warehouse(name: str, *, keep_retail: bool = False, dump: str = "") -> dict[str, Any]:
    scenario = load_scenario(name)
    p = paths(name)
    p["dir"].mkdir(parents=True, exist_ok=True)
    result: dict[str, Any] = {"scenario": name, "frames": scenario["frames"]}
    have_retail = p["retail"].is_file() and (ss.run_dir(sb.retail_label(name)) / "session.trace").is_file()
    if not (keep_retail and have_retail) and not run_retail(name, scenario, dump_spec(dump)):
        result.update(status="failed", detail="the retail capture failed")
    else:
        done, text = run_standalone(name, scenario, dump_spec(dump))
        stop = fatal_line(text)
        trace = ss.run_dir(sb.standalone_label(name)) / "session.trace"
        if not trace.is_file():
            result.update(status="failed", detail="the standalone left no session.trace\n" + "\n".join(text.splitlines()[-15:]))
        else:
            count, frames, complaint = standalone_frames(p["standalone"], p["dir"])
            result["recorded"] = count
            result["frame_differences"] = compare_frames(p["retail"], frames)
            if complaint:
                result["decode"] = complaint
            result["state"] = compare_blocks(name)
            result["stop"] = "recorded all frames" if done else (stop or "no FATAL line (see run.txt)")
            equal = not result["frame_differences"] and not complaint and not result["state"]["differing"] and not result["state"]["clock_mismatches"]
            result["status"] = "match" if equal and done and count == scenario["frames"] else ("diverge" if not equal else "stopped")
    p["result"].write_text(json.dumps(result, indent=2, default=str) + "\n")
    return result


def print_result(result: dict[str, Any]) -> None:
    name = result["scenario"]
    if result["status"] == "failed":
        print(f"{name}: {result['detail']}")
        return
    state = result["state"]
    print(f"{name}: {result['recorded']} of {result['frames']} frames recorded, {len(result['frame_differences'])} differ; stop: {result['stop']}")
    print(f"{name}: records {state['records'][0]} retail / {state['records'][1]} standalone ({state['common']} common), deliveries or polls differ in "
          f"{state['clock_mismatches']}, {len(state['differing'])} of {state['blocks']} blocks differ outside the identity blocks")
    if state["first_clock"]:
        print(f"  first clock difference at record {state['first_clock']['record']}: delivery/poll retail {state['first_clock']['retail']}, standalone {state['first_clock']['standalone']}")
    for row in state["differing"]:
        print(f"  block {row['block']:3d} @ {row['address']}: from record {row['first_record']} (delivery {row['first_delivery']}, poll {row['first_poll']}), {row['records']} records")
    for row in result["frame_differences"][:FRAME_FIELDS_SHOWN]:
        print(f"  frame {row['frame']}: {row['path']}: retail {row['retail']!r} standalone {row['standalone']!r}")
    if result.get("decode"):
        print(f"  the capture did not decode: {result['decode']}")
    print(f"{name}: {result['status'].upper()}")


def warehouse_command(args) -> int:
    """`tony standalone warehouse [NAME...]`: 0 when every scenario matches, 1 when one stopped or diverged, 2 when a run failed."""

    sb.set_arch(args.arch)
    names = args.names or [path.stem for path in scenario_files()]
    results = []
    for name in names:
        result = warehouse(name, keep_retail=args.keep_retail, dump=args.dump)
        print_result(result)
        results.append(result)
    if any(r["status"] == "failed" for r in results):
        return 2
    return 0 if all(r["status"] == "match" for r in results) else 1


def register(sub) -> None:
    p = sub.add_parser("warehouse", help="Warehouse scenarios in the standalone and in retail: the recorded frames and the .data blocks, compared (milestone 10f)")
    p.add_argument("names", nargs="*", help="scenario names of re/scenarios (default: all nine)")
    p.add_argument("--keep-retail", action="store_true", help="reuse the retail reference run when it is there")
    p.add_argument("--dump", default="", help="also dump both images at these deliveries (A or A,B), for build/s9/ddiff.py")
    p.add_argument("--arch", choices=("32", "64"), default="32", help="the 32-bit build (default) or the x86-64 one (milestone 11b)")
    p.set_defaults(func=warehouse_command)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python -m tony.standalone_warehouse", description=__doc__.splitlines()[0])
    parser.add_argument("names", nargs="*")
    parser.add_argument("--keep-retail", action="store_true")
    parser.add_argument("--dump", default="")
    parser.add_argument("--arch", choices=("32", "64"), default="32")
    return warehouse_command(parser.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
