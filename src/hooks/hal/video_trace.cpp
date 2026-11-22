#include "video_trace.h"

#include "../hook_runtime.h"
#include "video_mirror.h"
#include "video_shadow.h"

#include <string.h>

// Direct3D 7 call recorder for the primitive-stream oracle (video_trace.h).
// File format (little endian), mirrored by tony/hal/video.py:
//
//   frame_NNNNN.otvs  "OTVS" u32 version=1, u32 frame, u32 record_count,
//                     then records: u16 type, u16 flags, u32 payload bytes,
//                     payload (see enum RecordType).
//   *.ottx            "OTTX" u32 version=1, u32 width, u32 height,
//                     u32 bits, u32 r/g/b/a masks, u32 pixel-format flags,
//                     then height rows of width*bits/8 bytes, top down.

namespace {

// IDirect3DDevice7 vtable slots: IUnknown, then tony/hal/com.py order.
enum DeviceSlot {
    kBeginScene = 5,
    kEndScene = 6,
    kGetRenderTarget = 9,
    kClear = 10,
    kSetViewport = 13,
    kSetRenderState = 20,
    kGetRenderState = 21,
    kGetViewport = 15,
    kDrawPrimitive = 25,
    kDrawIndexedPrimitive = 26,
    kDrawPrimitiveVB = 31,
    kDrawIndexedPrimitiveVB = 32,
    kGetTexture = 34,
    kSetTexture = 35,
    kGetTextureStageState = 36,
    kSetTextureStageState = 37,
    kDeviceSlots = 49
};
enum SurfaceSlot { kRelease = 2, kSurfaceLock = 25, kSurfaceUnlock = 32 };
enum VertexBufferSlot { kVbLock = 3, kVbUnlock = 4, kVbGetDesc = 6 };

enum RecordType {
    kRecordBeginScene = 1,
    kRecordEndScene = 2,
    kRecordClear = 3,        // u32 count, flags, color, f32 z, u32 stencil, count x (4 x i32 rect)
    kRecordRenderState = 4,  // u32 state, value
    kRecordStageState = 5,   // u32 stage, type, value
    kRecordTexture = 6,      // u32 stage, u32 status (0 none, 1 hashed, 2 unreadable), u64 hash, u32 surface
    kRecordDraw = 7,         // u32 primitive, fvf, vertex count, flags, stride; vertices
    kRecordDrawIndexed = 8,  // u32 primitive, fvf, vertex count, index count, flags, stride; vertices; u16 indices
    kRecordViewport = 9      // D3DVIEWPORT7: u32 x, y, w, h, f32 min z, max z
};
enum RecordFlags {
    kFromVertexBuffer = 1, // draw: vertices read from a vertex buffer
    kSnapshot = 2          // state/texture/viewport read back at the start of the frame
};

// Device state read back when a traced frame starts, so a frame replays on
// its own: the render states THPS2 sets, and the first two texture stages.
const DWORD kSnapshotRenderStates[] = {2, 3, 4, 7, 8, 9, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 34, 35, 36, 37, 38, 41, 47, 48, 136, 137, 138, 139, 140};
const DWORD kSnapshotStageStates[] = {1, 2, 3, 4, 5, 6, 11, 13, 14, 16, 17, 18};

const DWORD kLockReadOnly = 0x10; // DDLOCK_READONLY
const DWORD kLockWait = 0x01;     // DDLOCK_WAIT

struct PixelFormat {
    DWORD size, flags, fourcc, bits, red, green, blue, alpha;
};

// DDSURFACEDESC2 (the VC6 headers predate DirectX 7).
struct SurfaceDesc {
    DWORD size, flags, height, width;
    LONG pitch;
    DWORD back_buffers, mipmaps, alpha_depth, reserved;
    void *bits;
    DWORD color_keys[8];
    PixelFormat format;
    DWORD caps[4];
    DWORD texture_stage;
};
static_assert(sizeof(SurfaceDesc) == 124, "DDSURFACEDESC2 is 124 bytes");

struct VertexBufferDesc {
    DWORD size, caps, fvf, count;
};

typedef HRESULT(__stdcall *DeviceCall0)(void *);
typedef HRESULT(__stdcall *ClearCall)(void *, DWORD, const LONG *, DWORD, DWORD, float, DWORD);
typedef HRESULT(__stdcall *PointerCall)(void *, const void *);
typedef HRESULT(__stdcall *StateCall)(void *, DWORD, DWORD);
typedef HRESULT(__stdcall *StageStateCall)(void *, DWORD, DWORD, DWORD);
typedef HRESULT(__stdcall *DrawCall)(void *, DWORD, DWORD, const void *, DWORD, DWORD);
typedef HRESULT(__stdcall *DrawIndexedCall)(void *, DWORD, DWORD, const void *, DWORD, const WORD *, DWORD, DWORD);
typedef HRESULT(__stdcall *DrawVbCall)(void *, DWORD, void *, DWORD, DWORD, DWORD);
typedef HRESULT(__stdcall *DrawIndexedVbCall)(void *, DWORD, void *, DWORD, DWORD, const WORD *, DWORD, DWORD);
typedef HRESULT(__stdcall *SetTextureCall)(void *, DWORD, void *);
typedef HRESULT(__stdcall *GetTargetCall)(void *, void **);
typedef HRESULT(__stdcall *GetStateCall)(void *, DWORD, DWORD *);
typedef HRESULT(__stdcall *GetStageStateCall)(void *, DWORD, DWORD, DWORD *);
typedef HRESULT(__stdcall *GetTextureCall)(void *, DWORD, void **);
typedef ULONG(__stdcall *ReleaseCall)(void *);
typedef HRESULT(__stdcall *SurfaceLockCall)(void *, RECT *, SurfaceDesc *, DWORD, HANDLE);
typedef HRESULT(__stdcall *SurfaceUnlockCall)(void *, RECT *);
typedef HRESULT(__stdcall *VbLockCall)(void *, DWORD, void **, DWORD *);
typedef HRESULT(__stdcall *VbUnlockCall)(void *);
typedef HRESULT(__stdcall *VbDescCall)(void *, VertexBufferDesc *);

int g_enabled = 0;
DWORD g_first = 0;
DWORD g_count = 0;
DWORD g_frame = 0; // EndScene calls so far
DWORD g_snapshot_frame = 0xffffffffu; // frame whose start state was recorded
void **g_vtable = 0;
void *g_original[kDeviceSlots];
char g_directory[MAX_PATH];
char g_textures[MAX_PATH];

const DWORD kBufferSize = 48u << 20;
unsigned char *g_buffer = 0;
DWORD g_used = 0;
DWORD g_records = 0;
int g_truncated = 0;

DWORD g_frames_written = 0;
DWORD g_draws = 0;
DWORD g_textures_written = 0;

const DWORD kHashSlots = 16384;
unsigned __int64 g_hashes[kHashSlots];

template <typename T>
T original(int slot) {
    return reinterpret_cast<T>(g_original[slot]);
}

void **vtable_of(void *object) {
    return *reinterpret_cast<void ***>(object);
}

bool in_range() {
    return g_enabled && g_frame >= g_first && g_frame - g_first < g_count;
}

void snapshot(void *device);
void *g_device = 0;

bool recording() {
    if (!in_range()) {
        return false;
    }
    if (g_snapshot_frame != g_frame) {
        g_snapshot_frame = g_frame;
        snapshot(g_device);
    }
    return true;
}

void begin_record(unsigned type, unsigned flags, DWORD payload) {
    if (g_truncated || g_used + 8u + payload > kBufferSize) {
        g_truncated = 1;
        return;
    }
    unsigned char *at = g_buffer + g_used;
    at[0] = static_cast<unsigned char>(type);
    at[1] = static_cast<unsigned char>(type >> 8);
    at[2] = static_cast<unsigned char>(flags);
    at[3] = static_cast<unsigned char>(flags >> 8);
    memcpy(at + 4, &payload, 4);
    g_used += 8u;
    ++g_records;
}

void put(const void *data, DWORD size) {
    if (g_truncated) {
        return;
    }
    memcpy(g_buffer + g_used, data, size);
    g_used += size;
}

void put32(DWORD value) {
    put(&value, 4);
}

bool write_file(const char *path, const void *head, DWORD head_size, const void *body, DWORD body_size) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    WriteFile(file, head, head_size, &written, 0);
    if (body_size != 0) {
        WriteFile(file, body, body_size, &written, 0);
    }
    CloseHandle(file);
    return true;
}

// Stride of a flexible vertex format (2D texture coordinates).
DWORD fvf_stride(DWORD fvf) {
    DWORD stride = 0;
    switch (fvf & 0x00e) {
    case 0x002: stride = 12; break;                   // XYZ
    case 0x004: stride = 16; break;                   // XYZRHW
    case 0x006: case 0x008: case 0x00a: case 0x00c: case 0x00e:
        stride = 12 + 4 * (((fvf & 0x00e) - 0x004) / 2); // XYZB1..B5
        break;
    default: break;
    }
    if (fvf & 0x010) stride += 12; // normal
    if (fvf & 0x020) stride += 4;  // point size / reserved1
    if (fvf & 0x040) stride += 4;  // diffuse
    if (fvf & 0x080) stride += 4;  // specular
    stride += ((fvf >> 8) & 0xf) * 8;
    return stride;
}

unsigned __int64 fnv(unsigned __int64 hash, const void *data, DWORD size) {
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    for (DWORD index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool remember(unsigned __int64 hash) { // true when new
    DWORD slot = static_cast<DWORD>(hash) & (kHashSlots - 1u);
    for (DWORD probe = 0; probe < kHashSlots; ++probe) {
        unsigned __int64 &entry = g_hashes[(slot + probe) & (kHashSlots - 1u)];
        if (entry == hash) {
            return false;
        }
        if (entry == 0) {
            entry = hash;
            return true;
        }
    }
    return false; // table full: stop writing new textures
}

// Locks `surface`, hashes its pixels and, when `path` is set or the hash is
// new, writes it as .ottx. Returns 0 when unreadable.
unsigned __int64 capture_surface(void *surface, const char *path) {
    SurfaceDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    void **vtable = vtable_of(surface);
    if (reinterpret_cast<SurfaceLockCall>(vtable[kSurfaceLock])(surface, 0, &desc, kLockReadOnly | kLockWait, 0) != 0) {
        return 0;
    }
    const DWORD row = desc.width * (desc.format.bits / 8u);
    const DWORD size = row * desc.height;
    DWORD header[12] = {0x5854544fu /* OTTX */, 1u, desc.width, desc.height, desc.format.bits,
        desc.format.red, desc.format.green, desc.format.blue, desc.format.alpha, desc.format.flags, 0u, 0u};
    unsigned __int64 hash = fnv(0xcbf29ce484222325ull, header + 2, 8u * 4u);
    unsigned char *packed = 0;
    if (row != 0 && size <= (16u << 20)) {
        packed = static_cast<unsigned char *>(VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    }
    if (packed != 0) {
        for (DWORD y = 0; y < desc.height; ++y) {
            memcpy(packed + y * row, static_cast<const unsigned char *>(desc.bits) + y * desc.pitch, row);
        }
        hash = fnv(hash, packed, size);
    }
    reinterpret_cast<SurfaceUnlockCall>(vtable[kSurfaceUnlock])(surface, 0);
    if (hash == 0) {
        hash = 1;
    }
    if (packed != 0) {
        char name[MAX_PATH];
        if (path == 0 && remember(hash)) {
            wsprintfA(name, "%s%08X%08X.ottx", g_textures, static_cast<unsigned>(hash >> 32),
                static_cast<unsigned>(hash));
            path = name;
            ++g_textures_written;
        }
        if (path != 0) {
            write_file(path, header, sizeof(header), packed, size);
        }
        VirtualFree(packed, 0, MEM_RELEASE);
    }
    return hash;
}

unsigned __int64 capture_surface(void *surface, const char *path);

void snapshot(void *device) {
    DWORD value;
    for (DWORD index = 0; index < sizeof(kSnapshotRenderStates) / sizeof(kSnapshotRenderStates[0]); ++index) {
        if (original<GetStateCall>(kGetRenderState)(device, kSnapshotRenderStates[index], &value) == 0) {
            begin_record(kRecordRenderState, kSnapshot, 8u);
            put32(kSnapshotRenderStates[index]);
            put32(value);
        }
    }
    for (DWORD stage = 0; stage < 2; ++stage) {
        for (DWORD index = 0; index < sizeof(kSnapshotStageStates) / sizeof(kSnapshotStageStates[0]); ++index) {
            if (original<GetStageStateCall>(kGetTextureStageState)(device, stage, kSnapshotStageStates[index],
                    &value) == 0) {
                begin_record(kRecordStageState, kSnapshot, 12u);
                put32(stage);
                put32(kSnapshotStageStates[index]);
                put32(value);
            }
        }
        void *texture = 0;
        if (original<GetTextureCall>(kGetTexture)(device, stage, &texture) == 0) {
            unsigned __int64 hash = texture != 0 ? capture_surface(texture, 0) : 0;
            begin_record(kRecordTexture, kSnapshot, 20u);
            put32(stage);
            put32(texture == 0 ? 0u : hash != 0 ? 1u : 2u);
            put(&hash, 8);
            put32(static_cast<DWORD>(reinterpret_cast<UINT_PTR>(texture)));
            if (texture != 0) {
                reinterpret_cast<ReleaseCall>(vtable_of(texture)[kRelease])(texture);
            }
        }
    }
    DWORD viewport[6];
    if (original<PointerCall>(kGetViewport)(device, viewport) == 0) {
        begin_record(kRecordViewport, kSnapshot, 24u);
        put(viewport, 24u);
    }
}

void ot_video_trace_census_flush_forward(DWORD frame);

void flush_frame(void *device) {
    char path[MAX_PATH];
    ot_video_trace_census_flush_forward(g_frame);
    wsprintfA(path, "%sframe_%05u.otvh", g_directory, static_cast<unsigned>(g_frame));
    ot_video_shadow_flush(path, g_frame);
    DWORD header[4] = {0x5356544fu /* OTVS */, 1u, g_frame, g_records};
    if (g_truncated) {
        header[1] |= 0x80000000u; // truncated
    }
    wsprintfA(path, "%sframe_%05u.otvs", g_directory, static_cast<unsigned>(g_frame));
    write_file(path, header, sizeof(header), g_buffer, g_used);
    ++g_frames_written;
    void *target = 0;
    if (original<GetTargetCall>(kGetRenderTarget)(device, &target) == 0 && target != 0) {
        wsprintfA(path, "%sframe_%05u.ottx", g_directory, static_cast<unsigned>(g_frame));
        capture_surface(target, path);
        reinterpret_cast<ReleaseCall>(vtable_of(target)[kRelease])(target);
    }
    g_used = 0;
    g_records = 0;
    g_truncated = 0;
}

HRESULT __stdcall hook_BeginScene(void *device) {
    g_device = device;
    ot_video_mirror_begin();
    if (recording()) {
        begin_record(kRecordBeginScene, 0, 0);
    }
    return original<DeviceCall0>(kBeginScene)(device);
}

HRESULT __stdcall hook_EndScene(void *device) {
    g_device = device;
    const HRESULT result = original<DeviceCall0>(kEndScene)(device);
    ot_video_mirror_present(device);
    if (recording()) {
        begin_record(kRecordEndScene, 0, 0);
        flush_frame(device);
    }
    ++g_frame;
    return result;
}

HRESULT __stdcall hook_Clear(void *device, DWORD count, const LONG *rects, DWORD flags, DWORD color, float z,
    DWORD stencil) {
    if (recording()) {
        const DWORD rect_count = rects != 0 ? count : 0;
        begin_record(kRecordClear, 0, 20u + rect_count * 16u);
        put32(count);
        put32(flags);
        put32(color);
        put(&z, 4);
        put32(stencil);
        if (rect_count != 0) {
            put(rects, rect_count * 16u);
        }
    }
    if (ot_video_mirror_enabled()) {
        const uint32_t hal = ((flags & 1u) ? OT_HAL_CLEAR_COLOR : 0u) | ((flags & 2u) ? OT_HAL_CLEAR_DEPTH : 0u);
        if (rects == 0 || count == 0) {
            ot_video_mirror_clear(hal, color, z, 0);
        }
        for (DWORD index = 0; rects != 0 && index < count; ++index) {
            const LONG *rect = rects + index * 4u;
            OtHalRect area = {rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]};
            ot_video_mirror_clear(hal, color, z, &area);
        }
    }
    return original<ClearCall>(kClear)(device, count, rects, flags, color, z, stencil);
}

HRESULT __stdcall hook_SetViewport(void *device, const void *viewport) {
    if (recording() && viewport != 0) {
        begin_record(kRecordViewport, 0, 24u);
        put(viewport, 24u);
    }
    if (viewport != 0 && ot_video_mirror_enabled()) {
        const DWORD *fields = static_cast<const DWORD *>(viewport);
        OtHalRect area = {static_cast<int32_t>(fields[0]), static_cast<int32_t>(fields[1]),
            static_cast<int32_t>(fields[2]), static_cast<int32_t>(fields[3])};
        ot_video_mirror_viewport(&area);
    }
    return original<PointerCall>(kSetViewport)(device, viewport);
}

HRESULT __stdcall hook_SetRenderState(void *device, DWORD state, DWORD value) {
    if (recording()) {
        begin_record(kRecordRenderState, 0, 8u);
        put32(state);
        put32(value);
    }
    return original<StateCall>(kSetRenderState)(device, state, value);
}

HRESULT __stdcall hook_SetTextureStageState(void *device, DWORD stage, DWORD type, DWORD value) {
    if (recording()) {
        begin_record(kRecordStageState, 0, 12u);
        put32(stage);
        put32(type);
        put32(value);
    }
    return original<StageStateCall>(kSetTextureStageState)(device, stage, type, value);
}

HRESULT __stdcall hook_SetTexture(void *device, DWORD stage, void *texture) {
    if (recording()) {
        unsigned __int64 hash = 0;
        DWORD status = 0;
        if (texture != 0) {
            hash = capture_surface(texture, 0);
            status = hash != 0 ? 1u : 2u;
        }
        begin_record(kRecordTexture, 0, 20u);
        put32(stage);
        put32(status);
        put(&hash, 8);
        put32(static_cast<DWORD>(reinterpret_cast<UINT_PTR>(texture)));
    }
    return original<SetTextureCall>(kSetTexture)(device, stage, texture);
}

void record_draw(unsigned flags, DWORD primitive, DWORD fvf, const void *vertices, DWORD vertex_count,
    const WORD *indices, DWORD index_count) {
    const DWORD stride = fvf_stride(fvf);
    const DWORD vertex_bytes = vertices != 0 ? stride * vertex_count : 0;
    ++g_draws;
    if (indices == 0) {
        begin_record(kRecordDraw, flags, 20u + vertex_bytes);
        put32(primitive);
        put32(fvf);
        put32(vertex_count);
        put32(0);
        put32(stride);
    } else {
        begin_record(kRecordDrawIndexed, flags, 24u + vertex_bytes + index_count * 2u);
        put32(primitive);
        put32(fvf);
        put32(vertex_count);
        put32(index_count);
        put32(0);
        put32(stride);
    }
    if (vertex_bytes != 0) {
        put(vertices, vertex_bytes);
    }
    if (indices != 0 && index_count != 0) {
        put(indices, index_count * 2u);
    }
}

HRESULT __stdcall hook_DrawPrimitive(void *device, DWORD primitive, DWORD fvf, const void *vertices, DWORD count,
    DWORD flags) {
    if (recording()) {
        record_draw(0, primitive, fvf, vertices, count, 0, 0);
    }
    return original<DrawCall>(kDrawPrimitive)(device, primitive, fvf, vertices, count, flags);
}

HRESULT __stdcall hook_DrawIndexedPrimitive(void *device, DWORD primitive, DWORD fvf, const void *vertices,
    DWORD vertex_count, const WORD *indices, DWORD index_count, DWORD flags) {
    if (recording()) {
        record_draw(0, primitive, fvf, vertices, vertex_count, indices, index_count);
    }
    return original<DrawIndexedCall>(kDrawIndexedPrimitive)(device, primitive, fvf, vertices, vertex_count, indices,
        index_count, flags);
}

// Vertex buffers: record the vertices the draw reads.
void record_vertex_buffer(void *buffer, DWORD primitive, DWORD start, DWORD count, const WORD *indices,
    DWORD index_count) {
    void **vtable = vtable_of(buffer);
    VertexBufferDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    void *data = 0;
    DWORD size = 0;
    if (reinterpret_cast<VbDescCall>(vtable[kVbGetDesc])(buffer, &desc) != 0 ||
        reinterpret_cast<VbLockCall>(vtable[kVbLock])(buffer, kLockReadOnly | kLockWait, &data, &size) != 0) {
        record_draw(kFromVertexBuffer, primitive, 0, 0, count, indices, index_count);
        return;
    }
    const DWORD stride = fvf_stride(desc.fvf);
    record_draw(kFromVertexBuffer, primitive, desc.fvf, static_cast<const unsigned char *>(data) + start * stride,
        count, indices, index_count);
    reinterpret_cast<VbUnlockCall>(vtable[kVbUnlock])(buffer);
}

HRESULT __stdcall hook_DrawPrimitiveVB(void *device, DWORD primitive, void *buffer, DWORD start, DWORD count,
    DWORD flags) {
    if (recording() && buffer != 0) {
        record_vertex_buffer(buffer, primitive, start, count, 0, 0);
    }
    return original<DrawVbCall>(kDrawPrimitiveVB)(device, primitive, buffer, start, count, flags);
}

HRESULT __stdcall hook_DrawIndexedPrimitiveVB(void *device, DWORD primitive, void *buffer, DWORD start,
    DWORD count, const WORD *indices, DWORD index_count, DWORD flags) {
    if (recording() && buffer != 0) {
        record_vertex_buffer(buffer, primitive, start, count, indices, index_count);
    }
    return original<DrawIndexedVbCall>(kDrawIndexedPrimitiveVB)(device, primitive, buffer, start, count, indices,
        index_count, flags);
}

void patch_slot(int slot, void *hook) {
    DWORD old_protect;
    DWORD ignored;
    g_original[slot] = g_vtable[slot];
    VirtualProtect(&g_vtable[slot], sizeof(void *), PAGE_EXECUTE_READWRITE, &old_protect);
    g_vtable[slot] = hook;
    VirtualProtect(&g_vtable[slot], sizeof(void *), old_protect, &ignored);
}

unsigned parse_number(const char *&at) {
    unsigned value = 0;
    while (*at >= '0' && *at <= '9') {
        value = value * 10u + static_cast<unsigned>(*at++ - '0');
    }
    return value;
}

} // namespace

void ot_video_trace_configure(void) {
    char value[64];
    const DWORD length = GetEnvironmentVariableA("OPENTONY_VIDEO_TRACE", value, sizeof(value));
    if (length == 0 || length >= sizeof(value)) {
        return;
    }
    const char *at = value;
    g_first = parse_number(at);
    g_count = 1;
    if (*at == ':') {
        ++at;
        g_count = parse_number(at);
    }
    if (g_count == 0) {
        return;
    }
    ot_output_path(g_directory, "video\\");
    CreateDirectoryA(g_directory, 0);
    lstrcpynA(g_textures, g_directory, MAX_PATH - 16);
    lstrcatA(g_textures, "textures\\");
    CreateDirectoryA(g_textures, 0);
    g_buffer = static_cast<unsigned char *>(VirtualAlloc(0, kBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_enabled = g_buffer != 0;
    ot_log("video trace: frames %u..%u to %s", static_cast<unsigned>(g_first),
        static_cast<unsigned>(g_first + g_count - 1u), g_directory);
}

int ot_video_trace_enabled(void) {
    return g_enabled || ot_video_mirror_enabled();
}

void ot_video_trace_attach(void *device) {
    if ((!g_enabled && !ot_video_mirror_enabled()) || device == 0) {
        return;
    }
    void **vtable = vtable_of(device);
    if (vtable == g_vtable) {
        return; // same device class: already patched
    }
    if (g_vtable != 0) {
        ot_log("video trace: a second device vtable %08X; not traced", static_cast<unsigned>(reinterpret_cast<UINT_PTR>(vtable)));
        return;
    }
    g_vtable = vtable;
    g_device = device;
    g_original[kGetRenderState] = vtable[kGetRenderState];
    g_original[kGetViewport] = vtable[kGetViewport];
    g_original[kGetTexture] = vtable[kGetTexture];
    g_original[kGetTextureStageState] = vtable[kGetTextureStageState];
    patch_slot(kBeginScene, reinterpret_cast<void *>(&hook_BeginScene));
    patch_slot(kEndScene, reinterpret_cast<void *>(&hook_EndScene));
    patch_slot(kClear, reinterpret_cast<void *>(&hook_Clear));
    patch_slot(kSetViewport, reinterpret_cast<void *>(&hook_SetViewport));
    patch_slot(kSetRenderState, reinterpret_cast<void *>(&hook_SetRenderState));
    patch_slot(kSetTextureStageState, reinterpret_cast<void *>(&hook_SetTextureStageState));
    patch_slot(kSetTexture, reinterpret_cast<void *>(&hook_SetTexture));
    patch_slot(kDrawPrimitive, reinterpret_cast<void *>(&hook_DrawPrimitive));
    patch_slot(kDrawIndexedPrimitive, reinterpret_cast<void *>(&hook_DrawIndexedPrimitive));
    patch_slot(kDrawPrimitiveVB, reinterpret_cast<void *>(&hook_DrawPrimitiveVB));
    patch_slot(kDrawIndexedPrimitiveVB, reinterpret_cast<void *>(&hook_DrawIndexedPrimitiveVB));
    g_original[kGetRenderTarget] = vtable[kGetRenderTarget];
    ot_log("video trace: device %08X vtable %08X patched", static_cast<unsigned>(reinterpret_cast<UINT_PTR>(device)),
        static_cast<unsigned>(reinterpret_cast<UINT_PTR>(vtable)));
}

// Ordering-table census for traced frames: how many packets of each command
// byte D3D_DrawPolygonList sees (logged at the frame's EndScene).
namespace {
DWORD g_census[256];
DWORD g_census_lists = 0;
}

void ot_video_trace_draw_list(const void *list) {
    const int traced = in_range();
    if (!traced) {
        if (ot_video_mirror_enabled()) {
            ot_video_shadow_list(list, 0, OT_VIDEO_SHADOW_MIRROR);
        }
        return;
    }
    ++g_census_lists;
    ot_video_shadow_list(list, 1, ot_video_mirror_enabled() ? OT_VIDEO_SHADOW_MIRROR : OT_VIDEO_SHADOW_RECORD_ONLY);
    for (const unsigned char *packet = static_cast<const unsigned char *>(list); packet != 0;
         packet = *reinterpret_cast<const unsigned char *const *>(packet)) {
        if (*reinterpret_cast<const DWORD *>(packet + 4) == 0) {
            continue;
        }
        ++g_census[packet[7]];
    }
}

void ot_video_trace_census_flush(DWORD frame) {
    char line[1024];
    int length = wsprintfA(line, "video census frame %u: %u lists;", static_cast<unsigned>(frame),
        static_cast<unsigned>(g_census_lists));
    for (int code = 0; code < 256 && length < 960; ++code) {
        if (g_census[code] != 0) {
            length += wsprintfA(line + length, " %02X:%u", code, static_cast<unsigned>(g_census[code]));
        }
    }
    ot_log("%s", line);
    memset(g_census, 0, sizeof(g_census));
    g_census_lists = 0;
}

int ot_video_trace_describe(char *out, int size) {
    char line[256];
    int length = wsprintfA(line, "video_trace frames_seen=%u frames_written=%u draws=%u textures=%u ",
        static_cast<unsigned>(g_frame), static_cast<unsigned>(g_frames_written), static_cast<unsigned>(g_draws),
        static_cast<unsigned>(g_textures_written));
    length += ot_video_shadow_describe(line + length, static_cast<int>(sizeof(line)) - length);
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}

namespace {
void ot_video_trace_census_flush_forward(DWORD frame) {
    ot_video_trace_census_flush(frame);
}
} // namespace
