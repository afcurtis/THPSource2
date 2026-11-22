# Functions and globals used by the HAL adapters (Phase 3.3)

Status: reviewed (2026-09-26). Canonical build `executables.thps2_pc`; the
original-2000 addresses are in `re/config/native_hooks.yml`
(`services.hal.symbols`), checked by `tony hooks verify` against
`re/xref/original_2000.yml`.

The clock and input adapters (`src/hooks/hal/`) replace retail hook points
and call or update the retail code and data below. Each name was checked
against the decompilation and disassembly of the function and of every
reference to the address (`llvm-objdump` over the whole `.text`).

## Functions

| Canonical | Name | Basis |
|---|---|---|
| 0x00413f30 | Options_ResetControllerDefaults | writes the per-port option words: `AutoKickState[port] = 1`, 0x0055fc34[port] = 1 (vibration), button layout 0..3 at 0x0055fc0c + port*16; PCInput_Initialize calls it with 0 and 1. The name was provisional (Cumulative Patch `Career.h`); the body confirms it. |
| 0x004dab30 | D3DTIMER_IsActive | `mov eax, [TimerState]; test eax, eax; setne al; ret`; D3DTimer_Shutdown calls it before `timeKillEvent`. Provisional name confirmed. |
| 0x004e3e80 | PCInput_ResetBindings | copies the default key table 0x0054ace8..0x0054ad1c into the live bindings 0x0054ad94..0x0054adc8 and sets the joypad bindings 0x0054ae04.. to 1, 4, 2, (0x006a6984 =) 0, 3, 5, 6, 7, -1, -1; called by PCInput_Initialize and by PCInput_LoadBindings when the saved bindings are invalid or collide. |
| 0x004ec930 | SDXMouse_SetBounds | thiscall on SDXMouse: stores (left, top, right, bottom) at +0x14/+0x18/+0x1c/+0x20 and clamps the position at +4/+8 into them; PCInput_Initialize calls it with (0, 0, 0x260, 0x1c0). No platform calls. |

## Globals

| Canonical | Name | Basis |
|---|---|---|
| 0x006a0588 | TimerCaps | the `TIMECAPS` out-parameter of `timeGetDevCaps(&TimerCaps, 8)` in D3DTimer_Init; wPeriodMin at +0, wPeriodMax at +4 choose TimerState+8 (the period). |
| 0x006a05b8 | TimerTickFrames | `fild qword (interval); fmul [60.0]; fmul [0.001]; fstp [0x006a05b8]` in D3DTimer_Init and in SimulationTimeTimerCallback: the tick length in 60 Hz frames (0.96 for 16 ms). |
| 0x029d8374 | MainWindow | `CreateWindowExA` result stored by Game_Startup, passed to WINMAIN_InitDirectDraw, PCInput_Initialize and Audio_Init; Game_FrameTick's `PeekMessageA`/`GetMessageA` filter. |
| 0x006a4500 | SDXInput | `this` of the DirectInputCreateA wrapper (0x004ebf90 stores HINSTANCE at +0x10, HWND at +0x14, the interface at +4); the joystick enumeration callbacks 0x004ec100/0x004ec160 count devices at +0x2458 and set +0x245c (any joystick) / +0x245d (force feedback), read by PCInput_Initialize and PCInput_ActuatorOn/Off as 0x006a695c/0x006a695d. |
| 0x006a3f20 | SDXMouse | `this` of the mouse create/update/acquire wrappers (0x004ec790, 0x004ec990, 0x004ecbf0); the update reads buffered data and accumulates +4/+8 scaled by +0xc/256, clamped to +0x14..+0x20; PCInput_Poll updates it only while +0x50 (acquired, 0x006a3f70) is set, which only the acquire wrapper (reached from the update itself) sets. |
| 0x029da3c0 | D3DDevice | out-parameter of `IDirect3D7::CreateDevice` in WINMAIN_InitDirect3D (tony hal derive types it IDirect3DDevice7); D3DPoly_StartScene calls BeginScene (+0x14), Clear (+0x28) and SetRenderState (+0x50) on it, D3D_PresentFrame EndScene (+0x18), D3D_Shutdown releases it (+0x8). The video trace attaches to it. |
| 0x006a00b4 | ColorLevelRed | 256 red contributions (already shifted to bits 16-23) indexed by an 8-bit red level: the brightness/gamma table every D3D_Draw* colour goes through |
| 0x005dd340 | ColorLevelGreen | 256 green contributions (bits 8-15) indexed by an 8-bit green level; see ColorLevelRed |
| 0x0069fc64 | ColorLevelBlue | 256 blue contributions (bits 0-7) indexed by an 8-bit blue level; see ColorLevelRed |
| 0x0061d82c | rFog | depth-cue table: red contribution indexed by red * 256 + fog level (0..255); with aFog, bFog, gFog |
| 0x0065fc60 | gFog | depth-cue table: green contribution indexed by green * 256 + fog level |
| 0x0059d340 | aFog | depth-cue table: alpha contribution indexed by alpha * 256 + fog level |
| 0x005dd740 | bFog | depth-cue table: blue contribution indexed by blue * 256 + fog level |
| 0x00549d9c | fogThreshold | float: depth-cue start (DpqMin over the viewport edge, set by D3DPoly_StartScene); no fog while it is not below RenderProjectionUnitScale |
| 0x00549d98 | FogYonScale | float: far-plane scale of the depth-cue ratio, from VideoFogYonScale per scene |
| 0x0069fc60 | D3DPolyAlpha | alpha (0xAA000000) the ordering-table walk ORs into PSX packet colours: 0xff opaque, 0x80/0x40/0 by semi-transparency mode |
| 0x006a04bc | TexpageBlendMode | current PSX semi-transparency mode (tpage >> 5 & 3) from DR_TPAGE (0xE1) packets and textured packets |
| 0x029da394 | ScreenWidth | render target width in pixels (D3D_SubmitViewportRect scales PSX x by it / 512) |
| 0x029da398 | ScreenHeight | render target height in pixels (D3D_SubmitViewportRect scales PSX y by it / 240) |
| 0x006a42e8 | SDXJoystick | `this` of the joystick create/update wrappers (0x004ecce0, 0x004ed2c0); SetProperty(DIPROP_RANGE, -1000..1000) in 0x004ecfb0; the update reads a 0x50-byte DIJOYSTATE to +4 and copies button bytes >> 7 to the dword array at +0x6c (0x006a4354); PCInput_BuildActionMask reads +0xec (acquired, 0x006a43d4), lX (0x006a42ec) and lY (0x006a42f0) against +-500, and the button array through the joypad bindings. |

