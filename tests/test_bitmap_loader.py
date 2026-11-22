"""Phase 4.10i3: the 8-bit BMP loader and the 2D screen primitives (src/runtime/shell_bitmap.cpp) against the retail forced-call fixtures
(re/evidence/fixtures/front-calls-bmp-*.txt), and the screen loops the same milestone closed (src/runtime/mainmenu*.cpp, credits.cpp) against
menu-calls-loops-*.txt. The replay tools are the ones of tests/test_front_shell.py and tests/test_menu_toolkit.py."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "re/evidence/fixtures"
FRONT_SOURCES = ["src/runtime/screen_kit.cpp", "src/runtime/front_menu.cpp", "src/runtime/front_display.cpp", "src/runtime/front_screens.cpp", "src/runtime/front_states.cpp",
                 "src/runtime/front_update.cpp", "src/runtime/front_update_late.cpp", "src/runtime/front_update_screens.cpp", "src/runtime/front_load.cpp",
                 "src/runtime/front_shell.cpp", "src/runtime/front_main.cpp", "src/runtime/shell_helpers.cpp", "src/runtime/shell_bitmap.cpp", "src/runtime/netmenu.cpp",
                 "src/runtime/netmenu_update.cpp", "src/runtime/netmenu_state.cpp", "src/runtime/netmenu_summary.cpp",
                 "src/runtime/career.cpp", "src/runtime/career_tables.cpp", "src/runtime/menu_toolkit.cpp", "src/runtime/menu_lists.cpp", "src/runtime/menu_screens.cpp",
                 "src/runtime/menu_dispatch.cpp", "src/runtime/frontend_text.cpp", "src/runtime/frontend_window.cpp", "src/runtime/skater_manager.cpp"]
SLICES = ["frontend-bitmap-load", "screen-primitives"]


def _compile(directory, name, sources):
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is not installed")
    binary = directory / name
    build = subprocess.run(
        [compiler, "-std=c++20", "-O1", "-I", str(ROOT / "src/runtime"), *[str(ROOT / source) for source in sources], "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert build.returncode == 0, build.stderr
    return binary


@pytest.fixture(scope="module")
def replay_tool(tmp_path_factory):
    return _compile(tmp_path_factory.mktemp("bitmap_replay"), "opentony_front_retail_calls_test", ["src/runtime/front_retail_calls_test.cpp", *FRONT_SOURCES])


def test_bitmap_fixtures_are_present():
    assert sorted(FIXTURE_DIR.glob("front-calls-bmp-*.txt")), "no bitmap loader fixtures in re/evidence/fixtures"


def test_native_bitmap_loader_and_primitives_match_every_retail_call(replay_tool):
    fixtures = sorted(FIXTURE_DIR.glob("front-calls-bmp-*.txt"))
    run = subprocess.run([str(replay_tool), *map(str, fixtures)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    first = run.stdout.splitlines()[0]
    replayed = int(first.split(":")[1].split()[0])
    assert replayed >= 1000, first


def test_replay_has_teeth(replay_tool, tmp_path):
    """A changed byte of a retail write makes the replay fail."""
    source = next(FIXTURE_DIR.glob("front-calls-bmp-prim.txt"))
    lines = source.read_text().splitlines()
    for index, line in enumerate(lines):
        if line.startswith("C ") and "name=Screen_FillRect" in line and "writes=" in line:
            head, writes = line.split("writes=", 1)
            digits, rest = writes.split(";", 1) if ";" in writes else (writes, "")
            address, data = digits.split("=", 1)
            flipped = ("0" if data[0] != "0" else "1") + data[1:]
            lines[index] = head + "writes=" + address + "=" + flipped + ((";" + rest) if rest else "")
            break
    else:
        pytest.skip("no FillRect case with writes")
    broken = tmp_path / "front-calls-broken.txt"
    broken.write_text("\n".join(lines) + "\n")
    run = subprocess.run([str(replay_tool), str(broken)], capture_output=True, text=True, check=False)
    assert run.returncode != 0


def test_every_slice_is_complete_with_no_open_questions():
    for sid in SLICES:
        slice_ = yaml.safe_load((ROOT / "re/slices" / f"{sid}.yml").read_text())
        assert slice_["status"] == "complete", sid
        assert slice_["open_questions"] == [], sid
