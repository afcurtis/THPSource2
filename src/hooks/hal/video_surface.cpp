#include "video_surface.h"

#include "hal_adapters.h"
#include "../hook_runtime.h"
#include "../../hal/oracle/ddraw_surface.h"

#include <string.h>

// Stand-in DirectDraw (video_surface.h). Retail vtable layouts: IDirectDraw7
// and IDirectDrawSurface7 in tony/hal/com.py order, DDSURFACEDESC2,
// DDPIXELFORMAT, DDBLTFX.

namespace {

const HRESULT kOk = 0;
const HRESULT kNoInterface = static_cast<HRESULT>(0x80004002L);
const HRESULT kNotImplemented = static_cast<HRESULT>(0x80004001L); // E_NOTIMPL
const HRESULT kInvalidParams = static_cast<HRESULT>(0x80070057L);  // DDERR_INVALIDPARAMS (E_INVALIDARG)
const HRESULT kOutOfMemory = static_cast<HRESULT>(0x8007000EL);    // DDERR_OUTOFMEMORY (E_OUTOFMEMORY)
const HRESULT kSurfaceBusy = static_cast<HRESULT>(0x887601AEL);    // DDERR_SURFACEBUSY

const DWORD kDescCaps = 0x1, kDescHeight = 0x2, kDescWidth = 0x4, kDescPitch = 0x8, kDescPixelFormat = 0x1000;
const DWORD kPixelRgb = 0x40;
const DWORD kLockReadOnly = 0x10;
const DWORD kBltColorFill = 0x00000400;

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

struct BltFx { // DDBLTFX up to dwFillColor (0x50); the rest (colour keys) is unused here
    DWORD size;
    DWORD reserved[19];
    DWORD fill_color;
};

typedef HRESULT(__stdcall *EnumModesCallback)(SurfaceDesc *, void *);

// IID_IUnknown, IID_IDirectDraw7, IID_IDirectDrawSurface7
const GUID kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIidDirectDraw7 = {0x15e65ec0, 0x3b9c, 0x11d2, {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};
const GUID kIidDirectDrawSurface7 = {0x06675a80, 0x3b9b, 0x11d2, {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};

bool same_guid(const GUID *left, const GUID &right) {
    return left != 0 && memcmp(left, &right, sizeof(GUID)) == 0;
}

// ------------------------------------------------------------------ state

struct Surface;
extern void *const g_surface_vtable[];
extern void *const g_ddraw_vtable[];

struct DDraw {
    void *const *vtable;
    LONG references;
};
DDraw g_ddraw = {g_ddraw_vtable, 0};
OtDdrawPresentCallback g_on_primary_flip = 0;

const OtHalVideoBackend *g_video = 0;
DWORD g_render_thread = 0;
int g_suppress_primary_refresh = 0;

const DWORD kPendingDestroy = 64;
OtHalHandle g_pending_destroy[kPendingDestroy];
DWORD g_pending_destroy_count = 0;

const DWORD kDdrawSlots = 30;
const DWORD kSurfaceSlots = 49;
DWORD g_ddraw_calls[kDdrawSlots];
DWORD g_surface_calls[kSurfaceSlots];
DWORD g_surfaces_created = 0;
DWORD g_surfaces_live = 0;
DWORD g_textures = 0;
DWORD g_texture_failures = 0;
DWORD g_unsupported_blt = 0;

struct Surface {
    void *const *vtable;
    LONG references;
    int32_t width, height;
    LONG pitch;
    PixelFormat format;
    DWORD caps[4];
    unsigned char *pixels;
    DWORD lock_flags; // 0 = not locked
    uint8_t dirty;
    uint8_t primary;
    OtHalHandle texture;
};

void mark_dirty(Surface *surface) {
    surface->dirty = 1;
}

// Fills the primary's own pixel memory from what the video backend actually
// shows, for a screenshot: nothing under video=sdl ever draws into this
// surface (the ordering-table adapter and the stand-in D3D device draw to
// the backend's own offscreen target), so it otherwise stays whatever
// CreateSurface zero-filled it to. Silently leaves the buffer alone (no
// worse than today) if the backend has no frame yet or reports a size that
// doesn't match the surface's -- both should be unreachable, since
// create_back_buffer always sizes the primary from the same video mode.
void refresh_primary_from_video(Surface *surface) {
    if (g_video == 0) {
        ot_log("video=sdl: screenshot refresh skipped: no video backend");
        return;
    }
    OtHalImage image;
    if (!g_video->read_back(&image)) {
        ot_log("video=sdl: screenshot refresh skipped: read_back failed");
        return;
    }
    if (image.width != surface->width || image.height != surface->height) {
        ot_log("video=sdl: screenshot refresh skipped: size mismatch (backend %dx%d, primary %dx%d)",
            static_cast<int>(image.width), static_cast<int>(image.height), static_cast<int>(surface->width),
            static_cast<int>(surface->height));
        return;
    }
    ot_ddraw_convert_from_argb8888(surface->pixels, surface->pitch, static_cast<int32_t>(surface->format.bits),
        surface->format.red, surface->format.green, surface->format.blue, surface->format.alpha, image.pixels,
        image.pitch, surface->width, surface->height);
    ot_log("video=sdl: screenshot refreshed %dx%d, %u-bit, masks r%08X g%08X b%08X a%08X", static_cast<int>(surface->width),
        static_cast<int>(surface->height), static_cast<unsigned>(surface->format.bits),
        static_cast<unsigned>(surface->format.red), static_cast<unsigned>(surface->format.green),
        static_cast<unsigned>(surface->format.blue), static_cast<unsigned>(surface->format.alpha));
}

// ---------------------------------------------------------- IDirectDraw7

ULONG __stdcall ddraw_add_ref(DDraw *ddraw) {
    ++g_ddraw_calls[1];
    return static_cast<ULONG>(++ddraw->references);
}

ULONG __stdcall ddraw_release(DDraw *ddraw) {
    ++g_ddraw_calls[2];
    if (ddraw->references > 0) {
        --ddraw->references;
    }
    return static_cast<ULONG>(ddraw->references);
}

HRESULT __stdcall ddraw_query_interface(DDraw *ddraw, const GUID *iid, void **out) {
    ++g_ddraw_calls[0];
    if (out == 0) {
        return kInvalidParams;
    }
    if (same_guid(iid, kIidUnknown) || same_guid(iid, kIidDirectDraw7)) {
        ddraw_add_ref(ddraw);
        *out = ddraw;
        return kOk;
    }
    *out = 0;
    return kNoInterface;
}

void destroy_surface_texture(Surface *surface) {
    if (surface->texture == OT_HAL_INVALID_HANDLE) {
        return;
    }
    if (g_video != 0 && (g_render_thread == 0 || GetCurrentThreadId() == g_render_thread)) {
        g_video->texture_destroy(surface->texture);
    } else if (g_pending_destroy_count < kPendingDestroy) {
        g_pending_destroy[g_pending_destroy_count++] = surface->texture;
    }
    surface->texture = OT_HAL_INVALID_HANDLE;
}

HRESULT __stdcall ddraw_create_surface(DDraw *ddraw, SurfaceDesc *desc, Surface **out, void *outer) {
    ++g_ddraw_calls[6];
    if (desc == 0 || out == 0 || outer != 0) {
        return kInvalidParams;
    }
    *out = 0;
    PixelFormat format = {sizeof(PixelFormat), kPixelRgb, 0, 16, 0xf800u, 0x07e0u, 0x001fu, 0}; // 5-6-5 default
    if ((desc->flags & kDescPixelFormat) != 0 && desc->format.size == sizeof(PixelFormat)) {
        format = desc->format;
    }
    if (format.bits != 16 && format.bits != 32) {
        return kInvalidParams; // only the 16- and 32-bit formats a Direct3D 7 device offers
    }
    const int32_t width = (desc->flags & kDescWidth) != 0 ? static_cast<int32_t>(desc->width) : 0;
    const int32_t height = (desc->flags & kDescHeight) != 0 ? static_cast<int32_t>(desc->height) : 0;
    if (width <= 0 || height <= 0) {
        return kInvalidParams;
    }
    const int32_t bytes_per_pixel = static_cast<int32_t>(format.bits / 8u);
    const int32_t pitch = ot_ddraw_pitch(width, bytes_per_pixel);
    unsigned char *pixels = static_cast<unsigned char *>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, static_cast<SIZE_T>(pitch) * static_cast<SIZE_T>(height)));
    if (pixels == 0) {
        return kOutOfMemory;
    }
    Surface *surface = static_cast<Surface *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Surface)));
    if (surface == 0) {
        HeapFree(GetProcessHeap(), 0, pixels);
        return kOutOfMemory;
    }
    surface->vtable = g_surface_vtable;
    surface->references = 1;
    surface->width = width;
    surface->height = height;
    surface->pitch = pitch;
    surface->format = format;
    if (desc->flags & kDescCaps) {
        memcpy(surface->caps, desc->caps, sizeof(surface->caps));
    }
    surface->pixels = pixels;
    surface->texture = OT_HAL_INVALID_HANDLE;
    surface->dirty = 1;
    ddraw_add_ref(ddraw); // each live surface holds one reference on DirectDraw
    ++g_surfaces_created;
    ++g_surfaces_live;
    *out = surface;
    return kOk;
}

