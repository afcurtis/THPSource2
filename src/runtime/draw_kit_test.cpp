// The draw path on the retail memory model (milestone 7c): an ordering table cleared, a scene begun, a list of packets walked into a recording HAL backend, the
// frame presented, against a fake engine. What this pins is the Kit's reading of the disassembly (the draws, and the retail words the walk leaves); equivalence with
// retail is tony/standalone_frame.py (the stream and the memory image of the Options screen against the boot-menu session's).
#include "draw_kit.hpp"
#include "render_presentation.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::drawkit;

namespace {

int failures = 0;

void expect_eq(std::uint32_t actual, std::uint32_t expected, const char* what) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL: " << what << ": 0x" << std::hex << actual << " != 0x" << expected << std::dec << "\n";
    }
}

void expect_float(float actual, float expected, const char* what) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL: " << what << ": " << actual << " != " << expected << "\n";
    }
}

// (the HUD layout adds and multiplies in another order than the ungrouped one: the same place to a thousandth of a pixel)
void expect_near(float actual, float expected, const char* what) {
    if (!(actual > expected - 0.001f && actual < expected + 0.001f)) {
        ++failures;
        std::cerr << "FAIL: " << what << ": " << actual << " != " << expected << "\n";
    }
}

std::uint32_t bits(float value) {
    std::uint32_t out;
    std::memcpy(&out, &value, sizeof out);
    return out;
}

constexpr std::uint32_t kGlobals = 0x00510000;      // .rdata .. .bss: the level tables, the fog tables, the caches
constexpr std::uint32_t kGlobalsSize = 0x001a0000;
constexpr std::uint32_t kScreen = 0x029da000;       // the screen size and the software renderer word
constexpr std::uint32_t kHeap = 0x01000000;
constexpr std::uint32_t kHeapSize = 0x00010000;

class Engine final : public ServiceHost {
public:
    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string&) override {
        calls.push_back(name);
        if (name == "ddraw_hal_texture_for") {
            return args.empty() ? 0 : args[0] + 0x1000;   // the HAL texture of a surface
        }
        return 0;
    }
    std::vector<std::string> calls;
};

// ---- the recording backend ----
struct Draw {
    OtHalDrawState state;
    std::uint32_t topology;
    std::vector<OtHalVertex> vertices;
};
std::vector<Draw> g_draws;
std::vector<std::string> g_events;
std::vector<OtHalRect> g_viewports;
std::uint32_t g_clear_colour = 0;
struct Clear {
    std::uint32_t colour;
    bool has_area;
    OtHalRect area;
};
std::vector<Clear> g_clears;
std::uint32_t g_movie_texture = 0;   // what the program says the open clip's HAL texture is (0: none)
std::uint32_t movie_texture() { return g_movie_texture; }
std::vector<std::uint32_t> g_presented;

int OT_HAL_CALL no_init(const OtHalVideoMode*) { return 1; }
void OT_HAL_CALL no_void() {}
int OT_HAL_CALL no_mode(const OtHalVideoMode*) { return 1; }
std::uint32_t OT_HAL_CALL no_modes(OtHalVideoMode*, std::uint32_t) { return 0; }
OtHalHandle OT_HAL_CALL no_texture(const OtHalTextureDesc*) { return 0; }
void OT_HAL_CALL no_update(OtHalHandle, const OtHalRect*, const void*, std::int32_t) {}
void OT_HAL_CALL no_destroy(OtHalHandle) {}
OtHalHandle OT_HAL_CALL no_palette(std::uint32_t) { return 0; }
void OT_HAL_CALL no_palette_update(OtHalHandle, std::uint32_t, std::uint32_t, const std::uint32_t*) {}
void OT_HAL_CALL begin_frame() { g_events.push_back("begin"); }
void OT_HAL_CALL clear(std::uint32_t flags, std::uint32_t colour, float depth, const OtHalRect* area) {
    g_events.push_back(flags == (OT_HAL_CLEAR_COLOR | OT_HAL_CLEAR_DEPTH) && depth == 0.0f && area == nullptr ? "clear" : "clear?");
    g_clear_colour = colour;
    g_clears.push_back(Clear{colour, area != nullptr, area != nullptr ? *area : OtHalRect{0, 0, 0, 0}});
}
void OT_HAL_CALL viewport(const OtHalRect* area) { g_viewports.push_back(*area); }
void OT_HAL_CALL no_scissor(const OtHalRect*) {}
void OT_HAL_CALL draw(const OtHalDrawState* state, std::uint32_t topology, const OtHalVertex* vertices, std::uint32_t count) {
    g_draws.push_back(Draw{*state, topology, std::vector<OtHalVertex>(vertices, vertices + count)});
}
void OT_HAL_CALL no_sprite(const OtHalDrawState*, const OtHalRect*, const OtHalRect*, std::uint32_t) {}
void OT_HAL_CALL present() { g_events.push_back("present"); }
int OT_HAL_CALL no_read(OtHalImage*) { return 0; }

