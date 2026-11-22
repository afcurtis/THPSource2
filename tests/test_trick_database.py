"""Phase 4.7b: the native trick database (src/runtime/trick_database.cpp) against hand-built
cases and the retail Warehouse trace (manager images, character records, key tables)."""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
TRICKS = ROOT / "build/assets/all-pkr/files/data/TRICKS.BIN"
FIXTURE = ROOT / "re/evidence/fixtures/trickdb-warehouse.txt"
NEEDS_CORPUS = pytest.mark.skipif(not TRICKS.is_file(), reason="TRICKS.BIN not extracted")
SOURCES = ["src/runtime/trick_database.cpp", "src/runtime/trick_combo_tables.cpp"]


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
    return _compile(tmp_path_factory.mktemp("trickdb_unit"), "opentony_trick_database_test",
                    ["src/runtime/trick_database_test.cpp", *SOURCES])


@pytest.fixture(scope="module")
def replay_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("trickdb_replay"), "opentony_trick_database_retail_trace_test",
                    ["src/runtime/trick_database_retail_trace_test.cpp", *SOURCES])


def test_hand_built_database_semantics(unit_tool):
    run = subprocess.run([str(unit_tool)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr


def _summary(run):
    fields = {}
    for part in run.stdout.splitlines()[0].split():
        key, value = part.split("=")
        fields[key] = int(value)
    return fields


@NEEDS_CORPUS
def test_native_database_matches_the_retail_trace(replay_tool):
    run = subprocess.run([str(replay_tool), str(FIXTURE), str(TRICKS)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    fields = _summary(run)
    assert fields["failures"] == 0
    assert fields["records"] == 105
    assert fields["characters"] == 20
    assert fields["tables"] >= 7  # six characters and the alternate stream
    assert fields["alternates"] >= 1


@NEEDS_CORPUS
@pytest.mark.parametrize("kind", ["R", "K", "W", "A"])
def test_a_corrupted_trace_fails_the_replay(replay_tool, tmp_path, kind):
    """Changing one recorded value (a record field, a character record, a table word or the
    alternate block) must make the replay fail: the check has teeth."""
    lines = FIXTURE.read_text().splitlines()
    for index, line in enumerate(lines):
        parts = line.split(" ")
        if parts[0] != kind:
            continue
        if kind == "R":
            parts[2] = str(int(parts[2]) + 1)
        elif kind == "K":
            record = bytearray.fromhex(parts[4])
            record[0xAC] ^= 0x01
            parts[4] = record.hex()
        elif kind == "W":
            parts[2] = str(int(parts[2]) + 1)
        else:
            block = bytearray.fromhex(parts[2])
            block[0] ^= 0x01
            parts[2] = block.hex()
        lines[index] = " ".join(parts)
        break
    else:
        pytest.skip(f"trace has no {kind} record")
    corrupted = tmp_path / "corrupted.txt"
    corrupted.write_text("\n".join(lines) + "\n")
    run = subprocess.run([str(replay_tool), str(corrupted), str(TRICKS)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr
