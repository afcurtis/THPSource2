# Platform HAL

The platform HAL is the seam between the game and the machine it runs on. The
retail executable talks to Win32, DirectDraw/Direct3D 7, DirectInput 7,
DirectSound, winmm and Bink directly. THPSource2 moves that traffic, service by
service, behind small portable interfaces in `src/hal/`, with SDL3 as the first
backend. The same interfaces are the target of the eventual standalone (64-bit,
non-Windows) build.

- **What the boundary is:** `re/config/hal_boundary.yml` (curated hook points
  and the generated inventory of every platform call site, for both builds);
  `re/notes/platform-boundary.md` is the report. `tony hal check` (part of
  `tony verify`) keeps it closed: every platform call belongs to a hook point.
- **What sits behind it:** this document.

## Layers

```
 retail game code ──calls──▶ hook point (retail function in hal_boundary.yml)
                                  │  replaced by
                                  ▼
                   adapter  src/hooks/hal/      knows retail structs and globals,
                                  │             reproduces retail logic exactly
                                  ▼
                   service  src/hal/*.h         portable C interface
                                  │
                                  ▼
                   backend  src/hal/sdl/        SDL3 (loaded from SDL3.dll)
```

- **Service headers** (`src/hal/*.h`) are portable C with fixed-width integers
  only. They include nothing but `<stdint.h>` and each other; no SDL, Windows
  or Direct3D type appears in them. `opentony_hal_headers_test` compiles them
  as strict C99 and pins the layouts that are contracts;
  `tests/test_hal.py::test_hal_headers_are_platform_free` rejects platform
  tokens.
- **Backends** implement a service as a struct of function pointers
  (`OtHalInputBackend`, ...). Everything platform-specific stays inside the
  backend.
- **Adapters** exist only in the hook DLL. They replace a retail hook point,
  keep the retail data structures up to date and call the service. Where
  retail logic decides game behaviour (key bindings, audio volume arithmetic,
  the ordering-table walk), the adapter reproduces it exactly or calls the
  retail function in place. The HAL never reinterprets game rules.
- **Portable helpers** that need no platform (the clock pacer) live in
  `src/hal/` beside the headers and build on the host as well
  (`opentony_hal` in the root CMakeLists).

## Selection and lifetime

Each service is chosen once, at start-up, and never changes while the game
runs:

```ini
[hal]
clock = retail      ; retail | sdl
window = retail
input = retail
video = retail
audio = retail
movie = retail
sdl_probe = off     ; start/stop SDL even with every service retail
```

`OPENTONY_HAL` overrides the file for one run: `input=sdl,clock=sdl`,
`all=sdl`, `none`, `probe`. F9 flips only per-function leaf hooks
(`native_hooks.yml` `hooks:`); it never touches a HAL service, because device
state cannot move between two implementations in the middle of a frame.

