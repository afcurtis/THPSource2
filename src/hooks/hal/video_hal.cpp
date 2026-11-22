#include "video_hal.h"

#include "hal_adapters.h"
#include "video_renderer.h"
#include "video_shadow.h"
#include "video_surface.h"
#include "../hook_runtime.h"
#include "../../hal/oracle/d3d7_stream.h"
#include "../../hal/sdl/sdl_api.h"
#include "../../hal/sdl/sdl_backends.h"
#include "../../hal/sdl/sdl_video.h"
#include "../../hal/sdl/sdl_video_gpu.h"

#include <string.h>

// video=sdl adapters (video_hal.h). Retail layouts: DirectDraw 7 and
// Direct3D 7 vtables in tony/hal/com.py order, DDSURFACEDESC2, DDPIXELFORMAT.

namespace {

// ------------------------------------------------------------ retail state

typedef void(__cdecl *VoidCall)(void);
typedef void(__cdecl *IntCall)(int);
typedef unsigned(__cdecl *ControlFpCall)(unsigned, unsigned);

// D3DTextureCaps: what WINMAIN_InitDirect3D derives from the device caps.
enum TextureCap { kPow2 = 0, kSquareOnly = 1, kMaxWidth = 2, kMaxHeight = 3, kMinWidth = 4, kMinHeight = 5 };

template <typename T>
T &global(int symbol) {
    return *reinterpret_cast<T *>(ot_hal_data(symbol));
}

// ------------------------------------------------------------- DirectDraw

enum DirectDrawSlot { kDdCreateSurface = 6 };
enum SurfaceSlot { kSurfaceAddRef = 1, kSurfaceRelease = 2 };

const DWORD kDescCaps = 0x1, kDescHeight = 0x2, kDescWidth = 0x4, kDescPixelFormat = 0x1000;
const DWORD kCapsOffscreenPlain = 0x40, kCapsSystemMemory = 0x800;
const DWORD kPixelRgb = 0x40, kPixelAlpha = 0x1;

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

typedef ULONG(__stdcall *ReleaseCall)(void *);
typedef HRESULT(__stdcall *CreateSurfaceCall)(void *, SurfaceDesc *, void **, void *);

void **vtable_of(void *object) {
    return *reinterpret_cast<void ***>(object);
}

// ------------------------------------------------------------------ state

enum BackendKind { kBackendNone = 0, kBackendGpu = 1, kBackendGl = 2 };

const OtHalVideoBackend *g_video = 0;
BackendKind g_backend_kind = kBackendNone;
int g_prepared = 0;
DWORD g_presented = 0;
DWORD g_list_walks = 0;
DWORD g_direct_draws = 0;
DWORD g_unsupported = 0;
DWORD g_mode_switches = 0;
DWORD g_movie_frames = 0;
DWORD g_dump_first = 0;
DWORD g_dump_count = 0;
DWORD g_dump_step = 1;
DWORD g_dumped = 0;
char g_dump_directory[MAX_PATH];

void fatal(const char *what) {
    ot_log("video=sdl: %s; cannot continue", what);
    MessageBoxA(0, what, "THPSource2 video=sdl", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

const char *current_video_error() {
    return g_backend_kind == kBackendGpu ? ot_hal_sdl_video_gpu_error() : ot_hal_sdl_video_error();
}

const char *backend_label() {
    return g_backend_kind == kBackendGpu ? "SDL_GPU (vulkan)" : "SDL GL 3.3";
}

// ------------------------------------------------- stand-in IDirect3DDevice7

const DWORD kDeviceSlots = 49;
DWORD g_calls[kDeviceSlots]; // per vtable slot, for the stats line
LONG g_references = 0;
uint32_t g_render[256];      // D3DRENDERSTATETYPE
uint32_t g_stage[32];        // texture stage 0, D3DTEXTURESTAGESTATETYPE
DWORD g_texture = 0;         // stage 0 surface
DWORD g_viewport[6];         // D3DVIEWPORT7

const uint32_t kMaxVertices = 16384;
OtHalVertex g_vertices[kMaxVertices];

struct Device {
    void *const *vtable;
};
extern void *const g_device_vtable[kDeviceSlots];
Device g_device = {g_device_vtable};

const HRESULT kOk = 0;
const HRESULT kNotImplemented = static_cast<HRESULT>(0x80004001L);
const HRESULT kNoInterface = static_cast<HRESULT>(0x80004002L);

void reset_device_state() {
    memset(g_render, 0, sizeof(g_render));
    memset(g_stage, 0, sizeof(g_stage));
    // Direct3D 7 defaults for the states the translation reads.
    g_render[4] = 1;  // TEXTUREPERSPECTIVE
    g_render[7] = 1;  // ZENABLE (a depth buffer is attached)
    g_render[9] = 2;  // SHADEMODE gouraud
    g_render[14] = 1; // ZWRITEENABLE
    g_render[19] = 2; // SRCBLEND one
    g_render[20] = 1; // DESTBLEND zero
    g_render[22] = 3; // CULLMODE ccw
    g_render[23] = 4; // ZFUNC lessequal
    g_render[25] = 8; // ALPHAFUNC always
    g_stage[1] = 4;   // COLOROP modulate
    g_stage[2] = 2;   // COLORARG1 texture
    g_stage[3] = 1;   // COLORARG2 current
    g_stage[4] = 2;   // ALPHAOP selectarg1
    g_stage[5] = 2;   // ALPHAARG1 texture
    g_stage[6] = 1;   // ALPHAARG2 current
    g_stage[12] = g_stage[13] = g_stage[14] = 1; // ADDRESS(U/V) wrap
    g_stage[16] = g_stage[17] = g_stage[18] = 1; // MAG/MIN point, MIP none
    g_texture = 0;
    memset(g_viewport, 0, sizeof(g_viewport));
}

void submit(DWORD primitive, DWORD fvf, const void *vertices, DWORD vertex_count, const WORD *indices,
    DWORD index_count) {
    const uint32_t topology = ot_d3d7_topology(primitive);
    const uint32_t emitted = indices != 0 ? index_count : vertex_count;
    if (g_video == 0 || vertices == 0 || topology == 0xffffffffu || emitted == 0 || emitted > kMaxVertices) {
        ++g_unsupported;
        return;
    }
    const uint32_t stride = ot_d3d7_fvf_stride(fvf);
    const uint8_t *base = static_cast<const uint8_t *>(vertices);
    for (uint32_t index = 0; index < emitted; ++index) {
        const uint32_t source = indices != 0 ? indices[index] : index;
        if (source >= vertex_count || !ot_d3d7_convert_vertex(base + source * stride, fvf, &g_vertices[index])) {
            ++g_unsupported;
            return;
        }
    }
    OtHalDrawState state;
    ot_d3d7_draw_state(g_render, g_stage, ((fvf >> 8) & 0xf) != 0 ? g_texture : OT_HAL_INVALID_HANDLE, &state);
    ot_video_renderer_draw(&state, topology, g_vertices, emitted);
    ++g_direct_draws;
}

HRESULT __stdcall device_query_interface(Device *, const GUID *, void **out) {
    ++g_calls[0];
    if (out != 0) {
        *out = 0;
    }
    return kNoInterface;
}

ULONG __stdcall device_add_ref(Device *) {
    ++g_calls[1];
    return static_cast<ULONG>(++g_references);
}

ULONG __stdcall device_release(Device *) {
    ++g_calls[2];
    if (g_references > 0) {
        --g_references;
    }
    return static_cast<ULONG>(g_references);
}

typedef HRESULT(__stdcall *FormatCallback)(PixelFormat *, void *);

// The 16-bit formats Direct3D 7 devices offer; D3DTex_Init picks ARGB1555.
HRESULT __stdcall device_enum_texture_formats(Device *, FormatCallback callback, void *context) {
    ++g_calls[4];
    static const PixelFormat kFormats[] = {
        {sizeof(PixelFormat), kPixelRgb | kPixelAlpha, 0, 16, 0x7c00u, 0x03e0u, 0x001fu, 0x8000u},
        {sizeof(PixelFormat), kPixelRgb | kPixelAlpha, 0, 16, 0x0f00u, 0x00f0u, 0x000fu, 0xf000u},
        {sizeof(PixelFormat), kPixelRgb, 0, 16, 0xf800u, 0x07e0u, 0x001fu, 0},
    };
    for (unsigned index = 0; callback != 0 && index < sizeof(kFormats) / sizeof(kFormats[0]); ++index) {
        PixelFormat format = kFormats[index];
        if (callback(&format, context) == 0) { // D3DENUMRET_CANCEL
            break;
        }
    }
    return kOk;
}

HRESULT __stdcall device_begin_scene(Device *) {
    ++g_calls[5];
    if (g_video != 0) {
        ot_video_renderer_frame();
        ot_ddraw_frame();
        g_video->begin_frame();
    }
    return kOk;
}

HRESULT __stdcall device_end_scene(Device *) {
    ++g_calls[6];
    return kOk; // D3D_PresentFrame presents
}

HRESULT __stdcall device_get_render_target(Device *, void **out) {
    ++g_calls[9];
    void *target = *reinterpret_cast<void **>(ot_hal_data(OT_HAL_SYM_D3DBackBuffer));
    if (out == 0 || target == 0) {
        return kNotImplemented;
    }
    reinterpret_cast<ReleaseCall>(vtable_of(target)[kSurfaceAddRef])(target);
    *out = target;
    return kOk;
}

HRESULT __stdcall device_clear(Device *, DWORD count, const LONG *rects, DWORD flags, DWORD color, float z, DWORD) {
    ++g_calls[10];
    if (g_video == 0) {
        return kOk;
    }
    const uint32_t hal = ((flags & 1u) ? OT_HAL_CLEAR_COLOR : 0u) | ((flags & 2u) ? OT_HAL_CLEAR_DEPTH : 0u);
    if (rects == 0 || count == 0) {
        g_video->clear(hal, color, z, 0);
    }
    for (DWORD index = 0; rects != 0 && index < count; ++index) {
        const LONG *rect = rects + index * 4u;
        const OtHalRect area = {rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]};
        g_video->clear(hal, color, z, &area);
    }
    return kOk;
}

HRESULT __stdcall device_set_viewport(Device *, const DWORD *viewport) {
    ++g_calls[13];
    if (viewport == 0) {
        return kNotImplemented;
    }
    memcpy(g_viewport, viewport, sizeof(g_viewport));
    if (g_video != 0) {
        const OtHalRect area = {static_cast<int32_t>(viewport[0]), static_cast<int32_t>(viewport[1]),
            static_cast<int32_t>(viewport[2]), static_cast<int32_t>(viewport[3])};
        g_video->set_viewport(&area);
    }
    return kOk;
}

HRESULT __stdcall device_get_viewport(Device *, DWORD *viewport) {
    ++g_calls[15];
    if (viewport != 0) {
        memcpy(viewport, g_viewport, sizeof(g_viewport));
    }
    return kOk;
}

HRESULT __stdcall device_set_render_state(Device *, DWORD state, DWORD value) {
    ++g_calls[20];
    if (state < 256) {
        g_render[state] = value;
    }
    return kOk;
}

HRESULT __stdcall device_get_render_state(Device *, DWORD state, DWORD *value) {
    ++g_calls[21];
    if (value != 0) {
        *value = state < 256 ? g_render[state] : 0;
    }
    return kOk;
}

HRESULT __stdcall device_draw_primitive(Device *, DWORD primitive, DWORD fvf, const void *vertices, DWORD count, DWORD) {
    ++g_calls[25];
    submit(primitive, fvf, vertices, count, 0, 0);
    return kOk;
}

HRESULT __stdcall device_draw_indexed_primitive(Device *, DWORD primitive, DWORD fvf, const void *vertices,
    DWORD vertex_count, const WORD *indices, DWORD index_count, DWORD) {
    ++g_calls[26];
    submit(primitive, fvf, vertices, vertex_count, indices, index_count);
    return kOk;
}

HRESULT __stdcall device_get_texture(Device *, DWORD stage, void **out) {
    ++g_calls[34];
    if (out == 0) {
        return kNotImplemented;
    }
    void *texture = stage == 0 ? reinterpret_cast<void *>(static_cast<UINT_PTR>(g_texture)) : 0;
    if (texture != 0) {
        reinterpret_cast<ReleaseCall>(vtable_of(texture)[kSurfaceAddRef])(texture);
    }
    *out = texture;
    return kOk;
}

HRESULT __stdcall device_set_texture(Device *, DWORD stage, void *texture) {
    ++g_calls[35];
    if (stage == 0) {
        g_texture = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(texture));
    }
    return kOk;
}

HRESULT __stdcall device_get_stage_state(Device *, DWORD stage, DWORD type, DWORD *value) {
    ++g_calls[36];
    if (value != 0) {
        *value = stage == 0 && type < 32 ? g_stage[type] : 0;
    }
    return kOk;
}

HRESULT __stdcall device_set_stage_state(Device *, DWORD stage, DWORD type, DWORD value) {
    ++g_calls[37];
    if (stage == 0 && type < 32) {
        g_stage[type] = value;
        if (type == 12) { // D3DTSS_ADDRESS sets U and V
            g_stage[13] = g_stage[14] = value;
        }
    }
    return kOk;
}

HRESULT __stdcall device_validate(Device *, DWORD *passes) {
    ++g_calls[38];
    if (passes != 0) {
        *passes = 1;
    }
    return kOk;
}

// Everything else: counted; setters succeed, getters are not implemented.
template <int Slot> HRESULT __stdcall ok0(Device *) { ++g_calls[Slot]; return kOk; }
template <int Slot> HRESULT __stdcall ok1(Device *, DWORD) { ++g_calls[Slot]; return kOk; }
template <int Slot> HRESULT __stdcall ok2(Device *, DWORD, DWORD) { ++g_calls[Slot]; return kOk; }
template <int Slot> HRESULT __stdcall ok5(Device *, DWORD, DWORD, DWORD, DWORD, DWORD) { ++g_calls[Slot]; return kOk; }
template <int Slot> HRESULT __stdcall ok7(Device *, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD) {
    ++g_calls[Slot];
    return kOk;
}
template <int Slot> HRESULT __stdcall no1(Device *, DWORD) { ++g_calls[Slot]; return kNotImplemented; }
template <int Slot> HRESULT __stdcall no2(Device *, DWORD, DWORD) { ++g_calls[Slot]; return kNotImplemented; }
template <int Slot> HRESULT __stdcall no3(Device *, DWORD, DWORD, DWORD) { ++g_calls[Slot]; return kNotImplemented; }
template <int Slot> HRESULT __stdcall no5(Device *, DWORD, DWORD, DWORD, DWORD, DWORD) {
    ++g_calls[Slot];
    return kNotImplemented;
}

#define OT_SLOT(function) reinterpret_cast<void *>(&function)

void *const g_device_vtable[kDeviceSlots] = {
    OT_SLOT(device_query_interface),        // 0
    OT_SLOT(device_add_ref),                // 1
    OT_SLOT(device_release),                // 2
    OT_SLOT(ok1<3>),                        // GetCaps (D3D_InitPrimitiveDispatch ignores the result)
    OT_SLOT(device_enum_texture_formats),   // 4
    OT_SLOT(device_begin_scene),            // 5
    OT_SLOT(device_end_scene),              // 6
    OT_SLOT(no1<7>),                        // GetDirect3D
    OT_SLOT(ok2<8>),                        // SetRenderTarget
    OT_SLOT(device_get_render_target),      // 9
    OT_SLOT(device_clear),                  // 10
    OT_SLOT(ok2<11>),                       // SetTransform
    OT_SLOT(no2<12>),                       // GetTransform
    OT_SLOT(device_set_viewport),           // 13
    OT_SLOT(ok2<14>),                       // MultiplyTransform
    OT_SLOT(device_get_viewport),           // 15
    OT_SLOT(ok1<16>),                       // SetMaterial
    OT_SLOT(no1<17>),                       // GetMaterial
    OT_SLOT(ok2<18>),                       // SetLight
    OT_SLOT(no2<19>),                       // GetLight
    OT_SLOT(device_set_render_state),       // 20
    OT_SLOT(device_get_render_state),       // 21
    OT_SLOT(ok0<22>),                       // BeginStateBlock
    OT_SLOT(no1<23>),                       // EndStateBlock
    OT_SLOT(ok1<24>),                       // PreLoad
    OT_SLOT(device_draw_primitive),         // 25
    OT_SLOT(device_draw_indexed_primitive), // 26
    OT_SLOT(ok1<27>),                       // SetClipStatus
    OT_SLOT(no1<28>),                       // GetClipStatus
    OT_SLOT(ok5<29>),                       // DrawPrimitiveStrided
    OT_SLOT(ok7<30>),                       // DrawIndexedPrimitiveStrided
    OT_SLOT(ok5<31>),                       // DrawPrimitiveVB
    OT_SLOT(ok7<32>),                       // DrawIndexedPrimitiveVB
    OT_SLOT(no5<33>),                       // ComputeSphereVisibility
    OT_SLOT(device_get_texture),            // 34
    OT_SLOT(device_set_texture),            // 35
    OT_SLOT(device_get_stage_state),        // 36
    OT_SLOT(device_set_stage_state),        // 37
    OT_SLOT(device_validate),               // 38
    OT_SLOT(ok1<39>),                       // ApplyStateBlock
    OT_SLOT(ok1<40>),                       // CaptureStateBlock
    OT_SLOT(ok1<41>),                       // DeleteStateBlock
    OT_SLOT(no2<42>),                       // CreateStateBlock
    OT_SLOT(no5<43>),                       // Load
    OT_SLOT(ok2<44>),                       // LightEnable
    OT_SLOT(no2<45>),                       // GetLightEnable
    OT_SLOT(ok2<46>),                       // SetClipPlane
    OT_SLOT(no2<47>),                       // GetClipPlane
    OT_SLOT(no3<48>),                       // GetInfo
};

#undef OT_SLOT

// --------------------------------------------------------------- helpers

void control_fp(unsigned value, unsigned mask) {
    ot_hal_function<ControlFpCall>(OT_HAL_SYM__controlfp)(value, mask);
}

void present_frame();

// Retail flips the primary only in DrawFullScreenMovie (startup movies while
// D3DTextureMode is 0): the movie quad went to the HAL through the stand-in
// device, so present it. The stand-in surface (video_surface.cpp) runs this
// in place of a real Flip only for the surface marked primary below.
void on_primary_flip() {
    present_frame();
    ++g_movie_frames;
}

// The offscreen surface that stands in for the primary and back buffer
// (movies, screenshots and D3D_ClearBuffers use it).
void create_back_buffer(int32_t width, int32_t height) {
    void *directdraw = global<void *>(OT_HAL_SYM_DirectDraw);
    SurfaceDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.flags = kDescCaps | kDescWidth | kDescHeight | kDescPixelFormat;
    desc.width = static_cast<DWORD>(width);
    desc.height = static_cast<DWORD>(height);
    desc.format.size = sizeof(PixelFormat);
    desc.format.flags = kPixelRgb;
    desc.format.bits = 16;
    desc.format.red = 0xf800u;
    desc.format.green = 0x07e0u;
    desc.format.blue = 0x001fu;
    desc.caps[0] = kCapsOffscreenPlain | kCapsSystemMemory;
    void *surface = 0;
    const HRESULT result =
        reinterpret_cast<CreateSurfaceCall>(vtable_of(directdraw)[kDdCreateSurface])(directdraw, &desc, &surface, 0);
    if (result != 0 || surface == 0) {
        fatal("CreateSurface for the offscreen back buffer failed");
    }
    // One surface, one reference: D3D_Shutdown releases the primary only.
    global<void *>(OT_HAL_SYM_D3DPrimarySurface) = surface;
    global<void *>(OT_HAL_SYM_D3DBackBuffer) = surface;
    ot_ddraw_surface_set_primary(surface);
}

void release_back_buffer() {
    void *&primary = global<void *>(OT_HAL_SYM_D3DPrimarySurface);
    if (primary != 0) {
        reinterpret_cast<ReleaseCall>(vtable_of(primary)[kSurfaceRelease])(primary);
    }
    primary = 0;
    global<void *>(OT_HAL_SYM_D3DBackBuffer) = 0;
}

int save_frame(const char *path) {
    OtHalImage image;
    if (g_video == 0 || !g_video->read_back(&image)) {
        return 0;
    }
    // .ottx (video_trace.cpp): 32-bit X8R8G8B8, top down.
    const DWORD header[12] = {0x5854544fu, 1u, static_cast<DWORD>(image.width), static_cast<DWORD>(image.height), 32u,
        0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0u, 0x40u, 0u, 0u};
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD written = 0;
    WriteFile(file, header, sizeof(header), &written, 0);
    for (int32_t y = 0; y < image.height; ++y) {
        WriteFile(file, static_cast<const unsigned char *>(image.pixels) + y * image.pitch,
            static_cast<DWORD>(image.width) * 4u, &written, 0);
    }
    CloseHandle(file);
    return 1;
}

void dump_frame() {
    if (g_dump_count == 0 || g_presented < g_dump_first || (g_presented - g_dump_first) % g_dump_step != 0 ||
        (g_presented - g_dump_first) / g_dump_step >= g_dump_count) {
        return;
    }
    char path[MAX_PATH];
    wsprintfA(path, "%sframe_%05u.hal.ottx", g_dump_directory, static_cast<unsigned>(g_presented));
    g_dumped += static_cast<DWORD>(save_frame(path));
}

int parse_decimal(const char *text) { // the game's atoi, for the mode names
    int value = 0;
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    const bool negative = *text == '-';
    if (*text == '-' || *text == '+') {
        ++text;
    }
    for (; *text >= '0' && *text <= '9'; ++text) {
        value = value * 10 + (*text - '0');
    }
    return negative ? -value : value;
}

// ---------------------------------------------------------------- adapters

void __cdecl hal_WINMAIN_InitDirect3D(void);

// Retail: DirectDrawCreateEx, exclusive full-screen cooperative level, the
// display mode, a flipping primary with one back buffer. Here: the stand-in
// DirectDraw (video_surface.cpp; every surface is a HAL texture, real
// ddraw.dll is never loaded), the offscreen back buffer and the HAL backend
// on the adopted game window.
void __cdecl hal_WINMAIN_InitDirectDraw(void) {
    ot_hal_adopt_main_window(); // the window entry site's job (this replaces its function)
    void *directdraw = ot_ddraw_create(&on_primary_flip);
    global<void *>(OT_HAL_SYM_DirectDraw) = directdraw;
    HWND window = global<HWND>(OT_HAL_SYM_MainWindow);
    global<DWORD>(OT_HAL_SYM_DisplayVideoMemory) = 32u << 20;
    int32_t &width = global<int32_t>(OT_HAL_SYM_ScreenWidth);
    int32_t &height = global<int32_t>(OT_HAL_SYM_ScreenHeight);
    if (width == 0 && height == 0) {
        width = 640;
        height = 480;
    }
    g_video = g_backend_kind == kBackendGpu ? &ot_hal_sdl_video_gpu : &ot_hal_sdl_video_gl;
    OtHalVideoMode mode = {width, height, 32u, 0u, 1u};
    if (!g_video->init(&mode)) {
        const char *error = current_video_error();
        g_video = 0;
        fatal(error);
    }
    ot_video_renderer_start(g_video);
    ot_ddraw_start(g_video);
    create_back_buffer(width, height);
    ot_hal_function<VoidCall>(OT_HAL_SYM_D3D_ClearBuffers)();
    ot_log("video=sdl: DirectDraw is a stand-in (HAL-native textures), %dx%d offscreen back buffer, %s on "
           "MainWindow %08X; video memory %u KB",
        static_cast<int>(width), static_cast<int>(height), backend_label(),
        static_cast<unsigned>(reinterpret_cast<UINT_PTR>(window)), static_cast<unsigned>(32u << 10));
}

// Retail: pick a device and z-buffer, set the texture caps, CreateDevice,
// then the viewport and the initial states. Here the device is the stand-in.
void __cdecl hal_WINMAIN_InitDirect3D(void) {
    DWORD &software = global<DWORD>(OT_HAL_SYM_D3DSoftwareRenderer);
    if (software != 0) {
        ot_log("video=sdl: the software renderer is not available; hardware path");
        software = 0;
    }
    // As retail's device reports them under Wine, the parity reference (a
    // Windows driver may report non-power-of-two textures: pow2 = 0). No FPU
    // set-up either: under Wine CreateDevice leaves the x87 control word as
    // it was (Windows' Direct3D switches to single precision).
    DWORD *caps = &global<DWORD>(OT_HAL_SYM_D3DTextureCaps);
    caps[kPow2] = 1;
    caps[kSquareOnly] = 0;
    caps[kMinWidth] = 1;
    caps[kMinHeight] = 1;
    caps[kMaxWidth] = 0x100;
    caps[kMaxHeight] = 0x100;
    reset_device_state();
    g_references = 1;
    global<Device *>(OT_HAL_SYM_D3DDevice) = &g_device;
    const DWORD viewport[6] = {0, 0, static_cast<DWORD>(global<int32_t>(OT_HAL_SYM_ScreenWidth)),
        static_cast<DWORD>(global<int32_t>(OT_HAL_SYM_ScreenHeight)), 0u, 0x3f800000u};
    device_set_viewport(&g_device, viewport);
    device_set_render_state(&g_device, 22, 1); // CULLMODE none
    device_set_render_state(&g_device, 41, 0); // COLORKEYENABLE
    device_set_render_state(&g_device, 27, 0); // ALPHABLENDENABLE
    device_set_render_state(&g_device, 7, 0);  // ZENABLE
    device_set_stage_state(&g_device, 0, 2, 2);
    device_set_stage_state(&g_device, 0, 3, 0);
    device_set_stage_state(&g_device, 0, 5, 2);
    device_set_stage_state(&g_device, 0, 6, 0);
    device_set_stage_state(&g_device, 0, 12, 1);
    device_set_stage_state(&g_device, 0, 16, 2);
    device_set_stage_state(&g_device, 0, 17, 2);
}

void present_frame() {
    dump_frame();
    g_video->present();
    ++g_presented;
}

// Retail: D3DPOLY_EndScene, EndScene, Flip.
void __cdecl hal_D3D_PresentFrame(void) {
    ot_hal_function<VoidCall>(OT_HAL_SYM_D3DPOLY_EndScene)();
    if (g_video != 0) {
        present_frame();
    }
}

// Retail walks the ordering table and dispatches each packet to the D3D_Draw*
// functions; the adapter flattens it into the same draws (1,306/1,306
// Warehouse frames identical in shadow mode) and leaves the retail state the
// walk leaves behind.
void __cdecl hal_D3D_DrawPolygonList(const void *list) {
    if (global<uint8_t>(OT_HAL_SYM_D3DSceneActive) == 0) {
        return;
    }
    global<uint8_t>(OT_HAL_SYM_D3DSceneListDrawn) = 1;
    control_fp(0x20000u, 0x30000u); // single precision for the walk, as retail
    ot_video_shadow_list(list, 0, OT_VIDEO_SHADOW_SOLE);
    ++g_list_walks;
    control_fp(0x9001fu, 0xfffffu);
}

// Retail: 1 = the display mode chosen in the options, 2 = 640x480 (front end).
// The mode string is cut exactly as retail cuts it. No display mode is set:
// the HAL renders at the new size and presents scaled to the window.
void __cdecl hal_WINMAIN_SwitchResolution(int which) {
    ot_hal_function<VoidCall>(OT_HAL_SYM_WINMAIN_BuildDisplayModeList)();
    int32_t &screen_width = global<int32_t>(OT_HAL_SYM_ScreenWidth);
    int32_t &screen_height = global<int32_t>(OT_HAL_SYM_ScreenHeight);
    int32_t width = 640;
    int32_t height = 480;
    if (which == 2) {
        if (screen_width == 640 && screen_height == 480) {
            return;
        }
    } else if (which == 1) {
        const char *const *names = &global<const char *>(OT_HAL_SYM_DisplayModeNames);
        const char *name = names[global<int32_t>(OT_HAL_SYM_DisplayModeIndex)];
        char text[16];
        memset(text, 0, sizeof(text));
        lstrcpynA(text, name != 0 ? name : "", sizeof(text));
        const int length = lstrlenA(text) + 1;
        if (length >= 5) {
            text[length - 5] = 0;
        }
        width = parse_decimal(text);
        height = parse_decimal(text + lstrlenA(text) + 1);
        if (screen_width == width && screen_height == height) {
            return;
        }
    } else {
        return;
    }
    screen_width = width;
    screen_height = height;
    ot_hal_function<IntCall>(OT_HAL_SYM_D3D_Shutdown)(0);
    const OtHalVideoMode mode = {width, height, 32u, 0u, 1u};
    if (g_video != 0 && !g_video->set_mode(&mode)) {
        fatal(current_video_error());
    }
    ot_hal_function<VoidCall>(OT_HAL_SYM_Game_FrameTick)();
    release_back_buffer();
    create_back_buffer(width, height);
    ot_hal_function<VoidCall>(OT_HAL_SYM_D3D_ClearBuffers)();
    hal_WINMAIN_InitDirect3D();
    ++g_mode_switches;
    ot_log("video=sdl: resolution %dx%d", static_cast<int>(width), static_cast<int>(height));
}

// Retail: 4 = Direct3D, 8 = the software renderer (with `mode`).
void __cdecl hal_WINMAIN_SwitchRenderer(int renderer, int mode) {
    if (renderer == 4) {
        DWORD &software = global<DWORD>(OT_HAL_SYM_D3DSoftwareRenderer);
        const bool restart = software != 0;
        if (restart) {
            ot_hal_function<IntCall>(OT_HAL_SYM_D3D_Shutdown)(0);
            software = 0;
        }
        global<DWORD>(OT_HAL_SYM_D3DConfiguredTextureRootEnabled) = 1;
        if (restart) {
            hal_WINMAIN_InitDirect3D();
        }
    } else if (renderer == 8) {
        ot_log("video=sdl: software renderer (mode %d) requested; not available, staying on the HAL", mode);
    }
}

} // namespace

