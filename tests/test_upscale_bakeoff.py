"""The texture upscaler's bake-off kit and download pins (texture-upscaling plan T4): the kit is written from a dump, every candidate it runs is pinned."""

from __future__ import annotations

import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import upscale_bakeoff as bakeoff

PINS = ROOT / "re" / "config" / "upscaler.yml"


def pinned_models() -> dict[str, dict]:
    document = yaml.safe_load(PINS.read_text())
    assert document["version"] == 1
    return {model["save_as"]: model for model in document["models"]}


def test_every_candidate_is_pinned_with_a_licence() -> None:
    models = pinned_models()
    for name, licence, _ in bakeoff.WORLD_MODELS + bakeoff.TWO_D_MODELS:
        if name == "nearest":
            continue
        assert name in models, name
        assert models[name]["licence"] == licence, name
        for part in ("param", "bin"):
            pin = models[name][part]
            assert pin["url"].startswith("https://") and pin["size"] > 0 and len(pin["sha256"]) == 64, (name, part)
    for _, world, two_d in bakeoff.PACKS:
        assert world in models and (two_d == "nearest" or two_d in models)


def test_the_command_defaults_are_the_chosen_methods() -> None:
    chosen = yaml.safe_load(PINS.read_text())["chosen"]
    header = (ROOT / "src/launcher/thpsource2/core/upscale.hpp").read_text()
    assert f'std::string model_world = "{chosen["world"]}";' in header
    assert ("std::string model_2d = kNearest;" in header) if chosen["2d"] == "nearest" else (f'model_2d = "{chosen["2d"]}";' in header)
    assert f"AlphaMethod alpha = AlphaMethod::{chosen['alpha'].capitalize()};" in header
    assert f"int scale = {chosen['scale']};" in header
    assert chosen["world"] in pinned_models()


def test_tool_pin() -> None:
    tool = yaml.safe_load(PINS.read_text())["tool"]
    assert tool["url"].endswith(".zip") and len(tool["sha256"]) == 64 and tool["licence"] == "AGPL-3.0"


def test_kit_is_written_from_a_dump(tmp_path: Path) -> None:
    dump = tmp_path / "Data" / "TextureDump"
    dump.mkdir(parents=True)
    rows = ["# key\twidth\theight\tformat\talpha\tuse\twrap\tmod2x\tvolatile\ttag"]
    rows += [f"{key}\t64\t32\targb1555\topaque\tworld\twrap\tno\t0\tpc-L12" for key, _ in bakeoff.SAMPLE_WORLD + bakeoff.SAMPLE_2D]
    (dump / "index.tsv").write_text("\n".join(rows) + "\n")
    sys.argv = ["upscale_bakeoff.py", str(tmp_path)]
    assert bakeoff.main() == 0
    kit = tmp_path / "T4 bake-off"
    assert (kit / "sample_world.txt").read_text().split() == [key for key, _ in bakeoff.SAMPLE_WORLD]
    assert len((kit / "sample_2d.txt").read_text().split()) == len(bakeoff.SAMPLE_2D)
    sheet = (kit / "sheet.html").read_text()
    for name, _, _ in bakeoff.WORLD_MODELS:
        assert f"out/world-{name}/" in sheet
    command = (tmp_path / "T4 bake-off.cmd").read_bytes().decode()
    assert "\r\n" in command and command.count("--upscale-textures") == len(bakeoff.WORLD_MODELS) + 1 + len(bakeoff.TWO_D_MODELS) + len(bakeoff.PACKS)
    for letter, _, _ in bakeoff.PACKS:
        assert f"Data\\Upscaled-{letter}" in (tmp_path / f"Play upscaled {letter}.cmd").read_text()
