#!/usr/bin/env python3
"""Phase 5.4a spike driver: natives in a process with no retail image.

    python scripts/standalone_spike.py run [SESSION [DELIVERY]]   # default: boot-career-hangar 5000
    python scripts/standalone_spike.py census IMAGE               # what the writable image says the standalone must provide

`run` (1) runs the session once under the hook harness with every hook retail, dumping the writable image and the large
private regions at DELIVERY (OPENTONY_SESSION_DUMP) and calling the argument-less getters of the Kit hooks at that moment
(OPENTONY_SESSION_ORACLE), which gives the answers retail has; (2) runs opentony_spike.exe, which reserves the retail address
range without loading the retail image, copies .rdata/.data out of THawk2.exe as a file, lays the dump over it, attaches
the Kits and calls the same getters natively. Exit 0 when every answer is equal (a bool result may differ in the bytes above AL:
the spike reports those apart).

`census` counts the code addresses in a dumped image and in .rdata (vtable slots, dispatch tables) and what each is.
"""

from __future__ import annotations

import collections
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import yaml  # noqa: E402

from tony import static_data  # noqa: E402
from tony.common import load_yaml, wine_env  # noqa: E402

WORK = ROOT / "build/standalone-spike"
AVOID = ("Set", "Alloc", "Construct", "Nothing", "Next", "Spare", "Debug", "Reset", "Init", "Clear")


def getters() -> list[tuple[str, int]]:
    """Kit hooks that take no argument, return a value and reach no service; names that say they change something are left out."""

    rows = yaml.safe_load((ROOT / "re/config/native_hooks.yml").read_text())["hooks"]
    return [
        (row["name"], row["builds"]["canonical"])
        for row in rows
        if row.get("kit") and row.get("reaches") == [] and row.get("returns") and row.get("stack_args") == 0
        and row.get("convention") == "cdecl" and not any(word in row["name"] for word in AVOID)
    ]


def run(session: str, delivery: int) -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    wanted = getters()
    label = f"spike-{session}"
    env = dict(os.environ, OPENTONY_SESSION_ORACLE=",".join(f"{address:x}" for _, address in wanted))
    subprocess.run(
        [sys.executable, "-m", "tony", "session", "run", session, "--hooks", "none", "--label", label, "--dump", str(delivery)],
        cwd=ROOT, env=env, check=True, capture_output=True, text=True,
    )
    runs = ROOT / "build/sessions/runs" / label
    log = (runs / "opentony.log").read_text(errors="replace")
    answers = {int(m.group(1), 16): m.group(2) for m in re.finditer(r"session: oracle ([0-9A-Fa-f]{8}) = ([0-9A-Fa-f]{8})", log)}
    expected = WORK / "expected.tsv"
    expected.write_text("".join(f"{name}\t{answers[address]}\n" for name, address in wanted if address in answers))
    print(f"retail answered {sum(1 for _, a in wanted if a in answers)} of {len(wanted)} getters")

    spike = ROOT / "build/hooks/kits/opentony_spike.exe"
    wine = wine_env(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    wine["WINEDEBUG"] = "-all"
    result = subprocess.run(
        ["wine", str(spike), str(static_data.ensure_image()), str(ROOT / "build/hooks/kits/opentony_standalone.dll"),
         str(runs / f"session_{delivery}.img"), str(expected), str(runs / f"session_{delivery}.heap")],
        cwd=ROOT, env=wine, capture_output=True, text=True, timeout=300,
    )
    (WORK / "spike.txt").write_text(result.stdout + result.stderr)
    print(result.stdout[-2500:])
    if result.returncode != 0:
        print(result.stderr[-1500:])
    return result.returncode


def census(image_path: Path) -> int:
    from tony.native_progress import load_native_progress
    from tony.xref import coverage

    import pefile

    image = image_path.read_bytes()
    pe = pefile.PE(str(ROOT / "build/disc/files/SETUP/data/THawk2.exe"))
    sections = {s.Name.rstrip(b"\0").decode(): s for s in pe.sections}
    text_lo = 0x400000 + sections[".text"].VirtualAddress
    text_hi = text_lo + sections[".text"].Misc_VirtualSize
    functions = {int(f["address"]) for f in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    exported = ROOT / "build/ghidra/functions.json"
    if exported.is_file():
        functions |= {int(f["address"]) for f in json.loads(exported.read_text())}
    hooked = {h["builds"]["canonical"] for h in yaml.safe_load((ROOT / "re/config/native_hooks.yml").read_text())["hooks"]}
    native = {int(f["address"]): f.get("status") for f in yaml.safe_load((ROOT / "re/native/functions.yml").read_text())["functions"]}
    seams = {int(s["address"]) for s in yaml.safe_load((ROOT / "re/symbols/seams.yml").read_text())["seams"]}
    features = json.loads((ROOT / "build/xref/features-canonical.json").read_text())
    features = features["functions"] if isinstance(features, dict) and "functions" in features else features
    oos = coverage.scope_map(coverage._library_functions(features), native=set(load_native_progress()))

    data_words = struct.unpack(f"<{len(image) // 4}I", image[: len(image) // 4 * 4])
    rdata = pe.get_data(sections[".rdata"].VirtualAddress, sections[".rdata"].Misc_VirtualSize)
    rdata_words = struct.unpack(f"<{len(rdata) // 4}I", rdata[: len(rdata) // 4 * 4])
    slots = collections.Counter()
    for source, words in ((".data", data_words), (".rdata", rdata_words)):
        for w in words:
            if text_lo <= w < text_hi and w in functions:
                slots[w] += 1
    print(f"code addresses in tables: {sum(slots.values())} slots, {len(slots)} distinct functions")
    classes: dict[str, list[int]] = collections.defaultdict(list)
    for address in slots:
        if address in hooked:
            cls = "hooked native"
        elif address in seams:
            cls = "seam (out of scope, named)"
        elif address in oos:
            cls = "out of scope, no seam: " + oos[address]["category"]
        elif native.get(address):
            cls = "native, no hook row"
        elif address >= 0x512000:
            cls = "import thunk (Win32 API)"
        else:
            cls = "unmapped"
        classes[cls].append(address)
    for cls, members in sorted(classes.items(), key=lambda item: -len(item[1])):
        print(f"  {cls:44s} {len(members):5d} functions {sum(slots[a] for a in members):6d} slots")
    inside = sum(1 for w in data_words if 0x524000 <= w < 0x524000 + len(image))
    outside = sum(1 for w in data_words if 0x00800000 <= w < 0x7ff00000 and not 0x524000 <= w < 0x524000 + len(image))
    print(f"words pointing into .data: {inside}; into other process memory (zones, CRT heap, DirectX): {outside}")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) >= 2 and argv[0] == "census":
        return census(Path(argv[1]))
    if argv and argv[0] == "run":
        return run(argv[1] if len(argv) > 1 else "boot-career-hangar", int(argv[2]) if len(argv) > 2 else 5000)
    print(__doc__)
    return 64


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