const OtHalVideoBackend kRecording = {"test", no_init, no_void, no_mode, no_modes, no_texture, no_update, no_destroy, no_palette, no_palette_update, no_destroy,
                                      begin_frame, clear, viewport, no_scissor, draw, no_sprite, present, no_read};

void on_present(std::uint32_t frame, void*) { g_presented.push_back(frame); }

struct Rig {
    RetailMemory memory;
    RetailServices services;
    Engine host;
    DrawKit kit;
    Rig() : kit(memory, services) {
        memory.add_region(kGlobals, kGlobalsSize);
        memory.add_region(kScreen, 0x1000);
        memory.add_region(kHeap, kHeapSize);
        services.attach(&host);
        // the colour levels pass each channel through, in its place
        for (std::uint32_t k = 0; k < 256; ++k) {
            memory.set32(kColorLevelRed + 4 * k, k << 16);
            memory.set32(kColorLevelGreen + 4 * k, k << 8);
            memory.set32(kColorLevelBlue + 4 * k, k);
        }
        memory.set32(kUnitScale, bits(1.0f));
        memory.set32(kScreenWidth, 640);
        memory.set32(kScreenHeight, 480);
        memory.set32(kViewportEdges, kHeap);       // the viewport edges: span (+0xa) 200, far edge (+0xe) 300
        memory.set16(kHeap + 0xa, 200);
        memory.set16(kHeap + 0xe, 300);
        memory.set32(kDpqMin, 50);
        memory.set32(kVideoFogYonScale, bits(2.0f));
        Output output;
        output.video = &kRecording;
        output.record = true;
        output.on_present = &on_present;
        output.movie_texture = &movie_texture;
        set_output(output);
    }
};

void ordering_table() {
    Rig rig;
    const std::uint32_t table = kHeap + 0x100;
    rig.memory.fill(table, 0xee, 4 * 8);
    rig.kit.clear_ordering_table(table, 4);
    expect_eq(rig.memory.u32(table), 0, "entry 0 links to nothing");
    expect_eq(rig.memory.u32(table + 4), 0, "entry 0's second word is cleared");
    expect_eq(rig.memory.u32(table + 8), table, "entry 1 links to entry 0");
    expect_eq(rig.memory.u32(table + 24), table + 16, "entry 3 links to entry 2");
}

void scene() {
    Rig rig;
    g_events.clear();
    rig.memory.set32(kSceneCount, 7);
    rig.memory.set32(kBoundTexture, 0x1234);
    rig.kit.start_scene(0x00102030, 0x40112233);
    expect_eq(rig.memory.u8(kSceneActive), 1, "the scene is active");
    expect_eq(rig.memory.u8(kSceneListDrawn), 0, "no list drawn yet");
    expect_eq(rig.memory.u32(kSceneCount), 8, "the scene count moves");
    expect_eq(rig.memory.u32(kFogThreshold), bits(50.0f / 200.0f), "the fog threshold is DpqMin over the far edge");
    expect_eq(rig.memory.u32(kFogYonScale), bits(2.0f), "the yon scale is the video's");
    expect_eq(rig.memory.u32(kBoundTexture), 0, "no texture is bound");
    expect_eq(rig.memory.u32(kDepthCache), 0xffffffffu, "the depth cache is unknown");
    expect_eq(rig.memory.u32(kColorBlendCache), 0, "the colour blend cache is reset");
    expect_eq(rig.memory.u32(kCacheA), 0, "cache A is reset");
    expect_eq(rig.memory.u8(kStagesSet), 1, "the stages are set");
    expect_eq(static_cast<std::uint32_t>(g_events.size()), 2, "begin and clear");
    expect_eq(g_events.size() == 2 && g_events[0] == "begin" && g_events[1] == "clear", 1, "begin, then a full colour and depth clear to 0");
    expect_eq(g_clear_colour, 0x40112233, "the clear colour at the device's levels, its alpha kept");
    rig.kit.start_scene(0, 0);
    expect_eq(rig.memory.u32(kSceneCount), 8, "a running scene does not begin again");
}

