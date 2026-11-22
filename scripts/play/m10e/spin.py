"""Run the standalone cold boot of a session for a short time with extra environment; the partial output goes to build/m10e/spin.txt.
usage: spin.py NAME SECONDS [VAR=VALUE ...]"""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10e"), exist_ok=True)  # outputs stay in build/
import subprocess
import sys

from tony import standalone_boot as sb
from tony import session_scenarios as ss

name, seconds = sys.argv[1], int(sys.argv[2])
extra = dict(item.split("=", 1) for item in sys.argv[3:])
real = subprocess.run


def limited(command, **kwargs):
    kwargs["timeout"] = seconds
    try:
        return real(command, **kwargs)
    except subprocess.TimeoutExpired as error:
        class Partial:
            stdout = (error.stdout or b"").decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
            stderr = (error.stderr or b"").decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
        return Partial()


sb.subprocess.run = limited
document = ss.load_session(name)
done, text = sb.run_standalone(name, document, "", extra)
open("build/m10e/spin.txt", "w").write(text)
print("done" if done else "no session.done", len(text), "bytes")
