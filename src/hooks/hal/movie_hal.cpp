#include "movie_hal.h"

#include "hal_adapters.h"
#include "video_hal.h"
#include "video_surface.h"
#include "../hook_runtime.h"
#include "../../hal/movie.h"

#include <string.h>

// movie=sdl adapters (movie_hal.h). Retail layouts: DirectDraw 7 and
// Direct3D 7 vtables in tony/hal/com.py order, DDSURFACEDESC2.

namespace {

typedef int(__cdecl *StartBinkMovieCall)(const char *, int, int); // returns a byte
typedef int(__cdecl *IntCall)(void);
typedef void(__cdecl *VoidCall)(void);
typedef unsigned(__cdecl *FirstKeyDownCall)(unsigned);
typedef int(__stdcall *BinkCall)(void *);
typedef HRESULT(__stdcall *SetTextureCall)(void *, DWORD, void *);
typedef HRESULT(__stdcall *SurfaceLockCall)(void *, RECT *, void *, DWORD, HANDLE);
typedef HRESULT(__stdcall *SurfaceUnlockCall)(void *, RECT *);

enum DeviceSlot { kDeviceSetTexture = 35 };
enum SurfaceSlot { kSurfaceLock = 25, kSurfaceUnlock = 32 };
const DWORD kLockReadOnly = 0x10; // DDLOCK_READONLY
const DWORD kLockWait = 0x01;     // DDLOCK_WAIT

struct PixelFormat { // DDPIXELFORMAT
    DWORD size, flags, fourcc, bits, red, green, blue, alpha;
};

struct SurfaceDesc { // DDSURFACEDESC2
    DWORD size, flags, height, width;
    LONG pitch;
    DWORD back_buffers, mipmaps, alpha_depth, reserved;
    void *bits;
    DWORD color_keys[8];
    PixelFormat format;
    DWORD caps[4];
    DWORD texture_stage;
};

template <typename T>
T &global(int symbol) {
    return *reinterpret_cast<T *>(ot_hal_data(symbol));
}

void **vtable_of(void *object) {
    return *reinterpret_cast<void ***>(object);
}

const OtHalMovieBackend *g_movie = 0;
BinkCall g_bink_service = 0;
BinkCall g_bink_wait = 0;
DWORD g_movies = 0;
DWORD g_frames = 0;
DWORD g_shown = 0;
DWORD g_quad_movies = 0; // D3DTextureMode 0: retail's quad and flip
DWORD g_trace_first = 0;
DWORD g_trace_count = 0;
DWORD g_trace_step = 1;
DWORD g_traced = 0;
char g_trace_directory[MAX_PATH];

uint32_t hal_format(const PixelFormat &format) {
    if (format.bits == 16 && format.red == 0xf800u && format.green == 0x07e0u && format.blue == 0x001fu) {
        return OT_HAL_PIXEL_RGB565;
    }
    if (format.bits == 16 && format.red == 0x7c00u && format.green == 0x03e0u && format.blue == 0x001fu) {
        return OT_HAL_PIXEL_ARGB1555;
    }
    if (format.bits == 32 && format.red == 0x00ff0000u) {
        return OT_HAL_PIXEL_ARGB8888;
    }
    return 0;
}

void trace_frame() {
    if (g_trace_count == 0 || g_frames < g_trace_first || (g_frames - g_trace_first) % g_trace_step != 0 ||
        (g_frames - g_trace_first) / g_trace_step >= g_trace_count) {
        return;
    }
    char path[MAX_PATH];
    wsprintfA(path, "%smovie_%05u.hal.ottx", g_trace_directory, static_cast<unsigned>(g_frames));
    g_traced += static_cast<DWORD>(ot_video_hal_save_frame(path));
}

// Bink has just decoded into the primary (the stand-in): what retail would
// have put on the screen is that whole surface, the movie in its centre.
// Copy it into the movie frame target and present it.
void show_primary(bool *opened) {
    void *primary = global<void *>(OT_HAL_SYM_D3DPrimarySurface);
    if (primary == 0 || g_movie == 0) {
        return;
    }
    SurfaceDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    // This Lock must see what Bink itself just decoded into the primary, not
    // the video backend's current frame (video_surface.cpp's screenshot
    // refresh, which this same Lock would otherwise trigger).
    ot_ddraw_primary_suppress_refresh(1);
    const HRESULT locked_primary = reinterpret_cast<SurfaceLockCall>(vtable_of(primary)[kSurfaceLock])(
        primary, 0, &desc, kLockReadOnly | kLockWait, 0);
    ot_ddraw_primary_suppress_refresh(0);
    if (locked_primary != 0) {
        return;
    }
    const uint32_t format = hal_format(desc.format);
    const int32_t width = static_cast<int32_t>(desc.width);
    const int32_t height = static_cast<int32_t>(desc.height);
    OtHalMovieFrame frame;
    if (!*opened && format != 0) {
        *opened = g_movie->open(width, height, format, OT_HAL_MOVIE_FULLSCREEN) != 0;
    }
    const bool locked = *opened && g_movie->lock_frame(&frame) && frame.width == width && frame.height == height &&
                        frame.format == format;
    if (locked) {
        const int32_t row = width * static_cast<int32_t>(desc.format.bits / 8u);
        for (int32_t y = 0; y < height; ++y) {
            memcpy(static_cast<unsigned char *>(frame.pixels) + y * frame.pitch,
                static_cast<const unsigned char *>(desc.bits) + y * desc.pitch, static_cast<size_t>(row));
        }
        g_movie->unlock_frame();
    }
    reinterpret_cast<SurfaceUnlockCall>(vtable_of(primary)[kSurfaceUnlock])(primary, 0);
    if (locked) {
        const OtHalRect target = {0, 0, width, height};
        g_movie->present(&target);
        ++g_shown;
        trace_frame();
    }
}

// Retail: open the movie, then its own message loop until the movie ends, a
// key goes down (WM_KEYDOWN), WM_QUIT, or a key or pad button is pressed
// after everything held at the start was released. Unchanged here except that
// a frame Bink decoded into the primary is shown through the movie service.
int __cdecl hal_PCMovie_PlayStartupMedia(const char *name) {
    if ((ot_hal_function<StartBinkMovieCall>(OT_HAL_SYM_StartBinkMovie)(name, 0, 1) & 0xff) == 0) {
        return 0;
    }
    if (ot_hal_function<IntCall>(OT_HAL_SYM_CreateBinkSurface)() == 0) {
        ot_hal_function<VoidCall>(OT_HAL_SYM_CloseBinkMovie)();
        return 0;
    }
    ot_hal_function<VoidCall>(OT_HAL_SYM_Game_FrameTick)();
    ++g_movies;
    const OtHalVideoBackend *video = ot_video_hal_backend();
    g_movie = video != 0 ? ot_hal_movie_on_video(video) : 0;
    bool playing = true;
    bool held = true; // keys or buttons still down from before the movie
    const bool texture_mode = global<DWORD>(OT_HAL_SYM_D3DTextureMode) != 0;
    // Texture mode: movies of 200 lines or more go to the primary; smaller
    // ones to a texture nothing draws here (as retail).
    const bool to_primary = texture_mode && global<uint8_t>(OT_HAL_SYM_BinkMovieInTexture) == 0;
    bool opened = false;
    if (!texture_mode) {
        ot_hal_function<VoidCall>(OT_HAL_SYM_SetupFullScreenMovie)();
        ++g_quad_movies;
    }
    MSG message;
    do {
        if (!PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
            void *bink = global<void *>(OT_HAL_SYM_BinkMovie);
            g_bink_service(bink);
            if (g_bink_wait(bink) == 0) {
                if (ot_hal_function<IntCall>(OT_HAL_SYM_NextBinkFrame)() == 0) {
                    playing = false;
                }
                ++g_frames;
                if (!texture_mode) {
                    ot_hal_function<VoidCall>(OT_HAL_SYM_DrawFullScreenMovie)();
                } else if (to_primary) {
                    show_primary(&opened);
                }
                ot_hal_function<VoidCall>(OT_HAL_SYM_Input_UpdateActionStates)();
                if (ot_hal_function<FirstKeyDownCall>(OT_HAL_SYM_PCInput_FirstKeyDown)(0) == 0 &&
                    ot_hal_function<IntCall>(OT_HAL_SYM_PCInput_FirstJoystickButton)() < 0) {
                    held = false;
                } else if (!held) {
                    break;
                }
            }
        } else {
            if (message.message == WM_QUIT || message.message == WM_KEYDOWN) {
                playing = false;
            }
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    } while (playing);
    if (!texture_mode) {
        // Retail unbinds the movie texture; the stand-in device never fails.
        void *device = global<void *>(OT_HAL_SYM_D3DDevice);
        reinterpret_cast<SetTextureCall>(vtable_of(device)[kDeviceSetTexture])(device, 0, 0);
    }
    if (opened) {
        g_movie->close();
    }
    ot_hal_function<VoidCall>(OT_HAL_SYM_CloseBinkMovie)();
    ot_hal_function<VoidCall>(OT_HAL_SYM_Input_ClearPadTriggers)();
    return 1;
}

DWORD parse_decimal(const char **at) {
    DWORD value = 0;
    for (; **at >= '0' && **at <= '9'; ++*at) {
        value = value * 10u + static_cast<DWORD>(**at - '0');
    }
    return value;
}

} // namespace

void ot_movie_hal_prepare(void) {
    HMODULE bink = GetModuleHandleA("binkw32.dll");
    g_bink_service = bink != 0 ? reinterpret_cast<BinkCall>(GetProcAddress(bink, "_BinkService@4")) : 0;
    g_bink_wait = bink != 0 ? reinterpret_cast<BinkCall>(GetProcAddress(bink, "_BinkWait@4")) : 0;
    char value[32];
    const DWORD length = GetEnvironmentVariableA("OPENTONY_MOVIE_TRACE", value, sizeof(value));
    if (length != 0 && length < sizeof(value)) {
        const char *at = value;
        g_trace_first = parse_decimal(&at);
        if (*at == ':') {
            ++at;
            g_trace_count = parse_decimal(&at);
        }
        if (*at == ':') {
            ++at;
            g_trace_step = parse_decimal(&at);
            g_trace_step = g_trace_step != 0 ? g_trace_step : 1u;
        }
        ot_output_path(g_trace_directory, "video\\");
        CreateDirectoryA(g_trace_directory, 0);
        ot_log("movie=sdl: %u movie frames from %u, every %u, go to %smovie_NNNNN.hal.ottx",
            static_cast<unsigned>(g_trace_count), static_cast<unsigned>(g_trace_first),
            static_cast<unsigned>(g_trace_step), g_trace_directory);
    }
}

int ot_movie_hal_ready(void) {
    return g_bink_service != 0 && g_bink_wait != 0;
}

int ot_movie_hal_describe(char *out, int size) {
    char line[160];
    const int length = wsprintfA(line, "movie_hal movies=%u frames=%u shown=%u quad_movies=%u traced=%u",
        static_cast<unsigned>(g_movies), static_cast<unsigned>(g_frames), static_cast<unsigned>(g_shown),
        static_cast<unsigned>(g_quad_movies), static_cast<unsigned>(g_traced));
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}

OT_REGISTER_HAL_ADAPTER(PCMovie_PlayStartupMedia, hal_PCMovie_PlayStartupMedia)
