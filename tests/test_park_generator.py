"""Phase 4.11c: the park generator (levelgen.cpp: src/runtime/level_gen_*.cpp, on the 4.11b generator objects) against the retail forced-call fixtures
(re/evidence/fixtures/levelgen-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
SOURCES = ["src/runtime/gen_objs_pool.cpp", "src/runtime/gen_objs_out.cpp", "src/runtime/gen_objs_gap.cpp", "src/runtime/gen_objs_poly.cpp",
           "src/runtime/gen_face_removal.cpp", "src/runtime/gen_piece_tables.cpp", "src/runtime/level_gen_maps.cpp", "src/runtime/level_gen_pieces.cpp",
           "src/runtime/level_gen_edit.cpp", "src/runtime/level_gen_prk.cpp", "src/runtime/level_gen_load.cpp", "src/runtime/level_gen_region.cpp",
           "src/runtime/level_gen_rails.cpp"]
SLICES = ["park-maps", "park-pieces", "park-edit", "park-prk", "park-region", "park-rails"]


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
    return _compile(tmp_path_factory.mktemp("level_gen_replay"), "opentony_level_gen_retail_calls_test",
                    ["src/runtime/level_gen_retail_calls_test.cpp", *SOURCES])


def test_park_generator_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("levelgen-calls-*.txt")), "no park generator fixtures in re/evidence/fixtures"


def test_native_park_generator_matches_every_retail_call(replay_tool):
    fixtures = sorted(FIXTURE_DIR.glob("levelgen-calls-*.txt"))
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1400, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in sorted(FIXTURE_DIR.glob("levelgen-calls-*.txt")):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def test_replay_has_teeth(replay_tool, tmp_path):
    """A changed byte of a retail write makes the replay fail."""
    for source in sorted(FIXTURE_DIR.glob("levelgen-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and "name=LevelGen_AddItem" in line and "writes=" in line:
                head, writes = line.split("writes=", 1)
                digits, rest = writes.split(";", 1) if ";" in writes else (writes, "")
                address, data = digits.split("=", 1)
                flipped = ("0" if data[0] != "0" else "1") + data[1:]
                lines[index] = head + "writes=" + address + "=" + flipped + ((";" + rest) if rest else "")
                broken = tmp_path / "levelgen-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return
    pytest.skip("no AddItem case with writes")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
