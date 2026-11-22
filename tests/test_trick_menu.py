"""Phase 4.7b: the native trick-list decision logic (src/runtime/trick_menu.cpp) against hand-built
cases derived from the disassembly (the retail window needs the frontend widget toolkit)."""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["src/runtime/trick_menu.cpp", "src/runtime/trick_database.cpp", "src/runtime/trick_combo_tables.cpp"]


def test_hand_built_menu_semantics(tmp_path):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = tmp_path / "opentony_trick_menu_test"
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(ROOT / "src/runtime"),
         str(ROOT / "src/runtime/trick_menu_test.cpp"), *[str(ROOT / s) for s in SOURCES], "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
