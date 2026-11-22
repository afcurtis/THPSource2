#include "d3d7_stream.h"

#include <cstdio>
#include <cstring>
#include <vector>

// Replays hand-built Direct3D 7 streams onto a recording backend and checks
// the HAL calls: state translation, fan vertices, textures, clears.

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
    if (!condition) {
        std::fprintf(stderr, "d3d7_stream_test.cpp:%d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

struct Draw {
    OtHalDrawState state;
    uint32_t topology;
    std::vector<OtHalVertex> vertices;
};

std::vector<Draw> draws;
std::vector<OtHalRect> clears;
uint32_t textures_created = 0;
uint32_t texture_width = 0;

OtHalHandle OT_HAL_CALL texture_create(const OtHalTextureDesc *desc) {
    texture_width = static_cast<uint32_t>(desc->width);
    return ++textures_created;
}
void OT_HAL_CALL texture_update(OtHalHandle, const OtHalRect *, const void *, int32_t) {}
void OT_HAL_CALL clear(uint32_t, uint32_t, float, const OtHalRect *area) {
    clears.push_back(area ? *area : OtHalRect{0, 0, 0, 0});
}
void OT_HAL_CALL set_viewport(const OtHalRect *) {}
void OT_HAL_CALL draw(const OtHalDrawState *state, uint32_t topology, const OtHalVertex *vertices, uint32_t count) {
    draws.push_back(Draw{*state, topology, std::vector<OtHalVertex>(vertices, vertices + count)});
}

OtHalVideoBackend backend() {
    OtHalVideoBackend video;
    std::memset(&video, 0, sizeof(video));
    video.name = "recording";
    video.texture_create = texture_create;
    video.texture_update = texture_update;
    video.clear = clear;
    video.set_viewport = set_viewport;
    video.draw = draw;
    return video;
}

struct Stream {
    std::vector<uint8_t> bytes;
    uint32_t records = 0;

    void u32(uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
    void f32(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        u32(bits);
    }
    void record(uint16_t type, uint32_t size) {
        bytes.push_back(static_cast<uint8_t>(type));
        bytes.push_back(static_cast<uint8_t>(type >> 8));
        bytes.push_back(0);
        bytes.push_back(0);
        u32(size);
        ++records;
    }
    void render_state(uint32_t state, uint32_t value) {
        record(4, 8);
        u32(state);
        u32(value);
    }
    void stage_state(uint32_t state, uint32_t value) {
        record(5, 12);
        u32(0);
        u32(state);
        u32(value);
    }
    std::vector<uint8_t> finish() {
        std::vector<uint8_t> out = {'O', 'T', 'V', 'S', 1, 0, 0, 0, 7, 0, 0, 0};
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(records >> shift));
        out.insert(out.end(), bytes.begin(), bytes.end());
        return out;
    }
};

// A retail textured fan: D3DFVF_XYZRHW | DIFFUSE | TEX1, three vertices.
void textured_fan(Stream &stream) {
    stream.record(7, 20 + 3 * 28);
    stream.u32(6);     // D3DPT_TRIANGLEFAN
    stream.u32(0x144); // fvf
    stream.u32(3);
    stream.u32(0);
    stream.u32(28);
    for (int vertex = 0; vertex < 3; ++vertex) {
        stream.f32(10.0f * vertex);
        stream.f32(20.0f);
        stream.f32(0.5f);
        stream.f32(1.0f);
        stream.u32(0x80ff0000u);
        stream.f32(0.25f * vertex);
        stream.f32(0.75f);
    }
}

int find_texture(void *, uint64_t hash, uint32_t, OtHalTextureDesc *desc, const void **pixels, int32_t *pitch) {
    static const uint16_t texels[4] = {0x8000, 0x7fff, 0x801f, 0x7c00};
    if (hash != 0x1234) {
        return 0;
    }
    desc->width = 2;
    desc->height = 2;
    desc->format = OT_HAL_PIXEL_ARGB1555;
    desc->palette = 0;
    *pixels = texels;
    *pitch = 4;
    return 1;
}

void reset() {
    draws.clear();
    clears.clear();
    textures_created = 0;
}

void test_state_translation_and_fan() {
    reset();
    Stream stream;
    stream.render_state(27, 1); // alpha blend enable
    stream.render_state(19, 5); // src: SRCALPHA
    stream.render_state(20, 6); // dest: INVSRCALPHA
    stream.render_state(7, 1);  // z enable
    stream.render_state(23, 7); // z func: greater-equal
    stream.render_state(14, 0); // z write off
    stream.render_state(9, 1);  // flat shading
    stream.stage_state(1, 4);   // color op: modulate
    stream.stage_state(16, 2);  // mag filter: linear
    stream.stage_state(13, 1);  // address u: wrap
    stream.record(6, 16);       // texture 0x1234 on stage 0
    stream.u32(0);
    stream.u32(1);
    stream.u32(0x1234);
    stream.u32(0);
    textured_fan(stream);
    const std::vector<uint8_t> bytes = stream.finish();
    OtHalVideoBackend video = backend();
    OtD3d7ReplayStats stats;
    CHECK(ot_d3d7_replay(bytes.data(), static_cast<uint32_t>(bytes.size()), &video, find_texture, nullptr, &stats));
    CHECK(stats.draws == 1 && stats.textures_loaded == 1 && stats.unsupported == 0);
    CHECK(draws.size() == 1);
    if (draws.size() != 1) return;
    const Draw &fan = draws[0];
    CHECK(fan.topology == OT_HAL_TRIANGLE_FAN);
    CHECK(fan.state.texture == 1 && texture_width == 2);
    const uint32_t expected = OT_HAL_DRAW_BLEND | OT_HAL_DRAW_DEPTH_TEST | OT_HAL_DRAW_FLAT | OT_HAL_DRAW_MODULATE |
                              OT_HAL_DRAW_FILTER | OT_HAL_DRAW_WRAP | OT_HAL_DRAW_PERSPECTIVE |
                              OT_HAL_DRAW_DEPTH_GREATER;
    CHECK(fan.state.flags == expected);
    CHECK(fan.state.src_factor == OT_HAL_FACTOR_SRC_ALPHA && fan.state.dst_factor == OT_HAL_FACTOR_INV_SRC_ALPHA);
    CHECK(fan.vertices.size() == 3 && fan.vertices[2].x == 20.0f && fan.vertices[2].u == 0.5f);
    CHECK(fan.vertices[1].color == 0x80ff0000u && fan.vertices[1].rhw == 1.0f);
}

void test_select_diffuse_drops_the_texture_and_missing_textures_are_counted() {
    reset();
    Stream stream;
    stream.stage_state(1, 3); // color op: select diffuse
    stream.record(6, 16);
    stream.u32(0);
    stream.u32(1);
    stream.u32(0x9999); // not on disk
    stream.u32(0);
    textured_fan(stream);
    const std::vector<uint8_t> bytes = stream.finish();
    OtHalVideoBackend video = backend();
    OtD3d7ReplayStats stats;
    CHECK(ot_d3d7_replay(bytes.data(), static_cast<uint32_t>(bytes.size()), &video, find_texture, nullptr, &stats));
    CHECK(draws.size() == 1 && draws[0].state.texture == OT_HAL_INVALID_HANDLE);
    CHECK(stats.textures_missing == 1);
}

void test_clear_rects_and_malformed_streams() {
    reset();
    Stream stream;
    stream.record(3, 20 + 16);
    stream.u32(1);
    stream.u32(3); // target | z
    stream.u32(0xff102030u);
    stream.f32(1.0f);
    stream.u32(0);
    stream.u32(8);
    stream.u32(16);
    stream.u32(648);
    stream.u32(496);
    std::vector<uint8_t> bytes = stream.finish();
    OtHalVideoBackend video = backend();
    OtD3d7ReplayStats stats;
    CHECK(ot_d3d7_replay(bytes.data(), static_cast<uint32_t>(bytes.size()), &video, nullptr, nullptr, &stats));
    CHECK(clears.size() == 1 && clears[0].x == 8 && clears[0].w == 640 && clears[0].h == 480);
    bytes.resize(bytes.size() - 4); // cut the last record short
    CHECK(!ot_d3d7_replay(bytes.data(), static_cast<uint32_t>(bytes.size()), &video, nullptr, nullptr, &stats));
}

void test_blend_presets() {
    OtHalDrawState state;
    std::memset(&state, 0, sizeof(state));
    ot_hal_blend_preset(&state, OT_HAL_BLEND_SUBTRACT);
    CHECK((state.flags & OT_HAL_DRAW_BLEND) && state.blend_op == OT_HAL_BLEND_OP_REV_SUBTRACT);
    ot_hal_blend_preset(&state, OT_HAL_BLEND_ADD_QUARTER);
    CHECK(state.src_factor == OT_HAL_FACTOR_SRC_ALPHA && state.dst_factor == OT_HAL_FACTOR_ONE);
    ot_hal_blend_preset(&state, OT_HAL_BLEND_OPAQUE);
    CHECK(!(state.flags & OT_HAL_DRAW_BLEND));
}

// The pieces the live Direct3D 7 stand-in (video=sdl) uses.
void test_live_building_blocks() {
    CHECK(ot_d3d7_fvf_stride(0x044) == 20 && ot_d3d7_fvf_stride(0x144) == 28 && ot_d3d7_fvf_stride(0x1c4) == 32);
    CHECK(ot_d3d7_topology(6) == OT_HAL_TRIANGLE_FAN && ot_d3d7_topology(2) == OT_HAL_LINES);
    CHECK(ot_d3d7_topology(1) == 0xffffffffu); // points are not in the HAL
    const float position[4] = {3.0f, 4.0f, 0.25f, 2.0f};
    const uint32_t colors[2] = {0x80402010u, 0x7f000000u};
    const float uv[2] = {0.5f, 0.75f};
    uint8_t vertex[32];
    std::memcpy(vertex, position, 16);
    std::memcpy(vertex + 16, colors, 8);
    std::memcpy(vertex + 24, uv, 8);
    OtHalVertex out;
    CHECK(ot_d3d7_convert_vertex(vertex, 0x1c4, &out));
    CHECK(out.x == 3.0f && out.rhw == 2.0f && out.color == 0x80402010u && out.specular == 0x7f000000u && out.v == 0.75f);
    CHECK(ot_d3d7_convert_vertex(vertex, 0x044, &out) && out.specular == 0xff000000u && out.u == 0.0f);
    CHECK(!ot_d3d7_convert_vertex(vertex, 0x142, &out)); // untransformed XYZ
}

} // namespace

int main() {
    test_state_translation_and_fan();
    test_select_diffuse_drops_the_texture_and_missing_textures_are_counted();
    test_clear_rects_and_malformed_streams();
    test_blend_presets();
    test_live_building_blocks();
    if (failures != 0) {
        std::fprintf(stderr, "d3d7_stream_test: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("d3d7_stream_test: ok");
    return 0;
}
