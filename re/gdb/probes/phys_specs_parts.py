"""Phase 4.15a probe: the model parts of a skater (physics.cpp, group php): the part lookup (0x0048fbe0), the vertex shift of three parts (0x0048fc90) and the fattening of the parts (0x0048fd60).

Like phys_specs.py: the live player-one skater (all of it captured) and a scratch slab; the model table of a region (`PsxModelTablesByRegion` 0x0056d43c, 0x44 bytes a region: the table pointer at +0, two
state bytes at +0x2a and +0x2b) is redirected into the slab, the selection records (0x0053a514, 0x170 bytes a character) and the two player pointers are windows, and the three fattening tables (0x00536950..)
are captured read-only. The log (`printf_log`) and the assertion routine are the shared stubs.

Slab layout (offsets): the model table of the case at 0x000 (the count at +0 is read as the word before the table, so the table is at 0x004, pointers to eight models), the models at 0x100 + 0x80 * k (the vertex
count at +2, the vertices from +0x1c: eight bytes each).
"""

from __future__ import annotations

import phys_specs as ps
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import merged
from frontend_specs_menu import Image

GROUPS = ("php",)

SLAB = 0x500
TABLE_COUNT = 0x000
TABLE = 0x004
MODELS = 0x100
MODEL_STRIDE = 0x80
MODEL_SLOTS = 8

MODEL_TABLES = 0x0056D43C
REGION_STRIDE = 0x44
REGIONS = 3
PLAYER_ONE = 0x0056A858
PLAYER_TWO = 0x0056A85C
SELECTION = 0x0053A514
SELECTION_STRIDE = 0x170
FATTEN_TABLES = 0x00536950
FATTEN_TABLES_BYTES = 0x100

WINDOWS = [(MODEL_TABLES, REGION_STRIDE * REGIONS), (PLAYER_ONE, 8), (SELECTION, SELECTION_STRIDE * 3)]


def php_regions(slab):
    return merged([(slab, SLAB), (ps.skater(), ps.SKATER_BYTES)] + WINDOWS + [(FATTEN_TABLES, FATTEN_TABLES_BYTES)], gap=0)


def php_seed(env, rng):
    """The fixed content: the table pointers of the models (model k is at MODELS + MODEL_STRIDE * k)."""
    return {env.slab + TABLE: b"".join((env.slab + MODELS + MODEL_STRIDE * k).to_bytes(4, "little") for k in range(MODEL_SLOTS))}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = php_regions
    READ_ONLY[_group] = [(FATTEN_TABLES, FATTEN_TABLES_BYTES)]
    SEEDS[_group] = php_seed


def php(address, name):
    return spec(address, name, "php", this=lambda env: ps.skater())


def changed(env, img):
    """The pokes of an image, without the bytes the base already holds (a poke is the slow part of a case)."""
    from rail_specs import base_byte

    out = Image()
    for address, value in img.b.items():
        if base_byte(env, address) != value:
            out.b[address] = value
    return out.pokes()


def world(img, env, rng, *, parts=None):
    """The model table of a region redirected into the slab with a few models; the skater's region and its part order. Returns the region and the model count."""
    sk = ps.skater()
    slab = env.slab
    region = rng.randrange(0, REGIONS)
    img.u8(sk + 0x1F, region)
    count = rng.choice([0, 1, 2, 3, 5, 8, 8])
    img.u32(MODEL_TABLES + REGION_STRIDE * region, slab + TABLE)
    img.u32(slab + TABLE_COUNT, count)
    for k in range(MODEL_SLOTS):
        model = slab + MODELS + MODEL_STRIDE * k
        vertices = rng.choice([0, 0, 1, 2, 3, 5, 6])
        img.u16(model + 2, vertices)
        for v in range(6):
            base = model + 0x1C + 8 * v
            for field in (0, 2, 4):
                img.u16(base + field, ps.short(rng) & 0xFFFF)
            img.u8(base + 6, rng.choice([0, 0, 1, 2, 0x10, 0x12, 0x11, 4, rng.getrandbits(8)]))
    order = parts if parts is not None else [rng.choice([0, 1, 2, 3, 0xF, 0xF, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, rng.randrange(0, 20)]) for _ in range(0x13)]
    for k, part in enumerate(order):
        img.u8(sk + 0x150 + k, part)
    return region, count


@php(0x0048FBE0, "Skater_FindPartIndex")
def gen_find_part(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    order = [rng.choice([0, 1, 2, 3, 5, 7, 0xF, 10, rng.randrange(0, 0x20), rng.getrandbits(8)]) for _ in range(0x13)]
    for k, part in enumerate(order):
        img.u8(sk + 0x150 + k, part)
    img.u32(sk + 0x14C, rng.choice([0, 1, 1, 1, 5]))
    part = rng.choice([order[rng.randrange(0, 0x13)], order[rng.randrange(0, 0x13)], rng.randrange(0, 0x20), rng.getrandbits(8), 0x100 + rng.randrange(0, 0x100), rng.getrandbits(32)])
    return [part], changed(env, img)


@php(0x0048FC90, "Skater_OffsetPartVertices")
def gen_offset(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    region, count = world(img, env, rng)
    current = rng.choice([0, 1, 5, 0x80, 0xFF, rng.getrandbits(8)])
    img.u8(MODEL_TABLES + REGION_STRIDE * region + 0x2A, current)
    for field in (0x2CD0, 0x2CD4, 0x2CD8):
        img.u32(sk + field, rng.choice([0, 1, 2, 3, 4, 7, 8, 0xFFFFFFFF, rng.randrange(0, 8)]))
    amount = rng.choice([current, current, current + 1, current - 1, rng.getrandbits(8), 0, 0xFF, rng.getrandbits(32), 0x100 + current, rng.randrange(0, 300)]) & 0xFFFFFFFF
    return [amount], changed(env, img)


@php(0x0048FD60, "Skater_Fatten")
def gen_fatten(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    region, count = world(img, env, rng)
    img.u8(MODEL_TABLES + REGION_STRIDE * region + 0x2B, rng.choice([0, 0, 1, 2, 3, rng.getrandbits(8)]))
    # the two player pointers: this skater, nothing, or something else; the selection records say whether the skater is kept as it is
    one = rng.choice([sk, sk, 0, sk + 0x100, 0x1234])
    two = rng.choice([sk, 0, sk + 0x100, 0x5678, sk])
    img.u32(PLAYER_ONE, one)
    img.u32(PLAYER_TWO, two)
    img.u32(sk + 0x2CC0, rng.randrange(0, 3))
    for k in range(3):
        img.u32(SELECTION + SELECTION_STRIDE * k, rng.choice([0, 0x100, 0x1FF, 0xFF, rng.getrandbits(32), 0x100]))
    amount = rng.choice([0x78, 0xFA, 100, 101, 99, 150, 200, 0, 50, rng.randrange(0, 400), -rng.randrange(1, 300) & 0xFFFFFFFF, rng.getrandbits(32)]) & 0xFFFFFFFF
    return [amount, rng.choice([0, 0, 1]), rng.choice([0, 1, 2, 3, rng.getrandbits(8), rng.getrandbits(32)])], changed(env, img)
