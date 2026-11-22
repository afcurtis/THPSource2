#ifndef OPENTONY_HOOKS_HAL_RUNTIME_H
#define OPENTONY_HOOKS_HAL_RUNTIME_H

#include <windows.h>

#include "../hook_types.h"
#include "../../hal/hal.h"

/* Platform HAL inside the hook DLL (docs/HAL.md).
 *
 * Configuration: [hal] in opentony.ini, then OPENTONY_HAL
 * ("input=sdl,clock=sdl", "all=sdl", "none", "probe"). Every service
 * defaults to retail. The HAL starts at the Game_Startup entry site and stops
 * after WINMAIN_ShutDown, both on the game thread. */

/* Reads the configuration; `ini` may be 0 when no opentony.ini exists. */
void ot_hal_configure(const char *ini);
const OtHalConfig *ot_hal_config(void);
/* Puts one service back to retail before anything is installed for it. */
void ot_hal_disable(int service);

/* Patches the entry sites into OT_HAL_MAX_SITES code slots of 96 bytes.
 * `dll_dir` (with a trailing separator) is where SDL3.dll is looked up first. */
int ot_hal_install(const OtHalSpec *spec, int build, unsigned char *slots, const char *dll_dir);

/* Called from the monitor thread (~50 Hz): test switches such as
 * OPENTONY_HAL_QUIT_AFTER_MS. */
void ot_hal_poll(void);

/* One line per service for opentony.stats. */
int ot_hal_describe(char *out, int size);
/* What each started service is doing (adopted window, input polls, ...). */
int ot_hal_describe_services(char *out, int size);

/* Names of the lockstep delivery sites the HAL clock replaces (its adapter
 * delivers itself); 0 unless clock=sdl. Valid after ot_hal_configure. */
int ot_hal_clock_names(const OtHalSpec *spec, const char **names, int capacity);

#endif /* OPENTONY_HOOKS_HAL_RUNTIME_H */
