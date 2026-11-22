# Goals, game-mode and competition screens (Phase 4.10h)

Source: `competition.cpp` (`0x00419460..0x0041bff3`), `videowin.cpp`'s methods (`0x0045b3a0..0x0045b8a0`), the run after `videowin.cpp` that the 4.12 sizing attributed to `goalwin.cpp`
(`0x0045b950..0x0045e4e7`), `medals.cpp` (`0x0046e550`), `tag.cpp`'s run (`0x004b84c0..0x004b8fb0`) and `tapes.cpp` (`0x004b8fc0`). Native: `src/runtime/goals.hpp` and
`goals_{competition,displays,windows,lights,loading,objects,tag,dispatch}.cpp` (`GoalScreens`, derived from `ScreenKit`).

## What the run really is

The plan called `0x0045bd30..0x0045db40` "`displaygoals` and `goalwin`". Reading it in the objdump shows five unrelated things; the screens of the same name in the plan are in
other files (`displaygoals.cpp`'s `0x0044eac0` is not in this milestone's address range).

| Range | What it is |
|---|---|
| `0x0045b3a0..0x0045b8a0` | the level information window (`VideoWindow`, vtable `0x00518b30`; the constructor is 4.10g's): destructor, **draw** (`0x0045b470`, unlisted), update, the setter and the "may this level's picture be shown" check |
| `0x0045b950..0x0045c360` | the level picture window (`LevelPic`, vtable `0x00518b50`): fourteen level pictures that slide past each other, the arrows, the two lines that say what a locked level needs; **update** (`0x0045be00`) and **draw** (`0x0045c1e0`) are vtable-only |
| `0x0045c390..0x0045c4a0` | the TRG type 500 node's runtime record (`0x58` bytes, vtable `0x00518b70`): it makes a light object while the camera is near and sets its strength by the distance |
| `0x0045c5e0..0x0045cd10` | the light editor window (vtable `0x00518b78`): a scroll list of fourteen rows (ambient, top, middle and bottom red/green/blue and two angles), adjusted by the pad and stored in a light record; **draw** (`0x0045cd10`) is vtable-only |
| `0x0045d010..0x0045d830` | the loading screen: the picture and its lines for two frames, the progress bar, a frame copy |
| `0x0045d830` | `LoadingScreen_Start`: which picture and caption by game mode, level and editor |
| `0x0045db40` | `GoalsScreen_Run`: the goals checklist (`"<level> Checklist"`, a tick per goal got, the goal text and its reward) or the loading wait, with prompt windows; two exits (play, back) |

`competition.cpp` is as planned: the object at `0x0055fe28` (vtable `0x00518738`; ECX is that address in every call), eight participants of `0x14` bytes, five judges of 8, three medal objects
(`+0x108`, `+0x10c`, `+0x110`), the counters of the displays (`+0xa8` the judges' scores, `+0xac` the standings, `+0xb0` the end screen), the marquee text and scroll, and flags (`+0x169` the
ending video is playing, `+0x16a` the prize money was paid). The array elements' constructors and destructors are 16-byte stubs (`0x0041adf0`, `0x0041ae10` are unlisted) that only set a vtable;
the object's constructor and destructor reach the CRT's vector helpers (`0x00500400`, `0x0050030c`) as seams.

## Findings that are not in the code

* **Four contest levels.** The level table flags four levels as contests; `Competition_AllMedalsWon` asserts "too many medals" when more than three of them carry a medal. The medals screen collects
  the medal of every level into a three-word local array, so a record with four medaled levels overruns the array into the saved registers and the return address (the probe found this as a crash:
  `eip = 0xa`, the medal type written over the return address). The generators give a character at most three medaled levels, as the game's own state has.
* **Stack garbage in arguments.** Two display functions pass a register with only its low byte replaced as an argument: `Competition_PrizeMoneyDisplay` hands `Mess_SetRGB` the cash word with its low
  byte replaced by the flashing colour (a read of the argument slot it reuses as a local), and `LoadingScreen_UpdateProgress` hands the bar's fill the elapsed time with its low byte replaced. The port
  passes the same words; the replay compares them.
* **The text of a digit column.** The prize money counts six digits up from the left: a column draws its settled digit with a scale that ramps down to `0x100` in ten frames, and a random digit before
  that; the comma is drawn when the cash is above 999 and a column only when the cash has that many digits.
* **Totals are the two best of three heats.** The standings and the results sum the two best heat scores (the compares in retail are on unsigned words; the port keeps their order).
* **The loading screen's fonts and rectangles are stack locals.** Both screens build `Font` objects in their frames; the probe records a pointer into the frame as the fixed scratch address
  (`stack_to`), the progress bar's rectangle as its eight bytes.
* **A buffer that is too small for its own message.** `Competition_StartDisplay` formats "Player %d go!" / "Player %d get ready!" into a 20-byte local; with a three-digit seat number the
  longer text (21 characters and the terminator) runs two bytes over the return address and the call returns to garbage (a smoke capture found this: `eip = 0x220021`). Seat numbers are 1 to 8
  in the game, so it never shows; the generator keeps them legal.
* **The loop's first pass is special.** `GoalsScreen_Run` calls two engine functions (`0x0047fb20`, `0x004dbd90`) once, then loops from the frame tick on; the loop's back jump skips them.
* **The clip rectangle is put on and taken off the other way round.** `LevelPic` draws its pictures between two `FUN_004568d0` calls: with the software renderer the first call clips to the window
  and the second restores the whole screen (`0,0,512,256`); with the hardware renderer it is the opposite.
* **The progress bar's two fills.** While the time is in range the bar is filled with `(0x96, colour, 0)`; past the end `(0, 0, 0xff)` (a blue bar). The colour is the elapsed time with its low byte
  replaced (stack garbage the port reproduces).
* **Assertion bookkeeping is not compared.** The words an assertion records before it checks (the line at `0x00568638`, the file name at `0x0056881c`) are written by nearly every function of these
  screens, including the scroll list's constructor; the port reports only a failed check, so the probe leaves those two words out of the compared regions.
* **Two players in the tag game.** The running level has one player object; the tag probe points `0x0056a85c` at a fake second player past the slab.

## What is not in the fixtures

* `Competition_ResetMarquee`: 3 of 45 generated cases made retail divide by zero (`SIGFPE` in the judge scoring it calls, whose divisors are the level's score thresholds and their differences; the case was not traced further); they are dropped, not replayed.
* `GoalsScreen_Run`: 146 of 150 cases; four ran out of the probe's allocation arena (the frame copy allocates 8,400 bytes on every pass and nothing frees it in a forced call). The loop is
  replayed on frames that end it; its waits for the clock are scripted (`Game_FrameTick` advances the simulation clock) and the pad is scripted (`pad_scan` consumes two values).
* The medal objects the competition creates and the video it plays are fake objects whose virtual methods are reported by name; the real classes belong to later milestones.
* `Competition_StartDisplay`, `_MarqueeDisplay` and the others draw with the Mess text calls, which are seams (4.10a replayed them natively).

## Seams

The Mess text calls (`0x00473650`, `0x004734f0`, `0x00473580`, `0x004734b0`, `0x004739e0`, `0x004736c0`), sounds, the movie and pad calls, the GTE multiply (`0x004e2ef0`, its three results are
scripted and written to `0x006a3eb8..`), the trigonometry and vector helpers (`0x004f3980`, `0x004f39b0`, `0x004cabf0`, `0x00465f60`, `0x004ca8f0`, `0x004e8560`: 4.14a), the draw primitives
(`0x0048ab20`, `0x004e8730`, `0x004e8750`, `0x004e8770`), the picture, spool, sprite and font-draw calls of the windows, `Powerup_Construct`, `Career_LoadingScreen` (`0x00415b30`). The
career (`Career_LevelOpen`, `Career_GiveMedal`, `Career_GotGoalType`, `Career_Got`, `Career_LevelNeeds`), the skater manager, the window base, the menu toolkit and the font code run natively.

## Probe

`re/gdb/probes/frontend_specs_goals.py` (groups, regions, seeds, stubs), `frontend_specs_goals_cases.py` (competition, medals, tapes, tag; the 4.8 career sampler is reused through
`career_specs.base_state` and `career_specs_competition.competition_state`), `frontend_specs_goals_win.py` (the window groups). `fcall.py` gained `ret_arg` (a function that returns its output buffer),
the mapping of `this` under `stack_to`, real pointers for `pokes`, `fill` and `blob` whatever `stack_to` records, and `blob=(-1, n)` (the bytes `this` points at).

## Replay results (Phase 4.10h)

Forced calls of the retail functions in the running game on generated state (`re/gdb/probes/frontend_specs_goals{,_cases,_win}.py`), replayed by `opentony_goals_retail_calls_test` against `GoalScreens`: the return value, every byte written and the ordered service and virtual calls must match. 68 functions, 3802 cases, 0 mismatches (fixtures `re/evidence/fixtures/goal-calls-*.txt`).

| Class | Function | Address | Cases |
|---|---|---|---|
| Competition | `Competition_ConstructGlobal` | `0x004195b0` | 45 |
| Competition | `Competition_InitIfActive` | `0x004195e0` | 34 |
| Competition | `Competition_Init` | `0x00419600` | 35 |
| Competition | `Competition_WinLoseDisplay` | `0x00419da0` | 60 |
| Competition | `Competition_DestroyMedals` | `0x00419f10` | 45 |
| Competition | `Competition_WinLoseLogic` | `0x0041a040` | 45 |
| Competition | `Competition_MedalsDisplay` | `0x0041a270` | 60 |
| Competition | `Competition_EndGameLogic` | `0x0041a610` | 45 |
| Competition | `Competition_ResultsDisplay` | `0x0041a780` | 60 |
| Competition | `Competition_Constructor` | `0x0041ad40` | 45 |
| Competition | `Competition_DeletingDestructor` | `0x0041add0` | 45 |
| Competition | `Competition_ParticipantDestructor` | `0x0041adf0` | 45 |
| Competition | `Competition_ParticipantConstructor` | `0x0041ae00` | 45 |
| Competition | `Competition_JudgeDestructor` | `0x0041ae10` | 45 |
| Competition | `Competition_JudgeConstructor` | `0x0041ae20` | 45 |
| Competition | `Competition_Destructor` | `0x0041ae30` | 45 |
| Competition | `Competition_ParticipantDeletingDestructor` | `0x0041aea0` | 45 |
| Competition | `Competition_JudgeDeletingDestructor` | `0x0041aec0` | 45 |
| Competition | `Competition_MarqueeDisplay` | `0x0041af40` | 60 |
| Competition | `Competition_ResetMarquee` | `0x0041b540` | 42 |
| Competition | `Competition_EndGameDisplay` | `0x0041b590` | 60 |
| Competition | `Competition_StartDisplay` | `0x0041b8b0` | 60 |
| Competition | `Competition_PrizeMoneyDisplay` | `0x0041ba50` | 60 |
| VideoWindow | `VideoWindow_ScalarDelete` | `0x0045b3a0` | 60 |
| VideoWindow | `VideoWindow_Destroy` | `0x0045b3c0` | 60 |
| VideoWindow | `VideoWindow_Draw` | `0x0045b470` | 60 |
| VideoWindow | `VideoWindow_Update` | `0x0045b750` | 60 |
| VideoWindow | `VideoWindow_SetNext` | `0x0045b890` | 60 |
| VideoWindow | `VideoWindow_LevelAvailable` | `0x0045b8a0` | 60 |
| LevelPic | `LevelPic_Construct` | `0x0045b950` | 60 |
| LevelPic | `LevelPic_ScalarDelete` | `0x0045bd30` | 60 |
| LevelPic | `LevelPic_Destroy` | `0x0045bd50` | 60 |
| LevelPic | `LevelPic_Update` | `0x0045be00` | 60 |
| LevelPic | `LevelPic_SetLockText` | `0x0045c050` | 60 |
| LevelPic | `LevelPic_Draw` | `0x0045c1e0` | 60 |
| LevelPic | `LevelPic_Select` | `0x0045c360` | 60 |
| TRG | `TRG_InitializeExtendedRecord` | `0x0045c390` | 60 |
| TRG | `TRG_ExtendedRecord_ScalarDelete` | `0x0045c420` | 60 |
| TRG | `TRG_ExtendedRecord_Destroy` | `0x0045c440` | 60 |
| TRG | `TRG_ExtendedRecord_Update` | `0x0045c4a0` | 60 |
| LightEditor | `LightEditor_Construct` | `0x0045c5e0` | 60 |
| LightEditor | `LightEditor_ScalarDelete` | `0x0045cc60` | 60 |
| LightEditor | `LightEditor_Destroy` | `0x0045cc80` | 60 |
| LightEditor | `LightEditor_Draw` | `0x0045cd10` | 60 |
| LoadingScreen | `LoadingScreen_Show` | `0x0045d010` | 60 |
| LoadingScreen | `LoadingScreen_BeginProgress` | `0x0045d5e0` | 60 |
| LoadingScreen | `LoadingScreen_EndProgress` | `0x0045d680` | 60 |
| LoadingScreen | `LoadingScreen_UpdateProgress` | `0x0045d690` | 60 |
| LoadingScreen | `LoadingScreen_Tick` | `0x0045d760` | 60 |
| LoadingScreen | `LoadingScreen_CopyFrame` | `0x0045d7a0` | 60 |
| LoadingScreen | `LoadingScreen_Start` | `0x0045d830` | 60 |
| GoalsScreen | `GoalsScreen_Run` | `0x0045db40` | 146 |
| Medal | `Medal_Create` | `0x0046e550` | 80 |
| Tag | `Tag_ShowBanners` | `0x004b84c0` | 50 |
| Tag | `Tag_SwapRoles` | `0x004b8500` | 50 |
| Tag | `Tag_CheckCaught` | `0x004b8560` | 50 |
| Tag | `Tag_PushTriangle` | `0x004b8610` | 50 |
| Tag | `Tag_RotatedLine` | `0x004b8720` | 50 |
| Tag | `Tag_RotatedTriangle` | `0x004b8800` | 50 |
| Tag | `Tag_DrawArrow` | `0x004b8930` | 50 |
| Tag | `Tag_PointerToSkater` | `0x004b8990` | 50 |
| Tag | `Tag_DrawPointers` | `0x004b8a90` | 50 |
| Tag | `Tag_VersusUpdate` | `0x004b8b40` | 50 |
| Tag | `Tag_PlayerBox` | `0x004b8ca0` | 50 |
| Tag | `Tag_DrawScoreBoxes` | `0x004b8ef0` | 50 |
| Tag | `Tag_PlayerOneFlag` | `0x004b8fa0` | 50 |
| Tag | `Tag_PlayerTwoFlag` | `0x004b8fb0` | 50 |
| Tape | `Tape_Construct` | `0x004b8fc0` | 80 |
