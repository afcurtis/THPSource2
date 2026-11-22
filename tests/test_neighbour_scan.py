"""Neighbour scan, scope map and library signature matcher (4.12 cartography)."""

from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


scan_module = _load("neighbour_scan")
signatures = _load("match_library_signatures")


def fn(address: int, size: int = 0x10, callees: list[int] | None = None) -> dict:
    return {"a": address, "size": size, "callees": callees or [], "thunk": False, "name": f"FUN_{address:08x}"}


def test_runs_group_consecutive_unattributed_functions():
    addresses = [0x1000, 0x1010, 0x1020, 0x1030, 0x1040, 0x1050]
    units = {0x1000: "a.cpp", 0x1010: None, 0x1020: None, 0x1030: "b.cpp", 0x1040: None, 0x1050: None}
    assert scan_module.unattributed_runs(addresses, units) == [(1, 3), (4, 6)]


def test_scan_reports_neighbours_position_and_counts():
    functions = [fn(0x1000), fn(0x1010), fn(0x1020, 0x30), fn(0x1050), fn(0x1060)]
    units = {0x1000: "a.cpp", 0x1010: None, 0x1020: None, 0x1050: "a.cpp", 0x1060: None}
    records = scan_module.scan(functions, units, curated={0x1010}, native={0x1010, 0x1020},
                               scope={0x1060: "crt"}, subsystem_of={"a.cpp": "core"})
    interior, tail = records
    assert (interior["start"], interior["end"], interior["functions"], interior["bytes"]) == (0x1010, 0x1020, 2, 0x40)
    assert interior["left"] == interior["right"] == "a.cpp" and interior["position"] == "interior"
    assert interior["named"] == 1 and interior["native"] == 2 and interior["subsystems"] == ["core"]
    assert tail["position"] == "tail" and tail["right"] is None and tail["out_of_scope"] == 1


def test_scan_flags_runs_whose_callers_point_away_from_both_neighbours():
    functions = [fn(0x1000), fn(0x1010), fn(0x1020), fn(0x2000, callees=[0x1010]),
                 fn(0x2010, callees=[0x1010, 0x1010]), fn(0x2020, callees=[0x1010])]
    units = {0x1000: "a.cpp", 0x1010: None, 0x1020: "b.cpp", 0x2000: "c.cpp", 0x2010: "c.cpp", 0x2020: "d.cpp"}
    (record,) = scan_module.scan(functions, units)
    assert record["divergent"] and record["evidence"][0][0] == "c.cpp"
    # The same callers inside a neighbour's file do not diverge.
    units[0x2000] = units[0x2010] = units[0x2020] = "a.cpp"
    (record,) = scan_module.scan(functions, units)
    assert not record["divergent"]


def test_summary_totals_runs():
    records = scan_module.scan([fn(0x1000), fn(0x1010)], {0x1000: None, 0x1010: None})
    summary = scan_module.summarise(records)
    assert summary["runs"] == 1 and summary["functions"] == 2 and summary["functions_by_position"] == {"head": 2}


def _coff_object(code: bytes, relocation_offsets: list[int], symbol: str = "_f") -> bytes:
    """A minimal i386 COFF object with one COMDAT .text section."""

    section_header_size, count = 40, 1
    raw = 20 + section_header_size
    reloc = raw + len(code)
    symbols = reloc + 10 * len(relocation_offsets)
    header = struct.pack("<HHIIIHH", 0x14C, count, 0, symbols, 2, 0, 0)
    section = struct.pack("<8sIIIIIIHHI", b".text", 0, 0, len(code), raw, reloc, 0, len(relocation_offsets), 0,
                          0x60501020)
    relocs = b"".join(struct.pack("<IIH", offset, 1, 0x0006) for offset in relocation_offsets)
    name = symbol.encode().ljust(8, b"\0")
    entry = name + struct.pack("<IhHBB", 0, 1, 0x20, 2, 0)
    target = b"_g".ljust(8, b"\0") + struct.pack("<IhHBB", 0, 0, 0, 2, 0)
    return header + section + code + relocs + entry + target + struct.pack("<I", 4)


def test_coff_relocated_bytes_are_wildcards():
    code = bytes([0x8B, 0x44, 0x24, 0x04, 0xA1, 0xAA, 0xBB, 0xCC, 0xDD, 0xC3]) + bytes(8)
    (section,) = signatures.parse_coff(_coff_object(code, [5]))
    assert section["comdat"] and section["symbols"] == [(0, "_f")]
    assert list(section["wild"][5:9]) == [1, 1, 1, 1] and sum(section["wild"]) == 4
    blob = {"bytes": section["bytes"], "wild": section["wild"], "member": "x.obj", "symbol": "_f"}
    other = bytearray(code)
    other[5:9] = b"\x01\x02\x03\x04"
    assert signatures.masked_equal(bytes(other), blob)
    other[1] ^= 0xFF
    assert not signatures.masked_equal(bytes(other), blob)


def test_match_functions_allows_padding_but_not_other_sizes():
    code = bytes(range(1, 33))
    blob = {"bytes": code, "wild": bytes(32), "member": "m.obj", "symbol": "_f"}
    image = bytes(16) + code + b"\xcc" * 8 + bytes(64)
    exact = {"a": 0x401010, "size": 32, "callees": [], "thunk": False, "name": "x"}
    padded = {"a": 0x401010, "size": 40, "callees": [], "thunk": False, "name": "x"}
    far = {"a": 0x401010, "size": 90, "callees": [], "thunk": False, "name": "x"}
    got = signatures.match_functions([exact, padded, far], image, 0x400000, 0x1000, {"lib": [blob]})
    assert got[0x401010][0]["symbol"] == "_f"
    assert len(got) == 1


def test_archive_members_with_long_names():
    body = b"\x00" * 4
    longnames = b"averyveryverylongname.obj\0"
    member = lambda name, data: name.ljust(16).encode() + b"0".ljust(12) + b"0".ljust(6) + b"0".ljust(6) + b"100644".ljust(8) + str(len(data)).ljust(10).encode() + b"`\n" + data + (b"\n" if len(data) & 1 else b"")
    archive = b"!<arch>\n" + member("/", b"") + member("//", longnames) + member("/0", body)
    assert signatures.parse_archive(archive) == [("averyveryverylongname.obj", body)]


@pytest.mark.skipif(not (ROOT / "build/xref/features-canonical.json").is_file(), reason="xref features not exported")
def test_coverage_report_has_scope_columns():
    from tony.xref import coverage

    report = coverage.compute()
    total = report["total"]
    assert total["in_scope"] + total["out_of_scope"] == total["functions"]
    assert all("out_of_scope" in row and "in_scope" in row for row in report["subsystems"])
