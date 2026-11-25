"""Phase 5.3b: movies and music on FFmpeg through the HAL, no Bink (re/config/ffmpeg.yml, src/hal/ffmpeg/, src/hooks/kits/standalone_movie.cpp).

The tests that need no Wine check the pin, the licence guards and where the code is linked. The tests of the decode itself run the 32-bit
tools under Wine (`OPENTONY_WINE_TESTS=1`, after `tony hooks build`): `opentony_movie_compare.exe` puts binkw32.dll and the FFmpeg source on the same
file, `opentony_movie_player.exe` plays through the real natives and the seams.
"""

from __future__ import annotations

import os
import re
import subprocess
from pathlib import Path

import pytest

from tony import ffmpeg
from tony.common import load_yaml, wine_env

ROOT = Path(__file__).resolve().parents[1]
KITS = ROOT / "build/hooks/kits"
DISC = ROOT / "build/disc/files/SETUP"
BINK = DISC / "data/binkw32.dll"
WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"


# ---- no Wine ----------------------------------------------------------------------------------------------------------------------


def test_the_pin_is_a_decode_only_lgpl_build_with_the_bink_components_only():
    spec = ffmpeg.spec()
    flags = spec["configure"]
    assert re.fullmatch(r"[0-9a-f]{64}", spec["sha256"]) and spec["url"].endswith(f"ffmpeg-{spec['release']}.tar.xz")
    assert {"--disable-everything", "--enable-shared", "--disable-static", "--disable-gpl", "--disable-nonfree", "--disable-version3"} <= set(flags)
    assert not [flag for flag in flags if flag in ("--enable-gpl", "--enable-nonfree", "--enable-version3") or flag.startswith("--enable-lib")]
    assert "--enable-demuxer=bink" in flags and "--enable-decoder=bink,binkaudio_dct,binkaudio_rdft" in flags
    assert not any(flag.startswith(("--enable-encoder", "--enable-muxer", "--enable-filter")) for flag in flags)
    assert spec["libraries"] == ["avutil", "avcodec", "avformat"]


def test_the_x86_64_pin_is_the_same_build_for_the_other_target():
    """11b: the 64-bit standalone loads a 64-bit FFmpeg: the same source and options, only the flags that name the target differ."""
    base, wide = ffmpeg.spec(), ffmpeg.spec("x86_64")
    assert "--arch=x86_64" in wide["configure"] and "--arch=x86" not in wide["configure"]
    assert "--cross-prefix=x86_64-w64-mingw32-" in wide["configure"] and "--cc=x86_64-w64-mingw32-clang" in wide["configure"]
    shared = lambda flags: [flag for flag in flags if not flag.startswith(("--arch=", "--cross-prefix=", "--cc=", "--cxx="))]
    assert shared(wide["configure"]) == shared(base["configure"])
    assert wide["sha256"] == base["sha256"] and wide["libraries"] == base["libraries"]
    assert wide["install_dir"] != base["install_dir"] and wide["build_dir"] != base["build_dir"]
    assert "x86_64" not in base and "x86_64" not in wide


def test_the_licence_check_refuses_a_gpl_build(tmp_path):
    (tmp_path / "config.h").write_text("#define CONFIG_GPL 1\n#define CONFIG_NONFREE 0\n#define CONFIG_VERSION3 0\n#define CONFIG_GPLV3 0\n")
    assert any("CONFIG_GPL " in error or "CONFIG_GPL is" in error for error in ffmpeg.licence_errors(tmp_path))
    (tmp_path / "config.h").write_text("#define CONFIG_GPL 0\n#define CONFIG_NONFREE 0\n#define CONFIG_VERSION3 0\n#define CONFIG_GPLV3 0\n")
    assert ffmpeg.licence_errors(tmp_path) == []


def test_the_notice_names_the_pinned_release_and_its_hash():
    notices = (ROOT / "THIRD_PARTY_NOTICES.md").read_text()
    spec = ffmpeg.spec()
    assert f"FFmpeg {spec['release']}" in notices and spec["sha256"] in notices and "LGPL" in notices


def test_ffmpeg_is_linked_into_the_standalone_and_the_tools_never_the_hooked_dll():
    cmake = (ROOT / "src/hooks/kits/CMakeLists.txt").read_text()
    native = re.search(r"add_library\(opentony_native SHARED(.*?)\)\n", cmake, re.DOTALL).group(1)
    assert "movie" not in native
    assert "target_link_libraries(opentony_native PRIVATE opentony_kits_runtime)" in cmake
    assert "standalone_movie.cpp" in re.search(r"add_library\(opentony_standalone SHARED(.*?)\)\n", cmake, re.DOTALL).group(1)
    assert "target_link_libraries(opentony_standalone PRIVATE opentony_kits_runtime opentony_movie_source opentony_zlib)" in cmake


