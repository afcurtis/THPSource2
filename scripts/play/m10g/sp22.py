"""sp22.py NAME DELIVERIES PREFIXES VAR=value...: the standalone on a Warehouse scenario as `tony standalone warehouse` runs it (FFmpeg, media, zone block), with extra
variables and a delivery limit; prints the run's lines that start with one of the comma-separated PREFIXES"""
import sys

sys.path.insert(0, ".")
from tony import standalone_boot as sb  # noqa: E402
from tony import standalone_warehouse as sw  # noqa: E402
from tony.scenarios import load_scenario  # noqa: E402

name, deliveries, prefixes = sys.argv[1], int(sys.argv[2]), tuple(sys.argv[3].split(","))
more = dict(a.split("=", 1) for a in sys.argv[4:])
original = sb.run_standalone


def patched(n, document, dump="", extra=None):
    return original(n, dict(document, deliveries=deliveries), dump=dump, extra=dict(extra or {}, **more))


sb.run_standalone = patched
done, text = sw.run_standalone(name, load_scenario(name))
print("done", done)
for line in text.splitlines():
    if line.startswith(prefixes):
        print(line[:300])
