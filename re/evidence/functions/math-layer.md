# The math layer: GTE emulation, rotation and vector helpers, PSX library stubs (Phase 4.14a)

Status: observed, replayed against retail forced calls (see "Validation").

The PC build of the game keeps the PlayStation's geometry engine alive as plain C: the GTE "registers" are
globals in `0x006a3e00..0x006a3f20`, every libgte call of the PS1 code is a function that reads some of them,
writes the MAC / IR registers and clamps, and the game's own fixed-point helpers (rotation matrices of three
angles, normalisation, arctangent, Q12 dot and cross products) are built on those operations. The 4.14a range
is `0x004e2070..0x004e3d1f` (the GTE), `0x004e7c60..0x004e8d3f` (the helpers and the PSX library stubs),
`0x004f3980`, `0x004f39b0` and `0x004f5f50..0x004f60d0` (trigonometry, the Q12 products, the frame clock and the
park file test). Ghidra listed 97 of the 170 functions in it; the other 73 (ten dead GTE operations, the
2 + 2 + 41 one-line PSX stubs the stub run contains, two matrix routines, the 20 static-initialisation thunks
of `0x004e3d20..0x004e3e60`) were found by walking the range in the objdump. The static-initialisation thunks
are compiler glue (`scope.yml`); everything else is ported.

## The register file

| Address | Register | Notes |
|---|---|---|
| `0x006a3e00` | H | dword; the distance of the projection plane |
| `0x006a3e04..0x006a3e0e` | SX0 SY0 SX1 SY1 SX2 SY2 | six shorts; RTPS fills SXY1, RTPT SXY0, SXY2 and SXY1 in that order |
| `0x006a3e10..0x006a3e20` | RT | nine shorts, row major (rows at `+0`, `+6`, `+0xc`) |
| `0x006a3e34` | IR0 | short; the factor of GPF / GPL |
| `0x006a3e48..0x006a3e50` | BK | three dwords |
| `0x006a3e58`, `0x006a3e60`, `0x006a3e68` | V0, V1, V2 | three shorts each |
| `0x006a3e70..0x006a3e7a` | RGB FIFO | `svtorgb` writes the byte triple into all three slots (`+0`, `+4`, `+8`) |
| `0x006a3e80..0x006a3e90` | LLM | the light matrix |
| `0x006a3ea0..0x006a3ea8` | TR | three dwords |
| `0x006a3eb0..0x006a3eb4` | IR1 IR2 IR3 | shorts |
| `0x006a3eb8..0x006a3ec0` | MAC1 MAC2 MAC3 | dwords |
| `0x006a3ec8..0x006a3ed8` | LCM | the colour matrix |
| `0x006a3ee8`, `0x006a3eec` | OFX, OFY | dwords |
| `0x006a3ef8` | OTZ | short |
| `0x006a3efa..0x006a3f00` | SZ0..SZ3 | four shorts (RTPT leaves its vertices in SZ3, SZ2, SZ1) |
| `0x006a3f04` | LZCR | |
| `0x006a3f0c`, `0x006a3f10` | MAC0 | `nclip` writes both; `avsz4` the second |

## The operations

Every matrix-times-vector operation is one shape: `MAC[r] = (sum over c of M[r][c] * v[c]) [>> 12] [+ T[r]]`,
all in wrapping 32-bit arithmetic (`imul`, `add`, `sar`), then `IR = clamp(MAC)`. The matrix is RT, LLM or
LCM; the vector V0, V1, V2 or IR; `T` is TR, BK or nothing; the `_sf0` forms skip the shift; the `_lm` forms
use the unsigned clamp. The clamps are `0x004e2070` (`[-32768, 32767]`, the bounds come from
`pow(2, 15)` and `pow(2, 15) - 1` through `_ftol`) and `0x004e2130` (`[0, 32767]`).

