"""Phase 4.15f probe: the create-a-skater objects, part 2: the record bank, the singleton, the strings, the viewport message and the trick leftovers (group cmisc).

Uses the slab layout of cobj_specs.py: the object under test at 0x0100, a string at 0x6c00, fake skaters from 0x7000, the arena from 0x8000.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

import cobj_specs
from cobj_specs import ARENA, ARENA_BYTES, BITMAP, OBJECT, SCRATCH, SKATERS, SKATERS_BYTES, SLAB, VTABLES, flags, noise, r, u32, word

GROUPS = ("cmisc",)

VERSUS = [(0x0056A858, 8), (0x0056DB5C, 12)]
SINGLETON = 0x0056DB98
OUT_X = SCRATCH + 0x20
OUT_Y = SCRATCH + 0x24
CAREER = (0x00568A4C, 8)
PLAYERS = (0x00533F3C, 4)
VIEWPORT_MODE = (0x0056A928, 4)


def regions(slab):
    return merged([(slab + OBJECT, 0x1500), (slab + BITMAP, 0x200), (slab + ARENA, ARENA_BYTES), (SINGLETON, 8), *VERSUS, CAREER, PLAYERS, VIEWPORT_MODE], gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_BYTES
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = [(0x00519828, 4)]       # (the vtable whose first slot the virtual delete reads)
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_cobj2_installed", False):
        return
    fc._cobj2_installed = True
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    ensure(fc, 0x00466BB0, 1, "operator_delete")
    ensure(fc, 0x00492B20, 0, "engine_00492b20")                                                  # UnloadGTricks
    ensure(fc, 0x004DE010, 1, "engine_004de010", result=lambda a: rng.choice([0, 0, 1, 0x100, 0x101]))     # NetGame_IsRemotePlayer (al)
    ensure(fc, 0x0048C760, 3, "engine_0048c760", pokes=[(1, 4, lambda a: rng.getrandbits(32)), (2, 4, lambda a: rng.getrandbits(32))],
           stack_to={1: OUT_X, 2: OUT_Y})                                                         # Panel_ViewportOffset(mode, &x, &y)
    ensure(fc, 0x004736F0, 5, "engine_004736f0")                                                  # Mess_AddMessage
    ensure(fc, 0x004BB4F0, 0, "engine_004bb4f0", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])   # Tricks_InitializeManager
    ensure(fc, 0x004BCF00, 2, "engine_004bcf00", convention="thiscall", this=True)                # TrickTable_BuildForPlayer


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def cm(address, name, this=None):
    return r(address, name, "cmisc", this)


@cm(0x004B0B50, "RecordBank_Construct", OBJECT)
def gen_bank_construct(env, n):
    img = Image()
    img.put(env.slab + OBJECT, noise(env.rng, 0xC0))
    return [], img.pokes()


@cm(0x004B0BB0, "RecordBank_Destruct", OBJECT)
def gen_bank_destruct(env, n):
    img = Image()
    img.put(env.slab + OBJECT, noise(env.rng, 0x20))
    return [], img.pokes()


@cm(0x004B0B90, "RecordBank_ScalarDelete", OBJECT)
def gen_bank_scalar_delete(env, n):
    img = Image()
    img.put(env.slab + OBJECT, noise(env.rng, 0x20))
    return [flags(env.rng)], img.pokes()


@cm(0x004C43B0, "Singleton_ConstructGlobal")
def gen_singleton_global(env, n):
    img = Image()
    img.put(SINGLETON, noise(env.rng, 8))
    return [], img.pokes()


@cm(0x004C43F0, "Singleton_Construct", OBJECT)
def gen_singleton_construct(env, n):
    img = Image()
    img.put(env.slab + OBJECT, noise(env.rng, 8))
    return [], img.pokes()


@cm(0x004C43E0, "Singleton_Destruct", OBJECT)
def gen_singleton_destruct(env, n):
    img = Image()
    img.put(env.slab + OBJECT, noise(env.rng, 8))
    return [], img.pokes()


@cm(0x004C4400, "Singleton_ScalarDelete", OBJECT)
def gen_singleton_scalar_delete(env, n):
    img = Image()
    img.put(env.slab + OBJECT, noise(env.rng, 8))
    return [flags(env.rng)], img.pokes()


@cm(0x004B8340, "String_ToLowerInPlace")
def gen_to_lower(env, n):
    rng = env.rng
    img = Image()
    length = rng.choice([0, 1, 3, 8, 20])
    text = bytes(rng.choice([rng.randrange(0x41, 0x5B), rng.randrange(0x61, 0x7B), rng.randrange(0x20, 0x7F), rng.randrange(1, 256), 0x40, 0x5B, 0x5A, 0x41]) for _ in range(length))
    img.put(env.slab + BITMAP, text + b"\0" + noise(rng, 4))
    return [env.slab + BITMAP], img.pokes()


@cm(0x004B8360, "Char_IsBlank")
def gen_is_blank(env, n):
    rng = env.rng
    img = Image()
    img.put(env.slab + BITMAP, bytes([rng.choice([0x20, 0x09, 0x20, 0x09, 0x0A, 0x41, 0x00, 0xFF, 0x21, rng.randrange(256)])]) + noise(rng, 3))
    return [env.slab + BITMAP], img.pokes()


@cm(0x004B8380, "LevelEvent_SelectPlayers")
def gen_select_players(env, n):
    img = Image()
    for address, size in VERSUS:
        img.put(address, noise(env.rng, size))
    return [], img.pokes()


@cm(0x004B83B0, "Mess_AddViewportMessage")
def gen_viewport_message(env, n):
    rng = env.rng
    img = Image()
    img.put(env.slab + OBJECT, noise(rng, 0x180))
    img.u32(VIEWPORT_MODE[0], rng.choice([0, 1, 2, 3, word(rng)]))
    return [env.slab + OBJECT, word(rng)], img.pokes()


@cm(0x00416050, "TrickConfig_FindByCharacter")
def gen_find_trick_config(env, n):
    return [env.rng.choice([0, 1, 2, 3, 7, 12, 0x13, env.rng.randrange(0, 0x100)])], {}


def trick_manager(img, env, rng, base):
    img.u32(base, VTABLES["trick"])
    img.put(base + 0x1400, noise(rng, 0xC))
    img.u32(base + 0x1408, rng.choice([0, 0, 1, word(rng)]))


@cm(0x004BB790, "TrickManager_Destructor", OBJECT)
def gen_trick_destruct(env, n):
    img = Image()
    trick_manager(img, env, env.rng, env.slab + OBJECT)
    return [], img.pokes()


@cm(0x004BB770, "TrickManager_DeletingDestructor", OBJECT)
def gen_trick_scalar_delete(env, n):
    img = Image()
    trick_manager(img, env, env.rng, env.slab + OBJECT)
    return [flags(env.rng)], img.pokes()


@cm(0x004BD1E0, "TrickRuntime_BuildPlayerTables")
def gen_build_tables(env, n):
    rng = env.rng
    img = Image()
    # the manager is the first block the arena hands out: its vtable decides what the virtual delete runs
    trick_manager(img, env, rng, env.slab + ARENA)
    img.put(CAREER[0], noise(rng, 8))
    img.u32(PLAYERS[0], rng.choice([1, 2, 2, 0, 3]))
    return [], img.pokes()
