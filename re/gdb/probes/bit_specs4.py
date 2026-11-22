"""Phase 4.15d probe: the shatter and the effect object it spawns (group bsh: 0x004add10..0x004af660).

An item is a fake object in the slab (flags, position, angles, model index, region); its model is a fake record in the slab: a header, vertices (8 bytes: x, y, z, pad), normals and faces
(a dword of flags and size, four vertex indices, a packed colour, a normal index, the second flags, a pointer to a record of texture coordinates). The region tables 0x0056d43c / 0x0056d450 point at
the model table and a colour table in the slab. The zone query is a stub that fills three scripted fields.

Slab layout (offsets): item 0x000 (0x100), record 0x100 (0x40), tables 0x180 (model table 0x00: 4 entries, colour table 0x20: 256 dwords), model 0x600, bits 0x1000 (16 of 0x100), arena 0x2000
(0x2000 bytes), scratch 0x4100.
"""

from __future__ import annotations

import struct

from bit_specs import (BIT_STRIDE, FAKE_VTABLE, GLOBALS, LISTS, SCRATCH, STATIC, bit, chain_with, counters, install_stubs as install_base_stubs, merged, noise, put_anim, word)
from bit_specs2 import scattered, vector
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Skip
from frontend_specs_front import ensure
from frontend_specs_menu import Image

GROUPS = ("bsh",)

ITEM = 0x000
RECORD = 0x100
TABLES = 0x180
COLOURS = TABLES + 0x20
MODEL = 0x600
BITS4 = 0x1000
ARENA4 = 0x2000
ARENA4_BYTES = 0x2000
SLAB4 = ARENA4 + ARENA4_BYTES + 0x100
INFO = 0x7FF00800 + 0x500          # (collision::kInfoScratch)

SHATTER_WORLD = [(0x0056C510, 0x40), (0x0056A98C, 4), (0x0056E770, 4), (0x005244CC, 4), (0x0055FA3C, 4), (0x0055F7CC, 0xC), (0x0056D43C, 0x5C), (0x006A3E00, 0x120), (0x0055F6C0, 0x74),
                 (0x00561C18, 4)]
STATE = {"hit": 0, "slope": 0, "hity": 0}


def regions(slab):
    return merged([(slab, SLAB4)] + SHATTER_WORLD, gap=0)


def seed(env, rng):
    env.alloc_base = ARENA4
    env.alloc_size = ARENA4_BYTES
    return {env.slab + FAKE_VTABLE: struct.pack("<4I", 0x004AD950, 0x004AD9A0, 0x004AD9A0, 0x004AD9A0)}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB4 + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = []
    SEEDS[_group] = seed


def install_stubs(fc, env):
    if getattr(fc, "_bit4_installed", False):
        return
    fc._bit4_installed = True
    install_base_stubs(fc, env)
    ensure(fc, 0x004AD620, 3, "engine_004ad620")                                                          # SFX_PlayX
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=INFO)                                          # M3D_InitCollisionQuery
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=INFO,
           pokes=[(0, 4, lambda a: STATE["hit"], 0x68), (0, 2, lambda a: STATE["slope"], 0x7A), (0, 4, lambda a: STATE["hity"], 0x70)])       # M3D_QueryZoneCollision


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def sh(address, name, this=None):
    resolve = this if callable(this) else ((lambda env: env.slab + this) if this is not None else None)
    return bit(address, name, "bsh", resolve)


def slot(env, index):
    return env.slab + BITS4 + BIT_STRIDE * index


def bit_slot(img, env, rng, index, size=0xC4, protected=None):
    at = slot(env, index)
    img.put(at, noise(rng, size))
    img.u32(at, env.slab + FAKE_VTABLE)
    img.u8(at + 0x3A, rng.choice([0, 0, 1]) if protected is None else protected)
    img.u8(at + 0x37, rng.choice([0, 0, 1]))
    return at


def chain_in(img, env, rng, list_name, size=0xC4, first=0):
    """The bit in slot `first` inside a chain of the list."""
    others = rng.choice([0, 0, 1, 2])
    order = list(range(first, first + others + 1))
    rng.shuffle(order)
    at = {k: bit_slot(img, env, rng, k, size) for k in order}
    for i, k in enumerate(order):
        img.u32(at[k] + 4, at[order[i - 1]] if i > 0 else 0)
        img.u32(at[k] + 8, at[order[i + 1]] if i + 1 < len(order) else 0)
    img.u32(LISTS[list_name], at[order[0]])
    return slot(env, first)


