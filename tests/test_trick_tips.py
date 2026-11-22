"""Phase 4.7b: the native trick tips (src/runtime/trick_tips.cpp) against hand-built cases and the
retail Warehouse trace (monitor object images, tip masks, display draw calls)."""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "re/evidence/fixtures/tricktips-warehouse.txt"
SOURCES = ["src/runtime/trick_tips.cpp", "src/runtime/trick_tip_table.cpp"]


def _compile(tmp_path, name, main):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = tmp_path / name
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(ROOT / "src/runtime"),
         str(ROOT / main), *[str(ROOT / s) for s in SOURCES], "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert build.returncode == 0, build.stderr
    return binary


@pytest.fixture(scope="module")
def unit_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("tips_unit"), "opentony_trick_tips_test", "src/runtime/trick_tips_test.cpp")


@pytest.fixture(scope="module")
def replay_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("tips_replay"), "opentony_trick_tips_retail_trace_test",
                    "src/runtime/trick_tips_retail_trace_test.cpp")


def test_hand_built_tip_semantics(unit_tool):
    run = subprocess.run([str(unit_tool)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr


def _summary(run):
    fields = {}
    for part in run.stdout.splitlines()[0].split():
        key, value = part.split("=")
        fields[key] = int(value)
    return fields


def test_native_tips_match_the_retail_trace(replay_tool):
    run = subprocess.run([str(replay_tool), str(FIXTURE)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    fields = _summary(run)
    assert fields["failures"] == 0
    assert fields["monitors"] >= 100
    assert fields["displays"] >= 100
    assert fields["selected"] >= 4  # the first-frame tip, hold, +0x3054, +0x3040 or +0x3038 rises


@pytest.mark.parametrize("kind", ["O", "D"])
def test_a_corrupted_trace_fails_the_replay(replay_tool, tmp_path, kind):
    """Changing one recorded value (an exit image word or a drawing argument) must make the replay
    fail: the check has teeth."""
    lines = FIXTURE.read_text().splitlines()
    for index, line in enumerate(lines):
        parts = line.split(" ")
        if parts[0] != kind:
            continue
        if kind == "D":
            calls = [i for i, p in enumerate(parts) if p.startswith("draw_panel_text:")]
            if not calls:
                continue
            fields = parts[calls[0]].split(":")
            fields[2] = str(int(fields[2]) + 1)
            parts[calls[0]] = ":".join(fields)
        else:
            diffs = [i for i, p in enumerate(parts[1:], start=1) if ":" in p]
            if not diffs:
                continue
            index_word, value = parts[diffs[0]].split(":")
            parts[diffs[0]] = f"{index_word}:{int(value, 16) ^ 1:x}"
        lines[index] = " ".join(parts)
        break
    else:
        pytest.skip(f"trace has no {kind} record to corrupt")
    corrupted = tmp_path / "corrupted.txt"
    corrupted.write_text("\n".join(lines) + "\n")
    run = subprocess.run([str(replay_tool), str(corrupted)], capture_output=True, text=True, check=False)
    assert run.returncode == 1, run.stdout + run.stderr
