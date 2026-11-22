"""reach.py NAME...: the standalone on each Warehouse scenario with OT_RESOLVER_TRACE=1; writes build/m10h/reach_NAME.txt (the `kit: resolve:` lines)"""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10h"), exist_ok=True)  # outputs stay in build/
import sys

sys.path.insert(0, ".")
from tony import standalone_warehouse as sw  # noqa: E402
from tony.scenarios import load_scenario  # noqa: E402

for name in sys.argv[1:]:
    scenario = load_scenario(name)
    original = sw.sb.run_standalone

    def patched(n, document, dump="", extra=None, _o=original):
        return _o(n, document, dump=dump, extra=dict(extra or {}, OT_RESOLVER_TRACE="1"))

    sw.sb.run_standalone = patched
    done, text = sw.run_standalone(name, scenario)
    sw.sb.run_standalone = original
    lines = [line for line in text.splitlines() if "resolve:" in line]
    open(f"build/m10h/reach_{name}.txt", "w").write("\n".join(lines) + "\n")
    print(name, "done" if done else "NOT DONE", len(lines), flush=True)
