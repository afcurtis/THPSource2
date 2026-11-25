#include "sdl_internal.h"
#include "sdl_video.h"

#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>

#include "../texture_key_hash.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// Video service on SDL3 + OpenGL 3.3 core (docs/HAL.md, "Video").
//
// Frames render into an offscreen framebuffer of the mode's size, so
// read_back is independent of the window; present blits it to the window.
// Vertices arrive pre-transformed as Direct3D 7 submits them (OtHalVertex),
// and the shaders reproduce its conventions: pixel centres on integer
// coordinates, depth 0..1, w = 1/rhw so every attribute interpolates
// perspective-correctly as a Direct3D 7 device does today (the retail
// D3DRS_TEXTUREPERSPECTIVE switch is ignored by current implementations),
// flat shading from each triangle's first vertex.
//
// The target need not have the mode's size (ot_hal_sdl_video_gl_options): the
// vertices stay in mode pixels and the viewport stretches them over a target
// of any size, multisampled or not, so the game's 640 x 480 picture can be
// drawn at the window's resolution. With no options set every call is what
// it was before they existed.

namespace {

// OpenGL entry points, resolved through SDL_GL_GetProcAddress.
#define OT_GL_FUNCTIONS(X)                                                                                         \
    X(glGetString) X(glGetError) X(glViewport) X(glScissor) X(glEnable) X(glDisable) X(glClearColor)              \
    X(glClearDepth) X(glClear) X(glBlendFuncSeparate) X(glBlendEquation) X(glDepthFunc) X(glDepthMask)            \
    X(glGenTextures) X(glDeleteTextures) X(glBindTexture) X(glTexImage2D) X(glTexSubImage2D) X(glTexParameteri)  \
    X(glPixelStorei) X(glActiveTexture) X(glReadPixels) X(glCreateShader) X(glShaderSource) X(glCompileShader)    \
    X(glGetShaderiv) X(glGetShaderInfoLog) X(glCreateProgram) X(glAttachShader) X(glLinkProgram)                  \
    X(glGetProgramiv) X(glGetProgramInfoLog) X(glUseProgram) X(glGetUniformLocation) X(glUniform1i)               \
    X(glUniform1f) X(glUniform4f) X(glGenVertexArrays) X(glBindVertexArray) X(glGenBuffers) X(glBindBuffer)       \
    X(glBufferData) X(glVertexAttribPointer) X(glEnableVertexAttribArray) X(glGenFramebuffers)                    \
    X(glBindFramebuffer) X(glFramebufferRenderbuffer) X(glGenRenderbuffers) X(glBindRenderbuffer)                 \
    X(glRenderbufferStorage) X(glCheckFramebufferStatus) X(glBlitFramebuffer) X(glDeleteFramebuffers)             \
    X(glDeleteRenderbuffers) X(glDeleteBuffers) X(glDeleteVertexArrays) X(glDeleteProgram) X(glDeleteShader)      \
    X(glProvokingVertex) X(glDrawArrays) X(glGetIntegerv) X(glRenderbufferStorageMultisample)                      \
    X(glFramebufferTexture2D) X(glUniform2f)                                                                       \
    X(glGenerateMipmap) X(glGetFloatv) X(glGetStringi) X(glGenSamplers) X(glDeleteSamplers) X(glBindSampler)      \
    X(glSamplerParameteri) X(glSamplerParameterf) X(glGetTexImage) X(glTexImage2DMultisample) X(glDrawBuffer) X(glReadBuffer) X(glUniform2i)

struct GlApi {
#define OT_GL_POINTER(name) decltype(&::name) name;
    OT_GL_FUNCTIONS(OT_GL_POINTER)
#undef OT_GL_POINTER
};
GlApi gl;

const char *kVertexShader =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_position;\n" // x, y, z, rhw
    "layout(location = 1) in vec4 a_color;\n"
    "layout(location = 2) in vec2 a_uv;\n"
    "uniform vec4 u_target;\n" // width, height, perspective (0/1)
    "out vec4 v_color;\n"
    "flat out vec4 v_flat_color;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 pixel = a_position.xy + vec2(0.5);\n"
    "    vec2 ndc = vec2(pixel.x / u_target.x * 2.0 - 1.0, 1.0 - pixel.y / u_target.y * 2.0);\n"
    "    float w = (u_target.z != 0.0 && a_position.w > 0.0) ? 1.0 / a_position.w : 1.0;\n"
    "    gl_Position = vec4(ndc * w, (a_position.z * 2.0 - 1.0) * w, w);\n"
    "    v_color = a_color;\n"
    "    v_flat_color = a_color;\n"
    "    v_uv = a_uv;\n"
    "}\n";

const char *kFragmentShader =
    "#version 330 core\n"
    "in vec4 v_color;\n"
    "flat in vec4 v_flat_color;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_texture;\n"
    "uniform int u_mode;\n" // 0 colour, 1 texture, 2 texture x colour
    "uniform int u_flat;\n"
    "uniform int u_alpha_test;\n"
    "uniform float u_alpha_ref;\n"
    "uniform int u_coverage;\n" // alpha-to-coverage: the alpha is sharpened around one half and left to the multisample coverage
    "uniform vec2 u_sharp;\n"   // sharp 2D: the texture's size in texels (0 = off). Each texel keeps its whole area, only the last output pixel at its edge blends
    "out vec4 o_color;\n"
    "void main() {\n"
    "    vec4 color = u_flat != 0 ? v_flat_color : v_color;\n"
    "    vec4 result = color;\n"
    "    vec2 uv = v_uv;\n"
    "    if (u_sharp.x != 0.0) {\n"
    "        vec2 t = v_uv * u_sharp;\n"
    "        vec2 w = clamp(fwidth(t), vec2(1e-4), vec2(1.0));\n" // texels per output pixel; 1 or more = plain bilinear
    "        w = mix(w, vec2(1.0), 0.4);\n"                       // played at 4K, the full-sharp edge was a touch hard: 60% of the way from bilinear (w = 1) to sharp
    "        vec2 off = fract(t) - vec2(0.5);\n"
    "        vec2 range = vec2(0.5) - vec2(0.5) * w;\n"
    "        uv = (floor(t) + vec2(0.5) + (off - clamp(off, -range, range)) / w) / u_sharp;\n"
    "    }\n"
    "    if (u_mode == 1) result = texture(u_texture, uv);\n"
    "    else if (u_mode == 2) result = texture(u_texture, uv) * color;\n"
    "    else if (u_mode == 3) result = texture(u_texture, uv) * color * vec4(2.0, 2.0, 2.0, 1.0);\n"
    "    if (u_alpha_test != 0 && result.a <= u_alpha_ref) discard;\n"
    "    if (u_coverage != 0) result.a = clamp((result.a - 0.5) / max(fwidth(result.a), 1e-4) + 0.5, 0.0, 1.0);\n"
    "    o_color = result;\n"
    "}\n";

// The curved screen (g_options.crt_curve), shared by every present shader (with_curve puts it after the #version line): a barrel warp in which each axis bulges by the
// square of the other (1/32 across, 1/24 down at the corners), the screen's edge a rounded rectangle (corner radius 3% of the height) faded over one output pixel. Returns
// the warped uv and, in edge, the coverage of the output pixel (1 inside, 0 outside: black). u_curve 0 returns the uv as it came.
const char *kCurveGlsl =
    "uniform float u_curve;\n" // 1: the curved screen
    "uniform vec2 u_shown;\n"  // the shown rectangle in output pixels
    "vec2 curve_warp(vec2 v, out float edge) {\n"
    "    edge = 1.0;\n"
    "    if (u_curve < 0.5) return v;\n"
    "    vec2 c = v * 2.0 - vec2(1.0);\n"
    "    c *= vec2(1.0 + c.y * c.y / 32.0, 1.0 + c.x * c.x / 24.0);\n"
    "    vec2 uv = c * 0.5 + vec2(0.5);\n"
    "    float radius = 0.03 * u_shown.y;\n"
    "    vec2 q = abs(uv - vec2(0.5)) * u_shown - (u_shown * 0.5 - vec2(radius));\n"
    "    float outside = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;\n"
    "    edge = clamp(0.5 - outside, 0.0, 1.0);\n"
    "    return uv;\n"
    "}\n";

// The present of OT_HAL_SDL_PRESENT_SHARP: one triangle over the shown rectangle; each source texel keeps its whole
// area and only the last output pixel at its edges is blended with the neighbour ("sharp bilinear").
const char *kPresentVertexShader =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    v_uv = corner;\n"
    "    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

const char *kPresentFragmentShader =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_image;\n"
    "uniform vec2 u_source;\n" // the image's size in texels
    "uniform vec2 u_scale;\n"  // output pixels per texel
    "out vec4 o_color;\n"
    "void main() {\n"
    "    float edge;\n"
    "    vec2 uv = curve_warp(v_uv, edge);\n"
    "    if (edge <= 0.0) {\n"
    "        o_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 texel = uv * u_source;\n"
    "    vec2 scale = max(floor(u_scale), vec2(1.0));\n"
    "    vec2 range = vec2(0.5) - vec2(0.5) / scale;\n"
    "    vec2 off = fract(texel) - vec2(0.5);\n"
    "    vec2 f = (off - clamp(off, -range, range)) * scale + vec2(0.5);\n"
    "    o_color = vec4(texture(u_image, (floor(texel) + f) / u_source).rgb * edge, 1.0);\n"
    "}\n";

// The present of a target larger than the rectangle it is shown in: a box filter over each output pixel's footprint in the source, as taps.x x taps.y bilinear taps
// spread evenly across it (taps = the footprint's size in texels, rounded up). Uses the present's vertex shader; the source is sampled with linear filtering.
const char *kDownsampleFragmentShader =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_image;\n"
    "uniform vec2 u_source;\n" // the image's size in texels
    "uniform vec2 u_ratio;\n"  // source texels per output pixel
    "uniform vec2 u_taps;\n"   // taps across and down (whole numbers)
    "out vec4 o_color;\n"
    "void main() {\n"
    "    float edge;\n"
    "    vec2 uv = curve_warp(v_uv, edge);\n"
    "    if (edge <= 0.0) {\n"
    "        o_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 centre = uv * u_source;\n"
    "    vec3 sum = vec3(0.0);\n"
    "    for (int j = 0; j < int(u_taps.y); ++j) {\n"
    "        for (int i = 0; i < int(u_taps.x); ++i) {\n"
    "            vec2 along = (vec2(float(i), float(j)) + vec2(0.5)) / u_taps - vec2(0.5);\n"
    "            sum += texture(u_image, (centre + along * u_ratio) / u_source).rgb;\n"
    "        }\n"
    "    }\n"
    "    o_color = vec4(sum / (u_taps.x * u_taps.y) * edge, 1.0);\n"
    "}\n";

// The CRT present (OT_HAL_SDL_PRESENT_CRT), written for this project (no shader pack). Per output pixel, 4 sub-samples across its height, so a window height that is not a
// multiple of the source's gives no moire: each takes the source line it falls in (never blending lines, so the lines stay distinct), 3 taps across the line (a soft
// blur of about one source pixel), and a beam profile down the line: a Gaussian that is wider on bright pixels, so bright pixels darken less between lines. u_mask (the
// output has at least 4 pixels a texel across) adds a faint red, green, blue column mask. The curved screen is not part of it: curve_warp, when g_options.crt_curve is on.
const char *kCrtFragmentShader =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_image;\n"
    "uniform vec2 u_source;\n" // the image's size in texels
    "uniform vec2 u_scale;\n"  // output pixels per texel
    "uniform float u_mask;\n"  // 1: the aperture mask
    "out vec4 o_color;\n"
    "void main() {\n"
    "    float edge;\n"
    "    vec2 uv = curve_warp(v_uv, edge);\n"
    "    if (edge <= 0.0) {\n"
    "        o_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 texel = uv * u_source;\n"
    "    vec3 sum = vec3(0.0);\n"
    "    for (int k = 0; k < 4; ++k) {\n"
    "        float y = texel.y + ((float(k) + 0.5) / 4.0 - 0.5) / u_scale.y;\n"
    "        float row = floor(y);\n"
    "        float dy = y - row - 0.5;\n"
    "        float v = (row + 0.5) / u_source.y;\n"
    "        vec3 c = texture(u_image, vec2((texel.x - 0.75) / u_source.x, v)).rgb * 0.25 + texture(u_image, vec2(texel.x / u_source.x, v)).rgb * 0.5 +\n"
    "                 texture(u_image, vec2((texel.x + 0.75) / u_source.x, v)).rgb * 0.25;\n"
    "        float sigma = mix(0.22, 0.40, dot(c, vec3(0.299, 0.587, 0.114)));\n"
    "        sum += c * exp(-dy * dy / (2.0 * sigma * sigma));\n"
    "    }\n"
    "    vec3 colour = sum * (1.35 / 4.0);\n"
    "    if (u_mask > 0.5) {\n"
    "        int column = int(mod(floor(gl_FragCoord.x), 3.0));\n"
    "        vec3 mask = vec3(0.72);\n"
    "        mask[column] = 1.0;\n"
    "        colour *= mask * 1.18;\n"
    "    }\n"
    "    o_color = vec4(min(colour, vec3(1.0)) * edge, 1.0);\n"
    "}\n";

// The present of the curved screen when no other present shader is wanted (a linear or nearest scaling): the picture through curve_warp, read with the texture's own
// linear filter, or (u_nearest) by the nearest texel.
const char *kPlainFragmentShader =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_image;\n"
    "uniform vec2 u_source;\n"   // the image's size in texels
    "uniform float u_nearest;\n" // 1: the nearest texel
    "out vec4 o_color;\n"
    "void main() {\n"
    "    float edge;\n"
    "    vec2 uv = curve_warp(v_uv, edge);\n"
    "    if (edge <= 0.0) {\n"
    "        o_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 colour = u_nearest > 0.5 ? texelFetch(u_image, ivec2(clamp(floor(uv * u_source), vec2(0.0), u_source - vec2(1.0))), 0).rgb : texture(u_image, uv).rgb;\n"
    "    o_color = vec4(colour * edge, 1.0);\n"
    "}\n";

// The PlayStation dither (g_options.psx_dither), over the finished single-sample picture at its own size: the console added this 4 x 4 matrix to each 8-bit channel
// before dropping the low three bits (psx-spx, "GPU Rendering Attributes", dithering; Nocash's PSX specifications). Applied here to the whole picture, not only to the
// shaded polygons as the console did. Output: 5 bits a channel, expanded back to 8 (c * 255 / 31).
const char *kDitherFragmentShader =
    "#version 330 core\n"
    "uniform sampler2D u_image;\n"
    "uniform ivec2 u_size;\n"
    "out vec4 o_color;\n"
    "const int kMatrix[16] = int[16](-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2);\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    int d = kMatrix[((u_size.y - 1 - p.y) & 3) * 4 + (p.x & 3)];\n"
    "    vec3 c = floor(texelFetch(u_image, p, 0).rgb * 255.0 + 0.5) + float(d);\n"
    "    c = floor(clamp(c, 0.0, 255.0) / 8.0);\n"
    "    o_color = vec4(c / 31.0, 1.0);\n"
    "}\n";

// Ambient occlusion (g_options.ambient_occlusion). Depth is the game's reciprocal z = near / depth (larger is nearer, cleared to 0), so a view-space position is
// proportional to (ndc x tan_x, ndc y tan_y, 1) / z in units of the near plane. The near plane's size and the field of view are not known to the backend (they are the
// game's, in guest memory): the radius, the field of view and the strengths below were tuned by eye against the Warehouse, the School and New York.
// Pass 1 (half resolution): from a copy of the depth, per pixel the view position, a normal from the smaller depth step on each axis, and taps spread on a
// spiral (rotated per pixel by interleaved gradient noise) within a radius that shrinks with distance; a tap occludes by how far above the surface plane it
// lies, fading out with its distance from the pixel. Output: occlusion (r) and the depth (g, for the blur and the upsample).
// Pass 2: a separable 7-tap blur, weighted by depth similarity. Pass 3 (full resolution): a bilateral upsample of the occlusion multiplied onto the colour.
const char *kAoFragmentShader =
    "#version 330 core\n"
    "uniform sampler2D u_depth;\n"
    "uniform vec4 u_size;\n"   // full width, full height
    "uniform vec4 u_params;\n" // radius (near units), strength, tan of half the vertical field of view, bias
    "uniform int u_taps;\n"
    "out vec2 o_color;\n"
    "float depth_at(ivec2 p) { return texelFetch(u_depth, clamp(p, ivec2(0), ivec2(u_size.xy) - ivec2(1)), 0).r; }\n"
    "vec3 view_pos(vec2 pixel, float z) {\n"
    "    vec2 ndc = pixel / u_size.xy * 2.0 - 1.0;\n"
    "    float tan_y = u_params.z;\n"
    "    return vec3(ndc.x * tan_y * u_size.x / u_size.y, ndc.y * tan_y, 1.0) / max(z, 1e-5);\n"
    "}\n"
    "void main() {\n"
    "    ivec2 base = ivec2(gl_FragCoord.xy) * 2;\n"
    "    float z = depth_at(base);\n"
    "    if (z <= 1e-5) { o_color = vec2(1.0, 0.0); return; }\n"
    "    vec2 centre = vec2(base) + 0.5;\n"
    "    vec3 p = view_pos(centre, z);\n"
    "    float zr = depth_at(base + ivec2(2, 0)); float zl = depth_at(base - ivec2(2, 0));\n"
    "    float zu = depth_at(base + ivec2(0, 2)); float zd = depth_at(base - ivec2(0, 2));\n"
    "    vec3 dx = abs(1.0 / max(zr, 1e-5) - 1.0 / z) < abs(1.0 / z - 1.0 / max(zl, 1e-5)) ? view_pos(centre + vec2(2.0, 0.0), zr) - p : p - view_pos(centre - vec2(2.0, 0.0), zl);\n"
    "    vec3 dy = abs(1.0 / max(zu, 1e-5) - 1.0 / z) < abs(1.0 / z - 1.0 / max(zd, 1e-5)) ? view_pos(centre + vec2(0.0, 2.0), zu) - p : p - view_pos(centre - vec2(0.0, 2.0), zd);\n"
    "    vec3 n = normalize(cross(dx, dy));\n"
    "    if (dot(n, p) > 0.0) n = -n;\n"
    "    float radius = u_params.x;\n"
    "    float focal = u_size.y / (2.0 * u_params.z);\n"
    "    float reach = clamp(radius * z * focal, 3.0, 0.08 * u_size.y);\n"
    "    float noise = fract(52.9829189 * fract(dot(vec2(base), vec2(0.06711056, 0.00583715))));\n"
    "    float sum = 0.0;\n"
    "    for (int i = 0; i < u_taps; ++i) {\n"
    "        float a = float(i) * 2.399963 + noise * 6.2831853;\n"
    "        float r = sqrt((float(i) + 0.5) / float(u_taps));\n"
    "        vec2 sp = centre + vec2(cos(a), sin(a)) * r * reach;\n"
    "        float zs = depth_at(ivec2(sp));\n"
    "        if (zs <= 1e-5) continue;\n"
    "        vec3 v = view_pos(vec2(ivec2(sp)) + 0.5, zs) - p;\n"
    "        float dist = max(length(v), 1e-5);\n"
    "        float occlusion = max(dot(n, v) / dist - u_params.w, 0.0);\n"
    "        sum += occlusion * (1.0 - smoothstep(0.6 * radius, 1.6 * radius, dist));\n"
    "    }\n"
    "    o_color = vec2(clamp(1.0 - u_params.y * sum / float(u_taps), 0.0, 1.0), z);\n"
    "}\n";

const char *kAoBlurFragmentShader =
    "#version 330 core\n"
    "uniform sampler2D u_ao;\n"
    "uniform ivec2 u_dir;\n"
    "out vec2 o_color;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    ivec2 last = textureSize(u_ao, 0) - ivec2(1);\n"
    "    vec2 c = texelFetch(u_ao, p, 0).rg;\n"
    "    if (c.g <= 0.0) { o_color = c; return; }\n"
    "    float sum = c.r * 4.0;\n"
    "    float total = 4.0;\n"
    "    for (int i = -3; i <= 3; ++i) {\n"
    "        if (i == 0) continue;\n"
    "        vec2 s = texelFetch(u_ao, clamp(p + i * u_dir, ivec2(0), last), 0).rg;\n"
    "        float w = s.g > 0.0 ? float(4 - abs(i)) * exp(-abs(s.g / c.g - 1.0) * 30.0) : 0.0;\n"
    "        sum += s.r * w;\n"
    "        total += w;\n"
    "    }\n"
    "    o_color = vec2(sum / total, c.g);\n"
    "}\n";

const char *kAoCompositeFragmentShader =
    "#version 330 core\n"
    "uniform sampler2D u_depth;\n"
    "uniform sampler2D u_ao;\n"
    "uniform vec2 u_fade;\n" // the depth (reciprocal) below which the occlusion fades out, and the depth at which it is whole
    "out vec4 o_color;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    float z = texelFetch(u_depth, p, 0).r;\n"
    "    if (z <= 1e-5) { o_color = vec4(1.0); return; }\n"
    "    ivec2 last = textureSize(u_ao, 0) - ivec2(1);\n"
    "    vec2 h = (vec2(p) + 0.5) * 0.5 - 0.5;\n"
    "    ivec2 b = ivec2(floor(h));\n"
    "    vec2 f = h - vec2(b);\n"
    "    float sum = 0.0;\n"
    "    float total = 0.0;\n"
    "    for (int k = 0; k < 4; ++k) {\n"
    "        ivec2 at = b + ivec2(k & 1, k >> 1);\n"
    "        vec2 s = texelFetch(u_ao, clamp(at, ivec2(0), last), 0).rg;\n"
    "        float bilinear = ((k & 1) != 0 ? f.x : 1.0 - f.x) * ((k >> 1) != 0 ? f.y : 1.0 - f.y);\n"
    "        float w = s.g > 0.0 ? bilinear * exp(-abs(s.g / z - 1.0) * 30.0) : 0.0;\n"
    "        sum += s.r * w;\n"
    "        total += w;\n"
    "    }\n"
    "    float ao = total > 1e-4 ? sum / total : 1.0;\n"
    "    ao = mix(1.0, ao, smoothstep(u_fade.x, u_fade.y, z));\n"
    "    o_color = vec4(ao, ao, ao, 1.0);\n"
    "}\n";

const uint32_t kMaxTextures = 8192;
const uint32_t kMaxPalettes = 1024;

struct Texture {
    GLuint name;
    int32_t width;
    int32_t height;
    uint32_t format;
    OtHalHandle palette;
    uint32_t palette_version; // indexed textures: the version last converted
    uint8_t *indices;         // indexed textures: texels kept for palette changes
    uint8_t mips_dirty;       // level 0 changed since the mip chain was built (or it never was)
    uint8_t cutout;           // 0 not looked at since level 0 changed, 1 a cut-out (alpha only 0 or 255, some of each), 2 not
    // texture pack (g_options.texture_pack): width and height stay what the game thinks (its sprite rectangles, the 2D layer); the GL texture has the real size
    int32_t real_width;
    int32_t real_height;
    uint8_t replaced;         // level 0 is a picture from the pack; `cutout` was settled from the original texels and stays
    uint8_t hashed;           // the last whole upload was hashed: `key` is its key (replaced or not)
    uint8_t logged;           // texture_pack_log: bit 0 a 2D draw of this upload was logged, bit 1 a world draw
    uint64_t key;
};

struct Palette {
    uint32_t used;
    uint32_t version;
    uint32_t count;
    uint32_t colors[256];
};

Texture g_textures[kMaxTextures];
Palette g_palettes[kMaxPalettes];
SDL_GLContext g_context = 0;
GLuint g_program = 0;
GLuint g_vertex_array = 0;
GLuint g_vertex_buffer = 0;
GLuint g_framebuffer = 0;
GLuint g_color_buffer = 0;
GLuint g_depth_buffer = 0;
GLint g_uniform_target = -1;
GLint g_uniform_mode = -1;
GLint g_uniform_flat = -1;
GLint g_uniform_alpha_test = -1;
GLint g_uniform_alpha_ref = -1;
OtHalVideoMode g_mode;
OtHalSdlVideoOptions g_options = {};
int32_t g_target_width = 0;  // the render target in pixels (the mode's size unless the options say otherwise)
int32_t g_target_height = 0;
int32_t g_samples = 0;       // of the target; 0 = not multisampled
GLuint g_resolve_framebuffer = 0; // a single-sample copy of the target, when it is multisampled or the present samples it
GLuint g_resolve_texture = 0;
GLuint g_present_program = 0;
GLuint g_present_array = 0;
GLint g_present_source = -1;
GLint g_present_scale = -1;
GLuint g_crt_program = 0;
GLuint g_plain_program = 0;       // crt_curve with a linear or nearest scaling
GLint g_plain_source = -1;
GLint g_plain_nearest = -1;
GLint g_crt_source = -1;
GLint g_crt_scale = -1;
GLint g_crt_mask = -1;
GLuint g_dither_program = 0;      // psx_dither: the pass, and the single-sample copy it writes (the present reads that instead of the resolve)
GLint g_dither_size = -1;
GLuint g_dither_texture = 0;
GLuint g_dither_fbo = 0;
GLuint g_downsample_program = 0;
GLint g_downsample_source = -1;
GLint g_downsample_ratio = -1;
GLint g_downsample_taps = -1;
GLint g_uniform_coverage = -1;
GLint g_uniform_sharp = -1;
float g_sharp_width = 0.0f;       // u_sharp as last set (the texture's size, 0 = off); only touched with g_options.sharp_2d
float g_sharp_height = 0.0f;
// Ambient occlusion state (only touched with g_options.ambient_occlusion)
GLuint g_depth_texture = 0;       // the target's depth when it is a texture (multisampled with the target); else 0 and g_depth_buffer is a renderbuffer
GLuint g_ao_depth_texture = 0;    // a single-sample copy of it, made when a pass runs
GLuint g_ao_depth_fbo = 0;
GLuint g_ao_texture[2] = {0, 0};  // half resolution, RG16F: occlusion and depth
GLuint g_ao_fbo[2] = {0, 0};
GLuint g_ao_program = 0;
GLuint g_ao_blur_program = 0;
GLuint g_ao_composite_program = 0;
GLint g_ao_u_size = -1;
GLint g_ao_u_params = -1;
GLint g_ao_u_taps = -1;
GLint g_ao_u_dir = -1;
GLint g_ao_u_fade = -1;
bool g_target_ao = false;         // the target was made for ambient occlusion (the option was on)
bool g_ao_ready = false;          // the programs and buffers exist for the current target
bool g_ao_pending = false;        // a world draw since the last pass
bool g_ao_programs_tried = false;
bool g_ao_after_pass = false;     // a pass ran in this frame (for ao_late)
int32_t g_ao_rect[4] = {0, 0, 0, 0}; // the union of the scissors of those draws, mode pixels: left, top, right, bottom
int32_t g_scissor[4] = {0, 0, 0, 0}; // the scissor as last set, mode pixels: x, y, w, h
// Tuned by eye (see the header comment of the shaders): the radius in near units, the strength, tan of half the vertical field of view, the bias, and the depths
// (reciprocal) between which the occlusion fades in (the far scenery, and the sky drawn at a fixed depth, get none).
const float kAoRadius = 4.0f;
const float kAoStrength = 3.0f;
const float kAoTan = 0.7f;
const float kAoBias = 0.1f;
const float kAoFadeFrom = 0.003f;
const float kAoFadeTo = 0.012f;
int32_t g_supersample_used = 100; // the factor (x100) wanted_target() settled on
// the most bytes a supersampled target may cost (width x height x (8 x samples + 6 with ambient occlusion)). A fixed number, not the card's memory: 1 GB crashed
// an AMD driver stack within seconds of the first frames (2026-11 report), so it is 400 MB
const int64_t kSupersampleBudget = 400000000ll;
int32_t g_coverage_state = -1;    // alpha-to-coverage on (1) or off (0) as last set; -1 not set yet
OtHalVertex *g_line_quads = 0;    // lines widened to quads for a target larger than the mode
uint32_t g_line_capacity = 0;
uint32_t *g_read_back = 0;
// The picture as the present shows it (ot_hal_sdl_video_gl_capture_shown): the window's shown rectangle, read back after a present that does not swap
uint32_t *g_shown_pixels = 0;
size_t g_shown_capacity = 0;     // pixels
int32_t g_shown_width = 0;
int32_t g_shown_height = 0;
bool g_capturing = false;        // video_present is running for a capture: it reads the shown rectangle in place of the swap
OtHalVideoStats g_stats;
// Texture detail (g_options.anisotropy != 0): world draws sample a mip chain through sampler objects. Made on the first draw that needs them, and again when the options change.
GLuint g_samplers[6] = {0, 0, 0, 0, 0, 0}; // [0,1] nearest, [2,3] linear, [4,5] linear with mips and anisotropy; each clamp then repeat
bool g_samplers_made = false;
int32_t g_sampler_anisotropy = -1;
float g_sampler_bias = 0.0f;
bool g_sampler_bound = false;     // a sampler object is bound to unit 0 (the draws' own parameters would not apply)
bool g_anisotropy_checked = false;
float g_anisotropy_limit = 1.0f;  // 1: the driver has no anisotropic filtering
char g_error[256];
// Texture pack: the folders (copied from the options, several joined by '|': the first one holding a key supplies its picture), and the keys of their files, sorted,
// read at the first upload that could use them
char g_pack_dir[1024];
struct PackKey {
    uint64_t key;
    uint32_t dir; // which folder (0 = the first)
};
PackKey *g_pack_keys = 0;
uint32_t g_pack_count = 0;
bool g_pack_scanned = false;
uint64_t g_draw_ticks = 0; // time_draws: ticks inside video_draw
uint64_t g_swap_ticks = 0; // time_draws: ticks inside SDL_GL_SwapWindow
uint64_t g_mip_ticks = 0;  // performance-counter ticks spent building mip chains (build_mips), all textures
uint64_t g_pack_ticks = 0; // performance-counter ticks spent in pack_replace (the folder scan, hashing, decoding, uploading), all uploads
const int32_t kPackMaxSide = 1024; // a larger picture is refused (a 256 x 256 texture at 4x)

void set_error(const char *text) {
    OT_SDL(SDL_strlcpy)(g_error, text, sizeof(g_error));
}

bool load_gl() {
    bool complete = true;
#define OT_GL_RESOLVE(name)                                                                              \
    gl.name = reinterpret_cast<decltype(gl.name)>(OT_SDL(SDL_GL_GetProcAddress)(#name));                  \
    if (gl.name == 0) {                                                                                  \
        set_error("OpenGL lacks " #name);                                                                \
        complete = false;                                                                                \
    }
    OT_GL_FUNCTIONS(OT_GL_RESOLVE)
#undef OT_GL_RESOLVE
    return complete;
}

// One line in run.txt naming the GPU and the driver the picture goes through (a crash report says only which DLL faulted), and the video memory where the driver says:
// GL_NVX_gpu_memory_info (total dedicated and what is free now) or GL_ATI_meminfo (what is free now). Neither is standard; with neither the line has no memory
void log_gpu() {
    const char *vendor = reinterpret_cast<const char *>(gl.glGetString(GL_VENDOR));
    const char *renderer = reinterpret_cast<const char *>(gl.glGetString(GL_RENDERER));
    const char *version = reinterpret_cast<const char *>(gl.glGetString(GL_VERSION));
    // 0 = no memory query, 1 = total and free (NVIDIA), 2 = free only (AMD)
    int memory_kind = 0;
    GLint total_mb = 0;
    GLint free_mb = 0;
    GLint count = 0;
    gl.glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint index = 0; index < count && memory_kind == 0; ++index) {
        const char *name = reinterpret_cast<const char *>(gl.glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
        if (name == 0) {
            continue;
        }
        GLint kb[4] = {0, 0, 0, 0};
        if (strcmp(name, "GL_NVX_gpu_memory_info") == 0) {
            GLint free_kb = 0;
            gl.glGetIntegerv(0x9047 /* GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX */, kb);
            gl.glGetIntegerv(0x9049 /* GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX */, &free_kb);
            total_mb = kb[0] / 1024;
            free_mb = free_kb / 1024;
            memory_kind = 1;
        } else if (strcmp(name, "GL_ATI_meminfo") == 0) {
            gl.glGetIntegerv(0x87FC /* TEXTURE_FREE_MEMORY_ATI */, kb);
            free_mb = kb[0] / 1024;
            memory_kind = 2;
        }
    }
    gl.glGetError(); // a query the driver refused leaves an error that is not the draws'
    // (no snprintf: the 32-bit VC6 SDK has none)
    printf("video: GPU %s | %s | OpenGL %s", vendor != 0 ? vendor : "?", renderer != 0 ? renderer : "?", version != 0 ? version : "?");
    if (memory_kind == 1) {
        printf(", %d MB video memory, %d MB free", total_mb, free_mb);
    } else if (memory_kind == 2) {
        printf(", %d MB of video memory free", free_mb);
    }
    printf("\n");
}

GLuint compile(GLenum type, const char *source) {
    GLuint shader = gl.glCreateShader(type);
    gl.glShaderSource(shader, 1, &source, 0);
    gl.glCompileShader(shader);
    GLint ok = 0;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        gl.glGetShaderInfoLog(shader, sizeof(g_error), 0, g_error);
        gl.glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLenum gl_factor(uint8_t factor) {
    switch (factor) {
    case OT_HAL_FACTOR_ZERO: return GL_ZERO;
    case OT_HAL_FACTOR_SRC_COLOR: return GL_SRC_COLOR;
    case OT_HAL_FACTOR_INV_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
    case OT_HAL_FACTOR_SRC_ALPHA: return GL_SRC_ALPHA;
    case OT_HAL_FACTOR_INV_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case OT_HAL_FACTOR_DST_COLOR: return GL_DST_COLOR;
    case OT_HAL_FACTOR_INV_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
    case OT_HAL_FACTOR_DST_ALPHA: return GL_DST_ALPHA;
    case OT_HAL_FACTOR_INV_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
    default: return GL_ONE;
    }
}

GLenum gl_topology(uint32_t topology) {
    switch (topology) {
    case OT_HAL_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
    case OT_HAL_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
    case OT_HAL_LINES: return GL_LINES;
    case OT_HAL_LINE_STRIP: return GL_LINE_STRIP;
    default: return GL_TRIANGLES;
    }
}

void release_target() {
    if (g_framebuffer != 0) {
        gl.glDeleteFramebuffers(1, &g_framebuffer);
        gl.glDeleteRenderbuffers(1, &g_color_buffer);
        if (g_depth_texture != 0) {
            gl.glDeleteTextures(1, &g_depth_texture);
            g_depth_texture = 0;
        } else {
            gl.glDeleteRenderbuffers(1, &g_depth_buffer);
        }
        g_framebuffer = 0;
    }
    if (g_ao_depth_fbo != 0) {
        gl.glDeleteFramebuffers(1, &g_ao_depth_fbo);
        gl.glDeleteTextures(1, &g_ao_depth_texture);
        gl.glDeleteFramebuffers(2, g_ao_fbo);
        gl.glDeleteTextures(2, g_ao_texture);
        g_ao_depth_fbo = 0;
        g_ao_depth_texture = 0;
    }
    g_ao_ready = false;
    g_ao_pending = false;
    if (g_dither_fbo != 0) {
        gl.glDeleteFramebuffers(1, &g_dither_fbo);
        gl.glDeleteTextures(1, &g_dither_texture);
        g_dither_fbo = 0;
        g_dither_texture = 0;
    }
    if (g_resolve_framebuffer != 0) {
        gl.glDeleteFramebuffers(1, &g_resolve_framebuffer);
        gl.glDeleteTextures(1, &g_resolve_texture);
        g_resolve_framebuffer = 0;
        g_resolve_texture = 0;
    }
}

// Whether the target has a single-sample copy: it is multisampled, the present samples it, or the picture is larger than the mode (the downsample reads it)
bool wants_resolve(int32_t width, int32_t height) {
    return g_options.samples >= 2 || g_options.present_filter == OT_HAL_SDL_PRESENT_SHARP || g_options.present_filter == OT_HAL_SDL_PRESENT_CRT || g_options.crt_curve != 0 || g_options.psx_dither != 0 || width > g_mode.width || height > g_mode.height;
}

// A present fragment shader with the curved screen's code after its #version line (a shader too long for the buffer is cut, and fails to compile)
struct CurvedShader {
    char text[8192];
};

void append_shader_text(CurvedShader *shader, size_t *used, const char *text, size_t length) {
    const size_t room = sizeof(shader->text) - 1 - *used;
    const size_t count = length < room ? length : room;
    memcpy(shader->text + *used, text, count);
    *used += count;
    shader->text[*used] = 0;
}

CurvedShader with_curve(const char *fragment) {
    CurvedShader shader;
    size_t used = 0;
    shader.text[0] = 0;
    const char *line_end = strchr(fragment, '\n');
    const size_t head = line_end != 0 ? static_cast<size_t>(line_end - fragment) + 1 : 0;
    append_shader_text(&shader, &used, fragment, head);
    append_shader_text(&shader, &used, kCurveGlsl, strlen(kCurveGlsl));
    append_shader_text(&shader, &used, fragment + head, strlen(fragment + head));
    return shader;
}

// u_curve and u_shown of the program in use: the present's curved screen over a shown rectangle of this size
void set_curve(GLuint program, int32_t width, int32_t height) {
    gl.glUniform1f(gl.glGetUniformLocation(program, "u_curve"), g_options.crt_curve != 0 ? 1.0f : 0.0f);
    gl.glUniform2f(gl.glGetUniformLocation(program, "u_shown"), static_cast<float>(width), static_cast<float>(height));
}

// The programs of the ambient occlusion passes, made at the first target that wants them (never with the option off)
GLuint make_program(const char *vertex_source, const char *fragment_source) {
    GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = 0;
    if (vertex != 0 && fragment != 0) {
        program = gl.glCreateProgram();
        gl.glAttachShader(program, vertex);
        gl.glAttachShader(program, fragment);
        gl.glLinkProgram(program);
        GLint linked = 0;
        gl.glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            gl.glGetProgramInfoLog(program, sizeof(g_error), 0, g_error);
            gl.glDeleteProgram(program);
            program = 0;
        }
    }
    if (vertex != 0) {
        gl.glDeleteShader(vertex);
    }
    if (fragment != 0) {
        gl.glDeleteShader(fragment);
    }
    return program;
}

bool ensure_ao_programs() {
    if (!g_ao_programs_tried) {
        g_ao_programs_tried = true;
        g_ao_program = make_program(kPresentVertexShader, kAoFragmentShader);
        g_ao_blur_program = make_program(kPresentVertexShader, kAoBlurFragmentShader);
        g_ao_composite_program = make_program(kPresentVertexShader, kAoCompositeFragmentShader);
        if (g_ao_program == 0 || g_ao_blur_program == 0 || g_ao_composite_program == 0) {
            printf("video: ambient occlusion unavailable: %s\n", g_error);
            return false;
        }
        gl.glUseProgram(g_ao_program);
        gl.glUniform1i(gl.glGetUniformLocation(g_ao_program, "u_depth"), 0);
        g_ao_u_size = gl.glGetUniformLocation(g_ao_program, "u_size");
        g_ao_u_params = gl.glGetUniformLocation(g_ao_program, "u_params");
        g_ao_u_taps = gl.glGetUniformLocation(g_ao_program, "u_taps");
        gl.glUseProgram(g_ao_blur_program);
        gl.glUniform1i(gl.glGetUniformLocation(g_ao_blur_program, "u_ao"), 0);
        g_ao_u_dir = gl.glGetUniformLocation(g_ao_blur_program, "u_dir");
        gl.glUseProgram(g_ao_composite_program);
        gl.glUniform1i(gl.glGetUniformLocation(g_ao_composite_program, "u_depth"), 0);
        gl.glUniform1i(gl.glGetUniformLocation(g_ao_composite_program, "u_ao"), 1);
        g_ao_u_fade = gl.glGetUniformLocation(g_ao_composite_program, "u_fade");
        gl.glUseProgram(g_program);
        if (g_present_array == 0) {
            gl.glGenVertexArrays(1, &g_present_array);
        }
    }
    return g_ao_program != 0 && g_ao_blur_program != 0 && g_ao_composite_program != 0;
}

// A single-sample 2D texture for the occlusion passes: depth, or RG16F colour, with nearest filtering and no mip chain
GLuint make_ao_texture(GLenum internal_format, GLenum format, GLenum type, int32_t width, int32_t height) {
    GLuint texture = 0;
    gl.glGenTextures(1, &texture);
    gl.glBindTexture(GL_TEXTURE_2D, texture);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, 0);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

// The single-sample depth copy and the two half-resolution occlusion buffers of a target of this size. False (the passes stay off) when one is incomplete.
bool create_ao_buffers(int32_t width, int32_t height) {
    const int32_t half_width = (width + 1) / 2;
    const int32_t half_height = (height + 1) / 2;
    g_ao_depth_texture = make_ao_texture(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, width, height);
    gl.glGenFramebuffers(1, &g_ao_depth_fbo);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_ao_depth_fbo);
    gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_ao_depth_texture, 0);
    gl.glDrawBuffer(GL_NONE);
    gl.glReadBuffer(GL_NONE);
    bool complete = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl.glGenFramebuffers(2, g_ao_fbo);
    for (int index = 0; index < 2; ++index) {
        g_ao_texture[index] = make_ao_texture(GL_RG16F, GL_RG, GL_FLOAT, half_width, half_height);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, g_ao_fbo[index]);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_ao_texture[index], 0);
        complete = complete && gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    return complete;
}

// The CRT present's and the dither pass's programs, made the first time a target or a present wants them (never with their options off)
bool g_crt_tried = false;
bool g_dither_tried = false;

void ensure_present_array() {
    if (g_present_array == 0) {
        gl.glGenVertexArrays(1, &g_present_array);
    }
}

bool ensure_crt_program() {
    if (!g_crt_tried) {
        g_crt_tried = true;
        g_crt_program = make_program(kPresentVertexShader, with_curve(kCrtFragmentShader).text);
        if (g_crt_program == 0) {
            printf("video: CRT scaling unavailable: %s\n", g_error);
        } else {
            gl.glUseProgram(g_crt_program);
            gl.glUniform1i(gl.glGetUniformLocation(g_crt_program, "u_image"), 0);
            g_crt_source = gl.glGetUniformLocation(g_crt_program, "u_source");
            g_crt_scale = gl.glGetUniformLocation(g_crt_program, "u_scale");
            g_crt_mask = gl.glGetUniformLocation(g_crt_program, "u_mask");
            gl.glUseProgram(g_program);
            ensure_present_array();
        }
    }
    return g_crt_program != 0;
}

bool g_plain_tried = false;

bool ensure_plain_program() {
    if (!g_plain_tried) {
        g_plain_tried = true;
        g_plain_program = make_program(kPresentVertexShader, with_curve(kPlainFragmentShader).text);
        if (g_plain_program == 0) {
            printf("video: curved screen unavailable: %s\n", g_error);
        } else {
            gl.glUseProgram(g_plain_program);
            gl.glUniform1i(gl.glGetUniformLocation(g_plain_program, "u_image"), 0);
            g_plain_source = gl.glGetUniformLocation(g_plain_program, "u_source");
            g_plain_nearest = gl.glGetUniformLocation(g_plain_program, "u_nearest");
            gl.glUseProgram(g_program);
            ensure_present_array();
        }
    }
    return g_plain_program != 0;
}

bool ensure_dither_program() {
    if (!g_dither_tried) {
        g_dither_tried = true;
        g_dither_program = make_program(kPresentVertexShader, kDitherFragmentShader);
        if (g_dither_program == 0) {
            printf("video: PlayStation dither unavailable: %s\n", g_error);
        } else {
            gl.glUseProgram(g_dither_program);
            gl.glUniform1i(gl.glGetUniformLocation(g_dither_program, "u_image"), 0);
            g_dither_size = gl.glGetUniformLocation(g_dither_program, "u_size");
            gl.glUseProgram(g_program);
            ensure_present_array();
        }
    }
    return g_dither_program != 0;
}

bool create_target(int32_t width, int32_t height) {
    release_target();
    GLint max_samples = 0;
    gl.glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    g_samples = g_options.samples > max_samples ? max_samples : g_options.samples;
    if (g_samples < 2) {
        g_samples = 0;
    }
    // ambient occlusion needs the depth as a texture; with the option off the depth is the renderbuffer it always was
    g_target_ao = g_options.ambient_occlusion != 0;
    const bool occlusion = g_target_ao && ensure_ao_programs();
    gl.glGenFramebuffers(1, &g_framebuffer);
    gl.glGenRenderbuffers(1, &g_color_buffer);
    gl.glBindRenderbuffer(GL_RENDERBUFFER, g_color_buffer);
    if (g_samples != 0) {
        gl.glRenderbufferStorageMultisample(GL_RENDERBUFFER, g_samples, GL_RGBA8, width, height);
    } else {
        gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    }
    if (occlusion) {
        const GLenum kind = g_samples != 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        gl.glGenTextures(1, &g_depth_texture);
        gl.glBindTexture(kind, g_depth_texture);
        if (g_samples != 0) {
            gl.glTexImage2DMultisample(kind, g_samples, GL_DEPTH_COMPONENT24, width, height, GL_TRUE);
        } else {
            gl.glTexImage2D(kind, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
            gl.glTexParameteri(kind, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.glTexParameteri(kind, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
    } else {
        gl.glGenRenderbuffers(1, &g_depth_buffer);
        gl.glBindRenderbuffer(GL_RENDERBUFFER, g_depth_buffer);
        if (g_samples != 0) {
            gl.glRenderbufferStorageMultisample(GL_RENDERBUFFER, g_samples, GL_DEPTH_COMPONENT24, width, height);
        } else {
            gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        }
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_color_buffer);
    if (occlusion) {
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, g_samples != 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, g_depth_texture, 0);
    } else {
        gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depth_buffer);
    }
    if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        set_error("incomplete framebuffer");
        return false;
    }
    if (occlusion) {
        g_ao_ready = create_ao_buffers(width, height);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
        if (!g_ao_ready) {
            printf("video: ambient occlusion unavailable: incomplete buffers\n");
        }
    }
    if (g_samples != 0 || wants_resolve(width, height)) {
        gl.glGenFramebuffers(1, &g_resolve_framebuffer);
        gl.glGenTextures(1, &g_resolve_texture);
        gl.glBindTexture(GL_TEXTURE_2D, g_resolve_texture);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, g_resolve_framebuffer);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_resolve_texture, 0);
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            set_error("incomplete resolve framebuffer");
            return false;
        }
        gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    }
    if (g_options.psx_dither != 0 && g_resolve_texture != 0 && ensure_dither_program()) {
        // the dithered copy of the picture: the present reads it instead of the resolved target
        gl.glGenFramebuffers(1, &g_dither_fbo);
        gl.glGenTextures(1, &g_dither_texture);
        gl.glBindTexture(GL_TEXTURE_2D, g_dither_texture);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, g_dither_fbo);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_dither_texture, 0);
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("video: PlayStation dither unavailable: incomplete buffer\n");
            gl.glDeleteFramebuffers(1, &g_dither_fbo);
            gl.glDeleteTextures(1, &g_dither_texture);
            g_dither_fbo = 0;
            g_dither_texture = 0;
        }
        gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    }
    if (g_samples != 0) {
        gl.glEnable(GL_MULTISAMPLE);
    } else {
        gl.glDisable(GL_MULTISAMPLE);
    }
    gl.glViewport(0, 0, width, height);
    g_target_width = width;
    g_target_height = height;
    if (g_read_back != 0) {
        OT_SDL(SDL_free)(g_read_back);
    }
    g_read_back = static_cast<uint32_t *>(OT_SDL(SDL_malloc)(static_cast<size_t>(width) * height * 8u));
    return g_read_back != 0;
}

// The rectangle of a `width` x `height` window the picture covers: all of it when stretched, else the largest one of the mode's shape, centred.
void shown_rectangle(int width, int height, int *left, int *bottom, int *shown_width, int *shown_height) {
    *shown_width = width;
    *shown_height = height;
    if (!g_options.stretch && g_mode.width > 0 && g_mode.height > 0) {
        *shown_height = static_cast<int>(static_cast<long long>(width) * g_mode.height / g_mode.width);
        if (*shown_height > height) {
            *shown_height = height;
            *shown_width = static_cast<int>(static_cast<long long>(height) * g_mode.width / g_mode.height);
        }
    }
    *left = (width - *shown_width) / 2;
    *bottom = (height - *shown_height) / 2;
}

// The size the options ask of the target: the mode's, a fixed one, or what the picture covers of the window
void wanted_target(int32_t *width, int32_t *height) {
    *width = g_mode.width;
    *height = g_mode.height;
    if (g_options.follow_window) {
        SDL_Window *window = ot_hal_sdl_window_get();
        int window_width = 0;
        int window_height = 0;
        if (window != 0 && OT_SDL(SDL_GetWindowSizeInPixels)(window, &window_width, &window_height) && window_width > 0 && window_height > 0) {
            int left = 0;
            int bottom = 0;
            int shown_width = 0;
            int shown_height = 0;
            shown_rectangle(window_width, window_height, &left, &bottom, &shown_width, &shown_height);
            if (shown_width > 0 && shown_height > 0) {
                *width = shown_width;
                *height = shown_height;
            }
        }
    } else if (g_options.target_width > 0 && g_options.target_height > 0) {
        *width = g_options.target_width;
        *height = g_options.target_height;
    }
    GLint limit = 0;
    gl.glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &limit);
    g_supersample_used = 100;
    if (g_options.supersample > 100 && (g_options.follow_window || (g_options.target_width > 0 && g_options.target_height > 0))) {
        // the largest factor up to the one asked (in steps of 5%) whose target fits the driver's size limit and about 400 MB: width x height x (4 colour + 4 depth) x samples
        // (the budget limits only the factor: a window-sized target is made whatever it costs)
        GLint max_samples = 0;
        gl.glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
        const int64_t samples = g_options.samples < 2 ? 1 : (g_options.samples > max_samples ? (max_samples < 1 ? 1 : max_samples) : g_options.samples);
        for (int32_t factor = g_options.supersample > 400 ? 400 : g_options.supersample; factor > 100; factor -= 5) {
            const int64_t scaled_width = (static_cast<int64_t>(*width) * factor / 100 + 1) & ~static_cast<int64_t>(1);
            const int64_t scaled_height = (static_cast<int64_t>(*height) * factor / 100 + 1) & ~static_cast<int64_t>(1);
            // (ambient occlusion adds a single-sample depth copy, 4 bytes, and two half-resolution RG16F buffers, 2 bytes, a pixel)
            if ((limit > 0 && (scaled_width > limit || scaled_height > limit)) ||
                scaled_width * scaled_height * (8 * samples + (g_options.ambient_occlusion != 0 ? 6 : 0)) > kSupersampleBudget) {
                continue;
            }
            *width = static_cast<int32_t>(scaled_width);
            *height = static_cast<int32_t>(scaled_height);
            g_supersample_used = factor;
            break;
        }
    }
    if (limit > 0 && (*width > limit || *height > limit)) {
        // still too big (a fixed size, or the window itself): the largest target of the same shape the driver takes
        const double fit = static_cast<double>(limit) / static_cast<double>(*width > *height ? *width : *height);
        *width = static_cast<int32_t>(static_cast<double>(*width) * fit) & ~1;
        *height = static_cast<int32_t>(static_cast<double>(*height) * fit) & ~1;
        if (*width < 2 || *height < 2) {
            *width = g_mode.width;
            *height = g_mode.height;
        }
    }
}

