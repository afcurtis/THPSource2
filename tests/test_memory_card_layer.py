"""Phase 4.14b: the memory-card layer (src/runtime/mmu_manager.cpp, mmu_setup.cpp: the card manager, set-up, replay helpers and backend; mmu_screens.cpp: the save screen) against the retail
forced-call fixtures (re/evidence/fixtures/mmu-calls-*.txt and mmu-screens-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
CARD_SOURCES = ["src/runtime/mmu_manager.cpp", "src/runtime/mmu_setup.cpp", "src/runtime/heap.cpp"]
SCREEN_SOURCES = [
    "src/runtime/mmu_screens.cpp", "src/runtime/screen_kit.cpp", "src/runtime/create_screens_ui.cpp", "src/runtime/create_screens_stat.cpp", "src/runtime/create_screens_save.cpp",
    "src/runtime/create_screens_editor.cpp", "src/runtime/create_screens_select.cpp", "src/runtime/create_spool.cpp", "src/runtime/skater_window_helpers.cpp", "src/runtime/menu_toolkit.cpp",
    "src/runtime/menu_lists.cpp", "src/runtime/menu_screens.cpp", "src/runtime/menu_dispatch.cpp", "src/runtime/frontend_text.cpp", "src/runtime/frontend_window.cpp",
    "src/runtime/skater_manager.cpp", "src/runtime/create_color.cpp",
]
SLICES = ["memory-card-records", "memory-card-operations", "memory-card-setup", "replay-card-helpers", "card-backend", "save-card-screen"]


def _compile(directory, name, sources):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = directory / name
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(ROOT / "src/runtime"), *[str(ROOT / source) for source in sources], "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert build.returncode == 0, build.stderr
    return binary


@pytest.fixture(scope="module")
def card_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("mmu_replay"), "opentony_mmu_retail_calls_test", ["src/runtime/mmu_retail_calls_test.cpp", *CARD_SOURCES])


@pytest.fixture(scope="module")
def screen_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("mmu_screens_replay"), "opentony_mmu_screens_retail_calls_test", ["src/runtime/mmu_screens_retail_calls_test.cpp", *SCREEN_SOURCES])


def test_card_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("mmu-calls-*.txt")), "no card fixtures in re/evidence/fixtures"
    assert sorted(FIXTURE_DIR.glob("mmu-screens-calls-*.txt")), "no save screen fixtures in re/evidence/fixtures"


def test_native_card_layer_matches_every_retail_call(card_tool):
    fixtures = sorted(FIXTURE_DIR.glob("mmu-calls-*.txt"))
    run = subprocess.run([str(card_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1500, first


def test_native_save_screen_matches_every_retail_call(screen_tool):
    fixtures = sorted(FIXTURE_DIR.glob("mmu-screens-calls-*.txt"))
    run = subprocess.run([str(screen_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 300, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in sorted(FIXTURE_DIR.glob("mmu*-calls-*.txt")):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def _break(card_tool, tmp_path, marker, edit, needs=""):
    """Replays every card fixture with the first case line that carries `marker` (and `needs`) rewritten by `edit`; the replay must fail."""
    for source in sorted(FIXTURE_DIR.glob("mmu-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and marker in line and needs in line:
                lines[index] = edit(line)
                broken = tmp_path / "mmu-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(card_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return True
    return False


def test_replay_has_teeth_on_a_written_byte(card_tool, tmp_path):
    """A changed byte of a retail write (a registered buffer slot) makes the replay fail."""

    def edit(line):
        head, writes = line.split("writes=", 1)
        digits, rest = writes.split(";", 1) if ";" in writes else (writes, "")
        address, data = digits.split("=", 1)
        flipped = ("0" if data[0] != "0" else "1") + data[1:]
        return head + "writes=" + address + "=" + flipped + ((";" + rest) if rest else "")

    if not _break(card_tool, tmp_path, "name=Mmu_AddSaveBuffer ", edit, needs="writes="):
        pytest.skip("no registration case that writes")


def test_replay_has_teeth_on_a_return_value(card_tool, tmp_path):
    """A changed return value (the file count) makes the replay fail."""

    def edit(line):
        head, tail = line.split(" ret=", 1)
        value, rest = tail.split(" ", 1)
        return head + " ret=" + format((int(value, 16) + 1) & 0xFFFFFFFF, "x") + " " + rest

    if not _break(card_tool, tmp_path, "name=Mmu_FileCount ", edit):
        pytest.skip("no file count case")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
