"""Phase 4.15c probe: the trick objects (groups wtk and wtk2): the list of tag/goal objects of a level, their activation by a skater, the flicker, the line renderer and the box outline
(0x004bd700..0x004be350).

The two delete-all functions have a group of their own (wtk2): they call the objects' deleting destructor (0x004bd920) through the vtable, which is a function under test in wtk. In wtk2 it is a
stub that scripts the new head of the list (the destructor unlinks the object).

Slab layout (offsets): image 0x1000 (table of records, 0x60 each), trick objects 0xa00 (4 of 0x40), enviro items 0xb00 (3 of 0x40), display 0xc00, ordering-table cell 0xc90, primitives 0xca0,
model 0xd00, model table 0xd20, new object 0xe00, skater 0x2000 (windows at +0x29bc and +0x2cdc).
"""

from __future__ import annotations

import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import word

GROUPS = ("wtk", "wtk2")

IMG = 0x1000
RECORDS = 0x100
STRIDE = 0x60
TOBJ = 0xA00
ITEMS = 0xB00
DISPLAY = 0xC00
OT_CELL = 0xC90
PRIMS = 0xCA0
MODEL = 0xD00
MODEL_TABLE = 0xD20
NEW = 0xE00
SKATER = 0x2000
SLAB = 0x2000 + 0x2CF0

LIST = 0x0056DB90
FLAG_A = 0x0056DB8C
FLAG_B = 0x0056DB88
REMINDER = 0x0056DB84
STEAL_REMINDER = 0x0056DB80
LAST = 0x0056DB78            # colour (dword), strength (dword)
GAME_MODE = 0x00533F38
RECORD_TABLE = 0x0056E210
RECORD_COUNT = 0x0056E214
OWNER = 0x0056A954
PLAYER0 = 0x0056A858
ENVIRONMENT = 0x0056DB28
FRAME = 0x0056E320
FLICKER = 0x00541548
LINE = 0x00541568
CURSOR = 0x00560FD0
LIMIT = 0x00568628
SCREEN = 0x029DA394
REGION_TABLE = 0x0056D43C
SPRITE_VTABLE_DUMMY = 0x0051982C
VTABLE = 0x0051982C
CLASS_DELETE_HEAP = (0x0056A920, 4), (0x0056A970, 4)
CHECKSUMS = [0x11111111, 0x22222222, 0x33333333, 0x44444444]


