"""Phase 4.4b: the native level-object script interpreter (src/trg/object_script.*)
against the shipped level files and a retail Warehouse trace."""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / "build/assets/all-pkr/files/data"
FIXTURE = ROOT / "re/evidence/fixtures/object-script-warehouse.txt"
NEEDS_CORPUS = pytest.mark.skipif(
    not CORPUS.is_dir() or not any(CORPUS.glob("*.TRG")),
    reason="shipped TRG files not extracted",
)


def _compile(tmp_path, name, sources):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = tmp_path / name
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(ROOT), "-I", str(ROOT / "src/trg"),
         *[str(ROOT / source) for source in sources], "-o", str(binary)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert build.returncode == 0, build.stderr
    return binary


@pytest.fixture(scope="module")
def scripts_tool(tmp_path_factory):
    return _compile(
        tmp_path_factory.mktemp("scripts_tool"),
        "opentony_trg_scripts",
        ["src/trg/trg_script_inspect.cpp", "src/trg/object_script.cpp", "src/trg/platform_object.cpp",
         "src/trg/traffic_object.cpp", "src/trg/powerup_object.cpp", "src/trg/world_objects.cpp", "src/trg/trg_runtime.cpp"],
    )


@pytest.fixture(scope="module")
def replay_tool(tmp_path_factory):
    return _compile(
        tmp_path_factory.mktemp("replay_tool"),
        "opentony_object_script_retail_trace_test",
        ["src/trg/object_script_retail_trace_test.cpp", "src/trg/object_script.cpp",
         "src/trg/platform_object.cpp", "src/trg/world_objects.cpp", "src/trg/trg_runtime.cpp"],
    )


@NEEDS_CORPUS
def test_every_shipped_level_object_script_runs_clean(scripts_tool):
    """All 32 shipped TRG files: every 0xcb object script and script point runs
    600 frames against a varying pseudo-world with no assertion and no fault."""

    files = sorted(CORPUS.glob("*.TRG"))
    assert len(files) == 32
    total_commands = 0
    for path in files:
        run = subprocess.run([str(scripts_tool), str(path)], capture_output=True, text=True, check=False)
        assert run.returncode == 0, (path.name, run.stdout, run.stderr)
        fields = dict(part.split("=") for part in run.stdout.split())
        assert fields["faults"] == "0" and fields["assertions"] == "0", (path.name, run.stdout)
        total_commands += int(fields["commands"])
    assert total_commands > 70000


@NEEDS_CORPUS
def test_native_scripts_match_retail_warehouse_trace(replay_tool):
    """981 retail updates replay from their entry images: same commands and
    cursors, same variable reads, same side-effect calls, same object fields."""

    run = subprocess.run(
        [str(replay_tool), str(FIXTURE), str(CORPUS / "SKWARE_T.TRG")],
        capture_output=True,
        text=True,
        check=False,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    fields = dict(part.split("=") for part in run.stdout.split())
    assert fields["updates"] == "981"
    assert fields["matched"] == "981"
    assert fields["mismatched"] == "0"
    assert int(fields["commands"]) >= 1600
    assert int(fields["distinct_opcodes"]) >= 15
