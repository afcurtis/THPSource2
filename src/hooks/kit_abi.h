#ifndef OPENTONY_KIT_ABI_H
#define OPENTONY_KIT_ABI_H

#include <stdint.h>

/* Phase 5.0: the C interface between opentony.dll (VC6 SDK and CRT, clang-cl)
 * and opentony_native.dll (the Kits of src/runtime/, llvm-mingw, C++20).
 *
 * opentony.dll loads opentony_native.dll next to itself before it installs
 * the hooks and calls its one export, `ot_kit_attach`, which creates the
 * Kits over the live process and returns the native entry point of every Kit
 * hook (the generated table in src/hooks/kits/kit_hooks_generated.cpp). Each
 * entry point has the retail function's own calling convention, so the
 * dispatch stub jumps to it exactly as to a hand-written adapter. */

#define OT_KIT_ABI_VERSION 2u   /* 2 (11b): OtKitHost::arena_base */

typedef struct OtKitEntry {
    const char *name; /* the hook name (re/config/native_hooks.yml) */
    void *function;   /* entry point with the retail calling convention */
} OtKitEntry;

typedef struct OtKitHost {
    uint32_t version;                /* OT_KIT_ABI_VERSION */
    const char *build;               /* the identified executable ("canonical", ...) */
    void (*log)(const char *line);   /* one line into opentony.log */
    uint32_t check_every_access;     /* OPENTONY_LIVE_GUARD: every live access asks VirtualQuery */
    uint64_t arena_base;             /* 11b: host address of guest address 0 (src/hooks/kits/guest_arena.hpp); 0 in a 32-bit process */
} OtKitHost;

/* Returns the entries (and their count), or 0 when the Kits cannot run in
 * this process (the reason is logged). */
typedef const OtKitEntry *(__cdecl *OtKitAttach)(const OtKitHost *host, uint32_t *count);

#define OT_KIT_DLL "opentony_native.dll"
#define OT_KIT_ATTACH "ot_kit_attach"

/* Phase 5.4a: a program with no retail image attaches the same Kits with a resolver instead of the retail
 * functions (a retail function a Kit reports is found by address among the natives and the seams), and calls a
 * native (or seam) by its retail address:
 *     uint32_t __cdecl ot_standalone_call(uint32_t address, const uint32_t *args, uint32_t count) */
#define OT_KIT_ATTACH_STANDALONE "ot_kit_attach_standalone"
#define OT_STANDALONE_CALL "ot_standalone_call"

#endif /* OPENTONY_KIT_ABI_H */
