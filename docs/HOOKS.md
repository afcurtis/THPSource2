# Native hook harness (`opentony.dll`)

The hook harness runs reconstructed native code inside the unmodified retail
`THawk2.exe`, one function at a time, with every replacement switchable between
native and retail at runtime. It is the "re3 model" stage of the port: the game
stays playable while functions move from retail to native.

```text
THawk2.exe ──static import──▶ dinput.dll (proxy, src/hooks/dinput_proxy.cpp)
                                 │ LoadLibrary
                                 ▼
                             opentony.dll (src/hooks/)
                                 │ identifies the build (PE timestamp + SizeOfImage),
                                 │ checks each prologue, then patches the entry:
retail entry ──jmp──▶ dispatch stub ──active?──▶ native implementation
                                     └─no────▶ trampoline (original prologue) ──▶ rest of retail function
```

- **Build identity.** The table carries addresses for the canonical build
  (Activision Value re-release, `executables.thps2_pc`) and for the original
  2000 retail build (via `re/xref/original_2000.yml`). An unknown executable,
  or a prologue that differs from the manifest (for example one patched by
  another mod), leaves that function retail.
- **Dispatch stub.** `cmp byte [active],0 / je retail / lock inc [native_calls]
  / jmp native / retail: lock inc [retail_calls] / jmp trampoline`. Only EFLAGS
  is touched. Flipping `active` is a single byte store, so switching at runtime
  is safe even while the game is running.
- **Trampolines.** The overwritten prologue bytes are copied and followed by a
  jump back. `tony hooks verify` guarantees the prologue ends on an instruction
  boundary, contains no relative branch, and is not a branch target.

## Files

| Path | Role |
|---|---|
| `re/config/native_hooks.yml` | Hook manifest: names, per-build addresses, expected prologues, ABI, fault variants, clock service |
| `src/hooks/native_hooks_generated.h` | Generated C table (`tony hooks generate`); defaults come from `re/native/functions.yml` status |
| `src/hooks/hook_runtime.cpp` | Identity, configuration, trampolines, dispatch stubs, hotkeys, log and stats |
| `src/hooks/native/*.cpp` | ABI adapters that register native implementations (`OT_REGISTER_NATIVE`) |
| `src/hooks/mod_overlay*.{h,cpp}`, `src/hooks/native/overlay_open.cpp` | Loose-file overlay (Phase 5.2): finds mods, indexes their files, serves them from `FileIO_BackendOpen` |
| `src/hooks/selftest.cpp` | In-process differential self-test (`OPENTONY_SELFTEST=1`) |
| `src/hooks/clock.cpp` | Lockstep clock service (deterministic fixed timestep) |
| `src/hooks/dinput_proxy.cpp`, `dinput.def` | Injection proxy; can chain to another dinput mod |
| `cmake/toolchains/win32-clang-cl.cmake` | Linux cross toolchain: clang-cl + lld-link + the pinned VC6 SDK/CRT |
| `src/hooks/kit_abi.h` | C interface between opentony.dll and the Kit DLL |
| `src/hooks/kits/` | `opentony_native.dll`: the Kits of `src/runtime/` over the live process, the live host (`kit_dll.cpp`) and the generated adapters (`kit_hooks_generated.cpp`) |
| `cmake/toolchains/win32-llvm-mingw.cmake` | Linux cross toolchain for the Kit DLL: the pinned llvm-mingw (`tony setup llvm-mingw`) |
| `tony/hooks.py`, `tony/kit_hooks.py` | `tony hooks ...` and the `--hooks/--clock` options of `tony scenario`; Kit hook rows, checks and code generation |

Native arithmetic lives in portable headers under `src/runtime/` (for example
`retail_vector3.hpp`) with Linux unit tests; the adapters in `src/hooks/native/`
only translate the calling convention.

## Build and install

```bash
tony setup sdl3             # pinned SDL3 for the platform HAL (re/config/sdl3.yml)
tony setup glslang          # pinned shader compiler for the SDL_GPU video backend (re/config/glslang.yml)
tony setup llvm-mingw       # pinned llvm-mingw for the Kit DLL (re/config/llvm_mingw.yml)
tony hal shaders            # compile src/hal/sdl/shaders/*.{vert,frag} to the committed sdl_video_gpu_shaders.h
tony hooks build            # build/hooks/win32/{opentony,dinput}.dll and build/hooks/kits/opentony_native.dll (add --capture for the recorder)
tony hooks stage            # copy dinput.dll, opentony.dll, opentony_native.dll, SDL3.dll next to the no-CD exe (build/disc/files/SETUP/data)
tony hooks stage --game-dir /mnt/c/Games/THPS2   # any install of either supported build
tony hooks stage --remove
```

`tony hooks build` needs clang-cl, lld-link and llvm-lib (LLVM 19+) and the VC6
toolchain from `tony setup vc6`; it publishes a lower-case view of the VC6 SDK
under `build/toolchain/vc6-sdk`. On Windows, any `cmake -A Win32` MSVC
generator builds `src/hooks` directly.

A manual install is four files next to `THawk2.exe`: `dinput.dll`,
`opentony.dll`, `SDL3.dll` and (optionally) `opentony.ini`. SDL3.dll is loaded
only when a `[hal]` service selects SDL, so it may be left out while every
service is retail. On Windows nothing else is needed. Under Wine set `WINEDLLOVERRIDES=dinput=n,b`. `stage` refuses to
overwrite another mod's `dinput.dll`: rename it and name it in `[proxy] chain=`.

## Configuration

Precedence: manifest defaults < `opentony.ini` < environment.

| Setting | Effect |
|---|---|
| default | Native where `re/native/functions.yml` says `trace-validated`/`integrated`, retail where it says `tested` |
| `[hooks] default = manifest\|on\|off`, `Name = on\|off` | Per-install selection |
| `OPENTONY_HOOKS=all\|none\|default,Name,-Name,fault:Name` | Per-run selection (`fault:` = deliberately wrong negative-control variant) |
| `[runtime] native = on\|off`, F9 in game | Master switch: every hook native or every hook retail |
| F10 in game | Log per-hook native/retail call counts |
| `[runtime] clock` / `OPENTONY_CLOCK=realtime\|lockstep\|lockstep-fast` | Clock service (below) |
| `OPENTONY_HOOK_STRESS=<ms>` | Flip the master switch every *ms* milliseconds (toggle stress test) |
| `OPENTONY_LOG_DIR` | Where `opentony.log` / `opentony.stats` go (default: next to the DLL, else %TEMP%) |
| `OPENTONY_SELFTEST=1` | Run the differential self-test in DllMain, then exit before WinMain |
| `[hal] clock\|window\|input\|video\|audio\|movie = retail\|sdl` | Platform HAL service selection, fixed at startup (docs/HAL.md); F9 never touches it |
| `OPENTONY_HAL=input=sdl,clock=sdl`, `video=sdl`, `all=sdl`, `none`, `probe` | Per-run HAL selection (`input=sdl` and `video=sdl` turn `window=sdl` on; `video=sdl` and `movie=sdl` turn each other on); `probe` starts and stops SDL with every service retail |
| `[hal] video_backend = auto\|gpu\|gl`, `OPENTONY_VIDEO_BACKEND=gpu\|gl\|auto` | Which SDL video backend `video=sdl` uses (docs/HAL.md, "Video"): `auto` (default) picks SDL_GPU if a device can be created, else falls back to OpenGL 3.3; `gpu` forces SDL_GPU and fails loudly if unavailable; `gl` forces OpenGL 3.3 |
| `OPENTONY_HAL_QUIT_AFTER_MS=<ms>` | Test switch: post WM_CLOSE to the game window this long after startup |
| `OPENTONY_VIDEO_TRACE=<first>:<count>` | Record the game's Direct3D 7 calls for those frames (counted by EndScene) to `<log dir>/video/`, and what the ordering-table adapter makes of the same ordering tables (`.otvh`), for the oracles (`tony hal video summary\|png\|replay\|shadow`). With `video=sdl`: write the HAL's frames (counted by D3D_PresentFrame; `<first>:<count>:<step>` writes every `step`th) as `frame_NNNNN.hal.ottx` for `tony hal video ingame` |
| `OPENTONY_AUDIO_DRIVER=<name>`, `OPENTONY_AUDIO_FILE=<path>` | `audio=sdl`: the SDL audio driver (`wasapi`, `directsound`, `dummy`, `disk`); with `disk`, the mixed output (float32 stereo) is written to the file |
| `OPENTONY_MOVIE_TRACE=<first>:<count>[:<step>]` | Test switch (`movie=sdl`): write full-screen movie frames (counted over all movies) as `<log dir>/video/movie_NNNNN.hal.ottx` |
| `OPENTONY_VIDEO_MIRROR=<N>` | Test switch: draw every frame a second time through the HAL video backend (SDL, OpenGL 3.3) in its own window, and compare it with retail's frame every N frames (0 = never) in `opentony.log`; needs window=retail |
| `OPENTONY_XVFB_DEPTH=24` | Headless runs: X server depth for one run (the configured 16 bits offer no OpenGL 3.3 pixel format) |
| `OPENTONY_HAL_TEST_GAMEPAD=1\|2\|3` | Test switch (input=sdl): attach an SDL virtual gamepad before input starts; `1` also presses south and pushes the left stick on a fixed poll schedule, `2` only attaches it, `3` presses east (joypad button 1, the default accept) on the schedule |

