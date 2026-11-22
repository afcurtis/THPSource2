#ifndef OPENTONY_HAL_SDL_INTERNAL_H
#define OPENTONY_HAL_SDL_INTERNAL_H

/* Shared state between the SDL backend files; not part of the HAL. */

#include "sdl_api.h"
#include "sdl_backends.h"

SDL_Window *ot_hal_sdl_window_get(void);
void ot_hal_sdl_input_pad_added(SDL_JoystickID id);
void ot_hal_sdl_input_pad_removed(SDL_JoystickID id);
OtHalSdlStats *ot_hal_sdl_stats_mutable(void);

#endif /* OPENTONY_HAL_SDL_INTERNAL_H */