void walk() {
    Rig rig;
    g_draws.clear();
    g_viewports.clear();
    rig.kit.start_scene(0, 0);
    // a texture: record -> descriptor (+0x14; the surface at +0, the flags at +0x10, the size at +0x14) -> holder (+4) -> surface holder (+0x14) -> surface
    const std::uint32_t record = kHeap + 0x200, descriptor = kHeap + 0x240, holder = kHeap + 0x280, surface_holder = kHeap + 0x2c0;
    rig.memory.set32(record + 0x14, descriptor);
    rig.memory.set32(descriptor, 0x55);
    rig.memory.set32(descriptor + 4, holder);
    rig.memory.set8(descriptor + 0x10, 0x10);
    rig.memory.set16(descriptor + 0x14, 33);
    rig.memory.set16(descriptor + 0x16, 17);
    rig.memory.set32(holder + 0x14, surface_holder);
    rig.memory.set32(surface_holder, 0x55);
    // the list: FT4 (opaque, textured), F3 (semi-transparent, z below 0), a viewport, the end
    const std::uint32_t ft4 = kHeap + 0x400, f3 = kHeap + 0x500, area = kHeap + 0x600;
    rig.memory.set32(ft4, f3);
    rig.memory.set32(ft4 + 4, 0x00808080);   // colour; word 4 set
    rig.memory.set8(ft4 + 7, 0x2c);
    const std::int16_t xy[4][2] = {{10, 20}, {50, 20}, {10, 60}, {50, 60}};
    const std::uint8_t xy_at[4] = {0x08, 0x10, 0x18, 0x20};
    for (int k = 0; k < 4; ++k) {
        rig.memory.set16(ft4 + xy_at[k], static_cast<std::uint16_t>(xy[k][0]));
        rig.memory.set16(ft4 + xy_at[k] + 2, static_cast<std::uint16_t>(xy[k][1]));
    }
    rig.memory.set8(ft4 + 0x14, 32);         // u of vertex 1
    rig.memory.set32(ft4 + 0x28, bits(0.5f));
    rig.memory.set32(ft4 + 0x2c, record);
    rig.memory.set32(f3, area);
    rig.memory.set32(f3 + 4, 0x00ff0000);
    rig.memory.set8(f3 + 7, 0x22);
    rig.memory.set32(f3 + 0x14, bits(-1.0f));
    rig.memory.set32(area, 0);
    rig.memory.set32(area + 4, 0xe3000000u);
    rig.memory.set8(area + 7, 0xe3);
    rig.memory.set16(area + 8, 256);
    rig.memory.set16(area + 10, 300);         // wraps to 44
    rig.memory.set16(area + 0xc, 256);
    rig.memory.set16(area + 0xe, 120);
    rig.memory.set32(kTexpageBlendMode, 1);
    const std::uint32_t bindings = rig.memory.u32(kBindings);
    rig.kit.draw_polygon_list(ft4);

    expect_eq(rig.memory.u8(kSceneListDrawn), 1, "the list is drawn");
    expect_eq(static_cast<std::uint32_t>(g_draws.size()), 2, "two draws");
    if (g_draws.size() == 2) {
        const Draw& quad = g_draws[0];
        expect_eq(quad.state.texture, 0x55 + 0x1000, "the quad draws with the surface's HAL texture");
        expect_eq(quad.topology, OT_HAL_TRIANGLE_FAN, "a quad is a fan");
        expect_eq(quad.state.flags, OT_HAL_DRAW_BLEND | OT_HAL_DRAW_MODULATE | OT_HAL_DRAW_FILTER | OT_HAL_DRAW_DEPTH_GREATER | OT_HAL_DRAW_PERSPECTIVE |
                                        OT_HAL_DRAW_DEPTH_TEST | OT_HAL_DRAW_DEPTH_WRITE,
                  "opaque, textured, depth tested and written");
        expect_float(quad.vertices[2].x, 50.0f, "fan order 0, 1, 3, 2");
        expect_float(quad.vertices[1].u, 32.0f / 32.0f, "u scaled by the width less one");
        expect_eq(quad.vertices[0].color, 0xff808080u, "the colour at the levels, opaque");
        expect_float(quad.vertices[0].z, 0.5f, "the packet's z");
        const Draw& triangle = g_draws[1];
        expect_eq(triangle.state.texture, OT_HAL_INVALID_HANDLE, "the triangle is untextured");
        expect_eq(triangle.state.flags & (OT_HAL_DRAW_DEPTH_TEST | OT_HAL_DRAW_DEPTH_WRITE), 0, "z below 0: no depth test");
        expect_float(triangle.vertices[0].z, 1.0f, "z below 0 draws at 1");
        expect_eq(triangle.vertices[0].color, 0xff0000ffu, "texpage mode 1: opaque alpha, blue in the packet's first byte position");
        expect_eq(triangle.state.dst_factor, OT_HAL_FACTOR_ONE, "mode 1 adds");
    }
    expect_eq(static_cast<std::uint32_t>(g_viewports.size()), 1, "one viewport");
    if (!g_viewports.empty()) {
        expect_eq(static_cast<std::uint32_t>(g_viewports[0].x), 320, "x scaled by the screen");
        expect_eq(static_cast<std::uint32_t>(g_viewports[0].y), 88, "the wrapped top scaled");
    }
    expect_eq(rig.memory.u16(area + 10), 44, "the wrapped top goes back into the packet");
    expect_eq(rig.memory.u32(kPrimitiveCount), 2, "both polygons counted");
    expect_eq(rig.memory.u32(kTextureDescriptor), descriptor, "the descriptor word");
    expect_eq(rig.memory.u32(kBoundTexture), surface_holder, "the surface holder is bound");
    expect_eq(rig.memory.u32(kBindings), bindings + 1, "one binding");
    expect_eq(rig.memory.u32(kCacheA), 1, "the last primitive was untextured");
    expect_eq(rig.memory.u32(kCacheG), 0, "cache G follows it");
    expect_eq(rig.memory.u32(kDepthCache), 0, "z below 0 turned the depth test off");
    expect_eq(rig.memory.u32(kPolyAlpha), 0xff000000u, "the alpha of the last packet");
    expect_eq(rig.memory.u32(kPacketFlags), 0x18, "semi-transparent in mode 1");
    expect_eq(stream_records(), 3, "the stream has two draws and a viewport");
    const std::vector<std::uint8_t> file = stream_file(9);
    expect_eq(file.size() >= 16 && std::memcmp(file.data(), "OTVH", 4) == 0, 1, "an .otvh file");
    // 8d: the D3DTLVERTEX staging buffers hold the last primitive of each kind (found by tony/draw_oracle.py): the quad in the 28-byte textured entries in fan order, the
    // triangle in the 20-byte untextured ones; the fourth word of an entry is never written
    expect_eq(rig.memory.u32(kStagingTextured), bits(10.0f), "the quad's first vertex is staged: x");
    expect_eq(rig.memory.u32(kStagingTextured + 4), bits(20.0f), "y");
    expect_eq(rig.memory.u32(kStagingTextured + 8), bits(0.5f), "z");
    expect_eq(rig.memory.u32(kStagingTextured + 0xc), 0, "the reciprocal depth word is not written");
    expect_eq(rig.memory.u32(kStagingTextured + 0x10), 0xff808080u, "the colour");
    expect_eq(rig.memory.u32(kStagingTextured + 0x1c * 2), bits(50.0f), "entry 2 is the packet's fourth vertex (fan order)");
    expect_eq(rig.memory.u32(kStagingTextured + 0x1c + 0x14), bits(32.0f / 32.0f), "u of entry 1");
    expect_eq(rig.memory.u32(kStagingPlain + 8), bits(1.0f), "the triangle's z below 0 is staged as 1");
    expect_eq(rig.memory.u32(kStagingPlain + 0x10), 0xff0000ffu, "its colour");
}