bool create_wanted_target() {
    int32_t width = 0;
    int32_t height = 0;
    wanted_target(&width, &height);
    return create_target(width, height);
}

// lround, which the VC6 SDK headers of the video_replay target lack: half away from zero
int64_t round_away(double value) { return static_cast<int64_t>(value >= 0.0 ? floor(value + 0.5) : ceil(value - 0.5)); }

// GL_EXT_texture_filter_anisotropic / ARB_texture_filter_anisotropic and its limit (1 without them)
void check_anisotropy() {
    if (g_anisotropy_checked) {
        return;
    }
    g_anisotropy_checked = true;
    GLint count = 0;
    gl.glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint index = 0; index < count; ++index) {
        const char *name = reinterpret_cast<const char *>(gl.glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
        if (name != 0 && (strcmp(name, "GL_EXT_texture_filter_anisotropic") == 0 || strcmp(name, "GL_ARB_texture_filter_anisotropic") == 0)) {
            GLfloat limit = 1.0f;
            gl.glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &limit);
            if (limit > 1.0f) {
                g_anisotropy_limit = limit;
            }
            return;
        }
    }
}

void release_samplers() {
    if (g_samplers_made) {
        gl.glDeleteSamplers(6, g_samplers);
        g_samplers_made = false;
    }
}

