#ifndef OPENTONY_HOOK_TYPES_H
#define OPENTONY_HOOK_TYPES_H

#include <stdint.h>

/* Types for the generated native hook table (native_hooks_generated.h, from
 * re/config/native_hooks.yml via `tony hooks generate`). */

#define OT_HOOK_MAX_OVERWRITE 16u
#define OT_HOOK_MAX_BUILDS 4u

/* Argument shape of the retail function; selects the self-test comparator. */
enum OtHookAbi {
    OT_ABI_THISCALL_VEC3_VEC3_REF = 1, /* Vec3 *__thiscall f(const Vec3 &) */
    OT_ABI_THISCALL_VEC3_INT_REF = 2,  /* Vec3 *__thiscall f(const int &) */
    OT_ABI_THISCALL_VEC3_COMPARE = 3,  /* int __thiscall f(const Vec3 &) */
    OT_ABI_THISCALL_VEC3_DIVISOR_REF = 4, /* Vec3 *__thiscall f(const int &divisor), never 0 or INT_MIN/-1 */
    OT_ABI_THISCALL_VEC3_BYTES_REF = 5,   /* Vec3 *__thiscall f(const unsigned char *counts) */
    OT_ABI_FASTCALL_VEC3S = 6,            /* void __fastcall f(Vec3s *) */
    OT_ABI_THISCALL_VEC3S_VEC3S_REF = 7,  /* Vec3s *__thiscall f(const Vec3s &) */
    OT_ABI_THISCALL_VEC3S_BYTES_REF = 8,  /* Vec3s *__thiscall f(const unsigned char *counts) */
    OT_ABI_THISCALL_VEC3S_COMPARE = 9,    /* int __thiscall f(const Vec3s &) */
    OT_ABI_CDECL_VEC3S_OUT = 10,          /* Vec3s f(const Vec3s &, const Vec3s &): hidden result pointer first */
    OT_ABI_KIT = 11,                      /* a Kit hook: generated adapter in opentony_native.dll (kit_abi.h) */
    OT_ABI_CDECL_OVERLAY_OPEN = 12        /* int f(const char *path, int mode): the loose-file overlay (5.2); no comparator */
};

/* re/native/functions.yml status (only `tested` and later can be hooked). */
enum OtNativeStatus {
    OT_NATIVE_TESTED = 2,
    OT_NATIVE_TRACE_VALIDATED = 3,
    OT_NATIVE_INTEGRATED = 4
};

typedef struct OtBuildIdentity {
    const char *id;
    uint32_t pe_timestamp;
    uint32_t size_of_image;
} OtBuildIdentity;

typedef struct OtHookSpec {
    const char *name;
    uint32_t abi;
    uint32_t overwrite;
    uint8_t expected[OT_HOOK_MAX_OVERWRITE];
    uint32_t native_status;
    uint32_t default_enabled;
    void *const *native; /* address of the registered native entry point (a Kit hook's is filled at startup) */
    void *const *fault;  /* negative-control variant, or 0 */
    uint32_t address[OT_HOOK_MAX_BUILDS]; /* per build; 0 = absent */
    /* Kit hooks: the stack arguments, whether the self-test may call the
     * function with any words (`selftest: words`; cdecl only), and whether its
     * callers use EAX (`returns`) */
    uint32_t kit_stack_args;
    uint32_t kit_selftest_words;
    uint32_t kit_returns;
} OtHookSpec;

/* One retail code site a platform service patches, per build. */
typedef struct OtPatchSite {
    uint32_t address; /* 0 = absent */
    uint8_t expected[OT_HOOK_MAX_OVERWRITE];
} OtPatchSite;

#define OT_CLOCK_MAX_SITES 8u

enum OtClockSiteKind {
    OT_CLOCK_SITE_ENTRY = 1, /* deliver, then run the function */
    OT_CLOCK_SITE_SPIN = 2,  /* call a thunk that delivers, then runs the instruction */
    OT_CLOCK_SITE_PUMP = 3   /* entry site whose first caller is the game thread */
};

