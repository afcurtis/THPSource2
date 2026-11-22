# Features and play-mode fixes

For work a player sees: the launcher, graphics, sound, the HUD, options, mods, packaging, and the crashes and wrong pictures the person reports.
Read `docs/WORKING.md` first. Scripts named here are in `scripts/play/` (index: its README).

## The loop

1. **Evidence from the person's play** (the test install's `Logs\`; its path is in `AGENTS.local.md`): `sessions.txt` first (one
   line per run: NORMAL QUIT / CRASH / HANG SUSPECTED), then `crashes.txt` or the run's folder under `Logs\runs\` (`run.txt`, `input.txt`,
   `build.txt`); `notes.txt` holds the person's remarks. With the launcher's Debug mode off only the active session and the last crashes are kept.
   **A stale install is the first suspect:** compare `build.txt` with the build's date. Settings and the game's bindings are one file,
   `Config\THPSource2.ini`; the install's layout is `docs/LAUNCHER.md` "The install folder".
2. **Reproduce headless** before changing anything: replay the person's run (`scripts/input_log_to_script.py RUN\input.txt -o x.txt`, then
   `m12/replay_play.py x.txt POLLS NAME`; set `XTR_OT_FRAME_RATE=120` when they played at 120), a level idle (`m12/levels_spin.py`), random input
   (`m12/fuzz_hangar.py`, `m12/fuzz_career.py`), a quick start with keys by poll (`qs/run.py`), session scenarios (`re/scenarios/sessions/*.yml`).
   A stop is a `FATAL in native X: ...` line; `m12/sym.py` names a `CRASH c0000005` (a native faulted: exit 192, no FATAL).
3. **Show, don't describe:** take the picture headless and look at it (`g9/ride.py`, `g9/replay_shots.py`, `g1/shot.py`, `qp/run.py`).
   Compare a wide frame's middle 640 columns with the 4:3 one to measure a layout error in pixels.
4. Fix, check, gate, ship and write it down: `docs/WORKING.md` section 5. End with what the person should check in play.

## How the pieces fit (2026-11)

- **`THPSource2.exe`** (`src/launcher/`, Dear ImGui on SDL3): settings, presets, game files import, high scores, the crash panel and its Report on GitHub button, the update notice (docs/LAUNCHER.md, "Updates"); starts
  `Build\opentony.exe` with the settings as environment variables. `docs/LAUNCHER.md`.
- **`opentony.exe`** (x86-64; a 32-bit build exists): the first run reads the static data out of the player's `THawk2.exe` once (`Data\Unpacked\
  thawk2_static.img`) and unpacks `ALL.PKR`; after that `THawk2.exe` is not used. It starts `opentony_spike.exe` (linked at 0x00400000, a
  zero-filled section reserves the retail range) with `opentony_standalone.dll` (every ported function plus the resolver: `engine_/vcall_<addr>`
  -> native -> seam -> `ServiceFault` naming the address) and `opentony_spike_video.dll` (SDL). Fault policy: fail-stop (`kits: FATAL in native X`).
- **The game's code** is the retail functions ported to C++ in `src/runtime/` (Kits over the game's memory, 987 of the 1,325 a Hangar load runs, all
  traced against retail). Changing one is reconstruction work: `docs/working/RECONSTRUCTION.md`.
- **The platform layer** (`src/hal/`, `docs/HAL.md`): window, input, sound, clock on SDL3; the GL backend draws the game's screen-space polygons
  (final colours, no normals or lights). Movies and music: FFmpeg 8.1.3 decode-only DLLs behind the Bink seams (`standalone_movie.cpp`).
- **Mods:** the loose-file overlay (`src/hooks/mod_overlay*`, `docs/HOOKS.md`): `<game dir>\Mods`, `mod.ini`, exact paths below `data`;
  `Mods\Music` is the custom soundtrack.
- **The 64-bit build** keeps guest memory in a 4 GiB arena; every guest/host pointer conversion goes through `arena::` (`docs/PORT64_AUDIT.md`).

## A new player option, end to end

1. Launcher setting: `src/launcher/thpsource2/core/settings.{hpp,cpp}` (the INI key and its default; the `env.emplace_back("OT_...", ...)` list that
   becomes the game's environment), the control on its tab, a preset's value if presets set it, and a check in `core/launcher_core_test.cpp`.
2. The game reads it once: `src/hooks/kits/standalone_options.cpp` (`runtime::host_options().x = flag("OT_X")`).
3. The runtime acts on `runtime::host_options()` (`src/runtime/host_options.hpp`). Read and write it only in the runtime: it is an inline static,
   so the video DLL (`standalone_video.cpp`) has its own copy and never sees the runtime's.
4. Default off and absent from the gates' environment: the recorded scenarios run with no options, so a host-side feature behind an option
   cannot change them (that is why the launcher, the GL backend and options behind a flag need only `gates quick`, `AGENTS.md`).
5. `docs/LAUNCHER.md` (the option and its env var) and the plan's Standing facts.

## What generalises from the play-mode fixes

- **Read the game's memory, do not write it**, for anything cosmetic (weather, HUD layout, pictures): the run stays the same run (physics, scores,
  replays, parity). Label the exceptions and keep them behind their own option.
- **The game thinks at 30 Hz.** At 60 or 120 fps anything counted per frame runs fast or steps: message timers (`Mess_Update`), menu key repeat
  (`CMenu_Update`, `Pad_RepeatDirection`: `framepacing::hold_steps`), objects drawn where they stood (cars, platforms, power-ups: drawn between their
  last two 30 Hz states, `for_each_smoothed`). Check a new feature at 30 and at 120.
- **Wide pictures:** the 2D layer keeps its 4:3 width (a 3630-wide 2D layer stopped the game); the HUD is placed by element groups
  (`hud_groups`, `place_hud_group`); a menu's 3D model is walked after the view's full-screen cleanup rectangle, so tie a fix to the polygon
  (`presentation_view_polygon`), not to "the current viewport".
- **Distances are the game's own:** fog far distance, the ±32767 object box around the camera (`cull_objects_to_view`), the power-up hide and park
  distances, trigger boxes that hide areas. Stretch them for a player option by the same scale, only for what the option is about.
- **A save is the player's data:** quick play greys every path that writes `*.SAV`/`*.PRK` (`user_file_locked` is the backstop); the save image
  holds copies of live words (sound levels, trick tips) that screens read: set both.
- **Paths are UTF-8:** a path that goes to FFmpeg, SDL or `std::filesystem` is read wide (`GetEnvironmentVariableW`) and converted; the programs run
  with the UTF-8 code page (`utf8.manifest`). Test with a linked folder named `Gäme` (`nb/umedia.py`).
- **The person's PC is the reference for feel and speed:** time it there when it matters (a slow boot was SDL's DirectInput pad scan:
  `[Gamepad] DirectInput`, off by default).
- **Bisect by behaviour:** flip the one option or row and rerun the one scenario before reading code. A difference is first a question about the
  input (settings, start image, working directory, path form). Smoke before any gate chain: a failure in the first minute means stop, not wait.

## Traps (symptom -> cause -> do)

| Symptom | Cause | Do |
|---|---|---|
| A `host_options()` flag set in one place is never seen in another | inline static: the runtime and the video DLL each have a copy | read and write it only inside the runtime (DrawKit) |
| Black or flickering window when the frame ends on a world draw | GL state left by the last draw (mip sampler, alpha-to-coverage) reached the present's downsample | `video_present` resets it; reset any new per-draw GL state there |
| Window "(Not Responding)" / WER `Application Hang` in the menu, no FATAL | SDL's pump does not reset Windows' 5 s hang timer for an idle window | `platform_drain_messages()` (`standalone_platform.cpp`); diagnose hidden: `IsHungAppWindow` / `SendMessageTimeout(WM_NULL)` every 2 s, `OT_HANG_DUMP=SECONDS` |
| Frame rates read low and worse run after run (120: 107, 97, 85, 69) | a run ended with `Process.Kill()` leaves `opentony_spike.exe` spinning | end runs with `taskkill /F /T /PID`; `Get-Process opentony*` empty before measuring |
| Level music silent, log shows `Redbook_Next` -> `StartBinkMovie(LTIX..)` | the Bink seam had no audio backend | `OT_MOVIE_LOG=1` shows `stream N opened` |
| Retail and the standalone differ after a save, or a run differs the second time | the game folder is shared: a leftover `*.SAV`/`*.PRK` changes the card scan | `tony.standalone_boot.clean_saves()` runs before both; remove strays by hand |
| Parallel runs: one dies at once with `FileNotFoundError ... CAREER A.SAV` | two runs started in the same instant unlink the same save | stagger starts and retry (`fr/sweep.py`: 4 workers, random delay, 4 tries) |
| Parallel runs end with no FATAL and no `session:` line, or hang to the timeout | a retail/capture run's `wineserver -k`; a leftover `services.exe` holds the output pipe | no retail run, capture or gates during fuzzing; rerun the casualties (`m12/wide_redo.sh`); kill leftovers by PID |
| `run.txt` shows file events far from their `delivery N` line | stdout and stderr of a Wine run merge out of order | order events of one stream; `OT_DELIVERY_LOG=first:last` |
| Scenario `action` lines and `OPENTONY_SESSION_KEYS` both given: keys do nothing during an action | `scenario_after_bindings` overwrites the action mask's low word | pause = a `start` (0x800) action with the others cut around it (`fuzz_hangar.py`); menu keys by poll |
| `tests/test_scenarios.py` fails after a new `re/scenarios/*.yml` | the tests accept only the gated scenarios | keep experiment scenarios untracked and delete them after (`fuzz_compare.sh` does) |
| A retail capture fails at once, `status=4, error=7` | a crashed capture left a non-empty `build/standalone-warehouse/NAME/retail.otcap` | delete it, rerun |
| A standalone run that hits no FATAL prints nothing | normal | `build/m10e/spin.txt` (`session: N deliveries`) |
| `draw: general polygon ... has no descriptor (skipped)` in `run.txt` | a textured 0xB0 polygon whose texture record has no descriptor (retail faults there) | expected; it is logged, not drawn |
| A file opens from an ASCII path but not from `C:\Users\José\...` | a narrow-call path handed to a UTF-8 API | read it wide and convert (see above) |
| The install stops half-way | the launcher is open and holds `Build\SDL3.dll` | close it, install again |
