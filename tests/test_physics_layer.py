"""Phase 4.15a: rail.cpp, the small skater functions of physics.cpp and the M3D helpers against the retail forced-call fixtures (re/evidence/fixtures/{rail,skater,m3d}-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"

MATH = ["src/runtime/math_gte.cpp", "src/runtime/math_fixed.cpp", "src/runtime/math_psx.cpp"]
KITS = {
    "rail": {
        "test": "src/runtime/rail_retail_calls_test.cpp",
        "sources": ["src/runtime/rail_nodes.cpp", "src/runtime/rail_create.cpp", "src/runtime/collision_cache.cpp", *MATH],
        "slices": ["rail-node-accessors", "rail-runtime-records", "rail-creation", "collision-face-cache"],
        "return": "Rail_NumLinks",
        "write": "SetupLookup",
        "service": ("TRG_ReadRailTriggerWord", "engine_004c8650"),
        "min_cases": 5000,
    },
    "skater": {
        "test": "src/runtime/skater_retail_calls_test.cpp",
        "sources": ["src/runtime/skater_pose.cpp", "src/runtime/skater_trick.cpp", "src/runtime/skater_tricks_load.cpp", "src/runtime/skater_stats.cpp", "src/runtime/skater_misc.cpp",
                    "src/runtime/skater_rail.cpp", "src/runtime/skater_motion.cpp", "src/runtime/skater_damp.cpp", "src/runtime/skater_physics_frame.cpp", "src/runtime/skater_ground_physics.cpp", "src/runtime/skater_air_physics.cpp", "src/runtime/skater_start_grind.cpp", "src/runtime/skater_trick_script.cpp", "src/runtime/skater_action_step.cpp", "src/runtime/skater_heading.cpp", "src/runtime/skater_acceleration.cpp", "src/runtime/skater_trick_input.cpp", "src/runtime/skater_brake.cpp", "src/runtime/skater_dispatcher.cpp", "src/runtime/skater_ground_steps.cpp", "src/runtime/skater_floor_recovery.cpp", "src/runtime/skater_parts.cpp", "src/runtime/skater_transform.cpp", "src/runtime/skater_sounds.cpp", "src/runtime/skater_ollie.cpp", "src/runtime/skater_swept.cpp", "src/runtime/skater_rail_physics.cpp",
                    "src/runtime/skater_events.cpp", "src/runtime/trick_database.cpp", "src/runtime/trick_combo_tables.cpp", "src/runtime/rail_nodes.cpp", "src/runtime/rail_create.cpp", "src/runtime/collision_cache.cpp", "src/runtime/collision_query.cpp", "src/runtime/collision_zone.cpp", *MATH],
        "slices": ["skater-pose-and-state", "skater-small-handlers", "skater-bail-and-pad-history", "tricks-bin-loader", "skater-stats", "skater-motion-handlers"],
        "return": "TrickInput_PressTime",
        "write": "Skater_RestoreSavedPose",
        "service": ("Animation_RequestStart", "engine_00480730"),
        "min_cases": 5000,
    },
    "m3d": {
        "test": "src/runtime/m3d_retail_calls_test.cpp",
        "sources": ["src/runtime/m3d_matrix.cpp", "src/runtime/m3d_boxes.cpp", "src/runtime/m3d_transform.cpp", "src/runtime/m3d_collision.cpp", "src/runtime/collision_cache.cpp", *MATH],
        "slices": ["m3d-matrix-helpers", "m3d-culling-boxes", "m3d-model-transforms"],
        "return": "Math_MulDivTruncated",
        "write": "Matrix_CopyQ12",
        "service": None,
        "min_cases": 500,
    },
}


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


@pytest.fixture(scope="module", params=sorted(KITS))
def kit(request, tmp_path_factory):
    name = request.param
    spec = KITS[name]
    return name, spec, _compile(tmp_path_factory.mktemp(f"{name}_replay"), f"opentony_{name}_retail_calls_test", [spec["test"], *spec["sources"]])


def _fixtures(name):
    return sorted(FIXTURE_DIR.glob(f"{name}-calls-*.txt"))


def test_fixtures_are_present(kit):
    name, _, _ = kit
    assert _fixtures(name), f"no {name} fixtures in re/evidence/fixtures"


def test_native_layer_matches_every_retail_call(kit):
    name, spec, tool = kit
    run = subprocess.run([str(tool), *map(str, _fixtures(name))], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= spec["min_cases"] // 10, first


def test_every_replayed_function_carries_a_curated_name(kit):
    name, _, _ = kit
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in _fixtures(name):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def _break(tool, name, tmp_path, marker, edit, needs=""):
    """Replays every fixture with the first case line that carries `marker` (and `needs`) rewritten by `edit`; the replay must fail."""
    for source in _fixtures(name):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and marker in line and needs in line:
                lines[index] = edit(line)
                broken = tmp_path / f"{name}-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return True
    return False


def test_replay_has_teeth_on_a_return_value(kit, tmp_path):
    name, spec, tool = kit

    def edit(line):
        head, tail = line.split(" ret=", 1)
        value, rest = tail.split(" ", 1)
        return head + " ret=" + format((int(value, 16) + 1) & 0xFFFF, "x") + " " + rest

    assert _break(tool, name, tmp_path, f"name={spec['return']} ", edit)


def test_replay_has_teeth_on_a_written_byte(kit, tmp_path):
    name, spec, tool = kit

    def edit(line):
        head, writes = line.split("writes=", 1)
        digits, rest = writes.split(";", 1) if ";" in writes else (writes.split(" ", 1)[0], " " + writes.split(" ", 1)[1])
        address, data = digits.split("=", 1)
        flipped = ("0" if data[0] != "0" else "1") + data[1:]
        return head + "writes=" + address + "=" + flipped + ((";" + rest) if not rest.startswith(" ") else rest)

    assert _break(tool, name, tmp_path, f"name={spec['write']} ", edit, needs="writes=")


def test_replay_has_teeth_on_a_service_call(kit, tmp_path):
    name, spec, tool = kit
    if spec["service"] is None:
        pytest.skip("this layer reaches no service")
    function, service = spec["service"]

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

    assert _break(tool, name, tmp_path, f"name={function} ", edit, needs=f"calls={service}")


def test_every_slice_is_complete_with_no_open_questions(kit):
    _, spec, _ = kit
    for sid in spec["slices"]:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
