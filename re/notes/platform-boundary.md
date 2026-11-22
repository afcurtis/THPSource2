# Platform boundary (Phase 3.0 audit)

Every place retail THPS2 calls into the Windows platform, grouped by the HAL
service that will replace it, with the game-level function (the *hook point*)
that fully owns each call. The machine-readable inventory is
`re/config/hal_boundary.yml`; `tony hal check` (also run by `tony verify`)
re-derives it from Ghidra and fails when it is incomplete or stale. Curated
names are backed by `re/evidence/functions/platform-boundary.md`.

## Numbers (canonical build)

| | |
|---|---|
| Boundary call sites | **874**: 406 COM vtable calls, 444 import calls, 24 import tail-jumps |
| Functions containing them | 284: 57 hook points, 83 owned helpers, 33 multiplayer (stay retail), 6 dead, 105 C runtime |
| Platform -> game callbacks | 10 (enumeration callbacks, the timer callback, thread starts) |
| Indirect calls in the executable | 1,478, every one accounted for: 406 COM, 775 C++ virtuals (instruction evidence), 18 reviewed C++ virtuals, 91 function-pointer tables |
| Original-2000 addresses | derived for every site except the re-release's CD check (absent from that build) |

Sites by service: video 244, movie 77, input 71, audio 40, fs 25, window 23,
clock 5, debug 2, net 122 (multiplayer), runtime 265 (C runtime).

## How the inventory is derived

`tony hal derive` (`tony/hal/`):

1. **Facts** (`facts.py`). Every function is decompiled twice inside one
   Ghidra transaction that is rolled back: the first pass commits the
   decompiler's inferred prototypes (so callers pass `this` to thiscall
   wrappers), the second exports every call, store and return as a symbolic
   expression. Instruction-level ground truth is exported alongside: every
   reference to an import slot or thunk (including register-cached imports,
   `mov edi, [__imp__X]; call edi`) and every computed call, with code Ghidra
   left outside any function attributed to its owner.
2. **Types** (`infer.py`). Interface pointers are seeded from the factories
   (`DirectDrawCreateEx`, `DirectInputCreateA`, `DirectSoundCreate`,
   `DirectPlayLobbyCreateA`, `CoCreateInstance`) and followed to a fixpoint
   through producer out-parameters (`CreateSurface`, `QueryInterface` by IID,
   `CreateDevice`, ...), stores, locals, parameters, returns, object fields
   across call sites, and enumeration callbacks. A callee inherits a type
   only from its typed callers and never passes an inherited type back out,
   which keeps generic helpers from leaking an interface into unrelated game
   objects.
3. **Refinement.** Ghidra guesses caller cleanup for some un-prototyped
   indirect calls and then mis-tracks the stack after them (seen in the SDX
   joystick wrapper and D3DTex_SaveSurfaceAsBMP). Every typed COM call gets a
   stdcall call-site override with the SDK arity, and its function is
   decompiled again, until no new site is typed.
4. **Classification.** An indirect call `call [R+off]` is a C++ virtual when
   `this` travels in ECX and a COM call when the object is pushed last; the
   check fails if a typed COM site's instructions say otherwise. Calls the
   rules cannot decide are reviewed by hand and recorded as `roots` in the
   YAML with the instruction-level evidence.
5. **Ownership.** For each hook point the YAML lists the helpers it owns. The
   check proves that every owned function is referenced only from hook
   points, owned functions or deliberate groups, and is reachable from its
   own hook point (the primitive dispatch table is an explicit edge), so
   replacing all hook points removes every path from game code to a boundary
   site.
6. **Original 2000.** Functions map through `re/xref/original_2000.yml`
   (function map, data map for dispatch-table entries, identical-spacing
   interpolation); each site maps to the same-ordinal call with the same
   signature (import name, or vtable offset) in the variant function.

## Hook points

Addresses: canonical / original 2000. Aliases are the PARTYMOD (MIT) hook or
Cumulative Patch name at the same function; the original-2000 addresses match
PARTYMOD's own hook addresses.

