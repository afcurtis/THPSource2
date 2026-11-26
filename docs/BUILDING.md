# Building and running THPSource2

This is the whole path from a clean checkout to the game running on Windows. `.github/workflows/native.yml` runs the same commands (steps 1-3 and 5).

THPSource2 contains no game data. You supply your own copy of the PC game: the **Activision Value re-release** (the one on the Value disc, `SETUP\data`):

| File | Where it is | Identity |
|---|---|---|
| `THawk2.exe` | the game folder | SHA-256 `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`, 1,450,035 bytes |
| `ALL.PKR` | the game folder | the package of the same disc |
| `med\Movies\*.dat`, `Music\*.dat` | the game folder | movies and music (Bink; optional: a movie or track that cannot be opened is logged and skipped) |

The original 2000 release works too, with its unprotected `THawk2.exe` (SHA-256 `003152194111a3e564c1a458da2a02cc1187a4f47c147a4126f5d37913cfee8a`, 1,460,224 bytes): `ALL.PKR`, the music and the movies are the same files, and the first run rebuilds the static data at the re-release's addresses (`re/xref/original_2000_static.yml`, `re/evidence/xref-static-original-2000.md`). The copy-protected `THawk2.exe` on the original disc (SafeDisc, data encrypted) and any other build are refused, with the reason.

## 1. What you need

The Windows program is cross-compiled with a pinned clang (llvm-mingw), so the *build* runs on Linux; on Windows, use **WSL2 with Ubuntu 22.04 or later**. The *game* runs on Windows 10/11, x86-64 (a 32-bit build exists too: `--arch 32` below).

```bash
sudo apt-get install -y git python3 python3-venv cmake ninja-build make xz-utils zlib1g-dev curl
git clone <this repository> THPSource2 && cd THPSource2
python3 -m venv .tools/venv && .tools/venv/bin/pip install -e ".[dev]"
```

Everything else is downloaded, size- and SHA-256-checked against the pins in `re/config/*.yml`, and kept under `.tools/`: `tony setup llvm-mingw` (compiler), `tony setup sdl3` (window, input, sound; zlib licence), `tony setup ffmpeg --arch 64` (decode-only FFmpeg 8.1.3, LGPL, built from the release source for the Bink movies and music; about ten minutes), and zlib 1.3.1 source (fetched by the build). `tony hooks build` runs all of them when they are missing.

## 2. Build

```bash
./tony.sh hooks build --arch 64          # build/hooks/kits64: opentony.exe, opentony_spike.exe, opentony_standalone.dll, opentony_spike_video.dll
./tony.sh release package --arch 64      # build/release/thpsource2-win64/ and thpsource2-win64.zip, laid out as an install folder (below)
./tony.sh release install --to /mnt/c/Games/THPSource2 --game-files <folder with THawk2.exe, ALL.PKR, Music, med/Movies>
                                         # the same layout anywhere; an existing install keeps its Config, Data, Saves, Mods, Screenshots and Logs
```

The install folder holds `THPSource2.exe` and `README.md` (docs/PLAYING.md), and folders: `Build\` (opentony.exe, the standalone's programs, SDL3.dll, the FFmpeg DLLs), `Licenses\` (`THIRD_PARTY_NOTICES.md` and the licence texts), and the player's own, empty in a release: `Config\` (`THPSource2.ini`, `TH2_OPT.CFG`), `Data\Game\` (`THawk2.exe`, `ALL.PKR`, `Music\`, `med\Movies\`), `Data\Unpacked\` (made on the first run), `Saves\`, `Mods\`, `Screenshots\`, `Logs\`. The layout is `src/launcher/layout.hpp`; `docs/LAUNCHER.md` says what reads each folder.

`--arch 32` builds the 32-bit program the same way (`build/hooks/kits`; note that `./tony.sh hooks build` with no `--arch` also builds the *hooked* retail-process build, which needs the Visual C++ toolchain of `tony setup vc6` and is only the differential reference of the test gates).

The zip is everything a player needs and nothing of the game.

## 3. Test

```bash
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native -j"$(nproc)"
ctest --test-dir build/native -j"$(nproc)" --output-on-failure   # the reconstruction against recorded retail fixtures
.tools/venv/bin/python -m pytest -q                              # tests that need game files or Wine skip themselves
```

The retail fixtures (`re/evidence/fixtures/`) are not in the repository: they hold the retail game's memory, so you capture them from your own
copy (`re/evidence/fixtures/README.md`). Without them the ctest cases that replay one are disabled and the pytest modules that read them are skipped. The 19 ctest cases that read the unpacked game (`build/assets/all-pkr/files`, from `tony media extract`) or `ALL.PKR` (`build/disc/files/SETUP/data/ALL.PKR`) report **Skipped** when those files are missing; `OPENTONY_ASSET_ROOT` and `OPENTONY_ALL_PKR` point them elsewhere (`tests/test_assets.hpp`).

## 4. Run on Windows

### Native Linux host and SDL3 backend

The Linux host can load the standalone's static data and initialize guest
memory. From a checkout with the Python tools installed and the PC game
files extracted:

```bash
./tony.sh hooks static-data --exe /path/to/THawk2.exe
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --target opentony_linux_host -j"$(nproc)"
build/native/opentony_linux_host --check-static-data build/static/thawk2_static.img
build/native/opentony_linux_host --check-initializers build/static/thawk2_static.img
```

`THawk2.exe` supplies data for the image. Both checks run without Wine.
`--check-initializers` also attaches the reconstructed functions and runs
the game's C++ initializer table. The check ends before platform startup
and the game loop.

The top-level build can compile the SDL3 window, input, clock, audio and
graphics backends against the system SDL3 library. This target is groundwork
for a Linux standalone executable. The playable host described below still
requires Windows.

With SDL3 3.4 development files and a working OpenGL 3.3 implementation:

```bash
cmake -S . -B build/native-sdl -G Ninja -DOPENTONY_BUILD_SDL=ON
cmake --build build/native-sdl --target opentony_texture_pack_test -j"$(nproc)"
SDL_VIDEODRIVER=offscreen OPENTONY_NATIVE_TEXTURE_TEST="$PWD/build/native-sdl/opentony_texture_pack_test" \
    .tools/venv/bin/python -m pytest -o addopts= -q tests/test_texture_pack_backend.py -k native
