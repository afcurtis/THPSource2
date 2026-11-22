"""`tony hal ...` command handlers (heavy imports stay lazy)."""

from __future__ import annotations

import json

BOUNDARY = "re/config/hal_boundary.yml"


def _print_findings(errors: list[str], notes: list[str], limit: int = 60) -> None:
    for note in notes[:limit]:
        print(f"NOTE {note}")
    if len(notes) > limit:
        print(f"NOTE ... {len(notes) - limit} more notes")
    for error in errors[:limit]:
        print(f"FAIL {error}")
    if len(errors) > limit:
        print(f"FAIL ... {len(errors) - limit} more errors")


def hal_derive(args) -> int:
    from .inventory import derive, write

    derived = derive(force=bool(getattr(args, "force", False)))
    _print_findings(derived.errors, derived.notes)
    print(json.dumps(derived.summary, indent=2))
    if getattr(args, "write", False):
        if derived.errors and not getattr(args, "allow_errors", False):
            print(f"FAIL not writing {BOUNDARY}: {len(derived.errors)} errors")
            return 1
        print(f"wrote {write(derived)}")
    return 1 if derived.errors else 0


def hal_check(args=None, *, quiet: bool = False) -> int:
    from .inventory import check

    errors, notes, derived = check(force=bool(getattr(args, "force", False)))
    if errors or not quiet:
        _print_findings(errors, [] if quiet else notes)
    summary = derived.summary
    print(f"{'FAIL' if errors else 'OK  '} hal boundary: {summary['sites']} sites in {summary['functions']} functions, "
          f"{summary['hook_points']} hook points, {summary['callbacks']} callbacks, {len(errors)} errors ({BOUNDARY})")
    return 1 if errors else 0


def hal_status(_args) -> int:
    from ..common import load_yaml

    document = load_yaml(BOUNDARY)
    generated = document.get("generated", {})
    print(json.dumps(generated.get("summary", {}), indent=2))
    by_service: dict[str, list[str]] = {}
    for row in generated.get("hook_points", []):
        by_service.setdefault(row[3], []).append(f"{row[2]} ({row[0]:#010x}/{(row[1] or 0):#010x})")
    for service, hooks in sorted(by_service.items()):
        print(f"{service}: " + ", ".join(hooks))
    return 0


def hal_trace_sites(args) -> int:
    from .trace import write_sites_file

    count = write_sites_file(args.output)
    print(f"wrote {count} call-site addresses -> {args.output}")
    print(f"run with OPENTONY_HAL_TRACE_SITES={args.output} (canonical build only)")
    return 0


def hal_trace_report(args) -> int:
    from .trace import coverage_report

    report = coverage_report(args.hits)
    if args.json:
        print(json.dumps(report, indent=2))
        return 0
    print(f"coverage: {report['hit_sites']}/{report['known_sites']} known sites hit "
          f"({report['coverage'] * 100:.1f}%)")
    if report["unknown_hits"]:
        print(f"FAIL {len(report['unknown_hits'])} hit address(es) not in the inventory (impossible by "
              "construction; the hits file does not match this inventory)")
    print("by kind:")
    for kind, counts in sorted(report["by_kind"].items()):
        print(f"  {kind}: {counts['hit']}/{counts['known']}")
    print("by service:")
    for service, counts in sorted(report["by_service"].items()):
        print(f"  {service}: {counts['hit']}/{counts['known']}")
    return 1 if report["unknown_hits"] else 0


def register(subparsers) -> None:
    hal = subparsers.add_parser("hal", help="platform boundary audit and HAL tooling")
    sub = hal.add_subparsers(dest="hal_command", required=True)
    p = sub.add_parser("derive", help="re-derive the platform boundary inventory from Ghidra")
    p.add_argument("--write", action="store_true", help=f"rewrite the generated part of {BOUNDARY}")
    p.add_argument("--allow-errors", action="store_true", help="write even when checks fail")
    p.add_argument("--force", action="store_true", help="re-export the Ghidra facts even when cached")
    p.set_defaults(func=hal_derive)
    p = sub.add_parser("check", help=f"fail when {BOUNDARY} is incomplete or stale")
    p.add_argument("--force", action="store_true", help="re-export the Ghidra facts even when cached")
    p.set_defaults(func=hal_check)
    p = sub.add_parser("status", help="summarize the recorded inventory and hook points")
    p.set_defaults(func=hal_status)
    p = sub.add_parser("trace", help="dynamic call-site coverage tracer (OPENTONY_HAL_TRACE_SITES)")
    trace_sub = p.add_subparsers(dest="trace_command", required=True)
    tp = trace_sub.add_parser("sites", help="write the canonical-build call-site list the hook DLL reads")
    tp.add_argument("output", nargs="?", default="build/hal_trace/sites.txt",
                    help="path for OPENTONY_HAL_TRACE_SITES (default: build/hal_trace/sites.txt)")
    tp.set_defaults(func=hal_trace_sites)
    tp = trace_sub.add_parser("report", help="cross-reference a <sites file>.hits log against the inventory")
    tp.add_argument("hits", help="the .hits file written next to the sites file during a capture")
    tp.add_argument("--json", action="store_true")
    tp.set_defaults(func=hal_trace_report)

    p = sub.add_parser("video", help="primitive-stream oracle files (OPENTONY_VIDEO_TRACE)")
    p.add_argument("action", choices=("summary", "png", "replay", "compare", "shadow", "ingame"),
                   help="summary: stream statistics; png: screenshots; replay: render each frame through the "
                        "SDL video backend and compare (image oracle); compare: compare existing replays; "
                        "shadow: primitive-stream oracle, retail stream vs the ordering-table adapter; "
                        "ingame: retail frames vs the frames video=sdl drew in game (--hal)")
    p.add_argument("directory", nargs="?", default="build/hooks/runtime/video", help="trace directory")
    p.add_argument("--frame", type=int, help="only this frame")
    p.add_argument("--hal", help="ingame: directory with the video=sdl run's frame_NNNNN.hal.ottx")
    p.add_argument("--json", action="store_true", help="summary as JSON")
    p.add_argument("--backend", choices=("gpu", "gl"), default="gl",
                   help="replay: which SDL video backend to render with (default gl); writes distinct output names "
                        "per backend so both can be compared")
    p.set_defaults(func=hal_video)

    from .shaders import register as register_shaders

    register_shaders(sub)


