"""Phase 4.10c: the native menu toolkit, title screen and credits roll (src/runtime/menu_*.cpp, mainmenu*.cpp, credits.cpp) against the retail
forced-call fixtures, and their own unit tests."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = sorted((ROOT / "re/evidence/fixtures").glob("menu-calls-*.txt"))
SOURCES = ["src/runtime/menu_toolkit.cpp", "src/runtime/menu_lists.cpp", "src/runtime/menu_screens.cpp", "src/runtime/menu_dispatch.cpp",
           "src/runtime/mainmenu.cpp", "src/runtime/mainmenu_screens.cpp", "src/runtime/credits.cpp", "src/runtime/frontend_text.cpp",
           "src/runtime/frontend_window.cpp"]
SLICES = ("menu-toolkit", "frontend-title-screen", "frontend-credits-roll", "frontend-screen-loops")


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
    return _compile(tmp_path_factory.mktemp("menu_replay"), "opentony_menu_retail_calls_test",
                    ["src/runtime/menu_retail_calls_test.cpp", *SOURCES])


@pytest.fixture(scope="module")
def unit_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("menu_unit"), "opentony_menu_test", ["src/runtime/menu_test.cpp", *SOURCES])


def test_fixtures_are_present():
    assert FIXTURES, "no re/evidence/fixtures/menu-calls-*.txt fixtures"


def test_menu_unit_behaviour(unit_tool):
    run = subprocess.run([str(unit_tool)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr


def test_native_menu_code_matches_every_retail_call(replay_tool):
    run = subprocess.run([str(replay_tool), *map(str, FIXTURES)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 3000, first


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


def test_every_replayed_function_is_in_a_slice():
    replayed = set()
    for fixture in FIXTURES:
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                replayed.add(int(_fields(line)["fn"], 16))
    in_slices = set()
    for slice_id in SLICES:
        manifest = yaml.safe_load((ROOT / f"re/slices/{slice_id}.yml").read_text())
        in_slices.update(manifest["scope"]["functions"])
    assert replayed <= in_slices, [hex(a) for a in sorted(replayed - in_slices)]


def _mutate(source, tmp_path, function, edit):
    mutated, changed = [], False
    for line in source.read_text().splitlines():
        if not changed and line.startswith("C ") and f"name={function} " in line:
            edited = edit(line)
            changed = edited != line
            line = edited
        mutated.append(line)
    assert changed, f"no case of {function} could be mutated in {source.name}"
    path = tmp_path / "mutated.txt"
    path.write_text("\n".join(mutated) + "\n")
    return path


def _flip_first_write(line):
    parts = line.split(" ")
    for i, part in enumerate(parts):
        if part.startswith("writes=") and len(part) > 14:
            address, _, data = part[len("writes="):].partition("=")
            flipped = ("0" if data[0] != "0" else "1") + data[1:]
            parts[i] = f"writes={address}={flipped}" + part[len(f"writes={address}={data}"):]
            return " ".join(parts)
    return line


def _change_first_call_argument(line):
    parts = line.split(" ")
    for i, part in enumerate(parts):
        if part.startswith("calls=") and len(part) > len("calls="):
            calls = part[len("calls="):].split(";")
            head, _, text = calls[0].partition("@")
            fields = head.split(",")
            if len(fields) > 1:
                fields[1] = f"{int(fields[1], 16) ^ 1:x}"
                calls[0] = ",".join(fields) + ("@" + text if text else "")
                parts[i] = "calls=" + ";".join(calls)
                return " ".join(parts)
    return line


def _change_return(line):
    parts = line.split(" ")
    for i, part in enumerate(parts):
        if part.startswith("ret=") and part != "ret=-":
            parts[i] = f"ret={int(part[4:], 16) ^ 1:x}"
            return " ".join(parts)
    return line


def _first_fixture_with(function, key):
    for fixture, line in _cases(function):
        if any(part.startswith(key) and len(part) > len(key) + 7 for part in line.split(" ")):
            return fixture
    pytest.skip(f"no case of {function} with {key}")


@pytest.mark.parametrize("function", ["MenuList_Layout", "MenuGrid_Layout", "MainMenuWheel_Tint", "Credits_LoadText"])
def test_replay_has_teeth_for_written_bytes(replay_tool, tmp_path, function):
    """Changing one byte a retail function wrote must make the replay fail."""
    path = _mutate(_first_fixture_with(function, "writes="), tmp_path, function, _flip_first_write)
    run = subprocess.run([str(replay_tool), str(path)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr


@pytest.mark.parametrize("function", ["MenuText_Draw", "MenuRow_Draw", "CreditText_Update"])
def test_replay_has_teeth_for_service_calls(replay_tool, tmp_path, function):
    """Changing the object a service call was made on must make the replay fail."""
    path = _mutate(_first_fixture_with(function, "calls="), tmp_path, function, _change_first_call_argument)
    run = subprocess.run([str(replay_tool), str(path)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr


@pytest.mark.parametrize("function", ["MenuControl_GetId", "MenuList_HighlightedId", "NameEntry_HandleInput"])
def test_replay_has_teeth_for_return_values(replay_tool, tmp_path, function):
    path = _mutate(_first_fixture_with(function, "ret="), tmp_path, function, _change_return)
    run = subprocess.run([str(replay_tool), str(path)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr
