"""Phase 4.8: the native career, skater-manager and competition code (src/runtime/career*.cpp,
skater_manager.cpp, competition.cpp) against the retail forced-call fixtures and the generated
static tables."""

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from tony import hooks

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = sorted((ROOT / "re/evidence/fixtures").glob("career-calls-*.txt"))
SOURCES = [
    "src/runtime/career.cpp",
    "src/runtime/career_save.cpp",
    "src/runtime/career_tables.cpp",
    "src/runtime/competition.cpp",
    "src/runtime/skater_manager.cpp",
    "src/runtime/trick_database.cpp",
    "src/runtime/trick_combo_tables.cpp",
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
    return _compile(tmp_path_factory.mktemp("career_replay"), "opentony_career_retail_calls_test",
                    ["src/runtime/career_retail_calls_test.cpp", *SOURCES])


@pytest.fixture(scope="module")
def unit_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("career_unit"), "opentony_career_test",
                    ["src/runtime/career_test.cpp", *SOURCES])


def test_fixtures_are_present():
    assert FIXTURES, "no re/evidence/fixtures/career-calls-*.txt fixtures"


def test_career_unit_behaviour(unit_tool):
    run = subprocess.run([str(unit_tool)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr


def test_native_career_matches_every_retail_forced_call(replay_tool):
    run = subprocess.run([str(replay_tool), *map(str, FIXTURES)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1000, first


def test_replay_has_teeth(replay_tool, tmp_path):
    """Changing one retail return value must make the replay fail."""
    source = next(f for f in FIXTURES if "progress" in f.name)
    lines = source.read_text().splitlines()
    mutated = []
    changed = False
    for line in lines:
        if not changed and line.startswith("C ") and "name=Career_Got " in line and " ret=1 " in line:
            line = line.replace(" ret=1 ", " ret=0 ", 1)
            changed = True
        mutated.append(line)
    assert changed, "no Career_Got case returning 1 in the progress fixture"
    path = tmp_path / "mutated.txt"
    path.write_text("\n".join(mutated) + "\n")
    run = subprocess.run([str(replay_tool), str(path)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr


@pytest.mark.skipif(not hooks._canonical_exe().is_file(), reason="canonical executable not extracted")
def test_career_tables_match_canonical_executable():
    result = subprocess.run([sys.executable, "scripts/generate_career_tables.py", "--check"],
                            cwd=ROOT, capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stderr
