# The camera object of camera.cpp: the quaternion class, the rigs, the setters and the shake

Status: confirmed by retail forced calls (23,939 cases of the camera layer, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0040b650`, `0x0040bbe0`, `0x0040bc00`, `0x0040bce0`..`0x0040bd30`, `0x0040bd40`, `0x0040be40`, `0x0040be60`, `0x0040c070`..`0x0040c170`, `0x0040e000`..`0x0040e060`, `0x0040f0d0`..`0x0040f1a0`, `0x0040f1f0`,
`0x004113e0`, `0x004117a0`..`0x004117f0`, `0x00411880`, `0x004118a0`, `0x004119e0`, `0x00411c50`..`0x00411cd0`, `0x00411ec0`..`0x00411f00` (slice `camera-object-basics`)
Native: `src/runtime/camera_kit.hpp`, `camera_basic.cpp`; replay `src/runtime/camera_retail_calls_test.cpp`; probe `re/gdb/probes/cam_specs_small.py` (group `cms`); fixtures
`re/evidence/fixtures/camera-calls-cms*.txt`

## What is here

- **The quaternion class** (`0x0040e000..0x0040e060`): five dwords {vtable `0x005184d8`, x, y, z, w} in Q12. The constructor stores the four components and the vtable; the destructor leaves the base
  class's vtable (`0x005184b4`); the scalar deleting destructor frees the block through the class delete when bit 0 of the flag is set; the copy constructor goes through the base vtable too before it
  takes the derived one.
- **The rigs** (`CameraRig_*`, 0x94 bytes, vtable `0x005184dc`; a camera holds four, at `+0xcc`, `+0x180`, `+0x234`, `+0x2e8`): the constructor makes the three quaternions the identity (`+0x1c`, `+0x40`,
  `+0x74`; their w at `+0x2c`, `+0x50`, `+0x84` is 0x1000) and zeroes the vectors; the record constructor takes a level record of shorts {x, y, z, w of the orientation, a distance word, three offsets},
  scales the quaternion to unit length (an integer square root of the sum of squares, 1 for a zero length) and the offsets to Q12; `CameraRig_CopyMatrix` copies the 0x20-byte matrix at `+0x94`,
  `GetPosition` the vector at `+0x88`, `GetTarget` the one at `+0x68`.
- **The camera points** (the level's camera records): the destructor puts the base vtable back in two places; `CameraPoints_Clear` zeroes the count at `0x0055fae4` and
  `CameraPoints_ClearAndResetTuning` also restores the three defaults at `0x00524a40..0x00524a48` (0x122, 0x96, -0x6c).
- **The constructor** (`SkaterCamera_Construct`, `0x0040b650`, a thiscall with the skater and the player number as a short on the stack, 1,409 bytes): the base object's constructor (a service), the four rigs,
  then a run of stores that clears the camera: the target at `+0x3a4` and `+0x3dc`, nine quaternions (`+0x444`, `+0x458`, `+0x46c`, `+0x480`, `+0x494`, `+0x51c`, `+0x580`, `+0x5a0` and `+0x64c`) set to
  the identity, a list node at `+0x510` (vtable `0x005184d4`, its pointer to itself at `+0x518`), the two overrides set to all ones, the window 0x200 x 0x310 (and 0x568), the mode 1, the vtable `0x005184b8`. The player number (`+0xc8`) is then read from the pad the skater listens to
  (`+0x2ccc` of the skater: the first pad `0x0056aff8` gives 0, the second `0x0056b164` gives 1, any other pad is logged); the look-at point (`+0x3c0`) and the look target (`+0x3f0`) start at the skater's
  position; three words of the tuning (`0x00524a40..0x00524a50`) are loaded from shorts at `0x00524a70..0x00524a74`; the viewport is committed (`Camera_CommitViewportEffects`), the camera is added to the
  camera list (`0x0055fa38`, a service) and the count (`0x0055fa34`) goes up. The function asserts that the skater is not null.
- **The camera** (`CCamera`, vtable `0x005184b8` at destruction): the destructor takes the camera off the camera list (`0x0055fa38`, count `0x0055fa34`), puts the base vtable in the ten quaternions it holds
  and the four rigs, and calls the base destructor; the scalar deleting destructor frees it through `Object_Free`. The setters store the two overrides (`+0x4d4`, `+0x4d8`) and the window width and height
  (`+0x4dc`, `+0x4de`); `PushOverrides` / `PopOverrides` (and the `B` pair) save and restore them in `+0x4e4..+0x4f0`; `Camera_ApplyShake` copies the three constants of the shake type (0, 1 or 2) into the
  camera (`+0x4f2..+0x500`) and flags the objects of both object lists for the shake (bit 1 of the word at `+0x68`); `Camera_SetBlendTarget` makes the blend value (`+0x40c`) go to a target over a number
  of frames (the step is `(target - value) / frames`, or the value jumps when the length is 0); `CCamera__InCompetitionIntro` tells whether the mode (`+0x504`) is 0x17; `ClearFlag419` clears a byte.
- The empty virtual hooks (`Camera_Hook_F0D0..F190`, `CameraRig_Hook_7A0..7E0`, `CameraRig_Hook_CC0/CD0`, `Camera_ShakeHook`) are one-instruction returns of 0 or nothing; they are replayed so
  the vtables of the classes can be taken over.

## Method

As in the earlier phases: forced calls on a fake camera (0x6a0 bytes) in a scratch slab with the object lists as fake chains; `Object_Free`, the list removal and the base destructor are services; the
quaternion records are filled with random dwords. The first capture of this group ran out of time before its last functions, so the rest were captured again (`cmsB`; the two shake functions again in `cmsC`).

## Lessons

(1) A destructor that is replayed must leave the vtables it leaves in retail: the quaternion destructor leaves the base vtable, not zero. (2) The record constructor's integer square root is the 4.15a native;
a length of 0 is replaced by 1 before the division. (3) `Camera_SetBlendTarget` divides by the length as a 16-bit value; a length that is a multiple of 0x10000 is the "jump" case.