// The LOD bias of the world's filtered draws: the option's, and -log2 of the supersampling factor (at most -2), so a texture is as sharp as it is at the picture's own size
float world_lod_bias() {
    float bias = g_options.lod_bias;
    if (g_supersample_used > 100) {
        const float sharpen = -static_cast<float>(log(static_cast<double>(g_supersample_used) / 100.0) / 0.6931471805599453);
        bias += sharpen < -2.0f ? -2.0f : sharpen;
    }
    return bias;
}

// The six samplers for the options' anisotropy (1 = trilinear, 2..16 = that many taps up to the driver's limit) and LOD bias
void ensure_samplers() {
    if (g_samplers_made && g_sampler_anisotropy == g_options.anisotropy && g_sampler_bias == world_lod_bias()) {
        return;
    }
    check_anisotropy();
    release_samplers();
    gl.glGenSamplers(6, g_samplers);
    float taps = static_cast<float>(g_options.anisotropy);
    if (taps > g_anisotropy_limit) {
        taps = g_anisotropy_limit;
    }
    for (int index = 0; index < 6; ++index) {
        const GLuint sampler = g_samplers[index];
        const GLint wrap = (index & 1) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        const GLint min_filter = index < 2 ? GL_NEAREST : index < 4 ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
        gl.glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, min_filter);
        gl.glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, index < 2 ? GL_NEAREST : GL_LINEAR);
        gl.glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, wrap);
        gl.glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, wrap);
        if (index >= 4) {
            if (taps > 1.0f) {
                gl.glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY_EXT, taps);
            }
            if (world_lod_bias() != 0.0f) {
                gl.glSamplerParameterf(sampler, GL_TEXTURE_LOD_BIAS, world_lod_bias());
            }
        }
    }
    g_samplers_made = true;
    g_sampler_anisotropy = g_options.anisotropy;
    g_sampler_bias = world_lod_bias();
}

// The mip chain of a texture whose alpha is a cut-out (every texel 0 or 255: a fence, a leaf, a chain-link). Averaged alpha would thin it out with distance, and the
// colour of the clear texels would darken its edges, so: each level averages colour weighted by alpha, and its alpha is scaled (clipped at 1) until the share of
// texels past the cut-off (one half) is the base level's. `base` is level 0 as BGRA. False when there is nothing to preserve (opaque, or alpha that is not a cut-out):
// the caller lets the driver build the chain. `smooth`: level 0 is a picture from the texture pack of a texture that is a cut-out, whose alpha an upscaler made smooth;
// it counts as a cut-out all the same, its coverage being the texels at one half or more (for alpha of 0 and 255 only, the same count as without).
struct MipTexel {
    float r, g, b, a; // colour 0..255, alpha 0..1 (the true average, which the next level is made from)
};

bool build_cutout_mips(const uint32_t *base, int32_t width, int32_t height, bool smooth) {
    const uint32_t texels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    uint32_t covered = 0;
    for (uint32_t index = 0; index < texels; ++index) {
        const uint32_t alpha = base[index] >> 24;
        if (!smooth && alpha != 0 && alpha != 255) {
            return false;
        }
        covered += alpha >= 128 ? 1u : 0u;
    }
    if (covered == 0 || covered == texels || width * height < 2) {
        return false;
    }
    const float target = static_cast<float>(covered) / static_cast<float>(texels);
    MipTexel *previous = static_cast<MipTexel *>(OT_SDL(SDL_malloc)(sizeof(MipTexel) * texels));
    MipTexel *current = static_cast<MipTexel *>(OT_SDL(SDL_malloc)(sizeof(MipTexel) * texels));
    uint32_t *bytes = static_cast<uint32_t *>(OT_SDL(SDL_malloc)(sizeof(uint32_t) * texels));
    if (previous == 0 || current == 0 || bytes == 0) {
        OT_SDL(SDL_free)(previous);
        OT_SDL(SDL_free)(current);
        OT_SDL(SDL_free)(bytes);
        return false;
    }
    for (uint32_t index = 0; index < texels; ++index) {
        previous[index].r = static_cast<float>((base[index] >> 16) & 0xffu);
        previous[index].g = static_cast<float>((base[index] >> 8) & 0xffu);
        previous[index].b = static_cast<float>(base[index] & 0xffu);
        previous[index].a = static_cast<float>(base[index] >> 24) / 255.0f;
    }
    int32_t source_width = width;
    int32_t source_height = height;
    for (int32_t level = 1; source_width > 1 || source_height > 1; ++level) {
        const int32_t level_width = source_width > 1 ? source_width / 2 : 1;
        const int32_t level_height = source_height > 1 ? source_height / 2 : 1;
        const uint32_t count = static_cast<uint32_t>(level_width) * static_cast<uint32_t>(level_height);
        for (int32_t y = 0; y < level_height; ++y) {
            for (int32_t x = 0; x < level_width; ++x) {
                float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f, plain_r = 0.0f, plain_g = 0.0f, plain_b = 0.0f;
                for (int32_t k = 0; k < 4; ++k) {
                    const int32_t sx = (2 * x + (k & 1)) < source_width ? 2 * x + (k & 1) : source_width - 1;
                    const int32_t sy = (2 * y + (k >> 1)) < source_height ? 2 * y + (k >> 1) : source_height - 1;
                    const MipTexel &texel = previous[sy * source_width + sx];
                    r += texel.r * texel.a;
                    g += texel.g * texel.a;
                    b += texel.b * texel.a;
                    plain_r += texel.r;
                    plain_g += texel.g;
                    plain_b += texel.b;
                    a += texel.a;
                }
                MipTexel &out = current[y * level_width + x];
                out.a = a * 0.25f;
                out.r = a > 0.0f ? r / a : plain_r * 0.25f;
                out.g = a > 0.0f ? g / a : plain_g * 0.25f;
                out.b = a > 0.0f ? b / a : plain_b * 0.25f;
            }
        }
        // the smallest scale (1..64) that gives the base level's share of texels past one half
        float scale = 1.0f;
        uint32_t past = 0;
        for (uint32_t index = 0; index < count; ++index) {
            past += current[index].a > 0.5f ? 1u : 0u;
        }
        if (static_cast<float>(past) < target * static_cast<float>(count)) {
            float low = 1.0f;
            float high = 64.0f;
            for (int step = 0; step < 14; ++step) {
                const float middle = (low + high) * 0.5f;
                uint32_t over = 0;
                for (uint32_t index = 0; index < count; ++index) {
                    over += current[index].a * middle > 0.5f ? 1u : 0u;
                }
                if (static_cast<float>(over) >= target * static_cast<float>(count)) {
                    high = middle;
                } else {
                    low = middle;
                }
            }
            scale = high;
        }
        for (uint32_t index = 0; index < count; ++index) {
            const float alpha = current[index].a * scale;
            const uint32_t a8 = static_cast<uint32_t>((alpha > 1.0f ? 1.0f : alpha) * 255.0f + 0.5f);
            bytes[index] = (a8 << 24) | (static_cast<uint32_t>(current[index].r + 0.5f) << 16) | (static_cast<uint32_t>(current[index].g + 0.5f) << 8) |
                           static_cast<uint32_t>(current[index].b + 0.5f);
        }
        gl.glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, level_width, level_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, bytes);
        MipTexel *swap = previous;
        previous = current;
        current = swap;
        source_width = level_width;
        source_height = level_height;
    }
    OT_SDL(SDL_free)(previous);
    OT_SDL(SDL_free)(current);
    OT_SDL(SDL_free)(bytes);
    return true;
}

