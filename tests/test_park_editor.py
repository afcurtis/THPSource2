"""Phases 4.11d and 4.11e: the park editor (src/runtime/level_editor_*.cpp: its state and piece operations, then the editor object, its pad, its menus and its frame)
against the retail forced-call fixtures (re/evidence/fixtures/leveleditor-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
SOURCES = ["src/runtime/level_editor_global.cpp", "src/runtime/level_editor_load.cpp", "src/runtime/level_editor_ui.cpp", "src/runtime/level_editor_camera.cpp",
           "src/runtime/level_editor_cursor.cpp", "src/runtime/level_editor_object.cpp", "src/runtime/level_editor_menus.cpp", "src/runtime/level_editor_states.cpp"]
SLICES = ["park-editor-global", "park-menu", "park-editor-loop", "park-editor-hud", "park-editor-camera", "park-editor-cursor", "park-editor-object", "park-editor-frame",
          "park-editor-pad", "park-editor-menus"]


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
def replay_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("level_editor_replay"), "opentony_level_editor_retail_calls_test",
                    ["src/runtime/level_editor_retail_calls_test.cpp", *SOURCES])


def test_park_editor_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("leveleditor-calls-*.txt")), "no park editor fixtures in re/evidence/fixtures"


def test_native_park_editor_matches_every_retail_call(replay_tool):
    fixtures = sorted(FIXTURE_DIR.glob("leveleditor-calls-*.txt"))
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1500, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in sorted(FIXTURE_DIR.glob("leveleditor-calls-*.txt")):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def test_replay_has_teeth(replay_tool, tmp_path):
    """A changed byte of a retail write makes the replay fail."""
    for source in sorted(FIXTURE_DIR.glob("leveleditor-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and "name=EditorCursor_Update" in line and "writes=" in line:
                head, writes = line.split("writes=", 1)
                digits, rest = writes.split(";", 1) if ";" in writes else (writes, "")
                address, data = digits.split("=", 1)
                flipped = ("0" if data[0] != "0" else "1") + data[1:]
                lines[index] = head + "writes=" + address + "=" + flipped + ((";" + rest) if rest else "")
                broken = tmp_path / "leveleditor-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return
    pytest.skip("no cursor update case with writes")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid


def test_replay_has_teeth_on_a_service_call(replay_tool, tmp_path):
    """Phase 4.11e: a changed argument of a reported service (the sound the pad plays) makes the replay fail."""
    for source in sorted(FIXTURE_DIR.glob("leveleditor-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and "name=LevelEditor__HandleInput" in line and "engine_004ad620,90," in line:
                lines[index] = line.replace("engine_004ad620,90,", "engine_004ad620,91,", 1)
                broken = tmp_path / "leveleditor-calls-broken-service.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return
    pytest.skip("no pad case that played the category sound")
