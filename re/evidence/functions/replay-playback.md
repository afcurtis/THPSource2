# The frames of a replay: writing a frame and playing it back (video.cpp)

Status: confirmed by retail forced calls (19,151 cases of the replay recorder layer, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004cbbb0`, `0x004cc0a0`, `0x004cc0e0`, `0x004cc190`, `0x004ccd70`, `0x004cd750`, `0x004cda90`, `0x004cdab0` (slices `replay-frame-writer` and `replay-frame-player`)
Native: `src/runtime/video_kit.hpp`, `video_frame.cpp`; replay `src/runtime/video_retail_calls_test.cpp`; probes `re/gdb/probes/video_specs_frame.py` (groups `vrw`, `vra`); fixtures
`re/evidence/fixtures/video-calls-vrw*.txt`, `video-calls-vra*.txt`

## What is here

- **A frame** holds, for each skater: the eight body-part records (position >> 12 as three 20-bit fields and three 16-bit angles each, from `+0xc4` and `+0x144` of the state block), the skater's position as the
  second difference of its whole-unit position (a prefix code picks the narrowest of the six widths that holds the three components, then the three fields), the orientation as three angle deltas (the same
  scheme with the six angle widths), then the state (one bit when unchanged, else 5), the animation (1 or 9 bits), the animation frame (3 bits for a step of 0..3 from the last, else 9), the facing flag (bit 1 of the skater's flags), the flip
  (a flag, then a prefix code of the six widths, the phase step), the time step since the last frame (4 bits, at most 15 ticks: a longer gap moves the recorded clock on), a sound command from the ring of
  `replay-recorder.md` (a flag, a start/stop bit and a 9-bit sound) and the frame's events (a flag; a splat: a flag and 2 bits; a trick object: a flag and 9 bits; the splat and trick object are those of the
  skater being written, player one's or the other's). The widths come from the tables at `0x005481ac..0x00548238` (six position widths and the limit
  0x14, six angle widths, six flip widths, seven prefix codes and their lengths).
- **Writing** (`Replay_WriteFrame`): `Replay_WriteFrameChecked` asserts first that the skater is not null and that its stream has a boundary; the writer ends the recording when the buffer is full (the boundary
  plus 0x7b800 is not above the cursor plus 0x218: a log line and the skater's input is reset), then writes every field above, with the asserts "Bits must be 0..32", "BodyPart > 2 bits", "TrickObIndex > 8
  bits" and "wrong skater no" on the way. `Replay_MatrixToAngles` takes an orientation matrix with three of its entries negated (the replay keeps the mirrored handedness) to three angles (a service does the
  conversion; its three shorts are scripted back).
- **Playing** (`Replay_ApplySkaterFrame`): the fields are read back in the order they were written. Mode 1 plays the frame for the screen (the angles become the rotation matrix through
  `rotation_from_angles_again` with the same three entries negated, the transform is rebuilt by services, and the game sleeps until the clock has caught up with the frame's time); mode -1 skips over it
  (`Replay_AdvanceFrames` calls it for the stretch before a highlight, after clearing every object's input flags, then runs the objects' AI; a skipped frame plays no sound). The sound command plays or stops a
  sound; a splat starts the splat effect; a trick object is found in the level's list by its index, marked touched (its item's flags and colour, the player number in the object) and every node its links name is
  activated as a goal (node types 0xc and 0xe) or sent a pulse; when the skater has a previous position the line it moved along is tested against the trigger zones (the skater as query owner) and the
  power-ups; the board is switched off in the throwing-away tricks (animations 0x2c, 0x2e, 0x36, 0x37, 0x88, 0x8a, 0xb1, 0xcb and the late parts of 0x89, 0xcc, 0xce, 0xcf) and on otherwise.
- **One frame of playback** (`Video_Playback`): the camera trigger, then, while frames are left, the frame is applied and the skater is checked: a skater in state 4 with one of twelve board-trick animations
  while the glass count (`0x0055f72c`) is under 250 breaks the glass of a window (setup of the effect, nine shards from the allocator, each thrown by a service); the water line (`+0x3080`) and the
  ground are restored or the position copied; the board is updated, a bounce is sent when the previous position is not zero, and the water handled. At the end of the stream the next highlight begins
  (advance and count), or, outside the three competition statuses (0x14..0x16), the skater's input is reset and the video screen is shown.
- `Video_Paused` is the playing-and-paused test and `Video_HandleStepping` the pause and frame-step buttons of the video screen (the step buttons of either pad, the repeat buttons, the pause flag at `0x0056e698`).

## Method

As in `replay-recorder.md`; the playback group (`vra`) pokes a loaded replay (mode 2, random streams, short highlight ranges so that a skip runs the game only a few times) and fake skaters with the fields
the code reads. The glass break needs a skater left in state 4 with a board-trick animation: an all-zero stream keeps the skater's own state and animation (every changed flag reads 0), so a share of the
`Video_Playback` cases use one (a second capture, `vraB`).

## Lessons

(1) The 16-bit angle sums wrap in the state block before the rotation matrix is built (`& 0xfff` per angle). (2) `Replay_AdvanceHighlight` for the first highlight skips range[0] - 1 frames, for the others the
distance between two records less 2. (3) The glass branch is reachable only through the decoded state and animation, so random streams almost never reach it.