void ot_video_hal_prepare(void) {
    char value[32];
    const DWORD length = GetEnvironmentVariableA("OPENTONY_VIDEO_TRACE", value, sizeof(value));
    if (length != 0 && length < sizeof(value)) {
        const char *at = value;
        for (; *at >= '0' && *at <= '9'; ++at) {
            g_dump_first = g_dump_first * 10u + static_cast<DWORD>(*at - '0');
        }
        if (*at == ':') {
            for (++at; *at >= '0' && *at <= '9'; ++at) {
                g_dump_count = g_dump_count * 10u + static_cast<DWORD>(*at - '0');
            }
        }
        if (*at == ':') { // an optional step
            DWORD step = 0;
            for (++at; *at >= '0' && *at <= '9'; ++at) {
                step = step * 10u + static_cast<DWORD>(*at - '0');
            }
            g_dump_step = step != 0 ? step : 1u;
        }
        ot_output_path(g_dump_directory, "video\\");
        CreateDirectoryA(g_dump_directory, 0);
        ot_log("video=sdl: %u frames from %u, every %u, go to %sframe_NNNNN.hal.ottx", static_cast<unsigned>(g_dump_count),
            static_cast<unsigned>(g_dump_first), static_cast<unsigned>(g_dump_step), g_dump_directory);
    }
    // The GPU device (if any) is created here, before MainWindow is adopted:
    // the window must be created/adopted with SDL_PROP_WINDOW_CREATE_VULKAN_
    // BOOLEAN or _OPENGL_BOOLEAN set up front (probed under Wine + lavapipe),
    // so the choice has to be made before the window exists.
    const uint32_t setting = ot_hal_config()->video_backend;
    if (setting == OT_HAL_VIDEO_BACKEND_GPU || setting == OT_HAL_VIDEO_BACKEND_AUTO) {
        if (ot_hal_sdl_video_gpu_probe()) {
            g_backend_kind = kBackendGpu;
            ot_hal_sdl_window_request_vulkan(1);
            ot_hal_sdl_window_request_opengl(0);
            ot_log("video=sdl: video_backend=%s picked SDL_GPU (%s)",
                setting == OT_HAL_VIDEO_BACKEND_GPU ? "gpu" : "auto", ot_hal_sdl_video_gpu_driver());
        } else if (setting == OT_HAL_VIDEO_BACKEND_GPU) {
            fatal(ot_hal_sdl_video_gpu_error()); // gpu fails loudly, no fallback
        } else {
            ot_log("video=sdl: video_backend=auto: SDL_GPU unavailable (%s); falling back to GL 3.3",
                ot_hal_sdl_video_gpu_error());
        }
    }
    if (g_backend_kind != kBackendGpu) {
        g_backend_kind = kBackendGl;
        ot_hal_sdl_video_gl_hints();
        ot_hal_sdl_window_request_opengl(1);
        ot_hal_sdl_window_request_vulkan(0);
    }
    g_prepared = 1;
}

