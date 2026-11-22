"""crashbis.py SCENARIO [--names FILE]: which new (5.1t) rows crash a session when on?

Candidates = the rows of re/config/native_hooks.yml after the "Phase 5.1t: <kit>" markers. Each probe enables a subset
through opentony.ini ([hooks] default = manifest + Name = on) and asks whether the probe finishes (session.done).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tony import hooks, session_scenarios as ss  # noqa: E402

INI = ROOT / "build/disc/files/SETUP/data/opentony.ini"


def candidates() -> list[str]:
    text = (ROOT / "re/config/native_hooks.yml").read_text()
    tail = text[text.index("# Phase 5.1t: audio"):]
    return re.findall(r"(?m)^  - name: (\S+)", tail)


def probe(scenario: str, names: list[str]) -> bool:
    base = INI.read_text()
    text = re.sub(r"(?m)^default\s*=.*$", "default = manifest", base)
    text = text.replace("[hooks]", "[hooks]\n" + "".join(f"{n} = on\n" for n in names), 1)
    INI.write_text(text)
    try:
        document = ss.load_session(scenario)
        ok = ss.session_run(f"{scenario}-cbis", document, hooks.ParityVariant("cbis", "ini"), timeout=120)
    finally:
        INI.write_text(base)
    return ok


def main() -> None:
    scenario = sys.argv[1]
    names = candidates()
    print(len(names), "candidates", flush=True)
    if probe(scenario, names):
        print("all new rows on: session finishes; nothing to bisect"); return
    culprits: list[str] = []
    pool = names
    while pool:
        # find the smallest crashing prefix by halving
        lo, hi = 0, len(pool)   # pool[:hi] crashes (given culprits excluded); find minimal hi
        base_off = [c for c in culprits]
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if not probe(scenario, pool[:mid]):
                hi = mid
            else:
                lo = mid
            print(f"  prefix {lo}..{hi}", flush=True)
        c = pool[hi - 1]
        print("CULPRIT", c, flush=True)
        culprits.append(c)
        pool = [n for n in pool[hi:]]
        # rows after the culprit: do they crash on their own (without the culprit)?
        if not pool or probe(scenario, pool):
            break
    print("culprits:", culprits)


main()
