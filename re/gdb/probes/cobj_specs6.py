"""Phase 4.15f probe: the create-a-skater objects, part 5: applying textures to a skater (group capp: CreatePart_ApplyTexture / ApplyPreviewTexture / AttachLoadedBitmap,
TextureSet_ApplyBodyGroup / ApplyPreviewGroups and the spooler's pump).

The scene is cobj_world's (a part database, regions, models and materials for the checksum lookups the functions do inline; three texture sets the stub TextureSet_FindById answers).
"""

from __future__ import annotations

import cobj_world as W
from cobj_world import u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

import cobj_specs
import cobj_specs7
from cobj_specs import PLAYER_SPOOL_HOLDER, SCRATCH, SPOOLER, SPOOLER_BYTES, noise, r, word

GROUPS = ("capp",)

MANAGER = (0x0056A87C, 4)
LOCAL_NAME = SCRATCH + 0x100                  # createobj::kLocalName: the bitmap name built in a local
SAVE_DIR = SCRATCH + 0x400
LITERALS = [(0x00525F40, 0xA0), (0x005264D0, 0x10), (0x00526210, 0x10), (0x00526328, 0x20), (0x00518764, 0x18), (0x00525DB0, 0x60), (0x00525F08, 0xD8)]       # the part names, "NONE", "skatedit", the stitcher's vtable and the part name tables


def regions(slab):
    return merged([(slab + W.DB, W.DB_BYTES), (slab + W.SKATERS, 0x800), (slab + W.SRC_TABLE, 0x100), (slab + W.BUILD_TABLE, 0x100), (slab + W.TABLES, 0x100),
                   (slab + W.MATERIALS, 0x100), (slab + W.TEXTURES, 0x100), (slab + W.SCENE, 0x100), (slab + W.LIST, 0x300), (slab + W.SETS, 0x400), (slab + W.SET_ENTRIES, 0x400),
                   (slab + W.FAKE, 0x300), (slab + W.BITMAP if hasattr(W, "BITMAP") else slab + W.FAKE + 0x300, 0x100), (slab + W.SRC_MODELS, W.MODEL_STRIDE * W.PARTS),
                   (slab + W.BUILD_MODELS, W.MODEL_STRIDE * W.PARTS), (slab + W.PART_MAP, 0x400), (slab + W.LOG, 0x200), (slab + W.POSE, 0x400), (slab + W.ARENA, 0x6000), cobj_specs7.GTE,
                   MANAGER, (SPOOLER, SPOOLER_BYTES), (PLAYER_SPOOL_HOLDER, 4), *W.region_windows()], gap=0)


def seed(env, rng):
    env.alloc_base = W.ARENA
    env.alloc_size = W.ARENA_BYTES
    env.region_seq = []
    env.animated = True
    env.free_slot = None
    writes = W.seed_scene(env, rng)
    writes[env.slab + W.FAKE + 0x300] = b"front\0"                     # the sub-directory string
    writes[PLAYER_SPOOL_HOLDER] = u32(env.slab + W.FAKE + 0x200)       # the player spool object (only its address is used)
    return writes


