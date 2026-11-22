from __future__ import annotations

import struct

from tony import session_scenarios as ss
from tony.cli import build_parser
from tony.common import load_yaml
from tony.scenarios import scenario_files

SESSIONS = [
    "boot-career-end-run",
    "boot-career-hangar",
    "boot-create-skater",
    "boot-menu",
    "boot-options",
    "boot-park-editor",
    "boot-saved-data",
]


def test_session_manifests_are_valid_and_complete():
    assert [path.stem for path in ss.scenario_files()] == SESSIONS
    for path in ss.scenario_files():
        document = load_yaml(path)
        assert ss.validate_session(document, path.name) == []


def test_the_session_masks_fit_the_recorders_table():
    # session_run sends DEFAULT_MASKS, HEAP_ADDRESS_MASKS and the scenario's own; the recorder keeps the first 32 (kMaxMasks)
    for path in ss.scenario_files():
        document = load_yaml(path)
        masks = ",".join([ss.DEFAULT_MASKS, ss.HEAP_ADDRESS_MASKS, *document.get("masks", [])]).split(",")
        assert len(masks) <= 32, path.name


def test_session_files_stay_out_of_the_warehouse_scenario_list():
    assert not {path.stem for path in scenario_files()} & set(SESSIONS)


def test_keys_spec_matches_the_probe_syntax():
    document = ss.load_session("boot-options")
    assert ss.keys_spec(document) == "500:cb:5,560:1c:5,700:43:2,900:1:5,1000:43:2"


def test_validation_rejects_bad_keys():
    document = {"version": 1, "description": "x", "deliveries": 10,
                "keys": [{"poll": 5, "key": "nope", "hold": 1}, {"poll": 2, "key": "esc", "hold": 0}]}
    errors = ss.validate_session(document)
    assert any("unknown key" in e for e in errors)
    assert any("not before the previous key" in e for e in errors)
    assert any("hold must be" in e for e in errors)


def test_trace_roundtrip(tmp_path, monkeypatch):
    monkeypatch.setattr(ss, "RUN_ROOT", tmp_path)
    run = tmp_path / "x"
    run.mkdir()
    header = b"OTSESS1\0" + struct.pack("<4I", 0x10000, 2, 3, 1) + struct.pack("<2I", 0x400000, 0x30000)
    body = struct.pack("<5I", 2, 1, 7, 8, 9) + struct.pack("<5I", 4, 2, 7, 8, 10)
    (run / "session.trace").write_bytes(header + body)
    trace = ss.load_trace("x")
    assert [r[0] for r in trace[3]] == [2, 4]
    assert ss.blocks_differing(trace, trace) == {}
    other = (trace[0], trace[1], trace[2], [trace[3][0], (4, 2, (7, 9, 10))])
    assert ss.blocks_differing(trace, other) == {1: [1]}
    assert ss.block_address(2, trace[0], trace[2]) == 0x420000


def test_session_command_is_registered():
    args = build_parser().parse_args(["session", "parity", "boot-menu"])
    assert args.names == ["boot-menu"]


def test_variants_include_the_shipped_selection_and_the_stress_reference():
    variants = {v.label: v for v in ss.session_variants()}
    assert variants["native-default"].hooks == "default"
    assert variants["native-default"].expect_equal
    assert variants["native-all-stress"].stress_ms and variants["retail-stress"].hooks == "none"
    assert not variants["fault-Math_Vector3Add"].expect_equal


def _varying_size(hooks_spec: str, label: str, stress: bool, dump: int) -> int:
    env = {
        "OPENTONY_HOOKS": hooks_spec,
        "OPENTONY_LOG_DIR": "Z:/home/player/THPSource2/build/sessions/runs/" + label,
        "OPENTONY_SESSION_DUMP": str(dump or 99999999),
    }
    if stress:
        env["OPENTONY_HOOK_STRESS"] = "3"
    ss.pad_environment(env)
    return sum(len(k) + len(v) + 2 for k, v in env.items())


def test_environment_is_one_size_for_every_run_of_a_matrix():
    # 7a: the Wine heap moves with the environment block, and the runs of a matrix differ in these four variables
    sizes = {
        _varying_size(variant.hooks, f"boot-career-end-run-{variant.label}", bool(variant.stress_ms), dump)
        for variant in ss.session_variants()
        for dump in (0, 5000)
    }
    assert len(sizes) == 1
    assert _varying_size("none", "x", False, 0) == _varying_size("fault:Math_Vector3Add", "a-much-longer-label", True, 12345678)


def test_environment_pad_refuses_a_run_that_does_not_fit():
    import pytest

    env = {"OPENTONY_LOG_DIR": "Z:/" + "x" * ss.ENV_VARYING_BYTES}
    with pytest.raises(ss.SessionError):
        ss.pad_environment(env)


def test_run_directories_of_a_matrix_have_one_length():
    # 7a: the game's log directory is converted to a wide string on the Wine heap for every file the probe opens
    names = {len(ss.session_label("boot-options", variant)) for variant in ss.session_variants()}
    assert len(names) == 1
    assert all(ss.session_label("boot-menu", v).startswith("boot-menu-") for v in ss.session_variants())
