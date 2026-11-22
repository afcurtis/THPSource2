"""Phase 4.13a: the PC platform layer (src/runtime/pad_input.cpp, pc_system.cpp: the pad, the PC input bindings, the options file and the video options, the custom park files,
the PC file backends, the game-logic helpers of pc\\d3d*.cpp) against the retail forced-call fixtures (re/evidence/fixtures/platform-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
SOURCES = ["src/runtime/pad_input.cpp", "src/runtime/pc_system.cpp"]
SLICES = ["pad-input", "pc-input-keys", "pc-input-bindings", "pc-winmain-options", "video-options-window", "pc-custom-park-files", "pc-render-glue", "pc-texture-glue",
          "pc-timers-and-small"]


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
    return _compile(tmp_path_factory.mktemp("platform_replay"), "opentony_platform_retail_calls_test", ["src/runtime/platform_retail_calls_test.cpp", *SOURCES])


def test_platform_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("platform-calls-*.txt")), "no PC platform fixtures in re/evidence/fixtures"


def test_native_platform_layer_matches_every_retail_call(replay_tool):
    fixtures = sorted(FIXTURE_DIR.glob("platform-calls-*.txt"))
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 2000, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in sorted(FIXTURE_DIR.glob("platform-calls-*.txt")):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def test_replay_has_teeth(replay_tool, tmp_path):
    """A changed byte of a retail write makes the replay fail."""
    for source in sorted(FIXTURE_DIR.glob("platform-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and "name=Input_UpdateActionStates" in line and "writes=" in line:
                head, writes = line.split("writes=", 1)
                digits, rest = writes.split(";", 1) if ";" in writes else (writes, "")
                address, data = digits.split("=", 1)
                flipped = ("0" if data[0] != "0" else "1") + data[1:]
                lines[index] = head + "writes=" + address + "=" + flipped + ((";" + rest) if rest else "")
                broken = tmp_path / "platform-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return
    pytest.skip("no input update case with writes")


def test_replay_has_teeth_on_a_service_call(replay_tool, tmp_path):
    """A changed argument of a reported service (the options file entry a save writes) makes the replay fail."""
    for source in sorted(FIXTURE_DIR.glob("platform-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and "name=PCInput_SaveBindings" in line and "engine_004f6cd0,54b08c," in line:
                lines[index] = line.replace("engine_004f6cd0,54b08c,", "engine_004f6cd0,54b08d,", 1)
                broken = tmp_path / "platform-calls-broken-service.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return
    pytest.skip("no bindings save case")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid

