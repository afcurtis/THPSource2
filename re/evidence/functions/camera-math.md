# The camera's quaternion math (camera.h)

Status: confirmed by retail forced calls (23,939 cases of the camera layer, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004103b0`, `0x004a9650`, `0x004a9750`, `0x004a9820`, `0x004a9870`, `0x004a98c0`, `0x004a9910`, `0x004a9a00`, `0x004a9bf0` (slice `camera-quaternion-math`)
Native: `src/runtime/camera_kit.hpp`, `camera_math.cpp`; replay `src/runtime/camera_retail_calls_test.cpp`; probe `re/gdb/probes/cam_specs.py` (group `cmq`); fixtures
`re/evidence/fixtures/camera-calls-cmq*.txt`

## What is here

A quaternion is five dwords {vtable `0x005184d8`, x, y, z, w} in Q12; a matrix is nine shorts (Q12), row major, followed by six more shorts the conversion clears.

- `Camera_ComposeTransformQ12` (`0x004a9650`): the Hamilton product a * b, each component rounded down (an arithmetic shift) from Q24 (the sums are 32-bit and wrap before the shift); the result carries the class
  vtable and the function answers the output pointer.
- `Camera_QuaternionFromRotationVector` (`0x004a9750`): the axis scaled by the angle in 12-bit turns; the angle is the truncated square root of the sum of squares, the axis the Q12 unit vector (`Math_NormalizeQ12`),
  x, y, z are `sin(angle / 2)` along it, w is `cos(angle / 2)`; the zero vector is the identity. `Camera_QuaternionAboutX/Y/Z` (`0x004a9820..0x004a98c0`) are a turn about one axis: the short argument is halved
  (arithmetic) for the half angle.
- `Camera_TransformToMatrixQ12` (`0x004a9910`): the rotation matrix of a unit quaternion (Q12 entries from the `2ab >> 12` style products, truncated to shorts) with the six shorts after it cleared.
  `Camera_MatrixToTransformQ12` (`0x004a9a00`): a positive trace gives `w = sqrt(trace + 1) / 2` and the vector from the antisymmetric part; otherwise the largest diagonal entry picks the axis (the axis table
  `0x0053757c` {1, 2, 0} gives the next axis in the cycle).
- `Camera_SlerpTransformQ12` (`0x004a9bf0`): the spherical interpolation of two quaternions (each passed whole on the stack) at `t / 4096`. Both are scaled to unit length first; the shorter way round is taken
  (the first one negated when the dot product is negative); nearly equal ones are blended linearly, the rest by the ratio of two sines of `(1 - t) * angle` and `t * angle`; the result is scaled to unit length again.
- `Camera_AngleDelta12` (`0x004103b0`): `(to & 0xfff) - (from & 0xfff)` folded into -0x800..0x800 by the short's sign.

## Method

Forced calls with quaternions in a slab: unit-length ones mostly, but also the zero quaternion, ones of a few units and random dwords; the sine and cosine of the interpolation are the x87 sequences of
`Math_SinQ12` / `Math_CosQ12` (53-bit precision, as the forced calls run). The scalar square root is the 4.15a native one.

## Lessons

(1) The Q24 products wrap at 32 bits before the shift: the port multiplies as unsigned. (2) A division by zero in the matrix conversion faults in retail; the port refuses with an exception and the probe never
asks. (3) The near-opposite branch of the interpolation (`dot + 0x1000 < 0x81`) is unreachable: the shorter way round is taken first, which leaves a dot product of at least 0, while the test needs one below -0xf7f. The port keeps the branch (it is in the disassembly) and gcov reports its ten lines as the only ones of `camera_math.cpp` never executed.
