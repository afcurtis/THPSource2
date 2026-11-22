"""python scripts/play/qp/run.py SCRIPT TAG MODE [VAR=value ...]: a headless quick start (Hangar, Tony) under xvfb replaying an OT_INPUT_LOG script; frames 300..2400 every
25 kept as build/qp/shots/TAG/frame_NNNNN.png, the run's output in output.txt. MODE is free or single."""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/qp"), exist_ok=True)  # outputs stay in build/
import sys
from pathlib import Path

sys.path.insert(0, _os.path.expanduser("~/THPSource2"))
from tony import sdl3  # noqa: E402
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402
from tony.hal import video  # noqa: E402

orig = sb.subprocess.run
sb.subprocess.run = lambda cmd, **kw: orig(["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", *cmd], **kw)
script, tag, mode = Path(sys.argv[1]).resolve(), sys.argv[2], sys.argv[3]
d = Path((_os.path.expanduser("~/THPSource2") + "/build/qp/shots")) / tag
d.mkdir(parents=True, exist_ok=True)
for old in d.glob("frame_*"):
    old.unlink()
extra = {"OT_INPUT_LOG": sw._wine_z_path(script), "OT_QUICK_START": "0,0", "OT_QUICK_MODE": "single" if mode == "single" else "",
         "OT_VIDEO_DIR": sw._wine_z_path(d), "OT_VIDEO_SDL": sw._wine_z_path(sdl3.dll_path().parent) + "\\",
         "OT_VIDEO_SHOTS": ",".join(str(f) for f in range(300, 2401, 25)), "OT_FILES_LOG": "1"}
for pair in sys.argv[4:]:
    key, value = pair.split("=", 1)
    extra[key] = value
document = {"name": f"qp{tag}", "deliveries": 2 * 2600 + 4000, "every": 2000, "keys": [], "masks": []}
finished, text = sb.run_standalone(f"qp-{tag}", document, extra=extra)
(d / "output.txt").write_text(text)
for path in sorted(d.glob("frame_*.ottx")):
    image = video.read_image(path)
    video.write_png(path.with_suffix(".png"), image.width, image.height, image.rgba())
print(f"finished={finished}", len(list(d.glob("*.png"))), "pngs")
