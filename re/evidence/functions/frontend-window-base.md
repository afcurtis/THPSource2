# Frontend window base (screentools.cpp, textwin.cpp)

Status: native port of the window base, replayed against retail forced calls and against natural main-menu frames
(Phase 4.10a; method in [frontend-text-layer.md](frontend-text-layer.md#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/frontend_window.{hpp,cpp}`; fixtures `re/evidence/fixtures/frontend-calls-window.txt`,
`frontend-calls-window-elements.txt` (forced calls) and `frontend-calls-trace-{mainmenu,careerselect}.txt` (natural frames)
Capture tooling: `re/gdb/probes/frontend_calls.py` + `frontend_specs_window.py` (forced calls) and
`re/gdb/probes/frontend_trace.py` + `scripts/make_frontend_trace_fixture.py` (natural frames; see
[frontend-capture-harness.md](frontend-capture-harness.md))

Every frontend screen is a tree of these objects. A **ScreenElement** has a target position and slides toward it a
quarter of the remaining distance per frame; a **Window** holds up to 32 children and passes update, draw and "snap" to
them; a **TextScreenElement** draws a string with a Font; a **SpriteScreenElement** draws a sprite. Menus, select
screens and the options pages are built from them (4.10c onwards), so this is the base every later screen replays
against.

## Objects

ScreenElement (0x28 bytes): `+0x00` vtable, `+0x04` word, `+0x08, +0x0c` target x, y, `+0x10` draw argument (passed on
to the sprite or font), `+0x14` hidden flag, `+0x18` delay in frames, `+0x1c` arrived flag (byte), `+0x20, +0x24` current
x, y. Window: `+0x28` 32 child pointers, `+0xa8` child count. TextScreenElement: `+0x28` its own copy of the string,
`+0x2c` the font. SpriteScreenElement: `+0x28` the sprite object (0x5c bytes, the engine's).

Virtual methods (vtable slot times four): 0 deleting destructor (flag), 1 update, 2 draw (x, y), 3 returns -1, 4 nothing,
5 snap x, 6 snap y, 7 per-frame hook (called first by `ScreenElement_Update`). The four classes' vtables are at
`0x005196a8`, `0x005196c8`, `0x005196e8` and `0x00519708`; every destructor ends by resetting the vtable to the root class
(`0x005184b4`).

## Functions

### Screen elements, windows, text and sprite elements (31 functions, 1405 retail calls)

| Address | Name | Retail calls replayed |
|---|---|---:|
| `0x004ac430` | `ScreenElement_Construct` | 25 |
| `0x004ac490` | `ScreenElement_Delete` | 25 |
| `0x004ac4b0` | `ScreenElement_Destroy` | 25 |
| `0x004ac4c0` | `ScreenElement_NoResult` | 25 |
| `0x004ac4d0` | `ScreenElement_Update` | 105 (80 natural) |
| `0x004ac560` | `ScreenElement_Nothing` | 25 |
| `0x004ac570` | `ScreenElement_SnapX` | 25 |
| `0x004ac580` | `ScreenElement_SnapY` | 25 |
| `0x004ac590` | `ScreenElement_GetX` | 25 |
| `0x004ac5a0` | `ScreenElement_GetY` | 25 |
| `0x004ac5b0` | `Window_Construct` | 25 |
| `0x004ac5f0` | `Window_Delete` | 25 |
| `0x004ac610` | `Window_Destroy` | 25 |
| `0x004ac670` | `Window_AddElement` | 25 |
| `0x004ac730` | `Window_Update` | 178 (153 natural) |
| `0x004ac770` | `Window_Draw` | 262 (237 natural) |
| `0x004ac7c0` | `Window_SnapX` | 25 |
| `0x004ac800` | `Window_SnapY` | 25 |
| `0x004ac840` | `TextScreenElement_Construct` | 25 |
| `0x004ac970` | `TextScreenElement_Delete` | 25 |
| `0x004ac990` | `TextScreenElement_Destroy` | 25 |
| `0x004aca30` | `TextScreenElement_SetString` | 25 |
| `0x004acb50` | `TextScreenElement_Draw` | 105 (80 natural) |
| `0x004acbd0` | `TextScreenElement_Width` | 25 |
| `0x004acbe0` | `SpriteScreenElement_Construct` | 25 |
| `0x004acd60` | `SpriteScreenElement_Delete` | 25 |
| `0x004acd80` | `SpriteScreenElement_Destroy` | 25 |
| `0x004ace20` | `SpriteScreenElement_Draw` | 105 (80 natural) |
| `0x004ace60` | `SpriteScreenElement_Width` | 25 |
| `0x004acec0` | `SpriteScreenElement_Height` | 25 |
| `0x004bad00` | `TextWindow_SetLineColour` | 25 |

Not ported: `0x004afd8c` (see below). The two destructor chains end in `operator delete` (`0x00466bb0`) when the
deleting destructor's flag is odd; that is reported as a service call.

## What the disassembly and the replays found

- **9 of these 31 functions are not in Ghidra's list** (`ScreenElement_NoResult`, `ScreenElement_Nothing`,
  both snap methods of a window, the draw methods and widths of the text and sprite elements, the sprite's height
  and any other that is only reachable through a vtable). The decompile of everything around them was right; the
  vtables were the way in.
- **A slide is a quarter of the distance, or half when that rounds to nothing, and snaps within one unit**; the
  arrived flag is set only when both axes are on target, and is not touched while the delay counts down.
- **A text element with an empty string draws nothing**: `TextScreenElement_Draw` compares its string with a global
  empty string (`0x0055fe10`) and skips the draw. In 500 ticks of the main menu every text element was
  empty: its labels are sprites. That is why the main menu never reaches `Font_Draw`.
- **The element's draw argument (`+0x10`) is the third argument of the sprite's draw**, and the text element passes it as
  the fourth argument of `Font_Draw`.
- **`Window_AddElement` asserts `child != 0` and carries on**: a null child is stored in the next free slot and counted.
  A 33rd child asserts "Too many screen elements in list" and is dropped.
- **`TextWindow_SetLineColour` writes even after its assertions fail** (an out-of-range line writes before the table, a
  colour above 3 is or-ed in as is): it only logs. The replay covers the assertion paths.
- **The BMP loader at `0x004afd8c` is not a function**: Ghidra split the real `Load8BitBMP2` (it starts at
  `0x004afd40`, with the header check `bpp == 8`) at its first branch target, so the "function" at `0x004afd8c` has no caller
  and its decompile is garbage (stack offsets from the wrong frame). It was ported in Phase 4.10i3 ([bitmap-loader.md](bitmap-loader.md), slice `frontend-bitmap-load`).

## Validation

**Forced calls** (the 4.9 pattern, see the text-layer page): windows, elements and fake children are laid out in a scratch
slab, the virtual calls go to stubbed `SFX_*` entry points, and `operator new`, the sprite constructor, the allocator and
the sprite's size accessors are stubbed services. 1405 calls, every written byte and every ordered virtual
call compared, assertion paths included.

**Natural frames** (the trace method of 4.7b, new tooling for 4.10): `re/gdb/probes/frontend_trace.py` parks the game at
the main menu, and records every call of `ScreenElement_Update`, `Window_Update`, `Window_Draw`, `Window_SnapX`,
`Window_SnapY`, `TextScreenElement_Draw` and `SpriteScreenElement_Draw` with the object's bytes on entry and on exit and
the ordered calls it made through virtual methods and the engine, taken at the call instructions inside the original
functions. `scripts/make_frontend_trace_fixture.py` writes them in the forced-call fixture format, so
`frontend_retail_calls_test` replays them with no new test code: **630 natural calls** replay exactly.

## Honest gaps

1. **The natural frames are the main menu and the career select screen** (the two screens the harness reached; the
   frontend-capture-harness page lists them). Select-player, options and the pause menu build the same objects but were
   not walked.
2. **The classes' construction in a running frontend is not replayed**: `TextScreenElement_Construct` and
   `SpriteScreenElement_Construct` are forced calls (services stubbed), not captured creations.
3. **Not generated**: a text or sprite element whose sprite pointer is null (the original faults after its assertion), a
   null string at `TextScreenElement_Construct`.
4. **`Load8BitBMP2`** was ported later, in Phase 4.10i3 (slice `frontend-bitmap-load`, [bitmap-loader.md](bitmap-loader.md)).
