# Statically linked libraries and the scope map (4.12 Cartography)

Status: reviewed 2026-10-07. Canonical build `executables.thps2_pc`. Inputs: `scripts/match_library_signatures.py`
(`re/xref/library-matches.yml`), `re/symbols/scope.yml`, `re/xref/attribution.yml`, `re/xref/milestones.yml`;
policy in `docs/phases/PHASE_4_SUMMARY.md` ("Scope and the completion target").

## C runtime: byte equality with the VC6 libraries

Ghidra's Function ID needs a hand-built FidDb per library; the toolchain in `.tools/vc6` already contains the
exact objects, so the matcher does the same job directly. Each code section of `LIBC.LIB` and `LIBCMT.LIB`
(VC6 SP3, linker 6.00.8447; sha256 in `library-matches.yml`) becomes a blob, every field a relocation patches
(`DIR32`, `REL32`, `DIR32NB`) is a wildcard, and an executable function matches when it equals the blob at every
other byte (the function may be longer by alignment padding only; blobs under 16 bytes are ignored).

| Result | |
|---|---|
| Executable functions matched | 354, all inside `0x004ffd83..0x005122b1` (no match anywhere else, so no false positive outside the runtime) |
| Names unambiguous | 316 (the other 38 are byte-identical siblings, e.g. `__fgetchar` / `__fgetwchar`); applied as `inferred` `CRT_*` names by `tony xref apply` |
| Linked runtime | **`LIBCMT.LIB` (multithreaded, static)**: 109 matches exist only there (`_CallSETranslator`, `__beginthread`), 1 only in `LIBC.LIB`, 244 in both |
| Unmatched functions between matched ones | about 40 (large formatted-output, stdio and locale functions): the retail build links a later service pack's variant of the same objects; their position between matched neighbours places them in the runtime |

`0x004ffd3a..0x005122b1` (405 functions, 64,542 bytes) is tagged `crt` in the scope map. The two functions at
`0x004f8630` and `0x004f86f0` are `pc\winmain.cpp` (`WINMAIN_DirectDrawEnumCallback`, `WINMAIN_ShutDown`), not runtime.

### Runtime functions game code calls (what must keep retail behaviour)

53 runtime entry points are called from outside the runtime and zlib ranges (`import thunks` excluded). The
ones whose behaviour the game can observe, and so need a native equivalent in a standalone build:

| Entry point | Used by | Why it is observable |
|---|---|---|
| `__ftol` (`0x005004f4`) | 90+ call sites: physics, camera, soundlib, d3dpoly, netgame, utils, fonttools | float to int truncation under the x87 control word; out-of-range values give `0x80000000`. Reproduced in native render code (4.6); every native that converts a float must use the same truncation. |
| `_controlfp` (`0x005015a6`) | d3dpoly, winmain and 6 others | the 24-bit precision word the render code sets and restores around the ordering-table walk (`_controlfp(0x20000, 0x30000)`) |
| x87 intrinsics `__CIsin`, `__CIcos`, `__CIatan`, `__CIacos`, `__CIasin`, `__CIlog`, `__CIlog10`, `__CIpow` (`0x00500db0`, `0x00501740`) | `utils.cpp`, `d3dpoly.cpp` and 6 callers in unattributed code | `fsin`/`fcos`/`fpatan`/`fyl2x` results can differ in the last bit from a host `libm`; native code that feeds them into game state needs an x87-faithful version (or a recorded tolerance) |
| `_sprintf` (`0x005002a1`), `__vsnprintf`, `_sscanf` | about 300 sites (competition 35, front 36, mmu 24, panel 13, ...) | text formatting and parsing: `ScreenKit::retail_format` covers `%s`, `%d`, `%6d`; any other conversion (floats, `%x`) must be added with its call site |
| `_strtok`, `_strncpy`, `_strncmp`, `_strchr`, `_strrchr`, `__stricmp` (`CRT_StrICmp`, native), `__strupr`, `_toupper`, `_isdigit`, `_isspace`, `__mbschr`, `__mbsrchr` | netgame, pcmovie, d3dtex and `?` | C-locale behaviour; the host runtime gives the same results |
| `_fopen`, `_fclose` (`ResourceStream_Close`, native), `_fread`, `_fwrite`, `_fseek`, `_ftell`, `__stat`, `__findfirst`/`__findnext`/`__findclose`, `__getcwd`, `_remove`, `__splitpath` (`CRT_SplitPath`, native) | file I/O backends, saves, `mmu.cpp`, `thstub.cpp` | file semantics; native equivalents exist for the stream wrappers (4.5) and the rest are ANSI/POSIX-equivalent apart from path separators |
| `_time`, `_localtime`, `_asctime`, `_clock` (`systemclock`) | `?` (save stamps, the frame limiter) | wall-clock values; HAL clock for the limiter |
| `_malloc`, `_free`, `operator new`/`delete`, `_atexit`, `_onexit`, `_exit`, `__beginthread`, `__chkstk`, `__amsg_exit`, SEH and C++ EH frame handlers | everywhere | allocation, start-up and unwinding: the toolchain's; nothing the game can see (the HAL registers its shutdown with `_onexit`, `docs/HAL.md`) |

