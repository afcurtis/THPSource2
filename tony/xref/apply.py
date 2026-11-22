"""Merge cross-reference evidence into ``re/symbols/imported.yml``.

Curated knowledge (``functions.yml`` / ``globals.yml``) always wins: an address that is
already tracked only receives an ``aliases`` record listing the external names. Untracked
addresses get an imported symbol whose name comes from, in order of preference, the PSX
demo symbol table and then the community projects.
"""

from __future__ import annotations

import re
from collections import Counter, defaultdict

import yaml

from ..common import load_yaml, resolve
from .community import load_community
from .psx import load_psx
from .units import load_units

OUTPUT = "re/symbols/imported.yml"
EVIDENCE = {
    "library": "re/evidence/xref-libraries.md",
    "psx": "re/evidence/xref-psx-demo.md",
    "community": "re/evidence/xref-community.md",
    "build": "re/evidence/xref-cross-build.md",
    "units": "re/evidence/xref-source-units.md",
}
_DTOR = re.compile(r"^_\._(\d+)(\w+)$")


def psx_symbol_name(mangled: str, demangled: str) -> str:
    """Turn a PSX (g++ 2.9) symbol into a Ghidra-safe name: ``Class::Method`` -> ``Class__Method``."""

    dtor = _DTOR.match(mangled)
    if dtor:
        return f"{dtor.group(2)[:int(dtor.group(1))]}__dtor"
    base = demangled.split("(")[0].strip()
    if base.endswith("::"):
        base = base[:-2] + "__ctor"
    base = base.replace("::", "__").replace("~", "dtor_")
    base = re.sub(r"\W+", "_", base).strip("_")
    return base or mangled


def library_symbol_name(symbol: str) -> str:
    """`_malloc` -> `CRT_malloc`; a mangled C++ name keeps only its identifier (`?_Foo@@YAXXZ` -> `CRT_Foo`)."""

    base = symbol.split("@@")[0].lstrip("?") if symbol.startswith("?") else symbol
    base = re.sub(r"\W+", "_", base.split("@")[0]).strip("_")
    return f"CRT_{base}" if base else "CRT_unnamed"


def load_library_matches() -> list[dict]:
    try:
        rows = load_yaml("re/xref/library-matches.yml").get("functions") or []
    except FileNotFoundError:
        return []
    return [dict(zip(("canonical", "symbols", "libraries", "members", "size"), row, strict=True)) for row in rows]


def _same_name(a: str, b: str) -> bool:
    """Names agree modulo case and ``Class__Method`` / ``Class_Method`` spelling."""

    def fold(name: str) -> str:
        return re.sub(r"_+", "_", name.lower())

    return fold(a) == fold(b)


def _name_rank(entry: dict) -> tuple:
    name = entry["name"]
    return (not entry["commented"], name[:1].isupper(), "_" in name, entry["tool"] == "thps2_cumulative_patch",
            -len(name))


def _unique(name: str, used: set[str]) -> str:
    candidate, n = name, 2
    while candidate.lower() in used:
        candidate, n = f"{name}_{n}", n + 1
    used.add(candidate.lower())
    return candidate


def _dump(document: dict) -> str:
    text = yaml.safe_dump(document, sort_keys=False, width=110)
    return re.sub(r"^(\s*(?:- )?address: )(\d+)$", lambda m: f"{m.group(1)}0x{int(m.group(2)):08x}", text,
                  flags=re.MULTILINE)


def build_imported() -> dict:
    tracked_functions = {int(f["address"]): f["name"] for f in load_yaml("re/symbols/functions.yml")["functions"]}
    tracked_globals = {int(g["address"]): g["name"] for g in load_yaml("re/symbols/globals.yml")["globals"]}
    used = {n.lower() for n in list(tracked_functions.values()) + list(tracked_globals.values())}
    units = load_units()
    psx = {row["canonical"]: row for row in load_psx()}
    community = defaultdict(list)
    community_globals = defaultdict(list)
    for entry in load_community():
        if entry["canonical"] is None:
            continue
        (community if entry["kind"] == "function" else community_globals)[entry["canonical"]].append(entry)

    functions, aliases, globals_, conflicts = [], [], [], []
    for address in sorted(set(psx) | set(community)):
        row = psx.get(address)
        names = sorted(community.get(address, []), key=_name_rank, reverse=True)
        sources = []
        if row:
            sources.append({"psx_demo": row["symbol"], "method": row["method"]})
        for entry in names:
            sources.append({entry["tool"]: entry["name"], "at": entry["source"]})
        if address in tracked_functions:
            aliases.append({"address": address, "tracked_name": tracked_functions[address], "sources": sources})
            continue
        if row:
            name = psx_symbol_name(row["symbol"], row["demangled"])
            confidence = row["confidence"]
            evidence = [EVIDENCE["psx"]]
            if names:
                evidence.append(EVIDENCE["community"])
                if not any(_same_name(n["name"], name) for n in names):
                    conflicts.append({"address": address, "psx": name, "community": sorted({n["name"] for n in names})})
        else:
            name = names[0]["name"]
            confidence = "provisional"
            evidence = [EVIDENCE["community"]]
        if names:
            evidence.append(EVIDENCE["build"])
        if row:
            evidence.append(EVIDENCE["units"])
        item = {"address": address, "name": _unique(name, used), "confidence": confidence}
        if address in units:
            item["source_file"] = units[address][0]
        item["sources"] = sources
        item["evidence"] = sorted(set(evidence))
        functions.append(item)

    named = {f["address"] for f in functions} | set(tracked_functions)
    for row in load_library_matches():
        address = row["canonical"]
        if address in named or "|" in row["symbols"]:
            continue  # curated or already named elsewhere; byte-identical siblings stay unnamed
        functions.append({"address": address, "name": _unique(library_symbol_name(row["symbols"]), used),
                          "confidence": "inferred",
                          "sources": [{"library": row["symbols"], "libraries": row["libraries"],
                                       "members": row["members"], "method": "masked-bytes"}],
                          "evidence": [EVIDENCE["library"]]})
    functions.sort(key=lambda f: f["address"])

    for address in sorted(community_globals):
        names = sorted(community_globals[address], key=_name_rank, reverse=True)
        sources = [{e["tool"]: e["name"], "at": e["source"]} for e in names]
        if address in tracked_globals:
            aliases.append({"address": address, "tracked_name": tracked_globals[address], "sources": sources})
            continue
        globals_.append({"address": address, "name": _unique(names[0]["name"], used), "confidence": "provisional",
                         "sources": sources, "evidence": [EVIDENCE["community"], EVIDENCE["build"]]})

    return {"functions": functions, "globals": globals_, "aliases": aliases, "conflicts": conflicts}


def write_imported() -> dict:
    result = build_imported()
    summary = {
        "functions": len(result["functions"]),
        "functions_by_confidence": dict(Counter(f["confidence"] for f in result["functions"]).most_common()),
        "globals": len(result["globals"]),
        "aliases_on_tracked": len(result["aliases"]),
        "psx_vs_community_name_conflicts": len(result["conflicts"]),
    }
    header = (
        "# Generated by `tony xref apply` from re/xref/*.yml - do not edit by hand.\n"
        "# Curated names in functions.yml/globals.yml always take precedence; promote an entry\n"
        "# there (with its own evidence) once it has been reviewed.\n"
    )
    document = {"version": 1, "summary": summary, **result}
    resolve(OUTPUT).write_text(header + _dump(document), encoding="utf-8")
    return summary


def load_imported() -> dict:
    path = resolve(OUTPUT)
    return load_yaml(OUTPUT) if path.is_file() else {"functions": [], "globals": []}
