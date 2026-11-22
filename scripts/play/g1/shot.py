"""G1 step 5: render a level headless (SDL GL under Wine + Xvfb) and keep the pictures of chosen frames as PNGs.

python scripts/play/g1/shot.py TAG LEVEL FRAMES SHOTS [VAR=value ...]
  e.g. python scripts/play/g1/shot.py aniso16 1 64 60 OT_MSAA=4 OT_ANISOTROPY=16 OT_INTERNAL_RESOLUTION=960
  -> build/g1/shots/TAG/frame_NNNNN.png (+ .otvh), output.txt
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/g1"), exist_ok=True)  # outputs stay in build/
import sys
from pathlib import Path

sys.path.insert(0, _os.path.expanduser("~/THPSource2"))
from tony import sdl3  # noqa: E402
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402
from tony.hal import video  # noqa: E402

orig = sb.subprocess.run
sb.subprocess.run = lambda cmd, **kw: orig(["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", *cmd], **kw)

tag, level, frames, shots = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
d = Path((_os.path.expanduser("~/THPSource2") + "/build/g1/shots")) / tag
d.mkdir(parents=True, exist_ok=True)
for old in d.glob("frame_*"):
    old.unlink()
spec = d / "scenario.txt"
cap = d / "standalone.otcap"
spec.write_text(f"level {level}\nframes {frames}\nsha256 f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c\n")
cap.unlink(missing_ok=True)
extra = {"OT_SCENARIO": sw._wine_z_path(spec), "OT_SCENARIO_OUT": sw._wine_z_path(cap), "OT_VIDEO_DIR": sw._wine_z_path(d),
         "OT_VIDEO_SDL": sw._wine_z_path(sdl3.dll_path().parent) + "\\", "OT_VIDEO_SHOTS": shots}
for pair in sys.argv[5:]:
    key, value = pair.split("=", 1)
    extra[key] = value
document = {"name": f"g1shot{tag}", "deliveries": 4000 + 8 * frames, "every": 2, "keys": [], "masks": []}
finished, text = sb.run_standalone(f"g1-{tag}", document, extra=extra)
(d / "output.txt").write_text(text)
for path in sorted(d.glob("frame_*.ottx")):
    image = video.read_image(path)
    rgba = bytearray()
    # Image pixels are 32-bit with channel masks; convert through the helper's own reader when present
    raw = image.pixels if hasattr(image, "pixels") else b""
    masks = getattr(image, "masks", None)
    rgba = bytes(raw)
    out = path.with_suffix(".png")
    try:
        video.write_png(out, image.width, image.height, rgba)
    except Exception as error:  # noqa: BLE001
        print("png:", error)
    print(path.name, image.width, "x", image.height)
print(f"finished={finished}", [l for l in text.splitlines() if l.startswith("video:")][:4])