// The mip chain of the bound texture, from its level 0 as it is now
void build_mips(Texture &texture) {
    uint32_t *base = static_cast<uint32_t *>(OT_SDL(SDL_malloc)(static_cast<size_t>(texture.real_width) * texture.real_height * 4u));
    bool built = false;
    if (base != 0) {
        gl.glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, base);
        built = build_cutout_mips(base, texture.real_width, texture.real_height, texture.replaced && texture.cutout == 1);
        g_stats.mip_cutouts += built ? 1u : 0u;
        if (!texture.replaced) {
            texture.cutout = built ? 1 : 2; // a replaced texture's class is its original's: an upscaled alpha is smooth, which would drop it out of the cut-out path
        }
        OT_SDL(SDL_free)(base);
    }
    if (!built) {
        gl.glGenerateMipmap(GL_TEXTURE_2D);
    }
}

// Whether the bound texture is a cut-out (every texel's alpha 0 or 255, some of each), looked at once after its texels change (build_mips settles it too)
bool is_cutout(Texture &texture) {
    if (texture.cutout == 0) {
        const uint32_t texels = static_cast<uint32_t>(texture.width) * static_cast<uint32_t>(texture.height);
        uint32_t *base = static_cast<uint32_t *>(OT_SDL(SDL_malloc)(static_cast<size_t>(texels) * 4u));
        texture.cutout = 2;
        if (base != 0) {
            gl.glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, base);
            uint32_t clear = 0;
            uint32_t solid = 0;
            for (uint32_t index = 0; index < texels; ++index) {
                const uint32_t alpha = base[index] >> 24;
                clear += alpha == 0 ? 1u : 0u;
                solid += alpha == 255 ? 1u : 0u;
            }
            texture.cutout = (clear != 0 && solid != 0 && clear + solid == texels) ? 1 : 2;
            OT_SDL(SDL_free)(base);
        }
    }
    return texture.cutout == 1;
}

// A rectangle in mode pixels (top-left origin) as the target's scissor
void scissor_mode(int32_t x, int32_t y, int32_t w, int32_t h) {
    g_scissor[0] = x;
    g_scissor[1] = y;
    g_scissor[2] = w;
    g_scissor[3] = h;
    if (g_target_width == g_mode.width && g_target_height == g_mode.height) {
        gl.glScissor(x, g_mode.height - y - h, w, h);
        return;
    }
    const double sx = static_cast<double>(g_target_width) / g_mode.width;
    const double sy = static_cast<double>(g_target_height) / g_mode.height;
    const int32_t left = static_cast<int32_t>(round_away(x * sx));
    const int32_t right = static_cast<int32_t>(round_away((x + w) * sx));
    const int32_t top = static_cast<int32_t>(round_away(y * sy));
    const int32_t bottom = static_cast<int32_t>(round_away((y + h) * sy));
    gl.glScissor(left, g_target_height - bottom, right - left, bottom - top);
}

// The single-sample picture of the target: the target itself, or its copy (made here) when there is one
GLuint resolved_target() {
    if (g_resolve_framebuffer == 0) {
        return g_framebuffer;
    }
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, g_framebuffer);
    gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_resolve_framebuffer);
    gl.glDisable(GL_SCISSOR_TEST);
    gl.glBlitFramebuffer(0, 0, g_target_width, g_target_height, 0, 0, g_target_width, g_target_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.glEnable(GL_SCISSOR_TEST);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    return g_resolve_framebuffer;
}

// The ambient occlusion passes over the part of the frame the world draws since the last pass covered (g_ao_rect). Leaves the state a draw expects: the target
// bound, the main program and vertex array, the full viewport, the scissor as it was.
void ao_apply() {
    if (!g_ao_pending) {
        return;
    }
    g_ao_pending = false;
    if (!g_ao_ready) {
        return;
    }
    const int32_t saved[4] = {g_scissor[0], g_scissor[1], g_scissor[2], g_scissor[3]};
    const int32_t half_width = (g_target_width + 1) / 2;
    const int32_t half_height = (g_target_height + 1) / 2;
    // the draws that follow may leave these on: the coverage would take the composite's alpha, a sampler object would replace the textures' own parameters
    if (g_coverage_state == 1) {
        g_coverage_state = 0;
        gl.glUniform1i(g_uniform_coverage, 0);
        gl.glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
    if (g_sampler_bound) {
        gl.glBindSampler(0, 0);
        g_sampler_bound = false;
    }
    gl.glDisable(GL_SCISSOR_TEST);
    gl.glDisable(GL_BLEND);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDepthMask(GL_FALSE);
    // a single-sample copy of the depth (the multisampled one is resolved by taking a sample)
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, g_framebuffer);
    gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_ao_depth_fbo);
    gl.glBlitFramebuffer(0, 0, g_target_width, g_target_height, 0, 0, g_target_width, g_target_height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    gl.glBindVertexArray(g_present_array);
    gl.glActiveTexture(GL_TEXTURE0);
    // 1: the occlusion, from the depth copy
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_ao_fbo[0]);
    gl.glViewport(0, 0, half_width, half_height);
    gl.glUseProgram(g_ao_program);
    gl.glUniform4f(g_ao_u_size, static_cast<float>(g_target_width), static_cast<float>(g_target_height), 0.0f, 0.0f);
    gl.glUniform4f(g_ao_u_params, kAoRadius, kAoStrength, kAoTan, kAoBias);
    gl.glUniform1i(g_ao_u_taps, g_options.ambient_occlusion >= 2 ? 12 : 8);
    gl.glBindTexture(GL_TEXTURE_2D, g_ao_depth_texture);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    // 2: the blur, across and then down (the result is in the first buffer again)
    gl.glUseProgram(g_ao_blur_program);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_ao_fbo[1]);
    gl.glUniform2i(g_ao_u_dir, 1, 0);
    gl.glBindTexture(GL_TEXTURE_2D, g_ao_texture[0]);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_ao_fbo[0]);
    gl.glUniform2i(g_ao_u_dir, 0, 1);
    gl.glBindTexture(GL_TEXTURE_2D, g_ao_texture[1]);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    // 3: multiplied onto the colour, inside the part of the frame the world covered
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    gl.glViewport(0, 0, g_target_width, g_target_height);
    scissor_mode(g_ao_rect[0], g_ao_rect[1], g_ao_rect[2] - g_ao_rect[0], g_ao_rect[3] - g_ao_rect[1]);
    gl.glEnable(GL_SCISSOR_TEST);
    gl.glEnable(GL_BLEND);
    gl.glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
    gl.glBlendEquation(GL_FUNC_ADD);
    gl.glUseProgram(g_ao_composite_program);
    gl.glUniform2f(g_ao_u_fade, kAoFadeFrom, kAoFadeTo);
    gl.glActiveTexture(GL_TEXTURE1);
    gl.glBindTexture(GL_TEXTURE_2D, g_ao_texture[0]);
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindTexture(GL_TEXTURE_2D, g_ao_depth_texture);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    gl.glDisable(GL_BLEND);
    gl.glUseProgram(g_program);
    gl.glBindVertexArray(g_vertex_array);
    scissor_mode(saved[0], saved[1], saved[2], saved[3]);
    ++g_stats.ao_passes;
    g_ao_after_pass = true;
}

int OT_HAL_CALL video_init(const OtHalVideoMode *mode) {
    SDL_Window *window = ot_hal_sdl_window_get();
    if (window == 0) {
        set_error("no SDL window");
        return 0;
    }
    g_context = OT_SDL(SDL_GL_CreateContext)(window);
    if (g_context == 0) {
        set_error(OT_SDL(SDL_GetError)());
        return 0;
    }
    OT_SDL(SDL_GL_MakeCurrent)(window, g_context);
    if (!load_gl()) {
        return 0;
    }
    log_gpu();
    GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0) {
        return 0;
    }
    g_program = gl.glCreateProgram();
    gl.glAttachShader(g_program, vertex);
    gl.glAttachShader(g_program, fragment);
    gl.glLinkProgram(g_program);
    GLint linked = 0;
    gl.glGetProgramiv(g_program, GL_LINK_STATUS, &linked);
    gl.glDeleteShader(vertex);
    gl.glDeleteShader(fragment);
    if (!linked) {
        gl.glGetProgramInfoLog(g_program, sizeof(g_error), 0, g_error);
        return 0;
    }
    gl.glUseProgram(g_program);
    g_uniform_target = gl.glGetUniformLocation(g_program, "u_target");
    g_uniform_mode = gl.glGetUniformLocation(g_program, "u_mode");
    g_uniform_flat = gl.glGetUniformLocation(g_program, "u_flat");
    g_uniform_alpha_test = gl.glGetUniformLocation(g_program, "u_alpha_test");
    g_uniform_alpha_ref = gl.glGetUniformLocation(g_program, "u_alpha_ref");
    g_uniform_coverage = gl.glGetUniformLocation(g_program, "u_coverage");
    g_uniform_sharp = gl.glGetUniformLocation(g_program, "u_sharp");
    g_sharp_width = 0.0f;
    g_sharp_height = 0.0f;
    g_coverage_state = -1;
    gl.glUniform1i(gl.glGetUniformLocation(g_program, "u_texture"), 0);

    gl.glGenVertexArrays(1, &g_vertex_array);
    gl.glBindVertexArray(g_vertex_array);
    gl.glGenBuffers(1, &g_vertex_buffer);
    gl.glBindBuffer(GL_ARRAY_BUFFER, g_vertex_buffer);
    gl.glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(OtHalVertex), reinterpret_cast<void *>(0));
    gl.glVertexAttribPointer(1, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(OtHalVertex), reinterpret_cast<void *>(16));
    gl.glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(OtHalVertex), reinterpret_cast<void *>(24));
    gl.glEnableVertexAttribArray(0);
    gl.glEnableVertexAttribArray(1);
    gl.glEnableVertexAttribArray(2);
    gl.glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.glDepthFunc(GL_LEQUAL);
    gl.glEnable(GL_SCISSOR_TEST);
    memset(g_textures, 0, sizeof(g_textures));
    memset(g_palettes, 0, sizeof(g_palettes));
    memset(&g_stats, 0, sizeof(g_stats));
    g_pack_scanned = false; // the folder is read at the first upload that could use it
    g_pack_ticks = 0;
    g_mip_ticks = 0;
    g_draw_ticks = 0;
    g_swap_ticks = 0;
    g_samplers_made = false;
    g_sampler_bound = false;
    g_anisotropy_checked = false;
    g_anisotropy_limit = 1.0f;
    g_ao_programs_tried = false;
    g_ao_program = 0;
    g_ao_blur_program = 0;
    g_ao_composite_program = 0;
    g_mode = *mode;
    // the sharp present's program and its empty vertex array (the triangle comes from gl_VertexID)
    {
        GLuint present_vertex = compile(GL_VERTEX_SHADER, kPresentVertexShader);
        GLuint present_fragment = compile(GL_FRAGMENT_SHADER, with_curve(kPresentFragmentShader).text);
        if (present_vertex != 0 && present_fragment != 0) {
            g_present_program = gl.glCreateProgram();
            gl.glAttachShader(g_present_program, present_vertex);
            gl.glAttachShader(g_present_program, present_fragment);
            gl.glLinkProgram(g_present_program);
            GLint present_linked = 0;
            gl.glGetProgramiv(g_present_program, GL_LINK_STATUS, &present_linked);
            if (!present_linked) {
                gl.glDeleteProgram(g_present_program);
                g_present_program = 0;
            } else {
                gl.glUseProgram(g_present_program);
                gl.glUniform1i(gl.glGetUniformLocation(g_present_program, "u_image"), 0);
                g_present_source = gl.glGetUniformLocation(g_present_program, "u_source");
                g_present_scale = gl.glGetUniformLocation(g_present_program, "u_scale");
                gl.glUseProgram(g_program);
                gl.glGenVertexArrays(1, &g_present_array);
            }
        }
        // the downsample's program, from the same vertex shader
        GLuint downsample_fragment = present_vertex != 0 ? compile(GL_FRAGMENT_SHADER, with_curve(kDownsampleFragmentShader).text) : 0;
        if (downsample_fragment != 0) {
            g_downsample_program = gl.glCreateProgram();
            gl.glAttachShader(g_downsample_program, present_vertex);
            gl.glAttachShader(g_downsample_program, downsample_fragment);
            gl.glLinkProgram(g_downsample_program);
            GLint downsample_linked = 0;
            gl.glGetProgramiv(g_downsample_program, GL_LINK_STATUS, &downsample_linked);
            if (!downsample_linked) {
                gl.glDeleteProgram(g_downsample_program);
                g_downsample_program = 0;
            } else {
                gl.glUseProgram(g_downsample_program);
                gl.glUniform1i(gl.glGetUniformLocation(g_downsample_program, "u_image"), 0);
                g_downsample_source = gl.glGetUniformLocation(g_downsample_program, "u_source");
                g_downsample_ratio = gl.glGetUniformLocation(g_downsample_program, "u_ratio");
                g_downsample_taps = gl.glGetUniformLocation(g_downsample_program, "u_taps");
                gl.glUseProgram(g_program);
                if (g_present_array == 0) {
                    gl.glGenVertexArrays(1, &g_present_array);
                }
            }
            gl.glDeleteShader(downsample_fragment);
        }
        if (present_vertex != 0) {
            gl.glDeleteShader(present_vertex);
        }
        if (present_fragment != 0) {
            gl.glDeleteShader(present_fragment);
        }
    }
    if (!create_wanted_target()) {
        return 0;
    }
    scissor_mode(0, 0, mode->width, mode->height);
    gl.glUniform4f(g_uniform_target, static_cast<float>(mode->width), static_cast<float>(mode->height), 0.0f, 0.0f);
    return 1;
}

void OT_HAL_CALL video_shutdown(void) {
    if (g_context == 0) {
        return;
    }
    for (uint32_t index = 1; index < kMaxTextures; ++index) {
        if (g_textures[index].name != 0) {
            gl.glDeleteTextures(1, &g_textures[index].name);
        }
        if (g_textures[index].indices != 0) {
            OT_SDL(SDL_free)(g_textures[index].indices);
        }
    }
    OT_SDL(SDL_free)(g_pack_keys);
    g_pack_keys = 0;
    g_pack_count = 0;
    release_samplers();
    release_target();
    GLuint *const ao_programs[3] = {&g_ao_program, &g_ao_blur_program, &g_ao_composite_program};
    for (GLuint *program : ao_programs) {
        if (*program != 0) {
            gl.glDeleteProgram(*program);
            *program = 0;
        }
    }
    gl.glDeleteBuffers(1, &g_vertex_buffer);
    gl.glDeleteVertexArrays(1, &g_vertex_array);
    gl.glDeleteProgram(g_program);
    if (g_present_program != 0) {
        gl.glDeleteProgram(g_present_program);
        g_present_program = 0;
    }
    if (g_downsample_program != 0) {
        gl.glDeleteProgram(g_downsample_program);
        g_downsample_program = 0;
    }
    GLuint *const extra_programs[3] = {&g_crt_program, &g_plain_program, &g_dither_program};
    for (GLuint *program : extra_programs) {
        if (*program != 0) {
            gl.glDeleteProgram(*program);
            *program = 0;
        }
    }
    g_crt_tried = false;
    g_plain_tried = false;
    g_dither_tried = false;
    if (g_present_array != 0) {
        gl.glDeleteVertexArrays(1, &g_present_array);
        g_present_array = 0;
    }
    OT_SDL(SDL_GL_DestroyContext)(g_context);
    g_context = 0;
    if (g_line_quads != 0) {
        OT_SDL(SDL_free)(g_line_quads);
        g_line_quads = 0;
        g_line_capacity = 0;
    }
    if (g_read_back != 0) {
        OT_SDL(SDL_free)(g_read_back);
        g_read_back = 0;
    }
    if (g_shown_pixels != 0) {
        OT_SDL(SDL_free)(g_shown_pixels);
        g_shown_pixels = 0;
        g_shown_capacity = 0;
    }
}

