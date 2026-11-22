"""Dynamic call-site coverage tracer: an independent runtime cross-check of
the static boundary audit (re/notes/platform-boundary.md, "Residual risk";
docs/phases/PHASE_3_SUMMARY.md, housekeeping item 3).

`tony hal trace sites` writes every canonical-build call-site address from
`re/config/hal_boundary.yml`'s generated inventory (874 sites: COM calls,
import calls, import jumps) to a flat file the hook DLL reads from
``OPENTONY_HAL_TRACE_SITES`` (src/hooks/hal/site_trace.cpp): one one-shot
software breakpoint per site, hits appended to ``<file>.hits`` as they occur.

`tony hal trace report` cross-references that hit log against the full
inventory: coverage by kind and by service, and which known sites a given
capture never reached (not necessarily a problem: DirectPlay sites, error
paths and rarely-taken branches are expected to be silent in a single-player
Warehouse capture).

This proves the listed sites are real (retail's own code reaches them, not
just Ghidra's static reading of it); it cannot discover a site the audit
failed to list; a dynamic check that could do that would need to intercept
every method of every live COM interface regardless of address, which
docs/phases/PHASE_3_SUMMARY.md defers with its reasoning.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ..common import resolve
from .audit import load_boundary

BUILD = "canonical"


def canonical_sites(document: dict[str, Any] | None = None) -> list[tuple[int, str, str]]:
    """Return (address, kind, service) for every generated site, canonical build."""

    document = document if document is not None else load_boundary()
    generated = document.get("generated", {})
    # functions: [canonical, original_2000, name, unit, service, hook_point, status]
    services = {row[0]: row[4] for row in generated.get("functions", []) if row[0] is not None}
    sites = []
    for row in generated.get("sites", []):
        canonical_address, _original_2000, function_address, kind, _target = row
        if canonical_address is None:
            continue
        sites.append((int(canonical_address), str(kind), services.get(function_address, "?")))
    return sites


def write_sites_file(output: str | Path) -> int:
    """Write the sites file the hook DLL's OPENTONY_HAL_TRACE_SITES reads. Returns the count."""

    sites = canonical_sites()
    path = resolve(output)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="ascii", newline="\n") as stream:
        stream.write(f"{BUILD}\n")
        for address, _kind, _service in sites:
            stream.write(f"0x{address:08x}\n")
    return len(sites)


def read_hits(path: str | Path) -> set[int]:
    hits: set[int] = set()
    hits_path = resolve(path)
    if not hits_path.is_file():
        return hits
    for line in hits_path.read_text(encoding="ascii").splitlines():
        line = line.strip()
        if not line:
            continue
        hits.add(int(line, 16))
    return hits


def coverage_report(hits_path: str | Path) -> dict[str, Any]:
    """Cross-reference a recorded hit log against the full inventory."""

    sites = canonical_sites()
    known = {address for address, _kind, _service in sites}
    hits = read_hits(hits_path)
    unknown_hits = sorted(hits - known)  # impossible by construction (breakpoints only exist at known sites)
    hit_known = hits & known

    def _bucket(key_index: int) -> dict[str, dict[str, int]]:
        totals: dict[str, dict[str, int]] = {}
        for address, kind, service in sites:
            key = kind if key_index == 0 else service
            bucket = totals.setdefault(key, {"known": 0, "hit": 0})
            bucket["known"] += 1
            if address in hit_known:
                bucket["hit"] += 1
        return totals

    return {
        "known_sites": len(known),
        "hit_sites": len(hit_known),
        "coverage": (len(hit_known) / len(known)) if known else 0.0,
        "unknown_hits": unknown_hits,
        "by_kind": _bucket(0),
        "by_service": _bucket(1),
        "never_hit": sorted(known - hit_known),
    }
