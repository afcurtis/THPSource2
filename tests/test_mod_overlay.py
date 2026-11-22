"""Phase 5.2: the loose-file overlay (src/hooks/mod_overlay*.{h,cpp}, src/hooks/native/overlay_open.cpp).

The key rules and the index have a native unit test (src/hooks/mod_overlay_core_test.cpp, ctest). The rest of
this file checks the manifest row, and (opt-in, a few minutes under Wine: OPENTONY_WINE_TESTS=1) that the staged
game really serves a mod's texture, sound and script and plays the script's level exactly as before.
"""

from __future__ import annotations

import math
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

from tony import hooks
from tony.common import load_yaml

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "build/assets/all-pkr/files/data"
GAME = ROOT / "build/disc/files/SETUP/data"


def test_overlay_hook_is_installed_but_off_until_a_mod_has_files():
    rows = [row for row in load_yaml("re/config/native_hooks.yml")["hooks"] if row["name"] == "FileIO_BackendOpen"]
    assert len(rows) == 1
    row = rows[0]
    assert row["abi"] == "cdecl_overlay_open"
    assert row["default"] == "off"
    assert set(row["builds"]) == {"canonical"}  # the slot arrays and the allocator are the canonical build's
    assert hooks.default_on(row, "trace-validated") is False
    assert row["overwrite"] == 6 and row["expected"] == "81 ec d8 01 00 00"


def test_core_unit_test_is_registered_with_ctest():
    assert "opentony_mod_overlay_core_test" in (ROOT / "CMakeLists.txt").read_text()


def test_overlay_sources_are_built_into_the_hook_dll():
    cmake = (ROOT / "src/hooks/CMakeLists.txt").read_text()
    assert "native/overlay_open.cpp" in cmake and "mod_overlay.cpp" in cmake


# ------------------------------------------------------------------ in the game (opt-in)

WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"


def _wine_path(path: Path) -> str:
    return "Z:" + str(path.resolve()).replace("/", "\\")


def _make_mod(folder: Path, *, texture: bool, sound: bool, script: bool) -> None:
    if folder.exists():
        shutil.rmtree(folder)
    (folder / "Data" / "audio").mkdir(parents=True)
    (folder / "mod.ini").write_text("[Mod]\nName = Overlay Test\n")
    names = {p.name.lower(): p for p in ASSETS.iterdir()}
    if texture:  # same size, different palette
        bmp = bytearray(names["s2bgd0.bmp"].read_bytes())
        for i in range(54, struct.unpack_from("<I", bmp, 10)[0]):
            bmp[i] ^= 0xFF
        (folder / "Data" / "s2bgd0.bmp").write_bytes(bytes(bmp))
    if sound:  # 16-bit mono PCM, 0.2 s
        rate = 22050
        pcm = b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * i / rate))) for i in range(rate // 5))
        header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
        (folder / "Data" / "audio" / "selectD.wav").write_bytes(header + b"data" + struct.pack("<I", len(pcm)) + pcm)
    if script:  # byte-identical to the game's own
        shutil.copyfile(names["skware_t.trg"], folder / "Data" / "SkWare_T.trg")


def _tony(*args: str, mods: Path, check: bool = True) -> subprocess.CompletedProcess:
    env = dict(os.environ, OPENTONY_MODS=_wine_path(mods), OPENTONY_MOD_TRACE="1")
    return subprocess.run([sys.executable, "-m", "tony", *args], cwd=ROOT, env=env, capture_output=True, text=True, check=check)


@pytest.mark.skipif(not WINE, reason="set OPENTONY_WINE_TESTS=1 (runs the game under Wine)")
@pytest.mark.skipif(not (ASSETS / "SKWARE_T.TRG").is_file() or not (GAME / "opentony.dll").is_file(), reason="needs extracted assets and a staged hook build")
def test_mod_texture_sound_and_script_replace_the_games_files():
    work = ROOT / "build/mod-overlay-test"
    # texture and sound: the front end opens both while booting to the menu
    mod = work / "mods/textsnd"
    _make_mod(mod, texture=True, sound=True, script=False)
    _tony("session", "run", "boot-menu", "--hooks", "none,FileIO_BackendOpen", "--label", "mod-overlay-test", mods=mod)
    run = ROOT / "build/sessions/runs/mod-overlay-test"
    log = (run / "opentony.log").read_text(errors="replace")
    assert (run / "session.done").is_file()
    assert "mods: replaced s2bgd0.bmp <- " in log
    assert "mods: replaced audio/selectd.wav <- " in log
    # the optional high-resolution variant is a different file: the texture must not stand in for it
    assert "replaced newbmp/s2bgd0.bmp" not in log

    # script: a byte-identical copy of SkWare_T.trg must play the level exactly as the packaged one does
    # (same folder-name length keeps the environment block, and with it the heap, the same size)
    empty, trg = work / "mods/emptymd", work / "mods/trgonly"
    _make_mod(empty, texture=False, sound=False, script=False)
    _make_mod(trg, texture=False, sound=False, script=True)
    for name, mod, selection in (("A", empty, "none"), ("B", trg, "none,FileIO_BackendOpen")):
        output = work / name
        output.unlink(missing_ok=True)
        done = _tony("scenario", "capture", "warehouse-idle", "--hooks", selection, "--clock", "lockstep-fast", "--force",
                     "--output", str(output), mods=mod)
        assert done.returncode == 0
    # the log of the last run (B) shows the script came from the mod
    assert "mods: replaced skware_t.trg <- " in (ROOT / "build/hooks/runtime/opentony.log").read_text(errors="replace")
    from tony.capture import compare_recordings

    # "portable" leaves out the process identity (heap pointers): the mod's index is an allocation the packaged run lacks
    result = compare_recordings(work / "A", work / "B", scope="portable")
    assert result["equal"], result.get("difference")
