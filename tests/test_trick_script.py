"""Phase 4.7a: the native trick interpreter and recognition (src/runtime/trick_script.cpp,
trick_recognition.cpp) against hand-built cases and retail Warehouse traces."""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / "build/assets/all-pkr/files/data"
TRICKS = CORPUS / "TRICKS.BIN"
FIXTURES = sorted((ROOT / "re/evidence/fixtures").glob("trick-*.txt"))
NEEDS_CORPUS = pytest.mark.skipif(not TRICKS.is_file(), reason="TRICKS.BIN not extracted")
SOURCES = ["src/runtime/trick_script.cpp", "src/runtime/trick_recognition.cpp", "src/runtime/fixed_math.cpp"]


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
def unit_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("trick_unit"), "opentony_trick_script_test",
                    ["src/runtime/trick_script_test.cpp", *SOURCES])


@pytest.fixture(scope="module")
def replay_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("trick_replay"), "opentony_trick_retail_trace_test",
                    ["src/runtime/trick_retail_trace_test.cpp", *SOURCES])


def test_hand_built_trick_semantics(unit_tool):
    run = subprocess.run([str(unit_tool)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr


def _summary(run):
    fields = {}
    for part in run.stdout.splitlines()[0].split():
        key, value = part.split("=")
        fields[key] = int(value)
    return fields


@NEEDS_CORPUS
@pytest.mark.parametrize("fixture", FIXTURES, ids=lambda path: path.name)
def test_native_tricks_match_the_retail_trace(replay_tool, fixture):
    run = subprocess.run([str(replay_tool), str(fixture), str(TRICKS)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    fields = _summary(run)
    assert fields["failures"] == 0
    assert fields["updates"] >= 100
    assert fields["stand_alone"] == fields["executes"]


@NEEDS_CORPUS
@pytest.mark.parametrize("fixture", FIXTURES[:1], ids=lambda path: path.name)
def test_a_corrupted_trace_fails_the_replay(replay_tool, fixture, tmp_path):
    """Changing one recorded service argument must make the replay fail (the check has teeth)."""
    lines = fixture.read_text().splitlines()
    for index, line in enumerate(lines):
        parts = line.split(" ")
        if parts[0] == "K" and parts[1] == "sfx_play":
            parts[3] = str(int(parts[3]) + 1)
            lines[index] = " ".join(parts)
            break
    else:
        pytest.skip("trace has no sound call to corrupt")
    corrupted = tmp_path / "corrupted.txt"
    corrupted.write_text("\n".join(lines) + "\n")
    run = subprocess.run([str(replay_tool), str(corrupted), str(TRICKS)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr
