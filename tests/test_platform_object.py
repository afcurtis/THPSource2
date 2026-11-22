"""Phase 4.4c: the native platform (subtype 0x0192) object (src/trg/platform_object.cpp)
against the shipped level files and retail traces."""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / "build/assets/all-pkr/files/data"
FIXTURES = ROOT / "re/evidence/fixtures"
NEEDS_CORPUS = pytest.mark.skipif(
    not CORPUS.is_dir() or not any(CORPUS.glob("*.TRG")),
    reason="shipped TRG files not extracted",
)
SOURCES = ["src/trg/object_script.cpp", "src/trg/platform_object.cpp", "src/trg/traffic_object.cpp", "src/trg/powerup_object.cpp",
           "src/trg/world_objects.cpp", "src/trg/trg_runtime.cpp"]


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
    return _compile(tmp_path_factory.mktemp("platform_scripts"), "opentony_trg_scripts",
                    ["src/trg/trg_script_inspect.cpp", *SOURCES])


@pytest.fixture(scope="module")
def replay_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("platform_replay"), "opentony_platform_object_retail_trace_test",
                    ["src/trg/platform_object_retail_trace_test.cpp", *SOURCES])


@NEEDS_CORPUS
def test_every_shipped_platform_script_runs_clean(scripts_tool):
    """All 351 platform objects of the 32 shipped levels run 600 frames, bounces forced,
    against a varying pseudo-world with no assertion and no fault."""

    objects = 0
    for path in sorted(CORPUS.glob("SK*_T.TRG")):
        run = subprocess.run([str(scripts_tool), str(path)], capture_output=True, text=True, check=False)
        assert run.returncode == 0, (path.name, run.stdout, run.stderr)
        fields = dict(part.split("=") for part in run.stdout.split())
        assert fields["platform_faults"] == "0" and fields["platform_assertions"] == "0", (path.name, run.stdout)
        objects += int(fields["platform_objects"])
    assert objects == 351


@NEEDS_CORPUS
@pytest.mark.parametrize("fixture,level,minimum_updates,minimum_bounces", [
    ("platform-object-warehouse.txt", "SKWARE_T.TRG", 5300, 25),
    ("platform-object-hangar.txt", "SKHAN_T.TRG", 1600, 10),
])
def test_native_platforms_match_retail_traces(replay_tool, fixture, level, minimum_updates, minimum_bounces):
    """Retail platform updates and bounces replay from their entry images: same commands,
    Rnd draws, collision queries, side-effect calls and object fields."""

    if not (FIXTURES / fixture).is_file():
        pytest.skip("fixture not present")
    run = subprocess.run(
        [str(replay_tool), str(FIXTURES / fixture), str(CORPUS / level)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    fields = dict(part.split("=") for part in run.stdout.split())
    assert fields["mismatched"] == "0"
    assert fields["matched"] == fields["updates"]
    assert int(fields["updates"]) >= minimum_updates
    assert int(fields["bounces"]) >= minimum_bounces
    assert int(fields["collision_queries"]) > 0
