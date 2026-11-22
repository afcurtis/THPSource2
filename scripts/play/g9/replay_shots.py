"""python scripts/play/g9/replay_shots.py SCRIPT POLLS TAG SHOTS [VAR=value ...]: replay a player's OT_INPUT_LOG script (quick start NYC, single session, the player's
options) under xvfb with SDL video and keep frames SHOTS as build/g9/shots/TAG/frame_NNNNN.png."""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/g9"), exist_ok=True)  # outputs stay in build/
import sys
from pathlib import Path

sys.path.insert(0, _os.path.expanduser("~/THPSource2"))
from tony import sdl3  # noqa: E402
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402
from tony.hal import video  # noqa: E402

orig = sb.subprocess.run
sb.subprocess.run = lambda cmd, **kw: orig(["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", *cmd], **kw)
sb.set_arch(64)
script, polls, tag, shots = Path(sys.argv[1]).resolve(), int(sys.argv[2]), sys.argv[3], sys.argv[4]
d = Path((_os.path.expanduser("~/THPSource2") + "/build/g9/shots")) / tag
d.mkdir(parents=True, exist_ok=True)
for old in d.glob("frame_*"):
    old.unlink()
extra = {"OT_INPUT_LOG": sw._wine_z_path(script), "OT_QUICK_START": "3,1", "OT_QUICK_MODE": "single", "OT_UNLOCK": "levels,skaters,cheats",
         "OT_PSX_COLOR": "1", "OT_FRAME_RATE": "120", "OT_FOG_DISTANCE": "off", "OT_DETAIL_DISTANCE": "1000",
         "OT_INTERNAL_RESOLUTION": "720", "OT_ASPECT": "16:9",
         "OT_VIDEO_DIR": sw._wine_z_path(d), "OT_VIDEO_SDL": sw._wine_z_path(sdl3.dll_path().parent) + "\\", "OT_VIDEO_SHOTS": shots}
for pair in sys.argv[5:]:
    key, value = pair.split("=", 1)
    extra[key] = value
document = {"name": f"g9{tag}", "deliveries": 2 * polls + 4000, "every": 2000, "keys": [], "masks": []}
finished, text = sb.run_standalone(f"g9-{tag}", document, extra=extra)
(d / "output.txt").write_text(text)
for path in sorted(d.glob("frame_*.ottx")):
    image = video.read_image(path)
    video.write_png(path.with_suffix(".png"), image.width, image.height, image.rgba())
print(f"finished={finished}", len(list(d.glob("*.png"))), "pngs")
