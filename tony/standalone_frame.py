"""The first frame of the standalone (milestone 7c, PHASE_5_SUMMARY.md "7c result").

`opentony_spike.exe` runs `Options_Run` on the main-menu session image with the draw path of `src/runtime/draw_kit.*` and the program's video
(`src/hooks/kits/standalone_video.cpp`: a recording layer over the HAL's SDL GL backend). This module runs it, and compares what it drew with what retail
drew on the same screen in the `boot-options` session:

- the primitive stream: each draw (state and every vertex, bit for bit) against the `.otvh` the hooked build's ordering-table adapter wrote for a traced
  retail frame (`OPENTONY_VIDEO_TRACE`; the adapter's stream equals retail's Direct3D stream, `tony hal video shadow`). Textures are matched by the order
  in which a frame first uses them: retail's are surface pointers, the standalone's are surface-store handles;
- the picture: the frame the standalone rendered against retail's F9 screenshot (`D3D_SaveScreenshot`'s BMP) and against the traced retail frame, with
  the video oracle's rule (`tony.hal.video.compare_images`: 5-6-5 precision, PSNR >= 30 dB, >= 95 % of pixels within 16 levels).

    python -m tony.standalone_frame [--frames 60] [--retail-frame 700] [--keep-retail]
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path
from typing import Any

from . import static_data
from .common import ROOT, headless_wine_env, resolve

OUT = Path("build/standalone-frame")
IMAGE_LABEL = "spike-boot-menu"         # tony session run boot-menu --hooks none --label spike-boot-menu --dump 2000
IMAGE_DELIVERY = 2000
RETAIL_LABEL = "standalone-frame-retail"
OPTIONS_RUN = 0x00483EF0
# `restore`: the fonts and the mouse cursors of the image are made again (their textures were in the C runtime heap, which a session dump does not hold;
# standalone_spike.cpp), then the screen runs
DEFAULT_CALL = f"restore,{OPTIONS_RUN:#010x}"


def spike_command(out: Path, ticks: int, render: bool, shots: str = "", extra: dict[str, str] | None = None) -> tuple[list[str], dict[str, str]]:
    from . import sdl3
    from .hooks import _wine_z_path

    run = resolve("build/sessions/runs") / IMAGE_LABEL
    image = run / f"session_{IMAGE_DELIVERY}.img"
    if not image.is_file():
        raise SystemExit(f"{image} missing: tony session run boot-menu --hooks none --label {IMAGE_LABEL} --dump {IMAGE_DELIVERY}")
    env = headless_wine_env()
    env.update({
        "WINEDEBUG": "-all",
        "OT_SPIKE_CALL": DEFAULT_CALL,
        "OT_SPIKE_FRAMES": str(ticks),
        "OPENTONY_DATA_DIR": str(resolve("build/assets/all-pkr/files")),
        "OT_VIDEO_DIR": _wine_z_path(out),
    })
    if render:
        env["OT_VIDEO_SDL"] = _wine_z_path(sdl3.dll_path().parent) + "\\"
    if shots:
        env["OT_VIDEO_SHOTS"] = shots
    if extra:
        env.update(extra)
    command = ["wine", str(resolve("build/hooks/kits/opentony_spike.exe")), str(static_data.ensure_image()),
               str(resolve("build/hooks/kits/opentony_standalone.dll")), str(image), str(resolve("build/s5/empty.tsv")),
               str(image.with_suffix(".heap"))]
    if render:
        command = ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24", *command]
    return command, env


def run_standalone(out: Path, ticks: int = 60, render: bool = True, shots: str = "", extra: dict[str, str] | None = None) -> str:
    """Runs the spike; returns its output (also written to OUT/run.txt)."""

    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("frame_*"):
        old.unlink()
    command, env = spike_command(out, ticks, render, shots, extra)
    result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=600, check=False)
    text = (result.stdout or "") + (result.stderr or "")
    (out / "run.txt").write_text(text)
    if result.returncode != 0:
        raise SystemExit(f"opentony_spike.exe exited {result.returncode & 0xffffffff:#x}; see {out / 'run.txt'}")
    return text


# ---- the primitive stream ---------------------------------------------------------------------------------------------------

def stream_records(path: Path) -> list[tuple[int, bytes]]:
    data = path.read_bytes()
    if data[:4] != b"OTVH":
        raise ValueError(f"{path}: not an .otvh stream")
    at, out = 16, []
    while at + 8 <= len(data):
        kind, size = data[at], struct.unpack_from("<I", data, at + 4)[0]
        out.append((kind, data[at + 8:at + 8 + size]))
        at += 8 + size
    return out


def canonical(records: list[tuple[int, bytes]]) -> list[tuple[int, bytes]]:
    """Each draw's texture replaced by the order in which the frame first uses it (0 = untextured)."""

    names: dict[int, int] = {}
    out = []
    for kind, body in records:
        if kind == 1:
            texture = struct.unpack_from("<I", body, 0)[0]
            if texture:
                names.setdefault(texture, len(names) + 1)
            body = struct.pack("<I", names.get(texture, 0)) + body[4:]
        out.append((kind, body))
    return out


