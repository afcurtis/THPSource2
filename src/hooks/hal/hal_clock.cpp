#include "hal_adapters.h"

#include "../clock.h"
#include "../hook_runtime.h"

// Clock adapters (clock=sdl). The lockstep scheduler delivers the simulation
// tick on the game thread; with the HAL clock no multimedia timer is created
// at all. D3DTimer_Init keeps TimerState exactly as retail fills it, except
// that the "timer id" names no OS timer (deliveries only check it is
// nonzero). The Win32 timer resolution call (timeBeginPeriod) has no
// equivalent: the clock backend sleeps precisely on its own.

namespace {

// TimerState (re/symbols/globals.yml): +0 id, +4 interval ms, +8 period,
// +0xc accumulated ms, +0x10 callback. TimerCaps: wPeriodMin, wPeriodMax.
const uint32_t kTimerId = 1u;
const uint32_t kIntervalMs = 16u;      // D3DTimer_Init asks for 16 ms
const uint32_t kMinimumPeriod = 0x10u; // retail clamps the period to at least 16
const uint32_t kCapsMinimum = 1u;      // what Windows reports: 1 ms ..
const uint32_t kCapsMaximum = 1000000u; // .. 1000 s

// D3DTimer_Init's TimerTickFrames constants (interval in 60 Hz frames).
const double kFramesPerSecond = 60.0;
const double kSecondsPerMillisecond = 0.001;

typedef int(__cdecl *OnexitFunction)(void);
typedef OnexitFunction(__cdecl *OnexitRegister)(OnexitFunction);

// Retail: fild qword (interval); fmul [60.0]; fmul [0.001]; fstp. Done the
// same way here so the result rounds exactly as retail under the game
// thread's current x87 control word.
void store_tick_frames(uint32_t interval, double *out) {
    uint64_t wide = interval;
    const double *frames = &kFramesPerSecond;
    const double *seconds = &kSecondsPerMillisecond;
    __asm {
        mov eax, frames
        mov ecx, seconds
        mov edx, out
        fild qword ptr [wide]
        fmul qword ptr [eax]
        fmul qword ptr [ecx]
        fstp qword ptr [edx]
    }
}

int __cdecl hal_D3DTimer_Shutdown(void) {
    volatile uint32_t *state = reinterpret_cast<volatile uint32_t *>(ot_hal_data(OT_HAL_SYM_TimerState));
    if (state[0] != 0) { // D3DTIMER_IsActive
        state[0] = 0;    // timeKillEvent: nothing to kill
    }
    return 0;
}

void __cdecl hal_D3DTimer_Init(void) {
    volatile uint32_t *state = reinterpret_cast<volatile uint32_t *>(ot_hal_data(OT_HAL_SYM_TimerState));
    volatile uint32_t *caps = reinterpret_cast<volatile uint32_t *>(ot_hal_data(OT_HAL_SYM_TimerCaps));
    state[0] = 0;
    state[1] = 0;
    state[2] = 0;
    state[3] = 0;
    state[4] = 0;
    caps[0] = kCapsMinimum;
    caps[1] = kCapsMaximum;

    // Period choice, as retail computes it from TIMECAPS.
    uint32_t period = caps[0] < kMinimumPeriod + 1u ? kMinimumPeriod : caps[0];
    state[2] = caps[1];
    if (period < caps[1]) {
        state[2] = caps[0] < kMinimumPeriod + 1u ? kMinimumPeriod : caps[0];
    }
    // D3DTimer_StartEvent(&TimerState, 16, SimulationTimeTimerCallback)
    state[4] = ot_clock_callback_address();
    state[1] = kIntervalMs;
    if (kIntervalMs < state[2]) {
        state[1] = state[2];
    }
    state[0] = kTimerId;
    store_tick_frames(state[1], reinterpret_cast<double *>(ot_hal_data(OT_HAL_SYM_TimerTickFrames)));
    ot_hal_function<OnexitRegister>(OT_HAL_SYM__onexit)(
        ot_hal_function<OnexitFunction>(OT_HAL_SYM_D3DTimer_Shutdown));
    ot_log("hal clock: D3DTimer_Init: interval %u ms, period %u, no OS timer", (unsigned)state[1], (unsigned)state[2]);
}

// Retail: Sleep(ms). As a lockstep delivery site it delivered first.
void __cdecl hal_WINMAIN_Sleep(DWORD milliseconds) {
    ot_clock_deliver();
    const OtHalClockBackend *clock = ot_hal_clock_backend();
    if (clock != 0) {
        clock->sleep_ns(static_cast<uint64_t>(milliseconds) * 1000000ull);
    } else {
        Sleep(milliseconds); // SDL did not start: Win32 time
    }
}

} // namespace

OT_REGISTER_HAL_ADAPTER(D3DTimer_Init, hal_D3DTimer_Init)
OT_REGISTER_HAL_ADAPTER(D3DTimer_Shutdown, hal_D3DTimer_Shutdown)
OT_REGISTER_HAL_ADAPTER(WINMAIN_Sleep, hal_WINMAIN_Sleep)
