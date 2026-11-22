# The game's audio and movie layer: the crowd, the redbook sequencer, the sound effect front, the VAB banks, the game FMVs and the movie interface (Phase 4.13b1)

Source: `crowd.cpp` (`0x0042f2e0..0x0042f880`), `redbook.cpp` (`0x004ab9c0..0x004ac020`), `sfx.cpp` (`0x004acff0..0x004adc80`), `pc\soundlib.cpp`'s `Audio_StopAll` (`0x004f38d0`), `gamefmv.cpp`
(`0x00454d30..0x00454e70`) and the movie interface of `pc\pcmovie.cpp` (`0x004e5ec0`, `0x004e6e70..0x004e73f0`). Native: `src/runtime/audio_game.hpp` (`AudioGame`, a `PcKit`) with
`audio_crowd.cpp`, `audio_redbook.cpp`, `audio_sfx.cpp`, `audio_movie.cpp`. **63 functions** in five slices (`crowd-audio` 10, `redbook-sequencer` 12, `sfx-front` 20, `vab-banks` 5,
`game-fmv-and-movie-interface` 16). 4.13b's netgame range (about 75 functions) is the next session, 4.13b2.

## What the code is

**The crowd** has a looping sound (voice slot `0x00560fa8`, -1 for none) with two levels (`0x00560fac`, `0x00560fb0`) that wander towards random targets (`0x00560fb4`, `0x00560fb8`) by 100 a
frame, handed to the positional-sound update as two thirds of their value; a count of waiting cheers (`0x00560fc0`) turns into boos at random. A level has a crowd when its record (`0x0053c240`, 0x1ac
bytes a level) holds a non-zero dword at `+0x3c`; the byte at `+0` makes the crowd roar at scores above 15000. The reactions (`ReactToScore`, `ReactToTrick`, `ReactToBail`) scale the volume from the
score with the compiler's multiply-by-reciprocal divisions (read from the disassembly: `mulhi`, shift, round towards zero) and play the cheer sounds `0x30` / `0x11d`, `0x2e`, `0x31`, `0x2f` / `0x32`.

**The sound effect front.** `SFX_PlayX(sound, volume, extra)` (and `PlayLR`, `Audio_PlayPositionalSound`) look the sound up in the bank table (`0x029d7d20`, 128 record pointers), load it on demand when
its record says it is not loaded, scale the volume by the effects setting (`0x0052be44`, 0x4000 full, `>> 14`), store `pitch scale * constant` truncated for the HAL (`0x0056bed8`) and call the HAL's
`Audio_PlaySound`. In a split screen game (`0x00533f3c == 2` with a second player object) a player whose sound set flag is set hears substitutes (`0x1c` -> `0x189`, `0xb5..0xb8` -> `0x18a`,
`0xb9..0xbb` -> `0x18b`, `0x180` -> `0x181`) and nothing for `0x1d` / `0x1e`. The VAB openers match a file name against the known banks (front end `shell.vab`, `skate2.vab`, `skate2g.vab`,
`skate2p.vab` = 0..3; the fourteen level names = 4..17) and hand the id to `Audio_LoadSoundBank`. The reverb functions build a command record on the stack for the SPU call.

**The redbook sequencer** (a name from the PSX CD music; here it drives the PC's Bink music files `LTIX<n>.DAT`) keeps a group (0..3) and track (0..7) at `0x0056b888` / `0x0056b88c`, a table of 32
track lengths (`0x0053770c`, -1 for none), a delay of 30 frames after a track starts, a fade (`0x0053778c`) and the movie interface for the actual playing. With the music setting at 0 the level picks
its track (`level / 8`, `level % 8`, the volume from the effects setting); otherwise `Redbook_Next(step)` walks along the tracks and skips the empty ones and the first two groups (index below 16).

**The movie interface** wraps Bink: `PCMOVIE_StartFMV`, `StopFMV`, `Update`, `Pause`, `StartMusic` (a streamed music file on a thread, `0x004e71d0`), `SetXAVolume` (`(left + right) * 64` to `BinkSetVolume`,
pan 0x8000), `XAPlay(group, track)` (the file name `LTIX%02i.DAT` of `group * 8 + track`), `XAStop`, `XADone`, the movie window's primitive (`PCMOVIE_DrawWindow`: four vertices from a rectangle of
16-bit corners, 0xc0 bytes, linked into the first bucket of the ordering table) and `PCMovie_PlayStartupMedia`, the blocking pump of messages that plays a movie until it ends, a key or button comes after a frame
without one, or the window is closed. `gamefmv.cpp` keeps the list of game FMVs (36-byte rows from `0x0052cfa2`, type `0x60` for a movie) and copies a row into the current FMV.

