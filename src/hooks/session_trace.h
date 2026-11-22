#ifndef OPENTONY_HOOK_SESSION_TRACE_H
#define OPENTONY_HOOK_SESSION_TRACE_H

#include "hook_types.h"

/* Session probe (Phase 5.1s). Boots the retail game past its movies, presses
 * scripted keys and logs a per-frame hash of the writable image, so two runs
 * of the same session can be compared (tony session diff). A test switch of
 * the lockstep clock: every variable is read once at startup and the probe
 * does nothing unless OPENTONY_SESSION_TRACE is set.
 *
 *   OPENTONY_SESSION_TRACE=<N>        stop the process after N clock deliveries
 *   OPENTONY_SESSION_EVERY=<k>        hash every k-th delivery (default 2: one per frame)
 *   OPENTONY_SESSION_KEYS=<poll>:<dik>:<hold>[,...]   hold DirectInput key <dik> (hex) for
 *                                     <hold> input polls from poll <poll>
 *   OPENTONY_SESSION_SKIP_MOVIES=1    the startup movies return at once (tony-skip-movies)
 *   OPENTONY_SESSION_DUMP=<delivery>  write the writable image at that delivery
 */

/* Reads the variables and plants the patches. Canonical build only. */
void ot_session_start(int build);

/* One delivery on the game thread, after it ran (count = deliveries so far). */
void ot_session_on_delivery(unsigned count);

#endif /* OPENTONY_HOOK_SESSION_TRACE_H */
