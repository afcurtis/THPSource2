#include "selftest.h"

#include "hook_runtime.h"

#include <string.h>

#define OT_SELFTEST_CASES 20000u

namespace {

// thiscall with one stack argument == fastcall with an unused EDX slot.
typedef void *(__fastcall *ThiscallOne)(void *self, void *edx, const void *argument);
// Genuine fastcall taking only ECX (the Vec3s Mask12/SnapNearZero helpers).
typedef void (__fastcall *FastcallSelf)(void *self);
// Value-returning Vec3s operators: hidden result pointer first, cdecl.
typedef void *(__cdecl *CdeclOut)(void *out, const void *a, const void *b);

// Every case runs on this arena: for the original three shapes `self` points
// at words[1..3] so that an argument can overlap the destination from either
// side; the other shapes place every pointer at a byte offset into it.
struct Arena {
    int32_t words[6];
    int32_t other[3];
    int32_t scalar;
};

enum ArgumentMode {
    ARG_OTHER = 0,   // a separate vector / scalar
    ARG_SELF,        // argument == this
    ARG_WORD,        // argument points at words[arg_word] (aliasing)
    ARG_BYTES        // byte offsets below (every newer shape)
};

struct Case {
    Arena arena;
    int mode;
    int arg_word;
    int self_byte;
    int arg_byte;
    int arg2_byte;
    int out_byte;
};

const int kOtherByte = (int)(sizeof(int32_t) * 6); // offset of Arena::other

struct Outcome {
    Arena arena;
    int32_t result; // compare result, or word offset of the returned pointer
};

uint32_t g_rng = 0x2545f491u;

uint32_t next_random() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

const int32_t kEdgeValues[] = {
    0, 1, -1, 2, -2, 0x1000, -0x1000, 0x7fff, -0x8000, 0x10000, -0x10000,
    0x7fffffff, (int32_t)0x80000000u, 0x7ffffffe, (int32_t)0x80000001u, 0x40000000,
};

const int32_t kShiftCounts[] = {
    32, 33, 63, 64, -1, -31, -32, 255, 256, 0x7fffffff, (int32_t)0x80000000u,
};

int32_t random_value() {
    const uint32_t choice = next_random() % 10u;
    if (choice < 4u) {
        return (int32_t)next_random();
    }
    if (choice < 7u) {
        return (int32_t)(next_random() % 0x20001u) - 0x10000;
    }
    return kEdgeValues[next_random() % (sizeof(kEdgeValues) / sizeof(kEdgeValues[0]))];
}

int32_t random_shift() {
    const uint32_t choice = next_random() % 4u;
    if (choice < 2u) {
        return (int32_t)(next_random() % 32u);
    }
    if (choice == 2u) {
        return kShiftCounts[next_random() % (sizeof(kShiftCounts) / sizeof(kShiftCounts[0]))];
    }
    return (int32_t)next_random();
}

const int16_t kEdgeShorts[] = {
    0, 1, -1, 2, -2, 0x0fff, 0x1000, -0x1000, 0x7fff, -0x8000, 0x7ffe, -0x7fff, 0x0800, -0x0800,
};

const uint8_t kEdgeCounts[] = {0, 1, 12, 15, 16, 17, 31, 32, 33, 63, 128, 255};

int16_t random_short() {
    const uint32_t choice = next_random() % 10u;
    if (choice < 4u) {
        return (int16_t)next_random();
    }
    if (choice < 7u) {
        return (int16_t)((int32_t)(next_random() % 0x2001u) - 0x1000);
    }
    return kEdgeShorts[next_random() % (sizeof(kEdgeShorts) / sizeof(kEdgeShorts[0]))];
}

uint8_t random_count() {
    const uint32_t choice = next_random() % 5u;
    if (choice < 3u) {
        return (uint8_t)(next_random() % 32u);
    }
    if (choice == 3u) {
        return (uint8_t)next_random();
    }
    return kEdgeCounts[next_random() % (sizeof(kEdgeCounts) / sizeof(kEdgeCounts[0]))];
}

bool is_vec3s_abi(uint32_t abi) {
    return abi == OT_ABI_FASTCALL_VEC3S || abi == OT_ABI_THISCALL_VEC3S_VEC3S_REF ||
        abi == OT_ABI_THISCALL_VEC3S_BYTES_REF || abi == OT_ABI_THISCALL_VEC3S_COMPARE ||
        abi == OT_ABI_CDECL_VEC3S_OUT;
}

// An even offset where a 6-byte Vec3s fits anywhere in the arena.
int random_vec3s_byte() {
    return (int)(next_random() % ((sizeof(Arena) - 6u) / 2u + 1u)) * 2;
}

// A Vec3s argument relative to `self`: separate, identical, overlapping by
// one or two words from either side, or anywhere.
int related_vec3s_byte(int self_byte) {
    switch (next_random() % 4u) {
    case 0:
        return kOtherByte;
    case 1:
        return self_byte;
    case 2: {
        static const int kShift[] = {-4, -2, 2, 4};
        const int byte = self_byte + kShift[next_random() % 4u];
        return byte >= 0 && byte <= (int)sizeof(Arena) - 6 ? byte : self_byte;
    }
    default:
        return random_vec3s_byte();
    }
}

// Byte-count argument: mostly the separate region, else overlapping self or
// anywhere (count bytes then change as earlier components are written).
int random_counts_byte(int self_byte) {
    switch (next_random() % 4u) {
    case 0:
    case 1:
        return kOtherByte + (int)(next_random() % 9u);
    case 2: {
        const int byte = self_byte + (int)(next_random() % 10u);
        return byte <= (int)sizeof(Arena) - 3 ? byte : kOtherByte;
    }
    default:
        return (int)(next_random() % (sizeof(Arena) - 2u));
    }
}

// idiv faults on a zero divisor and on INT32_MIN / -1. The divisor never
// changes during the call except to 1 (when it aliases the component being
// divided), so checking the initial value suffices.
void make_divisor_safe(Case *out) {
    int32_t *divisor = out->mode == ARG_OTHER ? &out->arena.scalar : &out->arena.words[out->arg_word];
    unsigned index;
    if (*divisor == 0) {
        *divisor = 1;
    }
    if (*divisor == -1) {
        for (index = 1; index <= 3; ++index) {
            if (out->arena.words[index] == (int32_t)0x80000000u) {
                out->arena.words[index] += 1;
            }
        }
    }
}

void make_bytes_case(uint32_t abi, Case *out) {
    unsigned index;
    uint8_t *bytes = (uint8_t *)&out->arena;
    const bool shorts = is_vec3s_abi(abi);

    for (index = 0; index < 6; ++index) {
        out->arena.words[index] = shorts
            ? (int32_t)(((uint32_t)(uint16_t)random_short() << 16) | (uint16_t)random_short())
            : random_value();
    }
    if (abi == OT_ABI_THISCALL_VEC3_BYTES_REF || abi == OT_ABI_THISCALL_VEC3S_BYTES_REF) {
        for (index = (unsigned)kOtherByte; index < sizeof(Arena); ++index) {
            bytes[index] = random_count();
        }
    } else {
        int16_t *halves = (int16_t *)(bytes + kOtherByte);
        for (index = 0; index < (sizeof(Arena) - (unsigned)kOtherByte) / 2u; ++index) {
            halves[index] = random_short();
        }
    }
    out->mode = ARG_BYTES;
    out->arg_word = 0;
    out->self_byte = shorts ? random_vec3s_byte() : 4 * (int)(next_random() % 4u);
    out->arg_byte = 0;
    out->arg2_byte = 0;
    out->out_byte = 0;
    switch (abi) {
    case OT_ABI_THISCALL_VEC3_BYTES_REF:
    case OT_ABI_THISCALL_VEC3S_BYTES_REF:
        out->arg_byte = random_counts_byte(out->self_byte);
        break;
    case OT_ABI_THISCALL_VEC3S_VEC3S_REF:
        out->arg_byte = related_vec3s_byte(out->self_byte);
        break;
    case OT_ABI_THISCALL_VEC3S_COMPARE:
        // Mostly equal inputs so both results are covered.
        out->arg_byte = (next_random() % 4u) == 0 ? out->self_byte : kOtherByte;
        if (out->arg_byte == kOtherByte) {
            memcpy(bytes + kOtherByte, bytes + out->self_byte, 6);
            if (next_random() & 1u) {
                bytes[kOtherByte + next_random() % 6u] ^= (uint8_t)(1u << (next_random() % 8u));
            }
        }
        break;
    case OT_ABI_CDECL_VEC3S_OUT:
        out->arg_byte = random_vec3s_byte();
        out->arg2_byte = related_vec3s_byte(out->arg_byte);
        switch (next_random() % 6u) {
        case 0:
            out->out_byte = out->arg_byte;
            break;
        case 1:
            out->out_byte = out->arg2_byte;
            break;
        case 2:
            out->out_byte = related_vec3s_byte(out->arg_byte);
            break;
        default:
            out->out_byte = random_vec3s_byte();
            break;
        }
        break;
    default: // OT_ABI_FASTCALL_VEC3S: self only
        break;
    }
}

void make_case(uint32_t abi, Case *out) {
    unsigned index;
    const int shift = abi == OT_ABI_THISCALL_VEC3_INT_REF && (next_random() & 1u);
    if (abi != OT_ABI_THISCALL_VEC3_VEC3_REF && abi != OT_ABI_THISCALL_VEC3_INT_REF &&
        abi != OT_ABI_THISCALL_VEC3_COMPARE && abi != OT_ABI_THISCALL_VEC3_DIVISOR_REF) {
        make_bytes_case(abi, out);
        return;
    }
    out->self_byte = 4;
    out->arg_byte = 0;
    out->arg2_byte = 0;
    out->out_byte = 0;
    for (index = 0; index < 6; ++index) {
        out->arena.words[index] = shift ? random_shift() : random_value();
    }
    for (index = 0; index < 3; ++index) {
        out->arena.other[index] = random_value();
    }
    out->arena.scalar = shift ? random_shift() : random_value();
    out->arg_word = 0;
    switch (abi) {
    case OT_ABI_THISCALL_VEC3_VEC3_REF:
        out->mode = (int)(next_random() % 4u);
        if (out->mode == 3) {
            // Overlap one word before or after the destination.
            out->mode = ARG_WORD;
            out->arg_word = (next_random() & 1u) ? 0 : 2;
        }
        break;
    case OT_ABI_THISCALL_VEC3_INT_REF:
    case OT_ABI_THISCALL_VEC3_DIVISOR_REF:
        out->mode = (int)(next_random() % 3u);
        if (out->mode == ARG_SELF) {
            out->mode = ARG_WORD; // scalar aliases self.x/.y/.z or a neighbour
        }
        if (out->mode == ARG_WORD) {
            out->arg_word = (int)(next_random() % 5u);
        }
        if (abi == OT_ABI_THISCALL_VEC3_DIVISOR_REF) {
            make_divisor_safe(out);
        }
        break;
    default: // compare: mostly equal inputs so both result values are covered
        out->mode = (int)(next_random() % 4u);
        if (out->mode == 3) {
            out->mode = ARG_OTHER;
        }
        if (out->mode == ARG_OTHER) {
            memcpy(out->arena.other, &out->arena.words[1], sizeof(out->arena.other));
            if (next_random() & 1u) {
                out->arena.other[next_random() % 3u] ^= (int32_t)(1u << (next_random() % 32u));
            }
        } else if (out->mode == ARG_WORD) {
            out->mode = ARG_SELF;
        }
        break;
    }
}

Outcome run_case(uint32_t abi, const Case &input, const void *function) {
    Outcome outcome;
    const void *argument;
    void *self;
    void *result;

    outcome.arena = input.arena;
    if (input.mode == ARG_BYTES) {
        char *base = (char *)&outcome.arena;
        if (abi == OT_ABI_FASTCALL_VEC3S) {
            ((FastcallSelf)function)(base + input.self_byte);
            outcome.result = 0; // void: EAX is scratch
        } else if (abi == OT_ABI_CDECL_VEC3S_OUT) {
            result = ((CdeclOut)function)(base + input.out_byte, base + input.arg_byte, base + input.arg2_byte);
            outcome.result = (int32_t)((char *)result - base);
        } else {
            result = ((ThiscallOne)function)(base + input.self_byte, 0, base + input.arg_byte);
            outcome.result = abi == OT_ABI_THISCALL_VEC3S_COMPARE ? (int32_t)(UINT_PTR)result
                                                                  : (int32_t)((char *)result - base);
        }
        return outcome;
    }
    self = &outcome.arena.words[1];
    if (input.mode == ARG_SELF) {
        argument = self;
    } else if (input.mode == ARG_WORD) {
        argument = &outcome.arena.words[input.arg_word];
    } else if (abi == OT_ABI_THISCALL_VEC3_INT_REF || abi == OT_ABI_THISCALL_VEC3_DIVISOR_REF) {
        argument = &outcome.arena.scalar;
    } else {
        argument = outcome.arena.other;
    }
    result = ((ThiscallOne)function)(self, 0, argument);
    if (abi == OT_ABI_THISCALL_VEC3_COMPARE) {
        outcome.result = (int32_t)(UINT_PTR)result;
    } else {
        outcome.result = (int32_t)(((int32_t *)result) - outcome.arena.words);
    }
    return outcome;
}

bool same_outcome(const Outcome &left, const Outcome &right) {
    return left.result == right.result && memcmp(&left.arena, &right.arena, sizeof(left.arena)) == 0;
}

void describe(char *out, const char *label, const Outcome &outcome) {
    wsprintfA(out, "%s: result=%d words=[%d %d %d %d %d %d] other=[%d %d %d] scalar=%d", label,
        (int)outcome.result, (int)outcome.arena.words[0], (int)outcome.arena.words[1],
        (int)outcome.arena.words[2], (int)outcome.arena.words[3], (int)outcome.arena.words[4],
        (int)outcome.arena.words[5], (int)outcome.arena.other[0], (int)outcome.arena.other[1],
        (int)outcome.arena.other[2], (int)outcome.arena.scalar);
}

// ---- Kit hooks (Phase 5.0) -------------------------------------------------
//
// A Kit hook works on the game's globals, so a case compares the whole of the
// executable's writable sections: run the retail function (trampoline), keep
// what it left, restore, run the patched entry with the hook active, compare
// EAX (when the callers use it) and every byte. Only `selftest: words` hooks
// (cdecl, any words are legal arguments in the initial state) are called; the
// rest are covered live by `tony hooks parity`. The test runs before WinMain,
// on the initial .data/.bss.

#define OT_KIT_SELFTEST_CASES 64u
#define OT_KIT_TIMING_CALLS 200000u
#define OT_KIT_MAX_RANGES 8u

typedef uint32_t(__cdecl *Cdecl0)(void);
typedef uint32_t(__cdecl *Cdecl1)(uint32_t);
typedef uint32_t(__cdecl *Cdecl2)(uint32_t, uint32_t);
typedef uint32_t(__cdecl *Cdecl3)(uint32_t, uint32_t, uint32_t);
typedef uint32_t(__cdecl *Cdecl4)(uint32_t, uint32_t, uint32_t, uint32_t);

struct ImageRange {
    unsigned char *at;
    DWORD size;
};

unsigned writable_ranges(ImageRange *out) {
    unsigned char *base = (unsigned char *)GetModuleHandleA(0);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    unsigned count = 0;
    unsigned index;
    for (index = 0; index < nt->FileHeader.NumberOfSections && count < OT_KIT_MAX_RANGES; ++index, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0) {
            out[count].at = base + section->VirtualAddress;
            out[count].size = section->Misc.VirtualSize;
            ++count;
        }
    }
    return count;
}

