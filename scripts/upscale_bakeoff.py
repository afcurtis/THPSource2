#!/usr/bin/env python3
"""The texture upscaler's bake-off (texture-upscaling plan, T4 steps 10-11; the person's half is T4.5): writes into an install folder

    python scripts/upscale_bakeoff.py INSTALL [--dump DIR]

  INSTALL/T4 bake-off.cmd        runs every candidate over the sample textures (THPSource2.exe --upscale-textures), opens the contact sheet, then
                                 builds the two full sets A and B for play (Data/Upscaled-A, Data/Upscaled-B)
  INSTALL/T4 bake-off/           sample_world.txt, sample_2d.txt (keys), sheet.html (each sample: the original at 4x nearest beside each candidate)
  INSTALL/Play upscaled A.cmd, INSTALL/Play upscaled B.cmd

The candidates are the ncnn builds that exist (OpenModelDB lists none; upscayl and xinntao publish them), each with its licence (re/config/upscaler.yml).
The upscaler is a Windows program that needs a Vulkan GPU, so the .cmd runs on the person's machine; nothing here runs it.
"""

from __future__ import annotations

import argparse
import html
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

# 12 world samples (Warehouse, level 12; picked by eye from the census, 2026-11-18): what each shows
SAMPLE_WORLD = [
    ("07b865039d6dab3d", "brick wall"),
    ("15603b8972fa907e", "crate with FRAGILE"),
    ("b3b4f39329b2f1b2", "wood floor"),
    ("de57ace8a13cfdda", "Shorty's sign (text)"),
    ("92f2416b9f4a3a3d", "chains (cut-out)"),
    ("d06d17f7f67a196c", "Hawk's face"),
    ("830ede4fcbd0e9e1", "Hawk's shirt"),
    ("76bae2ba0addc0b0", "dirt ground"),
    ("595afbd7ce9599a4", "poster with words"),
    ("43fd1482915d08cd", "metal panel"),
    ("368cf23a9b2a5c32", "broken windows (cut-out)"),
    ("4e2be14de0ec3491", "door, PlayStation texture"),
]
# 14 2D samples: HUD glyphs and bars, menu buttons, screen tiles, the main menu's wheel
SAMPLE_2D = [
    ("0e398e0807282090", "timer digit"),
    ("2a0d119d37407fef", "trick name letter (large)"),
    ("391b865774bb794e", "trick name letter (small)"),
    ("708c7eed7c0c2425", "SPECIAL bar"),
    ("91bd686e6f8f8e86", "SCORE"),
    ("9b2db5f945bcda67", "SWITCH"),
    ("1df1a3ee6b157a74", "BACK button"),
    ("122ef8c117a30e4c", "arrow cursor"),
    ("d472e1f4bdbeadac", "title screen tile (small print)"),
    ("277ace1e87162b47", "loading screen tile"),
    ("2970727c51cfee83", "menu title tile"),
    ("3b0b29ddc5b217f3", "EXIT button"),
    ("59b36b5416da8914", "menu arrow"),
    ("eff301dbe4c1223c", "main menu wheel"),
]

# name, licence, note
WORLD_MODELS = [
    ("realesrgan-x4plus", "BSD-3-Clause", "the safe baseline (Real-ESRGAN)"),
    ("RealESRGAN_General_x4_v3", "BSD-3-Clause", "small and fast (realesr-general-x4v3)"),
    ("ultrasharp-4x", "CC-BY-NC-SA-4.0", "4x-UltraSharp (Kim2091), popular for game textures"),
    ("remacri-4x", "CC-BY-NC-SA-4.0", "Remacri (foolhardy)"),
    ("4x_NMKD-Siax_200k", "WTFPL", "NMKD Siax, clean and slightly compressed images"),
    ("4x_NMKD-Superscale-SP_178000_G", "WTFPL", "NMKD Superscale, clean real-world images"),
    ("4xNomos8kSC", "CC-BY-4.0", "Nomos8kSC (Phhofm), photos"),
    ("4xLSDIRCompactC3", "CC-BY-4.0", "LSDIR Compact C3 (Phhofm), small and fast"),
]
TWO_D_MODELS = [
    ("nearest", "-", "built in: the original pixels, 4x (crisp at any size)"),
    ("realesrgan-x4plus-anime", "BSD-3-Clause", "Real-ESRGAN anime: flat colour and lines"),
    ("realesr-animevideov3-x4", "BSD-3-Clause", "Real-ESRGAN anime video, small"),
    ("4xHFA2k", "CC-BY-4.0", "HFA2k (Phhofm), anime"),
    ("realesrgan-x4plus", "BSD-3-Clause", "the world baseline, for comparison"),
    ("ultrasharp-4x", "CC-BY-NC-SA-4.0", "UltraSharp, for comparison"),
]
# the full sets for play: (letter, world model, 2D method); C is the person's pick (T4.5: B's world, A's HUD)
PACKS = [("A", "realesrgan-x4plus", "nearest"), ("B", "ultrasharp-4x", "realesrgan-x4plus-anime"), ("C", "ultrasharp-4x", "nearest")]


def read_sizes(dump: Path) -> dict[str, tuple[int, int, str]]:
    sizes = {}
    for line in (dump / "index.tsv").read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        f = line.split("\t")
        sizes[f[0]] = (int(f[1]), int(f[2]), f[4])
    return sizes


def cmd_text() -> str:
    lines = [
        "@echo off",
        "rem Texture upscaling bake-off (T4.5). Runs the upscaler on this PC's GPU: the samples with every candidate (a few minutes), opens the contact sheet,",
        "rem then builds the two full sets for play (Data\\Upscaled-A, Data\\Upscaled-B; longer). Progress is printed here and in Logs\\upscale.txt.",
        "setlocal",
        'cd /d "%~dp0"',
        'set "L=%~dp0THPSource2.exe"',
        'set "B=%~dp0T4 bake-off"',
        'if not exist "%~dp0Data\\Upscaler\\upscayl-bin.exe" (echo Data\\Upscaler\\upscayl-bin.exe is missing & pause & exit /b 1)',
        "echo === World samples",
    ]
    for name, _, _ in WORLD_MODELS:
        lines.append(f'"%L%" --upscale-textures --keys "%B%\\sample_world.txt" --model3d {name} --model2d none --out "%B%\\out\\world-{name}"')
    lines.append('"%L%" --upscale-textures --keys "%B%\\sample_world.txt" --model3d realesrgan-x4plus --model2d none --alpha bicubic '
                 '--out "%B%\\out\\world-realesrgan-x4plus-bicubic-alpha"')
    lines.append("echo === 2D samples")
    for name, _, _ in TWO_D_MODELS:
        lines.append(f'"%L%" --upscale-textures --keys "%B%\\sample_2d.txt" --model3d none --model2d {name} --out "%B%\\out\\2d-{name}"')
    lines.append('start "" "%B%\\sheet.html"')
    lines.append("echo === The contact sheet is open in your browser. Now the two full sets for play (this takes longer)...")
    for letter, world, two_d in PACKS:
        lines.append(f'"%L%" --upscale-textures --model3d {world} --model2d {two_d} --out "%~dp0Data\\Upscaled-{letter}"')
    lines += [
        "echo === Done. Play with \"Play upscaled A.cmd\" and \"Play upscaled B.cmd\".",
        "pause",
    ]
    return "\r\n".join(lines) + "\r\n"


def play_cmd(letter: str, world: str, two_d: str) -> str:
    return "\r\n".join([
        "@echo off",
        f"rem Upscaled textures, set {letter} (T4.5): world textures by {world}, HUD / fonts / menus by {two_d}. Made by \"T4 bake-off.cmd\".",
        "rem Press Play in the launcher that opens. Logs\\runs\\<newest>\\run.txt gets the \"video: texture pack\" line (how many were replaced, the time it took).",
        f'set "OT_TEXTURE_PACK=%~dp0Data\\Upscaled-{letter}"',
        'start "" "%~dp0THPSource2.exe"',
    ]) + "\r\n"


