"""Phase 4.14b: the game's heap (src/runtime/heap.cpp) against the retail forced-call fixtures (re/evidence/fixtures/heap-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
SOURCES = ["src/runtime/heap.cpp"]
SLICES = ["heap-allocator"]


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
    return _compile(tmp_path_factory.mktemp("heap_replay"), "opentony_heap_retail_calls_test", ["src/runtime/heap_retail_calls_test.cpp", *SOURCES])


def test_heap_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("heap-calls-*.txt")), "no heap fixtures in re/evidence/fixtures"


def test_native_heap_matches_every_retail_call(replay_tool):
    fixtures = sorted(FIXTURE_DIR.glob("heap-calls-*.txt"))
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 500, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in sorted(FIXTURE_DIR.glob("heap-calls-*.txt")):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def _break(replay_tool, tmp_path, marker, edit, needs=""):
    """Replays every fixture with the first case line that carries `marker` (and `needs`) rewritten by `edit`; the replay must fail."""
    for source in sorted(FIXTURE_DIR.glob("heap-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and marker in line and needs in line:
                lines[index] = edit(line)
                broken = tmp_path / "heap-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return True
    return False


def test_replay_has_teeth_on_a_written_byte(replay_tool, tmp_path):
    """A changed byte of a retail write (the header of an allocated block) makes the replay fail."""

    def edit(line):
        head, writes = line.split("writes=", 1)
        digits, rest = writes.split(";", 1) if ";" in writes else (writes, "")
        address, data = digits.split("=", 1)
        flipped = ("0" if data[0] != "0" else "1") + data[1:]
        return head + "writes=" + address + "=" + flipped + ((";" + rest) if rest else "")

    if not _break(replay_tool, tmp_path, "name=Mem_NewX ", edit, needs="writes="):
        pytest.skip("no allocation case that writes")


def test_replay_has_teeth_on_a_return_value(replay_tool, tmp_path):
    """A changed return value (the block size) makes the replay fail."""

    def edit(line):
        head, tail = line.split(" ret=", 1)
        value, rest = tail.split(" ", 1)
        return head + " ret=" + format((int(value, 16) + 1) & 0xFFFFFFFF, "x") + " " + rest

    if not _break(replay_tool, tmp_path, "name=Mem_GetBlockSizeX ", edit):
        pytest.skip("no block size case")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
