"""Function-level oracle for the texture and sprite layer (Phase 5.4a, 7b continued, part 3).

The same script (`src/hooks/oracle_script.h`) runs twice: in the hooked retail process, where it calls the real `D3DTex_*` / `D3DSprite_*` functions on the game's own
DirectDraw surfaces (`OPENTONY_SESSION_SCRIPT`, hooks off), and in `opentony_spike.exe`, where it calls the natives of the TextureKit on the surface store
(`OT_ORACLE_SCRIPT`). Each prints the records, palette cache records and surface pixels it left behind in a canonical text form (heap pointers print as `P`), and the two
texts must be equal. The cases are built here from a few generators, so the script is one deterministic text.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

from . import hooks, session_scenarios, static_data
from .common import ROOT, load_yaml, wine_env

SPIKE = ROOT / "build/hooks/kits/opentony_spike.exe"
KITS = ROOT / "build/hooks/kits/opentony_standalone.dll"
EXE = ROOT / "build/disc/files/SETUP/data/THawk2.exe"
DUMP = ROOT / "build/sessions/runs/spike-boot-career-hangar/session_5000.img"
HEAP = DUMP.with_suffix(".heap")
DATA = ROOT / "build/assets/all-pkr/files"
OUT_ROOT = ROOT / "build/oracle/texture"

# the delivery at which the retail process runs the script (the front end is up and DirectDraw is initialised long before); the session ends right after it
SCRIPT_AT = 1500

ALLOC_LOADED_BUFFER = 0x0046F490  # FileIO_AllocLoadedBuffer(size, heap, 1, 0): the loaders' allocation, the one Mem_Delete pairs with


def bitmap8(width: int, height: int, palette: list[tuple[int, int, int]], pixels_top_first: list[list[int]], *, colours_in_use: int | None = None) -> bytes:
    """A paletted 8-bit BMP file image: 14-byte header, 40-byte info header, 4-byte palette entries, rows bottom first padded to four bytes."""
    rows = b""
    for row in reversed(pixels_top_first):
        data = bytes(row)
        rows += data + b"\0" * (-len(data) % 4)
    table = b"".join(bytes((b0, b1, b2, 0)) for b0, b1, b2 in palette)
    used = len(palette) if colours_in_use is None else colours_in_use
    header = b"BM" + (14 + 40 + len(table) + len(rows)).to_bytes(4, "little") + bytes(4) + (14 + 40 + len(table)).to_bytes(4, "little")
    info = (40).to_bytes(4, "little") + width.to_bytes(4, "little") + height.to_bytes(4, "little") + (1).to_bytes(2, "little") + (8).to_bytes(2, "little") \
        + bytes(4) + len(rows).to_bytes(4, "little") + bytes(8) + used.to_bytes(4, "little") + bytes(4)
    return header + info + table + rows


def bitmap24(width: int, height: int, pixels_top_first: list[list[tuple[int, int, int]]]) -> bytes:
    """A 24-bit BMP file image: rows bottom first, B G R, each padded to four bytes. Pixels are given as (R, G, B)."""
    rows = b""
    for row in reversed(pixels_top_first):
        data = b"".join(bytes((b, g, r)) for r, g, b in row)
        rows += data + b"\0" * (-len(data) % 4)
    header = b"BM" + (14 + 40 + len(rows)).to_bytes(4, "little") + bytes(4) + (14 + 40).to_bytes(4, "little")
    info = (40).to_bytes(4, "little") + width.to_bytes(4, "little") + height.to_bytes(4, "little") + (1).to_bytes(2, "little") + (24).to_bytes(2, "little") \
        + bytes(4) + len(rows).to_bytes(4, "little") + bytes(16)
    return header + info + rows


def picture24(width: int, height: int) -> list[list[tuple[int, int, int]]]:
    """Every channel varies; one pixel is the key colour (R and B above 0xf7, G zero), one is black."""
    rows = [[((x * 37 + y * 11) & 0xFF, (x * 5 + y * 29 + 3) & 0xFF, (x * 13 + y * 7 + 1) & 0xFF) for x in range(width)] for y in range(height)]
    rows[min(1, height - 1)][min(2, width - 1)] = (0xFF, 0x00, 0xFF)
    rows[0][0] = (0, 0, 0)
    return rows


# the files the oracle's mod serves to both processes under NEWBMP\ (the game's own relative path; the overlay key is `newbmp/<name>`)
def named_files() -> dict[str, bytes]:
    return {
        "o8.bmp": bitmap8(6, 4, gradient_palette(16, key_at=3, zero_at=0), picture(6, 4, 16), colours_in_use=16),
        "o24.bmp": bitmap24(5, 3, picture24(5, 3)),
        "o24w.bmp": bitmap24(6, 5, picture24(6, 5)),
    }


MOD_ROOT = OUT_ROOT / "mods"


def write_mod() -> Path:
    """The mod folder (`mod.ini` and `Data\\NEWBMP\\*.bmp`) both processes find through OPENTONY_MODS."""
    data = MOD_ROOT / "oracle" / "Data" / "NEWBMP"
    data.mkdir(parents=True, exist_ok=True)
    (MOD_ROOT / "oracle" / "mod.ini").write_text("[Mod]\nName = oracle\nEnabled = 1\n")
    for name, content in named_files().items():
        (data / name).write_bytes(content)
    return MOD_ROOT


def gradient_palette(size: int = 256, key_at: int | None = 5, zero_at: int | None = 0) -> list[tuple[int, int, int]]:
    """Entries with every channel width in use; one is the key colour (R and B above 0xf7, G zero), one black."""
    entries = [((index * 5 + 3) & 0xFF, (index * 3) & 0xFF, (index * 7 + 1) & 0xFF) for index in range(size)]
    if key_at is not None:
        entries[key_at] = (0xFF, 0x00, 0xFF)
    if zero_at is not None:
        entries[zero_at] = (0, 0, 0)
    return entries


def picture(width: int, height: int, colours: int) -> list[list[int]]:
    return [[(x * 5 + y * 11 + (x * y) % 7) % colours for x in range(width)] for y in range(height)]


class Script:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def add(self, *parts: object) -> None:
        self.lines.append(" ".join(str(part) for part in parts))

    def comment(self, text: str) -> None:
        """A section heading; the output so far reaches its file first, so a run that crashes in a section still shows the sections before it."""
        self.lines.append(f"# {text}")
        self.lines.append("flush")

    def image(self, name: str, data: bytes) -> None:
        """The bitmap in a block of the loaders' allocator, as a PRE resource is."""
        self.add("call", f"{name}={ALLOC_LOADED_BUFFER:#x}", "c", len(data), 0, 1, 0)
        self.add("fill", name, 0, len(data))
        self.add("bytes", name, data.hex())

    def text(self, name: str, value: str) -> None:
        """A NUL-terminated string in a block of the game's allocator."""
        self.add("alloc", name, len(value) + 1)
        self.add("bytes", name, value.encode("ascii").hex())

    def device(self, *, pow2: int = 0, square: int = 0, max_size: int = 256, min_size: int = 1) -> None:
        self.add("set32", "0x29da3a0", 0)  # D3DSoftwareRenderer
        self.add("set8", "0x29da3a5", 0)
        self.add("set32", "0x6a052c", pow2)
        self.add("set32", "0x6a0530", square)
        self.add("set32", "0x6a0534", max_size)
        self.add("set32", "0x6a0538", max_size)
        self.add("set32", "0x6a053c", min_size)
        self.add("set32", "0x6a0540", min_size)
        self.add("set32", "0x6a0544", 0)  # no BMP dump of each surface

    def dump_record(self, label: str, record: str, *, surface: bool = True, pointers: str = "0,1,2,8,9,10") -> None:
        """The record's words (the listed ones are pointers) and its surface's pixels. Word 3 is a pointer under the software renderer (flag 0x200)."""
        self.add("rec", label, record, pointers)
        if surface:
            self.add("surf", f"{label}_surface", f"[{record}]")

    def dump_palette(self, label: str, record: str, entries: int) -> None:
        """The palette cache record and its `entries` colours in both tables (the rest of the 0x200-byte tables is whatever the allocator held)."""
        self.add("val", f"{label}_palette_checksum", f"[[{record}+0x20]]")
        self.add("val", f"{label}_palette_count", f"[[{record}+0x20]+4]")
        self.add("mem", f"{label}_palette_device", f"[[{record}+0x20]+8]", entries * 2)
        self.add("mem", f"{label}_palette_fixed", f"[[{record}+0x20]+0xc]", entries * 2)