uint32_t call_cdecl_words(const void *function, unsigned count, const uint32_t *a) {
    switch (count) {
    case 0: return ((Cdecl0)function)();
    case 1: return ((Cdecl1)function)(a[0]);
    case 2: return ((Cdecl2)function)(a[0], a[1]);
    case 3: return ((Cdecl3)function)(a[0], a[1], a[2]);
    default: return ((Cdecl4)function)(a[0], a[1], a[2], a[3]);
    }
}

void copy_ranges(unsigned char *to, const ImageRange *ranges, unsigned count, int save) {
    unsigned index;
    for (index = 0; index < count; ++index) {
        if (save) {
            memcpy(to, ranges[index].at, ranges[index].size);
        } else {
            memcpy(ranges[index].at, to, ranges[index].size);
        }
        to += ranges[index].size;
    }
}

// Retail's assertion macro stores its source file and line here before every
// M3dUtils_InBetween, passing or not; the natives report only a failed
// assertion and never store them (equipment-select-screen.md). Canonical
// build only, like the Kit hooks.
const uint32_t kAssertLocation[] = {0x0056881cu, 0x00568638u};

int ignored_byte(uint32_t address) {
    unsigned index;
    for (index = 0; index < sizeof(kAssertLocation) / sizeof(kAssertLocation[0]); ++index) {
        if (address - kAssertLocation[index] < 4u) {
            return 1;
        }
    }
    return 0;
}