`retail` for a service means its hook points are left alone: the retail code
runs unmodified. `sdl` installs that service's adapters. SDL3.dll is loaded
(from the DLL's directory) only when some service selects SDL or the probe is
on.

The HAL starts on the game thread at the `Game_Startup` entry site
(`services.hal.entry_sites` in `re/config/native_hooks.yml`), never inside
`DllMain`, and registers its shutdown with the game's own C runtime `_onexit`
so it stops on the game thread however the game exits (retail `D3D_Shutdown`
calls `exit()` when a Release leaves references behind).

## Conventions

- Every function pointer and exported entry point uses `OT_HAL_CALL` (cdecl on
  32-bit x86).
- Objects are `OtHalHandle` ids, 32-bit, `0` invalid. They are never pointers,
  so recordings and oracle logs match between 32- and 64-bit builds.
- All calls come from the game thread, except audio streams, which the
  producer (the Bink music thread) may feed from its own thread.
- **Determinism.** A HAL service may change *when* things happen in wall-clock
  time, never *what* the simulation sees. With no physical input, SDL input
  must produce the same raw device state as retail; clock pacing must never add
  or drop a tick. `tony hooks parity` is the gate for that.

## Clock (`clock.h`)

**Retail.** `D3DTimer_Init` starts a 16 ms `timeSetEvent` whose callback
(`SimulationTimeTimerCallback`, `TimerState`) advances
`SimulationPublicTick` on a winmm thread; every frame limiter spins
(`Game_FrameTick`, `WINMAIN_Sleep`, two call-free loops) until the tick has
moved twice. The frame count is therefore a race between two threads.

**HAL.** The HAL clock is the lockstep scheduler that Phase 2 introduced
(`services.lockstep_clock`): the timer callback body runs on the game thread,
once per delivery site, so every frame sees exactly two ticks and a run is a
pure function of its inputs. The backend only provides monotonic time and
sleep (`OtHalClockBackend`), used by the portable pacer
(`ot_hal_clock_pacer_*`) to hold each delivery until it is due in wall-clock
time. Pacing is `realtime` for play and `unpaced` for headless verification
(`OPENTONY_CLOCK=lockstep-fast`); the two are bit-identical.

`clock=sdl` = lockstep deliveries + SDL time (`SDL_GetTicksNS`,
`SDL_DelayPrecise`) + no winmm timer at all (`D3DTimer_Init` /
`D3DTimer_Shutdown` are replaced; `TimerState` keeps its retail meaning so the
deliveries still see a running timer).

In the hook DLL (`src/hooks/hal/hal_clock.cpp`):

- `clock=sdl` implies the lockstep scheduler; with `OPENTONY_CLOCK` unset or
  `realtime` it runs paced, with `lockstep-fast` unpaced.
- `D3DTimer_Init` fills `TimerState` and `TimerCaps` as retail would (period
  16, interval 16, the callback address) with a timer id that names no OS
  timer, computes `TimerTickFrames` with the same x87 sequence as retail
  (`fild; fmul 60.0; fmul 0.001; fstp` under the game thread's control word),
  and registers `D3DTimer_Shutdown` with the game's `_onexit`. There is no
  `timeBeginPeriod`: the SDL clock sleeps precisely by itself.
- `WINMAIN_Sleep` was a lockstep delivery site; the adapter delivers, then
  sleeps on the clock backend. The lockstep installer leaves that site to the
  HAL (`ot_clock_install(..., skip)`), so it is patched once.
- The clock adapters are installed in `DllMain` like the lockstep clock itself
  (they need no SDL); pacing starts on Win32 time and switches to SDL time when
  SDL starts. If SDL3.dll is missing the game still runs, paced by Win32.

## Window (`window.h`)

**Retail.** `Game_Startup` registers `WINMAIN_WndProc`, creates and shows the
window; `Game_FrameTick` is the message pump.

**HAL.** A backend can create the window or adopt one. The hook DLL adopts
the game's window (DirectDraw needs the HWND it was given), so SDL sees the
same messages before `WINMAIN_WndProc` does; the game's own pump keeps
running. With `video=sdl` the adopted window gets an OpenGL pixel format and
the video service presents on it. `pump` turns OS events into the small
`OtHalWindowEvent` set, and processes input device events. The standalone
build creates the window instead, and the adapter feeds those events to the
game's activation/close logic.

In the hook DLL (3.3) the window service adopts `MainWindow` at the `window`
entry site (the start of `WINMAIN_InitDirectDraw`, after `CreateWindowExA` and
before DirectDraw subclasses the window). SDL never runs a message loop of its
own (`SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP=0`): retail `Game_FrameTick` stays
the only pump, so message order, the dropped `WM_SYSKEYDOWN`/`WM_SYSKEYUP` and
`WM_QUIT` handling are exactly retail. Game_FrameTick reads only `MainWindow`'s
messages, so the backend drains SDL's own message-only helper windows itself.
`input=sdl` turns `window=sdl` on (SDL sees keys only through a window it
knows).

## Input (`input.h`)

**Retail.** `PCInput_Poll` (reached only through `ReadControllers` from the
per-frame controller read at 0x00489a10, and from multiplayer) reads the SDX
DirectInput wrappers into raw device state, then `PCInput_BuildActionMask`
applies the player's bindings and writes the action bank `ActionMask`: one
PlayStation pad word per player. After it come the screenshot key (F9) and an
Alt-F4 log line.

| Retail state            | Canonical  | Meaning                                    |
|-------------------------|------------|--------------------------------------------|
| SDX keyboard            | 0x006a43e0 | `+4` 256 key bytes (set-1 scan codes), `+0x108` device |
| SDX mouse               | 0x006a3f20 | position, deltas, 3-button click state machine |
| SDX joystick            | 0x006a42e8 | `+4` DIJOYSTATE (lX, lY, ... buttons at `+0x34`), `+0x6c` 32 button flags |
| `ActionMask`            | 0x006a3f1c | pad word per player (`OtHalPadButton`)     |

**HAL.** Two layers, split where retail splits them:

1. *Devices* (`OtHalInputBackend::poll` → `OtHalInputState`): keyboard in
   set-1 scan codes (the numbering the retail binding tables and saved
   settings use), mouse, up to two gamepads in the standard layout.
2. *Actions*: the retail bindings stay. The adapter writes the device state
   into the SDX structures exactly as DirectInput would have, then calls the
   retail `PCInput_BuildActionMask`.

Recorded or scripted input enters at the action layer: an **action source**
(`ot_hal_input_set_action_source`, exported by opentony.dll) runs once per
poll right after the bindings, where the capture recorder injects today
(0x00489a15, the instruction after the poll). It returns 1 only when HAL input
is selected, so a caller that gets 0 keeps its own injection.

In the hook DLL (`src/hooks/hal/hal_input.cpp`) the adapters replace
`PCInput_Initialize`, `PCInput_Poll`, `PCInput_Shutdown`,
`PCInput_ActuatorOn/Off` and `PCInput_SetCursorPos`; DirectInput is never
created. They keep the SDX records as the DirectInput wrappers would:

- *Initialize* runs the retail game logic in place
  (`Options_ResetControllerDefaults(0/1)`, `PCInput_ResetBindings`,
  `PCInput_LoadBindings`), links the wrapper records, marks the keyboard
  acquired, sets the joystick flags from the first gamepad and runs
  `SDXMouse_SetBounds(0, 0, 608, 448)` with sensitivity 0x80. No
  `SetCapture`.
- *Poll* copies the 256 key bytes only while the window has keyboard focus
  (exclusive/foreground DirectInput leaves them untouched otherwise), fills
  DIJOYSTATE from gamepad 0 (left stick and d-pad on lX/lY in -1000..1000;
  buttons in DirectInput order A, B, X, Y, LB, RB, Back, Start, LS, RS, then
  LT, RT), calls retail `PCInput_BuildActionMask`, runs the action source, and
  handles the F9 screenshot key. The SDX mouse is left alone: retail never
  acquires it (its acquire is reachable only from its own update).
- Rumble (`ActuatorOn/Off`) and cursor warps go to the input backend.

The capture recorder registers its action source when it finds opentony.dll;
its 0x00489a15 detour stays installed as a fallback and stands down for every
poll the HAL served, so injection happens at exactly one layer.

The pad word names controller positions (`OT_PAD_CROSS`), not tricks; which
button ollies is the game's configuration.

## Verification of the hook-DLL services

`tony hooks parity <scenarios> --hal input=sdl,clock=sdl` captures each
scenario with every hook and service retail (the reference), then twice with
the selection and once more with every native hook on and the master switch
flipping. A variant passes when its recording equals the reference in the
`portable` scope: every recorded field (per-frame input, timer boundaries,
causal events, both player snapshots) exactly, except the player object's heap
address. That address is process identity, not game state: with `clock=sdl`
alone it moves 1 MB down (the winmm timer thread and its stack are gone), with
`window=sdl` 3 MB up (SDL video initialization maps memory before the game
allocates its 6 MB `PCMemBuffer`). Parity prints the address difference when
there is one, so it is never hidden.

## Video (`video.h`)

**Retail.** The game builds a PlayStation-style ordering table each frame:
libgpu packets (`POLY_F3/FT3/G3/GT3/F4/FT4/G4/GT4`, `LINE_*`, `TILE*`,
`DR_TPAGE` 0xE1 for the blend mode, `DR_AREA` 0xE3 for the viewport) plus a
PC-only pre-transformed polygon (command 0xB0). `D3D_DrawPolygonList` walks the
table and dispatches each packet through `D3DPrimitiveDispatchTable`
(0x0065f82c) to Direct3D 7 `DrawPrimitive` calls on `D3DTLVERTEX` vertices.
Textures are 16-bit surfaces built from 4/8-bit PSX images and palettes;
sprites (`D3DSprite_*`) are tiled images drawn as quads.

**HAL.** The HAL never sees the ordering table. The adapter walks it and
flattens each packet into `draw(state, topology, vertices)` on pre-transformed
`OtHalVertex` (the `D3DTLVERTEX` layout, pinned by the header test) with an
explicit `OtHalDrawState`: texture, PSX blend mode, depth/alpha/filter/fog
flags. That sequence of draw calls is the **primitive stream**. Textures,
palettes and sprites go through the same backend; `present` ends the frame;
`read_back` returns the frame for screenshots and the image oracle.
The GL backend also has `ot_hal_sdl_video_gl_capture_shown` (`sdl_video.h`): with the CRT scaling, the curved screen or the dither on, it runs the present's pass
over the frame once more without a swap and reads the picture's rectangle at the window's resolution, so the standalone's F8 screenshot carries the look;
`read_back` stays the render target, which the oracles and the recorders compare.

Verification (3.4): a *primitive-stream oracle* records what the retail
dispatch-table functions submit and what the adapter submits for the same
lockstep frame, and compares them call by call; an *image oracle* compares
retail and HAL frames with a tolerance, plus screenshots. SDL_GPU is the
preferred backend, OpenGL 3.3 the fallback.

### What exists (3.4, first part)

- **Retail primitive stream.** `OPENTONY_VIDEO_TRACE=<first>:<count>`
  patches the game's `IDirect3DDevice7` vtable (at the `video` entry site,
  `D3D_ClearBuffers`) and records every scene bracket, clear, viewport,
  render and texture-stage state, texture bind and draw with its vertices for
  those frames, plus the device state at the start of each frame, the render
  target after `EndScene` and every distinct texture once, by content hash
  (`src/hooks/hal/video_trace.cpp`, format in `tony/hal/video.py`).
  `tony hal video summary` prints what a frame uses; `tony hal video png`
  writes screenshots.
- **What retail submits** (Warehouse gameplay): about 1,050 draws a frame,
  every one a triangle fan of pre-transformed vertices, textured
  (`XYZRHW|DIFFUSE|TEX1`) or not (`XYZRHW|DIFFUSE`); no fog, no alpha test,
  no dithering, `D3DRS_TEXTUREPERSPECTIVE` off; **reversed depth** (cleared to
  0, `D3DCMP_GREATEREQUAL`); blending as Direct3D factor pairs
  (SRCALPHA/INVSRCALPHA, ONE/ONE, ...), which is why `OtHalDrawState` carries
  factors and an op, with `ot_hal_blend_preset` for the PlayStation modes;
  wrap/clamp switched per draw through `D3DTSS_ADDRESS`.
- **SDL video backend** (`src/hal/sdl/sdl_video_gl.cpp`): OpenGL 3.3 core
  through SDL, rendering into an offscreen framebuffer (present blits it to
  the window). It honours the Direct3D conventions the stream needs: pixel
  centres on integers, w = 1/rhw, first-vertex flat shading, reversed depth.
  SDL_GPU needs offline-compiled shaders, so the stated GL 3.3 fallback came
  first.
- **Image oracle.** `src/hal/oracle/d3d7_stream.cpp` replays a recorded frame
  onto any `OtHalVideoBackend` (host-tested with a recording backend);
  `opentony_video_replay.exe` does it with the SDL backend under Wine;
  `tony hal video replay` runs it for every traced frame and compares with
  retail's own frame at the 5-6-5 precision of its target (pass: PSNR >= 30 dB
  and >= 95% of pixels within 16 levels), writing `.replay.png` and
  `.diff.png`. Warehouse frames 150-152: **46 dB, 99.93% within tolerance**;
  the rest are edge, filtering and translucency rounding.
- **Ordering-table adapter, shadow mode**
  (`src/hooks/hal/video_shadow.cpp`). While a frame is traced, every ordering
  table `D3D_DrawPolygonList` is about to draw (a `draw_list` entry site,
  kind `before_arg`) is first flattened into HAL draws, reproducing the
  hardware path of the 17 `D3D_Draw*` functions: PC general polygons (0xB0,
  pre-transformed vertices with the level, depth-cue and blend colour rules),
  every PSX polygon, line and tile packet, texture pages and viewports. It
  writes `frame_NNNNN.otvh`; `tony hal video shadow` replays the retail stream
  (`opentony_video_shadow_compare`) and compares draw by draw, state and every
  vertex bit for bit. **All 1,306 traced frames of the five Warehouse
  scenarios (frontend and gameplay) are identical.**
- **Live HAL mirror** (`src/hooks/hal/video_mirror.cpp`, test switch
  `OPENTONY_VIDEO_MIRROR=<N>`, needs window=retail). Retail keeps rendering;
  the adapter draws every frame through the SDL backend into a second
  window, with scene brackets, clears and viewports from the Direct3D device
  hooks and textures copied from the game's surfaces (a `texture` entry site
  on `D3DTex_UploadTexture` and `D3DTex_DestroyRecord` marks a surface stale
  when the game refills or releases it). Every Nth frame the mirror frame is
  compared with retail's render target and logged. On the five Warehouse
  scenarios (1,237 frames, about 1.4 million draws, 59 comparisons) the worst
  frame is **40 dB**, each above 99.9% within tolerance. Headless runs need a
  24-bit X server for OpenGL (`OPENTONY_XVFB_DEPTH=24`).