The colour, fog and blend globals were read from `D3D_DrawGeneralPolygon`
(0x004d68b0) and `D3D_DrawTexturedQuad` (0x004d4bf0): every vertex colour is
`ColorLevelRed[r] | ColorLevelGreen[g] | ColorLevelBlue[b] | alpha`, after the
depth cue `rFog[r*256+i] | gFog[g*256+i] | aFog[a*256+i] | bFog[b*256+i]` with
`i = ftol(min((viewport[0xe] / rhw) / (viewport[0xa] * FogYonScale) -
fogThreshold) / (RenderProjectionUnitScale - fogThreshold), 1) * 255)` when
`fogThreshold < RenderProjectionUnitScale` and the ratio reaches the
threshold (disassembly 0x004d6a89-0x004d6b60). aFog and bFog, fogThreshold
and FogYonScale were provisional names (PARTYMOD); the code confirms them.

## Video (Phase 3.4, `video=sdl`)

Reviewed 2026-09-27 from the decompilation of WINMAIN_InitDirectDraw,
WINMAIN_InitDirect3D, WINMAIN_SwitchResolution, WINMAIN_SwitchRenderer,
D3D_PresentFrame, D3D_ClearBuffers, D3D_Shutdown, D3D_DrawPolygonList,
D3DPoly_StartScene and D3DTex_Init, and Ghidra references to each address.

| Canonical | Name | Basis |
|---|---|---|
| 0x004d40d0 | D3DPOLY_EndScene | `if (D3DSceneActive) { D3DSceneActive = 0; _controlfp(0x20000, 0x30000); if (D3DSoftwareRenderer) <software flush>; _controlfp(0x9001f, 0xfffff); }`; D3D_PresentFrame calls it first. Provisional name (PARTYMOD) confirmed. |
| 0x005015a6 | _controlfp | `mov eax, [esp+8]; and eax, 0xfff7ffff; ... call _control87`: the MSVC runtime `_controlfp`, which masks `_EM_DENORMAL` out of the mask. D3D_DrawPolygonList brackets its walk with `(0x20000, 0x30000)` (24-bit precision) and `(0x9001f, 0xfffff)`. |
| 0x004cf420 | WINMAIN_BuildDisplayModeList | guarded by the byte 0x0056e7b8; calls WINMAIN_EnumDisplayModes, then stores the names "320x240" .. "1280x1024" that WINMAIN_CanDoMode accepts into 0x0056e788[]. First call in WINMAIN_SwitchResolution. |

