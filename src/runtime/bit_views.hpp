#pragma once

// 11c, Kit bit: the typed views of its object layouts and the helpers for the words no recovered layout holds (single globals, untyped buffers). Included by the Kit's .cpp files only
// (never by a header: the generated views are large and the Kit's header is included by half the runtime).

#include "bit_kit.hpp"
#include "core_views.hpp"

namespace opentony::runtime::bitkit::views {

using namespace opentony::runtime::core::views;

using guest::RuntimeBitObjectView;
using guest::RuntimeBitSpecialView;
using guest::RuntimeBitNonRenderedView;
using guest::RuntimeBitFt4View;
using guest::RuntimeBitQuadView;
using guest::RuntimeBitGenPolyView;
using guest::RuntimeBitChunkView;
using guest::RuntimeBitSparkView;
using guest::RuntimeBitSimpleAnimView;
using guest::RuntimeBitMotionBlurView;
using guest::RuntimeBitFragView;
using guest::RuntimeBitRibbonView;
using guest::RuntimeBitSmokeTrailView;
using guest::RuntimeBitGlassPieceView;
using guest::RuntimeBitPolylineView;
using guest::RuntimeBitGPolylineView;
using guest::RuntimeBitSmokeGeneratorView;
using guest::RuntimeBitGLineBitView;
using guest::RuntimeBitGLineView;
using guest::RuntimeBitBloodView;
using guest::RuntimeBitGlowView;
using guest::RuntimeBitWobblyView;
using guest::RuntimeBitFlatView;
using guest::RuntimeBitLinkedView;
using guest::RuntimeBitPixelView;
using guest::RuntimeBitEmitterView;

[[nodiscard]] inline guest::RuntimeBitObjectView bit_at(RetailMemory& m, Word at) { return guest::RuntimeBitObjectView(m, at); }
[[nodiscard]] inline guest::RuntimeBitSpecialView special_at(RetailMemory& m, Word at) { return guest::RuntimeBitSpecialView(m, at); }
[[nodiscard]] inline guest::RuntimeBitNonRenderedView nonrendered_at(RetailMemory& m, Word at) { return guest::RuntimeBitNonRenderedView(m, at); }
[[nodiscard]] inline guest::RuntimeBitFt4View ft4_at(RetailMemory& m, Word at) { return guest::RuntimeBitFt4View(m, at); }
[[nodiscard]] inline guest::RuntimeBitQuadView quad_at(RetailMemory& m, Word at) { return guest::RuntimeBitQuadView(m, at); }
[[nodiscard]] inline guest::RuntimeBitGenPolyView genpoly_at(RetailMemory& m, Word at) { return guest::RuntimeBitGenPolyView(m, at); }
[[nodiscard]] inline guest::RuntimeBitChunkView chunk_at(RetailMemory& m, Word at) { return guest::RuntimeBitChunkView(m, at); }
[[nodiscard]] inline guest::RuntimeBitSparkView spark_at(RetailMemory& m, Word at) { return guest::RuntimeBitSparkView(m, at); }
[[nodiscard]] inline guest::RuntimeBitSimpleAnimView simple_anim_at(RetailMemory& m, Word at) { return guest::RuntimeBitSimpleAnimView(m, at); }
[[nodiscard]] inline guest::RuntimeBitMotionBlurView motion_blur_at(RetailMemory& m, Word at) { return guest::RuntimeBitMotionBlurView(m, at); }
[[nodiscard]] inline guest::RuntimeBitFragView frag_at(RetailMemory& m, Word at) { return guest::RuntimeBitFragView(m, at); }
[[nodiscard]] inline guest::RuntimeBitRibbonView ribbon_at(RetailMemory& m, Word at) { return guest::RuntimeBitRibbonView(m, at); }
[[nodiscard]] inline guest::RuntimeBitSmokeTrailView smoke_trail_at(RetailMemory& m, Word at) { return guest::RuntimeBitSmokeTrailView(m, at); }
[[nodiscard]] inline guest::RuntimeBitGlassPieceView glass_at(RetailMemory& m, Word at) { return guest::RuntimeBitGlassPieceView(m, at); }
[[nodiscard]] inline guest::RuntimeBitPolylineView polyline_at(RetailMemory& m, Word at) { return guest::RuntimeBitPolylineView(m, at); }
[[nodiscard]] inline guest::RuntimeBitGPolylineView gpolyline_at(RetailMemory& m, Word at) { return guest::RuntimeBitGPolylineView(m, at); }
[[nodiscard]] inline guest::RuntimeBitSmokeGeneratorView smoke_gen_at(RetailMemory& m, Word at) { return guest::RuntimeBitSmokeGeneratorView(m, at); }
[[nodiscard]] inline guest::RuntimeBitGLineBitView gline_bit_at(RetailMemory& m, Word at) { return guest::RuntimeBitGLineBitView(m, at); }
[[nodiscard]] inline guest::RuntimeBitGLineView gline_at(RetailMemory& m, Word at) { return guest::RuntimeBitGLineView(m, at); }
[[nodiscard]] inline guest::RuntimeBitBloodView blood_at(RetailMemory& m, Word at) { return guest::RuntimeBitBloodView(m, at); }
[[nodiscard]] inline guest::RuntimeBitGlowView glow_at(RetailMemory& m, Word at) { return guest::RuntimeBitGlowView(m, at); }
[[nodiscard]] inline guest::RuntimeBitWobblyView wobbly_at(RetailMemory& m, Word at) { return guest::RuntimeBitWobblyView(m, at); }
[[nodiscard]] inline guest::RuntimeBitFlatView flat_at(RetailMemory& m, Word at) { return guest::RuntimeBitFlatView(m, at); }
[[nodiscard]] inline guest::RuntimeBitLinkedView linked_at(RetailMemory& m, Word at) { return guest::RuntimeBitLinkedView(m, at); }
[[nodiscard]] inline guest::RuntimeBitPixelView pixel_at(RetailMemory& m, Word at) { return guest::RuntimeBitPixelView(m, at); }
[[nodiscard]] inline guest::RuntimeBitEmitterView emitter_at(RetailMemory& m, Word at) { return guest::RuntimeBitEmitterView(m, at); }

}  // namespace opentony::runtime::bitkit::views
