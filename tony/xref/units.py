"""Attribute canonical functions to Neversoft source files.

Retail THPS2 keeps its assert machinery: a function that asserts pushes the literal
``H:\\TonyHawk\\Pc2\\<file>.cpp``. MSVC emits each object's functions contiguously, so
a function bracketed by two functions asserting in the same file is attributed to it.

Bases: ``assert`` (direct reference), ``bracketed`` (between two same-file asserts),
``assert-multi`` (references several files, typically inlined header code).
"""

from __future__ import annotations

import re
from collections import Counter

from ..common import load_yaml
from .common import CANONICAL, load_features, write_rows

ASSERT_PATH = re.compile(r"^[A-Za-z]:\\TonyHawk\\Pc2\\(?P<file>.+)$", re.IGNORECASE)


def attribute(functions: list[dict]) -> dict[int, tuple[str, str]]:
    ordered = sorted(functions, key=lambda f: f["a"])
    direct: dict[int, tuple[str, str]] = {}
    for function in ordered:
        files = sorted({m.group("file").lower() for s in function["strs"] if (m := ASSERT_PATH.match(s))})
        if len(files) == 1:
            direct[function["a"]] = (files[0], "assert")
        elif files:
            direct[function["a"]] = ("|".join(files), "assert-multi")
    result = dict(direct)
    last_file, last_index = None, None
    addresses = [f["a"] for f in ordered]
    for index, address in enumerate(addresses):
        if address not in direct or direct[address][1] != "assert":
            continue
        unit = direct[address][0]
        if last_file == unit and last_index is not None:
            for between in addresses[last_index + 1:index]:
                result.setdefault(between, (unit, "bracketed"))
        last_file, last_index = unit, index
    return result


def write_units() -> dict:
    functions = load_features(CANONICAL)["functions"]
    units = attribute(functions)
    counts = Counter(basis for _unit, basis in units.values())
    header = {
        "version": 1,
        "generated_by": "tony xref units",
        "canonical_sha256": load_yaml("re/config/binaries.yml")["executables"]["thps2_pc"]["sha256"],
        "summary": {
            "functions": len(functions),
            "attributed": len(units),
            "by_basis": dict(sorted(counts.items())),
            "files": len({u for u, b in units.values() if b != "assert-multi"}),
        },
    }
    rows = [[a, unit, basis] for a, (unit, basis) in sorted(units.items())]
    write_rows("re/xref/source-units.yml", header, {"functions": ("[canonical, source_file, basis]", rows)})
    return header["summary"]


def load_units() -> dict[int, tuple[str, str]]:
    doc = load_yaml("re/xref/source-units.yml")
    return {a: (unit, basis) for a, unit, basis in doc.get("functions") or []}
