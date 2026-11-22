"""usage: python scripts/play/s9/boot_run.py LABEL [--lenient] [--frames N] [--dump A[,B]] [--session NAME] [--video] [--trace]

The cold boot in opentony_spike.exe (OT_SPIKE_CALL=boot, no session image) with a session scenario's recorder variables; output in
build/sessions/runs/LABEL (run.txt, session.trace, session_N.img)."""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tony import session_scenarios as ss, static_data  # noqa: E402
from tony.common import headless_wine_env, resolve  # noqa: E402
from tony.hooks import _wine_z_path  # noqa: E402

p = argparse.ArgumentParser()
p.add_argument("label")
p.add_argument("--lenient", action="store_true")
p.add_argument("--frames", type=int, default=0)
p.add_argument("--dump", default="")
p.add_argument("--session", default="boot-menu")
p.add_argument("--video", action="store_true")
p.add_argument("--trace", action="store_true")
p.add_argument("--deliveries", type=int, default=0)
args = p.parse_args()

doc = ss.load_session(args.session)
out = ss.run_dir(args.label)
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
env = headless_wine_env()
env.update({
    "WINEDEBUG": "-all",
    "OT_SPIKE_CALL": "boot",
    "OPENTONY_DATA_DIR": str(resolve("build/assets/all-pkr/files")),
    "OPENTONY_LOG_DIR": _wine_z_path(out),
    "OPENTONY_SESSION_TRACE": str(args.deliveries or doc["deliveries"]),
    "OPENTONY_SESSION_EVERY": str(doc.get("every", 2)),
    "OPENTONY_SESSION_SKIP_MOVIES": "1",
    "OPENTONY_SESSION_MASK": ",".join([ss.DEFAULT_MASKS, *doc.get("masks", [])]),
    "OPENTONY_SESSION_KEYS": ss.keys_spec(doc),
})
if args.dump:
    env["OPENTONY_SESSION_DUMP"] = args.dump
if args.frames:
    env["OT_SPIKE_FRAMES"] = str(args.frames)
if args.lenient:
    env["OT_RESOLVER_LENIENT"] = "1"
if args.trace:
    env["OT_RESOLVER_TRACE"] = "1"
command = ["wine", str(resolve("build/hooks/kits/opentony_spike.exe")), str(static_data.ensure_image()),
           str(resolve("build/hooks/kits/opentony_standalone.dll")), "-", str(resolve("build/s5/empty.tsv"))]
if args.video:
    from tony import sdl3
    env["OT_VIDEO_DIR"] = _wine_z_path(out)
    env["OT_VIDEO_SDL"] = _wine_z_path(sdl3.dll_path().parent) + "\\"
    command = ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24", *command]
result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=900, check=False)
text = (result.stdout or "") + (result.stderr or "")
(out / "run.txt").write_text(text)
print("exit", hex(result.returncode & 0xFFFFFFFF), "done" if (out / "session.done").exists() else "no session.done")
lines = text.splitlines()
print("\n".join(lines[-40:]))
