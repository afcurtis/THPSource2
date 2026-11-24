#ifndef OPENTONY_HOOKS_MOD_OVERLAY_H
#define OPENTONY_HOOKS_MOD_OVERLAY_H

#ifdef _WIN32
#include <windows.h>
#else
#include "win32_posix.h"
#endif

#include "mod_overlay_core.h"

/* Loose-file overlay (Phase 5.2, docs/HOOKS.md "Mods"): files in a mod folder
 * answer the game's own opens before ALL.PKR does. */

/* Finds the mods and indexes their files. `dll_dir` is the directory of
 * opentony.dll (the game directory, with a trailing separator); `ini` is
 * opentony.ini or 0. Returns the number of overlay files. */
unsigned ot_overlay_configure(const char *dll_dir, const char *ini);

/* The mod file that answers a path the game opens, or 0. `key` receives the
 * normalised path (for the trace). */
const OtOverlayEntry *ot_overlay_lookup(const char *request, char *key, unsigned key_size);

/* 0 off, 1 log every replaced file, 2 log every open. */
int ot_overlay_trace(void);

unsigned ot_overlay_file_count(void);

#endif /* OPENTONY_HOOKS_MOD_OVERLAY_H */