// A closed line strip (LINE_F3) ends on its first point, which retail copies as a whole entry: the word nobody writes included.
void line_strip_staging() {
    Rig rig;
    rig.kit.start_scene(0, 0);
    const std::uint32_t packet = kHeap + 0x400;
    rig.memory.set32(kStagingPlain + 0xc, 0xabcd1234u);        // the stale word of entry 0
    rig.memory.set32(packet, 0);
    rig.memory.set32(packet + 4, 0x00112233);
    rig.memory.set8(packet + 7, 0x48);
    const std::uint8_t xy_at[3] = {0x08, 0x0c, 0x10};
    for (int k = 0; k < 3; ++k) {
        rig.memory.set16(packet + xy_at[k], static_cast<std::uint16_t>(10 + k));
        rig.memory.set16(packet + xy_at[k] + 2, static_cast<std::uint16_t>(20 + k));
    }
    rig.memory.set32(packet + 0x18, bits(1.0f));
    rig.kit.draw_polygon_list(packet);
    expect_eq(rig.memory.u32(kStagingPlain), bits(10.0f), "entry 0 is the first point");
    expect_eq(rig.memory.u32(kStagingPlain + 0x14 * 2), bits(12.0f), "entry 2 is the third point");
    expect_eq(rig.memory.u32(kStagingPlain + 0x14 * 3), bits(10.0f), "entry 3 is the first point again");
    expect_eq(rig.memory.u32(kStagingPlain + 0x14 * 3 + 0xc), 0xabcd1234u, "with its stale fourth word");
}

