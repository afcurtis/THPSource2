"""Milestone 11c (docs/PORT64_AUDIT.md D4, D8): Kits that have moved to typed views keep no raw memory access.

A typed Kit reads and writes game memory through the generated views (`guest_views_generated.hpp`) or `guest::read_scalar` / `guest::write_scalar` for a single global;
it has no `m_.u32(address + 12)` left. Add a Kit's sources to TYPED_KITS in the same commit that converts it. Headers never include the views (they are large and a Kit header is
included by half the runtime); only the `.cpp` files do.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "src/runtime"

TYPED_KITS: dict[str, list[str]] = {
    "math": ["math_gte.cpp", "math_fixed.cpp", "math_psx.cpp"],
    "collision": ["collision_cache.cpp", "collision_query.cpp", "collision_zone.cpp", "collision_response.cpp", "collision_recovery.cpp"],
    "heap": ["heap.cpp"],
    "core": ["core_init.cpp", "core_pkr.cpp", "core_utils.cpp", "core_display.cpp", "core_main.cpp"],
    "m3d": ["m3d_matrix.cpp", "m3d_boxes.cpp", "m3d_transform.cpp", "m3d_collision.cpp"],
    "rail": ["rail_create.cpp", "rail_nodes.cpp"],
    "mmu": ["mmu_manager.cpp", "mmu_setup.cpp", "mmu_screens.cpp"],
    "skater, first half": ["skater_ground_physics.cpp"],
    "equip, racks": ["equip_select_racks.cpp"],
    "equip, window and panels": ["equip_select_window.cpp", "equip_select_panels.cpp"],
    "world": ["world_extra.cpp", "world_object.cpp", "world_baddy.cpp", "world_script.cpp", "world_trig_commands.cpp", "world_trig.cpp", "world_trig_nodes.cpp",
              "world_skater_effects.cpp", "world_skater.cpp", "world_trick.cpp", "world_cmd.cpp", "world_traffic.cpp"],
    "bit": ["bit_core.cpp", "bit_spark.cpp", "bit_misc.cpp", "bit_glow.cpp", "bit_shatter.cpp", "bit_dust.cpp"],
    'camera': ['camera_basic.cpp', 'camera_modes.cpp', 'camera_rigs.cpp', 'camera_smooth.cpp', 'camera_update.cpp', 'camera_effects.cpp', 'camera_math.cpp', 'camera_tuning.cpp'],
    'skater, second half': ['skater_physics_frame.cpp', 'skater_action_step.cpp', 'skater_air_physics.cpp', 'skater_swept.cpp', 'skater_rail_physics.cpp', 'skater_rail.cpp', 'skater_pose.cpp', 'skater_start_grind.cpp', 'skater_damp.cpp', 'skater_brake.cpp', 'skater_motion.cpp', 'skater_acceleration.cpp', 'skater_ollie.cpp', 'skater_heading.cpp', 'skater_transform.cpp', 'skater_floor_recovery.cpp', 'skater_ground_steps.cpp', 'skater_dispatcher.cpp', 'skater_events.cpp', 'skater_trick_input.cpp', 'skater_trick_script.cpp', 'skater_trick.cpp', 'skater_misc.cpp', 'skater_sounds.cpp', 'skater_stats.cpp', 'skater_parts.cpp', 'skater_tricks_load.cpp'],
    'gen': ['gen_objs_poly.cpp', 'gen_objs_out.cpp', 'gen_objs_gap.cpp', 'gen_objs_pool.cpp', 'gen_face_removal.cpp', 'gen_piece_tables.cpp'],
    "auto-typed by self (11c s15)": ['audio_crowd.cpp', 'audio_movie.cpp', 'audio_redbook.cpp', 'audio_sfx.cpp', 'bit_cheat.cpp', 'bit_display.cpp', 'career.cpp', 'career_save.cpp', 'career_select.cpp', 'competition.cpp', 'create_color.cpp', 'create_palette.cpp', 'create_parts.cpp', 'create_records.cpp', 'create_screens_editor.cpp', 'create_screens_save.cpp', 'create_screens_select.cpp', 'create_screens_stat.cpp', 'create_screens_ui.cpp', 'create_spool.cpp', 'create_stitch.cpp', 'create_textures.cpp', 'createobj_def.cpp', 'createobj_misc.cpp', 'createobj_object.cpp', 'createobj_screen.cpp', 'createobj_small.cpp', 'createobj_texture.cpp', 'createobj_weld.cpp', 'credits.cpp', 'draw_kit.cpp', 'file_loader.cpp', 'front_display.cpp', 'front_load.cpp', 'front_main.cpp', 'front_menu.cpp', 'front_screens.cpp', 'front_shell.cpp', 'front_states.cpp', 'front_update.cpp', 'front_update_late.cpp', 'front_update_screens.cpp', 'frontend_effects.cpp', 'frontend_mess.cpp', 'frontend_text.cpp', 'frontend_window.cpp', 'goals_competition.cpp', 'goals_dispatch.cpp', 'goals_displays.cpp', 'goals_lights.cpp', 'goals_loading.cpp', 'goals_objects.cpp', 'goals_tag.cpp', 'goals_windows.cpp', 'handicap.cpp', 'hud_display.cpp', 'hud_panel.cpp', 'level_editor_camera.cpp', 'level_editor_cursor.cpp', 'level_editor_global.cpp', 'level_editor_load.cpp', 'level_editor_menus.cpp', 'level_editor_object.cpp', 'level_editor_states.cpp', 'level_editor_ui.cpp', 'level_gen_edit.cpp', 'level_gen_load.cpp', 'level_gen_maps.cpp', 'level_gen_pieces.cpp', 'level_gen_prk.cpp', 'level_gen_rails.cpp', 'level_gen_region.cpp', 'level_select.cpp', 'level_select_classes.cpp', 'mainmenu.cpp', 'mainmenu_screens.cpp', 'menu_dispatch.cpp', 'menu_lists.cpp', 'menu_screens.cpp', 'menu_toolkit.cpp', 'multi_select.cpp', 'net_mouse.cpp', 'net_play.cpp', 'net_state.cpp', 'net_update.cpp', 'netmenu.cpp', 'netmenu_state.cpp', 'netmenu_summary.cpp', 'netmenu_update.cpp', 'options_controls.cpp', 'options_dispatch.cpp', 'options_gaps.cpp', 'options_menu.cpp', 'options_pages.cpp', 'options_scores.cpp', 'pad_input.cpp', 'pc_system.cpp', 'player_select_dispatch.cpp', 'player_select_load.cpp', 'player_select_panels.cpp', 'player_select_ui.cpp', 'player_select_view.cpp', 'player_select_window.cpp', 'player_spooler.cpp', 'pre_manager.cpp', 'psx_env_kit.cpp', 'psx_spool.cpp', 'psx_spool_anim.cpp', 'psx_spool_tex.cpp', 'render_anim.cpp', 'render_d3d.cpp', 'render_display.cpp', 'render_image.cpp', 'render_m3d.cpp', 'render_m3d_anim.cpp', 'render_model.cpp', 'render_palette.cpp', 'render_scene.cpp', 'render_view.cpp', 'resource_kit.cpp', 'screen_kit.cpp', 'shell_bitmap.cpp', 'shell_helpers.cpp', 'skater_manager.cpp', 'skater_spool.cpp', 'skater_window_helpers.cpp', 'skater_windows_appearance.cpp', 'skater_windows_dispatch.cpp', 'skater_windows_personal.cpp', 'skater_windows_stats.cpp', 'skater_windows_sync.cpp', 'surface_store.cpp', 'texture_kit.cpp', 'trick_screens_input.cpp', 'trick_screens_menu.cpp', 'trick_screens_pages.cpp', 'trick_screens_screen.cpp', 'video_frame.cpp', 'video_highlights.cpp', 'video_menu.cpp', 'video_session.cpp', 'video_shorts.cpp', 'video_stream.cpp'],
}

# the views headers of the typed Kits: the only headers that may include the generated views (the .cpp files of their Kit include them)
VIEWS_HEADERS = {"math_views.hpp", "collision_views.hpp", "heap_views.hpp", "core_views.hpp", "m3d_views.hpp", "rail_views.hpp", "mmu_views.hpp", "skater_views.hpp", "equip_views.hpp", "world_views.hpp", "bit_views.hpp", "camera_views.hpp", "gen_views.hpp"}

RAW_ACCESS = re.compile(r"\b(?:m_|memory_?|mem)\s*(?:\.|->)\s*(?:u8|s8|u16|s16|u32|s32|set8|set16|set32)\s*\(")


def raw_accesses(text: str) -> list[str]:
    """Raw accessor calls outside comments."""
    hits = []
    for number, line in enumerate(text.splitlines(), 1):
        code = line.split("//", 1)[0]
        if RAW_ACCESS.search(code):
            hits.append(f"{number}: {line.strip()}")
    return hits


def test_typed_kits_have_no_raw_accessor_calls():
    problems = []
    for kit, sources in TYPED_KITS.items():
        for source in sources:
            for hit in raw_accesses((RUNTIME / source).read_text(encoding="utf-8")):
                problems.append(f"{kit}: {source}:{hit}")
    assert not problems, "\n".join(problems)


def test_the_check_sees_a_raw_access():
    assert raw_accesses("    m_.set16(matrix + 2 * k, 0);\n")
    assert raw_accesses("    const auto v = memory_->u32(a + 4);\n")
    assert not raw_accesses("    // m_.set16(matrix + 2 * k, 0);\n    write_scalar<std::uint16_t>(m_, a, 0);\n")


def test_kit_headers_do_not_include_the_views():
    offenders = [
        path.name
        for path in sorted(RUNTIME.glob("*.hpp"))
        if path.name not in VIEWS_HEADERS and '#include "guest_views_generated.hpp"' in path.read_text(encoding="utf-8")
    ]
    assert not offenders, f"headers that include guest_views_generated.hpp: {offenders}"


def test_typed_kit_sources_exist():
    for sources in TYPED_KITS.values():
        for source in sources:
            assert (RUNTIME / source).is_file(), source
