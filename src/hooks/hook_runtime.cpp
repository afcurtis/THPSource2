#include "hook_runtime.h"

#include "clock.h"
#include "hal/hal_runtime.h"
#include "hal/site_trace.h"
#include "hal/video_trace.h"
#include "kit_abi.h"
#include "mod_overlay.h"
#include "native_hooks_generated.h"
#include "selftest.h"
#include "session_trace.h"

#include <stdarg.h>
#include <string.h>

/* Layout of one code slot: trampoline at +0 (<= 16 + 5 bytes), dispatch stub
 * at +32 (33 bytes). */
#define OT_SLOT_SIZE 96u
#define OT_STUB_OFFSET 32u
#define OT_STATS_INTERVAL_MS 250u

#ifndef INVALID_FILE_ATTRIBUTES /* newer than the VC6 SDK */
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

static OtHookState g_hooks[OT_HOOK_COUNT];
static volatile LONG g_master = 1;
static int g_build = -1;
static char g_dir[MAX_PATH];        /* directory of opentony.dll */
static char g_output_dir[MAX_PATH]; /* log, stats, self-test report */
static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_log_lock;
static unsigned char *g_code = 0;
static HANDLE g_monitor = 0;
static volatile LONG g_stop = 0;
static DWORD g_stress_ms = 0;
static volatile LONG g_stress_flips = 0;
static int g_hotkeys = 1;
static int g_clock_mode = OT_CLOCK_REALTIME;

/* ------------------------------------------------------------------ logging */

void ot_log(const char *format, ...) {
    char line[1100];
    va_list args;
    int length;
    DWORD written;

    va_start(args, format);
    length = wvsprintfA(line, format, args); /* bounded to 1024 characters */
    va_end(args);
    if (length < 0) {
        return;
    }
    line[length++] = '\r';
    line[length++] = '\n';
    line[length] = 0;
    OutputDebugStringA(line);
    if (g_log != INVALID_HANDLE_VALUE) {
        EnterCriticalSection(&g_log_lock);
        WriteFile(g_log, line, (DWORD)length, &written, 0);
        LeaveCriticalSection(&g_log_lock);
    }
}

static void join_path(char *out, const char *dir, const char *name) {
    lstrcpynA(out, dir, MAX_PATH);
    if (lstrlenA(out) + lstrlenA(name) < MAX_PATH) {
        lstrcatA(out, name);
    }
}

/* opentony.ini is read next to the DLL. */
static void path_in_dir(char *out, const char *name) {
    join_path(out, g_dir, name);
}

/* Log, stats and self-test output: OPENTONY_LOG_DIR, else next to the DLL,
 * else %TEMP% (the game directory may be read-only, e.g. a mounted disc). */
static void path_in_output_dir(char *out, const char *name) {
    join_path(out, g_output_dir, name);
}

void ot_output_path(char *out, const char *name) {
    path_in_output_dir(out, name);
}

static void choose_output_dir(void) {
    DWORD length = GetEnvironmentVariableA("OPENTONY_LOG_DIR", g_output_dir, MAX_PATH - 2);
    if (length != 0 && length < MAX_PATH - 2) {
        if (g_output_dir[length - 1] != '\\' && g_output_dir[length - 1] != '/') {
            lstrcatA(g_output_dir, "\\");
        }
        CreateDirectoryA(g_output_dir, 0);
    } else {
        lstrcpynA(g_output_dir, g_dir, MAX_PATH);
    }
}

static void open_log(void) {
    char path[MAX_PATH];
    choose_output_dir();
    path_in_output_dir(path, "opentony.log");
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (g_log == INVALID_HANDLE_VALUE && GetTempPathA(MAX_PATH, g_output_dir) != 0) {
        path_in_output_dir(path, "opentony.log");
        g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, 0);
    }
}

/* ---------------------------------------------------------------- identity */

static int identify_build(void) {
    const unsigned char *base = (const unsigned char *)GetModuleHandleA(0);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt;
    unsigned index;

    if (base == 0 || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return -1;
    }
    nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return -1;
    }
    ot_log("host image: base=%08X timestamp=%08X size_of_image=%08X",
        (unsigned)(UINT_PTR)base, (unsigned)nt->FileHeader.TimeDateStamp,
        (unsigned)nt->OptionalHeader.SizeOfImage);
    /* Every hook address is absolute; the retail images have no relocations. */
    if ((UINT_PTR)base != 0x00400000u) {
        return -1;
    }
    for (index = 0; index < OT_BUILD_COUNT; ++index) {
        if (nt->FileHeader.TimeDateStamp == ot_builds[index].pe_timestamp &&
            nt->OptionalHeader.SizeOfImage == ot_builds[index].size_of_image) {
            return (int)index;
        }
    }
    return -1;
}

