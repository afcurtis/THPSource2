#version 450

// SDL_GPU video backend vertex shader (docs/HAL.md, "Video", 3.4d).
// Compiled to SPIR-V by `tony hal shaders` (glslang -V --target-env vulkan1.0)
// into the committed src/hal/sdl/sdl_video_gpu_shaders.h; never compiled at
// build or run time. Reproduces the same Direct3D 7 conventions as the GL 3.3
// backend's vertex shader (sdl_video_gl.cpp): pixel centres on integers,
// w = 1/rhw only with perspective texturing, first-vertex flat shading (the
// vertex order is pre-flattened for that on the CPU side: sdl_video_gpu.cpp).
//
// SDL's SPIR-V binding convention for a graphics pipeline: vertex uniform
// buffers at set 1, fragment sampled textures at set 2, fragment uniform
// buffers at set 3 (SDL_CreateGPUShader, SDL_GPU_SHADERFORMAT_SPIRV).
//
// SDL flips y itself for a Vulkan swapchain/render target (probed: NDC y-up,
// top-left scissor and read-back, no manual row flip needed), so the pixel ->
// NDC formula is identical to the GL backend's.
//
// The vertex colour arrives as OtHalVertex::color, a 0xAARRGGBB uint32 read
// here as four unnormalized bytes (UBYTE4_NORM): little-endian memory order
// puts B first, so `.zyxw` reorders (B,G,R,A) -> (R,G,B,A).
//
// SDL_GPU's depth range is 0..1 (Direct3D-style), unlike GL's -1..1, so no
// z*2-1 remap is needed: gl_Position.z is simply z*w.

layout(location = 0) in vec4 a_position; // x, y (pixels), z (0..1), rhw
layout(location = 1) in vec4 a_color_bytes; // UBYTE4_NORM, memory order B,G,R,A
layout(location = 2) in vec2 a_uv;

layout(set = 1, binding = 0) uniform VertexUniforms {
    vec4 u_target; // width, height, perspective (0/1), unused
} u;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out vec4 v_flat_color;

void main() {
    vec4 color = a_color_bytes.zyxw;
    vec2 pixel = a_position.xy + vec2(0.5);
    vec2 ndc = vec2(pixel.x / u.u_target.x * 2.0 - 1.0, 1.0 - pixel.y / u.u_target.y * 2.0);
    float w = (u.u_target.z != 0.0 && a_position.w > 0.0) ? 1.0 / a_position.w : 1.0;
    gl_Position = vec4(ndc * w, a_position.z * w, w);
    v_color = color;
    v_flat_color = color;
    v_uv = a_uv;
}
