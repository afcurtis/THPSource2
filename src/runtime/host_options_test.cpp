// host_options.hpp: the detail distance is the game's own at its default, and a large percentage neither wraps nor changes the sign of a distance.
#include "host_options.hpp"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace rt = opentony::runtime;

static int g_failures = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c);    \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

int main() {
    // the default is retail: 100 percent returns the game's value untouched, whatever it is
    CHECK(rt::host_options().detail_distance == 100);
    for (const std::int32_t limit : {0, 1, 4020, 65535, -1, -300, INT32_MAX, INT32_MIN}) {
        CHECK(rt::detail_reach(limit) == limit);
    }

    rt::host_options().detail_distance = 200;
    CHECK(rt::detail_reach(4020) == 8040 && rt::detail_reach(0) == 0 && rt::detail_reach(-300) == -600);
    rt::host_options().detail_distance = 400;
    CHECK(rt::detail_reach(4020) == 16080 && rt::detail_reach(1) == 4);
    rt::host_options().detail_distance = 150;
    CHECK(rt::detail_reach(101) == 151);   // whole units, the remainder dropped

    // the largest percentage the launcher offers (maximum, 100000) on the largest distance the game's 16-bit field can give after its scaling: held at the 32-bit limit, not wrapped
    rt::host_options().detail_distance = 100000;
    CHECK(rt::detail_reach(65535) == 65535 * 1000 && rt::detail_reach(-65535) == -65535 * 1000);
    CHECK(rt::detail_reach(INT32_MAX) == INT32_MAX && rt::detail_reach(INT32_MIN) == INT32_MIN);
    CHECK(rt::detail_reach(1) == 1000);

    rt::host_options().detail_distance = 100;
    CHECK(rt::detail_reach(4020) == 4020);

    // the cull box: the game's at 100 percent or without arena room, stretched otherwise (maximum: far past any level)
    CHECK(rt::host_options().detail_distance == 100);
    CHECK(rt::cull_reach(32767.0f, true) == 32767.0f && rt::cull_reach(-32767.0f, true) == -32767.0f);
    rt::host_options().detail_distance = 200;
    CHECK(rt::cull_reach(32767.0f, true) == 65534.0f && rt::cull_reach(-32767.0f, true) == -65534.0f);
    CHECK(rt::cull_reach(32767.0f, false) == 32767.0f);
    rt::host_options().detail_distance = 100000;
    CHECK(rt::cull_reach(32767.0f, true) == 32767000.0f);
    rt::host_options().detail_distance = 100;

    // the sky's depth: retail's 16380 by default whatever the far plane; with the option, the far plane when it is farther, and still 16380 at the game's own fog distances
    constexpr float kForced = 16380.0f;
    CHECK(!rt::host_options().sky_behind_yon);
    CHECK(rt::background_depth(kForced, 7024.0f * 1000.0f) == kForced && rt::background_depth(kForced, 10536.0f) == kForced);
    rt::host_options().sky_behind_yon = true;
    CHECK(rt::background_depth(kForced, 10536.0f) == kForced);              // fog 150: yon 7024 x 1.5
    CHECK(rt::background_depth(kForced, kForced) == kForced);
    CHECK(rt::background_depth(kForced, 19316.0f) == 19316.0f);             // fog 275
    CHECK(rt::background_depth(kForced, 7024.0f * 1000.0f) == 7024000.0f);   // fog off
    rt::host_options().sky_behind_yon = false;

    // a power-up's reach: the level's fog far distance unless the player set a fog distance, then that times the same scale as the far plane
    CHECK(rt::item_reach(8548) == 8548);
    rt::host_options().item_reach_scale = 1.0f;                             // fog 100
    CHECK(rt::item_reach(8548) == 8548);
    rt::host_options().item_reach_scale = 2.75f;                            // fog 275
    CHECK(rt::item_reach(8548) == 23507);
    rt::host_options().item_reach_scale = 64.0f;                            // fog off
    CHECK(rt::item_reach(8548) == 547072);
    CHECK(rt::item_reach(0xffffffffu) == 0xffffffffu);
    rt::host_options().item_reach_scale = 0.0f;

    // the areas a level's trigger boxes hide: by default nothing is remembered and every hidden object stays hidden
    CHECK(!rt::host_options().draw_hidden_areas);
    rt::note_area_visibility(0x054bf0d0, 0x41);
    CHECK(rt::host_options().area_hidden.empty());
    CHECK(!rt::hidden_only_by_area(0x054bf0d0, 0x41, 0x01));
    rt::host_options().draw_hidden_areas = true;
    rt::note_area_visibility(0x054bf0d0, 0x8041);                           // the cull's bit 15 left on from the last frame does not count
    CHECK(rt::hidden_only_by_area(0x054bf0d0, 0x41, 0x01));
    CHECK(rt::hidden_only_by_area(0x054bf0d0, 0x41, 0x40));                // the second skater's view
    CHECK(!rt::hidden_only_by_area(0x054bf0d0, 0x1041, 0x1001));           // another bit of the mask hides it too
    CHECK(!rt::hidden_only_by_area(0x054bf0d0, 0x61, 0x01));               // the word changed since the boxes wrote it (a goal's show and hide shares the bits)
    CHECK(!rt::hidden_only_by_area(0x054ab750, 0x41, 0x01));               // an object the boxes never hid
    rt::note_area_visibility(0x054bf0d0, 0x00);                             // the boxes show it again: forgotten
    CHECK(rt::host_options().area_hidden.empty());
    // a box round a single object (Hangar's chopper debris, hidden until the chopper comes through the roof) is not an area's: the game's rule stays
    CHECK(!rt::area_sized_box(3230, -5336, 3500, -5170));                   // Hangar node 658/629: the debris, 270 x 166
    CHECK(!rt::area_sized_box(2452, -8797, 2824, 1713));                    // Hangar node 497/498: a roof slab, 372 across, 10510 long
    CHECK(!rt::area_sized_box(-69849, 17247, -69649, 17447));               // NYC node 784: 200 x 200
    CHECK(rt::area_sized_box(1745, -9587, 2575, 3236));                     // Hangar node 618: the narrowest area, 830 across
    CHECK(rt::area_sized_box(-60030, 17310, -42373, 34584));                // NYC node 682: a city block
    CHECK(rt::area_sized_box(-2000000000, -2000000000, 2000000000, 2000000000));   // no overflow
    rt::note_area_visibility(0x054c33ac, 0x41, false);                      // hidden by an object's box: not remembered
    CHECK(rt::host_options().area_hidden.empty());
    CHECK(!rt::hidden_only_by_area(0x054c33ac, 0x41, 0x01));
    rt::note_area_visibility(0x054c33ac, 0x41);                             // an area's box hid it first, then an object's box hid it again: the game's rule
    rt::note_area_visibility(0x054c33ac, 0x41, false);
    CHECK(!rt::hidden_only_by_area(0x054c33ac, 0x41, 0x01));
    CHECK(rt::host_options().area_hidden.empty());
    rt::host_options().draw_hidden_areas = false;

    // the polygon arena: retail's by default; 4x when an option draws beyond retail; OT_POLY_BUFFER overrides either way, held to 1..16; a bad value is as if unset
    CHECK(rt::host_options().poly_buffer_scale == 1 && rt::host_options().poly_buffer_area_scale == 0);
    CHECK(rt::poly_buffer_scale_for(nullptr, false) == 1 && rt::poly_buffer_scale_for("", false) == 1);
    CHECK(rt::poly_buffer_scale_for(nullptr, true) == rt::kPolyBufferScaleAuto && rt::kPolyBufferScaleAuto == 4);
    CHECK(rt::poly_buffer_scale_for("1", true) == 1);
    CHECK(rt::poly_buffer_scale_for("8", false) == 8);
    CHECK(rt::poly_buffer_scale_for("0", true) == 1);
    CHECK(rt::poly_buffer_scale_for("16", false) == 16 && rt::poly_buffer_scale_for("17", false) == 16);
    CHECK(rt::poly_buffer_scale_for("99999999999999999999", false) == 16);   // no wrap
    CHECK(rt::poly_buffer_scale_for("x", true) == 4 && rt::poly_buffer_scale_for("-2", false) == 1 && rt::poly_buffer_scale_for("4x", false) == 1);
    // the largest area is a 32-bit size with room to spare: P1 measured buffers of 1.31 to 1.36 MB; even a 4 MB level extra at the most is under 2 GB
    CHECK(static_cast<std::uint64_t>(rt::kPolyBufferScaleMax) * (0x400000 + 0x145000) * 2 + rt::kPolyBufferPad < 0x80000000ull);

    if (g_failures == 0) {
        std::printf("host_options_test: ok\n");
    }
    return g_failures == 0 ? 0 : 1;
}
