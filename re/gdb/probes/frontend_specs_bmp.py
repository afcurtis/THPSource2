"""Phase 4.10i3 probe: the 8-bit BMP loader and the 2D screen primitives of shellutils.cpp (0x004afd40..0x004b0970).

Groups: bmp (Load8BitBMP2, its file wrapper, the palette semi-transparency pass), bmpv (the two dead variants that expand the picture to 16-bit pixels and upload it) and prim (the
flat quad, the draw area, the primitive reset, the textured quad). They share one world: a small slab with the picture, the out cells, a renderer with its ordering table, a draw
list and the allocation arena. Run one group per capture (the groups stub the same VRAM calls differently).

Slab layout (offsets from the scratch slab):
  0x0000 the BMP the loader reads (0x1800)   0x1800 the four out cells   0x1840 the file name   0x1880 the viewport edges record   0x1900 the renderer   0x1a00 the main list head cell
  0x2000 the ordering table (0x500; the renderer's table pointer is 0x2080, so the slots -16..127 are inside)   0x2600 the draw packets (0x800)   0x3000 a texture record
  0x4000 the allocation arena (0xc000, zeroed)
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import SCRATCH_LINE, ensure
from frontend_specs_menu import Image

GROUPS = ("bmp", "bmpv", "prim")

SLAB = 0x10000
BMP = 0x0000
OUTS = 0x1800
NAME = 0x1840
VIEW = 0x1880
RENDERER = 0x1900
HEAD = 0x1A00
OT = 0x2000
OT_TABLE = 0x2080
PACKETS = 0x2600
RECT = 0x3000
ARENA = 0x4000
ARENA_SIZE = SLAB - ARENA

WORLD = [
    (0x0056B6FC, 4), (0x0056B2EC, 4),      # where the next palette goes in VRAM
    (0x00560FD0, 8),                       # the draw list's cursor and the renderer pointer
    (0x00568628, 4),                       # the end of the draw list
    (0x00537B3C, 4), (0x0056C57C, 4),      # the ordering-table slot, the primitive count
    (0x00563A38, 4),                       # the viewport edges pointer
    (0x029DA394, 8),                       # the screen size
]
STATIC = [(0x00518484, 4)]                 # the depth threshold (a float, zero)

# what the stubs read from the case being generated
CURRENT: dict = {"count": 256, "slot": 0, "size": 0, "file": b"", "vram": bytes(512), "alloc_fail": False, "depth16": True}


def regions(slab):
    return [(slab, SLAB)] + WORLD


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = STATIC


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    return {}


for _group in GROUPS:
    SEEDS[_group] = seed


def bmp(address, name, this=None):
    return spec(address, name, "bmp")


def bmpv(address, name, this=None):
    return spec(address, name, "bmpv")


def prim(address, name, this=None):
    return spec(address, name, "prim")


# ---- pictures ----------------------------------------------------------------------------------------------------------------------------------------------------------

def palette_entry(rng, magenta=True):
    kinds = ["random", "random", "magenta", "magenta", "pure", "black", "near", "edge", "green"]
    kind = rng.choice(kinds if magenta else [k for k in kinds if k not in ("magenta", "pure")])
    if kind == "magenta":
        return bytes([rng.randrange(0x80, 256), 0, rng.randrange(0x80, 256), 0])
    if kind == "pure":
        return bytes([0xFF, 0, 0xFF, 0])
    if kind == "black":
        return bytes([0, 0, 0, 0])
    if kind == "near":
        return bytes([rng.randrange(8), rng.randrange(8), rng.randrange(8), 0])
    if kind == "edge":
        entry = [rng.choice([0x7F, 0x80, 0xF7, 0xF8, 0xFF, 0]) for _ in range(3)] + [0]
    elif kind == "green":
        entry = [0xFF, rng.randrange(1, 256), 0xFF, 0]
    else:
        entry = [rng.randrange(256) for _ in range(3)] + [0]
    if not magenta and entry[0] >= 0x80 and entry[1] == 0 and entry[2] >= 0x80:
        entry[1] = 1                      # (not the transparent colour)
    return bytes(entry)


def build_bmp(rng, width, height, field, depth=8, index_limit=256):
    """A BMP as the loader reads it: the header words it uses, the palette (`field` colours, 0 meaning 256) and the bottom-up rows padded to four bytes."""
    count = field or 256
    stride = (width + 3) & ~3
    # (the transparent index also fills the row padding, and the 16-bit variants index the palette with a signed byte: they keep it below 0x80)
    palette = b"".join(palette_entry(rng, magenta=index_limit > 128 or i < 128) for i in range(count))
    rows = bytearray()
    for _ in range(height):
        rows += bytes(rng.randrange(index_limit) for _ in range(width)) + bytes(rng.randrange(256) for _ in range(stride - width))
    header = bytearray(0x36)
    header[0:2] = b"BM"
    struct.pack_into("<I", header, 0x0E, 40)
    struct.pack_into("<I", header, 0x12, width)
    struct.pack_into("<I", header, 0x16, height)
    struct.pack_into("<H", header, 0x1A, 1)
    struct.pack_into("<H", header, 0x1C, depth)
    struct.pack_into("<I", header, 0x2E, field)
    data = bytes(header) + palette + bytes(rows)
    data += bytes(-len(data) % 4)
    struct.pack_into("<I", header, 2, len(data))
    return bytes(header) + data[0x36:]


def picture(rng, *, loader):
    """Width, height, colour field and depth for a case. The 16-bit variants index the palette with a signed byte, so their pictures keep every index below 0x80 and name at least that many colours."""
    width = rng.choice([1, 2, 3, 4, 5, 7, 8, 12, 16, 17, 31, 32, 33, rng.randrange(1, 41)])
    height = rng.choice([1, 2, 3, 5, 8, rng.randrange(1, 31)])
    if loader:
        field = rng.choice([0, 0, 0, 256, 16, 17, 100, 255, 1, 2])
        depth = rng.choice([8, 8, 8, 8, 8, 8, 4, 16, 24, 1, 32])
        limit = rng.choice([256, 256, 64, 4])
    else:
        field = rng.choice([0, 0, 256, 200, 150, 128])
        depth = rng.choice([8, 8, 8, 8, 8, 4, 24])
        limit = 128
    return width, height, field, depth, limit


def place(env, img, data):
    img.put(env.slab + BMP, data)


def out_cells(env, img, rng):
    for k in range(4):
        img.u32(env.slab + OUTS + 4 * k, rng.getrandbits(32))
    return [env.slab + OUTS + 4 * k for k in range(4)]


def palette_world(env, img, rng):
    img.u32(0x0056B6FC, rng.choice([0, 64, 128, 960, 0x3C0, 0x3F0, rng.randrange(0, 1024), 0xFFFFFFF0, 0xFFFFFC40, 16]))
    img.u32(0x0056B2EC, rng.choice([0, 0x100, 0x1FF, rng.randrange(0, 512), 0xFFFFFF00]))
    CURRENT["slot"] = rng.choice([0, 1, 7, rng.randrange(0, 512), 0xFFFFFFFF])
    CURRENT["alloc_fail"] = False


# ---- the loader group -------------------------------------------------------------------------------------------------------------------------------------------------

def tail_blank():
    count = CURRENT["count"]
    return [(8 + 2 * count, 512 - 2 * count)] if count < 256 else []


@bmp(0x004AFD40, "Load8BitBMP2")
def gen_load(env, n):
    img, rng = Image(), env.rng
    width, height, field, depth, limit = picture(rng, loader=True)
    data = build_bmp(rng, width, height, field, depth, limit)
    place(env, img, data)
    palette_world(env, img, rng)
    CURRENT["count"] = field or 256
    return [env.slab + BMP] + out_cells(env, img, rng), img.pokes()


@bmp(0x004AFFE0, "Load8BitBMP2_File")
def gen_load_file(env, n):
    img, rng = Image(), env.rng
    width, height, field, depth, limit = picture(rng, loader=True)
    data = build_bmp(rng, width, height, field, depth, limit)
    palette_world(env, img, rng)
    CURRENT["count"] = field or 256
    kind = rng.random()
    img.put(env.slab + NAME, b"Pic_%02d.bmp\0" % rng.randrange(100))
    if kind < 0.08:
        CURRENT["size"] = rng.choice([0, 0xFFFFFFFF, 0x80000000])      # the opener found no file
        CURRENT["file"] = b""
    else:
        CURRENT["size"] = len(data)
        CURRENT["file"] = data
        CURRENT["alloc_fail"] = kind > 0.94                             # the allocator refused
    return [env.slab + NAME] + out_cells(env, img, rng), img.pokes()


@bmp(0x004B0580, "Clut_SetSemiTransparent")
def gen_semi(env, n):
    img, rng = Image(), env.rng
    CURRENT["count"] = 256
    words = []
    for _ in range(256):
        words.append(rng.choice([0, 0, 0x8000, 0x7FFF, rng.getrandbits(16), rng.getrandbits(15), 1]))
    CURRENT["vram"] = struct.pack("<256H", *words)
    return [rng.choice([0, 64, 960, rng.randrange(0, 1024), 0x10000 + rng.randrange(0x100)]), rng.choice([0, 480, rng.randrange(0, 512), 0xFFFF0000 | rng.randrange(512)])], img.pokes()


# ---- the dead 16-bit variants -------------------------------------------------------------------------------------------------------------------------------------------

@bmpv(0x004B0320, "Bmp8_Upload")
def gen_upload(env, n):
    img, rng = Image(), env.rng
    width, height, field, depth, limit = picture(rng, loader=False)
    place(env, img, build_bmp(rng, width, height, field, depth, limit))
    return [env.slab + BMP], img.pokes()


@bmpv(0x004B0060, "Bmp8_UploadTall")
def gen_upload_tall(env, n):
    return gen_upload(env, n)


# ---- the primitives -----------------------------------------------------------------------------------------------------------------------------------------------------

def draw_world(env, img, rng, *, room=None):
    """The draw list (a cursor with room for `room` bytes, or a random amount, some of it none or negative), the renderer with its ordering table, the viewport depth source and the screen size."""
    slab = env.slab
    cursor = slab + PACKETS + 4 * rng.randrange(0, 8)
    room = rng.choice([-4, 0, 0x14, 0x1C, 0x20, 0x80, 0x400]) if room is None else room
    img.u32(0x00560FD0, cursor)
    img.u32(0x00568628, (cursor + room) & 0xFFFFFFFF)
    img.u32(0x00560FD4, slab + RENDERER)
    table = slab + OT_TABLE
    img.u32(slab + RENDERER + 0x88, table)
    img.u32(slab + RENDERER + 0x84, table if rng.random() < 0.3 else slab + HEAD)
    img.u32(slab + HEAD, rng.choice([0, slab + PACKETS + 0x700, rng.getrandbits(32)]))
    for slot in range(-16, 128):
        img.u32(table + 8 * slot, rng.choice([0, 0, slab + PACKETS + 0x100 + 4 * rng.randrange(0, 64), rng.getrandbits(32)]))
    img.u32(0x00537B3C, rng.choice([0xFFF - 0xFFF + rng.randrange(1, 100), rng.randrange(-16, 128), 0, 1, 127, -16 & 0xFFFFFFFF]) & 0xFFFFFFFF)
    img.u32(0x00563A38, slab + VIEW)
    img.put(slab + VIEW, bytes(rng.randrange(256) for _ in range(0x10)))
    img.u16(slab + VIEW + 8, rng.choice([0, 1, 100, 512, 640, 0xFFFF, rng.getrandbits(16)]))
    img.u32(0x0056C57C, rng.getrandbits(32))
    img.u32(0x029DA394, rng.choice([640, 512, 800, 1024, 320, 0x7FFFFF + 5]))
    img.u32(0x029DA398, rng.choice([480, 240, 600, 768, 100]))


def coordinate(rng):
    return rng.choice([rng.randrange(0, 512), rng.randrange(0, 240), rng.randrange(-40, 600), 0, 511, 0xFFFF8000 | rng.randrange(0x8000), rng.randrange(0x10000, 0x12000),
                       rng.getrandbits(32)]) & 0xFFFFFFFF


def extent(rng):
    return rng.choice([rng.randrange(0, 512), rng.randrange(0, 100), rng.randrange(-40, 40), 0, 1, 0xFFFF, rng.getrandbits(32)]) & 0xFFFFFFFF


@prim(0x004B0600, "Screen_FillRect")
def gen_fill(env, n):
    img, rng = Image(), env.rng
    draw_world(env, img, rng)
    return [coordinate(rng), coordinate(rng), extent(rng), extent(rng), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], img.pokes()


@prim(0x004B0800, "Screen_SetDrawArea")
def gen_area(env, n):
    img, rng = Image(), env.rng
    draw_world(env, img, rng)
    return [coordinate(rng), coordinate(rng), extent(rng), extent(rng)], img.pokes()


@prim(0x004B0950, "Screen_ResetPrimitives")
def gen_reset(env, n):
    img, rng = Image(), env.rng
    img.u32(0x00537B3C, rng.getrandbits(32))
    img.u32(0x0056C57C, rng.getrandbits(32))
    return [], img.pokes()


@prim(0x004B0970, "Screen_DrawTexturedRect")
def gen_textured(env, n):
    img, rng = Image(), env.rng
    record = env.slab + RECT
    img.put(record, struct.pack("<4H", rng.getrandbits(16), rng.getrandbits(16), rng.choice([rng.randrange(0, 64), rng.getrandbits(16), 255, 256]),
                                rng.choice([rng.randrange(0, 64), rng.getrandbits(16), 255, 256])))
    depth = rng.choice([0x10, 0x10, 0x10, 8, 4, 0, 0x110, 0xFFFFFFFF])
    CURRENT["depth16"] = depth == 0x10
    return [record, coordinate(rng), coordinate(rng), depth, rng.getrandbits(32)], img.pokes()


# ---- stubs --------------------------------------------------------------------------------------------------------------------------------------------------------------------

def install_common(fc, env):
    """The allocator (it can refuse), the VRAM synchronisation calls."""
    ensure(fc, 0x0046F490, 4, "alloc", result=lambda a: 0 if CURRENT["alloc_fail"] else env.alloc(a))
    ensure(fc, 0x0046F4D0, 1, "mem_delete")
    ensure(fc, 0x004E8740, 1, "engine_004e8740")


def install_bmp_stubs(fc, env):
    if getattr(fc, "_bmp_installed", False):
        return
    fc._bmp_installed = True
    install_common(fc, env)
    ensure(fc, 0x0048A590, 0, "engine_0048a590", result=lambda a: CURRENT["slot"])
    ensure(fc, 0x004E8770, 2, "engine_004e8770", stack_to=SCRATCH_LINE, blob=[(0, 8), (1, 512)], blank=tail_blank)
    ensure(fc, 0x004E8750, 2, "engine_004e8750", stack_to=SCRATCH_LINE, blob=(0, 8), fill=(1, lambda a: CURRENT["vram"]))
    ensure(fc, 0x00449030, 1, "engine_00449030", text=True, result=lambda a: CURRENT["size"])
    ensure(fc, 0x00449230, 1, "engine_00449230", fill=(0, lambda a: CURRENT["file"]))
    ensure(fc, 0x00449660, 0, "engine_00449660")


def install_bmpv_stubs(fc, env):
    if getattr(fc, "_bmp_installed", False):
        return
    fc._bmp_installed = True
    install_common(fc, env)
    ensure(fc, 0x004E8770, 2, "engine_004e8770", stack_to=SCRATCH_LINE, blob=(0, 8))
    ensure(fc, 0x004E8960, 1, "engine_004e8960")


def install_prim_stubs(fc, env):
    if getattr(fc, "_bmp_installed", False):
        return
    fc._bmp_installed = True
    install_common(fc, env)
    ensure(fc, 0x004E87F0, 2, "engine_004e87f0", stack_to=SCRATCH_LINE, blob=(1, 8))
    ensure(fc, 0x004E87E0, 1, "engine_004e87e0", stack_to=SCRATCH_LINE, blob=(0, 0x28),
           blank=lambda: [(0, 4), (0x1E, 2), (0x26, 2)] + ([(0x0E, 2)] if CURRENT["depth16"] else []))


GROUP_STUBS = {"bmp": install_bmp_stubs, "bmpv": install_bmpv_stubs, "prim": install_prim_stubs}