def regions(slab):
    windows = [(slab + IMG, 0x900), (slab + TOBJ, 0x100), (slab + ITEMS, 0xC0), (slab + DISPLAY, 0x90), (slab + OT_CELL, 8), (slab + PRIMS, 0x100), (slab + MODEL, 0x20), (slab + MODEL_TABLE, 0x10),
               (slab + NEW, 0x40), (slab + SKATER, 0x40), (slab + SKATER + 0x168, 4), (slab + SKATER + 0x29BC, 4), (slab + SKATER + 0x2CDC, 8)]
    globals_ = [(LAST, 8), (REMINDER, 4), (STEAL_REMINDER, 4), (FLAG_B, 4), (FLAG_A, 8), (LIST, 4), (GAME_MODE, 4), (RECORD_TABLE, 8), (OWNER, 4), (PLAYER0, 4), (ENVIRONMENT, 4), (FRAME, 4),
                (LINE, 0x10), (CURSOR, 8), (LIMIT, 4), (SCREEN, 8), (REGION_TABLE, 4 * 17 * 3), *CLASS_DELETE_HEAP]
    return merged(windows + globals_, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = [(FLICKER, 0x20), (VTABLE, 8)]


def wtk(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "wtk", this=resolve)


def wtk2(address, name):
    return spec(address, name, "wtk2")


wtk_this = [0]          # (the object a delete is called on: one of the list, picked per case)


def wtk_node(address, name):
    return spec(address, name, "wtk", this=lambda env: wtk_this[0])


STATE = {"item": 0}


def install_stubs(fc, env):
    from opentony.memory import mem
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_wtk_installed", False):
        return
    fc._wtk_installed = True
    mark_under_test()
    stop_clock(fc)
    rng = env.rng
    ensure(fc, 0x004B1F40, 1, "engine_004b1f40", result=lambda a: rng.choice(CHECKSUMS + [rng.getrandbits(32)]))      # PSX_GetModelNameChecksum
    ensure(fc, 0x004B1EF0, 1, "engine_004b1ef0", result=lambda a: STATE["item"])                                        # Spool_FindEnviroItem
    ensure(fc, 0x0048F800, 1, "engine_0048f800", convention="thiscall", this=True)                                       # Trick_RegisterObject
    ensure(fc, 0x004AD620, 3, "engine_004ad620")                                                                         # SFX_PlayX
    ensure(fc, 0x004DF2C0, 4, "engine_004df2c0")                                                                         # NetGame_OnPoint
    ensure(fc, 0x004DE010, 1, "engine_004de010", result=lambda a: rng.choice([0, 1, 0x100, rng.getrandbits(32)]))        # NetGame_IsRemotePlayer
    ensure(fc, 0x0048E950, 0, "engine_0048e950", convention="thiscall", this=True)                                       # Panel_ShowStealsMessage
    ensure(fc, 0x004CB4C0, 2, "engine_004cb4c0")                                                                         # Replay_RecordTrickObject
    ensure(fc, 0x004C58B0, 1, "engine_004c58b0")                                                                         # TRG_SendPulse
    # CClass_OpDelete runs as retail code with its Mem_Delete call site stubbed
    fc.stub_site(0x00466BBC, 5, 1, "mem_delete", writes=[(0x0056A970, 4, lambda a: rng.getrandbits(32))])
    if "wtk2" in os.environ.get("P410_GROUPS", "").split(","):
        # the deleting destructor of a trick object (slot 0 of its vtable): the object leaves the list, which the stub scripts as the new head
        ensure(fc, 0x004BD920, 1, "engine_004bd920", convention="thiscall", this=True,
               writes=[(LIST, 4, lambda a: mem.u32(mem.u32(LIST) + 0x10))])


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


# ---- the image and the objects -----------------------------------------------------------------------------------------------------------------------------------------------------------

def record_bytes(rng, base, kind, links, checksum):
    body = bytearray(rng.getrandbits(8) & 0xFE for _ in range(STRIDE))
    struct.pack_into("<HH", body, 0, kind, len(links))
    for k, link in enumerate(links):
        struct.pack_into("<H", body, 4 + 2 * k, link)
    at = base + 2 * len(links) + 4
    if at & 3:
        at += 2
    struct.pack_into("<I", body, at - base, checksum)
    return bytes(body)


def put_records(img, env, rng, count=None, kinds=None):
    """The table of records (types 0xc / 0xe mostly, each linking to others); returns (kinds, checksums)."""
    count = count if count is not None else rng.choice([2, 3, 4, 6])
    kinds = kinds or [rng.choice([0xC, 0xE, 0xE, 0xC, 8, 3]) for _ in range(count)]
    base = env.slab + IMG
    data = bytearray(RECORDS + STRIDE * (count + 1))
    struct.pack_into("<IIH", data, 0, 0x4752545F, 2, count)
    checks = []
    for k in range(count):
        address = base + RECORDS + STRIDE * k
        links = [rng.randrange(count) for _ in range(rng.choice([0, 0, 1, 2, 3]))]
        checks.append(rng.choice(CHECKSUMS))
        data[RECORDS + STRIDE * k:RECORDS + STRIDE * (k + 1)] = record_bytes(rng, address, kinds[k], links, checks[-1])
        struct.pack_into("<I", data, 0xC + 4 * k, address)
    img.put(base, bytes(data))
    img.u32(RECORD_TABLE, base + 0xC)
    img.u32(RECORD_COUNT, count)
    return kinds, checks


def put_objects(img, env, rng, kinds, checks, count=None):
    """A list of trick objects (+0x10), each pointing at one of the records."""
    count = count if count is not None else rng.choice([0, 1, 2, 3, 4])
    nodes = [env.slab + TOBJ + 0x40 * k for k in range(count)]
    for k, node in enumerate(nodes):
        index = rng.randrange(len(kinds))
        img.put(node, noise(rng, 0x40))
        img.u32(node, VTABLE)
        img.u32(node + 0x10, nodes[k + 1] if k + 1 < count else 0)
        img.u32(node + 4, checks[index] if rng.random() < 0.8 else rng.choice(CHECKSUMS))
        img.u16(node + 8, index)
        img.u8(node + 0xA, rng.choice([0, 1]))
        img.u8(node + 0xB, rng.choice([0, 1, 2]))
        img.u16(node + 0xC, rng.choice([0, 1, 100, 1000, 2000, 0x7FFF, 0xFFFF, rng.getrandbits(16)]))
        img.u32(node + 0x14, rng.choice([0, env.slab + ITEMS, env.slab + ITEMS + 0x40]))
    img.u32(LIST, nodes[0] if nodes else 0)
    return nodes


def put_items(img, env, rng):
    for k in range(3):
        item = env.slab + ITEMS + 0x40 * k
        img.put(item, noise(rng, 0x40))
        img.u32(item + 0x20, env.slab + ITEMS + 0x40 * (k + 1) if k < 2 else 0)
    STATE["item"] = env.slab + ITEMS + 0x40 * rng.randrange(3)


def put_globals(img, env, rng, mode=None):
    img.u32(GAME_MODE, mode if mode is not None else rng.choice([0, 1, 8, 8, 2, rng.getrandbits(32)]))
    img.u32(FLAG_A, rng.choice([0, 1, 1, rng.getrandbits(32)]))
    img.u32(FLAG_B, rng.choice([0, 0, 1]))
    img.u32(REMINDER, rng.choice([0, 1]))
    img.u32(STEAL_REMINDER, rng.choice([0, 1]))
    img.put(LAST, noise(rng, 8))
    img.u32(FRAME, rng.choice([0, 1, 5, 99, 1000, rng.getrandbits(32)]))
    img.u32(OWNER, rng.choice([0, env.slab + SKATER]))
    img.u32(PLAYER0, rng.choice([env.slab + SKATER, env.slab + SKATER + 0x40, 0]))


# ---- the list --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@wtk2(0x004BD700, "TrickObject_DeleteAll")
def gen_delete_all(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    put_objects(img, env, rng, kinds, checks, count=rng.choice([0, 1, 2, 3, 4]))
    return [], img.pokes()


@wtk2(0x004BD720, "TrickObject_DeleteAllPreserve")
def gen_delete_all_preserve(env, n):
    return gen_delete_all(env, n)


@wtk(0x004BD740, "TRG_IsGoalObjectNode")
def gen_is_goal(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    return [rng.randrange(len(kinds)) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0)], img.pokes()


@wtk(0x004BD760, "TrickObject_Construct", NEW)
def gen_construct(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    put_objects(img, env, rng, kinds, checks, count=rng.choice([0, 1, 2]))
    img.put(env.slab + NEW, noise(rng, 0x40))
    index = rng.randrange(len(kinds))
    return [index | (rng.getrandbits(16) << 16 if rng.random() < 0.2 else 0), rng.choice([0, 1, 2, 2, 3, 0x10000])], img.pokes()


@wtk_node(0x004BD920, "TrickObject_ScalarDelete")
def gen_scalar_delete(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    nodes = put_objects(img, env, rng, kinds, checks, count=rng.choice([1, 2, 3, 4]))
    img.u32(0x0056A920, word(rng))
    img.u32(0x0056A970, word(rng))
    wtk_this[0] = rng.choice(nodes)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@wtk_node(0x004BD940, "TrickObject_Destruct")
def gen_destruct(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    nodes = put_objects(img, env, rng, kinds, checks, count=rng.choice([1, 2, 3, 4]))
    wtk_this[0] = rng.choice(nodes)
    return [], img.pokes()


# ---- activation ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def put_skater(img, env, rng):
    img.u32(env.slab + SKATER + 0x29BC, env.slab + SKATER)
    img.put(env.slab + SKATER + 0x2CDC, noise(rng, 8))
    img.put(env.slab + SKATER + 0x168, noise(rng, 4))
    img.put(env.slab + SKATER, noise(rng, 0x40))


@wtk(0x004BD980, "TrickObject_ActivateForObject")
def gen_activate(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    put_objects(img, env, rng, kinds, checks)
    put_items(img, env, rng)
    put_globals(img, env, rng)
    put_skater(img, env, rng)
    skater = env.slab + SKATER
    strength = rng.choice([0xFFFFFFFF, 0, 1, 500, 1000, 1500, 0x7FFF, 0x10000, rng.getrandbits(32), rng.randrange(0, 2000)])
    if rng.random() < 0.4:                       # the tag game, a round under way: the steal path
        img.u32(GAME_MODE, 8)
        img.u32(FLAG_A, 0)
        img.u32(REMINDER, rng.choice([0, 1]))
        img.u32(STEAL_REMINDER, rng.choice([0, 1]))
        strength = rng.choice([1500, 2000, 0x7FFF, 100, rng.randrange(0, 3000)])
        for k in range(4):
            img.u16(env.slab + TOBJ + 0x40 * k + 0xC, rng.choice([0, 1, 100, 1000, 0xFFFF]))
            img.u8(env.slab + TOBJ + 0x40 * k + 0xA, rng.choice([0, 1]))
    return [env.slab + ITEMS + 0x40 * rng.randrange(3), skater, strength], img.pokes()


@wtk(0x004BDBD0, "TrickObject_Take")
def gen_take(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    nodes = put_objects(img, env, rng, kinds, checks)
    put_items(img, env, rng)
    put_globals(img, env, rng)
    wanted = rng.choice([0, *range(len(kinds)), *range(len(kinds)), 0x10000])
    return [wanted], img.pokes()


@wtk(0x004BDC40, "TrickObject_ActivateGoalObject")
def gen_goal(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    put_objects(img, env, rng, kinds, checks)
    put_items(img, env, rng)
    put_globals(img, env, rng)
    STATE["item"] = env.slab + ITEMS + 0x40 * rng.randrange(3)
    wanted = rng.choice([0, *range(len(kinds)), *range(len(kinds))])
    return [wanted], img.pokes()


@wtk(0x004BDD00, "TrickObject_Update")
def gen_update(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    nodes = put_objects(img, env, rng, kinds, checks)
    put_items(img, env, rng)
    put_globals(img, env, rng)
    img.put(env.slab + ITEMS, noise(rng, 0xC0))
    if rng.random() < 0.5:                       # the tag game between rounds: the strength pulse
        img.u32(GAME_MODE, 8)
        img.u32(FLAG_A, 0)
        for k in range(4):
            img.u8(env.slab + TOBJ + 0x40 * k + 0xA, 1)
    return [], img.pokes()


@wtk(0x004BDEA0, "TrickObject_LineRender")
def gen_line(env, n):
    img, rng = Image(), env.rng
    img.put(LINE, noise(rng, 0xC))
    img.u32(LINE + 8, rng.choice([0x1000, 0x2000, 0x100, 0x4000, word(rng) & 0xFFFF]))
    img.u32(LINE, rng.choice([0, 0x10, 0xFFFFFFF0, 0x100, 100]))
    img.u32(LINE + 4, rng.choice([0, 0x10, 0xFFFFFFF0, 0x100, 60]))
    prim = env.slab + PRIMS
    img.u32(CURSOR, prim)
    img.u32(LIMIT, rng.choice([prim + 0x100, prim + 0x100, prim + 0x14, prim + 0x13, prim + 0x28]))
    img.u32(CURSOR + 4, env.slab + DISPLAY)
    img.u32(env.slab + DISPLAY + 0x84, env.slab + OT_CELL)
    img.put(env.slab + OT_CELL, noise(rng, 8))
    img.put(SCREEN, noise(rng, 8))
    img.u32(SCREEN, rng.choice([1, 2, 0x200, 0x100, 0x400, rng.getrandbits(16)]))
    img.u32(SCREEN + 4, rng.choice([1, 2, 0x100, 0xF0, 0x400, rng.getrandbits(16)]))
    coords = [rng.choice([-0x8000, -300, -1, 0, 1, 100, 0x1000, 0x2000, 0x3000, 0x7000, rng.randrange(-0x6000, 0x6000)]) & 0xFFFFFFFF for _ in range(4)]
    if rng.random() < 0.2:
        coords[2], coords[3] = coords[0], coords[1]
    return [*coords, word(rng)], img.pokes()


@wtk(0x004BE0B0, "TrickObject_DrawBox")
def gen_draw_box(env, n):
    img, rng = Image(), env.rng
    obj = env.slab + ITEMS
    img.put(obj, noise(rng, 0x40))
    region = rng.choice([0, 1, 2])
    img.u8(obj + 0x1F, region)
    index = rng.choice([0, 1, 2, 3])
    img.u16(obj + 0x1A, index)
    img.u32(REGION_TABLE + 4 * 17 * region, env.slab + MODEL_TABLE)
    for k in range(4):
        img.u32(env.slab + MODEL_TABLE + 4 * k, env.slab + MODEL)
    img.put(env.slab + MODEL, noise(rng, 0x20))
    img.put(LINE, noise(rng, 0xC))
    img.u32(LINE + 8, rng.choice([0x1000, 0x2000, 0x100]))
    img.u32(LINE, 0)
    img.u32(LINE + 4, 0)
    prim = env.slab + PRIMS
    img.u32(CURSOR, prim)
    img.u32(LIMIT, prim + 0x100)
    img.u32(CURSOR + 4, env.slab + DISPLAY)
    img.u32(env.slab + DISPLAY + 0x84, env.slab + OT_CELL)
    img.put(env.slab + OT_CELL, noise(rng, 8))
    img.u32(SCREEN, 0x200)
    img.u32(SCREEN + 4, 0xF0)
    return [obj, word(rng)], img.pokes()


@wtk(0x004BE340, "TrickObject_EmptyHook")
def gen_hook(env, n):
    return [], Image().pokes()


@wtk(0x004BE350, "TrickObject_OfferAll")
def gen_offer_all(env, n):
    img, rng = Image(), env.rng
    kinds, checks = put_records(img, env, rng)
    put_objects(img, env, rng, kinds, checks)
    put_items(img, env, rng)
    put_globals(img, env, rng)
    put_skater(img, env, rng)
    img.u32(ENVIRONMENT, env.slab + ITEMS if rng.random() < 0.8 else 0)
    return [], img.pokes()
