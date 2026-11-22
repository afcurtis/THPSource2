"""Phase 4.14c: the core utilities and start-up code (utils.cpp, init.cpp, main.cpp, the module loader, the PKR2 wrappers) against the retail forced-call fixtures (re/evidence/fixtures/core-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
SOURCES = ["src/runtime/core_utils.cpp", "src/runtime/core_pkr.cpp", "src/runtime/core_init.cpp", "src/runtime/core_main.cpp", "src/runtime/core_display.cpp",
           "src/runtime/math_gte.cpp", "src/runtime/math_fixed.cpp", "src/runtime/math_psx.cpp"]
SLICES = ["core-vector-operators", "core-distance-and-angle-math", "core-startup-sequences", "pkr2-package-wrappers", "relocatable-modules-and-explosions", "main-small-helpers", "main-display",
          "main-level-setup", "main-level-loop", "main-entry-and-launch"]


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
    return _compile(tmp_path_factory.mktemp("core_replay"), "opentony_core_retail_calls_test", ["src/runtime/core_retail_calls_test.cpp", *SOURCES])


def test_core_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("core-calls-*.txt")), "no core fixtures in re/evidence/fixtures"


def test_native_core_layer_matches_every_retail_call(replay_tool):
    fixtures = sorted(FIXTURE_DIR.glob("core-calls-*.txt"))
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 2000, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in sorted(FIXTURE_DIR.glob("core-calls-*.txt")):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def _break(replay_tool, tmp_path, marker, edit, needs=""):
    """Replays every fixture with the first case line that carries `marker` (and `needs`) rewritten by `edit`; the replay must fail."""
    for source in sorted(FIXTURE_DIR.glob("core-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and marker in line and needs in line:
                lines[index] = edit(line)
                broken = tmp_path / "core-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return True
    return False


def test_replay_has_teeth_on_a_return_value(replay_tool, tmp_path):
    """A changed return value (the distance) makes the replay fail."""

    def edit(line):
        head, tail = line.split(" ret=", 1)
        value, rest = tail.split(" ", 1)
        return head + " ret=" + format((int(value, 16) + 1) & 0xFFFFFFFF, "x") + " " + rest

    assert _break(replay_tool, tmp_path, "name=Utils_Dist ", edit)


def test_replay_has_teeth_on_a_written_byte(replay_tool, tmp_path):
    """A changed byte of a retail write (a rotated vector) makes the replay fail."""

    def edit(line):
        head, writes = line.split("writes=", 1)
        digits, rest = writes.split(";", 1) if ";" in writes else (writes.split(" ", 1)[0], " " + writes.split(" ", 1)[1])
        address, data = digits.split("=", 1)
        flipped = ("0" if data[0] != "0" else "1") + data[1:]
        return head + "writes=" + address + "=" + flipped + ((";" + rest) if not rest.startswith(" ") else rest)

    assert _break(replay_tool, tmp_path, "name=Utils_RotateY ", edit, needs="writes=0")


def test_replay_has_teeth_on_a_service_call(replay_tool, tmp_path):
    """A changed argument of a reported service call (the square root of a distance) makes the replay fail."""

    def edit(line):
        head, tail = line.split(" calls=", 1)
        calls, rest = tail.split(" values=", 1)
        first, _, others = calls.partition(";")
        name, _, args = first.partition(",")
        number, _, more = args.partition(",")
        return head + " calls=" + name + "," + format(int(number, 16) + 1, "x") + ("," + more if more else "") + (";" + others if others else "") + " values=" + rest

    assert _break(replay_tool, tmp_path, "name=Utils_Dist ", edit, needs="calls=engine_004f53b0")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
