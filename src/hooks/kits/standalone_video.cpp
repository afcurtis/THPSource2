// Milestone 7c: the video of opentony_spike.exe. The draw path of the standalone DLL (src/runtime/draw_kit.*) draws into the HAL backend the program sets
// (ot_standalone_set_video_hal); this is that backend: a recording layer that counts every call per frame and forwards it to the HAL's SDL GL backend
// (src/hal/sdl/sdl_video_gl.cpp, as opentony_video_replay.exe uses it), or to nothing.
//
// Milestone 8a: the same DLL is the program's platform. With SDL3 loaded it creates the window, starts the HAL's input (keyboard, mouse, gamepad) and audio services and
// hands them to the standalone DLL (ot_standalone_set_platform, ot_standalone_set_audio_hal); SDL decides the devices (OT_AUDIO=dummy selects its dummy audio driver).
//
//   OT_VIDEO_DIR=<dir>      record: each presented frame N writes <dir>/frame_NNNNN.otvh (the primitive stream, src/hooks/hal/video_shadow.cpp's format)
//                           and one line of counts to stdout
//   OT_VIDEO_SDL=<dir>\     render with SDL3.dll from that directory (a 640 x 480 window, hidden unless OT_VIDEO_SHOW); without it the frames are recorded, not rendered
//   OT_VIDEO_SHOW=1         show the window and follow the wall clock (60 Hz)
//   OT_PACING=lockstep|realtime   the clock: 1/60 s a frame, the same on every machine (the default), or the wall clock (the default with OT_VIDEO_SHOW)
//   OT_VIDEO_SHOTS=3,29     the frames whose picture is read back into <dir>/frame_NNNNN.ottx (default: every frame); needs OT_VIDEO_SDL and OT_VIDEO_DIR
//   OT_AUDIO=off|dummy      off: no sound device (the HAL audio service that makes no sound); dummy: SDL's dummy driver (voices advance in time, nothing is heard);
//                           otherwise SDL's default driver, which falls back to the dummy one when there is no device
//   OT_INPUT_LOG=<file>     scripted keyboard and pointer (standalone_platform.cpp); read by the standalone DLL, so it needs no SDL
//
// The player's display options (THPSource2.exe sets them, docs/LAUNCHER.md; unset, the picture is retail's 640 x 480 scaled to the window):
//   OT_ASPECT=4:3|16:9|W:H|window|stretch   the picture's shape. Wider than 4:3, the game is told its screen is that much wider than 640 (its height stays 480) and
//                           widens its own view; `window` takes the shape of the window or display; `stretch` keeps 4:3 and fills the window with it
//   OT_INTERNAL_RESOLUTION=psx|pc|window|<lines>   the render target: 240 lines of 512 PlayStation columns, the PC game's 480, the window's own pixels, or that many lines
//   OT_MSAA=2|4|8           multisampling of the render target
//   OT_SUPERSAMPLE=1|1.5|2|3|4              draw that many times larger (each side) than the window's or the 960 / 1920 line target, and shrink it to the window with a box filter;
//                           ignored for psx and pc. Lowered to what the driver and about 1 GB hold; the log line says what it got
//   OT_ALPHA_TO_COVERAGE=1  with OT_MSAA: cut-out textures (fences, leaves) get multisampled edges
//   OT_TEXTURE_FILTER=sharp|smooth          every texture nearest or bilinear, instead of what each draw asks for
//   OT_ANISOTROPY=0|trilinear|2|4|8|16      texture detail: mip maps for the 3D world's filtered textures, with anisotropic filtering at 2, 4, 8 or 16 taps (capped at the
//                           driver's limit); 0 or unset: none. The 2D layer (HUD, menus) and nearest-filtered draws never use mips
//   OT_TEXTURE_PACK=<dir>   a folder of <key>.png files (key: src/hal/texture_key_hash.h; the names OT_TEXTURE_DUMP writes): a texture the game uploads whole whose key is there is drawn
//                           from that picture, larger than the game's own. Mip maps (OT_ANISOTROPY) should be on with it. A miss or an unusable file draws the original; unset: nothing is hashed
//   OT_TEXTURE_PACK_LOG=1   with a pack: a `video: pack use: <key> <w>x<h> replaced|missed 2d|world` line the first time each hashed texture is drawn in 2D and in the world
//   OT_HUD_FILTER=sharp     with smooth textures: the 2D layer (HUD, fonts, menus) keeps whole texels, blending only the last output pixel at each texel edge; unset: bilinear
//   OT_AMBIENT_OCCLUSION=low|high   an experiment, not part of the original look: soft darkening where surfaces meet, from the depth buffer, applied to the 3D world before the 2D layer (8 or 12 taps); unset: none
//   OT_SCALING=smooth|sharp|nearest|crt    how the target is scaled to the window: bilinear (the default), whole pixels with antialiased edges, nearest, or a CRT (scanlines that follow the
//                           target's lines, a soft blur across them, a faint aperture mask from four output pixels a line; a target larger than the window is box-filtered instead)
//   OT_CRT_CURVE=1          the picture bent as on a curved tube (a barrel warp, rounded corners, black outside), with any OT_SCALING and any target size
//   OT_PSX_DITHER=1         the PlayStation's 4 x 4 dither and 5 bits a channel over the finished picture, before the scaling (meant for OT_INTERNAL_RESOLUTION=psx)
//   OT_FPS=1                a frame counter in the picture's top right corner
//   OT_SCREENSHOTS=<dir>    F8 writes the frame as a PNG there
//   OT_SCREENSHOT_AT=<n>    (tests) the n-th presented frame is saved too, as the key would
//   OT_TEXTURE_DUMP=<dir>   write the game's textures there as PNGs named by key, with an index (standalone_texture_dump.hpp); needs no window
#include "../../platform/path_separator.h"
#include "../../hal/sdl/sdl_api.h"
#include "../../hal/sdl/sdl_backends.h"
#include "../../hal/sdl/sdl_video.h"
#include "../../hal/movie.h"
#include "../../hal/video.h"

#include "standalone_png.hpp"
#include "standalone_texture_dump.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include "win32_posix.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const OtHalVideoBackend* g_inner = nullptr;   // the SDL GL backend, or null
std::string g_directory;
std::vector<std::uint32_t> g_shots;          // empty: every frame
bool g_every_shot = true;

struct FrameCounts {
    std::uint32_t draws = 0;
    std::uint32_t vertices = 0;
    std::uint32_t sprites = 0;
    std::uint32_t clears = 0;
    std::uint32_t viewports = 0;
    std::uint32_t texture_creates = 0;
    std::uint32_t texture_updates = 0;
    std::uint32_t textured_draws = 0;
} g_frame;

