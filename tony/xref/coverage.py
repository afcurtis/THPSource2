"""Named-function coverage of the canonical executable, overall and per subsystem.

Two reviewed files refine the raw assert-path attribution (`re/xref/source-units.yml`):

* `re/xref/attribution.yml` assigns still-unattributed functions to a source unit by range, with the
  basis recorded (interior of one file, call graph, PSX layout, ...).
* `re/symbols/scope.yml` tags functions that Phase 4 replaces instead of reconstructing (C runtime,
  zlib, platform backends behind the HAL, ...). The completion figure is native / in-scope.
"""

from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

from ..common import load_yaml, resolve
from .apply import load_imported
from .common import CANONICAL, load_features
from .milestones import report as milestone_report
from .psx import load_psx
from .units import load_units

TIERS = ("curated", "inferred", "provisional", "unnamed")
ATTRIBUTION = "re/xref/attribution.yml"
SCOPE = "re/symbols/scope.yml"


def _subsystem_table() -> tuple[list[dict], dict[str, str], int | None]:
    doc = load_yaml("re/xref/subsystems.yml")
    table, by_unit = doc["subsystems"], {}
    for subsystem in table:
        for unit in subsystem.get("units", []):
            by_unit[unit.lower()] = subsystem["id"]
    library = doc.get("library_start")
    return table, by_unit, int(library) if library is not None else None


def subsystem_by_unit() -> dict[str, str]:
    return _subsystem_table()[1]


def _library_functions(everything: list[dict]) -> list[dict]:
    # Compiler-generated C++ EH unwind funclets and import thunks are not nameable source functions.
    return [f for f in everything if not f["thunk"] and not f["name"].startswith("Unwind@")]


def attribution_rows(*, generated: bool = True) -> list[dict]:
    """Reviewed rows first (they override), then the rows `scripts/neighbour_scan.py` proposed."""

    try:
        document = load_yaml(ATTRIBUTION)
    except FileNotFoundError:
        return []
    return list(document.get("reviewed") or []) + (list(document.get("generated") or []) if generated else [])


def attribution(functions: list[dict], *, generated: bool = True) -> dict[int, str | None]:
    """Source unit of every function: assert paths, PSX unit matches, then the reviewed ranges."""

    units = load_units()
    psx_units = {row["canonical"]: row["unit"].lower() + ".cpp" for row in load_psx()}
    ranges = attribution_rows(generated=generated)
    result: dict[int, str | None] = {}
    for function in functions:
        address = function["a"]
        unit = None
        if address in units and units[address][1] != "assert-multi":
            unit = units[address][0]
        elif address in psx_units:
            unit = psx_units[address]
        else:
            for row in ranges:
                if int(row["first"]) <= address <= int(row["last"]):
                    unit = str(row["unit"]).lower()
                    break
        result[address] = unit
    return result


def scope_document() -> dict:
    try:
        return load_yaml(SCOPE)
    except FileNotFoundError:
        return {"version": 1, "categories": {}, "ranges": [], "functions": [], "keep": []}


def scope_map(functions: list[dict] | None = None, native: set[int] | None = None) -> dict[int, dict]:
    """Address -> {category, note} of every function tagged out of scope.

    A function with a native reconstruction is in scope by definition (the runtime wrappers and the
    `d3dfunc` natives stay), as is anything the file lists under `keep`.
    """

    document = scope_document()
    if functions is None:
        functions = _library_functions(load_features(CANONICAL)["functions"])
    if native is None:
        native = {int(f["address"]) for f in load_yaml("re/native/functions.yml")["functions"]}
    kept = {int(row["address"]) for row in document.get("keep") or []}
    explicit = {int(row["address"]): row for row in document.get("functions") or []}
    result: dict[int, dict] = {}
    for function in functions:
        address = function["a"]
        if address in native or address in kept:
            continue
        row = explicit.get(address)
        if row is None:
            for candidate in document.get("ranges") or []:
                if int(candidate["first"]) <= address <= int(candidate["last"]):
                    row = candidate
                    break
        if row is not None:
            result[address] = {"category": row["category"], "note": row.get("note", "")}
    return result


def compute() -> dict:
    everything = load_features(CANONICAL)["functions"]
    functions = _library_functions(everything)
    excluded = {"thunks": sum(1 for f in everything if f["thunk"]),
                "unwind_funclets": sum(1 for f in everything if f["name"].startswith("Unwind@"))}
    curated = {int(f["address"]) for f in load_yaml("re/symbols/functions.yml")["functions"]}
    native = {int(f["address"]): f["status"] for f in load_yaml("re/native/functions.yml")["functions"]}
    imported = {int(f["address"]): f["confidence"] for f in load_imported().get("functions", [])}
    unit_of = attribution(functions)
    scope = scope_map(functions, set(native))
    table, by_unit, library_start = _subsystem_table()
    categories = scope_document().get("categories") or {}

    rows = defaultdict(Counter)
    unit_rows = defaultdict(Counter)
    unmapped_units = Counter()
    scope_rows = defaultdict(Counter)
    for function in functions:
        address = function["a"]
        if address in curated:
            tier = "curated"
        elif address in imported:
            tier = imported[address] if imported[address] in TIERS else "provisional"
        else:
            tier = "unnamed"
        unit = unit_of[address]
        if unit is not None:
            subsystem = by_unit.get(unit)
            if subsystem is None:
                unmapped_units[unit] += 1
                subsystem = "unassigned-unit"
        elif library_start is not None and address >= library_start:
            subsystem = "runtime-library"
        else:
            subsystem = "unattributed"
        out_of_scope = address in scope
        for bucket in (rows[subsystem], rows["TOTAL"]):
            bucket[tier] += 1
            bucket["functions"] += 1
            if out_of_scope:
                bucket["out_of_scope"] += 1
            if address in native:
                bucket["native"] += 1
        if out_of_scope:
            category = scope[address]["category"]
            scope_rows[category]["functions"] += 1
            scope_rows[category]["bytes"] += function["size"]
        if unit:
            unit_rows[unit][tier] += 1
            unit_rows[unit]["functions"] += 1

    def finish(counter: Counter) -> dict:
        total = counter["functions"]
        named = total - counter["unnamed"]
        in_scope = total - counter["out_of_scope"]
        return {
            "functions": total, **{t: counter[t] for t in TIERS}, "named": named,
            "named_pct": round(100 * named / total, 1) if total else 0.0,
            "named_inferred_or_better_pct": round(100 * (counter["curated"] + counter["inferred"]) / total, 1)
            if total else 0.0,
            "native": counter["native"],
            "out_of_scope": counter["out_of_scope"],
            "in_scope": in_scope,
            "native_pct_of_in_scope": round(100 * counter["native"] / in_scope, 1) if in_scope else 0.0,
        }

    titles = {s["id"]: s["title"] for s in table}
    titles.update({"runtime-library": "C runtime and third-party libraries (by address)",
                   "unattributed": "Not yet attributed to a source file",
                   "unassigned-unit": "Source file not assigned to a subsystem"})
    order = [s["id"] for s in table] + ["runtime-library", "unattributed", "unassigned-unit"]
    return {
        "ghidra_functions": len(everything),
        "excluded": excluded,
        "total": finish(rows["TOTAL"]),
        "subsystems": [{"id": s, "title": titles[s], **finish(rows[s])} for s in order if rows[s]["functions"]],
        "out_of_scope": [{"category": c, "title": categories.get(c, {}).get("title", c),
                          "replacement": categories.get(c, {}).get("replacement", ""),
                          "functions": n["functions"], "bytes": n["bytes"]}
                         for c, n in sorted(scope_rows.items())],
        "units": {u: finish(c) for u, c in sorted(unit_rows.items())},
        "unmapped_units": dict(unmapped_units),
        "milestones": milestone_report(functions, unit_of, scope, set(native)),
    }


def render_markdown(report: dict) -> str:
    total = report["total"]
    lines = [
        "# Named-function coverage (canonical THawk2.exe)",
        "",
        ("Generated by `tony xref coverage`. Tiers: **curated** = `re/symbols/functions.yml`; "
         "**inferred** / **provisional** = `re/symbols/imported.yml`."),
        "",
        (f"**Total: {total['named']} / {total['functions']} functions named ({total['named_pct']}%)** - "
         f"curated {total['curated']}, inferred {total['inferred']}, provisional {total['provisional']}; "
         f"{total['native']} with native reconstructions."),
        "",
        (f"**Completion: {total['native']} / {total['in_scope']} in-scope functions native "
         f"({total['native_pct_of_in_scope']}%)**; {total['out_of_scope']} tagged out of scope in "
         f"`{SCOPE}`."),
        "",
        (f"Denominator excludes {report['excluded']['unwind_funclets']} compiler-generated `Unwind@` funclets "
         f"and {report['excluded']['thunks']} thunks out of {report['ghidra_functions']} Ghidra functions."),
        "",
        "| Subsystem | Functions | Curated | Inferred | Provisional | Named % | Inferred+ % | Native | Out of scope |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in report["subsystems"]:
        lines.append(f"| {row['title']} | {row['functions']} | {row['curated']} | {row['inferred']} | "
                     f"{row['provisional']} | {row['named_pct']} | {row['named_inferred_or_better_pct']} | "
                     f"{row['native']} | {row['out_of_scope']} |")
    if report["out_of_scope"]:
        lines += ["", "| Out-of-scope category | Functions | Bytes | Replaced by |", "|---|---:|---:|---|"]
        for row in report["out_of_scope"]:
            lines.append(f"| {row['title']} | {row['functions']} | {row['bytes']} | {row['replacement']} |")
    if report["milestones"]:
        lines += ["", "In-scope work per sub-milestone (`re/xref/milestones.yml`; ceiling 28 KB or 65 functions per session):", "",
                  "| Sub-milestone | In scope | Native | Remaining | Remaining bytes | Sessions |", "|---|---:|---:|---:|---:|---:|"]
        for row in report["milestones"]:
            lines.append(f"| {row['id']} {row['title']} | {row['functions']} | {row['native']} | {row['remaining']} | "
                         f"{row['remaining_bytes']} | {row['sessions']} |")
    return "\n".join(lines) + "\n"


def write_coverage(output: Path | None = None) -> dict:
    report = compute()
    target = output or resolve("build/xref/coverage.json")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    target.with_suffix(".md").write_text(render_markdown(report), encoding="utf-8")
    return report
