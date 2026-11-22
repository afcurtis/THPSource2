#include "render_presentation.hpp"

#include <unordered_map>

namespace opentony::runtime::drawkit {

namespace {

float g_screen = 0.0f;
float g_hud = 0.0f;
std::unordered_map<std::uint32_t, float> g_polygons;   // polygon address -> horizontal shift

}  // namespace

void set_view_polygon_screen(std::int32_t screen_width, std::int32_t hud_width) {
    g_screen = static_cast<float>(screen_width);
    g_hud = static_cast<float>(hud_width);
    g_polygons.clear();
}

void presentation_view_polygon(std::uint32_t polygon, std::int32_t left, std::int32_t right) {
    if (!(g_hud > 0.0f) || !(g_screen > g_hud) || (left <= 1 && static_cast<float>(right) >= g_screen - 2.0f)) {
        return;   // no squeezed 2D layer, or a view across the whole width
    }
    const float centre = 0.5f * static_cast<float>(left + right);   // the edges record's +0x10, as M3D_BeginRenderView sets it
    g_polygons[polygon] = centre * (g_hud / g_screen) + (g_screen - g_hud) * 0.5f - centre;
}

float view_polygon_shift(std::uint32_t polygon) {
    if (g_polygons.empty()) {
        return 0.0f;
    }
    const auto found = g_polygons.find(polygon);
    return found != g_polygons.end() ? found->second : 0.0f;
}

void forget_view_polygons() { g_polygons.clear(); }

}  // namespace opentony::runtime::drawkit
