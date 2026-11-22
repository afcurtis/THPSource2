"""Derive the platform boundary of one build from Ghidra facts."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from ..common import load_yaml, resolve
from .facts import export
from .infer import Engine, Result, overrides_for

BOUNDARY_PATH = "re/config/hal_boundary.yml"


def load_boundary(path: str | Path = BOUNDARY_PATH) -> dict[str, Any]:
    target = resolve(path)
    return load_yaml(target) if target.is_file() else {}


def image_for(build: str):
    from ..hooks import PeImage, build_executable

    exe = build_executable(build)
    if exe is None or not exe.is_file():
        raise SystemExit(f"{build}: executable not present")
    return PeImage(exe)


def analyze(build: str = "canonical", *, roots: list[dict[str, Any]] | None = None,
            force: bool = False) -> tuple[dict[str, Any], Result]:
    """Export (cached) facts with COM call-site refinement, then propagate types."""

    if roots is None:
        roots = load_boundary().get("roots", {}).get(build, [])
    image = image_for(build)
    # The COM call-site overrides written during export depend on the roots
    # and on the propagation code, so both are part of the cache key.
    digest = hashlib.sha256(json.dumps(roots, sort_keys=True, default=str).encode())
    for module in ("infer.py", "com.py", "expr.py"):
        digest.update((Path(__file__).parent / module).read_bytes())
    salt = digest.hexdigest()

    def refine(facts: dict[str, Any]) -> dict[int, tuple[int, int]]:
        return overrides_for(Engine(facts, image, roots).run())

    facts = export(build, force=force, refine=refine, salt=salt)
    return facts, Engine(facts, image, roots).run()
