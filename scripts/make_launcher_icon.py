#!/usr/bin/env python3
"""Builds the THPSource2 launcher's icon from its artwork, src/launcher/thpsource2/app/icon.png (512 x 512 RGBA, transparent outside the tile):

  thpsource2.ico   16..256 px (PNG-compressed): the file's icon in Explorer, the window's and the taskbar's (launcher.rc, resource 1)
  header.png       96 px: the picture beside the title in the launcher's header (launcher.rc, resource 2)

Needs Pillow:  python scripts/make_launcher_icon.py
"""
import io
import struct
from pathlib import Path

from PIL import Image

APP = Path(__file__).resolve().parent.parent / "src" / "launcher" / "thpsource2" / "app"
SIZES = (16, 24, 32, 48, 64, 128, 256)
HEADER_SIZE = 96


def png_bytes(image: Image.Image) -> bytes:
    out = io.BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def main() -> None:
    master = Image.open(APP / "icon.png").convert("RGBA")
    images = [(size, png_bytes(master.resize((size, size), Image.LANCZOS))) for size in SIZES]
    ico = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, data in images:
        ico += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        ico += data
    (APP / "thpsource2.ico").write_bytes(bytes(ico))
    (APP / "header.png").write_bytes(png_bytes(master.resize((HEADER_SIZE, HEADER_SIZE), Image.LANCZOS)))
    print(f"wrote {APP / 'thpsource2.ico'} ({len(ico)} bytes) and {APP / 'header.png'}")


if __name__ == "__main__":
    main()
