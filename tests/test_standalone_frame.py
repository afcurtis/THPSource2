"""Milestone 7c: the standalone's first frame (tony/standalone_frame.py). The comparisons are tested on made-up data; the oracles themselves (the Options screen
drawn by opentony_spike.exe against the retail boot-options and boot-menu sessions) run under Wine with OPENTONY_WINE_TESTS=1."""

from __future__ import annotations

import os
import struct
from pathlib import Path

import pytest

from tony import standalone_frame as sf
from tony.common import ROOT


def _draw(texture: int, x: float, flags: int = 0x6c8) -> bytes:
    state = struct.pack("<IIBBBBI", texture, flags, 4, 5, 0, 0, 0)
    vertices = b"".join(struct.pack("<ffffIIff", x + k, 1.0, 1.0, 1.0, 0xFFFFFFFF, 0xFF000000, 0.0, 0.0) for k in range(4))
    body = state + struct.pack("<II", 2, 4) + vertices
    return bytes((1, 0, 0, 0)) + struct.pack("<I", len(body)) + body


def _stream(tmp_path: Path, name: str, draws: list[bytes]) -> Path:
    path = tmp_path / name
    path.write_bytes(struct.pack("<4I", 0x4856544F, 1, 0, len(draws)) + b"".join(draws))
    return path


def test_streams_match_when_only_the_texture_names_differ(tmp_path):
    retail = _stream(tmp_path, "retail.otvh", [_draw(0x0BAD0000, 1), _draw(0x0BAD1000, 2), _draw(0x0BAD0000, 3)])
    standalone = _stream(tmp_path, "standalone.otvh", [_draw(0x26, 1), _draw(0x18, 2), _draw(0x26, 3)])
    assert sf.compare_streams(retail, standalone)["pass"]


def test_a_texture_used_in_another_order_or_a_moved_vertex_is_a_difference(tmp_path):
    retail = _stream(tmp_path, "retail.otvh", [_draw(1, 1), _draw(2, 2), _draw(1, 3)])
    other_texture = _stream(tmp_path, "a.otvh", [_draw(7, 1), _draw(8, 2), _draw(8, 3)])
    moved = _stream(tmp_path, "b.otvh", [_draw(7, 1), _draw(8, 2.5), _draw(7, 3)])
    shorter = _stream(tmp_path, "c.otvh", [_draw(7, 1), _draw(8, 2)])
    assert sf.compare_streams(retail, other_texture)["differ"] == 1
    assert sf.compare_streams(retail, moved)["differ"] == 1
    assert sf.compare_streams(retail, shorter)["differ"] == 1


def test_a_screenshot_bmp_reads_as_the_video_oracles_image(tmp_path):
    width, height = 3, 2
    rows = [bytes((10, 20, 30)) * width + b"\0" * 3, bytes((40, 50, 60)) * width + b"\0" * 3]   # bottom-up, padded to 4 bytes
    pixels = b"".join(rows)
    header = b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54) + struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
    path = tmp_path / "shot.bmp"
    path.write_bytes(header + pixels)
    image = sf.read_bmp(path)
    rgba = image.rgba()
    assert (image.width, image.height) == (3, 2)
    assert rgba[:4] == bytes((60, 50, 40, 255))      # the top row is the last in the file; B, G, R become R, G, B
    assert rgba[-4:] == bytes((30, 20, 10, 255))


def test_memory_masks_cover_only_their_words_and_pointers_must_agree_on_null(tmp_path):
    base = 0x00524000
    size = 0x006B0000 - base
    left = bytearray(size)
    right = bytearray(size)
    struct.pack_into("<I", left, 0x00568638 - base, 0x29F)          # assertion line: masked
    struct.pack_into("<I", left, 0x006A04C0 - base, 0x03CD7C40)       # a heap pointer on both sides: masked
    struct.pack_into("<I", right, 0x006A04C0 - base, 0x0AAB2CE8)
    struct.pack_into("<I", left, 0x0056E31C - base, 100)              # the frame clock: compared
    struct.pack_into("<I", right, 0x0056E31C - base, 101)
    a, b = tmp_path / "a.img", tmp_path / "b.img"
    a.write_bytes(bytes(left))
    b.write_bytes(bytes(right))
    report = sf.compare_memory(a, b)
    assert report["differing_words"] == 1 and report["first"][0]["address"] == "0056e31c"
    struct.pack_into("<I", right, 0x0056E31C - base, 100)
    struct.pack_into("<I", right, 0x006A04C0 - base, 0)               # null on one side only: a difference
    b.write_bytes(bytes(right))
    report = sf.compare_memory(a, b)
    assert report["differing_words"] == 1 and report["first"][0]["address"] == "006a04c0"


WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"
needs_wine = pytest.mark.skipif(
    not WINE or not (ROOT / "build/hooks/kits/opentony_spike_video.dll").is_file()
    or not (ROOT / "build/sessions/runs" / sf.IMAGE_LABEL / f"session_{sf.IMAGE_DELIVERY}.img").is_file(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build`, `tony hooks stage` and the main-menu image (runs retail sessions under Wine)",
)


@needs_wine
def test_the_options_screen_draws_what_retail_draws():
    assert sf.main([]) == 0
    report = __import__("json").loads((ROOT / sf.OUT / "report.json").read_text())
    assert report["stream"]["retail_records"] == 114 and report["stream"]["differ"] == 0


@needs_wine
def test_the_memory_after_sixty_ticks_is_retails():
    assert sf.main(["--memory", "60"]) == 0


@needs_wine
def test_scripted_input_drives_the_game_and_the_picture_is_still_retails():
    """Milestone 8a: the window, input and (dummy) audio services of the program. A scripted log holds Down for four polls (the game's own bindings make 0x4000 of it and 0
    when it is released), puts the pointer at rest, and the frame is the retail Options frame; a log that moves the pointer changes the cursor's draw and nothing else."""

    assert sf.main(["--input", "--keep-retail"]) == 0
    report = __import__("json").loads((ROOT / sf.OUT / "input.json").read_text())
    assert report["action_masks"][:2] == ["4000 0000", "0000 0000"]
    assert report["scripted_stream"]["differ"] == 0 and report["pointer_only_moved_the_cursor"]
