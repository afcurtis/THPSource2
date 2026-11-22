"""Phase 5, milestone 12 (5.7): the Windows release folder, and an install of it.

The install folder holds the launcher and README.md; everything else is in a folder of its own (src/launcher/layout.hpp, docs/LAUNCHER.md):

    THPSource2.exe   README.md
    Build/       opentony.exe, the standalone (opentony_spike.exe, opentony_standalone.dll, opentony_spike_video.dll), SDL3.dll, the decode-only FFmpeg DLLs
    Config/      THPSource2.ini (the launcher's settings and the game's own), TH2_OPT.CFG                      (the player's)
    Data/Game/   the player's own THawk2.exe, ALL.PKR, Music/, med/Movies/                                      (the player's)
    Data/Unpacked/  what the first run reads out of them                                                       (made on the first run)
    Saves/  Mods/  Screenshots/  Logs/                                                                          (the player's)
    Licenses/    THIRD_PARTY_NOTICES.md and the licence texts (the LGPL text and the source pointer FFmpeg's licence asks for)

`tony release package [--arch 32|64]` collects what a player needs from the build directories into `build/release/thpsource2-win<arch>/` and a zip beside it. It contains
no game data. `tony release install --to DIR [--game-files DIR]` makes the same folder anywhere (an empty DIR gets the whole tree; an existing install gets only Build/,
Licenses/, the launcher and the README replaced, and nothing of the player's is touched), and copies a game's files into Data/Game when asked. Build first with
`tony hooks build --arch 64` (see docs/BUILDING.md).
"""

from __future__ import annotations

import hashlib
import shutil
import zipfile
from pathlib import Path

from . import ffmpeg, imgui_source, llvm_mingw, sdl3, zlib_source
from .common import ROOT, load_yaml, resolve

LAUNCHER = "THPSource2.exe"
# the launcher's earlier names: an install over an older one deletes them, so the root keeps one program to start
FORMER_LAUNCHERS = ("THPSource2_Launcher.exe",)
BUILD_PROGRAMS = ("opentony.exe", "opentony_spike.exe", "opentony_standalone.dll", "opentony_spike_video.dll")
PROGRAMS = (LAUNCHER, *BUILD_PROGRAMS)
# folders the player's files go in, made empty in a new install and in the zip; the program never replaces what is in them
PLAYER_FOLDERS = ("Config", "Data/Game", "Saves", "Mods", "Screenshots", "Logs")
# folders the release owns: an install replaces them wholesale
OWNED_FOLDERS = ("Build", "Licenses")
README_SOURCE = ROOT / "docs" / "PLAYING.md"


class ReleaseError(RuntimeError):
    pass


def build_dir(arch: int) -> Path:
    return resolve("build/hooks/kits64" if arch == 64 else "build/hooks/kits")


def release_name(arch: int) -> str:
    return f"thpsource2-win{arch}"


def sdl_dll(arch: int) -> Path:
    return sdl3.dll_path("x64" if arch == 64 else "x86")


def license_files(arch: int) -> dict[str, Path]:
    """Licence texts that go in `Licenses/` (relative name -> source)."""

    pinned = load_yaml("re/config/ffmpeg.yml")["ffmpeg"]
    ffmpeg_source = resolve(pinned["source_dir"]) / pinned["root"]
    return {
        "THPSource2-LICENSE.txt": ROOT / "LICENSE",
        "THIRD_PARTY_NOTICES.md": ROOT / "THIRD_PARTY_NOTICES.md",
        "FFmpeg-COPYING.LGPLv2.1": ffmpeg_source / "COPYING.LGPLv2.1",
        "FFmpeg-LICENSE.md": ffmpeg_source / "LICENSE.md",
        "SDL3-LICENSE.txt": sdl3.root() / sdl3.spec()["license"],
        "zlib-LICENSE": zlib_source.source_dir() / "LICENSE",
        "imgui-LICENSE.txt": imgui_source.source_dir() / imgui_source.spec()["license"],
        "PARTYMOD-LICENSE.txt": ROOT / "src/launcher/thpsource2/LICENSE-PARTYMOD.txt",
        "llvm-LICENSE.TXT": llvm_mingw.root() / "LICENSE.TXT",
        "mingw-w64-runtime-COPYING.txt": llvm_mingw.root() / f"{'x86_64' if arch == 64 else 'i686'}-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt",
    }


def layout_files(arch: int) -> dict[str, Path]:
    """Every file of a release: its place in the install folder (forward slashes) -> the source file."""

    target = "x86_64" if arch == 64 else "i686"
    source = build_dir(arch)
    files: dict[str, Path] = {LAUNCHER: source / LAUNCHER, "README.md": README_SOURCE}
    for name in BUILD_PROGRAMS:
        files[f"Build/{name}"] = source / name
    files["Build/SDL3.dll"] = sdl_dll(arch)
    for dll in ffmpeg.dll_paths(target):
        files[f"Build/{dll.name}"] = dll
    for name, path in license_files(arch).items():
        files[f"Licenses/{name}"] = path
    missing = [f"{name} <- {path}" for name, path in files.items() if not path.is_file()]
    if missing:
        raise ReleaseError("not built or not set up (docs/BUILDING.md):\n  " + "\n  ".join(missing))
    return files


