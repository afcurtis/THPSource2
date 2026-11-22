# The camera rigs and the camera modes of camera.cpp

Status: confirmed by retail forced calls (23,939 cases of the camera layer, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0040be70`, `0x0040c1a0`, `0x0040e090`, `0x0040f200`, `0x0040f270`, `0x004103e0`, `0x00410610`, `0x00410bb0`, `0x00410c40`, `0x00410c90`, `0x00410f70`, `0x004113f0`, `0x00411a00`, `0x00411c00`,
`0x00411ce0` (slices `camera-rigs` and `camera-modes`)
Native: `src/runtime/camera_kit.hpp`, `camera_rigs.cpp`, `camera_modes.cpp`, `camera_smooth.cpp`; replay `src/runtime/camera_retail_calls_test.cpp`; probe `re/gdb/probes/cam_specs_modes.py` (group `cmm`); fixtures
`re/evidence/fixtures/camera-calls-cmm*.txt`

## What is here

- **The rigs** (0x94 bytes): a follow flag (`+4`), an owner (`+8`), a position (`+0xc`), a rotation flag (`+0x18`), a quaternion (`+0x1c`, whose Q12 components are `+0x20..+0x2c`), an offset vector (`+0x30`), a
  distance (`+0x3c`) and the outputs: the position (`+0x88`), the rotation quaternion (`+0x74`) and the matrix (`+0x94`). `CameraRig_Update` turns the rig's own quaternion by the owner's matrix when the
  rotation flag is set (else it takes the fixed quaternion), scales it to unit length, builds its matrix, takes the offset (0, 0, -distance) through it and adds the result to the position (the owner's
  when the rig follows it). `CameraRig_BuildMatrix` is the matrix of the output quaternion with the output position as its translation. `CameraRig_Blend` mixes two rigs at `t / 4096`: the position and the offset
  as `a + ((b - a) * t >> 12)`, the orientation by the spherical interpolation from b's quaternion to a's at `(AnimTimeScale * t) >> 8`, the distance (a short) with an unsigned shift as retail has it, and
  the flag and the owner taken from b.
- **The modes.** `Camera_FollowTarget` (`0x00410610`) trails the target: the direction it looks along is the target's travel when that is fast and the target is on the ground (or the camera has not given up
  following), else the old direction; the direction is smoothed and turned into the quaternion the camera steers to. `Camera_Mode2` (`0x004113f0`) is the same without the state machine. Both end in the
  same tail (`steer_to_view`): the direction goes through the GTE with the target's up vector to a view matrix, which becomes the quaternion the camera steers to, turned by the camera's spin about y
  (`0x004a9870`). `Camera_BuildTripodRotation` (`0x004103e0`) takes the target's own basis as the view. `CCamera__ResetOrientation` (`0x0040c1a0`) puts the camera behind its target (a camera with no target
  takes the query owner and is marked as having borrowed it), copies the history vectors and quaternions and commits; `Camera_ResetAndSettle` (`0x00410c40`) does that, then ten updates of a still target.
  `Camera_CommitViewportEffects` (`0x0040be70`) writes the camera's position and orientation to the viewport record of its player (the shaken position for a skater that is not in state 5).
  `Camera_DeathMode` (`0x00410c90`) blends the quaternion towards the one it was given over thirty frames and the position along the line to the point it was given; the divisor 0x82 (130) is shared with the
  point mode. `Camera_PointMode` (`0x00410f70`): from a point of the level (record `+0x54..+0x5c` the end position, `+0x60..+0x64` three turns composed into an orientation) the camera swings to the
  skater over 130 frames; the first frame places it (ten object updates with the skater's velocity set to 20 times its offset) and the counter steps by 6 instead of 1 once the pad byte at `0x0056b028` has been seen set after frame 10.
  `Camera_StartCompetitionIntro` (`0x00410bb0`) sets the competition intro shot up (the competition on first use, and the heat title "level - heat n" built into the global text). `Camera_SmoothAndValidate`
  (`0x0040e090`, one stack argument, the factor of the steering in the plain case) is the end of the camera's frame: without a target the camera looks along the line to its look target and settles there;
  with one it keeps a running average of the target's speed, derives the distance and the shake from it, steers the render quaternion (`+0x444`) towards the one the follow mode chose (`+0x458`) with a factor
  that depends on the situation, runs the effects (`Camera_ApplyEffects`, `0x0040c370`, the 4.3 camera model: a service here) and derives the position and the velocity from the quaternion.
- **The debug.** `Camera_DrawDebugBars` (`0x0040f200`) draws two bars when the debug byte `0x0055fa40` is set; `Camera_HandlePad` (`0x0040f270`) changes the view number (`0x0055fa44`) or steps the camera
  mode (`0x0055fa42`) through two tables (next: 1, 2, 3, 5, -, 6, 8, -, 9, 10, 0; previous: 10, 0, 1, 2, -, 3, 5, -, 6, 8, 9) and turns the chosen rig's quaternion by 10 units about an axis or moves its
  distance by 10 (in the modes 5 and 6).

## Method

A fake camera (0x700 bytes, all captured), a fake vtable whose slot 3 is an unreached retail function (the camera's virtual update, a stub), three rigs and two fake skaters (the camera's target and a rig's owner;
only the fields the code reads are captured) in the slab; the GTE register window, the viewport records, the camera globals (distance, pitch, shake, tuning), the level records and the pad bytes are captured too.
`Camera_ApplyEffects` and `Camera_Update` (4.3 model natives), the collision queries, the object update, the competition screens, the line drawer and `sprintf` are stubs; the quaternion, vector and GTE
math runs as retail code. The ten updates of `Camera_ResetAndSettle` run the same stubs ten times.

## Lessons

(1) The decompile hides which slot of a shared tail a mode uses: `Camera_FollowTarget` and `Camera_Mode2` jump into the same `steer_to_view` with different registers. (2) `Camera_DeathMode` divides by 0x82
where the point mode keeps its counter limit in the same slot. (3) `CCamera__ResetOrientation` copies an uninitialised stack vector into the camera when the camera has no target and no query owner exists:
the bytes are retail leftovers, so the probe always names an owner (the branch is not replayed). (4) `CameraRig_Blend`'s distance short is computed with an unsigned shift of a product whose span is
signed: a negative span does not give a small negative step.
