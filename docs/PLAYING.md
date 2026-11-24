# THPSource2: Tony Hawk's Pro Skater 2 on its own executable

THPSource2 contains no game data. You need your own copy of the PC "Tony Hawk's Pro Skater 2", either release:

- **The Activision Value re-release**: `THawk2.exe` with SHA-256 `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c` (on the Value disc, in `SETUP\data`).
- **The original 2000 release**, with its *unprotected* `THawk2.exe` (SHA-256 `003152194111a3e564c1a458da2a02cc1187a4f47c147a4126f5d37913cfee8a`, 1,460,224
  bytes; the one PARTYMOD and the THPS2 Cumulative Patch use). The `THawk2.exe` on the original disc itself is copy-protected (SafeDisc), its data is encrypted
  and it cannot be read: import the disc anyway (everything else on it is used) and put the unprotected `THawk2.exe` in `Data\Game` in its place.

Both play the same game: the levels, music and movies on the two discs are the same files, and THPSource2 reads the original release's tables and text at the
places the re-release keeps them.

## Start

Run **THPSource2.exe**. It asks where your game files are (an installed game folder, the disc, or a disc image: `.iso`, `.bin`, `.img`), copies them
into `Data\Game` if needed, unpacks them once, and keeps your settings: window, fullscreen, resolution, VSync, keyboard and controller bindings. Press PLAY.
`THPSource2.exe --play` starts the game straight away (a desktop shortcut can use it).

## The folders

```
THPSource2.exe   the program you start
README.md                 this page
Build\                    the programs the launcher runs: opentony.exe, the game's engine, SDL3.dll, the movie and music decoders
Config\                   THPSource2.ini (the one settings file: the launcher's and the game's own, with your key and pad bindings), TH2_OPT.CFG
Data\Game\                your own game files: THawk2.exe, ALL.PKR, Music\, med\Movies\
Data\Unpacked\            what the first run reads out of them (about 200 MB); safe to delete, it is made again
Data\TextureDump\         the game's own textures, found by "Upscale textures..."; safe to delete
Data\Upscaler\            the upscaler and its model, downloaded by "Upscale textures..."; safe to delete
Data\TrueColour\          the PC textures in full colour, made at the first Play; safe to delete, it is made again
Data\Upscaled\            the upscaled textures (about 710 MB); safe to delete, or use the launcher's "Delete upscaled textures"
Saves\                    career and skater saves (*.SAV), custom parks (*.PRK), highscores.tsv (every finished run, the High Scores page)
Mods\                     loose files that replace the game's own files of the same name
  Music\                  your own soundtrack (.bik songs, loose or in folders), switched on in the launcher's Sound page
Screenshots\              F8 saves a picture here
Logs\                     sessions.txt, one folder per run, reports\ (crash zips); with Debug mode also notes.txt, crashes.txt
Licenses\                 THIRD_PARTY_NOTICES.md and the licence texts
```

