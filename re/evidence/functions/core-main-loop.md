# The game's main loop, display and level launch (main.cpp)

Status: confirmed by retail forced calls (7,045 cases in `main.cpp` proper, 600 in the entry point and the launcher, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00466b60..0x0046ae8f` (36 functions)
Native: `src/runtime/core_kit.hpp`, `src/runtime/core_main.cpp` (the small functions, the frame, the level), `src/runtime/core_display.cpp` (Display, M3D_RenderStage, LaunchGame); replay
`src/runtime/core_retail_calls_test.cpp`; probes `re/gdb/probes/core_specs_main.py` + `core_specs_main_cases.py` (group `mn`) and `core_specs_top.py` (group `top`);
fixtures `re/evidence/fixtures/core-calls-mn*.txt`, `core-calls-top*.txt`

## What is here

`main.cpp` is the game's entry point and its whole frame: `main` (start-up, intro movies, logo, front end, shut-down message, shut-down), `LaunchGame` (a level from the front end's choices),
`Game_LevelLoop` (one level), the frame (`Game_InputUpdate`, `Game_GameplayUpdate` (native in 4.8's slice), `NetGame_Update`, `Game_Display`, `MainLoop_End`, `Level_Teardown`),
`Display` and `M3D_RenderStage`, the level set-up (`PlayAway_Init`, `SkaterObjects_Create`, `Skater_SetupAppearance`, `Skater_LoadBitmap`, the replay set-up) and a handful of polygon
arena helpers. The port is written from the machine code: the decompiler hides the `ecx` of method calls, the 16-bit stores of the screen scaling, the magic-number division by 240 and the
order of the stack arguments, all of which show in the retail state.

- **Polygon arena.** `RenderPolygonArenaCursor` (`0x560fd0`) advances by 0x1c per primitive; a primitive's ordering-table link is `[prim] = *ot; *ot = prim` with `ot = [[0x560fd4] + 0x88]`
  (or `+0x84`). Screen scaling: x is `(short)x * ScreenWidth >> 9` (logical), y is `(short)y * ScreenHeight / 240` through the unsigned magic `0x88888889` (`mul`, `shr 7`).
  `Display_AddQuad` (nine stack arguments: eight coordinates and a word; three of them get +1) stores the raw coordinates first and the scaled ones over them; the float at `+0x18` is `-1.0` when the
  two ordering tables are the same, else the viewport's depth word as a float. `Display_DrawDimOverlay` adds two primitives and raises the fade level by 4 up to 0x28.
  `FlipPrimitives` (the screen-flip cheat) walks the table backwards from `[state+0x84] + OTSize*8 - 8` and mirrors the x coordinates of each primitive code (`0x20..0xb0`, 12 shapes; the
  0xb0 shape holds vertices 0x14 or 0x1c bytes apart depending on a flag word).
- **Display.** `ViewportMode` (`0x56a928`) is **set to 0 right before the switch**, so only the first layout is reached unless the mouse-cursor draw (`0x4dbea0`) changes it (the probe scripts that).
  The camera toggle (`closeCam`) flips on the pad edge; the balance meters are drawn for the players whose trick state allows.
- **M3D_RenderStage(viewport, index).** The world objects, the player objects with the owner record's `next` pointer cleared while they draw, and, for the lone player with the draw-in-front flag
  (`+0xdc`), a second pass: the player's rotation matrix is multiplied column by column through the GTE (`gte_rtir`) into a local, copied back **as 32 bytes** (two padding bytes of stack garbage
  and 12 zero bytes of translation come with it), the position is swapped for the one at `+0x90`, the model is submitted with a depth scale `1 - clamp(+0x317c / 255, 0, 1)` from the x87, and everything is
  restored. Versus mode 9 adds the pointer object's angle (two `orig_ratan2` of the normalised directions).
- **Level.** `Game_LevelLoop` runs `Game_Init` (the state reset, the replay streams, ten warm-up camera updates), the animation start on each player, then the frame loop: tick, clock,
  frame rate, input, gameplay, network update (which can set the exit code), render wait, display, frame end, teardown; after it the movie, mouse, network and trick data are released.
  `Level_Teardown` flushes the frame, plays the pending movie, reloads the trick data (with `FileIO_LoadHed` / `UnloadHed` around the spool of a cutscene level) and applies the pending status word.
- **Set-up.** `Skater_SetupAppearance(slot, skater, name)` builds a custom skater from the editor's record, or merges the animation set `sk2anim` with the character's parts (the path's first
  character is the slot digit), then loads the texture named by `sprintf`'s output with byte 2 set to `'g'`. `SkaterObjects_Create` makes one player (two in a versus split) with its camera.
- **Launcher.** `LaunchGame(level)` applies the cheat flags, sets the screen up (`Init_Restart`, `Init_ForGame`, the resolution and the five viewport rectangles), calls the front end's loader, then loops
  `Game_LevelLoop` while the exit code is 7 (restart: clean up, reload the head, recreate the players, parse the nodes, rails and restart script), and tears down.

## Method

Forced calls on generated state (`P410_GROUPS=mn` and `top`): the players (0x3300 bytes each, only the touched fields captured), the render state, ordering tables, polygon arena and replay buffers in a
scratch slab, ~100 globals. Everything outside `main.cpp` is a stub that reports its call and answers a scripted value; a few stubs also script the side effect that matters (the exit code, the movie
skip flags, the viewport layout, the camera-update flag, the heap counter) with `writes=`, or on a **call site** (`stub_site`) when the same function is called elsewhere without that effect.

## Lessons

(1) A `writes=` side effect that only one caller needs must be a call-site stub, or every other caller's replay has to consume its values too. (2) The jump-table shapes of `FlipPrimitives` are only
certain against random primitive codes. (3) A stack object passed by pointer to a service (a font, a vector, a path) is recorded as one fixed scratch address (`stack_to`), including `this`.
