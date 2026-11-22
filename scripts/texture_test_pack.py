#!/usr/bin/env python3
"""A hand-made texture pack for OT_TEXTURE_PACK (docs/HOOKS.md): every texture of a texture dump scaled by nearest neighbour and marked, so a replaced texture is obvious on screen.

    python scripts/texture_test_pack.py DUMP_DIR OUT_DIR [--tag TEXT ...] [--scale 4] [--mark both|tint|grid|none] [--classes TSV] [--jobs N]

DUMP_DIR is an OT_TEXTURE_DUMP folder (`<key>.png`, `index.tsv`, `seen.tsv`). With --tag only the textures seen in runs whose tag contains one of the texts are made
(the census tags its runs `pc:L12`, `psx:L12`: level 12 is Warehouse). Not made: a texture that is only ever drawn in 2D (the HUD stays original), and one under 16 texels on
a side (the plan's T3 rule). The mark is a tint towards cyan and a dark line at every eighth texel; alpha is kept as it is. Nothing here needs a GPU: this is also the stand-in
for the upscaler in tests. Writes `<key>.png` and `stamp.txt` into OUT_DIR.
With --classes (the class table, re/config/texture_classes.tsv) the table decides instead of the dump's use column: every key of the table the dump has, `world` ones of 16
texels a side or more tinted cyan, `2d` ones of any size tinted magenta, so a class mistake shows on screen.
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path

MIN_SIDE = 16


def read_png(data: bytes) -> tuple[int, int, bytes]:
    """8-bit RGBA PNG (the dump's, or any filter type) -> (width, height, packed rows)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, kind, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if (depth, kind, interlace) != (8, 6, 0):
                raise ValueError("only 8-bit RGBA, not interlaced")
        elif tag == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    previous = bytearray(stride)
    for row in range(height):
        start = row * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = line[i - 4] if i >= 4 else 0
            up = previous[i]
            upper_left = previous[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 255
            elif kind == 4:
                p = left + up - upper_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else upper_left)) & 255
        out += line
        previous = line
    return width, height, bytes(out)


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    stride = width * 4
    raw = b"".join(b"\0" + rgba[row * stride:(row + 1) * stride] for row in range(height))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def scale_nearest(width: int, height: int, rgba: bytes, scale: int) -> bytes:
    rows = []
    for y in range(height):
        line = b"".join(rgba[4 * (y * width + x):4 * (y * width + x) + 4] * scale for x in range(width))
        rows.extend([line] * scale)
    return b"".join(rows)


def mark(width: int, height: int, rgba: bytes, scale: int, how: str, colour: str = "cyan") -> bytes:
    """`width` x `height` is the scaled picture. Tint: 35% towards cyan (or magenta). Grid: a dark pixel line at every eighth original texel."""
    if how == "none":
        return rgba
    out = bytearray(rgba)
    if how in ("tint", "both"):   # per channel lookup tables over the whole picture
        down = bytes(c * 65 // 100 for c in range(256))
        up = bytes((c * 65 + 255 * 35) // 100 for c in range(256))
        for channel, table in zip((0, 1, 2), (down, up, up) if colour == "cyan" else (up, down, up)):
            out[channel::4] = out[channel::4].translate(table)
    if how in ("grid", "both"):
        step = 8 * scale
        dark = bytes(c // 3 for c in range(256))
        stride = 4 * width
        for y in range(height):
            row = out[y * stride:(y + 1) * stride]
            if y % step == 0:
                for channel in range(3):
                    row[channel::4] = row[channel::4].translate(dark)
            else:
                for channel in range(3):
                    row[channel:stride:4 * step] = row[channel:stride:4 * step].translate(dark)
            out[y * stride:(y + 1) * stride] = row
    return bytes(out)


def wanted_keys(dump: Path, tags: list[str]) -> list[str]:
    """Keys of the dump to make: seen in a run whose tag matches (all when no tag is given), not 2D-only, both sides at least MIN_SIDE."""
    keys: dict[str, tuple[int, int, str]] = {}
    for line in (dump / "index.tsv").read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        cols = line.split("\t")
        keys[cols[0]] = (int(cols[1]), int(cols[2]), cols[5])
    seen: set[str] | None = None
    if tags:
        seen = {cols[1] for cols in (line.split("\t") for line in (dump / "seen.tsv").read_text().splitlines()) if len(cols) > 1 and any(t in cols[0] for t in tags)}
    return sorted(k for k, (w, h, use) in keys.items() if (seen is None or k in seen) and use != "2d" and w >= MIN_SIDE and h >= MIN_SIDE)


def read_classes(path: Path) -> dict[str, tuple[int, int, str]]:
    """The class table: key -> (width, height, class)."""
    rows = (line.split("\t") for line in path.read_text().splitlines() if line and not line.startswith("#"))
    return {cols[0]: (int(cols[1]), int(cols[2]), cols[3]) for cols in rows}


def wanted_by_class(dump: Path, tags: list[str], classes: dict[str, tuple[int, int, str]]) -> dict[str, str]:
    """Keys to make by the class table: key -> class; the dump must have the key (and a run whose tag matches, when tags are given); world keys need 16 a side."""
    present = {line.split("\t")[0] for line in (dump / "index.tsv").read_text().splitlines() if line and not line.startswith("#")}
    if tags:
        present &= {cols[1] for cols in (line.split("\t") for line in (dump / "seen.tsv").read_text().splitlines()) if len(cols) > 1 and any(t in cols[0] for t in tags)}
    return {key: cls for key, (w, h, cls) in sorted(classes.items()) if key in present and (cls == "2d" or (w >= MIN_SIDE and h >= MIN_SIDE))}


def _make_one(job: tuple[Path, Path, str, int, str, str]) -> None:
    dump, out, key, scale, how, colour = job
    width, height, rgba = read_png((dump / f"{key}.png").read_bytes())
    big = mark(width * scale, height * scale, scale_nearest(width, height, rgba, scale), scale, how, colour)
    write_png(out / f"{key}.png", width * scale, height * scale, big)


def make_pack(dump: Path, out: Path, tags: list[str], scale: int, how: str, jobs: int = 1, classes: Path | None = None) -> int:
    out.mkdir(parents=True, exist_ok=True)
    if classes is None:
        chosen = {key: "world" for key in wanted_keys(dump, tags)}
    else:
        chosen = wanted_by_class(dump, tags, read_classes(classes))
    keys = list(chosen)
    work = [(dump, out, key, scale, how, "cyan" if chosen[key] == "world" else "magenta") for key in keys]
    if jobs > 1:
        import multiprocessing

        with multiprocessing.Pool(jobs) as pool:
            pool.map(_make_one, work, chunksize=16)
    else:
        for job in work:
            _make_one(job)
    rule = f"classes {classes.name} (world cyan, 2d magenta)" if classes is not None else "classes none (the dump's use: not 2D-only)"
    (out / "stamp.txt").write_text(f"tool nearest-neighbour test pack\nscale {scale}\nmark {how}\ntags {' '.join(tags) or 'all'}\n{rule}\n")
    return len(keys)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("dump", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--tag", action="append", default=[])
    parser.add_argument("--scale", type=int, default=4)
    parser.add_argument("--mark", choices=["both", "tint", "grid", "none"], default="both")
    parser.add_argument("--classes", type=Path, default=None, help="the class table: make world and 2d keys, marked in two colours")
    parser.add_argument("--jobs", type=int, default=1, help="processes (the whole dump at 4x takes minutes on one)")
    args = parser.parse_args()
    print(f"{make_pack(args.dump, args.out, args.tag, args.scale, args.mark, args.jobs, args.classes)} textures -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