- **`video=sdl`** (`src/hooks/hal/video_hal.cpp`; implies `window=sdl`). The
  game draws through the HAL; Direct3D is never created.
  - *DirectDraw* stays only as the store of the texture and sprite surfaces:
    `WINMAIN_InitDirectDraw` is replaced by one that creates DirectDraw at the
    normal cooperative level (no display mode, no flipping chain), makes one
    offscreen system-memory surface the primary and back buffer the rest of
    the game expects, and starts the SDL GL backend on the adopted window.
    The retail texture code (`D3DTex_*`, `D3DSprite_*`) runs unchanged; the
    renderer (`video_renderer.cpp`, shared with the mirror) copies each
    surface into a HAL texture on first use and again after the `texture`
    site reports a refill or release.
  - *D3DDevice* is a stand-in `IDirect3DDevice7` (`WINMAIN_InitDirect3D` is
    replaced): scene brackets, clears, viewports, render and texture-stage
    states, texture binds and every draw retail makes on it are translated
    to the HAL with the image oracle's Direct3D 7 rules, and
    `EnumTextureFormats` offers the 16-bit formats a Direct3D 7 device does.
    Texture caps are the ones retail's device reports under Wine
    (power-of-two, 256 maximum).
  - *Ordering tables*: `D3D_DrawPolygonList` is replaced by the adapter in
    `OT_VIDEO_SHADOW_SOLE` mode, which also applies the viewports, writes the
    wrapped viewport top back into the packet and leaves `TexpageBlendMode`
    as retail's walk would, inside retail's `_controlfp` single-precision
    bracket.
  - *Present*: `D3D_PresentFrame` runs `D3DPOLY_EndScene` and presents on the
    game window, scaled with the 4:3 aspect kept. `DrawFullScreenMovie`
    (startup movies while `D3DTextureMode` is 0) draws its quad through the
    stand-in and flips the primary: that `Flip` (the surface vtable slot is
    patched for this one surface) presents instead of failing.
  - *Mode switches*: `WINMAIN_SwitchResolution` does what retail does (mode
    list, `atoi` of the cut mode name, `D3D_Shutdown(0)`, `Game_FrameTick`,
    new back buffer, `D3D_ClearBuffers`, device) without setting a display
    mode; `WINMAIN_SwitchRenderer` keeps the hardware path (the software
    renderer is not available).
  - *Entry sites* whose function a service configured for SDL replaces (the
    `window` site on `WINMAIN_InitDirectDraw`, `draw_list` on
    `D3D_DrawPolygonList`) are left unpatched; the adapters do their work.
  - *Oracle*: with `OPENTONY_VIDEO_TRACE=<first>:<count>` a `video=sdl` run
    writes those frames as `frame_NNNNN.hal.ottx`; `tony hal video ingame
    RETAIL_DIR --hal HAL_DIR` compares them with a retail trace of the same
    scenario, frame for frame.
  - *Results*: HAL parity MATCH on the five Warehouse scenarios with
    `video=sdl,input=sdl,clock=sdl` (15/15, `portable` scope; the player
    heap address moves 4 MB); the in-game oracle passes 1,050/1,050 frames
    (210 per scenario, frontend and gameplay, worst 40.2 dB, over 99.9%
    within tolerance); a real-time Wine boot runs the startup movies, the
    menu and exits through the exit table; Windows 11 native (the
    original-2000 exe, NVIDIA OpenGL) shows the main menu on the game
    window.
  - *Textures edited in place* (3.5): the renderer copies a surface again
    after any write into it or its release, seen where every writer passes:
    the shared `IDirectDrawSurface7` vtable's `Unlock`, `Blt`, `BltFast`
    and last `Release` are patched while the renderer runs. Before, only
    D3DTex_UploadTexture and D3DTex_DestroyRecord were seen (the `texture`
    site): a sprite released by D3DSprite_Destroy whose address the next
    surface reused drew with the old copy (the Select Player menu lost its
    A, B, C and E glyphs), and the level-select clips (Bink decoding into
    their texture surface) stayed on their first frame. Neither screen is in
    the in-game oracle's frames. Only the live mirror (below) still runs on
    real DirectDraw and needs this watched-vtable mechanism (3.4e).
  - *HAL-native textures* (3.4e, `src/hooks/hal/video_surface.cpp`): real
    `ddraw.dll` is never loaded. A stand-in `IDirectDraw7`/`IDirectDrawSurface7`
    (`re/evidence/functions/hal-adapters.md`, "DirectDraw surfaces the retail
    code still uses") answers every call the unchanged `D3DTex_*`/
    `D3DSprite_*`/Bink surface code still makes: `CreateSurface` honours
    width, height, pixel format and caps and allocates its own pixel memory;
    `Lock`/`Unlock`/`Blt`/`BltFast` read and write that memory directly
    (pixel rules — pitch, fill, same-size copy, nearest-pixel stretch — are
    portable and host-tested, `src/hal/oracle/ddraw_surface.h`); each surface
    is its own HAL texture, re-uploaded only when written since the last
    draw, no periodic flush needed. `video_renderer.cpp`'s `texture_for`
    recognises a stand-in surface by its vtable and asks it directly for its
    texture, bypassing the address-keyed cache and the watched vtable (kept
    only for the live mirror's real-DirectDraw surfaces). Each live surface
    holds one reference on the stand-in DirectDraw, so `D3D_Shutdown(1)`'s
    leaked-reference exit code is unchanged. Checks: parity 15/15 (portable
    scope, `all=sdl`); in-game oracle 1,050/1,050 frames; primitive-stream
    oracle re-checked 1,050/1,050 identical over the same frames.
  - *Movies* are the movie service's (below); `video=sdl` turns `movie=sdl`
    on.
  - *SDL_GPU backend* (3.4d, `src/hal/sdl/sdl_video_gpu.cpp`): the preferred
    backend, Vulkan on this project's tested platforms (headless Wine + Mesa
    lavapipe, Windows 11 + NVIDIA); OpenGL 3.3 (`sdl_video_gl.cpp`) stays the
    fallback. `[hal] video_backend = auto|gpu|gl` (`OPENTONY_VIDEO_BACKEND`
    overrides it for one run) picks between them; `auto` (the default) tries
    SDL_GPU first and falls back to GL only if a device cannot be created,
    `gpu` fails loudly instead of falling back. Reproduces the same Direct3D 7
    conventions GL already honours (pixel centres on integers, w = 1/rhw only
    with perspective texturing, reversed depth, first-vertex flat shading via
    a CPU-side fan-to-list reorder, since SDL_GPU's Vulkan backend provokes
    the last vertex): nothing above the backend (the renderer, the stand-in
    device, the oracles) needed to change. Shaders are GLSL 450
    (`src/hal/sdl/shaders/*.{vert,frag}`) compiled to SPIR-V by the pinned
    glslang (`tony setup glslang`, `tony hal shaders`) into a **committed**
    header, `sdl_video_gpu_shaders.h`, so building the hook DLL never needs
    glslang; `tony hooks verify` fails with "run `tony hal shaders`" if a
    shader source and the header's recorded digest disagree. SDL_GPU cannot
    upload textures or vertices inside a render pass, so draws are recorded
    into a command list and only actually submitted (one command buffer: a
    copy pass uploading dirty textures and the frame's vertices, then a
    render pass replaying the recorded draws) at present, read_back, a mode
    switch, a batch size cap, or just before a texture mutation that would
    otherwise reorder a draw already recorded against it in the still-open
    batch. Pipelines are cached lazily by primitive type, blend, depth and
    colour-write state; every texture format is converted to RGBA8 on the
    CPU before upload (including indexed formats, through the palette, same
    as the GL backend already did on the GPU side).
  - *Screenshots* (3.6, `src/hooks/hal/video_surface.cpp`): retail's
    `D3D_SaveScreenshot` reads the primary surface's pixel memory through
    `Lock`, which nothing under `video=sdl` otherwise ever writes into (the
    ordering-table adapter and the stand-in D3D device draw straight to the
    backend's own offscreen target). Locking the primary read-only now
    refreshes its pixel memory from the active backend's `read_back()`
    first, converted down to whatever pixel format `CreateSurface` gave it
    (`ot_ddraw_convert_from_argb8888`, `src/hal/oracle/ddraw_surface.h`,
    portable and host-tested, same split as the other pixel rules). The one
    other caller that locks the primary read-only, the movie adapter's own
    read of what Bink just decoded into it, turns the refresh off for the
    duration of its Lock (`ot_ddraw_primary_suppress_refresh`) so it still
    sees Bink's write, not the backend's frame.
  - *Not yet*: the software renderer (permanent, see
    `docs/phases/PHASE_3_SUMMARY.md` Housekeeping item 5).
- **The standalone's draw path** (milestone 7c, `PHASE_5_SUMMARY.md` "7c result").
  A program with no retail image has no hook adapter to walk the ordering
  table, so `src/runtime/draw_kit.*` (`DrawKit`) is the same walk on the
  retail memory model, at the addresses of `Db_ClearOrderingTable`,
  `D3D_DrawPolygonList`, `D3DPoly_StartScene` and `D3D_PresentFrame`, drawing
  into the backend the program sets (`ot_standalone_set_video_hal`) with the
  surface store's textures (`HalTextureCache`), and writing the retail words
  the walk and the scene functions write. `opentony_spike_video.dll`
  (`src/hooks/kits/standalone_video.cpp`) is that backend for the spike: a
  recording layer over the SDL GL backend. `python -m tony.standalone_frame`
  compares a frame with retail's: the stream draw by draw against the
  shadow adapter's `.otvh` of a traced retail frame, the picture with
  `compare_images`, and (`--memory TICKS`) the memory image.

## Audio (`audio.h`)

**Retail.** Each sound effect is a PCM WAV in its own DirectSound buffer
(`Audio_LoadSoundBank`); a voice is one of up to eight duplicates,
addressed `(duplicate << 16) | sound`. Volume/pan/frequency are computed from
the PlayStation left/right volumes in DirectSound units (millibels, Hz,
clamped at 88200).

**HAL.** Sounds are PCM (`OtHalSoundDesc`); voices take the same units
(millibels, Hz), so retail's arithmetic stays exact and only the backend
converts to linear gain (`10^(mB/2000)`, the pan attenuating the other
channel: `ot_hal_audio_gains`). The model is DirectSound's, which Bink's
sound output relies on too: a sound may be rewritten in place
(`sound_write`), a voice plays it from a byte position, once or looping, and
reports where it is (`voice_position`), so a looping stream is refilled
behind its play cursor. `ot_hal_audio_render` (portable, host-tested) turns
8/16-bit mono/stereo PCM into gained float stereo from a position, wrapping
or stopping at the end. Streams carry PCM queued rather than looped.

SDL backend (`src/hal/sdl/sdl_audio.cpp`): one SDL audio stream per voice on
the default playback device, fed by a callback that renders the sound from
the voice's position; SDL resamples (the frequency is the stream's ratio)
and mixes. With no device it falls back to SDL's `dummy` driver, which still
consumes in real time. `OPENTONY_AUDIO_DRIVER` picks the driver;
`OPENTONY_AUDIO_FILE` with `disk` writes the mixed output (float32 stereo)
to a file, which is how the output is checked headless.

`audio=sdl` (`src/hooks/hal/audio_hal.cpp`) replaces `Audio_Init` only:
`AudioDevice` becomes a stand-in `IDirectSound` whose buffers are HAL
sounds and voices, and everything else in pc\soundlib.cpp (loading, the
PlayStation-volume arithmetic, duplicates, looping) runs unchanged on it.
So does Bink's DirectSound output (InitBink hands `AudioDevice` to
`BinkSetSoundSystem`), which carries the movie and music sound: Bink streams
through a 128 KB looping buffer it refills a quarter at a time. The stand-in
keeps the DirectSound behaviour retail and Bink can see: control flags and
parameter ranges (DSERR_CONTROLUNAVAIL, DSERR_INVALIDPARAM, as Wine), a
stopped buffer resuming where it stopped, a finished one-shot back at 0,
duplicates sharing memory, a play cursor always inside the buffer, and a
Lock offset equal to the buffer size meaning its start (Bink asks for that
when it has filled the ring to the end; refusing it crashed Bink).

