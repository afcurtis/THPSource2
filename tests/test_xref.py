import yaml

from tony.xref import common as xref_common
from tony.xref.apply import psx_symbol_name
from tony.xref.buildmap import BuildMatcher
from tony.xref.community import extract_declarations
from tony.xref.features import normalize_operand
from tony.xref.units import attribute


def function(address, h, size=32, callees=(), absolute=(), strs=(), n=10, hm=None):
    return {"a": address, "name": f"FUN_{address:08x}", "size": size, "n": n, "bb": 1, "h": h,
            "hm": hm or f"m{h}", "callees": list(callees), "abs": list(absolute), "consts": [],
            "strs": list(strs), "thunk": False}


def test_normalize_operand_masks_only_in_image_addresses():
    assert normalize_operand("dword ptr [0x0055e85c]") == "dword ptr [A]"
    assert normalize_operand("0x10") == "0x10"
    assert normalize_operand("dword ptr [ESI + 0x3118]") == "dword ptr [ESI + 0x3118]"


def test_build_matcher_hash_seed_order_gapfill_and_data_map():
    variant = [
        function(0x401000, "aa", callees=[0x401100], absolute=[0x530000]),
        function(0x401100, "x1", size=40),
        function(0x401200, "bb", absolute=[0x530010]),
    ]
    canonical = [
        function(0x402000, "aa", callees=[0x402100], absolute=[0x540000]),
        function(0x402100, "y1", size=44),
        function(0x402200, "bb", absolute=[0x540010]),
    ]
    matcher = BuildMatcher(variant, canonical)
    matcher.run()
    assert matcher.match == {0x401000: 0x402000, 0x401100: 0x402100, 0x401200: 0x402200}
    assert matcher.method[0x401000] == "hash"
    assert matcher.method[0x401100] in {"callgraph", "order"}
    data, conflicts = matcher.data_map()
    assert data == {0x530000: 0x540000, 0x530010: 0x540010}
    assert conflicts == 0
    assert matcher.consistency(data)[0x401000] == 1.0
    assert matcher.monotone() == set(matcher.match)


def test_build_matcher_ignores_ambiguous_hashes():
    variant = [function(0x401000, "dup"), function(0x401100, "dup")]
    canonical = [function(0x402000, "dup"), function(0x402100, "dup")]
    matcher = BuildMatcher(variant, canonical)
    matcher.seed()
    assert matcher.match == {}


def test_extract_declarations_recognises_community_idioms():
    source = (
        "static const Career_Init_t Career_Init = (Career_Init_t)0x004138f0;\n"
        "\tvoid (__cdecl *D3DMODEL_Startup)() = 0x004cffb0;\n"
        "int* GStatus = (int*)0x0055e85c;\n"
        "//static const Career_Got_t Career_Got = (Career_Got_t)0x00414030;\n"
        "CPatch::SetChar(0x004cddd7, 0);\n"
    )
    found = extract_declarations(source)
    assert (1, "Career_Init", 0x004138F0, False) in found
    assert (2, "D3DMODEL_Startup", 0x004CFFB0, False) in found
    assert (3, "GStatus", 0x0055E85C, False) in found
    assert (4, "Career_Got", 0x00414030, True) in found
    assert all(name != "SetChar" for _line, name, _address, _commented in found)


def test_attribute_brackets_runs_between_same_file_asserts():
    path = "H:\\TonyHawk\\Pc2\\physics.cpp"
    functions = [
        function(0x401000, "a", strs=[path]),
        function(0x401100, "b"),
        function(0x401200, "c", strs=[path, "some message"]),
        function(0x401300, "d"),
        function(0x401400, "e", strs=["H:\\TonyHawk\\Pc2\\rail.cpp"]),
    ]
    units = attribute(functions)
    assert units[0x401000] == ("physics.cpp", "assert")
    assert units[0x401100] == ("physics.cpp", "bracketed")
    assert 0x401300 not in units
    assert units[0x401400] == ("rail.cpp", "assert")


def test_psx_symbol_name_is_ghidra_safe():
    assert psx_symbol_name("Career_Init__Fv", "Career_Init(void)") == "Career_Init"
    assert psx_symbol_name("AI__11CBackground", "CBackground::AI(void)") == "CBackground__AI"
    assert psx_symbol_name("__11CBackgroundUiRC8CSVector",
                           "CBackground::(unsigned int, const CSVector &)") == "CBackground__ctor"
    assert psx_symbol_name("_._11CBackground", "_._11CBackground") == "CBackground__dtor"


def test_write_rows_round_trips_hex_addresses(tmp_path, monkeypatch):
    monkeypatch.setattr(xref_common, "resolve", lambda path: tmp_path / path)
    target = xref_common.write_rows("out.yml", {"version": 1}, {
        "functions": ("[a, b, method, flag, score]", [[0x00401000, 0x00402000, "hash", True, 0.5]]),
        "empty": ("[a]", []),
    })
    text = target.read_text()
    assert "[0x00401000, 0x00402000, \"hash\", true, 0.5]" in text
    doc = yaml.safe_load(text)
    assert doc["functions"] == [[0x401000, 0x402000, "hash", True, 0.5]]
    assert doc["empty"] == []
