"""Phase 4.10h: the native goals, game-mode and competition screens (src/runtime/goals*.cpp) against the retail forced-call fixtures, and their own unit tests."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = sorted((ROOT / "re/evidence/fixtures").glob("goal-calls-*.txt"))
SOURCES = ["src/runtime/screen_kit.cpp", "src/runtime/goals_competition.cpp", "src/runtime/goals_displays.cpp", "src/runtime/goals_windows.cpp",
           "src/runtime/goals_lights.cpp", "src/runtime/goals_loading.cpp", "src/runtime/goals_objects.cpp", "src/runtime/goals_tag.cpp",
           "src/runtime/goals_dispatch.cpp", "src/runtime/competition.cpp", "src/runtime/career.cpp", "src/runtime/career_tables.cpp",
           "src/runtime/menu_toolkit.cpp", "src/runtime/menu_lists.cpp", "src/runtime/menu_screens.cpp", "src/runtime/menu_dispatch.cpp",
           "src/runtime/frontend_text.cpp", "src/runtime/frontend_window.cpp", "src/runtime/skater_manager.cpp"]
SLICES = ["competition-runtime", "career-medal-objects", "goal-level-windows", "loading-goals-screen", "tag-game-helpers"]


def _compile(tmp_path, name, sources):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = tmp_path / name
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(ROOT / "src/runtime"),
         *[str(ROOT / source) for source in sources], "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert build.returncode == 0, build.stderr
    return binary


@pytest.fixture(scope="module")
def replay_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("goals_replay"), "opentony_goals_retail_calls_test", ["src/runtime/goals_retail_calls_test.cpp", *SOURCES])


@pytest.fixture(scope="module")
def unit_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("goals_unit"), "opentony_goals_test", ["src/runtime/goals_test.cpp", *SOURCES])


def test_fixtures_are_present():
    assert FIXTURES, "no goal-calls fixtures in re/evidence/fixtures"


def test_goals_unit_behaviour(unit_tool):
    run = subprocess.run([str(unit_tool)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr


def test_native_goals_screens_match_every_retail_call(replay_tool):
    run = subprocess.run([str(replay_tool), *map(str, FIXTURES)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1000, first


def _fields(line):
    return dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)


def test_fixture_functions_carry_curated_names():
    names = {entry["address"]: entry["name"]
             for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in FIXTURES:
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = _fields(line)
                address = int(fields["fn"], 16)
                assert names.get(address) == fields["name"], (fixture.name, fields["name"])


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
