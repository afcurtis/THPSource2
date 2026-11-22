"""Pinned glslang for compiling SDL_GPU shaders (`tony setup glslang`, re/config/glslang.yml)."""

from __future__ import annotations

import hashlib
import os
import stat
import tarfile
import tempfile
import urllib.request
from pathlib import Path

from .common import load_yaml, resolve, sha256

CONFIG = "re/config/glslang.yml"
_CHUNK = 1024 * 1024


class GlslangError(RuntimeError):
    pass


def spec() -> dict:
    return load_yaml(CONFIG)["glslang"]


def root() -> Path:
    return resolve(spec()["install_dir"])


def binary_path() -> Path:
    return root() / spec()["binary"]


def installed() -> bool:
    path = binary_path()
    return path.is_file() and os.access(path, os.X_OK)


def _download() -> Path:
    item = spec()
    target = resolve(item["path"])
    if target.is_file():
        if target.stat().st_size == int(item["size"]) and sha256(target) == item["sha256"]:
            return target
        raise GlslangError(f"refusing to replace {target}: unexpected size or SHA-256")
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
        raise GlslangError(f"{target.name}: got {size} bytes, SHA-256 {digest.hexdigest()}; expected "
                           f"{item['size']} bytes, {item['sha256']}")
    os.replace(partial, target)
    return target


def ensure() -> Path:
    """Download (verified) and extract the pinned package; return its root."""

    if installed():
        return root()
    archive = _download()
    destination = root()
    staging = destination.parent / f".{destination.name}.extract"
    if staging.exists():
        import shutil
        shutil.rmtree(staging)
    staging.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, "r:gz") as package:
        for member in package.getmembers():
            name = member.name
            if name.startswith("/") or ".." in Path(name).parts:
                raise GlslangError(f"unsafe path in {archive.name}: {name}")
            if (member.issym() or member.islnk()) and (
                member.linkname.startswith("/") or ".." in Path(member.linkname).parts
            ):
                raise GlslangError(f"unsafe link entry in {archive.name}: {name} -> {member.linkname}")
        package.extractall(staging)
    import shutil
    if destination.exists():
        shutil.rmtree(destination)
    os.replace(staging, destination)
    binary = binary_path()
    if binary.is_file():
        mode = binary.stat().st_mode
        binary.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    if not installed():
        raise GlslangError(f"{archive.name} does not contain the expected {spec()['binary']}")
    return root()


def setup_glslang(_args) -> int:
    try:
        path = ensure()
    except (GlslangError, OSError) as exc:
        print(f"FAIL {exc}")
        return 1
    print(f"glslang {spec()['release']}: binary {binary_path()}")
    print(f"sha256(glslang) = {sha256(binary_path())}")
    del path
    return 0
