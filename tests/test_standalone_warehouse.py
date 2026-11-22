"""Milestone 10f: a Warehouse scenario in the standalone (tony/standalone_warehouse.py, src/hooks/kits/standalone_scenario.cpp)."""

from __future__ import annotations

import os
import re
import struct
import sys
from pathlib import Path

import pytest

from tony import capture, session_scenarios, standalone_boot, standalone_warehouse
from tony.scenarios import load_scenario, scenario_files

ROOT = Path(__file__).resolve().parents[1]
KITS_DIR = ROOT / "src/hooks/kits"
SPIKE = ROOT / "build/hooks/kits/opentony_spike.exe"
WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_capture import _capture  # the synthetic .otcap of the decoder's own tests


def test_the_scenario_spec_is_what_the_recorder_reads():
    scenario = load_scenario("warehouse-rail")
    lines = standalone_warehouse.scenario_spec(scenario).splitlines()
    assert lines[:3] == ["level 12", "frames 254", f"sha256 {capture.SUPPORTED_BUILD_SHA256}"]
    actions = [line.split() for line in lines[3:]]
    assert all(words[0] == "action" and words[1].startswith("0x") for words in actions)
    # one interval per press: the yml's up, right, triangle, cross; each held until its release (or the end)
    assert [(int(w[1], 16), int(w[2]), int(w[3])) for w in actions] == [
        (capture.ACTION_MASKS[action], start, hold) for action, start, hold in capture._scenario_action_intervals(scenario)]
    assert len(actions) == 4


def test_an_idle_scenario_has_no_action_lines():
    assert standalone_warehouse.scenario_spec(load_scenario("warehouse-idle")).count("action") == 0


def test_the_recorder_reads_exactly_the_lines_the_command_writes():
    """The C++ parser has one branch per fact the Python writers emit (this command's, and the texture census's skater and look); a new fact on one side must
    reach the other."""

    import importlib.util

    spec = importlib.util.spec_from_file_location("texture_census", ROOT / "scripts" / "texture_census.py")
    census = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(census)
    source = (KITS_DIR / "standalone_scenario.cpp").read_text()
    parsed = set(re.findall(r'what == "(\w+)"', source))
    written = {line.split()[0] for line in standalone_warehouse.scenario_spec(load_scenario("warehouse-rail")).splitlines()}
    run = {"level": 12, "frames": 600, "skater": 19, "look": 3, "actions": ["0x1000 20 100"]}
    written |= {line.split()[0] for line in census.scenario_spec(run, capture.SUPPORTED_BUILD_SHA256).splitlines()}
    assert written == parsed


def test_delivery_two_is_always_dumped():
    dump = standalone_warehouse.dump_spec
    assert dump("") == "2" and dump("2") == "2" and dump("8") == "2,8" and dump("8,14") == "2,8"


def test_the_zone_block_is_read_from_the_retail_image(tmp_path, monkeypatch):
    run = tmp_path / "run"
    run.mkdir()
    monkeypatch.setattr(session_scenarios, "run_dir", lambda label: run)
    assert standalone_warehouse.zone_block("warehouse-idle") is None
    image = bytearray(0x024B7E3C)
    struct.pack_into("<4I", image, 0x0056A97C - 0x00524000, 0x091E0020, 0x091E0030, 0x091E0030, 0x097BC020)
    (run / "session_2.img").write_bytes(bytes(image))
    assert standalone_warehouse.zone_block("warehouse-idle") == 0x091E0020