int differs_from(const unsigned char *saved, const ImageRange *ranges, unsigned count, uint32_t *first) {
    unsigned index;
    DWORD offset;
    for (index = 0; index < count; ++index) {
        if (memcmp(saved, ranges[index].at, ranges[index].size) != 0) {
            for (offset = 0; offset < ranges[index].size; ++offset) {
                if (saved[offset] != ranges[index].at[offset] &&
                    !ignored_byte((uint32_t)(UINT_PTR)(ranges[index].at + offset))) {
                    *first = (uint32_t)(UINT_PTR)(ranges[index].at + offset);
                    return 1;
                }
            }
        }
        saved += ranges[index].size;
    }
    return 0;
}

// 1 when the hook failed
int run_kit_hook(HANDLE file, OtHookState *hook, char *line);

void append(HANDLE file, const char *text) {
    DWORD written;
    WriteFile(file, text, (DWORD)lstrlenA(text), &written, 0);
    WriteFile(file, "\r\n", 2, &written, 0);
}

int run_kit_hook(HANDLE file, OtHookState *hook, char *line) {
    const OtHookSpec *spec = hook->spec;
    const uint8_t saved_active = hook->active;
    ImageRange ranges[OT_KIT_MAX_RANGES];
    unsigned count;
    unsigned index;
    unsigned case_index;
    unsigned mismatches = 0;
    DWORD total = 0;
    unsigned char *initial;
    unsigned char *after_retail;

    if (!spec->kit_selftest_words || spec->kit_stack_args > 4) {
        wsprintfA(line, "LIVE %s (Kit hook installed; checked by tony hooks parity)", spec->name);
        ot_log("selftest: %s", line);
        append(file, line);
        return 0;
    }
    count = writable_ranges(ranges);
    for (index = 0; index < count; ++index) {
        total += ranges[index].size;
    }
    initial = (unsigned char *)VirtualAlloc(0, 2 * total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (initial == 0) {
        wsprintfA(line, "FAIL %s (no memory for %u bytes of snapshots)", spec->name, (unsigned)(2 * total));
        append(file, line);
        return 1;
    }
    after_retail = initial + total;
    g_rng = 0x6a09e667u ^ spec->address[0];
    for (case_index = 0; case_index < OT_KIT_SELFTEST_CASES; ++case_index) {
        uint32_t a[4];
        uint32_t first = 0;
        uint32_t retail;
        uint32_t native;
        for (index = 0; index < 4; ++index) {
            a[index] = (uint32_t)random_value();
        }
        copy_ranges(initial, ranges, count, 1);
        retail = call_cdecl_words(hook->trampoline, spec->kit_stack_args, a);
        copy_ranges(after_retail, ranges, count, 1);
        copy_ranges(initial, ranges, count, 0);
        hook->active = 1;
        native = call_cdecl_words(hook->target, spec->kit_stack_args, a);
        hook->active = 0;
        if ((spec->kit_returns && retail != native) || differs_from(after_retail, ranges, count, &first)) {
            if (mismatches < 3) {
                wsprintfA(line, "MISMATCH %s case %u args %08x %08x eax retail %08x native %08x first byte %08x",
                    spec->name, case_index, a[0], a[1], retail, native, first);
                append(file, line);
                ot_log("selftest: %s", line);
            }
            ++mismatches;
        }
        copy_ranges(initial, ranges, count, 0);
    }
    {
        // the cost of the bridge: the same call through the trampoline (retail) and the patched entry
        // (dispatch stub, generated adapter, Kit), in nanoseconds per call
        const uint32_t a[4] = {0x12345u, 7u, 0u, 0u};
        LARGE_INTEGER frequency, t0, t1, t2;
        unsigned calls;
        copy_ranges(initial, ranges, count, 1);
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&t0);
        for (calls = 0; calls < OT_KIT_TIMING_CALLS; ++calls) {
            call_cdecl_words(hook->trampoline, spec->kit_stack_args, a);
        }
        QueryPerformanceCounter(&t1);
        hook->active = 1;
        for (calls = 0; calls < OT_KIT_TIMING_CALLS; ++calls) {
            call_cdecl_words(hook->target, spec->kit_stack_args, a);
        }
        hook->active = 0;
        QueryPerformanceCounter(&t2);
        copy_ranges(initial, ranges, count, 0);
        wsprintfA(line, "TIME %s retail %u ns/call, native %u ns/call (%u calls)", spec->name,
            (unsigned)((t1.QuadPart - t0.QuadPart) * 1000000000 / frequency.QuadPart / OT_KIT_TIMING_CALLS),
            (unsigned)((t2.QuadPart - t1.QuadPart) * 1000000000 / frequency.QuadPart / OT_KIT_TIMING_CALLS),
            OT_KIT_TIMING_CALLS);
        ot_log("selftest: %s", line);
        append(file, line);
    }
    VirtualFree(initial, 0, MEM_RELEASE);
    hook->active = saved_active;
    wsprintfA(line, "%s %s cases=%u mismatches=%u (Kit hook, %u bytes of image compared)", mismatches ? "FAIL" : "PASS",
        spec->name, OT_KIT_SELFTEST_CASES, mismatches, (unsigned)total);
    ot_log("selftest: %s", line);
    append(file, line);
    return mismatches != 0;
}

} // namespace

