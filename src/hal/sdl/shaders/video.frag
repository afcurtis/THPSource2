#version 450

// SDL_GPU video backend fragment shader (docs/HAL.md, "Video", 3.4d); see
// video.vert for the compilation and binding-convention notes.
//
// Mirrors the GL 3.3 backend's fragment shader (sdl_video_gl.cpp) exactly:
// mode 0 vertex colour only, 1 texture only, 2 texture x colour; flat picks
// the fan's shared-vertex colour over the (unused when flat) interpolated
// one; alpha test discards at or below the reference (the PSX colour-key
// convention, ref 0 by default).

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in vec4 v_flat_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;

layout(set = 3, binding = 0) uniform FragmentUniforms {
    int u_mode;        // 0 colour, 1 texture, 2 texture x colour
    int u_flat;
    int u_alpha_test;
    float u_alpha_ref;
} u;

layout(location = 0) out vec4 o_color;

void main() {
    vec4 color = u.u_flat != 0 ? v_flat_color : v_color;
    vec4 result = color;
    if (u.u_mode == 1) {
        result = texture(u_texture, v_uv);
    } else if (u.u_mode == 2) {
        result = texture(u_texture, v_uv) * color;
    }
    if (u.u_alpha_test != 0 && result.a <= u.u_alpha_ref) {
        discard;
    }
    o_color = result;
}