def fake_model(img, env, rng, faces=None, tri_bias=0.5, vertices=None):
    """Item (region 0 or 1) and its model; returns (item, face addresses)."""
    item = env.slab + ITEM
    region = rng.choice([0, 1])
    img.put(item, noise(rng, 0x40))
    img.u8(item + 0x1F, region)
    img.u16(item + 0x1A, rng.randrange(0, 4))
    img.u8(item + 4, rng.getrandbits(8))
    vector(img, rng, item + 8, -0x100000, 0x100000)
    for k in range(3):
        img.u16(item + 0x14 + 2 * k, rng.choice([0, rng.randrange(0, 0x1000), rng.getrandbits(16)]))
    vertex_count = vertices or rng.choice([4, 4, 8])
    normal_count = rng.choice([1, 2, 4])
    face_count = rng.choice([1, 2, 3, 4]) if faces is None else faces
    table = env.slab + TABLES + 0x44 * 0       # (the region tables are 0x44 apart: the model table of region r is at 0x56d43c + 0x44 * r)
    for r in (0, 1):
        img.u32(0x0056D43C + 0x44 * r, env.slab + TABLES)
        img.u32(0x0056D450 + 0x44 * r, env.slab + COLOURS)
    for k in range(4):
        img.u32(env.slab + TABLES + 4 * k, env.slab + MODEL)
    img.put(env.slab + COLOURS, noise(rng, 0x400))
    model = env.slab + MODEL
    img.put(model, noise(rng, 0x1C))
    img.u16(model + 2, vertex_count)
    img.u16(model + 4, normal_count)
    img.u16(model + 6, face_count)
    for k in range(vertex_count):
        for j in range(3):
            img.u16(model + 0x1C + 8 * k + 2 * j, rng.choice([rng.randrange(-0x400, 0x400), rng.getrandbits(16)]))
    for k in range(normal_count):
        for j in range(3):
            img.u16(model + 0x1C + 8 * vertex_count + 8 * k + 2 * j, rng.choice([rng.randrange(-0x1000, 0x1000), rng.getrandbits(16)]))
    at = model + 0x1C + 8 * (vertex_count + normal_count)
    addresses = []
    for k in range(face_count):
        triangle = rng.random() < tri_bias
        flags = rng.choice([0x0, 0x1, 0x1, 0x40, 0x41, 0x20, 0x21, 0x800, 0x801, 0x861, rng.getrandbits(12)]) | (0x10 if triangle else 0)
        flags &= 0xFFFF
        size_words = 0x10
        img.put(at, noise(rng, size_words * 4))
        img.u32(at, flags | (size_words << 18))
        for j in range(4):
            img.u8(at + 4 + j, rng.randrange(0, vertex_count) if rng.random() < 0.95 else rng.getrandbits(8) % 0x20)
        img.u16(at + 0xC, rng.randrange(0, normal_count) << 3)
        img.u16(at + 0xE, rng.choice([0, 8, 9, 1, 0xFFFF, rng.getrandbits(16)]))
        img.u32(at + 0x10, env.slab + RECORD)
        addresses.append(at)
        at += size_words * 4
    img.put(env.slab + RECORD, noise(rng, 0x20))
    return item, addresses


def world(img, env, rng):
    img.u32(0x0056A98C, rng.choice([0, 0, 0, 0, 1]))
    img.u32(0x0056E770, rng.choice([0, 0, 0, 1]))
    img.u32(0x005244CC, word(rng))
    img.u32(0x0056C544, rng.choice([0, 1]))
    img.put(0x0056C510, noise(rng, 0x40))
    img.u32(0x0055FA3C, 0)
    vector(img, rng, 0x0055F7CC, -0x1000, 0x1000)
    img.put(0x006A3E00, noise(rng, 0x120))
    img.u32(0x0055F6C0 + 0x10, 0)       # (the chunk and glass lists start empty: the pieces link into them)
    counters_for(img, rng)
    STATE["hit"] = rng.choice([0, 0, env_body, env_body])
    STATE["slope"] = rng.getrandbits(16)
    STATE["hity"] = rng.choice([rng.randrange(-0x400000, 0x400000), 0x100000, 0x7FFFFFFF]) & 0xFFFFFFFF


env_body = 0x00123456


def counters_for(img, rng):
    img.u32(0x0055F72C, rng.choice([0, 1, 5, 100]))
    img.u32(0x0055F728, rng.choice([0, 0x100, 0x1000]))


# ---- the emitter -------------------------------------------------------------------------------------------------------------------------------------------------------------------