HRESULT __stdcall ddraw_enum_display_modes(
    DDraw *, DWORD, SurfaceDesc *, void *context, EnumModesCallback callback) {
    ++g_ddraw_calls[8];
    struct Mode {
        DWORD width, height;
    };
    static const Mode kModes[] = {
        {320, 240}, {400, 300}, {512, 384}, {640, 400}, {640, 480}, {800, 600}, {1024, 768}};
    for (unsigned index = 0; callback != 0 && index < sizeof(kModes) / sizeof(kModes[0]); ++index) {
        SurfaceDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.size = sizeof(desc);
        desc.flags = kDescCaps | kDescHeight | kDescWidth | kDescPitch | kDescPixelFormat;
        desc.height = kModes[index].height;
        desc.width = kModes[index].width;
        desc.pitch = ot_ddraw_pitch(static_cast<int32_t>(kModes[index].width), 2);
        desc.format.size = sizeof(PixelFormat);
        desc.format.flags = kPixelRgb;
        desc.format.bits = 16;
        desc.format.red = 0xf800u;
        desc.format.green = 0x07e0u;
        desc.format.blue = 0x001fu;
        if (callback(&desc, context) == 0) { // D3DENUMRET_CANCEL
            break;
        }
    }
    return kOk;
}

HRESULT __stdcall ddraw_restore_all_surfaces(DDraw *) {
    ++g_ddraw_calls[25];
    return kOk;
}

