#pragma once

#include <cstdint>

// Presentation (not retail; nothing here touches the game's memory): the model polygons of views that fill only part of a wide screen's width.
//
// On a screen wider than its 2D layer (draw_kit.hpp, Presentation), a view that fills only part of the width (the player select's skater, the menus' models) was
// projected around the centre of the view's rectangle on the wide screen, but the frame around it is laid out in the 4:3 middle (DrawKit::fit_flat).
// M3D_ClipAndBucketPolygon (render_model.cpp) tells each such polygon and the view's edges (`left` and `right` in display pixels, the edges record's +4 and +0); the
// walk draws it moved by as much as the view's centre moves (DrawKit::general_polygon). The view's own viewport packets do not bracket its polygons in the walk
// (M3d_RenderCleanup's full-screen rectangle follows the opening one at once), so the polygon itself carries the shift. Forgotten after each present. With no wide
// screen set nothing is kept. A file of its own so that the render layer's replays (tests/test_render_layer.py) link without the draw path.
namespace opentony::runtime::drawkit {

void set_view_polygon_screen(std::int32_t screen_width, std::int32_t hud_width);   // from set_presentation
void presentation_view_polygon(std::uint32_t polygon, std::int32_t left, std::int32_t right);
[[nodiscard]] float view_polygon_shift(std::uint32_t polygon);   // 0 for a polygon nobody told
void forget_view_polygons();

}  // namespace opentony::runtime::drawkit
