# Native hook harness: first replacements

Status: trace-validated (2026-09-26; second batch 2026-09-28)

## Scope

`opentony.dll` (src/hooks/, see docs/HOOKS.md) redirects five retail leaf
functions to native implementations built from
`src/runtime/retail_vector3.hpp`:

| Function | Canonical | Original 2000 | Prologue replaced |
|---|---|---|---|
| Math_Vector3Add | 0x004ca9f0 | 0x004c7c50 | `8b c1 56 8b 4c 24 08` |
| Math_Vector3Sub | 0x004caa20 | 0x004c7c80 | `8b c1 56 8b 4c 24 08` |
| Math_Vector3MulScalar | 0x004caa50 | 0x004c7cb0 | `8b c1 8b 4c 24 04` |
| Math_Vector3ShiftRight | 0x004caab0 | 0x004c7d10 | `8b 54 24 04 8b c1` |
| Math_Vector3Equal | 0x004cab50 | 0x004c7db0 | `8b 44 24 04 8b 11` |

All five are byte-matching VC6 `cpp` modules (vector-math.md). The
original-2000 addresses come from `re/xref/original_2000.yml` (method `hash`,
identical normalized code) and the prologue bytes were checked in both
executables.

## Retail contract reproduced

From the disassembly of the five modules:

- `__thiscall`, one stack argument, `ret 4`; in-place operators return `this`
  in EAX; Equal returns 0/1 in full EAX.
- add/sub/imul keep the low 32 bits; `sar r32, cl` masks the count to 5 bits.
- the argument is re-read through its reference for every component, so an
  argument aliasing (part of) the destination sees earlier writes.

The portable implementation goes through `uint32_t` to avoid C++ signed
overflow and oversized shifts; `src/runtime/retail_vector3_test.cpp` covers
wrap, shift masking and aliasing cases.

## Runtime validation

1. **In-process differential self-test** (`tony hooks selftest --build all`):
   20,000 randomized, edge-value and aliasing cases per function, each run four
   ways (retail code via trampoline, native entry, patched retail entry with
   the toggle on, and off). 0 mismatches for all five functions on both the
   canonical and the original-2000 executable, under Wine and natively on
   Windows 11. The fault variants of Add and Equal were detected in 20,000 of
   20,000 cases.
2. **Lockstep parity** (`tony hooks parity`, clock `lockstep-fast`): every
   scenario captured with every hook retail twice, every hook native, the
   master switch flipping every 3 ms, and `fault:Math_Vector3Add`; recordings
   compared with `compare_recordings(scope="all")`.

   | Scenario | retail vs retail | native | toggle stress | fault | native calls (Add/Sub/Mul/Shr/Eq) |
   |---|---|---|---|---|---|
   | warehouse-idle | MATCH | MATCH | MATCH | DIVERGE frame 0 | 791/279/50/347/1472 |
   | warehouse-straight | MATCH | MATCH | MATCH | DIVERGE frame 0 | 581/227/50/312/1052 |
   | warehouse-turn | MATCH | MATCH | MATCH | DIVERGE frame 0 | 665/255/50/312/1220 |
   | warehouse-ollie-land | MATCH | MATCH | MATCH | DIVERGE frame 0 | 598/214/50/315/1088 |
   | warehouse-turn-ollie | MATCH | MATCH | MATCH | DIVERGE frame 0 | 760/271/50/312/1388 |

   Call counts come from `opentony.stats`, sampled every 250 ms, so they are
   lower bounds.
3. **Real-time play**: `tony scenario capture warehouse-turn-ollie --hooks default
   --frames 900` (retail multimedia-timer pacing, defaults from this record,
   i.e. all five native) completed 900 gameplay frames with 9,524 native calls
   and no retail calls; a 1,200-frame repeat also completed.

## Lockstep clock finding

Two plain retail captures of the same scenario differ from the first frames
on (`accumulated_ms` 528 vs 368; `raw_physics_words[113]` at frame 2),
because the 16 ms `timeSetEvent` callback `SimulationTimeTimerCallback`
(0x004dace0, dwUser = `TimerState` 0x006a05a0) advances the clock
asynchronously. Every frame limiter waits for two ticks: 17 loops spin
`Game_FrameTick` (0x004f7ce0, the Win32 message pump), two spin
`WINMAIN_Sleep` (0x004f6550), and two mode loops (0x00455f90, 0x0047c5eb)
spin on `SimulationPublicTick` with no call at all (0x0045600a, 0x0047c665).
Delivering the callback on the game thread at those sites (and ignoring
`WINMAIN_Sleep` calls from other threads) makes captures bit-identical. A
first version without the game-thread gate produced varying delivery counts
(498 vs 523) and 2/3 failed captures.