def emitter_state(img, env, rng, with_trail=True):
    at = bit_slot(img, env, rng, 0, 0xC4, protected=0)
    chain_here = chain_in(img, env, rng, "chunk", 0xC4, first=0)
    trail = 0
    if with_trail and rng.random() < 0.6:
        # a fake ribbon (the slots after the emitter): count, ring, bit array
        trail = bit_slot(img, env, rng, 6, 0x60)
        bits = rng.choice([1, 2, 3])
        points = 2
        total = (points - 1) * bits + 1
        ring = env.slab + ARENA4 + 0x1000
        array = env.slab + ARENA4 + 0x1400
        img.put(ring, noise(rng, 12 * total))
        img.u32(trail + 0x3C, bits)
        img.u32(trail + 0x40, points - 1)
        img.u32(trail + 0x44, total)
        img.u32(trail + 0x48, rng.randrange(0, total))
        img.u32(trail + 0x4C, ring)
        img.u32(trail + 0x50, array)
        for k in range(bits):
            piece = bit_slot(img, env, rng, 7 + k, 0x70, protected=1)
            img.u32(array + 4 * k, piece)
    img.u32(at + 0xC0, trail)
    for k in range(3):
        img.u16(at + 0x8C + 2 * k, rng.choice([0, rng.getrandbits(16)]))
        img.u16(at + 0xB8 + 2 * k, rng.choice([0, rng.randrange(0, 0x100), rng.getrandbits(16)]))
        img.u16(at + 0x3C + 2 * k, rng.getrandbits(16))
    vector(img, rng, at + 0x10, -0x100000, 0x100000)
    vector(img, rng, at + 0x1C, -0x100000, 0x100000)
    return at


@sh(0x004ADD10, "Emitter_Construct", lambda env: slot(env, 0))
def gen_emitter_construct(env, n):
    img, rng = Image(), env.rng
    world(img, env, rng)
    bit_slot(img, env, rng, 0, 0xC4)
    img.put(slot(env, 0) + 0xB8, bytes(12))      # (the class new zeroes the block: the trail pointer must start null)
    img.u32(LISTS["chunk"], 0)
    img.u32(LISTS["nonrendered"], 0)
    corners = env.slab + 0x4000
    for k in range(9):
        img.u16(corners + 2 * k, rng.choice([rng.randrange(-0x200, 0x200), rng.getrandbits(16)]))
    img.u16(corners + 0x20, 0)
    centre = env.slab + 0x4040
    for k in range(3):
        img.u32(centre + 4 * k, scattered(rng, -0x100000, 0x100000))
    img.put(0x0056C520, noise(rng, 12))
    if rng.random() < 0.15:
        for k in range(3):
            img.u32(0x0056C520 + 4 * k, img.b.get(centre + 4 * k, 0) | (img.b.get(centre + 4 * k + 1, 0) << 8) | (img.b.get(centre + 4 * k + 2, 0) << 16) | (img.b.get(centre + 4 * k + 3, 0) << 24))
    return [corners, corners + 6, corners + 12, centre, 0], img.pokes()


