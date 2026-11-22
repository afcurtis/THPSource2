# Multi-player select screen (multisel.cpp)

Status: ported and replayed against retail forced calls (Phase 4.10f3)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slice: `multi-select-screen`. Native code: `src/runtime/multi_select.{hpp,cpp}` (methods of `MultiSelect`, which derives from `ScreenKit`).

Read first: [career-select-screen.md](career-select-screen.md) (the same model), [handicap-screen.md](handicap-screen.md), [menu-toolkit.md](menu-toolkit.md).

## What the screen does

`MultiSelect_Run` (`0x0047c560`) is frontend screen 9 (`MULTI_SELECT`): the list of **two-player games**. It loads two fonts (`mainf`, `s2font2`), builds the window
(`0x238` bytes, vtable `0x00519328`: it loads `multisel.pre` itself and unloads it again at the end of its constructor) and runs frames until the poll answers 1 (a game
was chosen) or 2 (back) or the engine asks to leave. The answer goes to the global `0x00561c6c`: the highlighted row's id, or 2 for back. The function itself answers `0`, or
`-1` after a back.

The list has a scaled title ("2 player games", style with its own colour `0x91, 0x12, 0x0f`) and five rows built from the `mainf` font (centred, grey, scale `0x600`): **Trick
attack** (id 5), **Graffiti** (8), **Horse** (7), **Tag** (9) and **Free Skate** (4), two arrow sprites (`s2arrow9.bmp`, `s2arrow8.bmp`) and three prompt windows (select,
accept, back).

| Window field | Meaning |
|---|---|
| `+0xac` | the background sprite |
| `+0xb0` | a Font object (`s2font2`) |
| `+0x1fc` | the MenuList |
| `+0x200`, `+0x204` | the rows' style and the title's |
| `+0x208` | ten row slots (the title, then the five games) |
| `+0x230`, `+0x234` | the arrow sprites |

`MultiSelect_Poll` (`0x0047c4a0`) takes no `this`: a move (the up or down edge of either pad) plays sound `0x90`; **confirm on either pad** plays `0x91`, clears both
edges and answers 1; back on either pad plays `0x93`, clears both and answers 2; else 0. `MultiSelect_Update` (`0x0047c460`) moves the list (list slots 3, `0x0046f650`'s
pad edge, 5, 4) and updates the window; `MultiSelect_SelectedId` (`0x0047c550`) is `MenuList_HighlightedId` of the list.

**The netgame flag.** The frontend harness (`re/gdb/probes/frontend_netflag.py`) parked the game on this screen, pressed Enter and followed it into `SKATER_SELECT`: the
flag `0x006a3b21` read 0 on every tick (details in [xref-libraries.md](../xref-libraries.md), "Local two-player play"). The two-player games do not touch the network layer.

## Functions (11)

| Address | Name | Replayed |
|---|---|---|
| `0x0047ba30` | `MultiSelect_InitCallbacks` | yes |
| `0x0047ba50` | `MultiSelect_ExitCallback` | yes |
| `0x0047ba60` | `MultiSelect_ScreenCall` | yes |
| `0x0047ba70` | `MultiSelect_Construct` | yes |
| `0x0047c290` | `MultiSelect_ScalarDelete` | yes |
| `0x0047c2b0` | `MultiSelect_Destroy` | yes |
| `0x0047c3e0` | `MultiSelect_Draw` | yes |
| `0x0047c460` | `MultiSelect_Update` | yes |
| `0x0047c4a0` | `MultiSelect_Poll` | yes |
| `0x0047c550` | `MultiSelect_SelectedId` | yes |
| `0x0047c560` | `MultiSelect_Run` | yes |

What the 4.12 sizing gave this range but is not the screen's: the front of it (`0x0047b9d0`, `0x0047ba00`) are two string copies (`mmu.cpp`, 4.14b), and everything from `0x0047c6e0`
on (three item-table initialisers that fill the lobby's `NETWORK`, `HOST`, `JOIN`, `INTERNET`, `HOT SEAT`, `HORSE` rows, `NetMenu_Main` at `0x0047cbb0`, whose window
constructor `0x0047ce60` is 3.5 KB) is `netmenu.cpp`, which `milestones.yml` now sends to 4.10i with the rest of the lobby.

## Validation

Forced calls in the running game (groups `mulwin` and `mulloop` of `frontend_specs_multi.py`): the window built by hand in the scratch slab with real text rows and a fake list,
fake pool objects whose virtual methods are reported services; the constructor runs for real in the arena. **The loop's frame waits for the clock with a bare spin on it, which a
forced call cannot satisfy**, so the loop is replayed on first frames that end it: the poll is a seam that answers 1 or 2 (the poll itself is replayed on its own). Fixtures
`re/evidence/fixtures/multi-calls-{mulwin,mulloop}.txt`.
