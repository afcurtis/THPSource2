"""Phase 4.15f probe: the create-a-skater objects, part 6: the stitcher's weld and the skater object's lifecycle (groups cweld: MySkater__StitchParts, PartStitcher_Construct / Weld /
WeldPair and its small methods; cskt: MySkater__Free / ScalarDelete / Delete, ReleasePartDatabase, Collision_InitModelCacheSlot, AttachRegion, ResetTextureNames,
AssignModelPartOrder, ApplyBodyScale and CustomSkater_BuildObject).

The scene is cobj_world's. The weld goes through the GTE (plain memory, 0x006a3e00..), so the register file is a captured window; the animated object the weld asks the pose of is a
fake with the harness's vtable (its first slot is a retail function the harness stubs).
"""

from __future__ import annotations

import cobj_world as W
from cobj_world import u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

import cobj_specs
from cobj_specs import SCRATCH, noise, r, word

GROUPS = ("cweld", "cskt")

NAME_BUFFER = SCRATCH + 0x200
GTE = (0x006A3E00, 0x120)
CUSTOM_RECORDS = (0x0056A488, 4 * 0x80)
LITERALS = [(0x00525DB0, 0x60), (0x00525F08, 0xD8), (0x00526210, 0x10), (0x00518764, 0x18)]       # the part name tables and "NONE"
MAP_LOCAL = SCRATCH + 0x1800
STITCHER = W.FAKE + 0x2C0                      # a stitcher object (only its first word is used)


def regions(slab):
    return merged([(slab + W.DB, W.DB_BYTES), (slab + W.SKATERS, 0x1000), (slab + W.SRC_TABLE, 0x100), (slab + W.BUILD_TABLE, 0x100), (slab + W.TABLES, 0x100),
                   (slab + W.MATERIALS, 0x100), (slab + W.TEXTURES, 0x100), (slab + W.SCENE, 0x100), (slab + W.PART_MAP, 0x400), (slab + W.LOG, 0x200), (slab + W.LIST, 0x200),
                   (slab + W.POSE, 0x400), (slab + W.FAKE, 0x400), (slab + W.SRC_MODELS, W.MODEL_STRIDE * W.PARTS), (slab + W.BUILD_MODELS, W.MODEL_STRIDE * W.PARTS),
                   (slab + W.ARENA, 0x6000), GTE, CUSTOM_RECORDS, *W.region_windows()], gap=0)


def seed(env, rng):
    env.alloc_base = W.ARENA
    env.alloc_size = W.ARENA_BYTES
    env.region_seq = []
    env.animated = True
    env.free_slot = None
    return W.seed_scene(env, rng)


for _group in GROUPS:
    SLAB_BYTES[_group] = W.SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(LITERALS)
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_cobj7_installed", False):
        return
    fc._cobj7_installed = True
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    ensure(fc, 0x00466BB0, 1, "operator_delete")
    ensure(fc, 0x004B20F0, 1, "engine_004b20f0", result=lambda a: env.slab + W.TABLES)
    ensure(fc, 0x004B2030, 1, "engine_004b2030", result=lambda a: env.materials.get(a[0], 0))
    ensure(fc, 0x004B4E00, 1, "engine_004b4e00", result=lambda a: rng.choice([0, 1, 5, 20, 100, 0xFFFFFFFB, rng.randrange(0, 200)]))
    ensure(fc, 0x004B3230, 1, "engine_004b3230", text=True, stack_to=NAME_BUFFER, result=lambda a: env.region_seq.pop(0) if env.region_seq else W.World.source)
    ensure(fc, 0x004B4640, 0, "engine_004b4640", result=lambda a: W.World.other if env.free_slot is None else env.free_slot)
    # the weld: an animated object (storage and constructor), the region it selects, the pose; the object's virtual delete is the harness's first fake slot
    ensure(fc, 0x0047FD30, 1, "engine_0047fd30", result=lambda a: env.slab + W.ANIMATED if env.animated else 0)
    ensure(fc, 0x00480610, 0, "engine_00480610", convention="thiscall", this=True, result=lambda a: env.slab + W.ANIMATED)
    ensure(fc, 0x0047FE30, 1, "engine_0047fe30", convention="thiscall", this=True, text=True)
    ensure(fc, 0x00430920, 1, "engine_00430920", result=lambda a: env.slab + W.POSE)
    ensure(fc, W.ANIM_DELETE, 1, "engine_%08x" % W.ANIM_DELETE, convention="thiscall", this=True)
    ensure(fc, 0x00480CD0, 2, "engine_00480cd0", convention="thiscall", this=True, masks={1: 0xFF})
    ensure(fc, 0x0046F470, 1, "engine_0046f470")
    ensure(fc, 0x0048FD60, 3, "engine_0048fd60", convention="stdcall")
    ensure(fc, 0x004B32F0, 3, "engine_004b32f0")
    for address in (0x004B2390, 0x0048A420):
        ensure(fc, address, 0, "engine_%08x" % address)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def cw(address, name, this=None):
    return r(address, name, "cweld", this)


