# The replay recorder of video.cpp: the bit stream, the event queues, the highlights and the session

Status: confirmed by retail forced calls (19,151 cases of the replay recorder layer, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004cb2b0`/`0x004cb2c0`, `0x004cb320`..`0x004cb4c0`, `0x004cb580`, `0x004cb5c0`, `0x004cb6e0`, `0x004cb810`, `0x004cba30`, `0x004cbc40`, `0x004cbc90`, `0x004cbca0`, `0x004cbce0`, `0x004cbf30`..`0x004cc020`,
`0x004ccc40`, `0x004ccc70`, `0x004cd5a0`, `0x004cd690`, `0x004cd710`, `0x004cdc20`, `0x004cddc0`, `0x004cde70`..`0x004cdef0` (slices `replay-bit-stream`, `replay-event-queues`, `replay-highlights`, `replay-session`)
Native: `src/runtime/video_kit.hpp`, `video_stream.cpp`, `video_highlights.cpp`, `video_session.cpp`; replay `src/runtime/video_retail_calls_test.cpp`; probes `re/gdb/probes/video_specs.py` (group `vbt`),
`video_specs_frame.py` (group `vrs`), `video_specs_more.py` (group `vcs`); fixtures `re/evidence/fixtures/video-calls-vbt*.txt`, `video-calls-vrs*.txt`, `video-calls-vcs*.txt`

## What is here

- **The stream.** A recording is a bit-packed stream per skater (two state blocks, `0x0056e5d8` for player one and `0x0056e450` for the second, 0x188 bytes each): `+0x00` the mode (0 idle, 1 recording, 2 playing),
  `+0x04` the 0x80000-byte buffer, `+0x08` where the header goes (the buffer + 0x200 while recording), `+0x0c` where the frames start (the buffer + 0x4800), `+0x14` the cursor, `+0x18` the word being filled or
  consumed, `+0x1c` the bits of it still free (writing) or unread (reading), `+0x20` the frames recorded, `+0x24` the frames still to play, `+0x50` the highlight being played and `+0x54` five highlight records of
  8 bytes. Bits go into the word from its low end; a word that fills up is stored through the cursor, which steps by 4. `Replay_WriteBits` takes 0..32 bits (a field wider than the room left spills into the next
  word, the spill shifted arithmetically as the retail code has it); `Replay_ReadBits` takes 1..32 and `Replay_ReadSignedBits` extends the sign from the top bit of the field; `Replay_ReadFlag` is one bit.
  `Replay_BitsNeeded` is the width of the smallest signed field that holds a value (2 bits for -2..1, one more for every doubling); for 0x40000000 and more the retail loop never ends, so the port refuses
  and the probe never asks. Stopping a stream leaves a poisoned cursor (`0xdeadbeef`), no mode and no bits; rewinding puts the cursor back on the boundary with every frame still to play.
- **The event queues.** The sound commands the frame being recorded carries are a ring of 30 records of {kind, sound} (`0x0056e340`, read and write indexes at `0x0056e338` and `0x0056e438`): push answers the
  quotient of the step (1 on a wrap), pop likewise; starts (kind 1) and stops (kind 2) are queued only while player one's run is being recorded and the recording is not muted (`0x00561c04`). The last splat of
  each skater (`0x0056e43c`, `0x0056e440`: a body part under 4) and the last trick object of each player (`0x0056e430`, `0x0056e434`: an index under 0x200; player 1 or 2) are -1 when none. `Replay_ClearBodyState`
  (behind the jump thunk `0x004cb2b0`) zeroes eighteen dwords of the playback states.
- **The highlights.** While a run is recorded the five best-scoring stretches are kept in the first state block: `Replay_RecordHighlightStart` notes the frame a score starts to count, `Replay_RecordHighlightEnd`
  closes it with the score handed in, or, when that is 0, twice the height gained; a negative score marks a bail; the new record goes into the first slot whose score it beats. `Replay_PrepareHighlights`
  clamps the ranges to the length of the replay, picks them in order of start (a record with no score is empty), merges neighbours that touch or overlap and packs the list for the header.
  `Replay_AdvanceHighlight` (playback) skips the stretch before the highlight now playing by running the game for the frames in between.
- **The session.** The header (`Replay_WriteHeader`): the frame count, the five ranges, how many skaters, which skater and costume each is, the level and game mode, the money mask and a 0x80-byte block of
  custom-skater data per skater. `Replay_AllocateStreamBuffers` takes a 0x80000-byte buffer for each skater (the second only with two players); `Replay_ConfigureStreamState` starts a recording (mode 1: the buffer
  is cleared and the header written) or a playback (any other mode) over the frames `first`..`last` (0x7fffffff: all); `Replay_StartPlayback` reads the loaded file's header (level, game mode, highlights, frame
  count) and answers 0, or 1 when the file did not load (the end cause becomes 9); `Replay_RegisterAnimations` registers every animation of the trick scripts once for each skater of the replay;
  `Video_Playing` is the playing test; the car positions (`0x0056e69c` eight records of three dwords, `0x0056e71c` eight records of three shorts) are stored and loaded by slot, a slot over 7 logging a line.

## Method

As in the earlier phases: forced calls in the running game with stubbed services, a scratch slab for the two stream buffers and the fake skaters, the recorded return value, every written byte and the ordered
service calls compared with the native replay. The state blocks and the globals are mutable windows poked with random legal words; the streams are random words (any bit pattern is a legal stream).

## Lessons

(1) Shifts on x86 take the count modulo 32, so a 32-bit field written into a word with 32 bits free uses a shift of 0: the port masks every count. (2) The spill of `Replay_WriteBits` is shifted arithmetically (`sar`),
which only matters for a value with bit 31 set. (3) `Replay_RegisterAnimations` is slow under the debugger (a walk of the whole trick table for each skater): its capture is 47 cases. (4) The probe's printf
stub reports only the format of a log line, so the car-position log lines compare only their format address.