The same wait explained why the GDB retail replay (`tony scenario retail`)
stalled on in-process recordings (at frame 3 of warehouse-idle; the log only
prints every sixteenth frame): it applied the deliveries a limiter waits for
before the present, so the limiter latched too late a tick. Resolved in
Phase 4.2 by chronological boundary segments and limiter-site deliveries; all
five Warehouse recordings now replay 256/256 through retail
(simulation-time-callback.md, "Where the deliveries fall in a frame").

## Second batch: the rest of the matched Vec3 family and Vec3s (2026-09-28)

Thirteen more byte-matched leaf operators joined the table
(`re/config/native_hooks.yml`), all with portable implementations in
`src/runtime/retail_vector3.hpp` and host tests in
`src/runtime/retail_vector3_test.cpp`:

| Function | Canonical | Self-test shape | Default |
|---|---|---|---|
| Math_Vector3DivScalar | 0x004caa80 | `thiscall_vec3_divisor_ref` | native |
| Math_Vector3ShiftLeft | 0x004caae0 | `thiscall_vec3_int_ref` | native |
| Math_Vector3DecayShift | 0x004cab10 | `thiscall_vec3_bytes_ref` | native |
| Math_Vector3NotEqual | 0x004cab80 | `thiscall_vec3_compare` | native |
| Math_Vector3sMask12 | 0x004cad60 | `fastcall_vec3s` | native |
| Math_Vector3sSnapNearZero | 0x004cad80 | `fastcall_vec3s` | native |
| Math_Vector3sAdd | 0x004cae10 | `thiscall_vec3s_vec3s_ref` | native |
| Math_Vector3sSub | 0x004cae30 | `thiscall_vec3s_vec3s_ref` | retail (tested) |
| Math_Vector3sDecayShift | 0x004caef0 | `thiscall_vec3s_bytes_ref` | retail (tested) |
| Math_Vector3sEqual | 0x004caf40 | `thiscall_vec3s_compare` | retail (tested) |
| Math_Vector3sNotEqual | 0x004caf70 | `thiscall_vec3s_compare` | retail (tested) |
| Math_Vector3sAddOut | 0x004cb040 | `cdecl_vec3s_out` | retail (tested) |
| Math_Vector3sSubOut | 0x004cb090 | `cdecl_vec3s_out` | retail (tested) |

Contract details the new shapes exercise:

- `idiv` truncates toward zero and faults on a zero divisor or
  `INT32_MIN / -1`; the self-test never generates those (the divisor only ever
  changes to 1 when it aliases the component being divided).
- `sar`/`shl` mask the count to five bits for 16-bit operands too, so a Vec3s
  decay count of 16..31 leaves only sign bits.
- Mask12 and SnapNearZero are genuine `__fastcall` functions (ECX only,
  `ret`); their EAX is scratch.
- The value-returning Vec3s operators are cdecl with the hidden result
  pointer first and read every input word before writing the result, so the
  result may alias an input. A fault variant that writes `x` early is caught
  only through that aliasing (3,546 of 20,000 cases).

The self-test generator now places every pointer at a byte offset into one
arena, so arguments can overlap `this`/the result at word and byte
granularity. Results (`tony hooks selftest`): 18/18 PASS with 0 mismatches on
the canonical build; 13/13 installed PASS on original-2000 (Math_Vector3sSub,
the Vec3s compares and the value-returning Vec3s operators are byte-identical
there at canonical − 0x2da0 but absent from the generated
`re/xref/original_2000.yml` map, so they hook the canonical build only). Every
negative control is detected (DecayShift, SnapNearZero, Vec3sAdd, Vec3sAddOut).

Lockstep parity (`tony hooks parity --clock lockstep-fast`, all five Warehouse
scenarios): retail-b, native-all and native-all-stress MATCH, fault DIVERGE,
in every scenario. Seven of the new hooks ran natively during those captures
(maximum native calls in one scenario: DivScalar 100, ShiftLeft 9, DecayShift
40, NotEqual 255, Vec3sMask12 72, Vec3sSnapNearZero 36, Vec3sAdd 36; the
Vec3s ones only in `warehouse-turn`) and are `trace-validated`. The other six
recorded no native calls, so they stay `tested` and default to retail.

## Decisions

- **Not hooked yet:** the seven remaining value-returning Vec3s operators
  (`0x004cafa0` WrappedSub, `0x004cb0e0`/`0x004cb130` scalar multiplies,
  `0x004cb180` DivScalarOut, `0x004cb1d0`/`0x004cb220` shifts, `0x004cb270`
  Negate) need five further self-test shapes (thiscall with a hidden result,
  scalar-reference and reversed-argument cdecl forms), each covering one or
  two functions, and the int32 out-of-place operators `0x004cabb0..0x004cad30`
  are not yet VC6-matched. They join when a slice that calls them needs a
  native path; they are not needed by any 4.1 ground-movement slice.
- **GDB retail replay stall** (above): it is a timer-delivery problem in the
  GDB replay engine, not a property of the hooked functions, so it moved to
  the `simulation-time-callback` slice, which owns callback delivery.
  Lockstep parity remains the hook gate.
