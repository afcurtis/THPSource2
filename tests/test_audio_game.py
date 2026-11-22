"""Phase 4.13b: the game's audio and movie layer (src/runtime/audio_*.cpp: the crowd, the redbook sequencer, the sound effect front, the VAB banks, the game FMV list and the movie
interface) against the retail forced-call fixtures (re/evidence/fixtures/audio-calls-*.txt)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
SOURCES = ["src/runtime/audio_crowd.cpp", "src/runtime/audio_redbook.cpp", "src/runtime/audio_sfx.cpp", "src/runtime/audio_movie.cpp"]
SLICES = ["crowd-audio", "redbook-sequencer", "sfx-front", "vab-banks", "game-fmv-and-movie-interface"]


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
    return _compile(tmp_path_factory.mktemp("audio_replay"), "opentony_audio_retail_calls_test", ["src/runtime/audio_retail_calls_test.cpp", *SOURCES])


def test_audio_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("audio-calls-*.txt")), "no audio fixtures in re/evidence/fixtures"


def test_native_audio_layer_matches_every_retail_call(replay_tool):
    fixtures = sorted(FIXTURE_DIR.glob("audio-calls-*.txt"))
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1500, first


def test_every_replayed_function_carries_a_curated_name():
    names = {entry["address"]: entry["name"] for entry in yaml.safe_load((ROOT / "re/symbols/functions.yml").read_text())["functions"]}
    for fixture in sorted(FIXTURE_DIR.glob("audio-calls-*.txt")):
        for line in fixture.read_text().splitlines():
            if line.startswith("C "):
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                assert names[int(fields["fn"], 16)] == fields["name"], (fixture.name, fields["fn"])


def _break(replay_tool, tmp_path, marker, edit):
    """Replays every fixture with the first case line that carries `marker` rewritten by `edit`; the replay must fail."""
    for source in sorted(FIXTURE_DIR.glob("audio-calls-*.txt")):
        lines = source.read_text().splitlines()
        for index, line in enumerate(lines):
            if line.startswith("C ") and marker in line:
                lines[index] = edit(line)
                broken = tmp_path / "audio-calls-broken.txt"
                broken.write_text("\n".join(lines) + "\n")
                run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
                assert run.returncode != 0
                return True
    return False


def test_replay_has_teeth_on_a_written_byte(replay_tool, tmp_path):
    """A changed byte of a retail write makes the replay fail."""

    def edit(line):
        head, writes = line.split("writes=", 1)
        digits, rest = writes.split(";", 1) if ";" in writes else (writes, "")
        address, data = digits.split("=", 1)
        flipped = ("0" if data[0] != "0" else "1") + data[1:]
        return head + "writes=" + address + "=" + flipped + ((";" + rest) if rest else "")

    if not _break(replay_tool, tmp_path, "name=Redbook_Reset ", edit):
        pytest.skip("no sequencer reset case")


def test_replay_has_teeth_on_a_service_call(replay_tool, tmp_path):
    """A changed argument of a reported service (the HAL voice play) makes the replay fail."""
    if not _break(replay_tool, tmp_path, "engine_004f2e20,", lambda line: line.replace("engine_004f2e20,", "engine_004f2e20,1", 1)):
        pytest.skip("no case that plays a voice")


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
