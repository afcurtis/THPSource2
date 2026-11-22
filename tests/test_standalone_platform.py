"""Milestone 8a: the window, input, sound and clock of the standalone (src/hooks/kits/standalone_platform.cpp, standalone_audio.cpp, standalone_video.cpp).

Everything here runs `opentony_spike.exe` under Wine, so it needs OPENTONY_WINE_TESTS=1, `tony hooks build` and the main-menu image of 7c. The scripted-input check lives in
tony/standalone_frame.py (`python -m tony.standalone_frame --input`); the audio functions are called through the resolver with their retail arguments and the HAL calls they
make are read from OT_AUDIO_LOG. The expected numbers are computed from the retail code (Audio_PlaySound 0x004f2e20 and its siblings): volume = max(left, right) x scale x 0.4 -
2857 millibels, pan = -+10000 x quieter / louder, frequency = pitch / 60 x 44100."""

from __future__ import annotations

import os
import re
import subprocess
import time

import pytest

from tony import standalone_frame as sf
from tony.common import ROOT, resolve

WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"
needs_wine = pytest.mark.skipif(
    not WINE or not (ROOT / "build/hooks/kits/opentony_spike_video.dll").is_file()
    or not (ROOT / "build/sessions/runs" / sf.IMAGE_LABEL / f"session_{sf.IMAGE_DELIVERY}.img").is_file()
    or not (ROOT / "build/assets/all-pkr/files/audio").is_dir(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build`, the main-menu image and the extracted ALL.PKR (runs opentony_spike.exe under Wine)",
)

INIT, LOAD, FREE, PLAY, SET_VOLUME, SET_FREQUENCY, GET_FREQUENCY, STOP, PAUSE, RESUME = (
    "0x004f27c0", "0x004f2960", "0x004f2ad0", "0x004f2e20", "0x004f34b0", "0x004f3690", "0x004f3720", "0x004f37c0", "0x004f3900", "0x004f3940")
CONTROLFP = "0x005015a6"   # the C runtime's _controlfp: D3D leaves the game at 24 bits


def _audio_run(calls: str, extra: dict[str, str] | None = None) -> tuple[list[str], list[str]]:
    """The spike with these calls, no video: the HAL audio service is the one that makes no sound. Returns the audio log and the values the calls returned."""

    out = resolve("build/standalone-audio-test")
    out.mkdir(parents=True, exist_ok=True)
    command, env = sf.spike_command(out, 10, False)
    for name in ("OT_VIDEO_DIR", "OT_SPIKE_FRAMES"):
        env.pop(name, None)
    env.update({"OT_SPIKE_CALL": calls, "OT_AUDIO_LOG": "1", **(extra or {})})
    result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=300, check=False)
    text = (result.stdout or "") + (result.stderr or "")
    assert "CRASH" not in text and "neither a native nor a seam" not in text, text[-2000:]
    return [line for line in text.splitlines() if line.startswith("audio: hal")], re.findall(r"^returned ([0-9a-f]{8})$", text, re.MULTILINE)


@needs_wine
def test_a_sound_bank_loads_and_a_sound_plays_with_the_volume_pan_and_frequency_retail_computes():
    # bank 0 is the front end's: id 144 is selectD (scale 0.9, pitch 60), id 145 acceptA (scale 1.0). Mode 0: duplicates, so a voice is (duplicate << 16) | slot.
    hal, returned = _audio_run(f"{INIT},{LOAD}(0),poke:0x006a6964=0,{PLAY}(144,100,100),{PLAY}(145,100,50),{PLAY}(144,100,100)")
    assert returned[2:] == ["00010000", "00010001", "00020000"]     # duplicates 1 and 2 of slot 0, duplicate 1 of slot 1
    plays = [line for line in hal if "voice_play" in line]
    assert plays[0] == "audio: hal voice_play sound 1 volume -2821 pan 0 frequency 44100 flags 0 at 0"       # 100 x 0.9 x 0.4 = 36; 36 - 2857
    assert plays[1] == "audio: hal voice_play sound 2 volume -2817 pan -5000 frequency 44100 flags 0 at 0"   # 40 - 2857; right is half the left: -10000 x 50 / 100
    assert plays[2] == plays[0]


@needs_wine
def test_a_looping_sound_is_flagged_paused_and_resume_stops_it_as_retail_does():
    # bank 4 is a level's: id 1 is rollwoodA (loops, scale 0.5, pitch 60), id 0 rollconcrete2 (loops, scale 0.2, pitch 100)
    hal, _ = _audio_run(f"{INIT},{LOAD}(4),poke:0x006a6964=0,{PLAY}(1,100,100),{PAUSE},{RESUME},{PLAY}(0,100,100),{PAUSE}")
    lines = [line for line in hal if "voice_play" in line or "voice_stop" in line]
    assert lines[0] == "audio: hal voice_play sound 1 volume -2837 pan 0 frequency 44100 flags 1 at 0"    # flags 1: the HAL voice loops
    assert lines[1] == "audio: hal voice_stop 1"                                                         # Audio_PauseLooping halts it
    assert "voice_play sound 2 volume -2849 pan 0 frequency 73500 flags 1" in lines[2]                    # 100 / 60 x 44100; 100 x 0.2 x 0.4 - 2857
    assert lines[3] == "audio: hal voice_stop 2"
    assert len(lines) == 4                                                                               # Audio_ResumeLooping had nothing left to stop: it stops, it does not restart


def _shared_voice_calls(extra: dict[str, str]) -> list[str]:
    """Two emitters of one looping sound in one tick (the cars of NYC): the far one asks for silence first, then the near one for its volume."""

    setup = f"{INIT},{LOAD}(4),poke:0x006a6964=1,{PLAY}(1,100,100)"      # the windowed set-up: a sound plays on its first buffer, every emitter shares the voice
    _, returned = _audio_run(setup)
    voice = f"0x{returned[-1]}"
    hal, _ = _audio_run(f"{setup},{SET_VOLUME}({voice},0,0),{SET_VOLUME}({voice},100,100),{SET_VOLUME}({voice},0,0)", extra)
    return [line for line in hal if "voice_play" in line or "voice_stop" in line]


@needs_wine
def test_retail_restarts_a_shared_looping_voice_every_time_a_far_emitter_asks_for_silence():
    # the bug of NYC and Philadelphia, kept as retail has it: silence stops the voice, the next emitter starts it again from the first sample
    lines = _shared_voice_calls({})
    assert [line.split()[2] for line in lines] == ["voice_play", "voice_stop", "voice_play", "voice_stop"]


@needs_wine
def test_a_shared_looping_voice_keeps_playing_when_a_far_emitter_asks_for_silence_beside_a_near_one():
    # OT_VEHICLE_SOUND_FIX: the nearest (loudest) emitter alone moves the voice; the far one's silence only mutes it until a tick nobody wanted it
    lines = _shared_voice_calls({"OT_VEHICLE_SOUND_FIX": "1"})
    assert [line.split()[2] for line in lines] == ["voice_play"]


@needs_wine
def test_the_frequency_is_computed_at_the_precision_the_game_set():
    # 44100 x (1 / 44100) x 60 truncates to 59 at 53 bits and is exactly 60 at the 24 bits D3D sets
    _, returned = _audio_run(f"{INIT},{LOAD}(0),poke:0x006a6964=0,{PLAY}(144,100,100),{GET_FREQUENCY}(0x10000),{CONTROLFP}(0x20000,0x30000),{GET_FREQUENCY}(0x10000)")
    assert returned[-3] == "0000003b" and returned[-1] == "0000003c"


@needs_wine
def test_a_slot_of_a_session_image_is_silent_and_a_free_bank_forgets_its_sounds():
    # the main-menu image holds retail's DirectSound pointers, which this program has not: nothing to play until the bank is loaded here
    _, returned = _audio_run(f"{PLAY}(144,100,100),{INIT},{LOAD}(0),{FREE}(0),{PLAY}(144,100,100)")
    assert returned[0] == "ffffffff" and returned[-1] == "ffffffff"


@needs_wine
def test_a_scripted_pad_pushes_a_stick_with_no_controller_attached(tmp_path):
    # camera plan C3: a held left x shows up as the joystick x word (20000 / 32767 x 1000 = 610), the right stick and a button are traced, release returns to rest
    script = tmp_path / "pad.txt"
    script.write_text("2 pad axis 0 20000\n3 pad axis 2 -32768\n3 pad down 8\n6 pad axis 0 0\n6 pad axis 2 0\n6 pad up 8\n")
    from tony.hooks import _wine_z_path

    text = sf.run_standalone(resolve("build/standalone-pad-test"), ticks=12, render=False,
                             extra={"OT_INPUT_LOG": _wine_z_path(script), "OT_INPUT_TRACE": "1", "OT_AUDIO": "off"})
    pad = [line for line in text.splitlines() if re.match(r"input: poll \d+: pad ", line)]
    assert [re.match(r"input: poll (\d+): pad buttons (\w+) axes (-?\d+) -?\d+ (-?\d+) .* joystick (-?\d+) (-?\d+)", line).groups() for line in pad] == [
        ("2", "00000", "20000", "0", "610", "0"),
        ("3", "00100", "20000", "-32768", "610", "0"),
        ("6", "00000", "0", "0", "0", "0"),
    ]


@needs_wine
def test_the_free_look_switches_are_read_and_reported(tmp_path):
    # camera plan C4: OT_FREELOOK=spring|stay switches the look on (stay acts as spring until C9), OT_FREELOOK_STICK=left picks the left stick, a bad value leaves it off
    def options(**env: str) -> list[str]:
        text = sf.run_standalone(resolve("build/standalone-freelook-test"), ticks=4, render=False, extra={"OT_AUDIO": "off", **env})
        return [line for line in text.splitlines() if "free look" in line or "OT_FREELOOK" in line]

    assert options(OT_FREELOOK="spring") == ["options: free look spring, right stick"]
    assert options(OT_FREELOOK="stay", OT_FREELOOK_STICK="left") == ["options: free look stay (acts as spring for now), left stick"]
    assert options(OT_FREELOOK="sideways") == ["options: OT_FREELOOK 'sideways' is not spring or stay; free look stays off"]
    assert options() == []


@needs_wine
def test_the_clock_follows_the_wall_clock_when_asked():
    """Realtime pacing: the multimedia timer's ticks fall due as time passes (an interval of 16 ms, about 62 a second), not one a Game_FrameTick call."""

    def run(frames: int) -> tuple[float, int]:
        started = time.time()
        text = sf.run_standalone(resolve("build/standalone-realtime-test"), ticks=frames, render=True, extra={"OT_VIDEO_SHOW": "1", "OT_PACING": "realtime", "OT_AUDIO": "off"})
        match = re.search(r"frame limit: \d+ frames of Game_FrameTick, sim tick (\d+)", text)
        assert match and "clock realtime" in text
        return time.time() - started, int(match.group(1))

    short_wall, short_tick = run(100)
    long_wall, long_tick = run(1500)
    rate = (long_tick - short_tick) / (long_wall - short_wall)
    assert 30 < rate < 100, rate