def describe(record: tuple[int, bytes] | None) -> str:
    if record is None:
        return "(none)"
    kind, body = record
    if kind != 1:
        return f"record type {kind}: {body.hex()}"
    texture, flags, src, dst, op = struct.unpack_from("<IIBBB", body, 0)
    topology, count = struct.unpack_from("<II", body, 16)
    vertices = [struct.unpack_from("<ffffIIff", body, 24 + 32 * k) for k in range(count)]
    points = " ".join(f"({x:g},{y:g},{z:g},{rhw:g},{c:08x},{u:.4g},{v:.4g})" for x, y, z, rhw, c, _s, u, v in vertices)
    return f"texture #{texture} flags {flags:04x} blend {src}/{dst}/{op} topology {topology} {points}"


def compare_streams(retail: Path, standalone: Path, show: int = 5) -> dict[str, Any]:
    left, right = canonical(stream_records(retail)), canonical(stream_records(standalone))
    differ = [k for k in range(max(len(left), len(right))) if (left[k] if k < len(left) else None) != (right[k] if k < len(right) else None)]
    return {
        "retail": str(retail), "standalone": str(standalone), "retail_records": len(left), "standalone_records": len(right),
        "differ": len(differ), "pass": not differ and len(left) > 0,
        "first": [{"record": k, "retail": describe(left[k] if k < len(left) else None), "standalone": describe(right[k] if k < len(right) else None)}
                  for k in differ[:show]],
    }


# ---- the picture ------------------------------------------------------------------------------------------------------------

def read_bmp(path: Path):
    """A 24-bit BMP (D3D_SaveScreenshot's) as a tony.hal.video.Image."""

    from .hal import video

    data = path.read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits != 24:
        raise ValueError(f"{path}: {bits}-bit BMP, expected 24")
    stride = (width * 3 + 3) & ~3
    rows = []
    for row in range(abs(height)):
        y = abs(height) - 1 - row if height > 0 else row
        line = data[offset + y * stride:offset + y * stride + width * 3]
        rows.append(b"".join(line[3 * x:3 * x + 3] + b"\0" for x in range(width)))
    return video.Image(width, abs(height), 32, (0x00FF0000, 0x0000FF00, 0x000000FF, 0), 0x40, b"".join(rows))


def compare_pictures(retail, standalone_path: Path, diff_png: Path) -> dict[str, Any]:
    from .hal import video

    return video.compare_images(retail, video.read_image(standalone_path), diff_png)


# ---- retail -----------------------------------------------------------------------------------------------------------------

def screenshots() -> list[Path]:
    return sorted(resolve("build/disc/files/SETUP/data").glob("SCR*.bmp"))


def run_retail(first: int, count: int) -> tuple[Path, Path]:
    """The boot-options session (retail, hooks none) with a video trace of frames [first, first + count); its F9 screenshot of the Options screen."""

    from . import session_scenarios as ss

    before = set(screenshots())
    document = ss.load_session("boot-options")
    if not ss.session_run(RETAIL_LABEL, document, hooks_spec="none", extra_env={"OPENTONY_VIDEO_TRACE": f"{first}:{count}"}):
        raise SystemExit(f"the retail session {RETAIL_LABEL} did not finish")
    new = sorted(set(screenshots()) - before)
    if len(new) < 1:
        raise SystemExit("the retail session wrote no screenshot (F9)")
    shot = ss.RUN_ROOT / RETAIL_LABEL / "options_f9.bmp"
    shot.write_bytes(new[0].read_bytes())    # the first F9 of boot-options is the Options screen (poll 700); the second, the main menu
    for path in new:
        path.unlink()
    return resolve(ss.RUN_ROOT / RETAIL_LABEL / "video"), shot


