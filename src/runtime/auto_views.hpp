#pragma once

// 11c, Kit auto: the typed views of its object layouts and the helpers for the words no recovered layout holds (single globals, untyped buffers). Included by the Kit's .cpp files only
// (never by a header: the generated views are large and the Kit's header is included by half the runtime).

#include "core_views.hpp"

namespace opentony::runtime::autoviews {

using namespace opentony::runtime::core::views;

using guest::RuntimeCareerSelectUpdateObjectView;
using guest::RuntimeCreateScreensEditorPersonalObjectView;
using guest::RuntimeCreateScreensEditorObjectView;
using guest::RuntimeCreateScreensSaveObjectView;
using guest::RuntimeCreateScreensSelectObjectView;
using guest::RuntimeCreateScreensStatObjectView;
using guest::RuntimeCreateScreensUiBoxObjectView;
using guest::RuntimeCreateScreensUiConfirmObjectView;
using guest::RuntimeCreateobjDefDefinitionObjectView;
using guest::RuntimeCreateobjMiscTrickObjectView;
using guest::RuntimeCreateobjObjectSkaterObjectView;
using guest::RuntimeCreateobjScreenKeyconfigObjectView;
using guest::RuntimeCreateobjSmallTextureObjectView;
using guest::RuntimeCreateobjSmallSpoolerObjectView;
using guest::RuntimeCreateobjTextureApplyObjectView;
using guest::RuntimeCreditsTextObjectView;
using guest::RuntimeCreditsBitmapObjectView;
using guest::RuntimeCreditsMusicObjectView;
using guest::RuntimeFrontMenuCmenuObjectView;
using guest::RuntimeFrontendEffectsEffectObjectView;
using guest::RuntimeFrontendEffectsFallingObjectView;
using guest::RuntimeFrontendEffectsZoomObjectView;
using guest::RuntimeFrontendWindowElementObjectView;
using guest::RuntimeFrontendWindowObjectView;
using guest::RuntimeFrontendWindowTextObjectView;
using guest::RuntimeFrontendWindowSpriteObjectView;
using guest::RuntimeGoalsCompetitionCompObjectView;
using guest::RuntimeGoalsDisplaysCompObjectView;
using guest::RuntimeGoalsLightsLightObjectView;
using guest::RuntimeGoalsWindowsVideoObjectView;
using guest::RuntimeGoalsWindowsLevelpicObjectView;
using guest::RuntimeHandicapPanelObjectView;
using guest::RuntimeHandicapWindowObjectView;
using guest::RuntimeLevelEditorCameraObjectView;
using guest::RuntimeLevelEditorCursorObjectView;
using guest::RuntimeLevelEditorGlobalMenuObjectView;
using guest::RuntimeLevelEditorObjectObjectView;
using guest::RuntimeLevelEditorUiObjectView;
using guest::RuntimeLevelGenMapsFreeObjectView;
using guest::RuntimeLevelGenPiecesBuildObjectView;
using guest::RuntimeLevelSelectClassesBannerObjectView;
using guest::RuntimeLevelSelectClassesGlobeObjectView;
using guest::RuntimeLevelSelectClassesNameObjectView;
using guest::RuntimeLevelSelectClassesMenuObjectView;
using guest::RuntimeMainmenuWindowObjectView;
using guest::RuntimeMainmenuScreensWheelObjectView;
using guest::RuntimeMainmenuScreensCardObjectView;
using guest::RuntimeMenuListsListObjectView;
using guest::RuntimeMenuListsScrollObjectView;
using guest::RuntimeMenuListsGridObjectView;
using guest::RuntimeMenuScreensNameObjectView;
using guest::RuntimeMenuScreensWrapperObjectView;
using guest::RuntimeMenuToolkitControlObjectView;
using guest::RuntimeMenuToolkitPropertiesObjectView;
using guest::RuntimeMenuToolkitTextObjectView;
using guest::RuntimeMenuToolkitRowObjectView;
using guest::RuntimeMenuToolkitDotsObjectView;
using guest::RuntimeMenuToolkitSliderObjectView;
using guest::RuntimeNetMouseEditObjectView;
using guest::RuntimeNetmenuSummaryObjectView;
using guest::RuntimeOptionsControlsObjectView;
using guest::RuntimeOptionsControlsCheatsObjectView;
using guest::RuntimeOptionsGapsObjectView;
using guest::RuntimeOptionsMenuObjectView;
using guest::RuntimeOptionsMenuStepObjectView;
using guest::RuntimeOptionsPagesMovieObjectView;
using guest::RuntimeOptionsPagesSoundObjectView;
using guest::RuntimeOptionsPagesCardObjectView;
using guest::RuntimeOptionsPagesReplayObjectView;
using guest::RuntimeOptionsScoresObjectView;
using guest::RuntimePcSystemVideoObjectView;
using guest::RuntimePlayerSelectLoadObjectView;
using guest::RuntimePlayerSelectPanelsBioObjectView;
using guest::RuntimePlayerSelectPanelsDotsObjectView;
using guest::RuntimePlayerSelectPanelsHeaderObjectView;
using guest::RuntimePlayerSelectPanelsMenuObjectView;
using guest::RuntimePlayerSelectUiCounterObjectView;
using guest::RuntimePlayerSelectUiMedalObjectView;
using guest::RuntimePlayerSelectUiPromptObjectView;
using guest::RuntimePlayerSelectUiFlagObjectView;
using guest::RuntimePlayerSelectViewObjectView;
using guest::RuntimePlayerSelectWindowObjectView;
using guest::RuntimeRenderAnimModelObjectView;
using guest::RuntimeRenderImageObjectView;
using guest::RuntimeRenderImageSlicedObjectView;
using guest::RuntimeRenderImageBmpObjectView;
using guest::RuntimeRenderImageSpriteObjectView;
using guest::RuntimeShellHelpersPieObjectView;
using guest::RuntimeShellHelpersEffectObjectView;
using guest::RuntimeShellHelpersTextwinObjectView;
using guest::RuntimeSkaterWindowHelpersColourObjectView;
using guest::RuntimeSkaterWindowHelpersStepperObjectView;
using guest::RuntimeSkaterWindowHelpersNumberObjectView;
using guest::RuntimeSkaterWindowHelpersModelObjectView;
using guest::RuntimeSkaterWindowHelpersInitialsObjectView;
using guest::RuntimeSkaterWindowsAppearanceObjectView;
using guest::RuntimeSkaterWindowsPersonalObjectView;
using guest::RuntimeSkaterWindowsStatsStatObjectView;
using guest::RuntimeSkaterWindowsStatsPointsObjectView;
using guest::RuntimeSkaterWindowsSyncAppearanceObjectView;
using guest::RuntimeTrickScreensInputUpdateObjectView;
using guest::RuntimeTrickScreensInputPanelObjectView;
using guest::RuntimeTrickScreensMenuObjectView;
using guest::RuntimeTrickScreensScreenObjectView;
using guest::RuntimeVideoShortsObjectView;

[[nodiscard]] inline guest::RuntimeCareerSelectUpdateObjectView career_select_update_at(RetailMemory& m, Word at) { return guest::RuntimeCareerSelectUpdateObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateScreensEditorPersonalObjectView create_screens_editor_personal_at(RetailMemory& m, Word at) { return guest::RuntimeCreateScreensEditorPersonalObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateScreensEditorObjectView create_screens_editor_editor_at(RetailMemory& m, Word at) { return guest::RuntimeCreateScreensEditorObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateScreensSaveObjectView create_screens_save_save_at(RetailMemory& m, Word at) { return guest::RuntimeCreateScreensSaveObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateScreensSelectObjectView create_screens_select_select_at(RetailMemory& m, Word at) { return guest::RuntimeCreateScreensSelectObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateScreensStatObjectView create_screens_stat_stat_at(RetailMemory& m, Word at) { return guest::RuntimeCreateScreensStatObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateScreensUiBoxObjectView create_screens_ui_box_at(RetailMemory& m, Word at) { return guest::RuntimeCreateScreensUiBoxObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateScreensUiConfirmObjectView create_screens_ui_confirm_at(RetailMemory& m, Word at) { return guest::RuntimeCreateScreensUiConfirmObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateobjDefDefinitionObjectView createobj_def_definition_at(RetailMemory& m, Word at) { return guest::RuntimeCreateobjDefDefinitionObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateobjMiscTrickObjectView createobj_misc_trick_at(RetailMemory& m, Word at) { return guest::RuntimeCreateobjMiscTrickObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateobjObjectSkaterObjectView createobj_object_skater_at(RetailMemory& m, Word at) { return guest::RuntimeCreateobjObjectSkaterObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateobjScreenKeyconfigObjectView createobj_screen_keyconfig_at(RetailMemory& m, Word at) { return guest::RuntimeCreateobjScreenKeyconfigObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateobjSmallTextureObjectView createobj_small_texture_at(RetailMemory& m, Word at) { return guest::RuntimeCreateobjSmallTextureObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateobjSmallSpoolerObjectView createobj_small_spooler_at(RetailMemory& m, Word at) { return guest::RuntimeCreateobjSmallSpoolerObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreateobjTextureApplyObjectView createobj_texture_apply_at(RetailMemory& m, Word at) { return guest::RuntimeCreateobjTextureApplyObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreditsTextObjectView credits_text_at(RetailMemory& m, Word at) { return guest::RuntimeCreditsTextObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreditsBitmapObjectView credits_bitmap_at(RetailMemory& m, Word at) { return guest::RuntimeCreditsBitmapObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeCreditsMusicObjectView credits_music_at(RetailMemory& m, Word at) { return guest::RuntimeCreditsMusicObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeFrontMenuCmenuObjectView front_menu_cmenu_at(RetailMemory& m, Word at) { return guest::RuntimeFrontMenuCmenuObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeFrontendEffectsEffectObjectView frontend_effects_effect_at(RetailMemory& m, Word at) { return guest::RuntimeFrontendEffectsEffectObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeFrontendEffectsFallingObjectView frontend_effects_falling_at(RetailMemory& m, Word at) { return guest::RuntimeFrontendEffectsFallingObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeFrontendEffectsZoomObjectView frontend_effects_zoom_at(RetailMemory& m, Word at) { return guest::RuntimeFrontendEffectsZoomObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeFrontendWindowElementObjectView frontend_window_element_at(RetailMemory& m, Word at) { return guest::RuntimeFrontendWindowElementObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeFrontendWindowObjectView frontend_window_window_at(RetailMemory& m, Word at) { return guest::RuntimeFrontendWindowObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeFrontendWindowTextObjectView frontend_window_text_at(RetailMemory& m, Word at) { return guest::RuntimeFrontendWindowTextObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeFrontendWindowSpriteObjectView frontend_window_sprite_at(RetailMemory& m, Word at) { return guest::RuntimeFrontendWindowSpriteObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeGoalsCompetitionCompObjectView goals_competition_comp_at(RetailMemory& m, Word at) { return guest::RuntimeGoalsCompetitionCompObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeGoalsDisplaysCompObjectView goals_displays_comp_at(RetailMemory& m, Word at) { return guest::RuntimeGoalsDisplaysCompObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeGoalsLightsLightObjectView goals_lights_light_at(RetailMemory& m, Word at) { return guest::RuntimeGoalsLightsLightObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeGoalsWindowsVideoObjectView goals_windows_video_at(RetailMemory& m, Word at) { return guest::RuntimeGoalsWindowsVideoObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeGoalsWindowsLevelpicObjectView goals_windows_levelpic_at(RetailMemory& m, Word at) { return guest::RuntimeGoalsWindowsLevelpicObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeHandicapPanelObjectView handicap_panel_at(RetailMemory& m, Word at) { return guest::RuntimeHandicapPanelObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeHandicapWindowObjectView handicap_window_at(RetailMemory& m, Word at) { return guest::RuntimeHandicapWindowObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelEditorCameraObjectView level_editor_camera_camera_at(RetailMemory& m, Word at) { return guest::RuntimeLevelEditorCameraObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelEditorCursorObjectView level_editor_cursor_cursor_at(RetailMemory& m, Word at) { return guest::RuntimeLevelEditorCursorObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelEditorGlobalMenuObjectView level_editor_global_menu_at(RetailMemory& m, Word at) { return guest::RuntimeLevelEditorGlobalMenuObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelEditorObjectObjectView level_editor_object_editor_at(RetailMemory& m, Word at) { return guest::RuntimeLevelEditorObjectObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelEditorUiObjectView level_editor_ui_ui_at(RetailMemory& m, Word at) { return guest::RuntimeLevelEditorUiObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelGenMapsFreeObjectView level_gen_maps_free_at(RetailMemory& m, Word at) { return guest::RuntimeLevelGenMapsFreeObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelGenPiecesBuildObjectView level_gen_pieces_build_at(RetailMemory& m, Word at) { return guest::RuntimeLevelGenPiecesBuildObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelSelectClassesBannerObjectView level_select_classes_banner_at(RetailMemory& m, Word at) { return guest::RuntimeLevelSelectClassesBannerObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelSelectClassesGlobeObjectView level_select_classes_globe_at(RetailMemory& m, Word at) { return guest::RuntimeLevelSelectClassesGlobeObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelSelectClassesNameObjectView level_select_classes_name_at(RetailMemory& m, Word at) { return guest::RuntimeLevelSelectClassesNameObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeLevelSelectClassesMenuObjectView level_select_classes_menu_at(RetailMemory& m, Word at) { return guest::RuntimeLevelSelectClassesMenuObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMainmenuWindowObjectView mainmenu_window_at(RetailMemory& m, Word at) { return guest::RuntimeMainmenuWindowObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMainmenuScreensWheelObjectView mainmenu_screens_wheel_at(RetailMemory& m, Word at) { return guest::RuntimeMainmenuScreensWheelObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMainmenuScreensCardObjectView mainmenu_screens_card_at(RetailMemory& m, Word at) { return guest::RuntimeMainmenuScreensCardObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuListsListObjectView menu_lists_list_at(RetailMemory& m, Word at) { return guest::RuntimeMenuListsListObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuListsScrollObjectView menu_lists_scroll_at(RetailMemory& m, Word at) { return guest::RuntimeMenuListsScrollObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuListsGridObjectView menu_lists_grid_at(RetailMemory& m, Word at) { return guest::RuntimeMenuListsGridObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuScreensNameObjectView menu_screens_name_at(RetailMemory& m, Word at) { return guest::RuntimeMenuScreensNameObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuScreensWrapperObjectView menu_screens_wrapper_at(RetailMemory& m, Word at) { return guest::RuntimeMenuScreensWrapperObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuToolkitControlObjectView menu_toolkit_control_at(RetailMemory& m, Word at) { return guest::RuntimeMenuToolkitControlObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuToolkitPropertiesObjectView menu_toolkit_properties_at(RetailMemory& m, Word at) { return guest::RuntimeMenuToolkitPropertiesObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuToolkitTextObjectView menu_toolkit_text_at(RetailMemory& m, Word at) { return guest::RuntimeMenuToolkitTextObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuToolkitRowObjectView menu_toolkit_row_at(RetailMemory& m, Word at) { return guest::RuntimeMenuToolkitRowObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuToolkitDotsObjectView menu_toolkit_dots_at(RetailMemory& m, Word at) { return guest::RuntimeMenuToolkitDotsObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeMenuToolkitSliderObjectView menu_toolkit_slider_at(RetailMemory& m, Word at) { return guest::RuntimeMenuToolkitSliderObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeNetMouseEditObjectView net_mouse_edit_at(RetailMemory& m, Word at) { return guest::RuntimeNetMouseEditObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeNetmenuSummaryObjectView netmenu_summary_summary_at(RetailMemory& m, Word at) { return guest::RuntimeNetmenuSummaryObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsControlsObjectView options_controls_controls_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsControlsObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsControlsCheatsObjectView options_controls_cheats_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsControlsCheatsObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsGapsObjectView options_gaps_gaps_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsGapsObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsMenuObjectView options_menu_menu_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsMenuObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsMenuStepObjectView options_menu_step_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsMenuStepObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsPagesMovieObjectView options_pages_movie_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsPagesMovieObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsPagesSoundObjectView options_pages_sound_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsPagesSoundObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsPagesCardObjectView options_pages_card_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsPagesCardObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsPagesReplayObjectView options_pages_replay_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsPagesReplayObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeOptionsScoresObjectView options_scores_scores_at(RetailMemory& m, Word at) { return guest::RuntimeOptionsScoresObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePcSystemVideoObjectView pc_system_video_at(RetailMemory& m, Word at) { return guest::RuntimePcSystemVideoObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectLoadObjectView player_select_load_load_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectLoadObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectPanelsBioObjectView player_select_panels_bio_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectPanelsBioObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectPanelsDotsObjectView player_select_panels_dots_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectPanelsDotsObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectPanelsHeaderObjectView player_select_panels_header_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectPanelsHeaderObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectPanelsMenuObjectView player_select_panels_menu_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectPanelsMenuObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectUiCounterObjectView player_select_ui_counter_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectUiCounterObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectUiMedalObjectView player_select_ui_medal_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectUiMedalObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectUiPromptObjectView player_select_ui_prompt_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectUiPromptObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectUiFlagObjectView player_select_ui_flag_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectUiFlagObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectViewObjectView player_select_view_view_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectViewObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePlayerSelectWindowObjectView player_select_window_select_at(RetailMemory& m, Word at) { return guest::RuntimePlayerSelectWindowObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeRenderAnimModelObjectView render_anim_model_at(RetailMemory& m, Word at) { return guest::RuntimeRenderAnimModelObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeRenderImageObjectView render_image_image_at(RetailMemory& m, Word at) { return guest::RuntimeRenderImageObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeRenderImageSlicedObjectView render_image_sliced_at(RetailMemory& m, Word at) { return guest::RuntimeRenderImageSlicedObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeRenderImageBmpObjectView render_image_bmp_at(RetailMemory& m, Word at) { return guest::RuntimeRenderImageBmpObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeRenderImageSpriteObjectView render_image_sprite_at(RetailMemory& m, Word at) { return guest::RuntimeRenderImageSpriteObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeShellHelpersPieObjectView shell_helpers_pie_at(RetailMemory& m, Word at) { return guest::RuntimeShellHelpersPieObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeShellHelpersEffectObjectView shell_helpers_effect_at(RetailMemory& m, Word at) { return guest::RuntimeShellHelpersEffectObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeShellHelpersTextwinObjectView shell_helpers_textwin_at(RetailMemory& m, Word at) { return guest::RuntimeShellHelpersTextwinObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowHelpersColourObjectView skater_window_helpers_colour_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowHelpersColourObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowHelpersStepperObjectView skater_window_helpers_stepper_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowHelpersStepperObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowHelpersNumberObjectView skater_window_helpers_number_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowHelpersNumberObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowHelpersModelObjectView skater_window_helpers_model_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowHelpersModelObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowHelpersInitialsObjectView skater_window_helpers_initials_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowHelpersInitialsObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowsAppearanceObjectView skater_windows_appearance_appearance_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowsAppearanceObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowsPersonalObjectView skater_windows_personal_personal_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowsPersonalObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowsStatsStatObjectView skater_windows_stats_stat_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowsStatsStatObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowsStatsPointsObjectView skater_windows_stats_points_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowsStatsPointsObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeSkaterWindowsSyncAppearanceObjectView skater_windows_sync_appearance_at(RetailMemory& m, Word at) { return guest::RuntimeSkaterWindowsSyncAppearanceObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeTrickScreensInputUpdateObjectView trick_screens_input_update_at(RetailMemory& m, Word at) { return guest::RuntimeTrickScreensInputUpdateObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeTrickScreensInputPanelObjectView trick_screens_input_panel_at(RetailMemory& m, Word at) { return guest::RuntimeTrickScreensInputPanelObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeTrickScreensMenuObjectView trick_screens_menu_menu_at(RetailMemory& m, Word at) { return guest::RuntimeTrickScreensMenuObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeTrickScreensScreenObjectView trick_screens_screen_screen_at(RetailMemory& m, Word at) { return guest::RuntimeTrickScreensScreenObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeVideoShortsObjectView video_shorts_shorts_at(RetailMemory& m, Word at) { return guest::RuntimeVideoShortsObjectView(m, at); }

}  // namespace opentony::runtime::autoviews
