"""Phase 4 work remaining per sub-milestone, from `re/xref/milestones.yml`.

A function belongs to the sub-milestone that lists its source unit; a function with no unit falls back to
the address `ranges` of the same file. Out-of-scope functions (re/symbols/scope.yml) belong to none. The
report sizes what is left (functions without a native reconstruction) against the session ceiling of
4.10: about 28 KB of retail code or 65 functions, whichever is reached first.
"""

from __future__ import annotations

import math
from collections import defaultdict

from ..common import load_yaml

MILESTONES = "re/xref/milestones.yml"
CEILING_BYTES = 28_000
CEILING_FUNCTIONS = 65


def milestone_document() -> dict:
    try:
        return load_yaml(MILESTONES)
    except FileNotFoundError:
        return {"version": 1, "milestones": [], "ranges": []}


def assign(functions: list[dict], unit_of: dict[int, str | None], scope: dict[int, dict]) -> dict[int, str | None]:
    """Sub-milestone id of every in-scope function (None when nothing claims it)."""

    document = milestone_document()
    by_unit = {str(unit).lower(): milestone["id"] for milestone in document.get("milestones") or []
               for unit in milestone.get("units") or []}
    ranges = document.get("ranges") or []
    result: dict[int, str | None] = {}
    for function in functions:
        address = function["a"]
        if address in scope:
            continue
        milestone = None
        for row in ranges:
            if int(row["first"]) <= address <= int(row["last"]):
                milestone = row["milestone"]
                break
        if milestone is None:
            milestone = by_unit.get((unit_of.get(address) or "").lower())
        result[address] = milestone
    return result


def report(functions: list[dict], unit_of: dict[int, str | None], scope: dict[int, dict],
           native: set[int]) -> list[dict]:
    """One row per sub-milestone (document order), then `unassigned` when something is unclaimed."""

    document = milestone_document()
    assigned = assign(functions, unit_of, scope)
    size_of = {f["a"]: f["size"] for f in functions}
    tally: dict[str | None, dict] = defaultdict(lambda: {"functions": 0, "native": 0, "remaining": 0,
                                                         "bytes": 0, "remaining_bytes": 0, "attributed": 0})
    for address, milestone in assigned.items():
        row = tally[milestone]
        row["functions"] += 1
        row["bytes"] += size_of[address]
        if unit_of.get(address):
            row["attributed"] += 1
        if address in native:
            row["native"] += 1
        else:
            row["remaining"] += 1
            row["remaining_bytes"] += size_of[address]
    rows = []
    for milestone in document.get("milestones") or []:
        row = dict(tally.pop(milestone["id"], None) or {"functions": 0, "native": 0, "remaining": 0, "bytes": 0,
                                                          "remaining_bytes": 0, "attributed": 0})
        rows.append({"id": milestone["id"], "title": milestone.get("title", ""), **row,
                     "sessions": sessions(row["remaining"], row["remaining_bytes"])})
    if tally:
        row = tally[None]
        rows.append({"id": "unassigned", "title": "No sub-milestone claims these functions", **row,
                     "sessions": sessions(row["remaining"], row["remaining_bytes"])})
    return rows


def sessions(remaining: int, remaining_bytes: int) -> int:
    """Sessions of one ceiling (28 KB or 65 functions) the remaining work needs."""

    if not remaining:
        return 0
    return max(1, math.ceil(remaining_bytes / CEILING_BYTES), math.ceil(remaining / CEILING_FUNCTIONS))
