#pragma once

// 11c, Kit gen: the typed views of its object layouts and the helpers for the words no recovered layout holds (single globals, untyped buffers). Included by the Kit's .cpp files only
// (never by a header: the generated views are large and the Kit's header is included by half the runtime).

#include "gen_objs.hpp"
#include "core_views.hpp"

namespace opentony::runtime::genobjs::views {

using namespace opentony::runtime::core::views;

using guest::RuntimeGenPointView;
using guest::RuntimeGenPolygonView;
using guest::RuntimeGenPolygonListView;
using guest::RuntimeGenOutView;
using guest::RuntimeGenInView;
using guest::RuntimeGenGapView;

[[nodiscard]] inline guest::RuntimeGenPointView gen_point_at(RetailMemory& m, Word at) { return guest::RuntimeGenPointView(m, at); }
[[nodiscard]] inline guest::RuntimeGenPolygonView gen_poly_at(RetailMemory& m, Word at) { return guest::RuntimeGenPolygonView(m, at); }
[[nodiscard]] inline guest::RuntimeGenPolygonListView gen_plist_at(RetailMemory& m, Word at) { return guest::RuntimeGenPolygonListView(m, at); }
[[nodiscard]] inline guest::RuntimeGenOutView gen_out_at(RetailMemory& m, Word at) { return guest::RuntimeGenOutView(m, at); }
[[nodiscard]] inline guest::RuntimeGenInView gen_in_at(RetailMemory& m, Word at) { return guest::RuntimeGenInView(m, at); }
[[nodiscard]] inline guest::RuntimeGenGapView gen_gap_at(RetailMemory& m, Word at) { return guest::RuntimeGenGapView(m, at); }

}  // namespace opentony::runtime::genobjs::views
