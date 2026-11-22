"""Phase 4.4c: the native power-up object (src/trg/powerup_object.cpp) against the shipped
level files and a retail Hangar trace."""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / "build/assets/all-pkr/files/data"
FIXTURE = ROOT / "re/evidence/fixtures/powerup-object-hangar.txt"
NEEDS_CORPUS = pytest.mark.skipif(
    not CORPUS.is_dir() or not any(CORPUS.glob("*.TRG")),
    reason="shipped TRG files not extracted",
)
SOURCES = ["src/trg/object_script.cpp", "src/trg/platform_object.cpp", "src/trg/traffic_object.cpp",
           "src/trg/powerup_object.cpp", "src/trg/world_objects.cpp", "src/trg/trg_runtime.cpp"]


def _compile(tmp_path, name, sources):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = tmp_path / name
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(ROOT), "-I", str(ROOT / "src/trg"),
         *[str(ROOT / source) for source in sources], "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert build.returncode == 0, build.stderr
    return binary


@pytest.fixture(scope="module")
def scripts_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("powerup_scripts"), "opentony_trg_scripts",
                    ["src/trg/trg_script_inspect.cpp", *SOURCES])


@pytest.fixture(scope="module")
def replay_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("powerup_replay"), "opentony_powerup_object_retail_trace_test",
                    ["src/trg/powerup_object_retail_trace_test.cpp", *SOURCES])


@NEEDS_CORPUS
def test_every_shipped_powerup_runs_cleanly(scripts_tool):
    powerups = 0
    for path in sorted(CORPUS.glob("SK*_T.TRG")):
        run = subprocess.run([str(scripts_tool), str(path)], capture_output=True, text=True, check=False)
        assert run.returncode == 0, (path.name, run.stdout, run.stderr)
        fields = {}
        for line in run.stdout.splitlines():
            if line.startswith("powerups="):
                fields = dict(part.split("=") for part in line.split())
        assert fields["powerup_faults"] == "0" and fields["powerup_assertions"] == "0", (path.name, run.stdout)
        powerups += int(fields["powerups"])
    assert powerups > 100


@NEEDS_CORPUS
def test_native_powerups_match_the_retail_hangar_trace(replay_tool):
    run = subprocess.run([str(replay_tool), str(FIXTURE), str(CORPUS / "SKHAN_T.TRG")],
                         capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    fields = dict(part.split("=") for part in run.stdout.split())
    assert fields["mismatched"] == "0" and fields["construct_mismatches"] == "0"
    assert fields["matched"] == fields["records"]
    assert int(fields["records"]) >= 2000
    assert int(fields["takes"]) >= 50
    assert int(fields["constructed"]) >= 10
