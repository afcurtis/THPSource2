#ifndef OPENTONY_HOOK_CLOCK_H
#define OPENTONY_HOOK_CLOCK_H

#include "hook_types.h"
#include "../hal/clock.h"

/* Lockstep clock service (services.lockstep_clock in native_hooks.yml). */
enum OtClockMode {
    OT_CLOCK_REALTIME = 0,     /* retail: asynchronous 16 ms multimedia timer */
    OT_CLOCK_LOCKSTEP = 1,     /* one delivery per delivery site, paced to wall clock */
    OT_CLOCK_LOCKSTEP_FAST = 2 /* same deliveries, unpaced (headless verification) */
};

/* Installs the service into 1 + OT_CLOCK_MAX_SITES code slots of 96 bytes.
 * Delivery sites named in `skip` are left to the platform HAL, whose adapter
 * calls ot_clock_deliver itself. Returns 1 when the clock is replaced, 0 for
 * realtime or on any byte mismatch (retail kept). */
int ot_clock_install(const OtLockstepClockSpec *spec, int build, int mode, unsigned char *slots,
    const char *const *skip, unsigned skip_count);
int ot_clock_mode(void);
unsigned ot_clock_deliveries(void);

/* One delivery (ignored off the game thread or while no timer runs). */
extern "C" void __cdecl ot_clock_deliver(void);
/* SimulationTimeTimerCallback in this build; 0 unless the clock is installed. */
uint32_t ot_clock_callback_address(void);
/* Wall-clock source for lockstep pacing (0 = Win32); restarts the pacer. */
void ot_clock_set_time(const OtHalClockBackend *time);
unsigned ot_clock_resyncs(void);

#endif /* OPENTONY_HOOK_CLOCK_H */