int OT_HAL_CALL video_set_mode(const OtHalVideoMode *mode) {
    g_mode = *mode;
    if (!create_wanted_target()) {
        return 0;
    }
    scissor_mode(0, 0, mode->width, mode->height);
    gl.glUniform4f(g_uniform_target, static_cast<float>(mode->width), static_cast<float>(mode->height), 0.0f, 0.0f);
    return 1;
}

uint32_t OT_HAL_CALL video_enumerate_modes(OtHalVideoMode *modes, uint32_t capacity) {
    // The offscreen target takes any size; report the retail defaults.
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

// ---- Texture pack (g_options.texture_pack). Everything below is skipped while g_pack_dir is empty. ----

int compare_keys(const void *a, const void *b) {
    const PackKey &left = *static_cast<const PackKey *>(a);
    const PackKey &right = *static_cast<const PackKey *>(b);
    return left.key < right.key ? -1 : left.key > right.key ? 1 : left.dir < right.dir ? -1 : left.dir > right.dir ? 1 : 0;
}

int compare_key_only(const void *a, const void *b) {
    const uint64_t left = *static_cast<const uint64_t *>(a);
    const uint64_t right = static_cast<const PackKey *>(b)->key;
    return left < right ? -1 : left > right ? 1 : 0;
}

// The `index`-th folder of g_pack_dir (folders are joined by '|'): its start and length. False past the last one.
bool pack_folder(uint32_t index, const char *&start, size_t &length) {
    const char *at = g_pack_dir;
    for (;;) {
        const char *end = strchr(at, '|');
        const size_t size = end != 0 ? static_cast<size_t>(end - at) : strlen(at);
        if (index == 0) {
            start = at;
            length = size;
            return size != 0;
        }
        if (end == 0) {
            return false;
        }
        at = end + 1;
        --index;
    }
}

// "<16 lower-case hex digits>.png" -> the key
bool pack_file_key(const char *name, uint64_t &key) {
    uint64_t value = 0;
    int digits = 0;
    for (; digits < 16; ++digits) {
        const char c = name[digits];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | static_cast<uint64_t>(digit);
    }
    if (strcmp(name + 16, ".png") != 0) {
        return false;
    }
    key = value;
    return true;
}

// The names in the folders, once. A folder that cannot be read is an empty one. A key in several folders is kept for the first.
void pack_scan() {
    g_pack_scanned = true;
    OT_SDL(SDL_free)(g_pack_keys);
    g_pack_keys = 0;
    g_pack_count = 0;
    g_stats.pack_files = 0;
    uint32_t capacity = 0;
    for (uint32_t folder = 0; folder < 4; ++folder) {
        const char *start = 0;
        size_t length = 0;
        if (!pack_folder(folder, start, length) || length >= 1024) {
            continue;
        }
        char path[1024];
        memcpy(path, start, length);
        path[length] = ' ';
        int count = 0;
        char **names = OT_SDL(SDL_GlobDirectory)(path, 0, 0, &count);
        if (names == 0 || count <= 0) {
            OT_SDL(SDL_free)(names);
            continue;
        }
        PackKey *grown = static_cast<PackKey *>(OT_SDL(SDL_malloc)(sizeof(PackKey) * (capacity + static_cast<uint32_t>(count))));
        if (grown == 0) {
            OT_SDL(SDL_free)(names);
            continue;
        }
        if (g_pack_keys != 0) {
            memcpy(grown, g_pack_keys, sizeof(PackKey) * g_pack_count);
            OT_SDL(SDL_free)(g_pack_keys);
        }
        g_pack_keys = grown;
        capacity += static_cast<uint32_t>(count);
        for (int index = 0; index < count; ++index) {
            uint64_t key = 0;
            if (names[index] != 0 && pack_file_key(names[index], key)) {
                g_pack_keys[g_pack_count].key = key;
                g_pack_keys[g_pack_count].dir = folder;
                ++g_pack_count;
            }
        }
        OT_SDL(SDL_free)(names);
    }
    if (g_pack_count > 0) {
        qsort(g_pack_keys, g_pack_count, sizeof(PackKey), compare_keys);
        uint32_t kept = 0; // sorted by key then folder: the first of a run is the folder that wins
        for (uint32_t index = 0; index < g_pack_count; ++index) {
            if (kept == 0 || g_pack_keys[kept - 1].key != g_pack_keys[index].key) {
                g_pack_keys[kept++] = g_pack_keys[index];
            }
        }
        g_pack_count = kept;
    }
    g_stats.pack_files = g_pack_count;
}

// Whether the texels of a texture the game uploads are a cut-out (some clear, some set, nothing between), the way is_cutout reads it from the GL texture
uint8_t original_cutout(uint32_t format, const void *pixels, int32_t width, int32_t height) {
    const uint32_t texels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    const uint8_t *bytes = static_cast<const uint8_t *>(pixels);
    uint32_t clear = 0;
    uint32_t solid = 0;
    for (uint32_t index = 0; index < texels; ++index) {
        switch (format) {
        case OT_HAL_PIXEL_ARGB1555: {
            const bool set = (bytes[2 * index + 1] & 0x80u) != 0;
            clear += set ? 0u : 1u;
            solid += set ? 1u : 0u;
            break;
        }
        case OT_HAL_PIXEL_ARGB4444: {
            const uint32_t alpha = bytes[2 * index + 1] >> 4;
            clear += alpha == 0 ? 1u : 0u;
            solid += alpha == 15 ? 1u : 0u;
            break;
        }
        case OT_HAL_PIXEL_ARGB8888: {
            const uint32_t alpha = bytes[4 * index + 3];
            clear += alpha == 0 ? 1u : 0u;
            solid += alpha == 255 ? 1u : 0u;
            break;
        }
        default: // RGB565 has no alpha
            solid += 1u;
            break;
        }
    }
    return (clear != 0 && solid != 0 && clear + solid == texels) ? 1 : 2;
}

uint64_t pack_gpu_bytes(int32_t width, int32_t height) {
    const uint64_t level0 = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
    return g_options.anisotropy > 0 ? level0 + level0 / 3u : level0;
}

// A texture that was drawn from the pack goes back to its own size (before it takes other texels, or is destroyed)
void pack_release(Texture &texture, bool respecify) {
    if (!texture.replaced) {
        return;
    }
    g_stats.pack_bytes -= pack_gpu_bytes(texture.real_width, texture.real_height);
    --g_stats.pack_live;
    texture.replaced = 0;
    texture.real_width = texture.width;
    texture.real_height = texture.height;
    if (respecify) {
        gl.glBindTexture(GL_TEXTURE_2D, texture.name);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
        texture.mips_dirty = 1;
        texture.cutout = 0;
    }
}

// Whether a whole-texture upload has a picture in the pack; if so, the picture is level 0 and true is returned (the texels are not uploaded)
bool pack_replace_timed(Texture &texture, const OtHalRect *area, const void *pixels, int32_t pitch);

bool pack_replace(Texture &texture, const OtHalRect *area, const void *pixels, int32_t pitch) {
    const uint64_t start = OT_SDL(SDL_GetPerformanceCounter)();
    const bool replaced = pack_replace_timed(texture, area, pixels, pitch);
    g_pack_ticks += OT_SDL(SDL_GetPerformanceCounter)() - start;
    g_stats.pack_us = g_pack_ticks * 1000000u / OT_SDL(SDL_GetPerformanceFrequency)();
    return replaced;
}

bool pack_replace_timed(Texture &texture, const OtHalRect *area, const void *pixels, int32_t pitch) {
    texture.hashed = 0;
    texture.logged = 0;
    if (texture.indices != 0 || area->x != 0 || area->y != 0 || area->w != texture.width || area->h != texture.height || texture.width > 512 || texture.height > 512) {
        return false;
    }
    const int32_t bytes = texture.format == OT_HAL_PIXEL_ARGB8888 ? 4
                          : (texture.format == OT_HAL_PIXEL_ARGB1555 || texture.format == OT_HAL_PIXEL_ARGB4444 || texture.format == OT_HAL_PIXEL_RGB565) ? 2
                                                                                                                                                        : 0;
    if (bytes == 0 || pitch != texture.width * bytes) {
        return false;
    }
    if (!g_pack_scanned) {
        pack_scan();
    }
    if (g_pack_count == 0) {
        return false;
    }
    const uint64_t key = ot_hal_texture_key(texture.width, texture.height, texture.format, pixels, static_cast<size_t>(texture.width) * texture.height * bytes);
    ++g_stats.pack_hashes;
    texture.key = key;
    texture.hashed = 1;
    const PackKey *found = static_cast<const PackKey *>(bsearch(&key, g_pack_keys, g_pack_count, sizeof(PackKey), compare_key_only));
    if (found == 0) {
        return false;
    }
    char path[1100];
    const char *folder_start = 0;
    size_t length = 0;
    if (!pack_folder(found->dir, folder_start, length) || length >= 1024) {
        return false;
    }
    memcpy(path, folder_start, length);
    if (length > 0 && path[length - 1] != '\\' && path[length - 1] != '/') {
        path[length++] = '/';
    }
    for (int index = 15; index >= 0; --index) {
        path[length++] = "0123456789abcdef"[(key >> (4 * index)) & 0xfu];
    }
    memcpy(path + length, ".png", 5);
    SDL_Surface *loaded = OT_SDL(SDL_LoadPNG)(path);
    SDL_Surface *picture = loaded != 0 ? OT_SDL(SDL_ConvertSurface)(loaded, SDL_PIXELFORMAT_ARGB8888) : 0;
    OT_SDL(SDL_DestroySurface)(loaded);
    // the same shape, never smaller, not past the cap: anything else is a file made for another texture
    if (picture == 0 || picture->pixels == 0 || picture->w < texture.width || picture->h < texture.height || picture->w > kPackMaxSide || picture->h > kPackMaxSide ||
        static_cast<int64_t>(picture->w) * texture.height != static_cast<int64_t>(picture->h) * texture.width || picture->pitch < picture->w * 4) {
        OT_SDL(SDL_DestroySurface)(picture);
        ++g_stats.pack_rejected;
        return false;
    }
    const uint8_t cutout = original_cutout(texture.format, pixels, texture.width, texture.height);
    pack_release(texture, false);
    gl.glBindTexture(GL_TEXTURE_2D, texture.name);
    gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, picture->pitch / 4);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, picture->w, picture->h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, picture->pixels);
    gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    texture.real_width = picture->w;
    texture.real_height = picture->h;
    texture.replaced = 1;
    texture.cutout = cutout;
    texture.mips_dirty = 1;
    ++g_stats.pack_hits;
    ++g_stats.pack_live;
    g_stats.pack_bytes += pack_gpu_bytes(picture->w, picture->h);
    OT_SDL(SDL_DestroySurface)(picture);
    return true;
}

OtHalHandle OT_HAL_CALL video_texture_create(const OtHalTextureDesc *desc) {
    for (uint32_t index = 1; index < kMaxTextures; ++index) {
        Texture &texture = g_textures[index];
        if (texture.name != 0) {
            continue;
        }
        gl.glGenTextures(1, &texture.name);
        gl.glBindTexture(GL_TEXTURE_2D, texture.name);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, desc->width, desc->height, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
        texture.width = desc->width;
        texture.height = desc->height;
        texture.real_width = desc->width;
        texture.real_height = desc->height;
        texture.replaced = 0;
        texture.format = desc->format;
        texture.palette = desc->palette;
        texture.palette_version = 0;
        texture.mips_dirty = 1;
        texture.cutout = 0;
        if (desc->format == OT_HAL_PIXEL_INDEX8 || desc->format == OT_HAL_PIXEL_INDEX4) {
            texture.indices = static_cast<uint8_t *>(OT_SDL(SDL_malloc)(static_cast<size_t>(desc->width) * desc->height));
        }
        ++g_stats.textures;
        return index;
    }
    return OT_HAL_INVALID_HANDLE;
}

// Indexed texels -> BGRA through the palette, for the whole texture.
void upload_indexed(Texture &texture) {
    if (texture.indices == 0 || texture.palette == 0 || texture.palette >= kMaxPalettes) {
        return;
    }
    const Palette &palette = g_palettes[texture.palette];
    const uint32_t texels = static_cast<uint32_t>(texture.width) * static_cast<uint32_t>(texture.height);
    uint32_t *converted = static_cast<uint32_t *>(OT_SDL(SDL_malloc)(texels * 4u));
    if (converted == 0) {
        return;
    }
    for (uint32_t index = 0; index < texels; ++index) {
        converted[index] = palette.colors[texture.indices[index] & 0xffu];
    }
    gl.glBindTexture(GL_TEXTURE_2D, texture.name);
    gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture.width, texture.height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
        converted);
    OT_SDL(SDL_free)(converted);
    texture.palette_version = palette.version;
    texture.mips_dirty = 1;
    texture.cutout = 0;
}

void OT_HAL_CALL video_texture_update(OtHalHandle handle, const OtHalRect *area, const void *pixels, int32_t pitch) {
    if (handle == 0 || handle >= kMaxTextures || g_textures[handle].name == 0) {
        return;
    }
    Texture &texture = g_textures[handle];
    if (g_pack_dir[0] != '\0') {
        if (pack_replace(texture, area, pixels, pitch)) {
            return;
        }
        pack_release(texture, true); // other texels than the ones whose picture it was
    }
    const uint8_t *rows = static_cast<const uint8_t *>(pixels);
    if (texture.indices != 0) {
        for (int32_t y = 0; y < area->h; ++y) {
            uint8_t *out = texture.indices + (area->y + y) * texture.width + area->x;
            const uint8_t *in = rows + y * pitch;
            for (int32_t x = 0; x < area->w; ++x) {
                out[x] = texture.format == OT_HAL_PIXEL_INDEX8 ? in[x]
                                                               : static_cast<uint8_t>((in[x / 2] >> ((x & 1) * 4)) & 0xf);
            }
        }
        upload_indexed(texture);
        return;
    }
    GLenum format = GL_BGRA;
    GLenum type = GL_UNSIGNED_INT_8_8_8_8_REV;
    int32_t bytes = 4;
    switch (texture.format) {
    case OT_HAL_PIXEL_ARGB1555: type = GL_UNSIGNED_SHORT_1_5_5_5_REV; bytes = 2; break;
    case OT_HAL_PIXEL_ARGB4444: type = GL_UNSIGNED_SHORT_4_4_4_4_REV; bytes = 2; break;
    case OT_HAL_PIXEL_RGB565: format = GL_RGB; type = GL_UNSIGNED_SHORT_5_6_5; bytes = 2; break;
    default: break;
    }
    gl.glBindTexture(GL_TEXTURE_2D, texture.name);
    if (pitch == area->w * bytes) {
        gl.glTexSubImage2D(GL_TEXTURE_2D, 0, area->x, area->y, area->w, area->h, format, type, pixels);
    } else {
        for (int32_t y = 0; y < area->h; ++y) {
            gl.glTexSubImage2D(GL_TEXTURE_2D, 0, area->x, area->y + y, area->w, 1, format, type, rows + y * pitch);
        }
    }
    texture.mips_dirty = 1;
    texture.cutout = 0;
}