std::uint32_t(__cdecl* g_draw_stream)(std::uint32_t, std::uint8_t*, std::uint32_t) = nullptr;
void(__cdecl* g_draw_counters)(std::uint32_t*) = nullptr;
int(__cdecl* g_surface)(std::uint32_t, std::uint32_t*, std::uint32_t*, std::uint32_t*, unsigned char*, std::uint32_t) = nullptr;
std::uint32_t g_walk[7] = {};   // the walk's counts at the last present
OtHalHandle g_next_texture = 1;   // the handles of a recording without a renderer

// ---- the recording layer -------------------------------------------------------------------------------------------------
int OT_HAL_CALL rec_init(const OtHalVideoMode* mode) { return g_inner != nullptr ? g_inner->init(mode) : 1; }
void OT_HAL_CALL rec_shutdown() {
    if (g_inner != nullptr) g_inner->shutdown();
}
int OT_HAL_CALL rec_set_mode(const OtHalVideoMode* mode) { return g_inner != nullptr ? g_inner->set_mode(mode) : 1; }
std::uint32_t OT_HAL_CALL rec_enumerate_modes(OtHalVideoMode* modes, std::uint32_t capacity) {
    return g_inner != nullptr ? g_inner->enumerate_modes(modes, capacity) : 0;
}
OtHalHandle OT_HAL_CALL rec_texture_create(const OtHalTextureDesc* desc) {
    ++g_frame.texture_creates;
    const OtHalHandle handle = g_inner != nullptr ? g_inner->texture_create(desc) : g_next_texture++;
    texdump::on_texture_create(handle, desc);   // OT_TEXTURE_DUMP: these and the draws below; a no-op when it is unset
    return handle;
}
void OT_HAL_CALL rec_texture_update(OtHalHandle texture, const OtHalRect* area, const void* pixels, std::int32_t pitch) {
    ++g_frame.texture_updates;
    texdump::on_texture_update(texture, area, pixels, pitch);
    if (g_inner != nullptr) g_inner->texture_update(texture, area, pixels, pitch);
}
void OT_HAL_CALL rec_texture_destroy(OtHalHandle texture) {
    texdump::on_texture_destroy(texture);
    if (g_inner != nullptr) g_inner->texture_destroy(texture);
}
OtHalHandle OT_HAL_CALL rec_palette_create(std::uint32_t count) { return g_inner != nullptr ? g_inner->palette_create(count) : g_next_texture++; }
void OT_HAL_CALL rec_palette_update(OtHalHandle palette, std::uint32_t first, std::uint32_t count, const std::uint32_t* colors) {
    if (g_inner != nullptr) g_inner->palette_update(palette, first, count, colors);
}
void OT_HAL_CALL rec_palette_destroy(OtHalHandle palette) {
    if (g_inner != nullptr) g_inner->palette_destroy(palette);
}
void OT_HAL_CALL rec_begin_frame() {
    g_frame = FrameCounts{};
    if (g_inner != nullptr) g_inner->begin_frame();
}
void OT_HAL_CALL rec_clear(std::uint32_t flags, std::uint32_t color, float depth, const OtHalRect* area) {
    ++g_frame.clears;
    if (g_inner != nullptr) g_inner->clear(flags, color, depth, area);
}
void OT_HAL_CALL rec_set_viewport(const OtHalRect* area) {
    ++g_frame.viewports;
    if (g_inner != nullptr) g_inner->set_viewport(area);
}
void OT_HAL_CALL rec_set_scissor(const OtHalRect* area) {
    if (g_inner != nullptr) g_inner->set_scissor(area);
}
void OT_HAL_CALL rec_draw(const OtHalDrawState* state, std::uint32_t topology, const OtHalVertex* vertices, std::uint32_t count) {
    ++g_frame.draws;
    g_frame.vertices += count;
    g_frame.textured_draws += state->texture != OT_HAL_INVALID_HANDLE ? 1u : 0u;
    texdump::on_draw(state, vertices, count);
    if (g_inner != nullptr) g_inner->draw(state, topology, vertices, count);
}
void OT_HAL_CALL rec_draw_sprite(const OtHalDrawState* state, const OtHalRect* source, const OtHalRect* target, std::uint32_t color) {
    ++g_frame.sprites;
    texdump::on_sprite(state);
    if (g_inner != nullptr) g_inner->draw_sprite(state, source, target, color);
}
// ---- the frame counter and the screenshot key (the player's options) ------------------------------------------------------------------------------
bool g_show_fps = false;
bool g_log_fps = false;   // OT_FPS=log: the counter also goes to the log, once a half second
std::string g_screenshots;          // the directory; empty = no screenshot key
std::int32_t g_picture_width = 640;   // the mode: the size the game draws in
bool g_picture_stretch = false;       // OT_ASPECT=stretch: the picture fills the window, so it has no shape of its own
bool g_shot_key_down = false;
int(__cdecl* g_take_shot_hotkey)() = nullptr;   // standalone_platform.cpp: the screenshot hotkey the launcher's Hotkeys tab set (F8 unless it says otherwise)
std::uint32_t g_shot_at = 0;          // OT_SCREENSHOT_AT=<n>: the n-th presented frame is saved without a key (before/after pictures from a script)
std::uint32_t g_presents = 0;
std::uint32_t g_frames_counted = 0;
double g_frame_shortest = 0.0;   // OT_FPS=log: the shortest and longest time between two frames in the half second (the pacing, which the count cannot show)
double g_frame_longest = 0.0;
LARGE_INTEGER g_frame_last = {};
LARGE_INTEGER g_fps_since = {};
std::uint32_t g_fps = 0;

