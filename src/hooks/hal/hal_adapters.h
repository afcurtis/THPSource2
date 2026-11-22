#ifndef OPENTONY_HOOKS_HAL_ADAPTERS_H
#define OPENTONY_HOOKS_HAL_ADAPTERS_H

/* Shared by the adapters in src/hooks/hal/: retail symbols of this build and
 * the active HAL backends (docs/HAL.md, "Layers"). */

#include "hal_runtime.h"
#include "../native_hooks_generated.h"
#include "../../hal/clock.h"
#include "../../hal/input.h"
#include "../../hal/window.h"

/* Address of services.hal.symbols[index] in the running build (0 = absent;
 * a service whose adapters need an absent symbol is never installed). */
uint32_t ot_hal_symbol(int index);

template <typename T>
inline T ot_hal_function(int index) {
    return reinterpret_cast<T>(static_cast<UINT_PTR>(ot_hal_symbol(index)));
}

inline unsigned char *ot_hal_data(int index) {
    return reinterpret_cast<unsigned char *>(static_cast<UINT_PTR>(ot_hal_symbol(index)));
}

/* Backends of the services that select SDL and started; 0 otherwise. */
const OtHalClockBackend *ot_hal_clock_backend(void);
const OtHalWindowBackend *ot_hal_window_backend(void);
const OtHalInputBackend *ot_hal_input_backend(void);

/* The window service adopts MainWindow now (the `window` entry site's work,
 * for adapters that replace the function the site is on). */
void ot_hal_adopt_main_window(void);

/* OPENTONY_HAL_TEST_GAMEPAD: drive the virtual test gamepad (no-op otherwise). */
void ot_hal_test_input_poll(uint32_t poll);

/* Input adapter lifecycle, driven by hal_runtime.cpp. */
int ot_hal_input_adapter_start(void);
void ot_hal_input_adapter_stop(void);
int ot_hal_input_adapter_describe(char *out, int size);

#endif /* OPENTONY_HOOKS_HAL_ADAPTERS_H */