| Function | Operation |
|---|---|
| `0x004e2070`, `0x004e2130` | IR = clamp(MAC), signed / unsigned |
| `0x004e21e0` `mygte_svtorgb` | red / green / blue = (IR >> 4 as a short) clamped to a byte, written to all three FIFO slots |
| `0x004e22b0` `gte_ldlzc` | LZCR = number of leading bits equal to the sign bit; **loops for ever on 0** (the port leaves LZCR cleared) |
| `0x004e22f0` `gte_avsz4` | MAC0 = trunc(trunc(SZ0..SZ3 sum / 4) / 4); OTZ = MAC0 as an unsigned short, limited to 65535 (no retail caller) |
| `0x004e23a0 / 23f0 / 2450` | GPF12 (MAC = IR * IR0 >> 12), GPL0 (MAC += IR * IR0), GPL12 (MAC += IR * IR0 >> 12); then the signed clamp and `svtorgb` |
| `0x004e24b0..0x004e2e30` | the LCM and LLM forms (`gte_lcv0` .. `gte_llv2bk`, see the names table) |
| `0x004e2ef0` `gte_mvmva` | dispatcher: (arg0, arg2) = (0, 0) -> RT x V0 unshifted, (0, 3) -> RT x IR unshifted, (1, any) -> RT x V0 shifted; anything else returns |
| `0x004e2f20` `gte_nclip` | MAC0 = x0 y2 - x0 y1 - y2 x1 - x2 y0 + y1 x2 + x1 y0 (wrapping), stored at both MAC0 words |
| `0x004e2f80`, `0x004e2ff0` | the outer product of the RT diagonal (`0x3e10`, `0x3e18`, `0x3e20`) with IR; the second shifts the three results by 12 |
| `0x004e3ce0` `gte_sqr0` | MAC = IR * IR, unsigned clamp |
| `0x004e3330` `gte_rtps` | RT x V0 + TR, IR = clamp, SZ0 = MAC3 limited to `[0, 65535]`; when SZ is not zero SX = (IR1 * ((H << 12) / SZ) >> 12) + OFX and SY likewise, limited to `[-1024, 1023]` as shorts, into SXY1 (the other five screen shorts cleared) |
| `0x004e34e0` `gte_rtpt` | three of those for V0, V1, V2 into (SXY0, SZ3), (SXY2, SZ2), (SXY1, SZ1) with the lower limit **-1023**; every SZ and every screen short is cleared first, a vertex whose depth is zero keeps its screen pair at zero |

The two projection ops differ in one constant (`0xfffffc00` against `0xfffffc01`); both are transcribed, not
unified. The divide never overflows (the dividend is at most 2^31 in magnitude and the divisor at least 1).

## The helpers on top

* `0x004e7c60`, `0x004e7de0`, `0x004e7f60` (`Math_RotateMatrixX/Y/Z`): copy the nine shorts of the matrix into RT,
  then for each column set IR to the rotated unit vector (X: `(0x1000,0,0) (0,c,s) (0,-s,c)`; Y:
  `(c,0,-s) (0,0x1000,0) (s,0,c)`; Z: `(c,s,0) (-s,c,0) (0,0,0x1000)`), call `gte_rtir` and store IR into that
  column of the matrix. `c` and `s` are `_ftol(cos * 4096)` and `_ftol(sin * 4096)` of the angle (4096 per turn,
  not masked; `angle * 2^-12 * 2 pi` with each product rounded to the precision control). They leave RT, IR
  and MAC behind, which the replay compares.
* `0x004e80e0` / `0x004e81a0` build the identity and apply Y, X, Z; `0x004e8140` applies Z, Y, X.
  `0x004e81a0` is a second compiled copy of `0x004e80e0`.
* `0x004e8200`: a 3x3 Q12 multiply into a local that is then copied as **32 bytes** to the output: the 14 bytes
  after the 18-byte matrix are stack leftovers. The replay does not compare them (`unspecified`).
  `0x004e8280` (no retail caller) builds the Rz * Rx * Ry product with it.
* `0x004e83e0` integer square root through `fsqrt` + `_ftol` (0 for 0 or less); `0x004e8480` normalises a
  three-dword vector to length 4096 (`ftol(x * 4096 / isqrt(ftol(x^2 + y^2 + z^2)))`, every operation rounded
  to the precision control, the sum in the order z, y, x) and **returns the truncated squared length**;
  `0x004e8410` transposes, `0x004e8460` stores a vector at `+0x14` of a matrix record.