// a 3 x 5 digit, a bit a cell, rows top down
const std::uint8_t kDigits[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

void put_rectangle(std::vector<OtHalVertex>& out, float x, float y, float w, float h, std::uint32_t colour) {
    const OtHalVertex corners[4] = {
        {x, y, 0.0f, 1.0f, colour, 0xff000000u, 0.0f, 0.0f},
        {x + w, y, 0.0f, 1.0f, colour, 0xff000000u, 0.0f, 0.0f},
        {x + w, y + h, 0.0f, 1.0f, colour, 0xff000000u, 0.0f, 0.0f},
        {x, y + h, 0.0f, 1.0f, colour, 0xff000000u, 0.0f, 0.0f},
    };
    for (const int k : {0, 1, 2, 0, 2, 3}) {
        out.push_back(corners[k]);
    }
}

// the frames presented in the last half second, as a number in the top right corner of the picture (mode pixels; a cell is 2 x 2)
void draw_fps() {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    ++g_frames_counted;
    if (g_fps_since.QuadPart == 0) {
        g_fps_since = now;
    }
    if (g_frame_last.QuadPart != 0) {
        const double interval = static_cast<double>(now.QuadPart - g_frame_last.QuadPart) / static_cast<double>(frequency.QuadPart);
        g_frame_shortest = g_frame_shortest == 0.0 || interval < g_frame_shortest ? interval : g_frame_shortest;
        g_frame_longest = interval > g_frame_longest ? interval : g_frame_longest;
    }
    g_frame_last = now;
    const double elapsed = static_cast<double>(now.QuadPart - g_fps_since.QuadPart) / static_cast<double>(frequency.QuadPart);
    if (elapsed >= 0.5) {
        g_fps = static_cast<std::uint32_t>(static_cast<double>(g_frames_counted) / elapsed + 0.5);
        if (g_log_fps) {
            // G6: the backend's draws, vertices and the time in video_draw and in the swap, per frame over this half second
            static std::uint64_t last_draws = 0, last_vertices = 0, last_draw_us = 0, last_swap_us = 0;
            static std::uint32_t last_ao = 0, last_ao_late = 0;
            const OtHalVideoStats* stats = ot_hal_sdl_video_stats();
            const double frames = static_cast<double>(g_frames_counted);
            if (stats->draws < last_draws || stats->draw_us < last_draw_us) {
                last_draws = last_vertices = last_draw_us = last_swap_us = 0;   // the backend was restarted (a mode change): its counters began again
                last_ao = last_ao_late = 0;
            }
            std::printf("video: fps %u (frames %.2f to %.2f ms) | per frame: %.0f draws, %.0f vertices, %.3f ms in draw, %.3f ms in swap", static_cast<unsigned>(g_fps),
                        g_frame_shortest * 1000.0, g_frame_longest * 1000.0, static_cast<double>(stats->draws - last_draws) / frames,
                        static_cast<double>(stats->vertices - last_vertices) / frames, static_cast<double>(stats->draw_us - last_draw_us) / 1000.0 / frames,
                        static_cast<double>(stats->swap_us - last_swap_us) / 1000.0 / frames);
            if (stats->ao_passes != 0) {   // ambient occlusion: the passes per frame, and how many of them were followed by more world draws in the same frame
                std::printf(", %.2f occlusion passes (%.2f late)", static_cast<double>(stats->ao_passes - last_ao) / frames, static_cast<double>(stats->ao_late - last_ao_late) / frames);
            }
            std::printf("\n");
            std::fflush(stdout);
            last_draws = stats->draws;
            last_vertices = stats->vertices;
            last_draw_us = stats->draw_us;
            last_swap_us = stats->swap_us;
            last_ao = stats->ao_passes;
            last_ao_late = stats->ao_late;
        }
        g_frames_counted = 0;
        g_fps_since = now;
        g_frame_shortest = 0.0;
        g_frame_longest = 0.0;
    }
    char text[16];
    const int length = std::snprintf(text, sizeof text, "%u", static_cast<unsigned>((std::min)(g_fps, 9999u)));
    const float cell = 2.0f;
    const float width = static_cast<float>(length) * 4.0f * cell + cell;
    const float left = static_cast<float>(g_picture_width) - width - 6.0f;
    const float top = 6.0f;
    std::vector<OtHalVertex> quads;
    put_rectangle(quads, left - cell, top - cell, width + cell, 7.0f * cell, 0xff000000u);
    for (int k = 0; k < length; ++k) {
        const std::uint8_t* rows = kDigits[text[k] - '0'];
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if ((rows[row] & (4 >> column)) != 0) {
                    put_rectangle(quads, left + (static_cast<float>(k) * 4.0f + static_cast<float>(column)) * cell - 0.5f, top + static_cast<float>(row) * cell - 0.5f, cell, cell,
                                  0xffffe000u);
                }
            }
        }
    }
    const OtHalDrawState state = {OT_HAL_INVALID_HANDLE, 0u, 0, 0, 0, 0, 0u};
    g_inner->set_scissor(nullptr);
    g_inner->draw(&state, OT_HAL_TRIANGLES, quads.data(), static_cast<std::uint32_t>(quads.size()));
}

// A render target of another shape than the picture on screen (the PlayStation's 512 x 240 columns are shown at 4:3) is resampled, bilinear, to the shape the player sees:
// as many columns or rows as the target has at the most, so nothing is thrown away. A stretched picture has no shape to keep.
void correct_shape(std::vector<std::uint8_t>& rgb, std::int32_t& width, std::int32_t& height) {
    if (g_picture_stretch || g_picture_width <= 0 || width <= 0 || height <= 0) {
        return;
    }
    const std::int64_t have = static_cast<std::int64_t>(width) * 480, want = static_cast<std::int64_t>(height) * g_picture_width;
    if ((have > want ? have - want : want - have) * 100 <= have) {   // within 1%: the shape already
        return;
    }
    const std::int32_t out_width = (std::max)(width, static_cast<std::int32_t>((static_cast<std::int64_t>(height) * g_picture_width + 240) / 480));
    const std::int32_t out_height = static_cast<std::int32_t>((static_cast<std::int64_t>(out_width) * 480 + g_picture_width / 2) / g_picture_width);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(out_width) * 3u * static_cast<std::size_t>(out_height));
    for (std::int32_t y = 0; y < out_height; ++y) {
        const float source_y = (std::max)(0.0f, (static_cast<float>(y) + 0.5f) * static_cast<float>(height) / static_cast<float>(out_height) - 0.5f);
        const std::int32_t y0 = (std::min)(static_cast<std::int32_t>(source_y), height - 1), y1 = (std::min)(y0 + 1, height - 1);
        const float fy = source_y - static_cast<float>(y0);
        for (std::int32_t x = 0; x < out_width; ++x) {
            const float source_x = (std::max)(0.0f, (static_cast<float>(x) + 0.5f) * static_cast<float>(width) / static_cast<float>(out_width) - 0.5f);
            const std::int32_t x0 = (std::min)(static_cast<std::int32_t>(source_x), width - 1), x1 = (std::min)(x0 + 1, width - 1);
            const float fx = source_x - static_cast<float>(x0);
            for (int c = 0; c < 3; ++c) {
                const auto at = [&](std::int32_t sx, std::int32_t sy) { return static_cast<float>(rgb[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sx)) * 3u + static_cast<std::size_t>(c)]); };
                const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
                const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
                out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(out_width) + static_cast<std::size_t>(x)) * 3u + static_cast<std::size_t>(c)] =
                    static_cast<std::uint8_t>(top + (bottom - top) * fy + 0.5f);
            }
        }
    }
    rgb.swap(out);
    width = out_width;
    height = out_height;
}

// the picture (32-bit X8R8G8B8 rows) as a PNG: 8-bit RGB (standalone_png.cpp), in the shape it is shown in (as_shown: the window's own picture, whose shape is already
// the one it is shown in)
bool write_png(const std::string& path, const OtHalImage& image, bool as_shown = false) {
    std::vector<std::uint8_t> rows(static_cast<std::size_t>(image.width) * 3u * static_cast<std::size_t>(image.height));
    for (std::int32_t y = 0; y < image.height; ++y) {
        const std::uint8_t* in = static_cast<const std::uint8_t*>(image.pixels) + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.pitch);
        std::uint8_t* out = rows.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 3u;
        for (std::int32_t x = 0; x < image.width; ++x) {
            *out++ = in[4 * x + 2];
            *out++ = in[4 * x + 1];
            *out++ = in[4 * x];
        }
    }
    std::int32_t width = image.width, height = image.height;
    if (!as_shown) {
        correct_shape(rows, width, height);
    }
    return standalone_write_png(path, width, height, 3, rows.data());
}

