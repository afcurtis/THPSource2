"""Scope map, attribution rows and sub-milestone sizing (4.12 cartography)."""

from __future__ import annotations

from itertools import pairwise
from pathlib import Path

import pytest

from tony.common import load_yaml
from tony.xref import coverage, milestones

ROOT = Path(__file__).resolve().parents[1]
FEATURES = ROOT / "build/xref/features-canonical.json"


def fn(address: int, size: int = 0x20) -> dict:
    return {"a": address, "size": size, "callees": [], "thunk": False, "name": f"FUN_{address:08x}"}


def test_scope_map_applies_ranges_functions_keep_and_native(monkeypatch):
    document = {
        "categories": {"crt": {}, "platform-hal": {}},
        "ranges": [{"first": 0x2000, "last": 0x20FF, "category": "crt", "note": "runtime"}],
        "functions": [{"address": 0x1000, "category": "platform-hal", "note": "hook point"}],
        "keep": [{"address": 0x2010, "reason": "adapter calls it"}],
    }
    monkeypatch.setattr(coverage, "scope_document", lambda: document)
    functions = [fn(0x1000), fn(0x1100), fn(0x2000), fn(0x2010), fn(0x2020), fn(0x2200)]
    result = coverage.scope_map(functions, native={0x2020})
    assert result == {0x1000: {"category": "platform-hal", "note": "hook point"},
                      0x2000: {"category": "crt", "note": "runtime"}}


def test_attribution_prefers_reviewed_rows_over_generated(monkeypatch):
    monkeypatch.setattr(coverage, "load_units", lambda: {0x1000: ("a.cpp", "assert"), 0x1010: ("m.cpp", "assert-multi")})
    monkeypatch.setattr(coverage, "load_psx", lambda: [{"canonical": 0x1020, "unit": "PSX"}])
    document = {"reviewed": [{"first": 0x1030, "last": 0x1030, "unit": "Rev.cpp"}],
                "generated": [{"first": 0x1030, "last": 0x1040, "unit": "gen.cpp"},
                              {"first": 0x1000, "last": 0x1000, "unit": "ignored.cpp"}]}
    monkeypatch.setattr(coverage, "load_yaml", lambda _path: document)
    functions = [fn(a) for a in (0x1000, 0x1010, 0x1020, 0x1030, 0x1040, 0x1050)]
    assert coverage.attribution(functions) == {0x1000: "a.cpp", 0x1010: None, 0x1020: "psx.cpp", 0x1030: "rev.cpp",
                                               0x1040: "gen.cpp", 0x1050: None}
    only_reviewed = coverage.attribution(functions, generated=False)
    assert only_reviewed[0x1040] is None and only_reviewed[0x1030] == "rev.cpp"


def test_milestones_assign_ranges_first_then_units_and_skip_out_of_scope(monkeypatch):
    document = {
        "milestones": [{"id": "A", "units": ["one.cpp"]}, {"id": "B", "units": ["two.cpp"]}, {"id": "C", "units": []}],
        "ranges": [{"first": 0x3000, "last": 0x3010, "milestone": "C"}],
    }
    monkeypatch.setattr(milestones, "milestone_document", lambda: document)
    functions = [fn(0x1000), fn(0x2000), fn(0x3000), fn(0x3010), fn(0x4000), fn(0x5000)]
    unit_of = {0x1000: "one.cpp", 0x2000: "Two.cpp", 0x3000: "one.cpp", 0x3010: None, 0x4000: None, 0x5000: "one.cpp"}
    assigned = milestones.assign(functions, unit_of, {0x5000: {"category": "crt"}})
    assert assigned == {0x1000: "A", 0x2000: "B", 0x3000: "C", 0x3010: "C", 0x4000: None}


def test_milestone_report_sizes_remaining_work_against_the_session_ceiling(monkeypatch):
    document = {"milestones": [{"id": "A", "title": "alpha", "units": ["a.cpp"]},
                               {"id": "B", "title": "beta", "units": ["b.cpp"]}], "ranges": []}
    monkeypatch.setattr(milestones, "milestone_document", lambda: document)
    functions = [fn(0x1000, 20_000), fn(0x2000, 20_000), fn(0x3000, 100), fn(0x4000, 100)]
    unit_of = {0x1000: "a.cpp", 0x2000: "a.cpp", 0x3000: "b.cpp", 0x4000: None}
    rows = milestones.report(functions, unit_of, {}, native={0x3000})
    by_id = {row["id"]: row for row in rows}
    assert (by_id["A"]["functions"], by_id["A"]["remaining"], by_id["A"]["remaining_bytes"]) == (2, 2, 40_000)
    assert by_id["A"]["sessions"] == 2  # 40 KB needs two 28 KB sessions
    assert by_id["B"]["native"] == 1 and by_id["B"]["remaining"] == 0 and by_id["B"]["sessions"] == 0
    assert by_id["unassigned"]["functions"] == 1


def test_sessions_use_the_stricter_of_bytes_and_function_count():
    assert milestones.sessions(0, 0) == 0
    assert milestones.sessions(10, 1_000) == 1
    assert milestones.sessions(66, 1_000) == 2
    assert milestones.sessions(10, 28_001) == 2
    assert milestones.sessions(130, 1_000) == 2


def test_committed_scope_file_is_well_formed():
    document = load_yaml("re/symbols/scope.yml")
    categories = document["categories"]
    assert categories and all(row.get("title") and row.get("replacement") for row in categories.values())
    for row in document["ranges"]:
        assert int(row["first"]) <= int(row["last"]) and row["category"] in categories and row["note"]
    seen = set()
    for row in document["functions"]:
        address = int(row["address"])
        assert row["category"] in categories and row["note"] and address not in seen
        seen.add(address)
    for row in document["keep"]:
        assert row["reason"]
    ordered = sorted((int(r["first"]), int(r["last"])) for r in document["ranges"])
    assert all(a[1] < b[0] for a, b in pairwise(ordered)), "scope ranges overlap"


def test_committed_milestone_and_attribution_files_are_well_formed():
    document = load_yaml("re/xref/milestones.yml")
    ids = [row["id"] for row in document["milestones"]]
    assert len(ids) == len(set(ids))
    claimed: dict[str, str] = {}
    for row in document["milestones"]:
        for unit in row["units"]:
            assert unit.lower() not in claimed, f"{unit} is listed under {claimed[unit.lower()]} and {row['id']}"
            claimed[unit.lower()] = row["id"]
    for row in document["ranges"]:
        assert int(row["first"]) <= int(row["last"]) and row["milestone"] in ids and row["note"]
    attribution = load_yaml("re/xref/attribution.yml")
    for section in ("reviewed", "generated"):
        for row in attribution.get(section) or []:
            assert int(row["first"]) <= int(row["last"]) and row["unit"] and row["basis"]


@pytest.mark.skipif(not FEATURES.is_file(), reason="xref features not exported")
def test_every_in_scope_function_has_a_sub_milestone_and_every_scope_row_exists():
    report = coverage.compute()
    total = report["total"]
    assert total["in_scope"] + total["out_of_scope"] == total["functions"]
    assert [row for row in report["milestones"] if row["id"] == "unassigned"] == []
    assert sum(row["functions"] for row in report["milestones"]) == total["in_scope"]
    assert sum(row["functions"] for row in report["out_of_scope"]) == total["out_of_scope"]
    from tony.xref.common import CANONICAL, load_features

    known = {f["a"] for f in load_features(CANONICAL)["functions"]}
    for row in coverage.scope_document()["functions"]:
        assert int(row["address"]) in known, f"{int(row['address']):#x} is not a function of the canonical build"