void ot_video_hal_stop(void) {
    if (g_video == 0) {
        return;
    }
    ot_ddraw_stop();
    ot_video_renderer_stop();
    g_video->shutdown();
    g_video = 0;
}

int ot_video_hal_running(void) {
    return g_video != 0;
}

const OtHalVideoBackend *ot_video_hal_backend(void) {
    return g_video;
}

int ot_video_hal_save_frame(const char *path) {
    return save_frame(path);
}

int ot_video_hal_describe(char *out, int size) {
    char line[512];
    int length = wsprintfA(line, "video_hal presented=%u lists=%u direct_draws=%u unsupported=%u movie_frames=%u "
                                 "mode_switches=%u dumped=%u ",
        static_cast<unsigned>(g_presented), static_cast<unsigned>(g_list_walks), static_cast<unsigned>(g_direct_draws),
        static_cast<unsigned>(g_unsupported), static_cast<unsigned>(g_movie_frames),
        static_cast<unsigned>(g_mode_switches), static_cast<unsigned>(g_dumped));
    length += ot_video_renderer_describe(line + length, static_cast<int>(sizeof(line)) - length);
    length += wsprintfA(line + length, " ");
    length += ot_ddraw_describe(line + length, static_cast<int>(sizeof(line)) - length);
    length += wsprintfA(line + length, " device_calls");
    for (DWORD slot = 0; slot < kDeviceSlots && length < static_cast<int>(sizeof(line)) - 16; ++slot) {
        if (g_calls[slot] != 0) {
            length += wsprintfA(line + length, " %u:%u", static_cast<unsigned>(slot), static_cast<unsigned>(g_calls[slot]));
        }
    }
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}

OT_REGISTER_HAL_ADAPTER(WINMAIN_InitDirectDraw, hal_WINMAIN_InitDirectDraw)
OT_REGISTER_HAL_ADAPTER(WINMAIN_InitDirect3D, hal_WINMAIN_InitDirect3D)
OT_REGISTER_HAL_ADAPTER(D3D_PresentFrame, hal_D3D_PresentFrame)
OT_REGISTER_HAL_ADAPTER(D3D_DrawPolygonList, hal_D3D_DrawPolygonList)
OT_REGISTER_HAL_ADAPTER(WINMAIN_SwitchResolution, hal_WINMAIN_SwitchResolution)
OT_REGISTER_HAL_ADAPTER(WINMAIN_SwitchRenderer, hal_WINMAIN_SwitchRenderer)