const char *ot_build_id(void) {
    return g_build >= 0 ? ot_builds[g_build].id : "unknown";
}

/* ------------------------------------------------------------ configuration */

static int text_equal(const char *text, unsigned length, const char *word) {
    unsigned index;
    for (index = 0; index < length; ++index) {
        char left = text[index];
        char right = word[index];
        if (right == 0) {
            return 0;
        }
        if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
        if (left != right) {
            return 0;
        }
    }
    return word[length] == 0;
}

static int find_hook(const char *text, unsigned length) {
    unsigned index;
    for (index = 0; index < OT_HOOK_COUNT; ++index) {
        if (text_equal(text, length, ot_hooks[index].name)) {
            return (int)index;
        }
    }
    return -1;
}

static void set_all(int enabled, int use_defaults) {
    unsigned index;
    for (index = 0; index < OT_HOOK_COUNT; ++index) {
        g_hooks[index].enabled = (uint8_t)(use_defaults ? ot_hooks[index].default_enabled : enabled);
        g_hooks[index].fault = 0;
    }
}

/* One OPENTONY_HOOKS token: all|on, none|off, default|manifest, Name, +Name,
 * -Name, fault:Name. */
static void apply_token(const char *token, unsigned length) {
    int index;
    if (length == 0) {
        return;
    }
    if (text_equal(token, length, "all") || text_equal(token, length, "on")) {
        set_all(1, 0);
    } else if (text_equal(token, length, "none") || text_equal(token, length, "off")) {
        set_all(0, 0);
    } else if (text_equal(token, length, "default") || text_equal(token, length, "manifest")) {
        set_all(0, 1);
    } else if (length > 6 && text_equal(token, 6, "fault:")) {
        index = find_hook(token + 6, length - 6);
        if (index < 0 || ot_hooks[index].fault == 0) {
            ot_log("config: no fault variant for '%s'", token);
            return;
        }
        g_hooks[index].enabled = 1;
        g_hooks[index].fault = 1;
    } else if (token[0] == '-' || token[0] == '+') {
        index = find_hook(token + 1, length - 1);
        if (index < 0) {
            ot_log("config: unknown hook in '%s'", token);
            return;
        }
        g_hooks[index].enabled = (uint8_t)(token[0] == '+');
        g_hooks[index].fault = 0;
    } else {
        index = find_hook(token, length);
        if (index < 0) {
            ot_log("config: unknown hook '%s'", token);
            return;
        }
        g_hooks[index].enabled = 1;
        g_hooks[index].fault = 0;
    }
}

static void apply_spec(const char *spec) {
    char token[128];
    unsigned length = 0;
    for (;; ++spec) {
        const char c = *spec;
        if (c == 0 || c == ',' || c == ';' || c == ' ' || c == '\t') {
            token[length] = 0;
            apply_token(token, length);
            length = 0;
            if (c == 0) {
                break;
            }
        } else if (length + 1 < sizeof(token)) {
            token[length++] = c;
        }
    }
}

/* True when the variable exists and fits in `size` bytes including the NUL. */
static int read_env(const char *name, char *out, DWORD size) {
    const DWORD length = GetEnvironmentVariableA(name, out, size);
    return length != 0 && length < size;
}

static int clock_mode(const char *value, int fallback) {
    const unsigned length = (unsigned)lstrlenA(value);
    if (text_equal(value, length, "realtime")) return OT_CLOCK_REALTIME;
    if (text_equal(value, length, "lockstep")) return OT_CLOCK_LOCKSTEP;
    if (text_equal(value, length, "lockstep-fast")) return OT_CLOCK_LOCKSTEP_FAST;
    ot_log("config: unknown clock mode '%s'", value);
    return fallback;
}

static int ini_flag(const char *value, int fallback) {
    unsigned length = (unsigned)lstrlenA(value);
    if (text_equal(value, length, "1") || text_equal(value, length, "on") ||
        text_equal(value, length, "yes") || text_equal(value, length, "true")) {
        return 1;
    }
    if (text_equal(value, length, "0") || text_equal(value, length, "off") ||
        text_equal(value, length, "no") || text_equal(value, length, "false")) {
        return 0;
    }
    return fallback;
}

/* Precedence: manifest defaults < opentony.ini < OPENTONY_HOOKS. */
static void load_configuration(void) {
    char ini[MAX_PATH];
    char value[512];
    unsigned index;

    set_all(0, 1);
    path_in_dir(ini, "opentony.ini");
    ot_hal_configure(GetFileAttributesA(ini) != INVALID_FILE_ATTRIBUTES ? ini : 0);
    if (GetFileAttributesA(ini) != INVALID_FILE_ATTRIBUTES) {
        GetPrivateProfileStringA("hooks", "default", "manifest", value, sizeof(value), ini);
        if (text_equal(value, (unsigned)lstrlenA(value), "manifest")) {
            set_all(0, 1);
        } else {
            set_all(ini_flag(value, 0), 0);
        }
        for (index = 0; index < OT_HOOK_COUNT; ++index) {
            GetPrivateProfileStringA("hooks", ot_hooks[index].name, "", value, sizeof(value), ini);
            if (value[0] != 0) {
                g_hooks[index].enabled = (uint8_t)ini_flag(value, g_hooks[index].enabled);
            }
        }
        GetPrivateProfileStringA("runtime", "native", "on", value, sizeof(value), ini);
        g_master = ini_flag(value, 1);
        GetPrivateProfileStringA("runtime", "hotkeys", "on", value, sizeof(value), ini);
        g_hotkeys = ini_flag(value, 1);
        GetPrivateProfileStringA("runtime", "clock", "realtime", value, sizeof(value), ini);
        g_clock_mode = clock_mode(value, OT_CLOCK_REALTIME);
        ot_log("config: read %s", ini);
    }
    if (read_env("OPENTONY_HOOKS", value, sizeof(value))) {
        ot_log("config: OPENTONY_HOOKS=%s", value);
        apply_spec(value);
        g_master = 1;
    }
    if (read_env("OPENTONY_CLOCK", value, sizeof(value))) {
        ot_log("config: OPENTONY_CLOCK=%s", value);
        g_clock_mode = clock_mode(value, g_clock_mode);
    }
    if (read_env("OPENTONY_HOOK_STRESS", value, sizeof(value))) {
        DWORD ms = 0;
        const char *digit;
        for (digit = value; *digit >= '0' && *digit <= '9'; ++digit) {
            ms = ms * 10u + (DWORD)(*digit - '0');
        }
        g_stress_ms = ms;
        ot_log("config: OPENTONY_HOOK_STRESS=%u ms (master flips native<->retail)", (unsigned)ms);
    }
}

/* ------------------------------------------------------------ installation */

static unsigned char *emit_jmp(unsigned char *at, const void *to) {
    LONG relative = (LONG)((const unsigned char *)to - (at + 5));
    at[0] = 0xe9;
    memcpy(at + 1, &relative, sizeof(relative));
    return at + 5;
}

static unsigned char *emit_abs32(unsigned char *at, const volatile void *address) {
    DWORD value = (DWORD)(UINT_PTR)address;
    memcpy(at, &value, sizeof(value));
    return at + 4;
}

/* active ? (++native_calls, native) : (++retail_calls, trampoline).
 * Only EFLAGS is touched, which is dead at a call boundary. */
static void emit_stub(OtHookState *hook, unsigned char *at, const void *native) {
    *at++ = 0x80; /* cmp byte ptr [active], 0 */
    *at++ = 0x3d;
    at = emit_abs32(at, &hook->active);
    *at++ = 0x00;
    *at++ = 0x74; /* je retail (+12) */
    *at++ = 0x0c;
    *at++ = 0xf0; /* lock inc dword ptr [native_calls] */
    *at++ = 0xff;
    *at++ = 0x05;
    at = emit_abs32(at, &hook->native_calls);
    at = emit_jmp(at, native);
    *at++ = 0xf0; /* retail: lock inc dword ptr [retail_calls] */
    *at++ = 0xff;
    *at++ = 0x05;
    at = emit_abs32(at, &hook->retail_calls);
    emit_jmp(at, hook->trampoline);
}

static void format_bytes(char *out, const unsigned char *bytes, unsigned count) {
    static const char digits[] = "0123456789abcdef";
    unsigned index;
    for (index = 0; index < count; ++index) {
        *out++ = digits[bytes[index] >> 4];
        *out++ = digits[bytes[index] & 15];
        *out++ = index + 1 < count ? ' ' : 0;
    }
    if (count == 0) {
        *out = 0;
    }
}

static int readable_code(const void *address, unsigned size) {
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT) {
        return 0;
    }
    if ((info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
            PAGE_EXECUTE_WRITECOPY)) == 0) {
        return 0;
    }
    return (const unsigned char *)address + size <=
        (const unsigned char *)info.BaseAddress + info.RegionSize;
}

static int install_hook(OtHookState *hook, unsigned char *slot) {
    const OtHookSpec *spec = hook->spec;
    const uint32_t address = spec->address[g_build];
    const void *native;
    unsigned char *target;
    DWORD old_protect;
    DWORD ignored;
    unsigned index;
    char actual[OT_HOOK_MAX_OVERWRITE * 3 + 1];

    if (address == 0) {
        ot_log("  %s: not present in build %s", spec->name, ot_build_id());
        return 0;
    }
    target = (unsigned char *)(UINT_PTR)address;
    if (!readable_code(target, spec->overwrite)) {
        ot_log("  %s: %08X is not mapped code", spec->name, (unsigned)address);
        return 0;
    }
    if (memcmp(target, spec->expected, spec->overwrite) != 0) {
        format_bytes(actual, target, spec->overwrite);
        ot_log("  %s: prologue mismatch at %08X (found %s); left retail",
            spec->name, (unsigned)address, actual);
        return 0;
    }
    native = hook->fault ? *spec->fault : *spec->native;
    if (native == 0) {
        ot_log("  %s: no native entry point (%s not loaded?); left retail", spec->name, OT_KIT_DLL);
        return 0;
    }
    hook->target = target;
    hook->trampoline = slot;
    hook->stub = slot + OT_STUB_OFFSET;
    memcpy(hook->trampoline, target, spec->overwrite);
    emit_jmp(hook->trampoline + spec->overwrite, target + spec->overwrite);
    emit_stub(hook, hook->stub, native);
    FlushInstructionCache(GetCurrentProcess(), slot, OT_SLOT_SIZE);

    if (!VirtualProtect(target, spec->overwrite, PAGE_EXECUTE_READWRITE, &old_protect)) {
        ot_log("  %s: VirtualProtect failed (%u)", spec->name, (unsigned)GetLastError());
        return 0;
    }
    emit_jmp(target, hook->stub);
    for (index = 5; index < spec->overwrite; ++index) {
        target[index] = 0x90;
    }
    FlushInstructionCache(GetCurrentProcess(), target, spec->overwrite);
    VirtualProtect(target, spec->overwrite, old_protect, &ignored);
    hook->installed = 1;
    return 1;
}

static void restore_hook(OtHookState *hook) {
    DWORD old_protect;
    DWORD ignored;
    if (!hook->installed) {
        return;
    }
    hook->active = 0;
    if (VirtualProtect(hook->target, hook->spec->overwrite, PAGE_EXECUTE_READWRITE, &old_protect)) {
        memcpy(hook->target, hook->spec->expected, hook->spec->overwrite);
        FlushInstructionCache(GetCurrentProcess(), hook->target, hook->spec->overwrite);
        VirtualProtect(hook->target, hook->spec->overwrite, old_protect, &ignored);
    }
    hook->installed = 0;
}

static void refresh_active(void) {
    unsigned index;
    for (index = 0; index < OT_HOOK_COUNT; ++index) {
        OtHookState *hook = &g_hooks[index];
        hook->active = (uint8_t)(hook->installed && hook->enabled && g_master);
    }
}

void ot_hooks_set_master(int enabled) {
    InterlockedExchange((LONG *)&g_master, enabled ? 1 : 0); /* VC6 prototypes lack volatile */
    refresh_active();
}

int ot_hooks_master(void) {
    return g_master != 0;
}

unsigned ot_hook_count(void) {
    return OT_HOOK_COUNT;
}

OtHookState *ot_hook_state(unsigned index) {
    return index < OT_HOOK_COUNT ? &g_hooks[index] : 0;
}

/* ------------------------------------------------------ stats and hotkeys */

static const char *status_name(uint32_t status) {
    switch (status) {
    case OT_NATIVE_TESTED: return "tested";
    case OT_NATIVE_TRACE_VALIDATED: return "trace-validated";
    case OT_NATIVE_INTEGRATED: return "integrated";
    default: return "?";
    }
}

static void write_stats(void) {
    char path[MAX_PATH];
    char line[3072];
    HANDLE file;
    DWORD written;
    unsigned index;
    int length;

    path_in_output_dir(path, "opentony.stats");
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    length = wsprintfA(line, "build %s\r\nmaster %d\r\nstress_flips %d\r\nclock %d deliveries %u\r\n",
        ot_build_id(), (int)g_master, (int)g_stress_flips, ot_clock_mode(), ot_clock_deliveries());
    WriteFile(file, line, (DWORD)length, &written, 0);
    length = wsprintfA(line, "hal ");
    length += ot_hal_describe(line + length, (int)sizeof(line) - length - 2);
    line[length++] = '\r';
    line[length++] = '\n';
    WriteFile(file, line, (DWORD)length, &written, 0);
    length = wsprintfA(line, "hal_services ");
    length += ot_hal_describe_services(line + length, (int)sizeof(line) - length - 2);
    line[length++] = '\r';
    line[length++] = '\n';
    WriteFile(file, line, (DWORD)length, &written, 0);
    if (ot_video_trace_enabled()) {
        length = ot_video_trace_describe(line, (int)sizeof(line) - 2);
        line[length++] = '\r';
        line[length++] = '\n';
        WriteFile(file, line, (DWORD)length, &written, 0);
    }
    for (index = 0; index < OT_HOOK_COUNT; ++index) {
        const OtHookState *hook = &g_hooks[index];
        length = wsprintfA(line,
            "hook %s installed=%d enabled=%d fault=%d active=%d native_calls=%u retail_calls=%u\r\n",
            hook->spec->name, (int)hook->installed, (int)hook->enabled, (int)hook->fault,
            (int)hook->active, (unsigned)hook->native_calls, (unsigned)hook->retail_calls);
        WriteFile(file, line, (DWORD)length, &written, 0);
    }
    CloseHandle(file);
}

void ot_write_stats(void) {
    write_stats();
}

static void log_stats(const char *reason) {
    unsigned index;
    ot_log("stats (%s): master=%d", reason, (int)g_master);
    for (index = 0; index < OT_HOOK_COUNT; ++index) {
        const OtHookState *hook = &g_hooks[index];
        ot_log("  %-24s native=%u retail=%u", hook->spec->name,
            (unsigned)hook->native_calls, (unsigned)hook->retail_calls);
    }
}

static int game_has_focus(void) {
    DWORD process = 0;
    HWND window = GetForegroundWindow();
    if (window == 0) {
        return 0;
    }
    GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId();
}

static DWORD WINAPI monitor_main(void *unused) {
    DWORD last_stats = GetTickCount();
    DWORD last_flip = last_stats;
    int f9_down = 0;
    int f10_down = 0;
    (void)unused;

    while (!g_stop) {
        const DWORD now = GetTickCount();
        Sleep(g_stress_ms != 0 && g_stress_ms < 20u ? g_stress_ms : 20u);
        if (g_hotkeys && game_has_focus()) {
            const int f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
            const int f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
            if (f9 && !f9_down) {
                ot_hooks_set_master(!g_master);
                ot_log("F9: native hooks %s", g_master ? "ON" : "OFF (retail)");
            }
            if (f10 && !f10_down) {
                log_stats("F10");
            }
            f9_down = f9;
            f10_down = f10;
        }
        if (g_stress_ms != 0 && now - last_flip >= g_stress_ms) {
            ot_hooks_set_master(!g_master);
            InterlockedIncrement((LONG *)&g_stress_flips);
            last_flip = now;
        }
        ot_hal_poll();
        if (now - last_stats >= OT_STATS_INTERVAL_MS) {
            write_stats();
            last_stats = now;
        }
    }
    return 0;
}

/* --------------------------------------------------------------- Kit hooks */

static void kit_log(const char *line) {
    ot_log("%s", line);
}

/* opentony_native.dll (src/hooks/kits): the Kits of src/runtime/ over the live
 * process. Its entry points fill the native slots of the Kit hooks; without
 * it those hooks stay retail. */
static void load_kits(void) {
    static OtKitHost host;
    char path[MAX_PATH];
    char value[16];
    HMODULE module;
    OtKitAttach attach;
    const OtKitEntry *entries;
    uint32_t count = 0;
    unsigned index;
    unsigned found = 0;
    uint32_t k;

    if (OT_KIT_HOOK_COUNT == 0) {
        return;
    }
    path_in_dir(path, OT_KIT_DLL);
    module = LoadLibraryA(path);
    if (module == 0) {
        ot_log("kits: %s not loaded (%u); the Kit hooks stay retail", OT_KIT_DLL, (unsigned)GetLastError());
        return;
    }
    attach = (OtKitAttach)GetProcAddress(module, OT_KIT_ATTACH);
    if (attach == 0) {
        ot_log("kits: %s has no %s; the Kit hooks stay retail", OT_KIT_DLL, OT_KIT_ATTACH);
        return;
    }
    host.version = OT_KIT_ABI_VERSION;
    host.build = ot_build_id();
    host.log = kit_log;
    host.check_every_access = read_env("OPENTONY_LIVE_GUARD", value, sizeof(value)) && value[0] == '1';
    entries = attach(&host, &count);
    if (entries == 0) {
        ot_log("kits: %s declined to attach; the Kit hooks stay retail", OT_KIT_DLL);
        return;
    }
    for (index = 0; index < OT_HOOK_COUNT; ++index) {
        const OtHookSpec *spec = &ot_hooks[index];
        if (spec->abi != OT_ABI_KIT) {
            continue;
        }
        for (k = 0; k < count; ++k) {
            if (lstrcmpA(entries[k].name, spec->name) == 0) {
                *(void **)spec->native = entries[k].function;
                ++found;
                break;
            }
        }
        if (k == count) {
            ot_log("kits: %s exports no adapter for %s", OT_KIT_DLL, spec->name);
        }
    }
    ot_log("kits: %u/%u Kit hooks have a native entry point", found, (unsigned)OT_KIT_HOOK_COUNT);
}

/* Phase 5.2: find the mods. The overlay hook is installed but off by default; it turns on when a mod
 * has files, unless the configuration named the hook (opentony.ini [hooks]) or chose the hooks
 * (OPENTONY_HOOKS), in which case the hook is exactly as configured. */
static void configure_overlay(void) {
    static const char name[] = "FileIO_BackendOpen";
    char ini[MAX_PATH];
    char value[16];
    int has_ini;
    int overlay;
    unsigned files;

    if (g_build != 0) { /* the slot arrays and the allocator are the canonical build's */
        return;
    }
    path_in_dir(ini, "opentony.ini");
    has_ini = GetFileAttributesA(ini) != INVALID_FILE_ATTRIBUTES;
    files = ot_overlay_configure(g_dir, has_ini ? ini : 0);
    overlay = find_hook(name, (unsigned)lstrlenA(name));
    if (files == 0 || overlay < 0) {
        return;
    }
    if (GetEnvironmentVariableA("OPENTONY_HOOKS", value, sizeof(value)) != 0 ||
        (has_ini && GetPrivateProfileStringA("hooks", name, "", value, sizeof(value), ini) != 0)) {
        ot_log("mods: %u files, hook %s as configured (%s)", files, name, g_hooks[overlay].enabled ? "on" : "off");
        return;
    }
    g_hooks[overlay].enabled = 1;
    ot_log("mods: %u files; hook %s on", files, name);
}

/* ------------------------------------------------------------ entry points */

int ot_hooks_attach(HMODULE self) {
    char *slash;
    char value[16];
    unsigned index;
    unsigned installed = 0;

    InitializeCriticalSection(&g_log_lock);
    GetModuleFileNameA(self, g_dir, sizeof(g_dir));
    slash = strrchr(g_dir, '\\');
    if (slash == 0) {
        slash = strrchr(g_dir, '/');
    }
    if (slash != 0) {
        slash[1] = 0;
    } else {
        g_dir[0] = 0;
    }
    open_log();
    ot_log("OpenTony native hook harness (%u hooks in table)", (unsigned)OT_HOOK_COUNT);

    for (index = 0; index < OT_HOOK_COUNT; ++index) {
        g_hooks[index].spec = &ot_hooks[index];
    }
    g_build = identify_build();
    if (g_build < 0) {
        ot_log("unsupported executable; no hooks installed");
        return 0;
    }
    ot_log("build: %s", ot_build_id());
    ot_site_trace_start(g_build);
    load_configuration();
    load_kits();

    configure_overlay();

    /* One slot per hook, the clock callback trampoline and its sites, and the
     * HAL entry sites. */
    g_code = (unsigned char *)VirtualAlloc(0,
        (OT_HOOK_COUNT + 1u + OT_CLOCK_MAX_SITES + OT_HAL_MAX_SITES) * OT_SLOT_SIZE,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (g_code == 0) {
        ot_log("VirtualAlloc for trampolines failed (%u)", (unsigned)GetLastError());
        return 0;
    }
    /* The dinput proxy loads this DLL during static-import initialization,
     * before WinMain, so no other game thread can be inside a prologue. */
    for (index = 0; index < OT_HOOK_COUNT; ++index) {
        installed += (unsigned)install_hook(&g_hooks[index], g_code + index * OT_SLOT_SIZE);
    }
    refresh_active();
    for (index = 0; index < OT_HOOK_COUNT; ++index) {
        const OtHookState *hook = &g_hooks[index];
        ot_log("  %-24s %08X %-15s %s%s", hook->spec->name,
            (unsigned)hook->spec->address[g_build], status_name(hook->spec->native_status),
            !hook->installed ? "NOT INSTALLED" : (hook->active ? "native" : "retail"),
            hook->fault ? " (FAULT VARIANT)" : "");
    }
    ot_log("installed %u/%u; master %s; F9 toggles native/retail, F10 logs call counts",
        installed, (unsigned)OT_HOOK_COUNT, g_master ? "on" : "off");

    if (read_env("OPENTONY_SELFTEST", value, sizeof(value))) {
        const int failures = ot_selftest_run(g_output_dir);
        write_stats();
        ot_log("selftest: %s; terminating before WinMain", failures ? "FAIL" : "PASS");
        TerminateProcess(GetCurrentProcess(), failures ? 3u : 0u);
    }

    {
        /* clock=sdl: the HAL clock is the lockstep scheduler, and its adapters
         * deliver at the delivery sites they replace (WINMAIN_Sleep). */
        const char *handed[OT_CLOCK_MAX_SITES];
        const int handed_count = ot_hal_clock_names(&ot_hal_spec, handed, (int)OT_CLOCK_MAX_SITES);
        if (ot_hal_config()->backend[OT_HAL_CLOCK] == OT_HAL_SDL && g_clock_mode == OT_CLOCK_REALTIME) {
            g_clock_mode = OT_CLOCK_LOCKSTEP;
            ot_log("config: clock=sdl runs the lockstep scheduler (paced)");
        }
        if (ot_hal_config()->backend[OT_HAL_CLOCK] == OT_HAL_SDL &&
            !ot_clock_install(&ot_lockstep_clock, g_build, g_clock_mode, g_code + OT_HOOK_COUNT * OT_SLOT_SIZE,
                handed, (unsigned)handed_count)) {
            ot_log("config: lockstep clock unavailable; the HAL clock stays retail");
            ot_hal_disable(OT_HAL_CLOCK);
        } else if (ot_hal_config()->backend[OT_HAL_CLOCK] != OT_HAL_SDL) {
            ot_clock_install(&ot_lockstep_clock, g_build, g_clock_mode, g_code + OT_HOOK_COUNT * OT_SLOT_SIZE, 0, 0);
        }
    }
    ot_hal_install(&ot_hal_spec, g_build,
        g_code + (OT_HOOK_COUNT + 1u + OT_CLOCK_MAX_SITES) * OT_SLOT_SIZE, g_dir);
    ot_session_start(g_build);
    write_stats();
    g_monitor = CreateThread(0, 0, monitor_main, 0, 0, 0);
    return 1;
}

void ot_hooks_detach(int process_terminating) {
    unsigned index;
    g_stop = 1;
    ot_site_trace_stop();
    if (g_build >= 0) {
        write_stats();
        log_stats("exit");
    }
    /* On an explicit FreeLibrary the stubs would dangle; put retail back. */
    if (!process_terminating) {
        for (index = 0; index < OT_HOOK_COUNT; ++index) {
            restore_hook(&g_hooks[index]);
        }
        if (g_monitor != 0) {
            WaitForSingleObject(g_monitor, 1000);
            CloseHandle(g_monitor);
        }
    }
    if (g_log != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}
