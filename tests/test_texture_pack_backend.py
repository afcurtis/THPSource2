"""The GL backend's texture pack (OT_TEXTURE_PACK) against a real GL context: build/hooks/opentony_texture_pack_test.exe under Wine and Xvfb.

Skipped where the tool is not built (`tony hooks build`) or Wine / Xvfb are missing. The pack folder is made here, with the keys computed from the documented FNV-1a
(src/hal/texture_key_hash.h) on the texels the tool uploads, so this also checks that the key means in the backend what the docs say it means."""

import importlib.util
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "build" / "hooks" / "win32" / "opentony_texture_pack_test.exe"

spec = importlib.util.spec_from_file_location("texture_test_pack", ROOT / "scripts" / "texture_test_pack.py")
ttp = importlib.util.module_from_spec(spec)
sys.modules.setdefault("texture_test_pack", ttp)
spec.loader.exec_module(ttp)

ARGB1555 = 2


def key(width: int, height: int, fmt: int, rows: bytes) -> str:
    h = 0xCBF29CE484222325
    for byte in struct.pack("<III", width, height, fmt) + rows:
        h = ((h ^ byte) * 0x100000001B3) & (2**64 - 1)
    return f"{h:016x}"


def texels(words: list[int]) -> bytes:
    return struct.pack(f"<{len(words)}H", *words)


def solid(width: int, height: int, rgba: tuple[int, int, int, int]) -> bytes:
    return bytes(rgba) * (width * height)


def make_pack(pack):
    pack.mkdir()
    red, yellow, magenta = [0xFC00] * 16, [0xFFE0] * 16, [0xFC1F] * 16
    cutout = [0x7FFF] * 8 + [0xFFFF] * 8
    smooth = bytes([255, 255, 255, 40]) * (16 * 8) + bytes([255, 255, 255, 255]) * (16 * 8)
    ttp.write_png(pack / f"{key(4, 4, ARGB1555, texels(red))}.png", 16, 16, solid(16, 16, (0, 0, 255, 255)))
    ttp.write_png(pack / f"{key(4, 4, ARGB1555, texels(yellow))}.png", 8, 4, solid(8, 4, (0, 255, 255, 255)))   # not the same shape
    ttp.write_png(pack / f"{key(4, 4, ARGB1555, texels(magenta))}.png", 2, 2, solid(2, 2, (0, 255, 255, 255)))  # smaller than the texture
    ttp.write_png(pack / f"{key(4, 4, ARGB1555, texels(cutout))}.png", 16, 16, smooth)                          # a cut-out with a smooth alpha
    (pack / "stamp.txt").write_text("not a key\n")
    (pack / "NOTAKEY.png").write_bytes(b"")


@pytest.mark.skipif(not TOOL.is_file() or shutil.which("wine") is None or shutil.which("xvfb-run") is None, reason="needs opentony_texture_pack_test.exe, wine and xvfb-run")
def test_texture_pack_in_the_gl_backend(tmp_path):
    from tony import sdl3
    from tony.common import headless_wine_env
    from tony.hooks import _wine_z_path

    pack = tmp_path / "pack"
    make_pack(pack)

    # a display above the gate workers' (:60..:99, tony/workers.py): `xvfb-run -a` alone starts at :99, worker 00's, and `tony gates full` runs pytest beside the Wine lane
    completed = subprocess.run(
        ["xvfb-run", "-a", "-n", "140", "-s", "-screen 0 1024x768x24", "wine", str(TOOL), _wine_z_path(pack) + "\\", _wine_z_path(sdl3.dll_path().parent) + "\\"],
        env=headless_wine_env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=300, check=False)
    lines = [line for line in completed.stdout.splitlines() if line.startswith(("ok ", "FAIL", "PASS"))]
    assert lines and lines[-1].startswith("PASS"), "\n".join(lines or completed.stdout.splitlines()[-20:])
    assert not [line for line in lines if line.startswith("FAIL")]


def test_texture_pack_in_the_native_gl_backend(tmp_path):
    tool = os.environ.get("OPENTONY_NATIVE_TEXTURE_TEST")
    if not tool:
        pytest.skip("set OPENTONY_NATIVE_TEXTURE_TEST to the native texture test executable")
    pack = tmp_path / "pack"
    make_pack(pack)
    completed = subprocess.run(
        [tool, str(pack)], capture_output=True, text=True, timeout=60, check=False)
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert "PASS: 0 failures" in completed.stdout