def build_script() -> str:
    s = Script()
    s.comment("function-level oracle of the texture and sprite layer: tony/texture_oracle.py")
    s.device()
    s.add("mem", "device_format", "0x6a04d8", 32)
    s.add("mem", "device_shifts", "0x6a04f8", 32)

    # 1. a paletted bitmap into a new record: the palette conversion, the key colour, the padding and flip of the rows, the upload
    s.comment("1: LoadIndexedBitmap, a new record")
    small = bitmap8(3, 2, gradient_palette(4, key_at=1, zero_at=0), [[3, 2, 1], [1, 2, 3]], colours_in_use=4)
    s.image("c1_image", small)
    s.add("call", "c1_record=0x4d9950", "c", "c1_image", 0, 0, 0)
    s.dump_record("c1", "c1_record")
    s.add("mem", "c1_pixels", "[c1_record+8]", 8)
    s.dump_palette("c1", "c1_record", 4)

    # 2. the same record loaded again: it keeps its palette cache record when the sizes agree
    s.comment("2: LoadIndexedBitmap, the record again")
    other = bitmap8(3, 2, [(9, 9, 9), (0xFF, 0, 0xFF), (200, 100, 30), (40, 0, 0)], [[0, 2, 3], [3, 2, 0]], colours_in_use=4)
    s.image("c2_image", other)
    s.add("let", "c2_old_palette", "[c1_record+0x20]")
    s.add("call", "c2_record=0x4d9950", "c", "c2_image", "c1_record", 0, 0)
    s.add("same", "c2_same_record", "c2_record", "c1_record")
    s.add("same", "c2_keeps_palette", "[c2_record+0x20]", "c2_old_palette")
    s.dump_record("c2", "c2_record")
    s.add("mem", "c2_pixels", "[c2_record+8]", 8)
    s.dump_palette("c2", "c2_record", 4)

    # 3. the device's rules: power of two, square, a smallest size, a largest size
    for name, rules, width, height in (
        ("c3a", {"pow2": 1}, 5, 3),
        ("c3b", {"pow2": 1, "square": 1}, 5, 3),
        ("c3c", {"min_size": 16}, 5, 3),
        ("c3d", {"max_size": 4}, 6, 5),
        ("c3e", {"pow2": 1, "square": 1, "min_size": 8, "max_size": 64}, 33, 7),
    ):
        s.comment(f"3: the device rules {rules}, {width}x{height}")
        s.device(**rules)
        s.image(f"{name}_image", bitmap8(width, height, gradient_palette(), picture(width, height, 256), colours_in_use=0))
        s.add("call", f"{name}_record=0x4d9950", "c", f"{name}_image", 0, 0, 0)
        s.dump_record(name, f"{name}_record")
    s.device()

    # 4. a picture larger than the device's texture limit, cut into tiles (a record that is in no list, as a sprite's is)
    s.comment("4: BuildTiles")
    for name, rules, width, height, tiles in (
        ("c4a", {}, 300, 100, 2),
        ("c4b", {"pow2": 1}, 300, 100, 2),
        ("c4c", {}, 100, 50, 1),
        ("c4d", {"max_size": 64, "min_size": 16}, 130, 70, 6),
    ):
        s.device(**rules)
        s.image(f"{name}_image", bitmap8(width, height, gradient_palette(), picture(width, height, 256)))
        s.add("alloc", f"{name}_blank", 0x2C)
        s.add("call", f"{name}_record=0x4d9950", "c", f"{name}_image", f"{name}_blank", 1, 0)
        s.add("alloc", f"{name}_out", 8)
        s.add("call", f"{name}_count=0x4d83a0", "c", f"{name}_record", f"{name}_out", f"{name}_out+4")
        s.add("val", f"{name}_count", f"{name}_count")
        s.add("let", f"{name}_tiles", f"[{name}_out]")
        s.add("mem", f"{name}_rectangles", f"[{name}_out+4]", 16 * tiles)
        for k in range(tiles):
            s.dump_record(f"{name}_tile{k}", f"{name}_tiles+{k * 0x2C:#x}")
    s.device()

    # 5. grey
    s.comment("5: GrayTexture")
    s.image("c5_image", bitmap8(8, 8, gradient_palette(), picture(8, 8, 256)))
    s.add("call", "c5_record=0x4d9950", "c", "c5_image", 0, 0, 0)
    s.add("surf", "c5_before", "[c5_record]")
    s.add("call", "0x4d9f50", "c", "c5_record")
    s.add("surf", "c5_after", "[c5_record]")

    # 6. sprites: one tile, two tiles, the background sprite; the tiles are attached to a material each
    s.comment("6: D3DSprite_Create / Destroy")
    for name, width, height, flag in (("c6a", 3, 2, 0), ("c6b", 300, 100, 0), ("c6c", 40, 30, 1)):
        s.device()
        s.image(f"{name}_image", bitmap8(width, height, gradient_palette(), picture(width, height, 256)))
        s.add("alloc", f"{name}_sprite", 0x40)
        s.add("call", f"{name}_result=0x4d74a0", "t", f"{name}_sprite", f"{name}_image", 0, flag)
        s.add("same", f"{name}_result", f"{name}_result", f"{name}_sprite")
        s.add("val", f"{name}_scale", f"[{name}_sprite+0x14]")
        s.add("val", f"{name}_count", f"[{name}_sprite+0xc]")
        s.add("same", f"{name}_no_screen_copy", f"[{name}_sprite+0x18]", 0)
        s.add("same", f"{name}_is_background", "[0x6a04c8]", f"{name}_sprite")
        s.add("let", f"{name}_tiles", f"[{name}_sprite+4]")
        s.add("let", f"{name}_materials", f"[{name}_sprite+0x10]")
        s.add("mem", f"{name}_rectangles", f"[{name}_sprite+8]", 32 if name == "c6b" else 16)
        for k in range(2 if name == "c6b" else 1):
            s.dump_record(f"{name}_tile{k}", f"{name}_tiles+{k * 0x2C:#x}")
            s.add("same", f"{name}_tile{k}_material", f"[{name}_tiles+{k * 0x2C + 4:#x}]", f"{name}_materials+{k * 0x2C:#x}")
            s.add("same", f"{name}_material{k}_record", f"[{name}_materials+{k * 0x2C + 0x14:#x}]", f"{name}_tiles+{k * 0x2C:#x}")
        s.add("call", "0x4d78c0", "t", f"{name}_sprite")
        s.add("same", f"{name}_background_after_destroy", "[0x6a04c8]", f"{name}_sprite")

    # 7. destroying records: the list, the count and the head move as the disassembly says
    s.comment("7: DestroyRecord")
    s.device()
    s.image("c7_image", bitmap8(3, 2, gradient_palette(), picture(3, 2, 256)))
    s.add("let", "c7_count0", "[0x6a04d4]")
    s.add("let", "c7_head0", "[0x6a051c]")
    s.add("let", "c7_tail0", "[0x6a0520]")
    s.add("call", "c7_a=0x4d9950", "c", "c7_image", 0, 0, 0)
    s.add("call", "c7_b=0x4d9950", "c", "c7_image", 0, 0, 0)
    s.add("same", "c7_count_after_two", "[0x6a04d4]", "c7_count0+2")
    s.add("same", "c7_tail_is_b", "[0x6a0520]", "c7_b")
    s.add("same", "c7_a_next_is_b", "[c7_a+0x24]", "c7_b")
    s.add("same", "c7_b_previous_is_a", "[c7_b+0x28]", "c7_a")
    s.add("let", "c7_head", "[0x6a051c]")
    s.add("call", "0x4d8b50", "c", "c7_b")
    s.add("same", "c7_count_after_b", "[0x6a04d4]", "c7_count0+1")
    s.add("same", "c7_head_after_b", "[0x6a051c]", "c7_head")
    s.add("same", "c7_tail_after_b", "[0x6a0520]", "c7_a")
    s.add("same", "c7_a_next_after_b", "[c7_a+0x24]", 0)
    s.add("call", "0x4d8b50", "c", "c7_a")
    s.add("same", "c7_count_after_a", "[0x6a04d4]", "c7_count0")
    s.add("same", "c7_head_after_a", "[0x6a051c]", "c7_head0")
    s.add("same", "c7_tail_after_a", "[0x6a0520]", "c7_tail0")

    # 10. the named file of LoadIndexedBitmap (8d): a file that NEWBMP\<name> opens replaces the bitmap. An 8-bit one is read whole and its pixels and palette are used; a 24-bit
    # one makes the record a named 24-bit bitmap whose pixels UploadTexture reads from the file row by row (key colour, narrowing to the device's channel widths)
    s.comment("10: LoadIndexedBitmap, a named file")
    s.device()
    for label, text in (("n8", "x\\o8.bmp"), ("n24", "x\\o24.bmp"), ("n24w", "x\\o24w.bmp"), ("nmissing", "x\\nothere.bmp")):
        s.text(f"{label}_name", text)
    s.image("n_image", bitmap8(3, 2, gradient_palette(4, key_at=1, zero_at=0), [[3, 2, 1], [1, 2, 3]], colours_in_use=4))
    for label in ("n8", "n24", "n24w", "nmissing"):
        s.add("call", f"{label}_record=0x4d9950", "c", "n_image", 0, 0, f"{label}_name")
        s.dump_record(label, f"{label}_record")
        if label == "n8":
            s.add("mem", f"{label}_pixels", f"[{label}_record+8]", 24)
            s.dump_palette(label, f"{label}_record", 16)
    # the same on a power-of-two, square device (the surface is larger than the file), and on a record that exists
    s.device(pow2=1, square=1)
    s.add("call", "n24p_record=0x4d9950", "c", "n_image", 0, 0, "n24w_name")
    s.dump_record("n24p", "n24p_record")
    s.device()
    s.add("call", "n_old=0x4d9950", "c", "n_image", 0, 0, 0)
    s.add("call", "n_again=0x4d9950", "c", "n_image", "n_old", 0, "n24_name")
    s.add("same", "n_again_same_record", "n_again", "n_old")
    s.dump_record("n_again", "n_again")
    s.add("call", "n_old8=0x4d9950", "c", "n_image", 0, 0, 0)
    s.add("call", "n_again8=0x4d9950", "c", "n_image", "n_old8", 0, "n8_name")
    s.dump_record("n_again8", "n_again8")
    s.add("mem", "n_again8_pixels", "[n_again8+8]", 24)
    s.dump_palette("n_again8", "n_again8", 16)
    # a keyed record (flag 0x80) gets mid grey for every palette entry
    s.add("call", "n_keyed=0x4d9950", "c", "n_image", 0, 0, 0)
    s.add("or32", "n_keyed+0x10", 0x80)
    s.add("call", "n_keyed_again=0x4d9950", "c", "n_image", "n_keyed", 0, 0)
    s.dump_record("n_keyed_again", "n_keyed_again")
    s.dump_palette("n_keyed_again", "n_keyed_again", 4)

    # 11. UploadTexture with `replace` (8d): the part of the existing surface the source covers is overwritten by a blit from a new staging surface
    s.comment("11: UploadTexture, replace")
    for label, rules in (("p8", {"pow2": 1}), ("p8s", {"pow2": 1, "square": 1, "min_size": 16})):
        s.device(**rules)
        s.image(f"{label}_image", bitmap8(5, 3, gradient_palette(), picture(5, 3, 256)))
        s.add("call", f"{label}_record=0x4d9950", "c", f"{label}_image", 0, 1, 0)
        s.add("surf", f"{label}_before", f"[{label}_record]")
        s.add("pattern", f"[{label}_record+8]", 24, 11)
        s.add("call", "0x4d8f10", "c", f"{label}_record", 0, 1, 1)
        s.dump_record(label, f"{label}_record")
    s.device(pow2=1)
    # (a named 24-bit record swaps its two size pairs at every upload, so the placeholder has the file's size: 6 x 5, o24w.bmp)
    s.image("p24_image", bitmap8(6, 5, gradient_palette(), picture(6, 5, 256)))
    s.add("call", "p24_record=0x4d9950", "c", "p24_image", 0, 0, "n24w_name")
    s.add("surf", "p24_before", "[p24_record]")
    s.add("call", "0x4d8f10", "c", "p24_record", 0, 1, 1)
    s.dump_record("p24", "p24_record")
    s.add("or32", "p24_record+0x10", 0x80)         # a keyed record: every pixel becomes mid grey
    s.add("call", "0x4d8f10", "c", "p24_record", 0, 1, 1)
    s.dump_record("p24_keyed", "p24_record")
    s.device()

    # 8. another pixel format of the device: 5-6-5, no alpha (the format words, and the channel widths and shifts the converters read)
    s.comment("8: a 5-6-5 device")
    for offset, value in ((0x00, 0x20), (0x04, 0x40), (0x08, 0), (0x0C, 16), (0x10, 0xF800), (0x14, 0x07E0), (0x18, 0x001F), (0x1C, 0)):
        s.add("set32", f"{0x6A04D8 + offset:#x}", value)
    for address, value in ((0x6A04F8, 5), (0x6A04FC, 6), (0x6A0500, 5), (0x6A0504, 0), (0x6A0508, 11), (0x6A050C, 5), (0x6A0510, 0), (0x6A0514, 0)):
        s.add("set32", f"{address:#x}", value)
    s.image("c8_image", bitmap8(5, 4, gradient_palette(), picture(5, 4, 256)))
    s.add("call", "c8_record=0x4d9950", "c", "c8_image", 0, 0, 0)
    s.dump_record("c8", "c8_record")
    s.dump_palette("c8", "c8_record", 256)
    s.add("call", "0x4d9f50", "c", "c8_record")
    s.add("surf", "c8_gray", "[c8_record]")

    # 9. the software renderer: the record keeps a copy of its pixels in memory, a larger picture is one tile, the background sprite is copied through a screen-sized surface
    s.comment("9: the software renderer")
    for offset, value in ((0x00, 0x20), (0x04, 0x41), (0x08, 0), (0x0C, 16), (0x10, 0x7C00), (0x14, 0x03E0), (0x18, 0x001F), (0x1C, 0x8000)):
        s.add("set32", f"{0x6A04D8 + offset:#x}", value)
    for address, value in ((0x6A04F8, 5), (0x6A04FC, 5), (0x6A0500, 5), (0x6A0504, 1), (0x6A0508, 10), (0x6A050C, 5), (0x6A0510, 0), (0x6A0514, 15)):
        s.add("set32", f"{address:#x}", value)
    s.add("set32", "0x29da3a0", 1)
    s.image("c9_image", bitmap8(5, 4, gradient_palette(), picture(5, 4, 256)))
    s.add("call", "c9_record=0x4d9950", "c", "c9_image", 0, 0, 0)
    s.dump_record("c9", "c9_record", pointers="0,1,2,3,8,9,10")
    s.add("mem", "c9_copy", "[c9_record+8]", 5 * 4 * 2)
    s.add("val", "c9_screen", "[0x29da394]")
    s.add("val", "c9_screen_height", "[0x29da398]")
    s.image("c9b_image", bitmap8(300, 100, gradient_palette(), picture(300, 100, 256)))
    s.add("alloc", "c9b_sprite", 0x40)
    s.add("call", "0x4d74a0", "t", "c9b_sprite", "c9b_image", 0, 1)
    s.add("val", "c9b_count", "[c9b_sprite+0xc]")
    s.add("mem", "c9b_rectangles", "[c9b_sprite+8]", 16)
    s.add("let", "c9b_tiles", "[c9b_sprite+4]")
    s.dump_record("c9b_tile", "c9b_tiles", pointers="0,1,2,3,8,9,10")
    s.add("mem", "c9b_screen_copy_start", "[c9b_sprite+0x18]", 256)
    s.add("call", "0x4d78c0", "t", "c9b_sprite")
    # a record that keeps its surface (the software renderer then also keeps a copy of the pixels in memory), uploaded again with `replace`
    s.comment("11b: UploadTexture, replace, software renderer")
    s.image("c9r_image", bitmap8(5, 4, gradient_palette(), picture(5, 4, 256)))
    s.add("call", "c9r_record=0x4d9950", "c", "c9r_image", 0, 1, 0)
    s.dump_record("c9r", "c9r_record", pointers="0,1,2,3,8,9,10")
    s.add("mem", "c9r_copy", "[c9r_record+8]", 5 * 4 * 2)
    s.add("call", "0x4d8f10", "c", "c9r_record", 0, 1, 1)
    s.dump_record("c9r_replaced", "c9r_record", pointers="0,1,2,3,8,9,10")
    s.add("mem", "c9r_replaced_copy", "[c9r_record+8]", 5 * 4 * 2)
    s.add("set32", "0x29da3a0", 0)
    return "\n".join(s.lines) + "\n"