template <int Slot> HRESULT __stdcall ddraw_no0(DDraw *) { ++g_ddraw_calls[Slot]; return kNotImplemented; }
template <int Slot> HRESULT __stdcall ddraw_no1(DDraw *, DWORD) { ++g_ddraw_calls[Slot]; return kNotImplemented; }
template <int Slot> HRESULT __stdcall ddraw_no2(DDraw *, DWORD, DWORD) { ++g_ddraw_calls[Slot]; return kNotImplemented; }
template <int Slot> HRESULT __stdcall ddraw_no3(DDraw *, DWORD, DWORD, DWORD) {
    ++g_ddraw_calls[Slot];
    return kNotImplemented;
}
template <int Slot> HRESULT __stdcall ddraw_no4(DDraw *, DWORD, DWORD, DWORD, DWORD) {
    ++g_ddraw_calls[Slot];
    return kNotImplemented;
}
template <int Slot> HRESULT __stdcall ddraw_no5(DDraw *, DWORD, DWORD, DWORD, DWORD, DWORD) {
    ++g_ddraw_calls[Slot];
    return kNotImplemented;
}

#define OT_SLOT(function) reinterpret_cast<void *>(&function)

void *const g_ddraw_vtable[kDdrawSlots] = {
    OT_SLOT(ddraw_query_interface),      // 0
    OT_SLOT(ddraw_add_ref),              // 1
    OT_SLOT(ddraw_release),              // 2
    OT_SLOT(ddraw_no0<3>),               // Compact
    OT_SLOT(ddraw_no3<4>),               // CreateClipper
    OT_SLOT(ddraw_no4<5>),               // CreatePalette
    OT_SLOT(ddraw_create_surface),       // 6
    OT_SLOT(ddraw_no2<7>),               // DuplicateSurface
    OT_SLOT(ddraw_enum_display_modes),   // 8
    OT_SLOT(ddraw_no4<9>),               // EnumSurfaces
    OT_SLOT(ddraw_no0<10>),              // FlipToGDISurface
    OT_SLOT(ddraw_no2<11>),              // GetCaps
    OT_SLOT(ddraw_no1<12>),              // GetDisplayMode
    OT_SLOT(ddraw_no2<13>),              // GetFourCCCodes
    OT_SLOT(ddraw_no1<14>),              // GetGDISurface
    OT_SLOT(ddraw_no1<15>),              // GetMonitorFrequency
    OT_SLOT(ddraw_no1<16>),              // GetScanLine
    OT_SLOT(ddraw_no1<17>),              // GetVerticalBlankStatus
    OT_SLOT(ddraw_no1<18>),              // Initialize
    OT_SLOT(ddraw_no0<19>),              // RestoreDisplayMode
    OT_SLOT(ddraw_no2<20>),              // SetCooperativeLevel
    OT_SLOT(ddraw_no5<21>),              // SetDisplayMode
    OT_SLOT(ddraw_no2<22>),              // WaitForVerticalBlank
    OT_SLOT(ddraw_no3<23>),              // GetAvailableVidMem
    OT_SLOT(ddraw_no2<24>),              // GetSurfaceFromDC
    OT_SLOT(ddraw_restore_all_surfaces), // 25
    OT_SLOT(ddraw_no0<26>),              // TestCooperativeLevel
    OT_SLOT(ddraw_no2<27>),              // GetDeviceIdentifier
    OT_SLOT(ddraw_no3<28>),              // StartModeTest
    OT_SLOT(ddraw_no2<29>),              // EvaluateMode
};