int ot_selftest_run(const char *dir) {
    char path[MAX_PATH];
    char line[512];
    HANDLE file;
    unsigned index;
    int failures = 0;

    lstrcpynA(path, dir, MAX_PATH - 20);
    lstrcatA(path, "opentony.selftest");
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    for (index = 0; index < ot_hook_count(); ++index) {
        OtHookState *hook = ot_hook_state(index);
        const OtHookSpec *spec = hook->spec;
        const uint8_t saved_active = hook->active;
        const void *native = *spec->native;
        unsigned mismatches = 0;
        unsigned fault_detected = 0;
        unsigned case_index;

        if (!hook->installed) {
            wsprintfA(line, "SKIP %s (not installed)", spec->name);
            ot_log("selftest: %s", line);
            append(file, line);
            continue;
        }
        if (spec->abi == OT_ABI_KIT) {
            failures += run_kit_hook(file, hook, line);
            continue;
        }
        if (spec->abi == OT_ABI_CDECL_OVERLAY_OPEN) {
            // With no mod it is retail's own function; with one it serves a file. Neither has a
            // retail/native comparator: mod_overlay_core_test (ctest) and tests/test_mod_overlay.py (OPENTONY_WINE_TESTS=1) cover it.
            wsprintfA(line, "SKIP %s (overlay: no comparator)", spec->name);
            ot_log("selftest: %s", line);
            append(file, line);
            continue;
        }
        g_rng = 0x2545f491u ^ (index * 0x9e3779b9u);
        for (case_index = 0; case_index < OT_SELFTEST_CASES; ++case_index) {
            Case input;
            make_case(spec->abi, &input);
            const Outcome retail = run_case(spec->abi, input, hook->trampoline);
            const Outcome direct = run_case(spec->abi, input, native);
            hook->active = 1;
            const Outcome entry_native = run_case(spec->abi, input, hook->target);
            hook->active = 0;
            const Outcome entry_retail = run_case(spec->abi, input, hook->target);
            if (!same_outcome(retail, direct) || !same_outcome(retail, entry_native) ||
                !same_outcome(retail, entry_retail)) {
                if (mismatches < 3) {
                    wsprintfA(line, "MISMATCH %s case %u mode %d arg_word %d bytes self %d arg %d arg2 %d out %d",
                        spec->name, case_index, input.mode, input.arg_word, input.self_byte, input.arg_byte,
                        input.arg2_byte, input.out_byte);
                    append(file, line);
                    ot_log("selftest: %s", line);
                    describe(line, "  retail", retail);
                    append(file, line);
                    describe(line, "  native", direct);
                    append(file, line);
                }
                ++mismatches;
            }
            if (spec->fault != 0) {
                const Outcome faulty = run_case(spec->abi, input, *spec->fault);
                fault_detected += !same_outcome(retail, faulty);
            }
        }
        hook->active = saved_active;
        wsprintfA(line, "%s %s cases=%u mismatches=%u", mismatches ? "FAIL" : "PASS", spec->name,
            OT_SELFTEST_CASES, mismatches);
        if (spec->fault != 0) {
            // The negative control must be visible, otherwise the comparison
            // itself is not testing anything.
            wsprintfA(line + lstrlenA(line), " fault_detected=%u", fault_detected);
            if (fault_detected == 0) {
                ++mismatches;
                lstrcatA(line, " (NEGATIVE CONTROL NOT DETECTED)");
            }
        }
        ot_log("selftest: %s", line);
        append(file, line);
        failures += mismatches != 0;
    }
    wsprintfA(line, "%s build=%s failures=%d", failures ? "RESULT FAIL" : "RESULT PASS", ot_build_id(),
        failures);
    append(file, line);
    CloseHandle(file);
    return failures;
}
