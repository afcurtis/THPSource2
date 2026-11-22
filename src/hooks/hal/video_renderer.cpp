#include "video_renderer.h"

#include "video_surface.h"

#include <windows.h>

#include <string.h>

// Game primitive stream -> HAL video backend (video_renderer.h).

namespace {

enum SurfaceSlot { kSurfaceRelease = 2, kSurfaceBlt = 5, kSurfaceBltFast = 7, kSurfaceLock = 25, kSurfaceUnlock = 32 };
const DWORD kLockReadOnly = 0x10; // DDLOCK_READONLY
const DWORD kLockWait = 0x01;     // DDLOCK_WAIT

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

typedef HRESULT(__stdcall *SurfaceLockCall)(void *, RECT *, SurfaceDesc *, DWORD, HANDLE);
typedef HRESULT(__stdcall *SurfaceUnlockCall)(void *, RECT *);
typedef ULONG(__stdcall *SurfaceReleaseCall)(void *);
typedef HRESULT(__stdcall *SurfaceBltCall)(void *, RECT *, void *, RECT *, DWORD, void *);
typedef HRESULT(__stdcall *SurfaceBltFastCall)(void *, DWORD, DWORD, void *, RECT *, DWORD);

const OtHalVideoBackend *g_video = 0;
DWORD g_frames = 0;
DWORD g_draws = 0;
DWORD g_textures = 0;
DWORD g_texture_failures = 0;

// Game surface pointer -> HAL texture.
const DWORD kTextureSlots = 4096;
const DWORD kTextureLifetime = 600;
DWORD g_surface[kTextureSlots];
OtHalHandle g_handle[kTextureSlots];
uint8_t g_stale[kTextureSlots];
DWORD g_forgotten = 0;
DWORD g_texture_epoch = 0;
DWORD g_uploading = 0; // the renderer's own Lock/Unlock
DWORD g_render_thread = 0;

void forget(uint32_t surface);

// Surface writes and releases, whoever makes them (D3DTex_*, D3DSprite_*,
// NextBinkFrame, ...): every write into a DirectDraw surface ends in Unlock or
// is a Blt/BltFast onto it, and a surface is gone when its last Release
// returns 0. The IDirectDrawSurface7 vtable DirectDraw shares between all its
// surfaces is patched once; the original methods run first.
SurfaceUnlockCall g_unlock = 0;
SurfaceReleaseCall g_release = 0;
SurfaceBltCall g_blt = 0;
SurfaceBltFastCall g_blt_fast = 0;
void **g_watched_vtable = 0;

void touched(void *surface) {
    if (g_uploading == 0 && GetCurrentThreadId() == g_render_thread) {
        forget(static_cast<uint32_t>(reinterpret_cast<UINT_PTR>(surface)));
    }
}

HRESULT __stdcall watched_unlock(void *surface, RECT *rect) {
    const HRESULT result = g_unlock(surface, rect);
    touched(surface);
    return result;
}

ULONG __stdcall watched_release(void *surface) {
    const ULONG left = g_release(surface);
    if (left == 0) {
        touched(surface);
    }
    return left;
}

HRESULT __stdcall watched_blt(void *surface, RECT *target, void *source, RECT *area, DWORD flags, void *effects) {
    const HRESULT result = g_blt(surface, target, source, area, flags, effects);
    touched(surface);
    return result;
}

HRESULT __stdcall watched_blt_fast(void *surface, DWORD x, DWORD y, void *source, RECT *area, DWORD flags) {
    const HRESULT result = g_blt_fast(surface, x, y, source, area, flags);
    touched(surface);
    return result;
}

void patch_slot(void **vtable, int slot, void *function) {
    DWORD old_protect;
    DWORD ignored;
    VirtualProtect(&vtable[slot], sizeof(void *), PAGE_EXECUTE_READWRITE, &old_protect);
    vtable[slot] = function;
    VirtualProtect(&vtable[slot], sizeof(void *), old_protect, &ignored);
}

void watch(void *surface) {
    void **vtable = *reinterpret_cast<void ***>(surface);
    if (g_watched_vtable != 0 || vtable[kSurfaceUnlock] == reinterpret_cast<void *>(&watched_unlock)) {
        return; // one IDirectDrawSurface7 vtable per DirectDraw
    }
    g_unlock = reinterpret_cast<SurfaceUnlockCall>(vtable[kSurfaceUnlock]);
    g_release = reinterpret_cast<SurfaceReleaseCall>(vtable[kSurfaceRelease]);
    g_blt = reinterpret_cast<SurfaceBltCall>(vtable[kSurfaceBlt]);
    g_blt_fast = reinterpret_cast<SurfaceBltFastCall>(vtable[kSurfaceBltFast]);
    patch_slot(vtable, kSurfaceUnlock, reinterpret_cast<void *>(&watched_unlock));
    patch_slot(vtable, kSurfaceRelease, reinterpret_cast<void *>(&watched_release));
    patch_slot(vtable, kSurfaceBlt, reinterpret_cast<void *>(&watched_blt));
    patch_slot(vtable, kSurfaceBltFast, reinterpret_cast<void *>(&watched_blt_fast));
    g_watched_vtable = vtable;
}

void unwatch() {
    if (g_watched_vtable == 0) {
        return;
    }
    patch_slot(g_watched_vtable, kSurfaceUnlock, reinterpret_cast<void *>(g_unlock));
    patch_slot(g_watched_vtable, kSurfaceRelease, reinterpret_cast<void *>(g_release));
    patch_slot(g_watched_vtable, kSurfaceBlt, reinterpret_cast<void *>(g_blt));
    patch_slot(g_watched_vtable, kSurfaceBltFast, reinterpret_cast<void *>(g_blt_fast));
    g_watched_vtable = 0;
}

void **vtable_of(void *object) {
    return *reinterpret_cast<void ***>(object);
}

uint32_t hal_format(const PixelFormat &format) {
    if (format.bits == 16 && format.red == 0x7c00u && format.green == 0x03e0u && format.blue == 0x001fu) {
        return format.alpha == 0x8000u ? OT_HAL_PIXEL_ARGB1555 : 0;
    }
    if (format.bits == 16 && format.red == 0x0f00u && format.green == 0x00f0u && format.blue == 0x000fu) {
        return OT_HAL_PIXEL_ARGB4444;
    }
    if (format.bits == 16 && format.red == 0xf800u && format.green == 0x07e0u && format.blue == 0x001fu) {
        return OT_HAL_PIXEL_RGB565;
    }
    if (format.bits == 32 && format.red == 0x00ff0000u) {
        return OT_HAL_PIXEL_ARGB8888;
    }
    return 0;
}

OtHalHandle upload(DWORD surface) {
    void *object = reinterpret_cast<void *>(static_cast<UINT_PTR>(surface));
    watch(object);
    SurfaceDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    void **vtable = vtable_of(object);
    ++g_uploading;
    if (reinterpret_cast<SurfaceLockCall>(vtable[kSurfaceLock])(object, 0, &desc, kLockReadOnly | kLockWait, 0) != 0) {
        --g_uploading;
        ++g_texture_failures;
        return OT_HAL_INVALID_HANDLE;
    }
    OtHalHandle handle = OT_HAL_INVALID_HANDLE;
    const uint32_t format = hal_format(desc.format);
    if (format != 0) {
        OtHalTextureDesc texture = {static_cast<int32_t>(desc.width), static_cast<int32_t>(desc.height), format,
            OT_HAL_INVALID_HANDLE};
        handle = g_video->texture_create(&texture);
        if (handle != OT_HAL_INVALID_HANDLE) {
            OtHalRect area = {0, 0, texture.width, texture.height};
            g_video->texture_update(handle, &area, desc.bits, desc.pitch);
            ++g_textures;
        }
    } else {
        ++g_texture_failures;
    }
    reinterpret_cast<SurfaceUnlockCall>(vtable[kSurfaceUnlock])(object, 0);
    --g_uploading;
    return handle;
}

OtHalHandle texture_for(DWORD surface) {
    if (surface == 0) {
        return OT_HAL_INVALID_HANDLE;
    }
    // video=sdl's own surfaces (video_surface.cpp) are already HAL textures
    // with exact dirty tracking (their own Unlock/Blt/BltFast/Release): no
    // address-keyed cache, no periodic flush. The live mirror's surfaces are
    // real DirectDraw's and keep the cache below.
    void *object = reinterpret_cast<void *>(static_cast<UINT_PTR>(surface));
    if (ot_ddraw_is_surface(object)) {
        return ot_ddraw_surface_texture(object);
    }
    DWORD slot = (surface >> 4) & (kTextureSlots - 1u);
    for (DWORD probe = 0; probe < kTextureSlots; ++probe, slot = (slot + 1u) & (kTextureSlots - 1u)) {
        if (g_surface[slot] == surface) {
            if (g_stale[slot]) {
                if (g_handle[slot] != OT_HAL_INVALID_HANDLE) {
                    g_video->texture_destroy(g_handle[slot]);
                }
                g_handle[slot] = upload(surface);
                g_stale[slot] = 0;
            }
            return g_handle[slot];
        }
        if (g_surface[slot] == 0) {
            g_surface[slot] = surface;
            g_handle[slot] = upload(surface);
            return g_handle[slot];
        }
    }
    return OT_HAL_INVALID_HANDLE;
}

void flush_textures() {
    for (DWORD slot = 0; slot < kTextureSlots; ++slot) {
        if (g_handle[slot] != OT_HAL_INVALID_HANDLE) {
            g_video->texture_destroy(g_handle[slot]);
        }
    }
    memset(g_surface, 0, sizeof(g_surface));
    memset(g_handle, 0, sizeof(g_handle));
    memset(g_stale, 0, sizeof(g_stale));
}

void forget(uint32_t surface) {
    if (g_video == 0 || surface == 0) {
        return;
    }
    DWORD slot = (surface >> 4) & (kTextureSlots - 1u);
    for (DWORD probe = 0; probe < kTextureSlots && g_surface[slot] != 0; ++probe, slot = (slot + 1u) & (kTextureSlots - 1u)) {
        if (g_surface[slot] == surface) {
            if (!g_stale[slot]) {
                g_stale[slot] = 1;
                ++g_forgotten;
            }
            return;
        }
    }
}

} // namespace

