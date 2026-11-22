"""Phase 4.15e: the renderer (the display buffers, the animation pose side, the image classes, the palette slots and cache, the scene renderer's small functions, the frame, view and object functions, and
the D3D start-up, fog, sprite quads and lit packets) against the retail forced-call fixtures (re/evidence/fixtures/render-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
SRC = ROOT / "src/runtime"


def _sources(pattern):
    return sorted(f"src/runtime/{path.name}" for path in SRC.glob(pattern) if not path.name.endswith("_test.cpp"))


RENDER = _sources("render_*.cpp")
DEPENDENCIES = [*_sources("core_*.cpp"), *_sources("math_*.cpp"), *_sources("m3d_*.cpp"), "src/runtime/collision_cache.cpp"]
TEST = "src/runtime/render_retail_calls_test.cpp"
SLICES = ["render-display-buffers", "render-animation-pose", "render-images", "render-palette", "render-m3d-submit", "render-scene-frame", "render-view-objects", "render-d3d", "d3d-fog-tables",
          "d3d-sprite-quads"]
MIN_CASES = 3000


@pytest.fixture(scope="module")
def tool(tmp_path_factory):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = tmp_path_factory.mktemp("render_replay") / "opentony_render_retail_calls_test"
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(SRC), *[str(ROOT / source) for source in [TEST, *RENDER, *DEPENDENCIES]], "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert build.returncode == 0, build.stderr
    return binary


def _fixtures():
    return sorted(FIXTURE_DIR.glob("render-calls-*.txt"))


def test_fixtures_are_present():
    assert _fixtures(), "no renderer fixtures in re/evidence/fixtures"


def test_native_layer_matches_every_retail_call(tool):
    run = subprocess.run([str(tool), *map(str, _fixtures())], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= MIN_CASES, first
    assert " 0 mismatches" in first, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in _fixtures():
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def test_the_24_bit_fixtures_are_named_for_their_precision():
    names = [path.name for path in _fixtures()]
    assert any("-p24-" in name for name in names), names


def _break(tool, tmp_path, marker, edit, needs=""):
    """Replays every fixture with the first case line that carries `marker` (and `needs`) rewritten by `edit`; the replay must fail."""
    for source in _fixtures():
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and marker in line and needs in line:
                lines[index] = edit(line)
                broken = tmp_path / source.name
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return True
    return False


def test_replay_has_teeth_on_a_return_value(tool, tmp_path):
    def edit(line):
        head, tail = line.split(" ret=", 1)
        value, rest = tail.split(" ", 1)
        return head + " ret=" + format((int(value, 16) + 1) & 0xFFFFFFFF, "x") + " " + rest

    assert _break(tool, tmp_path, "name=GetFree16Slot ", edit)


def test_replay_has_teeth_on_a_written_byte(tool, tmp_path):
    def edit(line):
        head, writes = line.split("writes=", 1)
        digits, rest = writes.split(";", 1) if ";" in writes else (writes.split(" ", 1)[0], " " + writes.split(" ", 1)[1])
        address, data = digits.split("=", 1)
        flipped = ("0" if data[0] != "0" else "1") + data[1:]
        return head + "writes=" + address + "=" + flipped + ((";" + rest) if not rest.startswith(" ") else rest)

    assert _break(tool, tmp_path, "name=Db_UpdateSky ", edit, needs="writes=0")


def test_replay_has_teeth_on_a_service_call(tool, tmp_path):
    def edit(line):
        head, tail = line.split(" calls=", 1)
        calls, rest = tail.split(" values=", 1)
        first, _, others = calls.partition(";")
        call_name, _, args = first.partition(",")
        number, _, more = args.rpartition(",") if "," in args else ("", "", args)
        last = more if more else number
        bumped = format(int(last, 16) + 1, "x")
        new_args = (number + "," + bumped) if number else bumped
        return head + " calls=" + call_name + "," + new_args + (";" + others if others else "") + " values=" + rest

    assert _break(tool, tmp_path, "name=M3D_SetGeometryState ", edit, needs="calls=engine_004d14d0,")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
