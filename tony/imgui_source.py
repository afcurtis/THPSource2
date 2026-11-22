"""Pinned Dear ImGui source for the THPSource2 launcher (`tony hooks build`, re/config/imgui.yml)."""

from __future__ import annotations

import hashlib
import os
import shutil
import tarfile
import tempfile
import urllib.request
from pathlib import Path

from .common import load_yaml, resolve, sha256

CONFIG = "re/config/imgui.yml"
_CHUNK = 1024 * 1024


class ImguiError(RuntimeError):
    pass


def spec() -> dict:
    return load_yaml(CONFIG)["imgui"]


def _download() -> Path:
    item = spec()
    target = resolve(item["path"])
    if target.is_file():
        if target.stat().st_size == int(item["size"]) and sha256(target) == item["sha256"]:
            return target
        raise ImguiError(f"refusing to replace {target}: unexpected size or SHA-256")
    target.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    size = 0
    print(f"Downloading {item['url']}\n       -> {target}")
    with tempfile.NamedTemporaryFile(mode="wb", dir=target.parent, prefix=f".{target.name}.", suffix=".part", delete=False) as output:
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
        raise ImguiError(f"{target.name}: got {size} bytes, SHA-256 {digest.hexdigest()}; expected {item['size']} bytes, {item['sha256']}")
    os.replace(partial, target)
    return target


def source_dir() -> Path:
    """The extracted release (imgui.h, the library and its backends), downloaded and checked when it is not there yet."""

    item = spec()
    destination = resolve(item["source_dir"])
    source = destination / item["root"]
    if (source / "imgui.h").is_file() and all((source / name).is_file() for name in item["sources"]):
        return source
    archive = _download()
    shutil.rmtree(destination, ignore_errors=True)
    destination.mkdir(parents=True)
    with tarfile.open(archive, "r:gz") as package:
        package.extractall(destination, filter="tar")
    missing = [name for name in ["imgui.h", item["license"], *item["sources"]] if not (source / name).is_file()]
    if missing:
        raise ImguiError(f"{archive.name} does not contain {', '.join(missing)}")
    return source
