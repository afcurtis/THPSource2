"""The function-level oracle of the small natives milestone 7c added (8d): tony/native_oracle.py, src/hooks/oracle_script.*."""

from __future__ import annotations

import os

import pytest

from tony import native_oracle as oracle
from tony import texture_oracle as base

WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"

needs_wine = pytest.mark.skipif(
    not WINE or not base.SPIKE.is_file() or not base.KITS.is_file() or not base.EXE.is_file() or not base.DUMP.is_file() or not base.DATA.is_dir(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` and `scripts/standalone_spike.py run` done (runs the retail game and the spike under Wine)",
)


def test_the_script_is_one_deterministic_text_that_calls_each_native():
    text = oracle.build_script()
    assert text == oracle.build_script()
    called = {line.split()[1].split("=")[-1] for line in text.splitlines() if line.startswith("call ")}
    # PRE_Unload, FileIO_Exists, Animation_Cycle, NetMouse_HitRect, and the loaders' allocator that makes the buffers PRE_Unload frees
    assert {"0x4a9330", "0x449460", "0x480890", "0x4dbf90", "0x46f490"} <= called
    # the timer callback is delivered by the backend (the retail process owns the function through the lockstep clock)
    assert text.splitlines().count("tick") == 6


def test_the_double_of_a_timer_case_is_eight_little_endian_bytes():
    assert oracle.double(1.0) == "000000000000f03f"


@needs_wine
def test_the_natives_do_what_the_retail_functions_do():
    # PRE_Unload (which slot is blanked, which buffer freed), FileIO_Exists (base directory, sub directory, overlay, package), Animation_Cycle (the fields it writes),
    # NetMouse_HitRect (the edges, signed) and one delivery of the simulation timer callback (x87 arithmetic on the clock, paused and not) run in the retail process with hooks off and in the program with no retail image.
    oracle.OUT_ROOT.mkdir(parents=True, exist_ok=True)
    script = oracle.OUT_ROOT / "natives.oracle"
    script.write_text(oracle.build_script())
    mods = base.write_mod()
    standalone = base.run_standalone(script, oracle.OUT_ROOT / "standalone.out", mods=mods)
    retail = base.run_retail(script, label="native-oracle", mods=mods, deliveries_after=40)
    assert standalone.splitlines()[-1] == "end", standalone[-300:]
    assert retail.splitlines()[-1] == "end", retail[-300:]
    assert len(retail.splitlines()) > 150
    problems = base.compare(retail, standalone)
    assert not problems, "\n".join(problems[:8])