## Movie (`movie.h`)

**Retail.** Bink (binkw32.dll) decodes the startup movies, in-game FMV and the
music (`Music/ltix*.dat` are Bink audio streams played by
`PCMOVIE_StreamThread`). Pictures go to a DirectDraw surface drawn as a quad,
sound to DirectSound.

**HAL.** The movie service owns only its outputs: a CPU frame target the
codec decodes into (`lock_frame`), presented by the video service, and an
audio stream for the sound. In the hooked game Bink stays the codec (its
sound reaches the HAL through the stand-in DirectSound, see Audio). **In the
standalone (5.3b) there is no Bink**: `src/hal/ffmpeg/movie_source.{h,cpp}`
decodes the same files with FFmpeg, and `src/hooks/kits/standalone_movie.cpp`
implements the Bink seams on it (below).

### Movies and music on FFmpeg (5.3b)

- **FFmpeg** is the pinned (`re/config/ffmpeg.yml`, `tony setup ffmpeg`),
  decode-only, LGPL 2.1+ 8.1.3 build: the Bink demuxer, `bink`,
  `binkaudio_dct` and `binkaudio_rdft` and the file protocol, shared 32-bit
  DLLs built by llvm-mingw; `THIRD_PARTY_NOTICES.md` has the notice. The DLLs
  are loaded at run time (`OPENTONY_FFMPEG_DIR`, else beside the program),
  so a player can swap in their own build of the same major versions; only
  `opentony_standalone.dll` and the tools use them, never `opentony_native.dll`.
