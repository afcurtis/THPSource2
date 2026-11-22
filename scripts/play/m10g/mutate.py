#!/usr/bin/env python3
"""mutate.py SRC FIXTURE[,FIXTURE...] MUTANTS.py [ONLY_PREFIX]: replay the skater test over single-edit mutants of SRC.

MUTANTS.py defines MUTANTS = [(name, from, to), ...]. A mutant survives when the replay still reports 0 mismatches. SRC is restored at the end (also on error).
"""
import re
import subprocess
import sys

root = (__import__("os").path.expanduser("~/THPSource2") + "/")
src, fixtures, mutants_file = sys.argv[1], sys.argv[2].split(","), sys.argv[3]
only = sys.argv[4] if len(sys.argv) > 4 else ""
ns = {}
exec(open(mutants_file).read(), ns)
mutants = ns["MUTANTS"]
path = root + src
original = open(path).read()
test = root + "build/native/opentony_skater_retail_calls_test"


def build():
    subprocess.run(["bash", root + "scripts/play/m10e/nb.sh", "opentony_skater_retail_calls_test"], cwd=root, capture_output=True)


def replay():
    out = subprocess.run([test] + [root + f for f in fixtures], cwd=root, capture_output=True, text=True)
    text = out.stdout + out.stderr
    m = re.search(r"(\d+) cases replayed, (\d+) mismatches", text)
    return (int(m.group(1)), int(m.group(2))) if m else (0, -1)


try:
    for name, a, b in mutants:
        if only and not name.startswith(only):
            continue
        if a not in original:
            print("%s: PATTERN NOT FOUND" % name, flush=True)
            continue
        open(path, "w").write(original.replace(a, b, 1))
        build()
        cases, bad = replay()
        flag = "SURVIVED" if bad == 0 else ("build/run error" if bad < 0 else "")
        print("%s: %d cases, %d mismatches %s" % (name, cases, bad, flag), flush=True)
finally:
    open(path, "w").write(original)
    build()
    print("restored; baseline: %s" % (replay(),), flush=True)