def hal_video(args) -> int:
    import json

    from ..common import resolve
    from . import video

    directory = resolve(args.directory)
    if not directory.is_dir():
        print(f"FAIL {directory}: no trace (run with OPENTONY_VIDEO_TRACE=<first>:<count>)")
        return 1
    streams = [path for path in video.frames(directory) if args.frame is None or path.stem == f"frame_{args.frame:05d}"]
    if args.action == "shadow":
        return _video_shadow(streams, as_json=args.json)
    if args.action == "ingame":
        return _video_ingame(directory, resolve(args.hal or args.directory), frame=args.frame, as_json=args.json)
    if args.action in ("replay", "compare"):
        return _video_oracle(directory, streams, replay=args.action == "replay", as_json=args.json,
                              backend=getattr(args, "backend", "gl"))
    if args.action == "png":
        written = 0
        for path in streams:
            target = path.with_suffix(".ottx")
            if target.is_file():
                image = video.read_image(target)
                video.write_png(path.with_suffix(".png"), image.width, image.height, image.rgba())
                written += 1
        print(f"wrote {written} PNG screenshot(s) in {directory}")
        return 0 if written else 1
    summaries = [video.summarize(video.read_frame(path)) for path in streams]
    if args.json:
        print(json.dumps(summaries, indent=2))
        return 0
    for summary in summaries:
        draws = summary["records"].get("draw", 0) + summary["records"].get("draw_indexed", 0)
        print(f"frame {summary['frame']}: {draws} draws, {summary['vertices']} vertices, "
              f"{summary['textures']} textures{' (TRUNCATED)' if summary['truncated'] else ''}")
        print("  primitives: " + ", ".join(f"{name} {count}" for name, count in sorted(summary["primitives"].items())))
        print("  vertex formats: " + ", ".join(f"{fvf} {count}" for fvf, count in summary["fvf"].items()))
        for name, values in summary["states"].items():
            print(f"  {name}: " + ", ".join(f"{value}x{count}" for value, count in values.items()))
    return 0


def _video_ingame(retail_directory, hal_directory, *, frame: int | None, as_json: bool) -> int:
    """In-game image oracle: the frame retail rendered (OPENTONY_VIDEO_TRACE)
    against the frame video=sdl drew at the same lockstep frame."""

    import re

    from . import video

    rows = []
    pattern = re.compile(r"frame_(\d{5})\.ottx$")
    for retail in sorted(retail_directory.glob("frame_*.ottx")):
        match = pattern.fullmatch(retail.name)
        if match is None or (frame is not None and int(match.group(1)) != frame):
            continue
        hal = hal_directory / f"frame_{match.group(1)}.hal.ottx"
        row = {"frame": int(match.group(1))}
        if not hal.is_file():
            row.update(result="MISSING", detail=f"no {hal.name}")
            rows.append(row)
            continue
        hal_image = video.read_image(hal)
        diff = hal.with_name(f"frame_{match.group(1)}.hal.diff.png")
        row.update(video.compare_images(video.read_image(retail), hal_image, diff))
        video.write_png(hal.with_name(f"frame_{match.group(1)}.hal.png"), hal_image.width, hal_image.height,
                        hal_image.rgba())
        row["result"] = "PASS" if row["pass"] else "FAIL"
        rows.append(row)
    if as_json:
        print(json.dumps(rows, indent=2))
    for row in rows if not as_json else []:
        print(f"{row['result']:7} frame {row['frame']:5}  psnr {row.get('psnr', '-')}  "
              f"within {row.get('within_tolerance', '-')}  max {row.get('max_difference', '-')}  {row.get('detail', '')}")
    passed = sum(1 for row in rows if row["result"] == "PASS")
    if not as_json:
        worst = min((row["psnr"] for row in rows if "psnr" in row), default=None)
        print(f"{'OK  ' if rows and passed == len(rows) else 'FAIL'} in-game video: {passed}/{len(rows)} frames pass "
              f"(worst {worst} dB; minimum {video.MIN_PSNR} dB, {video.MIN_WITHIN_TOLERANCE:.0%} within "
              f"{video.TOLERANCE})")
    return 0 if rows and passed == len(rows) else 1


