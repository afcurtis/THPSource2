#include "video_mirror.h"

#include "video_renderer.h"
#include "../hook_runtime.h"
#include "../../hal/sdl/sdl_api.h"
#include "../../hal/sdl/sdl_backends.h"
#include "../../hal/sdl/sdl_video.h"

#include <math.h>
#include <string.h>

// Live HAL mirror (video_mirror.h).

namespace {

enum SurfaceSlot { kRelease = 2, kSurfaceLock = 25, kSurfaceUnlock = 32 };
enum DeviceSlot { kGetRenderTarget = 9 };
const DWORD kLockReadOnly = 0x10;
const DWORD kLockWait = 0x01;

struct PixelFormat {
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

typedef ULONG(__stdcall *ReleaseCall)(void *);
typedef HRESULT(__stdcall *SurfaceLockCall)(void *, RECT *, SurfaceDesc *, DWORD, HANDLE);
typedef HRESULT(__stdcall *SurfaceUnlockCall)(void *, RECT *);
typedef HRESULT(__stdcall *GetTargetCall)(void *, void **);

const OtHalVideoBackend *g_video = 0;
DWORD g_compare_every = 0;
int g_configured = 0;
int g_running = 0;
DWORD g_frames = 0;
double g_worst_psnr = 99.0;
DWORD g_compared = 0;

void **vtable_of(void *object) {
    return *reinterpret_cast<void ***>(object);
}

// Retail's render target vs the mirror, both reduced to 5-6-5.
void compare(void *device) {
    OtHalImage image;
    if (!g_video->read_back(&image)) {
        return;
    }
    void *target = 0;
    typedef HRESULT(__stdcall * GetTarget)(void *, void **);
    if (reinterpret_cast<GetTarget>(vtable_of(device)[kGetRenderTarget])(device, &target) != 0 || target == 0) {
        return;
    }
    SurfaceDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    void **vtable = vtable_of(target);
    if (reinterpret_cast<SurfaceLockCall>(vtable[kSurfaceLock])(target, 0, &desc, kLockReadOnly | kLockWait, 0) == 0) {
        if (desc.format.bits == 16 && desc.format.green == 0x07e0u && static_cast<int32_t>(desc.width) == image.width &&
            static_cast<int32_t>(desc.height) == image.height) {
            double squared = 0.0;
            DWORD within = 0;
            for (int32_t y = 0; y < image.height; ++y) {
                const uint16_t *retail = reinterpret_cast<const uint16_t *>(static_cast<const unsigned char *>(desc.bits) + y * desc.pitch);
                const uint32_t *mirror = reinterpret_cast<const uint32_t *>(static_cast<const unsigned char *>(image.pixels) + y * image.pitch);
                for (int32_t x = 0; x < image.width; ++x) {
                    const uint32_t a = retail[x];
                    const uint32_t b = mirror[x];
                    const int channels[3][2] = {
                        {static_cast<int>(((a >> 11) & 31) * 255 / 31), static_cast<int>(((b >> 19) & 31) * 255 / 31)},
                        {static_cast<int>(((a >> 5) & 63) * 255 / 63), static_cast<int>(((b >> 10) & 63) * 255 / 63)},
                        {static_cast<int>((a & 31) * 255 / 31), static_cast<int>(((b >> 3) & 31) * 255 / 31)},
                    };
                    int largest = 0;
                    for (int channel = 0; channel < 3; ++channel) {
                        const int difference = channels[channel][0] - channels[channel][1];
                        squared += static_cast<double>(difference) * difference;
                        const int magnitude = difference < 0 ? -difference : difference;
                        largest = magnitude > largest ? magnitude : largest;
                    }
                    within += largest <= 16 ? 1u : 0u;
                }
            }
            const double samples = static_cast<double>(image.width) * image.height * 3.0;
            const double mse = squared / samples;
            const double psnr = mse == 0.0 ? 99.0 : 10.0 * log10(255.0 * 255.0 / mse);
            if (psnr < g_worst_psnr) {
                g_worst_psnr = psnr;
            }
            ++g_compared;
            if (psnr < 30.0) {
                // Keep both frames for a look (tony hal video png converts .ottx).
                char path[MAX_PATH];
                char name[64];
                DWORD header[12] = {0x5854544fu, 1u, static_cast<DWORD>(image.width), static_cast<DWORD>(image.height),
                    16u, desc.format.red, desc.format.green, desc.format.blue, 0u, desc.format.flags, 0u, 0u};
                DWORD written = 0;
                wsprintfA(name, "mirror_%05u.retail.ottx", static_cast<unsigned>(g_frames));
                ot_output_path(path, name);
                HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
                if (file != INVALID_HANDLE_VALUE) {
                    WriteFile(file, header, sizeof(header), &written, 0);
                    for (int32_t y = 0; y < image.height; ++y) {
                        WriteFile(file, static_cast<const unsigned char *>(desc.bits) + y * desc.pitch,
                            static_cast<DWORD>(image.width) * 2u, &written, 0);
                    }
                    CloseHandle(file);
                }
                DWORD mirror_header[12] = {0x5854544fu, 1u, static_cast<DWORD>(image.width),
                    static_cast<DWORD>(image.height), 32u, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0u, 0x40u, 0u, 0u};
                wsprintfA(name, "mirror_%05u.mirror.ottx", static_cast<unsigned>(g_frames));
                ot_output_path(path, name);
                file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
                if (file != INVALID_HANDLE_VALUE) {
                    WriteFile(file, mirror_header, sizeof(mirror_header), &written, 0);
                    WriteFile(file, image.pixels, static_cast<DWORD>(image.pitch * image.height), &written, 0);
                    CloseHandle(file);
                }
            }
            ot_log("video mirror frame %u: %d.%02d dB, %u.%02u%% within 16", static_cast<unsigned>(g_frames),
                static_cast<int>(psnr), static_cast<int>((psnr - static_cast<int>(psnr)) * 100.0),
                static_cast<unsigned>(within * 100u / (image.width * image.height)),
                static_cast<unsigned>((within * 10000u / (image.width * image.height)) % 100u));
        }
        reinterpret_cast<SurfaceUnlockCall>(vtable[kSurfaceUnlock])(target, 0);
    }
    reinterpret_cast<ReleaseCall>(vtable[kRelease])(target);
}

} // namespace

void ot_video_mirror_configure(void) {
    char value[16];
    const DWORD length = GetEnvironmentVariableA("OPENTONY_VIDEO_MIRROR", value, sizeof(value));
    if (length == 0 || length >= sizeof(value)) {
        return;
    }
    g_configured = 1;
    for (const char *digit = value; *digit >= '0' && *digit <= '9'; ++digit) {
        g_compare_every = g_compare_every * 10u + static_cast<DWORD>(*digit - '0');
    }
    ot_log("video mirror: on (compare every %u frames)", static_cast<unsigned>(g_compare_every));
}

int ot_video_mirror_enabled(void) {
    return g_configured;
}

int ot_video_mirror_start(void) {
    if (!g_configured || g_running) {
        return g_running;
    }
    if (!OT_SDL(SDL_InitSubSystem)(SDL_INIT_VIDEO)) {
        ot_log("video mirror: SDL video unavailable: %s", OT_SDL(SDL_GetError)());
        g_configured = 0;
        return 0;
    }
    ot_hal_sdl_window_hints();
    ot_hal_sdl_video_gl_hints();
    ot_hal_sdl_window_request_opengl(1);
    OtHalWindowDesc window = {"OpenTony HAL mirror", 640, 480, 0};
    if (!ot_hal_sdl_window.create(&window)) {
        ot_log("video mirror: window: %s", OT_SDL(SDL_GetError)());
        g_configured = 0;
        return 0;
    }
    OtHalVideoMode mode = {640, 480, 32u, 0u, 0u};
    g_video = &ot_hal_sdl_video_gl;
    if (!g_video->init(&mode)) {
        ot_log("video mirror: video backend: %s", ot_hal_sdl_video_error());
        ot_hal_sdl_window.destroy();
        g_video = 0;
        g_configured = 0;
        return 0;
    }
    ot_video_renderer_start(g_video);
    g_running = 1;
    ot_log("video mirror: SDL GL 3.3 window up");
    return 1;
}

void ot_video_mirror_stop(void) {
    if (!g_running) {
        return;
    }
    ot_video_renderer_stop();
    g_video->shutdown();
    ot_hal_sdl_window.destroy();
    g_running = 0;
}

void ot_video_mirror_begin(void) {
    if (!g_running) {
        return;
    }
    ot_video_renderer_frame();
    g_video->begin_frame();
}

void ot_video_mirror_clear(uint32_t flags, uint32_t color, float depth, const OtHalRect *area) {
    if (g_running) {
        g_video->clear(flags, color, depth, area);
    }
}

void ot_video_mirror_viewport(const OtHalRect *area) {
    if (g_running) {
        g_video->set_viewport(area);
    }
}

void ot_video_mirror_present(void *device) {
    if (!g_running) {
        return;
    }
    ++g_frames;
    if (g_compare_every != 0 && g_frames % g_compare_every == 0 && device != 0) {
        compare(device);
    }
    g_video->present();
    ot_hal_sdl_process_events();
}

int ot_video_mirror_describe(char *out, int size) {
    char line[256];
    int length = wsprintfA(line, "video_mirror frames=%u ", static_cast<unsigned>(g_frames));
    length += ot_video_renderer_describe(line + length, static_cast<int>(sizeof(line)) - length);
    length += wsprintfA(line + length, " compared=%u worst_psnr=%d", static_cast<unsigned>(g_compared),
        static_cast<int>(g_worst_psnr));
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}