| Canonical | Name | Basis |
|---|---|---|
| 0x029da3a8 | DirectDraw | DirectDrawCreateEx out-parameter in WINMAIN_InitDirectDraw; SetCooperativeLevel (+0x50) with 0x11, SetDisplayMode (+0x54), CreateSurface (+0x18) of the primary; also CreateSurface in D3DTex_UploadTexture, D3DSprite_Create/BuildTiles and CreateBinkSurface; released last by D3D_Shutdown(1). |
| 0x029da3ac | D3DPrimarySurface | CreateSurface out-parameter (caps 0x2218: primary, flip, complex, 3D device; one back buffer) in WINMAIN_InitDirectDraw and WINMAIN_SwitchResolution; Flip (+0x2c) in D3D_PresentFrame; D3D_SaveScreenshot saves it; D3D_Shutdown(1) releases it and clears D3DBackBuffer. |
| 0x029da3b0 | D3DBackBuffer | GetAttachedSurface (+0x30) out-parameter with DDSCAPS_BACKBUFFER; IDirect3D7::CreateDevice target in WINMAIN_InitDirect3D; locked by D3D_LockBackBuffer. |
| 0x029da3a0 | D3DSoftwareRenderer | WINMAIN_SwitchRenderer writes `mode + 1` for renderer 8 and 0 for renderer 4; D3DPoly_StartScene, D3D_DrawPolygonList, D3D_PresentFrame and the D3D_Draw* functions take the software path while it is nonzero. |
| 0x029da380 | DisplayVideoMemory | written from GetAvailableVidMem (+0x5c) in WINMAIN_InitDirectDraw, doubled when the device name contains "Voodoo Rush"; only WINMAIN_CanDoMode reads it. |
| 0x006a052c | D3DTextureCaps | WINMAIN_InitDirect3D: +0 = dwTextureCaps bit 1 (power of two), +4 = bit 5 (square only), +8/+0xc = 0x100, +0x10/+0x14 = the device's minimum texture width/height (the software renderer: 1, 1, 0, 1); read by D3DTex_UploadTexture, D3DSprite_BuildTiles, CreateBinkSurface. |
| 0x006a04c4 | D3DSceneActive | D3DPoly_StartScene: `if (!byte) { byte = 1; ...BeginScene... }`; D3DPOLY_EndScene clears it; D3D_DrawPolygonList returns at once while it is 0. |
| 0x006a04c5 | D3DSceneListDrawn | written 0 by D3DPoly_StartScene and 1 by D3D_DrawPolygonList; no reads. |
| 0x0056e7cc | DisplayModeIndex | WINMAIN_SwitchResolution(1) indexes `*(char **)(0x0056e7d4) [index]` (0x0056e7d4 is set to 0x0056e788 at 0x004cdfec) and copies it to 0x029da37c, which nothing reads. |
| 0x0056e788 | DisplayModeNames | the name table WINMAIN_BuildDisplayModeList fills; WINMAIN_SwitchResolution cuts the name at `strlen + 1 - 5` and parses the width with `atoi`, then the height `atoi` just past the cut. |

The `video=sdl` adapters (`src/hooks/hal/video_hal.cpp`) keep every value
above as retail leaves it, except that no display mode is set and the
primary and back buffer are one offscreen system-memory surface.

### DirectDraw surfaces the retail code still uses (Phase 3.4e)

Reviewed 2026-09-27 from the `llvm-objdump` disassembly of every function
with an `IDirectDraw7`/`IDirectDrawSurface7` call site in
`re/config/hal_boundary.yml`, and of `_BinkDDSurfaceType@4` in binkw32.dll.
Ghidra's decompilation of D3DSprite_BuildTiles loses its stack frame; the
arguments below are from the instructions.

