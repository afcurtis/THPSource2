"""Phase 4.15e probe: the multi-model item submitter (group rssi: RenderSuperItemShadow 0x004604f0).

The slab is the one of render_specs5: view record 0x0100, object 0x0200 (0x1a0), matrix 0x0400, model table 0x0500, models 0x0600 (0x20 each), pose entries 0x1000 (up to 0x1800).
"""

from __future__ import annotations

import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure
from frontend_specs_menu import Image
from render_specs5 import REGION_RECORDS, REGION_STRIDE, STATIC, regions, seed, install_stubs as install_stubs5
from render_specs import SCRATCH, noise, word

GROUPS = ("rssi", "rmdl", "robj")

for _group in GROUPS:
    SLAB_BYTES[_group] = 0xD000
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(STATIC)
    SEEDS[_group] = seed


def install_stubs(fc, env):
    install_stubs5(fc, env)
    if getattr(fc, "_render7_installed", False):
        return
    fc._render7_installed = True
    ensure(fc, 0x00430920, 1, "engine_00430920", result=lambda a: env.slab + 0x1000)
    ensure(fc, 0x00461B10, 2, "engine_00461b10")
    ensure(fc, 0x00461B50, 2, "engine_00461b50")
    if "rmdl" in os.environ.get("P410_GROUPS", "").split(","):
        # (the lit packets are submitted with the matrix built in a local: its nine shorts are part of the call; M3D_RenderObjects submits with a null matrix)
        ensure(fc, 0x004D11D0, 4, "engine_004d11d0", blob=(2, 18), stack_to={2: SCRATCH + 0x154}, masks={3: 0xFF})
    else:
        ensure(fc, 0x004D11D0, 4, "engine_004d11d0")
    ensure(fc, 0x004610F0, 1, "engine_004610f0")
    ensure(fc, 0x00461CF0, 1, "engine_00461cf0")
    ensure(fc, 0x00461A50, 1, "engine_00461a50")
    ensure(fc, 0x00461A90, 2, "engine_00461a90", stack_to={1: SCRATCH + 0x154})
    ensure(fc, 0x00461B70, 2, "engine_00461b70", stack_to={1: SCRATCH + 0x154})


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def item_scene(env, rng, home):
    """An item of up to eight models in the slab (the object itself at `home`): returns the picture and the object."""
    img = Image()
    img.put(0x005620A0, noise(rng, 0x60))
    img.put(0x00563A00, noise(rng, 0x140))
    img.put(0x00563B28, noise(rng, 0x40))
    img.put(0x00564360, noise(rng, 0x28))
    img.put(0x006A3E00, noise(rng, 0x120))
    view, obj, matrix, table, models, pose = (env.slab + 0x100, env.slab + home, env.slab + 0x400, env.slab + 0x500, env.slab + 0x600, env.slab + 0x1000)
    img.put(view, noise(rng, 0x90))
    img.put(obj, noise(rng, 0x1A0))
    if home != 0x200:
        img.put(obj + 0x2CD0, noise(rng, 0x10))
    img.u32(0x005620E0, view)
    region = rng.randrange(0, 3)
    record = REGION_RECORDS + REGION_STRIDE * region
    bones = rng.choice([1, 1, 2, 3, 5, 8])
    img.put(record, noise(rng, REGION_STRIDE))
    img.u8(record + 0x3A, rng.choice([0, 1, 1]))
    img.u8(record + 0x0A, 1 if rng.random() < 0.95 else 0)
    img.u16(record + 0x38, bones)
    img.u32(record + 0x14, table)
    img.u32(record + 0x28, word(rng))
    for k in range(bones):
        img.u32(table + 4 * k, models + 0x20 * k)
        img.put(models + 0x20 * k, noise(rng, 0x20))
        if rng.random() < 0.3:
            img.u8(models + 0x20 * k, 0x20 | rng.randrange(0, 0x20))
        else:
            img.u8(models + 0x20 * k, rng.randrange(0, 0x20))
    img.put(matrix, noise(rng, 0x40))
    img.put(pose, noise(rng, 0x1800))
    flags = rng.choice([0, 4, 4, 0x80, 0xC80, rng.getrandbits(16)])
    img.u16(obj + 4, flags)
    img.u8(obj + 0x1F, region)
    img.u16(obj + 0x1A, 0 if rng.random() < 0.95 else rng.randrange(1, 4))
    img.u32(obj + 0x40, matrix)
    img.u32(obj + 0x138, pose)
    img.u8(obj + 0xD8, rng.choice([0, 2, 2, rng.getrandbits(8)]))
    img.u32(obj + 0x88, rng.choice([0, 0, 0x80000000, word(rng)]))
    img.u32(obj + 0x148, rng.choice([0, word(rng), 1, 2]))
    img.u32(obj + 0x14C, rng.choice([0, 0, 1]))
    for k in range(bones):
        img.u8(obj + 0x150 + k, rng.randrange(0, 200))
    for axis in range(3):
        img.u32(obj + 8 + 4 * axis, rng.choice([word(rng), (rng.randrange(-300, 300) << 12) & 0xFFFFFFFF]))
        img.u32(view + 4 + 4 * axis, rng.randrange(-300, 300) & 0xFFFFFFFF)
    return img, obj