def sheet_html(sizes: dict[str, tuple[int, int, str]]) -> str:
    def section(title: str, kind: str, samples, models, extra=()) -> str:
        columns = [(f"{kind}-{name}", name, licence, note) for name, licence, note in models] + list(extra)
        head = "".join(f'<th><div class="m">{html.escape(name)}</div><div class="l">{html.escape(licence)}</div><div class="n">{html.escape(note)}</div>'
                       f'<label><input type="radio" name="{kind}1" value="{html.escape(name)}"> 1st</label> '
                       f'<label><input type="radio" name="{kind}2" value="{html.escape(name)}"> 2nd</label></th>'
                       for _, name, licence, note in columns)
        rows = []
        for key, what in samples:
            w, h, alpha = sizes.get(key, (64, 64, "?"))
            shown = max(4 * w, 4 * h)
            box = f'style="width:{4 * w * min(1, 256 / shown) :.0f}px;height:{4 * h * min(1, 256 / shown):.0f}px"'
            cells = [f'<td class="orig"><img {box} class="px" src="../Data/TextureDump/{key}.png" alt=""></td>']
            for folder, _, _, _ in columns:
                cells.append(f'<td><img {box} src="out/{folder}/{key}.png" alt="" onerror="this.replaceWith(Object.assign(document.createElement(\'span\'),'
                             f'{{className:\'miss\',textContent:\'not made\'}}))"></td>')
            rows.append(f'<tr><th class="what">{html.escape(what)}<div class="n">{w} x {h}, {alpha}<br>{key}</div></th>{"".join(cells)}</tr>')
        return (f'<h2>{title}</h2><div class="scroll"><table><tr><th></th><th><div class="m">original</div><div class="n">4x nearest</div></th>{head}</tr>'
                + "".join(rows) + "</table></div>")

    world = section("World textures (3D)", "world", SAMPLE_WORLD, WORLD_MODELS,
                    [("world-realesrgan-x4plus-bicubic-alpha", "realesrgan-x4plus, bicubic alpha", "BSD-3-Clause",
                      "the same model, alpha smoothed by bicubic instead of the model: compare the chains and the windows")])
    two_d = section("HUD, fonts, menus (2D)", "2d", SAMPLE_2D, TWO_D_MODELS)
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Upscaler bake-off</title>
<style>
:root {{ --bg:#1e1f22; --fg:#e8e8ea; --dim:#9a9ba1; --line:#3a3b40; --cut:#ff00ff; }}
body {{ background:var(--bg); color:var(--fg); font:14px/1.4 system-ui, sans-serif; margin:16px; }}
h1 {{ font-size:20px; margin:0 0 4px; }} h2 {{ font-size:17px; margin:28px 0 8px; }}
.scroll {{ overflow-x:auto; }}
table {{ border-collapse:collapse; }}
th, td {{ border:1px solid var(--line); padding:6px; vertical-align:top; text-align:left; }}
td {{ background:var(--cut); }} td.orig {{ outline:2px solid #6cf; }}
img {{ display:block; }} img.px {{ image-rendering:pixelated; }}
.m {{ font-weight:600; }} .l {{ color:#fc6; font-size:12px; }} .n {{ color:var(--dim); font-size:12px; max-width:220px; }}
th.what {{ min-width:150px; background:var(--bg); }}
.miss {{ color:#000; font-size:12px; }}
#answer {{ position:sticky; bottom:0; background:#2b2d31; border:1px solid var(--line); padding:10px; margin-top:20px; }}
#answer textarea {{ width:100%; height:48px; background:#111; color:var(--fg); border:1px solid var(--line); font:13px monospace; }}
label {{ font-size:12px; white-space:nowrap; }}
</style></head><body>
<h1>Upscaler bake-off</h1>
<p>Each row is one of the game's textures: the original (blue outline, shown at 4x with its own pixels) and what each candidate makes of it, at the same size.
Magenta shows through where a texture is transparent. Scroll sideways for more candidates. Pick a 1st and 2nd in each section; the line at the bottom is what to paste back.
Licences in yellow: "NC" means non-commercial use only (fine for a player's own copy, the set is made on their PC).</p>
{world}
{two_d}
<div id="answer">Your answer (paste it into the T4.5 chat): <textarea id="out" readonly></textarea></div>
<script>
function pick(n) {{ const e = document.querySelector('input[name="' + n + '"]:checked'); return e ? e.value : "-"; }}
function update() {{ document.getElementById("out").value = "World: 1st " + pick("world1") + ", 2nd " + pick("world2") +
  ". 2D: 1st " + pick("2d1") + ", 2nd " + pick("2d2") + ". Alpha (chains, windows): model or bicubic? ... Notes: "; }}
document.addEventListener("change", update); update();
</script>
</body></html>
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("install", type=Path)
    parser.add_argument("--dump", type=Path, default=None, help="the census dump (default INSTALL/Data/TextureDump)")
    args = parser.parse_args()
    dump = args.dump or args.install / "Data" / "TextureDump"
    sizes = read_sizes(dump)
    missing = [key for key, _ in SAMPLE_WORLD + SAMPLE_2D if key not in sizes]
    if missing:
        raise SystemExit(f"not in the dump: {missing}")
    folder = args.install / "T4 bake-off"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "sample_world.txt").write_text("".join(f"{key}\n" for key, _ in SAMPLE_WORLD))
    (folder / "sample_2d.txt").write_text("".join(f"{key}\n" for key, _ in SAMPLE_2D))
    (folder / "sheet.html").write_text(sheet_html(sizes), encoding="utf-8")
    (args.install / "T4 bake-off.cmd").write_bytes(cmd_text().encode())
    for letter, world, two_d in PACKS:
        (args.install / f"Play upscaled {letter}.cmd").write_bytes(play_cmd(letter, world, two_d).encode())
    print(f"bake-off written to {folder}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
