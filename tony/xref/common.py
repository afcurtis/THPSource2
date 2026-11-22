from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from ..common import ROOT, load_yaml, resolve, sha256

CONFIG = "re/xref/config.yml"
BUILD_DIR = ROOT / "build/xref"
CANONICAL = "canonical"


def config() -> dict[str, Any]:
    return load_yaml(CONFIG)


def variant_spec(name: str) -> dict[str, Any]:
    variants = config().get("variants", {})
    if name not in variants:
        raise SystemExit(f"unknown xref variant {name!r}; known: {', '.join(sorted(variants))}")
    return variants[name]


def verified_variant_path(name: str) -> Path:
    spec = variant_spec(name)
    path = resolve(spec["path"])
    if not path.is_file():
        raise SystemExit(f"variant executable not found: {path}")
    digest = sha256(path)
    if digest != spec["sha256"]:
        raise SystemExit(f"{path}: sha256 {digest} does not match recorded {spec['sha256']}")
    return path


def canonical_ranges() -> tuple[tuple[int, int], tuple[int, int]]:
    """Return (.text, data) virtual-address ranges of the canonical executable."""

    spec = load_yaml("re/config/binaries.yml")["executables"]["thps2_pc"]
    base = int(spec["image_base"])
    sections = {s["name"]: s for s in spec["sections"]}
    text = sections[".text"]
    text_range = (base + text["virtual_address"], base + text["virtual_address"] + text["virtual_size"])
    data_start = min(base + s["virtual_address"] for n, s in sections.items() if n in {".rdata", ".data"})
    data_end = max(base + s["virtual_address"] + s["virtual_size"] for n, s in sections.items()
                   if n in {".rdata", ".data"})
    return text_range, (data_start, data_end)


def reference_dir(name: str) -> Path:
    return BUILD_DIR / "references" / name


def features_path(build: str) -> Path:
    return BUILD_DIR / f"features-{build}.json"


def load_features(build: str) -> dict[str, Any]:
    path = features_path(build)
    if not path.is_file():
        raise SystemExit(f"missing {path}; run: tony xref features --build {build}")
    return json.loads(path.read_text(encoding="utf-8"))


def hex32(value: int) -> str:
    return f"0x{value:08x}"


def write_rows(path: str | Path, header: dict[str, Any], sections: dict[str, tuple[str, list[list[Any]]]]) -> Path:
    """Write a compact, diff-friendly YAML document of flow-style rows.

    ``sections`` maps a key to ``(column comment, rows)``. Integer cells that look like
    addresses are rendered as zero-padded hex so the files stay reviewable.
    """

    import yaml

    target = resolve(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    lines = [yaml.safe_dump(header, sort_keys=False).rstrip()]
    for key, (columns, rows) in sections.items():
        lines.append(f"# {key}: {columns}")
        if not rows:
            lines.append(f"{key}: []")
            continue
        lines.append(f"{key}:")
        for row in rows:
            cells = []
            for cell in row:
                if isinstance(cell, bool) or cell is None:
                    cells.append(yaml.safe_dump(cell).split("\n")[0])
                elif isinstance(cell, int) and cell >= 0x400000:
                    cells.append(hex32(cell))
                elif isinstance(cell, (int, float)):
                    cells.append(str(cell))
                else:
                    cells.append(json.dumps(str(cell)))
            lines.append(f"  - [{', '.join(cells)}]")
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return target