- **Files.** `OPENTONY_MEDIA_DIR` is the install's media root (`med\Movies`
  and `data\Music` under it, as on the disc and in an install; default the
  program's directory). The game's names (`\ATVILOGO.STR;1`, `LTIX05.DAT`) are
  turned into files as `StartBinkMovie` does: the part after the last
  backslash, `.STR...` becomes `.DAT`. `tony media list` already lists
  `SETUP/med/Movies` (34 movies, 2.4 to 25 MB) and `SETUP/data/Music`; there is
  nothing to extract that `build/disc/files` does not have.
- **Seams** (`bink` category of `re/symbols/seams.yml`, plus `_beginthread` for
  the music thread and the Bink imports the natives report by name,
  `win32_BinkWait`/`Service`/`Pause`/`SetVolume`/`SetPan`): `StartBinkMovie`
  opens the file (a music file is opened for its sound only) and stores a
  counter in `BinkMovie`; `CreateBinkSurface` opens the HAL frame target and the
  audio stream; `NextBinkFrame` decodes into `lock_frame`, shows the picture of
  a full-screen movie, and answers 0 after the last frame (a small clip in
  texture mode loops, as retail's `0x006a6cfd` does); `BinkMovieUpdate` plays a
  clip a frame per update and stops it through `PCMOVIE_StopFMV`; the music
  runs on a host thread that feeds the HAL stream until the file ends or the
  natives' stop flag (`0x0054b0a8`) is set. Pacing is the standalone's lockstep
  clock (`Game_FrameTick` moves it 1/60 s; a `BinkWait` that is not over moves
  it a millisecond); real time arrives with 5.5. The sound is as long as the
  picture, as Bink's is (the last audio block usually runs past it).
- **Colour.** Bink's YUV to RGB is not a matrix product but four integer tables,
  summed and clamped, with the chroma replicated: `R = F[y] + Cr[v]`,
  `B = F[y] + Cb[u]`, `G = F[y] - Gu[u] - Gv[v]`, each entry the truncation
  toward zero of a constant times the offset (luma `1.1643835 * (max(y,16) - 16)`,
  chroma `1.1384 * {1.402, 1.772, 0.344136, 0.714136} * (c - 128)`). It was
  recovered from binkw32.dll's output (R depends on Y and V only, B on Y and U
  only, with no exception in 691,200 pixels, and the sums are exactly additive)
  and `convert_to_pixels` reproduces it.

