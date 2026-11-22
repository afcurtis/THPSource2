// Deterministic checks for the retail-faithful model submission chain that the retail trace replay
// (render_model_retail_trace_test) cannot pin down by itself: the ordering table walk, the
// software-renderer early exit, arena exhaustion and a hand-built quad through the whole chain.
#include "../../tests/test_check.hpp"
#include "render_model_submit.hpp"

#include <bit>
#include <cstring>
#include <map>
#include <vector>

using namespace opentony::trg;

namespace {

void put16(std::vector<std::uint8_t>& out, std::size_t offset, std::uint16_t value) {
    out[offset] = static_cast<std::uint8_t>(value & 0xff);
    out[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

RetailRenderGlobals screen_globals() {
    RetailRenderGlobals g;
    g.viewport = {640, 480, 0, 0, 10, 4000, 384, 320, 240};
    // Identity rotation (1.0 == 4096), no translation.
    g.matrix_a = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
    g.camera_matrix = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
    g.selector_source = 0x0000'0880U;  // solid ramp-colored face, depth mode 0
    return g;
}

// One-face model: four vertices and a solid quad whose corners are the vertices in order.
std::vector<std::uint8_t> solid_quad_packet(const std::array<std::array<std::int16_t, 3>, 4>& v) {
    std::vector<std::uint8_t> packet(0x1c + 4 * 8 + 0x20, 0);
    put16(packet, 2, 4);   // vertex count
    put16(packet, 4, 0);   // normal count
    put16(packet, 6, 1);   // face count
    for (std::size_t i = 0; i < 4; ++i) {
        put16(packet, 0x1c + i * 8 + 0, static_cast<std::uint16_t>(v[i][0]));
        put16(packet, 0x1c + i * 8 + 2, static_cast<std::uint16_t>(v[i][1]));
        put16(packet, 0x1c + i * 8 + 4, static_cast<std::uint16_t>(v[i][2]));
    }
    const std::size_t face = 0x1c + 4 * 8;
    put16(packet, face + 0, 0xffff);  // per-face selector mask
    put16(packet, face + 2, 0x20);    // face length
    packet[face + 4] = 0;
    packet[face + 5] = 1;
    packet[face + 6] = 2;
    packet[face + 7] = 3;
    packet[face + 8] = 0;   // face color word: ramp indices 0, 1, 2, 3
    packet[face + 9] = 1;
    packet[face + 10] = 2;
    packet[face + 11] = 3;
    return packet;
}

void test_ordering_table_walks_far_to_near() {
    RetailOrderingTable table(0x0100'0000U, 8);
    // Fresh table: entry i links to entry i-1, entry 0 is the terminator.
    CHECK(table.raw_entry(0) == 0);
    CHECK(table.head(0) == table.entry_address(1));  // bucket 0 is entry 2, linked to entry 1
    CHECK(table.head(5) == table.entry_address(6));

    std::map<std::uint32_t, std::uint32_t> next;
    const auto add = [&](std::size_t bucket, std::uint32_t polygon) {
        next[polygon] = table.prepend(bucket, polygon);
    };
    add(3, 0x1000);
    add(3, 0x1100);  // same bucket: most recent first
    add(6, 0x2000);
    add(0, 0x3000);
    const auto order = table.draw_order([&](std::uint32_t polygon) { return next.at(polygon); });
    const std::vector<std::uint32_t> expected{0x2000, 0x1100, 0x1000, 0x3000};
    CHECK(order == expected);

    table.clear();
    CHECK(table.draw_order([&](std::uint32_t polygon) { return next.at(polygon); }).empty());
}

void test_software_renderer_early_exit() {
    RetailRenderGlobals g = screen_globals();
    g.software_renderer = 1;
    RetailModelRenderer renderer(g, 0x4000, 0x4000 + 0xc0 * 8);
    const auto packet = solid_quad_packet({{{-50, -50, 400}, {50, -50, 400}, {50, 50, 400}, {-50, 50, 400}}});
    RetailSubmitInputs inputs;
    inputs.packet = packet;
    inputs.flags = 0x10;  // state bit 0x10 leaves early below software level 2
    CHECK(!renderer.submit(inputs));
    CHECK(renderer.arena_cursor() == 0x4000);
}

void test_arena_exhaustion_is_reported() {
    RetailModelRenderer renderer(screen_globals(), 0x4000, 0x4000);
    const auto packet = solid_quad_packet({{{-50, -50, 400}, {50, -50, 400}, {50, 50, 400}, {-50, 50, 400}}});
    RetailSubmitInputs inputs;
    inputs.packet = packet;
    bool threw = false;
    try {
        renderer.submit(inputs);
    } catch (const RetailRenderError&) {
        threw = true;
    }
    CHECK(threw);
}

void test_quad_through_the_chain() {
    RetailRenderGlobals g = screen_globals();
    const auto packet = solid_quad_packet({{{-50, -50, 400}, {50, -50, 400}, {50, 50, 400}, {-50, 50, 400}}});
    RetailSubmitInputs inputs;
    inputs.packet = packet;
    // Ramp path (selector bit 0x800): corner i takes ramp[byte i of the face color word].
    inputs.ramp_color = [](std::uint32_t index) { return 0x10U * (index + 1); };
    RetailModelRenderer renderer(g, 0x4000, 0x4000 + 0xc0 * 8);
    std::vector<RetailRenderEvent> events;
    renderer.set_sink([&](const RetailRenderEvent& event) { events.push_back(event); });
    CHECK(renderer.submit(inputs));

    // x' = 384 / 400 * x + 320, y' = 384 / 400 * y + 240, z = 400, reciprocal = 384 / 400.
    const auto& heap = renderer.vertex_heap();
    const float recip = 384.0F / 400.0F;
    const float scaled = recip * -50.0F;
    CHECK(std::bit_cast<float>(heap[0][0]) == scaled + 320.0F);
    CHECK(std::bit_cast<float>(heap[0][2]) == 400.0F);
    CHECK(std::bit_cast<float>(heap[0][3]) == recip);
    CHECK(heap[0][5] == 0);  // fully on screen: no clip bits

    // One solid quad: build, clip_bucket, classify (mode 0, no video), end, build_end.
    const RetailRenderEvent::Kind expected[] = {
        RetailRenderEvent::Kind::build, RetailRenderEvent::Kind::clip_bucket,
        RetailRenderEvent::Kind::classify, RetailRenderEvent::Kind::classify_end,
        RetailRenderEvent::Kind::clip_bucket_end, RetailRenderEvent::Kind::build_end};
    CHECK(events.size() == std::size(expected));
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK(events[i].kind == expected[i]);
    }
    const auto& polygon = renderer.slot(0x4000);
    CHECK((polygon[1] >> 24) == 0xb0);
    CHECK(polygon[5] == 4);
    CHECK(renderer.arena_cursor() == 0x4000 + 0xc0);
    // The quad vertex order is 0, 1, 3, 2 and each corner carries the matching color.
    CHECK(polygon[6 + 4] == 0xff100000U);
    CHECK(polygon[11 + 4] == 0xff200000U);
    CHECK(polygon[16 + 4] == 0xff400000U);
    CHECK(polygon[21 + 4] == 0xff300000U);
    // z = 400 (mode 0): bucket = trunc(400 + 0.5) >> 2 = 100, and the polygon heads it.
    CHECK(events[3].a == 100);
    CHECK(renderer.bucket_head(100) == 0x4000);
}

} // namespace

int main() {
    test_ordering_table_walks_far_to_near();
    test_software_renderer_early_exit();
    test_arena_exhaustion_is_reported();
    test_quad_through_the_chain();
    return 0;
}