def ck(address, name, this=None):
    return r(address, name, "cskt", this)


def skater_of(env, k=0):
    return env.slab + W.SKATERS + W.SKATER_STRIDE * k


def source_record():
    return W.REGION_TABLE + W.World.source * W.REGION_STRIDE


def build_record():
    return W.REGION_TABLE + W.World.build * W.REGION_STRIDE


def map_bytes(rng, complete=True):
    """The part map of a stitch: a permutation of the parts (a hidden part maps to -1), the rest of the 0x78 pairs unused (-1)."""
    order = list(range(W.PARTS))
    rng.shuffle(order)
    entries = b""
    for i in range(W.PARTS):
        entries += u32(i) + u32(order[i] if complete or rng.random() < 0.8 else 0xFFFFFFFF)
    return entries + b"".join(u32(0xFFFFFFFF) + u32(0xFFFFFFFF) for _ in range(0x78 - W.PARTS))


def consistent_map(img, env, rng, base):
    """The map StitchParts' BuildPartMap would make for the models' hidden flags (set here): every visible part has a slot of its own, a hidden part -1."""
    hidden = [False for _ in range(W.PARTS)]          # (a stitch always has its 19 parts: the build region's 19 slots are all filled)
    for p in range(W.PARTS):
        img.u8(env.slab + W.SRC_MODELS + W.MODEL_STRIDE * p, 0x20 if hidden[p] else rng.choice([0, 1]))
    slots = list(range(W.PARTS))
    rng.shuffle(slots)
    entries = b"".join(u32(p) + u32(0xFFFFFFFF if hidden[p] else slots[p]) for p in range(W.PARTS))
    img.put(env.slab + base, entries + b"".join(u32(0xFFFFFFFF) + u32(0xFFFFFFFF) for _ in range(0x78 - W.PARTS)))


def gte_noise(img, rng):
    img.put(GTE[0], noise(rng, GTE[1]))


def hide_some(img, env, rng, base, chance=0.3):
    for p in range(W.PARTS):
        if rng.random() < chance:
            img.u8(env.slab + base + W.MODEL_STRIDE * p, rng.choice([0x20, 0x21, 0x00, 0x01]))


def stitcher_state(env, rng, img):
    img.put(env.slab + STITCHER, noise(rng, 8))
    img.u32(env.slab + W.LOG, rng.getrandbits(32))


# ---- the stitcher -------------------------------------------------------------------------------------------------------------------------------------------------------

@cw(0x00424900, "PartStitcher_Construct", lambda env: env.slab + STITCHER)
def gen_stitcher_construct(env, n):
    rng = env.rng
    img = Image()
    stitcher_state(env, rng, img)
    consistent_map(img, env, rng, W.PART_MAP)
    # the build region keeps some models of an earlier copy (freed first)
    holders = [rng.choice([0, env.slab + W.BUILD_MODELS + W.MODEL_STRIDE * q]) for q in range(W.PARTS)]
    img.put(env.slab + W.BUILD_TABLE + 4, b"".join(u32(h) for h in holders))
    return [source_record(), build_record(), env.slab + W.PART_MAP], img.pokes()


@cw(0x00424A60, "PartStitcher_ScalarDelete", lambda env: env.slab + STITCHER)
def gen_stitcher_scalar_delete(env, n):
    img = Image()
    stitcher_state(env, env.rng, img)
    return [cobj_specs.flags(env.rng)], img.pokes()


