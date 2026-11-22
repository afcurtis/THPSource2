"""Pinned llvm-mingw for the native Kit DLL (`tony setup llvm-mingw`, re/config/llvm_mingw.yml)."""

from __future__ import annotations

import hashlib
import os
import shutil
import tarfile
import tempfile
import urllib.request
from pathlib import Path

from .common import load_yaml, resolve, sha256

CONFIG = "re/config/llvm_mingw.yml"
_CHUNK = 1024 * 1024


class LlvmMingwError(RuntimeError):
    pass


def spec() -> dict:
    return load_yaml(CONFIG)["llvm_mingw"]


def root() -> Path:
    item = spec()
    return resolve(item["install_dir"]) / item["root"]


def cxx_i686() -> Path:
    return root() / spec()["cxx_i686"]


def installed() -> bool:
    return cxx_i686().is_file()


def _download() -> Path:
    item = spec()
    target = resolve(item["path"])
    if target.is_file():
        if target.stat().st_size == int(item["size"]) and sha256(target) == item["sha256"]:
            return target
        raise LlvmMingwError(f"refusing to replace {target}: unexpected size or SHA-256")
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
        raise LlvmMingwError(f"{target.name}: got {size} bytes, SHA-256 {digest.hexdigest()}; expected "
                             f"{item['size']} bytes, {item['sha256']}")
    os.replace(partial, target)
    return target


def ensure() -> Path:
    """Download (verified) and extract the pinned toolchain; return its root."""

    if installed():
        return root()
    archive = _download()
    destination = resolve(spec()["install_dir"])
    destination.mkdir(parents=True, exist_ok=True)
    staging = destination / f".{spec()['root']}.extract"
    shutil.rmtree(staging, ignore_errors=True)
    with tarfile.open(archive, "r:xz") as package:
        package.extractall(staging, filter="tar")
    shutil.rmtree(root(), ignore_errors=True)
    os.replace(staging / spec()["root"], root())
    shutil.rmtree(staging, ignore_errors=True)
    if not installed():
        raise LlvmMingwError(f"{archive.name} does not contain {spec()['cxx_i686']}")
    return root()


def setup_llvm_mingw(_args) -> int:
    try:
        ensure()
    except (LlvmMingwError, OSError, tarfile.TarError) as exc:
        print(f"FAIL {exc}")
        return 1
    print(f"llvm-mingw {spec()['release']}: {root()}")
    return 0
