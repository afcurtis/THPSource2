# THPSource2 launcher

`THPSource2.exe` is the program a player starts. It sits in front of `opentony.exe`, which still does the first-run reads, the
run logs and the hang watchdog (docs/PLAYING.md):

## The install folder

The root holds the launcher and `README.md`; everything else is in a folder of its own (`src/launcher/layout.hpp` is the one place that names them,
shared by the launcher and `opentony.exe`; `tony release install` makes the folder, docs/BUILDING.md). It can be anywhere, also under a path with letters outside
the system's code page (`C:\Users\Дмитрий\...` on a Western Windows): `opentony.exe` and the x86-64 `opentony_spike.exe` run with the UTF-8 code page
(`src/hooks/kits/utf8.manifest`, Windows 10 1903 and later), and the paths the game hands to FFmpeg and SDL are UTF-8:

| Folder | Holds | Read or written by |
|---|---|---|
| `Build\` | `opentony.exe`, `opentony_spike.exe`, `opentony_standalone.dll`, `opentony_spike_video.dll`, `SDL3.dll`, the FFmpeg DLLs | the launcher starts `Build\opentony.exe`, which starts the rest |
| `Config\` | `THPSource2.ini`, `TH2_OPT.CFG`, `TrickSets.ini`, `Presets.ini` (the player's own presets) | the launcher; the game (`OPENTONY_SETTINGS`, `OPENTONY_CONFIG_DIR`) |
| `Data\Game\` | `THawk2.exe`, `ALL.PKR`, `Music\`, `med\Movies\` (or the installed game elsewhere, `[Launcher] GameDir`) | the launcher's import; `opentony.exe` (`--game-dir`, `OPENTONY_GAME_DIR`, `OPENTONY_MEDIA_DIR`) |
| `Data\Unpacked\` | `thawk2_static.img`, `data\` (ALL.PKR unpacked), stamps: made by `opentony.exe --prepare`, safe to delete | `opentony.exe`, the game (`OPENTONY_DATA_DIR`) |
| `Data\TextureDump\` | the game's own textures as `<key>.png` + `index.tsv`, `seen.tsv` (the texture census): safe to delete | `opentony.exe` with `OT_TEXTURE_DUMP` (set by Debug mode); the upscaler reads it |
| `Data\TrueColour\` | the PC port's 24-bit bitmaps in all 8 bits a channel, `<key>.png` + `stamp.txt`: safe to delete | the launcher makes it at the first PLAY after an import (seconds); the game reads it (`OT_TEXTURE_PACK`) |
| `Data\Upscaled\` | the upscaled set, `<key>.png` + `stamp.txt`: safe to delete | the upscaler writes it; the game reads it (`OT_TEXTURE_PACK`) |
| `Data\Upscaler\` | `upscayl-bin.exe`, `models\`, `texture_classes.tsv` (downloaded, not shipped: `re/config/upscaler.yml`) | the upscaler |
| `Saves\` | `*.SAV`, `*.PRK`; `highscores.tsv` (every finished run, "Scores" below) | the game (`OPENTONY_SAVES_DIR`; `OT_SCORES_FILE`); the launcher reads `highscores.tsv` |
| `Mods\` | loose files that replace the game's (docs/HOOKS.md) | the game (`OPENTONY_MODS`) |
| `Screenshots\` | screenshot hotkey (F8) pictures | the game (`OT_SCREENSHOTS`) |
| `Logs\` | `runs\`, `sessions.txt`, `reports\`; with Debug mode also `notes.txt`, `crashes.txt` | `opentony.exe`; the launcher (`reports\`) |
| `Licenses\` | `THIRD_PARTY_NOTICES.md` and the licence texts | nothing: for the reader |

The game still builds the names of its saves, parks and options file from the folder it runs in (the game folder, its working directory). The standalone
answers for exactly those names (`*.SAV`, `*.PRK`, `TH2_OPT.CFG` of the working directory) from `Saves\` and `Config\`, in one place:
`user_path` in `src/hooks/kits/standalone_userdata.hpp`, used by the file backend and by the host's stat, find and remove calls (so the memory card's scan of
`*.SAV` looks in `Saves\`). Unset, the variables leave everything where retail put it. `opentony.exe` sets them.

The launcher itself sits in the root while `SDL3.dll` is in `Build\`: `SDL3.dll` is delay-loaded (`-Wl,--delayload=SDL3.dll`), `main()` points the DLL search at
`Build\` before the first SDL call, and `SDL_MAIN_HANDLED` keeps SDL's own `WinMain` (which would call `SDL3.dll` first) out.

## What the launcher does

1. **Finding the game files.** The player points the launcher at one of two sources:
   - a folder: an installed game (`THawk2.exe` beside `ALL.PKR`), used where it is, or the disc as a drive letter or a copy of it
     (`SETUP\data`, `SETUP\med`), which is imported;
   - a disc image (`.iso`, or a raw `.bin`/`.img` with Mode 1 or Mode 2 sectors), read directly with no mounting.

   An import copies `THawk2.exe`, `ALL.PKR`, `Music\` and `med\Movies\` into `Data\Game` (the default; `[Launcher] GameDir` is empty then, so a moved install
   still finds its files). Two builds of `THawk2.exe` are used: the Activision Value re-release's (the SHA-256 in `re/config/native_hooks.yml`) and the
   original 2000 release's unprotected one (`re/xref/config.yml` `variants.original_2000`; `opentony.exe --prepare` rebuilds its static data at the
   re-release's addresses from `src/launcher/original_2000_static.inc`). Any other is refused after the 1.4 MB exe is copied and before the 190 MB
   package, except the original 2000 disc's copy-protected one: that import copies everything else and then says to put the unprotected exe in its place.
   The launcher then runs `opentony.exe --prepare` once.
2. **Settings.** These are kept in `Config\THPSource2.ini`, using PARTYMOD's `partymod.ini` layout (sections `[Window]`, `[Graphics]`,
   `[Gamepad] MovementStick`) plus `[Game]` and `[Launcher]` sections of its own. The launcher does not import a `partymod.ini`.
   A file without `[Launcher] SettingsVersion=2` is from before the graphics options did anything: its `UsePSXTextures`, `TextureFilter` and
   `InternalResolution` were placeholders, and read as the enhanced defaults.
3. **Bindings.** These are the game's own `[KEYBOARD]` and `[JOYPAD]` sections, in the same file: there is one settings file, and the game reads it
   (`OPENTONY_SETTINGS`; its other sections are `[VERSION]`, `[VIDEO]`, `[NETWORK]`). Its Options menu writes them there too. The launcher's editor keeps
   every line it does not own, so each side leaves the other's sections as they are.
4. **Play.** The launcher saves, sets the environment below, runs `Build\opentony.exe --root <install> --game-dir <game> [--movies]`, hides itself while the
   game runs, and comes back with the run's `sessions.txt` line.

Sources: `src/launcher/thpsource2/`. The `core/` folder is portable, with no SDL or Windows code: the INI editor, settings, bindings,
ISO 9660 reader, game-file checks and import. It is tested natively as `opentony_launcher_core_test`. `app/` holds the SDL3 and Dear
ImGui window, and the Windows process and Explorer calls. The launcher is built with the 64-bit kits
(`./tony.sh hooks build --arch 64` → `build/hooks/kits64/THPSource2.exe`). Dear ImGui is pinned in `re/config/imgui.yml`.

## What the game reads

Every setting is wired. Every run sets every variable in the list; an empty value unsets it, so nothing leaks from one run into the next. The
standalone logs what it made in `run.txt` (`video: window ...`, `video: picture 1280 x 720, rendered at ...`, `options: ...`), including one `video: GPU <vendor> | <renderer> | OpenGL <version>` line (with the video memory where the driver reports it: NVIDIA's and AMD's extensions only) that names the GPU a crash report's DLL belongs to.

| Launcher setting | `THPSource2.ini` | Environment | Done in |
|---|---|---|---|
| Window / borderless / fullscreen | `[Window] Windowed`, `Borderless` | `OT_WINDOW_MODE` = `windowed`, `borderless`, `fullscreen` | standalone_video.cpp |
| Resolution (0 = automatic) | `[Window] ResolutionX`, `ResolutionY` | `OT_WINDOW_SIZE` = `WxH` (unset: the largest window of the picture's shape the desktop holds); fullscreen with a size = exclusive mode | standalone_video.cpp |
| Vertical sync | `[Window] VSync` (0 off by default; -1 driver default) | `OT_VSYNC` = `0` or `1` | standalone_video.cpp |
| Frame rate | `[Window] FrameRate` (60 default for a new file, 30 or 120; anything else is 30) | `OT_FRAME_RATE` = `60` or `120` (empty at 30) | frame_pacing.hpp, standalone_seams.cpp |
| Picture shape | `[Window] Aspect` (0 follow the window, 1 4:3, 2 16:9, 3 16:10, 4 21:9; an older file's 5, the stretched 4:3, is read as 0) | `OT_ASPECT` = `window`, `4:3`, `16:9`, `16:10`, `21:9` (the game also takes `stretch`, which the launcher no longer offers) | standalone_video.cpp, standalone_options.cpp |
| Field of view on a wide picture | `[Window] FovAdjust` (default on) | `OT_FOV` = `adjust` (Hor+) or `fixed` (the 4:3 horizontal angle at every shape) | standalone_options.cpp (`write_wide_aspect`) |
| HUD anchored in a level | `[Window] HudAnchor` (default on; greyed with a 4:3 picture) | `OT_HUD_ANCHOR` = `1` or `0` | draw_kit.cpp (`place_hud_group`), hud_display.cpp (`ElementScope`), host_options.hpp |
| HUD size | `[Window] HudSize` (0 = Auto, else 50 to 150 percent) | `OT_HUD_SCALE` = percent (the launcher turns Auto into a number from the window's height, or the display's, when it starts the game) | same |
| HUD margin (no launcher row: set by hand) | `[Window] HudMargin` (0 to 10 percent) | `OT_HUD_MARGIN` = percent of the picture kept clear at an anchored edge | same |
| Render resolution | `[Graphics] InternalResolution` (0 PSX 240 lines, 1 PC 480, 2 Native, the window's own; a file from before the list was cut to three, with 3 or 4, reads as 2) | `OT_INTERNAL_RESOLUTION` = `psx`, `pc`, `window` (the engine also takes `960`, `1920` or another line count, for scripted runs) | sdl_video_gl.cpp |
| Smooth edges | `[Graphics] MSAA` (0, 2, 4, 8) | `OT_MSAA` | sdl_video_gl.cpp |
| Texture filter | `[Graphics] TextureFilter` (0 sharp, 1 smooth) | `OT_TEXTURE_FILTER` = `sharp`, `smooth` | sdl_video_gl.cpp |
| Texture detail | `[Graphics] Anisotropy` (0 off, 1 mip maps, 2, 4, 8, 16; default 16; greyed with sharp textures) | `OT_ANISOTROPY` = `0`, `trilinear`, `2` .. `16` (`0` while the texture filter is sharp) | sdl_video_gl.cpp |
| Sharp menus and HUD | `[Graphics] SharpHud` (default on, also for a file without the key; greyed with sharp textures; Enhanced on, the Original presets off) | `OT_HUD_FILTER` = `sharp` or `smooth` (`smooth` while the texture filter is sharp) | sdl_video_gl.cpp |
| Output (the launcher's name for the scaling) | `[Graphics] Scaling` (0 Clean, 1 Sharp pixels, 2 Nearest pixels, 3 CRT; the looks scale a picture smaller than the window, so they exist at the PlayStation and PC sizes only: at Native the launcher greys the row, shows Clean and sends `smooth`, keeping the stored choice; the Original PlayStation preset is 3) | `OT_SCALING` = `smooth`, `sharp`, `nearest`, `crt` (CRT: scanlines that follow the target's lines, a soft blur across them, a faint aperture mask from four output pixels a line; a target larger than the window is box-filtered instead) | sdl_video_gl.cpp |
| Curved screen | `[Graphics] CrtCurve` (default off; on in the Original PlayStation preset only, part of a preset's match) | `OT_CRT_CURVE` = `1` or `0`: the picture bent as on a curved tube, a barrel warp (1/32 across, 1/24 down at the corners) with the screen's edge a rounded rectangle (corner radius 3% of the height) faded over one output pixel and black outside it. Independent of the output and the render resolution: the warp is in every present shader (the box filter, sharp, CRT and a plain one for linear and nearest), so each reads the picture at the warped place | sdl_video_gl.cpp (`curve_warp`) |
| PlayStation dither | `[Graphics] PSXDither` (default off, on in the Original PlayStation preset only; greyed unless the resolution is PlayStation) | `OT_PSX_DITHER` = `1` or `0` (`0` unless the resolution is PlayStation): the 4 x 4 matrix and 5 bits a channel over the whole finished picture before scaling | sdl_video_gl.cpp |
| Ambient occlusion | `[Graphics] AmbientOcclusion` (0 off, 1 low, 2 high; default High, High in Enhanced and off in the originals; a launcher file from before the key reads off) | `OT_AMBIENT_OCCLUSION` = `off`, `low`, `high` | sdl_video_gl.cpp |
| Smooth cut-out edges | `[Graphics] SmoothCutouts` (default on; greyed without multisampling) | `OT_ALPHA_TO_COVERAGE` = `1` or `0` (`0` without multisampling) | sdl_video_gl.cpp |
| Supersampling | `[Graphics] Supersample` (100 off, 150, 200, 300, 400; default 200, a value that is not an option is off; greyed for PSX and PC) | `OT_SUPERSAMPLE` = `1`, `1.5`, `2`, `3`, `4` (`1` for PSX and PC) | sdl_video_gl.cpp |
| PSX colour | `[Graphics] PSXColor` | `OT_PSX_COLOR` = `0` or `1` | draw_kit.cpp (`Presentation`) |
| PSX textures | `[Graphics] UsePSXTextures` | `OT_PSX_TEXTURES` = `0` or `1` | standalone_files.cpp |
| Fog distance | `[Graphics] FogDistance` (100 / 150 / 275 / 500 / 1000 = off; the launcher shows them as multiples of the level's own: PlayStation (1x), PC (1.5x), Dreamcast (2.75x), Far (5x), Off) | `OT_FOG_DISTANCE` = percent or `off` | standalone_options.cpp |
| Object detail distance | `[Graphics] DetailDistance` (100 the game's, 200, 400, 1000 = maximum; default maximum, also for a file without the key; Enhanced maximum, the Original presets 100) | `OT_DETAIL_DISTANCE` = `100`, `200`, `400` or `max` (`100` is the game's own) | standalone_options.cpp; runtime/host_options.hpp, `RenderKit::render_objects` |
| Movement stick | `[Gamepad] MovementStick` | `OT_PAD_STICK` = `none`, `left`, `right` | standalone_platform.cpp |
| Camera stick | `[Gamepad] CameraStick` (default on, also for a file without the key), `CameraMode` (0 spring back; 1 stay is not offered yet), `CameraSensitivity` (1 to 9, default 5), `CameraInvertX`, `CameraInvertY` (default off) | `OT_FREELOOK` = `spring` / `stay` (unset: off), `OT_FREELOOK_STICK` = `left` when the movement stick is the right one, else `right`, `OT_FREELOOK_SENS`, `OT_FREELOOK_INVERT` = `x`, `y`, `xy` | standalone_freelook.cpp; runtime/free_look.hpp |
| DirectInput support (slow) | `[Gamepad] DirectInput` (default off: on some PCs SDL's DirectInput device scan held the launcher's and the game's start for about 20 s each; Xbox, PlayStation and Switch pads use SDL's other drivers) | `SDL_JOYSTICK_DIRECTINPUT` = `0` while off (SDL's own hint; the launcher sets it for itself before `SDL_Init`) | main.cpp, SDL |
| Autokick, vibration | `[Game] Autokick`, `Vibration` (0 off, 1 on, 2 leave) | `OT_AUTOKICK`, `OT_VIBRATION` | standalone_options.cpp; rumble: standalone_host.cpp |
| Vehicle sounds (no menu entry) | n/a, always on | `OT_VEHICLE_SOUND_FIX` = `1`: cars (and platforms, script objects) that share a looping sound no longer restart it every frame (NYC, Philadelphia); unset = retail's stutter, which is how every gate runs | standalone_audio.cpp |
| Retail fixes (no menu entry) | n/a, always on | `OT_RETAIL_FIXES` = `1`: the retail game's own mistakes corrected. The polygon arena is the enlarged, padded one (4 times retail's, as with a fog or detail distance of the player's): at retail's size several writers test only that the cursor is not past the end and then write up to 0xc0 bytes past the block into the game's heap, and a full arena stops the program; the enlarged one drops a polygon that does not fit and logs it once per level. Unset = retail, which is how every gate runs (`OT_POLY_BUFFER=1` still forces retail's arena) | runtime/host_options.hpp `retail_fixes`; standalone_options.cpp |
| Unlocks | `[Game] UnlockCheats`, `UnlockMovies` | `OT_UNLOCK` = `cheats,movies` | standalone_options.cpp; movies: runtime/host_options.hpp. The game also takes `levels` and `skaters` (the scripts and census runs use them); the launcher has no switch for them, as a quick start opens both itself and PLAY keeps the career as earned. `decks` (no setting; the texture census's Skate Shop runs) makes every character own every deck |
| Starting camera | `[Game] DefaultCamera` (0 first position, shown as Default (close); 1 second, Alternate (far)) | `OT_CAMERA` = `2` (unset: the first) | standalone_options.cpp; runtime/host_options.hpp |
| Grind balance meter (Display > HUD) | `[Game] GrindMeter` | `OT_GRIND_METER` = `1`: a horizontal bar above the trick names while a skater grinds, its marker at the rail's balance (`+0x2ef0`, a bail at the ends). It is the PlayStation game's meter: the PC game ports its drawing function (`Panel_BalanceLR`, `0x0048abf0`) but never calls it. Not drawn with the HUD hidden or paused; unset = retail, which is how every gate runs | core_display.cpp (`CoreKit::display`); runtime/host_options.hpp |
| Sound levels (Audio) | `[Sound] MusicLevel` (0..10, default 5, the game's stock), `SfxLevel` (0..10, default 10) | `OT_MUSIC_LEVEL`, `OT_SFX_LEVEL`: the game's own Options > Sound levels start there every launch, after Options_Init and after a loaded save (the save image's copies too, which the pause menu's Sound screen reads and sets the music from as it closes); the movies' sound follows the music level | standalone_options.cpp, standalone_movie.cpp |
| Custom soundtrack (Audio) | `[Sound] CustomMusic`, `MusicShuffle`, `MusicIncludeDefault` (all default 0), `MusicFoldersOff` (the packs switched off, `\|` between their names) | `OT_CUSTOM_MUSIC` = `1`, `OT_MUSIC_DIR` = `<install>\Mods\Music`, `OT_MUSIC_FOLDERS_OFF`, `OT_MUSIC_SHUFFLE` = `1`, `OT_MUSIC_DEFAULT` = `1`; all unset when it is off = retail, which is how every gate runs | standalone_music.cpp, standalone_movie.cpp (`start_movie`), kit_dll.cpp (the level loop's mark); the folder scan: src/launcher/music_library.hpp |
| Song title (Display > HUD) | `[Sound] MusicNowPlaying` (default 0, off; the key stays in `[Sound]` for saved settings) | `OT_MUSIC_NOW_PLAYING` = `1` (unset = off = retail and every gate), whether or not the custom soundtrack is on | standalone_music.cpp (`kGameSongs`, `music_choice`) |
| Quick start (Quick Start) | `[Game] QuickMode` (0 free skate, 1 single session, 2 goals speed run, 3 competition), `QuickLevel`, `QuickSkater`, `QuickLook` (0 A .. 3 D), `MaxStats`, `SpeedRunCash`, `QuickTrickSet` (a set of `Config\TrickSets.ini`, empty: each skater's own; "Trick sets" below); the Quick Start page's Free Skate, Single Session, Goals Speed Run and Competition buttons start the game (PLAY never does, and no quick start is stored) | `OT_QUICK_START` = `level[,skater]`; `OT_QUICK_MODE` = `single`, `speedrun`, `competition` (else free skate); `OT_QUICK_LOOK` = `0`..`3`; `OT_MAX_STATS` = `1`; `OT_SPEED_RUN_CASH` = `1` (a speed run only: to 100%); `OT_QUICK_TRICKS` = the set's binding block in 106 hex digits (all only with a quick start). The two career sessions: "Goals Speed Run and Competition" below. A quick start also opens every level and skater (`OT_UNLOCK` levels and skaters) and is kept away from the save file and the shell (see "Quick start" below) | standalone_options.cpp; the menus: runtime/host_options.hpp (`quick_play`), front_screens.cpp, front_update.cpp, front_update_late.cpp; the save file: standalone_userdata.hpp (`user_file_locked`) |
| Score initials (Gameplay) | `[Game] Initials` (three of A to Z or a space, stored in quotes so a trailing space survives; anything else, or empty, leaves the game alone) | `OT_INITIALS` = `XYZ` (empty when unset): written to the remembered initials (`0x0056a408`, the save image's, which the score tables take their letters from, and `0x0055fc5c`, the live C string) at every poll, so each entry starts from them and a letter changed on the entry screen applies to that score only | standalone_options.cpp (`write_initials`) |
| True-colour textures (Graphics) | `[Graphics] TrueColour` (default 1; Enhanced 1, the two Original presets 0); "True-colour textures" below | `OT_TEXTURE_PACK` gets `<install>\Data\TrueColour` after the upscaled folder (a vertical bar between) when ticked and "Texture set" is PC | sdl_video_gl.cpp (several folders) |
| Upscaled textures (Graphics) | `[Graphics] UpscaledTextures` (default 0); the set is made by "Upscale textures..." ("Upscaled textures" below) | `OT_TEXTURE_PACK` = `<install>\Data\Upscaled` when ticked, a set exists and textures are smooth with texture detail on; empty otherwise | sdl_video_gl.cpp (the backend's replacement, T2) |
| Score file | n/a (always on) | `OT_SCORES_FILE` = `<install>\Saves\highscores.tsv`: the game appends every finished career level run, single session, speed run and competition session ("Scores" below); unset (every gate) records nothing | standalone_scores.cpp, score_recorder.hpp; runtime/score_hooks.hpp (front_states.cpp, front_update_late.cpp); the format: src/launcher/scores.hpp |
| Scores in the game (no menu entry) | n/a, always on (an old file's `[Game] ScoresInGame` is ignored) | `OT_SCORES_IN_GAME` = `1` (with the score file): the game's 14 score tables (Options > High Scores, the table after a run) show the file's best runs, merged with what the tables hold | standalone_scores.cpp (`fill_tables`), score_recorder.hpp (`merge_table`) |
| Trick tips (no menu entry) | n/a, always passed | `OT_TRICK_TIPS` = `0`: Options > Display Options > Trick Tips starts off at every launch, after retail's Options_Init (`0x00413f70`, which sets it on) and after a loaded save (`0x004165d0`): the live word `0x0055fcc0` (the menu's) and the save image's `0x0056a46c` (which `TrickTips_Show` reads). The in-game option still turns them on until the game closes; unset (every gate) leaves the game's | standalone_options.cpp (`options_after_options_reset`) |
| Frame counter | `[Game] ShowFPS` | `OT_FPS` = `1` (drawn), `log` (to run.txt, with the shortest and longest frame of each half second) | standalone_video.cpp |
| Screenshots | always on, no setting (an old `[Game] Screenshots` is ignored); Display > Capture opens the folder | `OT_SCREENSHOTS` = `<install>\Screenshots`, always; the screenshot hotkey (F8) writes a PNG of the render target, resampled to the picture's shape when the target is of another one (the PlayStation preset's 512 x 240 is saved at 4:3, 512 x 384). With the CRT scaling, the curved screen or the dither on, it is the picture as the window shows it instead (`ot_hal_sdl_video_gl_capture_shown`: the present's own pass run once more, no swap, read back at the window's resolution: scanlines, curve and dither are in the file) | standalone_video.cpp |
| Hotkeys (Controls > Hotkeys) | `[Hotkeys] <Name>Key`, `<Name>Mods`, `<Name>Pad`, `<Name>Hold` for Screenshot, Retry, Quit, Fullscreen; `PauseOnFocusLoss` | `OT_HOTKEY_SCREENSHOT`, `_RETRY`, `_QUIT`, `_FULLSCREEN` = `key,mods,pad,hold` (key a DirectInput code, mods 1 Ctrl / 2 Alt / 4 Shift, pad a mask of SDL gamepad buttons 0..14 and bit 15 / 16 the triggers, hold in ms; `0,0,0,0` unbound; a name not set keeps F8 for the screenshot and nothing else); `OT_PAUSE_ON_FOCUS_LOSS` = `1` | standalone_platform.cpp (`poll_hotkeys`), standalone_video.cpp; retry and pause: `HostOptions::request`, `FrontScreens::update` |
| Window title | n/a | `OT_WINDOW_TITLE` = `THPSource2` | standalone_video.cpp |
| Intro movies | `[Launcher] PlayMovies` (off in every preset) | `opentony.exe --movies` | opentony.exe |

Defaults of a new file: fullscreen, vertical sync off, pause on a lost focus on, and the Enhanced preset (`default_settings()` holds its values; a new file also
ticks Upscaled textures when the set has been made). **Presets** (the footer's list at the bottom left, `apply_preset` in `core/settings.hpp`) set the look and feel
at once: the Display tab's picture shape, field of view, HUD layout and size, balance meter, song title and frame rate; every Graphics option; the camera stick,
autokick, vibration, starting camera and intro movies. The window's mode and size, V-Sync, the HUD's visibility, the FPS counter, sound, unlocks, initials, controls,
hotkeys and trick sets are the player's, and no preset touches them. **Enhanced** is everything turned up, short of what a driver may not hold: the window's resolution with 2x supersampling, 4x
multisampling, smooth cut-out edges, smooth textures with 16x texture detail, sharp HUD, PSX colour, no fog, the most object detail, High ambient occlusion, true colour
and the upscaled textures once made, on the PC's replacement bitmaps (the PlayStation textures are lower resolution); the window's own shape, the wide view, the HUD at
the edges at the Auto size, 60 fps (every display holds it), the song title, the camera stick, autokick and vibration on. **Original PC** is the game as
it ran: 640 x 480, 4:3, the 4:3 view and HUD at 100%, 30 fps, PC colour and fog, 15-bit textures, no camera stick, autokick and vibration left to the game's menu.
**Original PlayStation** is the console on a TV: 512 x 240 with its own unfiltered textures, colour and fog, the PlayStation dither, CRT output scaling and the curved screen,
otherwise as Original PC. No preset shows the balance meter (it is off in all three; a player can tick it). The backend lowers a supersampling factor the GPU cannot hold (`sdl_video.h`). The list shows the preset the options
match (`preset_of`, the fog compared by the option it shows), or "Custom (not saved)". **Save as...** keeps the preset options under a name in `Config\Presets.ini`
(one section a preset with THPSource2.ini's key names, `write_presets`; a name already there is replaced, a built-in's name is refused), and the player's presets follow
the three in the list; **Delete...** (asks first) removes the player's preset the options match. There is no Save or Revert: every change is written the moment it is
finished (a slider when it is let go, a box when it loses focus): the settings to `THPSource2.ini`, the trick sets to `TrickSets.ini`, and the bindings once no two
conflict (the footer says so until then).

The launcher's tabs: Display (the window, resolution, aspect, widescreen FOV, frame rate, vertical sync; the HUD's layout, size, visibility, the grind meter, the frame counter), Graphics, Audio, Controls (Controller, Keyboard, Hotkeys; the keypad's keys read "Numpad 2" for the game's PAD2), Gameplay (skating: autokick, starting camera, score initials; game: pause on a lost focus, intro movies; unlocks), System (game files, with the folder cut in the middle when it is long; Appearance: the Theme (Follow system by default, which takes Windows' light or dark mode, follows a change of it while the launcher is open, and is dark when the system cannot say; or Dark, or Light; `[Launcher] Theme` = `system`, `dark` or `light`); Diagnostics: Debug mode as "Debug logging", logs, Report on GitHub, the last session; Updates: the check, Check now, what it found; Reset; about). Reset all to defaults (asks first) puts every option of the six tabs and the controller, keyboard and hotkey bindings back to their defaults, as a new settings file has them (Upscaled textures on when the set has been made); the game's location, the Quick Start page's choices and trick sets, the update check's setting and the release whose notice was dismissed, and the hand-set `HudMargin` are kept. Like every change it is written at once and cannot be undone. Every row is a label and a control, under a semibold section heading. What an option does is a tooltip, shown while the pointer is on it; an option that cannot be used says why beside itself ("Needs anti-aliasing") instead of only greying out, so no row moves. The page is one column, at most 1180 points wide and centred. Fields, boxes and buttons have a border in both themes. The tabs inside a tab (the Controls pages, the trick pages) are pills; each Controls page has its own reset (Reset buttons, Reset keys, Reset hotkeys). Quick Start is apart from the settings: it is the green button at PLAY's left, the same size, and replaces the tabs
with its own page of two tabs, Session and Tricks (the footer's button turns into Back to settings, in a button's neutral colour; Esc returns too; the footer's Presets, Save as... and Delete... are hidden there, as on High Scores,
and PLAY stays). The gold High Scores button at the top right of the header opens the High Scores page (a page of its own, not a tab of Quick Start: the footer keeps Quick Start and PLAY where they are; on that page the gold button and the footer's Presets, Save as... and Delete... are hidden, and the back arrow or Esc return and bring them back). The icon and title at the top left also go back to the settings from any other page (a big round back arrow at the far left shows it, the logo and title shifting over, and the group highlights on hover). When the game returns after a run that set a
new high score ("New high score! ... on <level>", or the speed run's or the competition's; "New high scores on N levels" for several; `scores.hpp` `new_bests`, a tie
does not count), the High Scores button pulses with a ring around it and a gold callout names the score beside it, then fades out after about ten seconds; a click on
the button opens that board, and opening the High Scores page or starting another run ends it. A run that was recorded without a new best gets the same pulse and a callout of where it stands ("Run recorded: 196,407 on Hangar (#2 of 3)", `scores.hpp` `placing`; "N runs recorded" for several); a run that was not recorded (no score, a cheat) shows nothing. The High Scores page lists the session's runs under its tabs ("This session": up to five, newest first, each with its place on its level) whether or not they made the board. The launcher always waits
for the game and comes back when it ends (the old `[Launcher] CloseOnPlay` is ignored): that is when it reads the run's result, the crash panel and the new scores.

### Supersampling and the downsample

- **Supersampling** multiplies the size of the render target (each side) when the render resolution is Native; PSX and PC ignore it. The factor is lowered
  in steps of 5% until the target fits `GL_MAX_RENDERBUFFER_SIZE` and about 400 MB (width x height x 8 bytes x samples; a fixed budget, not the card's memory, and only the factor is lowered by it: a window-sized target is made whatever it costs); `run.txt` says what it got
  (`video: picture ... rendered at ...`, then `video: supersampling 150% of 1.5`). A target past the driver's limit with no supersampling is clamped to the largest of its shape.
- **The downsample.** A target larger than the rectangle it is shown in is presented through a box filter over each output pixel's footprint (N x N bilinear taps, N the
  footprint rounded up, at most 6), not the bilinear blit, which skips texels above 2x. Smaller targets keep the old paths. The multisample resolve stays a blit and comes first.
- **LOD bias.** With supersampling at factor f the world's filtered textures get a bias of -log2(f) (at most -2), so they are as sharp as at the picture's own size.
- **Smooth cut-out edges.** The game never uses the alpha test: a fence or leaf is a blended texture whose alpha is 0 or 255. With multisampling, a world draw that blends plainly
  (source alpha, one minus source alpha) a texture of that kind with untinted vertices is drawn without blending and its alpha, sharpened around one half, becomes the coverage,
  so the multisampling antialiases the cut-out's edge. Anything else (tinted, translucent, smooth alpha, the 2D layer) is drawn as before.

### How each one works

- **Camera stick.** The stick that does not move the skater swings the camera round the skater (left and right, up and down) and springs back when let go; the
  skater's controls do not change. With the movement stick on the right, the left stick looks; with the left or none, the right one. The look stands down where the
  game owns the camera (end-of-run swing, replays, level intros, bails). Plan and facts: THPS2 `docs/post_phase_5/camera.md`.

- **The picture's shape.** The game's screen words (`0x029da394` / `0x029da398`) are 640 x 480. For a wider picture the program raises the width
  (`ot_standalone_set_display`; 16:9 = 854 x 480) before the boot, and the game's own projection does the rest: `M3dInit_SetResolution`
  (`0x00464690`) sets the view's pixel aspect words (`0x00563a6c` / `0x00563a70`) from the screen size. Retail's branch for a wide screen magnifies the view
  (written for displays without square pixels, never reached by a mode retail lists), so `write_wide_aspect` sets both words to
  `1 / sqrt(width / 640)` in 4.12, which keeps the view's height and adds width: **Hor+**, with the same framing as 4:3 at the centre. The view planes
  (culling) follow, being built from the same words. Verified on 16:9: the level at the sides, the same vertical framing.
- **The 2D layer.** The HUD, menus and loading screens are laid out by scaling the PlayStation's 512 columns to the screen width. `DrawKit::fit_flat`
  maps the corners of every 2D primitive (z below the winding threshold, or a general polygon whose reciprocal depths are all exactly 1: the sprite quads of
  `D3D_BuildSpriteQuad`) from the wide screen to a 4:3 area in the middle. A primitive that reaches both screen edges (a fade, a bar) is left alone. The
  mouse is mapped back the same way (`window_to_display`).
- **The in-level HUD's layout (anchor, size, margin).** With any of the three set, `Panel::display` records each element it draws (the score block with the special meter,
  the switch icon, the clock, the balance icon, trick points and names, the SKATE letters, the level message) as the arena range its packets use plus where the game put it
  (`ElementScope`, `host_options().hud_groups`); `DrawKit::walk` tells a packet which element it belongs to and `fit_flat` places the element about one point
  (`place_hud_group`): x' = to + (x - from) x (hud width / screen width) x size, y' = to + (y - from) x size. An element whose point is in the left third of the game's layout
  goes to the picture's left edge (from = 0, to = margin), the right third to the right edge, the top and bottom thirds the same for the height; the middle keeps its place in
  the 4:3 middle, which is where the game put it. So the score and the special meter hug the left edge, the switch icon and the SKATE letters the right, the clock, trick
  names and trick points stay in the centre. Size is about that point, so it grows or shrinks an element away from its edge. Only a layout that uses the picture's width
  (one player, or one above the other) is recorded; two players side by side keep the 4:3 layout, and the banner messages (`Mess`) and the menus are not touched. Auto
  is 100% up to 1080 lines and `sqrt(1080 / lines)` beyond (1440: 85%, 2160: 70%, never below 70%) from the window's height (or the display's, for automatic windows and
  fullscreen), fixed when the game starts. The gates set none of it: the 2D layer is retail's.
- **The render target.** `ot_hal_sdl_video_gl_options`: the target need not have the mode's size. Vertices stay in mode pixels, the viewport stretches them
  over a target of any size, multisampled or not; scissors, clears and lines (widened to a mode pixel) follow. `follow_window` remakes the target between
  frames when the window changes. `sharp` scaling is the "sharp bilinear" shader (whole source texels, one blended pixel at the edges).
- **PSX colour.** The PC port sends every vertex colour through its gamma tables and multiplies the texel once. A texture that is still the PlayStation's
  own (a texture record without flags 4 or 8: levels without a replacement bitmap, skaters, fonts) was made for the PlayStation's blend, texel x colour x 2,
  and comes out dark through the PC's. With PSX colour the draw path hands those draws the colour from before the levels and the video doubles it
  (`OT_HAL_DRAW_MODULATE_2X`). Replacement bitmaps keep the PC maths.
- **PSX textures.** The game's own switch (`D3DConfiguredTextureRootEnabled`) is passed over while a level loads (texture mode 1), so the option stops
  the loads of `NEWTEX\...` at the file backend: every texture is the level's own.
- **Fog.** `VideoFogYonScale` (`0x005485a0`, the options file's `FOGSCALE` / 100) is replaced after the options are read and kept; "off" is 64 times
  the level's own. The Options menu's slider stops at 500 %.
- **Unlocks.** `AllLevelsOpen` (`0x0056a8e4`, the game's own switch), the hidden characters as `Career::unlock_character` opens them, the cheat list's
  "earned" bytes. Put back at every poll; the career is not written unless the player saves.
- **All movies.** `OptionsScreen::movie_unlocked` (`0x00486910`) answers true for every kind but 3 (the one the game never unlocks) when `HostOptions::unlock_movies` is set.
- **Hotkeys.** Watched at every input poll by the host, never by the game's own bindings: a key (with its modifiers) or a set of pad buttons, either fires; a hold time makes a press too short do
  nothing (Quit defaults to L3 + R3 held one second). Screenshot: the video takes the press at the next present. Fullscreen: `SDL_SetWindowFullscreen` (the desktop's size, not an exclusive
  mode). Quit: the game ends with exit code 0, so the launcher comes back with "ended normally". Retry: sets `HostOptions::request` = 2; the front end's update, in a level, puts the pause menu up
  as Start does and takes the Retry choice on its first frame (the pause set-up and tear-down are retail's own); a request made outside a level is dropped. Pause on a lost focus is request 1: the
  pause menu, nothing more (the pause also holds the sound).
- **The crash panel.** A run that ends as a `CRASH` or a `HANG SUSPECTED` (its line of `sessions.txt`) puts a red panel over the tabs with the recorded reason, a box for what the player was doing
  (written into the run's folder as `what_happened.txt`) and a button that zips the run's folder (`Logs\reports\<run>.zip`, with Windows' `tar.exe`). Without Debug mode the zip is made before the
  panel shows; the note is added to it when the player clicks out of the box, on Dismiss and on either button. Three buttons: **Report on GitHub** (accent coloured), **Show Crash Zip** (re-zips with the
  note and selects the zip in Explorer) and **Dismiss**. Report on GitHub makes the zip (`report_on_github`: the same `make_report`, in either mode), selects it in Explorer (`reveal_in_explorer`) and opens the bug form
  (`kIssueUrl` = `<project page>/issues/new?template=bug.md`, `.github/ISSUE_TEMPLATE/bug.md`) with `SDL_OpenURL`; the player drags the zip into the form. Nothing is uploaded
  from the launcher. System > Diagnostics > Problems opens the same form. `--page crash` shows the panel for a made-up crash (the screenshot mode of the tests).
- **Updates** (`core/update_check.hpp`, `app/update_task.cpp`; System > Updates, `[Launcher] CheckForUpdates`, default on, and `SkippedUpdate`). When the launcher opens (not with
  `--no-update-check` or `--screenshot`) a detached thread runs Windows' `curl.exe --silent --head --proto =https --ssl-no-revoke --connect-timeout 5 --max-time 10
  --output NUL --write-out %{redirect_url} <project page>/releases/latest`: a HEAD request, not followed. GitHub answers 302 with the newest release's page as the address
  (`<project page>/releases/tag/v0.8`); a repository with no release answers `<project page>/releases`, a missing one 404 (an empty address). `tag_from_redirect` accepts only
  an address under the project page whose tag is a plain version (`v` and one to four numbers; "v0.8-rc1" is not), `is_newer` compares it with the running version number by
  number (0.10 is after 0.9). The result is `Newer`, `Current`, `NoRelease` or `Failed` (offline, no `curl.exe`: silent, with no banner). A newer release asks the API once
  more (`api.github.com/repos/<owner>/<repo>/releases/tags/<tag>`, unauthenticated: 60 an hour for an address, and only a newer release asks) for the release's text and the
  package's size (`parse_release`; `plain_notes` takes the Markdown marks out); if that fails the banner just has no notes. `Newer` shows a banner above the page: the notes
  ("What's new in v0.8", a box no taller than 150 points), **Update now**, **Release page** (`kLatestReleaseUrl`) and **Dismiss** (stores the tag in `SkippedUpdate` so the next
  release shows its own); the System page has the same Update now and Release page beside **Check now**, which asks at once, also with the check switched off.
  **Update now** (`UpdateInstall`, only for the launcher of an install folder: `THPSource2.exe` beside `Build\`; a launcher run from a build tree only offers the release page)
  downloads `<project page>/releases/download/<tag>/thpsource2-win64.zip` and its `.sha256` into `Data\Update\` (`curl --location --fail --proto =https --proto-redir =https`;
  a bar from the file's growing size, Cancel stops curl), then refuses the package unless its size is the API's and its SHA-256 is the `.sha256` file's, unpacks it with `tar.exe`,
  and refuses it unless it holds `THPSource2.exe`, `Build\opentony.exe` and `Build\SDL3.dll`. Only then does the banner say it is ready: **Restart and install** saves, starts
  `Data\Update\apply.cmd <pid>` (`update::apply_script`, `start_batch`: `cmd.exe /d /c` with a hidden console; a `DETACHED_PROCESS` start left the script hanging in its
  first `tasklist`) and closes. The script waits for that process to end (60 s at most), mirrors `Build\` and `Licenses\` from the unpacked release (robocopy `/MIR`: the release
  owns them, as `tony release install` does), copies `README.md` and `THPSource2.exe`, deletes the staging and starts the launcher again. `Config`, `Data`, `Saves`, `Mods`,
  `Screenshots` and `Logs` are never named in it. It logs to `Logs\update.txt`; on a failure it writes `Data\Update\failed.txt` and starts whatever launcher is there, which says
  "The last update did not finish" until OK is pressed (the install may be a mix of two versions: extract the release zip over it). A launcher that starts without a failure note
  deletes what an earlier update left in `Data\Update\`. The addresses are built from the project page and a tag that is a version, never taken from an answer; the notes are
  drawn as text and nothing in them is opened. Reset all keeps the check's setting. A release is found by the tag, so `kVersion` and the release's tag must agree, and the zip
  and `.sha256` must be named as `tony release package` names them ("Releasing", docs/BUILDING.md). `--page update` shows the banner and the section for a made-up newer release
  (no request).
  **Testing an update** (nothing newer than the newest release exists to test with): `THPSource2.exe --pretend-version v0.1` compares the newest release with v0.1 instead of
  the launcher's own version, so the real newest release counts as an update and the whole path (API, download, checks, restart) runs against GitHub; `--update-now` then
  does the two clicks itself. Do it on a throwaway install (`tony release install --to DIR`), never the one you play: the update replaces `THPSource2.exe` and `Build\` with the
  published ones. `Logs\update.txt` and the files' hashes against the zip's say what happened.
- **Debug mode** (System > Debug logging, `[Launcher] DebugMode`, off by default; the launcher sets `OPENTONY_DEBUG=1`, `opentony.exe` reads it). On: every run is kept in `Logs\runs` (the newest 100 normal and 200 crashed
  folders) and listed in `sessions.txt`, `crashes.txt` gets the output of every stopped run, `notes.txt` gets a line at each start and end, and the game runs with `OT_TEXTURE_DUMP=Data\TextureDump`
  (every texture it draws, and the census) and `OT_TEXTURE_PACK_LOG=1` (run.txt: which textures a pack replaced or missed). Off: only the active session is logged: `Logs\runs` keeps the session, the one
  before it (`OPENTONY_KEEP_LOGS` has no say) and the newest three crashed ones, `sessions.txt` holds the last run's line only, and there is no `notes.txt`, `crashes.txt` or texture dump. Both modes
  write `run.txt`, `opentony.log`, `input.txt`, `build.txt`, `result.txt` and, on a hang, `watchdog.txt` and `hang.dmp` to the run's folder. A variable already set in the environment (`OT_TEXTURE_DUMP`) still wins.
- **Starting camera.** The in-level camera choice (`0x0056a860`, flipped by the view button in `CoreKit::display`) is reset to 0 as a level starts; with `OT_CAMERA=2` it is reset
  to 1, the second position.
- **Volume.** Attenuation in millibels added to what the game asks of the HAL: effect voices get master x effects (`params_of`), the CD music master x music and a
  movie's sound master x movies (one Bink stream, `BinkSetVolume`). The game's own Options > Sound levels (`0x0055fb3c` / `0x0055fb40`, 0..12) and their mixer words (`0x0052be44` / `0x0052be48`) are set to the maximum once, after the options are read, so a player who never opens the menu hears the launcher's levels; the menu's sliders still move them and multiply with these.
- **Quick start.** The scenario bootstrap's calls (`standalone_scenario.cpp`), with the free skate's mode (2; the single session's is 3, the timed one), the skater's slot, the
  skater's look (the slot's costume and the remembered one, 0 to 3: only a style the row has and has unlocked) and the rules screen's PLAY pressed. The main menu's answer (`0x26`, straight to the level select) sets the mode itself; set only at the level select it was read too late and every quick start was a single session. It watches calls only until the level has launched. Just before the level loads it also starts the CD music on a random level track (the group
  and track words are set to the one before it, as the next-track step moves on from them) and, with Max stats, writes 10 into the ten stat bytes of every character's career record (`+0x38`; `SkaterKit::profile_value` reads those, the skater rows' base stats are only the career's starting point).
- **Goals Speed Run and Competition** (`src/hooks/kits/standalone_challenge.cpp`; the runtime's side behind `host_options.hpp` `speed_run`, `judge_level`, `hold_intro`,
  all off in every gate). Both are the career (front mode 1) on the chosen level, through the same bootstrap and the same save lock as the other quick starts.
  The kit follows one call after the launch, `Game_PrepareVideoMasksWrapper` (`0x00469900`): every run's object set-up calls it just before it reads which goals
  and cash the career has (the letters, tape and pickups from `0x00568a24`, the cash icons from `Career_GotMoney`), so the career record (the slot's character,
  never saved) is set there. **The speed run** takes Hangar, School II, New York City, Venice Beach or Philadelphia (`speed_run_level`, the same list in the
  launcher: the contests have no goals, Chopper Drop and Skatepark Heaven none of their own, and Downhill Jam, Skate Park and Warehouse carry Hangar's rows while
  the game hides every item of levels 10 and up). Each run starts with the level's goal word and its two cash words cleared. The clock counts up with retail's own
  count-up mode: the byte `0x029da3a4` set, `SkaterKit::handle_timer` and `Panel::display_holding` take the skater's `+0x2cc8` as the time itself, and every poll
  writes the animation clock (60 a second of game time, still in the pause menu) less its value at the run's first poll in play, plus one (a 0 there stops the
  trick code). A speed run does not pass its goals screen (the other quick starts pulse Enter on it; `standalone_options.cpp` `player_starts_run`) but waits there for the
  player's Play, so the goals can be read, and its clock starts after it (`host_options.hpp` `goals_screen_up`, set while `GoalScreens::goals_run` loops;
  `OT_SPEED_RUN_TEST` keeps the pulse for a headless check). In any quick start the screen's Back sets `quit_to_launcher` rather than leading to the shell.
  `Panel::display_holding` drops the countdown's last-ten-seconds flash and beep, which would come in the first ten seconds. The goals are every goal
  of the level but its "100% Goals and Cash" (type 7), or, started with "Goals and cash", all ten. When the record has them the clock stops, "ALL GOALS 4:05.32"
  ("100% ..." for the 100% run) comes up as a goal message (`Mess_AddMessage` with the career's style), the line is written (`speedrun` or `speedrun_cash`), and
  2.5 seconds later the word is set to 0 with the byte cleared, so the run ends the game's own way; the career run line that end makes is not written. Retry
  starts again with no goals. **The competition** takes any level 0..12: the level's contest byte (`+0x48`) is set for the session, which gives it the 60-second
  heats, the competition object and its screens; `Competition::judge_score` judges it as The Bullring (`judge_level` 7: Bullring's thresholds 100000 / 70000 /
  40000 and its deduction); on a level whose record has no opening shot (`+0x9c..+0xac` all 0: every level that is not a contest but Chopper Drop)
  `CameraKit::point_mode` holds the shot where its first frame put it, behind the skater, instead of flying in from the world's origin; and
  `GoalScreens::loading_start`, which has a picture only for the three contests and otherwise leaves the screen's lines unset (the goals screen then read address 0:
  retail's own Skate Park contest would too), shows `loadingg.bmp` with the empty lines. Before each heat the record
  gets all ten goals and every cash icon of the level, so no letter, tape, pickup or cash icon appears and no goal message comes (score goals are not checked on a
  contest, count and gap goals test the goal first); at the first heat the level's medal bits are cleared. The kit reads each heat's trick score (`+0x16c`) when
  the judges' screen comes up (status `0x13` / `0x14`), and at the end screen of the third heat (status `0x17`) the player's three judged heats (`0x0055fe28 +
  seat * 0x14 + 0x0e`): the total is the two best, the medal the bit the competition gives (or none when the screen is left without one, or the game quits), the
  score the better trick score of the two counted heats. A cheat on at any poll of either keeps its line out of the file.
- **Quick play keeps the save file safe.** With `OT_QUICK_START` the player stays in the level and the save file is never written (`HostOptions::quick_play`, off for PLAY
  and for every gate). The end-of-run menu (`FrontScreens::init`, `0x0044be50`) shows Level Select, Update save file, Save Career and Spend Some Cash (a career's line: the speed run and the competition, which would leave for the shell's screens) greyed (CMenu's own "cannot be
  chosen" byte, `+0x26`: drawn at half brightness, the cursor passes over it), and the replay menus' Save Replay the same. The career's "Save Career?" questions after an
  unlock message (statuses 7 and `0xc`) take their No without being shown. The pause menu gets a Quit line after End Run. Quit there ("really quit ?", status 3) or on the
  end-of-run menu ("Are you sure?", status `0x11`, always asked, also where retail's network test would quit at once) sets `quit_to_launcher` on Yes, and the next input poll
  ends the program with exit code 0, as the Quit hotkey does, so the launcher comes back. Behind the menus the file calls refuse to create, write or delete a `*.SAV` or
  `*.PRK` (`user_file_locked`): the memory card's update opens become read-only, `PCcreate`, `PCwrite`, `PCremove`, the C runtime's write opens and `remove` fail, and
  run.txt logs the first refusal. Every level and skater is opened as `OT_UNLOCK` levels and skaters would, which is safe because nothing reaches the career.
- **Custom soundtrack.** Every folder under `Mods\Music` (and the folder itself) that holds a `.bik` file is a pack with a tick box; a RETHAWED music pack works as it
  comes (its `pack.ini` names each file's band and title, `[PackInfo] Name` the pack). The game chooses its tracks as it always does (`redbook_next`, `0x004abcb0`, at a song's
  end, a level's start and on the pause menu's Sound > TRACK line). When it asks the music seam for a level song (`LTIX16`..`LTIX31`) while the level loop
  (`0x0046a3a0`, marked by the resolver) runs, the seam opens the playlist's song in its place: the game's fades, volume and pause apply, and the end of the file is the end of
  the song (`xa_done`). Which way to move is read from the game's own walk: the track before the one it last asked for is a step back, the same one is the same song (a song
  restarted after a cut scene), and anything else is a step on, so TRACK skips through any number of songs (its number shows as `--`: it would name the disc track a song replaced). Shuffle plays a new random order each time round, never starting
  with the song that ended the last. With the game's soundtrack included, its level songs are entries in the same list. Menus, the credits and the level's ambience (`LTIX00`..`09`,
  what plays with the music level at 0) stay the game's. A new song puts its band and title up (Display > HUD > Show song title, with the custom soundtrack on or off) through the game's message layer (`Mess_AddMessage`,
  `0x004736f0`): its own font and its gap messages' pop (`0x00534ec8`), at the bottom left, small, about four seconds, once the level is back on screen (not under the pause
  menu, not with the HUD hidden). The font has letters, digits and a few marks: an accented letter is drawn plain, `&` as AND, anything else is left out. The game's own songs
  have no names in their files; `kGameSongs` names `LTIX16`..`LTIX30` from the soundtrack's published track list, by order (the files' lengths agree with that order: nine
  of the fifteen within three seconds of the list's, the rest within twenty-one). With the custom soundtrack off the seam only reads which level song the game asked
  for (the game's own file still plays) and names it once per level when it changes; the pause menu's TRACK line names the song it lands on.
- **Vehicle sounds.** Retail's emitters of one looping sound share its voice (`Audio_PlaySound` plays a slot on its first buffer). A car out of range asks `Audio_SetVoiceVolume`
  for silence, which stops the voice and clears its "playing" flag; the next car in range sees the flag clear and starts it again from sample 0, so two cars repeat the first
  slice of the sound every frame. PARTYMOD's fix (`sfx.c`) lets only the nearest emitter move the sound. Here the host's voice layer does it without knowing the emitters: per
  slot and tick (frame counter + animation clock) the loudest call wins (the level comes from the distance), and a silent call on a playing loop only mutes it, the stop
  happening when a whole tick passes with nobody wanting the voice. One-shots are untouched.
- **Frame rate.** The game limits a scene to one per two simulation ticks (1/60 s each): `PcSystem::begin_scene`'s wait, the only limiter in a level, is `limiter_ticks`
  of `runtime/frame_pacing.hpp` (2 = retail's 30 fps). 60 makes it 1 (120: half a tick, on the sub-tick clock; realtime waits for the limiter's own deadline with a high-resolution waitable timer, then a yield, and the clock resumes from the deadline); the game's own variable time step (`CoreKit::animation_clock_update`: `AnimationTimeScale`
  `0x0056865c` = the last three frames' ticks x 256 / 6, 128 at 60) keeps speeds the same. In realtime `deliver_tick` then does what the timer callback does with the
  wall clock's elapsed time as the step (both accumulators, the tick words truncated), since 0.96 tick a call would show a 32 ms frame every 0.4 s, and raises the Windows
  timer resolution to 1 ms for `Sleep(1)`. A counter the game keeps in frames must count 30 Hz frames, not frames: `framepacing::frame_count_step` adds
  `0x0056a940` (the game's own "30 Hz frames this frame is worth", 0 or 1 at 60) in place of 1; so far the ollie's charge (`+0x2de8`). The point timer credits
  `(step x share) >> 12` a call, so at 60 a share of 1 (a manual's point a frame) was shifted away: `framepacing::point_credit` carries the half points, and the trick script's
  rate points (command `0x17`) leave out the scale the step already applies. Skater physics (F1.5C): the ground brake no longer scales its acceleration a second time
  (`acceleration_term`), the turn accumulator's per-frame decay keeps its rate a second (`decay_amount`), `scale x N >> 8` clock windows keep N (`clock_threshold`), and a per-frame ease or
  random kick is owed in proportion (`scaled_step`, one grind-balance draw per 30 Hz frame). World objects and effects update on 30 Hz frames only (the gate in
  `CoreKit::game_gameplay_update`); cars and platforms (vtable `0x005194f8`: the Bullring's helicopter, moving and thrown props) are drawn between their last two 30 Hz states
  (`framepacing::motion_state`), their real state put back at the start of the next frame. Menus keep their own two-tick limiters (30 fps). Lockstep is unchanged: one tick a delivery, so one tick a frame.
  **Pacing and input at 60 and 120 (realtime).** The limiter's deadline is waited for at the top of the frame, in `Game_FrameTick`'s delivery, not in `begin_scene` after the
  frame's input and update: a high-resolution waitable timer, then a yield, to the deadline; the frame accumulator is set to it exactly and the clock resumes from it. The
  window's messages are taken after the wait, so the keyboard state the input poll reads is current (SDL's own message loop is off; pads are read at the poll). A loop that
  draws nothing (the menus' limiters, loading) waits for the next whole tick from its third delivery since a scene. 30 keeps retail's order and `Sleep(1)`. `OT_FPS=log`
  prints the shortest and longest frame of each half second (`video: fps 120 (frames 7.10 to 9.60 ms)`) and the window line is followed by the desktop's refresh rate
  and the vertical sync setting. The launcher warns under the frame rate when vertical sync is not off and the display's refresh is not a whole multiple of the rate
  (`frame_rate_judders`: 120 or 60 on a 144 Hz display).
- **Rumble.** `PCInput_ActuatorOn` / `Off` (the game's calls, which retail makes through DirectInput) go to the HAL's pad: PARTYMOD's mapping
  (both motors, duration x 66 ms).

## Trick sets

The game's Edit Tricks is a screen of the career's skater select, which a quick start passes over, so the Quick Start page has its own: the Session tab's Tricks row chooses a set (or
"Each skater's own", the default) and Edit Tricks... opens the page's Tricks tab, the editor. A set holds what the game's binding block holds
(`re/evidence/functions/trick-database-runtime.md`): a trick for each of the 30 button combinations of the executable's table (`0x00540cb8`) and up to five specials,
each one of the 36 special combinations (`0x00540e30`) with a special trick. One set is used for whichever skater the quick start has; PLAY never passes it and the
career's records are never written in a quick start (the save lock above), so nothing outside the quick start changes.

- **The pages** are the game's: Flip tricks (a direction or a double tap with Flip: 12), Grab tricks (with Grab: 12), Lip tricks (a direction with Grind: 4) and
  Specials, plus Grinds (Up, Up and Down, Down with Grind: 2), a page the game's menu has a label for (`0x0052e7e4`) but leaves out of its list. The tricks each row offers
  are the menu's (`TrickMenu_BuildPageList`, `0x004c21a0`): the flip and grab pages take every flip and grab trick, the lip page the lip tricks, the grinds page the grinds,
  never a special. A special on a combination ending in Grind is a grind or lip special, on one ending in Flip or Grab any other (the game's own rule: the Grind button
  selects scripts that wait for the skater to land a grind, so an air or manual special there would never start); a special trick is on one slot only and a
  special combination holds one special. The game's requirement for the hidden characters' tricks is met by a quick start, which opens every skater, so every trick of
  TRICKS.BIN is offered (105, 54 of them specials); points and buying do not apply.
- **The data** is the player's own: `core/trick_sets.cpp` reads `Data\Unpacked\data\data\TRICKS.BIN` (made by the first start; until then the row is greyed with a note)
  through the runtime's trick database (`src/runtime/trick_database.cpp`, the same code the game's table builder runs), so the names, the indices and each skater's own set
  ("Start from" and "Use their tricks": `TrickConfig_InitializeCharacter` on the skater's key stream) are the game's. The labels are the launcher's names for the
  actions (Flip, Grab, Grind; the codes 9 to 12 are Circle, Square, Cross and Triangle in the symbol font's order).
- **The file**: `Config\TrickSets.ini`, one section a set; each combination's key (`Up+Flip`, `UpRight+Flip`, `Up,Up+Grind`) names its trick by the database's name (`kickflip`;
  empty: none), `Special1`..`Special5` are `<combination>: <trick>` (`Right,Down+Grab: the 900`). It is written the moment it changes, as the settings are
  (Use their tricks and Delete ask first). A line the game could not take (an unknown name, a trick off its page, a special twice) is listed under the editor and left out.
- **The game's side** (`standalone_options.cpp`, `write_quick_tricks`): with `OT_QUICK_START`, `OT_QUICK_TRICKS` (checked whole: record indices below `0x80` or `0xff`,
  special combinations below 36, each special's two bytes both set or both empty) is written at the quick start's level select, after the skater is chosen, to the binding block
  (`+0xcc`, `0x35` bytes) of all 20 career records and to the alternate block Tony Hawk's third style reads (`0x0056a690`). The level's object set-up builds the player's
  trick table from that block (`CreateObjKit::trick_table_build`, `0x004bcf00`): the skater's other moves (manuals, grinds by direction, the rest of the key stream) stay theirs.

## Upscaled textures

Plan: `docs/post_phase_5/textureupscale.md` (THPS2 docs). The game draws a larger picture for a texture whose key (a hash of its texels) has a PNG in
`OT_TEXTURE_PACK` (the GL backend, `sdl_video_gl.cpp`). The launcher makes that set from the dump, by each key's class in the class table (`world` or `2d`):

    THPSource2.exe --upscale-textures [--model3d NAME] [--model2d NAME] [--scale 2|4] [--alpha model|bicubic] [--keys FILE]
                            [--dump DIR] [--table FILE] [--out DIR] [--tool EXE] [--models DIR]

`NAME` is a model in `--models` (`NAME.param` + `NAME.bin`, run by the ncnn tool over a folder at a time), `nearest` (built in, no tool) or `none` (that class is
left out). Defaults (the person's pick, T4.5): `ultrasharp-4x` for the world, `nearest` for 2D, the model's alpha, 4x, `Data\TextureDump`, `Data\Upscaler\texture_classes.tsv`, `Data\Upscaled`,
`Data\Upscaler\upscayl-bin.exe`, `Data\Upscaler\models`. Progress goes to the console it was started from and to `Logs\upscale.txt`; the exit code is 0 when
every selected texture was written. It opens no window and makes no SDL call.

The pipeline is `core/upscale.cpp` (portable; PNGs through `core/png.cpp` on zlib), natively tested by `opentony_upscale_test` with a stub model. Per texture:
world textures under 16 a side are left out (2D ones never are); transparent texels take their neighbours' colours (bleed); a 16-texel border is added (tiled for a
texture drawn wrapped or of unknown wrap, edge-repeated for a clamped one); each class goes through its model in batches of 400 (colour as RGB); the border is
cropped; 2x is the mean of 2 x 2 of the 4x result; each original texel's area gets the original's mean colour back (models drift); a world texture's alpha goes
through the model as a grey picture (or bicubic), a 2D texture's by nearest (a glyph keeps its outline). `stamp.txt` names the tool (file name and size), both
methods, the scale, the alpha method, the pipeline version and the class table's hash: a run under the same stamp skips what is there (a stopped run resumes),
another stamp removes the old set first. The launcher's button runs it after the download and the census (below).

With "sharp menus and HUD" on, a replaced 2D texture is snapped to its picture's texels, not the original's (`u_sharp` takes the real size).

**The button** (T5; `app/upscale_task.cpp`, `core/upscale_setup.cpp`). Graphics > "Upscale textures..." opens the Upscale Textures window: the status (the upscaler
installed or not, the game textures found, the upscaled count and MB), the scale (4x for 1440p and 4K, 2x for 1080p, with the disk estimates of T4.5, about 710 MB
and 180 MB; "Recommended for your display" on 4x when the launcher's resolution, or the desktop's, has 1440 lines or more, else on 2x, and with no set yet the
window opens on that one), the components (the tool and the chosen world model, their licences, the download size), Start (Update once a set exists)
and Delete. Start runs, in the task window with its progress bar (download MB, census step of 168, textures upscaled of the run's total, from the
pipeline's `progress` callback) and Cancel:

1. **Download** what is missing into `Data\Upscaler` with Windows' own `curl.exe` (the zip unpacked with `tar.exe`, `LICENSE-upscayl-bin.txt` kept beside it),
   each file checked against its pinned size and SHA-256; a wrong one is deleted and named.
2. **The census**: every run of the shipped recipe (`re/config/texture_census.yml`, 168 runs: every level in both texture modes, every skater and look, the
   menus), `Build\opentony_spike.exe` started directly with no window (`OT_TEXTURE_DUMP` and no `OT_VIDEO_SDL`: recorded-only video), a lockstep clock and the
   session limit, each run into `Data\TextureDump\runs\<name>\` with its own empty saves and config folder (a career run never touches `Saves\` or the player's
   options), several side by side, the environment the launcher's own minus every `OT_` / `OPENTONY_` variable (`run_isolated`). A run that writes `session.done`
   and does not exit is ended 15 s later (seen under Wine since T3); one that runs 20 minutes is ended and counted failed. The runs' dumps are merged into
   `Data\TextureDump` (a dump there already, from Debug mode, is kept; a known key's use, wrap and mod2x are widened as `scripts/texture_census.py` merges), then
   `census.done` holds the recipe's hash and `runs\` is removed. A finished census is not run again; a stopped one goes on from the runs it finished.
3. **The pipeline** above, by the person's pick (`re/config/upscaler.yml` `chosen`), with the class table written from the copy compiled into the launcher
   (`Data\Upscaler\texture_classes.tsv`, byte for byte the repo's, so a set made before keeps its stamp).

The recipe, the class table and the pins are compiled in from `re/config` by `scripts/make_upscale_tables.py` (`core/upscale_tables.inc`;
`tests/test_upscale_tables.py` keeps them equal). The same steps from a console: `THPSource2.exe --download-upscaler`, `--texture-census [--game DIR]`
(both into the install's folders; progress on the console and in `Logs\upscale.txt`), then `--upscale-textures`.

**The checkbox** "Upscaled textures" (`[Graphics] UpscaledTextures`, off by default) sets `OT_TEXTURE_PACK` = `<install>\Data\Upscaled` at the next PLAY when a set
exists there (`stamp.txt`) and textures are smooth with texture detail on (mip maps: without them a replaced texture shimmers when minified); greyed with a hint
otherwise. Unticking only stops the game reading the set. When an upscale run finishes the launcher ticks it (when the smooth filtering it needs is on) and says
"Upscaled textures are ready." in the footer. The Original PC and Original PlayStation presets switch it off; Enhanced switches it on once a set has been made. A pack named in the
launcher's own environment (the test `.cmd` files of T2 to T4.5) still wins over the setting.

## True-colour textures

Plan: `docs/post_phase_5/graphics2.md`, G5. The PC port's level textures (`newtex\`, 1,458 files) and some skater parts (`newbmp\`) are 24-bit bitmaps, but `TextureKit`
(`upload_rgb_pixels`, 0x004d9310) writes them into a 16-bit 1555 surface, five bits a channel, and the backend stores that as it is. `core/true_colour.{hpp,cpp}` reads each
24-bit bitmap of `Data\Unpacked\data\newtex` and `\newbmp` (up to 256 a side, bottom row first, the key colour R > 0xf7, G = 0, B > 0xf7 written as black with alpha 0), builds the
1555 surface the game builds, takes its key (`src/hal/texture_key_hash.h`: the same hash the dump and the pack use) and writes the bitmap's own 8-bit texels as
`Data\TrueColour\<key>.png`, opaque unless the bitmap has key-colour texels. No dump and no census are needed: the key comes from the file. The GL backend's texture pack then draws
that picture for the texture (the cut-out class is taken from the original, so fences keep their edges). About 1,500 files, 38 MB, 10 s through WSL (a few seconds native); a bitmap
that no level loads (the park editor's and Bullring's unused ones) writes a file nobody asks for.

**The upscaler starts from them.** `upscale::Options::originals`: a `<key>.png` there of the dump's size is the original the pipeline bleeds, pads, upscales and colour-matches
instead of the dump's 15-bit one. The button (`run_upscale_task`) makes `Data\TrueColour` first when it is missing or old and passes it; `--upscale-textures` uses it when it
is current and the dump is the install's. The stamp gets a line `originals true-colour`, so a set made from the dump alone is another set (and is replaced at the next run).

`OT_TEXTURE_PACK` may name up to four folders joined by `|`; the first that holds a key supplies the picture. The launcher passes `<upscaled>|<true colour>`, so an upscaled
picture wins for a key both hold. The set is made at PLAY (the thread that starts the game, one log line) when `stamp.txt` is missing or names another version or another number
of bitmaps; "True-colour textures" is greyed with the PlayStation textures (15-bit at source). Measured: 1,255 keys of the set are keys of the census dump (the rest are textures the
census never loads); in Hangar and NYC (headless, 960 lines) 84 and 63 textures are drawn from it and a frame differs from the 15-bit one by at most 5 of 255 (the cut loses up to 7),
visible as mottling in smooth concrete and sky when the contrast is stretched.

## Scores

Plan: `docs/post_phase_5/highscores.md` (THPS2 docs). The game keeps its score tables in the save image, so without a loaded save they are lost. The score file
keeps every finished run instead, with no save: `<install>\Saves\highscores.tsv`, UTF-8, tab separated, one line a run, appended by the game the moment a run
ends and never rewritten. The format, the writer and the reader are one header, `src/launcher/scores.hpp` (standard library only: the launcher and the game's DLL
include it):

    # THPSource2 scores v1
    date	mode	level	level_name	skater	skater_name	initials	score	time_ms	judged	medal
    2026-11-15 21:04:33	single	12	Warehouse	0	Tony Hawk	AFC	48210
    2026-11-20 18:02:10	speedrun	0	Hangar	0	Tony Hawk	AFC	912400	245320
    2026-11-20 18:20:41	competition	3	New York City	0	Tony Hawk	AFC	151230		1786	gold

`date` is local time; `mode` is `career`, `single`, `speedrun` (a Goals Speed Run to every goal but the 100% one), `speedrun_cash` (one to 100%) or
`competition`; `level` 0 to 12, 13 for a park editor level ("Custom park"); `skater` is the game's character number; `initials` keep their spaces. A speed run's
`time_ms` is its game time and its `score` the trick score when it ended; a competition's `judged` is the judges' total in tenths (178.6), `medal` gold, silver,
bronze or none, and `score` its better counted heat. Career and single lines stop at `score`. The column line is read by name, so later versions can add columns
(unknown ones are passed by); a file whose column line has not the last three gets the column line again before its first speed run or competition. A line that
cannot be read (another mode, a level or score that is not a number in range, too few columns, a speed run without its time, a competition without its total) is
skipped and counted; `#` lines are comments. The file is the player's: they may edit or delete it. The memory card's scan of `*.SAV` passes it by.

**What is recorded** (`src/hooks/kits/score_recorder.hpp`, fed by `standalone_scores.cpp`). `FrontScreens::switch_on_end_level` (`0x0044e0f0`) calls a host hook
(`runtime/score_hooks.hpp`, null unless `OT_SCORES_FILE` is set) after it has entered a run of game mode 1 (a career level that is not a contest) or 3 (a single
session) in the level's table of eight: the timer running out, End Run, a quick start's run too. A line is written when the run had one player, a score above 0
(player `+0x16c` at the call, the number the table got), and no cheat on at any poll in a level (status 0 or 1) since the previous run ended: the cheat list
`0x0056a374`, entries 3-6 and 9-16 on when `+2` is not 0, entry 8 (Weight) when it is not 100; STUD (max stats) and the skater unlocks are allowed. A second end with
no play between is ignored (one run, one line). Free skate, contests, two players, the horse game and replays are never recorded. `run.txt` says why a run was not.
The quick start's speed runs and competitions are written by the kit itself when they finish (`Recorder::session`, with the remembered initials: there is no
entry screen), and a speed run's own career run ends are not.

**Initials.** A run that placed is held until the entry screen's accept (status `0x2e`, a second hook before the place `0x0052bf34` is reset), so the line has the
letters the player chose. If the entry is left another way (the place reset without an accept, the next run ending, the program ending: the window closed, Quit,
the game's exit, `ExitProcess` through the DLL's exit list) the run is written with the letters it started from. A crash loses at most that one held run.