@cw(0x00424A80, "PartStitcher_Destroy", lambda env: env.slab + STITCHER)
def gen_stitcher_destroy(env, n):
    img = Image()
    stitcher_state(env, env.rng, img)
    return [], img.pokes()


@cw(0x00425100, "PartStitcher_Finish")
def gen_stitcher_finish(env, n):
    return [], {}


def weld_state(env, rng, img):
    """What a weld needs: the GTE registers, a part map, the animated object (rarely missing), and some hidden parts."""
    gte_noise(img, rng)
    consistent_map(img, env, rng, W.PART_MAP)
    env.animated = rng.random() < 0.9
    env.region_seq = [W.World.source]
    img.u32(env.slab + W.ANIMATED, env.slab + W.ANIM_VTABLE)


@cw(0x004252C0, "PartStitcher_Weld")
def gen_weld(env, n):
    rng = env.rng
    img = Image()
    weld_state(env, rng, img)
    hide_some(img, env, rng, W.BUILD_MODELS, chance=0.1)
    return [source_record(), source_record(), build_record(), env.slab + W.PART_MAP], img.pokes()


@cw(0x004254C0, "PartStitcher_WeldPair")
def gen_weld_pair(env, n):
    rng = env.rng
    img = Image()
    weld_state(env, rng, img)
    first = rng.choice([0, 2, 0, 2, rng.randrange(0, W.PARTS - 1)])
    second = first + 1 if rng.random() < 0.8 else rng.randrange(0, W.PARTS)
    obj = env.slab + W.ANIMATED if env.animated else 0
    return [obj, source_record(), build_record(), first, second, env.slab + W.PART_MAP], img.pokes()


def skater_visible_state(img, env, rng, base):
    """The skater's four body group choices and the part database's group and index of every part, so that some parts are visible."""
    for k in range(4):
        img.u32(base + 0x70 + 4 * k, 0)
    img.u32(base + 0x280, env.slab + W.DB)
    img.u32(base + 0x84, W.World.build)
    for i in range(W.PARTS):                          # (every part belongs to group 0, variant 0: all are visible while the skater's first word is 0)
        img.u32(env.slab + W.DB + 4 + 0x34 * i + 0x10, 0)
        img.u32(env.slab + W.DB + 4 + 0x34 * i + 0x18, 0)


@cw(0x00421690, "MySkater__StitchParts", skater_of)
def gen_stitch_parts(env, n):
    rng = env.rng
    img = Image()
    weld_state(env, rng, img)
    skater_visible_state(img, env, rng, skater_of(env))
    holders = [rng.choice([0, env.slab + W.BUILD_MODELS + W.MODEL_STRIDE * q]) for q in range(W.PARTS)]
    img.put(env.slab + W.BUILD_TABLE + 4, b"".join(u32(h) for h in holders))
    return [], img.pokes()


# ---- the skater object --------------------------------------------------------------------------------------------------------------------------------------------

