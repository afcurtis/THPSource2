"""Phase 4.10i2: the shell (src/runtime/front_shell.cpp, front_main.cpp), its small classes (shell_helpers.cpp) and the network lobby (netmenu*.cpp) against the retail
forced-call fixtures. The fixtures are the ones of tests/test_front_screens.py (one replay tool serves the whole front end)."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = sorted((ROOT / "re/evidence/fixtures").glob("front-calls-*.txt"))
SOURCES = ["src/runtime/screen_kit.cpp", "src/runtime/front_menu.cpp", "src/runtime/front_display.cpp", "src/runtime/front_screens.cpp", "src/runtime/front_states.cpp",
           "src/runtime/front_update.cpp", "src/runtime/front_update_late.cpp", "src/runtime/front_update_screens.cpp", "src/runtime/front_load.cpp",
           "src/runtime/front_shell.cpp", "src/runtime/front_main.cpp", "src/runtime/shell_helpers.cpp", "src/runtime/shell_bitmap.cpp", "src/runtime/netmenu.cpp", "src/runtime/netmenu_update.cpp",
           "src/runtime/netmenu_state.cpp", "src/runtime/netmenu_summary.cpp",
           "src/runtime/career.cpp", "src/runtime/career_tables.cpp", "src/runtime/menu_toolkit.cpp", "src/runtime/menu_lists.cpp", "src/runtime/menu_screens.cpp",
           "src/runtime/menu_dispatch.cpp", "src/runtime/frontend_text.cpp", "src/runtime/frontend_window.cpp", "src/runtime/skater_manager.cpp"]
SLICES = ["front-shell-helpers", "front-shell-loop", "front-preload-screen", "shell-small-classes", "text-window", "netmenu-lobby", "netmenu-windows"]
SHELL_FIXTURE_PREFIXES = ("front-calls-shell", "front-calls-net")


@pytest.fixture(scope="module")
def replay_tool(tmp_path_factory):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = tmp_path_factory.mktemp("shell_replay") / "opentony_front_retail_calls_test"
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(ROOT / "src/runtime"),
         *[str(ROOT / source) for source in ["src/runtime/front_retail_calls_test.cpp", *SOURCES]], "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert build.returncode == 0, build.stderr
    return binary


def test_shell_fixtures_are_present():
    assert [f for f in FIXTURES if f.name.startswith(SHELL_FIXTURE_PREFIXES)], "no shell or lobby fixtures in re/evidence/fixtures"


def test_native_shell_and_lobby_match_every_retail_call(replay_tool):
    fixtures = [f for f in FIXTURES if f.name.startswith(SHELL_FIXTURE_PREFIXES)]
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1000, first


def test_every_shell_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