for _group in GROUPS:
    SLAB_BYTES[_group] = W.SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(LITERALS)
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_cobj6_installed", False):
        return
    fc._cobj6_installed = True
    mark_under_test()
    rng = env.rng
    cobj_specs7.install_stubs(fc, env)          # (a body group that changes restitches the skater: the weld's services)
    ensure(fc, 0x004B20F0, 1, "engine_004b20f0", result=lambda a: env.slab + W.TABLES)
    ensure(fc, 0x004B2030, 1, "engine_004b2030", result=lambda a: env.materials.get(a[0], 0))
    ensure(fc, 0x004496F0, 0, "engine_004496f0", result=lambda a: env.slab + W.FAKE + 0x300)
    ensure(fc, 0x00449700, 1, "engine_00449700", text=True, stack_to=SAVE_DIR)
    ensure(fc, 0x0041DC30, 1, "engine_0041dc30", convention="thiscall", this=True, result=lambda a: W.World.sets.get(a[0], 0))
    ensure(fc, 0x0041FE50, 2, "engine_0041fe50", convention="thiscall", this=True, result=lambda a: 0)       # (the group is always found at 0: a restitch needs every part visible)
    ensure(fc, 0x004674D0, 2, "engine_004674d0", text=1, stack_to=LOCAL_NAME)
    ensure(fc, 0x00457E10, 3, "engine_00457e10", text=2)
    for address in (0x00422460, 0x00422C10, 0x004214F0):
        ensure(fc, address, 2, "engine_%08x" % address, convention="thiscall", this=True)
    # the player spool (methods of the object the holder names)
    ensure(fc, 0x004B55C0, 0, "engine_004b55c0", convention="thiscall", this=True)
    ensure(fc, 0x004B58A0, 1, "engine_004b58a0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1, 1, 0x100, 0x101]))
    ensure(fc, 0x004B5990, 1, "engine_004b5990", convention="thiscall", this=True, result=lambda a: env.slab + W.FAKE + 0x340)
    ensure(fc, 0x004B52B0, 0, "engine_004b52b0", convention="thiscall", this=True)
    ensure(fc, 0x004B5370, 4, "engine_004b5370", convention="thiscall", this=True, result=lambda a: rng.getrandbits(32))
    ensure(fc, 0x004B5580, 0, "engine_004b5580", convention="thiscall", this=True)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def ca(address, name, this=None):
    return r(address, name, "capp", this)


def skater_of(env):
    return env.slab + W.SKATERS


def skater_state(img, env, rng, spool=None):
    """The skater's texture state: the spool flag, the stored names, the body group words."""
    base = env.slab + W.SKATERS
    img.u32(base + 4, rng.choice([0, 1]) if spool is None else spool)
    img.u32(base + 0x1F8, rng.getrandbits(32))
    for slot in range(10):
        img.put(base + 0x288 + 0x10 * slot, rng.choice([b"NONE\0", b"skin00_0.bmp\0", b"skin01_1.bmp\0"]))
    img.put(base + 0x308, rng.choice([b"NONE\0", b"skin00_0.bmp\0", b"skin01_1.bmp\0"]))
    for offset in (0x70, 0x74, 0x78, 0x7C):
        img.u32(base + offset, rng.choice([0, 1, 2, 0xFFFFFFFF]))
    for offset in (0x80, 0x144, 0x150, 0x15C):
        img.u32(base + offset, rng.getrandbits(32))
    img.u32(base + 0x280, env.slab + W.DB)


def spooler_state(img, env, rng, busy=None):
    cobj_specs.spooler_state(img, env, rng, busy)
    img.u32(PLAYER_SPOOL_HOLDER, env.slab + W.FAKE + 0x200 + 0x10 * rng.randrange(0, 4))


def bitmap_name(rng, tone_digit=None):
    digit = rng.choice(["0", "1", "2"]) if tone_digit is None else tone_digit
    return f"skin{rng.randrange(0, 3)}{rng.randrange(0, 4)}_{digit}.bmp".encode() + b"\0"


def put_name(img, env, offset, name):
    img.put(env.slab + W.STRINGS + offset, name)
    return env.slab + W.STRINGS + offset


def restitch_state(img, env, rng):
    """What a restitch needs (the body group words and visible parts, the GTE, the part map, the animated object)."""
    cobj_specs7.weld_state(env, rng, img)
    cobj_specs7.skater_visible_state(img, env, rng, skater_of(env))
    holders = [rng.choice([0, env.slab + W.BUILD_MODELS + W.MODEL_STRIDE * q]) for q in range(W.PARTS)]
    img.put(env.slab + W.BUILD_TABLE + 4, b"".join(u32(h) for h in holders))


def words_state(img, env, rng, first):
    """The skater's body group words: the parts of the scene are all group 0, so only the first word decides what is visible; a word that differs from what the lookup answers (always 0) makes the
    function reset the texture names and restitch, which leaves the word 0."""
    base = skater_of(env)
    img.u32(base + 0x70, rng.choice([0, 1, 2, 3]) if first else 0)
    for offset in (0x74, 0x78, 0x7C):
        img.u32(base + offset, rng.choice([0, 0, 1, 2, 3]))


@ca(0x004221A0, "CreatePart_ApplyTexture", skater_of)
def gen_apply_texture(env, n):
    rng = env.rng
    img = Image()
    skater_state(img, env, rng)
    spooler_state(img, env, rng)
    slot = rng.choice([0, 1, 2, 2, 3, 4, 5, 6, 7])
    name = bitmap_name(rng)
    tone = rng.choice([0, 1, 2, 3, 0x100, 0xFFFFFF01])
    # sometimes the slot already holds the bitmap this call would name (the digit at 7 replaced by the tone)
    if rng.random() < 0.3:
        built = bytearray(name)
        if built[7] != 0x30:
            built[7] = (tone + 0x31) & 0xFF
        img.put(env.slab + W.SKATERS + 0x288 + 0x10 * slot, bytes(built))
    checksum = rng.choice([0, rng.choice(W.World.checksums), rng.getrandbits(32)])
    return [checksum, put_name(img, env, 0, name), tone, slot, word(rng), rng.choice([0, 1, word(rng)])], img.pokes()


@ca(0x00422340, "CreatePart_ApplyPreviewTexture", skater_of)
def gen_apply_preview_texture(env, n):
    rng = env.rng
    img = Image()
    skater_state(img, env, rng)
    spooler_state(img, env, rng)
    name = bitmap_name(rng)
    kind = rng.choice([0, 1, 1, 2, 0x101])
    if rng.random() < 0.3:
        built = bytearray(name)
        built[4] = 0x73 if kind != 1 else 0x6C
        img.put(env.slab + W.SKATERS + 0x308, bytes(built))
    checksum = rng.choice([0, rng.choice(W.World.checksums), rng.getrandbits(32)])
    return [checksum, put_name(img, env, 0, name), kind, word(rng)], img.pokes()


@ca(0x00422B30, "CreatePart_AttachLoadedBitmap", skater_of)
def gen_attach(env, n):
    rng = env.rng
    img = Image()
    skater_state(img, env, rng)
    name = bitmap_name(rng)
    data = rng.choice([0, env.slab + W.FAKE + 0x340, env.slab + W.FAKE + 0x340])
    return [data, rng.choice(W.World.checksums), rng.choice([0, 1, 2, 2, 5, 8]), put_name(img, env, 0, name), word(rng), rng.choice([0, 1, word(rng)])], img.pokes()


def manager_state(img, env, rng):
    img.u32(MANAGER[0], rng.choice([env.slab + W.FAKE + 0x240, env.slab + W.FAKE + 0x240, env.slab + W.FAKE + 0x240, 0]))


def vary_scene(img, env, rng):
    for p in range(W.PARTS):
        if rng.random() < 0.2:
            img.u8(env.slab + W.SRC_MODELS + W.MODEL_STRIDE * p, rng.choice([0x20, 0x21, 0x00, 0x01]))
    for k, address in W.World.sets.items():
        if rng.random() < 0.3:
            img.u8(address + 0x41, rng.choice([0, 1]))
        entry = rng.randrange(0, 4)
        if rng.random() < 0.3:
            img.u8(env.slab + W.SET_ENTRIES + 0x40 * (4 * (k - 1) + entry) + 0x18, rng.choice([0, 1]))


@ca(0x004208A0, "TextureSet_ApplyBodyGroup", skater_of)
def gen_apply_body_group(env, n):
    rng = env.rng
    img = Image()
    skater_state(img, env, rng)
    spooler_state(img, env, rng)
    manager_state(img, env, rng)
    vary_scene(img, env, rng)
    restitch_state(img, env, rng)
    kind = rng.choice([0, 1, 4, 5, 6, 0, 1, 2, 7])
    words_state(img, env, rng, first=kind == 0)
    return [kind, rng.choice([1, 2, 3, 3, 9]), rng.choice([0, 1, 2, 3])], img.pokes()


@ca(0x00422D80, "TextureSet_ApplyPreviewGroups", skater_of)
def gen_apply_preview_groups(env, n):
    rng = env.rng
    img = Image()
    skater_state(img, env, rng)
    spooler_state(img, env, rng)
    manager_state(img, env, rng)
    vary_scene(img, env, rng)
    restitch_state(img, env, rng)
    words_state(img, env, rng, first=False)
    return [rng.choice([1, 2, 3]), rng.choice([1, 2, 3]), rng.choice([1, 2, 3]), rng.choice([0, 1, 2, 3])], img.pokes()


@ca(0x00424630, "Spooler_Pump", lambda env: SPOOLER)
def gen_pump(env, n):
    rng = env.rng
    img = Image()
    skater_state(img, env, rng)
    spooler_state(img, env, rng)
    for k in range(2):
        img.u32(env.slab + W.SKATERS + 0x400 * k + 0x1F8, rng.getrandbits(32))
    return [], img.pokes()


@ca(0x00422B10, "Create_SpoolTexturesThunk")
def gen_pump_thunk(env, n):
    rng = env.rng
    img = Image()
    skater_state(img, env, rng)
    spooler_state(img, env, rng)
    for k in range(2):
        img.u32(env.slab + W.SKATERS + 0x400 * k + 0x1F8, rng.getrandbits(32))
    return [], img.pokes()
