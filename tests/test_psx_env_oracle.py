"""The function-level oracle of the PSX region parser (7b continued, part 4): tony/psx_env_oracle.py, src/runtime/psx_env_kit.*."""

from __future__ import annotations

import os
import random

import pytest

from tony import psx_env_oracle as oracle
from tony import texture_oracle as base

WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"

needs_wine = pytest.mark.skipif(
    not WINE or not base.SPIKE.is_file() or not base.KITS.is_file() or not base.EXE.is_file() or not base.DUMP.is_file() or not base.DATA.is_dir(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` and `scripts/standalone_spike.py run` done (runs the retail game and the spike under Wine)",
)


def test_the_script_is_one_deterministic_text_that_calls_the_parser():
    text = oracle.build_script()
    assert text == oracle.build_script()
    called = [line.split()[1] for line in text.splitlines() if line.startswith("call ") and line.split()[1] == "0x4b2450"]
    assert len(called) == 4 + 30 + 9 + 24  # the four named scenarios, the random ones, the nine named ones of 8d and its 24 random ones
    assert "call node=0x48a400 c" in text  # the palettes are looked up in the source lookup afterwards
    assert text.splitlines()[-1] == "set32 0x29da3a0 0"
    # 8d: what the first version left out is in the script: texture records, the palette cheat, the zone blockmap tag and the named-resource tag
    assert "scenario t1:" in text and "scenario z1:" in text and "scenario n1:" in text
    assert "cheat 0xd" in text and "tags ['0xa']" in text and "tags ['0x45']" in text
    assert "maskval t1_texture0_material_page" in text and "same z1_zone_item_" in text and "call n1_hit0=0x4b3680 c" in text


def test_a_region_is_laid_out_the_way_the_parser_reads_it():
    rng = random.Random(1)
    region = oracle.build_region(rng, objects=2, models=2, checksums=[11, 22, 33], small=[44], wide=[55], tags=[oracle.TAG_PULSE, oracle.TAG_WIB])
    data = region.data
    word = lambda at: int.from_bytes(data[at:at + 4], "little")
    assert word(4) == region.tags_at and word(8) == 2
    assert word(region.models_at) == 2 and word(region.models_at + 4) == region.models[0][0]
    assert word(region.tags_at) == oracle.TAG_PULSE and word(region.tags_at + 4) == len(data[region.tag_bodies[oracle.TAG_PULSE]:region.tag_bodies[oracle.TAG_WIB] - 8])
    # the stream ends with -1 and the trailing tables follow: a name checksum per model, the material checksums, then the palette groups
    assert word(region.trailing - 4) == 0xFFFFFFFF
    assert word(region.trailing + 4 * 2) == 3 and word(region.checks_at) == 11
    assert word(region.palette_at) == 1 and word(region.small_data[0] - 4) == 44
    assert region.wide_data[0] - 4 > region.small_data[0] + 32 and word(region.wide_data[0] - 4) == 55
    # every face with a material names one of the checksums
    for face in region.faces:
        if face.has_material:
            assert word(face.offset + 0x10) < 3


def test_a_region_can_carry_texture_records_a_zone_blockmap_and_named_resources():
    rng = random.Random(2)
    region = oracle.build_region(rng, objects=2, models=1, checksums=[11, 22, 33], small=[44], wide=[55], tags=[oracle.TAG_ZONE], records=3)
    data = region.data
    word = lambda at: int.from_bytes(data[at:at + 4], "little")
    # the texture records follow the wide palettes: a count, the offsets, then flags, colours, the palette's checksum, the material's index, size, pixels
    assert len(region.textures) == 3
    for texture in region.textures:
        assert word(texture.start + 4) in (0x10, 0x100) and word(texture.start + 8) in (44, 55) and word(texture.start + 0xC) < 3
    seen: set[int] = set()
    for texture in region.textures:
        assert texture.flags == 0 or texture.material not in seen      # a second record for a material never asks to own it
        seen.add(texture.material)
    # the zone blockmap: five header dwords (the cell counts last), then per cell 8 bytes, an item count, the items and one more dword
    columns, rows = region.zone_counts
    assert (word(region.tag_bodies[oracle.TAG_ZONE] + 16) & 0xFFFF, word(region.tag_bodies[oracle.TAG_ZONE] + 16) >> 16) == (columns, rows)
    assert len(region.zone_cells) == columns * rows
    for column, row, offset, items in region.zone_cells:
        assert word(offset - 4) == len(items)
        for item, index in items:
            assert word(item) == index < 2
    named = oracle.build_region(random.Random(3), objects=0, models=0, checksums=[11, 22], small=[], wide=[], tags=[oracle.TAG_NAMED])
    assert named.named
    entry, name, frames, listed = named.named[0]
    assert int.from_bytes(named.data[entry:entry + 8], "little") == int.from_bytes(name, "little") and len(listed) == frames
    assert int.from_bytes(named.data[4:8], "little") == named.tags_at and int.from_bytes(named.data[8:12], "little") == 0


@needs_wine
def test_the_psx_env_kit_does_what_the_retail_parser_does():
    # The script (about 5,000 lines: random regions with objects, models, faces, material checksums, both palette groups, the tag stream, texture records, the palette cheat,
    # the zone blockmap tag and the named-resource tag, a material whose use count is already at its largest, a non-zero palette base) runs in the retail process, with hooks off, where PSX_ParseEnvironment and everything under it is retail code,
    # and in the program with no retail image, where it is PsxEnvKit over the other Kits. Heap pointers are checked with `same`; every other word is compared.
    oracle.OUT_ROOT.mkdir(parents=True, exist_ok=True)
    script = oracle.OUT_ROOT / "psx_env.oracle"
    script.write_text(oracle.build_script())
    standalone = base.run_standalone(script, oracle.OUT_ROOT / "standalone.out")
    retail = base.run_retail(script, label="psx-env-oracle")
    assert standalone.splitlines()[-1] == "end", standalone[-300:]
    assert retail.splitlines()[-1] == "end", retail[-300:]
    assert len(retail.splitlines()) > 3000
    problems = base.compare(retail, standalone)
    assert not problems, "\n".join(problems[:8])
