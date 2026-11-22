#ifndef OPENTONY_HOOK_RUNTIME_H
#define OPENTONY_HOOK_RUNTIME_H

#include <windows.h>

#include "hook_types.h"

/* Runtime state of one native hook. The dispatch stub reads `active` and bumps
 * one of the two counters on every call, so these live at fixed addresses. */
typedef struct OtHookState {
    const OtHookSpec *spec;
    unsigned char *target;     /* retail entry (patched with a jmp to stub) */
    unsigned char *trampoline; /* original prologue + jmp back: the retail path */
    unsigned char *stub;       /* active ? native : trampoline */
    volatile uint8_t active;   /* enabled && master && installed */
    uint8_t enabled;           /* configuration: native requested */
    uint8_t fault;             /* configuration: negative-control variant */
    uint8_t installed;
    volatile LONG native_calls;
    volatile LONG retail_calls;
} OtHookState;

int ot_hooks_attach(HMODULE self);
void ot_hooks_detach(int process_terminating);

unsigned ot_hook_count(void);
OtHookState *ot_hook_state(unsigned index);
const char *ot_build_id(void);

/* Master switch (F9 in game): routes every installed hook to native (1) or
 * back to retail (0) without touching the configured per-hook selection. */
void ot_hooks_set_master(int enabled);
int ot_hooks_master(void);

void ot_log(const char *format, ...);

/* Writes opentony.stats now (the monitor thread does it every 250 ms). */
void ot_write_stats(void);

/* `out` = the output directory (OPENTONY_LOG_DIR or the DLL's) + `name`. */
void ot_output_path(char *out, const char *name);

#endif /* OPENTONY_HOOK_RUNTIME_H */
