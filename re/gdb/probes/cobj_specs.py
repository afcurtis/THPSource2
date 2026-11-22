"""Phase 4.15f probe: the create-a-skater objects (createobj_*.cpp), part 1: the small objects (groups cdb, cspool).

Slab layout (offsets from the slab):
  0x0100  an object under test (0x6b00 bytes: the texture-set manager is the largest)
  0x6c00  a fake bitmap image the player spool hands back, 0x6e00 the sub-directory string, 0x7000 fake skater (MySkater) objects, 0x400 apart
  0x8000  the allocator arena (blocks for operator new, FileIO_AllocLoadedBuffer)
Every service the code reaches is a stub; the stubs record their arguments (and `this` for a method of an engine object).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("cdb", "cspool")

OBJECT = 0x0100
OBJECT_BYTES = 0x6B00
BITMAP = 0x6C00
SUBDIR = 0x6E00
SKATERS = 0x7000
SKATERS_BYTES = 0x1000
ARENA = 0x8000
ARENA_BYTES = 0x6000
SLAB = ARENA + ARENA_BYTES + 0x100
SCRATCH = 0x7FF00800 + 0x1000          # (createobj::kScratch: where the port builds the locals the retail code keeps on the stack)
SAVE_DIR = SCRATCH + 0x400

SPOOLER = 0x005601D0
SPOOLER_BYTES = 0xB40
PLAYER_SPOOL_HOLDER = 0x0056DB4C
VTABLES = {"database": 0x00518764, "manager": 0x00518768, "partdb": 0x0051876C, "skater": 0x00518770, "spooler": 0x00518774, "stitcher": 0x00518778,
           "definition": 0x00518848, "trick": 0x00519828, "bank": 0x00519734, "single": 0x00519890, "base": 0x005184B4}


def word(rng):
    return rng.choice([0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1000, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x10000), rng.randrange(0, 0x100)])


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def u32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def regions(slab):
    return merged([(slab + OBJECT, OBJECT_BYTES), (slab + BITMAP, 0x200), (slab + SUBDIR, 0x100), (slab + SKATERS, SKATERS_BYTES), (slab + ARENA, ARENA_BYTES), (SPOOLER, SPOOLER_BYTES),
                   (PLAYER_SPOOL_HOLDER, 4)], gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_BYTES
    img = Image()
    img.put(env.slab + SUBDIR, b"front\0")
    img.u32(PLAYER_SPOOL_HOLDER, env.slab + SKATERS + 0x2F0)       # the player spool object (only its address is used)
    return dict(img.pokes())


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = []
    SEEDS[_group] = seed


def r(address, name, group, this=None):
    resolve = this if callable(this) else ((lambda env: env.slab + this) if this is not None else None)
    return spec(address, name, group, this=resolve)


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_cobj_installed", False):
        return
    fc._cobj_installed = True
    mark_under_test()
    rng = env.rng
    # the class allocator and the heap: operator new hands out arena blocks (a null block when the generator wants the allocation to fail), operator delete and Mem_Delete report
    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    ensure(fc, 0x00466BB0, 1, "operator_delete")
    # the file system's current sub-directory (a string) and its setter (the caller keeps the old name in a local)
    ensure(fc, 0x004496F0, 0, "engine_004496f0", result=lambda a: env.slab + SUBDIR)
    ensure(fc, 0x00449700, 1, "engine_00449700", text=True, stack_to=SAVE_DIR)
    # the player spool (methods of the object the holder names)
    ensure(fc, 0x004B55C0, 0, "engine_004b55c0", convention="thiscall", this=True)                       # Pump
    ensure(fc, 0x004B58A0, 1, "engine_004b58a0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1, 1, 0x100, 0x101]))   # EntryReady (al)
    ensure(fc, 0x004B5990, 1, "engine_004b5990", convention="thiscall", this=True, result=lambda a: env.slab + BITMAP)                # TakeBuffer
    ensure(fc, 0x004B52B0, 0, "engine_004b52b0", convention="thiscall", this=True)                       # Reset
    ensure(fc, 0x004B5370, 4, "engine_004b5370", convention="thiscall", this=True, result=lambda a: rng.getrandbits(32))              # QueueResource
    ensure(fc, 0x004B5580, 0, "engine_004b5580", convention="thiscall", this=True)                       # ForceFinalize
    # the texture-set manager's cretex loader, the PSX region clear
    ensure(fc, 0x0041DEA0, 1, "engine_0041dea0", convention="thiscall", this=True)
    ensure(fc, 0x004B32F0, 3, "engine_004b32f0")
    # the pump's attach: the bitmap goes into the materials, then the palette, preset and animation
    ensure(fc, 0x00457E10, 3, "engine_00457e10", text=2)
    for address in (0x00422460, 0x00422C10, 0x004214F0):
        ensure(fc, address, 2, "engine_%08x" % address, convention="thiscall", this=True)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def cdb(address, name, this=None):
    return r(address, name, "cdb", this)


def database(img, env, rng, base, count=None):
    """A texture database: vtable, `count` entries (pointers) and the count."""
    count = rng.choice([0, 1, 2, 5, 9]) if count is None else count
    img.u32(base, VTABLES["database"])
    for i in range(count):
        img.u32(base + 4 + 4 * i, env.slab + ARENA + 0x40 * rng.randrange(0, 60))
    img.u32(base + 0x404, count)
    return img


def flags(rng):
    return rng.choice([0, 1, 1, 2, 3, 0x100, 0xFFFFFFFF, rng.getrandbits(32)])


@cdb(0x0041D790, "TextureDatabase_Construct", OBJECT)
def gen_db_construct(env, n):
    img = Image()
    img.garbage(env.slab + OBJECT, 8, env.rng)
    img.garbage(env.slab + OBJECT + 0x400, 8, env.rng)
    return [], img.pokes()


@cdb(0x0041D7D0, "TextureDatabase_Destruct", OBJECT)
def gen_db_destruct(env, n):
    img = Image()
    database(img, env, env.rng, env.slab + OBJECT)
    return [], img.pokes()


@cdb(0x0041D7B0, "TextureDatabase_ScalarDelete", OBJECT)
def gen_db_scalar_delete(env, n):
    img = Image()
    database(img, env, env.rng, env.slab + OBJECT)
    return [flags(env.rng)], img.pokes()


def manager(img, env, rng, base):
    img.u32(base, VTABLES["manager"])
    database(img, env, rng, base + 0x6608)
    img.u32(base + 0x6604, rng.choice([0, 3, 0x180]))


@cdb(0x0041DB50, "TextureSetManager_Initialize", OBJECT)
def gen_manager_construct(env, n):
    img = Image()
    img.garbage(env.slab + OBJECT, 8, env.rng)
    img.garbage(env.slab + OBJECT + 0x6600, 0x10, env.rng)
    img.garbage(env.slab + OBJECT + 0x6608, 8, env.rng)
    img.garbage(env.slab + OBJECT + 0x6608 + 0x400, 8, env.rng)
    return [], img.pokes()


@cdb(0x0041DBE0, "TextureSetManager_Destroy", OBJECT)
def gen_manager_destruct(env, n):
    img = Image()
    manager(img, env, env.rng, env.slab + OBJECT)
    return [], img.pokes()


@cdb(0x0041DBC0, "TextureSetManager_ScalarDelete", OBJECT)
def gen_manager_scalar_delete(env, n):
    img = Image()
    manager(img, env, env.rng, env.slab + OBJECT)
    return [flags(env.rng)], img.pokes()


def part_database(img, env, rng, base):
    img.u32(base, VTABLES["partdb"])
    img.u32(base + 0x1C78, rng.choice([0, 3, 7, 0xFFFFFFFF, word(rng)]))


@cdb(0x0041F370, "PartDatabase_Destroy", OBJECT)
def gen_partdb_destruct(env, n):
    img = Image()
    part_database(img, env, env.rng, env.slab + OBJECT)
    return [], img.pokes()


@cdb(0x0041F350, "PartDatabase_ScalarDelete", OBJECT)
def gen_partdb_scalar_delete(env, n):
    img = Image()
    part_database(img, env, env.rng, env.slab + OBJECT)
    return [flags(env.rng)], img.pokes()


def spooler_state(img, env, rng, busy=None):
    """The spooler object (the global one): 0x40 entries of 0x2c bytes from +8 (name at +4 of an entry, slot +0x18, free flag +0x1c), the copy of the front entry at +0xb08."""
    img.put(SPOOLER, noise(rng, SPOOLER_BYTES))
    img.u32(SPOOLER, rng.choice([VTABLES["spooler"], VTABLES["spooler"], VTABLES["base"], word(rng)]))
    img.u8(SPOOLER + 4, rng.choice([0, 1]) if busy is None else busy)
    live = rng.choice([0, 1, 2, 5, 0x40])
    order = list(range(0x40))
    rng.shuffle(order)
    used = set(order[:live])
    for i in range(0x40):
        entry = SPOOLER + 8 + 0x2C * i
        img.u32(entry + 0x1C, 0 if i in used else rng.choice([1, 1, 2, 0xFFFFFFFF]))
        if i in used:
            img.u32(entry, word(rng))
            img.put(entry + 4, b"skin%02d_%d.bmp\0" % (rng.randrange(0, 40), rng.randrange(0, 3)) + bytes(4))
            img.u32(entry + 0x18, rng.randrange(0, 9))
            img.u32(entry + 0x20, word(rng))
            img.u32(entry + 0x24, word(rng))
            img.u32(entry + 0x28, env.slab + SKATERS + 0x400 * rng.randrange(0, 2))
    # the entry being loaded
    img.u32(SPOOLER + 0xB08, word(rng))
    img.put(SPOOLER + 0xB0C, b"load%02d_%d.bmp\0" % (rng.randrange(0, 40), rng.randrange(0, 3)) + bytes(4))
    img.u32(SPOOLER + 0xB1C, rng.getrandbits(32))
    img.u32(SPOOLER + 0xB20, rng.choice([0, 1, 2, 2, 3, 8, word(rng) & 0xF]))
    img.u32(SPOOLER + 0xB28, word(rng))
    img.u32(SPOOLER + 0xB2C, word(rng))
    img.u32(SPOOLER + 0xB30, env.slab + SKATERS + 0x400 * rng.randrange(0, 2))


@cdb(0x00424610, "Spooler_Clear", lambda env: SPOOLER)
def gen_spooler_clear(env, n):
    img = Image()
    spooler_state(img, env, env.rng)
    return [], img.pokes()


@cdb(0x00424550, "Spooler_Construct", lambda env: SPOOLER)
def gen_spooler_construct(env, n):
    img = Image()
    spooler_state(img, env, env.rng)
    return [], img.pokes()


@cdb(0x004245C0, "Spooler_Destruct", lambda env: SPOOLER)
def gen_spooler_destruct(env, n):
    img = Image()
    spooler_state(img, env, env.rng)
    return [], img.pokes()


@cdb(0x004245A0, "Spooler_ScalarDelete", lambda env: SPOOLER)
def gen_spooler_scalar_delete(env, n):
    img = Image()
    spooler_state(img, env, env.rng)
    return [flags(env.rng)], img.pokes()


@cdb(0x00423B10, "Create_SpoolerUpdateThunk")
def gen_spooler_construct_thunk(env, n):
    img = Image()
    spooler_state(img, env, env.rng)
    return [], img.pokes()


@cdb(0x00422B20, "Create_SpoolerUpdate")
def gen_spooler_clear_thunk(env, n):
    img = Image()
    spooler_state(img, env, env.rng)
    return [], img.pokes()


@cdb(0x00423BD0, "Create_ResetSpool")
def gen_reset_spool(env, n):
    img = Image()
    spooler_state(img, env, env.rng)
    img.u32(PLAYER_SPOOL_HOLDER, env.slab + SKATERS + 0x2F0 + 0x10 * env.rng.randrange(0, 4))
    return [], img.pokes()