// A general polygon (0xB0) whose texture record has no descriptor: retail reads [descriptor + 0x10] and faults (0x004d6ef5); the port skips it
// (seen in play on the level select, 2026-11-16) instead of reading guest address 0.
void general_polygon_without_descriptor() {
    Rig rig;
    rig.kit.start_scene(0, 0);
    const std::uint32_t packet = kHeap + 0x400;
    const std::uint32_t record = kHeap + 0x800;
    rig.memory.set32(packet, 0);
    rig.memory.set32(packet + 4, 0x00112233);   // the walk skips a packet whose second word is 0
    rig.memory.set8(packet + 7, 0xb0);
    rig.memory.set32(packet + 8, 0x80000040);   // no fog: a fogged-out polygon returns as faded before it reaches the texture
    rig.memory.set32(packet + 0x10, record);
    rig.memory.set32(packet + 0x14, 3);
    rig.memory.set32(record + 0x14, 0);   // no descriptor
    const std::uint32_t before = counters().no_texture;
    const std::uint32_t draws = counters().draws;
    rig.kit.draw_polygon_list(packet);
    expect_eq(counters().no_texture, before + 1, "the polygon is counted as having no texture");
    expect_eq(counters().draws, draws, "and nothing is drawn");
}

// The level select's video box: the movie's record (0x006a69f8) has no descriptor, the program has the clip in a HAL texture: the quad shows it (a HAL handle, not a game
// surface word: no translation), and a clip that is not open skips the polygon as before.
void movie_quad() {
    Rig rig;
    rig.kit.start_scene(0, 0);
    const std::uint32_t packet = kHeap + 0x400;
    rig.memory.set32(packet, 0);
    rig.memory.set32(packet + 4, 0x00112233);
    rig.memory.set8(packet + 7, 0xb0);
    rig.memory.set32(packet + 8, 0x80000040);
    rig.memory.set32(packet + 0x10, kMovieRecord);
    rig.memory.set32(packet + 0x14, 4);
    rig.memory.set32(kMovieRecord + 0x14, 0);
    g_draws.clear();
    g_movie_texture = 0;
    const std::uint32_t skipped = counters().no_texture;
    rig.kit.draw_polygon_list(packet);
    expect_eq(counters().no_texture, skipped + 1, "no clip open: skipped");
    g_movie_texture = 77;
    rig.kit.draw_polygon_list(packet);
    g_movie_texture = 0;
    expect_eq(counters().no_texture, skipped + 1, "a clip open: not skipped");
    expect_eq(static_cast<std::uint32_t>(g_draws.size()), 1, "and drawn");
    expect_eq(g_draws.empty() ? 0 : g_draws.back().state.texture, 77, "with the clip's HAL texture as it is");
}