void ot_video_renderer_start(const OtHalVideoBackend *video) {
    g_video = video;
    g_render_thread = GetCurrentThreadId();
    memset(g_surface, 0, sizeof(g_surface));
    memset(g_handle, 0, sizeof(g_handle));
    memset(g_stale, 0, sizeof(g_stale));
    g_texture_epoch = g_frames;
}

void ot_video_renderer_stop(void) {
    unwatch();
    if (g_video != 0) {
        flush_textures();
        g_video = 0;
    }
}

const OtHalVideoBackend *ot_video_renderer_backend(void) {
    return g_video;
}

void ot_video_renderer_frame(void) {
    if (g_video == 0) {
        return;
    }
    ++g_frames;
    if (g_frames - g_texture_epoch >= kTextureLifetime) {
        flush_textures();
        g_texture_epoch = g_frames;
    }
}

void ot_video_renderer_draw(const OtHalDrawState *state, uint32_t topology, const OtHalVertex *vertices, uint32_t count) {
    if (g_video == 0) {
        return;
    }
    OtHalDrawState mapped = *state;
    mapped.texture = texture_for(state->texture);
    if (state->texture != 0 && mapped.texture == OT_HAL_INVALID_HANDLE) {
        return; // a texture that did not copy: drop rather than draw untextured
    }
    g_video->draw(&mapped, topology, vertices, count);
    ++g_draws;
}

void ot_video_renderer_forget(uint32_t surface) {
    forget(surface);
}

int ot_video_renderer_describe(char *out, int size) {
    char line[160];
    const int length = wsprintfA(line, "draws=%u textures=%u refreshed=%u texture_failures=%u",
        static_cast<unsigned>(g_draws), static_cast<unsigned>(g_textures), static_cast<unsigned>(g_forgotten),
        static_cast<unsigned>(g_texture_failures));
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}
