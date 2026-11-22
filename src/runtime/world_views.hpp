#pragma once

// 11c, Kit world: the typed views of its object layouts and the helpers for the words no recovered layout holds (single globals, untyped buffers). Included by the Kit's .cpp files only
// (never by a header: the generated views are large and the Kit's header is included by half the runtime).

#include "world_kit.hpp"
#include "core_views.hpp"

namespace opentony::runtime::worldkit::views {

using namespace opentony::runtime::core::views;

using guest::RuntimePlatformObjectView;
using guest::RuntimePowerupObjectView;
using guest::RuntimeBaddyObjectView;
using guest::RuntimeTriggerTrafficObjectView;
using guest::RuntimeTriggerScriptObject0xabView;
using guest::RuntimeTriggerSubtype0x00cbView;
using guest::RuntimeTriggerSubtype0x0192View;
using guest::RuntimeGameplaySkaterView;
using guest::RuntimeListObjectView;
using guest::RuntimeAnimatedObjectView;
using guest::RuntimeTrickObjectView;
using guest::RuntimeEditorCommandBaseView;
using guest::RuntimeEditorNodeCommandView;
using guest::RuntimeEditorLinkCommandView;
using guest::RuntimeEditorEndCommandView;
using guest::RuntimeEditorCommandCView;
using guest::RuntimeEditorCommandEView;
using guest::RuntimeEditorCommandFView;
using guest::RuntimeEditorCommandGView;

[[nodiscard]] inline guest::RuntimePlatformObjectView platform_at(RetailMemory& m, Word at) { return guest::RuntimePlatformObjectView(m, at); }
[[nodiscard]] inline guest::RuntimePowerupObjectView powerup_at(RetailMemory& m, Word at) { return guest::RuntimePowerupObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeBaddyObjectView baddy_at(RetailMemory& m, Word at) { return guest::RuntimeBaddyObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeTriggerTrafficObjectView traffic_at(RetailMemory& m, Word at) { return guest::RuntimeTriggerTrafficObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeTriggerScriptObject0xabView script_object_at(RetailMemory& m, Word at) { return guest::RuntimeTriggerScriptObject0xabView(m, at); }
[[nodiscard]] inline guest::RuntimeTriggerSubtype0x00cbView subtype_cb_at(RetailMemory& m, Word at) { return guest::RuntimeTriggerSubtype0x00cbView(m, at); }
[[nodiscard]] inline guest::RuntimeTriggerSubtype0x0192View subtype_192_at(RetailMemory& m, Word at) { return guest::RuntimeTriggerSubtype0x0192View(m, at); }
[[nodiscard]] inline guest::RuntimeGameplaySkaterView skater_view(RetailMemory& m, Word at) { return guest::RuntimeGameplaySkaterView(m, at); }
[[nodiscard]] inline guest::RuntimeListObjectView list_object_at(RetailMemory& m, Word at) { return guest::RuntimeListObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeAnimatedObjectView animated_at(RetailMemory& m, Word at) { return guest::RuntimeAnimatedObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeTrickObjectView trick_at(RetailMemory& m, Word at) { return guest::RuntimeTrickObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeEditorCommandBaseView cmd_at(RetailMemory& m, Word at) { return guest::RuntimeEditorCommandBaseView(m, at); }
[[nodiscard]] inline guest::RuntimeEditorNodeCommandView node_cmd_at(RetailMemory& m, Word at) { return guest::RuntimeEditorNodeCommandView(m, at); }
[[nodiscard]] inline guest::RuntimeEditorLinkCommandView link_cmd_at(RetailMemory& m, Word at) { return guest::RuntimeEditorLinkCommandView(m, at); }
[[nodiscard]] inline guest::RuntimeEditorEndCommandView end_cmd_at(RetailMemory& m, Word at) { return guest::RuntimeEditorEndCommandView(m, at); }
[[nodiscard]] inline guest::RuntimeEditorCommandCView cmd_c_at(RetailMemory& m, Word at) { return guest::RuntimeEditorCommandCView(m, at); }
[[nodiscard]] inline guest::RuntimeEditorCommandEView cmd_e_at(RetailMemory& m, Word at) { return guest::RuntimeEditorCommandEView(m, at); }
[[nodiscard]] inline guest::RuntimeEditorCommandFView cmd_f_at(RetailMemory& m, Word at) { return guest::RuntimeEditorCommandFView(m, at); }
[[nodiscard]] inline guest::RuntimeEditorCommandGView cmd_g_at(RetailMemory& m, Word at) { return guest::RuntimeEditorCommandGView(m, at); }

}  // namespace opentony::runtime::worldkit::views