def run_retail(script: Path, *, label: str = "texture-oracle", timeout: float = 300.0, mods: Path | None = None, deliveries_after: int = 1) -> str:
    """The script's output from the retail process (hooks off; with `mods`, only the loose-file overlay on, so the files of the mod answer the game's opens).

    The session ends `deliveries_after` clock deliveries after the script starts; a script that delivers the timer's ticks itself (`tick`) needs room for them."""
    document = {"deliveries": SCRIPT_AT + deliveries_after, "keys": [], "every": 100000}
    extra = {"OPENTONY_SESSION_SCRIPT": hooks._wine_z_path(script), "OPENTONY_SESSION_SCRIPT_AT": str(SCRIPT_AT)}
    if mods is not None:
        extra.update({"OPENTONY_HOOKS": "none,FileIO_BackendOpen", "OPENTONY_MODS": hooks._wine_z_path(mods)})
    failure = ""
    try:
        finished = session_scenarios.session_run(label, document, hooks.ParityVariant("retail", "none"), timeout=timeout, extra_env=extra)
    except session_scenarios.SessionError as error:
        finished, failure = False, f" ({error})"
    if not finished:
        failure = f"crash: the retail process did not finish{failure}\n"   # (the script's `flush` lines left what it had printed)
    out = session_scenarios.run_dir(label) / "oracle.out"
    if not out.is_file():
        raise RuntimeError(f"the retail process wrote no {out}; see {session_scenarios.run_dir(label)}/")
    return out.read_text() + failure


