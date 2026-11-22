#include "clock.h"

#include "hook_runtime.h"
#include "session_trace.h"

#include <stdlib.h>
#include <string.h>

extern "C" void *_AddressOfReturnAddress(void);

// Lockstep clock. Retail advances its simulation clock from a 16 ms
// timeSetEvent callback on the winmm thread, and every frame limiter waits
// until SimulationPublicTick has moved twice. Here the asynchronous callback
// returns at once (`ret 0x14`) and the original callback body runs on the game
// thread at each delivery site instead (see services.lockstep_clock in
// re/config/native_hooks.yml). Every frame then sees exactly two deliveries,
// so a run is a pure function of its inputs; that is what makes retail/native
// A/B captures comparable bit for bit.

namespace {

typedef void(__stdcall *TimerCallback)(UINT id, UINT message, DWORD user, DWORD reserved1, DWORD reserved2);

const unsigned kSlotSize = 96u;
const unsigned kStubOffset = 32u;

int g_mode = OT_CLOCK_REALTIME;
unsigned char *g_callback_trampoline = 0;
volatile uint32_t *g_timer_state = 0; // +0 timer id (0 = stopped), +4 interval ms
volatile LONG g_deliveries = 0;
DWORD g_game_thread = 0; // first thread to run Game_FrameTick (the window thread)
uint32_t g_callback_address = 0;

// Wall-clock pacing (lockstep only). The time source starts as Win32 and the
// platform HAL may switch it (clock=sdl); pacing never changes deliveries.
uint64_t OT_HAL_CALL win32_now_ns(void) {
    return static_cast<uint64_t>(GetTickCount()) * 1000000ull;
}

void OT_HAL_CALL win32_sleep_ns(uint64_t ns) {
    Sleep(static_cast<DWORD>((ns + 999999ull) / 1000000ull));
}

const OtHalClockBackend kWin32Time = {"win32", 0, 0, win32_now_ns, win32_sleep_ns};
OtHalClockPacer g_pacer;

// The callback does x87 double arithmetic. On the winmm thread it runs with
// the process default control word (53-bit precision, round to nearest, all
// exceptions masked); the game thread may run with Direct3D's 24-bit
// precision, which would round the accumulators differently. Deliver under
// the timer thread's control word.
const unsigned short kTimerThreadFpuControl = 0x027f;

} // namespace

extern "C" void __cdecl ot_clock_deliver(void) {
    unsigned short saved;
    unsigned short timer_control = kTimerThreadFpuControl;
    uint32_t interval;

    // Only the game thread may advance the clock: helper threads also call
    // WINMAIN_Sleep, and deliveries from them would depend on scheduling.
    if (g_game_thread == 0 || GetCurrentThreadId() != g_game_thread) {
        return;
    }
    if (g_timer_state[0] == 0) {
        return; // no multimedia timer is running: retail would not deliver either
    }
    interval = g_timer_state[1];
    __asm {
        fnstcw saved
        fldcw timer_control
    }
    ((TimerCallback)(void *)g_callback_trampoline)(0, 0, (DWORD)(UINT_PTR)g_timer_state, 0, 0);
    __asm {
        fldcw saved
    }
    InterlockedIncrement((LONG *)&g_deliveries); // VC6 prototypes lack volatile
    {
        // 10g s22: OPENTONY_DELIVERY_LOG=<first>:<last> logs each delivery in that range with the retail code addresses on the stack above this
        // function's return (the delivery site's callers among them; a measurement mode, the standalone's twin is OT_DELIVERY_LOG)
        static int s_parsed = 0;
        static unsigned s_first = 1, s_last = 0;
        if (!s_parsed) {
            s_parsed = 1;
            const char *text = getenv("OPENTONY_DELIVERY_LOG");
            if (text != 0 && *text != '\0') {
                char *next = 0;
                s_first = (unsigned)strtoul(text, &next, 10);
                s_last = *next == ':' ? (unsigned)strtoul(next + 1, 0, 10) : s_first;
            }
        }
        const unsigned count = (unsigned)g_deliveries;
        if (count >= s_first && count <= s_last) {
            const uint32_t *stack = (const uint32_t *)_AddressOfReturnAddress();
            // (only the words below the top of the stack's committed region: near the thread's first frames 64 words run past it)
            MEMORY_BASIC_INFORMATION region;
            unsigned words = 0;
            if (VirtualQuery(stack, &region, sizeof region) != 0) {
                const UINT_PTR end = (UINT_PTR)region.BaseAddress + region.RegionSize;
                words = (unsigned)((end - (UINT_PTR)stack) / 4);
            }
            char line[400];
            unsigned at = 0;
            for (unsigned k = 0; k < 64 && k < words && at + 10 < sizeof line; ++k) {
                const uint32_t word = stack[k];
                if (word >= 0x00401000u && word < 0x00512000u) {
                    line[at++] = ' ';
                    for (int shift = 28; shift >= 0; shift -= 4) {
                        line[at++] = "0123456789ABCDEF"[(word >> shift) & 0xfu];
                    }
                }
            }
            line[at] = '\0';
            ot_log("delivery %u:%s", count, line);
        }
    }
    ot_session_on_delivery((unsigned)g_deliveries);
    if (g_mode == OT_CLOCK_LOCKSTEP) {
        // Pace to wall-clock time without letting it influence the simulation.
        ot_hal_clock_pacer_tick(&g_pacer, interval);
    }
}