# ---- the memory image -------------------------------------------------------------------------------------------------------

MEMORY_LABEL = "standalone-memory-retail"
START = 2000                    # the delivery both sides start from

# What the spike does before the screen (standalone_spike.cpp, `restore`), and the screen, as an oracle script for the retail process: so both run the same calls
# from the same image. The name is a block of the game's allocator holding "mainmenu.pre".
RETAIL_SCRIPT = """\
# 7c, tony/standalone_frame.py: the spike's `restore`, then Options_Run (it does not return; the probe's second dump is taken inside it)
alloc name 16
bytes name {name}
let manager [0x0056b834]
call 0x004a90f0 t manager name 0
set32 0x0056161c 0
call 0x0044ada0 c 0x0056163c
set32 0x00561620 0
call 0x0044ada0 c 0x0056164c
set32 0x006a1424 0
set32 0x006a1428 0
call 0x004dbd90 c
call 0x004a9330 t manager name
call 0x00483ef0 c
"""


def run_retail_memory(ticks: int) -> Path:
    """The boot-menu session, retail: images at START and at START + ticks, the restore and Options_Run run at START by the probe's script."""

    from . import hooks
    from . import session_scenarios as ss

    run = ss.run_dir(MEMORY_LABEL)
    script = resolve("build/standalone-frame/retail_options.script")
    script.parent.mkdir(parents=True, exist_ok=True)
    script.write_text(RETAIL_SCRIPT.format(name=b"mainmenu.pre\0".hex()))
    extra = {"OPENTONY_SESSION_DUMP": f"{START},{START + ticks}", "OPENTONY_SESSION_SCRIPT": hooks._wine_z_path(script),
             "OPENTONY_SESSION_SCRIPT_AT": str(START)}
    ss.session_run(MEMORY_LABEL, ss.load_session("boot-menu"), hooks_spec="none", extra_env=extra)
    for delivery in (START, START + ticks):
        if not (run / f"session_{delivery}.img").is_file():
            raise SystemExit(f"{run}: no session_{delivery}.img (see opentony.log)")
    return run


def run_standalone_memory(run: Path, ticks: int, out: Path) -> Path:
    from .hooks import _wine_z_path

    out.mkdir(parents=True, exist_ok=True)
    dump = out / f"standalone_{START + ticks}.img"
    if dump.exists():
        dump.unlink()
    image = run / f"session_{START}.img"
    command, env = spike_command(out, ticks + 10, render=False)
    command[command.index(str(resolve("build/sessions/runs") / IMAGE_LABEL / f"session_{IMAGE_DELIVERY}.img"))] = str(image)
    command[-1] = str(image.with_suffix(".heap"))
    env.pop("OT_VIDEO_DIR", None)
    env["OT_SPIKE_DUMP"] = f"{_wine_z_path(dump)}:{ticks}"
    result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=600, check=False)
    (out / "memory_run.txt").write_text((result.stdout or "") + (result.stderr or ""))
    if not dump.is_file():
        raise SystemExit(f"the standalone wrote no memory image; see {out / 'memory_run.txt'}")
    return dump


def symbol_names() -> list[tuple[int, str]]:
    """Named data addresses (re/symbols), sorted, to label a differing range by the symbol at or before it."""

    from .common import load_yaml

    names: dict[int, str] = {}
    for path in resolve("re/symbols").glob("*.yml"):
        data = load_yaml(path)
        if not isinstance(data, dict):
            continue
        for items in data.values():
            if isinstance(items, list):
                for item in items:
                    if isinstance(item, dict) and "address" in item and "name" in item and int(item["address"]) >= 0x00524000:
                        names.setdefault(int(item["address"]), str(item["name"]))
    return sorted(names.items())


