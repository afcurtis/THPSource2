"""Phase 5.4a, continued: every native has an entry point by retail address (re/config/standalone_natives.yml, kNatives)."""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

from tony import kit_hooks, static_data
from tony.common import load_yaml, wine_env

ROOT = Path(__file__).resolve().parents[1]
SPIKE = ROOT / "build/hooks/kits/opentony_spike.exe"
KITS = ROOT / "build/hooks/kits/opentony_standalone.dll"
EXE = ROOT / "build/disc/files/SETUP/data/THawk2.exe"
DUMP = ROOT / "build/sessions/runs/spike-boot-career-hangar/session_5000.img"
DATA = ROOT / "build/assets/all-pkr/files"  # the extracted ALL.PKR: the standalone's file backend reads loose files below it (OPENTONY_DATA_DIR)
WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"

# The natives with no adapter, by the source file their native status names. They are not on the retail memory model (a value-owned
# C++ type, or a typed record) so a Kit cannot call them with retail addresses yet: 5.5's work, one group at a time. Shrink this list
# when a group gets its Kit; it must never grow.
TYPED_MODEL_GROUPS = {
    "re/evidence/collision_reference.hpp",
    "src/assets/bmp_asset.cpp",
    "src/assets/fnt_asset.hpp",
    "src/assets/pc_texture_runtime.cpp",
    "src/assets/pkr_asset.cpp",
    "src/assets/pkr_asset.hpp",
    "src/assets/pre_asset.hpp",
    "src/assets/pre_runtime.hpp",
    "src/assets/psh_asset.cpp",
    "src/assets/psx_runtime.cpp",
    "src/assets/resource_runtime.hpp",
    "src/assets/sound_runtime.cpp",
    "src/assets/wav_asset.cpp",
    "src/camera/camera_system.hpp",
    "src/camera/camera_timing.hpp",
    "src/camera/camera_update.hpp",
    "src/collision/psx_scene.hpp",
    "src/physics_state_machine.cpp",
    "src/runtime/action_sequence.cpp",
    "src/runtime/air_contact.cpp",
    "src/runtime/animation_cursor.hpp",
    "src/runtime/career.cpp",
    "src/runtime/career_save.cpp",
    "src/runtime/collision_recovery.hpp",
    "src/runtime/collision_response.cpp",
    "src/runtime/create_color.cpp",
    "src/runtime/create_screens.hpp",
    "src/runtime/equip_select.hpp",
    "src/runtime/front.hpp",
    "src/runtime/ground_animation.hpp",
    "src/runtime/ground_motion.cpp",
    "src/runtime/ground_physics.cpp",
    "src/runtime/ground_surface_response.hpp",
    "src/runtime/ground_turn.cpp",
    "src/runtime/handicap.hpp",
    "src/runtime/hud_panel.cpp",
    "src/runtime/options.hpp",
    "src/runtime/physics_dispatch.cpp",
    "src/runtime/physics_frame.cpp",
    "src/runtime/player_select.hpp",
    "src/runtime/player_state.cpp",
    "src/runtime/position_commit.cpp",
    "src/runtime/retail_memory.hpp",
    "src/runtime/retail_vector3.hpp",
    "src/runtime/shell_helpers.hpp",
    "src/runtime/skater_window_helpers.hpp",
    "src/runtime/trick_database.cpp",
    "src/runtime/trick_recognition.cpp",
    "src/runtime/trick_script.cpp",
    "src/runtime/trick_tips.cpp",
    "src/trg/camera_point_registry.cpp",
    "src/trg/gap_table.cpp",
    "src/trg/level_trigger_state.cpp",
    "src/trg/level_trigger_state.hpp",
    "src/trg/object_script.hpp",
    "src/trg/platform_object.cpp",
    "src/trg/powerup_object.cpp",
    "src/trg/render_command_dispatch.hpp",
    "src/trg/render_model_submit.hpp",
    "src/trg/retail_random.hpp",
    "src/trg/traffic_object.cpp",
    "src/trg/traffic_runtime.cpp",
    "src/trg/trg_runtime.cpp",
    "src/trg/trg_runtime.hpp",
    "src/trg/trigger_factory_runtime.cpp",
    "src/trg/world_objects.hpp",
}


def _adapters() -> set[int]:
    manifest = load_yaml("re/config/native_hooks.yml")
    rows = {int(hook["builds"]["canonical"]) for hook in manifest["hooks"] if kit_hooks.is_kit_hook(hook)}
    return rows | {int(row["address"]) for row in kit_hooks.load_natives()}


def test_the_address_table_lists_every_hook_and_every_row_once_and_is_sorted():
    source = (ROOT / kit_hooks.STANDALONE_SOURCE_PATH).read_text()
    # the hooked build's DLL must not carry the table or the rows: linking every native into the retail process changed what the session
    # scenarios compare (5.4a), so opentony_native.dll's source stays the hook adapters only
    hooked = (ROOT / kit_hooks.KIT_SOURCE_PATH).read_text()
    assert "kNatives" not in hooked and "ot_native_" not in hooked
    table = source[source.index("const NativeEntry kNatives[]"):]
    addresses = [int(found, 16) for found in re.findall(r"^    \{0x([0-9a-f]{8})u, ", table, re.MULTILINE)]
    assert addresses == sorted(set(addresses)), "kNatives must be sorted by address with no duplicate (the resolver bisects it)"
    assert set(addresses) == _adapters()
    assert f"const std::size_t kNativeCount = {len(addresses)};" in source