// F8 (retail's F9, D3D_SaveScreenshot 0x004d0d00, is not reproduced), on the press: the frame as it was drawn, THPS2_<date>_<time>[_n].png. With the CRT scaling, the
// curved screen or the dither on it is the frame as the window shows it, at the window's resolution (the present's own pass over it); otherwise the render target's picture
void screenshot_key() {
    int count = 0;
    const bool* keys = OT_SDL(SDL_GetKeyboardState)(&count);
    const bool down = keys != nullptr && SDL_SCANCODE_F8 < count && keys[SDL_SCANCODE_F8];
    const bool by_key = g_take_shot_hotkey != nullptr ? g_take_shot_hotkey() != 0 : (down && !g_shot_key_down);
    const bool pressed = by_key || (g_shot_at != 0 && g_presents == g_shot_at);
    g_shot_key_down = down;
    if (!pressed) {
        return;
    }
    OtHalImage image{};
    const bool as_shown = ot_hal_sdl_video_gl_capture_shown(&image) != 0;
    if (!as_shown && g_inner->read_back(&image) == 0) {
        return;
    }
    CreateDirectoryA(g_screenshots.c_str(), nullptr);
    SYSTEMTIME now;
    GetLocalTime(&now);
    for (int n = 0; n < 100; ++n) {
        char name[64];
        std::snprintf(name, sizeof name, "THPS2_%04d-%02d-%02d_%02d-%02d-%02d%s%s.png", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, n != 0 ? "_" : "",
                      n != 0 ? std::to_string(n).c_str() : "");
        const std::string path = g_screenshots + OT_PATH_SEPARATOR + name;
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        const bool saved = write_png(path, image, as_shown);
        std::printf("video: screenshot %s%s\n", path.c_str(), saved ? "" : " (not written)");
        std::fflush(stdout);
        return;
    }
}

// OT_TEXTURE_PACK: one line when the numbers have not moved for 20 presents (a level load takes a few frames of uploads, then it is quiet), so run.txt holds what each load
// hashed and replaced, not a line a frame. Called at every present, played or recorded.
void pack_report() {
    static std::uint32_t frame = 0, shown_hashes = 0, shown_live = 0, shown_rejected = 0, changed_at = 0, flat_at = 0;
    static bool pending = false;
    ++frame;
    if (g_inner == nullptr) {
        return;
    }
    const OtHalVideoStats* stats = ot_hal_sdl_video_stats();
    if (stats->pack_hashes != shown_hashes || stats->pack_live != shown_live || stats->pack_rejected != shown_rejected) {
        pending = true;
        changed_at = frame;
        shown_hashes = stats->pack_hashes;   // what the last present had; the line below prints once the numbers settle
        shown_live = stats->pack_live;
        shown_rejected = stats->pack_rejected;
    } else if ((pending && frame - changed_at >= 20) || (stats->pack_flat_draws != 0 && frame - flat_at >= 300)) {
        pending = false;
        flat_at = frame;   // a 2D draw of a replaced texture is a leak into the HUD: repeat the line every 300 presents while it goes on
        std::printf("video: texture pack: present %u, %u files, %u textures hashed, %u replaced in all (%u drawn from the pack now, %.1f MB on the GPU), %u files not used, %.0f ms hashing and decoding in all, %u 2D draws of a replaced texture so far\n",
                    static_cast<unsigned>(frame), static_cast<unsigned>(stats->pack_files), static_cast<unsigned>(stats->pack_hashes), static_cast<unsigned>(stats->pack_hits),
                    static_cast<unsigned>(stats->pack_live), static_cast<double>(stats->pack_bytes) / 1048576.0, static_cast<unsigned>(stats->pack_rejected),
                    static_cast<double>(stats->pack_us) / 1000.0, static_cast<unsigned>(stats->pack_flat_draws));
        std::fflush(stdout);
    }
}

void OT_HAL_CALL rec_present() {
    texdump::on_present();
    pack_report();
    if (g_inner == nullptr) {
        return;
    }
    if (!g_screenshots.empty()) {
        ++g_presents;
        screenshot_key();   // before the counter is drawn: the picture is the game's
    }
    if (g_show_fps) {
        draw_fps();
    }
    g_inner->present();
}
int OT_HAL_CALL rec_read_back(OtHalImage* image) { return g_inner != nullptr ? g_inner->read_back(image) : 0; }

const OtHalVideoBackend kRecording = {
    "recording",  rec_init,  rec_shutdown, rec_set_mode, rec_enumerate_modes, rec_texture_create, rec_texture_update, rec_texture_destroy,
    rec_palette_create, rec_palette_update, rec_palette_destroy, rec_begin_frame, rec_clear, rec_set_viewport, rec_set_scissor, rec_draw, rec_draw_sprite, rec_present,
    rec_read_back,
};

bool write_file(const std::string& path, const void* data, std::size_t size, const void* more = nullptr, std::size_t more_size = 0) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    std::fwrite(data, 1, size, file);
    if (more_size != 0) {
        std::fwrite(more, 1, more_size, file);
    }
    std::fclose(file);
    return true;
}

// .ottx (video_trace.cpp): 32-bit X8R8G8B8, top down
bool write_image(const std::string& path, const OtHalImage& image) {
    const std::uint32_t header[12] = {0x5854544fu, 1u, static_cast<std::uint32_t>(image.width), static_cast<std::uint32_t>(image.height), 32u, 0x00ff0000u, 0x0000ff00u,
                                      0x000000ffu, 0u, 0x40u, 0u, 0u};
    std::vector<std::uint8_t> rows(static_cast<std::size_t>(image.width) * 4u * static_cast<std::size_t>(image.height));
    for (std::int32_t y = 0; y < image.height; ++y) {
        std::memcpy(rows.data() + static_cast<std::size_t>(y) * image.width * 4u, static_cast<const std::uint8_t*>(image.pixels) + y * image.pitch,
                    static_cast<std::size_t>(image.width) * 4u);
    }
    return write_file(path, header, sizeof header, rows.data(), rows.size());
}

