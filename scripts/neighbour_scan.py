#!/usr/bin/env python3
"""List every run of unattributed functions with its neighbours and call-graph hints.

Usage: python scripts/neighbour_scan.py [--json PATH] [--markdown PATH] [--min N] [--propose PATH]
                                        [--write-attribution] [--quiet]

Reads the data `tony xref coverage` reads (`build/xref/features-canonical.json`,
`re/xref/source-units.yml`, the PSX unit matches, `re/xref/attribution.yml`,
`re/native/functions.yml`, `re/symbols/functions.yml`, `re/symbols/scope.yml`) and
groups the functions that no source file claims into runs of consecutive addresses.
Address order is only a hint: MSVC emits one object's functions contiguously, but a
run between two files may belong to either, to a third file, or to a library. Each run
therefore also reports who calls it and whom it calls, and is flagged `divergent`
when its callers point away from both neighbours.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def unattributed_runs(addresses: list[int], unit_of: dict[int, str | None]) -> list[tuple[int, int]]:
    """Half-open index ranges of consecutive functions that have no unit."""

    runs, start = [], None
    for index, address in enumerate(addresses):
        if unit_of.get(address) is None:
            if start is None:
                start = index
        elif start is not None:
            runs.append((start, index))
            start = None
    if start is not None:
        runs.append((start, len(addresses)))
    return runs


def call_graph(functions: list[dict]) -> dict[int, set[int]]:
    """Reverse call graph restricted to known functions: callee -> callers."""

    known = {f["a"] for f in functions}
    callers: dict[int, set[int]] = {}
    for function in functions:
        for callee in function.get("callees", []):
            if callee in known and callee != function["a"]:
                callers.setdefault(callee, set()).add(function["a"])
    return callers


def scan(functions: list[dict], unit_of: dict[int, str | None], *, curated: set[int] | None = None,
         native: set[int] | None = None, scope: dict[int, str] | None = None,
         subsystem_of: dict[str, str] | None = None) -> list[dict]:
    """One record per run of unattributed functions, in address order."""

    ordered = sorted(functions, key=lambda f: f["a"])
    addresses = [f["a"] for f in ordered]
    curated, native, scope, subsystem_of = curated or set(), native or set(), scope or {}, subsystem_of or {}
    callers = call_graph(ordered)
    size_of = {f["a"]: f["size"] for f in ordered}
    records = []
    for start, end in unattributed_runs(addresses, unit_of):
        members = addresses[start:end]
        inside = set(members)
        left = unit_of.get(addresses[start - 1]) if start else None
        right = unit_of.get(addresses[end]) if end < len(addresses) else None
        caller_units, callee_units = Counter(), Counter()
        external_callers = external_callees = 0
        for function in ordered[start:end]:
            for caller in callers.get(function["a"], ()):
                if caller not in inside:
                    external_callers += 1
                    caller_units[unit_of.get(caller) or "(unattributed)"] += 1
            for callee in function.get("callees", []):
                if callee in size_of and callee not in inside:
                    external_callees += 1
                    callee_units[unit_of.get(callee) or "(unattributed)"] += 1
        # Callers say who uses the code; callees mostly say which shared helpers it uses (every screen
        # calls the font and screen-tool libraries), so only callers decide the flag.
        evidence = Counter({u: n for u, n in caller_units.items() if u != "(unattributed)"})
        top = evidence.most_common(3)
        neighbours = {u for u in (left, right) if u}
        # Divergent: the strongest attributed caller unit is neither neighbour and outweighs both.
        best_neighbour = max((evidence[u] for u in neighbours), default=0)
        divergent = bool(top) and top[0][0] not in neighbours and top[0][1] > best_neighbour
        if left and left == right:
            position = "interior"
        elif left and right:
            position = "boundary"
        else:
            position = "head" if not left else "tail"
        records.append({
            "start": members[0],
            "end": members[-1],
            "end_exclusive": members[-1] + size_of[members[-1]],
            "functions": len(members),
            "bytes": members[-1] + size_of[members[-1]] - members[0],
            "named": sum(1 for a in members if a in curated),
            "native": sum(1 for a in members if a in native),
            "out_of_scope": sum(1 for a in members if a in scope),
            "left": left,
            "right": right,
            "position": position,
            "subsystems": sorted({subsystem_of[u] for u in neighbours if u in subsystem_of}),
            "external_callers": external_callers,
            "external_callees": external_callees,
            "evidence": [[unit, count] for unit, count in top],
            "callee_evidence": [[u, n] for u, n in callee_units.most_common(3) if u != "(unattributed)"],
            "divergent": divergent,
        })
    return records


def function_votes(functions: list[dict], unit_of: dict[int, str | None], data_range: tuple[int, int],
                   *, max_sharers: int = 12) -> dict[int, dict[str, int]]:
    """Per unattributed function: attributed units that call it or share a data address with it.

    A data address referenced by only a few functions is a file-local static, and MSVC lays statics out
    in object order, so a function that touches the same static as an attributed one is probably in that
    file. Addresses shared by many functions (tables, engine globals) carry no information.
    """

    low, high = data_range
    callers = call_graph(functions)
    sharers: dict[int, set[int]] = {}
    for function in functions:
        for address in function.get("abs", []):
            if low <= address < high:
                sharers.setdefault(address, set()).add(function["a"])
    votes: dict[int, dict[str, int]] = {}
    for function in functions:
        if unit_of.get(function["a"]) is not None:
            continue
        tally: Counter = Counter()
        tally.update(unit_of[c] for c in callers.get(function["a"], ()) if unit_of.get(c))
        for address in set(function.get("abs", [])):
            group = sharers.get(address, ())
            if low <= address < high and len(group) <= max_sharers:
                for other in group:
                    if other != function["a"] and unit_of.get(other):
                        tally[unit_of[other]] += 1
        votes[function["a"]] = dict(tally)
    return votes


def best_split(left_votes: list[int], right_votes: list[int]) -> tuple[int, int]:
    """Split index k (first `k` functions go left) and its margin over the next best split."""

    scores = []
    for k in range(len(left_votes) + 1):
        score = sum(left_votes[:k]) - sum(right_votes[:k]) + sum(right_votes[k:]) - sum(left_votes[k:])
        scores.append((score, k))
    ranked = sorted(scores, reverse=True)
    margin = ranked[0][0] - (ranked[1][0] if len(ranked) > 1 else 0)
    return ranked[0][1], margin


def propose(records: list[dict], functions: list[dict], unit_of: dict[int, str | None],
            votes: dict[int, dict[str, int]]) -> list[dict]:
    """One attribution row per function of every run that has a left and a right neighbour.

    Basis `interior` (both neighbours are one file), `layout` (the vote-chosen split of a boundary run, a
    margin of at least two votes) or `boundary` (no usable evidence: reported with both neighbours and no
    unit, so nothing is guessed). A function whose strongest voter is a third file is `review`.
    """

    ordered = sorted(functions, key=lambda f: f["a"])
    index = {f["a"]: i for i, f in enumerate(ordered)}
    rows = []
    for record in records:
        if record["position"] not in ("interior", "boundary"):
            continue
        members = [f["a"] for f in ordered[index[record["start"]]:index[record["end"]] + 1]]
        left, right = record["left"], record["right"]
        tallies = [votes.get(a, {}) for a in members]
        if record["position"] == "interior":
            cut, margin, units = len(members), 0, [left] * len(members)
            basis = "interior"
        else:
            cut, margin = best_split([t.get(left, 0) for t in tallies], [t.get(right, 0) for t in tallies])
            informative = any(t.get(left, 0) or t.get(right, 0) for t in tallies)
            basis = "layout" if informative and margin >= 2 else "boundary"
            units = [left if i < cut else right for i in range(len(members))] if basis == "layout"                 else [None] * len(members)
        for address, unit, tally in zip(members, units, tallies):
            strongest = max(tally.items(), key=lambda item: item[1], default=(None, 0))
            # A function many files use is a utility: its strongest caller says nothing, so it is not flagged.
            third = (strongest[0] not in (None, left, right) and strongest[1] >= 2 and len(tally) <= 3
                     and strongest[1] > max(tally.get(left, 0), tally.get(right, 0)))
            rows.append({"address": address, "unit": unit, "basis": basis, "between": [left, right],
                         "margin": margin, "review": bool(third), "third": strongest[0] if third else None})
    return rows


def attribution_ranges(proposals: list[dict], addresses: list[int], skip: set[int] | None = None) -> list[dict]:
    """Group per-function proposals into `{first, last, unit, basis}` rows over consecutive functions.

    `addresses` is every function address in order: two proposals only merge when no other function sits
    between them. Rows flagged `review`, without a unit, or in `skip` (out of scope) are left out.
    """

    skip = skip or set()
    position = {address: index for index, address in enumerate(addresses)}
    rows: list[dict] = []
    last_index = None
    for row in sorted(proposals, key=lambda r: r["address"]):
        if row["unit"] is None or row["review"] or row["address"] in skip:
            continue
        index = position[row["address"]]
        previous = rows[-1] if rows else None
        if (previous and last_index is not None and index == last_index + 1 and previous["unit"] == row["unit"]
                and previous["basis"] == row["basis"]):
            previous["last"] = row["address"]
            previous["margin"] = min(previous["margin"], row["margin"])
        else:
            rows.append({"first": row["address"], "last": row["address"], "unit": row["unit"],
                         "basis": row["basis"], "margin": row["margin"]})
        last_index = index
    return rows


def write_generated_attribution(path: Path, rows: list[dict]) -> None:
    """Rewrite the `generated:` section of attribution.yml; the `reviewed:` section is preserved verbatim."""

    text = path.read_text(encoding="utf-8") if path.is_file() else ""
    reviewed_at, generated_at = text.find("\nreviewed:\n") + 1, text.find("\ngenerated:\n") + 1
    if not reviewed_at or not generated_at:
        raise SystemExit(f"{path}: expected top-level `reviewed:` and `generated:` sections")
    header, reviewed = text[:reviewed_at], text[reviewed_at:generated_at]
    lines =[f"  - {{first: {r['first']:#010x}, last: {r['last']:#010x}, unit: {json.dumps(r['unit'])}, "
             f"basis: {r['basis']}, margin: {r['margin']}}}" for r in rows]
    path.write_text(header + reviewed + "generated:\n" + "\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def summarise(records: list[dict]) -> dict:
    by_position = Counter()
    for record in records:
        by_position[record["position"]] += record["functions"]
    return {
        "runs": len(records),
        "functions": sum(r["functions"] for r in records),
        "bytes": sum(r["bytes"] for r in records),
        "named": sum(r["named"] for r in records),
        "native": sum(r["native"] for r in records),
        "divergent_runs": sum(1 for r in records if r["divergent"]),
        "divergent_functions": sum(r["functions"] for r in records if r["divergent"]),
        "functions_by_position": dict(by_position),
    }


def render_markdown(records: list[dict], summary: dict, minimum: int = 1) -> str:
    lines = [
        "# Neighbour scan: runs of unattributed functions", "",
        (f"{summary['functions']} functions in {summary['runs']} runs ({summary['bytes']} bytes); "
         f"{summary['named']} curated, {summary['native']} native; {summary['divergent_runs']} runs "
         f"({summary['divergent_functions']} functions) have call-graph evidence pointing away from both "
         f"neighbours. Position: {summary['functions_by_position']}. Proposals by basis: "
         f"{summary.get('proposals', {})}, {summary.get('proposals_review', 0)} need review."), "",
        "| Range | Fns | Bytes | Named | Native | Left | Right | Position | Callers/callees (top units) | Flag |",
        "|---|---:|---:|---:|---:|---|---|---|---|---|",
    ]
    for record in records:
        if record["functions"] < minimum:
            continue
        evidence = ", ".join(f"{u} {c}" for u, c in record["evidence"]) or "-"
        lines.append(
            f"| `{record['start']:#010x}..{record['end']:#010x}` | {record['functions']} | {record['bytes']} | "
            f"{record['named']} | {record['native']} | {record['left'] or '-'} | {record['right'] or '-'} | "
            f"{record['position']} | {evidence} | {'**divergent**' if record['divergent'] else ''} |")
    return "\n".join(lines) + "\n"


def load_model() -> tuple[list[dict], dict[int, str | None], dict]:
    """The real data: functions (no thunks, no Unwind), unit of each, and the id sets coverage uses."""

    from tony.common import load_yaml
    from tony.xref.common import CANONICAL, load_features
    from tony.xref.coverage import attribution

    functions = [f for f in load_features(CANONICAL)["functions"]
                 if not f["thunk"] and not f["name"].startswith("Unwind@")]
    unit_of = attribution(functions, generated=False)  # the scan proposes the generated rows itself
    extras = {
        "curated": {int(f["address"]) for f in load_yaml("re/symbols/functions.yml")["functions"]},
        "native": {int(f["address"]) for f in load_yaml("re/native/functions.yml")["functions"]},
    }
    return functions, unit_of, extras


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--json", type=Path, default=ROOT / "build/xref/neighbour-scan.json")
    parser.add_argument("--markdown", type=Path, default=ROOT / "build/xref/neighbour-scan.md")
    parser.add_argument("--min", type=int, default=1, help="hide runs smaller than N functions in the markdown")
    parser.add_argument("--propose", type=Path, default=ROOT / "build/xref/neighbour-proposals.json",
                        help="per-function attribution proposals (interior, layout votes, boundary)")
    parser.add_argument("--write-attribution", action="store_true",
                        help="rewrite the `generated:` section of re/xref/attribution.yml from the proposals")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    from tony.xref.coverage import scope_map, subsystem_by_unit

    functions, unit_of, extras = load_model()
    records = scan(functions, unit_of, curated=extras["curated"], native=extras["native"],
                   scope={a: row["category"] for a, row in scope_map().items()},
                   subsystem_of=subsystem_by_unit())
    summary = summarise(records)
    from tony.xref.common import canonical_ranges

    votes = function_votes(functions, unit_of, canonical_ranges()[1])
    proposals = propose(records, functions, unit_of, votes)
    summary["proposals"] = dict(Counter(row["basis"] for row in proposals))
    summary["proposals_review"] = sum(1 for row in proposals if row["review"])
    if args.write_attribution:
        scope = scope_map()
        rows = attribution_ranges(proposals, [f["a"] for f in sorted(functions, key=lambda f: f["a"])], set(scope))
        write_generated_attribution(ROOT / "re/xref/attribution.yml", rows)
        summary["attribution_rows"] = len(rows)
    args.propose.parent.mkdir(parents=True, exist_ok=True)
    args.propose.write_text(json.dumps(proposals, indent=1) + "\n", encoding="utf-8")
    for path in (args.json, args.markdown):
        path.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps({"summary": summary, "runs": records}, indent=1) + "\n", encoding="utf-8")
    args.markdown.write_text(render_markdown(records, summary, args.min), encoding="utf-8", newline="\n")
    if not args.quiet:
        print(render_markdown(records, summary, args.min))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
