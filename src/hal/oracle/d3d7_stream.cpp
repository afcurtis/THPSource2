#include "d3d7_stream.h"

#include <string.h>

// Direct3D 7 frame replay for the image oracle (d3d7_stream.h). The stream
// layout is written by src/hooks/hal/video_trace.cpp.

namespace {

// Record types (video_trace.cpp).
enum {
    kBeginScene = 1,
    kEndScene = 2,
    kClear = 3,
    kRenderState = 4,
    kStageState = 5,
    kTexture = 6,
    kDraw = 7,
    kDrawIndexed = 8,
    kViewport = 9
};

// D3DRENDERSTATETYPE
enum {
    kRsZEnable = 7,
    kRsShadeMode = 9,
    kRsZWriteEnable = 14,
    kRsZFunc = 23,
    kRsAlphaTestEnable = 15,
    kRsSrcBlend = 19,
    kRsDestBlend = 20,
    kRsAlphaRef = 24,
    kRsAlphaBlendEnable = 27,
    kRsFogEnable = 28,
    kRsFogColor = 34
};
// D3DTEXTURESTAGESTATETYPE
enum { kTssColorOp = 1, kTssAddress = 12, kTssAddressU = 13, kTssMagFilter = 16 };
// D3DTEXTUREOP
enum { kTopDisable = 1, kTopSelectArg1 = 2, kTopSelectArg2 = 3, kTopModulate = 4 };
const uint32_t kShadeFlat = 1;
const uint32_t kAddressWrap = 1;
const uint32_t kFilterLinear = 2;
// D3DCLEAR
const uint32_t kClearTarget = 1;
const uint32_t kClearZBuffer = 2;

const uint32_t kMaxVertices = 16384;
OtHalVertex g_vertices[kMaxVertices];

const uint32_t kTextureSlots = 4096;
uint64_t g_texture_hash[kTextureSlots];
OtHalHandle g_texture_handle[kTextureSlots];

uint32_t read32(const uint8_t *at) {
    return static_cast<uint32_t>(at[0]) | (static_cast<uint32_t>(at[1]) << 8) | (static_cast<uint32_t>(at[2]) << 16) |
           (static_cast<uint32_t>(at[3]) << 24);
}

float read_float(const uint8_t *at) {
    const uint32_t bits = read32(at);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

uint8_t blend_factor(uint32_t d3d) {
    switch (d3d) { // D3DBLEND
    case 1: return OT_HAL_FACTOR_ZERO;
    case 2: return OT_HAL_FACTOR_ONE;
    case 3: return OT_HAL_FACTOR_SRC_COLOR;
    case 4: return OT_HAL_FACTOR_INV_SRC_COLOR;
    case 5: return OT_HAL_FACTOR_SRC_ALPHA;
    case 6: return OT_HAL_FACTOR_INV_SRC_ALPHA;
    case 7: return OT_HAL_FACTOR_DST_ALPHA;
    case 8: return OT_HAL_FACTOR_INV_DST_ALPHA;
    case 9: return OT_HAL_FACTOR_DST_COLOR;
    case 10: return OT_HAL_FACTOR_INV_DST_COLOR;
    default: return OT_HAL_FACTOR_ONE;
    }
}

uint32_t topology(uint32_t primitive) {
    switch (primitive) { // D3DPRIMITIVETYPE
    case 2: return OT_HAL_LINES;
    case 3: return OT_HAL_LINE_STRIP;
    case 4: return OT_HAL_TRIANGLES;
    case 5: return OT_HAL_TRIANGLE_STRIP;
    case 6: return OT_HAL_TRIANGLE_FAN;
    default: return 0xffffffffu; // points: not in the HAL
    }
}

// Pre-transformed vertex (D3DFVF_XYZRHW | DIFFUSE [| SPECULAR] [| TEX1]).
bool convert_vertex(const uint8_t *at, uint32_t fvf, OtHalVertex *out) {
    if ((fvf & 0x00e) != 0x004) {
        return false; // untransformed vertices are not in the HAL
    }
    out->x = read_float(at);
    out->y = read_float(at + 4);
    out->z = read_float(at + 8);
    out->rhw = read_float(at + 12);
    at += 16;
    if (fvf & 0x010) at += 12;
    if (fvf & 0x020) at += 4;
    out->color = 0xffffffffu;
    out->specular = 0xff000000u;
    if (fvf & 0x040) {
        out->color = read32(at);
        at += 4;
    }
    if (fvf & 0x080) {
        out->specular = read32(at);
        at += 4;
    }
    out->u = 0.0f;
    out->v = 0.0f;
    if ((fvf >> 8) & 0xf) {
        out->u = read_float(at);
        out->v = read_float(at + 4);
    }
    return true;
}

OtHalHandle texture_for(uint64_t hash, uint32_t surface, const OtHalVideoBackend *video, OtD3d7TextureLoader loader,
    void *user, OtD3d7ReplayStats *stats) {
    if (hash == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    // One entry per (content, surface): two surfaces with the same pixels
    // stay distinct, which the shadow comparison relies on.
    uint64_t key = hash + static_cast<uint64_t>(surface) * 0x9e3779b97f4a7c15ull;
    if (key == 0) {
        key = 1;
    }
    uint32_t slot = static_cast<uint32_t>(key) & (kTextureSlots - 1u);
    for (uint32_t probe = 0; probe < kTextureSlots; ++probe, slot = (slot + 1u) & (kTextureSlots - 1u)) {
        if (g_texture_hash[slot] == key) {
            return g_texture_handle[slot];
        }
        if (g_texture_hash[slot] == 0) {
            OtHalTextureDesc desc;
            const void *pixels = 0;
            int32_t pitch = 0;
            OtHalHandle handle = OT_HAL_INVALID_HANDLE;
            if (loader != 0 && loader(user, hash, surface, &desc, &pixels, &pitch)) {
                handle = video->texture_create(&desc);
                if (handle != OT_HAL_INVALID_HANDLE) {
                    OtHalRect area = {0, 0, desc.width, desc.height};
                    video->texture_update(handle, &area, pixels, pitch);
                    ++stats->textures_loaded;
                }
            } else {
                ++stats->textures_missing;
            }
            g_texture_hash[slot] = key;
            g_texture_handle[slot] = handle;
            return handle;
        }
    }
    return OT_HAL_INVALID_HANDLE;
}

} // namespace

extern "C" uint32_t ot_d3d7_topology(uint32_t primitive) {
    return topology(primitive);
}

extern "C" int ot_d3d7_convert_vertex(const uint8_t *at, uint32_t fvf, OtHalVertex *out) {
    return convert_vertex(at, fvf, out) ? 1 : 0;
}

extern "C" uint32_t ot_d3d7_fvf_stride(uint32_t fvf) {
    uint32_t stride = 0;
    switch (fvf & 0x00e) {
    case 0x002: stride = 12; break; // XYZ
    case 0x004: stride = 16; break; // XYZRHW
    case 0x006: case 0x008: case 0x00a: case 0x00c: case 0x00e:
        stride = 12 + 4 * (((fvf & 0x00e) - 0x004) / 2); // XYZB1..B5
        break;
    default: break;
    }
    if (fvf & 0x010) stride += 12; // normal
    if (fvf & 0x020) stride += 4;  // point size / reserved1
    if (fvf & 0x040) stride += 4;  // diffuse
    if (fvf & 0x080) stride += 4;  // specular
    stride += ((fvf >> 8) & 0xf) * 8;
    return stride;
}

extern "C" int ot_ottx_parse(const uint8_t *data, uint32_t size, OtHalTextureDesc *desc, const uint8_t **pixels,
    int32_t *pitch) {
    if (size < 48 || memcmp(data, "OTTX", 4) != 0) {
        return 0;
    }
    const uint32_t width = read32(data + 8);
    const uint32_t height = read32(data + 12);
    const uint32_t bits = read32(data + 16);
    const uint32_t red = read32(data + 20);
    const uint32_t green = read32(data + 24);
    const uint32_t blue = read32(data + 28);
    const uint32_t alpha = read32(data + 32);
    const uint32_t row = width * (bits / 8u);
    if (bits == 0 || width == 0 || height == 0 || size < 48u + row * height) {
        return 0;
    }
    if (bits == 16 && red == 0x7c00u && green == 0x03e0u && blue == 0x001fu) {
        desc->format = OT_HAL_PIXEL_ARGB1555; // alpha 0x8000 or none (then every texel's bit is 0)
        if (alpha == 0) {
            return 0; // X1R5G5B5: alpha is not stored; not a HAL format
        }
    } else if (bits == 16 && red == 0x0f00u && green == 0x00f0u && blue == 0x000fu && alpha == 0xf000u) {
        desc->format = OT_HAL_PIXEL_ARGB4444;
    } else if (bits == 16 && red == 0xf800u && green == 0x07e0u && blue == 0x001fu) {
        desc->format = OT_HAL_PIXEL_RGB565;
    } else if (bits == 32 && red == 0x00ff0000u && green == 0x0000ff00u && blue == 0x000000ffu) {
        desc->format = OT_HAL_PIXEL_ARGB8888;
    } else {
        return 0;
    }
    desc->width = static_cast<int32_t>(width);
    desc->height = static_cast<int32_t>(height);
    desc->palette = OT_HAL_INVALID_HANDLE;
    *pixels = data + 48;
    *pitch = static_cast<int32_t>(row);
    return 1;
}

extern "C" void ot_d3d7_draw_state(const uint32_t *render, const uint32_t *stage, OtHalHandle texture,
    OtHalDrawState *state) {
    memset(state, 0, sizeof(*state));
    const uint32_t color_op = stage[kTssColorOp];
    if (texture != OT_HAL_INVALID_HANDLE && (color_op == kTopModulate || color_op == kTopSelectArg1)) {
        state->texture = texture;
        if (color_op == kTopModulate) {
            state->flags |= OT_HAL_DRAW_MODULATE;
        }
    }
    if (render[kRsZEnable] != 0) state->flags |= OT_HAL_DRAW_DEPTH_TEST;
    // D3DCMP_GREATER (5) / GREATEREQUAL (7): retail clears depth to 0 and
    // keeps the larger z. Other comparisons are treated as LESSEQUAL.
    if (render[kRsZFunc] == 5 || render[kRsZFunc] == 7) state->flags |= OT_HAL_DRAW_DEPTH_GREATER;
    // With D3DRS_ZENABLE off Direct3D writes no depth either, whatever
    // D3DRS_ZWRITEENABLE says (retail leaves it as it was).
    if (render[kRsZEnable] != 0 && render[kRsZWriteEnable] != 0) state->flags |= OT_HAL_DRAW_DEPTH_WRITE;
    if (render[kRsAlphaTestEnable] != 0) {
        state->flags |= OT_HAL_DRAW_ALPHA_TEST;
        state->alpha_ref = static_cast<uint8_t>(render[kRsAlphaRef]);
    }
    if (state->texture != OT_HAL_INVALID_HANDLE) { // sampling state only matters with a texture
        if (stage[kTssMagFilter] == kFilterLinear) state->flags |= OT_HAL_DRAW_FILTER;
        if (stage[kTssAddressU] == kAddressWrap) state->flags |= OT_HAL_DRAW_WRAP;
    }
    if (render[kRsShadeMode] == kShadeFlat) state->flags |= OT_HAL_DRAW_FLAT;
    // Retail turns D3DRS_TEXTUREPERSPECTIVE off, but every Direct3D 7
    // implementation it runs on today (wined3d, current Windows drivers)
    // ignores that switch and interpolates perspective-correctly from rhw.
    state->flags |= OT_HAL_DRAW_PERSPECTIVE;
    if (render[kRsFogEnable] != 0) {
        state->flags |= OT_HAL_DRAW_FOG;
        state->fog_color = render[kRsFogColor] & 0x00ffffffu;
    }
    if (render[kRsAlphaBlendEnable] != 0) {
        state->flags |= OT_HAL_DRAW_BLEND;
        state->src_factor = blend_factor(render[kRsSrcBlend]);
        state->dst_factor = blend_factor(render[kRsDestBlend]);
        state->blend_op = OT_HAL_BLEND_OP_ADD;
    } else {
        state->src_factor = OT_HAL_FACTOR_ONE;
        state->dst_factor = OT_HAL_FACTOR_ZERO;
    }
}

extern "C" int ot_d3d7_replay(const uint8_t *stream, uint32_t size, const OtHalVideoBackend *video,
    OtD3d7TextureLoader loader, void *user, OtD3d7ReplayStats *stats) {
    uint32_t render[256];
    uint32_t stage[2][32];
    uint64_t bound[2] = {0, 0};
    uint32_t bound_surface[2] = {0, 0};
    memset(stats, 0, sizeof(*stats));
    memset(render, 0, sizeof(render));
    memset(stage, 0, sizeof(stage));
    memset(g_texture_hash, 0, sizeof(g_texture_hash));
    memset(g_texture_handle, 0, sizeof(g_texture_handle));
    render[kRsZEnable] = 1;
    render[kRsZWriteEnable] = 1;
    render[kRsShadeMode] = 2;
    stage[0][kTssColorOp] = kTopModulate;
    if (size < 16 || memcmp(stream, "OTVS", 4) != 0 || (read32(stream + 4) & 0x7fffffffu) != 1u) {
        return 0;
    }
    const uint32_t count = read32(stream + 12);
    uint32_t offset = 16;
    for (uint32_t index = 0; index < count; ++index) {
        if (offset + 8u > size) {
            return 0;
        }
        const uint32_t type = static_cast<uint32_t>(stream[offset]) | (static_cast<uint32_t>(stream[offset + 1]) << 8);
        const uint32_t length = read32(stream + offset + 4);
        const uint8_t *payload = stream + offset + 8;
        offset += 8u + length;
        if (offset > size) {
            return 0;
        }
        ++stats->records;
        switch (type) {
        case kClear: {
            const uint32_t flags = read32(payload + 4);
            uint32_t hal = 0;
            if (flags & kClearTarget) hal |= OT_HAL_CLEAR_COLOR;
            if (flags & kClearZBuffer) hal |= OT_HAL_CLEAR_DEPTH;
            const uint32_t rects = (length - 20u) / 16u;
            if (rects == 0) {
                video->clear(hal, read32(payload + 8), read_float(payload + 12), 0);
            }
            for (uint32_t rect = 0; rect < rects; ++rect) {
                const uint8_t *at = payload + 20u + rect * 16u;
                OtHalRect area;
                area.x = static_cast<int32_t>(read32(at));
                area.y = static_cast<int32_t>(read32(at + 4));
                area.w = static_cast<int32_t>(read32(at + 8)) - area.x;
                area.h = static_cast<int32_t>(read32(at + 12)) - area.y;
                video->clear(hal, read32(payload + 8), read_float(payload + 12), &area);
            }
            break;
        }
        case kRenderState: {
            const uint32_t state = read32(payload);
            if (state < 256) render[state] = read32(payload + 4);
            break;
        }
        case kStageState: {
            const uint32_t which = read32(payload);
            const uint32_t state = read32(payload + 4);
            if (which < 2 && state < 32) {
                stage[which][state] = read32(payload + 8);
                if (state == kTssAddress) { // D3DTSS_ADDRESS sets both U and V
                    stage[which][kTssAddressU] = stage[which][state];
                    stage[which][kTssAddressU + 1] = stage[which][state];
                }
            }
            break;
        }
        case kTexture: {
            const uint32_t which = read32(payload);
            uint64_t hash = 0;
            memcpy(&hash, payload + 8, sizeof(hash));
            if (which < 2) {
                bound[which] = hash;
                bound_surface[which] = length >= 20 ? read32(payload + 16) : 0;
            }
            break;
        }
        case kViewport: {
            OtHalRect area;
            area.x = static_cast<int32_t>(read32(payload));
            area.y = static_cast<int32_t>(read32(payload + 4));
            area.w = static_cast<int32_t>(read32(payload + 8));
            area.h = static_cast<int32_t>(read32(payload + 12));
            video->set_viewport(&area);
            break;
        }
        case kDraw:
        case kDrawIndexed: {
            const uint32_t primitive = read32(payload);
            const uint32_t fvf = read32(payload + 4);
            const uint32_t vertex_count = read32(payload + 8);
            const uint32_t index_count = type == kDrawIndexed ? read32(payload + 12) : 0;
            const uint32_t stride = read32(payload + (type == kDrawIndexed ? 20 : 16));
            const uint8_t *vertices = payload + (type == kDrawIndexed ? 24 : 20);
            const uint8_t *indices = vertices + stride * vertex_count;
            const uint32_t emitted = type == kDrawIndexed ? index_count : vertex_count;
            const uint32_t hal_topology = topology(primitive);
            ++stats->draws;
            if (fvf == 0 || hal_topology == 0xffffffffu || emitted > kMaxVertices) {
                ++stats->unsupported;
                break;
            }
            bool ok = true;
            for (uint32_t vertex = 0; vertex < emitted && ok; ++vertex) {
                uint32_t source = vertex;
                if (type == kDrawIndexed) {
                    source = static_cast<uint32_t>(indices[vertex * 2u]) |
                             (static_cast<uint32_t>(indices[vertex * 2u + 1u]) << 8);
                    ok = source < vertex_count;
                }
                ok = ok && convert_vertex(vertices + source * stride, fvf, &g_vertices[vertex]);
            }
            if (!ok) {
                ++stats->unsupported;
                break;
            }
            const OtHalHandle texture =
                ((fvf >> 8) & 0xf) != 0 ? texture_for(bound[0], bound_surface[0], video, loader, user, stats)
                                        : OT_HAL_INVALID_HANDLE;
            OtHalDrawState state;
            ot_d3d7_draw_state(render, stage[0], texture, &state);
            video->draw(&state, hal_topology, g_vertices, emitted);
            stats->vertices += emitted;
            break;
        }
        default:
            break; // scene brackets
        }
    }
    return 1;
}
