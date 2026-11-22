#include "auto_views.hpp"
#include "draw_kit.hpp"

#include "host_options.hpp"
#include "math_kit.hpp"
#include "render_presentation.hpp"

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace opentony::runtime::drawkit {

using namespace opentony::runtime::autoviews;

namespace {

using mathkit::x87::Precision;
using mathkit::x87::round_to;

constexpr std::uint32_t kSetupFog = 0x004d3f50;     // D3D_SetupFog(colour): the four fog tables
constexpr std::uint32_t kEndScene = 0x004d40d0;     // D3DPOLY_EndScene()

enum RecordType : std::uint8_t { kDraw = 1, kViewport = 2, kUnsupported = 3 };
enum Unsupported : std::uint32_t { kUnknownPacket = 1, kOutline = 3 };
enum DepthMode { kDepthOff = 0, kDepthTest = 1, kDepthWrite = 2 };

Output g_output;
Presentation g_presentation;
std::vector<std::uint8_t> g_stream;
std::uint32_t g_records = 0;
Counters g_counters;

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int k = 0; k < 4; ++k) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * k)));
    }
}

void record(std::uint8_t type, const void* payload, std::size_t size, const void* tail = nullptr, std::size_t tail_size = 0) {
    if (!g_output.record) {
        return;
    }
    g_stream.push_back(type);
    g_stream.push_back(0);
    g_stream.push_back(0);
    g_stream.push_back(0);
    put32(g_stream, static_cast<std::uint32_t>(size + tail_size));
    const auto* bytes = static_cast<const std::uint8_t*>(payload);
    g_stream.insert(g_stream.end(), bytes, bytes + size);
    if (tail_size != 0) {
        const auto* more = static_cast<const std::uint8_t*>(tail);
        g_stream.insert(g_stream.end(), more, more + tail_size);
    }
    ++g_records;
}

// D3D_SetColorBlendMode(mode) as HAL factors (alpha blending is always on in retail)
void colour_blend(OtHalDrawState& state, int mode) {
    state.flags |= OT_HAL_DRAW_BLEND;
    state.blend_op = OT_HAL_BLEND_OP_ADD;
    switch (mode) {
    case 1: state.src_factor = OT_HAL_FACTOR_SRC_ALPHA; state.dst_factor = OT_HAL_FACTOR_INV_SRC_ALPHA; break;
    case 2: state.src_factor = OT_HAL_FACTOR_SRC_ALPHA; state.dst_factor = OT_HAL_FACTOR_ONE; break;
    case 4: state.src_factor = OT_HAL_FACTOR_ZERO; state.dst_factor = OT_HAL_FACTOR_INV_SRC_COLOR; break;
    case 6: state.src_factor = OT_HAL_FACTOR_ONE; state.dst_factor = OT_HAL_FACTOR_ONE; break;
    default: state.src_factor = OT_HAL_FACTOR_ONE; state.dst_factor = OT_HAL_FACTOR_ZERO; break;
    }
}

void depth(OtHalDrawState& state, int mode) {
    state.flags |= OT_HAL_DRAW_DEPTH_GREATER | OT_HAL_DRAW_PERSPECTIVE;    // device-wide in retail
    if (mode != kDepthOff) {
        state.flags |= OT_HAL_DRAW_DEPTH_TEST;
    }
    if (mode == kDepthWrite) {
        state.flags |= OT_HAL_DRAW_DEPTH_WRITE;
    }
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// PSX packets with the PC extension (a float z after the vertices, then for textured kinds a texture record), as the D3D_Draw* functions read them: vertex, colour
// and texture-coordinate offsets in submission order (quads become fans 0, 1, 3, 2; line strips repeat their first point). video_shadow.cpp's table.
struct PsxLayout {
    std::uint8_t kind;
    std::uint8_t count;
    std::uint8_t topology;
    std::uint8_t z;
    std::uint8_t texture;   // 0 = untextured
    bool guard;             // FUN_004d44d0's size check
    bool counted;           // the handler counts the polygon (0x006a0068); the line handlers do not
    std::uint8_t xy[5];
    std::uint8_t rgb[5];    // per vertex; flat kinds repeat 4
    std::uint8_t uv[5];
};

constexpr PsxLayout kLayouts[] = {
    {0x20, 3, OT_HAL_TRIANGLES, 0x14, 0, true, true, {0x08, 0x0c, 0x10}, {4, 4, 4}, {0}},                                     // F3
    {0x24, 3, OT_HAL_TRIANGLES, 0x20, 0x24, true, true, {0x08, 0x10, 0x18}, {4, 4, 4}, {0x0c, 0x14, 0x1c}},                     // FT3
    {0x28, 4, OT_HAL_TRIANGLE_FAN, 0x18, 0, true, true, {0x08, 0x0c, 0x14, 0x10}, {4, 4, 4, 4}, {0}},                           // F4
    {0x2c, 4, OT_HAL_TRIANGLE_FAN, 0x28, 0x2c, true, true, {0x08, 0x10, 0x20, 0x18}, {4, 4, 4, 4}, {0x0c, 0x14, 0x24, 0x1c}},    // FT4
    {0x30, 3, OT_HAL_TRIANGLES, 0x1c, 0, true, true, {0x08, 0x10, 0x18}, {4, 0x0c, 0x14}, {0}},                                  // G3
    {0x34, 3, OT_HAL_TRIANGLES, 0x28, 0x2c, true, true, {0x08, 0x14, 0x20}, {4, 0x10, 0x1c}, {0x0c, 0x18, 0x24}},                // GT3
    {0x38, 4, OT_HAL_TRIANGLE_FAN, 0x24, 0, true, true, {0x08, 0x10, 0x20, 0x18}, {4, 0x0c, 0x1c, 0x14}, {0}},                   // G4
    {0x3c, 4, OT_HAL_TRIANGLE_FAN, 0x34, 0x38, true, true, {0x08, 0x14, 0x2c, 0x20}, {4, 0x10, 0x28, 0x1c}, {0x0c, 0x18, 0x30, 0x24}},   // GT4
    {0x40, 2, OT_HAL_LINES, 0x10, 0, false, false, {0x08, 0x0c}, {4, 4}, {0}},                                                   // LINE_F2
    {0x48, 4, OT_HAL_LINE_STRIP, 0x18, 0, false, false, {0x08, 0x0c, 0x10, 0x08}, {4, 4, 4, 4}, {0}},                            // LINE_F3
    {0x4c, 5, OT_HAL_LINE_STRIP, 0x1c, 0, false, false, {0x08, 0x0c, 0x14, 0x10, 0x08}, {4, 4, 4, 4, 4}, {0}},                   // LINE_F4
    {0x50, 2, OT_HAL_LINES, 0x14, 0, false, false, {0x08, 0x10}, {4, 0x0c}, {0}},                                                // LINE_G2
};
constexpr std::uint32_t kLayoutCount = sizeof kLayouts / sizeof kLayouts[0];

// Retail's oversize guard (FUN_004d44d0): a bounding box wider than 1024 or taller than 512 pixels is dropped. (On a screen the program made wider than retail's
// modes the width limit grows with it, so what covers the screen is still drawn.)
bool oversized(const OtHalVertex* vertices, std::uint32_t count) {
    float left = vertices[0].x, right = vertices[0].x, top = vertices[0].y, bottom = vertices[0].y;
    for (std::uint32_t k = 1; k < count; ++k) {
        left = vertices[k].x < left ? vertices[k].x : left;
        right = vertices[k].x > right ? vertices[k].x : right;
        top = vertices[k].y < top ? vertices[k].y : top;
        bottom = vertices[k].y > bottom ? vertices[k].y : bottom;
    }
    const float limit = g_presentation.screen_width > 640 ? 1024.0f * static_cast<float>(g_presentation.screen_width) / 640.0f : 1024.0f;
    return right - left > limit || bottom - top > 512.0f;
}

}  // namespace

