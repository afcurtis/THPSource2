"""Phase 4.15e probe: the D3D start-up, the fog tables, the sprite quad and the geometry packets (groups rd3d: the 53-bit x87 default; rd3dr: the same cases under the render window's 24-bit control word,
captured through a trampoline; fixtures named p24-*).

Slab layout (offsets): sprite list object 0x0100, rectangle list 0x0200 (4 of 0x10), info list 0x0300 (4 of 0x2c), texture records 0x0400 (4 of 0x20), viewport edges 0x0500, packets 0x0600 (8 of 8 bytes),
light matrix 0x0700, colour list 0x0800, primitive arena 0x0900 (0x400), ordering table 0x0e00 (head word) and far word 0x8df8 (0x7ff8 further on); the 24-bit trampoline lives at 0x1000 of the slab, past
the captured window (which covers 0x0000..0x0f00 and the far word).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from math_specs import CW_PC24, CW_PC53, TRAMP
from render_specs import BUFFER_A, BUFFER_B, DB_BYTES, DB_FIRST, noise, word

GROUPS = ("rd3d", "rd3dr")

SPRITE = 0x0100
RECTS = 0x0200
INFOS = 0x0300
TEXTURES = 0x0400
EDGES = 0x0500
PACKETS = 0x0600
LIGHT = 0x0700
LIST = 0x0800
PRIMS = 0x0900
PRIM_BYTES = 0x400
TABLE = 0x0E00
FAR_WORD = TABLE + 0x7FF8
SLAB = 0x0F00
SLAB_TOTAL = FAR_WORD + 0x100

FOG_ROWS = (0, 1, 2, 100, 254, 255)
FOG_TABLES = (0x0059D340, 0x005DD740, 0x0061D82C, 0x0065FC60)
GLOBALS = [(0x006A3E00, 0x120), (0x00560FC8, 0x1F8), (0x00563A38, 4), (0x029DA390, 0x14), (0x006A04C8, 4), (0x0058F324, 4), (0x0057E888, 0x40), (0x0056E874, 0x100), (0x00549E94, 4),
           (0x005808AC, 15000 * 4)]
GLOBALS += [(table + 1024 * row, 1024) for table in FOG_TABLES for row in FOG_ROWS]
STATIC = [(0x00518480, 8), (0x00518908, 0x10), (0x00518D60, 0x20), (0x00519930, 0x10), (0x00519960, 0x14)]


def regions(slab):
    return merged([(slab, SLAB), (slab + FAR_WORD, 8)] + GLOBALS, gap=0)


def seed(env, rng):
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB_TOTAL
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(STATIC)
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_render6_installed", False):
        return
    fc._render6_installed = True
    mark_under_test()
    ensure(fc, 0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def d3d(address, name, rounding=True):
    """A function captured under the 53-bit default (rd3d) and, when its arithmetic is floating point, under 24 bits (rd3dr)."""

    def decorate(generate):
        spec(address, name, "rd3d")(generate)
        if rounding:
            spec(address, name, "rd3dr", via=lambda env: trampoline(env, address, CW_PC24))(generate)
        return generate

    return decorate


def d3d_this(address, name, this):
    def decorate(generate):
        spec(address, name, "rd3d", this=lambda env: env.slab + this)(generate)
        spec(address, name, "rd3dr", this=lambda env: env.slab + this, via=lambda env: trampoline(env, address, CW_PC24))(generate)
        return generate

    return decorate


def trampoline(env, target, control_word):
    """math_specs.trampoline keeps the return address in ecx, which a thiscall needs for `this`: this one keeps it in eax (the return value is set after the call)."""
    from opentony.fcall import write_chunks

    base = env.slab + TRAMP
    code = bytes([0x58]) + bytes([0xA3]) + struct.pack("<I", base)                           # pop eax ; mov [base], eax
    code += bytes([0xD9, 0x2D]) + struct.pack("<I", base + 4)                                # fldcw [base + 4]
    call_at = base + 0x10 + len(code)
    code += bytes([0xE8]) + struct.pack("<i", target - (call_at + 5))                        # call target
    code += bytes([0xD9, 0x2D]) + struct.pack("<I", base + 6)                                # fldcw [base + 6]
    code += bytes([0xFF, 0x35]) + struct.pack("<I", base) + bytes([0xC3])                    # push dword [base] ; ret
    write_chunks(base, struct.pack("<IHH", 0, control_word, CW_PC53) + bytes(8) + code)
    return base + 0x10


def float_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


@d3d(0x004D2DA0, "D3DMODEL_Startup", rounding=False)
def gen_startup(env, n):
    img, rng = Image(), env.rng
    img.put(0x005808AC, noise(rng, 15000 * 4))
    return [], img.pokes()


@d3d(0x004D3F50, "D3D_SetupFog")
def gen_setup_fog(env, n):
    img, rng = Image(), env.rng
    colour = rng.choice([0, 1, 0x02000000 | rng.getrandbits(24), 0x01FFFFFF, 0xFF000000, rng.getrandbits(32), (rng.getrandbits(8) << 24) | 0x00808080, 0x02FF8040])
    img.u32(0x00549E94, rng.getrandbits(32) if rng.random() < 0.97 else colour)
    for table in FOG_TABLES:
        for row in FOG_ROWS:
            img.put(table + 1024 * row, noise(rng, 1024))
    return [colour], img.pokes()


@d3d_this(0x004D7990, "D3D_BuildSpriteQuad", SPRITE)
def gen_sprite_quad(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 3, 4])
    sprite = env.slab + SPRITE
    img.put(sprite, noise(rng, 0x20))
    img.u32(sprite + 8, env.slab + RECTS)
    img.u32(sprite + 0xC, count)
    img.u32(sprite + 0x10, env.slab + INFOS)
    img.u32(sprite + 0x14, rng.choice([0x3F666666, 0x3F800000, rng.getrandbits(32) & 0x3FFFFFFF | 0x3F000000]))
    img.put(env.slab + RECTS, b"".join(struct.pack("<4i", *[rng.randrange(-100, 700) for _ in range(4)]) for _ in range(4)))
    for k in range(4):
        info = env.slab + INFOS + 0x2C * k
        img.put(info, noise(rng, 0x2C))
        texture = env.slab + TEXTURES + 0x20 * k
        img.put(texture, noise(rng, 0x20))
        img.u32(info + 0x14, texture)
    img.put(DB_FIRST, noise(rng, DB_BYTES))
    img.u32(BUFFER_A + 0x84, env.slab + TABLE)
    img.u32(BUFFER_B + 0x84, env.slab + TABLE + 0x40)
    img.u32(0x00560FD4, rng.choice([BUFFER_A, BUFFER_B]))
    table = env.slab + TABLE
    img.u32(0x00560FD0, env.slab + PRIMS + 4 * rng.randrange(0, 8))
    img.put(env.slab + PRIMS, noise(rng, PRIM_BYTES))
    img.put(env.slab + TABLE, noise(rng, 0x60))
    img.put(env.slab + FAR_WORD, noise(rng, 8))
    img.u32(0x00563A38, env.slab + EDGES)
    img.put(env.slab + EDGES, noise(rng, 0x20))
    img.u32(0x029DA3A0, rng.choice([0, 0, 0, 1]))
    img.u32(0x006A04C8, rng.choice([sprite, 0, rng.getrandbits(32)]))
    img.u32(0x029DA394, rng.choice([0x200, 0x2AA, 0x280, 0x400, rng.randrange(0x100, 0x500)]))
    img.u32(0x029DA398, rng.choice([0xF0, 0x1E0, 0x168, rng.randrange(0x80, 0x400)]))
    return [table, float_bits(rng.choice([0.0, 1.5, -20.25, 100.0, rng.uniform(-300.0, 700.0)])), float_bits(rng.choice([0.0, 2.5, -9.75, 50.0, rng.uniform(-200.0, 500.0)]))], img.pokes()


@d3d(0x004D11D0, "M3D_SubmitGeometryPackets")
def gen_geometry_packets(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 3, 5, 8]) if rng.random() < 0.95 else rng.choice([-1 & 0xFFFFFFFF, 0])
    img.put(0x006A3E00, noise(rng, 0x120))
    for k in range(3):
        img.u32(0x006A3E48 + 4 * k, rng.choice([0, 0x40, 0x80, 0x100, 0x1000, rng.randrange(-0x1000, 0x2000) & 0xFFFFFFFF]))
    # matrices of Q12 shorts: the identity-ish, random, large
    for base in (0x006A3E80, 0x006A3EC8):
        img.put(base, struct.pack("<9h", *[rng.choice([0x1000, 0x800, 0, -0x400, rng.randrange(-0x1800, 0x1800)]) for _ in range(9)]))
    light = rng.choice([0, env.slab + LIGHT, env.slab + LIGHT])
    img.put(env.slab + LIGHT, struct.pack("<9h", *[rng.choice([0x1000, 0x800, 0, -0x400, rng.randrange(-0x1800, 0x1800)]) for _ in range(9)]))
    img.u32(0x0058F324, env.slab + LIST)
    img.put(env.slab + LIST, noise(rng, 0x40))
    img.put(0x0057E888, noise(rng, 0x40))
    img.put(0x0056E874, noise(rng, 0x100))
    packets = b""
    for k in range(8):
        normal = struct.pack("<3h", *[rng.choice([0, 0x1000, -0x1000, 0x800, rng.randrange(-0x1000, 0x1001)]) for _ in range(3)])
        flags = rng.choice([0, 1, 2, 3, 0x10, 0x11])
        if flags & 2:
            normal = struct.pack("<I", rng.randrange(0, 32) << 3)[:4] + struct.pack("<h", 0)[:2]
            normal = normal[:6]
        packets += normal + struct.pack("<h", flags)
    img.put(env.slab + PACKETS, packets)
    return [env.slab + PACKETS, count, light, rng.choice([0, 1, 0x100])], img.pokes()