// A wide screen with a 4:3 2D layer: a scene after one that drew only the 2D layer clears black beside the middle (the game's colour fills the middle); after a scene with a
// projected draw the colour fills the whole width.
void dark_sides() {
    Rig rig;
    Presentation wide;
    wide.screen_width = 854;
    wide.hud_width = 640;
    set_presentation(wide);
    g_clears.clear();
    rig.kit.start_scene(0, 0x00ff8040);
    expect_eq(static_cast<std::uint32_t>(g_clears.size()), 2, "2D scene before: the sides, then the middle");
    if (g_clears.size() == 2) {
        expect_eq(g_clears[0].colour, 0xff000000u, "the whole target black first");
        expect_eq(g_clears[0].has_area, 0, "(all of it)");
        expect_eq(g_clears[1].has_area, 1, "then the middle");
        expect_eq(static_cast<std::uint32_t>(g_clears[1].area.x), 107, "from (854 - 640) / 2");
        expect_eq(static_cast<std::uint32_t>(g_clears[1].area.w), 640, "640 wide");
        expect_eq(static_cast<std::uint32_t>(g_clears[1].area.h), 480, "the screen's height");
    }
    // a projected draw: a general polygon whose reciprocal depths are not 1
    const std::uint32_t packet = kHeap + 0x400;
    rig.memory.set32(packet, 0);
    rig.memory.set32(packet + 4, 0x00112233);
    rig.memory.set8(packet + 7, 0xb0);
    rig.memory.set32(packet + 8, 0x80000040);
    rig.memory.set32(packet + 0x10, 0);
    rig.memory.set32(packet + 0x14, 3);
    for (std::uint32_t k = 0; k < 3; ++k) {
        rig.memory.set32(packet + 0x18 + 20 * k + 12, bits(0.5f));
    }
    rig.kit.draw_polygon_list(packet);
    rig.memory.set8(kSceneActive, 0);
    g_clears.clear();
    rig.kit.start_scene(0, 0x00ff8040);
    expect_eq(static_cast<std::uint32_t>(g_clears.size()), 1, "a scene after a projected one: one clear");
    expect_eq(g_clears.empty() ? 1 : g_clears[0].has_area, 0, "of the whole width");
    // and a 2D-only scene again
    rig.memory.set8(kSceneActive, 0);
    g_clears.clear();
    rig.kit.start_scene(0, 0x00ff8040);
    expect_eq(static_cast<std::uint32_t>(g_clears.size()), 2, "no draw in between: black sides again");
    set_presentation(Presentation{});
}