def run_standalone(script: Path, out: Path, *, timeout: float = 300.0, mods: Path | None = None) -> str:
    """The script's output from the program with no retail image (`mods`: the folder of the loose-file overlay)."""
    out.parent.mkdir(parents=True, exist_ok=True)
    out.unlink(missing_ok=True)
    env = wine_env(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    env.update({
        "WINEDEBUG": "-all",
        "OT_ORACLE_SCRIPT": hooks._wine_z_path(script),
        "OT_ORACLE_OUT": hooks._wine_z_path(out),
        "OT_FILES_LOG": "1",
        "OPENTONY_DATA_DIR": str(DATA),
    })
    if mods is not None:
        env["OPENTONY_MODS"] = hooks._wine_z_path(mods)
    result = subprocess.run(
        ["wine", str(SPIKE), str(static_data.ensure_image(EXE)), str(KITS), str(DUMP), str(ROOT / "build/standalone-spike/expected.tsv"), str(HEAP)],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout, check=False,
    )
    if not out.is_file():
        raise RuntimeError(f"the standalone program wrote no output (exit {result.returncode}):\n{result.stdout[-1500:]}\n{result.stderr[-800:]}")
    return out.read_text()


def compare(retail: str, standalone: str) -> list[str]:
    """The lines on which the two outputs differ, as `label: retail ... / standalone ...` (long hex strings are cut around the first difference)."""
    a = retail.splitlines()
    b = standalone.splitlines()
    problems: list[str] = []
    for k in range(max(len(a), len(b))):
        left = a[k] if k < len(a) else "<missing>"
        right = b[k] if k < len(b) else "<missing>"
        if left == right:
            continue
        label = ((right if left == "<missing>" else left).split() or ["?"])[0:2]
        first = next((i for i, (x, y) in enumerate(zip(left, right)) if x != y), min(len(left), len(right)))
        lo = max(0, first - 24)
        problems.append(f"{' '.join(label)}: first difference at character {first}\n    retail     ...{left[lo:first + 40]}\n    standalone ...{right[lo:first + 40]}")
    return problems


def oracle_command(_args=None) -> int:
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    script = OUT_ROOT / "texture.oracle"
    script.write_text(build_script())
    print(f"script: {script} ({len(script.read_text().splitlines())} lines)")
    mods = write_mod()
    standalone = run_standalone(script, OUT_ROOT / "standalone.out", mods=mods)
    retail = run_retail(script, mods=mods)
    (OUT_ROOT / "retail.out").write_text(retail)
    problems = compare(retail, standalone)
    for problem in problems:
        print(problem)
    print(f"{len(retail.splitlines())} lines compared, {len(problems)} differ")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(oracle_command())
