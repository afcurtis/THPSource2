"""The function-level oracle of the texture and sprite layer (7b continued, part 3): tony/texture_oracle.py, src/hooks/oracle_script.*."""

from __future__ import annotations

import os

import pytest

from tony import texture_oracle as oracle

WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"

needs_wine = pytest.mark.skipif(
    not WINE or not oracle.SPIKE.is_file() or not oracle.KITS.is_file() or not oracle.EXE.is_file() or not oracle.DUMP.is_file() or not oracle.DATA.is_dir(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` and `scripts/standalone_spike.py run` done (runs the retail game and the spike under Wine)",
)


def test_the_script_is_one_deterministic_text_that_names_every_function_of_the_layer():
    text = oracle.build_script()
    assert text == oracle.build_script()
    called = {line.split()[1].split("=")[-1] for line in text.splitlines() if line.startswith("call ")}
    # LoadIndexedBitmap, BuildTiles, GrayTexture, DestroyRecord, D3DSprite_Create, D3DSprite_Destroy and the loaders' allocator
    assert {"0x4d9950", "0x4d83a0", "0x4d9f50", "0x4d8b50", "0x4d74a0", "0x4d78c0", "0x46f490"} <= called
    # 8d: UploadTexture with `replace`, and LoadIndexedBitmap given a name (a file both processes find through the loose-file overlay)
    assert "0x4d8f10" in called and any("_name" in line for line in text.splitlines() if line.startswith("call n24_record="))
    assert text.splitlines()[-1] == "set32 0x29da3a0 0"  # the software renderer is switched off again


def test_the_named_files_are_bitmaps_the_overlay_can_serve():
    files = oracle.named_files()
    assert set(files) == {"o8.bmp", "o24.bmp", "o24w.bmp"}
    assert int.from_bytes(files["o24.bmp"][28:30], "little") == 24 and int.from_bytes(files["o8.bmp"][28:30], "little") == 8
    # a 24-bit row is B G R, padded to four bytes; the key colour (255, 0, 255) is in the picture
    wide = oracle.bitmap24(2, 1, [[(1, 2, 3), (255, 0, 255)]])
    assert wide[54:] == bytes([3, 2, 1, 255, 0, 255, 0, 0])
    assert (0xFF, 0x00, 0xFF) in [pixel for row in oracle.picture24(5, 3) for pixel in row]


def test_a_bitmap_is_a_bottom_up_file_image_with_padded_rows():
    image = oracle.bitmap8(3, 2, [(1, 2, 3), (4, 5, 6)], [[0, 1, 0], [1, 0, 1]])
    assert image[:2] == b"BM"
    assert int.from_bytes(image[2:6], "little") == len(image)
    assert int.from_bytes(image[10:14], "little") == 14 + 40 + 8  # the pixels follow the two palette entries
    assert int.from_bytes(image[18:22], "little") == 3 and int.from_bytes(image[22:26], "little") == 2
    assert image[14 + 40 + 8:] == bytes([1, 0, 1, 0, 0, 1, 0, 0])  # the second row first, each padded to four bytes


def test_the_comparison_names_the_first_difference_of_each_line_that_differs():
    same = "rec c1 P 0 P\nsurf c1_surface 0004x0002x02 0011\nend\n"
    assert oracle.compare(same, same) == []
    other = "rec c1 P 0 P\nsurf c1_surface 0004x0002x02 0012\nend\n"
    problems = oracle.compare(same, other)
    assert len(problems) == 1 and problems[0].startswith("surf c1_surface:")
    assert oracle.compare(same, same + "extra\n")[0].startswith("extra")  # a line that only one side has


@needs_wine
def test_the_texture_kit_does_what_the_retail_functions_do():
    # The script (about 600 lines: 8-bit bitmaps into new and existing records, the named 8-bit and 24-bit files of the loose-file overlay, UploadTexture with `replace`, the
    # device's power-of-two / square / limit rules, BuildTiles, GrayTexture, sprites with one tile, two tiles and as the background, DestroyRecord's list handling, a 5-6-5
    # device, the software renderer) runs in the retail process, with hooks off (only the overlay on), on the game's own DirectDraw surfaces, and in the program with no
    # retail image, on the surface store. Heap pointers print as P; every other word and every surface pixel is compared.
    oracle.OUT_ROOT.mkdir(parents=True, exist_ok=True)
    script = oracle.OUT_ROOT / "texture.oracle"
    script.write_text(oracle.build_script())
    mods = oracle.write_mod()
    standalone = oracle.run_standalone(script, oracle.OUT_ROOT / "standalone.out", mods=mods)
    retail = oracle.run_retail(script, mods=mods)
    assert standalone.splitlines()[-1] == "end", standalone[-300:]
    assert retail.splitlines()[-1] == "end", retail[-300:]
    assert len(retail.splitlines()) > 150
    problems = oracle.compare(retail, standalone)
    assert not problems, "\n".join(problems[:8])
