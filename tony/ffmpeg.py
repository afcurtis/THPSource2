"""Pinned, decode-only FFmpeg for the movie and music backend (`tony setup ffmpeg`, re/config/ffmpeg.yml)."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path

from .common import load_yaml, resolve, sha256

CONFIG = "re/config/ffmpeg.yml"
_CHUNK = 1024 * 1024


class FfmpegError(RuntimeError):
    pass


# The flags that name the target; everything else of the pin's `configure` list is shared by both builds.
_TARGET_FLAGS = ("--arch=", "--cross-prefix=", "--cc=", "--cxx=")


def spec(arch: str = "i686") -> dict:
    """The pin. `arch` is "i686" (the 32-bit standalone, the default) or "x86_64" (milestone 11b): the same source and options, built for the other target."""

    item = dict(load_yaml(CONFIG)["ffmpeg"])
    extra = item.pop("x86_64", None)
    if arch == "x86_64":
        if extra is None:
            raise FfmpegError(f"{CONFIG} has no x86_64 section")
        item.update(extra)
        item["configure"] = [*extra["configure_target"], *[flag for flag in load_yaml(CONFIG)["ffmpeg"]["configure"] if not flag.startswith(_TARGET_FLAGS)]]
    elif arch != "i686":
        raise FfmpegError(f"unknown FFmpeg target {arch!r}")
    return item


def install_dir(arch: str = "i686") -> Path:
    return resolve(spec(arch)["install_dir"])


def include_dir(arch: str = "i686") -> Path:
    return install_dir(arch) / "include"


def lib_dir(arch: str = "i686") -> Path:
    """The import libraries (`libavcodec.dll.a` ...)."""

    return install_dir(arch) / "lib"


def dll_paths(arch: str = "i686") -> list[Path]:
    """The built DLLs, `avutil` first (the others import it), or an empty list when they are not built."""

    found = []
    for name in spec(arch)["libraries"]:
        matches = sorted((install_dir(arch) / "bin").glob(f"{name}-*.dll"))
        if not matches:
            return []
        found.append(matches[-1])
    return found


def installed(arch: str = "i686") -> bool:
    return bool(dll_paths(arch)) and (include_dir(arch) / "libavcodec" / "avcodec.h").is_file()


def _download() -> Path:
    item = spec()
    target = resolve(item["path"])
    if target.is_file():
        if target.stat().st_size == int(item["size"]) and sha256(target) == item["sha256"]:
            return target
        raise FfmpegError(f"refusing to replace {target}: unexpected size or SHA-256")
    target.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    size = 0
    print(f"Downloading {item['url']}\n       -> {target}")
    with tempfile.NamedTemporaryFile(mode="wb", dir=target.parent, prefix=f".{target.name}.", suffix=".part",
                                     delete=False) as output:
        partial = Path(output.name)
        try:
            with urllib.request.urlopen(item["url"], timeout=60) as response:
                while chunk := response.read(_CHUNK):
                    output.write(chunk)
                    digest.update(chunk)
                    size += len(chunk)
        except BaseException:
            partial.unlink(missing_ok=True)
            raise
    if size != int(item["size"]) or digest.hexdigest() != item["sha256"]:
        partial.unlink(missing_ok=True)
        raise FfmpegError(f"{target.name}: got {size} bytes, SHA-256 {digest.hexdigest()}; expected "
                          f"{item['size']} bytes, {item['sha256']}")
    os.replace(partial, target)
    return target


def _extract(archive: Path) -> Path:
    item = spec()
    destination = resolve(item["source_dir"])
    source = destination / item["root"]
    if (source / "configure").is_file():
        return source
    shutil.rmtree(destination, ignore_errors=True)
    destination.mkdir(parents=True)
    with tarfile.open(archive, "r:xz") as package:
        package.extractall(destination, filter="tar")
    if not (source / "configure").is_file():
        raise FfmpegError(f"{archive.name} does not contain {item['root']}/configure")
    return source


def _toolchain_environment() -> dict[str, str]:
    from . import llvm_mingw

    root = llvm_mingw.ensure()
    env = os.environ.copy()
    env["PATH"] = f"{root / 'bin'}{os.pathsep}{env['PATH']}"
    return env


def licence_errors(build: Path) -> list[str]:
    """The build must be LGPL-2.1-or-later: config.h has no GPL, version 3 or non-free switch on."""

    text = (build / "config.h").read_text()
    errors = []
    for name in ("CONFIG_GPL", "CONFIG_NONFREE", "CONFIG_VERSION3", "CONFIG_GPLV3"):
        match = re.search(rf"^#define {name} (\d)", text, re.MULTILINE)
        if match is None or match.group(1) != "0":
            errors.append(f"{name} is not 0 in {build / 'config.h'}")
    return errors


def ensure(arch: str = "i686") -> Path:
    """Download (verified), extract, configure, build and install the pinned FFmpeg; return the install directory."""

    if installed(arch):
        return install_dir(arch)
    item = spec(arch)
    source = _extract(_download())
    env = _toolchain_environment()
    build = resolve(item["build_dir"])
    shutil.rmtree(build, ignore_errors=True)
    build.mkdir(parents=True)
    prefix = install_dir(arch)
    # FFmpeg bakes its configure line into every DLL (avutil_configuration()): a neutral --prefix, installed through DESTDIR into the same
    # folder, keeps this machine's paths (user name, checkout) out of the DLLs a release ships
    configure = [str(source / "configure"), f"--prefix=/{prefix.name}", *item["configure"]]
    jobs = str(max(1, (os.cpu_count() or 2) - 2))
    for command in (configure, ["make", f"-j{jobs}"], ["make", "install", f"DESTDIR={prefix.parent}"]):
        print("+ " + " ".join(command[:3]) + (" ..." if len(command) > 3 else ""))
        if command is configure:
            result = subprocess.run(command, cwd=build, env=env, check=False)
            if result.returncode != 0:
                log = build / "ffbuild" / "config.log"
                tail = log.read_text().splitlines()[-20:] if log.is_file() else []
                raise FfmpegError("configure failed:\n" + "\n".join(tail))
            problems = licence_errors(build)
            if problems:
                raise FfmpegError("not an LGPL build: " + "; ".join(problems))
        else:
            subprocess.run(command, cwd=build, env=env, check=True)
    if not installed(arch):
        raise FfmpegError(f"the build did not produce {', '.join(item['libraries'])} in {prefix / 'bin'}")
    return prefix


def setup_ffmpeg(args) -> int:
    arch = "x86_64" if getattr(args, "arch", "32") == "64" else "i686"
    try:
        prefix = ensure(arch)
    except (FfmpegError, OSError, tarfile.TarError, subprocess.CalledProcessError) as exc:
        print(f"FAIL {exc}")
        return 1
    print(f"ffmpeg {spec(arch)['release']} (decode-only, LGPL, {'64' if arch == 'x86_64' else '32'}-bit): {prefix}")
    for path in dll_paths(arch):
        print(f"  {path.name}")
    return 0
