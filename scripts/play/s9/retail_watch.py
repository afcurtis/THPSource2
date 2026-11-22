"""usage: python scripts/play/s9/retail_watch.py LABEL DELIVERIES ADDR:BYTES[,...] -> a retail boot-menu run (hooks off) with OPENTONY_SESSION_WATCH; the watch lines."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tony import session_scenarios as ss  # noqa: E402

label, deliveries, watch = sys.argv[1], int(sys.argv[2]), sys.argv[3]
doc = ss.load_session("boot-menu")
doc["deliveries"] = deliveries
ok = ss.session_run(label, doc, hooks_spec="none", extra_env={"OPENTONY_SESSION_WATCH": watch})
print("done" if ok else "FAILED")
run = ss.run_dir(label)
for path in sorted(run.glob("*.txt")) + sorted(run.glob("*.log")):
    for line in path.read_text(errors="replace").splitlines():
        if "watch" in line:
            print(line)