| Service | Hook point | Canonical | Original 2000 | Owned | Sites | Alias |
|---|---|---|---|---|---|---|
| clock | D3DTimer_Init | 0x004dabb0 | 0x004d7d70 | 2 | 3 | |
| clock | D3DTimer_Shutdown | 0x004dab80 | 0x004d7d40 | 0 | 1 | |
| clock | WINMAIN_Sleep | 0x004f6550 | 0x004f3600 | 0 | 1 | |
| window | Game_Startup | 0x004f7e30 | 0x004f4ec0 | 1 | 18 | CP WinMainX |
| window | Game_FrameTick | 0x004f7ce0 | 0x004f4d70 | 0 | 5 | PARTYMOD WinYield |
| input | PCInput_Initialize | 0x004e4690 | 0x004e1860 | 17 | 40 | PARTYMOD InitDirectInput |
| input | PCInput_Poll | 0x004e4a90 | 0x004e1c60 | 5 | 10 | PARTYMOD ReadDirectInput |
| input | PCInput_Shutdown | 0x004e4a60 | 0x004e1c30 | 5 | 10 | PARTYMOD PCINPUT_ShutDown |
| input | PCInput_ActuatorOn | 0x004e4c40 | 0x004e1e10 | 2 | 2 | PARTYMOD PCINPUT_ActuatorOn |
| input | PCInput_ActuatorOff | 0x004e4cf0 | 0x004e1ec0 | 1 | 1 | |
| input | PCInput_SetCursorPos | 0x004e4c10 | 0x004e1de0 | 0 | 1 | |
| video | WINMAIN_InitDirectDraw | 0x004f8130 | 0x004f5190 | 1 | 13 | PARTYMOD initDDraw |
| video | WINMAIN_InitDirect3D | 0x004f7110 | 0x004f41c0 | 2 | 18 | PARTYMOD initD3D |
| video | WINMAIN_EnumDisplayModes | 0x004f7a70 | 0x004f4b20 | 1 | 1 | |
| video | WINMAIN_SwitchResolution | 0x004f6e60 | 0x004f3f10 | 0 | 5 | PARTYMOD WINMAIN_SwitchResolution |
| video | WINMAIN_SwitchRenderer | 0x004f6d20 | 0x004f3dd0 | 0 | 2 | CP WINMAIN_SwitchRenderer |
| video | D3D_ClearBuffers | 0x004d0ec0 | 0x004ce0d0 | 0 | 2 | PARTYMOD D3D_ClearBuffers |
| video | D3DPoly_StartScene | 0x004d3b40 | 0x004d0d50 | 2 | 26 | PARTYMOD D3DPOLY_StartScene |
| video | D3D_InitPrimitiveDispatch | 0x004d41e0 | 0x004d13f0 | 0 | 1 | PARTYMOD D3DPOLY_Init |
| video | D3D_DrawPolygonList | 0x004d3160 | 0x004d0370 | 23 | 121 | PARTYMOD D3DPOLY_DrawOTag |
| video | D3D_PresentFrame | 0x004d0c30 | 0x004cde40 | 0 | 3 | PARTYMOD D3D_EndSceneAndFlip |
| video | D3D_Shutdown | 0x004d0f80 | 0x004ce190 | 1 | 8 | |
| video | D3D_SaveScreenshot | 0x004d0d00 | 0x004cdf10 | 0 | 0 | |
| video | D3DTex_Init | 0x004da840 | 0x004d7a00 | 0 | 2 | PARTYMOD D3DTEX_Init |
| video | D3DTex_UploadTexture | 0x004d8f10 | 0x004d6100 | 4 | 16 | PARTYMOD makeTextureListEntry |
| video | D3DTex_DestroyRecord | 0x004d8b50 | 0x004d5d40 | 0 | 2 | PARTYMOD freeD3DTexture |
| video | D3DTex_LoadIndexedBitmap | 0x004d9950 | 0x004d6b40 | 0 | 2 | PARTYMOD D3DTEX_AddToTextureList3 |
| video | D3DTex_GrayTexture | 0x004d9f50 | 0x004d7120 | 0 | 3 | PARTYMOD D3DTEX_GrayTexture |
| video | D3DTex_CountUsedPaletteEntries | 0x004da260 | 0x004d7430 | 0 | 2 | PARTYMOD D3DTEX_TextureCountColors |
| video | D3DSprite_Create | 0x004d74a0 | 0x004d46b0 | 1 | 15 | PARTYMOD D3DSprite_D3DSprite |
| video | D3DSprite_Destroy | 0x004d78c0 | 0x004d4ad0 | 0 | 2 | PARTYMOD D3DSprite_Destroy |
| audio | Audio_Init | 0x004f27c0 | 0x004ef8b0 | 0 | 3 | |
| audio | Audio_Shutdown | 0x004f28a0 | 0x004ef990 | 0 | 2 | |
| audio | Audio_LoadSoundBank | 0x004f2960 | 0x004efa50 | 3 | 16 | |
| audio | Audio_FreeSoundBank | 0x004f2ad0 | 0x004efbc0 | 1 | 1 | |
| audio | Audio_PlaySound | 0x004f2e20 | 0x004eff10 | 0 | 7 | |
| audio | Audio_StopSound | 0x004f37c0 | 0x004f08b0 | 0 | 2 | |
| audio | Audio_SetVoiceVolume | 0x004f34b0 | 0x004f05a0 | 1 | 6 | |
| audio | Audio_SetVoiceFrequency | 0x004f3690 | 0x004f0780 | 0 | 1 | |
| audio | Audio_GetVoiceFrequency | 0x004f3720 | 0x004f0810 | 0 | 1 | |
| audio | Audio_PauseLooping | 0x004f3900 | 0x004f09f0 | 1 | 1 | |
| audio | Audio_ResumeLooping | 0x004f3940 | 0x004f0a30 | 0 | 0 | |
| movie | PCMovie_PlayStartupMedia | 0x004e5ec0 | 0x004e3090 | 7 | 67 | CP PCMOVIE_PlayMovieFile |
| movie | PCMOVIE_StartFMV | 0x004e70e0 | 0x004e42b0 | 0 | 0 | |
| movie | PCMOVIE_Update | 0x004e71c0 | 0x004e4390 | 1 | 1 | |
| movie | PCMOVIE_StopFMV | 0x004e7140 | 0x004e4310 | 0 | 1 | |
| movie | PCMOVIE_StartMusic | 0x004e7260 | 0x004e4430 | 1 | 3 | |
| movie | PCMOVIE_XAStop | 0x004e7370 | 0x004e4540 | 0 | 1 | |
| movie | PCMOVIE_SetXAVolume | 0x004e72d0 | 0x004e44a0 | 0 | 2 | |
| movie | PCMOVIE_Pause | 0x004e6e70 | 0x004e4040 | 0 | 2 | |
| fs | PCinit | 0x004e73f0 | 0x004e45c0 | 1 | 1 | |
| fs | CD_CheckAudioTOC | 0x004bb240 | not in this build | 4 | 16 | |
| fs | CD_Recheck | 0x004f6510 | not in this build | 0 | 1 | |
| fs | WINMAIN_FindFiles | 0x004f6560 | 0x004f3610 | 0 | 4 | |
| fs | WINMAIN_LoadSetting | 0x004f6ca0 | 0x004f3d50 | 0 | 1 | |
| fs | WINMAIN_SaveSetting | 0x004f6cd0 | 0x004f3d80 | 0 | 2 | |
| debug | printf_log | 0x004cf850 | 0x004cca60 | 0 | 2 | |

