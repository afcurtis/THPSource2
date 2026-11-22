#include "sdl_internal.h"
#include "sdl_video_gpu.h"
#include "sdl_video.h" // ot_hal_sdl_video_gl_hints/error, for the auto fallback path only

#include "../oracle/video_gpu.h"
#include "sdl_video_gpu_shaders.h"

#include <string.h>

// Video service on SDL3 + SDL_GPU (docs/HAL.md, "Video", 3.4d). Vulkan on
// this project's tested platforms (probed under headless Wine + Mesa
// lavapipe, and Windows 11 + NVIDIA): the preferred backend, with GL 3.3
// (sdl_video_gl.cpp) as the fallback when a device cannot be created.
//
// Reproduces the same Direct3D 7 conventions the GL backend already honours
// (video.vert/video.frag are its shaders ported line for line): pixel
// centres on integers, w = 1/rhw only with perspective texturing, reversed
// depth, first-vertex flat shading. SDL flips y itself for a Vulkan
// swapchain/render target, so no manual row flip is needed anywhere here
// (probed: NDC y-up, top-left scissor and read-back).
//
// SDL_GPU cannot upload inside a render pass, so draws are RECORDED into a
// command list at record time (draw/draw_sprite/clear) and only actually
// submitted at flush() (present, read_back, set_mode, shutdown, a size cap,
// or before a texture mutation that would otherwise reorder a draw already
// recorded against it in the same still-open batch): one command buffer,
// one copy pass (texture re-uploads, then the vertex upload, both
// cycle=true growable buffers), one render pass replaying the recorded
// commands with LOAD ops onto the persistent offscreen target.

namespace {

// -------------------------------------------------------------- constants

const uint32_t kMaxTextures = 8192;
const uint32_t kMaxPalettes = 1024;
const uint32_t kMaxBatchVertices = 65536;
const uint32_t kMaxBatchCommands = 8192;
const uint32_t kSamplerCount = 4; // nearest/linear x clamp/repeat
const uint32_t kMaxPipelines = 128;

enum SamplerKind { kSamplerNearestClamp = 0, kSamplerNearestRepeat = 1, kSamplerLinearClamp = 2, kSamplerLinearRepeat = 3 };

// ------------------------------------------------------------------ state

struct Texture {
    SDL_GPUTexture *handle;
    int32_t width;
    int32_t height;
    uint32_t format;         // OtHalPixelFormat
    OtHalHandle palette;
    uint32_t palette_version; // last converted against
    uint8_t *indices;         // indexed formats: one raw index byte per texel, kept for palette changes
    uint32_t *rgba;           // CPU shadow of the GPU texture's contents (width * height)
    uint8_t dirty;            // rgba differs from what the GPU texture holds
    uint32_t last_batch;      // g_open_batch when last referenced by a recorded draw
};

struct Palette {
    uint32_t used;
    uint32_t version;
    uint32_t count;
    uint32_t colors[256];
};

struct Pipeline {
    uint32_t key;
    SDL_GPUGraphicsPipeline *handle;
};

// depth_mode: 0 = test off, 1 = LEQUAL, 2 = GEQUAL (reversed), 3 = ALWAYS (clears)
struct Command {
    SDL_GPUPrimitiveType primitive;
    uint32_t first;
    uint32_t count;
    OtHalHandle texture;
    uint32_t sampler;
    SDL_Rect scissor;
    float target_w, target_h, perspective;
    int32_t mode, flat, alpha_test;
    float alpha_ref;
    uint32_t blend;
    uint8_t src_factor, dst_factor, blend_op;
    uint8_t depth_mode;
    uint8_t depth_write;
    uint8_t color_write;
};

Texture g_textures[kMaxTextures];
Palette g_palettes[kMaxPalettes];
Pipeline g_pipelines[kMaxPipelines];
uint32_t g_pipeline_count = 0;

SDL_GPUDevice *g_device = 0;
SDL_GPUShader *g_vertex_shader = 0;
SDL_GPUShader *g_fragment_shader = 0;
SDL_GPUSampler *g_samplers[kSamplerCount];
SDL_GPUTexture *g_color_target = 0;
SDL_GPUTexture *g_depth_target = 0;
SDL_GPUTextureFormat g_depth_format = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
SDL_GPUBuffer *g_vertex_buffer = 0;
uint32_t g_vertex_buffer_capacity = 0; // in vertices
SDL_GPUTransferBuffer *g_vertex_transfer = 0;
uint32_t g_vertex_transfer_capacity = 0; // in bytes
SDL_GPUTexture *g_dummy_texture = 0;
int g_window_claimed = 0;
int g_target_needs_load = 0; // the target textures hold undefined contents until the first flush

OtHalVertex g_batch_vertices[kMaxBatchVertices];
uint32_t g_batch_vertex_count = 0;
Command g_batch_commands[kMaxBatchCommands];
uint32_t g_batch_command_count = 0;
uint32_t g_open_batch = 1; // tag for "the batch currently being recorded"

// Fan -> triangle-list conversion scratch (game thread only, reused every
// draw instead of a per-call stack array): generous ceiling on one draw
// call's input vertex count (retail's own submissions are small polygons).
const uint32_t kMaxFanVertices = 4096;
uint32_t g_fan_indices[(kMaxFanVertices - 2) * 3];
OtHalVertex g_fan_scratch[(kMaxFanVertices - 2) * 3];

OtHalRect g_scissor;
int g_has_scissor = 0;
OtHalVideoMode g_mode;
uint32_t *g_read_back = 0;
OtHalVideoGpuStats g_stats;
char g_error[256];
char g_driver[32];

void set_error(const char *text) {
    OT_SDL(SDL_strlcpy)(g_error, text, sizeof(g_error));
}

// ---------------------------------------------------------- pipeline cache

SDL_GPUPrimitiveType gpu_primitive(uint32_t topology) {
    switch (topology) {
    case OT_HAL_TRIANGLE_STRIP: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    case OT_HAL_LINES: return SDL_GPU_PRIMITIVETYPE_LINELIST;
    case OT_HAL_LINE_STRIP: return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
    default: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; // OT_HAL_TRIANGLES and fans (pre-converted)
    }
}

SDL_GPUBlendFactor gpu_factor(uint8_t factor) {
    switch (factor) {
    case OT_HAL_FACTOR_ZERO: return SDL_GPU_BLENDFACTOR_ZERO;
    case OT_HAL_FACTOR_SRC_COLOR: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case OT_HAL_FACTOR_INV_SRC_COLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case OT_HAL_FACTOR_SRC_ALPHA: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case OT_HAL_FACTOR_INV_SRC_ALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case OT_HAL_FACTOR_DST_COLOR: return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case OT_HAL_FACTOR_INV_DST_COLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case OT_HAL_FACTOR_DST_ALPHA: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case OT_HAL_FACTOR_INV_DST_ALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    default: return SDL_GPU_BLENDFACTOR_ONE;
    }
}

SDL_GPUBlendOp gpu_blend_op(uint8_t op) {
    switch (op) {
    case OT_HAL_BLEND_OP_SUBTRACT: return SDL_GPU_BLENDOP_SUBTRACT;
    case OT_HAL_BLEND_OP_REV_SUBTRACT: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
    default: return SDL_GPU_BLENDOP_ADD;
    }
}

uint32_t pipeline_key(const Command &command) {
    uint32_t key = static_cast<uint32_t>(command.primitive);
    key = key * 2u + (command.blend ? 1u : 0u);
    key = key * 16u + command.src_factor;
    key = key * 16u + command.dst_factor;
    key = key * 4u + command.blend_op;
    key = key * 8u + command.depth_mode;
    key = key * 2u + (command.depth_write ? 1u : 0u);
    key = key * 2u + (command.color_write ? 1u : 0u);
    return key;
}

SDL_GPUGraphicsPipeline *pipeline_for(const Command &command) {
    const uint32_t key = pipeline_key(command);
    for (uint32_t index = 0; index < g_pipeline_count; ++index) {
        if (g_pipelines[index].key == key) {
            return g_pipelines[index].handle;
        }
    }
    if (g_pipeline_count >= kMaxPipelines) {
        set_error("pipeline cache exhausted");
        return 0;
    }
    SDL_GPUVertexBufferDescription buffer_desc;
    buffer_desc.slot = 0;
    buffer_desc.pitch = sizeof(OtHalVertex);
    buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffer_desc.instance_step_rate = 0;
    SDL_GPUVertexAttribute attributes[3];
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(OtHalVertex, x)};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(OtHalVertex, color)};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(OtHalVertex, u)};
    SDL_GPUColorTargetBlendState blend;
    memset(&blend, 0, sizeof(blend));
    blend.enable_blend = command.blend != 0;
    blend.src_color_blendfactor = gpu_factor(command.src_factor);
    blend.dst_color_blendfactor = gpu_factor(command.dst_factor);
    blend.color_blend_op = gpu_blend_op(command.blend_op);
    blend.src_alpha_blendfactor = gpu_factor(command.src_factor);
    blend.dst_alpha_blendfactor = gpu_factor(command.dst_factor);
    blend.alpha_blend_op = gpu_blend_op(command.blend_op);
    blend.enable_color_write_mask = command.color_write == 0;
    blend.color_write_mask = command.color_write != 0 ? 0xfu : 0u;
    SDL_GPUColorTargetDescription color_target;
    color_target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    color_target.blend_state = blend;
    SDL_GPUGraphicsPipelineCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.vertex_shader = g_vertex_shader;
    info.fragment_shader = g_fragment_shader;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer_desc;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = command.primitive;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.enable_depth_test = command.depth_mode != 0;
    info.depth_stencil_state.enable_depth_write = command.depth_write != 0;
    info.depth_stencil_state.compare_op = command.depth_mode == 2   ? SDL_GPU_COMPAREOP_GREATER_OR_EQUAL
                                          : command.depth_mode == 3 ? SDL_GPU_COMPAREOP_ALWAYS
                                                                    : SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = g_depth_format;
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline *handle = OT_SDL(SDL_CreateGPUGraphicsPipeline)(g_device, &info);
    if (handle == 0) {
        set_error(OT_SDL(SDL_GetError)());
        return 0;
    }
    g_pipelines[g_pipeline_count].key = key;
    g_pipelines[g_pipeline_count].handle = handle;
    ++g_pipeline_count;
    ++g_stats.pipelines;
    return handle;
}

// -------------------------------------------------------------- textures

// Regenerates the whole RGBA8 shadow of an indexed texture from its kept
// index bytes and the current palette (mirrors sdl_video_gl.cpp's
// upload_indexed, on the CPU shadow instead of a live GL texture).
void reconvert_indexed(Texture &texture) {
    if (texture.indices == 0 || texture.rgba == 0) {
        return;
    }
    const uint32_t *palette = texture.palette != 0 && texture.palette < kMaxPalettes && g_palettes[texture.palette].used
                                   ? g_palettes[texture.palette].colors
                                   : 0;
    for (int32_t y = 0; y < texture.height; ++y) {
        ot_gpu_convert_row(OT_HAL_PIXEL_INDEX8, texture.indices + static_cast<size_t>(y) * texture.width,
            texture.width, palette, texture.rgba + static_cast<size_t>(y) * texture.width);
    }
    texture.palette_version = texture.palette < kMaxPalettes ? g_palettes[texture.palette].version : 0;
    texture.dirty = 1;
}

void refresh_indexed_if_stale(Texture &texture) {
    if (texture.indices != 0 && texture.palette != 0 && texture.palette < kMaxPalettes &&
        g_palettes[texture.palette].version != texture.palette_version) {
        reconvert_indexed(texture);
    }
}

void flush(); // forward

// A texture already drawn in the still-open batch is about to change:
// flush first so the pending draws see its old contents.
void flush_if_pending(const Texture &texture) {
    if (texture.last_batch == g_open_batch && (g_batch_command_count != 0 || g_batch_vertex_count != 0)) {
        flush();
    }
}

// ------------------------------------------------------------------ init

bool create_samplers() {
    for (uint32_t index = 0; index < kSamplerCount; ++index) {
        SDL_GPUSamplerCreateInfo info;
        memset(&info, 0, sizeof(info));
        const bool linear = index == kSamplerLinearClamp || index == kSamplerLinearRepeat;
        const bool repeat = index == kSamplerNearestRepeat || index == kSamplerLinearRepeat;
        info.min_filter = linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mag_filter = info.min_filter;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u = repeat ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        info.address_mode_v = info.address_mode_u;
        info.address_mode_w = info.address_mode_u;
        info.max_lod = 1.0f;
        g_samplers[index] = OT_SDL(SDL_CreateGPUSampler)(g_device, &info);
        if (g_samplers[index] == 0) {
            set_error(OT_SDL(SDL_GetError)());
            return false;
        }
    }
    return true;
}

bool create_shaders() {
    SDL_GPUShaderCreateInfo vertex;
    memset(&vertex, 0, sizeof(vertex));
    vertex.code = kOtGpuVideoVertSpv;
    vertex.code_size = kOtGpuVideoVertSpvSize;
    vertex.entrypoint = "main";
    vertex.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertex.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex.num_uniform_buffers = 1;
    g_vertex_shader = OT_SDL(SDL_CreateGPUShader)(g_device, &vertex);
    SDL_GPUShaderCreateInfo fragment;
    memset(&fragment, 0, sizeof(fragment));
    fragment.code = kOtGpuVideoFragSpv;
    fragment.code_size = kOtGpuVideoFragSpvSize;
    fragment.entrypoint = "main";
    fragment.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragment.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment.num_samplers = 1;
    fragment.num_uniform_buffers = 1;
    g_fragment_shader = OT_SDL(SDL_CreateGPUShader)(g_device, &fragment);
    if (g_vertex_shader == 0 || g_fragment_shader == 0) {
        set_error(OT_SDL(SDL_GetError)());
        return false;
    }
    return true;
}

bool create_dummy_texture() {
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = 1;
    info.height = 1;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    g_dummy_texture = OT_SDL(SDL_CreateGPUTexture)(g_device, &info);
    if (g_dummy_texture == 0) {
        set_error(OT_SDL(SDL_GetError)());
        return false;
    }
    SDL_GPUTransferBufferCreateInfo transfer_info;
    memset(&transfer_info, 0, sizeof(transfer_info));
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = 4;
    SDL_GPUTransferBuffer *transfer = OT_SDL(SDL_CreateGPUTransferBuffer)(g_device, &transfer_info);
    if (transfer == 0) {
        set_error(OT_SDL(SDL_GetError)());
        return false;
    }
    void *mapped = OT_SDL(SDL_MapGPUTransferBuffer)(g_device, transfer, false);
    const uint32_t white = 0xffffffffu;
    memcpy(mapped, &white, 4);
    OT_SDL(SDL_UnmapGPUTransferBuffer)(g_device, transfer);
    SDL_GPUCommandBuffer *command_buffer = OT_SDL(SDL_AcquireGPUCommandBuffer)(g_device);
    SDL_GPUCopyPass *copy_pass = OT_SDL(SDL_BeginGPUCopyPass)(command_buffer);
    SDL_GPUTextureTransferInfo source = {transfer, 0, 1, 1};
    SDL_GPUTextureRegion destination = {g_dummy_texture, 0, 0, 0, 0, 0, 1, 1, 1};
    OT_SDL(SDL_UploadToGPUTexture)(copy_pass, &source, &destination, false);
    OT_SDL(SDL_EndGPUCopyPass)(copy_pass);
    OT_SDL(SDL_SubmitGPUCommandBuffer)(command_buffer);
    OT_SDL(SDL_ReleaseGPUTransferBuffer)(g_device, transfer);
    return true;
}

bool create_target(int32_t width, int32_t height) {
    if (g_color_target != 0) {
        OT_SDL(SDL_ReleaseGPUTexture)(g_device, g_color_target);
    }
    if (g_depth_target != 0) {
        OT_SDL(SDL_ReleaseGPUTexture)(g_device, g_depth_target);
    }
    SDL_GPUTextureCreateInfo color_info;
    memset(&color_info, 0, sizeof(color_info));
    color_info.type = SDL_GPU_TEXTURETYPE_2D;
    color_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    color_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    color_info.width = static_cast<Uint32>(width);
    color_info.height = static_cast<Uint32>(height);
    color_info.layer_count_or_depth = 1;
    color_info.num_levels = 1;
    color_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    g_color_target = OT_SDL(SDL_CreateGPUTexture)(g_device, &color_info);
    g_depth_format = OT_SDL(SDL_GPUTextureSupportsFormat)(g_device, SDL_GPU_TEXTUREFORMAT_D24_UNORM,
                          SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                          ? SDL_GPU_TEXTUREFORMAT_D24_UNORM
                          : SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    SDL_GPUTextureCreateInfo depth_info = color_info;
    depth_info.format = g_depth_format;
    depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    g_depth_target = OT_SDL(SDL_CreateGPUTexture)(g_device, &depth_info);
    if (g_color_target == 0 || g_depth_target == 0) {
        set_error(OT_SDL(SDL_GetError)());
        return false;
    }
    g_target_needs_load = 0; // first render pass after (re)creation must not LOAD undefined contents
    if (g_read_back != 0) {
        OT_SDL(SDL_free)(g_read_back);
    }
    g_read_back = static_cast<uint32_t *>(OT_SDL(SDL_malloc)(static_cast<size_t>(width) * height * 4u));
    return g_read_back != 0;
}

} // namespace

// ------------------------------------------------------------ probe/prepare

int ot_hal_sdl_video_gpu_probe(void) {
    if (g_device != 0) {
        return 1;
    }
    g_device = OT_SDL(SDL_CreateGPUDevice)(SDL_GPU_SHADERFORMAT_SPIRV, false, 0);
    if (g_device == 0) {
        set_error(OT_SDL(SDL_GetError)());
        return 0;
    }
    OT_SDL(SDL_strlcpy)(g_driver, OT_SDL(SDL_GetGPUDeviceDriver)(g_device), sizeof(g_driver));
    return 1;
}

void ot_hal_sdl_video_gpu_release(void) {
    if (g_device != 0 && !g_window_claimed) {
        OT_SDL(SDL_DestroyGPUDevice)(g_device);
        g_device = 0;
        g_driver[0] = 0;
    }
}

const char *ot_hal_sdl_video_gpu_driver(void) {
    return g_driver;
}