# The words the standalone cannot have as retail has them, each with its reason (measured in 7c: after 60 ticks of Options these are the only differences
# besides none). `pointer`: a pointer into the C runtime heap; retail's malloc and the standalone's (the host's) are different heaps, so only null on both sides
# or on neither is compared.
MEMORY_MASKS: list[tuple[int, int, str, bool]] = [
    (0x00568638, 4, "assertion line: the Kits leave the assertion bookkeeping out (only a failed check is reported)", False),
    (0x0056881C, 0x40, "assertion file name: as above", False),
    (0x006A0080, 4, "the last texture descriptor a packet named (D3D_Draw*)", True),
    (0x006A04C0, 4, "the texture the device has bound (D3D_Draw*)", True),
    (0x006A04C8, 4, "the background sprite (D3DSprite_Create)", True),
    (0x006A051C, 8, "the texture record list's head and tail", True),
    (0x006A0524, 8, "the palette cache list's head and tail", True),
    (0x0055DF40, 4, "a stdio FILE's descriptor (the C runtime's own; the standalone's file calls go to the host's runtime)", False),
    (0x029DA7CC, 4, "a counter of the C runtime's own (incremented by library code at 0x00506a2a..0x0050ac9c)", False),
]


def _mask_of(address: int) -> tuple[int, int, str, bool] | None:
    return next((mask for mask in MEMORY_MASKS if mask[0] <= address < mask[0] + mask[1]), None)