void __cdecl on_present(std::uint32_t frame, void*) {
    char name[32];
    std::snprintf(name, sizeof name, "frame_%05u", static_cast<unsigned>(frame));
    const std::string base = g_directory + OT_PATH_SEPARATOR + name;
    if (g_draw_stream != nullptr) {
        std::vector<std::uint8_t> stream(g_draw_stream(frame, nullptr, 0));
        g_draw_stream(frame, stream.data(), static_cast<std::uint32_t>(stream.size()));
        write_file(base + ".otvh", stream.data(), stream.size());
    }
    bool shot = g_inner != nullptr && g_every_shot;
    for (const std::uint32_t wanted : g_shots) {
        shot = shot || (g_inner != nullptr && wanted == frame);
    }
    OtHalImage image{};
    const bool saved = shot && g_inner->read_back(&image) != 0 && write_image(base + ".ottx", image);
    std::uint32_t walk[7] = {};
    if (g_draw_counters != nullptr) {
        g_draw_counters(walk);
    }
    std::printf("video: frame %u draws %u (%u textured, %u vertices) clears %u viewports %u textures +%u ~%u | walk: packets %u, no texture %u, oversized %u, faded %u, "
                "unsupported %u%s\n",
                static_cast<unsigned>(frame), static_cast<unsigned>(g_frame.draws), static_cast<unsigned>(g_frame.textured_draws),
                static_cast<unsigned>(g_frame.vertices), static_cast<unsigned>(g_frame.clears), static_cast<unsigned>(g_frame.viewports),
                static_cast<unsigned>(g_frame.texture_creates), static_cast<unsigned>(g_frame.texture_updates), static_cast<unsigned>(walk[3] - g_walk[3]),
                static_cast<unsigned>(walk[4] - g_walk[4]), static_cast<unsigned>(walk[5] - g_walk[5]), static_cast<unsigned>(walk[6] - g_walk[6]),
                static_cast<unsigned>(walk[1] - g_walk[1]), saved ? " (picture saved)" : "");
    std::memcpy(g_walk, walk, sizeof walk);
    if (g_inner != nullptr) {
        static std::uint32_t mips = 0, cutouts = 0;
        const OtHalVideoStats* stats = ot_hal_sdl_video_stats();
        if (stats->mip_builds != mips) {
            std::printf("video: frame %u mip chains +%u (%u cut-outs), %u in all, %.0f ms in all\n", static_cast<unsigned>(frame), static_cast<unsigned>(stats->mip_builds - mips),
                        static_cast<unsigned>(stats->mip_cutouts - cutouts), static_cast<unsigned>(stats->mip_builds), static_cast<double>(stats->mip_us) / 1000.0);
            mips = stats->mip_builds;
            cutouts = stats->mip_cutouts;
        }
    }
    // OT_VIDEO_DUMP_SURFACE=<hex handle>: at the first present, the size of that surface of the store and its 16-bit words, row by row (a measurement mode)
    static bool dumped = false;
    if (const char* wanted = std::getenv("OT_VIDEO_DUMP_SURFACE"); wanted != nullptr && !dumped && g_surface != nullptr) {
        dumped = true;
        std::uint32_t width = 0, height = 0, bpp = 0;
        std::vector<std::uint8_t> pixels(1u << 20);
        const auto handle = static_cast<std::uint32_t>(std::strtoul(wanted, nullptr, 16));
        if (g_surface(handle, &width, &height, &bpp, pixels.data(), static_cast<std::uint32_t>(pixels.size())) != 0) {
            std::printf("surface %x: %u x %u, %u bytes a pixel\n", static_cast<unsigned>(handle), static_cast<unsigned>(width), static_cast<unsigned>(height),
                        static_cast<unsigned>(bpp));
            for (std::uint32_t y = 0; y < height && y < 32; ++y) {
                std::string row;
                for (std::uint32_t x = 0; x < width && x < 24; ++x) {
                    char cell[8];
                    std::snprintf(cell, sizeof cell, "%04x ", pixels[(y * width + x) * bpp] | (pixels[(y * width + x) * bpp + 1] << 8));
                    row += cell;
                }
                std::printf("  %s\n", row.c_str());
            }
        } else {
            std::printf("surface %x: not in the store\n", static_cast<unsigned>(handle));
        }
    }
    std::fflush(stdout);
}

// OT_ASPECT: the picture's shape as width over height, 0 for `window` (the caller knows the window), and whether it is stretched over the window
struct Aspect {
    double ratio = 4.0 / 3.0;
    bool window = false;
    bool stretch = false;
};
Aspect parse_aspect(const char* text) {
    Aspect aspect;
    if (text == nullptr || *text == '\0') {
        return aspect;
    }
    int w = 0, h = 0;
    if (std::strcmp(text, "window") == 0 || std::strcmp(text, "auto") == 0) {
        aspect.window = true;
    } else if (std::strcmp(text, "stretch") == 0) {
        aspect.stretch = true;
    } else if (std::sscanf(text, "%d:%d", &w, &h) == 2 && w > 0 && h > 0) {
        aspect.ratio = static_cast<double>(w) / static_cast<double>(h);
    }
    return aspect;
}

// The width of the game's screen for a picture of that shape, its height being 480: 640 for 4:3 and anything narrower (the game's 2D layer has no narrower layout), an
// even number up to 32:9
std::int32_t picture_width(double ratio) {
    const double clamped = (std::min)((std::max)(ratio, 4.0 / 3.0), 32.0 / 9.0);
    const auto width = static_cast<std::int32_t>(480.0 * clamped + 0.5);
    return (std::max)((width + 1) & ~1, 640);
}

}  // namespace