namespace {

extern "C" void __cdecl ot_clock_deliver_on_pump(void) {
    if (g_game_thread == 0) {
        g_game_thread = GetCurrentThreadId();
        ot_log("clock: game thread %u (first Game_FrameTick)", (unsigned)g_game_thread);
    }
    ot_clock_deliver();
}

unsigned char *emit_rel(unsigned char *at, unsigned char opcode, const void *to) {
    const LONG relative = (LONG)((const unsigned char *)to - (at + 5));
    at[0] = opcode;
    memcpy(at + 1, &relative, sizeof(relative));
    return at + 5;
}

// pushad; pushfd; call deliver; popfd; popad
unsigned char *emit_deliver(unsigned char *at, void(__cdecl *deliver)(void)) {
    *at++ = 0x60;
    *at++ = 0x9c;
    at = emit_rel(at, 0xe8, (const void *)deliver);
    *at++ = 0x9d;
    *at++ = 0x61;
    return at;
}

int site_ok(const OtPatchSite &site, uint32_t overwrite, const char *name) {
    if (site.address == 0 || overwrite < 5 || overwrite > OT_HOOK_MAX_OVERWRITE) {
        ot_log("clock: %s not available in this build", name);
        return 0;
    }
    if (memcmp((const void *)(UINT_PTR)site.address, site.expected, overwrite) != 0) {
        ot_log("clock: %s bytes differ at %08X", name, (unsigned)site.address);
        return 0;
    }
    return 1;
}

// Writes `opcode rel32 -> destination` at `target`, NOP-filled to `size`.
void patch_branch(unsigned char *target, uint32_t size, unsigned char opcode, const void *destination) {
    unsigned char patch[OT_HOOK_MAX_OVERWRITE];
    const LONG relative = (LONG)((const unsigned char *)destination - (target + 5));
    DWORD old_protect;
    DWORD ignored;

    memset(patch, 0x90, sizeof(patch));
    patch[0] = opcode;
    memcpy(patch + 1, &relative, sizeof(relative));
    VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy(target, patch, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    VirtualProtect(target, size, old_protect, &ignored);
}

void patch_return_0x14(unsigned char *target, uint32_t size) {
    unsigned char patch[OT_HOOK_MAX_OVERWRITE];
    DWORD old_protect;
    DWORD ignored;

    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xc2; // ret 0x14: five dword arguments
    patch[1] = 0x14;
    patch[2] = 0x00;
    VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy(target, patch, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    VirtualProtect(target, size, old_protect, &ignored);
}

} // namespace

static bool skipped(const char *name, const char *const *skip, unsigned skip_count) {
    for (unsigned index = 0; index < skip_count; ++index) {
        if (lstrcmpA(name, skip[index]) == 0) {
            return true;
        }
    }
    return false;
}

int ot_clock_install(const OtLockstepClockSpec *spec, int build, int mode, unsigned char *slots,
    const char *const *skip, unsigned skip_count) {
    const OtPatchSite &callback = spec->callback[build];
    unsigned char *callback_target = (unsigned char *)(UINT_PTR)callback.address;
    uint32_t index;

    if (mode == OT_CLOCK_REALTIME) {
        return 0;
    }
    // All or nothing: a partial lockstep clock would hang the first wait
    // loop left without deliveries.
    if (!site_ok(callback, spec->callback_overwrite, "SimulationTimeTimerCallback") ||
        spec->timer_state[build] == 0 || spec->site_count > OT_CLOCK_MAX_SITES) {
        ot_log("clock: left realtime");
        return 0;
    }
    for (index = 0; index < spec->site_count; ++index) {
        const OtClockSite &site = spec->sites[index];
        if (!site_ok(site.build[build], site.overwrite, site.name)) {
            ot_log("clock: left realtime");
            return 0;
        }
    }
    g_timer_state = (volatile uint32_t *)(UINT_PTR)spec->timer_state[build];

    // Keep the callback body reachable through a trampoline.
    g_callback_trampoline = slots;
    memcpy(g_callback_trampoline, callback_target, spec->callback_overwrite);
    emit_rel(g_callback_trampoline + spec->callback_overwrite, 0xe9, callback_target + spec->callback_overwrite);

    for (index = 0; index < spec->site_count; ++index) {
        const OtClockSite &site = spec->sites[index];
        unsigned char *target = (unsigned char *)(UINT_PTR)site.build[build].address;
        unsigned char *slot = slots + (index + 1u) * kSlotSize;
        if (skipped(site.name, skip, skip_count)) {
            ot_log("clock: delivery site %-44s %08X handed to the platform HAL", site.name,
                (unsigned)site.build[build].address);
            continue;
        }
        if (site.kind == OT_CLOCK_SITE_ENTRY || site.kind == OT_CLOCK_SITE_PUMP) {
            // trampoline: original prologue + jmp back; stub: deliver + jmp trampoline
            unsigned char *stub = slot + kStubOffset;
            memcpy(slot, target, site.overwrite);
            emit_rel(slot + site.overwrite, 0xe9, target + site.overwrite);
            emit_rel(emit_deliver(stub, site.kind == OT_CLOCK_SITE_PUMP ? ot_clock_deliver_on_pump : ot_clock_deliver),
                0xe9, slot);
            FlushInstructionCache(GetCurrentProcess(), slot, kSlotSize);
            patch_branch(target, site.overwrite, 0xe9, stub);
        } else {
            // thunk: deliver, original instruction, ret
            unsigned char *at = emit_deliver(slot, ot_clock_deliver);
            memcpy(at, target, site.overwrite);
            at[site.overwrite] = 0xc3;
            FlushInstructionCache(GetCurrentProcess(), slot, kSlotSize);
            patch_branch(target, site.overwrite, 0xe8, slot);
        }
        ot_log("clock: delivery site %-44s %08X (%s)", site.name, (unsigned)site.build[build].address,
            site.kind == OT_CLOCK_SITE_PUMP ? "pump" : site.kind == OT_CLOCK_SITE_ENTRY ? "entry" : "spin");
    }
    g_mode = mode;
    g_callback_address = callback.address;
    ot_hal_clock_pacer_start(&g_pacer, &kWin32Time, OT_HAL_PACE_REALTIME);
    patch_return_0x14(callback_target, spec->callback_overwrite);
    ot_log("clock: %s (timer callback %08X deferred to the game thread, TimerState %08X)",
        mode == OT_CLOCK_LOCKSTEP ? "lockstep, paced" : "lockstep-fast, unpaced",
        (unsigned)callback.address, (unsigned)spec->timer_state[build]);
    return 1;
}

int ot_clock_mode(void) {
    return g_mode;
}

unsigned ot_clock_deliveries(void) {
    return (unsigned)g_deliveries;
}

uint32_t ot_clock_callback_address(void) {
    return g_callback_address;
}

void ot_clock_set_time(const OtHalClockBackend *time) {
    ot_hal_clock_pacer_start(&g_pacer, time != 0 ? time : &kWin32Time, OT_HAL_PACE_REALTIME);
    ot_log("clock: pacing with %s time", g_pacer.clock->name);
}

unsigned ot_clock_resyncs(void) {
    return g_pacer.resyncs;
}
