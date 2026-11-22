"""Harvest function/global names from pinned community hook projects.

Only declarations that bind a *name* to an *address* are collected, e.g.::

    static const Foo_Init_t Foo_Init = (Foo_Init_t)0x00401000;
    void (__cdecl *Bar_Startup)() = 0x00402000;
    int* SomeGlobal = (int*)0x00500000;

Commented-out declarations are kept (flagged) because they still record the author's
identification. Code addresses must be exact function entries in the variant build;
mid-function patch sites are ignored. Addresses are translated onto the canonical build
through ``re/xref/<variant>.yml``.
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

from .buildmap import load_map
from .common import config, load_features, reference_dir, write_rows

_ADDRESS = r"(0x00?[45][0-9a-fA-F]{5})"
PATTERNS = (
    re.compile(r"\(\s*(?:__\w+\s+)?\*\s*(?:__\w+\s+)?(?P<n>[A-Za-z_]\w*)\s*\)\s*\([^;{}]*?\)\s*=\s*"
               r"(?:\([^)]*\)\s*)?" + _ADDRESS),
    re.compile(r"(?P<n>[A-Za-z_]\w*)\s*(?:\[\s*\d*\s*\])?\s*=\s*(?:\(\s*[\w\s\*:<>,()]*?\)\s*)?"
               + _ADDRESS + r"\s*[;,)]"),
    re.compile(r"#define\s+(?P<n>[A-Za-z_]\w*)\s+\(*(?:\([^)]*\)\s*)?" + _ADDRESS),
)
GENERIC = {"addr", "address", "ptr", "func", "fn", "result", "base", "tag", "value", "orig"}
# Tool-generated placeholders (Ghidra/IDA default names) carry no identification.
_PLACEHOLDER = re.compile(r"^(FUN|DAT|PTR|LAB|sub|unk|dword|byte|off|word)_[0-9A-Fa-f]+$|^unkVal\w*$")
SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".cs"}
COMMUNITY = ("partymod_thps2", "thps2_cumulative_patch")


def fetch_references(names: list[str] | None = None) -> None:
    refs = config()["references"]
    for name in names or list(refs):
        spec = refs[name]
        target = reference_dir(name)
        if not (target / ".git").is_dir():
            target.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "clone", "--quiet", spec["url"], str(target)], check=True)
        subprocess.run(["git", "-C", str(target), "fetch", "--quiet", "origin", spec["commit"]], check=False)
        subprocess.run(["git", "-C", str(target), "checkout", "--quiet", "--detach", spec["commit"]], check=True)
        print(f"{name}: {spec['commit']} -> {target}")


def require_reference(name: str) -> Path:
    spec = config()["references"][name]
    target = reference_dir(name)
    if not target.is_dir():
        raise SystemExit(f"missing reference {name}; run: tony xref fetch-references")
    head = subprocess.run(["git", "-C", str(target), "rev-parse", "HEAD"], capture_output=True, text=True,
                          check=True).stdout.strip()
    if head != spec["commit"]:
        raise SystemExit(f"{target} is at {head}, expected pinned {spec['commit']}; run: tony xref fetch-references")
    return target


def extract_declarations(text: str) -> list[tuple[int, str, int, bool]]:
    """Return ``(line, name, address, commented)`` declarations found in one source file."""

    found = []
    for number, line in enumerate(text.splitlines(), 1):
        commented = line.lstrip().startswith("//")
        for pattern in PATTERNS:
            for match in pattern.finditer(line):
                name = match.group("n")
                if name.lower() in GENERIC or name.endswith("_t") or len(name) < 3 or _PLACEHOLDER.match(name):
                    continue
                found.append((number, name, int(match.group(match.lastindex), 16), commented))
    return found


def harvest(variant: str = "original_2000") -> list[dict]:
    spec = config()["variants"][variant]
    text_lo, text_hi = spec["text"]
    data_lo, data_hi = spec["data"]
    entries = {f["a"] for f in load_features(variant)["functions"]}
    rows, seen = [], set()
    for tool in COMMUNITY:
        root = require_reference(tool)
        for path in sorted(root.rglob("*")):
            if path.suffix.lower() not in SOURCE_SUFFIXES or ".git" in path.parts:
                continue
            for line, name, address, commented in extract_declarations(path.read_text(errors="ignore")):
                if text_lo <= address < text_hi:
                    if address not in entries:
                        continue
                    kind = "function"
                elif data_lo <= address < data_hi:
                    kind = "global"
                else:
                    continue
                key = (tool, address, name)
                if key in seen:
                    continue
                seen.add(key)
                rows.append({"tool": tool, "kind": kind, "name": name, "variant": address,
                             "source": f"{path.relative_to(root)}:{line}", "commented": commented})
    return rows


def write_community(variant: str = "original_2000") -> dict:
    rows = harvest(variant)
    functions, data, _meta = load_map(variant)
    out = []
    for row in rows:
        mapping = functions if row["kind"] == "function" else data
        out.append([row["tool"], row["kind"], row["name"], row["variant"], mapping.get(row["variant"]),
                    row["source"], row["commented"]])
    out.sort(key=lambda r: (r[1], r[4] or 0, r[0], r[2]))
    mapped = sum(1 for r in out if r[4] is not None)
    header = {
        "version": 1,
        "generated_by": f"tony xref community --variant {variant}",
        "variant": variant,
        "references": {t: config()["references"][t]["commit"] for t in COMMUNITY},
        "summary": {
            "declarations": len(out),
            "functions": sum(1 for r in out if r[1] == "function"),
            "globals": sum(1 for r in out if r[1] == "global"),
            "mapped_to_canonical": mapped,
        },
    }
    write_rows("re/xref/community.yml", header, {
        "names": ("[tool, kind, name, variant_address, canonical_address, source, commented]", out),
    })
    return header["summary"]


def load_community() -> list[dict]:
    from ..common import load_yaml

    keys = ("tool", "kind", "name", "variant", "canonical", "source", "commented")
    return [dict(zip(keys, row)) for row in load_yaml("re/xref/community.yml").get("names") or []]
