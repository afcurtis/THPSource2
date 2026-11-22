"""usage: python scripts/play/m10g/retail_watch2.py LABEL SESSION DELIVERIES ADDR:BYTES[,...] -> a retail run of SESSION (hooks off, lockstep-fast) with OPENTONY_SESSION_WATCH; the watch lines with their delivery and the poll of that delivery."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tony import session_scenarios as ss  # noqa: E402

label, session, deliveries, watch = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
doc = ss.load_session(session)
doc["deliveries"] = deliveries
ok = ss.session_run(label, doc, hooks_spec="none", extra_env={"OPENTONY_SESSION_WATCH": watch}, timeout=900.0)
print("done" if ok else "FAILED")
run = ss.run_dir(label)
polls = {}
try:
    _bs, _every, _regions, records = ss.load_trace(label)
    polls = {d: p for d, p, _h in records}
except Exception as error:  # noqa: BLE001
    print("no trace:", error)
out = open(ROOT / "build/m10g" / f"{label}.watch.txt", "w")
for path in sorted(run.glob("*.txt")) + sorted(run.glob("*.log")):
    for line in path.read_text(errors="replace").splitlines():
        if "watch" in line and "delivery" in line:
            d = int(line.rsplit("delivery", 1)[1].split()[0])
            nearest = polls.get(d) or polls.get(d + 1) or polls.get(d - 1)
            out.write(f"{line} [poll {nearest}]\n")
print("written", out.name)