**Proof against Bink's own decode** (`opentony_movie_compare.exe`, which loads
the game's binkw32.dll and the FFmpeg source in one Wine process;
`tests/test_ffmpeg_movie.py`):
- **Pictures: exact.** All 34 movies, 20,814 frames: every pixel of every frame
  is Bink's `BinkCopyToBuffer` result (32-bit surface; the 565 surface too on
  the 60 frames tried). The four startup movies (ATVILOGO 232, NSLOGO 266,
  GRAYMAT 320, INTRO 1,503 = 2,321 frames, the count of Phase 3) played by the
  real `PCMovie_PlayStartupMedia` over the seams into a recording HAL
  (`opentony_movie_player.exe`) deliver 2,321 frames whose hashes are Bink's,
  frame for frame.
- **Sound: not bit-exact, measured.** Bink hands its sound to a DirectSound
  buffer only, so the tool gives binkw32.dll a stand-in `IDirectSound` that
  records the ring in play order (the cursor metered to the sound rate:
  faster and Bink writes silence, which is how the recording once went wrong
  under load). Against it, with the sound as long as the picture:
  about **94.5 %** of the 16-bit samples are equal (ATVILOGO 94.5 %, NSlogo
  94.4 %, Muska 94.5 %, LTIX05 94.4 %), 4 to 5 % differ by one and about 1 % by
  two; the rest of a file (0 to 0.1 %, up to 4,005 in the loudest transients)
  differ by more. Outside the first 128 samples per channel of each block
  rounding to nearest matches 99.85 % (LTIX00); all the differences are in the
  cross-fade where a block is blended into the previous one, and the
  larger ones are where the two blocks differ most (and where FFmpeg clamps at
  32767 and Bink does not reach it). Reason: two different float
  implementations of one codec; Bink's blend comes out about one least
  significant bit lower for both signs, as if done on integers. The exact blend
  cannot be recovered from FFmpeg's output (the unblended blocks are not
  exposed), so closing the gap means reading Bink's decoder or patching a copy
  of FFmpeg's `binkaudio.c`, and a patched FFmpeg would no longer be the
  unmodified pinned source. Not done.