void set_output(const Output& output) { g_output = output; }
void set_presentation(const Presentation& presentation) {
    g_presentation = presentation;
    set_view_polygon_screen(presentation.screen_width, presentation.hud_width);
}
const std::vector<std::uint8_t>& stream() { return g_stream; }
std::uint32_t stream_records() { return g_records; }
const Counters& counters() { return g_counters; }

std::vector<std::uint8_t> stream_file(std::uint32_t frame) {
    std::vector<std::uint8_t> out;
    put32(out, 0x4856544fu);   // OTVH
    put32(out, 1);
    put32(out, frame);
    put32(out, g_records);
    out.insert(out.end(), g_stream.begin(), g_stream.end());
    return out;
}

float DrawKit::f32(std::uint32_t address) const {
    const std::uint32_t bits = word_at(m_, address);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void DrawKit::software_renderer_check() const {
    if (global_word(m_, kSoftwareRenderer) != 0) {
        throw std::runtime_error("D3DSoftwareRenderer is set: only the hardware renderer's draw path exists in the standalone (draw_kit.hpp)");
    }
}

std::uint32_t DrawKit::hal_texture(std::uint32_t surface) {
    if (g_output.video == nullptr || !s_.live() || surface == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    s_.note("ddraw_hal_texture_for", {surface});
    return static_cast<std::uint32_t>(s_.value("ddraw_hal_texture_for"));
}

// 0x004d3b00 (from the disassembly): the table cleared, then entry k linked to entry k - 1 and entry 0 to nothing
void DrawKit::clear_ordering_table(std::uint32_t table, std::uint32_t count) {
    m_.fill(table, 0, static_cast<std::size_t>(count) * 8);
    for (std::uint32_t k = 0; static_cast<std::int32_t>(k) < static_cast<std::int32_t>(count); ++k) {
        set_word_at(m_, table + 8 * k, table + 8 * k - 8);
    }
    set_word_at(m_, table, 0);
}

// 0x004d3b40 (from the disassembly; the hardware renderer's path): a scene that is not running begins: the count moves, the fog tables are made for the fog colour, the
// fog threshold is DpqMin over the far edge, the yon scale is the video's (1000 in texture mode 1), the state caches are reset, the target is cleared to the clear colour
// at the device's colour levels and the depth to 0 (retail's depth is reversed).
void DrawKit::start_scene(std::uint32_t fog_colour, std::uint32_t clear_colour) {
    if (global_byte(m_, kSceneActive) != 0) {
        return;
    }
    set_global_byte(m_, kSceneActive, 1);
    set_global_word(m_, kSceneCount, global_word(m_, kSceneCount) + 1);
    set_global_byte(m_, kSceneListDrawn, 0);
    s_.note("engine_004d3f50", {fog_colour});
    (void)s_.value("engine_004d3f50");
    // fild DpqMin; fidiv the far edge; fstp: an integer quotient, which rounds to the float the same at 53 and at 64 bits
    const std::uint32_t far_edge = half_at(m_, global_word(m_, kViewportEdges) + 0xa);
    const auto dpq = static_cast<double>(global_int(m_, kDpqMin));
    float threshold;
    if (far_edge != 0) {
        threshold = static_cast<float>(dpq / static_cast<double>(far_edge));
    } else {
        threshold = dpq > 0 ? std::numeric_limits<float>::infinity() : dpq < 0 ? -std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::quiet_NaN();
    }
    const std::uint32_t clear = level(clear_colour) | (clear_colour & 0xff000000u);
    software_renderer_check();
    set_global_word(m_, kFogThreshold, float_bits(threshold));
    set_global_word(m_, kFogYonScale, global_word(m_, kVideoFogYonScale));
    if (global_word(m_, kTextureMode) == 1) {
        set_global_word(m_, kFogYonScale, 0x447a0000u);   // 1000.0
    }
    set_global_word(m_, kSceneWord, 0);
    for (const std::uint32_t cache : {kCacheA, kColorBlendCache, kDepthCache, kCacheC, kAlphaStageCache, kCacheF, kCacheG}) {
        set_word_at(m_, cache, 0xffffffffu);
    }
    // A screen wider than the 2D layer: a screen of the 2D layer alone (the loading screen, a menu) is laid out for 4:3 and sits in the middle, and the game's clear colour
    // (the level's sky, left in the clear colour word) would show beside it, which a 4:3 frame never shows. Such a screen is told by the scene before it, which draws the same
    // way for as long as it lasts: its sides are black, the colour fills the middle. A scene with a projected draw (a level) keeps the colour across the whole width.
    const bool wide = g_presentation.hud_width > 0 && g_presentation.screen_width > g_presentation.hud_width;
    const bool dark_sides = wide && !scene_projected_;
    scene_projected_ = false;
    flat_next_ = false;
    if (g_output.video != nullptr) {
        g_output.video->begin_frame();
        if (dark_sides) {
            g_output.video->clear(OT_HAL_CLEAR_COLOR | OT_HAL_CLEAR_DEPTH, 0xff000000u, 0.0f, nullptr);
            const std::int32_t side = (g_presentation.screen_width - g_presentation.hud_width) / 2;
            const OtHalRect middle = {side, 0, g_presentation.hud_width, global_int(m_, kScreenHeight)};
            g_output.video->clear(OT_HAL_CLEAR_COLOR, clear, 0.0f, &middle);
        } else {
            g_output.video->clear(OT_HAL_CLEAR_COLOR | OT_HAL_CLEAR_DEPTH, clear, 0.0f, nullptr);
        }
    }
    g_stream.clear();
    g_records = 0;
    set_global_byte(m_, kStagesSet, 1);
    // the caches that differ from 0 are set to 0 with their device states (the depth cache stays -1: the first D3D_SetTextureBlendMode always sends its states)
    for (const std::uint32_t cache : {kColorBlendCache, kCacheC, kCacheA, kCacheF, kCacheG, kAlphaStageCache}) {
        set_word_at(m_, cache, 0);
    }
}

// 0x004d0c30: D3DPOLY_EndScene, EndScene, the flip
void DrawKit::present_frame() {
    s_.note("engine_004d40d0");
    (void)s_.value("engine_004d40d0");
    software_renderer_check();
    if (g_output.video != nullptr) {
        g_output.video->present();
    }
    host_options().hidden_packets.clear();   // this frame's HUD packets have been walked (host_options.hpp)
    host_options().hud_groups.clear();
    hud_group_ = -1;
    forget_view_polygons();   // the next frame's polygons are bucketed anew (their arena slots are reused)
    const std::uint32_t frame = g_counters.presents++;
    if (g_output.on_present != nullptr) {
        g_output.on_present(frame, g_output.user);
    }
}

// 0x004d3160: the walk runs only inside a scene, marks the scene's list drawn, and runs under the single precision retail selects for it (_controlfp(0x20000, 0x30000);
// it restores 0x9001f afterwards)
void DrawKit::draw_polygon_list(std::uint32_t list) {
    if (global_byte(m_, kSceneActive) == 0) {
        return;
    }
    software_renderer_check();
    set_global_byte(m_, kSceneListDrawn, 1);
    walk(list);
}

void DrawKit::colour_blend_mode(std::uint32_t mode) {
    if (global_word(m_, kColorBlendCache) != mode) {
        set_global_word(m_, kColorBlendCache, mode);
    }
}

void DrawKit::depth_mode(std::uint32_t mode) {
    if (global_word(m_, kDepthCache) != mode) {
        set_global_word(m_, kDepthCache, mode);
    }
}

std::uint32_t DrawKit::level(std::uint32_t colour) const {
    return word_at(m_, kColorLevelRed + 4 * ((colour >> 16) & 0xff)) | word_at(m_, kColorLevelGreen + 4 * ((colour >> 8) & 0xff)) | word_at(m_, kColorLevelBlue + 4 * (colour & 0xff));
}

std::uint32_t DrawKit::packet_colour(std::uint32_t packet, std::uint32_t offset, std::uint32_t alpha) const {
    return word_at(m_, kColorLevelBlue + 4 * byte_at(m_, packet + offset + 2)) | word_at(m_, kColorLevelGreen + 4 * byte_at(m_, packet + offset + 1)) |
           word_at(m_, kColorLevelRed + 4 * byte_at(m_, packet + offset)) | alpha;
}

// The depth-cue level of a vertex, or -1: retail's x87 sequence at 0x004d6a89..0x004d6b17 at the walk's single precision
int DrawKit::fog_level(float rhw) const {
    const float threshold = f32(kFogThreshold);
    const float unit = f32(kUnitScale);
    if (!(threshold < unit)) {
        return -1;
    }
    const std::uint32_t edges = global_word(m_, kViewportEdges);
    const auto far_edge = static_cast<long double>(half_at(m_, edges + 0xe));
    const auto span = static_cast<long double>(half_at(m_, edges + 0xa));
    const long double yon = f32(kFogYonScale);
    const long double distance = round_to(Precision::k24, round_to(Precision::k24, far_edge / rhw) / round_to(Precision::k24, span * yon));
    if (distance < threshold || distance != distance) {
        return -1;
    }
    long double fade = round_to(Precision::k24, round_to(Precision::k24, distance - threshold) / round_to(Precision::k24, static_cast<long double>(unit) - threshold));
    if (fade > unit) {   // fcom; test ah, 0x41: below, equal and unordered keep the value
        fade = unit;
    }
    const long double scaled = round_to(Precision::k24, fade * 255.0L);
    if (scaled != scaled) {
        return 0;   // fistp of a NaN stores the integer indefinite, whose low dword (what retail keeps) is 0
    }
    return static_cast<int>(static_cast<std::int64_t>(scaled));
}

std::uint32_t DrawKit::fogged(std::uint32_t colour, std::uint32_t alpha, int fog) const {
    const auto i = static_cast<std::uint32_t>(fog);
    return word_at(m_, kRedFog + 4 * (((colour >> 16) & 0xff) * 256u + i)) | word_at(m_, kGreenFog + 4 * (((colour >> 8) & 0xff) * 256u + i)) |
           word_at(m_, kAlphaFog + 4 * ((alpha >> 24) * 256u + i)) | word_at(m_, kBlueFog + 4 * ((colour & 0xff) * 256u + i));
}

void DrawKit::emit(OtHalDrawState state, std::uint32_t topology, std::uint32_t count) {
    std::uint8_t head[sizeof(OtHalDrawState) + 8];
    std::memcpy(head, &state, sizeof state);
    std::memcpy(head + sizeof state, &topology, 4);
    std::memcpy(head + sizeof state + 4, &count, 4);
    record(kDraw, head, sizeof head, fan_, count * sizeof(OtHalVertex));
    if (!flat_next_) {
        scene_projected_ = true;
    }
    flat_next_ = false;
    const bool texture_is_hal = texture_is_hal_;
    texture_is_hal_ = false;
    if (g_output.video != nullptr && !host_options().draws_muted) {
        if (!texture_is_hal) {
            state.texture = hal_texture(state.texture);   // the game's surface word to the HAL's handle (the movie's texture already is one)
        }
        if (double_colour_ && (state.flags & OT_HAL_DRAW_MODULATE) != 0) {
            state.flags |= OT_HAL_DRAW_MODULATE_2X;
        }
        if (view_shift_ != 0.0f && fan_[0].z != 1.0f) {   // a projected (not 2D layer) draw inside a fitted view
            OtHalVertex shifted[64];
            for (std::uint32_t k = 0; k < count; ++k) {
                shifted[k] = fan_[k];
                shifted[k].x += view_shift_;
            }
            g_output.video->draw(&state, topology, shifted, count);
        } else {
            g_output.video->draw(&state, topology, fan_, count);
        }
    }
    ++g_counters.draws;
}

// Presentation::psx_colour: is the texture of this descriptor (a texture record: its flags at +0x10) the PlayStation's own, not a bitmap the PC port read from a file
// (flag 4: a paletted bitmap, 8: a 24-bit one; D3DTex_LoadIndexedBitmap 0x004d9950)?
bool DrawKit::psx_texture(std::uint32_t descriptor) const {
    return g_presentation.psx_colour && descriptor != 0 && (byte_at(m_, descriptor + 0x10) & 0x0cu) == 0;
}

// Presentation (a screen wider than its 2D layer): the corners of a 2D primitive, which the game placed by scaling the PlayStation's 512 columns to the whole screen,
// go to where they are on a screen `hud_width` wide in the middle of this one. A primitive that reaches both sides of the screen (a fade, a bar, a backdrop) stays:
// it is there to cover the screen.
//
// An element of the in-level HUD (HostOptions::hud_groups) is placed about a point of its own instead, by place_hud_group: an element the game laid out near the left or right
// third of the screen goes to that edge of the picture (HostOptions::hud_anchor), and HostOptions::hud_scale grows or shrinks it about its point.
void DrawKit::fit_flat(std::uint32_t count) {
    flat_next_ = true;
    const auto screen = static_cast<float>(g_presentation.screen_width);
    const auto hud = static_cast<float>(g_presentation.hud_width);
    auto& groups = host_options().hud_groups;
    const bool grouped = hud_group_ >= 0 && static_cast<std::size_t>(hud_group_) < groups.size();
    if (!(hud > 0.0f) || !(grouped ? screen >= hud : screen > hud)) {
        return;
    }
    float left = fan_[0].x, right = fan_[0].x;
    for (std::uint32_t k = 1; k < count; ++k) {
        left = fan_[k].x < left ? fan_[k].x : left;
        right = fan_[k].x > right ? fan_[k].x : right;
    }
    if (left <= 1.0f && right >= screen - 2.0f) {
        return;
    }
    if (grouped) {
        auto& group = groups[static_cast<std::size_t>(hud_group_)];
        const float height = static_cast<float>(global_int(m_, kScreenHeight));
        if (!group.placed) {
            place_hud_group(group, screen, hud, height);
        }
        const float scale = host_options().hud_scale;
        for (std::uint32_t k = 0; k < count; ++k) {
            fan_[k].x = group.to_x + (fan_[k].x - group.from_x) * (hud / screen) * scale;
            fan_[k].y = group.to_y + (fan_[k].y - group.from_y) * scale;
        }
        return;
    }
    const float scale = hud / screen;
    const float offset = (screen - hud) * 0.5f;
    for (std::uint32_t k = 0; k < count; ++k) {
        fan_[k].x = fan_[k].x * scale + offset;
    }
}

// The point an element of the HUD is placed about (HostOptions::HudGroup), in the wide screen's coordinates: `from` is where the game laid it out, `to` where it goes.
// A centre element (and every element when the HUD is not anchored) keeps its place in the 4:3 middle: from its own point to the same point on the 4:3 layout. An element in the
// outer thirds of the screen is held to the picture's left or right edge (top or bottom edge, for the height), inset by the margin.
void DrawKit::place_hud_group(HostOptions::HudGroup& group, float screen, float hud, float height) {
    const HostOptions& options = host_options();
    const float margin_x = options.hud_margin * screen;
    const float margin_y = options.hud_margin * height;
    const float point_x = group.fx * screen;
    const float point_y = group.fy * height;
    group.from_x = point_x;
    group.to_x = screen * 0.5f + (point_x - screen * 0.5f) * (hud / screen);
    group.from_y = point_y;
    group.to_y = point_y;
    if (options.hud_anchor) {
        if (group.fx < 1.0f / 3.0f) {
            group.from_x = 0.0f;
            group.to_x = margin_x;
        } else if (group.fx > 2.0f / 3.0f) {
            group.from_x = screen;
            group.to_x = screen - margin_x;
        }
        if (group.fy < 1.0f / 3.0f) {
            group.from_y = 0.0f;
            group.to_y = margin_y;
        } else if (group.fy > 2.0f / 3.0f) {
            group.from_y = height;
            group.to_y = height - margin_y;
        }
    }
    group.placed = true;
}

// Presentation (a screen wider than its 2D layer): a view that fills only part of the screen's width (the player select's skater, the menus' models) sits in the 2D
// layout, which fit_flat squeezes into the middle. The game laid the rectangle out across the whole wide screen, so the rectangle moves to where the layout puts it; its
// polygons follow by the same shift (presentation_view_polygon), and the size of the picture is already the 4:3 one. A view across the whole width (the world, a split
// screen) stays.
void DrawKit::fit_viewport(OtHalRect& area) {
    const auto screen = static_cast<float>(g_presentation.screen_width);
    const auto hud = static_cast<float>(g_presentation.hud_width);
    if (!(hud > 0.0f) || !(screen > hud) || (area.x <= 1 && static_cast<float>(area.x + area.w) >= screen - 2.0f)) {
        return;
    }
    const float scale = hud / screen;
    const float offset = (screen - hud) * 0.5f;
    const float centre = static_cast<float>(area.x) + 0.5f * static_cast<float>(area.w);
    const float fitted = centre * scale + offset;
    area.w = static_cast<std::int32_t>(static_cast<float>(area.w) * scale + 0.5f);
    area.x = static_cast<std::int32_t>(fitted - 0.5f * static_cast<float>(area.w) + 0.5f);
}

void DrawKit::unsupported(std::uint32_t command, std::uint32_t reason) {
    const std::uint32_t payload[2] = {command, reason};
    record(kUnsupported, payload, sizeof payload);
    ++g_counters.unsupported;
}

void DrawKit::walk(std::uint32_t list) {
    for (std::uint32_t packet = list; packet != 0; packet = word_at(m_, packet)) {
        if (word_at(m_, packet + 4) == 0) {
            continue;
        }
        // the HUD, with "HUD off" (host_options.hpp): the packet is walked as usual, so the state it leaves for the next one is the game's, and only its draws are dropped
        struct HudMute {
            bool was = host_options().draws_muted;
            ~HudMute() { host_options().draws_muted = was; }
        } hud_mute;
        for (const auto& [begin, end] : host_options().hidden_packets) {
            if (packet >= begin && packet < end) {
                host_options().draws_muted = true;
            }
        }
        // the HUD's layout (host_options.hpp): the element this packet belongs to, if Panel::display recorded one
        hud_group_ = -1;
        for (std::size_t g = 0; g < host_options().hud_groups.size(); ++g) {
            if (packet >= host_options().hud_groups[g].begin && packet < host_options().hud_groups[g].end) {
                hud_group_ = static_cast<std::int32_t>(g);
                break;
            }
        }
        ++g_counters.packets;
        const std::uint32_t command = byte_at(m_, packet + 7);
        if ((command & 0x80) == 0) {
            // a PSX packet: the walk sets the state, then the handler draws it
            const std::uint32_t kind = command & 0xfcu;
            const bool line = kind == 0x40 || kind == 0x48 || kind == 0x4c || kind == 0x50;
            set_global_word(m_, kPacketFlags, 0);
            set_global_byte(m_, kPacketRaw, command & 1);
            const bool textured = !line && (command & 4) != 0;
            std::uint32_t tpage = 0;
            if (textured) {
                if (global_word(m_, kAlphaStageCache) != 1) {
                    set_global_word(m_, kAlphaStageCache, 1);
                }
                tpage = (kind & 0x10) != 0 ? half_at(m_, packet + 0x1a) : half_at(m_, packet + 0x16);
            } else if (global_word(m_, kAlphaStageCache) != 0) {
                set_global_word(m_, kAlphaStageCache, 0);
            }
            std::uint32_t alpha = 0xff000000u;
            int colour_mode = 1;
            int depth = kDepthWrite;
            if ((command & 2) != 0) {
                depth = kDepthTest;
                if (global_word(m_, kDepthCache) != 1) {
                    set_global_word(m_, kDepthCache, 1);
                }
                if (textured) {
                    set_global_word(m_, kTexpageBlendMode, (tpage >> 5) & 3u);
                }
                switch (global_word(m_, kTexpageBlendMode)) {
                case 0: alpha = 0x80000000u; colour_mode = 1; break;
                case 1: alpha = 0xff000000u; colour_mode = 2; break;
                case 2: alpha = 0; colour_mode = 4; break;
                default: alpha = 0x40000000u; colour_mode = 2; break;
                }
                set_global_word(m_, kPolyAlpha, alpha);
                colour_blend_mode(static_cast<std::uint32_t>(colour_mode));
                set_global_word(m_, kPacketFlags, global_word(m_, kPacketFlags) | (8u | (global_word(m_, kTexpageBlendMode) << 4)));
            } else {
                set_global_word(m_, kPolyAlpha, 0xff000000u);
                colour_blend_mode(1);
                depth_mode(kDepthWrite);
            }
            std::uint32_t layout = kLayoutCount;
            for (std::uint32_t k = 0; k < kLayoutCount; ++k) {
                if (kLayouts[k].kind == kind) {
                    layout = k;
                }
            }
            if (layout != kLayoutCount) {
                psx_polygon(layout, packet, (command & 1) != 0, alpha, colour_mode, depth);
            } else if (kind == 0x60 || kind == 0x68 || kind == 0x70 || kind == 0x78) {
                psx_tile(packet, kind == 0x60 ? 0 : kind == 0x68 ? 1 : kind == 0x70 ? 8 : 16, alpha, colour_mode, depth);
            } else {
                unsupported(command, kUnknownPacket);
            }
        } else if (command == 0xe1) {
            set_global_word(m_, kTexpageBlendMode, (word_at(m_, packet + 4) >> 5) & 3u);
            const std::uint32_t texture = word_at(m_, packet + 0xc);
            set_global_word(m_, kTextureDescriptor, texture != 0 ? word_at(m_, texture + 0x14) : 0);
        } else if (command == 0xe3) {
            viewport(packet);
        } else if ((command & 0xfc) == 0xb0) {
            general_polygon(packet);
        } else {
            unsupported(command, kUnknownPacket);     // the dispatch table's empty handler (0x004d7420)
        }
    }
}

// 0xB0: a PC polygon of pre-transformed vertices (D3D_DrawGeneralPolygon, 0x004d68b0)
void DrawKit::general_polygon(std::uint32_t packet) {
    const std::uint32_t flags = word_at(m_, packet + 8);
    if ((flags & 0x30000000u) != 0) {
        unsupported(0xb0, kOutline);   // outline passes (line strips): not in the Warehouse census
        if ((flags & 0x30000000u) == 0x10000000u) {
            return;
        }
    }
    const bool levels = (flags & 0x1c0u) != 0x140u;
    OtHalDrawState state{};
    std::uint32_t alpha;
    int depth_rule;
    if ((flags & 0x40u) == 0) {
        alpha = 0xff000000u;
        colour_blend(state, 1);
        colour_blend_mode(1);
        depth_mode(kDepthWrite);
        depth_rule = kDepthWrite;
    } else {
        depth_rule = kDepthTest;
        if (global_word(m_, kDepthCache) != 1) {
            set_global_word(m_, kDepthCache, 1);
        }
        switch (flags & 0x180u) {
        case 0x100u: alpha = 0; colour_blend(state, 4); colour_blend_mode(4); break;
        case 0x000u: alpha = 0x80000000u; colour_blend(state, 1); colour_blend_mode(1); break;
        case 0x080u: alpha = 0xff000000u; colour_blend(state, 2); colour_blend_mode(2); break;
        default: alpha = 0x40000000u; colour_blend(state, 2); colour_blend_mode(2); break;
        }
    }
    const std::uint32_t texture_record = word_at(m_, packet + 0x10);
    const std::uint32_t count = word_at(m_, packet + 0x14);
    if (count == 0 || count > 64) {
        unsupported(0xb0, kUnknownPacket);
        return;
    }
    const std::uint32_t stride = texture_record != 0 ? 28u : 20u;
    bool all_clear = true;
    bool flat = true;   // every reciprocal depth exactly 1: a rectangle of the 2D layer laid out in screen pixels (D3D_BuildSpriteQuad, 0x004d7990), not a projected face
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t source = packet + 0x18 + k * stride;
        OtHalVertex& vertex = fan_[k];
        vertex.x = f32(source);
        vertex.y = f32(source + 4);
        vertex.z = f32(source + 8);
        vertex.rhw = f32(source + 12);
        flat = flat && word_at(m_, source + 12) == 0x3f800000u;
        std::uint32_t colour = (word_at(m_, source + 16) & 0x00ffffffu) | alpha;
        if ((flags & 0x80000000u) == 0) {
            const int fog = fog_level(vertex.rhw);
            if (fog >= 0) {
                colour = fogged(colour, alpha, fog);
            }
        }
        if ((colour & 0xff000000u) != 0) {
            all_clear = false;
        }
        plain_[k] = colour;
        if (levels) {
            colour = level(colour) | (colour & 0xff000000u);
        }
        set_word_at(m_, source + 16, colour);   // retail stores each stage (the alpha, the fog, the levels) back into the packet's vertex
        vertex.color = colour;
        vertex.specular = 0xff000000u;
        vertex.u = texture_record != 0 ? f32(source + 20) : 0.0f;
        vertex.v = texture_record != 0 ? f32(source + 24) : 0.0f;
    }
    if (texture_record != 0) {
        if (alpha != 0 && all_clear) {
            ++g_counters.faded;
            return;   // every vertex faded out (before the texture state: a faded polygon binds nothing)
        }
        const std::uint32_t descriptor = word_at(m_, texture_record + 0x14);
        // The level select's video box: retail's CreateBinkSurface gives the movie's record (0x006a69f8) a descriptor over a DirectDraw surface; the program has the picture in a
        // HAL texture instead (g_output.movie_texture), which this quad shows.
        const std::uint32_t movie = descriptor == 0 && texture_record == kMovieRecord && g_output.movie_texture != nullptr ? g_output.movie_texture() : 0;
        if (descriptor == 0 && movie == 0) {
            // Retail reads [descriptor + 0x10] here (0x004d6ef5) and faults: a texture record with no descriptor never reaches it in a run that matches
            // retail. Seen in play (2026-11-16, level 3, after the pause menu's trick screens); skip the polygon like psx_polygon does rather than stop the game.
            ++g_counters.no_texture;
            static int reported = 0;   // evidence for the missing texture: the first few records, where a run's log shows them
            if (reported < 20) {
                ++reported;
                std::fprintf(stderr, "draw: general polygon %08x flags %08x: texture record %08x has no descriptor (skipped)\n", packet, flags, texture_record);
                std::fflush(stderr);
            }
            return;
        }
        general_texture_tail(flags, descriptor);
        state.texture = descriptor != 0 ? word_at(m_, descriptor) : movie;   // the game's surface (the store's handle)
        texture_is_hal_ = descriptor == 0;
        state.flags |= OT_HAL_DRAW_MODULATE | OT_HAL_DRAW_FILTER;
        if ((flags & 0x20u) != 0) {
            state.flags |= OT_HAL_DRAW_WRAP;
        }
        if (descriptor != 0 && (byte_at(m_, descriptor + 0x10) & 0x10u) == 0 && depth_rule == kDepthWrite) {
            depth_rule = kDepthTest;
        }
    } else {
        handler_tail(0);   // the untextured state after the vertices (0x004d70d0), then the alpha stage goes back to "select" whatever the caches said (found by tony/draw_oracle.py)
        if (global_word(m_, kAlphaStageCache) != 0) {
            set_global_word(m_, kAlphaStageCache, 0);
        }
    }
    depth(state, depth_rule);
    if (!flat) {
        view_shift_ = view_polygon_shift(packet);   // Presentation: a part-width view's model polygon (render_presentation.hpp)
    }
    if (flat) {
        fit_flat(count);
    } else if (texture_record != 0 && psx_texture(word_at(m_, texture_record + 0x14))) {
        // Presentation::psx_colour: the colours as they were before the levels (the alpha and the fog stay), doubled by the video
        for (std::uint32_t k = 0; k < count; ++k) {
            fan_[k].color = plain_[k];
        }
        double_colour_ = true;
    }
    emit(state, OT_HAL_TRIANGLE_FAN, count);
    double_colour_ = false;
    view_shift_ = 0.0f;
}

// The textured general polygon's state after its vertices and the faded test (0x004d6dbb..0x004d6eb6): the caches of a textured draw, the alpha stage, the address mode the
// packet's wrap flag asks for (0x006a04b4 set while it wraps), and the descriptor itself bound
void DrawKit::general_texture_tail(std::uint32_t flags, std::uint32_t descriptor) {
    if (global_word(m_, kCacheA) != 3) {
        set_global_word(m_, kCacheA, 3);
        if (global_word(m_, kCacheF) != 1) {
            set_global_word(m_, kCacheF, 1);
        }
        if (global_word(m_, kCacheG) != 1) {
            set_global_word(m_, kCacheG, 1);
        }
    }
    if (global_word(m_, kAlphaStageCache) != 1) {
        set_global_word(m_, kAlphaStageCache, 1);
    }
    if ((flags & 0x20u) != 0) {
        if (global_byte(m_, kStagesSet) == 0) {
            set_global_byte(m_, kStagesSet, 1);
        }
    } else if (global_byte(m_, kStagesSet) != 0) {
        set_global_byte(m_, kStagesSet, 0);
    }
    if (global_word(m_, kBoundTexture) != descriptor) {
        set_global_word(m_, kBoundTexture, descriptor);
        set_global_word(m_, kBindings, global_word(m_, kBindings) + 1);
    }
}

void DrawKit::psx_polygon(std::uint32_t layout_index, std::uint32_t packet, bool raw, std::uint32_t alpha, int colour_mode, int depth_rule) {
    const PsxLayout& layout = kLayouts[layout_index];
    std::uint32_t descriptor = 0;
    OtHalDrawState state{};
    if (layout.texture != 0) {
        // the textured handlers count the polygon first, then need a texture record whose descriptor has a surface
        set_global_word(m_, kPrimitiveCount, global_word(m_, kPrimitiveCount) + 1);
        const std::uint32_t texture = word_at(m_, packet + layout.texture);
        if (texture == 0) {
            ++g_counters.no_texture;
            return;
        }
        descriptor = word_at(m_, texture + 0x14);
        set_global_word(m_, kTextureDescriptor, descriptor);
        if (descriptor == 0 || word_at(m_, descriptor) == 0) {
            ++g_counters.no_texture;
            return;
        }
        if ((byte_at(m_, descriptor + 0x10) & 0x10u) == 0) {
            if (global_word(m_, kDepthCache) != 1) {
                set_global_word(m_, kDepthCache, 1);
            }
            if (depth_rule != kDepthTest) {
                depth_rule = kDepthTest;
            }
        }
    }
    float z = f32(packet + layout.z);
    const bool flat = z < 0.0f;   // a primitive of the 2D layer: no depth, drawn over what is there
    if (z < 0.0f) {   // RenderWindingThreshold (0x00518484, 0)
        if (global_word(m_, kDepthCache) != 0) {
            set_global_word(m_, kDepthCache, 0);
        }
        depth_rule = kDepthOff;
        z = 1.0f;
    }
    if (layout.texture == 0 && layout.counted) {
        set_global_word(m_, kPrimitiveCount, global_word(m_, kPrimitiveCount) + 1);
    }
    float su = 0.0f;
    float sv = 0.0f;
    if (descriptor != 0) {
        const float unit = f32(kUnitScale);
        int width = short_at(m_, descriptor + 0x14);
        int height = short_at(m_, descriptor + 0x16);
        width = width > 1 ? width - 1 : width;
        height = height > 1 ? height - 1 : height;
        su = unit / static_cast<float>(width);
        sv = unit / static_cast<float>(height);
    }
    const std::uint32_t raw_colour = alpha | level(0x00ffffffu);
    for (std::uint32_t k = 0; k < layout.count; ++k) {
        OtHalVertex& vertex = fan_[k];
        vertex.x = static_cast<float>(short_at(m_, packet + layout.xy[k]));
        vertex.y = static_cast<float>(short_at(m_, packet + layout.xy[k] + 2u));
        vertex.z = z;
        vertex.rhw = 1.0f;
        vertex.color = descriptor != 0 && raw ? raw_colour : packet_colour(packet, layout.rgb[k], alpha);
        vertex.specular = 0xff000000u;
        vertex.u = descriptor != 0 ? static_cast<float>(byte_at(m_, packet + layout.uv[k])) * su : 0.0f;
        vertex.v = descriptor != 0 ? static_cast<float>(byte_at(m_, packet + layout.uv[k] + 1u)) * sv : 0.0f;
    }
    stage(descriptor != 0, layout.count, layout.topology == OT_HAL_LINE_STRIP);
    if (layout.guard && oversized(fan_, layout.count)) {
        ++g_counters.oversized;
        return;
    }
    handler_tail(descriptor);
    colour_blend(state, colour_mode);
    if (descriptor != 0) {
        const std::uint32_t holder = word_at(m_, descriptor + 4);
        state.texture = word_at(m_, word_at(m_, holder + 0x14));
        state.flags |= OT_HAL_DRAW_MODULATE | OT_HAL_DRAW_FILTER;   // address: clamp
    }
    depth(state, depth_rule);
    if (flat) {
        fit_flat(layout.count);
    }
    if (descriptor != 0 && psx_texture(descriptor)) {
        // Presentation::psx_colour: the packet's own colour bytes (a raw texture: the PlayStation's neutral 0x80), doubled by the video
        for (std::uint32_t k = 0; k < layout.count; ++k) {
            const std::uint32_t at = packet + layout.rgb[k];
            fan_[k].color = raw ? alpha | 0x00808080u
                                : alpha | (static_cast<std::uint32_t>(byte_at(m_, at)) << 16) | (static_cast<std::uint32_t>(byte_at(m_, at + 1)) << 8) | byte_at(m_, at + 2);
        }
        double_colour_ = true;
    }
    emit(state, layout.topology, layout.count);
    double_colour_ = false;
}

// The D3DTLVERTEX staging buffers the PSX handlers fill before their size guard (milestone 8d; the words were found by the oracle of tony/draw_oracle.py, which compares them
// with retail's): a textured primitive writes x, y, z, colour, u and v of each vertex into the 28-byte entries at 0x00549dc0, an untextured one x, y, z and colour into the
// 20-byte entries at 0x00549e30. The fourth word of an entry (the reciprocal depth) is never written, and a primitive with fewer vertices leaves the later entries alone.
void DrawKit::stage(bool textured, std::uint32_t count, bool closes) {
    const std::uint32_t base = textured ? kStagingTextured : kStagingPlain;
    const std::uint32_t stride = textured ? 0x1c : 0x14;
    // a closed line strip (LINE_F3, LINE_F4) ends on its first point, which retail copies as a whole entry, the unwritten word included
    for (std::uint32_t k = 0; k < count - (closes ? 1 : 0); ++k) {
        const std::uint32_t at = base + k * stride;
        set_word_at(m_, at, float_bits(fan_[k].x));
        set_word_at(m_, at + 4, float_bits(fan_[k].y));
        set_word_at(m_, at + 8, float_bits(fan_[k].z));
        set_word_at(m_, at + 0x10, fan_[k].color);
        if (textured) {
            set_word_at(m_, at + 0x14, float_bits(fan_[k].u));
            set_word_at(m_, at + 0x18, float_bits(fan_[k].v));
        }
    }
    if (closes) {
        m_.copy(base + (count - 1) * stride, base, stride);
    }
}

// The common tail of the PSX D3D_Draw* handlers, run for a primitive the size guard let through (hardware renderer; from their disassembly, e.g. 0x004d4451 of
// D3D_DrawSolidTriangle and 0x004d4f4a of D3D_DrawTexturedQuad): three device-state caches, then for a textured one the texture binding: the surface holder of
// the descriptor ([[descriptor + 4] + 0x14]) is bound when it is not the one bound already, and the binding counted.
void DrawKit::handler_tail(std::uint32_t descriptor) {
    const std::uint32_t mode = descriptor != 0 ? 3 : 1;
    if (global_word(m_, kCacheA) != mode) {
        set_global_word(m_, kCacheA, mode);
        if (global_word(m_, kCacheF) != 1) {
            set_global_word(m_, kCacheF, 1);
        }
        const std::uint32_t filter = descriptor != 0 ? 1 : 0;
        if (global_word(m_, kCacheG) != filter) {
            set_global_word(m_, kCacheG, filter);
        }
    }
    if (descriptor != 0) {
        bind_texture(descriptor);
    }
}

void DrawKit::bind_texture(std::uint32_t descriptor) {
    set_global_byte(m_, kStagesSet, 0);
    const std::uint32_t holder = word_at(m_, word_at(m_, descriptor + 4) + 0x14);
    if (global_word(m_, kBoundTexture) != holder) {
        set_global_word(m_, kBoundTexture, holder);
        set_global_word(m_, kBindings, global_word(m_, kBindings) + 1);
    }
}

// TILE (0x60, the size in the packet) and TILE_1/8/16 (0x68/0x70/0x78): D3D_DrawSolidRectangle, a four-vertex fan
void DrawKit::psx_tile(std::uint32_t packet, std::int32_t size, std::uint32_t alpha, int colour_mode, int depth_rule) {
    float z = f32(packet + 0x10);
    const bool flat = z < 0.0f;
    if (z < 0.0f) {
        if (global_word(m_, kDepthCache) != 0) {
            set_global_word(m_, kDepthCache, 0);
        }
        depth_rule = kDepthOff;
        z = 1.0f;
    }
    set_global_word(m_, kPrimitiveCount, global_word(m_, kPrimitiveCount) + 1);
    const float x = static_cast<float>(short_at(m_, packet + 8));
    const float y = static_cast<float>(short_at(m_, packet + 10));
    const float w = static_cast<float>(size != 0 ? size : short_at(m_, packet + 0x0c));
    const float h = static_cast<float>(size != 0 ? size : short_at(m_, packet + 0x0e));
    const float corners[4][2] = {{x, y}, {w + x, y}, {w + x, y + h}, {x, y + h}};
    const std::uint32_t colour = packet_colour(packet, 4, alpha);
    for (std::uint32_t k = 0; k < 4; ++k) {
        OtHalVertex& vertex = fan_[k];
        vertex.x = corners[k][0];
        vertex.y = corners[k][1];
        vertex.z = z;
        vertex.rhw = 1.0f;
        vertex.color = colour;
        vertex.specular = 0xff000000u;
        vertex.u = 0.0f;
        vertex.v = 0.0f;
    }
    stage(false, 4, false);
    if (oversized(fan_, 4)) {
        ++g_counters.oversized;
        return;
    }
    handler_tail(0);
    OtHalDrawState state{};
    colour_blend(state, colour_mode);
    depth(state, depth_rule);
    if (flat) {
        fit_flat(4);
    }
    emit(state, OT_HAL_TRIANGLE_FAN, 4);
}

// A handler called on its own (8c). The walk's preamble left its decisions in retail's words, and the handlers read them from there: the blend mode in the colour cache,
// the depth rule in the depth cache, the alpha in D3DPolyAlpha, the raw-texture bit in its byte.
void DrawKit::draw_handler(std::uint32_t kind, std::uint32_t packet) {
    if (kind == 0xb0) {
        general_polygon(packet);
        return;
    }
    const std::uint32_t colour_cache = global_word(m_, kColorBlendCache);
    const int colour_mode = colour_cache >= 1 && colour_cache <= 4 ? static_cast<int>(colour_cache) : 1;
    const std::uint32_t depth_cache = global_word(m_, kDepthCache);
    const int depth_rule = depth_cache == 0 ? kDepthOff : depth_cache == 1 ? kDepthTest : kDepthWrite;
    const std::uint32_t alpha = global_word(m_, kPolyAlpha);
    if (kind == 0x60) {
        const std::uint32_t command = byte_at(m_, packet + 7) & 0xfcu;
        psx_tile(packet, command == 0x60 ? 0 : command == 0x68 ? 1 : command == 0x70 ? 8 : 16, alpha, colour_mode, depth_rule);
        return;
    }
    for (std::uint32_t k = 0; k < kLayoutCount; ++k) {
        if (kLayouts[k].kind == kind) {
            psx_polygon(k, packet, global_byte(m_, kPacketRaw) != 0, alpha, colour_mode, depth_rule);
            return;
        }
    }
    unsupported(kind, kUnknownPacket);
}

// 0x004d36c0 (hardware renderer): the device's viewport becomes the rectangle (D3DVIEWPORT7, depth range 0 to 1)
void DrawKit::set_viewport_rect(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    software_renderer_check();
    const std::int32_t rect[4] = {x, y, width, height};
    record(kViewport, rect, sizeof rect);
    if (g_output.video != nullptr) {
        OtHalRect area = {x, y, width, height};
        fit_viewport(area);
        g_output.video->set_viewport(&area);
    }
}

// 0x004d0ec0 (from the disassembly): a DirectDraw colour-fill Blt (DDBLT_COLORFILL | DDBLT_WAIT, fill colour 0xff000000) of the back buffer: the colour black, the depth
// untouched
void DrawKit::clear_buffers() {
    software_renderer_check();
    if (g_output.video != nullptr) {
        g_output.video->clear(OT_HAL_CLEAR_COLOR, 0xff000000u, 0.0f, nullptr);
    }
}

// DR_AREA-like viewport packet (0xE3): D3D_SubmitViewportRect's integer arithmetic (0x004d3600); the wrapped top goes back into the packet, as retail stores it
void DrawKit::viewport(std::uint32_t packet) {
    const std::int32_t screen_width = global_int(m_, kScreenWidth);
    const std::int32_t screen_height = global_int(m_, kScreenHeight);
    const auto top_raw = static_cast<std::int16_t>(short_at(m_, packet + 10) % 256);
    set_half_at(m_, packet + 10, static_cast<std::uint16_t>(top_raw));
    std::int32_t x = (short_at(m_, packet + 8) * screen_width) >> 9;
    auto y = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(top_raw) * screen_height) / 0xf0u);
    std::int32_t w = (short_at(m_, packet + 0xc) * screen_width) >> 9;
    std::int32_t h = (short_at(m_, packet + 0xe) * screen_height) / 0xf0;
    x = x < 0 ? 0 : (x > screen_width ? screen_width : x);
    w = w < 0 ? 0 : (w > screen_width ? screen_width : w);
    y = y < 0 ? 0 : (y > screen_height ? screen_height : y);
    h = h < 0 ? 0 : (h > screen_height ? screen_height : h);
    if (w + x <= screen_width && h + y <= screen_height) {
        const std::int32_t rect[4] = {x, y, w, h};
        record(kViewport, rect, sizeof rect);
        if (g_output.video != nullptr) {
            OtHalRect area = {x, y, w, h};
            fit_viewport(area);
            g_output.video->set_viewport(&area);
        }
    }
}

}  // namespace opentony::runtime::drawkit
