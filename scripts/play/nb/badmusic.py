"""badmusic.py TAG FOLDER: a Hangar idle run of 400 frames with the custom soundtrack from build/nb/FOLDER (made by badmusic.sh), FFmpeg loaded; prints the music lines."""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/nb"), exist_ok=True)  # outputs stay in build/
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402

_run = sb.subprocess.run
sb.subprocess.run = lambda *a, **k: _run(*a, **{**k, "errors": "replace"})
tag, folder = sys.argv[1], sys.argv[2]
sb.set_arch(64)
d = Path((_os.path.expanduser("~/THPSource2") + "/build/nb/badmusic_runs")) / tag
d.mkdir(parents=True, exist_ok=True)
spec = d / "scenario.txt"
cap = d / "standalone.otcap"
spec.write_text("level 0\nframes 400\nsha256 f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c\n")
cap.unlink(missing_ok=True)
extra = {"OT_SCENARIO": sw._wine_z_path(spec), "OT_SCENARIO_OUT": sw._wine_z_path(cap), "OT_CUSTOM_MUSIC": "1",
         "OT_MUSIC_DIR": sw._wine_z_path(ROOT / "build/nb" / folder) + "\\",
         "OPENTONY_MEDIA_DIR": sw._wine_z_path(Path(_os.environ["OT_TEST_INSTALL"]) / "Data/Game") + "\\",
         "OPENTONY_FFMPEG_DIR": sw._wine_z_path(ROOT / ".tools/ffmpeg/x86_64/bin") + "\\", "OT_MOVIE_LOG": "1"}
document = {"name": f"bm{tag}", "deliveries": 8000, "every": 2, "keys": [], "masks": []}
finished, text = sb.run_standalone(f"nb-bm{tag}", document, extra=extra)
(d / "output.txt").write_text(text)
lines = [line for line in text.splitlines() if line.startswith("music:") or "StartBinkMovie" in line or "movie: start" in line or "FATAL" in line or line.startswith("CRASH")]
print("\n".join(lines[:12]))
print(f"... {len(lines)} music lines; finished={finished}")