// A wide screen with a 4:3 2D layer: a model polygon of a part-width view (the player select's skater, view edges 575..791 of 854) is drawn moved as far as the view's
// centre moves into the 4:3 middle (683 -> 619.5); a polygon nobody told, or one of a view across the whole width, stays; after a present the polygon is forgotten.
void view_polygon() {
    Rig rig;
    Presentation wide;
    wide.screen_width = 854;
    wide.hud_width = 640;
    set_presentation(wide);
    rig.kit.start_scene(0, 0);
    const std::uint32_t packet = kHeap + 0x400;
    rig.memory.set32(packet, 0);
    rig.memory.set32(packet + 4, 0x00112233);
    rig.memory.set8(packet + 7, 0xb0);
    rig.memory.set32(packet + 8, 0x80000040);
    rig.memory.set32(packet + 0x10, 0);
    rig.memory.set32(packet + 0x14, 3);
    for (std::uint32_t k = 0; k < 3; ++k) {
        rig.memory.set32(packet + 0x18 + 20 * k, bits(680.0f));
        rig.memory.set32(packet + 0x18 + 20 * k + 12, bits(0.5f));
    }
    g_draws.clear();
    rig.kit.draw_polygon_list(packet);
    expect_float(g_draws.empty() ? 0.0f : g_draws.back().vertices[0].x, 680.0f, "an untold polygon stays");
    presentation_view_polygon(packet, 0, 854);
    rig.memory.set8(kSceneListDrawn, 0);
    rig.kit.draw_polygon_list(packet);
    expect_float(g_draws.back().vertices[0].x, 680.0f, "a view across the whole width stays");
    presentation_view_polygon(packet, 575, 791);
    rig.memory.set8(kSceneListDrawn, 0);
    rig.kit.draw_polygon_list(packet);
    expect_float(g_draws.back().vertices[0].x, 680.0f + (683.0f * 640.0f / 854.0f + 107.0f - 683.0f), "a part-width view's polygon follows the view's centre");
    expect_eq(rig.memory.u32(packet + 0x18), bits(680.0f), "the packet itself is untouched");
    rig.kit.present_frame();
    rig.memory.set8(kSceneActive, 0);
    rig.kit.start_scene(0, 0);
    rig.kit.draw_polygon_list(packet);
    expect_float(g_draws.back().vertices[0].x, 680.0f, "forgotten after the present");
    set_presentation(Presentation{});
    presentation_view_polygon(packet, 575, 791);
    rig.memory.set8(kSceneListDrawn, 0);
    rig.kit.draw_polygon_list(packet);
    expect_float(g_draws.back().vertices[0].x, 680.0f, "no presentation: nothing kept");
}

