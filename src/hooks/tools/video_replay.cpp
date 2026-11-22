// opentony_video_replay: the image oracle's HAL half.
//
//   opentony_video_replay <frame.otvs> <textures dir\> <out.ottx> <SDL3 dir\> [width height]
//
// Replays a retail Direct3D 7 frame recorded with OPENTONY_VIDEO_TRACE
// (src/hooks/hal/video_trace.cpp) through the HAL video service on SDL3
// (src/hal/sdl/sdl_video_gl.cpp) and writes what the backend rendered as an
// .ottx image, for `tony hal video compare` to measure against the retail
// frame. Prints one line of statistics.

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../hal/oracle/d3d7_stream.h"
#include "../../hal/sdl/sdl_api.h"
#include "../../hal/sdl/sdl_backends.h"
#include "../../hal/sdl/sdl_video.h"
#include "../../hal/sdl/sdl_video_gpu.h"

namespace {

unsigned char *read_file(const char *path, DWORD *size) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    *size = GetFileSize(file, 0);
    unsigned char *data = static_cast<unsigned char *>(HeapAlloc(GetProcessHeap(), 0, *size + 1u));
    DWORD read = 0;
    if (data != 0 && (!ReadFile(file, data, *size, &read, 0) || read != *size)) {
        HeapFree(GetProcessHeap(), 0, data);
        data = 0;
    }
    CloseHandle(file);
    return data;
}

struct Loader {
    const char *directory;
    unsigned char *data; // the last texture read (kept until the next load)
};

int load_texture(void *user, uint64_t hash, uint32_t, OtHalTextureDesc *desc, const void **pixels, int32_t *pitch) {
    Loader *loader = static_cast<Loader *>(user);
    char path[MAX_PATH];
    wsprintfA(path, "%s%08X%08X.ottx", loader->directory, static_cast<unsigned>(hash >> 32),
        static_cast<unsigned>(hash));
    if (loader->data != 0) {
        HeapFree(GetProcessHeap(), 0, loader->data);
        loader->data = 0;
    }
    DWORD size = 0;
    loader->data = read_file(path, &size);
    const uint8_t *texels = 0;
    if (loader->data == 0 || !ot_ottx_parse(loader->data, size, desc, &texels, pitch)) {
        return 0;
    }
    *pixels = texels;
    return 1;
}

bool write_image(const char *path, const OtHalImage &image) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD header[12] = {0x5854544fu /* OTTX */, 1u, static_cast<DWORD>(image.width), static_cast<DWORD>(image.height),
        32u, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0u, 0x40u /* DDPF_RGB */, 0u, 0u};
    DWORD written = 0;
    WriteFile(file, header, sizeof(header), &written, 0);
    for (int32_t y = 0; y < image.height; ++y) {
        WriteFile(file, static_cast<const unsigned char *>(image.pixels) + y * image.pitch,
            static_cast<DWORD>(image.width) * 4u, &written, 0);
    }
    CloseHandle(file);
    return true;
}

int fail(const char *what, const char *detail) {
    printf("FAIL %s: %s\n", what, detail != 0 ? detail : "");
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    // --backend gpu|gl (default gl) may appear anywhere; strip it before
    // positional parsing so existing callers (width/height-less) still work.
    bool gpu = false;
    char *positional[8];
    int positional_count = 0;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--backend") == 0 && index + 1 < argc) {
            gpu = strcmp(argv[index + 1], "gpu") == 0;
            ++index;
        } else if (positional_count < 8) {
            positional[positional_count++] = argv[index];
        }
    }
    if (positional_count < 4) {
        printf("usage: opentony_video_replay <frame.otvs> <textures dir\\> <out.ottx> <SDL3 dir\\> "
               "[width height] [--backend gpu|gl]\n");
        return 2;
    }
    const int32_t width = positional_count >= 6 ? atoi(positional[4]) : 640;
    const int32_t height = positional_count >= 6 ? atoi(positional[5]) : 480;
    DWORD size = 0;
    unsigned char *stream = read_file(positional[0], &size);
    if (stream == 0) {
        return fail("read", positional[0]);
    }
    const char *missing = 0;
    if (!ot_sdl_load(positional[3], &missing)) {
        return fail("SDL3.dll", missing);
    }
    if (!OT_SDL(SDL_Init)(SDL_INIT_VIDEO)) {
        return fail("SDL_Init", OT_SDL(SDL_GetError)());
    }
    if (gpu) {
        if (!ot_hal_sdl_video_gpu_probe()) {
            return fail("SDL_GPU probe", ot_hal_sdl_video_gpu_error());
        }
        ot_hal_sdl_window_request_vulkan(1);
    } else {
        ot_hal_sdl_video_gl_hints();
        ot_hal_sdl_window_request_opengl(1);
    }
    OtHalWindowDesc window = {"OpenTony video replay", width, height, OT_HAL_WINDOW_HIDDEN};
    if (!ot_hal_sdl_window.create(&window)) {
        return fail("window", OT_SDL(SDL_GetError)());
    }
    OtHalVideoMode mode = {width, height, 32u, 0u, 0u};
    const OtHalVideoBackend &video = gpu ? ot_hal_sdl_video_gpu : ot_hal_sdl_video_gl;
    if (!video.init(&mode)) {
        return fail("video init", gpu ? ot_hal_sdl_video_gpu_error() : ot_hal_sdl_video_error());
    }
    video.begin_frame();
    video.clear(OT_HAL_CLEAR_COLOR | OT_HAL_CLEAR_DEPTH, 0xff000000u, 1.0f, 0);
    Loader loader = {positional[1], 0};
    OtD3d7ReplayStats stats;
    if (!ot_d3d7_replay(stream, size, &video, load_texture, &loader, &stats)) {
        return fail("replay", "malformed stream");
    }
    OtHalImage image;
    if (!video.read_back(&image) || !write_image(positional[2], image)) {
        return fail("read back", positional[2]);
    }
    printf("replay records=%u draws=%u vertices=%u textures=%u missing_textures=%u unsupported=%u\n",
        stats.records, stats.draws, stats.vertices, stats.textures_loaded, stats.textures_missing, stats.unsupported);
    video.shutdown();
    ot_hal_sdl_window.destroy();
    OT_SDL(SDL_Quit)();
    return 0;
}
