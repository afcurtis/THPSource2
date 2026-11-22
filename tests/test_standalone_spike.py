"""Phase 5.4a: natives in a process with no retail image (scripts/standalone_spike.py, src/hooks/kits/standalone_spike.cpp)."""

from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
from pathlib import Path

import pytest

from tony.common import load_yaml, wine_env

ROOT = Path(__file__).resolve().parents[1]
SPIKE = ROOT / "build/hooks/kits/opentony_spike.exe"
KITS = ROOT / "build/hooks/kits/opentony_standalone.dll"
EXE = ROOT / "build/disc/files/SETUP/data/THawk2.exe"
WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"


def _driver():
    spec = importlib.util.spec_from_file_location("standalone_spike", ROOT / "scripts/standalone_spike.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_the_getters_are_argument_less_kit_hooks_that_reach_no_service():
    wanted = _driver().getters()
    assert len(wanted) >= 25
    names = {name for name, _ in wanted}
    assert {"Career_CountMoney", "Redbook_TrackNumber", "Mess_GetScale"} <= names
    rows = {row["name"]: row for row in load_yaml("re/config/native_hooks.yml")["hooks"]}
    for name, address in wanted:
        row = rows[name]
        assert row["kit"] and row["reaches"] == [] and row["stack_args"] == 0 and row["builds"]["canonical"] == address


def test_the_spike_is_built_by_the_kit_build_at_the_retail_base():
    cmake = (ROOT / "src/hooks/kits/CMakeLists.txt").read_text()
    assert "add_executable(opentony_spike standalone_spike.cpp ../oracle_script.cpp)" in cmake  # (the function-level oracle's interpreter is linked in: 7b, part 3)
    assert "--image-base=0x00400000" in cmake  # the loader reserves the retail range: a VirtualAlloc there fails under Wine


def _wine(*arguments: str) -> subprocess.CompletedProcess:
    env = wine_env(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    env["WINEDEBUG"] = "-all"
    return subprocess.run(["wine", str(SPIKE), *arguments], cwd=ROOT, env=env, capture_output=True, text=True, timeout=300, check=False)


needs_wine = pytest.mark.skipif(
    not WINE or not SPIKE.is_file() or not KITS.is_file() or not EXE.is_file(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` done (runs under Wine)",
)


@needs_wine
def test_a_process_without_the_retail_image_answers_like_retail_on_a_career_image():
    result = subprocess.run(
        [sys.executable, str(ROOT / "scripts/standalone_spike.py"), "run", "boot-career-hangar", "5000"],
        cwd=ROOT, capture_output=True, text=True, timeout=900, check=False,
    )
    assert result.returncode == 0, result.stdout[-2000:] + result.stderr[-2000:]
    assert " 0 mismatched, 0 missing" in result.stdout
    report = (ROOT / "build/standalone-spike/spike.txt").read_text()
    assert " 0 refused" in report  # every large region of the retail process was placed at its address
    assert "from the static-data image" in report  # 8b: the driver starts from build/static/thawk2_static.img, not the executable (no code in it)


@needs_wine
def test_an_executable_that_is_not_the_expected_retail_build_is_rejected():
    other = ROOT / "build/disc/files/SETUP/data/register.exe"
    if not other.is_file():
        pytest.skip("no other executable to offer")
    result = _wine(str(other), str(KITS), "-", str(ROOT / "build/standalone-spike/expected.tsv"))
    assert result.returncode == 2
    assert "rejected" in result.stderr
