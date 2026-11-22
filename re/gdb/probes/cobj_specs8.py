"""Phase 4.15f probe: the create-a-skater objects, part 7: the custom definition holder and the custom skaters (group cdef: CustomDefinition_*, CustomSkater_LoadDefinitionPsx,
CustomSkaters_LoadAll / InitAll / FreeAll and CustomSkater_Create, the whole chain from a custom record to a built skater).

The services are the union of the groups below it (cobj_specs4's manifest, cobj_specs6's texture sets, cobj_specs7's weld and region stubs), with the definition file's own.
"""

from __future__ import annotations

import cobj_world as W
from cobj_world import u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

import cobj_specs
import cobj_specs4
import cobj_specs6
import cobj_specs7
from cobj_specs import PLAYER_SPOOL_HOLDER, SCRATCH, SPOOLER, SPOOLER_BYTES, noise, r, word

GROUPS = ("cdef",)

CUSTOM_RECORDS = (0x0056A488, 4 * 0x80)
MANAGER = (0x0056A87C, 4)
CACHED = (0x00561CB4, 4)
GLOBALS = (0x00568648, 8)
SELECTION = (0x0053A514, 0x1CC0)
GTE = (0x006A3E00, 0x120)
HOLDER = W.FAKE + 0x300
LITERALS = [(0x00525DB0, 0x60), (0x00525F08, 0xD8), (0x00526210, 0x10), (0x00518764, 0x18), (0x00525F40, 0xA0), (0x005264D0, 0x10), (0x00526328, 0x20), (0x00525FF8, 0x20),
            (0x00519828, 4), (0x00527D10, 0x20)]       # (… and "sk2def.psx")


def regions(slab):
    return merged([(slab + W.DB, W.DB_BYTES), (slab + W.SKATERS, 0x1000), (slab + W.SRC_TABLE, 0x100), (slab + W.BUILD_TABLE, 0x100), (slab + W.TABLES, 0x100),
                   (slab + W.MATERIALS, 0x100), (slab + W.TEXTURES, 0x100), (slab + W.SCENE, 0x100), (slab + W.PART_MAP, 0x400), (slab + W.LOG, 0x200), (slab + W.LIST, 0x300),
                   (slab + W.POSE, 0x400), (slab + W.SETS, 0x400), (slab + W.SET_ENTRIES, 0x400), (slab + W.FAKE, 0x400), (slab + W.SRC_MODELS, W.MODEL_STRIDE * W.PARTS),
                   (slab + W.BUILD_MODELS, W.MODEL_STRIDE * W.PARTS), (slab + W.MANIFEST, 0x1000), (slab + W.ARENA, 0x10000), (SPOOLER, SPOOLER_BYTES), (PLAYER_SPOOL_HOLDER, 4),
                   CUSTOM_RECORDS, MANAGER, CACHED, GLOBALS, SELECTION, GTE, *W.region_windows()], gap=0)


def seed(env, rng):
    env.alloc_base = W.ARENA
    env.alloc_size = W.ARENA_BYTES
    env.manifest = {}
    env.region_seq = []
    env.animated = True
    env.free_slot = None
    writes = W.seed_scene(env, rng)
    writes[env.slab + W.FAKE + 0x300 + 0x40] = b"front\0"                       # (the sub-directory string; the player spool object follows)
    writes[PLAYER_SPOOL_HOLDER] = u32(env.slab + W.FAKE + 0x200)
    return writes