// ---------------------------------------------------- IDirectDrawSurface7

HRESULT __stdcall surface_query_interface(Surface *surface, const GUID *iid, void **out) {
    ++g_surface_calls[0];
    if (out == 0) {
        return kInvalidParams;
    }
    if (same_guid(iid, kIidUnknown) || same_guid(iid, kIidDirectDrawSurface7)) {
        InterlockedIncrement(&surface->references);
        *out = surface;
        return kOk;
    }
    *out = 0;
    return kNoInterface;
}

ULONG __stdcall surface_add_ref(Surface *surface) {
    ++g_surface_calls[1];
    return static_cast<ULONG>(InterlockedIncrement(&surface->references));
}

ULONG __stdcall surface_release(Surface *surface) {
    ++g_surface_calls[2];
    const LONG left = InterlockedDecrement(&surface->references);
    if (left > 0) {
        return static_cast<ULONG>(left);
    }
    destroy_surface_texture(surface);
    HeapFree(GetProcessHeap(), 0, surface->pixels);
    HeapFree(GetProcessHeap(), 0, surface);
    --g_surfaces_live;
    ddraw_release(&g_ddraw); // gives back the reference CreateSurface took
    return 0;
}

HRESULT __stdcall surface_blt(Surface *dest, RECT *dest_rect, Surface *src, RECT *src_rect, DWORD flags, BltFx *fx) {
    ++g_surface_calls[5];
    RECT dest_area = {0, 0, dest->width, dest->height};
    if (dest_rect != 0) {
        dest_area = *dest_rect;
    }
    const int32_t dest_width = dest_area.right - dest_area.left;
    const int32_t dest_height = dest_area.bottom - dest_area.top;
    const int32_t dest_bpp = static_cast<int32_t>(dest->format.bits / 8u);
    unsigned char *dest_base = dest->pixels + dest_area.top * dest->pitch + dest_area.left * dest_bpp;
    if (flags & kBltColorFill) {
        if (fx == 0) {
            return kInvalidParams;
        }
        ot_ddraw_fill(dest_base, dest->pitch, dest_width, dest_height, dest_bpp, fx->fill_color);
        mark_dirty(dest);
        return kOk;
    }
    if (src == 0) {
        return kInvalidParams;
    }
    RECT src_area = {0, 0, src->width, src->height};
    if (src_rect != 0) {
        src_area = *src_rect;
    }
    const int32_t src_width = src_area.right - src_area.left;
    const int32_t src_height = src_area.bottom - src_area.top;
    const int32_t src_bpp = static_cast<int32_t>(src->format.bits / 8u);
    if (src_bpp != dest_bpp) {
        ++g_unsupported_blt;
        ot_log("video=sdl: DirectDraw Blt between mixed pixel formats (%u vs %u bits) not supported",
            static_cast<unsigned>(src->format.bits), static_cast<unsigned>(dest->format.bits));
        return kOk;
    }
    const unsigned char *src_base = src->pixels + src_area.top * src->pitch + src_area.left * src_bpp;
    if (src_width == dest_width && src_height == dest_height) {
        ot_ddraw_copy(dest_base, dest->pitch, src_base, src->pitch, dest_width, dest_height, dest_bpp);
    } else {
        ot_ddraw_stretch(dest_base, dest->pitch, dest_width, dest_height, src_base, src->pitch, src_width, src_height,
            dest_bpp);
    }
    mark_dirty(dest);
    return kOk;
}