Under `video=sdl` the functions that create DirectDraw, the flipping chain,
the z-buffer and the device (WINMAIN_InitDirectDraw, WINMAIN_InitDirect3D,
WINMAIN_SwitchResolution, WINMAIN_SwitchRenderer, D3D_PresentFrame) are
replaced, so no retail code that still runs calls SetCooperativeLevel,
SetDisplayMode, GetAttachedSurface, AddAttachedSurface,
DeleteAttachedSurface, GetDeviceIdentifier, GetAvailableVidMem or
QueryInterface. What it does call:

| Caller (canonical) | Calls |
|---|---|
| D3DTex_UploadTexture 0x004d8f10 | CreateSurface of a staging surface: flags 0x1007 (CAPS, HEIGHT, WIDTH, PIXELFORMAT), record +0x14/+0x16 as width/height, the format at 0x006a04d8, caps 0x1800 (TEXTURE, SYSTEMMEMORY); fills it (below); when record +0 is 0, CreateSurface of the texture into record +0: flags 0x1007, caps 0x1000 (TEXTURE), caps2 0x10 (TEXTUREMANAGE), size rounded to powers of two when D3DTextureCaps+0, made square when +4, the declared size (and a log line) when above +8/+0xc, raised to +0x10/+0x14; that size is stored at record +0x18/+0x1a; then Blt(texture, dest, staging, NULL, 0x01000200 = WAIT, ASYNC, NULL) with dest {0, 0, width, height} when argument 4 is nonzero and NULL (the whole texture: a stretch) when it is 0; Release(staging). |
| D3DTex_UploadRgbPixels 0x004d9310 | GetPixelFormat (DDPIXELFORMAT, dwSize 0x20) for the channel masks; Lock(NULL, 0x7c, 0x820 = WRITEONLY, NOSYSLOCK); Unlock(NULL). |
| D3DTex_UploadIndexedPixels 0x004d9630 | Lock(NULL, 0x7c, 0x820); Unlock(NULL). |
| D3DTex_GrayTexture 0x004d9f50 | GetPixelFormat (0x20); Lock(NULL, 0x7c, 0x820); Unlock(NULL) (hardware path). |
| D3DTex_CountUsedPaletteEntries 0x004da260 | Lock(NULL, 0x7c, 0x810 = READONLY, NOSYSLOCK); Unlock(NULL) (hardware path). |
| D3DTex_SaveSurfaceAsBMP 0x004d7e10 | GetPixelFormat (0x20); Lock(0x7c, 0x810); Unlock. Reached from D3DTex_UploadTexture only when the dump switch 0x006a0544 is set, and from D3D_SaveScreenshot on D3DPrimarySurface. |
| D3DTex_DestroyRecord 0x004d8b50, D3DTex_LoadIndexedBitmap 0x004d9950 | Release. |
| D3DSprite_Create 0x004d74a0 | Hardware path: D3DTex_LoadIndexedBitmap, then D3DSprite_BuildTiles. The screen-sized staging surface, Lock 0x810 and 1555-to-565 copy happen only while D3DSoftwareRenderer is set. |
| D3DSprite_BuildTiles 0x004d83a0 | `(record, &tiles, &rects)`. GetSurfaceDesc(record +0, DDSURFACEDESC2 with dwSize 0x7c): the only GetSurfaceDesc in the executable. A surface within the maximum texture size (D3DTextureCaps+8/+0xc, 256x256) becomes the single tile. A larger one: CreateSurface with the descriptor GetSurfaceDesc returned, caps changed to 0x1800 and caps2 to 0; Blt(staging, NULL, record +0, NULL, 0x01000200); Release(record +0); ceil(width/256) columns by ceil(height/256) rows of 0x2c-byte tile records and 16-byte rects, column-major; each tile a CreateSurface with flags 0x1007, the rect's size rounded like D3DTex_UploadTexture (no maximum check), the format at 0x006a04d8, caps 0x1000, caps2 0x10, stored at tile +0x18/+0x1a; Blt(tile, NULL, staging, &rect, 0x01000200, NULL): a stretch whenever the rect is not already a power of two (the bottom row of a 480-line image: 224 lines onto 256); Release(staging). |
| D3DSprite_Destroy 0x004d78c0 | Release of each tile surface; SetTexture(0, NULL) on the device when a Release returns 1. D3DSprite_Create does the same when releasing the record's surface. |
| CreateBinkSurface 0x004e6020 | While BinkMovieInTexture: CreateSurface of BinkMovieSurface from the descriptor at 0x006a6b18 (0x1007, movie size rounded to powers of two when D3DTextureCaps+0, format 0x006a04d8, caps 0x1000, caps2 0x10), BinkDDSurfaceType of it (0 or -1: log, Release, fail); above 256 in either direction (hardware, `0x006a6964 == 0`) two halves 0x006a69c8 and 0x006a6998 with the same flags. Otherwise BinkDDSurfaceType(D3DPrimarySurface). |
| NextBinkFrame 0x004e6890 | Lock(NULL, desc 0x006a6b18 or a local, 0x821 = WAIT, WRITEONLY, NOSYSLOCK) of BinkMovieSurface, D3DPrimarySurface or the two halves; Restore only after DDERR_SURFACELOST; Unlock(NULL). |
| CloseBinkMovie 0x004e6da0 | Release(BinkMovieSurface). |
| DrawFullScreenMovie 0x004e5a80 | Unlock(D3DBackBuffer) (software path), device SetTexture/DrawPrimitive, Flip(D3DPrimarySurface, NULL, DDFLIP_WAIT). |
| D3D_LockBackBuffer 0x004d0a60 | Lock(D3DBackBuffer, NULL, 0x7c, 0x821); on DDERR_SURFACELOST D3D_RestoreSurfaces and again; returns lpSurface (+0x24), width (+0xc), height (+8), lPitch (+0x10). |
| D3D_ClearBuffers 0x004d0ec0 | Blt(NULL, NULL, NULL, 0x01000400 = WAIT, COLORFILL, DDBLTFX{dwSize 0x64, dwFillColor 0xff000000}) on D3DPrimarySurface, then D3DBackBuffer (the same surface under `video=sdl`). |
| D3D_RestoreSurfaces 0x004d0dd0 | RestoreAllSurfaces, then the same two colour fills. Reached only after DDERR_SURFACELOST. |
| D3D_Shutdown 0x004d0f80 | Releases the device, the two colour fills; with argument 1 Release(D3DPrimarySurface), Release(0x029da3b4), Release(0x029da3bc), Release(DirectDraw). A nonzero count from the primary or DirectDraw logs and ends in `exit(count)`: under `video=sdl` with real DirectDraw that is exit code 12, the surfaces the game never released each holding a reference on DirectDraw. |
| WINMAIN_EnumDisplayModes 0x004f7a70 | EnumDisplayModes(0, DDSURFACEDESC2 with dwSize 0x7c and dwFlags 0, context 0x029d8478, callback 0x004f79e0). The callback keeps at most 0x40 modes of 16 bits per pixel no larger than 1024x768 (its pixel-format flag test `(flags \| 0x40) != 0` always passes), copying each 0x7c-byte descriptor. Called from WINMAIN_BuildDisplayModeList (0x004cf420), which WINMAIN_SwitchResolution (the `video=sdl` adapter too) and 0x004cf0c4 call. |

