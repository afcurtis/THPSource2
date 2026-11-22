#include "sdl_api.h"
#include "sdl_backends.h"

// Clock service on SDL3: monotonic nanoseconds and a precise sleep. SDL's
// timer functions need no SDL_Init.

namespace {

int OT_HAL_CALL clock_init(void) {
    return 1;
}

void OT_HAL_CALL clock_shutdown(void) {
}

uint64_t OT_HAL_CALL clock_now_ns(void) {
    return OT_SDL(SDL_GetTicksNS)();
}

void OT_HAL_CALL clock_sleep_ns(uint64_t ns) {
    OT_SDL(SDL_DelayPrecise)(ns);
}

} // namespace

extern "C" const OtHalClockBackend ot_hal_sdl_clock = {
    "sdl", clock_init, clock_shutdown, clock_now_ns, clock_sleep_ns,
};