HRESULT __stdcall surface_blt_fast(Surface *dest, DWORD x, DWORD y, Surface *src, RECT *src_rect, DWORD) {
    ++g_surface_calls[7];
    if (src == 0) {
        return kInvalidParams;
    }
    RECT src_area = {0, 0, src->width, src->height};
    if (src_rect != 0) {
        src_area = *src_rect;
    }
    const int32_t width = src_area.right - src_area.left;
    const int32_t height = src_area.bottom - src_area.top;
    const int32_t dest_bpp = static_cast<int32_t>(dest->format.bits / 8u);
    const int32_t src_bpp = static_cast<int32_t>(src->format.bits / 8u);
    if (dest_bpp != src_bpp) {
        ++g_unsupported_blt;
        return kOk;
    }
    unsigned char *dest_base = dest->pixels + static_cast<int32_t>(y) * dest->pitch + static_cast<int32_t>(x) * dest_bpp;
    const unsigned char *src_base = src->pixels + src_area.top * src->pitch + src_area.left * src_bpp;
    ot_ddraw_copy(dest_base, dest->pitch, src_base, src->pitch, width, height, dest_bpp);
    mark_dirty(dest);
    return kOk;
}

HRESULT __stdcall surface_flip(Surface *surface, Surface *, DWORD) {
    ++g_surface_calls[11];
    if (surface->primary && g_on_primary_flip != 0) {
        g_on_primary_flip();
    }
    return kOk;
}

HRESULT __stdcall surface_get_pixel_format(Surface *surface, PixelFormat *format) {
    ++g_surface_calls[21];
    if (format == 0) {
        return kInvalidParams;
    }
    memcpy(format, &surface->format, sizeof(PixelFormat)); // 0x20 bytes
    return kOk;
}

HRESULT __stdcall surface_get_surface_desc(Surface *surface, SurfaceDesc *desc) {
    ++g_surface_calls[22];
    if (desc == 0 || desc->size != sizeof(SurfaceDesc)) { // the only GetSurfaceDesc caller always sets dwSize 0x7c
        return kInvalidParams;
    }
    memset(desc, 0, sizeof(SurfaceDesc));
    desc->size = sizeof(SurfaceDesc);
    desc->flags = kDescCaps | kDescHeight | kDescWidth | kDescPitch | kDescPixelFormat;
    desc->height = static_cast<DWORD>(surface->height);
    desc->width = static_cast<DWORD>(surface->width);
    desc->pitch = surface->pitch;
    desc->format = surface->format;
    memcpy(desc->caps, surface->caps, sizeof(desc->caps));
    return kOk;
}

HRESULT __stdcall surface_lock(Surface *surface, RECT *rect, SurfaceDesc *desc, DWORD flags, HANDLE) {
    ++g_surface_calls[25];
    if (desc == 0 || desc->size != sizeof(SurfaceDesc)) {
        return kInvalidParams;
    }
    if (surface->lock_flags != 0) {
        return kSurfaceBusy;
    }
    if (surface->primary && (flags & kLockReadOnly) != 0 && !g_suppress_primary_refresh) {
        refresh_primary_from_video(surface);
    }
    RECT area = {0, 0, surface->width, surface->height};
    if (rect != 0) {
        area = *rect;
    }
    const int32_t bytes_per_pixel = static_cast<int32_t>(surface->format.bits / 8u);
    memset(desc, 0, sizeof(SurfaceDesc));
    desc->size = sizeof(SurfaceDesc);
    desc->flags = kDescCaps | kDescHeight | kDescWidth | kDescPitch | kDescPixelFormat;
    desc->height = static_cast<DWORD>(area.bottom - area.top);
    desc->width = static_cast<DWORD>(area.right - area.left);
    desc->pitch = surface->pitch;
    desc->format = surface->format;
    memcpy(desc->caps, surface->caps, sizeof(desc->caps));
    desc->bits = surface->pixels + area.top * surface->pitch + area.left * bytes_per_pixel;
    surface->lock_flags = flags != 0 ? flags : 1u;
    return kOk;
}