def test_a_native_row_is_not_also_a_hook_row_and_names_a_real_kit():
    manifest = load_yaml("re/config/native_hooks.yml")
    hooked = {int(hook["builds"]["canonical"]) for hook in manifest["hooks"] if kit_hooks.is_kit_hook(hook)}
    rows = kit_hooks.load_natives()
    assert len(rows) > 1000
    assert not hooked & {int(row["address"]) for row in rows}
    for row in rows:
        assert row["kit"] in manifest["kits"] or row["kit"] == kit_hooks.NO_KIT
        assert ("call" in row) != ("expr" in row)


def test_the_natives_without_an_adapter_are_only_the_typed_model_groups():
    natives = load_yaml("re/native/functions.yml")["functions"]
    adapters = _adapters()
    # the initializer bodies of milestone 9 (and their jump slots) need none: the cold boot runs them from the table, not by address
    boot_only = "src/hooks/kits/standalone_static_init_generated.cpp"
    missing = [item for item in natives if int(item["address"]) not in adapters and boot_only not in (item.get("sources") or [])]
    assert 0 < len(missing) <= 230  # 230 on 2026-10-29: 3,208 natives, 2,978 with an adapter (7b added the 20 resource-layer rows); this number only goes down
    stray = sorted({item["sources"][0] for item in missing if (item.get("sources") or ["?"])[0] not in TYPED_MODEL_GROUPS})
    assert not stray, f"natives without an adapter in a group not listed as typed-model work: {stray}"


@pytest.mark.skipif(not EXE.is_file(), reason="needs the extracted retail executable")
def test_the_standalone_rows_file_is_what_the_converter_writes():
    result = subprocess.run([sys.executable, "scripts/standalone_rows.py", "--check"], cwd=ROOT, capture_output=True, text=True, timeout=600, check=False)
    assert result.returncode == 0, result.stderr[-800:]


needs_wine = pytest.mark.skipif(
    not WINE or not SPIKE.is_file() or not KITS.is_file() or not EXE.is_file() or not DUMP.is_file() or not DATA.is_dir(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` and `scripts/standalone_spike.py run` done (runs under Wine)",
)


def _spike(calls: str, *, lenient: bool, frames: int = 30) -> subprocess.CompletedProcess:
    runs = DUMP.parent
    env = wine_env(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    env.update({"WINEDEBUG": "-all", "OT_SPIKE_CALL": calls, "OT_SPIKE_FRAMES": str(frames), "OT_FILES_LOG": "1", "OPENTONY_DATA_DIR": str(DATA)})
    if lenient:
        env["OT_RESOLVER_LENIENT"] = "1"
    return subprocess.run(
        ["wine", str(SPIKE), str(static_data.ensure_image(EXE)), str(KITS), str(DUMP), str(ROOT / "build/standalone-spike/expected.tsv"), str(runs / "session_5000.heap")],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=300, check=False,
    )


# The front end creates the PRE manager before any screen runs (front_shell.cpp: PRE_CreateManager, 0x004a8ee0), and the career-hangar image is past that.
OPTIONS = "0x004a8ee0,0x00483ef0"


@needs_wine
def test_a_screen_runs_its_frames_natively():
    # Options_Run (0x00483ef0) is a native on the screens Kit. Its resource layer runs on retail addresses (7b): PRE_Load opens data/options.pre through the
    # file backend, the fonts' textures and the screen's bitmaps go through the texture and sprite layer (TextureKit on the surface store), the window-graph virtual
    # calls resolve by vtable slot, the screen's three 3D models are parsed by the PSX region parser (PsxEnvKit, 7b part 4), and since 7c the draw path
    # (src/runtime/draw_kit.*), the multimedia timer's ticks and the menu classes' pick-by-mouse methods are natives too. Strict, the screen runs its frames to
    # the frame limit: nothing it reaches is retail code or an unimplemented seam.
    result = _spike(OPTIONS, lenient=False)
    assert result.returncode == 0, result.stdout[-600:]
    assert "frame limit: 30 frames of Game_FrameTick" in result.stdout
    assert "FATAL" not in result.stdout
    assert "files: open " in result.stderr and "/data/options.pre -> ok" in result.stderr


@needs_wine
def test_a_screen_needs_nothing_the_resolver_cannot_answer():
    result = _spike(OPTIONS, lenient=True)
    missing = sorted(set(re.findall(r"unresolved: service '([^']+)'", result.stdout)))
    assert missing == [], missing
    # the resource layer, the file layer and the bitmap loader work: the screen's own bitmaps are read from the extracted package
    opened = re.findall(r"files: open \S*/data/(\S+) -> ok", result.stderr)
    assert opened[:3] == ["options.pre", "s2bgd4.bmp", "s2video.bmp"], opened
