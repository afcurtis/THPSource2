"""The function-level oracle of the draw path (8d): tony/draw_oracle.py, src/runtime/draw_kit.*."""

from __future__ import annotations

import os

import pytest

from tony import draw_oracle as oracle
from tony import texture_oracle as base

WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"

needs_wine = pytest.mark.skipif(
    not WINE or not base.SPIKE.is_file() or not base.KITS.is_file() or not base.EXE.is_file() or not base.DUMP.is_file() or not base.DATA.is_dir(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` and `scripts/standalone_spike.py run` done (runs the retail game and the spike under Wine)",
)


def test_the_script_is_one_deterministic_text_that_draws_every_kind_of_packet():
    text = oracle.build_script()
    assert text == oracle.build_script()
    lines = text.splitlines()
    walks = [line for line in lines if line.startswith("call ") and line.split()[1] == "0x4d3160"]
    assert len(walks) > 90        # D3D_DrawPolygonList: one case per kind and variant, the general polygons, the state packets, a list of five
    # every dispatch-table handler is also called alone, with the walk's state words set and its single precision selected around it
    handlers = {line.split()[1] for line in lines if line.startswith("call [")}
    assert len(handlers) == len(oracle.HANDLERS)
    single = sum(1 for line in lines if line == f"call 0x5015a6 c {0x20000} {0x30000}")
    assert single == sum(1 for line in lines if line == f"call 0x5015a6 c {0x9001F} {0xFFFFF}") > 30
    assert "call 0x4d3b40 c 8421504 1056816" in lines       # D3DPoly_StartScene(fog colour 0x808080, clear colour 0x102030)
    assert lines[-1].startswith("mem after_")


def test_the_layouts_name_the_offsets_the_kit_reads():
    count, z_at, texture_at, xy, rgb, uv = oracle.LAYOUTS[0x3C]       # GT4: four vertices, a float z, a texture record after it
    assert (count, z_at, texture_at) == (4, 0x34, 0x38) and len(xy) == 4 and len(rgb) == 4 and len(uv) == 4
    assert oracle.LAYOUTS[0x20][2] == 0 and oracle.LAYOUTS[0x48][0] == 3     # F3 is untextured, LINE_F3 has three points (the draw adds the first again)


@needs_wine
def test_the_draw_kit_writes_the_words_the_retail_draw_path_writes():
    # D3DPoly_StartScene, Db_ClearOrderingTable, D3D_DrawPolygonList on one-packet lists (11 PSX polygon kinds, lines, four tile commands, general polygons, the texture-page
    # and viewport packets, unknown commands), each D3D_Draw* handler called alone, D3D_PresentFrame: the scene words, the device-state caches, the polygon counters, the
    # texture bindings and both D3DTLVERTEX staging buffers are compared word for word with the retail functions' (the pictures are the stream oracle's).
    oracle.OUT_ROOT.mkdir(parents=True, exist_ok=True)
    script = oracle.OUT_ROOT / "draw.oracle"
    script.write_text(oracle.build_script())
    standalone = base.run_standalone(script, oracle.OUT_ROOT / "standalone.out")
    retail = base.run_retail(script, label="draw-oracle")
    assert standalone.splitlines()[-1] == "end", standalone[-300:]
    assert retail.splitlines()[-1] == "end", retail[-300:]
    assert len(retail.splitlines()) > 1500
    problems = base.compare(retail, standalone)
    assert not problems, "\n".join(problems[:8])
