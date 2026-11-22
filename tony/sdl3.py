"""Pinned SDL3 for the platform HAL (`tony setup sdl3`, re/config/sdl3.yml)."""

from __future__ import annotations

import hashlib
import os
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path

from .common import load_yaml, resolve, sha256

CONFIG = "re/config/sdl3.yml"
_CHUNK = 1024 * 1024


class Sdl3Error(RuntimeError):
    pass


def spec() -> dict:
    return load_yaml(CONFIG)["sdl3"]


def root() -> Path:
    item = spec()
    return resolve(item["install_dir"]) / item["root"]


def include_dir() -> Path:
    return root() / spec()["include"]


def dll_path(arch: str = "x86") -> Path:
    """SDL3.dll of the package: lib/x86 (the default) or lib/x64 (the 64-bit standalone, milestone 11b)."""

    return root() / (spec()["dll_x86"] if arch == "x86" else spec()["dll_x86"].replace("/x86/", "/x64/"))


def installed() -> bool:
    return (include_dir() / "SDL3" / "SDL.h").is_file() and dll_path().is_file()


def _download() -> Path:
    item = spec()
    target = resolve(item["path"])
    if target.is_file():
        if target.stat().st_size == int(item["size"]) and sha256(target) == item["sha256"]:
            return target
        raise Sdl3Error(f"refusing to replace {target}: unexpected size or SHA-256")
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
        raise Sdl3Error(f"{target.name}: got {size} bytes, SHA-256 {digest.hexdigest()}; expected "
                        f"{item['size']} bytes, {item['sha256']}")
    os.replace(partial, target)
    return target


def ensure() -> Path:
    """Download (verified) and extract the pinned package; return its root."""

    if installed():
        return root()
    archive = _download()
    destination = resolve(spec()["install_dir"])
    staging = destination / f".{spec()['root']}.extract"
    shutil.rmtree(staging, ignore_errors=True)
    with zipfile.ZipFile(archive) as package:
        for member in package.namelist():
            if member.startswith("/") or ".." in Path(member).parts:
                raise Sdl3Error(f"unsafe path in {archive.name}: {member}")
        package.extractall(staging)
    shutil.rmtree(root(), ignore_errors=True)
    os.replace(staging / spec()["root"], root())
    shutil.rmtree(staging, ignore_errors=True)
    if not installed():
        raise Sdl3Error(f"{archive.name} does not contain the expected headers and SDL3.dll")
    return root()


def setup_sdl3(_args) -> int:
    try:
        path = ensure()
    except (Sdl3Error, OSError) as exc:
        print(f"FAIL {exc}")
        return 1
    print(f"SDL3 {spec()['release']}: headers {include_dir()}, SDL3.dll {dll_path()}")
    print(f"sha256(SDL3.dll) = {sha256(dll_path())}")
    del path
    return 0
