"""Phase 4.15c: the world objects (the object base class, the script and baddy classes, the trigger file and its record writers, the trick objects, the skater object's methods, the traffic cars'
collision, and the small platform / power-up methods) against the retail forced-call fixtures (re/evidence/fixtures/world-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"

MATH = ["src/runtime/animation_cursor.cpp", "src/runtime/math_gte.cpp", "src/runtime/math_fixed.cpp", "src/runtime/math_psx.cpp", "src/runtime/collision_cache.cpp"]
CORE = ["src/runtime/core_utils.cpp", "src/runtime/core_main.cpp", "src/runtime/core_init.cpp", "src/runtime/core_display.cpp", "src/runtime/core_pkr.cpp"]
WORLD = ["src/runtime/world_object.cpp", "src/runtime/world_baddy.cpp", "src/runtime/world_trig.cpp", "src/runtime/world_cmd.cpp", "src/runtime/world_trick.cpp", "src/runtime/world_skater.cpp",
         "src/runtime/world_skater_effects.cpp", "src/runtime/world_extra.cpp", "src/runtime/world_script.cpp", "src/runtime/world_traffic.cpp"]
TEST = "src/runtime/world_retail_calls_test.cpp"
SLICES = ["world-object-base", "world-script-baddy-classes", "world-trigger-file", "world-trigger-writers", "world-trick-objects", "world-skater-methods", "world-traffic-collision", "world-small-methods"]
MIN_CASES = 5000


@pytest.fixture(scope="module")
def tool(tmp_path_factory):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = tmp_path_factory.mktemp("world_replay") / "opentony_world_retail_calls_test"
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(ROOT / "src/runtime"), *[str(ROOT / source) for source in [TEST, *WORLD, *CORE, *MATH]], "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert build.returncode == 0, build.stderr
    return binary


def _fixtures():
    return sorted(FIXTURE_DIR.glob("world-calls-*.txt"))


def test_fixtures_are_present():
    assert _fixtures(), "no world fixtures in re/evidence/fixtures"


def test_native_layer_matches_every_retail_call(tool):
    run = subprocess.run([str(tool), *map(str, _fixtures())], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= MIN_CASES // 10, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in _fixtures():
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def _break(tool, tmp_path, marker, edit, needs=""):
    """Replays every fixture with the first case line that carries `marker` (and `needs`) rewritten by `edit`; the replay must fail."""
    for source in _fixtures():
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and marker in line and needs in line:
                lines[index] = edit(line)
                broken = tmp_path / "world-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return True
    return False


def test_replay_has_teeth_on_a_return_value(tool, tmp_path):
    def edit(line):
        head, tail = line.split(" ret=", 1)
        value, rest = tail.split(" ", 1)
        return head + " ret=" + format((int(value, 16) + 1) & 0xFFFF, "x") + " " + rest

    assert _break(tool, tmp_path, "name=Skater_NameHash ", edit)


def test_replay_has_teeth_on_a_written_byte(tool, tmp_path):
    def edit(line):
        head, writes = line.split("writes=", 1)
        digits, rest = writes.split(";", 1) if ";" in writes else (writes.split(" ", 1)[0], " " + writes.split(" ", 1)[1])
        address, data = digits.split("=", 1)
        flipped = ("0" if data[0] != "0" else "1") + data[1:]
        return head + "writes=" + address + "=" + flipped + ((";" + rest) if not rest.startswith(" ") else rest)

    assert _break(tool, tmp_path, "name=Object_Construct ", edit, needs="writes=")


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

    assert _break(tool, tmp_path, "name=Object_ScalarDelete ", edit, needs="calls=engine_0047fe20")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
