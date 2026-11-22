"""Phase 4.9: the native create-a-skater code (src/runtime/create_*.cpp) against the retail forced-call
fixtures, its own unit tests and the generated static tables."""

import shutil
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

from tony import hooks

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = sorted((ROOT / "re/evidence/fixtures").glob("create-calls-*.txt"))
CRETEX = ROOT / "build/assets/all-pkr/files/data/CRETEX.BIN"
SOURCES = [
    "src/runtime/create_color.cpp",
    "src/runtime/create_palette.cpp",
    "src/runtime/create_parts.cpp",
    "src/runtime/create_records.cpp",
    "src/runtime/create_spool.cpp",
    "src/runtime/frontend_text.cpp",
    "src/runtime/create_stitch.cpp",
    "src/runtime/create_tables.cpp",
    "src/runtime/create_textures.cpp",
    "src/runtime/career_tables.cpp",
    "src/runtime/skater_manager.cpp",
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
    return _compile(tmp_path_factory.mktemp("create_replay"), "opentony_create_retail_calls_test",
                    ["src/runtime/create_retail_calls_test.cpp", *SOURCES])


@pytest.fixture(scope="module")
def unit_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("create_unit"), "opentony_create_test", ["src/runtime/create_test.cpp", *SOURCES])


def test_fixtures_are_present():
    assert FIXTURES, "no re/evidence/fixtures/create-calls-*.txt fixtures"


def test_create_unit_behaviour(unit_tool):
    args = [str(unit_tool)] + ([str(CRETEX)] if CRETEX.is_file() else [])
    run = subprocess.run(args, capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr


def test_native_create_matches_every_retail_forced_call(replay_tool):
    run = subprocess.run([str(replay_tool), *map(str, FIXTURES)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1500, first


def test_fixture_functions_carry_curated_names():
    names = {entry["address"]: entry["name"]
             for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in FIXTURES:
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                address = int(fields["fn"], 16)
                assert names.get(address) == fields["name"], (fixture.name, fields["name"], hex(address))


def _mutate(source, tmp_path, function, edit):
    lines = source.read_text().splitlines()
    mutated, changed = [], False
    for line in lines:
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


@pytest.mark.parametrize("fixture,function", [
    ("create-calls-color.txt", "Color_HsvToRgb"),
    ("create-calls-textures.txt", "TextureDatabase_Allocate"),
    ("create-calls-cretex.txt", "TextureSetManager_LoadCretex"),
    ("create-calls-records.txt", "CustomSkater_Randomize"),
])
def test_replay_has_teeth(replay_tool, tmp_path, fixture, function):
    """Changing one byte a retail function wrote must make the replay fail."""
    source = ROOT / "re/evidence/fixtures" / fixture
    path = _mutate(source, tmp_path, function, _flip_first_write)
    run = subprocess.run([str(replay_tool), str(path)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr


@pytest.mark.skipif(not hooks._canonical_exe().is_file(), reason="canonical executable not extracted")
def test_create_tables_match_canonical_executable():
    result = subprocess.run([sys.executable, "scripts/generate_create_tables.py", "--check"],
                            cwd=ROOT, capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stderr
