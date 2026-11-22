"""Phase 4.10f3: the native career select, handicap, multi-player select and level select screens (src/runtime/career_select.cpp, handicap.cpp, multi_select.cpp,
level_select*.cpp) against the retail forced-call fixtures, and their own unit tests."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = sorted([*(ROOT / "re/evidence/fixtures").glob("carsel-calls-*.txt"), *(ROOT / "re/evidence/fixtures").glob("handi-calls-*.txt"),
                   *(ROOT / "re/evidence/fixtures").glob("multi-calls-*.txt"), *(ROOT / "re/evidence/fixtures").glob("levsel-calls-*.txt")])
SOURCES = ["src/runtime/screen_kit.cpp", "src/runtime/career_select.cpp", "src/runtime/handicap.cpp", "src/runtime/multi_select.cpp",
           "src/runtime/level_select.cpp", "src/runtime/level_select_classes.cpp",
           "src/runtime/menu_toolkit.cpp", "src/runtime/menu_lists.cpp", "src/runtime/menu_screens.cpp", "src/runtime/menu_dispatch.cpp",
           "src/runtime/frontend_text.cpp", "src/runtime/frontend_window.cpp", "src/runtime/skater_manager.cpp"]
SLICES = ["career-select-screen", "handicap-screen", "multi-select-screen", "level-select-screen"]


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
    return _compile(tmp_path_factory.mktemp("selscr_replay"), "opentony_select_screens_retail_calls_test",
                    ["src/runtime/select_screens_retail_calls_test.cpp", *SOURCES])


@pytest.fixture(scope="module")
def unit_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("selscr_unit"), "opentony_select_screens_test", ["src/runtime/select_screens_test.cpp", *SOURCES])


def test_fixtures_are_present():
    assert FIXTURES, "no carsel-, handi- or multi-calls fixtures in re/evidence/fixtures"


def test_select_screens_unit_behaviour(unit_tool):
    run = subprocess.run([str(unit_tool)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr


def test_native_select_screens_match_every_retail_call(replay_tool):
    run = subprocess.run([str(replay_tool), *map(str, FIXTURES)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1000, first


def _cases(function):
    for fixture in FIXTURES:
        for line in fixture.read_text().splitlines():
            if line.startswith("C ") and f" name={function} " in line:
                yield fixture, line


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
                assert names.get(address) == fields["name"], (fixture.name, fields["name"], hex(address))


def test_replay_fails_when_a_fixture_value_changes(replay_tool, tmp_path):
    """A replay that cannot fail proves nothing: flip the return value of one recorded CareerSelect_Poll and the replay must report a mismatch."""
    for fixture, line in _cases("CareerSelect_Poll"):
        fields = _fields(line)
        if fields.get("ret") in (None, "None"):
            continue
        changed = line.replace(f" ret={fields['ret']} ", f" ret={(int(fields['ret'], 16) + 1) & 0xFFFFFFFF:x} ", 1)
        assert changed != line
        text = fixture.read_text().replace(line, changed, 1)
        mutated = tmp_path / fixture.name
        mutated.write_text(text)
        run = subprocess.run([str(replay_tool), str(mutated)], capture_output=True, text=True, check=False)
        assert run.returncode == 1, run.stdout + run.stderr
        assert "CareerSelect_Poll" in run.stdout
        return
    pytest.skip("no CareerSelect_Poll case in the fixtures")


def test_slice_manifests_name_every_function_the_fixtures_replay():
    scope = set()
    for slice_id in SLICES:
        scope |= set(yaml.safe_load((ROOT / f"re/slices/{slice_id}.yml").read_text())["scope"]["functions"])
    for fixture in FIXTURES:
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                assert int(_fields(line)["fn"], 16) in scope
