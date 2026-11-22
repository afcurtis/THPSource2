import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10h"), exist_ok=True)  # outputs stay in build/
import glob
import re
import sys

import yaml

reached = {}
for path in sorted(glob.glob("build/m10h/reach_warehouse-*.txt")):
    scenario = path.split("reach_")[1][:-4]
    for line in open(path):
        m = re.search(r"resolve: (?:engine|vcall)_([0-9a-f]{8})\b", line)
        if m:
            reached.setdefault(int(m.group(1), 16), set()).add(scenario)
        m = re.search(r"resolve: \S+ -> 0x([0-9a-f]+)", line)
        if m:
            reached.setdefault(int(m.group(1), 16), set()).add(scenario)

doc = yaml.safe_load(open("re/config/native_hooks.yml"))
rows = doc["hooks"] if isinstance(doc, dict) and "hooks" in doc else doc
off = [r for r in rows if isinstance(r, dict) and r.get("default") == "off"]
print("rows", len(rows), "off", len(off))


def addr(row):
    for key in ("address", "addr"):
        if key in row:
            return int(str(row[key]), 16) if isinstance(row[key], str) else row[key]
    return None


hit = [r for r in off if addr(r) in reached]
print("off rows reached by resolver:", len(hit), "of", len(off), "(addressless rows:", sum(1 for r in off if addr(r) is None), ")")
standalone = yaml.safe_load(open("re/config/standalone_natives.yml"))
srows = standalone["natives"] if isinstance(standalone, dict) and "natives" in standalone else standalone
saddr = {addr(r) for r in srows if isinstance(r, dict)}
print("standalone natives:", len(saddr), "reached by resolver:", len(saddr & set(reached)))
with open("build/m10h/reach_off_rows.txt", "w") as out:
    for r in sorted(hit, key=addr):
        out.write("0x%08x %s %s\n" % (addr(r), r["name"], ",".join(sorted(reached[addr(r)]))))
    out.write("# NOT REACHED\n")
    for r in sorted((r for r in off if addr(r) not in reached), key=lambda r: addr(r) or 0):
        out.write("0x%08x %s\n" % (addr(r) or 0, r["name"]))
print(sorted(rows[0].keys()) if rows and isinstance(rows[0], dict) else "")
