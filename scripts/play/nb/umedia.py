"""umedia.py TAG [custom]: a Hangar idle run whose media folder (OPENTONY_MEDIA_DIR) has a name that is not ASCII (build/nb/Gäme, a link to the test install's Data/Game),
FFmpeg loaded; with `custom`, the custom soundtrack from build/nb/Músic too. Prints the music and movie lines: a song that opens has no StartBinkMovie error."""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/nb"), exist_ok=True)  # outputs stay in build/
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def _z(path):  # Wine path without resolving: the non-ASCII names here are symlinks that must stay as named
    return "Z:" + str(path).replace("/", chr(92))


sys.path.insert(0, str(ROOT))
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402

_run = sb.subprocess.run
sb.subprocess.run = lambda *a, **k: _run(*a, **{**k, "errors": "replace"})   # the program may print a path in the ANSI code page
tag = sys.argv[1]
custom = len(sys.argv) > 2 and sys.argv[2] == "custom"
sb.set_arch(64)
for name, target in (("G\u00e4me", _os.environ["OT_TEST_INSTALL"] + "/Data/Game"), ("M\u00fasic", _os.environ["OT_TEST_INSTALL"] + "/Mods/Music")):
    link = Path((os.path.expanduser("~/THPSource2") + "/build/nb")) / name
    if not link.exists():
        os.symlink(target, link)
d = Path((os.path.expanduser("~/THPSource2") + "/build/nb/umedia")) / tag
d.mkdir(parents=True, exist_ok=True)
spec = d / "scenario.txt"
cap = d / "standalone.otcap"
spec.write_text("level 0\nframes 300\nsha256 f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c\n")
cap.unlink(missing_ok=True)
extra = {"OT_SCENARIO": sw._wine_z_path(spec), "OT_SCENARIO_OUT": sw._wine_z_path(cap),
         "OPENTONY_MEDIA_DIR": _z(ROOT / "build/nb" / "G\u00e4me") + "\\",
         "OPENTONY_FFMPEG_DIR": _z(ROOT / ".tools/ffmpeg/x86_64/bin") + "\\", "OT_MOVIE_LOG": "1"}
if custom:
    extra.update({"OT_CUSTOM_MUSIC": "1", "OT_MUSIC_DIR": _z(ROOT / "build/nb" / "M\u00fasic")})
document = {"name": f"umd{tag}", "deliveries": 7000, "every": 2, "keys": [], "masks": []}
finished, text = sb.run_standalone(f"nb-umd{tag}", document, extra=extra)
(d / "output.txt").write_text(text)
lines = [line for line in text.splitlines() if line.startswith("music:") or "StartBinkMovie" in line or "movie: start" in line or "FATAL" in line or line.startswith("CRASH")]
print("\n".join(lines[:6]))
print(f"finished={finished}")
