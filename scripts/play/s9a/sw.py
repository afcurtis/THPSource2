"""usage: sw.py SESSION DELIVERIES WATCHADDRS -> the standalone's run with OT_RESOLVER_WATCH; prints watch lines to stdout file"""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/s9a"), exist_ok=True)  # outputs stay in build/
import sys
sys.path.insert(0, ".")
from tony import session_scenarios as ss, standalone_boot as sb
name, deliveries, watch = sys.argv[1], int(sys.argv[2]), sys.argv[3]
doc = ss.load_session(name); doc["deliveries"] = deliveries
done, text = sb.run_standalone(name + "-w", doc, extra={"OT_RESOLVER_WATCH": watch})
print("done" if done else "FAILED")
open("build/s9a/sw.out", "w").write(text)