Every surface descriptor in the executable is a DDSURFACEDESC2: 23 sites,
all `rep stosd` of 0x1f dwords with dwSize 0x7c. There is no 0x6c
(DDSURFACEDESC) descriptor anywhere.

`_BinkDDSurfaceType@4` (binkw32.dll 0x10007b80) calls only GetPixelFormat
(vtable +0x54) with a zeroed DDPIXELFORMAT of dwSize 0x20, ignores the
result and maps: FourCC 'UYVY' 0xe, 'YV12' 0xf, 'YUY2' 0xd; 8 bits 0; 24
bits 1 or 2 by the red mask; 32 bits 3/4 (no alpha) or 5/6 by the red mask;
5-6-5 0xa; alpha 0x8000 with 5-5-5 8; 5-5-5 9; 0xfc00/0x3e0/0x1f 0xb;
0xfc00/0x3f0/0xf 0xc; alpha 0xf000 with 4-4-4-4 7; anything else -1.
CreateBinkSurface treats 0 and -1 as failure. The texture format D3DTex_Init
chooses (ARGB1555) gives 8, the 5-6-5 primary 0xa.

## Movie (Phase 3.5, `movie=sdl`)

Reviewed 2026-09-27 from the decompilation of PCMovie_PlayStartupMedia,
StartBinkMovie, CreateBinkSurface, NextBinkFrame, CloseBinkMovie,
BinkMovieUpdate, PCMOVIE_StartFMV/StopFMV/Update/StartMusic/XAStop/Pause and
PCMOVIE_StreamThread, and the Bink headers of every `Movies/*.dat` (320x240:
logos, intro, GrayMat, the skater videos; 176x48: the level-select clips;
`Music/*.dat`: 4x4, audio only).

