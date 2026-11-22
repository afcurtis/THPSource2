# The core utilities (utils.cpp and the 3-vector operators)

Status: confirmed by retail forced calls (11,700 cases, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004c9300..0x004cad70` (35 functions; `Math_Vector3SquaredLength` at `0x004ca940` is not listed by Ghidra and was found in the gap)
Native: `src/runtime/core_kit.hpp`, `src/runtime/core_utils.cpp`; replay `src/runtime/core_retail_calls_test.cpp`; probe `re/gdb/probes/core_specs.py` (group `utl`);
fixtures `re/evidence/fixtures/core-calls-utl*.txt`

## What is here

`utils.cpp` is the game's grab bag of math and list helpers. Every function works on retail memory at the addresses its callers hand it; the Q12 trigonometry, the arctangent and the GTE
are the 4.14a `MathKit` (the forced calls run them for real), the integer square root `0x004f53b0` (an x87 `fsqrt` another milestone owns) and the collision queries are services.

- **Vectors.** `Utils_RotateY` / `Utils_RotateYQ12` turn a vector about y (the first shifts the inputs by 3 and the sum by 9, the second shifts the inputs by 12); the second half reads
  the input again after the first store, so an output that is also the input sees the new x. `Utils_CalcUnit` / `Math_Vector3Normalize` divide by the length (whole-unit components through
  `gte_sqr0`, then the integer root); **`Math_Vector3Normalize` divides z by the length twice** (it divides the already divided y again: a retail quirk the replay pins).
  `Math_Vector3DeadZone` zeroes components within +-0x800. The `...Out` operators write a result vector (add, subtract, scalar multiply, divide, shifts, negate) and answer the
  output pointer; `Math_Vector3sMask12` masks three shorts to 12 bits and answers 0xfff.
- **Distances.** `Utils_Dist` / `Utils_XZDist` square the whole-unit offsets and call the integer root. `Utils_VectorCosineQ12` (the cosine at the middle point of three) uses the octagonal
  distance `Utils_CrapDist` and **unsigned** divisions; 10000 when a distance is above 0x9c40.
- **Angles.** `Camera_BuildLookAngles` answers pitch (`angles[0]`) and yaw (`angles[1]`) of the line between two points through `Math_AtanSlopeQ12` (the dx / dz and dy / horizontal-length
  divisions: dx / dz signed, dy / length unsigned), the horizontal length is its result. `Utils_TurnTowards` takes two 3-short records **by value** (four stack words), wraps the difference
  into [-0x800, 0x800] and scales it. `Camera_AcosRatioToAngle12` is `ftol(acos(v / 4096) * 4096 / 2 pi)` with the C runtime's acos (`fpatan` of `sqrt((1 + x)(1 - x))` over x, exact at +-1) and the constant
  `0x3fc45f306dc9c883` (as the executable stores it: the `L` literal for 1 / 2 pi is a different number and rounds one unit wrong near integers).
- **Collision queries.** `Utils_LineOfSight` and `Utils_GetGroundHeight` build the 0xa4-byte query record in a stack local (start at +0, end at +0xc), call the two collision services with a
  pointer to it and read the answer from the record: the body hit at **+0x68**, and for the ground height the surface slope (a short at **+0x7a**; flat enough means `<= -0xc00`) and the hit
  height (**+0x70**). The offsets are not where the decompiler puts them (its record is the ghidra struct, the code reads through `esp` after pops): the two reads after the final `pop`s sit
  at +0x7a and +0x70. The probe's stub for the zone query writes the three fields into the caller's stack record through the new `pokes` offset of `fcall.py`.
- **Object lists.** `Utils_KillObjectsInBox` kills (virtual slot 2) or deletes (slot 0 with argument 1) every object of a list inside a box, after the object's own skip test (a service);
  `RuntimeObject_ApplyBoundsToLists` runs it over four lists; `RuntimeObject_UpdateVisibilityBounds` sets or clears the visibility bits (0x41, 0x01 or 0x40 depending on the owner) of five lists
  (the environment objects only when their region is loaded; the fifth list is the type-2 objects, whose byte at +0x4a is the result) for objects inside or outside a box.
- **Sound.** `Utils_CalculateSpatialAttenuation`: full (0x0fff0fff) inside `near`, nothing beyond `far`, else a linear fade; the bearing against the listener's rotation (copied into the GTE, `gte_rtir`,
  `orig_ratan2`) splits it into left and right levels, bit 31 set when the source is behind the listener.
- **Small ones.** `Utils_Pulse` (a triangle over a period), `Utils_LimitRange`, `Utils_InitialRand` (the three words of the generator), `Utils_ClearExplosionStack`,
  `CaseInsensitiveBoundedNameEqual`, `CPlatform__AI` (the angle-window test of a platform object), `Pause` (empty).

## Method

Forced calls on generated state in the running game (`P410_GROUPS=utl`): vectors, strings, objects and lists in a scratch slab, the GTE register file and a few globals poked per case with
edge-biased random words. Stubs: `0x004f53b0` (the truncated root of its argument), `0x004805f0` (the object skip test, scripted per object), `0x004624d0` / `0x00466090` (the collision
queries), and two fake virtual methods. Fixtures: the capture `utlA` (9,600 cases) and `utlB` (900, the three functions whose generator was fixed: lists that overlapped their own vtable, and a
divisor of -1 with INT_MIN).

## Lessons

(1) A record built in a stack local and read after the callee's `pop`s is addressed from `esp` at that point: recount the pushes and pops before trusting a decompiled field offset.
(2) A compiler constant is the double in the executable, not the decimal text of it: `0.1591549430918953L` differs from `0x3fc45f306dc9c883` in the last bits.
(3) A list generator must lay its objects out from the slab size, not from a guessed count (four lists of three objects overflowed the space before the fake vtable).