def _copy(files: dict[str, Path], out: Path) -> None:
    for name, path in files.items():
        (out / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, out / name)


def package(arch: int = 64, *, zip_it: bool = True) -> Path:
    files = layout_files(arch)
    out = resolve("build/release") / release_name(arch)
    if out.exists():
        shutil.rmtree(out)
    _copy(files, out)
    for folder in PLAYER_FOLDERS:
        (out / folder).mkdir(parents=True, exist_ok=True)
    if zip_it:
        archive = out.with_suffix(".zip")
        archive.unlink(missing_ok=True)
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(out.rglob("*")):
                arcname = (Path(release_name(arch)) / path.relative_to(out)).as_posix()
                if path.is_file():
                    bundle.write(path, arcname)
                elif not any(path.iterdir()):
                    bundle.writestr(arcname + "/", "")   # the empty folders of the player's files
        write_checksum(archive)
    return out


def write_checksum(archive: Path) -> Path:
    """`<archive>.sha256` beside it, in `sha256sum` format (`sha256sum -c` checks it), so a release never ships a stale one."""

    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    sidecar = archive.with_name(archive.name + ".sha256")
    sidecar.write_text(f"{digest}  {archive.name}\n", encoding="ascii", newline="\n")
    return sidecar


def _find(directory: Path, name: str) -> Path | None:
    """A child of `directory` by name, whatever its case (a copied install may have any)."""

    if directory.is_dir():
        for child in directory.iterdir():
            if child.name.lower() == name.lower():
                return child
    return None


def copy_game_files(source: Path, destination: Path) -> list[str]:
    """THawk2.exe, ALL.PKR, Music/ and med/Movies/ of an installed game (or a copy of the disc's SETUP/data and SETUP/med) into `destination`; what is already there with
    the same size stays. The names that were copied."""

    roots = [source]
    for sub in ("SETUP", "setup"):
        if (source / sub).is_dir():
            roots.append(source / sub / "data")
            roots.append(source / sub)
    copied: list[str] = []

    def first(name: str) -> Path | None:
        for root in roots:
            if (found := _find(root, name)) is not None:
                return found
        return None

    def put(found: Path, relative: str) -> None:
        target = destination / relative
        if found.is_dir():
            for child in sorted(found.rglob("*")):
                if child.is_file():
                    put(child, f"{relative}/{child.relative_to(found).as_posix()}")
            return
        if target.is_file() and target.stat().st_size == found.stat().st_size:
            return
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(found, target)
        copied.append(relative)

    for name in ("THawk2.exe", "ALL.PKR"):
        found = first(name)
        if found is None:
            raise ReleaseError(f"{name} not found in {source}")
        put(found, name)
    if (music := first("Music")) is not None:
        put(music, "Music")
    med = first("med")
    if med is not None and (movies := _find(med, "Movies")) is not None:
        put(movies, "med/Movies")
    return copied


def install(arch: int, target: Path, game_files: Path | None = None) -> Path:
    """The release's layout in `target`. A new folder gets everything; an existing install gets Build/, Licenses/, the launcher and the README replaced and its
    player's folders left as they are."""

    files = layout_files(arch)
    target.mkdir(parents=True, exist_ok=True)
    for folder in OWNED_FOLDERS:
        if (target / folder).exists():
            shutil.rmtree(target / folder)
    for name in FORMER_LAUNCHERS:
        (target / name).unlink(missing_ok=True)
    _copy(files, target)
    for folder in PLAYER_FOLDERS:
        (target / folder).mkdir(parents=True, exist_ok=True)
    if game_files is not None:
        copy_game_files(game_files, target / "Data" / "Game")
    return target


def release_package(args) -> int:
    try:
        out = package(int(args.arch), zip_it=not args.no_zip)
    except ReleaseError as exc:
        print(f"release: {exc}")
        return 1
    print(f"release: {out.relative_to(ROOT)} ({sum(1 for p in out.rglob('*') if p.is_file())} files)")
    return 0


def release_install(args) -> int:
    try:
        out = install(int(args.arch), Path(args.to), Path(args.game_files) if args.game_files else None)
    except ReleaseError as exc:
        print(f"release: {exc}")
        return 1
    print(f"release: installed in {out} ({sum(1 for p in out.rglob('*') if p.is_file())} files)")
    return 0


def register(subparsers) -> None:
    release = subparsers.add_parser("release", help="the Windows release folder (milestone 12)")
    sub = release.add_subparsers(dest="release_command", required=True)
    p = sub.add_parser("package", help="collect the built programs, DLLs and licences into build/release/thpsource2-winNN (and a zip)")
    p.add_argument("--arch", choices=("32", "64"), default="64")
    p.add_argument("--no-zip", action="store_true")
    p.set_defaults(func=release_package)
    p = sub.add_parser("install", help="lay the release out in a folder (a new one gets everything; an existing install keeps the player's files)")
    p.add_argument("--to", required=True, help="the install folder")
    p.add_argument("--arch", choices=("32", "64"), default="64")
    p.add_argument("--game-files", help="a folder with THawk2.exe, ALL.PKR, Music and med/Movies (an installed game, or a copy of the disc) to copy into Data/Game")
    p.set_defaults(func=release_install)