def _video_oracle(directory, streams, *, replay: bool, as_json: bool, backend: str = "gl") -> int:
    """Image oracle: replay recorded retail frames through the HAL video backend.

    `backend` picks which SDL video service the replay renders with (docs/HAL.md,
    "Video", 3.4d); gpu and gl write distinct output names (a `.gpu.` infix for
    gpu, none for gl, so existing gl baselines are untouched) so both can be
    compared against the same retail frames and against each other."""

    import json
    import subprocess

    from .. import sdl3
    from ..common import headless_wine_env, resolve
    from ..hooks import HOOKS_BUILD_DIR, _wine_z_path
    from . import video

    suffix = "" if backend == "gl" else f".{backend}"
    tool = resolve(HOOKS_BUILD_DIR) / "opentony_video_replay.exe"
    if replay and not tool.is_file():
        print(f"FAIL {tool} not built; run `tony hooks build`")
        return 1
    results = []
    for stream in streams:
        retail = stream.with_suffix(".ottx")
        output = stream.with_name(f"{stream.stem}{suffix}.replay.ottx")
        row = {"frame": stream.stem, "backend": backend}
        if replay:
            env = headless_wine_env()
            command = [
                "xvfb-run", "-a", "-s", "-screen 0 1024x768x24", "wine", str(tool),
                _wine_z_path(stream), _wine_z_path(directory / "textures") + "\\", _wine_z_path(output),
                _wine_z_path(sdl3.dll_path().parent) + "\\", "--backend", backend,
            ]
            completed = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                       timeout=300, check=False)
            line = next((text for text in completed.stdout.splitlines() if text.startswith(("replay ", "FAIL"))), "")
            row["replay"] = line
            if not line.startswith("replay "):
                row.update(result="ERROR", ok=False)
                results.append(row)
                continue
        if not output.is_file() or not retail.is_file():
            row.update(result="MISSING", ok=False)
            results.append(row)
            continue
        metrics = video.compare_images(video.read_image(retail), video.read_image(output),
                                       stream.with_name(f"{stream.stem}{suffix}.diff.png"))
        image = video.read_image(output)
        video.write_png(stream.with_name(f"{stream.stem}{suffix}.replay.png"), image.width, image.height,
                        image.rgba())
        row.update(metrics, result="PASS" if metrics["pass"] else "FAIL", ok=metrics["pass"])
        results.append(row)
    (directory / f"oracle{suffix}.json").write_text(json.dumps(results, indent=2) + "\n")
    if as_json:
        print(json.dumps(results, indent=2))
    else:
        for row in results:
            print(f"{row['frame']}: {row['result']:<7} psnr {row.get('psnr', '-')} dB, "
                  f"{100 * row.get('within_tolerance', 0):.2f}% within {video.TOLERANCE}, "
                  f"max {row.get('max_difference', '-')}  {row.get('replay', '')}")
    return 0 if results and all(row["ok"] for row in results) else 1


def _video_shadow(streams, *, as_json: bool) -> int:
    """Primitive-stream oracle: each traced frame's retail Direct3D stream vs
    the draws the ordering-table adapter made for it in shadow mode."""

    import json
    import subprocess

    from ..common import resolve

    tool = resolve("build/native/opentony_video_shadow_compare")
    if not tool.is_file():
        print(f"FAIL {tool} not built; run `cmake --build build/native --target opentony_video_shadow_compare`")
        return 1
    rows = []
    for stream in streams:
        shadow = stream.with_suffix(".otvh")
        if not shadow.is_file():
            rows.append({"frame": stream.stem, "result": "MISSING", "detail": "no .otvh (traced before the shadow existed?)"})
            continue
        completed = subprocess.run([str(tool), str(stream), str(shadow)], stdout=subprocess.PIPE, text=True, check=False)
        lines = completed.stdout.strip().splitlines()
        rows.append({
            "frame": stream.stem,
            "result": "MATCH" if completed.returncode == 0 else "DIFFER",
            "detail": lines[-1] if lines else "",
            "first_differences": [line.strip() for line in lines[:-1]],
        })
    matched = sum(row["result"] == "MATCH" for row in rows)
    if as_json:
        print(json.dumps(rows, indent=2))
    else:
        for row in rows:
            if row["result"] != "MATCH":
                print(f"{row['frame']}: {row['result']} {row['detail']}")
                for line in row.get("first_differences", [])[:3]:
                    print(f"    {line}")
        print(f"{matched}/{len(rows)} frames: retail stream and ordering-table adapter identical")
    return 0 if rows and matched == len(rows) else 1
