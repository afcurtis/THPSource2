"""Phase 4.15f probe: the create-a-skater objects, part 3: the checksums of the materials a part uses (group cmat: Part_CollectChecksums, PartDatabase_BuildChecksums,
CreatePart_SelectMaterialChecksum / FindMaterialChecksum / SelectSlotChecksum and MySkater__FindBoardChecksum).

The scene is cobj_world's: a part database of 19 parts, a source region with 19 models whose faces use eight materials, the same for a build region, and fake skaters.
"""

from __future__ import annotations

import cobj_world as W
from cobj_world import u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

import cobj_specs
from cobj_specs import noise, r, word

GROUPS = ("cmat",)

LITERALS = [(0x00525F40, 0xA0), (0x005264D0, 0x10)]       # the part names the code compares with


def regions(slab):
    return merged([(slab + W.DB, W.DB_BYTES), (slab + W.SKATERS, 0x800), (slab + W.SRC_TABLE, 0x100), (slab + W.BUILD_TABLE, 0x100), (slab + W.TABLES, 0x100),
                   (slab + W.MATERIALS, 0x100), (slab + W.TEXTURES, 0x100), (slab + W.SCENE, 0x100), (slab + W.LIST, 0x200),
                   (slab + W.SRC_MODELS, W.MODEL_STRIDE * W.PARTS), (slab + W.BUILD_MODELS, W.MODEL_STRIDE * W.PARTS), *W.region_windows()], gap=0)


def seed(env, rng):
    env.alloc_base = W.ARENA
    env.alloc_size = W.ARENA_BYTES
    writes = W.seed_scene(env, rng)
    # the fake skaters: the part database, the build region, the slots' animation indexes
    for k in range(2):
        skater = bytearray(0x31C)
        skater[0x280:0x284] = u32(env.slab + W.DB)
        skater[0x84:0x88] = u32(W.World.build)
        for i in range(W.PARTS):
            skater[0x1C + 4 * i:0x20 + 4 * i] = u32(i)
        writes[env.slab + W.SKATERS + W.SKATER_STRIDE * k] = bytes(skater)
    return writes


for _group in GROUPS:
    SLAB_BYTES[_group] = W.SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(LITERALS)
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_cobj3_installed", False):
        return
    fc._cobj3_installed = True
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x004B20F0, 1, "engine_004b20f0", result=lambda a: env.slab + W.TABLES)                 # PSX_FindTrailingTables(raw region)
    ensure(fc, 0x004B2030, 1, "engine_004b2030", result=lambda a: env.materials.get(a[0], 0))           # PSX_FindSceneMaterial(checksum)
    ensure(fc, 0x004B4E00, 1, "engine_004b4e00", result=lambda a: rng.choice([0, 1, 5, 20, 100, 0xFFFFFFFB, rng.randrange(0, 200)]))   # PSX_ScoreMaterialPaletteUsage


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def cm(address, name, this=None):
    return r(address, name, "cmat", this)


def source_record():
    return W.REGION_TABLE + W.World.source * W.REGION_STRIDE


def skater_of(env):
    return env.slab + W.SKATERS


def hide_some(img, env, rng, base, chance=0.3):
    """Models of the region hidden (bit 0x20 of their first byte) or shown."""
    for p in range(W.PARTS):
        if rng.random() < chance:
            img.u8(env.slab + base + W.MODEL_STRIDE * p, rng.choice([0x20, 0x21, 0x00, 0x01]))


@cm(0x0041F900, "Part_CollectChecksums", W.DB)
def gen_collect(env, n):
    rng = env.rng
    img = Image()
    p = rng.randrange(W.PARTS)
    count = rng.choice([0, 0, 0, 1, 2, 4, 0x1F, 0x20])
    entries = [rng.choice(W.World.checksums + [rng.getrandbits(32)]) for _ in range(0x20)]
    img.put(env.slab + W.LIST, b"".join(u32(e) for e in entries))
    img.u32(env.slab + W.COUNT, count)
    # some faces no material fits (the palette of a flagged face differs)
    return [source_record(), env.slab + W.SRC_MODELS + W.MODEL_STRIDE * p, env.slab + W.LIST, env.slab + W.COUNT], img.pokes()


@cm(0x0041FC80, "PartDatabase_BuildChecksums", W.DB)
def gen_build(env, n):
    rng = env.rng
    img = Image()
    img.u32(env.slab + W.DB + 0x1864, rng.choice([19, 19, 19, 19, 18, 5]))
    for i in range(W.PARTS):
        img.put(env.slab + W.DB + 4 + 0x34 * i + 0x1C, noise(rng, 0x18))
    return [source_record()], img.pokes()


def put_name(img, env, offset, name):
    img.put(env.slab + W.STRINGS + offset, name.encode() + b"\0" + bytes(4))
    return env.slab + W.STRINGS + offset


def pick_name(rng):
    return rng.choice(W.PART_NAMES + W.PART_NAMES + ["nothing", ""])


def vary_scene(img, env, rng):
    hide_some(img, env, rng, W.SRC_MODELS)
    for k in range(W.MATERIAL_COUNT):
        if rng.random() < 0.25:
            area = rng.choice([(0, 0, 9, 9), (4, 4, 13, 13), (0, 0, 20, 5), (250, 250, 5, 5)])
            img.put(env.slab + W.SCENE + 0x20 * k, bytes([area[0], area[1]]))
            img.put(env.slab + W.SCENE + 0x20 * k + 10, bytes([area[2], area[3]]))


@cm(0x00421910, "CreatePart_SelectMaterialChecksum", skater_of)
def gen_select_material(env, n):
    rng = env.rng
    img = Image()
    vary_scene(img, env, rng)
    name = put_name(img, env, 0, pick_name(rng))
    return [name, rng.choice([0, 1, 1, 0x100, 0xFF])], img.pokes()


@cm(0x00421BA0, "CreatePart_FindMaterialChecksum", skater_of)
def gen_find_material(env, n):
    rng = env.rng
    img = Image()
    vary_scene(img, env, rng)
    wanted = put_name(img, env, 0, pick_name(rng))
    other = put_name(img, env, 0x40, pick_name(rng))
    third = put_name(img, env, 0x80, pick_name(rng)) if rng.random() < 0.4 else 0
    return [wanted, other, third], img.pokes()


@cm(0x00422020, "CreatePart_SelectSlotChecksum", skater_of)
def gen_select_slot(env, n):
    rng = env.rng
    img = Image()
    vary_scene(img, env, rng)
    return [rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xFFFFFFFF, 100, 3, 3]), rng.choice([0, 1, 0xFF, 0x100, 0xFFFFFF00])], img.pokes()


@cm(0x00420420, "MySkater__FindBoardChecksum", skater_of)
def gen_find_board(env, n):
    rng = env.rng
    img = Image()
    hide_some(img, env, rng, W.BUILD_MODELS, chance=0.2)
    # the slots' animation indexes: the board is the one that has 0x10
    slots = [rng.randrange(0, 0x13) for _ in range(W.PARTS)]
    if rng.random() < 0.9:
        slots[rng.randrange(W.PARTS)] = 0x10
    img.put(skater_of(env) + 0x1C, b"".join(u32(s) for s in slots))
    img.u32(skater_of(env) + 0x6C, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.getrandbits(32)]))
    count = rng.choice([0, 0, 1, 3])
    img.put(env.slab + W.LIST, b"".join(u32(rng.choice(W.World.checksums)) for _ in range(0x20)))
    img.u32(env.slab + W.COUNT, count)
    return [env.slab + W.LIST, env.slab + W.COUNT], img.pokes()