Deliberately not hooked: 33 multiplayer functions (pc\dxplay.cpp,
pc\netgame.cpp and the Win32 setup dialogs; the multiplayer decision), 6
unreferenced SDX DirectInput wrapper methods, 4 static SDX wrapper
destructors the C runtime runs at exit (they release only non-NULL
interfaces), and the statically linked C runtime.

## Interface pointers (where each lives)

| Storage | Interface | Produced by |
|---|---|---|
| 0x029da3a8 | IDirectDraw7 | DirectDrawCreateEx (WINMAIN_InitDirectDraw) |
| 0x029da3ac / 0x029da3b0 | primary / back buffer | CreateSurface / GetAttachedSurface |
| 0x029da3b4 | z-buffer surface | CreateSurface (WINMAIN_InitDirect3D) |
| 0x029da3bc | IDirect3D7 | QueryInterface(IID_IDirect3D7) |
| 0x029da3c0 | IDirect3DDevice7 | IDirect3D7::CreateDevice |
| texture record +0x0 | texture surface | D3DTex_UploadTexture (CreateSurface) |
| 0x006a6998, 0x006a69c8, 0x006a6ce8, 0x006a6b98 | Bink surfaces | CreateBinkSurface |
| SDXInput (0x006a4500) +0x4 | IDirectInputA | DirectInputCreateA(0x0700) |
| keyboard wrapper +0x108 / mouse wrapper +0x28 | IDirectInputDeviceA | CreateDevice |
| SDXJoystick +0x58 | IDirectInputDevice2A | CreateDevice + QueryInterface |
| effect wrapper +0x8 | IDirectInputEffect | CreateEffect |
| AudioDevice (0x029d8360) | IDirectSound | DirectSoundCreate |
| AudioBankRecords (0x029d6920) slots 0..7 | IDirectSoundBuffer | CreateSoundBuffer / DuplicateSoundBuffer |
| DirectPlay wrapper +0x0 (0x006a05c8) | IDirectPlay4A | CoCreateInstance(CLSID_DirectPlay) |

