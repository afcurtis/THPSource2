"""Phase 4.10a: the native frontend text layer (src/runtime/frontend_*.cpp: fonts, messages, text effects and the window
base) against the retail forced-call fixtures and the natural main-menu trace, and its own unit tests."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = sorted((ROOT / "re/evidence/fixtures").glob("frontend-calls-*.txt"))
SOURCES = [
    "src/runtime/frontend_text.cpp",
    "src/runtime/frontend_mess.cpp",
    "src/runtime/frontend_window.cpp",
    "src/runtime/frontend_effects.cpp",
]


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
    return _compile(tmp_path_factory.mktemp("frontend_replay"), "opentony_frontend_retail_calls_test",
                    ["src/runtime/frontend_retail_calls_test.cpp", *SOURCES])


@pytest.fixture(scope="module")
def unit_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("frontend_unit"), "opentony_frontend_test", ["src/runtime/frontend_test.cpp", *SOURCES])


def test_fixtures_are_present():
    assert FIXTURES, "no re/evidence/fixtures/frontend-calls-*.txt fixtures"


def test_frontend_unit_behaviour(unit_tool):
    run = subprocess.run([str(unit_tool)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr


def test_native_frontend_matches_every_retail_call(replay_tool):
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


def test_fixture_functions_carry_curated_names():
    names = {entry["address"]: entry["name"]
             for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in FIXTURES:
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                address = int(fields["fn"], 16)
                assert names.get(address) == fields["name"], (fixture.name, fields["name"], hex(address))


def test_every_ported_function_has_a_replayed_case():
    """Every function a frontend slice names natively has at least one retail case in the fixtures."""
    replayed = set()
    for fixture in FIXTURES:
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                replayed.add(int(fields["fn"], 16))
    for slice_id in ("frontend-text-layer", "frontend-window-base"):
        manifest = yaml.safe_load((ROOT / f"re/slices/{slice_id}.yml").read_text())
        missing = [hex(a) for a in manifest["scope"]["functions"] if a not in replayed]
        assert not missing, (slice_id, missing)


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


def _first_fixture_with_writes(function):
    for fixture, line in _cases(function):
        if any(part.startswith("writes=") and len(part) > 14 for part in line.split(" ")):
            return fixture
    pytest.skip(f"no case of {function} writes memory")


@pytest.mark.parametrize("function", ["Font_ApplyEscape", "Mess_Update", "Window_AddElement", "FallingTextEffect_Init"])
def test_replay_has_teeth_for_written_bytes(replay_tool, tmp_path, function):
    """Changing one byte a retail function wrote must make the replay fail."""
    path = _mutate(_first_fixture_with_writes(function), tmp_path, function, _flip_first_write)
    run = subprocess.run([str(replay_tool), str(path)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr


@pytest.mark.parametrize("function", ["Font_Draw", "Window_Draw", "Mess_Display"])
def test_replay_has_teeth_for_service_calls(replay_tool, tmp_path, function):
    """Changing the object a virtual call was made on must make the replay fail."""
    source = next(fixture for fixture, line in _cases(function)
                  if any(part.startswith("calls=") and "," in part for part in line.split(" ")))
    path = _mutate(source, tmp_path, function, _change_first_call_argument)
    run = subprocess.run([str(replay_tool), str(path)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr
