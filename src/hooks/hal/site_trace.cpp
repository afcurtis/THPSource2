#include "site_trace.h"

#include "../hook_runtime.h"

#include <string.h>

// Dynamic call-site coverage tracer (site_trace.h). Plants a one-shot INT3
// at every known boundary-audit call site and records which ones a real run
// actually reaches, using a vectored exception handler the way every
// software-breakpoint debugger does: the trap address reported by Windows is
// one past the 0xCC byte, so the site address is `ExceptionAddress - 1`.
//
// AddVectoredExceptionHandler postdates the pinned VC6 SDK headers this
// project builds against (docs/HOOKS.md, "Build and install"), so it is
// resolved with GetProcAddress instead of linked directly; every type used
// here (EXCEPTION_POINTERS, CONTEXT, EXCEPTION_BREAKPOINT) is ordinary SEH
// and predates VC6.

namespace {

const DWORD kMaxSites = 1024u; // headroom over the audit's 874 sites

struct TracedSite {
    DWORD address;
    unsigned char original_byte;
    uint8_t patched;
    uint8_t hit;
};

TracedSite g_sites[kMaxSites];
DWORD g_site_count = 0;
HANDLE g_hits_log = INVALID_HANDLE_VALUE;
PVOID g_veh_handle = 0;

typedef PVOID(WINAPI *AddVectoredExceptionHandlerFn)(ULONG first_handler, PVOID handler);

int parse_hex(const char *text, int length, DWORD *out) {
    int index = 0;
    if (length >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        index = 2;
    }
    if (index >= length) {
        return 0;
    }
    DWORD value = 0;
    for (; index < length; ++index) {
        const char c = text[index];
        DWORD digit;
        if (c >= '0' && c <= '9') {
            digit = static_cast<DWORD>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<DWORD>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<DWORD>(c - 'A' + 10);
        } else {
            return 0;
        }
        value = (value << 4) | digit;
    }
    *out = value;
    return 1;
}

void log_hit(DWORD address) {
    if (g_hits_log == INVALID_HANDLE_VALUE) {
        return;
    }
    char line[32];
    const int length = wsprintfA(line, "0x%08x\r\n", static_cast<unsigned>(address));
    DWORD written = 0;
    WriteFile(g_hits_log, line, static_cast<DWORD>(length), &written, 0);
    FlushFileBuffers(g_hits_log);
}

LONG CALLBACK site_trace_handler(EXCEPTION_POINTERS *info) {
    if (info == 0 || info->ExceptionRecord == 0 ||
        info->ExceptionRecord->ExceptionCode != static_cast<DWORD>(EXCEPTION_BREAKPOINT)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // On real Windows, int3 traps after the 1-byte opcode, so the reported
    // address is one past the site the byte was planted at; accept either
    // that address or the raw one, since a hosting emulator (Wine) is not
    // guaranteed to match that convention exactly.
    const DWORD raw_address =
        static_cast<DWORD>(reinterpret_cast<UINT_PTR>(info->ExceptionRecord->ExceptionAddress));
    for (DWORD index = 0; index < g_site_count; ++index) {
        TracedSite &site = g_sites[index];
        if (!site.patched || site.hit || (site.address != raw_address && site.address != raw_address - 1u)) {
            continue;
        }
        unsigned char *target = reinterpret_cast<unsigned char *>(static_cast<UINT_PTR>(site.address));
        DWORD old_protect = 0;
        if (VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old_protect)) {
            *target = site.original_byte;
            FlushInstructionCache(GetCurrentProcess(), target, 1);
            VirtualProtect(target, 1, old_protect, &old_protect);
        }
        site.hit = 1;
        log_hit(site.address);
        info->ContextRecord->Eip = site.address;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void ot_site_trace_start(int build) {
    char path[MAX_PATH];
    const DWORD length = GetEnvironmentVariableA("OPENTONY_HAL_TRACE_SITES", path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return; // not configured: fully inert, matching every other test switch here
    }
    if (build != 0) {
        ot_log("hal_trace: OPENTONY_HAL_TRACE_SITES set but this is not the canonical build; skipping");
        return;
    }
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ot_log("hal_trace: could not open sites file %s (%u)", path, static_cast<unsigned>(GetLastError()));
        return;
    }
    static char buffer[131072];
    DWORD bytes_read = 0;
    const BOOL ok = ReadFile(file, buffer, sizeof(buffer) - 1, &bytes_read, 0);
    CloseHandle(file);
    if (!ok) {
        ot_log("hal_trace: could not read sites file %s", path);
        return;
    }
    buffer[bytes_read] = 0;

    const char *cursor = buffer;
    int line_index = 0;
    while (*cursor != 0 && g_site_count < kMaxSites) {
        const char *line_start = cursor;
        while (*cursor != 0 && *cursor != '\n' && *cursor != '\r') {
            ++cursor;
        }
        const int line_length = static_cast<int>(cursor - line_start);
        if (*cursor == '\r') {
            ++cursor;
        }
        if (*cursor == '\n') {
            ++cursor;
        }
        if (line_index == 0) {
            if (line_length != 9 || memcmp(line_start, "canonical", 9) != 0) {
                ot_log("hal_trace: %s does not start with the expected 'canonical' build line; skipping", path);
                return;
            }
        } else if (line_length > 0) {
            DWORD address = 0;
            if (parse_hex(line_start, line_length, &address)) {
                TracedSite &site = g_sites[g_site_count];
                site.address = address;
                site.original_byte = 0;
                site.patched = 0;
                site.hit = 0;
                ++g_site_count;
            }
        }
        ++line_index;
    }
    if (g_site_count == 0) {
        ot_log("hal_trace: %s had no usable call-site addresses", path);
        return;
    }

    DWORD patched_count = 0;
    for (DWORD index = 0; index < g_site_count; ++index) {
        TracedSite &site = g_sites[index];
        unsigned char *target = reinterpret_cast<unsigned char *>(static_cast<UINT_PTR>(site.address));
        DWORD old_protect = 0;
        if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old_protect)) {
            continue;
        }
        site.original_byte = *target;
        *target = 0xCC;
        FlushInstructionCache(GetCurrentProcess(), target, 1);
        VirtualProtect(target, 1, old_protect, &old_protect);
        site.patched = 1;
        ++patched_count;
    }
    if (patched_count == 0) {
        ot_log("hal_trace: none of %u sites could be patched", static_cast<unsigned>(g_site_count));
        return;
    }

    char hits_path[MAX_PATH + 16];
    lstrcpynA(hits_path, path, MAX_PATH);
    lstrcatA(hits_path, ".hits");
    g_hits_log =
        CreateFileA(hits_path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    const AddVectoredExceptionHandlerFn add_veh = reinterpret_cast<AddVectoredExceptionHandlerFn>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "AddVectoredExceptionHandler"));
    if (add_veh == 0) {
        ot_log("hal_trace: AddVectoredExceptionHandler unavailable; no sites armed");
        return;
    }
    g_veh_handle = add_veh(1, reinterpret_cast<PVOID>(&site_trace_handler));
    if (g_veh_handle == 0) {
        ot_log("hal_trace: AddVectoredExceptionHandler failed; no sites armed");
        return;
    }
    ot_log("hal_trace: %u/%u call sites armed; hits -> %s", static_cast<unsigned>(patched_count),
        static_cast<unsigned>(g_site_count), hits_path);
}

void ot_site_trace_stop(void) {
    if (g_hits_log != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hits_log);
        g_hits_log = INVALID_HANDLE_VALUE;
    }
}
