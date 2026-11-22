// opentony_video_shadow_compare <frame.otvs> <frame.otvh> [--verbose]
//
// Primitive-stream oracle: replays the retail Direct3D 7 stream of a frame
// (OPENTONY_VIDEO_TRACE) onto a recording HAL backend and compares its draws
// with the ones the ordering-table adapter produced in shadow mode for the
// same frame (src/hooks/hal/video_shadow.cpp): state and every vertex, bit
// for bit, in order. Exit status 0 when every draw matches.

#include "d3d7_stream.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct Draw {
    OtHalDrawState state;
    uint32_t topology;
    std::vector<OtHalVertex> vertices;
};

std::vector<Draw> retail;
uint32_t current_surface = 0;

std::vector<uint8_t> read_file(const char *path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

int load_texture(void *, uint64_t, uint32_t surface, OtHalTextureDesc *desc, const void **pixels, int32_t *pitch) {
    static const uint32_t texel = 0;
    current_surface = surface;
    desc->width = 1;
    desc->height = 1;
    desc->format = OT_HAL_PIXEL_ARGB8888;
    desc->palette = 0;
    *pixels = &texel;
    *pitch = 4;
    return 1;
}

// Texture handles are the game's surface pointers, as the shadow records them.
OtHalHandle OT_HAL_CALL texture_create(const OtHalTextureDesc *) { return current_surface; }
void OT_HAL_CALL texture_update(OtHalHandle, const OtHalRect *, const void *, int32_t) {}
void OT_HAL_CALL clear(uint32_t, uint32_t, float, const OtHalRect *) {}
void OT_HAL_CALL set_viewport(const OtHalRect *) {}
void OT_HAL_CALL draw(const OtHalDrawState *state, uint32_t topology, const OtHalVertex *vertices, uint32_t count) {
    retail.push_back(Draw{*state, topology, std::vector<OtHalVertex>(vertices, vertices + count)});
}

uint32_t read32(const uint8_t *at) {
    uint32_t value;
    std::memcpy(&value, at, 4);
    return value;
}

bool read_shadow(const std::vector<uint8_t> &data, std::vector<Draw> &draws, uint32_t &unsupported) {
    if (data.size() < 16 || std::memcmp(data.data(), "OTVH", 4) != 0) {
        return false;
    }
    const uint32_t count = read32(&data[12]);
    size_t offset = 16;
    for (uint32_t index = 0; index < count; ++index) {
        if (offset + 8 > data.size()) return false;
        const uint32_t type = data[offset] | (data[offset + 1] << 8);
        const uint32_t size = read32(&data[offset + 4]);
        const uint8_t *payload = &data[offset + 8];
        offset += 8 + size;
        if (offset > data.size()) return false;
        if (type == 1) {
            Draw draw;
            std::memcpy(&draw.state, payload, sizeof(OtHalDrawState));
            draw.topology = read32(payload + 16);
            const uint32_t vertices = read32(payload + 20);
            draw.vertices.resize(vertices);
            std::memcpy(draw.vertices.data(), payload + 24, vertices * sizeof(OtHalVertex));
            draws.push_back(draw);
        } else if (type == 3) {
            ++unsupported;
        }
    }
    return true;
}

std::string describe(const Draw &draw) {
    char text[256];
    std::snprintf(text, sizeof(text), "tex %08x flags %04x blend %u/%u op %u topo %u n %zu", draw.state.texture,
        draw.state.flags, draw.state.src_factor, draw.state.dst_factor, draw.state.blend_op, draw.topology,
        draw.vertices.size());
    return text;
}

std::string first_difference(const Draw &a, const Draw &b) {
    if (std::memcmp(&a.state, &b.state, sizeof(OtHalDrawState)) != 0 || a.topology != b.topology ||
        a.vertices.size() != b.vertices.size()) {
        return "state: retail " + describe(a) + " | shadow " + describe(b);
    }
    for (size_t index = 0; index < a.vertices.size(); ++index) {
        if (std::memcmp(&a.vertices[index], &b.vertices[index], sizeof(OtHalVertex)) != 0) {
            const OtHalVertex &x = a.vertices[index];
            const OtHalVertex &y = b.vertices[index];
            char text[400];
            std::snprintf(text, sizeof(text),
                "vertex %zu: retail (%.9g %.9g %.9g %.9g %08x %.9g %.9g) | shadow (%.9g %.9g %.9g %.9g %08x %.9g %.9g)",
                index, x.x, x.y, x.z, x.rhw, x.color, x.u, x.v, y.x, y.y, y.z, y.rhw, y.color, y.u, y.v);
            return text;
        }
    }
    return "";
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::puts("usage: opentony_video_shadow_compare <frame.otvs> <frame.otvh> [--verbose]");
        return 2;
    }
    const bool verbose = argc > 3 && std::strcmp(argv[3], "--verbose") == 0;
    const std::vector<uint8_t> stream = read_file(argv[1]);
    const std::vector<uint8_t> shadow_file = read_file(argv[2]);
    OtHalVideoBackend video;
    std::memset(&video, 0, sizeof(video));
    video.texture_create = texture_create;
    video.texture_update = texture_update;
    video.clear = clear;
    video.set_viewport = set_viewport;
    video.draw = draw;
    OtD3d7ReplayStats stats;
    if (!ot_d3d7_replay(stream.data(), static_cast<uint32_t>(stream.size()), &video, load_texture, nullptr, &stats)) {
        std::printf("FAIL %s: not a retail stream\n", argv[1]);
        return 2;
    }
    std::vector<Draw> shadow;
    uint32_t unsupported = 0;
    if (!read_shadow(shadow_file, shadow, unsupported)) {
        std::printf("FAIL %s: not a shadow stream\n", argv[2]);
        return 2;
    }
    size_t matched = 0;
    size_t reported = 0;
    const size_t common = retail.size() < shadow.size() ? retail.size() : shadow.size();
    for (size_t index = 0; index < common; ++index) {
        const std::string difference = first_difference(retail[index], shadow[index]);
        if (difference.empty()) {
            ++matched;
        } else if (reported < (verbose ? 50u : 5u)) {
            std::printf("  draw %zu: %s\n", index, difference.c_str());
            ++reported;
        }
    }
    const bool ok = matched == retail.size() && retail.size() == shadow.size() && unsupported == 0;
    std::printf("%s retail %zu draws, shadow %zu draws, %zu identical, %u unsupported packets\n", ok ? "MATCH" : "DIFFER",
        retail.size(), shadow.size(), matched, unsupported);
    return ok ? 0 : 1;
}