## Findings

- **Rendering boundary.** The game builds PSX GPU primitives into an ordering
  table; D3D_DrawPolygonList (PARTYMOD `D3DPOLY_DrawOTag`) walks it and calls
  the D3D_Draw* primitives through D3DPrimitiveDispatchTable, which
  D3D_InitPrimitiveDispatch fills. The primitives have no other callers, so
  the video HAL can take the ordering table as its input.
- **A software renderer exists.** WINMAIN_SwitchRenderer mode 8 sets
  0x029da3a0; D3DPoly_StartScene then locks the back buffer for SOFTREND_*
  instead of using Direct3D.
- **Music is Bink, not CD audio.** `Music/ltix*.dat` streams through
  PCMOVIE_StartMusic and PCMOVIE_StreamThread (a `_beginthread` worker running
  BinkWait/NextBinkFrame/BinkService); the PCMOVIE_XA* names are the PSX API
  it replaces. The only MCI traffic is MCI_OPEN/SET/STATUS/CLOSE in the CD
  check. Both builds still carry `play cdaudio ...` MCI command strings with
  no import to send them (no `mciSendStringA`).
- **CD check (canonical re-release only).** CD_CheckAudioTOC reads
  `texture.dat` next to the executable and compares it with the disc's audio
  track table; on failure WINMAIN_WndProc and Game_Startup show "Please insert
  the Tony Hawk's Pro Skater 2 CD-ROM..." and CD_Recheck closes the window.
  The original 2000 build has none of this (it relied on SafeDisc 2).
- **File I/O** reaches Win32 only through the statically linked C runtime;
  the game-level file API (PCinit, FileIO_Backend*, PKR*) is the future `fs`
  hook point for loose-file overrides.
- **Ghidra gaps fixed.** PCMOVIE_StreamThread and
  SDXJoystick_ReleaseEffectCallback were outside every Ghidra function; they
  are now curated (and created by `tony ghidra sync`).
- **Multiplayer** is isolated: its functions call nothing outside
  pc\dxplay.cpp/pc\netgame.cpp and USER32 dialogs, so it can stay retail.

## Residual risk

- Hand-reviewed items (roots in the YAML) are exactly the places the
  automatic rules could not decide; each carries its instruction-level
  evidence. A wrong review would show up as a runtime COM call from a site
  this inventory does not list.
  **Dynamic cross-check (Phase 3 housekeeping, 2026-09-27):**
  `tony hal trace` (`tony/hal/trace.py`, `src/hooks/hal/site_trace.cpp`)
  plants a one-shot software breakpoint at every one of this inventory's 874
  canonical-build call sites and records, from a real running process, which
  ones actually execute; `OPENTONY_HAL_TRACE_SITES` is inert unless set, so
  every other run is unaffected. Run across the five Warehouse scenarios
  (`--hooks none`, retail, no HAL): 293/874 sites (33.5%) fired, and — the
  point of the check — **every single hit matched a listed site exactly;
  zero hits at an unlisted address.** Coverage by service (audio 34/40,
  clock 4/5, debug 2/2, window 18/23, video 121/244, runtime 81/265, input
  16/71, movie 13/77, fs 4/25, net 0/122) tracks what a short single-player
  capture actually exercises: `net` is 0 because these scenarios never open
  a DirectPlay session (multiplayer is out of scope, decision-record above);
  `movie`/`fs` are low because a scenario capture starts already inside the
  Warehouse level, past the startup movies and most path-resolution code.
  This does not *prove* no site is missing (it can only confirm sites this
  inventory already lists), but it independently confirms the 293 it did
  see are exactly right, with no static-analysis artifact among them. A
  version that could also discover an unlisted site would need to intercept
  every method of every live COM interface regardless of address (patching
  each vtable slot, not a fixed address list); given the static audit's own
  already-clean result (0 contradictions, 3.0) and this session's cost/risk
  budget, that larger tool is deferred, not built — see
  docs/phases/PHASE_3_SUMMARY.md.
- Unit attribution relies on assert paths; functions without one carry the
  link-order bracket (`pc\pcinput.cpp .. pc\pcmovie.cpp`).