@sh(0x004ADE60, "Emitter_ScalarDelete", lambda env: slot(env, 0))
def gen_emitter_sdelete(env, n):
    img, rng = Image(), env.rng
    emitter_state(img, env, rng)
    counters_for(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@sh(0x004ADE80, "Emitter_Destruct", lambda env: slot(env, 0))
def gen_emitter_destruct(env, n):
    img, rng = Image(), env.rng
    emitter_state(img, env, rng)
    counters_for(img, rng)
    return [], img.pokes()


@sh(0x004ADEE0, "Emitter_SetVelocity", lambda env: slot(env, 0))
def gen_emitter_velocity(env, n):
    img, rng = Image(), env.rng
    emitter_state(img, env, rng)
    img.put(0x0056C510, noise(rng, 0x20))
    origin = env.slab + 0x4040
    position = env.slab + 0x4050
    vector(img, rng, position, -0x100000, 0x100000)
    for k in range(3):
        img.u32(0x0056C520 + 4 * k, scattered(rng, -0x100000, 0x100000))
    if rng.random() < 0.12:
        for k in range(3):
            img.u32(0x0056C520 + 4 * k, 0)
        for k in range(3):
            img.u32(position + 4 * k, 0)
    return [position], img.pokes()


@sh(0x004AE020, "Emitter_Move", lambda env: slot(env, 0))
def gen_emitter_move(env, n):
    img, rng = Image(), env.rng
    emitter_state(img, env, rng)
    img.u16(slot(env, 0) + 0xE, rng.choice([0, 0, 1, 2, 30, 0xFFFF]))
    return [], img.pokes()


# ---- the shatter ------------------------------------------------------------------------------------------------------------------------------------------------------------------

@sh(0x004AEEF0, "Shatter_Corner")
def gen_corner(env, n):
    img, rng = Image(), env.rng
    item, faces = fake_model(img, env, rng, faces=1)
    img.put(0x006A3E00, noise(rng, 0x120))
    out = env.slab + 0x4000
    img.put(out, noise(rng, 12))
    face = faces[0]
    model = env.slab + MODEL
    return [out, model + 0x1C, face + 4, rng.randrange(0, 4)], img.pokes()


@sh(0x004AEF70, "Shatter_Colour")
def gen_colour(env, n):
    img, rng = Image(), env.rng
    fake_model(img, env, rng, faces=1)
    img.put(0x0056C510, noise(rng, 0x40))
    return [rng.choice([0, 0, 0x800, 1, 0x1000]), word(rng), rng.choice([3, 4, 3, 4, 5]), env.slab + COLOURS], img.pokes()


@sh(0x004AF1C0, "Shatter_Glass")
def gen_glass(env, n):
    img, rng = Image(), env.rng
    fake_model(img, env, rng, faces=1)
    world(img, env, rng)
    img.u32(LISTS["glass"], 0)
    img.u32(0x0056C544, rng.choice([0, 0, 1, 1, 5]))      # (the world's noise leaves the burst-seen flag set: the first burst of a level is a case of its own)
    base = env.slab + 0x4000
    for k in range(9):
        img.u32(base + 4 * k, scattered(rng, -0x200000, 0x200000))
    wind = env.slab + 0x4060
    for k in range(3):
        img.u32(wind + 4 * k, rng.choice([rng.randrange(-0x100, 0x100), 0, rng.randrange(-4, 5)]) & 0xFFFFFFFF)
    return [rng.choice([0, 1, 2, 5, 0xF, 0x1E]), base, base + 12, base + 24, wind, word(rng), word(rng), word(rng)], img.pokes()


@sh(0x004AF630, "Shatter_BurstSound", None)
def gen_burst_sound(env, n):
    img, rng = Image(), env.rng
    img.u32(0x0056C544, rng.choice([0, 1, 2]))
    return [], img.pokes()


@sh(0x004AF190, "Shatter_ForgetBurstThunk", None)
def gen_forget_thunk(env, n):
    img, rng = Image(), env.rng
    img.put(0x0056C530, noise(rng, 12))
    return [], img.pokes()


@sh(0x004AF1A0, "Shatter_ForgetBurst", None)
def gen_forget(env, n):
    return gen_forget_thunk(env, n)


@sh(0x004AE730, "Shatter_Triangle")
def gen_triangle(env, n):
    img, rng = Image(), env.rng
    fake_model(img, env, rng, faces=1)
    world(img, env, rng)
    base = env.slab + 0x4000
    for k in range(9):
        img.u32(base + 4 * k, scattered(rng, -0x100000, 0x100000))
    img.u32(LISTS["chunk"], 0)
    img.u32(LISTS["nonrendered"], 0)
    img.u32(LISTS["glass"], 0)
    uv = [rng.randrange(0, 256) for _ in range(6)]
    return [base, base + 12, base + 24, *uv, rng.choice([0, 0x1000, word(rng)]), rng.choice([0, 0, 1, 1, 2]), env.slab + RECORD], img.pokes()


@sh(0x004AE090, "Shatter_Face")
def gen_face(env, n):
    img, rng = Image(), env.rng
    item, faces = fake_model(img, env, rng, faces=1)
    world(img, env, rng)
    img.u32(LISTS["chunk"], 0)
    img.u32(LISTS["nonrendered"], 0)
    img.u32(LISTS["glass"], 0)
    quota = rng.choice([0, 1, 1, env.slab + RECORD])
    return [item if rng.random() < 0.97 else 0, faces[0], rng.choice([0, 0, 1, 2]), 0, rng.choice([0, 1, 1]), rng.choice([0, 1]), quota], img.pokes()


@sh(0x004AF0B0, "Shatter_Item")
def gen_item(env, n):
    img, rng = Image(), env.rng
    item, faces = fake_model(img, env, rng, faces=None)
    world(img, env, rng)
    img.u32(LISTS["chunk"], 0)
    img.u32(LISTS["nonrendered"], 0)
    img.u32(LISTS["glass"], 0)
    return [item, rng.choice([0, 1, 1]), rng.choice([0, 1])], img.pokes()
