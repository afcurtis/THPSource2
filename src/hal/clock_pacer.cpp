#include "clock.h"

// Wall-clock pacing of simulation ticks. The pacer holds the game thread
// until each delivery is due; it never adds or drops deliveries, so paced and
// unpaced runs of the same inputs are bit-identical.

namespace {

const uint64_t kNanosecondsPerMillisecond = 1000000ull;
const uint64_t kSleepSlackNs = kNanosecondsPerMillisecond; // don't sleep for less

} // namespace

extern "C" void OT_HAL_CALL ot_hal_clock_pacer_start(OtHalClockPacer *pacer, const OtHalClockBackend *clock,
    uint32_t pacing) {
    pacer->clock = clock;
    pacer->pacing = pacing;
    pacer->origin_ns = pacing == OT_HAL_PACE_REALTIME ? clock->now_ns() : 0;
    pacer->due_ns = 0;
    pacer->resyncs = 0;
}

extern "C" void OT_HAL_CALL ot_hal_clock_pacer_tick(OtHalClockPacer *pacer, uint32_t interval_ms) {
    uint64_t elapsed;

    if (pacer->pacing != OT_HAL_PACE_REALTIME) {
        return;
    }
    elapsed = pacer->clock->now_ns() - pacer->origin_ns;
    pacer->due_ns += interval_ms * kNanosecondsPerMillisecond;
    if (pacer->due_ns > elapsed + kSleepSlackNs) {
        pacer->clock->sleep_ns(pacer->due_ns - elapsed);
    } else if (elapsed > pacer->due_ns + OT_HAL_CLOCK_MAX_LAG_NS) {
        pacer->due_ns = elapsed; // fell far behind (loading): don't sprint to catch up
        ++pacer->resyncs;
    }
}