### Session probe (Phase 5.1s)

A test switch of the lockstep clock for whole-session runs (boot, menus, career, a level), canonical build only,
off unless `OPENTONY_SESSION_TRACE` is set (`src/hooks/session_trace.cpp`):

| Variable | Effect |
|---|---|
| `OPENTONY_SESSION_TRACE=<N>` | Hash the writable image (`.data`, 64 KB blocks) every `OPENTONY_SESSION_EVERY` clock deliveries into `session.trace` in the log directory, write `session.done` and the stats, and stop the process at delivery N |
| `OPENTONY_SESSION_EVERY=<k>` | Deliveries per hashed record (default 2: one per frame) |
| `OPENTONY_SESSION_KEYS=<poll>:<dik hex>:<hold>[,...]` | Hold a DirectInput key for `<hold>` input polls from poll `<poll>`: written into `KeyboardState` after the keyboard device update of `PCInput_Poll`. Enter is `1c`, left `cb`, Esc `01`, F9 `43` (the game's screenshot, `SCRnnnnn.bmp` in the game directory) |
| `OPENTONY_SESSION_SKIP_MOVIES=1` | The startup movies (`0x004e5ec0`, `0x004e6590`) return 0 at once |
| `OPENTONY_SESSION_DUMP=<delivery>` | Write the image at that delivery to `session_<delivery>.img` and, since 5.4a, the large private read/write regions (the game's memory zones are allocated, not static) to `session_<delivery>.heap` as `(base, size, bytes)` records |
| `OPENTONY_SESSION_ORACLE=<addr hex>[,...]` | 5.4a: right after the dump, call these argument-less cdecl retail functions and log `session: oracle <addr> = <eax>` (the answers `opentony_spike.exe` must reproduce: `scripts/standalone_spike.py`) |
| `OPENTONY_SESSION_NORM=0` | Hash raw words. By default a word that points into the game's memory zones (`0x0056a97c..`, `Mem_Init`) is hashed as an offset from the lowest zone address, because any hook that allocates shifts the zones and every stored heap pointer with them |
| `OPENTONY_SESSION_MASK=<addr hex>:<len hex>[,...]` | Hash the words overlapping these ranges as zero: cells that hold no game state (the assertion bookkeeping `0x00568638` and `0x0056881c`, process-identity pointers) |
| `OPENTONY_SESSION_HEAP=1` | Also hash every committed private read/write region into `session.heap` (raw heap hashes differ run to run: absolute pointers) |
| `OPENTONY_SESSION_WATCH=<addr hex>:<bytes hex>[,...]` | 9, a measurement mode: log every call that passes the address as `session: watch <addr> from <return address> (<four stack words>) at delivery <n>`. `<bytes>` (5 to 16) is the length of the whole instructions there that the jump covers; they are copied to a trampoline as they are, so none may be relative. Hooks own the first bytes of a function even with `OPENTONY_HOOKS=none`, so watch an instruction after them (`Mem_New`: `46f49e:6`; an address that starts with a jump or call is refused). `build/s9/retail_watch.py` runs it on `boot-menu` |
| `OPENTONY_DELIVERY_LOG=<first>:<last>` | 10g s22, a measurement mode of the lockstep clock: each delivery in that range as `delivery <n>:` and the retail code addresses on the stack above the delivery (the site's callers among them); the standalone's twin is `OT_DELIVERY_LOG` (the site, the native that reached it, the polls and the simulation tick). A capture with a short `OPENTONY_SESSION_TRACE` may leave no log: export it into a full `tony standalone warehouse` run instead |

Use `lockstep-fast`. The game's own `THPCERR.LOG` names the screen (`Shell Screen: 17 - OPTIONS`) and the level load;
its tail is buffered, so the last screen of a short run may be missing: use F9 screenshots to see a screen.

**Session scenarios** are `re/scenarios/sessions/NAME.yml` (`version`, `description`, `deliveries`, `keys`: a list of
`{poll, key, hold}`, keys by name: `esc enter space up down left right f9 tab backspace`, or a scan code). The main menu
is horizontal (left: Options; right x1 Career, x3 network, x4 Create-a-Skater, x5 level editor). The Warehouse scenarios'
`tony scenario` commands do not read this directory.

```bash
tony session list
tony session run NAME [--hooks SPEC] [--dump DELIVERY]   # one run: build/sessions/runs/NAME-run/session.trace
tony session parity NAME...                              # the gate
```

`tony session parity` runs retail twice (`retail-a` is the reference, `retail-b` finds the cells retail itself does not
repeat), a retail run with the stress thread (`retail-stress`: the thread shifts the Wine heap), `native-all` (every row on,
including `default: "off"`), `native-default` (the shipped selection: every row that is not `default: "off"`),
`native-all-stress` (compared with `retail-stress`) and the fault control. Pass: `retail-b`, `native-all`,
`native-default` and `native-all-stress` MATCH, the fault control DIVERGES. The output counts the hooks and the `default: "off"`
rows each run reached natively and lists the off rows `native-all` reached (the input of 5.1t). Blocks 24 and 587 are masked
whole (process identity); everything else is compared on pointer-normalised words with the byte masks above. Results:
`build/sessions/parity/NAME/parity.json`. The scenarios: `boot-menu`, `boot-options`, `boot-career-hangar`,
`boot-create-skater`, `boot-career-end-run`.

**The environment is padded to one size (7a, 2026-10-29).** The Wine heap moves with the length of the process's environment
block, 32 bytes per step of it, and the cells that keep heap addresses move with it (five pointers at `0x0055dce8`, `0x0055dd0c`,
`0x0055dd10`, `0x0055dd28`, `0x0055dd5c` in block 3, two in block 23, the wrapper pointers of blocks 24 and 587). A sweep of the
length (`OPENTONY_ENV_PAD` of 0 to 60 characters, first record of `boot-options`) gave block 3 hashes that are a step function of it:
one value at 0, another at 4 to 24, a third at 28 to 56, a fourth at 60. The runs of one matrix differ in four variables: the hooks
selection (`none`, `all`, `default`, `fault:NAME`), the run directory (it holds the label), the stress switch (present or not) and
the dump delivery. So a matrix passed or failed depending on where the steps fell relative to the invoking shell's own environment: it
passed on 2026-10-29 at 05:26 and failed from 07:2x on, with the same bytes of hook DLL. The likely reason is that the environment of
the shell that started the gate had a different length by then (inferred: the earlier environment is gone, it was not recorded).
What is measured is the dependence on the length and that the fixed length makes both sessions pass.

The run directory matters a second way: it is the game's log directory (`OPENTONY_LOG_DIR`), and Wine converts its path to a wide
string on the heap for every file the probe opens, so a longer directory name moves the heap even when the environment is padded.
With the environment padded and the names `boot-menu-retail-stress` / `boot-menu-native-all-stress` (23 and 27 characters)
`retail-stress` and `native-all-stress` differed at block 3 in every run (`47f974a9` against `11dd4ff9`, three of three, serial or
parallel); with `session_label` padding every name to 24 characters they are equal (`47f974a9` both, three of three). So all
four things that differ between the runs of a matrix are now equal in size: the hooks selection, the stress switch and the dump
delivery (the environment pad) and the run directory (a fixed-width label). `tony/session_scenarios.py` `pad_environment` now adds `OPENTONY_ENV_PAD` so that the four
variables and the pad always add up to 360 characters (it refuses a run that does not fit); nothing in the comparison was masked or
loosened, and blocks 3 and 23 are compared again. Runs that need a different environment (a dump at another delivery, a longer label)
therefore stay comparable. Warehouse scenario parity (`tony hooks parity`) has the same exposure in principle (its labels and hooks
specs also differ) but does not fail; it was not changed.

**A fault control only counts where the faulted hook runs.** The fault hooks are the vector-math hooks (`Math_Vector3Add` ...), and
only the sessions that load a level reach them (`boot-career-hangar` 6,606 native calls, `boot-career-end-run` 6,375; `boot-menu`,
`boot-options`, `boot-create-skater` 0). In those three the control used to "DIVERGE" only because `fault:Math_Vector3Add` is a longer
environment than `none`: the artifact above, not a control. The comparison now reports such a run as `control not exercised:
<hook> has no native call in this session` and holds it to the rule of a real variant (it must MATCH). The three menu sessions
therefore have **no negative control** of their own; the hangar and end-run sessions and the Warehouse scenarios carry it.

## Mods: the loose-file overlay (Phase 5.2)

Files in a mod folder answer the game's own opens before `ALL.PKR` does. Canonical build only; the
hook is `FileIO_BackendOpen` (`0x004e75b0`, the one backend open every asset, bitmap, sound and script goes
through), `default: "off"` in the manifest and **turned on by the runtime when a mod has files**.

```text
<game dir>\Mods\<mod>\mod.ini        [Mod] Name, Enabled = 0 leaves the mod out
<game dir>\Mods\<mod>\Data\...       mirrors the game's data directory
```

- **Where mods are looked for** (first that is set): `OPENTONY_MODS` (`;`-separated folders, each a mod or a folder of mods;
  `none` turns mods off), `[mods] path` in `opentony.ini`, `<game dir>\Mods`. Mods in a folder load in alphabetical order; a
  folder without `mod.ini` is skipped (logged); a file in a later mod replaces the same file of an earlier one.
- **Names.** A file is addressed by the path the game opens, below `data`, case-insensitively: `Data\SkWare_T.trg` replaces
  `data\SKWARE_T.TRG`; `Data\audio\selectD.wav` replaces `audio/selectD.wav` (the sounds are not under `data\`);
  `Data\logos\x.bmp` replaces `data\logos\x.bmp`. **Only the exact path matches**: the game also asks for optional variants
  of a file in other directories (`NEWBMP\s2bgd0.bmp` beside `s2bgd0.bmp`), and a match by file name would stand in for
  them. A file the package does not have is added (`FileIO_Exists` finds it too). Files must have the formats of the
  files they replace (`.bmp` 8-bit at the same size, PCM `.wav`, `.trg`, `.psx`).
- **Not replaceable by name:** a file the game reads from inside a `.pre` archive (short names without `bmp`, the PRE
  path of `FileIO_OpenResource`). Replace the whole `.pre` instead.
- **Trace.** `OPENTONY_MOD_TRACE=1` (or `[mods] trace = 1`) logs every replaced file in `opentony.log`
  (`mods: replaced skware_t.trg <- ...\Data\SkWare_T.trg (10590 bytes, slot 0)`); `all` (or `2`) also logs the opens that
  were not replaced, which is how to find the name the game asks for.
- **What the hook does.** It builds the same slot a package hit builds (a memory image from the executable's own `malloc`,
  type 1), so seek, read and close are retail's. Everything else, and every open no mod matches, runs retail's code
  through the trampoline. With no mod files the hook stays off, so the game is the hooked build it was.
  `OPENTONY_HOOKS` (or `[hooks] FileIO_BackendOpen`) decides the hook when set: `OPENTONY_HOOKS=none,FileIO_BackendOpen`
  tests the overlay alone.
- **Tests.** `src/hooks/mod_overlay_core_test.cpp` (ctest: key rules and the index); `tests/test_mod_overlay.py` with
  `OPENTONY_WINE_TESTS=1` (about 50 s: boot-menu serves a changed texture and a new sound, and `warehouse-idle` with a
  byte-identical `SkWare_T.trg` in a mod records the same game state as the packaged script in the `portable` scope; the
  full-scope comparison differs only in heap pointers, because the mod index is an allocation the other run lacks).

## Lockstep clock

Retail paces itself with a 16 ms `timeSetEvent` callback
(`SimulationTimeTimerCallback`) on the winmm thread; every frame limiter waits
until `SimulationPublicTick` has advanced twice. Two live runs therefore differ
from the first frames on, which makes exact A/B comparison impossible.

With `OPENTONY_CLOCK=lockstep` the asynchronous callback returns immediately and
the original callback body runs on the game thread at each *delivery site*
instead: `Game_FrameTick` (the message pump every limiter spins; its first
caller defines the game thread), `WINMAIN_Sleep`, and the tick loads of the two
call-free limiter loops. Each frame then receives exactly two deliveries (a
fixed 30 fps timestep), the callback runs under the timer thread's x87 control
word, and a run becomes a pure function of its inputs. `lockstep` still paces
deliveries to wall-clock time and is playable; `lockstep-fast` is unpaced and
meant for headless verification.

## Verification gates

**Run them with `tony gates`** (Phase 5.1p), in tiers:

```bash
tony hooks smoke        # about 35 s, right after `hooks build` + `hooks stage`: any hook that crashes boot or the recorder
tony gates quick        # about 2 min, while developing: verify, build, smoke, warehouse-idle + warehouse-rail parity, boot-menu session parity, the tests of what changed
tony gates full         # about 8 min, once, before a commit: everything below, the CPU lane beside the Wine lane
```

`tony gates` runs the commands below as a graph of steps: a CPU lane (native build, ctest, `verify --all`, pytest with
`-n 3`, the self-test, replay32) beside a Wine lane of isolated workers, **one run (scenario, variant) per step**, and one
compare step per scenario once its runs are in. `--jobs N` sets the Wine workers (default `min(cores / 2, RAM / 1.5 GB)`),
`--cpu-jobs N` the CPU steps at once; `--keep-going` / `--fail-fast` override the default (`quick` stops at the first
failure, `full` keeps going except after a critical step: verify, build, smoke, self-test, native build). `--only PREFIX...` runs
the steps whose name starts with a prefix and what they need (`--only session/boot-menu`), `--dry-run` prints the steps. Every step logs to `build/gates/logs/NAME.log` and appends one line to `build/gates/status.txt` when it ends
(`NAME exit N SECONDSs`, `NAME skipped (...)`, a last `DONE failed=N ...` line): start a Monitor on that file and look at
the first parity line, not the last. The per-scenario tables are in the compare steps' logs
(`build/gates/logs/parity_SCENARIO.log`), the results in `build/hooks/parity/SCENARIO/parity.json` and
`build/sessions/parity/NAME/parity.json` as before.

Isolation (`tony/workers.py`, `OPENTONY_WORKER=NN`): each worker has its own Wine prefix (`.tools/wineprefix-head-wNN`, a clone
of the headless one, made once and warmed up with two throwaway `boot-menu` runs: the first run in a fresh clone is sometimes in
another Wine heap state), its own disc tree under `build/gw/NN/disc` (links to the shared files; the
executable, the small files the game may write such as `TH2_OPT.CFG`, `opentony.ini` and the staged DLLs are copies; screenshots
and `THPCERR.LOG` are the worker's own), its own runtime directory (`opentony.stats`) and its own X display (`xvfb-run -n 99-NN`).
`wineserver -k` was already prefix-scoped. **The environment block of a Wine run keeps the length it has without workers**
(the paths above are as long as `.tools/wineprefix-headless`, `build/disc/files` and `build/hooks/runtime`, and `OPENTONY_WORKER` is not
passed on to Wine): its size moves the Wine heap and with it the pointer cells that the session comparison sees. The native-call
counts of a Warehouse capture are not reproducible even serially (they run until the host stops the process: 323,194, 329,423 and
333,786 for three runs of `warehouse-idle`); the verdicts and the session traces are. A hook change is tested by the same gates in either mode; the serial commands
below remain the definition of each gate and still work unchanged. `tony gates quick --native-hooks fault:NAME` swaps the
`native-all` selection to a fault variant, to check that the gate catches it.

Every hook change must pass all of these (`tony gates full` runs each of them):

```bash
tony hooks verify                      # manifest vs both executables, names, native status, header
tony hooks smoke                       # before any long run: boot-menu with every row native, one recorder capture with every hook installed (about a minute)
ctest --test-dir build/native          # portable arithmetic (e.g. opentony_retail_vector3_test)
tony hooks selftest --build all        # in-process: retail (trampoline) vs native vs patched entry on/off, 20k cases/hook
tony hooks parity warehouse-idle warehouse-straight warehouse-turn warehouse-ollie-land warehouse-turn-ollie \
    warehouse-rail warehouse-manual warehouse-spin
tony session parity boot-menu boot-options boot-career-hangar boot-create-skater boot-career-end-run
tony hooks replay32                    # when Kit code changed: the forced-call replays built 32-bit, under Wine
```

`tony hooks parity` captures each scenario with the in-process recorder under
the lockstep clock: twice with every hook retail (must match each other
exactly), with every hook native, with the master switch flipping every 3 ms,
and with a fault variant (must diverge). Recordings are compared with
`compare_recordings(scope="all")`: every input, timer/service event and
before/after player snapshot of every frame. Results land in
`build/hooks/parity/<scenario>/parity.json`.

A native variant sets aside exactly two things, and the output counts the
first (Phase 5.0):

- the recorder's observation events of calls made from inside a hooked
  function (and the observed function's self-calls nested in them): a native
  that reaches the same computation calls its own C++, so those retail call
  sites do not run (`native_callers` of `compare_recordings`);
- the never-initialized high half of `raw_physics_words[235]`
  (player+0x312c), which retail copies from the pad of a stack-local record:
  a native that calls retail from another stack depth changes it
  (`mask_uninitialized`; the `portable` scope masks it too).

`tony hooks census` classifies every native for live hooking (its services,
whether its entry can be patched) and `tony hooks row` writes a Kit hook row.

Platform HAL services have their own gate (docs/HAL.md, "Verification"):

```bash
tony hooks parity warehouse-idle warehouse-straight warehouse-turn warehouse-ollie-land warehouse-turn-ollie \
    --hal input=sdl,clock=sdl
```

It compares an all-retail reference with the selection (twice, then with every
native hook flipping) in the `portable` scope, which excludes only the player
object's heap address; results land in
`build/hooks/parity/<scenario>/hal-<selection>/parity.json`.

HAL service replacements live in `re/config/native_hooks.yml`
(`services.hal.replacements`, `services.hal.symbols`) and are checked by
`tony hooks verify`: each must be a hook point of its service in
`re/config/hal_boundary.yml`, with a registered adapter
(`OT_REGISTER_HAL_ADAPTER` under `src/hooks/hal/`), exact entry bytes and
address-map agreement on every build. Unlike leaf hooks they have no
trampoline and no F9 switch: a service is replaced whole or not at all, at
startup.

The GDB-driven `tony scenario retail/verify` path replays in-process
recordings too. It suppresses the timer callback and applies the recorded
deliveries itself. Deliveries the recording observed across the present
limiter (`0x004d0bc0`, between the timer update and physics entry) are handed
to the game thread one at a time at the same delivery sites the lockstep clock
uses, while that limiter spins. The lockstep parity gate remains the hook gate.

## Kit hooks (Phase 5.0)

The Phase 4 natives live in the Kits of `src/runtime/` (C++20, the standard
library, exceptions). opentony.dll is built against the VC6 SDK and CRT
(no exceptions, no modern STL), so the Kits run in a second DLL,
`opentony_native.dll`, built with the pinned llvm-mingw (clang + libc++ for
i686, statically linked, on the system UCRT; double arithmetic on SSE2 as in
the x86-64 builds the Kits were validated with). opentony.dll loads it before
it installs the hooks and calls `ot_kit_attach` (`src/hooks/kit_abi.h`), which
creates every Kit once per process and returns one generated adapter per Kit
hook; those fill the hooks' native slots, and the dispatch stub, trampoline,
F9, `OPENTONY_HOOKS` and stats work as for any hook. Without the DLL the Kit
hooks stay retail.

A Kit hook is a table row (`tony hooks row NAME --kit KIT --call EXPR [--returns]`
writes it from the executable and the fixtures):

```yaml
  - name: CBruce__HandleFlips
    kit: skater                       # kits.<id>: the Kit object
    call: "handle_flips(self)"        # the replay tables' expression: self, a[i], S(), U()
    returns: false                    # whether the retail callers use EAX
    return_value: "a[0]"               # optional, with returns: false: EAX is this expression (a struct returned by value hands the output pointer back)
    convention: thiscall              # cdecl | stdcall | thiscall; checked against the function's `ret`
    stack_args: 0
    reaches: []                       # the services its fixtures observed; each must be a live call
    selftest: words                   # optional (cdecl): the self-test may call it with any words
    overwrite: 7
    expected: "56 8b b1 88 2c 00 00"
    builds:
      canonical: 0x0049e430           # the Kits use the canonical build's data addresses
```

The live forms of the runtime model:

- **Memory.** `RetailMemory::make_live`: an access is a plain load or store at
  that address, as retail's is. `OPENTONY_LIVE_GUARD=1` makes every access ask
  `VirtualQuery` first (debugging only). The replays' scratch window
  (`pcplatform::kScratchBase`, retail's stack locals at fixed addresses) cannot
  be mapped under Wine, so live memory aliases it onto a buffer and the live
  host passes the real address of any argument that points into it. A pointer
  *stored inside* a scratch record is not translated, and the window is shared
  by every thread and nested native.
- **Services.** `RetailServices::attach(host)`: each reported call
  (`engine_<address>`, `assert`) becomes a real call of the retail function
  through its patched entry (a hooked callee runs native when its hook is
  active), under the caller's x87 control word, with the convention and
  argument count of `services.live_calls` (checked against the function's
  `ret`); its EAX answers the `value` that follows. A `value` with no call
  before it (a service's memory writes replayed as scripted values), an
  `engine_answer`, an unknown service or a wrong argument count is a
  `ServiceFault`.
- **Variadic services.** A `live_calls` row with `variadic: true` (cdecl only; `printf_log`) takes
  at least `stack_args` arguments and passes as many as the Kit reported. The probes record a
  variadic call's format string and not the numbers behind it, so the live host **skips** a variadic call
  whose format converts more arguments than the Kit reported (`format_wants_more`, `kit_dll.cpp`): retail's
  printf would read stack garbage, and a `%s` faults (`Audio_OpenFrontendVab`, 5.1t). Debug output only.
- **Tail jumps.** A function with no `ret` of its own that ends in `jmp _ftol` (the x87 helpers:
  `Math_SinQ12`, `Fixed_MulQ12`) returns through the target, so `tony hooks verify` takes the
  convention from the target's `ret` (`kit_hooks.callee_pops`).
- **Faults.** A native that has started has written memory and called
  services, so retail cannot take over: an exception out of a Kit (a
  `MemoryFault`, a `ServiceFault`, anything else) is logged
  (`kits: FATAL in native ...`) and the process stops with exit code 0xE5.
- **x87.** The adapter reads the caller's control word (the Kits emulate its
  precision: 24 bits on the render thread, 53 elsewhere), runs the Kit under
  `0x037f` (the Kits' long double arithmetic needs 64 bits) and restores it.

- **Struct returns.** A function that returns a struct by value fills a hidden output pointer and returns
  it in EAX, and its callers use it. The replay tables call such functions void, so a row says
  `return_value: "a[0]"` (any expression over `self` and `a[i]`, with `returns: false`).
- **The recorder's seams are not hookable.** The in-process recorder patches a few entries itself
  (`tony/capture.py`, `src/capture/win32/hooks.cpp`; `Skater_ProfileValue`, 0x0048f3a0, is its
  `shared_random_call`). A second patch on the same entry crashes the game after startup.
- **`OPENTONY_HOOKS` is read into 512 bytes and ignored, without a log line, when it is longer.** Select
  many hooks through `opentony.ini` (`default = off`, `Name = on`) and keep the environment spec short.
- **A crashed capture leaves a partial `build/scenarios/<name>/retail.otcap`**, and the next capture of that
  scenario then times out whatever the game does. Delete the directory after a crash.

`tony hooks replay32` runs every forced-call replay (`*_retail_calls_test`)
built 32-bit with the same toolchain under Wine on the native ctest's
fixtures, so the Kits are known to behave the same in the DLL's build.

## Standalone: natives by retail address (Phase 5.4a)

A program that never loads the retail image (`opentony_spike.exe`, `src/hooks/kits/standalone_spike.cpp`) loads **`opentony_standalone.dll`**, not `opentony_native.dll`: the same host code (`kit_dll.cpp` with `OPENTONY_STANDALONE`) over `standalone_kits_generated.cpp` (every native by address; `kit_hooks_generated.cpp` is still the hook adapters only). The hooked build is the differential reference, so it stays as small as it was: linking every native into `opentony_native.dll` (4.6 to 8.4 MB) changed the retail process the session scenarios compare. It attaches the same Kits with
`ot_kit_attach_standalone` and calls natives by retail address with `ot_standalone_call` (`src/hooks/kit_abi.h`). What a Kit reports as a
call of a retail function (`engine_<address>`, a `vcall_<slot target>`, a named service) is **resolved** by the standalone host in
`kit_dll.cpp`:

1. `kNatives`, the generated table of every native that has an adapter, sorted by address: the Kit hooks (`ot_kit_*`) and the rows of
   `re/config/standalone_natives.yml` (`ot_native_*`). The native is called with its own calling convention, under the caller's x87 control
   word, exactly as a live call is.
2. `call_seam` (`standalone_seams.cpp`): the out-of-scope functions of `re/symbols/seams.yml` that the program implements itself
   (`Game_FrameTick`, the clock, `CRT_sprintf` ...). A seam with no implementation is not an error until it is called.
3. Otherwise the call is a `ServiceFault`: the process stops with `FATAL in native NAME: service 'engine_004a90f0': 0x004a90f0 is
   neither a native nor a seam with an implementation`. `OT_RESOLVER_LENIENT=1` logs each such call once and answers 0 instead (a
   measurement mode: what follows the first gap is not the game). `OT_SPIKE_FRAMES=N` ends the run after N `Game_FrameTick` calls.

**Movies and music (5.3b, `standalone_movie.cpp`).** The eight `bink` seams (`StartBinkMovie`, `CreateBinkSurface`, `NextBinkFrame`,
`CloseBinkMovie`, `BinkMovieUpdate`, `SetupFullScreenMovie`, `DrawFullScreenMovie`; `PCMOVIE_StreamThread` is the music thread's entry, which
the `_beginthread` seam starts as a host thread), and the Bink imports the natives report by name (`win32_BinkWait`, `win32_BinkService`,
`win32_BinkPause`, `win32_BinkSetVolume`, `win32_BinkSetPan`, plus `win32_Sleep` and the `PeekMessage` family, which answer "nothing") are
implemented on the HAL's FFmpeg decoder, the HAL movie backend (`src/hal/movie.h`) and the HAL audio streams (`src/hal/audio.h`). `call_seam`
asks `call_movie_seam` first and the host asks `call_movie_service` before it resolves a name; the program that owns the HAL hands its backends
over with the export `ot_standalone_set_movie_hal(movie, audio)` (null backends decode and discard). The natives are untouched: only one
runtime line changed for them, `RetailServices::value` lets a live `win32_*` import be read more than once after its one call (the message
loop of `PCMovie_PlayStartupMedia` reads three scripted values of one `PeekMessageA`); every other service still answers once per call.
Test switches: `OPENTONY_MEDIA_DIR` (the install's media root: `med\Movies`, `data\Music`), `OPENTONY_FFMPEG_DIR` (the FFmpeg DLLs). Pacing is
the lockstep clock (HAL.md "Movie"). `opentony_movie_player.exe` runs the real natives over the seams into a recording HAL (`startup NAME...`,
`clip NAME FRAMES`, `music N...`); it sets `OT_RESOLVER_LENIENT=1`, because the pad read behind `Input_UpdateActionStates`
(`ReadControllers`, 0x00489cc0) and a rumble call are the input layer of 5.5 and not Bink: the two lines it logs are exactly those.
`opentony_movie_compare.exe` is the proof tool (`video`, `yuv`, `sound`, `audio` modes).

**Static data (8b, `tony/static_data.py`).** `tony hooks static-data [--exe PATH] [--out PATH]` writes `build/static/thawk2_static.img` (git-ignored) from the player's `THawk2.exe`: its non-code sections at their retail addresses, after refusing any file whose sha256, PE timestamp or `SizeOfImage` differ from `builds.canonical`. `opentony_spike.exe` accepts the image (magic `OTSD`) or the executable as its first argument; the Python drivers call `static_data.ensure_image()` and pass the image. The session image still goes over `.data` (milestone 9 replaces it).

**The rest of the seams (8c, `standalone_host.cpp`).** Every seam of `re/symbols/seams.yml` (100) is now a native or has an implementation: `OT_SEAMS_REPORT=1` makes
the attach log `seams: N of M answered` (and one `unanswered:` line per gap), `tests/test_standalone_seams.py` checks it (statically from the sources, and under Wine
from that report). What each answers, from the retail function's disassembly where a detail mattered:

- *C runtime:* `atexit`/`exit` (a list run in reverse before the process ends), the vector destructor iterator (last element first), `isspace`, `stat` (the 36-byte
  MSVC 6 `struct _stat`, which the UCRT's `_stat32` is byte for byte), the error print, and the package stream: `PKR2_OpenPackageStream` is `fopen`, so it, `fread`
  (`0x00502358`) and `fclose` (`0x00500e7b`) are a table of host `FILE`s whose numbers are not pointers. The package is found at the path the program built, else
  `OPENTONY_PKR`, else its name in the working directory. The names of the player's own files, a `*.SAV` or `*.PRK` or `TH2_OPT.CFG` in the working directory (also
  as the search pattern `*.SAV`), are answered from `OPENTONY_SAVES_DIR` and `OPENTONY_CONFIG_DIR` when set (`standalone_userdata.hpp`, used by every host file
  call: stat, find, remove, the package stream opened to write, the memory card's file backend); a file anywhere else is not touched.
- *zlib:* upstream **1.3.1** (`re/config/zlib.yml`, fetched and SHA-256-checked by `tony hooks build`, compiled statically; notice in `THIRD_PARTY_NOTICES.md`) in
  place of the retail zlib 1.1.2 (`0x004f9c80..0x004ffd39`). `zlib_compress` and `uncompress` (`0x004f9d50`) answer upstream's codes, which `CoreKit`'s wrappers
  turn into their messages; `pkr_asset.cpp` (the archive reader) is in the Kit library now.
- *WINMAIN:* `LoadSetting`/`SaveSetting` are a private-profile file (`OPENTONY_SETTINGS`, else `THawk2.ini` in the working directory); `FindFiles` is
  `FindFirstFile` with retail's slot rule (names starting with `!` and names of 31 characters or more are not kept; the answer is kept + 1); `EnumDisplayModes`
  lists one mode, 640 x 480 at 16 bits; `SwitchResolution` leaves the screen words at that size; `CD_Recheck` finds the disc; `PCinit` clears the file table,
  reads the working directory, builds `<OPENTONY_GAME_DIR>\all.pkr` and preloads the package. `WINMAIN_Sleep` moves the clock one tick, delivers the
  multimedia timer's due tick and answers `SimulationPublicTick`, which is what `begin_scene` and the frame limiters read back.
- *The D3D handlers:* the 14 `D3D_Draw*` functions, `D3D_SetViewportRect` and `D3D_ClearBuffers` are natives of the `DrawKit` (`draw_handler(kind, packet)`),
  reading the state the walk leaves in the retail words. `D3D_DrawPolygonList` still draws the same packets by its own walk. (8d: `tony.draw_oracle` calls each
  retail handler and its native on the same packets and compares every word they write, the D3DTLVERTEX staging buffers included; the three sized-rectangle handlers
  `0x004d6090`, `0x004d60c0` and `0x004d60f0` are reached only through the walk and are not natives.)
- *DirectPlay:* the empty answers of a single-player program (no message, no latency, no session record, a refused connection, every command accepted).
- *Tables:* 18 out-of-scope functions that only data tables hold (`re/config/table_seams.yml`): the net menu's mouse test and the SDX input classes' destructors are
  answered; zlib's deflate strategies and the C runtime's initializers and unwind funclets are internals of code the host replaces whole, and a call stops with the reason.
- *Mods:* the 5.2 overlay (`src/hooks/mod_overlay.cpp`, the hooked build's code) is in front of `FileIO_BackendOpen`'s lookup: the same folders, keys and
  `OPENTONY_MOD_TRACE`; `FileIO_Exists` goes through the same open.

`opentony_spike.exe` has a `hostcheck` token (`OT_SPIKE_CALL=hostcheck`) that calls the host seams with buffers of its own and prints `hostcheck: NAME ok|FAIL` lines.

**Window, input, sound and clock (8a, `standalone_platform.cpp`, `standalone_audio.cpp`, `standalone_video.cpp`).** The program that owns the HAL
(`opentony_spike_video.dll`, started when `OT_VIDEO_DIR`, `OT_VIDEO_SDL` or `OT_VIDEO_SHOW` is set) creates the SDL window, starts the HAL's input
and audio services and hands them over with `ot_standalone_set_platform(input, window, flags)` and `ot_standalone_set_audio_hal(audio)`.
`PCInput_Poll` and `ReadControllers` (one body) write the HAL's keyboard, mouse and gamepad where DirectInput would have (the key bytes at
`0x006a43e4` only while the window has the focus, the joystick record at `0x006a42e8`), then run the game's own `PCInput_BuildActionMask`;
`SDXKeyboard_KeyIsDown` is the 40-byte chord test, transcribed; `win32_GetCursorPos` answers the HAL's pointer scaled to the 640 x 480 display
(the display centre until the pointer has been used, which is where it is in the retail sessions). `Game_FrameTick` pumps the window: closing it
ends the program. The `Audio_*` functions of the sound front (`Audio_PlaySound` ... `Audio_ResumeLooping`, `Audio_FreeSoundBank`, and
`Audio_Init` and `Audio_LoadSoundBank` at their retail addresses) are transcribed from the disassembly on the HAL's audio service; a slot's
pointers in a session image belong to a heap this program does not have, so those slots are silent until the bank is loaded here. Switches:

| Variable | Effect |
|---|---|
| `OT_VIDEO_SDL=<dir>\` | render with SDL3 from that directory (window, GL video, input, audio) |
| `OT_VIDEO_SHOW=1` | the window is shown, and the clock follows the wall clock |
| `OT_FRAME_RATE=60` | levels at 60 fps: `PcSystem::begin_scene` waits one tick, not two (`runtime/frame_pacing.hpp`); in realtime the tick words follow the wall clock exactly (`deliver_tick`). Unset or 30: retail's |
| `OT_FRAME_RATE=120` | levels at 120 fps on the sub-tick clock (`Pacing::sub_tick`): a scene is half a tick, so `begin_scene` waits on the frame accumulator (the double at `0x006a0590`) and `animation_clock_update` takes the frame length from the game accumulator (`0x006a0598`): scale `0x40`, step `0x400` on every frame. Lockstep delivers half a tick a delivery, and the first delivery past a frame's period adds no time (`framepacing::lockstep_holds`). In the launcher (F2D); realtime `deliver_tick` waits for the limiter's deadline (`framepacing::scene_remaining`: a high-resolution waitable timer to 1 ms before it, then yield) instead of `Sleep(1)`, and moves the accumulators to the deadline so the scene is stamped on it. `OT_SUB_TICK=1` puts 60 on the same clock (a measurement switch); at 120 a small value times the frame scale keeps its fraction for the next frame (F2B; `OT_CARRY=0` switches that off, a measurement switch) |
| `OT_PACING=lockstep\|realtime` | the clock: 1/60 s a `Game_FrameTick` (the default, the same on every machine) or the wall clock (the default with `OT_VIDEO_SHOW`: the multimedia timer's ticks are delivered as they fall due) |
| `OT_AUDIO=off\|dummy` | off: a HAL audio service that makes no sound; dummy: SDL's dummy driver (Wine does not pass `SDL_AUDIO_DRIVER` on) |
| `OT_INPUT_LOG=<file>` | scripted input, one event a line: `<poll> key down\|up <hex scan code>`, `<poll> mouse <x> <y> [buttons]`, `<poll> pad down\|up <button>` (SDL's numbers, 0 south .. 14 d-pad right), `<poll> pad axis <n> <value>` (poll 0 is the first `PCInput_Poll`; display coordinates). `scripts/input_log_to_script.py RUN/input.txt -o x.txt` turns a player's play log into one (the log's lines carry `poll N` since 2026-11-16) |
| `OT_INPUT_TRACE=1` | log each poll that changed the held keys, the action masks or the pad (`pad buttons <hex> axes lx ly rx ry lt rt joystick x y`; a scripted pad needs no controller, its values hold until changed); with `OT_FREELOOK`, also `input poll N: free look yaw Y pitch P step S` (S: the camera's quick steering step, 0 the game's own) at each poll where player one's look changed |
| `OT_FREELOOK=spring\|stay` | the free look (camera plan C4; `runtime/free_look.hpp`, `standalone_freelook.cpp`): the look stick turns player one's in-level camera about the skater and it springs back when let go (`stay` acts as spring until C9). Reach: a third of a turn each side, tilt up a sixteenth of a turn, down an eighth (`kYawMax`, `kPitchUpMax`, `kPitchDownMax`); radial dead zone 8000, squared curve. Pad only, one stick (`OT_FREELOOK_STICK=left\|right`, default right: the one that does not move the skater with the default "Movement stick: Left"). When (C5): player one's camera while it follows its skater in the follow modes (1, 2, 0x19); in the end-of-run swing (`0x00568658` == 1), a replay (`0x0056e5d8` == 2), the point and death cameras the stick is ignored and the look eases home; a camera built or reset behind the skater starts home; the look steps only on an update that followed an input poll (the level start's warm-up updates have none). The look is visual only: the state 1 air lean (`skater_action_step.cpp`) asks where the game's own camera would be (`freelook::stock_offset`), so up/down in the air means what it means without the look. Unset: the game's camera, byte for byte; `run.txt` prints `options: free look spring, right stick`. `OT_FREELOOK_SENS` (1 to 10, default 5) and `OT_FREELOOK_INVERT` (`x`, `y`, `xy`) set the sensitivity and the inversion; the launcher sets all four (Gamepad page, "Camera stick") |
| `OT_SPEED_RUN_TEST=ticks` | a test switch of the quick start's Goals Speed Run (`standalone_challenge.cpp`): at that much game time (60 a second) the run's goals are given, which a headless run cannot get, so the finish, the score line and the run's end can be checked; it also has the run pass its goals screen by itself (a speed run otherwise waits there for Play). Unset: nothing |
| `OT_COMPETITION_TEST=score` | a test switch of the quick start's Competition Session (`standalone_challenge.cpp`): the trick score is held at that value while a heat is played, so the judges, the medal and the score line can be checked headless. Unset: nothing |
| `OT_MUSIC_NAME_TEST=band\|title` | a test switch of the song's name (`standalone_music.cpp`): that name comes up at each level's start as a song's would (the headless runs play no song), so its place on a wide picture can be checked (`OT_HUD_ANCHOR`, `Mess::display`'s `hud_messages`). Unset: nothing |
| `OT_FREELOOK_FIXED=yaw,pitch[,distance]` | a test switch of the free look (`runtime/free_look.hpp`, `standalone_freelook.cpp`): the in-level camera held turned by these offsets, for both players (C numbers: `0x400,0`, `-0x555,0x100`). 12-bit angles: +yaw puts the camera on the skater's right looking left, +pitch raises it and looks down; the distance is added where the game's distance `0x00524a98` enters the camera's running average. The offsets are added where the camera code reads its spin (`+0x5b4`) and pitch (`0x00524a94`); nothing is written to guest memory. Unset: the game's camera, byte for byte. `opentony_free_look_camera_test` checks each offset against the game's own word holding it, on the camera fixtures' retail states |
| `OT_AUDIO_LOG=1` | log every sound call and the HAL calls it makes |
| `OT_TEXTURE_DUMP=<dir>` | the texture dump (`standalone_texture_dump.cpp`, called from the recording layer of `standalone_video.cpp`; needs no window, starts the video DLL by itself): every texture the game uploads for a draw, and after the surface store has held the same number of surfaces for `OT_TEXTURE_DUMP_WALK` presents (120; 0 never) every other texture surface too (the camera has not seen them yet), as `<key>.png` (RGBA; the key is `src/hal/texture_key.hpp`'s FNV-1a over size, format and the rows `HalTextureCache` sends) and `index.tsv` (key, size, format, alpha class, use world/2d/both/unknown from the first 64 draws of each key, wrap, mod2x, volatile, first tag); `seen.tsv` (tag, key, draw/walk) and `volatile.tsv` are appended. `OT_TEXTURE_DUMP_TAG=<text>` names the run (default `run`). Runs share one folder one after another, never at once. Unset, the video layer does nothing extra. The full census is `scripts/texture_census.py OUT` over the recipe `re/config/texture_census.yml` (written by `scripts/make_texture_census.py`: every level in both texture modes, every skater and look, the menus and screens; runs side by side on isolated Wine workers, one dump folder a run, merged with `runs.tsv`), and `scripts/texture_classes.py OUT` gives every key a class (`re/config/texture_classes.tsv`: `world` or `2d`, keys only) (plan: `docs/post_phase_5/textureupscale.md`, T1 and T3) |
| `OT_TEXTURE_PACK=<dir>` | the texture pack (up to four folders joined by a vertical bar; the first that holds a key supplies the picture), in the GL backend (`sdl_video_gl.cpp`, option `texture_pack`; needs the window path or `OT_VIDEO_SDL`, the recorded-only mode ignores it): the folder's `<key>.png` names are read once at the first upload; each whole-texture upload of an indexed-free texture up to 512 x 512 is hashed (`src/hal/texture_key_hash.h`, the dump's key) and on a hit the PNG is level 0 of the GL texture, larger than the game's own. The texture keeps its logical size (a sprite's source rectangle, the 2D layer), its cut-out class is the original's (alpha-to-coverage and the cut-out mip chain still apply to a smooth upscaled alpha), the mip chain is built from the picture. A miss, an unreadable file, a picture smaller than the texture, of another shape or over 1024 on a side draws the original (counted as "not used" in the `video: texture pack:` line that run.txt gets once the numbers have been still for 20 frames: files, textures hashed, replaced, drawn from the pack now, MB on the GPU). A texture that later takes other texels goes back to its own size or gets its new picture. Mip maps (`OT_ANISOTROPY`) should be on: without them a 4x picture on a distant wall shimmers. A hand-made pack: `scripts/texture_test_pack.py DUMP OUT [--tag L12] [--classes re/config/texture_classes.tsv]` (4x nearest with a visible mark: cyan for world textures; with the class table also the 2D ones, in magenta). Unset, nothing is hashed or read. The stats line is printed in played runs too. `OT_TEXTURE_PACK_LOG=1` adds `video: pack use: <key> <w>x<h> replaced|missed 2d|world` the first time each hashed texture is drawn in 2D and in the world: a play-time census of which textures the pack covers and how they are really used |

`OT_SPIKE_CALL` takes arguments (`0x004f2e20(144,100,100)`) and `poke:<hex address>=<value>`; `tests/test_standalone_platform.py` and
`python -m tony.standalone_frame --input` are the checks.

**Function-level oracles (7b, 8d).** A script in the language of `src/hooks/oracle_script.h` (`alloc`, `bytes`, `pattern`, `call`, `tick`, `mem`, `val`, `same`,
`maskval`, `flush`, ...) runs twice and prints the same canonical text: in the hooked retail process with hooks off (`OPENTONY_SESSION_SCRIPT` at delivery 1500 of the
hangar session; the real functions on the game's own memory and DirectDraw surfaces) and in `opentony_spike.exe` (`OT_ORACLE_SCRIPT`; the natives). Heap pointers print as
`P` or are checked with `same`; every other byte is compared. Five oracles use it:

| Module | Checks | Run |
|---|---|---|
| `tony.texture_oracle` | the texture and sprite layer: bitmaps into records, the device rules, tiles, grey, sprites, `DestroyRecord`, a 5-6-5 device, the software renderer, **8d:** named 8-bit and 24-bit files served by the loose-file overlay (`OPENTONY_MODS`), `UploadTexture` with `replace` (also keyed, also under the software renderer) | `python -m tony.texture_oracle` |
| `tony.psx_env_oracle` | `PSX_ParseEnvironment` on random regions, **8d:** texture records (`PSX_LoadTextureReferences` and `D3DTex_PrepareTexture`: the material's page and rectangle, the palette cache record, the texture's pixels), the palette cheat 0xd, the zone blockmap tag (10), the named-resource tag (0x45) with `BITS_FindNamedResource` | `python -m tony.psx_env_oracle` |
| `tony.native_oracle` (8d) | `PRE_Unload`, `FileIO_Exists`, `Animation_Cycle`, one delivery of `SimulationTimeTimerCallback` (`tick`: the retail process owns that function through the lockstep clock) | `python -m tony.native_oracle` |
| `tony.draw_oracle` (8d) | `D3DPoly_StartScene`, `Db_ClearOrderingTable`, `D3D_DrawPolygonList` on one-packet lists (the 12 PSX polygon and line kinds, four tile commands, general polygons, the 0xE1 and 0xE3 packets, unknown commands), each `D3D_Draw*` handler called alone through `D3DPrimitiveDispatchTable`, `D3D_PresentFrame`: the scene words, the device-state caches, the counters, the texture bindings and both D3DTLVERTEX staging buffers | `python -m tony.draw_oracle` |

Each takes about three minutes and fails when one line differs. Each module's `OUT_ROOT` (`build/oracle/<name>/`) keeps `retail.out` and `standalone.out`; a run that crashes
the retail process still has what the script `flush`ed before it (a `crash:` line is appended). The environment of a script is not the game's: a session image carries the
palette cache list's head and tail but not the C runtime heap its records are in, so the PSX oracle starts both processes from an empty list (and an empty source lookup
with every palette slot free); the draw oracle sets the fog threshold's inputs. `OPENTONY_WINE_TESTS=1 pytest tests/test_*_oracle.py` runs them as tests.

Named services (`font_load`, `file_open`, `sprite_construct`) are found by name in `services.live_calls` and then in `kServices` (the
probes' stubs that name one function entry). A virtual call the Kits report under a **name** (`element_update`, `element_draw`,
`virtual_delete`, `font_draw`, `sprite_draw`) is resolved through its object's vtable slot (see "Resolver additions" below); a class the
menu toolkit does not route becomes `vcall_<vtable[slot]>`.

**`re/config/standalone_natives.yml`** is written by `scripts/standalone_rows.py` from the adapter tables of
`src/runtime/*_retail_calls_test.cpp`: the expression a replay test calls a native with is the expression a hook row holds, with the test's
context `c.` in place of the Kit. The tool maps each context member to its Kit (`KITS` in the script; `ScreensKit`,
`src/runtime/screens_kit.hpp`, holds every screen the tests build, **without** their dispatch lambdas or loop seams: a virtual call
resolves through the vtable and the address table), checks its reading against the 1,226 hook rows that exist (it derives the same
`kit`/`call` for each of them), and takes the convention from the function's `ret` (`unchecked:` rows are tail jumps whose `ret` is
not in the function; `tony hooks verify` checks every other row). A row has `call` (a member call on the Kit) or `expr` (a whole
expression naming `kit.`: `!kit.options.controls_selectable(self)`), and kit `none` for a native that returns a constant. A row is
never patched into the retail executable.

The natives without an adapter are the ones whose model is not the retail memory model (a value-owned C++ type or a typed record):
`tests/test_standalone_natives.py` lists their source groups and fails if one appears that is not listed.

**Cold boot (milestone 9, `standalone_boot.cpp`, `standalone_session.cpp`).** `OT_SPIKE_CALL=boot` with no session image (`-`) starts the game from
the static data alone: the C++ dynamic initializers of the table `0x00524000..0x00524118` in order (a native through the resolver; a `jmp` stub's body
as `scripts/static_init_transcribe.py` transcribed it into `standalone_static_init_generated.cpp`; the `call CONSTRUCTOR; jmp REGISTER` stubs, the class
constructor on its global and the destructor to `atexit`), then `Game_Startup`'s work in its order (its calls into game code, and for its DirectDraw,
Direct3D, DirectInput and winmm calls what they leave in the game's memory: the device the recordings ran on presents itself, its texture formats,
caps and timer answers written as that device answered), then `main`. The C runtime's own initializers are the host's. During a cold boot
`operator new(0x5dc000)`, the zones of `Init_AtStart`, is placed where retail's are (`0x04f00020`).

The recorder is the session probe for the standalone, with the probe's variables (`OPENTONY_SESSION_TRACE`, `_EVERY`, `_KEYS`, `_SKIP_MOVIES`,
`_DUMP`, `_MASK`, `_NORM`; output in `OPENTONY_LOG_DIR`) and the same `session.trace` (the block hash is `src/hooks/session_hash.h`, shared with the
probe): a delivery is one run of the timer callback by the clock of `standalone_seams.cpp`, a poll one keyboard device update, the keys written after
it. A dump also writes the zone block as `session_N.heap`, as the probe's does. `python -m tony.standalone_boot [SESSION]` records the scenario in
retail (hooks off) and in the standalone, from retail's working directory, with `STANDALONE_MASKS` added to both (each class with its reason in the
module), and compares the traces block by block (`tests/test_standalone_boot.py`).

Resolver additions: a jump thunk is followed to its target (`standalone_thunks_generated.hpp`, `scripts/standalone_thunks.py`); a cdecl native
reported with more arguments than it takes gets its own (the caller pops the rest); a stdcall native reported with one more argument than it takes is a
method that ignores `this`; the window base's element services (`virtual_delete`, `element_update`, `element_draw`, `element_snap_x`/`_y`,
`element_hook`) are vtable slots 0, 1, 2, 5, 6, 7 of their object; `OT_RESOLVER_WATCH=<hex>,...` logs every call to those addresses with its
arguments and the native that made it (lines `kit: watch: ...` in the run's `run.txt`); `OT_DELIVERY_LOG=<first>:<last>` logs each clock delivery in that range with its
site (`Game_FrameTick` / `WINMAIN_Sleep`), the native that reached it, the input polls and the simulation tick, and the player's skater position, state and vertical speed;
`OT_DELIVERY_WORDS=<hex offset>,...` adds those words of the skater to each line (` w <offset>=<value>`: the score, the trick button, the animation id), for comparing a frame
rate with another at equal ticks (`build/fr/trick.py`, F1.5). `OT_DELIVERY_GWORDS=<hex address>,...` does the same for absolute guest words (` g <address>=<value>`, e.g. the camera object).

## Adding a hook

1. Pick a function whose semantics are pinned down (ideally a byte-matching
   `cpp` module in `match/manifest.yml`), with a curated name.
2. Implement the arithmetic portably under `src/runtime/` with a Linux test;
   reproduce wrap-around, shift masking, aliasing and full-register returns.
3. Add an ABI adapter plus `OT_REGISTER_NATIVE` under `src/hooks/native/`.
   `thiscall` with *n* stack arguments is `__fastcall(self, void *edx, ...)`.
4. Add a `re/native/functions.yml` entry (`tested`) and a manifest entry with
   both builds' addresses and the exact prologue; `tony hooks generate`.
5. If the ABI shape is new, teach `src/hooks/selftest.cpp` to generate inputs.
6. Run the gates above; promote to `trace-validated` (native by default) only
   when parity shows the hook exercised (`native calls per hook`) and matching.
