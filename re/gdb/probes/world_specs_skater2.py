"""Phase 4.15c probe: the skater's ground and light methods (groups wsb and wsg): the blood splat and the water splash (wsb: the zone query answers the body, the slope and the height), the ground
probe and the shade of the face under the skater (wsg: the zone query fills the hit record).

The fake skater is the same 0x3600 bytes as in world_specs_skater (all of it captured); the face, the object it is standing on, the model and the tables sit behind it.

Slab layout (offsets): skater 0x0000, light bytes 0x3700, face 0x3800, object 0x3880, model 0x3900 (vertices from +0x1c), colours 0x3a00, model table 0x3a80.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import SPRITE_B, word
from world_specs import install_stubs as install_base_stubs

GROUPS = ("wsb", "wsg")

SKATER = 0x0000
SKATER_BYTES = 0x3600
LIGHT = 0x3700
FACE = 0x3800
OBJECT = 0x3880
MODEL = 0x3900
COLOURS = 0x3A00
MODEL_TABLE = 0x3A80
ARENA = 0x3B00
SLAB = 0x3C00
SCR = 0x7FF00800 + 0xC00
INFO = 0x7FF00800 + 0x500
SKATER_VTABLE = 0x00518DD8

GLOBALS = [(0x00533FA0, 4), (0x0055F72C, 4), (0x005244E4, 4), (0x005244CC, 4), (0x00533F3C, 4), (0x00533F90, 8), (0x0056A960, 4), (0x0056A8D4, 4), (0x0056A898, 4), (0x005685F4, 4),
           (0x005614C4, 4), (0x0056D43C, 68 * 3 + 0x14)]
READ = [(0x0053DE8C, 0x100), (SKATER_VTABLE, 0x20)]


def regions(slab):
    return merged([(slab + SKATER, SKATER_BYTES), (slab + LIGHT, 0x10), (slab + FACE, 0x20), (slab + OBJECT, 0x40), (slab + MODEL, 0x100), (slab + COLOURS, 0x40), (slab + MODEL_TABLE, 0x20),
                   (slab + ARENA, 0x100)] + GLOBALS, gap=0)


STATE = {"hit": 0, "slope": 0, "hity": 0, "fill": bytes(0x50), "out": bytes(12)}


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = 0x100
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = READ
    SEEDS[_group] = seed


def wsb(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "wsb", this=resolve)


def wsg(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "wsg", this=resolve)


def install_stubs(fc, env):
    import os

    from opentony.memory import mem
    from platform_specs import mark_under_test

    if getattr(fc, "_wsb_installed", False):
        return
    fc._wsb_installed = True
    install_base_stubs(fc, env)
    mark_under_test()
    rng = env.rng
    groups = os.environ.get("P410_GROUPS", "").split(",")
    ensure(fc, 0x004AD620, 3, "engine_004ad620")                                                     # SFX_PlayX
    ensure(fc, 0x0049C7D0, 0, "engine_0049c7d0", convention="thiscall", this=True)                   # CBruce__FRUVectorsFromRotMatrix
    if "wsb" in groups:
        ensure(fc, 0x004CB430, 3, "engine_004cb430")                                                 # Replay_RecordSplat
        ensure(fc, 0x00406070, 1, "engine_00406070", stack_to={0: SCR + 0x20}, blob=(0, 6))
        ensure(fc, 0x00406090, 1, "engine_00406090", stack_to={0: SCR + 0x30}, blob=(0, 6))
        ensure(fc, 0x004061C0, 3, "engine_004061c0")
        ensure(fc, 0x004061E0, 3, "engine_004061e0")
        ensure(fc, 0x00406160, 1, "engine_00406160")
        ensure(fc, 0x00464E90, 3, "engine_00464e90", stack_to={0: SCR + 0x50, 2: SCR + 0x40}, blob=(2, 8), pokes=[(0, None, lambda a: STATE["out"], 0)])     # M3D_GetHookPosition
        ensure(fc, 0x0046F2C0, 1, "engine_0046f2c0", result=lambda a: rng.choice([0x100000, 0x100000, 0x2710, 0x270F, 5, 0xFFFFFFFF]))                       # Mem_GetFree
        ensure(fc, 0x00406200, 4, "engine_00406200", convention="thiscall", this=True, stack_to={0: SCR + 0x60}, blob=(0, 12), result=lambda a: env.slab + SPRITE_B)   # the new bit
        ensure(fc, 0x00448990, 7, "engine_00448990", convention="thiscall", this=True, stack_to={0: SCR + 0x60}, blob=(0, 12))                              # Exp_Construct
        ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=INFO)
        ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=INFO,
               pokes=[(0, 4, lambda a: STATE["hit"], 0x68), (0, 2, lambda a: STATE["slope"], 0x7A), (0, 4, lambda a: STATE["hity"], 0x70)])
    if "wsg" in groups:
        ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=INFO)
        ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=INFO, pokes=[(0, None, lambda a: STATE["fill"], 0x40)])
        slot5 = mem.u32(SKATER_VTABLE + 0x14)
        ensure(fc, slot5, 0, "engine_%08x" % slot5, convention="thiscall", this=True)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def skater_base(img, env, rng):
    img.put(env.slab + 0x4, noise(rng, 0x20))
    img.put(env.slab + 0x4C, noise(rng, 12))
    img.u32(env.slab + 0x2C54, rng.choice([0, 1, 1, 1, word(rng)]))
    img.u32(env.slab + 0x2C58, rng.choice([0, 1, 2, 3, 0xFFFFFFFF]))
    img.u32(env.slab + 0x2C5C, rng.choice([0, 1, 3, 10, 30, 100, 300]))


@wsb(0x0046DA70, "CBruce__BloodSplat", SKATER)
def gen_blood(env, n):
    img, rng = Image(), env.rng
    skater_base(img, env, rng)
    img.u32(0x00533FA0, rng.choice([0, 1, 1, 1]))
    img.u32(0x0055F72C, rng.choice([0, 100, 240, 249, 250, 251, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.put(0x005244E4, noise(rng, 4))
    img.u32(0x005244CC, word(rng))
    STATE["out"] = struct.pack("<3i", *[rng.randrange(-0x80000, 0x80000) for _ in range(3)])
    STATE["hit"] = rng.choice([0, 0, 1, 0x1234])
    STATE["slope"] = rng.getrandbits(16)
    STATE["hity"] = rng.randrange(-0x80000, 0x80000) & 0xFFFFFFFF
    return [], img.pokes()


@wsb(0x0046E080, "CBruce__HandleWater", SKATER)
def gen_water(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x4, noise(rng, 0x20))
    img.u32(0x00533F3C, rng.choice([1, 1, 2]))
    img.u32(0x0056A960, rng.choice([env.slab + SKATER, 0, env.slab + SKATER]))
    level = rng.randrange(-0x100000, 0x100000)
    img.u32(0x00533F90, level & 0xFFFFFFFF)
    img.u32(0x00533F94, (level + rng.choice([0, 0x1000, -0x1000])) & 0xFFFFFFFF)
    img.u32(env.slab + 0xC, (level + rng.choice([-0x40000, -0x19000, -0x18FFF, -0x19001, 0, 0x10000, rng.randrange(-0x40000, 0x40000)])) & 0xFFFFFFFF)
    img.u32(0x0056A8D4, rng.choice([0, 4, 8, 3, 1, 0xFF, 0x100, rng.getrandbits(8)]))
    return [], img.pokes()


@wsg(0x0046D7A0, "Skater_ProbeGround", SKATER)
def gen_probe(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x8, noise(rng, 12))
    img.u32(env.slab + 0x30B8, rng.choice([0, 1, 1, 2, 3, 4, 8, 9, rng.getrandbits(32)]))
    img.put(env.slab + 0x9C, noise(rng, 6))
    img.u32(env.slab + 0x2EDC, rng.choice([0, 100, 0xFFFFFFF0, rng.getrandbits(32)]))
    img.u32(env.slab + 0xE4, env.slab + LIGHT)
    img.put(env.slab + LIGHT, noise(rng, 4))
    img.put(env.slab + 0xDC, noise(rng, 1))
    img.u32(0x00533F3C, rng.choice([1, 1, 2]))
    img.u32(0x005685F4, rng.choice([0, 100, 106, 107, 0xFFFFFFFF, rng.getrandbits(32)]))
    if rng.random() < 0.4:                                                                     # (the frame just after the stamp: the state-1 hold-off)
        stamp = rng.choice([0, 100, 0xFFFFFFF0, 12345])
        img.u32(env.slab + 0x2EDC, stamp)
        img.u32(0x005685F4, (stamp + rng.randrange(0, 9)) & 0xFFFFFFFF)
        img.u32(env.slab + 0x30B8, 1)
    fill = bytearray(noise(rng, 0x50))
    struct.pack_into("<I", fill, 0x28, rng.randrange(0, 0x2000))                               # Q+0x68: the hit body
    if rng.random() < 0.3:
        struct.pack_into("<I", fill, 0x28, 0)
    struct.pack_into("<h", fill, 0x3A, rng.choice([-0x1000, -0xC00, -0x5DC, -0x5DB, -0x3E8, -0x3E7, -0x1F4, -0x1F3, 0, 100, rng.randrange(-0x1000, 0x400)]))     # Q+0x7a: the slope
    struct.pack_into("<I", fill, 0, rng.randrange(0, 0x2000))                                  # Q+0x40
    STATE["fill"] = bytes(fill)
    if rng.random() < 0.4:                                                                     # (the existing slope near the new one)
        img.u16(env.slab + 0x9E, struct.unpack_from("<H", fill, 0x3A)[0] + rng.choice([-1500, -1000, 0, 500, 1001]) & 0xFFFF)
    return [], img.pokes()


def put_shade_world(img, env, rng, count_flags):
    region = rng.choice([0, 1, 2])
    index = rng.randrange(3)
    obj = env.slab + OBJECT
    img.put(obj, noise(rng, 0x40))
    img.u8(obj + 0x1F, region)
    img.u16(obj + 0x1A, index)
    img.u32(obj + 8, rng.randrange(-0x20000, 0x20000) & 0xFFFFFFFF)
    img.u32(obj + 0x10, rng.randrange(-0x20000, 0x20000) & 0xFFFFFFFF)
    img.u32(0x0056D43C + 68 * region, env.slab + MODEL_TABLE)
    img.u32(0x0056D450 + 68 * region, env.slab + COLOURS)
    for k in range(3):
        img.u32(env.slab + MODEL_TABLE + 4 * k, env.slab + MODEL)
    img.put(env.slab + COLOURS, noise(rng, 0x40))
    # vertices: 8 bytes each from +0x1c; x and z are shorts (at +0 and +4), small so the triangles are around the origin
    vertices = bytearray(0x40)
    for k in range(8):
        struct.pack_into("<hhh", vertices, 8 * k, rng.randrange(-300, 300), 0, rng.randrange(-300, 300))
    img.put(env.slab + MODEL + 0x1C, bytes(vertices))
    face = env.slab + FACE
    img.put(face, noise(rng, 0x20))
    img.u16(face, count_flags)
    for k in range(4):
        img.u8(face + 4 + k, rng.randrange(8))
        img.u8(face + 8 + k, rng.randrange(16))
    return obj, face


@wsg(0x0046D2E0, "Skater_Shade", SKATER)
def gen_shade(env, n):
    img, rng = Image(), env.rng
    img.u32(env.slab + SKATER, SKATER_VTABLE)
    img.put(env.slab + 0x30B8, noise(rng, 4))
    img.u32(env.slab + 0x30B8, rng.choice([0, 1, 2, 2, 3]))
    img.put(env.slab + 0x317C, noise(rng, 0x18))
    img.u32(env.slab + 0x317C, rng.choice([0, 0x100, 0x200, 0x300, rng.randrange(0, 0x400)]))
    img.u32(0x005614C4, rng.choice([0, 1]))
    img.u32(0x0056A898, rng.choice([0, 1, 2, 3]))
    flags = rng.choice([0x800, 0x800, 0x810, 0x800, 0x000, 0x010, 0x1800, 0x810])
    obj, face = put_shade_world(img, env, rng, flags)
    if flags & 0x800 == 0:
        img.u32(face + 8, rng.getrandbits(24))          # (the flat colour; a face with corner colours keeps its small colour indices)
    return [face, obj, rng.randrange(-100, 100) & 0xFFFFFFFF, rng.randrange(-100, 100) & 0xFFFFFFFF], img.pokes()