* `0x004e8560` `atan2` and `0x004e8580` (`atan(value / 4096)`) times `651.8986` (4096 / 2 pi), truncated.
* `0x004e85a0` `Fixed_MatrixConvertQ12`: RT = the matrix, then each output is `ftol(((a + b) + c) * 2^-12)` with
  the retail summation order (row 1 is `m5 z + m3 x + m4 y`, row 2 `m6 x + m7 y + m8 z`, row 0
  `m0 x + m2 z + m1 y`) and a rounding after every product and sum.
* `0x004f3980`, `0x004f39b0`: `ftol(cos / sin(angle & 0xfff) * 4096)`.
* `0x004f5f50` (sign), `0x004f5f90` (dot) and `0x004f5fc0` (`a * b / 4096`) are x87 sequences with a rounding
  after the product and the sum, then `* 2^-12` and `_ftol` (the earlier integer ports are exact only while
  the products fit the mantissa).

## The x87 precision control

The retail binary sets and clears the precision control around the render window; a forced call from the
physics-frame breakpoint runs at the process default of **53 bits**. `MathKit` therefore takes a `Precision`
(24 or 53, default 53) and rounds every `fmul` / `fadd` / `fdiv` / `fsqrt` result with it; `fsin`, `fcos` and
`fpatan` are the real instructions (inline assembly on x86; `std::sin` etc. elsewhere), always at full
precision. Both settings are captured: the groups `mathk` (53 bits) and `mathr` (the generator sets
`$fctrl = 0x7f` first; fixtures `math-calls-p24-*.txt`), and the replay chooses the setting from the fixture
name. Callers in the render window must `set_precision(k24)`.

## The PSX library stubs and the small utilities

* The 41 + 6 + 26 + 1 + 1 stubs of `0x004e86f0..0x004e8cc0` are `return arg0`, `return arg1`, `return 0`,
  `return ax = 0` and a bare `ret`; they are named `PsxStub_<shape>_<address>` because the libgpu / libetc names
  cannot be read from the PC build. The ones with a body: `0x004e87f0` (a draw-area primitive: command word
  `0xe3000000`, the rectangle at `+8` with width and height of at least 1), `0x004e88a0` and `0x004e88f0`
  (`LoadClut`: a call to the zero-return `LoadImage` stub, then `(x >> 4 & 0x3f) | y << 6`), `0x004e8940`
  (clear four dwords, byte `+7` = `0xe0`), `0x004e8cd0` / `0x004e8cf0` / `0x004e8d10` (memory card state
  at `0x006a74bc` / `0x006a74c0`).
* `0x004f5ff0` `Timer_UpdateFrameClock`: unless the multimedia timer is active, `delta = (clock() - last) * 60 / 1000`
  (32-bit wrapping product, signed division); a positive delta advances `0x0056e31c`, and `0x0056e320` too
  while neither pause flag (`0x00561c04`, `0x0056a8e0`) is set; `last` (`0x029d836c`) always becomes `clock()`.
* `0x004f6080` cuts a path at its first `.` and appends an extension; `0x004f60d0` does that to a copy of the name with `.PRK`
  and tries `fopen(name, "r")` (the stack buffer is 64 bytes: the retail function overruns it on longer names).

## Layouts (11c, `re/types/math.yml`)

The offsets below are the ones the replayed functions read and write; every one is covered by the forced-call fixtures (the replay compares each written byte).

