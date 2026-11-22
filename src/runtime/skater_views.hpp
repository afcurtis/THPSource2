#pragma once

// 11c, Kit skater (skater_ground_physics.cpp so far): the typed views of the gameplay skater object, the collision query record and the platform list objects, and the helpers for the vectors the
// ground physics walks by index. Included by those .cpp files only (never by a header: the generated views are large and the Kit's header is included by half the runtime).

#include "core_views.hpp"
#include "skater_kit.hpp"

namespace opentony::runtime::skaterkit::views {

using namespace core::views;

using guest::RuntimeCollisionObject;

// The query record the ground physics fills in the scratch area and hands to the zone query (the retail function keeps it on its stack): the start and the end are three words each, the hit follows at +0x68.
[[nodiscard]] inline SLineInfoView line_at(RetailMemory& m, Word at) { return SLineInfoView(m, at); }

[[nodiscard]] inline Word line_start(const SLineInfoView& l, Word k) { return static_cast<Word>(k == 0 ? l.start_x() : (k == 1 ? l.start_y() : l.start_z())); }
[[nodiscard]] inline Word line_end(const SLineInfoView& l, Word k) { return static_cast<Word>(k == 0 ? l.end_x() : (k == 1 ? l.end_y() : l.end_z())); }
inline void set_line_start(const SLineInfoView& l, Word k, Word value) {
    const auto word = static_cast<std::int32_t>(value);
    if (k == 0) {
        l.set_start_x(word);
    } else if (k == 1) {
        l.set_start_y(word);
    } else {
        l.set_start_z(word);
    }
}
inline void set_line_end(const SLineInfoView& l, Word k, Word value) {
    const auto word = static_cast<std::int32_t>(value);
    if (k == 0) {
        l.set_end_x(word);
    } else if (k == 1) {
        l.set_end_y(word);
    } else {
        l.set_end_z(word);
    }
}

}  // namespace opentony::runtime::skaterkit::views
