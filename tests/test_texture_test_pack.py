"""scripts/texture_test_pack.py: the nearest-neighbour test pack for OT_TEXTURE_PACK (docs/post_phase_5/textureupscale.md, T2)."""

import importlib.util
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "texture_test_pack.py"
spec = importlib.util.spec_from_file_location("texture_test_pack", SCRIPT)
pack = importlib.util.module_from_spec(spec)
sys.modules["texture_test_pack"] = pack
spec.loader.exec_module(pack)


def _picture(width: int, height: int, seed: int) -> bytes:
    return bytes((x * 7 + y * 13 + c * 31 + seed) & 255 for y in range(height) for x in range(width) for c in range(4))


def _dump(root: Path) -> dict[str, bytes]:
    """Four textures: a 16 x 16 world one (seen in L1 and L2), a 32 x 16 seen only in L2, a 2D one, and an 8 x 8."""
    root.mkdir()
    made = {"a" * 16: (16, 16, "world", 1), "b" * 16: (32, 16, "both", 2), "c" * 16: (16, 16, "2d", 3), "d" * 16: (8, 8, "world", 4)}
    index = ["# key\twidth\theight\tformat\talpha\tuse\twrap\tmod2x\tvolatile\ttag"]
    pixels = {}
    for key, (w, h, use, seed) in made.items():
        pixels[key] = _picture(w, h, seed)
        pack.write_png(root / f"{key}.png", w, h, pixels[key])
        index.append(f"{key}\t{w}\t{h}\targb1555\topaque\t{use}\twrap\tno\t0\tpc:L1")
    (root / "index.tsv").write_text("\n".join(index) + "\n")
    (root / "seen.tsv").write_text("pc:L1\t" + "a" * 16 + "\tdraw\npc:L2\t" + "a" * 16 + "\tdraw\npc:L2\t" + "b" * 16 + "\twalk\npc:L2\t" + "c" * 16 + "\tdraw\n"
                                   "pc:L1\t" + "d" * 16 + "\twalk\n")
    return pixels


def test_png_round_trip_and_filters(tmp_path):
    rgba = _picture(5, 3, 9)
    pack.write_png(tmp_path / "x.png", 5, 3, rgba)
    assert pack.read_png((tmp_path / "x.png").read_bytes()) == (5, 3, rgba)


def test_scale_nearest_repeats_each_texel():
    rgba = bytes([1, 2, 3, 4, 5, 6, 7, 8])  # two texels
    big = pack.scale_nearest(2, 1, rgba, 3)
    assert len(big) == 2 * 3 * 3 * 4
    assert big[:4] == bytes([1, 2, 3, 4]) and big[8:12] == bytes([1, 2, 3, 4]) and big[12:16] == bytes([5, 6, 7, 8])
    assert big[:24] == big[24:48] == big[48:72] or big[:24] == big[24:48]  # the rows repeat


def test_mark_keeps_alpha_and_changes_colour():
    rgba = bytes([200, 200, 200, 255, 200, 200, 200, 0]) * 16
    out = pack.mark(8, 2, rgba, 4, "both")
    assert [out[i] for i in range(3, len(out), 4)] == [rgba[i] for i in range(3, len(rgba), 4)]
    assert out != rgba
    assert pack.mark(8, 2, rgba, 4, "none") == rgba


def test_mark_tint_and_grid_values():
    rgba = bytes([200, 100, 50, 77]) * (16 * 9)
    at = lambda data, x, y: tuple(data[4 * (y * 16 + x):4 * (y * 16 + x) + 4])
    tint = pack.mark(16, 9, rgba, 1, "tint")
    assert at(tint, 3, 3) == (130, (100 * 65 + 255 * 35) // 100, (50 * 65 + 255 * 35) // 100, 77)
    grid = pack.mark(16, 9, rgba, 1, "grid")   # a dark line at every eighth texel: columns 0 and 8, rows 0 and 8
    assert at(grid, 3, 3) == (200, 100, 50, 77)
    assert at(grid, 0, 3) == at(grid, 8, 3) == at(grid, 5, 0) == at(grid, 5, 8) == (66, 33, 16, 77)
    assert at(pack.mark(16, 9, rgba, 1, "both"), 0, 0) == (130 // 3, ((100 * 65 + 255 * 35) // 100) // 3, ((50 * 65 + 255 * 35) // 100) // 3, 77)


def test_jobs_make_the_same_pack(tmp_path):
    _dump(tmp_path / "dump")
    pack.make_pack(tmp_path / "dump", tmp_path / "one", [], 4, "both", jobs=1)
    pack.make_pack(tmp_path / "dump", tmp_path / "two", [], 4, "both", jobs=2)
    assert {p.name: p.read_bytes() for p in (tmp_path / "one").iterdir()} == {p.name: p.read_bytes() for p in (tmp_path / "two").iterdir()}


def test_selection_rules(tmp_path):
    _dump(tmp_path / "dump")
    assert pack.wanted_keys(tmp_path / "dump", []) == ["a" * 16, "b" * 16]  # no 2D-only, nothing under 16 a side
    assert pack.wanted_keys(tmp_path / "dump", ["L1"]) == ["a" * 16]
    assert pack.wanted_keys(tmp_path / "dump", ["L2"]) == ["a" * 16, "b" * 16]
    assert pack.wanted_keys(tmp_path / "dump", ["L9"]) == []


def test_pack_files_have_the_dump_s_shape_times_the_scale(tmp_path):
    _dump(tmp_path / "dump")
    assert pack.make_pack(tmp_path / "dump", tmp_path / "out", [], 4, "both") == 2
    width, height, _ = pack.read_png((tmp_path / "out" / ("b" * 16 + ".png")).read_bytes())
    assert (width, height) == (128, 64)
    assert sorted(p.name for p in (tmp_path / "out").iterdir()) == ["a" * 16 + ".png", "b" * 16 + ".png", "stamp.txt"]
    # unmarked, the pack is exactly the nearest-neighbour scale of the dump
    pack.make_pack(tmp_path / "dump", tmp_path / "plain", [], 2, "none")
    width, height, rgba = pack.read_png((tmp_path / "plain" / ("a" * 16 + ".png")).read_bytes())
    assert rgba == pack.scale_nearest(16, 16, _picture(16, 16, 1), 2)