def compare_memory(retail: Path, standalone: Path, base: int = 0x00524000) -> dict[str, Any]:
    import bisect

    left, right = retail.read_bytes(), standalone.read_bytes()
    size = min(len(left), len(right))
    ranges: list[list[int]] = []
    masked: dict[str, int] = {}
    for offset in range(0, size - 3, 4):
        if left[offset:offset + 4] != right[offset:offset + 4]:
            mask = _mask_of(base + offset)
            if mask is not None:
                a, b = struct.unpack_from("<I", left, offset)[0], struct.unpack_from("<I", right, offset)[0]
                if not mask[3] or (a == 0) == (b == 0):
                    masked[f"{mask[0]:08x} {mask[2]}"] = masked.get(f"{mask[0]:08x} {mask[2]}", 0) + 1
                    continue
            if ranges and ranges[-1][1] == offset:
                ranges[-1][1] = offset + 4
            else:
                ranges.append([offset, offset + 4])
    symbols = symbol_names()
    addresses = [a for a, _ in symbols]

    def label(address: int) -> str:
        k = bisect.bisect_right(addresses, address) - 1
        return f"{symbols[k][1]}+{address - symbols[k][0]:#x}" if k >= 0 and address - symbols[k][0] < 0x10000 else "?"

    words = sum((b - a) // 4 for a, b in ranges)
    return {
        "retail": str(retail), "standalone": str(standalone), "bytes": size, "differing_words": words, "ranges": len(ranges),
        "masked_words": masked, "pass": words == 0 and size == len(left) == len(right),
        "first": [{"address": f"{base + a:08x}", "words": (b - a) // 4, "near": label(base + a),
                   "retail": left[a:min(b, a + 16)].hex(), "standalone": right[a:min(b, a + 16)].hex()} for a, b in ranges[:400]],
    }


def memory_main(ticks: int, keep: bool) -> dict[str, Any]:
    out = resolve(OUT)
    run = ss_run = resolve("build/sessions/runs") / MEMORY_LABEL
    if not (keep and (ss_run / f"session_{START + ticks}.img").is_file()):
        run = run_retail_memory(ticks)
    dump = run_standalone_memory(run, ticks, out)
    report = compare_memory(run / f"session_{START + ticks}.img", dump)
    (out / "memory.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


# ---- the input (milestone 8a) -----------------------------------------------------------------------------------------------

# A scripted input log (src/hooks/kits/standalone_platform.cpp): Down is held for four polls (the game's own bindings turn it into the pad's Down, 0x4000), and the pointer
# is put where it rests when nothing has moved it: the display centre. The picture must be the one of 7c.
INPUT_LOG_SCRIPTED = """\
# 8a, tony/standalone_frame.py: the pointer at rest, then Down held for four polls
4 mouse 320 240
8 key down d0
12 key up d0
"""
# the pointer moved onto "SOUND LEVELS": the one draw that differs from the scripted run is the cursor's sprite
INPUT_LOG_POINTER = """\
# 8a, tony/standalone_frame.py: the pointer moved up to 168
8 mouse 320 168
"""
CURSOR_REST = (320, 240)
CURSOR_MOVED = (320, 168)


def _wine_text(path: Path) -> str:
    from .hooks import _wine_z_path

    return _wine_z_path(path)


def input_main(frames: int, retail_frame: int, keep_retail: bool) -> dict[str, Any]:
    """The standalone driven by a scripted input log with SDL's dummy audio: the window and input services of the program, no keyboard. Checks that the keys reach the game's
    own binding code (the action mask), that the picture is still 7c's (the retail Options frame), and that the pointer moves the cursor and nothing else."""

    from .hal import video

    out = resolve("build/standalone-frame-input")
    retail_video = resolve("build/sessions/runs") / RETAIL_LABEL / "video"
    shot = resolve("build/sessions/runs") / RETAIL_LABEL / "options_f9.bmp"
    if not (keep_retail and (retail_video / f"frame_{retail_frame:05d}.otvh").is_file() and shot.is_file()):
        retail_video, shot = run_retail(retail_frame, 1)
    env = {"OT_INPUT_TRACE": "1", "OT_AUDIO": "dummy"}   # Wine does not pass SDL_AUDIO_DRIVER to a Windows process, so the program sets the hint itself
    runs: dict[str, tuple[str, Path]] = {}
    for name, script in (("scripted", INPUT_LOG_SCRIPTED), ("pointer", INPUT_LOG_POINTER)):
        directory = out / name
        log = directory.parent / f"{name}.log"
        out.mkdir(parents=True, exist_ok=True)
        log.write_text(script)
        text = run_standalone(directory, frames, render=True, extra={**env, "OT_INPUT_LOG": _wine_text(log)})
        pictures = sorted(directory.glob("frame_*.ottx"))
        if not pictures:
            raise SystemExit(f"the standalone presented no frame in the {name} run; see {directory / 'run.txt'}")
        runs[name] = (text, pictures[-1])
    scripted_text, scripted = runs["scripted"]
    _pointer_text, pointer = runs["pointer"]
    masks = [line.split("action masks ")[1] for line in scripted_text.splitlines() if "action masks" in line]
    header = next((line for line in scripted_text.splitlines() if line.startswith("video: SDL")), "")
    pointer_diff = compare_streams(scripted.with_suffix(".otvh"), pointer.with_suffix(".otvh"))
    moved = [item for item in pointer_diff["first"] if str(CURSOR_MOVED[1]) in item["standalone"] and str(CURSOR_REST[1]) in item["retail"]]
    image = video.read_image(scripted)
    video.write_png(out / "scripted.png", image.width, image.height, image.rgba())
    return {
        "header": header,
        "action_masks": masks,
        "scripted_stream": compare_streams(retail_video / f"frame_{retail_frame:05d}.otvh", scripted.with_suffix(".otvh")),
        "scripted_picture": compare_pictures(read_bmp(shot), scripted, out / "diff_scripted.png"),
        "pointer_vs_scripted": pointer_diff,
        "pointer_only_moved_the_cursor": pointer_diff["differ"] == 1 and len(moved) == 1,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python -m tony.standalone_frame", description=__doc__.splitlines()[0])
    parser.add_argument("--frames", type=int, default=60, help="Game_FrameTick calls the standalone runs (two a frame)")
    parser.add_argument("--retail-frame", type=int, default=700, help="the traced retail frame of the Options screen")
    parser.add_argument("--keep-retail", action="store_true", help="reuse the last retail run")
    parser.add_argument("--memory", type=int, metavar="TICKS", help="instead: the memory image after TICKS Game_FrameTick calls against retail's")
    parser.add_argument("--input", action="store_true", help="instead: the window, input and audio services (milestone 8a): scripted input logs with SDL's dummy audio")
    args = parser.parse_args(argv)
    if args.input:
        report = input_main(args.frames, args.retail_frame, args.keep_retail)
        (resolve(OUT) / "input.json").parent.mkdir(parents=True, exist_ok=True)
        (resolve(OUT) / "input.json").write_text(json.dumps(report, indent=2) + "\n")
        from .hal import video

        stream, picture = report["scripted_stream"], report["scripted_picture"]
        print(report["header"])
        print(f"action masks seen: {report['action_masks']} (0x4000 is the pad's Down)")
        print(f"scripted run, stream: {stream['standalone_records']} standalone draws, {stream['retail_records']} retail, {stream['differ']} differ: "
              f"{'PASS' if stream['pass'] else 'FAIL'}")
        print(f"scripted run, picture vs the retail screenshot: psnr {picture['psnr']} dB, {100 * picture['within_tolerance']:.2f}% within {video.TOLERANCE}: "
              f"{'PASS' if picture['pass'] else 'FAIL'}")
        print(f"pointer moved: {report['pointer_vs_scripted']['differ']} draw differs, only the cursor: {'PASS' if report['pointer_only_moved_the_cursor'] else 'FAIL'}")
        masks_ok = report["action_masks"][:2] == ["4000 0000", "0000 0000"]
        ok = stream["pass"] and picture["pass"] and report["pointer_only_moved_the_cursor"] and masks_ok and "audio dummy" in report["header"]
        print("RESULT", "PASS" if ok else "FAIL", resolve(OUT) / "input.json")
        return 0 if ok else 1
    if args.memory:
        report = memory_main(args.memory, args.keep_retail)
        print(f"memory: {report['differing_words']} of {report['bytes'] // 4} words differ in {report['ranges']} ranges; masked:")
        for name, count in report["masked_words"].items():
            print(f"  {count:3d} words  {name}")
        for item in report["first"][:40]:
            print(f"  {item['address']} {item['words']:5d} words  {item['near']:40s} retail {item['retail']} standalone {item['standalone']}")
        print("RESULT", "PASS" if report["pass"] else "FAIL", resolve(OUT) / "memory.json")
        return 0 if report["pass"] else 1
    out = resolve(OUT)
    retail_video = resolve("build/sessions/runs") / RETAIL_LABEL / "video"
    shot = resolve("build/sessions/runs") / RETAIL_LABEL / "options_f9.bmp"
    if not (args.keep_retail and (retail_video / f"frame_{args.retail_frame:05d}.otvh").is_file() and shot.is_file()):
        retail_video, shot = run_retail(args.retail_frame, 1)
    text = run_standalone(out, args.frames, render=True)
    frames = sorted(out.glob("frame_*.ottx"))
    if not frames:
        print(text)
        raise SystemExit("the standalone presented no frame")
    last = frames[-1]
    from .hal import video

    report = {
        "standalone_frame": last.stem,
        "stream": compare_streams(retail_video / f"frame_{args.retail_frame:05d}.otvh", last.with_suffix(".otvh")),
        "picture_vs_screenshot": compare_pictures(read_bmp(shot), last, out / "diff_screenshot.png"),
        "picture_vs_traced_frame": compare_pictures(video.read_image(retail_video / f"frame_{args.retail_frame:05d}.ottx"), last, out / "diff_frame.png"),
    }
    image = video.read_image(last)
    video.write_png(out / f"{last.stem}.png", image.width, image.height, image.rgba())
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    stream = report["stream"]
    print(f"stream: {stream['standalone_records']} standalone draws, {stream['retail_records']} retail, {stream['differ']} differ: "
          f"{'PASS' if stream['pass'] else 'FAIL'}")
    for item in stream["first"]:
        print(f"  record {item['record']}\n    retail     {item['retail']}\n    standalone {item['standalone']}")
    for name in ("picture_vs_screenshot", "picture_vs_traced_frame"):
        row = report[name]
        print(f"{name}: psnr {row['psnr']} dB, {100 * row['within_tolerance']:.2f}% within {video.TOLERANCE}, max {row['max_difference']}: "
              f"{'PASS' if row['pass'] else 'FAIL'}")
    ok = stream["pass"] and report["picture_vs_screenshot"]["pass"] and report["picture_vs_traced_frame"]["pass"]
    print("RESULT", "PASS" if ok else "FAIL", out / "report.json")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