HRESULT __stdcall surface_unlock(Surface *surface, RECT *) {
    ++g_surface_calls[32];
    if (!(surface->lock_flags & kLockReadOnly)) {
        mark_dirty(surface);
    }
    surface->lock_flags = 0;
    return kOk;
}

HRESULT __stdcall surface_restore(Surface *) {
    ++g_surface_calls[27];
    return kOk; // never lost
}

template <int Slot> HRESULT __stdcall surface_no0(Surface *) { ++g_surface_calls[Slot]; return kNotImplemented; }
template <int Slot> HRESULT __stdcall surface_no1(Surface *, DWORD) { ++g_surface_calls[Slot]; return kNotImplemented; }
template <int Slot> HRESULT __stdcall surface_no2(Surface *, DWORD, DWORD) {
    ++g_surface_calls[Slot];
    return kNotImplemented;
}
template <int Slot> HRESULT __stdcall surface_no3(Surface *, DWORD, DWORD, DWORD) {
    ++g_surface_calls[Slot];
    return kNotImplemented;
}
template <int Slot> HRESULT __stdcall surface_no4(Surface *, DWORD, DWORD, DWORD, DWORD) {
    ++g_surface_calls[Slot];
    return kNotImplemented;
}
template <int Slot> HRESULT __stdcall surface_no5(Surface *, DWORD, DWORD, DWORD, DWORD, DWORD) {
    ++g_surface_calls[Slot];
    return kNotImplemented;
}

void *const g_surface_vtable[kSurfaceSlots] = {
    OT_SLOT(surface_query_interface),   // 0
    OT_SLOT(surface_add_ref),           // 1
    OT_SLOT(surface_release),           // 2
    OT_SLOT(surface_no1<3>),            // AddAttachedSurface
    OT_SLOT(surface_no1<4>),            // AddOverlayDirtyRect
    OT_SLOT(surface_blt),               // 5
    OT_SLOT(surface_no3<6>),            // BltBatch
    OT_SLOT(surface_blt_fast),          // 7
    OT_SLOT(surface_no2<8>),            // DeleteAttachedSurface
    OT_SLOT(surface_no2<9>),            // EnumAttachedSurfaces
    OT_SLOT(surface_no3<10>),           // EnumOverlayZOrders
    OT_SLOT(surface_flip),              // 11
    OT_SLOT(surface_no2<12>),           // GetAttachedSurface
    OT_SLOT(surface_no1<13>),           // GetBltStatus
    OT_SLOT(surface_no1<14>),           // GetCaps
    OT_SLOT(surface_no1<15>),           // GetClipper
    OT_SLOT(surface_no2<16>),           // GetColorKey
    OT_SLOT(surface_no1<17>),           // GetDC
    OT_SLOT(surface_no1<18>),           // GetFlipStatus
    OT_SLOT(surface_no2<19>),           // GetOverlayPosition
    OT_SLOT(surface_no1<20>),           // GetPalette
    OT_SLOT(surface_get_pixel_format),  // 21
    OT_SLOT(surface_get_surface_desc),  // 22
    OT_SLOT(surface_no2<23>),           // Initialize
    OT_SLOT(surface_no0<24>),           // IsLost
    OT_SLOT(surface_lock),              // 25
    OT_SLOT(surface_no1<26>),           // ReleaseDC
    OT_SLOT(surface_restore),           // 27
    OT_SLOT(surface_no1<28>),           // SetClipper
    OT_SLOT(surface_no2<29>),           // SetColorKey
    OT_SLOT(surface_no2<30>),           // SetOverlayPosition
    OT_SLOT(surface_no1<31>),           // SetPalette
    OT_SLOT(surface_unlock),            // 32
    OT_SLOT(surface_no5<33>),           // UpdateOverlay
    OT_SLOT(surface_no1<34>),           // UpdateOverlayDisplay
    OT_SLOT(surface_no2<35>),           // UpdateOverlayZOrder
    OT_SLOT(surface_no1<36>),           // GetDDInterface
    OT_SLOT(surface_no1<37>),           // PageLock
    OT_SLOT(surface_no1<38>),           // PageUnlock
    OT_SLOT(surface_no2<39>),           // SetSurfaceDesc
    OT_SLOT(surface_no4<40>),           // SetPrivateData
    OT_SLOT(surface_no3<41>),           // GetPrivateData
    OT_SLOT(surface_no1<42>),           // FreePrivateData
    OT_SLOT(surface_no1<43>),           // GetUniquenessValue
    OT_SLOT(surface_no0<44>),           // ChangeUniquenessValue
    OT_SLOT(surface_no1<45>),           // SetPriority
    OT_SLOT(surface_no1<46>),           // GetPriority
    OT_SLOT(surface_no1<47>),           // SetLOD
    OT_SLOT(surface_no1<48>),           // GetLOD
};

