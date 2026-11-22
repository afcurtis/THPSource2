"""Milestone 9: the cold boot of the standalone (no session image) and its session recorder, against the retail recording of a session scenario."""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

from tony import session_scenarios, standalone_boot

ROOT = Path(__file__).resolve().parents[1]
KITS_DIR = ROOT / "src/hooks/kits"
EXE = ROOT / "build/disc/files/SETUP/data/THawk2.exe"
SPIKE = ROOT / "build/hooks/kits/opentony_spike.exe"
WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"


def _generated(script: str, output: Path, *, stdout: bool) -> str:
    result = subprocess.run([sys.executable, str(ROOT / "scripts" / script)], cwd=ROOT, capture_output=True, text=True, check=True)
    return result.stdout if stdout else output.read_text()


@pytest.mark.skipif(not EXE.is_file(), reason="needs the retail executable (build/disc)")
def test_the_static_initializer_transcription_is_what_the_script_writes():
    path = KITS_DIR / "standalone_static_init_generated.cpp"
    assert _generated("static_init_transcribe.py", path, stdout=True) == path.read_text()


@pytest.mark.skipif(not EXE.is_file(), reason="needs the retail executable (build/disc)")
def test_the_thunk_table_is_what_the_script_writes():
    path = KITS_DIR / "standalone_thunks_generated.hpp"
    before = path.read_text()
    try:
        assert _generated("standalone_thunks.py", path, stdout=False) == before
    finally:
        path.write_text(before)


def test_every_initializer_slot_is_run_one_way():
    """A slot of the initializer table is a transcribed body, a constructor stub written by hand, or a native; never two of them."""

    transcribed = set(re.findall(r"\{0x([0-9a-f]{8}), 0x[0-9a-f]{8}, &init_", (KITS_DIR / "standalone_static_init_generated.cpp").read_text()))
    boot = (KITS_DIR / "standalone_boot.cpp").read_text()
    table = boot[boot.index("const ConstructorStub kConstructorStubs[]"):boot.index("};", boot.index("const ConstructorStub kConstructorStubs[]"))]
    stubs = set(re.findall(r"^\s+\{0x([0-9a-f]{8}),", table, re.MULTILINE))
    assert transcribed and stubs
    slot_functions = set(re.findall(r"\{0x[0-9a-f]{8}, 0x([0-9a-f]{8}), &init_", (KITS_DIR / "standalone_static_init_generated.cpp").read_text()))
    assert not slot_functions & stubs


def test_the_recorder_and_the_probe_share_the_hash():
    probe = (ROOT / "src/hooks/session_trace.cpp").read_text()
    recorder = (KITS_DIR / "standalone_session.cpp").read_text()
    assert '#include "session_hash.h"' in probe and '#include "../session_hash.h"' in recorder
    assert "ot_session_hash_block_normalized" in recorder and "ot_session_hash_block(" in recorder


def test_the_standalone_masks_are_ranges_with_a_class():
    masks = standalone_boot.masks(session_scenarios.load_session("boot-menu")).split(",")
    assert all(re.fullmatch(r"[0-9a-f]+:[0-9a-f]+", mask) for mask in masks)
    assert len(masks) <= 32   # the recorders' table (kMaxMasks, and the standalone's alike)
    assert all(group for group in standalone_boot.STANDALONE_MASKS.values())


def test_the_gated_sessions_are_session_scenarios():
    names = {path.stem for path in session_scenarios.scenario_files()}
    assert standalone_boot.GATED_SESSIONS and set(standalone_boot.GATED_SESSIONS) <= names


def test_standalone_parity_is_a_command():
    from tony.cli import build_parser

    args = build_parser().parse_args(["standalone", "parity", "boot-options", "--keep-retail"])
    assert args.names == ["boot-options"] and args.keep_retail and args.func is standalone_boot.parity_command


@pytest.mark.skipif(not WINE or not SPIKE.is_file(), reason="OPENTONY_WINE_TESTS=1 and a built spike")
@pytest.mark.parametrize("name", [n for n in standalone_boot.GATED_SESSIONS if n != "boot-menu"])
def test_the_cold_boot_matches_the_other_gated_recordings(name):
    """9a: the same comparison as boot-menu's, for each further session whose cold boot matches (the keys reach the main menu, Options opens)."""

    result = standalone_boot.parity(name)
    assert result["status"] == "match", result
    assert result["records"][0] == result["records"][1] and result["clock_mismatches"] == 0 and result["differing"] == []


@pytest.mark.skipif(not WINE or not SPIKE.is_file(), reason="OPENTONY_WINE_TESTS=1 and a built spike")
def test_the_cold_boot_matches_the_retail_boot_menu_recording():
    """The cold boot, strict, runs the whole boot-menu session; every record has retail's delivery and poll, and every block of .data equals retail's
    outside the identity blocks and the masks (milestone 9's done-when)."""

    name = "boot-menu"
    document = session_scenarios.load_session(name)
    if not (session_scenarios.run_dir(standalone_boot.retail_label(name)) / "session.done").exists():
        assert standalone_boot.run_retail(name, document)
    done, text = standalone_boot.run_standalone(name, document)
    assert done, text[-2000:]
    assert "unresolved" not in text and "FATAL" not in text
    result = standalone_boot.compare(name)
    assert result["records"][0] == result["records"][1] and result["clock_mismatches"] == 0
    assert result["differing"] == []
    assert result["match"]
