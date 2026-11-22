#include "video_shadow.h"

#include "video_renderer.h"

#include "hal_adapters.h"
#include "../hook_runtime.h"
#include "../../hal/video.h"

#include <string.h>

// Ordering-table adapter, shadow mode (video_shadow.h). Mirrors the hardware
// path of D3D_DrawPolygonList (0x004d3160), D3D_DrawGeneralPolygon
// (0x004d68b0), D3D_DrawTexturedQuad (0x004d4bf0) and
// D3D_SubmitViewportRect (0x004d3600); see re/evidence/functions/hal-adapters.md
// for the tables it reads.

namespace {

enum RecordType { kDraw = 1, kViewport = 2, kUnsupported = 3 };
enum Unsupported { kUnknownPacket = 1, kOutline = 3 };

// D3D_SetTextureBlendMode: 0 = no depth test, 1 = test, 2 = test and write.
enum DepthMode { kDepthOff = 0, kDepthTest = 1, kDepthWrite = 2 };

const DWORD kBufferSize = 24u << 20;
unsigned char *g_buffer = 0;
DWORD g_used = 0;
DWORD g_records = 0;
int g_truncated = 0;
DWORD g_draws = 0;
DWORD g_unsupported = 0;
int g_record = 0; // this walk goes to the .otvh buffer
int g_live = 0;   // OtVideoShadowLive: this walk also draws through the HAL renderer

const uint32_t kMaxFan = 64;
OtHalVertex g_fan[kMaxFan];

template <typename T>
const T &at(const void *base, uint32_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const unsigned char *>(base) + offset);
}

const uint32_t *table(int symbol) {
    return reinterpret_cast<const uint32_t *>(ot_hal_data(symbol));
}

float global_float(int symbol) {
    return *reinterpret_cast<const float *>(ot_hal_data(symbol));
}