// The in-level HUD's layout (host_options.hpp): an element Panel::display recorded is placed about a point of its own. On a 854 x 480 screen with a 4:3 2D layer (the factor
// 640 / 854 on x), a rectangle of the 2D layer 40 x 20 at (left, top):
//   not anchored: as the ungrouped layout puts it (its point is carried to the 4:3 middle);
//   anchored, in the left third at the top: held to the left edge and the top edge (so x' = x * factor, y' = y), then inset by the margin and sized by the scale;
//   anchored, in the right third: held to the right edge; in the middle: where the ungrouped layout puts it.
void hud_layout() {
    Rig rig;
    Presentation wide;
    wide.screen_width = 854;
    wide.hud_width = 640;
    set_presentation(wide);
    rig.memory.set32(kScreenHeight, 480);
    rig.kit.start_scene(0, 0);
    const std::uint32_t packet = kHeap + 0x400;
    const float factor = 640.0f / 854.0f;
    const auto draw = [&](float left, float top, float fx, float fy, bool anchor, float scale, float margin) {
        rig.memory.set32(packet, 0);
        rig.memory.set32(packet + 4, 0x00112233);
        rig.memory.set8(packet + 7, 0xb0);
        rig.memory.set32(packet + 8, 0x80000040);
        rig.memory.set32(packet + 0x10, 0);
        rig.memory.set32(packet + 0x14, 4);
        for (std::uint32_t k = 0; k < 4; ++k) {
            rig.memory.set32(packet + 0x18 + 20 * k, bits(left + 40.0f * static_cast<float>(k & 1)));
            rig.memory.set32(packet + 0x18 + 20 * k + 4, bits(top + 20.0f * static_cast<float>(k >> 1)));
            rig.memory.set32(packet + 0x18 + 20 * k + 12, bits(1.0f));
        }
        HostOptions& options = host_options();
        options.hud_anchor = anchor;
        options.hud_scale = scale;
        options.hud_margin = margin;
        HostOptions::HudGroup group;
        group.begin = packet;
        group.end = packet + 0x100;
        group.fx = fx;
        group.fy = fy;
        options.hud_groups.assign(1, group);
        rig.memory.set8(kSceneListDrawn, 0);
        g_draws.clear();
        rig.kit.draw_polygon_list(packet);
        options = HostOptions{};
        return g_draws.empty() ? OtHalVertex{} : g_draws.back().vertices[0];
    };
    OtHalVertex v = draw(20.0f, 10.0f, 0.05f, 0.05f, false, 1.0f, 0.0f);
    expect_near(v.x, 20.0f * factor + 107.0f, "not anchored: the ungrouped place (x)");
    expect_near(v.y, 10.0f, "(y)");
    v = draw(20.0f, 10.0f, 0.05f, 0.05f, true, 1.0f, 0.0f);
    expect_near(v.x, 20.0f * factor, "a left element hugs the left edge");
    expect_near(v.y, 10.0f, "and keeps its height at the top");
    v = draw(800.0f, 10.0f, 0.9f, 0.05f, true, 1.0f, 0.0f);
    expect_near(v.x, 854.0f + (800.0f - 854.0f) * factor, "a right element hugs the right edge");
    v = draw(400.0f, 10.0f, 0.5f, 0.05f, true, 1.0f, 0.0f);
    expect_near(v.x, 400.0f * factor + 107.0f, "a middle element stays where the ungrouped layout puts it");
    v = draw(20.0f, 10.0f, 0.05f, 0.05f, true, 0.5f, 0.05f);
    expect_near(v.x, 0.05f * 854.0f + 20.0f * factor * 0.5f, "margin and scale: inset from the left edge, half the size about it (x)");
    expect_near(v.y, 0.05f * 480.0f + 10.0f * 0.5f, "(y, from the top edge)");
    v = draw(20.0f, 440.0f, 0.05f, 0.95f, true, 0.5f, 0.05f);
    expect_near(v.y, 480.0f - 0.05f * 480.0f + (440.0f - 480.0f) * 0.5f, "a bottom element hugs the bottom edge");
    // a polygon that is not in a recorded element is laid out as it always was
    set_presentation(wide);
    rig.memory.set32(packet + 0x18, bits(20.0f));
    rig.memory.set8(kSceneListDrawn, 0);
    g_draws.clear();
    host_options().hud_anchor = true;
    rig.kit.draw_polygon_list(packet);
    host_options() = HostOptions{};
    expect_near(g_draws.empty() ? 0.0f : g_draws.back().vertices[0].x, 20.0f * factor + 107.0f, "no recorded element: the ungrouped layout");
    // the layout of a picture that is only 4:3 (the margin and the size apply there too)
    Presentation plain;
    plain.screen_width = 640;
    plain.hud_width = 640;
    set_presentation(plain);
    v = draw(20.0f, 10.0f, 0.05f, 0.05f, true, 1.0f, 0.05f);
    expect_near(v.x, 0.05f * 640.0f + 20.0f, "4:3: the margin from the edge (x)");
    set_presentation(Presentation{});
}

void presenting() {
    Rig rig;
    g_events.clear();
    g_presented.clear();
    rig.kit.start_scene(0, 0);
    rig.kit.present_frame();
    expect_eq(g_events.size() == 3 && g_events[2] == "present", 1, "the HAL presents");
    expect_eq(static_cast<std::uint32_t>(g_presented.size()), 1, "the program hears of it");
    bool ended = false;
    for (const std::string& name : rig.host.calls) {
        ended = ended || name == "engine_004d40d0";
    }
    expect_eq(ended, 1, "D3DPOLY_EndScene first");
}

void software_renderer() {
    Rig rig;
    rig.memory.set32(kSoftwareRenderer, 1);
    bool stopped = false;
    try {
        rig.kit.start_scene(0, 0);
    } catch (const std::exception&) {
        stopped = true;
    }
    expect_eq(stopped, 1, "the software renderer stops the program");
}

}  // namespace

int main() {
    ordering_table();
    scene();
    walk();
    line_strip_staging();
    general_polygon_without_descriptor();
    movie_quad();
    dark_sides();
    view_polygon();
    hud_layout();
    presenting();
    software_renderer();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "draw_kit: ok\n";
    return 0;
}
