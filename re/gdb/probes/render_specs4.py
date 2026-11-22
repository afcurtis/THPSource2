"""Phase 4.15e probe: the image classes (group rim: image.cpp 0x004564b0..0x004581a0).

An image object (0x80..0x100 bytes) lives in the slab with a random body and the fields the generators set; the flat rectangles and sprites are primitives built in a polygon arena of the slab
(the cursor at 0x00560fd0, the limit at 0x00568628) and linked into the current buffer's ordering table (a head word and the far word 0x7ff8 further on). The BMP loaders, the texture layer, the PSX
library and the base class of the objects are stubs; the BMP header of a file read is a scripted block of 0x40 bytes.

Slab layout (offsets): fake vtable 0x0000, objects 0x0100 (4 of 0x100), BMP buffers 0x0500 (0x200), material records 0x0700 (8 of 0x40), tables 0x0900 (0x100), palette nodes 0x0a00 (4 of 0x20),
palette colours 0x0a80 (0x40), primitive arena 0x1000 (0x1000), ordering table head 0x2000 and far word 0x9ff8, arena 0xa000 (0x2000 bytes).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from render_specs import BUFFER_A, BUFFER_B, DB_BYTES, DB_FIRST, SCRATCH, noise, word

GROUPS = ("rim",)

VTABLE = 0x0000
OBJECTS = 0x0100
BUFFERS = 0x0500
MATERIALS = 0x0700
TABLES = 0x0900
NODES = 0x0A00
COLOURS = 0x0A80
PRIMS = 0x1000
PRIM_BYTES = 0x1000
ORDERING = 0x2000
FAR_WORD = 0x9FF8
ARENA = 0xA000
ARENA_BYTES = 0x2000
SLAB = ARENA + ARENA_BYTES + 0x100

REGION_RECORDS = 0x0056D428
REGION_STRIDE = 0x44
GLOBALS = [(DB_FIRST, DB_BYTES), (0x00568628, 4), (0x029DA394, 8), (0x00562008, 0xC), (0x00561F68, 0x70), (0x0056B2EC, 4), (0x0056B4F4, 4), (0x0056B6F8, 8), (0x0056DB40, 4),
           (REGION_RECORDS, 22 * REGION_STRIDE)]
FAKE_DELETE = 0x004AD950


def regions(slab):
    return merged([(slab, ORDERING + 0x40), (slab + FAR_WORD, 8), (slab + ARENA, ARENA_BYTES + 0x100)] + GLOBALS, gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_BYTES
    return {env.slab + VTABLE: struct.pack("<4I", FAKE_DELETE, FAKE_DELETE, FAKE_DELETE, FAKE_DELETE)}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = []
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_render4_installed", False):
        return
    fc._render4_installed = True
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)
    for address, count in ((0x0047FDB0, 0), (0x0047FE20, 0)):
        ensure(fc, address, count, "engine_%08x" % address, convention="thiscall", this=True)
    ensure(fc, 0x0047FE30, 1, "engine_0047fe30", convention="thiscall", this=True)
    ensure(fc, 0x0047FD60, 1, "engine_0047fd60")
    ensure(fc, 0x004B1DE0, 2, "engine_004b1de0", result=lambda a: rng.getrandbits(16))
    ensure(fc, 0x0045EF80, 1, "engine_0045ef80")
    ensure(fc, 0x0045F530, 1, "engine_0045f530")
    ensure(fc, 0x004E8940, 5, "engine_004e8940")
    ensure(fc, 0x004E87F0, 2, "engine_004e87f0", blob=(1, 8), stack_to=SCRATCH)
    ensure(fc, 0x004E8730, 4, "engine_004e8730", blob=(0, 8), stack_to=SCRATCH)
    ensure(fc, 0x004D9F10, 4, "engine_004d9f10", result=lambda a: rng.getrandbits(32))
    ensure(fc, 0x004D9950, 4, "engine_004d9950", result=lambda a: rng.getrandbits(32))
    ensure(fc, 0x00449030, 1, "engine_00449030", result=lambda a: rng.choice([0x20, 0x100, 0x118, 0x400, rng.randrange(0x11, 0x600), 0x10, 0x11] if not env.bmp_error[0] else [0, 0xFFFFFFF0]))
    header = {"bytes": b""}
    env.bmp_error = [False]
    # the file read leaves a BMP header: the depth (4 or 8, sometimes wrong), the width and the height
    ensure(fc, 0x00449230, 1, "engine_00449230", fill=(0, lambda a: header["bytes"]))
    ensure(fc, 0x00449660, 0, "engine_00449660")
    ensure(fc, 0x004DA400, 1, "engine_004da400")
    ensure(fc, 0x004DA3E0, 2, "engine_004da3e0")
    ensure(fc, 0x004D9F40, 1, "engine_004d9f40", result=lambda a: env.slab + COLOURS + 0x20)
    ensure(fc, 0x004DA580, 1, "engine_004da580", result=lambda a: rng.getrandbits(32))
    ensure(fc, 0x004DA620, 2, "engine_004da620")
    ensure(fc, 0x004D9F50, 1, "engine_004d9f50", result=lambda a: rng.getrandbits(32))
    ensure(fc, 0x00466BB0, 1, "engine_00466bb0")
    ensure(fc, 0x004B2030, 1, "engine_004b2030", result=lambda a: env.slab + MATERIALS + 0x40 * rng.choice([0, 1, 1, 2, 3]) if env.rng.random() < 0.9 else 0)
    ensure(fc, 0x00416980, 1, "engine_00416980", result=lambda a: rng.choice([0, 1, 0x100, 0xFFFFFF01]))
    ensure(fc, 0x004D8C60, 6, "engine_004d8c60")
    ensure(fc, 0x004B20F0, 1, "engine_004b20f0", result=lambda a: env.slab + TABLES)
    env.bmp_header = header


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def rim(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "rim", this=resolve)


def obj(env, k=0):
    return env.slab + OBJECTS + 0x100 * k


def bmp_header(env, rng, depth=None, width=None, height=None):
    depth = rng.choice([4, 8, 8, 4, 16]) if depth is None else depth
    block = bytearray(noise(rng, 0x40))
    struct.pack_into("<H", block, 0x1C, depth)
    struct.pack_into("<H", block, 0x12, rng.randrange(1, 0x300) if width is None else width)
    struct.pack_into("<H", block, 0x16, rng.randrange(1, 0x200) if height is None else height)
    return bytes(block)


def world(img, env, rng, far_room=True):
    """The buffer records, the polygon arena and the screen scale."""
    img.put(DB_FIRST, noise(rng, DB_BYTES))
    for k, base in enumerate((BUFFER_A, BUFFER_B)):
        img.u32(base + 0x84, env.slab + ORDERING)
        img.u16(base + 0x90, rng.randrange(0, 0x100))
    img.u32(0x00560FD4, rng.choice([BUFFER_A, BUFFER_B]))
    cursor = env.slab + PRIMS + 4 * rng.randrange(0, 8)
    img.u32(0x00560FD0, cursor)
    img.u32(0x00568628, env.slab + PRIMS + PRIM_BYTES - 0x100 if rng.random() < 0.95 else cursor - 8)
    img.put(env.slab + PRIMS, noise(rng, PRIM_BYTES))
    img.put(env.slab + ORDERING, noise(rng, 0x40))
    img.put(env.slab + FAR_WORD, noise(rng, 8))
    img.u32(0x029DA394, rng.choice([0x200, 0x2AA, 0x400, 0x300, rng.randrange(0x100, 0x500)]))
    img.u32(0x029DA398, rng.choice([0xF0, 0x1E0, 0x168, 0x2D0, rng.randrange(0x80, 0x400)]))


def image_body(img, env, rng, at, depth_mode=None):
    img.put(at, noise(rng, 0x80))
    img.u8(at + 0xA, rng.choice([0, 1, 2]))
    img.u8(at + 0xB, rng.choice([0, 1, 2]))
    img.u32(at + 0xC, rng.choice([0x1000, 0x800, 0x2000, 0x1555, rng.randrange(0x100, 0x3000)]))
    img.u16(at + 0x40, rng.choice([8, 16, 32, 64, 100, rng.randrange(1, 256)]))
    img.u16(at + 0x42, rng.choice([8, 16, 32, 64, 100, rng.randrange(1, 256)]))
    img.u8(at + 0x58, rng.choice([0, 1, 2]) if depth_mode is None else depth_mode)
    img.u8(at + 4, rng.choice([0, 1]))
    img.u8(at + 5, rng.choice([0, 1]))


@rim(0x004564B0, "Image_Construct", OBJECTS)
def gen_image_construct(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + OBJECTS, noise(rng, 0x100))
    return [word(rng), word(rng)], img.pokes()


@rim(0x00456550, "Image_ScalarDelete", OBJECTS)
def gen_image_scalar_delete(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + OBJECTS, noise(rng, 0x100))
    return [rng.choice([0, 1, 3])], img.pokes()


@rim(0x00456570, "Image_Destruct", OBJECTS)
def gen_image_destruct(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + OBJECTS, noise(rng, 0x100))
    return [], img.pokes()


@rim(0x00456580, "Image_RenderObject", OBJECTS)
def gen_image_render(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + OBJECTS, noise(rng, 0x40))
    img.put(0x00561F68, noise(rng, 0x50))
    img.u32(0x0056200C, word(rng))
    return [], img.pokes()


def flat_args(rng):
    return [rng.choice([0, 10, 100, 0x1FF, 0xFFFF, 0xFFFFFFF0, rng.getrandbits(32), rng.randrange(0, 0x200)]) if k < 4 else rng.getrandbits(8) | (rng.getrandbits(24) << 8 if rng.random() < 0.3 else 0)
            for k in range(7)]


@rim(0x00456600, "Image_FlatRectangle")
def gen_flat(env, n):
    img, rng = Image(), env.rng
    world(img, env, rng)
    return flat_args(rng), img.pokes()


@rim(0x00456790, "Image_FlatRectangleNoTerminator")
def gen_flat2(env, n):
    img, rng = Image(), env.rng
    world(img, env, rng)
    return flat_args(rng), img.pokes()


@rim(0x004568D0, "Image_RectPrimitive")
def gen_rect(env, n):
    img, rng = Image(), env.rng
    world(img, env, rng)
    return [rng.randrange(0, 0x200) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0), rng.randrange(0, 0x100), rng.randrange(1, 0x200), rng.randrange(1, 0x100),
            rng.choice([0, 1, 0x100, 0xFF, 2])], img.pokes()


@rim(0x00456990, "Image_Nothing456990")
def gen_nothing(env, n):
    return [word(env.rng), word(env.rng)], Image().pokes()


@rim(0x004569A0, "Bmp_Depth")
def gen_depth(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + BUFFERS, bmp_header(env, rng))
    return [env.slab + BUFFERS], img.pokes()


@rim(0x00456A00, "Bmp_Load4")
def gen_load4(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + BUFFERS, bmp_header(env, rng, 4))
    img.u32(env.slab + TABLES, word(rng))
    return [env.slab + BUFFERS, env.slab + TABLES, env.slab + TABLES + 8, env.slab + TABLES + 12, env.slab + TABLES + 16, word(rng)], img.pokes()


@rim(0x00456A70, "Bmp_Load8")
def gen_load8(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + BUFFERS, bmp_header(env, rng, 8))
    img.u32(env.slab + TABLES, word(rng))
    return [env.slab + BUFFERS, env.slab + TABLES, env.slab + TABLES + 8, env.slab + TABLES + 12, env.slab + TABLES + 16, word(rng)], img.pokes()


@rim(0x00456AE0, "MakeTransparentClut2")
def gen_make_clut(env, n):
    from opentony.fcall import ForcedCalls  # noqa: F401  (the stub reads the header the generator chose)

    img, rng = Image(), env.rng
    env.bmp_header["bytes"] = bmp_header(env, rng, rng.choice([4, 8, 8, 4, 16]))
    env.bmp_error[0] = rng.random() < 0.15
    img.put(env.slab + TABLES, noise(rng, 0x40))
    name = env.slab + BUFFERS + 0x100
    img.put(name, b"data\\x.bmp\0")
    return [name, env.slab + TABLES, env.slab + TABLES + 8, env.slab + TABLES + 12, env.slab + TABLES + 16, env.slab + TABLES + 20], img.pokes()


@rim(0x00456C60, "SlicedImage_BaseConstruct", OBJECTS)
def gen_sliced_base(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    return [], img.pokes()


@rim(0x00456C90, "SlicedImage_PaletteSource", OBJECTS)
def gen_sliced_source(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    return [], img.pokes()


@rim(0x00456CB0, "SlicedImage_SetPalette", OBJECTS)
def gen_sliced_set_palette(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    return [env.slab + COLOURS], img.pokes()


@rim(0x00456CD0, "SlicedImage_Construct", OBJECTS)
def gen_sliced_construct(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    return [], img.pokes()


@rim(0x00456CF0, "SlicedImage_ScalarDelete", OBJECTS)
def gen_sliced_delete(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.u32(obj(env) + 0x3C, rng.choice([0, env.slab + ARENA + 0x100]))
    img.u8(obj(env) + 0x56, rng.choice([0, 1]))
    img.u8(obj(env) + 0x57, rng.choice([0, 1]))
    return [rng.choice([0, 1])], img.pokes()


def format_args(rng):
    width = rng.choice([8, 16, 64, 100, 255, 256, 0x27F, 0x280, 0, 0xFFFFFFFF, rng.randrange(1, 0x280)])
    height = rng.choice([8, 16, 64, 100, 255, 0x1DF, 0x1E0, 0, rng.randrange(1, 0x1E0)])
    x = rng.choice([0, 2, 8, 100, 0x27F, 0x280, 0xFFFFFFFF, rng.randrange(0, 0x280)])
    y = rng.choice([0, 2, 8, 100, 0x1DF, 0x1E0, 0xFFFFFFFF, rng.randrange(0, 0x1E0)])
    depth = rng.choice([4, 8, 0x10, 4, 8, 0x10, 5, 0x104, 0])
    return [word(rng), width, height, x, y, depth, rng.getrandbits(16) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0)]


@rim(0x00456D10, "FontGlyph_ConstructImage", OBJECTS)
def gen_glyph_construct(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.u32(0x00562010, rng.choice([0, 0, env.slab + COLOURS]))
    return format_args(rng), img.pokes()


@rim(0x00456D90, "SlicedImage2__removeFromMemory", OBJECTS)
def gen_remove(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.u32(obj(env) + 0x3C, rng.choice([0, env.slab + ARENA + 0x100]))
    img.u8(obj(env) + 0x56, rng.choice([0, 1]))
    img.u8(obj(env) + 0x57, rng.choice([0, 1]))
    return [], img.pokes()


@rim(0x00456E10, "FontGlyph_SetImageFormat", OBJECTS)
def gen_set_format(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.u32(0x00562010, rng.choice([0, 0, env.slab + COLOURS]))
    return format_args(rng), img.pokes()


@rim(0x00457090, "Image_BindPalette", OBJECTS)
def gen_bind_palette(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.u32(0x00562010, rng.choice([0, env.slab + COLOURS]))
    return [word(rng)], img.pokes()


for _address in (0x00457120, 0x00457130, 0x00457150):
    rim(_address, "Image_Nothing%06x" % _address, OBJECTS)(lambda env, n: ([], Image().pokes()))


@rim(0x00457160, "SlicedImage_Draw", OBJECTS)
def gen_sliced_draw(env, n):
    img, rng = Image(), env.rng
    world(img, env, rng)
    image_body(img, env, rng, obj(env))
    return [rng.randrange(-100, 700) & 0xFFFFFFFF, rng.randrange(-50, 500) & 0xFFFFFFFF, rng.choice([0, 1, 5])], img.pokes()


@rim(0x004573E0, "RawImage_Destruct", OBJECTS)
def gen_raw_destruct(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x20))
    return [], img.pokes()


@rim(0x00457400, "RawImage_ScalarDelete", OBJECTS)
def gen_raw_delete(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x20))
    return [rng.choice([0, 1, 2])], img.pokes()


def file_name(img, env):
    name = env.slab + BUFFERS + 0x100
    img.put(name, b"data\\x.bmp\0")
    return name


@rim(0x00457420, "BmpImage_Construct", OBJECTS)
def gen_bmp_construct(env, n):
    img, rng = Image(), env.rng
    env.bmp_error[0] = False
    img.put(obj(env), noise(rng, 0x100))
    env.bmp_header["bytes"] = bmp_header(env, rng, rng.choice([4, 8, 4, 8]), rng.randrange(1, 0x27F), rng.randrange(1, 0x1DF))
    return [file_name(img, env), rng.choice([0, 1]), rng.choice([0, 2, 8, 0x40, rng.randrange(0, 0x280)]), rng.choice([0, 2, 8, 0x40, rng.randrange(0, 0x1E0)])], img.pokes()


@rim(0x00457520, "BmpImage_ScalarDelete", OBJECTS)
def gen_bmp_delete(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.u8(obj(env) + 0x56, rng.choice([0, 1]))
    img.u8(obj(env) + 0x57, rng.choice([0, 1]))
    return [rng.choice([0, 1])], img.pokes()


@rim(0x00457540, "BmpImage_ConstructFromData", OBJECTS)
def gen_bmp_data_construct(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.put(env.slab + BUFFERS, bmp_header(env, rng, rng.choice([4, 8, 8, 4]), rng.randrange(1, 0x27F), rng.randrange(1, 0x1DF)))
    return [env.slab + BUFFERS, rng.choice([0, 1]), rng.choice([0, 2, 8, 0x40, rng.randrange(0, 0x280)]), rng.choice([0, 2, 8, 0x40, rng.randrange(0, 0x1E0)])], img.pokes()


@rim(0x00457660, "VramSprite_Construct", OBJECTS)
def gen_vram_sprite(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    return [], img.pokes()


@rim(0x004576F0, "BmpImage_Destruct", OBJECTS)
def gen_bmp_destruct(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.u8(obj(env) + 0x56, rng.choice([0, 1]))
    img.u8(obj(env) + 0x57, rng.choice([0, 1]))
    return [], img.pokes()


@rim(0x00457810, "Image_TextureValue", OBJECTS)
def gen_texture_value(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    return [], img.pokes()


@rim(0x00457820, "SlicedImage2__screenWidth", OBJECTS)
def gen_screen_width(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    return [], img.pokes()


@rim(0x00457830, "SlicedImage2__screenHeight", OBJECTS)
def gen_screen_height(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    return [], img.pokes()


@rim(0x00457870, "Sprite_Construct", OBJECTS)
def gen_sprite_construct(env, n):
    img, rng = Image(), env.rng
    env.bmp_error[0] = False
    img.put(obj(env), noise(rng, 0x100))
    env.bmp_header["bytes"] = bmp_header(env, rng, rng.choice([4, 8]), rng.randrange(1, 0x27F), rng.randrange(1, 0x1DF))
    return [file_name(img, env), rng.choice([0, 1]), rng.choice([0, 2, 8, 0x40]), rng.choice([0, 2, 8, 0x40])], img.pokes()


@rim(0x004578A0, "Sprite_ScalarDelete", OBJECTS)
def gen_sprite_delete(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.u8(obj(env) + 0x56, rng.choice([0, 1]))
    img.u8(obj(env) + 0x57, rng.choice([0, 1]))
    return [rng.choice([0, 1])], img.pokes()


@rim(0x004578C0, "Sprite_Destruct", OBJECTS)
def gen_sprite_destruct(env, n):
    img, rng = Image(), env.rng
    img.put(obj(env), noise(rng, 0x100))
    img.u8(obj(env) + 0x56, rng.choice([0, 1]))
    img.u8(obj(env) + 0x57, rng.choice([0, 1]))
    return [], img.pokes()


def material(img, env, rng, k):
    at = env.slab + MATERIALS + 0x40 * k
    img.put(at, noise(rng, 0x40))
    img.u32(at + 0x1C, env.slab + NODES + 0x20 * (k % 4))
    return at


def nodes(img, env, rng):
    for k in range(4):
        at = env.slab + NODES + 0x20 * k
        img.put(at, noise(rng, 0x20))
        img.u8(at + 3, rng.choice([0, 1, 2, 3]))
    img.u32(0x0056B6F8, rng.randrange(0, 0x400))
    img.u32(0x0056B6FC, rng.randrange(0, 0x400))
    img.u32(0x0056B4F4, rng.randrange(0, 0x200))
    img.u32(0x0056B2EC, rng.randrange(0, 0x200))


@rim(0x00457E10, "Image_LoadBmpIntoMaterial")
def gen_load_bmp_material(env, n):
    img, rng = Image(), env.rng
    for k in range(4):
        material(img, env, rng, k)
    img.put(env.slab + BUFFERS, bmp_header(env, rng, rng.choice([4, 8, 8, 4, 16])))
    img.u8(0x0056DB40, rng.choice([0, 1]))
    return [word(rng), env.slab + BUFFERS, word(rng)], img.pokes()


@rim(0x00457F30, "Image_DecrementRegionTextures")
def gen_decrement(env, n):
    img, rng = Image(), env.rng
    region = rng.randrange(0, 21) if rng.random() < 0.95 else 21
    record = REGION_RECORDS + REGION_STRIDE * region
    img.put(record, noise(rng, REGION_STRIDE))
    img.u8(record, rng.choice([0, 0x41, 0x41, 0x41]))
    img.u8(record + 0xA, rng.choice([0, 1, 1, 2]))
    raw = env.slab + BUFFERS
    img.u32(record + 0x18, raw)
    img.put(raw, noise(rng, 0x100))
    models = rng.choice([0, 1, 2, 5])
    img.u32(raw + 8, 0)
    img.u32(raw + 0xC, models)
    count = rng.choice([0, 1, 2, 3, 3, 4])
    entries = [material(img, env, rng, k) for k in range(4)]
    # the trailing tables answer: models words, then the count and the material pointers
    base = env.slab + TABLES
    nodes(img, env, rng)
    for k in range(4):
        img.u16(entries[k] + 0x10, rng.choice([1, 2, 5, 0, 10]))
        img.u32(entries[k] + 0x18, word(rng))
    tables = env.slab + TABLES
    img.u32(tables + 4 * models, count)
    for k in range(count):
        img.u32(tables + 4 + 4 * models + 4 * k, entries[k % 4])
    return [region], img.pokes()


@rim(0x004581A0, "Image_ClearMaterialPalette")
def gen_clear_palette(env, n):
    img, rng = Image(), env.rng
    for k in range(4):
        material(img, env, rng, k)
    nodes(img, env, rng)
    return [word(rng), rng.getrandbits(8) | (rng.getrandbits(24) << 8 if rng.random() < 0.3 else 0), rng.getrandbits(8), rng.getrandbits(8)], img.pokes()