## zlib 1.1.2

The executable carries both copyright strings (`deflate 1.1.2 Copyright 1995-1998 Jean-loup Gailly` at
`0x0051b517`, `inflate 1.1.2 Copyright 1995-1998 Mark Adler` at `0x0051c060`), and `0x004f9c80..0x004ff930+925`
(50 functions, 21,112 bytes) is a contiguous block whose functions reference zlib's own messages (`incorrect
header check`, `invalid block type`, `invalid distance code`, `oversubscribed dynamic bit lengths tree`,
`buffer error`, `insufficient memory`, `1.1.2`). The block is called only by the PKR2 wrappers
(`PKR2_*Zlib`, game code) and the allocator defaults `0x004fbb20`/`0x004fbb40` (`hal_boundary.yml` records the
`zalloc`/`zfree` call sites). Both directions are linked: `deflate()` (733 bytes), `inflate_blocks`
(3,473), `inflate_codes` (2,287), `huft_build` (1,211), `inflate_fast` (925).

A byte-level match would need the exact compiler flags: zlib 1.1.2 (fetched from zlib.net/fossils, sha256
`4fca0daaa58fa3b0be5c05443db7435667a01d4218b0db453b879cf6b1df97b9`) was compiled with the repository's VC6 at
`/O2 /Gy`, `/O1`, `/Ox`, `/O2 /G6`, `/O2 /G5`, plain `/O2` and `/Od`, and none of the object files equals the
retail code (sizes are within 5% but not equal: `inflate_blocks` 3,344 against 3,473 bytes). The tag therefore
rests on the strings, the call structure and the function set, not on a signature match; the range is
`0x004f9c80..0x004ffd39`, ending where the first runtime function (a 45-byte function at the 2-byte-aligned
address `0x004ffd3a`; zlib's objects are 16-byte aligned) begins. `inflate` output does not depend on the
version, so upstream zlib replaces it; recheck deflate if compressed output is ever compared byte for byte.

## What the HAL owns

`re/config/hal_boundary.yml` (generated inventory, checked by `tony hal check`) lists 284 functions that touch
the platform: 57 hook points, 83 owned helpers, 33 multiplayer, 6 dead, 105 runtime. `scope.yml` tags a hook
point or owned helper `platform-hal` (replaced by the adapter in `src/hooks/hal` and the service behind it)
except where the adapters call the retail function in place: `native_hooks.yml` `services.hal.symbols` lists 24
retail functions (`PCInput_BuildActionMask`, `PCInput_ResetBindings`, `PCInput_LoadBindings`,
`Options_ResetControllerDefaults`, `SDXMouse_SetBounds`, `Input_UpdateActionStates`, ...). Those keep their
retail logic and stay in scope. The movie hook points (the pcmovie interface the game calls) stay in scope; the
Bink bodies they own are `bink` (nine functions; six are called in place by the movie adapter until the HAL
movie backend exists).

Two ranges are tagged `platform-hal` as a whole because every function is the same backend: the SDX DirectInput
wrapper `0x004ebf90..0x004ed950` (51 functions, including the unreferenced dead methods; `SDXMouse_SetBounds`
is kept) and the SOFTREND software renderer `0x004edbb0..0x004f27b0` (41 functions: scanline fills, RGB565
conversion; the decompile of `0x004edce0` converts floats to RGB565 and sorts polygon edges, `SOFTREND_Startup`
at `0x004f2720` clears its tables; reached only through `D3DSoftwareRenderer` paths in `d3dpoly`/`pcmovie`,
which the SDL3 backends do not implement). In total 181 functions, 63,728 bytes.

Functions in `pc\d3d*.cpp`, `pc\soundlib.cpp` and `pc\winmain.cpp` that no hook point owns stay **in scope**
(texture glue the front end calls: `D3DTex_LoadBitmap`, `D3DTex_PrepareTexture`, `D3DTex_AttachToSceneMaterial`;
`WINMAIN_BuildDisplayModeList`, `WINMAIN_LoadOptions`). 4.13a triages them; moving one out needs an entry in the
scope map with its reason.

## The netgame query surface

The range `0x004dae60..0x004e2060` between `dxplay` and the GTE library holds 141 functions (the plan said the
transport and the surface were not separable by address; they are not, but they are separable by use):

| Class | Functions | Rule | Scope |
|---|---:|---|---|
| HAL `out_of_scope` (DirectPlay COM calls) or members of `pc\dxplay.cpp` | 37 | the inventory, the unit | `directplay` |
| netgame internals | 28 | no caller outside the range, reads the netgame state (`0x006a1000..`, `0x006a3a00..`) | `directplay` |
| surface: externally called and gated by the flag `0x006a3b21` | 22 | e.g. `FUN_004de040` returns `flag ? host != 0 : false`, `FUN_004de5c0`, `FUN_004df250`; with the flag clear every one is a no-op or a constant | in scope, 4.13b, native with single-player answers |
| externally called, reads net state but not gated | 19 | `NETGAME_SetupPlayer` (`Skater_RegisterPlayer`), the netgame start-up `0x004dd890` (called from winmain), `FUN_004de320` (`NETGAME_SaveCareerData`) | in scope, 4.13b: read each; the flag-clear path is what runs |
| game code that only lives at a PC address | 11 | `Pack_NewFree`, `Mess_DrawText`, `MENUPC_DrawMouseCursor`, `MySkater__GetSkinToneFileName`, the mouse position tracker `0x004dbee0` | in scope: 4.13a (mouse and input), 4.10i (`Mess_DrawText`) |
| internal, reads no net state | 24 | small helpers | in scope, 4.13b: confirm each is not transport |

**`0x004dae00` is not network code**: it is the frame-rate meter (a countdown at `0x006a05c0`; every 60 calls it
stores `k1 / (k2 / (elapsed ticks * k3))`, elapsed = `0x006a05ac - 0x006a05b4`, at `0x0054a218`), called by every
screen's run loop and the level loop. It belongs to 4.13a.

**Local two-player play.** The flag `0x006a3b21` is read by `front.cpp` (`Front_Init`, `Front_Update`, one more),
`physics.cpp` (`Skater_PhysicsFrame`, `CBruce__HandleTimer`) and five functions in the `multisel`/`netmenu`
runs (its writers were not traced); with it clear `Skater_RegisterPlayer` only normalises `0x006a3d14`. So split-screen play,
if the PC build has it, runs with the flag clear and does not need DirectPlay. This is a static reading; the
retail confirmation was made in 4.10f3: the frontend harness (`re/gdb/probes/frontend_netflag.py`: the 4.10a trace probe plus a watch on the byte at
`0x006a3b21`, both pads' connected words forced on and the next-screen variable `0x00561c78` set to 9) parked the game on `MULTI_SELECT` (screen 9, `multisel.cpp`), pressed
Enter and followed it into `SKATER_SELECT`; the flag read **0 on every input tick, 1 to 420**, before, during and after the screen. The wheel's other entries do not
reach it either (entry 0, which wants two pads, opens `NETWORK_MENU` (screen 21); 1 and 2 `SKATER_SELECT`; 3 `CAREER_SELECT`; 4 `OPTIONS`; 5 `LEVEL_EDITOR`; 6
`CREATE_SELECT`), and none of those set the flag either. So the split-screen path does not set the netgame flag and the DirectPlay side stays out of scope for it.

## Attribution and sizing

`scripts/neighbour_scan.py` lists the 181 runs of functions no assert path attributes (1,758 functions, 418,228
bytes before this milestone). Its attribution proposals:

* `interior` (both neighbours are one file): 123 functions.
* `layout`: a run between two files is split where the callers and the data statics point from one file to the
  other. A hold-out (hide an attributed window around each of 64 known file boundaries, re-derive it) was right 60
  times (94%); a first version that also dropped callers of widely used functions was right 17 times out of 21.
* Everything else (the runs the evidence cannot place) stays unattributed and is assigned to a sub-milestone by
  its neighbours in `milestones.yml`, with the reason in the row.

Function and byte counts per sub-milestone are produced by `tony xref coverage`; `docs/phases/PHASE_4_SUMMARY.md` has
the table. Natives inside a tagged range (`CRT_SplitPath`, `CRT_StrICmp`, `ResourceStream_Close`,
`PKR2_SeekResource`, `PKR2_ReadResource`, the eleven `d3dfunc` natives) are in scope by definition.
