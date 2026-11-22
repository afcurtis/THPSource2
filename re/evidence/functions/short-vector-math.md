# The short-vector class of vector.cpp

Status: confirmed by retail forced calls (19,151 cases of the replay recorder layer, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004cadc0`, `0x004cae50`, `0x004cae80`, `0x004caeb0`, `0x004caed0`, `0x004cafa0`, `0x004cb0e0`, `0x004cb130`, `0x004cb180`, `0x004cb1d0`, `0x004cb220`, `0x004cb270` (slice `short-vector-math`;
the other methods of the class, `0x004cae10`, `0x004cae30`, `0x004caef0`, `0x004caf40`, `0x004caf70`, `0x004cb040` and `0x004cb090`, were handled before this milestone)
Native: `src/runtime/video_kit.hpp`, `video_shorts.cpp`, `video_stream.cpp` (the two scalar-by-vector helpers); replay `src/runtime/video_retail_calls_test.cpp`; probes `re/gdb/probes/video_specs.py`
(group `vbt`), `video_specs_more.py` (group `vcs`); fixtures `re/evidence/fixtures/video-calls-vbt*.txt`, `video-calls-vcs*.txt`

## What is here

A short vector is three 16-bit components stored as a dword (x, y) and a word (z). The methods that change a vector in place do their arithmetic on 16-bit operands (`imul dx, ax`,
`sar word ptr [eax], cl`) where the count of a shift is taken modulo 32, so a count of 16 or more empties the operand (zero, or the sign fill for a right shift); the `Out` variants build their result in a
local and store it whole.

- `Math_Vector3sLength` (`0x004cadc0`): the GTE squares the three components (IR1..3 in, MAC1..3 out) and the sum goes through the truncated integer square root `0x004f53b0` (ported in 4.15a: this layer asks the core kit for its native version with `use_native_square_root()`, where the 4.14c replays
  still take the root from a service).
- `Math_Vector3sMulScalar` / `DivScalar` (`0x004cae50`, `0x004cae80`): each component times the short, or divided by the signed dword, that a pointer names (read again for each component). `Math_Vector3sShiftRight` /
  `ShiftLeft` (`0x004caeb0`, `0x004caed0`): 16-bit shifts by a count in a register.
- `Math_Vector3sAngleDifferenceOut` (`0x004cafa0`): this less the other, each component folded into -0x800..0x800 (below -0x800 add 0x1000, then above 0x800 subtract 0x1000).
- `Math_Vector3sMulScalarOut` (`0x004cb0e0`) and `Math_ScalarMulVector3sOut` (`0x004cb130`): the vector times the short a pointer names, every product truncated to a short; the second takes its arguments
  in the other order. `Math_Vector3sDivScalarOut` (`0x004cb180`): the vector divided by the signed dword a pointer names. `Math_Vector3sShiftRightOut` / `ShiftLeftOut` (`0x004cb1d0`, `0x004cb220`): the vector shifted
  by the count the low byte of a dword names (16-bit shifts). `Math_Vector3sNegateOut` (`0x004cb270`): the negated vector.

## Method

Forced calls with the vectors in a slab (random shorts, including the extremes), the scalars random shorts and dwords; a division by zero or `INT_MIN / -1` faults in retail and ends the case in an error, which the
fixture maker skips (the port refuses with an exception). The GTE register window is poked with random words before each call.

## Lessons

(1) A 16-bit `sar`/`shl` by a count of 16..31 gives 0 or the sign fill, not the C++ undefined behaviour: the port shifts a 32-bit value and truncates. (2) `imul dx, ax` keeps only the low 16 bits, so the
`Out` products are truncated shorts, not rounded ones. (3) The scalar is read again for every component (the compiled loops reload it), which the port repeats.
