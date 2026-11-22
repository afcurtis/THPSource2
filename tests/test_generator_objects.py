"""Phase 4.11b: the generator object layer of the park editor (genobjs.cpp, faceremoval.cpp, editorpieces2.cpp: src/runtime/gen_objs*.cpp, gen_face_removal.cpp,
gen_piece_tables.cpp) against the retail forced-call fixtures (re/evidence/fixtures/gen-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
SOURCES = ["src/runtime/gen_objs_pool.cpp", "src/runtime/gen_objs_out.cpp", "src/runtime/gen_objs_gap.cpp", "src/runtime/gen_objs_poly.cpp",
           "src/runtime/gen_face_removal.cpp", "src/runtime/gen_piece_tables.cpp"]
SLICES = ["editor-in-model", "out-model-statics", "out-model", "gap-members", "generator-polygons", "face-removal", "editor-piece-tables"]


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
    return _compile(tmp_path_factory.mktemp("gen_objs_replay"), "opentony_gen_objs_retail_calls_test",
                    ["src/runtime/gen_objs_retail_calls_test.cpp", *SOURCES])


def test_generator_object_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("gen-calls-*.txt")), "no generator object fixtures in re/evidence/fixtures"


def test_native_generator_objects_match_every_retail_call(replay_tool):
    fixtures = sorted(FIXTURE_DIR.glob("gen-calls-*.txt"))
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 5000, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in sorted(FIXTURE_DIR.glob("gen-calls-*.txt")):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def test_replay_has_teeth(replay_tool, tmp_path):
    """A changed byte of a retail write makes the replay fail."""
    for source in sorted(FIXTURE_DIR.glob("gen-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and "name=EditorOutModel_AllocNode" in line and "writes=" in line:
                head, writes = line.split("writes=", 1)
                digits, rest = writes.split(";", 1) if ";" in writes else (writes, "")
                address, data = digits.split("=", 1)
                flipped = ("0" if data[0] != "0" else "1") + data[1:]
                lines[index] = head + "writes=" + address + "=" + flipped + ((";" + rest) if rest else "")
                broken = tmp_path / "gen-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return
    pytest.skip("no AllocNode case with writes")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