// Starts the video, the window, the input and the audio when OT_VIDEO_DIR, OT_VIDEO_SDL or OT_VIDEO_SHOW is set: false only when it was asked for and cannot run.
// opentony_spike_video.dll exports it; the spike loads the DLL only then.
extern "C" __declspec(dllexport) bool __cdecl standalone_video_start(HMODULE kits) {
    const char* directory = std::getenv("OT_VIDEO_DIR");
    if (directory != nullptr) {
        g_directory = directory;
        CreateDirectoryA(directory, nullptr);
    }
    if (const char* shots = std::getenv("OT_VIDEO_SHOTS")) {
        g_every_shot = false;
        for (const char* at = shots; *at != '\0';) {
            char* end = nullptr;
            g_shots.push_back(static_cast<std::uint32_t>(std::strtoul(at, &end, 10)));
            at = *end == ',' ? end + 1 : end;
        }
    }
    const auto set_video = reinterpret_cast<void(__cdecl*)(const OtHalVideoBackend*)>(GetProcAddress(kits, "ot_standalone_set_video_hal"));
    const auto set_output = reinterpret_cast<void(__cdecl*)(int, void(__cdecl*)(std::uint32_t, void*), void*)>(GetProcAddress(kits, "ot_standalone_set_draw_output"));
    g_draw_stream = reinterpret_cast<decltype(g_draw_stream)>(GetProcAddress(kits, "ot_standalone_draw_stream"));
    g_draw_counters = reinterpret_cast<decltype(g_draw_counters)>(GetProcAddress(kits, "ot_standalone_draw_counters"));
    g_surface = reinterpret_cast<decltype(g_surface)>(GetProcAddress(kits, "ot_standalone_surface"));
    g_take_shot_hotkey = reinterpret_cast<decltype(g_take_shot_hotkey)>(GetProcAddress(kits, "ot_standalone_take_screenshot_hotkey"));
    if (set_video == nullptr || set_output == nullptr || g_draw_stream == nullptr) {
        std::fprintf(stderr, "video: the standalone DLL has no ot_standalone_set_video_hal / set_draw_output / draw_stream\n");
        return false;
    }
    const auto set_platform = reinterpret_cast<void(__cdecl*)(const OtHalInputBackend*, const OtHalWindowBackend*, std::uint32_t)>(GetProcAddress(kits, "ot_standalone_set_platform"));
    const auto set_audio = reinterpret_cast<void(__cdecl*)(const OtHalAudioBackend*)>(GetProcAddress(kits, "ot_standalone_set_audio_hal"));
    if (set_platform == nullptr || set_audio == nullptr) {
        std::fprintf(stderr, "video: the standalone DLL has no ot_standalone_set_platform / set_audio_hal\n");
        return false;
    }
    OtHalVideoMode mode = {640, 480, 32u, 0u, 0u};
    const bool show = std::getenv("OT_VIDEO_SHOW") != nullptr;
    // the player's display options: the picture's shape decides the mode (the size the game draws in), the rest is how the backend renders and presents it
    Aspect aspect = parse_aspect(std::getenv("OT_ASPECT"));
    if (!aspect.window) {
        mode.width = picture_width(aspect.ratio);
    }
    const bool display_options = std::getenv("OT_ASPECT") != nullptr || std::getenv("OT_INTERNAL_RESOLUTION") != nullptr;
    const char* pacing = std::getenv("OT_PACING");
    const bool realtime = pacing != nullptr ? std::strcmp(pacing, "realtime") == 0 : show;
    const char* audio_state = "none";
    bool have_audio = false;
    const char* input_state = "scripted only";
    if (const char* sdl = std::getenv("OT_VIDEO_SDL")) {
        const char* missing = nullptr;
        if (!ot_sdl_load(sdl, &missing)) {
            std::fprintf(stderr, "video: SDL3.dll from %s: %s\n", sdl, missing != nullptr ? missing : "?");
            return false;
        }
        if (!OT_SDL(SDL_Init)(SDL_INIT_VIDEO | ot_hal_sdl_window_subsystems() | ot_hal_sdl_input_subsystems())) {
            std::fprintf(stderr, "video: SDL_Init: %s\n", OT_SDL(SDL_GetError)());
            return false;
        }
        ot_hal_sdl_video_gl_hints();
        ot_hal_sdl_window_request_opengl(1);
        // The window is a multiple of the 640 x 480 the game renders at; the present scales the frame to it (aspect kept, black bars) and the
        // mouse is mapped back. OT_WINDOW_SCALE=N (1..8) or "auto" (the largest multiple the desktop's usable area holds); unset = 1, which the
        // scenarios and gates use. A shown window can be resized or maximised.
        // (a `window` aspect needs the window's shape before the mode is known: the size asked for, else the display's)
        int asked_width = 0, asked_height = 0;
        if (const char* size = std::getenv("OT_WINDOW_SIZE"); size == nullptr || std::sscanf(size, "%dx%d", &asked_width, &asked_height) != 2 || asked_width < 320 || asked_height < 240) {
            asked_width = 0;
            asked_height = 0;
        }
        SDL_Rect desktop = {0, 0, 0, 0};       // the whole display
        SDL_Rect usable = {0, 0, 0, 0};        // without the task bar
        OT_SDL(SDL_GetDisplayBounds)(OT_SDL(SDL_GetPrimaryDisplay)(), &desktop);
        OT_SDL(SDL_GetDisplayUsableBounds)(OT_SDL(SDL_GetPrimaryDisplay)(), &usable);
        const char* window_mode_text = std::getenv("OT_WINDOW_MODE");
        const bool wants_fullscreen = show && window_mode_text != nullptr && std::strcmp(window_mode_text, "fullscreen") == 0;
        const bool wants_borderless = show && window_mode_text != nullptr && std::strcmp(window_mode_text, "borderless") == 0;
        if (aspect.window) {
            if (show && asked_width > 0) {
                aspect.ratio = static_cast<double>(asked_width) / asked_height;
            } else if (wants_fullscreen && desktop.w > 0 && desktop.h > 0) {
                aspect.ratio = static_cast<double>(desktop.w) / desktop.h;
            } else if (wants_borderless && usable.w > 0 && usable.h > 0) {
                aspect.ratio = static_cast<double>(usable.w) / usable.h;
            } else if (show && desktop.w > 0 && desktop.h > 0) {
                aspect.ratio = static_cast<double>(desktop.w) / desktop.h;   // an automatic window takes the display's shape
            }
            mode.width = picture_width(aspect.ratio);
        }
        std::int32_t scale = 1;
        if (const char* wanted = std::getenv("OT_WINDOW_SCALE"); wanted != nullptr && show) {
            if (std::strcmp(wanted, "auto") == 0) {
                SDL_Rect area = {0, 0, 0, 0};
                if (OT_SDL(SDL_GetDisplayUsableBounds)(OT_SDL(SDL_GetPrimaryDisplay)(), &area)) {
                    scale = (std::min)(area.w / mode.width, (area.h - 60) / mode.height);   // 60: the title bar and borders
                }
            } else {
                scale = static_cast<std::int32_t>(std::strtol(wanted, nullptr, 10));
            }
            scale = (std::min)((std::max)(scale, 1), 8);
        }
        OtHalWindowDesc window = {"THPSource2", mode.width * scale, mode.height * scale, show ? OT_HAL_WINDOW_RESIZABLE : OT_HAL_WINDOW_HIDDEN};
        // with the display options an automatic window need not be a whole multiple of the mode (the target can follow it): the largest of the picture's shape the
        // desktop holds under a title bar
        if (show && display_options && std::getenv("OT_WINDOW_SCALE") != nullptr && std::strcmp(std::getenv("OT_WINDOW_SCALE"), "auto") == 0 && usable.w > 0 && usable.h > 60) {
            const double shape = aspect.stretch ? static_cast<double>(usable.w) / (usable.h - 60) : static_cast<double>(mode.width) / mode.height;
            std::int32_t height = usable.h - 60;
            auto width = static_cast<std::int32_t>(height * shape + 0.5);
            if (width > usable.w) {
                width = usable.w;
                height = static_cast<std::int32_t>(width / shape + 0.5);
            }
            window.width = width;
            window.height = height;
        }
        // The launcher's display settings (THPSource2.exe, docs/LAUNCHER.md): OT_WINDOW_MODE windowed | borderless | fullscreen, OT_WINDOW_SIZE WxH
        // (a window of that size; for fullscreen, the display mode closest to it, else the desktop's size), OT_WINDOW_TITLE. A hidden window ignores them.
        std::string title = "THPSource2";
        if (show) {
            if (const char* wanted = std::getenv("OT_WINDOW_TITLE"); wanted != nullptr && *wanted != '\0') {
                title = wanted;
            }
            window.title = title.c_str();
            const int width = asked_width, height = asked_height;
            if (width > 0) {
                window.width = width;
                window.height = height;
            }
            const char* window_mode = std::getenv("OT_WINDOW_MODE");
            if (window_mode != nullptr && std::strcmp(window_mode, "fullscreen") == 0) {
                window.flags = OT_HAL_WINDOW_FULLSCREEN | (width > 0 ? OT_HAL_WINDOW_EXCLUSIVE : 0u);
            } else if (window_mode != nullptr && std::strcmp(window_mode, "borderless") == 0) {
                window.flags = OT_HAL_WINDOW_BORDERLESS;
                if (width == 0) {   // a borderless window without a size covers the display
                    SDL_Rect bounds = {0, 0, 0, 0};
                    if (OT_SDL(SDL_GetDisplayUsableBounds)(OT_SDL(SDL_GetPrimaryDisplay)(), &bounds) && bounds.w > 0) {
                        window.width = bounds.w;
                        window.height = bounds.h;
                    }
                }
            }
        }
        if (show) {
            std::printf("video: window \"%s\" %d x %d%s%s%s\n", window.title, window.width, window.height,
                        (window.flags & OT_HAL_WINDOW_FULLSCREEN) != 0 ? ", fullscreen" : "", (window.flags & OT_HAL_WINDOW_EXCLUSIVE) != 0 ? " (exclusive)" : "",
                        (window.flags & OT_HAL_WINDOW_BORDERLESS) != 0 ? ", borderless" : "");
            // the desktop's refresh rate, for reading a frame-rate report: a rate that does not divide it judders with vertical sync on (docs/LAUNCHER.md, "Frame rate")
            const SDL_DisplayMode* mode = OT_SDL(SDL_GetDesktopDisplayMode)(OT_SDL(SDL_GetPrimaryDisplay)());
            const char* vsync_text = std::getenv("OT_VSYNC");
            std::printf("video: display %.2f Hz, vsync %s\n", mode != nullptr ? static_cast<double>(mode->refresh_rate) : 0.0,
                        vsync_text == nullptr || *vsync_text == '\0' ? "the driver's" : std::strcmp(vsync_text, "0") == 0 ? "off" : "on");
        }
        if (!ot_hal_sdl_window.create(&window)) {
            std::fprintf(stderr, "video: window: %s\n", OT_SDL(SDL_GetError)());
            return false;
        }
        g_inner = &ot_hal_sdl_video_gl;
        // how the backend renders the mode and presents it
        OtHalSdlVideoOptions options = {};
        options.stretch = aspect.stretch ? 1 : 0;
        if (const char* detail = std::getenv("OT_ANISOTROPY")) {
            const long taps = std::strcmp(detail, "trilinear") == 0 ? 1 : std::strtol(detail, nullptr, 10);
            options.anisotropy = taps >= 16 ? 16 : taps >= 8 ? 8 : taps >= 4 ? 4 : taps >= 2 ? 2 : taps == 1 ? 1 : 0;
        }
        if (const char* internal = std::getenv("OT_INTERNAL_RESOLUTION"); internal != nullptr && *internal != '\0') {
            if (std::strcmp(internal, "window") == 0) {
                options.follow_window = show ? 1 : 0;
            } else if (std::strcmp(internal, "psx") == 0) {
                options.target_height = 240;
                options.target_width = ((512 * mode.width / 640) + 1) & ~1;   // the PlayStation's 512 columns across 4:3, more across a wider picture
            } else if (const long lines = std::strtol(internal, nullptr, 10); lines >= 120 && lines <= 4320) {
                options.target_height = static_cast<std::int32_t>(lines);
                options.target_width = (static_cast<std::int32_t>(lines * mode.width / mode.height) + 1) & ~1;
            }
        }
        if (const char* msaa = std::getenv("OT_MSAA")) {
            const long samples = std::strtol(msaa, nullptr, 10);
            options.samples = samples >= 8 ? 8 : samples >= 4 ? 4 : samples >= 2 ? 2 : 0;
        }
        if (const char* super = std::getenv("OT_SUPERSAMPLE"); super != nullptr && *super != '\0') {
            const char* internal_size = std::getenv("OT_INTERNAL_RESOLUTION");
            const bool has_size = internal_size != nullptr && std::strcmp(internal_size, "psx") != 0 && std::strcmp(internal_size, "pc") != 0 && *internal_size != '\0';
            const double factor = std::strtod(super, nullptr);
            options.supersample = has_size && factor > 1.0 ? static_cast<std::int32_t>(factor >= 4.0 ? 400.0 : factor * 100.0 + 0.5) : 0;
        }
        if (const char* coverage = std::getenv("OT_ALPHA_TO_COVERAGE")) {
            options.alpha_to_coverage = std::atoi(coverage) != 0 ? 1 : 0;
        }
        if (const char* filter = std::getenv("OT_TEXTURE_FILTER")) {
            options.texture_filter = std::strcmp(filter, "sharp") == 0 ? OT_HAL_SDL_FILTER_NEAREST : std::strcmp(filter, "smooth") == 0 ? OT_HAL_SDL_FILTER_LINEAR : OT_HAL_SDL_FILTER_AS_DRAWN;
        }
        if (const char* occlusion = std::getenv("OT_AMBIENT_OCCLUSION")) {
            options.ambient_occlusion = std::strcmp(occlusion, "high") == 0 ? 2 : std::strcmp(occlusion, "low") == 0 ? 1 : 0;
        }
        if (const char* hud = std::getenv("OT_HUD_FILTER")) {
            options.sharp_2d = std::strcmp(hud, "sharp") == 0 ? 1 : 0;
        }
        if (const char* pack = std::getenv("OT_TEXTURE_PACK"); pack != nullptr && *pack != '\0') {
            // as UTF-8, which SDL_LoadPNG takes a file name in (the narrow getenv gives the ANSI code page's bytes: a folder named outside ASCII loaded nothing)
            static std::string pack_utf8;
            wchar_t wide[1024] = {};   // two folders can be named, joined by '|'
            const DWORD length = GetEnvironmentVariableW(L"OT_TEXTURE_PACK", wide, 1024);
            const int bytes = length != 0 && length < 1024 ? WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), nullptr, 0, nullptr, nullptr) : 0;
            if (bytes > 0) {
                pack_utf8.assign(static_cast<std::size_t>(bytes), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), pack_utf8.data(), bytes, nullptr, nullptr);
            } else {
                pack_utf8 = pack;
            }
            options.texture_pack = pack_utf8.c_str();   // copied by the backend
            options.texture_pack_log = std::getenv("OT_TEXTURE_PACK_LOG") != nullptr && std::atoi(std::getenv("OT_TEXTURE_PACK_LOG")) != 0 ? 1 : 0;
        }
        if (const char* scaling = std::getenv("OT_SCALING")) {
            options.present_filter = std::strcmp(scaling, "sharp") == 0 ? OT_HAL_SDL_PRESENT_SHARP : std::strcmp(scaling, "nearest") == 0 ? OT_HAL_SDL_PRESENT_NEAREST : std::strcmp(scaling, "crt") == 0 ? OT_HAL_SDL_PRESENT_CRT : OT_HAL_SDL_PRESENT_LINEAR;
        }
        if (const char* curve = std::getenv("OT_CRT_CURVE")) {
            options.crt_curve = std::atoi(curve) != 0 ? 1 : 0;
        }
        if (const char* dither = std::getenv("OT_PSX_DITHER")) {
            options.psx_dither = std::atoi(dither) != 0 ? 1 : 0;
        }
        g_show_fps = std::getenv("OT_FPS") != nullptr && std::strcmp(std::getenv("OT_FPS"), "0") != 0;
        g_log_fps = g_show_fps;   // the launcher's FPS counter sets OT_FPS=1: its run.txt then carries the half-second lines too
        options.time_draws = g_log_fps ? 1 : 0;   // OT_FPS=log also reports draws a frame and the time spent in the draw calls (G6)
        ot_hal_sdl_video_gl_options(&options);
        if (const char* shots = std::getenv("OT_SCREENSHOTS"); shots != nullptr && *shots != '\0') {
            g_screenshots = shots;
            if (const char* at = std::getenv("OT_SCREENSHOT_AT")) {
                g_shot_at = static_cast<std::uint32_t>(std::strtoul(at, nullptr, 10));
            }
            while (!g_screenshots.empty() && (g_screenshots.back() == '\\' || g_screenshots.back() == '/')) {
                g_screenshots.pop_back();
            }
        }
        // the input devices and the sound, on SDL
        if (ot_hal_sdl_input.init()) {
            input_state = "SDL";
        }
        const char* audio = std::getenv("OT_AUDIO");
        if (audio == nullptr || std::strcmp(audio, "off") != 0) {
            if (audio != nullptr && std::strcmp(audio, "dummy") == 0) {
                OT_SDL(SDL_SetHint)(SDL_HINT_AUDIO_DRIVER, "dummy");   // Wine does not pass SDL_AUDIO_DRIVER on to a Windows process
            }
            if (ot_hal_sdl_audio.init()) {
                set_audio(&ot_hal_sdl_audio);
                audio_state = ot_hal_sdl_audio_driver();
                have_audio = true;
            }
        }
        // The Bink seams of standalone_movie.cpp: the music (Music\LTIX*.DAT) and the movies' sound stream through the audio device (without it the level's music opened no
        // stream and was silent), and the movies' picture is drawn through the recording video layer (without it the intro movies were black, with their sound).
        if (const auto set_movie_hal = reinterpret_cast<void(__cdecl*)(const OtHalMovieBackend*, const OtHalAudioBackend*)>(GetProcAddress(kits, "ot_standalone_set_movie_hal"))) {
            set_movie_hal(ot_hal_movie_on_video(&kRecording), have_audio ? &ot_hal_sdl_audio : nullptr);
        }
        set_platform(input_state[0] == 'S' ? &ot_hal_sdl_input : nullptr, &ot_hal_sdl_window, realtime ? 1u : 0u);
    } else {
        set_platform(nullptr, nullptr, realtime ? 1u : 0u);
    }
    // the game is told the display it has before it boots: its screen words, the width its 2D layer keeps its shape for, the pointer's mapping
    g_picture_width = mode.width;
    g_picture_stretch = aspect.stretch;
    if (const auto set_display = reinterpret_cast<void(__cdecl*)(std::int32_t, std::int32_t, std::int32_t, std::int32_t)>(GetProcAddress(kits, "ot_standalone_set_display"))) {
        if (mode.width != 640 || aspect.stretch) {   // the 2D layer (HUD, menus) always keeps its 4:3 shape (a stretched one, the old OT_HUD=stretch, crashed the game)
            set_display(mode.width, mode.height, 640, aspect.stretch ? 1 : 0);
        }
    }
    if (!kRecording.init(&mode)) {
        std::fprintf(stderr, "video: init: %s\n", ot_hal_sdl_video_error());
        return false;
    }
    if (g_inner != nullptr && (display_options || std::getenv("OT_MSAA") != nullptr || std::getenv("OT_SCALING") != nullptr || std::getenv("OT_TEXTURE_FILTER") != nullptr || std::getenv("OT_SUPERSAMPLE") != nullptr)) {
        std::int32_t target_width = 0, target_height = 0, samples = 0;
        ot_hal_sdl_video_gl_target(&target_width, &target_height, &samples);
        std::printf("video: picture %d x %d%s, rendered at %d x %d%s, %d samples\n", mode.width, mode.height, aspect.stretch ? " (stretched over the window)" : "", target_width,
                    target_height, std::getenv("OT_INTERNAL_RESOLUTION") != nullptr && std::strcmp(std::getenv("OT_INTERNAL_RESOLUTION"), "window") == 0 ? " (the window's)" : "",
                    samples);
        if (const std::int32_t used = ot_hal_sdl_video_gl_supersample(); used > 100 || (std::getenv("OT_SUPERSAMPLE") != nullptr && std::atof(std::getenv("OT_SUPERSAMPLE")) > 1.0)) {
            std::printf("video: supersampling %d%% of %s\n", used, std::getenv("OT_SUPERSAMPLE") != nullptr ? std::getenv("OT_SUPERSAMPLE") : "1");
        }
    }
    if (g_inner != nullptr && std::getenv("OT_ANISOTROPY") != nullptr) {
        std::printf("video: texture detail %s\n", std::getenv("OT_ANISOTROPY"));
    }
    if (g_inner != nullptr && std::getenv("OT_TEXTURE_PACK") != nullptr && *std::getenv("OT_TEXTURE_PACK") != '\0') {
        std::printf("video: texture pack %s\n", std::getenv("OT_TEXTURE_PACK"));
    }
    // OT_VSYNC 0 | 1 (the launcher's VSync setting): the swap interval of the GL context init just made current; unset leaves the driver's default
    if (const char* vsync = std::getenv("OT_VSYNC"); vsync != nullptr && *vsync != '\0' && g_inner != nullptr && show) {
        if (!OT_SDL(SDL_GL_SetSwapInterval)(std::atoi(vsync) != 0 ? 1 : 0)) {
            std::fprintf(stderr, "video: OT_VSYNC=%s: %s\n", vsync, OT_SDL(SDL_GetError)());
        }
    }
    texdump::start(kits);
    set_video(&kRecording);
    set_output(directory != nullptr ? 1 : 0, directory != nullptr ? &on_present : nullptr, nullptr);
    std::printf("video: %s, frames to %s; window %s, input %s, audio %s, clock %s\n", g_inner != nullptr ? "SDL GL (recorded)" : "recorded only (no renderer)",
                directory != nullptr ? directory : "(not kept)", g_inner != nullptr ? (show ? "shown" : "hidden") : "none", input_state, audio_state,
                realtime ? "realtime" : "lockstep");
    std::fflush(stdout);
    return true;
}