namespace {

int OT_HAL_CALL video_init(const OtHalVideoMode *mode) {
    if (!ot_hal_sdl_video_gpu_probe()) {
        return 0;
    }
    SDL_Window *window = ot_hal_sdl_window_get();
    if (window == 0) {
        set_error("no SDL window");
        return 0;
    }
    if (!OT_SDL(SDL_ClaimWindowForGPUDevice)(g_device, window)) {
        set_error(OT_SDL(SDL_GetError)());
        return 0;
    }
    g_window_claimed = 1;
    if (!create_shaders() || !create_samplers() || !create_dummy_texture()) {
        return 0;
    }
    memset(g_textures, 0, sizeof(g_textures));
    memset(g_palettes, 0, sizeof(g_palettes));
    memset(&g_stats, 0, sizeof(g_stats));
    g_pipeline_count = 0;
    g_batch_vertex_count = 0;
    g_batch_command_count = 0;
    g_open_batch = 1;
    g_has_scissor = 0;
    g_mode = *mode;
    if (!create_target(mode->width, mode->height)) {
        return 0;
    }
    return 1;
}

void OT_HAL_CALL video_shutdown(void) {
    if (g_device == 0) {
        return;
    }
    for (uint32_t index = 1; index < kMaxTextures; ++index) {
        Texture &texture = g_textures[index];
        if (texture.handle != 0) {
            OT_SDL(SDL_ReleaseGPUTexture)(g_device, texture.handle);
        }
        if (texture.indices != 0) {
            OT_SDL(SDL_free)(texture.indices);
        }
        if (texture.rgba != 0) {
            OT_SDL(SDL_free)(texture.rgba);
        }
    }
    for (uint32_t index = 0; index < g_pipeline_count; ++index) {
        OT_SDL(SDL_ReleaseGPUGraphicsPipeline)(g_device, g_pipelines[index].handle);
    }
    for (uint32_t index = 0; index < kSamplerCount; ++index) {
        OT_SDL(SDL_ReleaseGPUSampler)(g_device, g_samplers[index]);
    }
    if (g_vertex_buffer != 0) {
        OT_SDL(SDL_ReleaseGPUBuffer)(g_device, g_vertex_buffer);
    }
    if (g_vertex_transfer != 0) {
        OT_SDL(SDL_ReleaseGPUTransferBuffer)(g_device, g_vertex_transfer);
    }
    if (g_dummy_texture != 0) {
        OT_SDL(SDL_ReleaseGPUTexture)(g_device, g_dummy_texture);
    }
    if (g_color_target != 0) {
        OT_SDL(SDL_ReleaseGPUTexture)(g_device, g_color_target);
    }
    if (g_depth_target != 0) {
        OT_SDL(SDL_ReleaseGPUTexture)(g_device, g_depth_target);
    }
    OT_SDL(SDL_ReleaseGPUShader)(g_device, g_vertex_shader);
    OT_SDL(SDL_ReleaseGPUShader)(g_device, g_fragment_shader);
    if (g_window_claimed) {
        SDL_Window *window = ot_hal_sdl_window_get();
        if (window != 0) {
            OT_SDL(SDL_ReleaseWindowFromGPUDevice)(g_device, window);
        }
        g_window_claimed = 0;
    }
    OT_SDL(SDL_DestroyGPUDevice)(g_device);
    g_device = 0;
    g_vertex_buffer = 0;
    g_vertex_buffer_capacity = 0;
    g_vertex_transfer = 0;
    g_vertex_transfer_capacity = 0;
    g_dummy_texture = 0;
    g_color_target = 0;
    g_depth_target = 0;
    g_pipeline_count = 0;
    if (g_read_back != 0) {
        OT_SDL(SDL_free)(g_read_back);
        g_read_back = 0;
    }
}

int OT_HAL_CALL video_set_mode(const OtHalVideoMode *mode) {
    flush();
    g_mode = *mode;
    return create_target(mode->width, mode->height);
}

uint32_t OT_HAL_CALL video_enumerate_modes(OtHalVideoMode *modes, uint32_t capacity) {
    static const int32_t kSizes[][2] = {{512, 384}, {640, 480}, {800, 600}, {1024, 768}, {1280, 960}};
    const uint32_t count = sizeof(kSizes) / sizeof(kSizes[0]);
    for (uint32_t index = 0; index < count && index < capacity; ++index) {
        modes[index].width = kSizes[index][0];
        modes[index].height = kSizes[index][1];
        modes[index].bits_per_pixel = 32;
        modes[index].fullscreen = 0;
        modes[index].vsync = 1;
    }
    return count;
}

// -------------------------------------------------------------- textures

OtHalHandle OT_HAL_CALL video_texture_create(const OtHalTextureDesc *desc) {
    for (uint32_t index = 1; index < kMaxTextures; ++index) {
        Texture &texture = g_textures[index];
        if (texture.handle != 0) {
            continue;
        }
        SDL_GPUTextureCreateInfo info;
        memset(&info, 0, sizeof(info));
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<Uint32>(desc->width);
        info.height = static_cast<Uint32>(desc->height);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        texture.handle = OT_SDL(SDL_CreateGPUTexture)(g_device, &info);
        if (texture.handle == 0) {
            set_error(OT_SDL(SDL_GetError)());
            return OT_HAL_INVALID_HANDLE;
        }
        texture.width = desc->width;
        texture.height = desc->height;
        texture.format = desc->format;
        texture.palette = desc->palette;
        texture.palette_version = 0;
        texture.last_batch = 0;
        const size_t texels = static_cast<size_t>(desc->width) * static_cast<size_t>(desc->height);
        texture.rgba = static_cast<uint32_t *>(OT_SDL(SDL_malloc)(texels * 4u));
        memset(texture.rgba, 0, texels * 4u);
        texture.indices =
            (desc->format == OT_HAL_PIXEL_INDEX8 || desc->format == OT_HAL_PIXEL_INDEX4)
                ? static_cast<uint8_t *>(OT_SDL(SDL_malloc)(texels))
                : 0;
        if (texture.indices != 0) {
            memset(texture.indices, 0, texels);
        }
        texture.dirty = 1; // upload the (zeroed) contents on first use
        ++g_stats.textures;
        return index;
    }
    return OT_HAL_INVALID_HANDLE;
}

void OT_HAL_CALL video_texture_update(OtHalHandle handle, const OtHalRect *area, const void *pixels, int32_t pitch) {
    if (handle == 0 || handle >= kMaxTextures || g_textures[handle].handle == 0) {
        return;
    }
    Texture &texture = g_textures[handle];
    flush_if_pending(texture);
    const uint8_t *rows = static_cast<const uint8_t *>(pixels);
    if (texture.indices != 0) {
        const bool packed4 = texture.format == OT_HAL_PIXEL_INDEX4;
        for (int32_t y = 0; y < area->h; ++y) {
            uint8_t *out = texture.indices + static_cast<size_t>(area->y + y) * texture.width + area->x;
            const uint8_t *in = rows + y * pitch;
            for (int32_t x = 0; x < area->w; ++x) {
                out[x] = packed4 ? static_cast<uint8_t>((in[x / 2] >> ((x & 1) * 4)) & 0xfu) : in[x];
            }
        }
        reconvert_indexed(texture);
        return;
    }
    for (int32_t y = 0; y < area->h; ++y) {
        ot_gpu_convert_row(texture.format, rows + y * pitch, area->w, 0,
            texture.rgba + static_cast<size_t>(area->y + y) * texture.width + area->x);
    }
    texture.dirty = 1;
}

void OT_HAL_CALL video_texture_destroy(OtHalHandle handle) {
    if (handle == 0 || handle >= kMaxTextures || g_textures[handle].handle == 0) {
        return;
    }
    flush_if_pending(g_textures[handle]);
    OT_SDL(SDL_ReleaseGPUTexture)(g_device, g_textures[handle].handle);
    if (g_textures[handle].indices != 0) {
        OT_SDL(SDL_free)(g_textures[handle].indices);
    }
    if (g_textures[handle].rgba != 0) {
        OT_SDL(SDL_free)(g_textures[handle].rgba);
    }
    memset(&g_textures[handle], 0, sizeof(Texture));
}

OtHalHandle OT_HAL_CALL video_palette_create(uint32_t count) {
    for (uint32_t index = 1; index < kMaxPalettes; ++index) {
        if (!g_palettes[index].used) {
            memset(&g_palettes[index], 0, sizeof(Palette));
            g_palettes[index].used = 1;
            g_palettes[index].count = count > 256u ? 256u : count;
            return index;
        }
    }
    return OT_HAL_INVALID_HANDLE;
}

void OT_HAL_CALL video_palette_update(OtHalHandle handle, uint32_t first, uint32_t count, const uint32_t *colors) {
    if (handle == 0 || handle >= kMaxPalettes || !g_palettes[handle].used) {
        return;
    }
    Palette &palette = g_palettes[handle];
    for (uint32_t index = 0; index < count && first + index < 256u; ++index) {
        palette.colors[first + index] = colors[index];
    }
    ++palette.version; // textures using it reconvert on next draw (draw() checks the version)
}

void OT_HAL_CALL video_palette_destroy(OtHalHandle handle) {
    if (handle != 0 && handle < kMaxPalettes) {
        g_palettes[handle].used = 0;
    }
}

// ---------------------------------------------------------------- frame

void OT_HAL_CALL video_begin_frame(void) {
    ++g_stats.frames;
}

SDL_Rect full_target_scissor() {
    SDL_Rect rect = {0, 0, g_mode.width, g_mode.height};
    return rect;
}

SDL_Rect to_sdl_rect(const OtHalRect &area) {
    SDL_Rect rect = {area.x, area.y, area.w, area.h};
    return rect;
}

// Appends `count` vertices (already flattened: no fans) and one command
// referencing them; flushes first if the batch is full (the size cap).
void append_command(Command command, const OtHalVertex *vertices, uint32_t count) {
    if (g_batch_vertex_count + count > kMaxBatchVertices || g_batch_command_count >= kMaxBatchCommands) {
        flush();
    }
    command.first = g_batch_vertex_count;
    command.count = count;
    memcpy(g_batch_vertices + g_batch_vertex_count, vertices, static_cast<size_t>(count) * sizeof(OtHalVertex));
    g_batch_vertex_count += count;
    g_batch_commands[g_batch_command_count++] = command;
    ++g_stats.draws;
    g_stats.vertices += count;
}

// Fans are reordered into a triangle list at record time (probed: SDL_GPU's
// Vulkan backend provokes the last vertex; (i+1, i+2, 0) per triangle keeps
// the fan's shared first vertex as the flat colour, matching GL's
// first-vertex convention). Everything else keeps its native topology.
// Writes into the shared g_fan_scratch buffer (game thread only).
void record_geometry(uint32_t topology, const OtHalVertex *vertices, uint32_t count, const OtHalVertex **out_vertices,
    uint32_t *out_count) {
    if (topology != OT_HAL_TRIANGLE_FAN) {
        *out_vertices = vertices;
        *out_count = count;
        return;
    }
    const uint32_t safe_count = count > kMaxFanVertices ? kMaxFanVertices : count;
    const uint32_t triangles = ot_gpu_fan_to_list_indices(safe_count, g_fan_indices);
    for (uint32_t index = 0; index < triangles * 3u; ++index) {
        g_fan_scratch[index] = vertices[g_fan_indices[index]];
    }
    *out_vertices = g_fan_scratch;
    *out_count = triangles * 3u;
}

void OT_HAL_CALL video_draw(const OtHalDrawState *state, uint32_t topology, const OtHalVertex *vertices,
    uint32_t count) {
    if (count == 0 || g_device == 0) {
        return;
    }
    Command command;
    memset(&command, 0, sizeof(command));
    command.primitive = gpu_primitive(topology);
    command.texture = OT_HAL_INVALID_HANDLE;
    command.sampler = kSamplerNearestClamp;
    command.mode = 0;
    if (state->texture != OT_HAL_INVALID_HANDLE && state->texture < kMaxTextures &&
        g_textures[state->texture].handle != 0) {
        Texture &texture = g_textures[state->texture];
        refresh_indexed_if_stale(texture);
        command.texture = state->texture;
        const bool linear = (state->flags & OT_HAL_DRAW_FILTER) != 0;
        const bool repeat = (state->flags & OT_HAL_DRAW_WRAP) != 0;
        command.sampler = (linear ? 2u : 0u) + (repeat ? 1u : 0u);
        command.mode = (state->flags & OT_HAL_DRAW_MODULATE) ? 2 : 1;
        texture.last_batch = g_open_batch;
    }
    command.scissor = g_has_scissor ? to_sdl_rect(g_scissor) : full_target_scissor();
    command.target_w = static_cast<float>(g_mode.width);
    command.target_h = static_cast<float>(g_mode.height);
    command.perspective = (state->flags & OT_HAL_DRAW_PERSPECTIVE) ? 1.0f : 0.0f;
    command.flat = (state->flags & OT_HAL_DRAW_FLAT) ? 1 : 0;
    command.alpha_test = (state->flags & OT_HAL_DRAW_ALPHA_TEST) ? 1 : 0;
    command.alpha_ref = state->alpha_ref / 255.0f;
    command.blend = (state->flags & OT_HAL_DRAW_BLEND) ? 1u : 0u;
    command.src_factor = state->src_factor;
    command.dst_factor = state->dst_factor;
    command.blend_op = state->blend_op;
    command.depth_mode = (state->flags & OT_HAL_DRAW_DEPTH_TEST)
                              ? ((state->flags & OT_HAL_DRAW_DEPTH_GREATER) ? 2 : 1)
                              : 0;
    command.depth_write = (state->flags & OT_HAL_DRAW_DEPTH_WRITE) ? 1 : 0;
    command.color_write = 1;
    const OtHalVertex *out_vertices = vertices;
    uint32_t out_count = count;
    record_geometry(topology, vertices, count, &out_vertices, &out_count);
    if (out_count != 0) {
        append_command(command, out_vertices, out_count);
    }
}

void OT_HAL_CALL video_draw_sprite(const OtHalDrawState *state, const OtHalRect *source, const OtHalRect *target,
    uint32_t color) {
    if (state->texture == OT_HAL_INVALID_HANDLE || state->texture >= kMaxTextures) {
        return;
    }
    const Texture &texture = g_textures[state->texture];
    const float u0 = static_cast<float>(source->x) / texture.width;
    const float v0 = static_cast<float>(source->y) / texture.height;
    const float u1 = static_cast<float>(source->x + source->w) / texture.width;
    const float v1 = static_cast<float>(source->y + source->h) / texture.height;
    const float x0 = static_cast<float>(target->x) - 0.5f;
    const float y0 = static_cast<float>(target->y) - 0.5f;
    const float x1 = x0 + static_cast<float>(target->w);
    const float y1 = y0 + static_cast<float>(target->h);
    OtHalVertex quad[4] = {
        {x0, y0, 0.0f, 1.0f, color, 0xff000000u, u0, v0},
        {x1, y0, 0.0f, 1.0f, color, 0xff000000u, u1, v0},
        {x1, y1, 0.0f, 1.0f, color, 0xff000000u, u1, v1},
        {x0, y1, 0.0f, 1.0f, color, 0xff000000u, u0, v1},
    };
    video_draw(state, OT_HAL_TRIANGLE_FAN, quad, 4);
}

void OT_HAL_CALL video_set_viewport(const OtHalRect *area) {
    g_scissor = *area;
    g_has_scissor = 1;
}

void OT_HAL_CALL video_set_scissor(const OtHalRect *area) {
    if (area != 0) {
        g_scissor = *area;
        g_has_scissor = 1;
    } else {
        g_has_scissor = 0;
    }
}

// A clear is a recorded full-target quad, clipped to `area` (or the whole
// target), depth-always, colour/depth write from the requested flags.
void OT_HAL_CALL video_clear(uint32_t flags, uint32_t color, float depth, const OtHalRect *area) {
    Command command;
    memset(&command, 0, sizeof(command));
    command.primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    command.texture = OT_HAL_INVALID_HANDLE;
    command.sampler = kSamplerNearestClamp;
    command.mode = 0;
    command.scissor = area != 0 ? to_sdl_rect(*area) : full_target_scissor();
    command.target_w = static_cast<float>(g_mode.width);
    command.target_h = static_cast<float>(g_mode.height);
    command.flat = 0;
    command.alpha_test = 0;
    command.blend = 0;
    command.depth_mode = 3; // ALWAYS
    command.depth_write = (flags & OT_HAL_CLEAR_DEPTH) ? 1 : 0;
    command.color_write = (flags & OT_HAL_CLEAR_COLOR) ? 1 : 0;
    OtHalVertex fan[4];
    ot_gpu_clear_quad(g_mode.width, g_mode.height, depth, color, fan);
    uint32_t indices[6];
    const uint32_t triangles = ot_gpu_fan_to_list_indices(4, indices);
    OtHalVertex list[6];
    for (uint32_t index = 0; index < triangles * 3u; ++index) {
        list[index] = fan[indices[index]];
    }
    append_command(command, list, triangles * 3u);
}

bool grow_vertex_buffers(uint32_t vertex_count) {
    if (vertex_count <= g_vertex_buffer_capacity) {
        return true;
    }
    uint32_t capacity = g_vertex_buffer_capacity == 0 ? 4096u : g_vertex_buffer_capacity;
    while (capacity < vertex_count) {
        capacity *= 2u;
    }
    if (g_vertex_buffer != 0) {
        OT_SDL(SDL_ReleaseGPUBuffer)(g_device, g_vertex_buffer);
    }
    SDL_GPUBufferCreateInfo buffer_info;
    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = capacity * static_cast<Uint32>(sizeof(OtHalVertex));
    g_vertex_buffer = OT_SDL(SDL_CreateGPUBuffer)(g_device, &buffer_info);
    if (g_vertex_transfer != 0) {
        OT_SDL(SDL_ReleaseGPUTransferBuffer)(g_device, g_vertex_transfer);
    }
    SDL_GPUTransferBufferCreateInfo transfer_info;
    memset(&transfer_info, 0, sizeof(transfer_info));
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = buffer_info.size;
    g_vertex_transfer = OT_SDL(SDL_CreateGPUTransferBuffer)(g_device, &transfer_info);
    g_vertex_buffer_capacity = capacity;
    g_vertex_transfer_capacity = buffer_info.size;
    return g_vertex_buffer != 0 && g_vertex_transfer != 0;
}

void flush() {
    if (g_device == 0) {
        return;
    }
    if (g_batch_command_count == 0 && g_batch_vertex_count == 0) {
        return;
    }
    SDL_GPUCommandBuffer *command_buffer = OT_SDL(SDL_AcquireGPUCommandBuffer)(g_device);
    if (command_buffer == 0) {
        set_error(OT_SDL(SDL_GetError)());
        return;
    }
    SDL_GPUCopyPass *copy_pass = OT_SDL(SDL_BeginGPUCopyPass)(command_buffer);
    for (uint32_t index = 1; index < kMaxTextures; ++index) {
        Texture &texture = g_textures[index];
        if (texture.handle == 0 || !texture.dirty) {
            continue;
        }
        const size_t bytes = static_cast<size_t>(texture.width) * texture.height * 4u;
        SDL_GPUTransferBufferCreateInfo transfer_info;
        memset(&transfer_info, 0, sizeof(transfer_info));
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = static_cast<Uint32>(bytes);
        SDL_GPUTransferBuffer *transfer = OT_SDL(SDL_CreateGPUTransferBuffer)(g_device, &transfer_info);
        if (transfer == 0) {
            continue;
        }
        void *mapped = OT_SDL(SDL_MapGPUTransferBuffer)(g_device, transfer, false);
        memcpy(mapped, texture.rgba, bytes);
        OT_SDL(SDL_UnmapGPUTransferBuffer)(g_device, transfer);
        SDL_GPUTextureTransferInfo source = {transfer, 0, static_cast<Uint32>(texture.width),
            static_cast<Uint32>(texture.height)};
        SDL_GPUTextureRegion destination = {texture.handle, 0, 0, 0, 0, 0, static_cast<Uint32>(texture.width),
            static_cast<Uint32>(texture.height), 1};
        OT_SDL(SDL_UploadToGPUTexture)(copy_pass, &source, &destination, true);
        OT_SDL(SDL_ReleaseGPUTransferBuffer)(g_device, transfer);
        texture.dirty = 0;
    }
    if (g_batch_vertex_count != 0 && grow_vertex_buffers(g_batch_vertex_count)) {
        void *mapped = OT_SDL(SDL_MapGPUTransferBuffer)(g_device, g_vertex_transfer, true);
        memcpy(mapped, g_batch_vertices, static_cast<size_t>(g_batch_vertex_count) * sizeof(OtHalVertex));
        OT_SDL(SDL_UnmapGPUTransferBuffer)(g_device, g_vertex_transfer);
        SDL_GPUTransferBufferLocation source = {g_vertex_transfer, 0};
        SDL_GPUBufferRegion destination = {g_vertex_buffer, 0,
            static_cast<Uint32>(g_batch_vertex_count * sizeof(OtHalVertex))};
        OT_SDL(SDL_UploadToGPUBuffer)(copy_pass, &source, &destination, true);
    }
    OT_SDL(SDL_EndGPUCopyPass)(copy_pass);

    SDL_GPUColorTargetInfo color_target;
    memset(&color_target, 0, sizeof(color_target));
    color_target.texture = g_color_target;
    color_target.load_op = g_target_needs_load ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth_target;
    memset(&depth_target, 0, sizeof(depth_target));
    depth_target.texture = g_depth_target;
    depth_target.load_op = g_target_needs_load ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    g_target_needs_load = 1;
    SDL_GPURenderPass *render_pass = OT_SDL(SDL_BeginGPURenderPass)(command_buffer, &color_target, 1, &depth_target);
    SDL_GPUViewport viewport = {0.0f, 0.0f, static_cast<float>(g_mode.width), static_cast<float>(g_mode.height), 0.0f,
        1.0f};
    OT_SDL(SDL_SetGPUViewport)(render_pass, &viewport);
    SDL_GPUBufferBinding vertex_binding = {g_vertex_buffer, 0};
    OT_SDL(SDL_BindGPUVertexBuffers)(render_pass, 0, &vertex_binding, 1);
    SDL_GPUGraphicsPipeline *bound_pipeline = 0;
    for (uint32_t index = 0; index < g_batch_command_count; ++index) {
        const Command &command = g_batch_commands[index];
        SDL_GPUGraphicsPipeline *pipeline = pipeline_for(command);
        if (pipeline == 0) {
            continue;
        }
        if (pipeline != bound_pipeline) {
            OT_SDL(SDL_BindGPUGraphicsPipeline)(render_pass, pipeline);
            bound_pipeline = pipeline;
        }
        OT_SDL(SDL_SetGPUScissor)(render_pass, &command.scissor);
        SDL_GPUTexture *bound_texture =
            command.texture != OT_HAL_INVALID_HANDLE ? g_textures[command.texture].handle : g_dummy_texture;
        SDL_GPUTextureSamplerBinding sampler_binding = {bound_texture, g_samplers[command.sampler]};
        OT_SDL(SDL_BindGPUFragmentSamplers)(render_pass, 0, &sampler_binding, 1);
        const float vertex_uniforms[4] = {command.target_w, command.target_h, command.perspective, 0.0f};
        OT_SDL(SDL_PushGPUVertexUniformData)(command_buffer, 0, vertex_uniforms, sizeof(vertex_uniforms));
        // Exactly the 16 bytes video.frag's FragmentUniforms block declares
        // (std140: four 4-byte scalars, no interior padding) - int,int,int,float.
        struct FragmentUniforms {
            int32_t mode;
            int32_t flat_shading;
            int32_t alpha_test;
            float alpha_ref;
        };
        const FragmentUniforms fragment_uniforms = {command.mode, command.flat, command.alpha_test,
            command.alpha_ref};
        OT_SDL(SDL_PushGPUFragmentUniformData)(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
        OT_SDL(SDL_DrawGPUPrimitives)(render_pass, command.count, 1, command.first, 0);
    }
    OT_SDL(SDL_EndGPURenderPass)(render_pass);
    OT_SDL(SDL_SubmitGPUCommandBuffer)(command_buffer);

    g_batch_vertex_count = 0;
    g_batch_command_count = 0;
    ++g_open_batch;
    ++g_stats.batches;
}

void OT_HAL_CALL video_present(void) {
    flush();
    SDL_Window *window = ot_hal_sdl_window_get();
    if (window == 0 || g_device == 0) {
        return;
    }
    SDL_GPUCommandBuffer *command_buffer = OT_SDL(SDL_AcquireGPUCommandBuffer)(g_device);
    if (command_buffer == 0) {
        return;
    }
    SDL_GPUTexture *swapchain = 0;
    Uint32 swapchain_width = 0;
    Uint32 swapchain_height = 0;
    if (!OT_SDL(SDL_WaitAndAcquireGPUSwapchainTexture)(command_buffer, window, &swapchain, &swapchain_width,
            &swapchain_height) ||
        swapchain == 0) {
        OT_SDL(SDL_SubmitGPUCommandBuffer)(command_buffer);
        return;
    }
    // Same 4:3-preserving letterbox arithmetic as the GL backend.
    int shown_width = static_cast<int>(swapchain_width);
    int shown_height = g_mode.width > 0
                            ? static_cast<int>(static_cast<long long>(swapchain_width) * g_mode.height / g_mode.width)
                            : static_cast<int>(swapchain_height);
    if (shown_height > static_cast<int>(swapchain_height)) {
        shown_height = static_cast<int>(swapchain_height);
        shown_width = g_mode.height > 0
                          ? static_cast<int>(static_cast<long long>(swapchain_height) * g_mode.width / g_mode.height)
                          : static_cast<int>(swapchain_width);
    }
    const int left = (static_cast<int>(swapchain_width) - shown_width) / 2;
    const int bottom = (static_cast<int>(swapchain_height) - shown_height) / 2;
    SDL_GPUBlitInfo blit;
    memset(&blit, 0, sizeof(blit));
    blit.source.texture = g_color_target;
    blit.source.w = static_cast<Uint32>(g_mode.width);
    blit.source.h = static_cast<Uint32>(g_mode.height);
    blit.destination.texture = swapchain;
    blit.destination.x = static_cast<Uint32>(left);
    blit.destination.y = static_cast<Uint32>(bottom);
    blit.destination.w = static_cast<Uint32>(shown_width);
    blit.destination.h = static_cast<Uint32>(shown_height);
    blit.load_op = SDL_GPU_LOADOP_CLEAR;
    blit.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    blit.filter = SDL_GPU_FILTER_LINEAR;
    OT_SDL(SDL_BlitGPUTexture)(command_buffer, &blit);
    OT_SDL(SDL_SubmitGPUCommandBuffer)(command_buffer);
}

int OT_HAL_CALL video_read_back(OtHalImage *image) {
    flush();
    if (g_read_back == 0 || g_device == 0) {
        return 0;
    }
    const int32_t width = g_mode.width;
    const int32_t height = g_mode.height;
    const size_t bytes = static_cast<size_t>(width) * height * 4u;
    SDL_GPUTransferBufferCreateInfo transfer_info;
    memset(&transfer_info, 0, sizeof(transfer_info));
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_info.size = static_cast<Uint32>(bytes);
    SDL_GPUTransferBuffer *transfer = OT_SDL(SDL_CreateGPUTransferBuffer)(g_device, &transfer_info);
    if (transfer == 0) {
        set_error(OT_SDL(SDL_GetError)());
        return 0;
    }
    SDL_GPUCommandBuffer *command_buffer = OT_SDL(SDL_AcquireGPUCommandBuffer)(g_device);
    SDL_GPUCopyPass *copy_pass = OT_SDL(SDL_BeginGPUCopyPass)(command_buffer);
    SDL_GPUTextureRegion source = {g_color_target, 0, 0, 0, 0, 0, static_cast<Uint32>(width),
        static_cast<Uint32>(height), 1};
    SDL_GPUTextureTransferInfo destination = {transfer, 0, static_cast<Uint32>(width), static_cast<Uint32>(height)};
    OT_SDL(SDL_DownloadFromGPUTexture)(copy_pass, &source, &destination);
    OT_SDL(SDL_EndGPUCopyPass)(copy_pass);
    OT_SDL(SDL_SubmitGPUCommandBuffer)(command_buffer);
    OT_SDL(SDL_WaitForGPUIdle)(g_device); // simplicity over latency: this path is screenshots/oracles, not per-frame
    const uint8_t *mapped = static_cast<const uint8_t *>(OT_SDL(SDL_MapGPUTransferBuffer)(g_device, transfer, false));
    // R8G8B8A8 (memory order R,G,B,A) -> OT_HAL_PIXEL_ARGB8888 (uint32 0xAARRGGBB).
    for (int32_t y = 0; y < height; ++y) {
        const uint8_t *row = mapped + static_cast<size_t>(y) * width * 4u;
        uint32_t *out = g_read_back + static_cast<size_t>(y) * width;
        for (int32_t x = 0; x < width; ++x) {
            const uint8_t r = row[x * 4 + 0];
            const uint8_t g = row[x * 4 + 1];
            const uint8_t b = row[x * 4 + 2];
            const uint8_t a = row[x * 4 + 3];
            out[x] = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
                     (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }
    OT_SDL(SDL_UnmapGPUTransferBuffer)(g_device, transfer);
    OT_SDL(SDL_ReleaseGPUTransferBuffer)(g_device, transfer);
    image->width = width;
    image->height = height;
    image->pitch = width * 4;
    image->format = OT_HAL_PIXEL_ARGB8888;
    image->pixels = g_read_back;
    return 1;
}

} // namespace

extern "C" const OtHalVideoBackend ot_hal_sdl_video_gpu = {
    "sdl-gpu",
    video_init,
    video_shutdown,
    video_set_mode,
    video_enumerate_modes,
    video_texture_create,
    video_texture_update,
    video_texture_destroy,
    video_palette_create,
    video_palette_update,
    video_palette_destroy,
    video_begin_frame,
    video_clear,
    video_set_viewport,
    video_set_scissor,
    video_draw,
    video_draw_sprite,
    video_present,
    video_read_back,
};

extern "C" const char *ot_hal_sdl_video_gpu_error(void) {
    return g_error;
}

extern "C" const OtHalVideoGpuStats *ot_hal_sdl_video_gpu_stats(void) {
    return &g_stats;
}