def test_a_stopped_run_decodes_the_frames_it_recorded(tmp_path):
    """A FATAL leaves a capture that says it is still recording: the frames before it are decoded as a complete capture of that many."""

    source = tmp_path / "standalone.otcap"
    _capture(source, frame_count=3)
    data = bytearray(source.read_bytes())
    fields = list(capture.HEADER_STRUCT.unpack_from(data))
    fields[10], fields[11], fields[12] = 2, 256, capture.STATUS_CAPTURING   # two frames of 256 recorded, not complete
    capture.HEADER_STRUCT.pack_into(data, 0, *fields)
    prefix = list(capture.CONFIG_PREFIX_STRUCT.unpack_from(data, fields[3]))
    prefix[2] = 256
    capture.CONFIG_PREFIX_STRUCT.pack_into(data, fields[3], *prefix)
    source.write_bytes(bytes(data))
    with pytest.raises(capture.CaptureDecodeError):
        capture.decode_capture(source)   # the decoder alone refuses it

    count, frames, complaint = standalone_warehouse.standalone_frames(source, tmp_path)
    assert (count, len(frames), complaint) == (2, 2, "")
    assert standalone_warehouse.standalone_frames(tmp_path / "missing.otcap", tmp_path)[2]
    other = tmp_path / "none.otcap"
    _capture(other, frame_count=1)
    empty = bytearray(other.read_bytes())
    fields = list(capture.HEADER_STRUCT.unpack_from(empty))
    fields[10], fields[12] = 0, capture.STATUS_READY
    capture.HEADER_STRUCT.pack_into(empty, 0, *fields)
    other.write_bytes(bytes(empty))
    assert standalone_warehouse.standalone_frames(other, tmp_path) == (0, [], "")


def test_the_frame_comparison_names_the_first_differing_field(tmp_path):
    retail = tmp_path / "retail.otcap"
    _capture(retail, frame_count=3)
    otrec = tmp_path / "retail.otrec"
    capture.convert_capture(retail, otrec, force=True)
    standalone = tmp_path / "standalone.otcap"
    _capture(standalone, frame_count=3)
    count, frames, _ = standalone_warehouse.standalone_frames(standalone, tmp_path)
    assert count == 3 and standalone_warehouse.compare_frames(otrec, frames) == []
    # frame 1's player blob differs in the word _capture() stamps with the frame number (offset 8)
    data = bytearray(standalone.read_bytes())
    data_offset = capture.HEADER_STRUCT.unpack_from(data)[7]
    struct.pack_into("<I", data, data_offset + capture.FRAME_STRUCT.size * 1 + capture.FRAME_HEADER_STRUCT.size + 0x08, 0xDEAD)
    standalone.write_bytes(bytes(data))
    _, frames, _ = standalone_warehouse.standalone_frames(standalone, tmp_path)
    rows = standalone_warehouse.compare_frames(otrec, frames)
    assert rows and rows[0]["frame"] == 1


def test_every_warehouse_scenario_is_a_target_of_the_command():
    names = [path.stem for path in scenario_files()]
    assert len(names) == 9 and all(name.startswith("warehouse-") for name in names)


def test_standalone_warehouse_is_a_command():
    from tony.cli import build_parser

    args = build_parser().parse_args(["standalone", "warehouse", "warehouse-idle", "--keep-retail", "--dump", "8"])
    assert args.names == ["warehouse-idle"] and args.keep_retail and args.dump == "8" and args.func is standalone_warehouse.warehouse_command


def test_the_recorder_is_part_of_the_standalone_library():
    cmake = (KITS_DIR / "CMakeLists.txt").read_text()
    assert "standalone_scenario.cpp" in cmake and "capture/win32/shared_buffer.cpp" in cmake


def test_the_standalone_masks_still_fit_the_recorders_table():
    masks = standalone_boot.masks({}).split(",")
    assert len(masks) <= 32 and all(re.fullmatch(r"[0-9a-f]+:[0-9a-f]+", mask) for mask in masks)


@pytest.mark.skipif(not WINE or not SPIKE.is_file(), reason="OPENTONY_WINE_TESTS=1 and a built spike")
def test_a_warehouse_scenario_runs_to_a_named_stop():
    """10f's done-when: the run ends at its last frame or on a named FATAL, and the comparison says which and where the state differs."""

    result = standalone_warehouse.warehouse("warehouse-idle", keep_retail=True)
    assert result["status"] in {"match", "stopped", "diverge"}, result
    assert result["stop"].startswith("recorded all frames") or "FATAL" in result["stop"], result
    assert result["state"]["clock_mismatches"] == 0