for _group in GROUPS:
    SLAB_BYTES[_group] = W.SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(LITERALS)
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_cobj8_installed", False):
        return
    fc._cobj8_installed = True
    mark_under_test()
    rng = env.rng
    for module in (cobj_specs7, cobj_specs6, cobj_specs4):
        module.install_stubs(fc, env)
    # the stubs of the lower groups answer for the scene they know; the ones this group needs differently:
    ensure(fc, 0x004496F0, 0, "engine_004496f0", result=lambda a: env.slab + W.FAKE + 0x340)
    ensure(fc, 0x00448DD0, 2, "engine_00448dd0", text=True, result=lambda a: env.slab + W.FAKE + 0x240)             # FileIO_OpenLoad(name, 1)
    ensure(fc, 0x0046F510, 1, "engine_0046f510", result=lambda a: rng.choice([0x40, 0x80, 0x100, 0x180]))          # Mem_GetBlockSize
    counter = [0]

    def alloc(args):
        """FileIO_AllocLoadedBuffer: a palette buffer (0x9000 bytes) is never touched, only handed on and freed: it gets an address of its own outside the arena."""
        if args[0] >= 0x8000:
            counter[0] += 1
            return 0x0A000000 + 0x10000 * counter[0]
        return env.alloc(args)

    ensure(fc, 0x0046F490, 4, "alloc", result=alloc)
    ensure(fc, 0x004B5010, 2, "engine_004b5010")                                                                    # PSX_RelocateDefinitionPointers
    ensure(fc, 0x004B2450, 1, "engine_004b2450")                                                                    # PSX_ParseEnvironment
    ensure(fc, 0x0041DEA0, 1, "engine_0041dea0", convention="thiscall", this=True)                                 # the texture-set manager's cretex loader
    ensure(fc, 0x00422690, 1, "engine_00422690", convention="thiscall", this=True)                                  # CustomSkater_LoadColours
    ensure(fc, 0x00422F20, 3, "engine_00422f20")                                                                    # CustomSkater_Init
    ensure(fc, 0x004BB4F0, 0, "engine_004bb4f0", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    ensure(fc, 0x00492B20, 0, "engine_00492b20")
    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    ensure(fc, 0x00466BB0, 1, "operator_delete")


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def cd(address, name, this=None):
    return r(address, name, "cdef", this)


def holder_of(env):
    return env.slab + HOLDER


def record(slot):
    return CUSTOM_RECORDS[0] + 0x80 * slot


def fake_manager(env):
    return env.slab + W.FAKE + 0x240


def records_state(img, env, rng, valid=None):
    """The four custom skaters' records: whether each exists, its texture set ids (1..3), its scale."""
    for slot in range(4):
        img.u8(record(slot) + 0x26, rng.choice([0, 1, 1]) if valid is None else valid[slot])
        img.put(record(slot) + 0x64, b"".join(u32(rng.choice([1, 2, 3])) for _ in range(7)))
        img.u8(record(slot) + 0x23, rng.choice([0, 50, 0x5A, 100, 0x7D, 200]))


def manifests(env, rng):
    env.manifest = {"process": cobj_specs4.process_manifest(env, rng, 19, 1, True), "anim": cobj_specs4.anim_manifest(env, rng)}       # (one texture group name: every part is variant 0, all visible)
    env.region_seq = [W.World.source, 0xFFFFFFFF]


def world_state(img, env, rng):
    img.put(GTE[0], noise(rng, GTE[1]))
    img.put(env.slab + W.PART_MAP, noise(rng, 0x10))
    img.u32(env.slab + W.ANIMATED, env.slab + W.ANIM_VTABLE)
    env.animated = True
    img.u32(env.slab + W.DB, 0x0051876C)
    env.free_slot = W.World.other
    for p in range(W.PARTS):
        if rng.random() < 0.15:
            img.u8(env.slab + W.SRC_MODELS + W.MODEL_STRIDE * p, rng.choice([0x20, 0x21, 0x00, 0x01]))


@cd(0x0042F880, "CustomDefinition_Construct", holder_of)
def gen_construct(env, n):
    img = Image()
    img.put(env.slab + HOLDER, noise(env.rng, 0x30))
    return [], img.pokes()


def holder_state(img, env, rng, base):
    img.u32(base, 0x00518848)
    owned = [0, 0, env.slab + W.SKATERS, env.slab + W.SKATERS + W.SKATER_STRIDE]     # (a skater is owned once: a second delete would find the base class's vtable)
    rng.shuffle(owned)
    for i in range(4):
        img.u32(base + 4 + 4 * i, owned[i])
        img.u32(base + 0x1C + 4 * i, rng.choice([0xFFFFFFFF, rng.randrange(0, 20)]))
    img.u32(base + 0x14, word(rng))
    img.u32(base + 0x18, rng.choice([0, env.slab + W.LIST]))


@cd(0x0042F8D0, "CustomDefinition_Destroy", holder_of)
def gen_destroy(env, n):
    rng = env.rng
    img = Image()
    holder_state(img, env, rng, env.slab + HOLDER)
    cobj_specs7.skater_alive(img, env, rng, env.slab + W.SKATERS)
    cobj_specs7.skater_alive(img, env, rng, env.slab + W.SKATERS + W.SKATER_STRIDE)
    img.u32(env.slab + W.SKATERS + W.SKATER_STRIDE + 0x280, 0)           # (one part database, owned by the first skater)
    world_state(img, env, rng)
    return [], img.pokes()


@cd(0x0042F8B0, "CustomDefinition_ScalarDelete", holder_of)
def gen_scalar_delete(env, n):
    rng = env.rng
    img = Image()
    holder_state(img, env, rng, env.slab + HOLDER)
    cobj_specs7.skater_alive(img, env, rng, env.slab + W.SKATERS)
    cobj_specs7.skater_alive(img, env, rng, env.slab + W.SKATERS + W.SKATER_STRIDE)
    img.u32(env.slab + W.SKATERS + W.SKATER_STRIDE + 0x280, 0)
    world_state(img, env, rng)
    return [cobj_specs.flags(rng)], img.pokes()


@cd(0x0042F950, "CustomSkater_LoadDefinitionPsx", holder_of)
def gen_load_definition(env, n):
    rng = env.rng
    img = Image()
    img.put(env.slab + HOLDER, noise(rng, 0x30))
    img.u32(env.slab + HOLDER + 0x18, rng.choice([0, 0, 0, env.slab + W.LIST]))
    return [rng.randrange(0, 4), word(rng), rng.randrange(0, 10)], img.pokes()


@cd(0x004236D0, "CustomSkaters_InitAll")
def gen_init_all(env, n):
    rng = env.rng
    img = Image()
    img.put(GLOBALS[0], noise(rng, 8))
    img.u32(CACHED[0], rng.choice([fake_manager(env), fake_manager(env), 0]))
    for k in range(SELECTION[1] // 0x170 + 1):
        if SELECTION[0] + 0x170 * k < SELECTION[0] + SELECTION[1]:
            img.u8(SELECTION[0] + 0x170 * k, rng.choice([0, 0, 0, 2, 2, 1, 3]) if k % 5 == 0 else 0)
    # the trick manager is the first block the arena hands out
    img.u32(env.slab + W.ARENA, 0x00519828)
    img.u32(env.slab + W.ARENA + 0x1408, rng.choice([0, 1]))
    return [], img.pokes()


@cd(0x00423B50, "CustomSkaters_FreeAll")
def gen_free_all(env, n):
    rng = env.rng
    img = Image()
    cobj_specs.spooler_state(img, env, rng)
    img.u32(PLAYER_SPOOL_HOLDER, env.slab + W.FAKE + 0x200)
    count = rng.choice([0, 1, 2, 4])
    entries = [rng.choice([0, env.slab + W.SKATERS, env.slab + W.SKATERS + W.SKATER_STRIDE]) for _ in range(4)]
    img.put(env.slab + W.LIST + 0x100, b"".join(u32(e) for e in entries))
    for k in range(2):
        img.u32(env.slab + W.SKATERS + W.SKATER_STRIDE * k + 0x88, rng.choice([0, env.slab + W.LIST]))
    return [count, rng.choice([env.slab + W.LIST + 0x100, env.slab + W.LIST + 0x100, 0])], img.pokes()


@cd(0x00423800, "CustomSkater_Create")
def gen_create(env, n):
    rng = env.rng
    img = Image()
    records_state(img, env, rng, [1, 1, 1, 1])
    manifests(env, rng)
    world_state(img, env, rng)
    cobj_specs.spooler_state(img, env, rng, 0)
    img.u32(PLAYER_SPOOL_HOLDER, env.slab + W.FAKE + 0x200)
    img.u32(MANAGER[0], 0 if rng.random() < 0.95 else fake_manager(env))
    slot = rng.randrange(0, 4)
    manager = rng.choice([0, fake_manager(env)])
    return [slot, rng.randrange(0, 10), rng.choice([0, 1, 1]), rng.choice([env.slab + W.LIST + 0x200, env.slab + W.LIST + 0x200]), manager], img.pokes()


@cd(0x0042FA10, "CustomSkaters_LoadAll", holder_of)
def gen_load_all(env, n):
    rng = env.rng
    img = Image()
    img.put(env.slab + HOLDER, noise(rng, 0x30))
    img.u32(env.slab + HOLDER + 0x18, 0 if rng.random() < 0.95 else env.slab + W.LIST)
    valid = [1, 1, 0, 0]                                                # (two skaters at most: the arena holds a manager and two skaters' parts)
    rng.shuffle(valid)
    records_state(img, env, rng, valid)
    manifests(env, rng)
    world_state(img, env, rng)
    cobj_specs.spooler_state(img, env, rng, 0)
    img.u32(PLAYER_SPOOL_HOLDER, env.slab + W.FAKE + 0x200)
    img.u32(MANAGER[0], 0)
    img.u32(CACHED[0], rng.choice([0, fake_manager(env)]))
    return [0], img.pokes()