typedef struct OtClockSite {
    const char *name;
    uint32_t kind;
    uint32_t overwrite;
    OtPatchSite build[OT_HOOK_MAX_BUILDS];
} OtClockSite;

/* services.lockstep_clock in re/config/native_hooks.yml. */
typedef struct OtLockstepClockSpec {
    uint32_t callback_overwrite;
    OtPatchSite callback[OT_HOOK_MAX_BUILDS];
    uint32_t timer_state[OT_HOOK_MAX_BUILDS];
    uint32_t site_count;
    OtClockSite sites[OT_CLOCK_MAX_SITES];
} OtLockstepClockSpec;

/* services.hal: game-thread entry sites where the platform HAL starts and
 * stops (docs/HAL.md). */
#define OT_HAL_MAX_SITES 8u

enum OtHalSiteRole {
    OT_HAL_ROLE_STARTUP = 1,
    OT_HAL_ROLE_SHUTDOWN = 2,
    OT_HAL_ROLE_WINDOW = 3, /* MainWindow exists; the window service may adopt it */
    OT_HAL_ROLE_VIDEO = 4,  /* the Direct3D device exists (video tracing, video service) */
    OT_HAL_ROLE_DRAW_LIST = 5, /* D3D_DrawPolygonList(ordering table): the video shadow walks it first */
    OT_HAL_ROLE_TEXTURE = 6    /* a texture record's surface is about to be refilled or released */
};

enum OtHalSiteKind {
    OT_HAL_SITE_BEFORE = 1, /* callback, then the function */
    OT_HAL_SITE_AFTER = 2,  /* the function, then the callback (argument-less cdecl only) */
    OT_HAL_SITE_BEFORE_ARG = 3 /* callback with the function's first stack argument, then the function */
};

typedef struct OtHalSite {
    const char *name;
    uint32_t role;
    uint32_t kind;
    uint32_t overwrite;
    OtPatchSite build[OT_HOOK_MAX_BUILDS];
} OtHalSite;

/* services.hal.replacements: a retail hook point whose entry becomes
 * `jmp adapter` when its service selects SDL. */
typedef struct OtHalReplacement {
    const char *name;
    uint32_t service; /* OtHalService (src/hal/hal.h) */
    uint32_t overwrite;
    void *const *adapter; /* registered with OT_REGISTER_HAL_ADAPTER */
    OtPatchSite build[OT_HOOK_MAX_BUILDS];
} OtHalReplacement;

/* services.hal.symbols: retail code or data an adapter uses, per build
 * (0 = absent). Indexed by the generated OT_HAL_SYM_* constants. */
typedef struct OtHalSymbol {
    const char *name;
    uint32_t address[OT_HOOK_MAX_BUILDS];
} OtHalSymbol;

typedef struct OtHalSpec {
    uint32_t onexit[OT_HOOK_MAX_BUILDS]; /* the game's CRT _onexit, per build */
    uint32_t site_count;
    OtHalSite sites[OT_HAL_MAX_SITES];
    uint32_t replacement_count;
    const OtHalReplacement *replacements;
    uint32_t symbol_count;
    const OtHalSymbol *symbols;
} OtHalSpec;

/* A native implementation registers its entry point under the hook name. The
 * generated table refers to these symbols. */
#ifdef __cplusplus
#define OT_REGISTER_NATIVE(hook, function) \
    extern "C" void *const ot_native_##hook = reinterpret_cast<void *>(&function);
#define OT_REGISTER_FAULT(hook, function) \
    extern "C" void *const ot_fault_##hook = reinterpret_cast<void *>(&function);
/* A HAL adapter replaces the retail hook point `hook` (services.hal.replacements). */
#define OT_REGISTER_HAL_ADAPTER(hook, function) \
    extern "C" void *const ot_hal_adapter_##hook = reinterpret_cast<void *>(&function);
#endif

#endif /* OPENTONY_HOOK_TYPES_H */