def test_every_bink_seam_has_an_implementation_in_the_standalone_movie_layer():
    seams = load_yaml("re/symbols/seams.yml")["seams"]
    source = (ROOT / "src/hooks/kits/standalone_movie.cpp").read_text()
    for seam in seams:
        if seam["category"] == "bink":
            assert f"seam::bink::{seam['name']}" in source, seam["name"]
    for name in ("win32_BinkWait", "win32_BinkService", "win32_BinkPause", "win32_BinkSetVolume", "win32_BinkSetPan"):
        assert name in source


# ---- Wine -------------------------------------------------------------------------------------------------------------------------

needs_wine = pytest.mark.skipif(
    not WINE or not (KITS / "opentony_movie_compare.exe").is_file() or not BINK.is_file() or not ffmpeg.installed(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` done and the disc extracted (runs under Wine)",
)


def _winepath(path: Path) -> str:
    return subprocess.run(["winepath", "-w", str(path)], capture_output=True, text=True, check=True, env=_env()).stdout.strip()


def _env() -> dict[str, str]:
    env = wine_env(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    env["WINEDEBUG"] = "-all"
    return env


def _run(exe: str, *arguments: str, timeout: int = 900) -> str:
    env = _env()
    env["OPENTONY_FFMPEG_DIR"] = _winepath(ffmpeg.install_dir() / "bin")
    env["OPENTONY_MEDIA_DIR"] = _winepath(DISC)
    result = subprocess.run(["wine", str(KITS / exe), *arguments], cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout, check=False)
    return result.stdout + result.stderr


def _values(output: str) -> dict[str, str]:
    return {m.group(1): m.group(2) for m in re.finditer(r"^(\w+) (\S+)", output, re.MULTILINE)}


def _movie(name: str) -> str:
    return _winepath(DISC / "med/Movies" / f"{name}.dat")


def _compare_video(name: str, *extra: str) -> dict[str, str]:
    return _values(_run("opentony_movie_compare.exe", "video", _winepath(BINK), _movie(name), *extra))


@needs_wine
@pytest.mark.parametrize("name,frames", [("NSlogo", 266), ("ATVILOGO", 232), ("B1", 455)])
def test_every_frame_is_bink_s_frame_exactly(name, frames):
    # a 320x240 logo, the Activision logo, and a 176x48 level-select clip: every pixel of every frame, in the 32-bit surface
    values = _compare_video(name)
    assert values["result"] == "IDENTICAL", values
    assert int(values["frames_compared"]) == frames and int(values["pixels_different"]) == 0 and values["max_channel_difference"] == "0"


@needs_wine
def test_the_16_bit_surface_is_bink_s_too():
    values = _compare_video("NSlogo", "60", "565")
    assert values["result"] == "IDENTICAL" and int(values["frames_compared"]) == 60 and int(values["pixels_different"]) == 0


@needs_wine
@pytest.mark.parametrize("name", ["ATVILOGO", "Muska"])
def test_the_sound_is_bink_s_except_the_block_cross_fades(name):
    # Bink's decoder and FFmpeg's are two float implementations. They agree exactly except where one block is blended into the next (the
    # first 128 samples of each channel of a block): by 1 or 2 least significant bits in the quiet parts, more in loud transients.
    # Measured on 2026-10-29: ATVILOGO 94.5 % of the samples equal, 4.5 % off by 1, 0.9 % by 2, none by more; Muska 94.5 %, 4.7 %, 0.8 %.
    output = _run("opentony_movie_compare.exe", "sound", _winepath(BINK), _movie(name))
    values = _values(output)
    found = re.search(r"difference_histogram equal (\d+) off_by_1 (\d+) off_by_2 (\d+) more (\d+)", output)
    assert found, output
    equal, off_by_1, off_by_2, more = (int(v) for v in found.groups())
    compared = int(values["samples_compared"])
    assert compared == equal + off_by_1 + off_by_2 + more
    assert equal / compared >= 0.93
    assert off_by_1 + off_by_2 > 0  # this is a measured difference, not equality
    assert more / compared < 0.001
    assert values["ffmpeg_extra_samples"] == "0"  # the sound is not longer than the picture, as Bink's is not


@needs_wine
@pytest.mark.skipif(not (KITS / "opentony_movie_player.exe").is_file(), reason="opentony_movie_player.exe not built")
def test_the_startup_movies_play_through_the_natives_and_the_seams_as_bink_plays_them(tmp_path):
    out = tmp_path / "play"
    out.mkdir()
    names = ["ATVILOGO", "NSLOGO"]
    output = _run("opentony_movie_player.exe", _winepath(KITS / "opentony_standalone.dll"), _winepath(out), "startup", *names)
    assert "NOT PLAYED" not in output and "FATAL" not in output, output
    sections: dict[str, list[str]] = {}
    current = ""
    for line in (out / "frames.txt").read_text().splitlines():
        words = line.split()
        if words[0] == "movie":
            current = words[1].upper()
            sections[current] = []
        elif len(words) == 2 and re.fullmatch(r"[0-9a-f]{16}", words[1]):
            sections[current].append(words[1])
    for name, file_name in (("ATVILOGO", "ATVILOGO"), ("NSLOGO", "NSlogo")):
        hashes = tmp_path / f"{name}.txt"
        compare = _values(_run("opentony_movie_compare.exe", "video", _winepath(BINK), _movie(file_name), "0", "32", _winepath(hashes)))
        assert compare["result"] == "IDENTICAL"
        bink = [line.split()[1] for line in hashes.read_text().splitlines()]
        assert sections[name] == bink, name       # every frame the natives and seams delivered is Bink's frame
    # the sound the seams fed the HAL stream is the decoder's: its length is the length of the picture
    info = dict(line.split() for line in (out / "audio.txt").read_text().splitlines())
    assert info["rate"] == "44100" and info["channels"] == "2"
    assert int(info["bytes"]) == (1364160 + 1564080) * 2  # the sample counts of the two files, 16 bits each (picture length x rate x channels)


@needs_wine
@pytest.mark.skipif(not (KITS / "opentony_movie_player.exe").is_file(), reason="opentony_movie_player.exe not built")
def test_a_movie_from_a_level_plays_full_screen_and_returns(tmp_path):
    # control mode 0 (the bails video after a medal): PCMovie_PlayStartupMedia ends with D3DDevice->SetTexture(0, NULL). The standalone has no
    # Direct3D device, so the first such movie gets a stand-in; without it the call read a vtable at guest address 0 (GitHub report, v0.7.1)
    out = tmp_path / "play"
    out.mkdir()
    output = _run("opentony_movie_player.exe", _winepath(KITS / "opentony_standalone.dll"), _winepath(out), "fullscreen", "NSLOGO", "NSLOGO")
    assert "FATAL" not in output and "CRASH" not in output, output[-2000:]
    assert output.count("fullscreen NSLOGO: played") == 2, output[-2000:]    # the second movie reuses the stand-in
    assert "RESULT 532 frames decoded" in output


@needs_wine
@pytest.mark.skipif(not (KITS / "opentony_movie_player.exe").is_file(), reason="opentony_movie_player.exe not built")
def test_a_level_select_clip_plays_a_frame_per_update_and_loops_and_music_streams_to_its_end(tmp_path):
    clip = tmp_path / "clip"
    clip.mkdir()
    output = _run("opentony_movie_player.exe", _winepath(KITS / "opentony_standalone.dll"), _winepath(clip), "clip", "B1", "460")
    assert "NOT STARTED" not in output and "FATAL" not in output, output
    ours = [line.split()[1] for line in (clip / "frames.txt").read_text().splitlines() if re.fullmatch(r"\d+ [0-9a-f]{16}", line)]
    hashes = tmp_path / "B1.txt"
    assert _values(_run("opentony_movie_compare.exe", "video", _winepath(BINK), _movie("B1"), "0", "32", _winepath(hashes)))["result"] == "IDENTICAL"
    bink = [line.split()[1] for line in hashes.read_text().splitlines()]
    assert len(bink) == 455 and ours[:455] == bink
    assert ours[455:460] == bink[:5]  # a clip in texture mode loops: the frame after the last is the first

    music = tmp_path / "music"
    music.mkdir()
    output = _run("opentony_movie_player.exe", _winepath(KITS / "opentony_standalone.dll"), _winepath(music), "music", "5")
    assert "NOT STARTED" not in output and "still playing" not in output and "FATAL" not in output, output
    info = dict(line.split() for line in (music / "audio.txt").read_text().splitlines())
    assert (info["rate"], info["channels"], info["bytes"]) == ("44100", "2", str(3980760 * 2))  # LTIX05.DAT: 45 s of 16-bit stereo
