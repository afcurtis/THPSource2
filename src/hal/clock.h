#ifndef OPENTONY_HAL_CLOCK_H
#define OPENTONY_HAL_CLOCK_H

/* Clock service (docs/HAL.md, "Clock").
 *
 * Retail advances the simulation from a 16 ms multimedia-timer callback on a
 * winmm thread (SimulationTimeTimerCallback, TimerState); every frame limiter
 * waits until SimulationPublicTick has moved twice. The HAL clock keeps the
 * tick but delivers it on the game thread at fixed delivery sites (the
 * lockstep scheduler), so a run is a pure function of its inputs. The backend
 * only supplies monotonic time and sleeping; it paces deliveries to wall-clock
 * time and never decides how many there are. */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Retail timer interval (TimerState[1]); the game asks for it at start-up. */
#define OT_HAL_CLOCK_TICK_MS 16u

typedef struct OtHalClockBackend {
    const char *name;
    int(OT_HAL_CALL *init)(void); /* 1 on success */
    void(OT_HAL_CALL *shutdown)(void);
    uint64_t(OT_HAL_CALL *now_ns)(void);     /* monotonic, arbitrary origin */
    void(OT_HAL_CALL *sleep_ns)(uint64_t ns); /* at least `ns`, as precisely as the platform allows */
} OtHalClockBackend;

enum OtHalClockPacing {
    OT_HAL_PACE_REALTIME = 0, /* hold each delivery until its wall-clock due time */
    OT_HAL_PACE_UNPACED = 1   /* deliver as fast as the game asks (headless verification) */
};

/* Wall-clock pacing of simulation ticks (src/hal/clock_pacer.cpp; portable,
 * backend-independent). Pacing only delays the game thread; it never changes
 * the number or order of deliveries, so paced and unpaced runs are
 * bit-identical. */
typedef struct OtHalClockPacer {
    const OtHalClockBackend *clock;
    uint32_t pacing;    /* OtHalClockPacing */
    uint64_t origin_ns; /* now_ns() at start */
    uint64_t due_ns;    /* simulated time of the last delivery, from origin */
    uint32_t resyncs;   /* times the pacer gave up catching up (loading) */
} OtHalClockPacer;

/* Longest the game may fall behind wall-clock time before the pacer stops
 * trying to catch up (it would otherwise sprint after every loading screen). */
#define OT_HAL_CLOCK_MAX_LAG_NS 250000000ull

void OT_HAL_CALL ot_hal_clock_pacer_start(OtHalClockPacer *pacer, const OtHalClockBackend *clock, uint32_t pacing);
/* Call once per delivery of `interval_ms`: sleeps until the delivery is due. */
void OT_HAL_CALL ot_hal_clock_pacer_tick(OtHalClockPacer *pacer, uint32_t interval_ms);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_CLOCK_H */