void OT_HAL_CALL video_texture_destroy(OtHalHandle handle) {
    if (handle == 0 || handle >= kMaxTextures || g_textures[handle].name == 0) {
        return;
    }
    pack_release(g_textures[handle], false);
    gl.glDeleteTextures(1, &g_textures[handle].name);
    if (g_textures[handle].indices != 0) {
        OT_SDL(SDL_free)(g_textures[handle].indices);
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
    ++palette.version;
}

void OT_HAL_CALL video_palette_destroy(OtHalHandle handle) {
    if (handle != 0 && handle < kMaxPalettes) {
        g_palettes[handle].used = 0;
    }
}

void OT_HAL_CALL video_begin_frame(void) {
    // a target that follows the window, or whose options changed, is remade between frames
    int32_t width = 0;
    int32_t height = 0;
    wanted_target(&width, &height);
    const int32_t samples = g_options.samples < 2 ? 0 : g_options.samples;
    const bool resolve = samples != 0 || wants_resolve(width, height);
    if (width != g_target_width || height != g_target_height || (samples == 0) != (g_samples == 0) || resolve != (g_resolve_framebuffer != 0) ||
        (g_options.ambient_occlusion != 0) != g_target_ao) {
        if (!create_target(width, height)) {
            create_target(g_mode.width, g_mode.height);
        }
        scissor_mode(0, 0, g_mode.width, g_mode.height);
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    ++g_stats.frames;
    g_ao_after_pass = false;
}

void OT_HAL_CALL video_clear(uint32_t flags, uint32_t color, float depth, const OtHalRect *area) {
    GLbitfield mask = 0;
    if (g_ao_pending && (flags & OT_HAL_CLEAR_DEPTH)) {
        ao_apply(); // the world drawn so far is about to lose its depth: its occlusion is made now
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    if (area != 0) {
        scissor_mode(area->x, area->y, area->w, area->h);
    } else {
        scissor_mode(0, 0, g_mode.width, g_mode.height);
    }
    if (flags & OT_HAL_CLEAR_COLOR) {
        gl.glClearColor(((color >> 16) & 0xff) / 255.0f, ((color >> 8) & 0xff) / 255.0f, (color & 0xff) / 255.0f,
            ((color >> 24) & 0xff) / 255.0f);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & OT_HAL_CLEAR_DEPTH) {
        gl.glClearDepth(depth);
        gl.glDepthMask(GL_TRUE);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (mask != 0) {
        gl.glClear(mask);
    }
    scissor_mode(0, 0, g_mode.width, g_mode.height);
}

void OT_HAL_CALL video_set_viewport(const OtHalRect *area) {
    // Pre-transformed vertices are already in target pixels: the viewport
    // only clips (Direct3D 7 clips TL vertices to it).
    scissor_mode(area->x, area->y, area->w, area->h);
}

void OT_HAL_CALL video_set_scissor(const OtHalRect *area) {
    if (area != 0) {
        scissor_mode(area->x, area->y, area->w, area->h);
    } else {
        scissor_mode(0, 0, g_mode.width, g_mode.height);
    }
}

// A line is one target pixel wide, which on a target larger than the mode is thinner than the mode pixel the game drew: each segment becomes a quad one mode pixel
// wide (two triangles), in g_line_quads. Returns the vertex count, 0 when the lines are drawn as they are.
uint32_t widen_lines(uint32_t topology, const OtHalVertex *vertices, uint32_t count) {
    const float scale = static_cast<float>(g_target_height) / static_cast<float>(g_mode.height);
    if ((topology != OT_HAL_LINES && topology != OT_HAL_LINE_STRIP) || scale < 1.5f || count < 2) {
        return 0;
    }
    const uint32_t segments = topology == OT_HAL_LINES ? count / 2 : count - 1;
    if (segments * 6 > g_line_capacity) {
        if (g_line_quads != 0) {
            OT_SDL(SDL_free)(g_line_quads);
        }
        g_line_capacity = segments * 6 + 60;
        g_line_quads = static_cast<OtHalVertex *>(OT_SDL(SDL_malloc)(g_line_capacity * sizeof(OtHalVertex)));
        if (g_line_quads == 0) {
            g_line_capacity = 0;
            return 0;
        }
    }
    uint32_t out = 0;
    for (uint32_t segment = 0; segment < segments; ++segment) {
        const OtHalVertex &a = vertices[topology == OT_HAL_LINES ? segment * 2 : segment];
        const OtHalVertex &b = vertices[topology == OT_HAL_LINES ? segment * 2 + 1 : segment + 1];
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        const float length = sqrtf(dx * dx + dy * dy);
        if (length > 0.0f) {
            dx /= length;
            dy /= length;
        } else {
            dx = 1.0f;
            dy = 0.0f;
        }
        const float nx = -dy * 0.5f;
        const float ny = dx * 0.5f;
        // the ends reach half a pixel further, as a line's own end pixels do
        OtHalVertex corners[4] = {a, a, b, b};
        corners[0].x = a.x - dx * 0.5f + nx;
        corners[0].y = a.y - dy * 0.5f + ny;
        corners[1].x = a.x - dx * 0.5f - nx;
        corners[1].y = a.y - dy * 0.5f - ny;
        corners[2].x = b.x + dx * 0.5f + nx;
        corners[2].y = b.y + dy * 0.5f + ny;
        corners[3].x = b.x + dx * 0.5f - nx;
        corners[3].y = b.y + dy * 0.5f - ny;
        const int order[6] = {0, 1, 2, 2, 1, 3};
        for (int k = 0; k < 6; ++k) {
            g_line_quads[out++] = corners[order[k]];
        }
    }
    return out;
}

void draw_impl(const OtHalDrawState *state, uint32_t topology, const OtHalVertex *vertices, uint32_t count) {
    if (count == 0) {
        return;
    }
    if (g_ao_ready) {
        // ambient occlusion: the world is what has a reciprocal depth other than 1; its occlusion is made before the 2D layer (HUD, menus) is drawn over it
        // A draw laid out in screen pixels (every rhw exactly 1) that tests depth against a real depth is part of the world too: the shattering glass of a level's
        // start is made of such polygons, drawn between the world's own. The 2D layer's draws test no depth, or sit at depth 0 (a sprite's).
        bool world_draw = false;
        bool any_z = false;
        for (uint32_t k = 0; k < count && !world_draw; ++k) {
            world_draw = vertices[k].rhw != 1.0f;
            any_z = any_z || vertices[k].z != 0.0f;
        }
        const bool flat_in_world = !world_draw && (state->flags & OT_HAL_DRAW_DEPTH_TEST) && any_z;
        world_draw = world_draw || flat_in_world;
        if (world_draw) {
            if (!g_ao_pending) {
                g_ao_pending = true;
                g_ao_rect[0] = g_scissor[0];
                g_ao_rect[1] = g_scissor[1];
                g_ao_rect[2] = g_scissor[0] + g_scissor[2];
                g_ao_rect[3] = g_scissor[1] + g_scissor[3];
                g_stats.ao_late += g_ao_after_pass ? 1u : 0u;
            } else {
                g_ao_rect[0] = g_scissor[0] < g_ao_rect[0] ? g_scissor[0] : g_ao_rect[0];
                g_ao_rect[1] = g_scissor[1] < g_ao_rect[1] ? g_scissor[1] : g_ao_rect[1];
                g_ao_rect[2] = g_scissor[0] + g_scissor[2] > g_ao_rect[2] ? g_scissor[0] + g_scissor[2] : g_ao_rect[2];
                g_ao_rect[3] = g_scissor[1] + g_scissor[3] > g_ao_rect[3] ? g_scissor[1] + g_scissor[3] : g_ao_rect[3];
            }
        } else if (g_ao_pending) {
            ao_apply();
        }
    }
    if (const uint32_t widened = widen_lines(topology, vertices, count)) {
        topology = OT_HAL_TRIANGLES;
        vertices = g_line_quads;
        count = widened;
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    int mode = 0;
    bool coverage = false;
    float sharp_width = 0.0f;   // sharp 2D: this draw's texture size, or 0
    float sharp_height = 0.0f;
    if (state->texture != OT_HAL_INVALID_HANDLE && state->texture < kMaxTextures &&
        g_textures[state->texture].name != 0) {
        Texture &texture = g_textures[state->texture];
        if (texture.indices != 0 && texture.palette < kMaxPalettes &&
            g_palettes[texture.palette].version != texture.palette_version) {
            upload_indexed(texture);
        }
        if (texture.replaced || (g_options.texture_pack_log && texture.hashed)) {
            // a 2D draw (every reciprocal depth exactly 1) that uses a picture from the pack: the HUD and the menus are meant to stay original, so the stats line counts these
            bool flat = true;
            for (uint32_t k = 0; k < count && flat; ++k) {
                flat = vertices[k].rhw == 1.0f;
            }
            g_stats.pack_flat_draws += (texture.replaced && flat) ? 1u : 0u;
            // texture_pack_log: how each hashed texture is used, once per upload and kind of draw, the play-time census (keys as the dump names them)
            const uint8_t bit = flat ? 1u : 2u;
            if (g_options.texture_pack_log && texture.hashed && !(texture.logged & bit)) {
                texture.logged |= bit;
                printf("video: pack use: %08x%08x %dx%d %s %s\n", static_cast<unsigned>(texture.key >> 32), static_cast<unsigned>(texture.key & 0xffffffffu), texture.width,
                       texture.height, texture.replaced ? "replaced" : "missed", flat ? "2d" : "world");
            }
        }
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, texture.name);
        const GLint filter = g_options.texture_filter == OT_HAL_SDL_FILTER_NEAREST  ? GL_NEAREST
                             : g_options.texture_filter == OT_HAL_SDL_FILTER_LINEAR ? GL_LINEAR
                             : (state->flags & OT_HAL_DRAW_FILTER)                  ? GL_LINEAR
                                                                                    : GL_NEAREST;
        const GLint wrap = (state->flags & OT_HAL_DRAW_WRAP) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        bool world = false;
        if (g_options.anisotropy > 0 || g_options.alpha_to_coverage || g_options.sharp_2d) {
            for (uint32_t k = 0; k < count && !world; ++k) {
                world = vertices[k].rhw != 1.0f;
            }
        }
        if (g_options.anisotropy > 0) {
            // A world draw (a vertex with a reciprocal depth other than 1: the 2D layer is laid out in screen pixels with every one exactly 1) that filters
            // samples the mip chain, built when the texture is first drawn after a change; everything else keeps the draw's own nearest or linear.
            ensure_samplers();
            const bool mipped = world && filter == GL_LINEAR;
            if (mipped && texture.mips_dirty) {
                const uint64_t start = OT_SDL(SDL_GetPerformanceCounter)();
                build_mips(texture);
                texture.mips_dirty = 0;
                ++g_stats.mip_builds;
                g_mip_ticks += OT_SDL(SDL_GetPerformanceCounter)() - start;
                g_stats.mip_us = g_mip_ticks * 1000000u / OT_SDL(SDL_GetPerformanceFrequency)();
            }
            gl.glBindSampler(0, g_samplers[(mipped ? 4 : filter == GL_LINEAR ? 2 : 0) + (wrap == GL_REPEAT ? 1 : 0)]);
            g_sampler_bound = true;
        } else {
            if (g_sampler_bound) {
                gl.glBindSampler(0, 0);
                g_sampler_bound = false;
            }
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        }
        // alpha-to-coverage: a cut-out drawn as a world draw with plain alpha blending (the game never uses the alpha test: a fence is blended from its alpha bit)
        // and untinted alpha becomes coverage instead, which the multisampling antialiases
        if (g_options.alpha_to_coverage && g_samples != 0 && world && (state->flags & OT_HAL_DRAW_BLEND) && state->src_factor == OT_HAL_FACTOR_SRC_ALPHA &&
            state->dst_factor == OT_HAL_FACTOR_INV_SRC_ALPHA && !(state->flags & OT_HAL_DRAW_ALPHA_TEST) && is_cutout(texture)) {
            coverage = true;
            for (uint32_t k = 0; k < count && coverage; ++k) {
                coverage = (vertices[k].color >> 24) == 0xffu;
            }
        }
        // sharp 2D: a 2D draw (every reciprocal depth exactly 1) that filters keeps whole texels, blending only the last output pixel at each texel edge
        // (a replaced texture keeps its picture's texels: with the logical size it would snap back to the original's and throw the upscale away)
        if (g_options.sharp_2d && !world && filter == GL_LINEAR && texture.width > 0 && texture.height > 0) {
            sharp_width = static_cast<float>(texture.replaced ? texture.real_width : texture.width);
            sharp_height = static_cast<float>(texture.replaced ? texture.real_height : texture.height);
        }
        mode = (state->flags & OT_HAL_DRAW_MODULATE) ? ((state->flags & OT_HAL_DRAW_MODULATE_2X) ? 3 : 2) : 1;
    }
    if (g_options.sharp_2d && (sharp_width != g_sharp_width || sharp_height != g_sharp_height)) {
        g_sharp_width = sharp_width;
        g_sharp_height = sharp_height;
        gl.glUniform2f(g_uniform_sharp, sharp_width, sharp_height);
    }
    gl.glUniform1i(g_uniform_mode, mode);
    gl.glUniform4f(g_uniform_target, static_cast<float>(g_mode.width), static_cast<float>(g_mode.height),
        (state->flags & OT_HAL_DRAW_PERSPECTIVE) ? 1.0f : 0.0f, 0.0f);
    gl.glUniform1i(g_uniform_flat, (state->flags & OT_HAL_DRAW_FLAT) ? 1 : 0);
    gl.glUniform1i(g_uniform_alpha_test, (state->flags & OT_HAL_DRAW_ALPHA_TEST) ? 1 : 0);
    gl.glUniform1f(g_uniform_alpha_ref, state->alpha_ref / 255.0f);
    if (g_options.alpha_to_coverage && g_samples != 0) {
        if (g_coverage_state != (coverage ? 1 : 0)) {
            g_coverage_state = coverage ? 1 : 0;
            gl.glUniform1i(g_uniform_coverage, g_coverage_state);
            if (coverage) {
                gl.glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            } else {
                gl.glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            }
        }
    }
    if ((state->flags & OT_HAL_DRAW_BLEND) && !coverage) {
        gl.glEnable(GL_BLEND);
        const GLenum source = gl_factor(state->src_factor);
        const GLenum destination = gl_factor(state->dst_factor);
        gl.glBlendFuncSeparate(source, destination, source, destination);
        gl.glBlendEquation(state->blend_op == OT_HAL_BLEND_OP_SUBTRACT       ? GL_FUNC_SUBTRACT
                           : state->blend_op == OT_HAL_BLEND_OP_REV_SUBTRACT ? GL_FUNC_REVERSE_SUBTRACT
                                                                             : GL_FUNC_ADD);
    } else {
        gl.glDisable(GL_BLEND);
    }
    if (state->flags & OT_HAL_DRAW_DEPTH_TEST) {
        gl.glEnable(GL_DEPTH_TEST);
        gl.glDepthFunc((state->flags & OT_HAL_DRAW_DEPTH_GREATER) ? GL_GEQUAL : GL_LEQUAL);
    } else {
        gl.glDisable(GL_DEPTH_TEST);
    }
    gl.glDepthMask((state->flags & OT_HAL_DRAW_DEPTH_WRITE) ? GL_TRUE : GL_FALSE);
    gl.glBindBuffer(GL_ARRAY_BUFFER, g_vertex_buffer);
    gl.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(OtHalVertex)), vertices, GL_STREAM_DRAW);
    gl.glDrawArrays(gl_topology(topology), 0, static_cast<GLsizei>(count));
    ++g_stats.draws;
    g_stats.vertices += count;
}

void OT_HAL_CALL video_draw(const OtHalDrawState *state, uint32_t topology, const OtHalVertex *vertices, uint32_t count) {
    if (!g_options.time_draws) {
        draw_impl(state, topology, vertices, count);
        return;
    }
    const uint64_t start = OT_SDL(SDL_GetPerformanceCounter)();
    draw_impl(state, topology, vertices, count);
    g_draw_ticks += OT_SDL(SDL_GetPerformanceCounter)() - start;
    g_stats.draw_us = g_draw_ticks * 1000000u / OT_SDL(SDL_GetPerformanceFrequency)();
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

// Reads the shown rectangle of the window's back buffer (just drawn by the present) into g_shown_pixels, top row first, opaque
void read_shown_rectangle(int left, int bottom, int shown_width, int shown_height) {
    g_shown_width = 0;
    g_shown_height = 0;
    if (shown_width <= 0 || shown_height <= 0) {
        return;
    }
    const size_t count = static_cast<size_t>(shown_width) * static_cast<size_t>(shown_height);
    if (count > g_shown_capacity) {
        if (g_shown_pixels != 0) {
            OT_SDL(SDL_free)(g_shown_pixels);
        }
        g_shown_pixels = static_cast<uint32_t *>(OT_SDL(SDL_malloc)(count * 4u));
        g_shown_capacity = g_shown_pixels != 0 ? count : 0;
    }
    if (g_shown_pixels == 0) {
        return;
    }
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl.glReadPixels(left, bottom, shown_width, shown_height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_shown_pixels);
    // bottom row first from GL: turn it over, and make every pixel opaque (the window's alpha is whatever the driver gave it)
    for (int y = 0; y < shown_height / 2; ++y) {
        uint32_t *top = g_shown_pixels + static_cast<size_t>(y) * shown_width;
        uint32_t *end = g_shown_pixels + static_cast<size_t>(shown_height - 1 - y) * shown_width;
        for (int x = 0; x < shown_width; ++x) {
            const uint32_t swap = top[x];
            top[x] = end[x];
            end[x] = swap;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        g_shown_pixels[i] |= 0xff000000u;
    }
    g_shown_width = shown_width;
    g_shown_height = shown_height;
}

void OT_HAL_CALL video_present(void) {
    SDL_Window *window = ot_hal_sdl_window_get();
    int width = 0;
    int height = 0;
    if (window == 0) {
        return;
    }
    if (!OT_SDL(SDL_GetWindowSizeInPixels)(window, &width, &height)) {
        OT_SDL(SDL_GetWindowSize)(window, &width, &height);
    }
    // Scale to the window keeping the frame's aspect ratio; black bars.
    int left = 0;
    int bottom = 0;
    int shown_width = 0;
    int shown_height = 0;
    shown_rectangle(width, height, &left, &bottom, &shown_width, &shown_height);
    ao_apply(); // a frame with no 2D layer after the world (the HUD hidden)
    // alpha-to-coverage is left on when the frame's last draw was a cut-out (a frame with no 2D layer after the world, e.g. the HUD hidden): the present's own draw
    // (downsample, sharp) would take its alpha as coverage and draw nothing, a black window
    if (g_coverage_state == 1) {
        g_coverage_state = 0;
        gl.glUniform1i(g_uniform_coverage, 0);
        gl.glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
    // likewise the sampler object of a world draw with texture detail on: its mipmapped minification would read the resolve texture (one level) as incomplete, black
    if (g_sampler_bound) {
        gl.glBindSampler(0, 0);
        g_sampler_bound = false;
    }
    GLuint source = resolved_target();
    GLuint source_texture = g_resolve_texture;
    if (g_dither_fbo != 0 && g_resolve_texture != 0) {
        // the PlayStation dither: the resolved picture into the dithered copy, which is then what the present scales
        gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_dither_fbo);
        gl.glViewport(0, 0, g_target_width, g_target_height);
        gl.glDisable(GL_SCISSOR_TEST);
        gl.glDisable(GL_BLEND);
        gl.glDisable(GL_DEPTH_TEST);
        gl.glDepthMask(GL_FALSE);
        gl.glUseProgram(g_dither_program);
        gl.glUniform2i(g_dither_size, g_target_width, g_target_height);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, g_resolve_texture);
        gl.glBindVertexArray(g_present_array);
        gl.glDrawArrays(GL_TRIANGLES, 0, 3);
        gl.glBindVertexArray(g_vertex_array);
        gl.glUseProgram(g_program);
        source = g_dither_fbo;
        source_texture = g_dither_texture;
    }
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
    gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl.glDisable(GL_SCISSOR_TEST);
    if (shown_width != width || shown_height != height) {
        gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl.glClear(GL_COLOR_BUFFER_BIT);
    }
    const float ratio_x = shown_width > 0 ? static_cast<float>(g_target_width) / static_cast<float>(shown_width) : 1.0f;
    const float ratio_y = shown_height > 0 ? static_cast<float>(g_target_height) / static_cast<float>(shown_height) : 1.0f;
    if ((ratio_x > 1.001f || ratio_y > 1.001f) && g_options.present_filter != OT_HAL_SDL_PRESENT_NEAREST && g_downsample_program != 0 && g_resolve_texture != 0 && shown_width > 0 &&
        shown_height > 0) {
        // a target larger than the picture's rectangle: a box filter over each output pixel's footprint (it takes the place of every scaling but nearest, so the
        // launcher sends smooth at Native, the only size that is as large as the window or larger: the pixel looks never meet it)
        float taps_x = static_cast<float>(ceil(ratio_x - 0.001f));
        float taps_y = static_cast<float>(ceil(ratio_y - 0.001f));
        taps_x = taps_x < 1.0f ? 1.0f : (taps_x > 6.0f ? 6.0f : taps_x);
        taps_y = taps_y < 1.0f ? 1.0f : (taps_y > 6.0f ? 6.0f : taps_y);
        gl.glViewport(left, bottom, shown_width, shown_height);
        gl.glDisable(GL_BLEND);
        gl.glDisable(GL_DEPTH_TEST);
        gl.glDepthMask(GL_FALSE);
        gl.glUseProgram(g_downsample_program);
        gl.glUniform2f(g_downsample_source, static_cast<float>(g_target_width), static_cast<float>(g_target_height));
        gl.glUniform2f(g_downsample_ratio, ratio_x, ratio_y);
        gl.glUniform2f(g_downsample_taps, taps_x, taps_y);
        set_curve(g_downsample_program, shown_width, shown_height);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, source_texture);
        gl.glBindVertexArray(g_present_array);
        gl.glDrawArrays(GL_TRIANGLES, 0, 3);
        gl.glBindVertexArray(g_vertex_array);
        gl.glUseProgram(g_program);
        gl.glViewport(0, 0, g_target_width, g_target_height);
    } else if (g_options.present_filter == OT_HAL_SDL_PRESENT_CRT && ensure_crt_program() && source_texture != 0 && shown_width > 0 && shown_height > 0) {
        const float scale_x = static_cast<float>(shown_width) / static_cast<float>(g_target_width);
        const float scale_y = static_cast<float>(shown_height) / static_cast<float>(g_target_height);
        gl.glViewport(left, bottom, shown_width, shown_height);
        gl.glDisable(GL_BLEND);
        gl.glDisable(GL_DEPTH_TEST);
        gl.glDepthMask(GL_FALSE);
        gl.glUseProgram(g_crt_program);
        gl.glUniform2f(g_crt_source, static_cast<float>(g_target_width), static_cast<float>(g_target_height));
        gl.glUniform2f(g_crt_scale, scale_x, scale_y);
        gl.glUniform1f(g_crt_mask, scale_x >= 4.0f ? 1.0f : 0.0f);
        set_curve(g_crt_program, shown_width, shown_height);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, source_texture);
        gl.glBindVertexArray(g_present_array);
        gl.glDrawArrays(GL_TRIANGLES, 0, 3);
        gl.glBindVertexArray(g_vertex_array);
        gl.glUseProgram(g_program);
        gl.glViewport(0, 0, g_target_width, g_target_height);
    } else if (g_options.present_filter == OT_HAL_SDL_PRESENT_SHARP && g_present_program != 0 && g_resolve_texture != 0 && shown_width > 0 && shown_height > 0) {
        gl.glViewport(left, bottom, shown_width, shown_height);
        gl.glDisable(GL_BLEND);
        gl.glDisable(GL_DEPTH_TEST);
        gl.glDepthMask(GL_FALSE);
        gl.glUseProgram(g_present_program);
        gl.glUniform2f(g_present_source, static_cast<float>(g_target_width), static_cast<float>(g_target_height));
        gl.glUniform2f(g_present_scale, static_cast<float>(shown_width) / static_cast<float>(g_target_width),
            static_cast<float>(shown_height) / static_cast<float>(g_target_height));
        set_curve(g_present_program, shown_width, shown_height);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, source_texture);
        gl.glBindVertexArray(g_present_array);
        gl.glDrawArrays(GL_TRIANGLES, 0, 3);
        gl.glBindVertexArray(g_vertex_array);
        gl.glUseProgram(g_program);
        gl.glViewport(0, 0, g_target_width, g_target_height);
    } else if (g_options.crt_curve != 0 && ensure_plain_program() && source_texture != 0 && shown_width > 0 && shown_height > 0) {
        // the curved screen over a linear or nearest scaling
        gl.glViewport(left, bottom, shown_width, shown_height);
        gl.glDisable(GL_BLEND);
        gl.glDisable(GL_DEPTH_TEST);
        gl.glDepthMask(GL_FALSE);
        gl.glUseProgram(g_plain_program);
        gl.glUniform2f(g_plain_source, static_cast<float>(g_target_width), static_cast<float>(g_target_height));
        gl.glUniform1f(g_plain_nearest, g_options.present_filter == OT_HAL_SDL_PRESENT_NEAREST ? 1.0f : 0.0f);
        set_curve(g_plain_program, shown_width, shown_height);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, source_texture);
        gl.glBindVertexArray(g_present_array);
        gl.glDrawArrays(GL_TRIANGLES, 0, 3);
        gl.glBindVertexArray(g_vertex_array);
        gl.glUseProgram(g_program);
        gl.glViewport(0, 0, g_target_width, g_target_height);
    } else {
        gl.glBlitFramebuffer(0, 0, g_target_width, g_target_height, left, bottom, left + shown_width, bottom + shown_height, GL_COLOR_BUFFER_BIT,
            g_options.present_filter == OT_HAL_SDL_PRESENT_NEAREST ? GL_NEAREST : GL_LINEAR);
    }
    gl.glEnable(GL_SCISSOR_TEST);
    if (g_capturing) {
        // a capture draws the present and keeps what it drew: no swap (the next real present draws the back buffer again)
        read_shown_rectangle(left, bottom, shown_width, shown_height);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
        return;
    }
    if (g_options.time_draws) {
        const uint64_t start = OT_SDL(SDL_GetPerformanceCounter)();
        OT_SDL(SDL_GL_SwapWindow)(window);
        g_swap_ticks += OT_SDL(SDL_GetPerformanceCounter)() - start;
        g_stats.swap_us = g_swap_ticks * 1000000u / OT_SDL(SDL_GetPerformanceFrequency)();
    } else {
        OT_SDL(SDL_GL_SwapWindow)(window);
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
}