#undef OT_SLOT

} // namespace

void *ot_ddraw_create(OtDdrawPresentCallback on_primary_flip) {
    g_ddraw.references = 1;
    g_on_primary_flip = on_primary_flip;
    return &g_ddraw;
}

void ot_ddraw_surface_set_primary(void *object) {
    if (ot_ddraw_is_surface(object)) {
        static_cast<Surface *>(object)->primary = 1;
    }
}

void ot_ddraw_primary_suppress_refresh(int suppress) {
    g_suppress_primary_refresh = suppress;
}

int ot_ddraw_is_surface(const void *object) {
    return object != 0 && *reinterpret_cast<void *const *>(object) == static_cast<const void *>(g_surface_vtable);
}

void ot_ddraw_start(const OtHalVideoBackend *video) {
    g_video = video;
    g_render_thread = GetCurrentThreadId();
}

void ot_ddraw_stop(void) {
    g_video = 0;
    g_render_thread = 0;
    g_pending_destroy_count = 0; // the process exits through D3D_Shutdown/exit() right after
}

void ot_ddraw_frame(void) {
    if (g_video == 0) {
        return;
    }
    for (DWORD index = 0; index < g_pending_destroy_count; ++index) {
        g_video->texture_destroy(g_pending_destroy[index]);
    }
    g_pending_destroy_count = 0;
}

OtHalHandle ot_ddraw_surface_texture(void *object) {
    if (g_video == 0 || !ot_ddraw_is_surface(object)) {
        return OT_HAL_INVALID_HANDLE;
    }
    Surface *surface = static_cast<Surface *>(object);
    if (surface->texture != OT_HAL_INVALID_HANDLE && !surface->dirty) {
        return surface->texture;
    }
    const uint32_t format = ot_ddraw_pixel_format(
        surface->format.bits, surface->format.red, surface->format.green, surface->format.blue, surface->format.alpha);
    if (format == 0) {
        ++g_texture_failures;
        return OT_HAL_INVALID_HANDLE;
    }
    if (surface->texture == OT_HAL_INVALID_HANDLE) {
        const OtHalTextureDesc desc = {surface->width, surface->height, format, OT_HAL_INVALID_HANDLE};
        surface->texture = g_video->texture_create(&desc);
        if (surface->texture == OT_HAL_INVALID_HANDLE) {
            ++g_texture_failures;
            return OT_HAL_INVALID_HANDLE;
        }
        ++g_textures;
    }
    const OtHalRect area = {0, 0, surface->width, surface->height};
    g_video->texture_update(surface->texture, &area, surface->pixels, surface->pitch);
    surface->dirty = 0;
    return surface->texture;
}

int ot_ddraw_describe(char *out, int size) {
    char line[256];
    int length = wsprintfA(line,
        "ddraw surfaces=%u live=%u textures=%u texture_failures=%u unsupported_blt=%u pending_destroy=%u",
        static_cast<unsigned>(g_surfaces_created), static_cast<unsigned>(g_surfaces_live),
        static_cast<unsigned>(g_textures), static_cast<unsigned>(g_texture_failures),
        static_cast<unsigned>(g_unsupported_blt), static_cast<unsigned>(g_pending_destroy_count));
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}