```

Omit `SDL_VIDEODRIVER=offscreen` to use your desktop's video driver. The test
creates an OpenGL context and checks rendered pixels without game data.

### Windows standalone

1. Unzip `thpsource2-win64.zip` anywhere.
2. Run `THPSource2.exe` (it finds or imports your game files into `Data\Game`), or `Build\opentony.exe` from a command line (`--root <install folder>`, `--game-dir <folder with THawk2.exe and ALL.PKR>`).

The first run does the two one-time reads of your own files, into `Data\Unpacked\`:

* **static data**: `thawk2_static.img`, the initialised tables, strings and constants of `THawk2.exe` at their retail addresses (the code is not read: the port has none of it). The same bytes `tony hooks static-data` writes. Afterwards `THawk2.exe` is no longer needed.
* **the package**: `ALL.PKR` is unpacked to `Data\Unpacked\data\` (about 200 MB, a few seconds to a minute).

Both are repeated only when the input file changes. `opentony.exe --prepare` does only these reads; `--force` redoes them. From a build tree, `cmake -DOPENTONY_GAME_DIR=<game folder> ...` and `--target opentony_prepare` do the same as a build step, with that folder as the install root (`-DOPENTONY_PREPARE_RUNNER=wine` on Linux).

Mods (loose files under `Mods\`), settings (`Config\THPSource2.ini`), logs (`Logs\`): `docs/HOOKS.md` "Mods", `README.md` (docs/PLAYING.md).

## 5. Recorded scenarios (needs the game files and Wine)

`./tony.sh gates full` builds everything above, runs ctest and pytest, and plays 14 recorded scenarios (five sessions from the boot to the menu, the options, create-a-skater and two careers; nine Warehouse runs) through retail under Wine and through the standalone on both the 32-bit and the 64-bit build, comparing every frame, clock record and state block. It takes about 15 minutes on 12 threads and is the check behind every commit (`AGENTS.md`). Its setup (the extracted disc, Wine prefixes, the recorder) is `./tony.sh media extract`, `./tony.sh prerequisites bootstrap`, `./tony.sh hooks build --capture` (the in-process recorder, `build/capture/win32`), `./tony.sh doctor`, and `docs/TOOLING.md`. CI cannot run it (it needs the proprietary files): run it on your own machine.

## 6. Releasing

The launcher's update notice (docs/LAUNCHER.md, "Updates") finds the newest release by its tag, so a release has to be made the way it expects:

1. Set `kVersion` in `src/launcher/thpsource2/app/main.cpp` to the new version: `v` and one to four numbers (`v0.8`, `v1.0.2`). The release's tag is the same string. A tag with a
   suffix (`v0.8-rc1`) is not read as a version; publish a pre-release as a GitHub *pre-release*, which `releases/latest` skips anyway.
2. `./tony.sh hooks build --arch 64`, then `./tony.sh release package --arch 64`: `build/release/thpsource2-win64.zip` and its `.sha256`. Write the release notes in a file of your own
   (`build/release/RELEASE_NOTES.md` is untracked).
3. Commit, tag and push: `git tag v0.8 && git push origin main v0.8`.
4. Publish it as a release (not a draft), on a public repository:

   ```bash
   gh release create v0.8 build/release/thpsource2-win64.zip build/release/thpsource2-win64.zip.sha256 --title "THPSource2 v0.8" --notes-file build/release/RELEASE_NOTES.md
   ```

5. Check what a launcher will see (it installs `thpsource2-win64.zip` after checking it against `thpsource2-win64.zip.sha256`, so both files must be on the release, under
   those names): `curl -sI -o NUL -w "%{redirect_url}" https://github.com/afcurtis/THPSource2/releases/latest` (on WSL or Linux, `-o /dev/null`) prints
   `https://github.com/afcurtis/THPSource2/releases/tag/v0.8`. Launchers older than v0.8 show the notice from their next start. Before the first release it prints
   `.../releases`, and the launcher says "No published release found".

## Third-party software

`THIRD_PARTY_NOTICES.md` (shipped in the zip with the full licence texts in `Licenses\`): SDL3 (zlib licence), zlib 1.3.1, FFmpeg 8.1.3 (LGPL 2.1 or later, unmodified source https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz, shared DLLs a player may replace), llvm-mingw runtime (libc++, libunwind, mingw-w64).