void record(unsigned type, const void *payload, DWORD size, const void *tail = 0, DWORD tail_size = 0) {
    if (!g_record) {
        return;
    }
    if (g_buffer == 0) {
        g_buffer = static_cast<unsigned char *>(VirtualAlloc(0, kBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (g_buffer == 0) {
            return;
        }
    }
    const DWORD total = size + tail_size;
    if (g_truncated || g_used + 8u + total > kBufferSize) {
        g_truncated = 1;
        return;
    }
    unsigned char *out = g_buffer + g_used;
    out[0] = static_cast<unsigned char>(type);
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    memcpy(out + 4, &total, 4);
    memcpy(out + 8, payload, size);
    if (tail_size != 0) {
        memcpy(out + 8 + size, tail, tail_size);
    }
    g_used += 8u + total;
    ++g_records;
}

void unsupported(uint32_t command, uint32_t reason) {
    const uint32_t payload[2] = {command, reason};
    record(kUnsupported, payload, sizeof(payload));
    ++g_unsupported;
}

void emit(const OtHalDrawState &state, uint32_t topology, const OtHalVertex *vertices, uint32_t count) {
    unsigned char head[sizeof(OtHalDrawState) + 8];
    memcpy(head, &state, sizeof(state));
    memcpy(head + sizeof(state), &topology, 4);
    memcpy(head + sizeof(state) + 4, &count, 4);
    record(kDraw, head, sizeof(head), vertices, count * static_cast<DWORD>(sizeof(OtHalVertex)));
    if (g_live != OT_VIDEO_SHADOW_RECORD_ONLY) {
        ot_video_renderer_draw(&state, topology, vertices, count);
    }
    ++g_draws;
}

// D3D_SetColorBlendMode(mode) as factors (alpha blending is always enabled).
void color_blend(OtHalDrawState &state, int mode) {
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

void depth(OtHalDrawState &state, int mode) {
    state.flags |= OT_HAL_DRAW_DEPTH_GREATER | OT_HAL_DRAW_PERSPECTIVE; // device-wide in retail
    if (mode != kDepthOff) state.flags |= OT_HAL_DRAW_DEPTH_TEST;
    if (mode == kDepthWrite) state.flags |= OT_HAL_DRAW_DEPTH_WRITE;
}

uint32_t level(uint32_t color) {
    return table(OT_HAL_SYM_ColorLevelRed)[(color >> 16) & 0xff] | table(OT_HAL_SYM_ColorLevelGreen)[(color >> 8) & 0xff] |
           table(OT_HAL_SYM_ColorLevelBlue)[color & 0xff];
}

// Depth-cue level for a vertex, or -1: retail's x87 sequence at
// 0x004d6a89..0x004d6b17 (evaluated under the game thread's control word).
int fog_level(const float *rhw) {
    const float threshold = global_float(OT_HAL_SYM_fogThreshold);
    const float unit = global_float(OT_HAL_SYM_RenderProjectionUnitScale);
    if (!(threshold < unit)) {
        return -1;
    }
    const unsigned char *edges = *reinterpret_cast<unsigned char *const *>(ot_hal_data(OT_HAL_SYM_RenderViewportEdges));
    int far_edge = at<uint16_t>(edges, 0xe);
    int span = at<uint16_t>(edges, 0xa);
    const float yon = global_float(OT_HAL_SYM_FogYonScale);
    const float scale = 255.0f;
    const float *yon_pointer = &yon;
    const float *threshold_pointer = &threshold;
    const float *unit_pointer = &unit;
    const float *scale_pointer = &scale;
    int result = -1;
    unsigned short control = 0;
    unsigned short chop = 0;
    __int64 value = 0;
    __asm {
        mov ecx, rhw
        mov edx, yon_pointer
        fild dword ptr [far_edge]
        fdiv dword ptr [ecx]
        fild dword ptr [span]
        fmul dword ptr [edx]
        fdivp st(1), st
        mov ecx, threshold_pointer
        fcom dword ptr [ecx]
        fnstsw ax
        test ah, 1
        je fogged
        fstp st(0)
        jmp done
    fogged:
        fsub dword ptr [ecx]
        mov edx, unit_pointer
        fld dword ptr [edx]
        fsub dword ptr [ecx]
        fdivp st(1), st
        fcom dword ptr [edx]
        fnstsw ax
        test ah, 0x41
        jne keep
        fstp st(0)
        fld dword ptr [edx]
    keep:
        mov ecx, scale_pointer
        fmul dword ptr [ecx]
        fnstcw control
        mov ax, control
        or ah, 0x0c
        mov chop, ax
        fldcw chop
        fistp qword ptr [value]
        fldcw control
        mov eax, dword ptr [value]
        mov result, eax
    done:
    }
    return result;
}

uint32_t fogged(uint32_t color, uint32_t alpha_index, int fog) {
    const uint32_t i = static_cast<uint32_t>(fog);
    return table(OT_HAL_SYM_rFog)[((color >> 16) & 0xff) * 256u + i] | table(OT_HAL_SYM_gFog)[((color >> 8) & 0xff) * 256u + i] |
           table(OT_HAL_SYM_aFog)[(alpha_index >> 24) * 256u + i] | table(OT_HAL_SYM_bFog)[(color & 0xff) * 256u + i];
}

// 0xB0: a PC polygon of pre-transformed vertices (D3D_DrawGeneralPolygon).
void general_polygon(const unsigned char *packet) {
    const uint32_t flags = at<uint32_t>(packet, 8);
    if ((flags & 0x30000000u) != 0) {
        unsupported(0xb0, kOutline); // outline passes (line strips): not in the Warehouse census yet
        if ((flags & 0x30000000u) == 0x10000000u) {
            return;
        }
    }
    const bool levels = (flags & 0x1c0u) != 0x140u;
    OtHalDrawState state;
    memset(&state, 0, sizeof(state));
    uint32_t alpha;
    int depth_mode;
    if ((flags & 0x40u) == 0) {
        alpha = 0xff000000u;
        color_blend(state, 1);
        depth_mode = kDepthWrite;
    } else {
        depth_mode = kDepthTest;
        switch (flags & 0x180u) {
        case 0x100u: alpha = 0; color_blend(state, 4); break;
        case 0x000u: alpha = 0x80000000u; color_blend(state, 1); break;
        case 0x080u: alpha = 0xff000000u; color_blend(state, 2); break;
        default: alpha = 0x40000000u; color_blend(state, 2); break;
        }
    }
    const uint32_t texture_record = at<uint32_t>(packet, 0x10);
    const uint32_t count = at<uint32_t>(packet, 0x14);
    if (count == 0 || count > kMaxFan) {
        unsupported(0xb0, kUnknownPacket);
        return;
    }
    const uint32_t stride = texture_record != 0 ? 28u : 20u;
    bool all_clear = true;
    for (uint32_t index = 0; index < count; ++index) {
        const unsigned char *source = packet + 0x18 + index * stride;
        OtHalVertex &vertex = g_fan[index];
        vertex.x = at<float>(source, 0);
        vertex.y = at<float>(source, 4);
        vertex.z = at<float>(source, 8);
        vertex.rhw = at<float>(source, 12);
        uint32_t color = (at<uint32_t>(source, 16) & 0x00ffffffu) | alpha;
        if ((flags & 0x80000000u) == 0) {
            const int fog = fog_level(&at<float>(source, 12));
            if (fog >= 0) {
                color = fogged(color, alpha, fog);
            }
        }
        if ((color & 0xff000000u) != 0) {
            all_clear = false;
        }
        if (levels) {
            color = level(color) | (color & 0xff000000u);
        }
        vertex.color = color;
        vertex.specular = 0xff000000u;
        vertex.u = texture_record != 0 ? at<float>(source, 20) : 0.0f;
        vertex.v = texture_record != 0 ? at<float>(source, 24) : 0.0f;
    }
    if (texture_record != 0) {
        if (alpha != 0 && all_clear) {
            return; // every vertex faded out
        }
        const uint32_t descriptor = at<uint32_t>(reinterpret_cast<const void *>(static_cast<UINT_PTR>(texture_record)), 0x14);
        const void *record_pointer = reinterpret_cast<const void *>(static_cast<UINT_PTR>(descriptor));
        state.texture = at<uint32_t>(record_pointer, 0); // the game's IDirectDrawSurface7
        state.flags |= OT_HAL_DRAW_MODULATE | OT_HAL_DRAW_FILTER;
        if (flags & 0x20u) {
            state.flags |= OT_HAL_DRAW_WRAP;
        }
        if ((at<uint8_t>(record_pointer, 0x10) & 0x10u) == 0 && depth_mode == kDepthWrite) {
            depth_mode = kDepthTest;
        }
    }
    depth(state, depth_mode);
    emit(state, OT_HAL_TRIANGLE_FAN, g_fan, count);
}

// Retail's oversize guard (FUN_004d44d0): bounding box wider than 1024 or
// taller than 512 pixels is dropped.
bool oversized(const OtHalVertex *vertices, uint32_t count) {
    float left = vertices[0].x, right = vertices[0].x, top = vertices[0].y, bottom = vertices[0].y;
    for (uint32_t index = 1; index < count; ++index) {
        if (vertices[index].x < left) left = vertices[index].x;
        if (vertices[index].x > right) right = vertices[index].x;
        if (vertices[index].y < top) top = vertices[index].y;
        if (vertices[index].y > bottom) bottom = vertices[index].y;
    }
    return right - left > 1024.0f || bottom - top > 512.0f;
}

// PSX packets with the PC extension (a float z after the vertices, then for
// textured kinds a texture record), as the D3D_Draw* functions read them:
// vertex, colour and texture-coordinate offsets in submission order (quads
// become fans 0, 1, 3, 2; line strips repeat their first point).
struct PsxLayout {
    uint8_t kind;
    uint8_t count;
    uint8_t topology;
    uint8_t z;
    uint8_t texture; // 0 = untextured
    bool guard;      // FUN_004d44d0 size check
    uint8_t xy[5];
    uint8_t rgb[5];  // per vertex; flat kinds repeat 4
    uint8_t uv[5];
};

const PsxLayout kLayouts[] = {
    {0x20, 3, OT_HAL_TRIANGLES, 0x14, 0, true, {0x08, 0x0c, 0x10}, {4, 4, 4}, {0}},                                  // F3
    {0x24, 3, OT_HAL_TRIANGLES, 0x20, 0x24, true, {0x08, 0x10, 0x18}, {4, 4, 4}, {0x0c, 0x14, 0x1c}},                  // FT3
    {0x28, 4, OT_HAL_TRIANGLE_FAN, 0x18, 0, true, {0x08, 0x0c, 0x14, 0x10}, {4, 4, 4, 4}, {0}},                        // F4
    {0x2c, 4, OT_HAL_TRIANGLE_FAN, 0x28, 0x2c, true, {0x08, 0x10, 0x20, 0x18}, {4, 4, 4, 4}, {0x0c, 0x14, 0x24, 0x1c}}, // FT4
    {0x30, 3, OT_HAL_TRIANGLES, 0x1c, 0, true, {0x08, 0x10, 0x18}, {4, 0x0c, 0x14}, {0}},                               // G3
    {0x34, 3, OT_HAL_TRIANGLES, 0x28, 0x2c, true, {0x08, 0x14, 0x20}, {4, 0x10, 0x1c}, {0x0c, 0x18, 0x24}},             // GT3
    {0x38, 4, OT_HAL_TRIANGLE_FAN, 0x24, 0, true, {0x08, 0x10, 0x20, 0x18}, {4, 0x0c, 0x1c, 0x14}, {0}},                // G4
    {0x3c, 4, OT_HAL_TRIANGLE_FAN, 0x34, 0x38, true, {0x08, 0x14, 0x2c, 0x20}, {4, 0x10, 0x28, 0x1c},
        {0x0c, 0x18, 0x30, 0x24}},                                                                                     // GT4
    {0x40, 2, OT_HAL_LINES, 0x10, 0, false, {0x08, 0x0c}, {4, 4}, {0}},                                                 // LINE_F2
    {0x48, 4, OT_HAL_LINE_STRIP, 0x18, 0, false, {0x08, 0x0c, 0x10, 0x08}, {4, 4, 4, 4}, {0}},                          // LINE_F3
    {0x4c, 5, OT_HAL_LINE_STRIP, 0x1c, 0, false, {0x08, 0x0c, 0x14, 0x10, 0x08}, {4, 4, 4, 4, 4}, {0}},                 // LINE_F4
    {0x50, 2, OT_HAL_LINES, 0x14, 0, false, {0x08, 0x10}, {4, 0x0c}, {0}},                                              // LINE_G2
};

uint32_t packet_color(const unsigned char *packet, uint32_t offset, uint32_t alpha) {
    return table(OT_HAL_SYM_ColorLevelBlue)[packet[offset + 2]] | table(OT_HAL_SYM_ColorLevelGreen)[packet[offset + 1]] |
           table(OT_HAL_SYM_ColorLevelRed)[packet[offset]] | alpha;
}

// The texture of a textured PSX packet: its record's descriptor (+0x14) must
// be set up; the surface is descriptor[1] -> +0x14 -> [0]. 0 = not drawn.
const void *packet_descriptor(const unsigned char *packet, uint32_t offset) {
    const uint32_t record_address = at<uint32_t>(packet, offset);
    if (record_address == 0) {
        return 0;
    }
    const uint32_t descriptor_address =
        at<uint32_t>(reinterpret_cast<const void *>(static_cast<UINT_PTR>(record_address)), 0x14);
    const void *descriptor = reinterpret_cast<const void *>(static_cast<UINT_PTR>(descriptor_address));
    if (descriptor == 0 || at<uint32_t>(descriptor, 0) == 0) {
        return 0;
    }
    return descriptor;
}

void psx_polygon(const PsxLayout &layout, const unsigned char *packet, bool raw, uint32_t alpha, int color_mode,
    int depth_mode) {
    const void *descriptor = 0;
    OtHalDrawState state;
    memset(&state, 0, sizeof(state));
    if (layout.texture != 0) {
        descriptor = packet_descriptor(packet, layout.texture);
        if (descriptor == 0) {
            return;
        }
        if ((at<uint8_t>(descriptor, 0x10) & 0x10u) == 0 && depth_mode != kDepthTest) {
            depth_mode = kDepthTest;
        }
    }
    float z = at<float>(packet, layout.z);
    if (z < 0.0f) { // RenderWindingThreshold (a data constant, 0)
        depth_mode = kDepthOff;
        z = 1.0f;
    }
    float su = 0.0f;
    float sv = 0.0f;
    if (descriptor != 0) {
        const float unit = global_float(OT_HAL_SYM_RenderProjectionUnitScale);
        int width = at<int16_t>(descriptor, 0x14);
        int height = at<int16_t>(descriptor, 0x16);
        if (width > 1) --width;
        if (height > 1) --height;
        su = unit / static_cast<float>(width);
        sv = unit / static_cast<float>(height);
    }
    const uint32_t raw_color = alpha | level(0x00ffffffu);
    for (uint32_t index = 0; index < layout.count; ++index) {
        OtHalVertex &vertex = g_fan[index];
        vertex.x = static_cast<float>(at<int16_t>(packet, layout.xy[index]));
        vertex.y = static_cast<float>(at<int16_t>(packet, layout.xy[index] + 2u));
        vertex.z = z;
        vertex.rhw = 1.0f;
        vertex.color = descriptor != 0 && raw ? raw_color : packet_color(packet, layout.rgb[index], alpha);
        vertex.specular = 0xff000000u;
        vertex.u = descriptor != 0 ? static_cast<float>(packet[layout.uv[index]]) * su : 0.0f;
        vertex.v = descriptor != 0 ? static_cast<float>(packet[layout.uv[index] + 1u]) * sv : 0.0f;
    }
    if (layout.guard && oversized(g_fan, layout.count)) {
        return;
    }
    color_blend(state, color_mode);
    if (descriptor != 0) {
        const uint32_t holder = at<uint32_t>(descriptor, 4);
        const uint32_t surface_holder = at<uint32_t>(reinterpret_cast<const void *>(static_cast<UINT_PTR>(holder)), 0x14);
        state.texture = at<uint32_t>(reinterpret_cast<const void *>(static_cast<UINT_PTR>(surface_holder)), 0);
        state.flags |= OT_HAL_DRAW_MODULATE | OT_HAL_DRAW_FILTER; // address: clamp
    }
    depth(state, depth_mode);
    emit(state, layout.topology, g_fan, layout.count);
}

// TILE (0x60, size in the packet) and TILE_1/8/16 (0x68/0x70/0x78):
// D3D_DrawSolidRectangle, a four-vertex fan.
void psx_tile(const unsigned char *packet, int32_t size, uint32_t alpha, int color_mode, int depth_mode) {
    float z = at<float>(packet, 0x10);
    if (z < 0.0f) {
        depth_mode = kDepthOff;
        z = 1.0f;
    }
    const float x = static_cast<float>(at<int16_t>(packet, 8));
    const float y = static_cast<float>(at<int16_t>(packet, 10));
    const float w = static_cast<float>(size != 0 ? size : at<int16_t>(packet, 0x0c));
    const float h = static_cast<float>(size != 0 ? size : at<int16_t>(packet, 0x0e));
    const float corners[4][2] = {{x, y}, {w + x, y}, {w + x, y + h}, {x, y + h}};
    const uint32_t color = packet_color(packet, 4, alpha);
    for (uint32_t index = 0; index < 4; ++index) {
        OtHalVertex &vertex = g_fan[index];
        vertex.x = corners[index][0];
        vertex.y = corners[index][1];
        vertex.z = z;
        vertex.rhw = 1.0f;
        vertex.color = color;
        vertex.specular = 0xff000000u;
        vertex.u = 0.0f;
        vertex.v = 0.0f;
    }
    if (oversized(g_fan, 4)) {
        return;
    }
    OtHalDrawState state;
    memset(&state, 0, sizeof(state));
    color_blend(state, color_mode);
    depth(state, depth_mode);
    emit(state, OT_HAL_TRIANGLE_FAN, g_fan, 4);
}

// DR_AREA-like viewport packet (0xE3): D3D_SubmitViewportRect's integer math.
void viewport(const unsigned char *packet) {
    const int32_t screen_width = static_cast<int32_t>(*table(OT_HAL_SYM_ScreenWidth));
    const int32_t screen_height = static_cast<int32_t>(*table(OT_HAL_SYM_ScreenHeight));
    const int16_t top_raw = static_cast<int16_t>(at<int16_t>(packet, 10) % 256); // retail wraps it (in place)
    if (g_live == OT_VIDEO_SHADOW_SOLE) {
        // D3D_SubmitViewportRect stores the wrapped top back into the packet.
        *reinterpret_cast<int16_t *>(const_cast<unsigned char *>(packet) + 10) = top_raw;
    }
    int32_t x = (at<int16_t>(packet, 8) * screen_width) >> 9;
    int32_t y = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(top_raw) * screen_height) / 0xf0u);
    int32_t w = (at<int16_t>(packet, 0xc) * screen_width) >> 9;
    int32_t h = (at<int16_t>(packet, 0xe) * screen_height) / 0xf0;
    x = x < 0 ? 0 : (x > screen_width ? screen_width : x);
    w = w < 0 ? 0 : (w > screen_width ? screen_width : w);
    y = y < 0 ? 0 : (y > screen_height ? screen_height : y);
    h = h < 0 ? 0 : (h > screen_height ? screen_height : h);
    if (w + x <= screen_width && h + y <= screen_height) {
        const int32_t rect[4] = {x, y, w, h};
        record(kViewport, rect, sizeof(rect));
        const OtHalVideoBackend *video = ot_video_renderer_backend();
        if (g_live == OT_VIDEO_SHADOW_SOLE && video != 0) {
            const OtHalRect area = {x, y, w, h};
            video->set_viewport(&area); // D3D_SetViewportRect
        }
    }
}

} // namespace

