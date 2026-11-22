"""G4 step 1: the level-of-detail census. python scripts/play/g4/census.py TAG LEVEL FRAMES [VAR=value ...]

A scenario of `level N`, FRAMES frames, the skater pushing forward with a few turns (a ride). OT_LOD_CENSUS=1 makes render_objects print one `census:` line a view
(a temporary log, marked CENSUS-TEMP in src/runtime/render_scene.cpp). Output: build/g4/census/TAG/output.txt and a summary on stdout."""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/g4"), exist_ok=True)  # outputs stay in build/
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, os.path.expanduser("~/THPSource2"))
from tony import capture  # noqa: E402
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402

tag, level, frames = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
d = Path((os.path.expanduser("~/THPSource2") + "/build/g4/census")) / tag
d.mkdir(parents=True, exist_ok=True)
spec = d / "scenario.txt"
cap = d / "standalone.otcap"
m = capture.ACTION_MASKS
lines = f"level {level}\nframes {frames}\nsha256 {capture.SUPPORTED_BUILD_SHA256}\n"
if os.environ.get("RIDE", "1") != "0":
    lines += f"action 0x{m['up']:x} 20 {frames - 40}\n"
    t = 60
    while t + 40 < frames:   # a turn every ~90 frames, alternating
        lines += f"action 0x{m['left' if (t // 90) % 2 else 'right']:x} {t} 25\n"
        t += 90
spec.write_text(lines)
cap.unlink(missing_ok=True)
extra = {"OT_SCENARIO": sw._wine_z_path(spec), "OT_SCENARIO_OUT": sw._wine_z_path(cap), "OT_LOD_CENSUS": "1"}
if frames > 2300:
    extra["OT_SCENARIO_MAPPING_MB"] = str(frames * 27 // 1024 + 64)
for pair in sys.argv[4:]:
    key, value = pair.split("=", 1)
    extra[key] = value
document = {"name": f"g4{tag}", "deliveries": 4000 + 8 * frames, "every": 2, "keys": [], "masks": []}
finished, text = sb.run_standalone(f"g4-{tag}", document, extra=extra)
(d / "output.txt").write_text(text)
rows = [dict((k, v) for k, v in re.findall(r"(\w+)=(\S+)", line)) for line in text.splitlines() if line.startswith("census:")]
print(f"{tag}: finished={finished} views={len(rows)} {sw.fatal_line(text)[:200]}")