| Type | Size | Where the layout comes from |
|---|---|---|
| `RuntimeGteRegisterFile` | 0x120 at `0x006a3e00` | the register table above; `kWindowBytes` (0x120) is the window the probe captures. Only registers some function names are fields (H, SXY, RT, IR0, BK, V0..V2, RGB FIFO, LLM, TR, IR, MAC, LCM, OFX, OFY, OTZ, SZ, LZCR, MAC0 twice). |
| `RuntimeQ12Matrix` | 18 | nine shorts, row major: RT / LLM / LCM and the matrix argument of `0x004e7c60..0x004e8200`, `0x004e8410`, `0x004e85a0`; element `3 * row + column` (`0x004e8200` reads `left + 6 * i + {0, 2, 4}` and `right + {0, 6, 12} + 2 * j`). |
| `RuntimeMatrixTransform` | 0x20 | `0x004e8460` stores a vector at `+0x14 / +0x18 / +0x1c`; `0x004e8200` copies **32 bytes** of its local to the output, the 18-byte matrix and 14 stack leftovers. |
| `RuntimeShortVector3` | 6 | V0..V2 (8 bytes apart in the register file) and IR1..IR3; the angle triple of `0x004e80e0` / `0x004e8140` / `0x004e81a0` / `0x004e8280` (X at +0, Y at +2, Z at +4: Y is applied first in `0x004e80e0`). |
| `RuntimeIntVector3` | 12 | MAC1..MAC3, TR, BK; the vectors of `0x004e8460`, `0x004e8480`, `0x004e85a0`, `0x004f5f90` (dwords at +0, +4, +8). |
| `RuntimePsxRect` | 8 | `0x004e87f0` reads four 16-bit words at +0, +2, +4, +6 (x, y, width, height). |
| `RuntimePsxDrawAreaPrimitive` | 0x10 | `0x004e87f0` writes the dword at +4 (`0xe3000000`) and the rectangle at +8..+0xf; `0x004e8940` clears the dwords at +0, +4, +8, +0xc and stores `0xe0` at +7, so the second dword ends as `0xe0000000`. |

`RetailTimerState` (`timing.yml`) types the timer record of `0x004dace0`. The single globals (card flag and state, the frame clocks, the pause flags, the two accumulators) belong to no recovered
layout and are read and written with `guest::read_scalar` / `guest::write_scalar` at their addresses.

## Validation

Retail forced calls (`re/gdb/probes/math_specs.py`, groups `gte`, `mathk`, `mathr`, `psxs`; fixtures
`re/evidence/fixtures/math-calls-*.txt`, the first 30 cases of each capture per function): the probe pokes an
edge-biased register file, matrices in a scratch slab and a few globals, calls the retail function in the
running game (the clamps, `gte_rtir`, `_ftol`, `pow` and the rotation builders are the real retail functions
nested inside), and records the return value and every written byte; the native replay
(`src/runtime/math_retail_calls_test.cpp`) starts from the same bytes and compares both. The services of the
three utilities (`D3DTIMER_IsActive`, `clock`, `fopen`, `fclose`) are stubbed on their call sites.

Results of the full captures (seed 7): **27,340 forced-call cases, 0 mismatches** - the GTE group 11,700 (300 per function
over 39), the math helpers 6,000 at 53 bits and 6,000 at 24 bits (300 per function over 20), the utilities 3,000
(300 each over 10) and the 80 stubs 640 (8 each). gcov of the three sources over all fixtures: `math_gte.cpp` 190 / 191
lines, `math_fixed.cpp` 146 / 146, `math_psx.cpp` 70 / 70; the one line no case reaches is the early return of
`gte_ldlzc(0)`, which the retail function never leaves. The committed fixtures keep the first 30 cases of each capture
per function.

Findings the replay forced:

* The x87 precision control matters: with 24-bit rounding emulated, 25 of the first 80 forced calls disagreed
  with retail, because the forced call runs at 53 bits (the same code at 53 bits matched all 80). The 24-bit
  window is validated too: gdb cannot write `$fctrl` under winedbg, so those cases call the retail function through a
  trampoline in the slab (`fldcw`, `call`, `fldcw`) and match with `Precision::k24`.
* `Fixed_MulQ12` / `Fixed_DotProductQ12` are float computations, not exact integer ones: products above 2^53 (or
  2^24 in the render window) lose low bits before the shift, which the earlier integer ports (`fixed_math.cpp`, kept
  for the callers they were validated against) do not model.
* `Math_RotationMatrixFromAngles` (0x004e80e0), `orig_ratan2`, `Fixed_CrossSignQ12` and `Fixed_MulQ12` were native
  before; they are replayed here too and keep their earlier slice mappings.
* The seams: natives written earlier still report these functions as `engine_<address>` services (their fixtures
  record them so); the host binds a service to the `MathKit` method (`src/runtime/math_kit.hpp`), and a caller in the
  render window sets `set_precision(x87::Precision::k24)`. Nothing in the committed fixtures changes.

