"""Phase 4.11a: the PSX region slots, scene materials and spool queue of spool.cpp (src/runtime/psx_spool*.cpp) against the retail forced-call fixtures
(re/evidence/fixtures/psx-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
SOURCES = ["src/runtime/psx_spool.cpp", "src/runtime/psx_spool_anim.cpp", "src/runtime/psx_spool_tex.cpp"]
SLICES = ["psx-spool-queue"]


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
    return _compile(tmp_path_factory.mktemp("psx_replay"), "opentony_psx_spool_retail_calls_test", ["src/runtime/psx_spool_retail_calls_test.cpp", *SOURCES])


def test_psx_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("psx-calls-*.txt")), "no PSX spool fixtures in re/evidence/fixtures"


def test_native_psx_spool_matches_every_retail_call(replay_tool):
    fixtures = sorted(FIXTURE_DIR.glob("psx-calls-*.txt"))
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 3000, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in sorted(FIXTURE_DIR.glob("psx-calls-*.txt")):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def test_replay_has_teeth(replay_tool, tmp_path):
    """A changed byte of a retail write makes the replay fail."""
    source = next(FIXTURE_DIR.glob("psx-calls-*.txt"))
    for source in sorted(FIXTURE_DIR.glob("psx-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and "name=PSX_AllocSceneMaterial" in line and "writes=" in line:
                head, writes = line.split("writes=", 1)
                digits, rest = writes.split(";", 1) if ";" in writes else (writes, "")
                address, data = digits.split("=", 1)
                flipped = ("0" if data[0] != "0" else "1") + data[1:]
                lines[index] = head + "writes=" + address + "=" + flipped + ((";" + rest) if rest else "")
                broken = tmp_path / "psx-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return
    pytest.skip("no AllocSceneMaterial case with writes")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
