# Platform boundary functions (Phase 3.0 audit)

Status: reviewed (2026-09-26). Canonical build `executables.thps2_pc`; the
original-2000 addresses are derived in `re/config/hal_boundary.yml`.

This record backs the curated names of the functions that own THPS2's calls
into DirectDraw/Direct3D 7, DirectInput, DirectSound, WinMM, Bink, USER32 and
the file system. How the boundary was derived, and why each hook point fully
owns its calls, is in `re/notes/platform-boundary.md`; `tony hal check`
re-derives it.

## Method

- Interface types come from `tony hal derive`: factory out-parameters
  (`DirectDrawCreateEx` into 0x029da3a8, `DirectInputCreateA`,
  `DirectSoundCreate` into `AudioDevice`, `CoCreateInstance(CLSID_DirectPlay)`),
  producer methods (`CreateSurface`, `QueryInterface(IID_IDirect3D7)`,
  `CreateDevice`, ...) and reviewed roots.
- Each name below was checked against the function's disassembly or
  decompilation, the assert/source strings it references, and where available
  the PARTYMOD (MIT) hook name or the Cumulative Patch declaration at the
  translated address (`re/xref/community.yml`). Only names and addresses are
  taken from those projects.

## Names

| Canonical | Name | Basis |
|---|---|---|
| 0x004dabb0 | D3DTimer_Init | `timeGetDevCaps`, `timeBeginPeriod`, D3DTimer_StartEvent(&TimerState, 16, SimulationTimeTimerCallback); log string `D3DTimer: Min %i Max %i`; atexit(D3DTimer_Shutdown); assert path `Pc\D3dTimer.cpp` |
| 0x004dad80 | D3DTimer_StartEvent | the only `timeSetEvent` call; stores the timer id in TimerState |
| 0x004dab80 | D3DTimer_Shutdown | `timeKillEvent(TimerState)` when D3DTIMER_IsActive; called by WINMAIN_ShutDown and registered with atexit |
| 0x004f6550 | WINMAIN_Sleep | `Sleep(arg)`; Cumulative Patch WINMAIN_Sleep (promoted) |
| 0x004e4a60 | PCInput_Shutdown | releases the SDX keyboard, mouse, joystick and DirectInput objects and `ReleaseCapture`; PARTYMOD PCINPUT_ShutDown |
| 0x004e4c40 | PCInput_ActuatorOn | starts the force-feedback effect of pad 0/1 when enabled; PARTYMOD PCINPUT_ActuatorOn |
| 0x004e4cf0 | PCInput_ActuatorOff | stops both effects (`IDirectInputEffect::Stop`) when force feedback is enabled |
| 0x004e4c10 | PCInput_SetCursorPos | `SetCursorPos(x, y)` |
| 0x004f8130 | WINMAIN_InitDirectDraw | `DirectDrawEnumerateExA`, device GUID from the registry, `DirectDrawCreateEx(IID_IDirectDraw7)`, cooperative level, display mode, primary/back buffers; PARTYMOD initDDraw |
| 0x004f8630 | WINMAIN_DirectDrawEnumCallback | callback passed to `DirectDrawEnumerateExA` at 0x004f8173 |
| 0x004f7110 | WINMAIN_InitDirect3D | `QueryInterface(IID_IDirect3D7)` into 0x029da3bc, EnumDevices, EnumZBufferFormats, z-buffer surface, `CreateDevice` into 0x029da3c0, initial render/stage states; PARTYMOD initD3D |
| 0x004f78f0 | WINMAIN_D3DDeviceEnumCallback | callback passed to `IDirect3D7::EnumDevices` at 0x004f7179 |
| 0x004f7980 | WINMAIN_ZBufferEnumCallback | callback passed to `IDirect3D7::EnumZBufferFormats` at 0x004f72da |
| 0x004f7a70 | WINMAIN_EnumDisplayModes | `IDirectDraw7::EnumDisplayModes(0, 16-bit desc, 0x029d8478, callback)`; assert path `Pc\WinMain.cpp` |
| 0x004f79e0 | WINMAIN_DisplayModeEnumCallback | callback passed at 0x004f7ab3 |
| 0x004f6e60 | WINMAIN_SwitchResolution | `SetDisplayMode`, recreates the primary/back surfaces, WINMAIN_InitDirect3D; PARTYMOD and Cumulative Patch (promoted) |
| 0x004f6d20 | WINMAIN_SwitchRenderer | mode 4 (hardware) / 8 (software renderer, 0x029da3a0): drops the z-buffer attachment, D3D_Shutdown(0), reinitializes Direct3D; log `D3D RENDERER: From %i to %i`; Cumulative Patch (promoted) |
| 0x004d0ec0 | D3D_ClearBuffers | colour-fill `Blt` of the primary and back buffers; PARTYMOD D3D_ClearBuffers |
| 0x004d3b40 | D3DPoly_StartScene | hardware renderer: `BeginScene` (D3D_RestoreSurfaces on `DDERR_SURFACELOST`), `Clear`, per-frame render/stage states; software renderer (0x029da3a0 != 0): D3D_LockBackBuffer and a software clear; PARTYMOD D3DPOLY_StartScene; assert path `Pc\d3dpoly.cpp` |
| 0x004d0a60 | D3D_LockBackBuffer | `Lock(DDLOCK 0x821)` of the back buffer, D3D_RestoreSurfaces on `DDERR_SURFACELOST` |
| 0x004d0dd0 | D3D_RestoreSurfaces | `IDirectDraw7::RestoreAllSurfaces`, then colour-fill of primary and back |
| 0x004d0f80 | D3D_Shutdown | releases device, Direct3D, surfaces and DirectDraw; with a non-zero argument (every fatal-error path) also D3DPoly_Shutdown and Audio_Shutdown |
| 0x004d7450 | D3DPoly_Shutdown | frees the polygon buffer (0x006a0070) and, with the hardware renderer, unbinds texture stage 0 |
| 0x004d0d00 | D3D_SaveScreenshot | at most once per SimulationTime: picks the first unused `SCR_%05i.bmp`-style name, logs `SCREENSHOT %s`, and calls D3DTex_SaveSurfaceAsBMP on the primary surface |
| 0x004d7e10 | D3DTex_SaveSurfaceAsBMP | 24-bit BMP writer; log string `SaveSurfaceAsBMP: Problems creating %s` |
| 0x004da840 | D3DTex_Init | `IDirect3DDevice7::EnumTextureFormats`, picks a 16-bit ARGB format (1555, then 4444); `MessageBeep` and `D3DTEX: Did not find acceptable...` when none; PARTYMOD D3DTEX_Init |
| 0x004d9f50 | D3DTex_GrayTexture | Lock/Unlock of the record's surface to grey it; PARTYMOD D3DTEX_GrayTexture |
| 0x004d74a0 | D3DSprite_Create | builds a sprite from a texture record (staging surface, tiles); PARTYMOD D3DSprite_D3DSprite; assert path `Pc\d3dsprite.cpp` |
| 0x004d83a0 | D3DSprite_BuildTiles | splits the record's surface into 0x2c-stride tile surfaces (GetSurfaceDesc, CreateSurface, Blt) |
| 0x004d78c0 | D3DSprite_Destroy | releases every tile surface; PARTYMOD D3DSprite_Destroy |
| 0x004f27c0 | Audio_Init | `DirectSoundCreate` into AudioDevice, `SetCooperativeLevel(DSSCL_EXCLUSIVE)`, `GetCaps`; clears AudioSoundIdMap and AudioBankRecords; assert path `Pc\soundlib.cpp` |
| 0x004f2ad0 | Audio_FreeSoundBank | frees every sound of bank `arg` (Audio_FreeSound), clears AudioSoundIdMap |
| 0x004f2da0 | Audio_FreeSound | Audio_StopSound, then `Release` of the 8 buffer slots of the sound's AudioBankRecords entry |
| 0x004f3200 | Audio_StartVoice | SetVolume/SetPan/SetFrequency then `Play` (looping when the sound's flag says so); log string `SND_SetVolume Err` |
| 0x004f34b0 | Audio_SetVoiceVolume | SetVolume/SetPan of a playing voice (starts it through Audio_StartVoice when idle) |
| 0x004f3690 | Audio_SetVoiceFrequency | `SetFrequency`, clamped to 0x15888 |
| 0x004f3720 | Audio_GetVoiceFrequency | `GetFrequency` |
| 0x004f3870 | Audio_HaltSound | `Stop` on the 8 buffer slots of one sound |
| 0x004f3900 | Audio_PauseLooping | Audio_HaltSound for every looping sound (flag bit 0) |
| 0x004f3940 | Audio_ResumeLooping | Audio_StartVoice for every looping sound |
| 0x004f38d0 | Audio_StopAll | Audio_StopSound for every loaded sound |
| 0x004e71d0 | PCMOVIE_StreamThread | thread start routine passed to `_beginthread` by PCMOVIE_StartMusic (and referenced by PCMOVIE_Pause): loops BinkWait, NextBinkFrame, Sleep(3), BinkService until 0x0054b0a8 is set, then logs, CloseBinkMovie and `_endthread`; Ghidra had left it outside every function |
| 0x004ecf90 | SDXJoystick_ReleaseEffectCallback | the callback FUN_004ecf50 passes to IDirectInputDevice2A::EnumCreatedEffectObjects; releases the effect it receives; Ghidra had left it outside every function |
| 0x004f86f0 | WINMAIN_ShutDown | logs, then calls the netgame shutdown (0x004ddb70), D3DTimer_Shutdown, PCInput_Shutdown, PCMOVIE_StopFMV, PCMOVIE_XAStop and D3D_Shutdown(1) in that order, and returns; Cumulative Patch WINMAIN_ShutDown (promoted); the HAL stops after it |
| 0x005001e2 | _onexit | statically linked VC6 runtime: `push esi; call _lockexit; push [__onexitbegin]; call _msize; ...`; D3DTimer_Init registers D3DTimer_Shutdown through it; original 2000 0x004fd212 (identical) |
| 0x004bb0b0 | CD_ReadLocalFile | reads a whole-dword file, relative to the executable's directory unless the path is absolute (`GetModuleFileNameA`, `CreateFileA`, `GetFileSize`, `ReadFile`), then decodes it; used by CD_CheckAudioTOC for `texture.dat` |

Promoted without change (Cumulative Patch names; the behaviour matches):
PCMOVIE_StartFMV, PCMOVIE_Update, PCMOVIE_StopFMV, PCMOVIE_StartMusic,
PCMOVIE_XAStop, PCMOVIE_SetXAVolume, PCMOVIE_Pause, SetupFullScreenMovie,
DrawFullScreenMovie, InitBink, CreateBinkSurface, StartBinkMovie,
NextBinkFrame, CloseBinkMovie, BinkMovieUpdate, PCinit, WINMAIN_FindFiles,
WINMAIN_LoadSetting, WINMAIN_SaveSetting, WINMAIN_WndProc, printf_log.

## Findings

- Music is Bink audio (`Music/ltix*.dat` through PCMOVIE_StartMusic and the
  XA-compatible PCMOVIE_* API), not CD audio. The only MCI traffic is
  `MCI_OPEN`/`MCI_SET`/`MCI_STATUS`/`MCI_CLOSE` inside CD_CheckAudioTOC, which
  compares `texture.dat` with the disc's audio track table (a CD check).
- The game has a software renderer (flag 0x029da3a0, WINMAIN_SwitchRenderer mode 8,
  SOFTREND_Startup) besides Direct3D; D3DPoly_StartScene locks the back buffer
  for it.
- The D3D_Draw* primitives have no direct callers: D3D_InitPrimitiveDispatch
  stores them in D3DPrimitiveDispatchTable (0x0065f82c) and the ordering-table
  walkers call through it.
- Game file I/O reaches the platform only through the statically linked C
  runtime (`fopen`/`fread` style), never through Win32 file calls directly.
