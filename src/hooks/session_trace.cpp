#include "session_trace.h"

#include "clock.h"
#include "hook_runtime.h"
#include "oracle_script.h"
#include "session_hash.h"

#include <string.h>

// Session probe: see session_trace.h. Everything here targets the canonical
// build's addresses and is a test switch, like the lockstep clock it rides on.

namespace {

const uint32_t kPollCall = 0x004e4a95;         // PCInput_Poll: call 0x004ec5c0 (the keyboard device update)
const uint32_t kPollCallTarget = 0x004ec5c0;
const unsigned char kPollCallBytes[5] = {0xe8, 0x26, 0x7b, 0x00, 0x00};
const uint32_t kKeyboardState = 0x006a43e4;    // 256 DirectInput key bytes, bit 7 = down
const uint32_t kMovieEntries[2] = {0x004e5ec0, 0x004e6590};
const uint32_t kBlockSize = 64u * 1024u;
const uint32_t kMaxKeys = 64;
const uint32_t kMaxRegions = 8;
const uint32_t kMaxMasks = 32;
const uint32_t kZoneBounds = 0x0056a97c; // begin, end of zone 0, then of zone 1 (Mem_Init)
const char kMagic[8] = {'O', 'T', 'S', 'E', 'S', 'S', '1', 0};

struct KeyEvent {
    uint32_t poll;
    uint32_t dik;
    uint32_t hold;
};

struct Region {
    uint32_t base;
    uint32_t size;
};

int g_enabled = 0;
uint32_t g_limit = 0;
uint32_t g_every = 2;
uint32_t g_dump = 0xffffffffu;
uint32_t g_dump_again = 0xffffffffu; // 7c: OPENTONY_SESSION_DUMP=<first>,<second> dumps at two deliveries of one run
uint32_t g_script_at = 0xffffffffu;
KeyEvent g_keys[kMaxKeys];
uint32_t g_key_count = 0;
Region g_regions[kMaxRegions];
uint32_t g_region_count = 0;
uint32_t g_block_count = 0;
volatile uint32_t g_polls = 0;
HANDLE g_file = INVALID_HANDLE_VALUE;
uint32_t *g_record = 0; // delivery, poll, then one hash per block
uint32_t g_normalize = 1;
Region g_masks[kMaxMasks]; // words overlapping these ranges hash as zero
uint32_t g_mask_count = 0;
uint32_t *g_scratch = 0;   // one block, for hashing a masked copy

int env_text(const char *name, char *out, DWORD size) {
    const DWORD length = GetEnvironmentVariableA(name, out, size);
    return length != 0 && length < size;
}

const char *parse_number(const char *text, uint32_t base, uint32_t *value) {
    uint32_t result = 0;
    int digits = 0;
    for (;; ++text, ++digits) {
        const char c = *text;
        uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a' + 10);
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A' + 10);
        } else {
            break;
        }
        result = result * base + digit;
    }
    *value = digits != 0 ? result : 0;
    return digits != 0 ? text : 0;
}

uint32_t env_number(const char *name, uint32_t fallback) {
    char text[32];
    uint32_t value;
    if (env_text(name, text, sizeof(text)) && parse_number(text, 10, &value) != 0) {
        return value;
    }
    return fallback;
}

// <poll>:<dik hex>:<hold>[,...]
void parse_keys(const char *text) {
    while (*text != 0 && g_key_count < kMaxKeys) {
        KeyEvent event;
        text = parse_number(text, 10, &event.poll);
        if (text == 0 || *text != ':') break;
        text = parse_number(text + 1, 16, &event.dik);
        if (text == 0 || *text != ':') break;
        text = parse_number(text + 1, 10, &event.hold);
        if (text == 0) break;
        g_keys[g_key_count++] = event;
        if (*text == ',') ++text;
    }
}

// <addr hex>:<length hex>[,...]
void parse_masks(const char *text) {
    while (*text != 0 && g_mask_count < kMaxMasks) {
        Region mask;
        text = parse_number(text, 16, &mask.base);
        if (text == 0 || *text != ':') break;
        text = parse_number(text + 1, 16, &mask.size);
        if (text == 0) break;
        g_masks[g_mask_count++] = mask;
        if (*text == ',') ++text;
    }
}

// The block hashes (session_hash.h): the standalone's recorder hashes with the same functions.
uint32_t hash_block(const uint32_t *words, uint32_t count) { return ot_session_hash_block(words, count); }

uint32_t hash_block_normalized(const uint32_t *words, uint32_t count, uint32_t low, uint32_t high) {
    return ot_session_hash_block_normalized(words, count, low, high);
}

void write_all(const void *data, DWORD size) {
    DWORD written;
    WriteFile(g_file, data, size, &written, 0);
}

void collect_regions(void) {
    const unsigned char *base = (const unsigned char *)GetModuleHandleA(0);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    unsigned index;

    for (index = 0; index < nt->FileHeader.NumberOfSections && g_region_count < kMaxRegions; ++index, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0) {
            continue;
        }
        uint32_t size = section->Misc.VirtualSize;
        size = (size + 3u) & ~3u;
        g_regions[g_region_count].base = (uint32_t)(UINT_PTR)base + section->VirtualAddress;
        g_regions[g_region_count].size = size;
        g_block_count += (size + kBlockSize - 1u) / kBlockSize;
        ++g_region_count;
        ot_log("session: region %08X +%08X (%.8s)", (unsigned)g_regions[g_region_count - 1].base, (unsigned)size,
            (const char *)section->Name);
    }
}

void write_header(void) {
    uint32_t words[4];
    uint32_t index;
    write_all(kMagic, sizeof(kMagic));
    words[0] = kBlockSize;
    words[1] = g_every;
    words[2] = g_block_count;
    words[3] = g_region_count;
    write_all(words, sizeof(words));
    for (index = 0; index < g_region_count; ++index) {
        write_all(&g_regions[index], sizeof(Region));
    }
}

// session.heap: per hashed delivery, `delivery, count` then `base, size, hash` for every committed
// private read/write region outside the image (the game's malloc heaps, surfaces, thread stacks).
HANDLE g_heap_file = INVALID_HANDLE_VALUE;
uint32_t *g_heap_buffer = 0;
const uint32_t kMaxHeapRegions = 8192;

void trace_heap(unsigned delivery) {
    MEMORY_BASIC_INFORMATION info;
    const unsigned char *at = 0;
    uint32_t count = 0;
    DWORD written;

    while (count < kMaxHeapRegions && VirtualQuery(at, &info, sizeof(info)) == sizeof(info)) {
        const unsigned char *next = (const unsigned char *)info.BaseAddress + info.RegionSize;
        if (next <= at) break;
        at = next;
        if (info.State != MEM_COMMIT || info.Type != MEM_PRIVATE || info.Protect != PAGE_READWRITE) continue;
        if ((uint32_t)(UINT_PTR)info.BaseAddress >= g_regions[0].base &&
            (uint32_t)(UINT_PTR)info.BaseAddress < g_regions[0].base + g_regions[0].size) continue;
        if (info.BaseAddress == g_record || info.BaseAddress == g_heap_buffer) continue;
        if (info.RegionSize < 0x1000 || (uint32_t)(UINT_PTR)info.BaseAddress >= 0x70000000u) continue;
        g_heap_buffer[2 + count * 3] = (uint32_t)(UINT_PTR)info.BaseAddress;
        g_heap_buffer[3 + count * 3] = (uint32_t)info.RegionSize;
        uint32_t hash = 0;
        __try {
            hash = hash_block((const uint32_t *)info.BaseAddress, (uint32_t)info.RegionSize / 4u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            hash = 0xdeadbeefu; // another thread released or protected the region meanwhile
        }
        g_heap_buffer[4 + count * 3] = hash;
        ++count;
    }
    g_heap_buffer[0] = delivery;
    g_heap_buffer[1] = count;
    WriteFile(g_heap_file, g_heap_buffer, (2u + count * 3u) * 4u, &written, 0);
}

void dump_image(unsigned delivery) {
    char name[64];
    char path[MAX_PATH];
    uint32_t index;
    HANDLE file;
    DWORD written;

    wsprintfA(name, "session_%u.img", delivery);
    ot_output_path(path, name);
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ot_log("session: cannot write %s", path);
        return;
    }
    for (index = 0; index < g_region_count; ++index) {
        WriteFile(file, (const void *)(UINT_PTR)g_regions[index].base, g_regions[index].size, &written, 0);
    }
    CloseHandle(file);
    ot_log("session: dumped the image at delivery %u to %s", delivery, path);
}

// Phase 5.4a: the large private read/write regions (the game's memory zones are allocated, not static) as
// (base, size, bytes) records in session_<delivery>.heap, for a program that rebuilds the process's memory.
void dump_heap(unsigned delivery) {
    char name[64];
    char path[MAX_PATH];
    MEMORY_BASIC_INFORMATION info;
    const unsigned char *at = 0;
    DWORD written;
    uint32_t regions = 0;
    HANDLE file;

    wsprintfA(name, "session_%u.heap", delivery);
    ot_output_path(path, name);
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    while (VirtualQuery(at, &info, sizeof(info)) == sizeof(info)) {
        const unsigned char *next = (const unsigned char *)info.BaseAddress + info.RegionSize;
        if (next <= at) break;
        at = next;
        if (info.State != MEM_COMMIT || info.Type != MEM_PRIVATE || info.Protect != PAGE_READWRITE) continue;
        if (info.RegionSize < 0x100000 || (uint32_t)(UINT_PTR)info.BaseAddress >= 0x70000000u) continue;
        if (info.BaseAddress == g_record || info.BaseAddress == g_heap_buffer || info.BaseAddress == g_scratch) continue;
        if ((uint32_t)(UINT_PTR)info.BaseAddress >= g_regions[0].base &&
            (uint32_t)(UINT_PTR)info.BaseAddress < g_regions[0].base + g_regions[0].size) continue;
        uint32_t header[2];
        header[0] = (uint32_t)(UINT_PTR)info.BaseAddress;
        header[1] = (uint32_t)info.RegionSize;
        WriteFile(file, header, sizeof(header), &written, 0);
        WriteFile(file, info.BaseAddress, (DWORD)info.RegionSize, &written, 0);
        ot_log("session: heap region %08X +%08X", (unsigned)header[0], (unsigned)header[1]);
        ++regions;
    }
    CloseHandle(file);
    ot_log("session: dumped %u heap regions to %s", (unsigned)regions, path);
}

// Phase 5.4a: OPENTONY_SESSION_ORACLE=<addr hex>[,...] calls these argument-less cdecl retail getters right after
// the image is dumped and logs what they return ("session: oracle 004141a0 = 00000000"): the answers a program that
// loads session_<N>.img without the retail image must reproduce.
void run_oracle(void) {
    char text[1024];
    const char *at = text;
    if (!env_text("OPENTONY_SESSION_ORACLE", text, sizeof(text))) {
        return;
    }
    while (*at != 0) {
        uint32_t address = 0;
        const char *next = parse_number(at, 16, &address);
        if (next == 0) {
            break;
        }
        at = next;
        if (*at == ',') ++at;
        if (address == 0) {
            break;
        }
        uint32_t value = 0;
        __try {
            value = ((uint32_t(__cdecl *)(void))(UINT_PTR)address)();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ot_log("session: oracle %08X faulted", (unsigned)address);
            continue;
        }
        ot_log("session: oracle %08X = %08X", (unsigned)address, (unsigned)value);
    }
}

// 7b, part 3: OPENTONY_SESSION_SCRIPT=<path> runs an oracle script (oracle_script.h) at delivery OPENTONY_SESSION_SCRIPT_AT with the real retail functions and
// the game's own DirectDraw surfaces, and writes what it printed to oracle.out in the log directory. The comparison with the program that has no retail image is
// tests/test_texture_oracle.py.
uint32_t retail_call(void *, uint32_t address, int convention, const uint32_t *a, uint32_t n) {
    const UINT_PTR f = (UINT_PTR)address;
    if (convention == 2) {
        switch (n) {
        case 1: return ((uint32_t(__stdcall *)(uint32_t))f)(a[0]);
        case 2: return ((uint32_t(__stdcall *)(uint32_t, uint32_t))f)(a[0], a[1]);
        case 3: return ((uint32_t(__stdcall *)(uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2]);
        case 4: return ((uint32_t(__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3]);
        case 5: return ((uint32_t(__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3], a[4]);
        default: return 0;
        }
    }
    if (convention == 1) {
        switch (n) {
        case 1: return ((uint32_t(__thiscall *)(uint32_t))f)(a[0]);
        case 2: return ((uint32_t(__thiscall *)(uint32_t, uint32_t))f)(a[0], a[1]);
        case 3: return ((uint32_t(__thiscall *)(uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2]);
        case 4: return ((uint32_t(__thiscall *)(uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3]);
        case 5: return ((uint32_t(__thiscall *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3], a[4]);
        default: return 0;
        }
    }
    switch (n) {
    case 0: return ((uint32_t(__cdecl *)(void))f)();
    case 1: return ((uint32_t(__cdecl *)(uint32_t))f)(a[0]);
    case 2: return ((uint32_t(__cdecl *)(uint32_t, uint32_t))f)(a[0], a[1]);
    case 3: return ((uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2]);
    case 4: return ((uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3]);
    case 5: return ((uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3], a[4]);
    case 6: return ((uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3], a[4], a[5]);
    default: return 0;
    }
}

// IDirectDrawSurface7, read through its own vtable: GetSurfaceDesc (slot 22), Lock (25), Unlock (32)
int retail_surface(void *, uint32_t surface, uint32_t *width, uint32_t *height, uint32_t *bytes_per_pixel, unsigned char *pixels, uint32_t capacity) {
    typedef long(__stdcall * LockFn)(void *self, void *rect, void *desc, DWORD flags, HANDLE event);
    typedef long(__stdcall * UnlockFn)(void *self, void *rect);
    void *self = (void *)(UINT_PTR)surface;
    void **vtable = *(void ***)self;
    unsigned char desc[0x7c];
    memset(desc, 0, sizeof(desc));
    *(DWORD *)desc = sizeof(desc);
    if (((LockFn)vtable[25])(self, 0, desc, 0x810, 0) != 0) {
        return 0;
    }
    const uint32_t h = *(DWORD *)(desc + 8);
    const uint32_t w = *(DWORD *)(desc + 0xc);
    const LONG pitch = *(LONG *)(desc + 0x10);
    const unsigned char *bits = *(unsigned char **)(desc + 0x24);
    const uint32_t bpp = *(DWORD *)(desc + 0x48 + 0xc) / 8u;
    int ok = 0;
    if (bits != 0 && bpp != 0 && w * h * bpp <= capacity) {
        for (uint32_t row = 0; row < h; ++row) {
            memcpy(pixels + row * w * bpp, bits + (LONG)row * pitch, w * bpp);
        }
        *width = w;
        *height = h;
        *bytes_per_pixel = bpp;
        ok = 1;
    }
    ((UnlockFn)vtable[32])(self, 0);
    return ok;
}

// The lockstep clock owns the timer callback in this process (its first bytes are a jump), so a delivery is the clock's: one real callback on the game thread.
void retail_tick(void *) { ot_clock_deliver(); }

void run_script(void) {
    char script[MAX_PATH];
    char path[MAX_PATH];
    if (!env_text("OPENTONY_SESSION_SCRIPT", script, sizeof(script))) {
        return;
    }
    ot_output_path(path, "oracle.out");
    const OtOracleBackend backend = {0, retail_call, retail_surface, retail_tick};
    const int failed = ot_oracle_run(script, path, &backend);
    ot_log("session: oracle script %s: %s (line %d)", script, failed == 0 ? "ran" : "failed", failed);
}

// Called after the keyboard device update of every PCInput_Poll.
extern "C" void __cdecl ot_session_after_poll(void) {
    const uint32_t poll = g_polls;
    volatile unsigned char *keys = (volatile unsigned char *)(UINT_PTR)kKeyboardState;
    uint32_t index;

    for (index = 0; index < g_key_count; ++index) {
        const KeyEvent &event = g_keys[index];
        if (event.dik > 0xff) continue;
        if (poll >= event.poll && poll < event.poll + event.hold) {
            keys[event.dik] = 0x80;
        } else if (poll == event.poll + event.hold) {
            keys[event.dik] = 0;
        }
    }
    g_polls = poll + 1;
}

void patch_bytes(uint32_t address, const unsigned char *bytes, uint32_t size) {
    DWORD old_protect;
    DWORD ignored;
    VirtualProtect((void *)(UINT_PTR)address, size, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy((void *)(UINT_PTR)address, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), (void *)(UINT_PTR)address, size);
    VirtualProtect((void *)(UINT_PTR)address, size, old_protect, &ignored);
}

void plant_poll_hook(void) {
    unsigned char *slot = (unsigned char *)VirtualAlloc(0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    unsigned char *at = slot;
    unsigned char patch[5];
    LONG relative;

    if (slot == 0 || memcmp((const void *)(UINT_PTR)kPollCall, kPollCallBytes, sizeof(kPollCallBytes)) != 0) {
        ot_log("session: PCInput_Poll call site differs; no key injection");
        return;
    }
    // call keyboard_update; pushad; pushfd; call ot_session_after_poll; popfd; popad; ret
    relative = (LONG)(kPollCallTarget - (uint32_t)(UINT_PTR)(at + 5));
    *at++ = 0xe8;
    memcpy(at, &relative, 4);
    at += 4;
    *at++ = 0x60;
    *at++ = 0x9c;
    relative = (LONG)((uint32_t)(UINT_PTR)ot_session_after_poll - (uint32_t)(UINT_PTR)(at + 5));
    *at++ = 0xe8;
    memcpy(at, &relative, 4);
    at += 4;
    *at++ = 0x9d;
    *at++ = 0x61;
    *at++ = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), slot, 64);
    relative = (LONG)((uint32_t)(UINT_PTR)slot - (kPollCall + 5u));
    patch[0] = 0xe8;
    memcpy(patch + 1, &relative, 4);
    patch_bytes(kPollCall, patch, sizeof(patch));
    ot_log("session: key injection planted after the keyboard update at %08X", (unsigned)kPollCall);
}

// 9: OPENTONY_SESSION_WATCH=<addr hex>:<bytes>[,...] logs every call of a retail function with its return address and first four stack arguments
// ("session: watch 0046f490 from 0044a1b3 (00008190 00000001 00000001 00000000) at delivery 1"), a measurement mode. <bytes> is the length of the whole
// instructions at the entry that the 5-byte jump covers; they are copied to the trampoline as they are, so they must not be relative.
volatile uint32_t g_delivery = 0;

extern "C" void __cdecl ot_session_watch_log(uint32_t address, const uint32_t *stack) {
    ot_log("session: watch %08X from %08X (%08X %08X %08X %08X) at delivery %u", (unsigned)address, (unsigned)stack[0], (unsigned)stack[1],
        (unsigned)stack[2], (unsigned)stack[3], (unsigned)stack[4], (unsigned)g_delivery);
}

void plant_watches(void) {
    char text[512];
    const char *at = text;
    if (!env_text("OPENTONY_SESSION_WATCH", text, sizeof(text))) {
        return;
    }
    while (*at != 0) {
        uint32_t address = 0, bytes = 0;
        at = parse_number(at, 16, &address);
        if (at == 0 || *at != ':') break;
        at = parse_number(at + 1, 16, &bytes);
        if (at == 0 || bytes < 5 || bytes > 16) break;
        if (*at == ',') ++at;
        const unsigned char first = *(const unsigned char *)(UINT_PTR)address;
        if (first == 0xe8 || first == 0xe9) {
            ot_log("session: %08X starts with a relative jump or call (a hook?); not watched", (unsigned)address);
            continue;
        }
        unsigned char *slot = (unsigned char *)VirtualAlloc(0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        unsigned char *p = slot;
        LONG relative;
        // pushad; pushfd; lea eax, [esp+36]; push eax; push address; call ot_session_watch_log; add esp, 8; popfd; popad; <bytes>; jmp address+bytes
        *p++ = 0x60;
        *p++ = 0x9c;
        *p++ = 0x8d; *p++ = 0x44; *p++ = 0x24; *p++ = 0x24;
        *p++ = 0x50;
        *p++ = 0x68;
        memcpy(p, &address, 4);
        p += 4;
        relative = (LONG)((uint32_t)(UINT_PTR)ot_session_watch_log - (uint32_t)(UINT_PTR)(p + 5));
        *p++ = 0xe8;
        memcpy(p, &relative, 4);
        p += 4;
        *p++ = 0x83; *p++ = 0xc4; *p++ = 0x08;
        *p++ = 0x9d;
        *p++ = 0x61;
        memcpy(p, (const void *)(UINT_PTR)address, bytes);
        p += bytes;
        relative = (LONG)((address + bytes) - (uint32_t)(UINT_PTR)(p + 5));
        *p++ = 0xe9;
        memcpy(p, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), slot, 64);
        unsigned char patch[16];
        memset(patch, 0x90, sizeof(patch));
        relative = (LONG)((uint32_t)(UINT_PTR)slot - (address + 5u));
        patch[0] = 0xe9;
        memcpy(patch + 1, &relative, 4);
        patch_bytes(address, patch, bytes);
        ot_log("session: watching %08X", (unsigned)address);
    }
}

} // namespace

void ot_session_start(int build) {
    char text[512];
    char path[MAX_PATH];
    (void)build;

    if (lstrcmpA(ot_build_id(), "canonical") != 0) {
        return;
    }
    g_limit = env_number("OPENTONY_SESSION_TRACE", 0);
    if (g_limit == 0) {
        return;
    }
    g_every = env_number("OPENTONY_SESSION_EVERY", 2);
    if (g_every == 0) g_every = 1;
    g_dump = env_number("OPENTONY_SESSION_DUMP", 0xffffffffu);
    if (env_text("OPENTONY_SESSION_DUMP", text, sizeof(text))) {
        const char *comma = text;
        while (*comma != 0 && *comma != ',') ++comma;
        if (*comma == ',' && parse_number(comma + 1, 10, &g_dump_again) == 0) {
            g_dump_again = 0xffffffffu;
        }
    }
    g_script_at = env_number("OPENTONY_SESSION_SCRIPT_AT", 0xffffffffu);
    g_normalize = env_number("OPENTONY_SESSION_NORM", 1);
    if (env_text("OPENTONY_SESSION_MASK", text, sizeof(text))) {
        parse_masks(text);
    }
    g_scratch = (uint32_t *)VirtualAlloc(0, kBlockSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (env_text("OPENTONY_SESSION_KEYS", text, sizeof(text))) {
        parse_keys(text);
    }
    ot_log("session: trace %u deliveries, every %u, %u key events, dump at %u", (unsigned)g_limit,
        (unsigned)g_every, (unsigned)g_key_count, (unsigned)g_dump);

    if (env_number("OPENTONY_SESSION_SKIP_MOVIES", 0) != 0) {
        static const unsigned char kReturnZero[3] = {0x31, 0xc0, 0xc3}; // xor eax, eax; ret
        for (unsigned index = 0; index < 2; ++index) {
            patch_bytes(kMovieEntries[index], kReturnZero, sizeof(kReturnZero));
        }
        ot_log("session: startup movies return at once");
    }
    plant_poll_hook();
    plant_watches();
    collect_regions();
    g_record = (uint32_t *)VirtualAlloc(0, (2u + g_block_count) * 4u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ot_output_path(path, "session.trace");
    g_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (g_file == INVALID_HANDLE_VALUE || g_record == 0) {
        ot_log("session: cannot open %s", path);
        g_limit = 0;
        return;
    }
    write_header();
    if (env_number("OPENTONY_SESSION_HEAP", 0) != 0) {
        g_heap_buffer = (uint32_t *)VirtualAlloc(0, (2u + kMaxHeapRegions * 3u) * 4u, MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE);
        ot_output_path(path, "session.heap");
        g_heap_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (g_heap_buffer == 0) {
            g_heap_file = INVALID_HANDLE_VALUE;
        }
    }
    g_enabled = 1;
}

void ot_session_on_delivery(unsigned count) {
    uint32_t region;
    uint32_t block = 0;
    uint32_t low = 0, high = 0;
    char path[MAX_PATH];

    if (!g_enabled) {
        return;
    }
    g_delivery = count;
    if (count == g_dump || count == g_dump_again) {
        dump_image(count);
        dump_heap(count);
        run_oracle();
    }
    if (count == g_script_at) {
        run_script();
    }
    if (count % g_every == 0) {
        g_record[0] = count;
        g_record[1] = g_polls;
        if (g_normalize) {
            const volatile uint32_t *zones = (const volatile uint32_t *)(UINT_PTR)kZoneBounds;
            low = zones[0] < zones[2] ? zones[0] : zones[2];
            high = zones[1] > zones[3] ? zones[1] : zones[3];
            if (low == 0 || high <= low) low = high = 0;
        }
        for (region = 0; region < g_region_count; ++region) {
            const unsigned char *at = (const unsigned char *)(UINT_PTR)g_regions[region].base;
            uint32_t left = g_regions[region].size;
            while (left != 0) {
                const uint32_t size = left < kBlockSize ? left : kBlockSize;
                const uint32_t *words = (const uint32_t *)at;
                const uint32_t address = (uint32_t)(UINT_PTR)at;
                if (g_mask_count != 0) {
                    bool touched = false;
                    for (uint32_t m = 0; m < g_mask_count; ++m) {
                        if (g_masks[m].base < address + size && g_masks[m].base + g_masks[m].size > address) {
                            if (!touched) {
                                memcpy(g_scratch, at, size);
                                touched = true;
                            }
                            const uint32_t from = g_masks[m].base > address ? g_masks[m].base - address : 0;
                            const uint32_t to = g_masks[m].base + g_masks[m].size - address < size ?
                                g_masks[m].base + g_masks[m].size - address : size;
                            memset((unsigned char *)g_scratch + from, 0, to - from);
                        }
                    }
                    if (touched) words = g_scratch;
                }
                g_record[2u + block++] = low != high ? hash_block_normalized(words, size / 4u, low, high)
                                                     : hash_block(words, size / 4u);
                at += size;
                left -= size;
            }
        }
        write_all(g_record, (2u + g_block_count) * 4u);
        if (g_heap_file != INVALID_HANDLE_VALUE) {
            trace_heap(count);
        }
    }
    if (count >= g_limit) {
        g_enabled = 0;
        FlushFileBuffers(g_file);
        CloseHandle(g_file);
        if (g_heap_file != INVALID_HANDLE_VALUE) {
            CloseHandle(g_heap_file);
        }
        ot_log("session: %u deliveries, %u polls; stopping", count, (unsigned)g_polls);
        ot_write_stats();
        ot_output_path(path, "session.done");
        CloseHandle(CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0));
        TerminateProcess(GetCurrentProcess(), 0);
    }
}
