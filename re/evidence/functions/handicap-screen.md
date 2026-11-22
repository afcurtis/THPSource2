# Handicap screen (handicap.cpp)

Status: ported and replayed against retail forced calls (Phase 4.10f3)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Slice: `handicap-screen`. Native code: `src/runtime/handicap.{hpp,cpp}` (methods of `HandicapScreen`, which derives from `ScreenKit`).

Read first: [career-select-screen.md](career-select-screen.md) (the same model), [create-select-screen.md](create-select-screen.md), [menu-toolkit.md](menu-toolkit.md) and
[frontend-window-base.md](frontend-window-base.md).

## What the screen does

`Handicap_Run` (`0x00455f00`) is frontend screen 19 (`MULTI_HANDICAP`), the two-player game's handicap screen. It loads two fonts (`s2font1`, `s2font2`), builds the
window (`0x350` bytes, vtable `0x00518a28`) and runs frames until the window's step answers 1 (both players are ready) or 2 (someone backed out) or the engine asks to
leave; then it deletes the window, unloads the fonts, runs the leak check (`SuperMem_CheckForLeaks`) and logs the two handicaps (`Handicap[0]: %d`, `Handicap[1]: %d`). It
answers `0`, or `-1` after a back.

The two handicaps are the words at `0x0056a2f0` (one per player), each between -5 and 5: a positive one is **added to the skater's stats**, a negative one **subtracted
from them**. Each player has a panel (`HandicapPlayer`, `0x364` bytes, vtable `0x00518a08`) holding a row of ten dots (lit as the handicap plus five, dim while the player
changes it), the skater's face, the title ("Player 1" or "Player 2") and the skater's name (trimmed to fit 240 pixels):

| Panel field | Meaning |
|---|---|
| `+0xac`, `+0x1f8` | two Font objects (`s2font1`: names; `s2font2`: the lines under the dots) |
| `+0x344`, `+0x348` | the two dots sprites (`s2dots.bmp`; the second marked by `0x00457810`) |
| `+0x34c`, `+0x350` | two MenuDots over them (the toolkit's) |
| `+0x354` | the face, an element of the window (the player's `SkaterManager_FaceBitmap`) |
| `+0x358` | the same picture as a sprite the panel draws by hand once the player is ready |
| `+0x35c`, `+0x360` | ready (byte), the player (0 or 1) |

`HandicapPlayer_Poll` (`0x004554f0`): confirm (the player's own pad) sets ready (sound `0x91`); back clears the edge, plays `0x93`, and answers 2 (leave) when the player was
not ready, else un-readies. It answers 1 while ready, else 0. `HandicapPlayer_Update` (`0x00455590`): while not ready, a left edge subtracts one and a right edge adds one
(sound `0x90` each), then `Utils_LimitRange(&handicap, -5, 5)` (the engine) clamps it; both rows of dots follow (`MenuDots_SetRange(handicap + 5, handicap + 5)`).
`HandicapPlayer_Draw` (`0x00455690`): the lit dots and the hand-drawn face when ready (the face element is hidden), otherwise the dim dots; two lines under them ("No change /
to stats", "add %d / to stats" or "subtract %d / from stats"); a ready player blinks (the second of the clock modulo 60 above 30) "Waiting for / Other player" in red; the
fonts go dark grey when ready, light grey otherwise.

`HandicapWindow_Step` (`0x00455e90`): with only one pad connected (`0x0056b2b0` zero) it answers 2 at once. Otherwise both panels are polled: 2 if either answers 2, 1 if both
answer 1, else 0.

## Functions (15)

| Address | Name | Replayed |
|---|---|---|
| `0x00454e80` | `Handicap_InitCallbacks` | yes |
| `0x00454ea0` | `Handicap_ExitCallback` | yes |
| `0x00454eb0` | `Handicap_ScreenCall` | yes |
| `0x00454ec0` | `HandicapPlayer_Construct` | yes |
| `0x00455410` | `HandicapPlayer_ScalarDelete` | yes |
| `0x00455430` | `HandicapPlayer_Destroy` | yes |
| `0x004554f0` | `HandicapPlayer_Poll` | yes |
| `0x00455590` | `HandicapPlayer_Update` | yes |
| `0x00455690` | `HandicapPlayer_Draw` | yes |
| `0x004558c0` | `HandicapWindow_Construct` | yes |
| `0x00455d20` | `HandicapWindow_ScalarDelete` | yes |
| `0x00455d40` | `HandicapWindow_Destroy` | yes |
| `0x00455e10` | `HandicapWindow_Draw` | yes |
| `0x00455e90` | `HandicapWindow_Step` | yes |
| `0x00455f00` | `Handicap_Run` | yes |

What the 4.12 sizing gave this range but is not the screen's: `0x00456090` is `hcull.cpp` (the cull grid), `0x004564b0..0x00456580` an "items" object class, and
`0x00456600..0x00456990` the 2D box primitives the menu toolkit calls (`fill_rect`, `selection_box`, `frame_box`); `milestones.yml` now sends them to 4.15e. The
gamefmv wrappers at `0x00454e50..0x00454e70` stay with 4.13b.

## Validation

Forced calls in the running game (groups `hanpan`, `hanwin`, `hanloop` of `frontend_specs_handi.py`): panels and windows built by hand in the scratch slab (real rows of
dots, fake pool objects whose virtual methods are reported services); the constructors run for real in the arena, the window's building two real panels. The name element
the panel's constructor builds holds the address of the local buffer the name was formatted into: the replay skips those four bytes (`unspecified`), and the font draws of
the lines under the dots are handed stack buffers (recorded as the native code's scratch line with `stack_to`). **The loop's frame waits for the clock with a bare spin on
it, which a forced call cannot satisfy** (nothing ticks the clock while the call runs), so the loop is replayed on first frames that end it: the window's step is a
seam that answers 1 or 2 (the single-pad path, where the real step answers 2, is covered by `HandicapWindow_Step` itself). Fixtures
`re/evidence/fixtures/handi-calls-{hanpan,hanwin,hanloop}.txt`.
