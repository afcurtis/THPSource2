# The native Linux build (x86-64)

The standalone game built as Linux programs, from the same sources as the Windows 64-bit standalone. It needs the Activision Value re-release's
`THawk2.exe` and `ALL.PKR` (docs/BUILDING.md); the music and movies are optional.

## Build

What you need: a distribution that packages SDL3 (tested on Ubuntu 26.04; Ubuntu 25.04 or later, Debian 13 and current Fedora and Arch have it), and

```bash
sudo apt-get install -y git cmake ninja-build g++ zlib1g-dev curl libsdl3-0 libavcodec61 libavformat61   # or your release's libavcodec/libavformat
```

The `libav*` major version only has to match the headers you build against (the script below takes them from the same release).

```bash
scripts/linux/fetch-headers.sh        # SDL3 and FFmpeg headers into .tools/linux/sysroot (or: sudo apt-get install libsdl3-dev libavcodec-dev libavformat-dev)
scripts/linux/fetch-imgui.sh          # the pinned Dear ImGui source, for the launcher
cmake -S src/linux -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
cmake --install build/linux --prefix <install folder>   # THPSource2 in it, the game's programs in Build/
```

Run time needs the system's `libSDL3.so.0` and FFmpeg (`libavcodec`/`libavformat`/`libavutil` of the major versions the headers name), both loaded with
`dlopen` as the Windows build loads its DLLs, and OpenGL.

## Run

Start `<install folder>/THPSource2`, the launcher (docs/LAUNCHER.md): it finds or imports the game's files on its first start (an installed game, the disc
folder or a disc image, through the desktop's file dialog), keeps every setting in `Config/THPSource2.ini`, and PLAY starts the game. Or without it:

```bash
<install folder>/Build/opentony --game-dir <folder with THawk2.exe, ALL.PKR, Music, med/Movies>
```

The first run checks `THawk2.exe` (SHA-256), writes `Data/Unpacked/thawk2_static.img` and unpacks `ALL.PKR` into `Data/Unpacked/data`, as on Windows; the
install folder's `Config`, `Saves`, `Mods`, `Logs` are made beside it. `--prepare` does only that. Every environment option of docs/LAUNCHER.md and
`src/hooks/kits/standalone_video.cpp` (`OT_ASPECT`, `OT_INTERNAL_RESOLUTION`, `OT_FPS`, ...) works the same.

## What differs from Windows

The programs and their process layout are the Windows build's: `opentony` (the first-run reads, then the game) starts `opentony_spike`, which reserves the 4 GiB
guest arena and loads `opentony_standalone.so` (the Kits) and `opentony_spike_video.so` (SDL3: window, input, sound) beside it. The OS differences are in
`src/platform`:

| | |
|---|---|
| `posix_compat.h` | forced include: the Microsoft keywords (`__cdecl`, `__declspec(dllexport)`, ...); x86-64 has one calling convention |
| `ot_os.hpp` | address space (`mmap`/`mprotect`, `/proc/self/maps` for the probes), `dlopen`, case-insensitive file names |
| `win32_posix.h` | the plain Win32 calls the host sources make (strings, clock, environment, file attributes and searches, INI files, the MSVC `_stat`/`_findfirst` layouts) |
| `sha256.hpp` | the executable's identification (bcrypt on Windows) |
| `path_separator.h` | the separator host paths are joined with |

The game names its files as a case-insensitive file system lets it (`data\tricks.bin` for `DATA/TRICKS.BIN`): every open of a game file resolves the name
without regard to case. Crashes and `OT_HANG_DUMP` are reported from signal handlers with the same `CRASH`/`HANG:` lines; the runner's hang watchdog uses a
POSIX shared memory heartbeat. There is no minidump.

## Not yet

- The launcher on Linux: fonts are the system's Noto Sans, Ubuntu, DejaVu Sans or Liberation Sans (Segoe UI on Windows); "show in folder" asks the
  desktop's file manager over D-Bus, else opens the folder; a crash report is zipped with `zip` or `bsdtar`; the texture upscaler downloads upscayl-bin's
  Linux build (pinned in `core/upscale_tables.inc`) and needs `curl` and `unzip` (or `bsdtar`) and a Vulkan driver. The upscaler has run on Linux (Intel
  UHD: about 2 s a texture, so hours for the full set; progress counts the textures inside a batch, and Cancel stops the tool). The update check has not
  been run on Linux yet.
- The scenario gates (`tony gates full`) have not been run against this build: two runs of a scenario give the same trace byte for byte, but the comparison
  with retail under Wine is still to do.
