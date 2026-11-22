# Names from PARTYMOD and the THPS2 Cumulative Patch

Status: provisional (third-party identifications; see agreement below)
Build: declarations target `original_2000`; translated via `re/xref/original_2000.yml`
Addresses: `re/xref/community.yml`

## Observation

Pinned sources (`re/xref/config.yml`):

- PARTYMANX/partymod-thps2 `5096a69d` (MIT)
- DCxDemo/THPS2-Cumulative-Patch `06ef5153` (GPL-3.0; names/addresses only)

`tony xref community` collects declarations that bind a name to an address
(function-pointer constants, typed global pointers, `#define`s), including commented-out
ones (flagged). Code addresses must be exact function entries in original-2000;
mid-function patch sites are ignored, as are tool placeholders (`FUN_…`, `unkVal`).

544 declarations (419 functions, 125 globals); 537 translate onto the canonical build.

## Agreement with independent evidence

- 167 community function names equal a real PSX demo symbol name and are used as the
  PSX matcher's anchors (`re/evidence/xref-psx-demo.md`).
- Where a community name lands on a curated OpenTony function the meanings agree
  (e.g. `FileIO_Open`/`FileIO_OpenResource`, `M3d_Render`/`M3D_RenderObjects`,
  `LaunchTheDamnGame`/`Front_LaunchGameLevel`, `Career_Init`/`Career_ResetSaveImage`).
- After unit-consistency filtering one PSX-vs-community disagreement remains:
  `0x004301f0` PSX `Db_Init` vs community `Db_AllocateBuffers` (compatible meanings).

## Open questions / falsifiers

- Community names are the authors' labels, not original symbols; upgrade individually
  when a PSX symbol or a controlled experiment agrees.
- Career globals/functions dominate the Cumulative Patch harvest (65 provisional career names).