int OT_HAL_CALL video_read_back(OtHalImage *image) {
    if (g_read_back == 0) {
        return 0;
    }
    ao_apply();
    const int32_t width = g_target_width;
    const int32_t height = g_target_height;
    uint32_t *flipped = g_read_back + static_cast<size_t>(width) * height;
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, resolved_target());
    gl.glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_read_back);
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, g_framebuffer);
    for (int32_t y = 0; y < height; ++y) {
        memcpy(flipped + static_cast<size_t>(y) * width, g_read_back + static_cast<size_t>(height - 1 - y) * width,
            static_cast<size_t>(width) * 4u);
    }
    image->width = width;
    image->height = height;
    image->pitch = width * 4;
    image->format = OT_HAL_PIXEL_ARGB8888;
    image->pixels = flipped;
    return 1;
}

} // namespace

// The frame as the window shows it, for a screenshot: the present's own pass (the CRT scanlines, the curved screen, the dither) run once more over the frame
// in the render target, with no swap, and the shown rectangle read back at the window's resolution. 0 when none of those looks is on (read_back is the
// picture then), when there is no window or no frame yet.
extern "C" int ot_hal_sdl_video_gl_capture_shown(OtHalImage *image) {
    if (g_read_back == 0 || (g_options.present_filter != OT_HAL_SDL_PRESENT_CRT && g_options.crt_curve == 0 && g_options.psx_dither == 0)) {
        return 0;
    }
    g_capturing = true;
    video_present();
    g_capturing = false;
    if (g_shown_pixels == 0 || g_shown_width <= 0 || g_shown_height <= 0) {
        return 0;
    }
    image->width = g_shown_width;
    image->height = g_shown_height;
    image->pitch = g_shown_width * 4;
    image->format = OT_HAL_PIXEL_ARGB8888;
    image->pixels = g_shown_pixels;
    return 1;
}

extern "C" const OtHalVideoBackend ot_hal_sdl_video_gl = {
    "sdl-gl33",
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

extern "C" void ot_hal_sdl_video_gl_hints(void) {
    OT_SDL(SDL_GL_SetAttribute)(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    OT_SDL(SDL_GL_SetAttribute)(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    OT_SDL(SDL_GL_SetAttribute)(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    OT_SDL(SDL_GL_SetAttribute)(SDL_GL_DOUBLEBUFFER, 1);
}

extern "C" void ot_hal_sdl_video_gl_options(const OtHalSdlVideoOptions *options) {
    char pack[sizeof(g_pack_dir)];
    size_t used = 0;
    for (const char *from = options->texture_pack; from != 0 && *from != '\0' && used + 1 < sizeof(pack); ++from) {
        pack[used++] = *from;
    }
    pack[used] = '\0';
    if (strcmp(pack, g_pack_dir) != 0) {
        memcpy(g_pack_dir, pack, sizeof(pack));
        g_pack_scanned = false; // a folder is read at the first upload that could use it
    }
    g_options = *options;
    g_options.texture_pack = g_pack_dir;
}

extern "C" void ot_hal_sdl_video_gl_target(int32_t *width, int32_t *height, int32_t *samples) {
    *width = g_target_width != 0 ? g_target_width : g_mode.width;
    *height = g_target_height != 0 ? g_target_height : g_mode.height;
    *samples = g_samples;
}

extern "C" int32_t ot_hal_sdl_video_gl_supersample(void) {
    return g_supersample_used;
}

extern "C" const char *ot_hal_sdl_video_error(void) {
    return g_error;
}

extern "C" const OtHalVideoStats *ot_hal_sdl_video_stats(void) {
    return &g_stats;
}