void ot_video_shadow_list(const void *list, int record, int live) {
    g_record = record;
    g_live = live;
    uint32_t blend_mode = *table(OT_HAL_SYM_TexpageBlendMode);
    for (const unsigned char *packet = static_cast<const unsigned char *>(list); packet != 0;
         packet = *reinterpret_cast<const unsigned char *const *>(packet)) {
        if (at<uint32_t>(packet, 4) == 0) {
            continue;
        }
        const uint8_t command = packet[7];
        if ((command & 0x80) == 0) {
            // PSX packet: state first (D3D_DrawPolygonList), then the primitive.
            const uint32_t kind = command & 0xfcu;
            const bool line = kind == 0x40 || kind == 0x48 || kind == 0x4c || kind == 0x50;
            bool texpage = false;
            uint32_t tpage = 0;
            if (!line && (command & 4)) {
                texpage = true;
                tpage = (command & 0x10) ? at<uint16_t>(packet, 0x1a) : at<uint16_t>(packet, 0x16);
            }
            uint32_t alpha = 0xff000000u;
            int color_mode = 1;
            int depth_mode = kDepthWrite;
            if (command & 2) {
                depth_mode = kDepthTest;
                if (texpage) {
                    blend_mode = (tpage >> 5) & 3u;
                }
                switch (blend_mode) {
                case 0: alpha = 0x80000000u; color_mode = 1; break;
                case 1: alpha = 0xff000000u; color_mode = 2; break;
                case 2: alpha = 0; color_mode = 4; break;
                default: alpha = 0x40000000u; color_mode = 2; break;
                }
            }
            const PsxLayout *layout = 0;
            for (uint32_t index = 0; index < sizeof(kLayouts) / sizeof(kLayouts[0]); ++index) {
                if (kLayouts[index].kind == kind) {
                    layout = &kLayouts[index];
                }
            }
            if (layout != 0) {
                psx_polygon(*layout, packet, (command & 1) != 0, alpha, color_mode, depth_mode);
            } else if (kind == 0x60 || kind == 0x68 || kind == 0x70 || kind == 0x78) {
                psx_tile(packet, kind == 0x60 ? 0 : kind == 0x68 ? 1 : kind == 0x70 ? 8 : 16, alpha, color_mode,
                    depth_mode);
            } else {
                unsupported(command, kUnknownPacket);
            }
        } else if (command == 0xe1) {
            blend_mode = (at<uint32_t>(packet, 4) >> 5) & 3u;
        } else if (command == 0xe3) {
            viewport(packet);
        } else if ((command & 0xfc) == 0xb0) {
            general_polygon(packet);
        } else {
            unsupported(command, kUnknownPacket);
        }
    }
    if (live == OT_VIDEO_SHADOW_SOLE) {
        *reinterpret_cast<uint32_t *>(ot_hal_data(OT_HAL_SYM_TexpageBlendMode)) = blend_mode; // as retail leaves it
    }
}

int ot_video_shadow_flush(const char *path, unsigned frame) {
    if (g_buffer == 0 && g_records == 0) {
        return 0;
    }
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD header[4] = {0x4856544fu /* OTVH */, g_truncated ? 0x80000001u : 1u, frame, g_records};
        DWORD written = 0;
        WriteFile(file, header, sizeof(header), &written, 0);
        if (g_used != 0) {
            WriteFile(file, g_buffer, g_used, &written, 0);
        }
        CloseHandle(file);
    }
    g_used = 0;
    g_records = 0;
    g_truncated = 0;
    return 1;
}

int ot_video_shadow_describe(char *out, int size) {
    char line[96];
    const int length = wsprintfA(line, "video_shadow draws=%u unsupported=%u", static_cast<unsigned>(g_draws),
        static_cast<unsigned>(g_unsupported));
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}
