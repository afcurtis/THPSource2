"""rerun_seed.py LEVEL SEED [FRAMES]: run one plain fuzz seed again and print the process's exit code and the last lines (a silent stop leaves no FATAL line)."""
import os
import subprocess
import sys

sys.path.insert(0, os.path.expanduser("~/THPSource2"))
_run = subprocess.run


def run(*a, **k):
    r = _run(*a, **k)
    print("EXIT CODE", r.returncode, flush=True)
    return r


subprocess.run = run
os.environ["LEVEL"] = sys.argv[1]
frames = sys.argv[3] if len(sys.argv) > 3 else "600"
sys.argv = ["fuzz_hangar.py", sys.argv[2], "1", frames]
exec(compile(open((os.path.expanduser("~/THPSource2") + "/scripts/play/m12/fuzz_hangar.py")).read(), "fuzz_hangar.py", "exec"))