def skater_alive(img, env, rng, base):
    """A skater that owns a build region (models, table, name), a part database, material checksums and a palette buffer."""
    img.put(base, noise(rng, 0x31C))
    img.u32(base, 0x00518770)
    skater_visible_state(img, env, rng, base)
    count = rng.choice([0, 1, 3, 6])
    img.u32(base + 0x27C, count)
    for k in range(count):
        img.u32(base + 0x1FC + 4 * k, rng.choice(W.World.checksums))
    img.u32(base + 0x88, rng.choice([0, env.slab + W.LIST]))
    img.put(base + 0x8C, noise(rng, 0x24))
    anim = [rng.randrange(0, 0x13) for _ in range(W.PARTS)]
    if rng.random() < 0.9:
        anim[rng.randrange(W.PARTS)] = 0x10
    img.put(base + 0x1C, b"".join(u32(a) for a in anim))
    img.u32(base + 0x6C, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(base + 0x18, rng.randrange(0, 4))
    img.u32(base + 0x284, rng.randrange(0, 4))
    img.u32(base + 8, rng.choice([0, 1]))
    img.u32(base + 0xC, rng.choice([0, 1, 2]))
    # the part database object is alive when the skater has not released it
    img.u32(env.slab + W.DB, 0x0051876C)
    img.u32(env.slab + W.DB + 0x1C78, W.World.source)
    for k in range(4):
        scale = bytes([rng.choice([0, 50, 0x5A, 100, 0x7D, 200])])
        img.put(CUSTOM_RECORDS[0] + 0x80 * k + 0x23, scale)


@ck(0x004200D0, "MySkater__Free", skater_of)
def gen_free(env, n):
    rng = env.rng
    img = Image()
    skater_alive(img, env, rng, skater_of(env))
    if rng.random() < 0.15:
        img.u32(skater_of(env) + 0x84, 0xFFFFFFFF)
    if rng.random() < 0.15:
        img.u32(skater_of(env) + 0x280, 0)
    return [], img.pokes()


@ck(0x004200B0, "MySkater__ScalarDelete", skater_of)
def gen_scalar_delete(env, n):
    rng = env.rng
    img = Image()
    skater_alive(img, env, rng, skater_of(env))
    return [cobj_specs.flags(rng)], img.pokes()


@ck(0x00422EC0, "MySkater__Delete")
def gen_delete(env, n):
    rng = env.rng
    img = Image()
    skater_alive(img, env, rng, skater_of(env))
    return [skater_of(env)], img.pokes()


@ck(0x004206C0, "MySkater__ReleasePartDatabase", skater_of)
def gen_release(env, n):
    rng = env.rng
    img = Image()
    skater_alive(img, env, rng, skater_of(env))
    img.u32(skater_of(env) + 0x27C, rng.choice([0, 0, 0, 0]))
    hide_some(img, env, rng, W.BUILD_MODELS, chance=0.1)
    return [], img.pokes()


@ck(0x00420FA0, "Collision_InitModelCacheSlot", skater_of)
def gen_init_cache(env, n):
    rng = env.rng
    img = Image()
    img.put(skater_of(env), noise(rng, 0x100))
    img.u32(skater_of(env) + 0x84, rng.choice([W.World.build, W.World.other]))
    return [], img.pokes()


@ck(0x00420C10, "MySkater__AttachRegion", skater_of)
def gen_attach_region(env, n):
    rng = env.rng
    img = Image()
    weld_state(env, rng, img)
    skater_alive(img, env, rng, skater_of(env))
    env.free_slot = W.World.other
    env.region_seq = [0xFFFFFFFF, W.World.source]
    img.u32(skater_of(env) + 0x280, env.slab + W.DB if rng.random() < 0.95 else 0)
    return [rng.randrange(0, 4)], img.pokes()


@ck(0x00421410, "MySkater__ResetTextureNames", skater_of)
def gen_reset_names(env, n):
    rng = env.rng
    img = Image()
    weld_state(env, rng, img)
    skater_alive(img, env, rng, skater_of(env))
    return [], img.pokes()


def fake_object(img, env, rng, base):
    img.put(base, noise(rng, 0x40))


@ck(0x00421460, "MySkater__AssignModelPartOrder", skater_of)
def gen_assign_order(env, n):
    rng = env.rng
    img = Image()
    skater_alive(img, env, rng, skater_of(env))
    return [rng.choice([env.slab + W.ANIMATED, env.slab + W.ANIMATED, 0])], img.pokes()


@ck(0x00422E30, "MySkater__ApplyBodyScale", skater_of)
def gen_apply_scale(env, n):
    rng = env.rng
    img = Image()
    skater_alive(img, env, rng, skater_of(env))
    obj = env.slab + W.FAKE + 0x200
    img.put(obj, noise(rng, 0x40))
    img.u8(obj + 0x1F, rng.randrange(0, 20))
    return [obj], img.pokes()


@ck(0x00423BE0, "CustomSkater_BuildObject")
def gen_build_object(env, n):
    rng = env.rng
    img = Image()
    skater_alive(img, env, rng, skater_of(env))
    obj = env.slab + W.FAKE + 0x200
    img.put(obj, noise(rng, 0x40))
    img.u8(obj + 0x1F, rng.randrange(0, 20))
    return [skater_of(env), obj], img.pokes()