Keep a copy of `Saves\` before you try a new build; a save the program cannot read shows as a message on the main menu.

## Options

The launcher's tabs: **Display** (window, resolution, aspect ratio, frame rate up to 120, V-Sync, HUD), **Graphics** (every setting on its own),
**Audio** (volume, your own soundtrack), **Controls** (controller, keyboard, hotkeys), **Gameplay** (autokick, camera, unlocks),
and **System** (game files, logs). Hover over an option to see what it does. Every change is saved as you make it; there is no Save button. The green **Quick Start** button beside PLAY goes straight into a level as
any skater, without touching your save; the gold **High Scores** button at the top right shows every run you finished. The Quick Start page has two tabs, Session and
Tricks; Back to settings (or Esc) returns. The High Scores page is a page of its own, so Quick Start and PLAY stay where they are. When the game returns after a run that set a new high score, the gold High Scores button pulses and names the score beside
it for a few seconds.

The Session tab has four ways in: **Free Skate**, **Single Session**, **Goals Speed Run** and **Competition**. A Goals Speed Run is a career run on Hangar, School II,
New York City, Venice Beach or Philadelphia with no goals done and no time limit: the clock counts up (it stops while paused). Clicking it shows how the clock
stops, and the button you click there starts the run: **All goals** (every goal but the 100% one) or **Goals and cash** (100%: every goal and every cash icon). The
level loads to its goals screen and waits there, so you can read the goals first: **Play** starts the run (the clock starts then), **Back** returns to the launcher. A
Competition is three one-minute heats on any level, judged as hard as The
Bullring, with that level's goals and cash hidden. Neither touches your save.

The game's Edit Tricks cannot be reached from a quick start, so Quick Start has its own: the Session tab's **Tricks** row chooses a trick set (or each skater's own), and
**Edit Tricks...** (or the Tricks tab) opens the editor. A set is the trick on every button combination (Flip tricks, Grab tricks, Lip tricks, Grinds) and up to five specials, any trick of
the game on any skater; "Start from" fills it with a skater's own tricks. Whoever you pick skates with that set in every quick start mode. Sets are kept in
`Config\TrickSets.ini`; PLAY and your career never use them.

Scores: every career level, single session, speed run and competition you finish is written to `Saves\highscores.tsv` the moment it ends, with no save needed
(quick starts too; not free skate, and not a run with a cheat on). The High Scores page has three boards: Scores (career levels and single sessions), Speed Runs
(the fastest times; All goals and 100% apart) and Competitions (the judges' total and your medal). Each shows the best run of every level (click one for that
level's best 20). "Scores in the game" puts your best career and single runs into the
game's own high score tables (Options > High Scores) as well. The file is plain text: open it in a spreadsheet, or edit it.

True-colour textures (Graphics, on by default in Enhanced): the PC version's textures are 24-bit pictures that the game cuts to 15 bits of colour. This draws them as they
are. The first Play after an import takes a few seconds to prepare them.

Upscaled textures: Graphics > "Upscale textures..." downloads an AI upscaler (about 36 MB, from GitHub), finds every texture of your copy of the game, and makes
sharper ones on your PC (a graphics card with Vulkan is needed; 15 to 30 minutes the first time). Then tick "Upscaled textures". The world gets the AI model's
version, the HUD and menus their own pixels at 4x.

Presets (bottom left, beside Save as... and Delete...): **Enhanced** (the default) turns everything up, including 4x supersampling, at 60 fps by default for broad compatibility (raise it to 120 on the **Display** tab if your PC can handle it); on a slower GPU,
lower Supersampling first. **Original PC** is the game as it ran on a PC; **Original PlayStation** is the console on a TV (its own textures, dither and a CRT picture on a curved screen).
A preset sets the picture, HUD, frame rate, graphics and a few gameplay options (camera stick, autokick, vibration, intro movies), never your window, V-Sync, sound,
controls or unlocks. Change anything and Save as... keeps it as a preset of your own.

Controller: an Xbox (or any SDL-supported) pad works without setup. Set the buttons in the launcher's Controls tab or in the game's own controller options.

Mods: loose files in `Mods\` replace the game's own files of the same name (docs/HOOKS.md, "Mods").

Your own soundtrack: put Bink (`.bik`) songs in `Mods\Music\`, loose or in folders (a RETHAWED music pack works as it comes, its `pack.ini` names the songs),
and switch it on in the launcher's Audio tab. They play in levels in place of the game's songs; each folder has a tick box, and there are Shuffle, "Include original
soundtrack". In a level, the pause menu's Sound > TRACK skips to the next song (left: the one
before). The menus keep the game's music.

Song titles: the launcher's Display > HUD > "Show song title" puts the band and title at the bottom left when a song starts in a level, for the game's own
songs as well as your own.

## Updating

When it opens, the launcher checks GitHub for the latest release (System > Updates; **Check now** checks at once). If there is a newer one, a notice at the top shows what
is new in it, with three buttons: **Update now** downloads the release and checks it (nothing changes yet), then **Restart and install** closes the launcher, puts the new
files over this folder and opens it again; **Release page** opens the release on GitHub; **Dismiss** hides the notice until a newer release comes out. `Config\`, `Data\`,
`Saves\`, `Mods\`, `Screenshots\` and `Logs\` are not touched, so your settings, saves, imported game files and screenshots are kept. `Logs\update.txt` says what the
update did. To update by hand instead, close the launcher, unzip the new release and copy everything in its folder over this one, replacing files when asked.

The check is two requests to github.com (the release's page, and its notes) made with Windows' own `curl.exe`; they show your IP address and nothing else. Untick **Check
for updates** and the launcher never touches the network on its own; `--no-update-check` on the command line skips it for one start.

## Reporting a problem

If the game crashes or hangs, the launcher shows a red panel when it comes back. **Report on GitHub** zips that run's logs (with the note you typed in the box,
saved as `what_happened.txt`) into `Logs\reports`, selects the zip in Explorer and opens the bug report form on GitHub in your browser: drag the zip into the form.
Nothing is sent for you. The logs hold file paths from this PC, such as the install folder, so open the zip first if you want to check what is in it. For a
problem that does not crash, **System > Problems > Report on GitHub** opens the same form; turn on Debug logging, reproduce it, and attach the newest folder of
`Logs\runs` zipped.

## Without the launcher

`Build\opentony.exe` does the same work from a command line: `--root <install folder>` (default: the folder above `Build`), `--game-dir <folder with
THawk2.exe and ALL.PKR>` (default: `Data\Game`), `--prepare` (only do the first-run work), `--force` (do it again), `--movies`. The first run reads the static
data out of `THawk2.exe` and unpacks `ALL.PKR` into `Data\Unpacked`. After that `THawk2.exe` is not used any more.

## Licence, credits, and the game

THPSource2 is free software under the MIT licence (`Licenses\THPSource2-LICENSE.txt`). It is based on **OpenTony** by Joao Matos (tritao),
https://github.com/tritao/OpenTony. No file of the game is included: you need your own copy. Tony Hawk's Pro Skater is a trademark of Activision;
THPSource2 is a fan project, not affiliated with or endorsed by Activision, Neversoft or Tony Hawk.

## Third-party software

SDL3 (window, input, sound; zlib licence), zlib, Dear ImGui and the settings layout of PARTYMOD (both MIT), the llvm-mingw runtime, and **FFmpeg** 8.1.3 (the
movie and music decoder; LGPL 2.1 or later). FFmpeg is the unmodified source from https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz, built as the three shared
DLLs in `Build\` (`avutil-60.dll`, `avcodec-62.dll`, `avformat-62.dll`); you may replace them with your own build of the same major versions. The licence texts
and `THIRD_PARTY_NOTICES.md` are in `Licenses\`.

## Logs (for whoever debugs this, human or AI)

All in `Logs\`. By default only the active session is kept: its run folder, the one before it and the newest three crashed ones, and `sessions.txt` holds the
last run's line. **Debug logging** (the launcher's System tab) keeps every run instead (the newest 100 normal and 200 other folders), appends to `sessions.txt`, and
writes `crashes.txt` and `notes.txt`. A run that crashes or hangs is zipped into `Logs\reports\` from the launcher's red panel, with a box for what you were doing.

| File | What it holds |
|---|---|
| `sessions.txt` | one line per run: time, NORMAL QUIT / CRASH (exit code) / HANG SUSPECTED, seconds played, the build's date, its folder, and the last error line |
| `crashes.txt` | (Debug mode) the full output of every crashed run, appended |
| `notes.txt` | (Debug mode) yours: the launcher adds a "run started" and a "run ended: <result> -> <folder>" line around every run; whatever you type under them is tied to that run. Type it while playing |
| `runs\<date_time>_NORMAL-QUIT`, `_CRASH-<exit code>`, `_HANG-SUSPECTED-then-quit` | one folder per run (below) |

Inside a run folder:

- `run.txt`: everything the game printed
- `input.txt`: every key, pad button and window event with its time in seconds and the input poll it fell in (the evidence of what was pressed; `scripts/input_log_to_script.py` turns it into a headless replay)
- `build.txt`: date and size of the program files that ran
- `result.txt`: this run's `sessions.txt` line
- `watchdog.txt`, `hang.dmp`: only after a hang (the game pumped no events for 60 s); `hang.dmp` (a minidump) holds every thread's stack. The game is left running.

With Debug mode, set `OPENTONY_KEEP_LOGS=0` to keep none of the normal folders. Please also note the screen and time of
anything odd that does not crash or hang.