| Canonical | Name | Basis |
|---|---|---|
| 0x004e41d0 | PCInput_FirstKeyDown | `for (k = arg; k <= 0xed; ++k) if (KeyboardState[k & 0xff] & 0x80) return k; return 0;` No platform calls. PCMovie_PlayStartupMedia calls it with 0 after every frame. |
| 0x004e4260 | PCInput_FirstJoystickButton | `for (i = 0; i < 32; ++i) if (SDXJoystick[+0x6c + 4*i]) return i; return -1;` (the button flag array PCInput_BuildActionMask reads). PCMovie_PlayStartupMedia skips the movie on a key or button pressed after everything held at its start was released. |
| 0x004e4290 | Input_ClearPadTriggers | `Pad_ClearTriggers(&InputActionStates); Pad_ClearTriggers(0x0056b164)` (disassembly: two `push imm; call 0x00489670`); the last call of PCMovie_PlayStartupMedia. |

| Canonical | Name | Basis |
|---|---|---|
| 0x006a6ce4 | BinkMovie | `BinkOpen` result stored by StartBinkMovie (logs `Bink Error %s File: %s` when 0); every BinkDoFrame/BinkCopyToBuffer/BinkNextFrame/BinkGoto in NextBinkFrame, BinkService/BinkWait in PCMovie_PlayStartupMedia and PCMOVIE_StreamThread, BinkSetVolume/BinkSetPan in PCMOVIE_SetXAVolume, BinkPause in PCMOVIE_Pause use it; CloseBinkMovie `BinkClose`s and zeroes it. One handle for the movies, the clips and the music. |
| 0x006a6ce1 | BinkMovieInTexture | StartBinkMovie clears it, then sets it only on the path taken when `D3DTextureMode == 0` or the movie has fewer than 200 lines (after the texture-mode path returned with a centred 640x480 destination); CreateBinkSurface then creates BinkMovieSurface (caps `DDSCAPS_TEXTURE`, `DDSCAPS2_TEXTUREMANAGE`, the texture pixel format, power-of-two sizes when D3DTextureCaps says so) instead of reading the type of D3DPrimarySurface; NextBinkFrame locks BinkMovieSurface when it is set and D3DPrimarySurface otherwise; CloseBinkMovie releases the surface only when it is set. |
| 0x006a6ce8 | BinkMovieSurface | IDirectDraw7::CreateSurface out-parameter in CreateBinkSurface; `BinkDDSurfaceType` of it; stored as the surface of the texture record at 0x006a6b98 that DrawFullScreenMovie binds; Lock (+0x64) / Unlock (+0x80) in NextBinkFrame; Release (+0x8) in CloseBinkMovie. |

Why the full-screen movies were not visible under `video=sdl`: in texture
mode (`D3DTextureMode != 0`, the case under Wine and on the tested installs)
StartBinkMovie opens a 320x240 movie with `BinkOpen(..., 0x41000000)`
(BINKIOSIZE | BINKCOPY2XWH: copied doubled to 640x480) and leaves
BinkMovieInTexture clear, so NextBinkFrame copies each frame into
D3DPrimarySurface at `((ScreenWidth - 640) / 2, (ScreenHeight - 480) / 2)`.
Retail's primary is the visible front buffer; `video=sdl`'s is an offscreen
system-memory surface.

## Audio (Phase 3.5, `audio=sdl`)

Reviewed 2026-09-27 from the disassembly of Audio_Init (0x004f27c0) and the
decompilation of every pc\soundlib.cpp function that uses the device.

| Canonical | Name | Basis |
|---|---|---|
| 0x029d68c0 | AudioCaps | Audio_Init: `mov ecx, 0x18; xor eax, eax; mov edi, 0x029d68c0; push 0x029d68c0; rep stosd; ...; mov dword [0x029d68c0], 0x60; ... call [ecx+0x10]` (IDirectSound::GetCaps); no other reference in the image. |

Audio_Init takes the game window as its only argument (`mov edx, [esp+0xc]`
after two pushes, passed to SetCooperativeLevel with DSSCL_EXCLUSIVE = 3),
then clears AudioSoundIdMap (0x190 dwords at 0x029d7d20) and
AudioBankRecords (0x500 dwords at 0x029d6920).