**The game's own tables** (`OT_SCORES_IN_GAME`). The file is read once at the first poll; each table keeps its 16 best runs (and this session's as they are written).
At every poll with no entry going on (the place -1, the status not `0x2e`, no run held), each table already set up (any score not 0) becomes the best eight of its
own rows and the file's runs for that level (career and single together, as the game keeps one table a level; never a speed run or a competition), table rows first on a tie; a run already in the table (the
same letters, skater and score: the game's own insert, or a save that holds it) is not put in twice. So the boot's reset, a new career and a loaded save all get the
file's runs back at the next poll, with no hook on them. Only bytes that differ are written; no game function is called and the random number generator is not
touched. A run whose skater number is not 0..19 or whose score is above `0x70000000` is left out; letters other than A to Z become spaces. Side effects: a career
saved now keeps the merged tables in its `.SAV` (loading it later matches them, nothing doubles); a run must beat the merged table to reach the entry screen, so
fewer runs do than against the game's random default names; those names stay below the player's runs until pushed out.

**The High Scores page** (`app/main.cpp` `scores_page`, `View::Scores`; the header's gold "High Scores" button or `--page scores`) reads the file when it first opens and again when
the game returns, and always opens on All. Three boards: Scores (career levels and single sessions together, by trick score; the mode is not shown), Speed Runs
(by time, the fastest first; a second row chooses "All goals" or "100%", the two kept apart) and Competitions (by the judges' total, then the trick score; the
medal and the best heat shown). A speed run's open-ended score and a competition's one-minute heats are not on Scores. On each board the level list shows All
(the best run of each level, best first; clicking a row shows that level's table and sets the level list to it) or one level's best 20 (the 13 levels, and Custom
park when the file has one), with a "Back to All" button beside the list. There is no list of recent runs. The runs of the session that just ended are marked "New". "Open Saves folder" opens `Saves\`; there is no delete: the
file is plain text. The initials are a setting, on the Gameplay tab.

## Not done

- PARTYMOD's per-level depth-bias and push-back tables (`gfx_hacks.c`, model-id and texture-checksum lists): they correct z-fighting of PARTYMOD's own
  Vulkan renderer; nothing of the kind has been seen here. Compare against its list if a level shows it.
- Its THPS1 career, and the other mod folder's level sets (THPS1/3/4, Mat Hoffman levels: file replacements plus exe patches that need mapping to the
  Value build).

## The binding rules

The launcher writes only what the game's reader accepts. That reader is `PadInput::load_bindings` (`0x004e4d10`, in
`src/runtime/pad_input.cpp`); a refused file makes it put every binding back to its defaults. The rules follow from that reader, and
`core/bindings.cpp` simulates it on the retail defaults:

- **Key names.** An action has up to three key names, separated by commas with no spaces, matched without regard to case against the
  256-name table at `0x0052eff0`. The launcher reads that table from the player's `THawk2.exe`, or from `opentony\thawk2_static.img`;
  it keeps no game data of its own. Code 3 is named `"2 "`, and `GetPrivateProfileString` trims the trailing space, so the game cannot
  read it back. The launcher refuses that key, as it does any name that comes first under another code.
- **Empty slots.** A slot left out (or written `-?-`) keeps its *default* key, so a binding cannot be cleared by omitting it. A chord
  is read only up to its first empty slot.
- **The "taken" check.** A key counts as taken when another action has it *in a different slot*. Actions read later still hold their
  defaults at that point. Moving a default key (for example `P`, Pause's slot 1) to an action the game reads earlier works only if the
  key is written in the same slot.
- **Spare slots.** The writer fills each spare slot with a repeated key, an empty default, or (only where neither passes the check) an
  inert *filler* key: a name the game knows that no physical key produces (`src/hal/keymap.cpp` never emits it). Fillers are left out
  when reading.
- **Pad buttons.** The `[JOYPAD]` values are button numbers from -1 to 32, none used twice. The standalone reports 12 buttons
  (`standalone_platform.cpp write_joystick`): 0 X, 1 A, 2 B, 3 LB, 4 Y, 5 RB, 6 LT, 7 RT, 8 Back, 9 Start, 10 L3, 11 R3. The d-pad
  and the movement stick move; they are not bindable.

## Checking it

- `ctest --test-dir build/native -R launcher_core` runs the core test (the trick catalog's checks with `build/assets/all-pkr/files/data/TRICKS.BIN`, or `OPENTONY_TRICKS_BIN`). Set `OPENTONY_THAWK2_EXE=<THawk2.exe>` and
  `OPENTONY_DISC_IMAGE=game/THPS2.img` to add the checks on the real exe and disc.
- `THPSource2.exe [--size W,H] [--hover X,Y] --page display|graphics|audio|controls|gameplay|quickstart|speedrun|banner|session|scores|tricks|specials|system|reset|setup --screenshot shot.bmp` draws the page, saves the
  picture and quits (`gamepad`, `keyboard` and `hotkeys` open that page of Controls; `tricks` and `specials` Edit Tricks on its first tab and on Specials; the old `game`, `sound` and `files` still work; `--size W,H` is the window's size, `--hover X,Y` puts the pointer there, so a tooltip can be seen; `reset` shows the Reset all question; `session` the High Scores page with the file's last run taken as the session's).
- `THPSource2.exe --play` starts the game at once with the saved settings. It also works as a desktop shortcut that skips the
  launcher's page.
