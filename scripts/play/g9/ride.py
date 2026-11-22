"""Pop-in at School II's start: ride forward from the start and keep chosen frames as PNGs.

python scripts/play/g9/ride.py TAG LEVEL FRAMES SHOTS [VAR=value ...]   -> build/g9/shots/TAG/frame_NNNNN.png
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/g9"), exist_ok=True)  # outputs stay in build/
import sys
from pathlib import Path

sys.path.insert(0, _os.path.expanduser("~/THPSource2"))
from tony import capture  # noqa: E402
from tony import sdl3  # noqa: E402
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402
from tony.hal import video  # noqa: E402

orig = sb.subprocess.run
sb.subprocess.run = lambda cmd, **kw: orig(["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", *cmd], **kw)

tag, level, frames, shots = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
d = Path((_os.path.expanduser("~/THPSource2") + "/build/g9/shots")) / tag
d.mkdir(parents=True, exist_ok=True)
for old in d.glob("frame_*"):
    old.unlink()
spec = d / "scenario.txt"
cap = d / "standalone.otcap"
up = capture.ACTION_MASKS["up"]
spec.write_text(f"level {level}\nframes {frames}\nsha256 {capture.SUPPORTED_BUILD_SHA256}\n"
               + (f"action 0x{up:x} 20 {frames - 20}\n" if __import__("os").environ.get("UP", "1") != "0" else "")
               + "".join(f"action 0x{capture.ACTION_MASKS[k]:x} {s} {n}\n" for k, s, n in
                         (step.split(":") for step in __import__("os").environ.get("STEER", "").split(",") if step)))
cap.unlink(missing_ok=True)
extra = {"OT_SCENARIO": sw._wine_z_path(spec), "OT_SCENARIO_OUT": sw._wine_z_path(cap), "OT_VIDEO_DIR": sw._wine_z_path(d),
         "OT_VIDEO_SDL": sw._wine_z_path(sdl3.dll_path().parent) + "\\", "OT_VIDEO_SHOTS": shots}
for pair in sys.argv[5:]:
    key, value = pair.split("=", 1)
    extra[key] = value
document = {"name": f"g9{tag}", "deliveries": 4000 + 8 * frames, "every": 2, "keys": [], "masks": []}
finished, text = sb.run_standalone(f"g9-{tag}", document, extra=extra)
(d / "output.txt").write_text(text)
for path in sorted(d.glob("frame_*.ottx")):
    image = video.read_image(path)
    video.write_png(path.with_suffix(".png"), image.width, image.height, image.rgba())
print(f"finished={finished}", sorted(p.name for p in d.glob("*.png"))[:3], "...")