The movies (Bink headers of `Movies/*.dat`): 320x240 for the logos, the
intro, GrayMat and the skater videos, all played by the blocking
`PCMovie_PlayStartupMedia` (the skater videos through `PCMovie_PlayGameFMV`);
176x48 for the level-select clips, played a frame per game frame by
`PCMOVIE_StartFMV`/`PCMOVIE_Update` into a texture the menu draws; the music
(`Music/*.dat`, 4x4, audio only) by `PCMOVIE_StreamThread`. All share one
Bink handle (`BinkMovie`). In texture mode (`D3DTextureMode != 0`, the case
under Wine and on the tested installs) a movie of 200 lines or more is opened
with BINKCOPY2XWH and decoded, doubled to 640x480, straight into
`D3DPrimarySurface` (retail's visible front buffer); smaller ones, and every
movie with `D3DTextureMode` 0, into a texture surface (`BinkMovieInTexture`,
`BinkMovieSurface`).

`movie_video.cpp` (portable, host-tested) implements the movie backend on
any video backend: the frame target is CPU memory, uploaded to one texture
and drawn as one quad before `present`.

`movie=sdl` (`src/hooks/hal/movie_hal.cpp`; implies `video=sdl`, which turns
it on in turn) replaces `PCMovie_PlayStartupMedia`. The adapter runs retail's
loop unchanged (StartBinkMovie, CreateBinkSurface, Game_FrameTick, its own
PeekMessage pump, BinkService/BinkWait, NextBinkFrame,
Input_UpdateActionStates, the skip rule of PCInput_FirstKeyDown and
PCInput_FirstJoystickButton, CloseBinkMovie, Input_ClearPadTriggers). After
each frame Bink decoded into the primary, the whole primary (what retail
would have shown) is copied into the movie frame and presented. With
`D3DTextureMode` 0 retail's own quad and `Flip` run, which `video=sdl`
presents. The level-select clips and the music are left to retail: the
renderer sees the clip surface's `Unlock`s (Video), and the music has no
picture. A real-time Wine boot shows the four startup movies (2,321 frames);
closing the window during a startup movie crashes retail and the HAL alike
(the same fault in the same place).

## File system (`fs.h`)

**Retail.** File I/O never leaves the game's static C runtime; only start-up
path discovery (`PCinit`, `CD_DriveScan`), `WINMAIN_FindFiles` and the
settings file (`WINMAIN_LoadSetting`/`SaveSetting`) call Win32. The CD check
(`CD_CheckAudioTOC`, canonical build only) uses MCI.

**HAL.** Not a `[hal]` service in the hook DLL: retail file I/O is kept. The
interface is for the standalone build: roots (install, media, save, config),
case-insensitive path resolution (the game spells the same file several ways),
files, directory listing and settings.

## Status

| Service | Interface | SDL backend | Adapter | Gate |
|---------|-----------|-------------|---------|------|
| clock   | 3.2 | 3.3 done | 3.3 done | parity MATCH (portable scope), 5 Warehouse scenarios; paced real-time runs under Wine and Windows 11 |
| window  | 3.2 | 3.3 done (adopt) | 3.3 done | real-time play under Wine; Windows 11 smoke test |
| input   | 3.2 | 3.3 done | 3.3 done | parity MATCH with `input=sdl`, recorded input through the action source; keyboard (Wine, Windows 11) and virtual-gamepad smoke tests |
| video   | 3.2 | 3.4: GL 3.3 backend, presents on the game window; 3.4d: SDL_GPU (Vulkan) backend, preferred, GL 3.3 the fallback | 3.4 done: `video=sdl` (stand-in Direct3D device, ordering-table adapter); 3.4e done: HAL-native textures (stand-in DirectDraw, real `ddraw.dll` never loaded); 3.4d done: `video_backend = auto\|gpu\|gl` selects the SDL video backend | parity MATCH with `all=sdl`; in-game oracle 1,050/1,050 frames (worst 40.2 dB); image oracle 46 dB (GL); primitive-stream oracle 1,306/1,306 (3.4), re-checked 1,050/1,050 identical over the in-game oracle's frames after 3.4e; live mirror worst 40 dB; Windows 11 native; 3.4d gates in PHASE_3_SUMMARY.md |
| audio   | 3.2 | 3.5: SDL audio streams, one per voice | 3.5: `Audio_Init`, stand-in DirectSound (soundlib and Bink unchanged on it) | parity MATCH with `all=sdl`; the four startup movies with their sound and the menu sound effects recorded from the mixer (Wine, `disk` driver) |
| movie   | 3.2 | 3.5: on the video backend (`movie_video.cpp`) | 3.5: `PCMovie_PlayStartupMedia`; 5.3b: the standalone's Bink seams on FFmpeg (no Bink) | startup movies on screen (Wine boot, 2,321 frames); parity MATCH with `video=sdl` (which turns it on); 5.3b: 20,814 frames of 34 movies identical to Bink, the sound 94.5 % bit-exact (above) |
| fs      | 3.2 | standalone build | none | |