@spec(0x004604F0, "RenderSuperItemShadow", "rssi", this=None)
def gen_super_item(env, n):
    img, obj = item_scene(env, env.rng, 0x200)
    return [obj], img.pokes()


@spec(0x004610F0, "M3D_RenderModel", "rmdl", this=None)
def gen_render_model(env, n):
    rng = env.rng
    img, obj = item_scene(env, rng, 0x2900)
    img.u32(0x0056A898, rng.choice([12, 12, 0, 9, 10, 13, 14, rng.randrange(0, 16)]))
    img.put(0x0053DB80, noise(rng, 0x34 * 16))
    img.u32(obj + 0x2CD4, rng.randrange(0, 8))
    img.u32(obj + 0x2CD8, rng.randrange(0, 8))
    img.u8(obj + 0xDC, rng.choice([0, 0, 1]))
    return [obj], img.pokes()


@spec(0x0045F530, "M3D_RenderObjects", "robj", this=None)
def gen_render_objects(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    plain = rng.random() < 0.4          # a scene the "variant" path draws: close, small models, unturned objects with depth offsets
    img.put(0x005620A0, noise(rng, 0x60))
    img.put(0x00563A00, noise(rng, 0x140))
    img.put(0x00563B28, noise(rng, 0x40))
    img.put(0x00564340, noise(rng, 0x48))
    img.put(0x006A3E00, noise(rng, 0x120))
    img.put(0x005620E0, noise(rng, 0x24))
    # the view record and the viewport edges (the planes of the view are the same words as the frame's light and colour matrices)
    view, edges = slab + 0x100, slab + 0x1A0
    img.put(view, noise(rng, 0x90))
    for axis in range(3):
        img.u32(view + 4 + 4 * axis, 0)
    if plain or rng.random() < 0.6:
        img.put(view + 0x74, struct.pack("<9h", 0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000))
    img.u32(0x005620E0, view)
    img.put(edges, noise(rng, 0x10))
    img.u16(edges + 0xA, 20 if plain else rng.choice([20, 50, 100, 320, 640, rng.randrange(1, 2000)]))
    img.u16(edges + 0xE, rng.choice([100, 320, 640, rng.randrange(1, 2000)]))
    img.u32(0x00563A38, edges)
    img.u32(0x00563A3C, rng.choice([0x100, 0x400, 0x1000, rng.randrange(0, 0x2000)]))
    if plain or rng.random() < 0.85:
        img.put(0x005620C0, bytes(0x12))
        img.put(0x005620E8, bytes(0x12))
    # the models: a table per region, a chain of up to four models whose +0x1a names the rougher one
    table, models = slab + 0x800, slab + 0x840
    count = rng.choice([1, 2, 3, 4])
    for k in range(count):
        at = models + 0x40 * k
        img.u32(table + 4 * k, at)
        img.put(at, noise(rng, 0x40))
        img.u16(at, rng.choice([0, 0, 4, 4, 0x20, 0x200, 0x400, 0x100, 0x604, rng.getrandbits(16)]))
        img.u16(at + 2, rng.choice([10, 50, 100, 101, 120]))
        img.u16(at + 4, rng.randrange(0, 8))
        img.u32(at + 8, (rng.randrange(0, 0x40) << 12) | rng.choice([0, 0x800]))
        img.u16(at + 0x18, rng.choice([-200, -1, 0, 5, 100, 1000, 30000, rng.randrange(-0x8000, 0x8000)]) & 0xFFFF)
        img.u16(at + 0x1A, k + 1 if k + 1 < count and rng.random() < 0.8 else 0xFFFF)
        if plain:
            img.u16(at, rng.choice([0, 4, 0x200, 0x400]))
            img.u16(at + 2, 10)
            img.u16(at + 0x18, 30000)
            img.u16(at + 0x1A, 0xFFFF)
    for region in range(3):
        record = REGION_RECORDS + REGION_STRIDE * region
        img.put(record, noise(rng, REGION_STRIDE))
        img.u8(record + 0xA, 1 if rng.random() < 0.9 else 0)
        img.u32(record + 0x14, table)
        img.u32(record + 0x28, word(rng))
    # the objects
    objects = [slab + 0x200 + 0x160 * k for k in range(rng.choice([1, 2, 3]))]
    matrix = slab + 0x700
    img.put(matrix, noise(rng, 0x40))
    for k, at in enumerate(objects):
        img.put(at, noise(rng, 0x160))
        img.u32(at + 0x20, objects[k + 1] if k + 1 < len(objects) else 0)
        flags = rng.choice([0, 0, 0x08, 0x80, 0x400, 0x480, 0x02, 0x02, 0x0A, 0x2000, 0x408, rng.getrandbits(16) & 0x2FBC])
        img.u16(at + 4, flags)
        img.u8(at + 5, (img_byte5(rng)))
        for angle in (0x14, 0x16, 0x18):
            img.u16(at + angle, 0 if rng.random() < 0.6 else rng.getrandbits(16))
        img.u16(at + 0x1A, rng.randrange(0, count))
        for offset in (0x1C, 0x1D, 0x1E):
            img.u8(at + offset, 0 if rng.random() < 0.6 else rng.getrandbits(8))
        img.u8(at + 0x1F, rng.randrange(0, 3))
        img.u32(at + 0x40, matrix)
        for axis in range(3):
            img.u32(at + 8 + 4 * axis, ((rng.randrange(-300, 300)) << 12) & 0xFFFFFFFF if rng.random() < 0.85 else word(rng))
        for axis in range(3):
            img.u16(at + 0x28 + 2 * axis, rng.choice([0x1000, 0x800, 0x2000, rng.getrandbits(16)]))
        img.u32(at + 0x14C, rng.choice([0, 0, 1]))
        if plain:
            for angle in (0x14, 0x16, 0x18):
                img.u16(at + angle, 0)
            img.u16(at + 4, rng.choice([0, 0x400, 0x10, 0x200, 0x2000]))
            img.u8(at + 5, rng.choice([0, 0, 0x20, 0x08]))
            img.u16(at + 0x1A, 0)
            img.u8(at + 0x1C, rng.choice([0, 3, 0xFD]))
            img.u8(at + 0x1D, rng.choice([0, 0, 5]))
            img.u8(at + 0x1E, rng.choice([0, 0xFB, 7]))
            for axis in range(3):
                img.u32(at + 8 + 4 * axis, 0 if axis != 2 else ((rng.randrange(60, 300)) << 12))
    img.u32(0x0055F6BC, rng.choice([0, objects[0]]))
    img.u32(0x0056DB28, rng.choice([0, objects[0], objects[0]]))
    img.u32(0x0056A960, rng.choice([0, objects[0], objects[-1]]))
    img.u32(0x0055FA3C, rng.choice([0, 1]))
    img.u32(0x00562108, rng.choice([0, 1, 1, rng.getrandbits(32)]))
    img.u32(0x0056A898, rng.choice([0, 12, 3]))
    img.u32(0x00533F90, word(rng))
    img.u32(0x00533F94, word(rng))
    img.u32(0x00533908, word(rng))
    img.u32(0x00537C80, rng.randrange(0, 20))
    img.u32(0x00537C84, rng.randrange(0, 20))
    return [objects[0] if rng.random() < 0.97 else 0], img.pokes()


def img_byte5(rng):
    """The second flag byte of an object: 0x80 (not drawn) is rare; 2 scales, 8 recolours, 0x20 fades."""
    bits = 0
    for bit, chance in ((0x02, 0.25), (0x08, 0.2), (0x20, 0.2), (0x80, 0.05)):
        if rng.random() < chance:
            bits |= bit
    return bits
