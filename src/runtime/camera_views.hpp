#pragma once

// 11c, Kit camera: the typed views of its object layouts and the helpers for the words no recovered layout holds (single globals, untyped buffers). Included by the Kit's .cpp files only
// (never by a header: the generated views are large and the Kit's header is included by half the runtime).

#include "camera_kit.hpp"
#include "core_views.hpp"

namespace opentony::runtime::camerakit::views {

using namespace opentony::runtime::core::views;

using guest::CCameraView;
using guest::RuntimeGameplaySkaterView;
using guest::RuntimeCameraQuaternionView;
using guest::RuntimeCameraRigView;

[[nodiscard]] inline guest::CCameraView cam_at(RetailMemory& m, Word at) { return guest::CCameraView(m, at); }
[[nodiscard]] inline guest::RuntimeGameplaySkaterView followed_at(RetailMemory& m, Word at) { return guest::RuntimeGameplaySkaterView(m, at); }
[[nodiscard]] inline guest::RuntimeCameraQuaternionView quaternion_at(RetailMemory& m, Word at) { return guest::RuntimeCameraQuaternionView(m, at); }
[[nodiscard]] inline guest::RuntimeCameraRigView rig_at(RetailMemory& m, Word at) { return guest::RuntimeCameraRigView(m, at); }

}  // namespace opentony::runtime::camerakit::views