## Not ported (and why)

| What | Verdict |
|---|---|
| `0x004add10..0x004ae020` (a particle emitter class: constructor with an SEH frame, destructor, update, tail) and the wind words `0x0056c510` / `0x0056c520` with their clears | not sound code: re-homed to **4.15d** (`milestones.yml`) |
| `0x004abae0`, `0x004adc90` (`SFX_SetVoiceVolume`, logs "not implemented"), `0x004adcb0` / `0x004adce0` (jumps), `0x00454e40`, `0x00454e60` (a jump to `StopFMV`), `0x0042f3c0` | Ghidra lists no function: dead or reached only by jump; not counted |
| Bink's functions (`StartBinkMovie`, `CreateBinkSurface`, `NextBinkFrame`, `CloseBinkMovie`, `BinkMovieUpdate`, `SetupFullScreenMovie`, `DrawFullScreenMovie`, the stream thread) | out of scope (`scope.yml`), services here |

`Crowd_ClearSound`, `Crowd_Nothing`, `Crowd_Start`, `Crowd_Stop` (`0x0042f2e0..0x0042f370`) were in 4.15f's "taken from the left" range; they are crowd code and were ported with the rest of `crowd.cpp`.

## Findings

1. **`tony-skip-movies` keeps breakpoints on `PCMovie_PlayStartupMedia` (`0x004e5ec0`) and `StartBinkMovie` (`0x004e6590`)** that return at once. A forced call that reached either was cut short (the
   stub saw the *call site's* stack, `eip` landed in a string). The probe disables them (the 4.13a lesson about harness breakpoints, again).
2. **Imports are stubbed on their call sites.** Sleep, `PeekMessageA`, the Bink calls are called through import slots; a breakpoint on the DLL function is also hit by Wine's own threads. `fcall.py` gained
   `stub_site(address, length, ...)`: the stub fires on the `call` instruction of the function under test only, reads the arguments from the top of the stack and resumes after the instruction.
3. **VC6's `_ftol` returns the low dword of a 64-bit truncation** (`fistp qword`): a pitch above 2^31 wraps instead of giving `0x80000000`, and one above 2^63 gives 0.
4. **The reverb command record is `{command, value, packed}`** (12 bytes): `SFX_SetReverb` fills the first two dwords (the third is stack garbage), `SFX_SetReverbDepth` the first and third.
5. **Bug-faithful details the replay pinned:** `Redbook_Play` calls the movie interface *before* its assertions; `Redbook_SetPaused` hands the movie interface its argument as a full dword; `Redbook_Next` with a
   table that has no valid track at or above 16 (or a step that never reaches track 0) loops for ever (the probe's generator checks it ends); `Audio_OpenFrontendVab` with an unknown name re-opens the bank
   already open; `Crowd_ReactToTrick` below volume 0x1000 raises it to 0x1000 and skips the half-volume `0x2e` sound; `PCMovie_PlayStartupMedia` ends on a key only after a frame without one.
6. **Scripted values of a call-site stub that writes memory** (`Sleep` clearing the "playing" flag): the native loop takes the value from the stub's return of the same call (`win32()` already consumed it).
7. **A split screen read through a real pointer.** `SFX_PlayX` reads `[0x0056a958] + 0x2cc0`; the crowd cases left that live pointer in place until the probe seeded a fake player object for every sound case.
8. **Not forced-callable: the display surface error branch of `PCMovie_PlayStartupMedia`** (`0x004e5fda`: a non-zero result of the surface's slot 0x23 reports the error and calls `exit(error)`).
   `exit` never returns, so the compiler left its argument on the stack; a stub that returns unbalances the stack and the forced call faults. The native port reports the same four calls
   (`engine_004cfb60`, `engine_004d0f80`, `engine_00501439`, then carries on) and that branch is **disassembly-derived only**; the probe's fake surface method always answers 0.
9. **x87 precision:** the pitch scale product is float x float, exact in the 53-bit mode of a forced call (and outside the render window in the game), so the native port uses double math.

## Validation

Retail forced calls on generated state in the running game (a level loaded, `re/gdb/probes/frontend_calls.py` with the groups `crowd`, `sfx`, `vab`, `redbook`, `movie` of `audio_specs.py`): the
probe makes a legal state in the game's globals and a scratch slab, calls the retail function, and records the return value, every written byte and the ordered service calls (HAL voices, the pan service,
the SPU reverb, Bink and the Win32 imports with their scripted answers). The replay (`audio_retail_calls_test.cpp`) starts from the same bytes and compares all three. The committed fixtures
`re/evidence/fixtures/audio-calls-*.txt` keep the first 30 cases of each function; the full captures are in `build/p413b`.
