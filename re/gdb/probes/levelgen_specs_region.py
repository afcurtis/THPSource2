"""Phase 4.11c case generators, part 5: the region (levelgen.cpp 0x0043bf80, 0x0043d400..0x0043e980, 0x0043f540..0x0043f710): groups `lgz` (zone packet, fog, region flags), `lgr`
(the region builder), `lgt` (the library load, the rails and the trigger nodes).

The PSX region tables (0x0056d438: 0x44 bytes per slot; +0 the object records, +4 the models, +8 the raw region) are the game's own, poked per case to point into the slab; the
region spooler's calls (FindTrailingTables, QueueResource, ...) are stubbed with scripted results.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Skip, spec
from gen_specs_model import CELL_X, CELL_Z, GRID_D, GRID_W, RING_USED, ROW_SET
from levelgen_specs import (ARENA, BLOCK_OM, BUILT_BASE, BUILT_STRIDE, CLOCK, GAP_BASE, GAP_STRIDE, ITEM_BASE, LEVEL_STATE, LG, LG_READS, LG_SIZE, LG_STATICS, LIVE_COUNT,
                            MODEL_BASE, MODEL_STRIDE, OBJECT_TABLES, PIECE_BASE, PIECE_STRIDE, REGION_NAMES, REGION_STATICS, RING, SLOT, START_BASE, START_STRIDE, THEME, World,
                            lg_regions, lg_stubs, put_gaps, put_starts, ring, window)
from levelgen_specs_pieces import models_that_fit, place_piece_records
from pspool_specs import Ov, ensure, under_test

ZONE_ROW_BYTES = 0x2418
ZONE_TABLE = 0x2D1E0
SCRATCH_NAME = 0x7FF00800
SCRATCH_LIBRARY = 0x7FF00840
SCRATCH_POSITION = 0x7FF00880

# region slots used by the tests: the source library, the second ("world") region, and the slot the park takes
SRC_SLOT, SECOND_SLOT = 3, 4
# the area for region data (above the arena of the other groups): the fake regions and their tables
REG_BASE = 0x13000
Z_FAKE, Z_FAKE_SIZE = REG_BASE, 0x1000              # fake regions, object tables and model tables
Z_ROWS = REG_BASE + 0x1000                          # the row table of the zone counts (0x50)
Z_DEST, Z_DEST_SIZE = REG_BASE + 0x1100, 0x8000     # the zone packet
ZA = REG_BASE + 0x9100                              # the zone counts (rows of 20 cells of 0x1ce bytes) or the arena that makes them


def slot_entry(slot):
    return OBJECT_TABLES + 0x44 * slot


# ---- lgz ----------------------------------------------------------------------------------------------------------------------

Z_ARENA = ZONE_TABLE + 0x400
Z_TOTAL = ZA + Z_ARENA


def lgz_regions(slab):
    return (lg_regions(slab, (LG, LG_SIZE), (RING, 0x400), (PIECE_BASE, 0x1200), (GAP_BASE, 0x600), (START_BASE, 0x100), (Z_FAKE, Z_FAKE_SIZE + 0x100), (Z_DEST, Z_DEST_SIZE),
                       (ZA, Z_ARENA)) + REGION_STATICS)


def lgz_seed(env, rng):
    env.alloc_base = ZA
    env.alloc_size = Z_ARENA
    return {}


SLAB_BYTES["lgz"] = Z_TOTAL + 0x100
SEEDS["lgz"] = lgz_seed
EXTRA_REGIONS["lgz"] = lgz_regions
READ_ONLY["lgz"] = LG_READS


def region_world(env, with_zone=False):
    rng = env.rng
    w = World(env, width=rng.choice([4, 6, 8, 16]), depth=rng.choice([4, 6, 8, 16]))
    w.object()
    w.maps(floor=False, out=False, flr_map=False)
    ring(w)
    return w


@spec(0x0043F710, "LevelGen_ZoneRoom", "lgz")
def gen_zone_room(env, n):
    ov = Ov()
    ov.u32(LIVE_COUNT, env.rng.choice([0, 1, 0xF9F, 0xFA0, 0xFA1, 0x2000, 0xFFFFFFFF, 0x7FFFFFFF, env.rng.randrange(1 << 32)]))
    return [], ov.pokes()


@spec(0x0043F5E0, "LevelGen_SetFog", "lgz", this=lambda env: env.slab + LG)
def gen_set_fog(env, n):
    w = region_world(env)
    rng = w.rng
    w.ov.u32(LEVEL_STATE, rng.choice([4, 5, 7, 8, 9, 0, 1, 2, 3, 6, 10]))
    w.ov.u8(w.lg + 0x3744, rng.choice([0, 1]))
    w.ov.u32(0x00563A60, rng.randrange(1 << 32))
    w.ov.u32(0x0056114C, rng.randrange(1 << 32))
    return [rng.choice([0, 1, 0x100, 0xFF])], w.ov.pokes()


@spec(0x0043F6C0, "LevelGen_DimFog", "lgz", this=lambda env: env.slab + LG)
def gen_dim_fog(env, n):
    w = region_world(env)
    rng = w.rng
    a = rng.choice([0x40, 0x100, 0x100000, 0x20, 0])
    w.ov.u32(0x00568628, a)
    w.ov.u32(0x00560FD0, rng.choice([(a - 0x40) & 0xFFFFFFFF, (a - 0x3F) & 0xFFFFFFFF, (a - 0x41) & 0xFFFFFFFF, rng.randrange(1 << 32)]))
    w.ov.u32(w.lg + 0x373C, rng.choice([0, 100, 1000, 0x7FFFFFFF, 0xFFFFFFFF, rng.randrange(1 << 32), 3333, 12345]))
    w.ov.u32(w.lg + 0x3740, rng.randrange(1 << 32))
    return [], w.ov.pokes()


@spec(0x0043BF80, "LevelGen_UnloadOutPsx", "lgz")
def gen_unload_out_psx(env, n):
    ov = Ov()
    rng = env.rng
    slot = rng.choice([0xFFFFFFFF, 0, 5, 19])
    ov.u32(SLOT, slot)
    if slot != 0xFFFFFFFF:
        ov.u32(slot_entry(slot) + 0x18, rng.choice([0, 0, rng.randrange(1, 1 << 32)]))
    ov.u32(0x00563A58, rng.randrange(1 << 32))
    return [rng.choice([0, 1, 1, 0x100])], ov.pokes()


@spec(0x0043E080, "PSX_DestroyObjectRecordArray", "lgz", this=lambda env: env.slab + REG_BASE + 0x104)
def gen_destroy_array(env, n):
    ov = Ov()
    rng = env.rng
    base = env.slab + REG_BASE + 0x100
    count = rng.randrange(0, 6)
    ov.u32(base, count)
    ov.put(base + 4, rng.randbytes(0x4C * 6))
    return [rng.choice([0, 1, 2, 3, 0x103])], ov.pokes()


@spec(0x0043F540, "LevelGen_SetFloorFlags", "lgz", this=lambda env: env.slab + LG)
def gen_floor_flags(env, n):
    w = region_world(env)
    rng = w.rng
    slot = rng.choice([2, 5, 9])
    w.ov.u32(SLOT, slot)
    total = rng.randrange(1, 8)
    region = w.a(REG_BASE)
    w.ov.u32(slot_entry(slot) + 8, region)
    w.ov.put(region, rng.randbytes(0x400))
    w.ov.u32(w.lg + 0x2354, 0)
    first = rng.randrange(0, 3)
    count = rng.randrange(0, 4)
    w.ov.u32(w.lg + 0x2358, first)
    w.ov.u32(w.lg + 0x2384, count)
    # the table of object pointers at region + total*0x24 + 0x10 (here total = 0): records in the slab
    table = region + 0x10
    for i in range(8):
        obj = w.a(REG_BASE + 0x200 + 0x40 * i)
        w.ov.put(obj, rng.randbytes(0x20))
        w.ov.u32(table + 4 * i, obj)
    return [rng.choice([0, 1, 0x100, 0xFF])], w.ov.pokes()


# ---- rasterise: a box on the zone grid ---------------------------------------------------------------------------------------------

def zone_table(w, rows_at):
    """The zone grid's counts: a row table at LG+0x23b4 pointing at 20 rows of 20 cells of 0x1ce bytes in the arena."""
    w.ov.u32(w.lg + 0x23B4, rows_at)


@spec(0x0043E0E0, "LevelGen_Rasterise", "lgz", this=lambda env: env.slab + LG)
def gen_rasterise(env, n):
    w = region_world(env)
    rng = w.rng
    base = w.a(ZA)
    for r in range(20):
        w.ov.u32(w.a(Z_ROWS) + 4 * r, base + r * ZONE_ROW_BYTES)
    w.ov.u32(w.lg + 0x23B4, w.a(Z_ROWS))
    extent = rng.choice([0x154 * 8, 0x154 * 16, 0x1000, 0x2A80, 0x5400])
    # a small box near the origin: the cells it touches are among the first columns of the top rows
    step = extent // 20
    x = rng.randrange(0, 3 * step)
    z = rng.randrange(0, 3 * step)
    ww = rng.randrange(0, 2 * step)
    dd = rng.randrange(0, 2 * step)
    build = rng.choice([0, 1, 1, 1, 0x100])
    if build & 0xFF:
        # the counters of the cells the box can touch start small (one of them near the cap)
        for r in range(15, 20):
            for c in range(0, 7):
                cell = base + r * ZONE_ROW_BYTES + c * 0x1CE
                w.ov.u8(cell, rng.choice([0, 0, 1, 5, 0xE5, 0xE6]))
    return [extent, x, z, ww, dd, rng.randrange(0, 0x10000) | rng.choice([0, 0x20000]), build], w.ov.pokes()


# the fake regions (offsets from Z_FAKE): R1 the source library's raw region, O1 its object records, T1 its trailing table, R2/O2/T2 the second ("world") region, NODES the
# trigger file's node data, TARGETS the objects the trailing tables point at, NAME a library name
R1, O1, T1, R2, O2, T2, NODES, TARGETS, NAME = 0x000, 0x200, 0x400, 0x500, 0x800, 0xA00, 0xB00, 0xD00, 0xFC0


def source_region(w, count, slot=SRC_SLOT):
    """The source library: its raw region (the model count, then after 0x10 + n * 0x24 the model table), the object records (positions in 4096ths) and a trailing table
    of model keys; returns the table's address."""
    rng, ov = w.rng, w.ov
    raw = w.a(Z_FAKE + R1)
    objects = w.a(Z_FAKE + O1)
    ov.u32(slot_entry(slot) + 8, raw)
    ov.u32(slot_entry(slot), objects)
    ov.put(raw, rng.randbytes(0x80 + 0x24 * count))
    ov.u32(raw + 8, count)
    table = raw + 0x10 + count * 0x24
    ov.u32(table - 4, count)
    for j in range(count):
        ov.u32(table + 4 * j, w.models[j % len(w.models)]["model"])
        rec = objects + 0x4C * j
        ov.put(rec, rng.randbytes(0x4C))
        ov.u32(rec + 8, (rng.randrange(-8, 8) * 0x154) << 12 & 0xFFFFFFFF)
        ov.u32(rec + 0xC, (rng.randrange(-4, 4) * 0xAA) << 12 & 0xFFFFFFFF)
        ov.u32(rec + 0x10, (rng.randrange(-8, 8) * 0x154) << 12 & 0xFFFFFFFF)
    return raw


def second_region(w, models_count, slot=SECOND_SLOT):
    """The second ("world") region: its raw region (size at +4, model count at +8, the model table after 0x10 + n * 0x24 pointing at blobs behind it), its object records;
    returns n."""
    rng, ov = w.rng, w.ov
    n = models_count
    raw = w.a(Z_FAKE + R2)
    objects = w.a(Z_FAKE + O2)
    ov.u32(slot_entry(slot) + 8, raw)
    ov.u32(slot_entry(slot), objects)
    ov.u32(w.lg + 0x2380, slot)
    ov.u32(w.lg + 0x2384, n)
    ov.put(raw, rng.randbytes(0x40))
    ov.u32(raw + 8, n)
    table = raw + 0x10 + n * 0x24
    ov.u32(table - 4, n)
    blob = table + 4 * n + 4
    blob += (-blob) & 3
    total = 0
    for j in range(n):
        size = rng.choice([0x40, 0x44, 0x60])
        ov.put(blob, rng.randbytes(size))
        ov.u16(blob + 6, rng.randrange(0, 20))
        ov.u32(table + 4 * j, blob)
        blob += size
        rec = objects + 0x4C * j
        ov.put(rec, rng.randbytes(0x4C))
        ov.u32(rec + 8, (rng.randrange(-8, 8) * 0x154) << 12 & 0xFFFFFFFF)
        ov.u32(rec + 0x10, (rng.randrange(-8, 8) * 0x154) << 12 & 0xFFFFFFFF)
    ov.u32(raw + 4, blob - raw)
    return n


def trailing_table(w, address, models, entries, which=0):
    """A trailing table at `address`: the model keys, then (after the count) pointers to objects whose refcount (a 16-bit word at +0x10) the region builder bumps."""
    rng, ov = w.rng, w.ov
    for j in range(models):
        ov.u32(address + 4 * j, rng.randrange(1 << 32))
    ov.u32(address + 4 * models, entries)
    for k in range(entries):
        target = w.a(Z_FAKE + TARGETS + 0x100 * which + 0x20 * k)
        ov.put(target, rng.randbytes(0x20))
        ov.u16(target + 0x10, rng.choice([0, 1, 5, 0xFFFE, 0xFFFF]))
        ov.u32(address + 4 * models + 4 + 4 * k, target)


@spec(0x0043E2D0, "LevelGen_CreateZonePacket", "lgz", this=lambda env: env.slab + LG)
def gen_zone_packet(env, n):
    w = region_world(env)
    rng = w.rng
    w.make_models(rng.randrange(2, 5))
    ps = place_piece_records(w, rng.randrange(0, 9), on_map=False)
    cut = rng.randrange(0, len(ps) + 1)
    if cut:
        w.ov.u32(w.lg + 0x2344, ps[0]["at"])
        w.ov.u32(ps[cut - 1]["at"] + 0x230, 0)
    else:
        w.ov.u32(w.lg + 0x2344, 0)
    if cut < len(ps):
        w.ov.u32(w.lg + 0x234C, ps[cut]["at"])
    else:
        w.ov.u32(w.lg + 0x234C, 0)
    w.ov.u32(w.lg + 0x2358, len(ps))
    second_region(w, rng.randrange(0, 4))
    starts = put_starts(w, rng.randrange(0, 3))
    w.ov.u32(w.lg + 0x23A4, starts[0] if starts else 0)
    w.ov.u32(w.lg + 0x23A8, len(starts))
    for s in starts:
        w.ov.u32(s, rng.randrange(-6, 6) * w.ov_u32(CELL_X) & 0xFFFFFFFF)
        w.ov.u32(s + 4, rng.randrange(-6, 6) * w.ov_u32(CELL_X) & 0xFFFFFFFF)
    gaps = put_gaps(w, rng.randrange(0, 3))
    w.ov.u32(w.lg + 0x2388, gaps[0] if gaps else 0)
    for g in gaps:
        w.ov.u32(g + 0x20, rng.randrange(-4, 4) * 0x100 & 0xFFFFFFFF)
        w.ov.u32(g + 0x2C, rng.randrange(-4, 4) * 0x100 & 0xFFFFFFFF)
        w.ov.u32(g + 0x30, rng.randrange(0, 5) * 0x100)
        w.ov.u32(g + 0x34, rng.randrange(0, 5) * 0x100)
    dry = rng.choice([0, 1, 1])
    skip = rng.choice([0, 0, 1])
    dest = 0 if dry else w.a(Z_DEST)
    if not dry:
        w.ov.put(w.a(Z_DEST), rng.randbytes(0x100))
    return [dest, dry, skip], w.ov.pokes()


# ---- lgt: the library load, the variant, the rails ---------------------------------------------------------------------------------------------
from levelgen_specs import GAPBUF_BASE, ITEMS, ROWS, ROWS_SOURCE, SCRATCH_LIBRARY as _SL  # noqa: E402,F401
import gen_specs_poly  # noqa: E402

Z_POLY = REG_BASE + 0x1000                        # polygons, points and list holders of the rails
LGT_ARENA_BASE = REG_BASE + 0x2000
LGT_ARENA = 0x24000
ROTATIONS = (0x00529E18, 0x2A8)


def lgt_regions(slab):
    return (lg_regions(slab, (RING, 0x400), (PIECE_BASE, 0x1200), (GAP_BASE, 0x600), (START_BASE, 0x100), (Z_FAKE, Z_FAKE_SIZE), (Z_POLY, 0x1000), (LGT_ARENA_BASE, LGT_ARENA),
                       (SCRATCH_PAD, 0x100)) + REGION_STATICS + [ROTATIONS])


SCRATCH_PAD = REG_BASE + 0x2000 - 0x100


def lgt_seed(env, rng):
    env.alloc_base = LGT_ARENA_BASE
    env.alloc_size = LGT_ARENA
    return {}


SLAB_BYTES["lgt"] = LGT_ARENA_BASE + LGT_ARENA + 0x100
SEEDS["lgt"] = lgt_seed
EXTRA_REGIONS["lgt"] = lgt_regions
READ_ONLY["lgt"] = LG_READS


def lgt_world(env, models=None, version=None):
    rng = env.rng
    w = World(env, width=rng.choice([4, 6, 8]), depth=rng.choice([4, 6, 8]), version=version)
    w.object()
    w.maps()
    w.source()
    ring(w)
    w.make_models(models if models is not None else rng.randrange(1, 4))
    return w


def pool_clean(w):
    from gen_specs_model import POOL_INDEX, POOL_TABLE
    w.ov.u32(POOL_INDEX, 0)
    for k in range(100):
        w.ov.u32(POOL_TABLE + 4 * k, 0)


@spec(0x00439B70, "LevelGen_LoadModels", "lgt", this=lambda env: env.slab + LG)
def gen_load_models(env, n):
    w = lgt_world(env, models=env.rng.randrange(1, 4))
    rng = w.rng
    count = rng.randrange(0, len(w.models) + 1)
    source_region(w, count)
    env.lg = {"queue": [6, 7, SRC_SLOT]}
    pool_clean(w)
    w.ov.u32(w.lg + 0x14, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 3]))
    w.ov.u32(w.lg + 0x2380, rng.choice([0xFFFFFFFF, 4]))
    w.ov.u32(w.lg + 0x236C, rng.choice([0xFFFFFFFF, 6]))
    w.ov.u8(w.lg + 0x3744, rng.choice([0, 1]))
    w.ov.u8(0x0056B700, rng.randrange(2))
    w.ov.u32(0x0056E214, 0)
    name = w.a(Z_FAKE + NAME)
    w.ov.put(name, rng.choice([b"SkEd1\0", b"SkEd2\0", b"x\0", b"SkEd4\0"]) + bytes(6))
    return [name], w.ov.pokes()


@spec(0x0043A280, "LevelGen_SelectVariant", "lgt", this=lambda env: env.slab + LG)
def gen_select_variant(env, n):
    w = lgt_world(env, models=env.rng.randrange(1, 4), version=0)
    rng = w.rng
    count = rng.randrange(0, len(w.models) + 1)
    source_region(w, count)
    env.lg = {"queue": [6, 7, SRC_SLOT]}
    pool_clean(w)
    theme = w.ov_u32(THEME)
    version = w.ov_u32(ROW_SET)
    choice = rng.random()
    if choice < 0.3:
        args = [0xFFFFFFFF, 0xFFFFFFFF, rng.choice([0, 1])]
    elif choice < 0.5:
        args = [theme, version, rng.choice([0, 1])]
    elif choice < 0.8:
        args = [rng.choice([t for t in range(4) if t != theme]), rng.choice([0xFFFFFFFF, version]), rng.choice([0, 1])]
    else:
        args = [rng.choice([0xFFFFFFFF, theme]), 0 if version != 0 else 0xFFFFFFFF, rng.choice([0, 1])]
        if version == 0:
            args[0] = rng.choice([t for t in range(4) if t != theme])
    w.ov.u8(w.lg + 0x3744, rng.choice([0, 1]))
    w.ov.u8(0x0056B700, rng.randrange(2))
    w.ov.u32(0x0056E214, 0)
    w.ov.u32(w.lg + 0x14, 0xFFFFFFFF)
    # the cells (about to be emptied) and the level state the fog reads
    w.cells(density=0.3)
    w.ov.u32(LEVEL_STATE, rng.choice([4, 5, 7, 8, 9, 1]))
    return args, w.ov.pokes()


def rails_world(env, models=None):
    """The library with `models` source models (holders for their polygon lists), the trailing table of their keys and a small graph of rail nodes."""
    rng = env.rng
    w = lgt_world(env, models=models)
    count = len(w.models)
    source_region(w, count)
    # holders: empty polygon lists
    pw = gen_specs_poly.World(env)
    for m, model in enumerate(w.models):
        holder = w.a(Z_POLY + 0x10 * m)
        w.ov.put(holder, bytes(0xC))
        w.ov.u32(model["at"] + 0x20, holder)
    pool_clean(w)
    keys = [rng.randrange(1, 1 << 32) for _ in range(count)]
    table = w.a(Z_FAKE + T1)
    for j, k in enumerate(keys):
        w.ov.u32(table + 4 * j, k)
    raw = w.a(Z_FAKE + R1)
    N = rng.randrange(2, 7)
    node_table = w.a(Z_FAKE + NODES)
    rail_nodes = [k for k in range(N) if rng.random() < 0.6] or [0]
    link_nodes = [k for k in range(N) if k not in rail_nodes]
    scripted = dict(is_rail={}, links={}, next={}, pos={}, record=w.a(Z_FAKE + NODES + 0x1E0))
    w.ov.u16(scripted["record"], rng.randrange(1 << 16))
    for k in range(N):
        data = node_table + 0x40 + 0x20 * k
        w.ov.u32(node_table + 4 * k, data)
        kind = 0xC if k in link_nodes else rng.choice([1, 2, 5])
        c = rng.randrange(0, 4)
        w.ov.u16(data, kind)
        w.ov.u16(data + 2, c)
        key_at = data + 4 + c * 2
        key_at += 2 if key_at & 3 else 0
        w.ov.u32(key_at, rng.choice(keys))      # (a key no model has is a logged assertion and then a crash in retail)
        scripted["pos"][k] = (rng.randrange(-8, 8) << 12, rng.randrange(-4, 4) << 12, rng.randrange(-8, 8) << 12)
    for i, r in enumerate(rail_nodes):
        scripted["is_rail"][r] = 1
        scripted["links"][r] = [rng.choice(link_nodes)] if link_nodes and rng.random() < 0.9 else ([rng.choice(range(N))] if N else [0])
        if i + 1 < len(rail_nodes):
            scripted["next"][r] = rail_nodes[i + 1]
        else:
            scripted["next"][r] = rail_nodes[0] if rng.random() < 0.5 else 0
    for k in range(N):
        scripted["is_rail"].setdefault(k, 0)
    env.lg = {"queue": [6, 7, SRC_SLOT], "tables": {raw: table}, "rails": scripted}
    w.ov.u32(0x0056E214, N)
    w.ov.u32(0x0056E210, node_table)
    w.ov.u32(w.lg + 0x14, SRC_SLOT)
    name = w.a(Z_FAKE + NAME)
    w.ov.put(name, b"SkEd1_T\0" + bytes(6))
    return w, name


@spec(0x0043BCD0, "LevelGen_LoadRails", "lgt", this=lambda env: env.slab + LG)
def gen_load_rails(env, n):
    w, name = rails_world(env)
    return [name], w.ov.pokes()


GROUP_STUBS: dict = {}


# ---- create_rails: the polygons of the pieces' models ------------------------------------------------------------------------------------------------

def put_rail_polygons(w, models_with_polys=None):
    """Polygon lists for the source models' holders: a few rectilinear loops and strings from the 4.11b polygon world, relocated into Z_POLY."""
    rng = w.rng
    pw = gen_specs_poly.World(w.env)
    pw.point_at = w.a(Z_POLY + 0x200)
    pw.poly_at = w.a(Z_POLY + 0x100)
    for m, model in enumerate(w.models):
        holder = w.a(Z_POLY + 0x10 * m)
        polys = []
        if rng.random() < 0.7:
            for _ in range(rng.randrange(1, 3)):
                if rng.random() < 0.6:
                    x0 = rng.randrange(-4, 4) * 128
                    z0 = rng.randrange(-4, 4) * 128
                    pts = gen_specs_poly.rect_loop(rng, x0, x0 + rng.randrange(1, 4) * 128, z0, z0 + rng.randrange(1, 4) * 128, ccw=False, start=0)
                    poly, _ = pw.string(pts, True)
                else:
                    poly, _ = pw.string(gen_specs_poly.string_points(rng, rng.randrange(2, 5)), False)
                polys.append(poly)
        pw.link_polys(polys)
        w.ov.put(holder, bytes(0xC))
        w.ov.u32(holder, polys[0] if polys else 0)
        w.ov.u32(model["at"] + 0x20, holder)
    for a, d in pw.ov.bytes.items():
        w.ov.bytes[a] = d


@spec(0x0043E7C0, "LevelGen_CreateRails", "lgt", this=lambda env: env.slab + LG)
def gen_create_rails(env, n):
    w = lgt_world(env, models=env.rng.randrange(1, 3))
    rng = w.rng
    ps = place_piece_records(w, rng.randrange(0, 5), on_map=False)
    for p in ps:
        w.ov.u32(p["at"] + 0x224, rng.choice(w.models)["at"])
    w.ov.u32(w.lg + 0x2344, ps[0]["at"] if ps else 0)
    put_rail_polygons(w)
    pool_clean(w)
    w.ov.put(w.lg + 0x23B8, bytes(0xC))
    env.lg = {}
    return [], w.ov.pokes()


# ---- the trigger nodes -------------------------------------------------------------------------------------------------------------------------------

NODE_CLASSES = ((0x004C8870, 7, 0x005198A0), (0x004C8A20, 1, 0x005198AC), (0x004C8BD0, 0, 0x005198B8), (0x004C8C30, 1, 0x005198C4), (0x004C8CD0, 3, 0x005198D0),
                (0x004C8DA0, 2, 0x005198DC))


def node_stubs(fc, env):
    """The node classes of the trigger file are engine objects: their constructors give the object its class's vtable (the effect the port models) and every virtual method
    is a service with a scripted answer (the object's size, the size written)."""
    if getattr(fc, "node_stubs", False):
        return
    fc.node_stubs = True
    from opentony.fcall import write_chunks
    from opentony.memory import mem
    under_test()

    def make_ctor(vtable):
        def result(values):
            obj = env.alloc_log[-1][0]
            write_chunks(obj, struct.pack("<I", vtable))
            return obj
        return result

    for address, nargs, vtable in NODE_CLASSES:
        ensure(fc, address, nargs, convention="thiscall", this=True, result=make_ctor(vtable))
        slots = [mem.u32(vtable + 4 * i) for i in range(3)]
        ensure(fc, slots[0], 1, convention="thiscall", this=True)
        ensure(fc, slots[1], 0, convention="thiscall", this=True, result=lambda a: env.rng.choice([0x10, 0x14, 0x1C, 0x2C]))
        ensure(fc, slots[2], 1, convention="thiscall", this=True, result=lambda a: env.rng.choice([0x10, 0x14, 0x1C, 0x2C]))
    ensure(fc, 0x004C8A80, 2, convention="thiscall", this=True)


def lgt_stubs(fc, env):
    lg_stubs(fc, env)
    node_stubs(fc, env)


@spec(0x0043E980, "LevelGen_BuildNodes", "lgt", this=lambda env: env.slab + LG)
def gen_build_nodes(env, n):
    w = lgt_world(env, models=env.rng.randrange(1, 3))
    rng = w.rng
    pool_clean(w)
    w.items(active=rng.randrange(0, 4))
    w.ov.u8(w.lg + 0x3744, rng.choice([0, 1, 1]))
    w.ov.u32(LEVEL_STATE, rng.choice([7, 7, 7, 1, 4]))
    for m in w.models:
        if rng.random() < 0.3:
            w.ov.u8(m["at"] + 8, rng.choice([w.ov_u32(0x005614A4), w.ov_u32(0x00561158)]))
    ps = place_piece_records(w, rng.randrange(0, 12), on_map=True)
    w.ov.u32(w.lg + 0x2344, ps[0]["at"] if ps else 0)
    # the start and horse records
    for j in range(2):
        rec = w.lg + 0x369C + 0x14 * j
        w.ov.u8(rec, rng.choice([0, 1]))
        w.ov.u32(rec + 4, rng.randrange(w.W))
        w.ov.u32(rec + 8, rng.randrange(w.D))
        w.ov.u32(rec + 0xC, rng.randrange(5))
        w.ov.u32(rec + 0x10, rng.randrange(4))
    for i in range(6):
        rec = w.lg + 0x36C4 + 0x14 * i
        w.ov.u8(rec, rng.choice([0, 1]))
        w.ov.u32(rec + 4, rng.randrange(w.W))
        w.ov.u32(rec + 8, rng.randrange(w.D))
        w.ov.u32(rec + 0xC, rng.randrange(5))
        w.ov.u32(rec + 0x10, rng.randrange(4))
    # the rail outlines: polygons of points with a key (the piece they belong to)
    pw = gen_specs_poly.World(env)
    pw.point_at = w.a(Z_POLY + 0x200)
    pw.poly_at = w.a(Z_POLY + 0x100)
    polys = []
    for _ in range(rng.randrange(0, 4)):
        if rng.random() < 0.6:
            x0 = rng.randrange(-4, 4) * 128
            z0 = rng.randrange(-4, 4) * 128
            poly, addrs = pw.string(gen_specs_poly.rect_loop(rng, x0, x0 + 128, z0, z0 + 128, ccw=False, start=0), True)
        else:
            poly, addrs = pw.string(gen_specs_poly.string_points(rng, rng.randrange(2, 5)), False)
        key = rng.randrange(0, 4)
        for a in addrs:
            pw.ov.u32(a + 0xC, rng.choice([key, key, key + 1, 0xFFFFFFFF]))
        polys.append(poly)
    pw.link_polys(polys)
    for a, d in pw.ov.bytes.items():
        w.ov.bytes[a] = d
    w.ov.put(w.lg + 0x23B8, bytes(0xC))
    w.ov.u32(w.lg + 0x23B8, polys[0] if polys else 0)
    # gaps (pairs), starts
    gaps = put_gaps(w, 2 * rng.randrange(0, 3))
    for i in range(0, len(gaps), 2):
        w.ov.u32(gaps[i] + 8, gaps[i + 1])
        w.ov.u32(gaps[i + 1] + 8, gaps[i])
        w.ov.u8(gaps[i], 1)
        w.ov.u8(gaps[i + 1], 0)
    for g in gaps:
        w.ov.u32(g + 0x18, rng.randrange(0, 40))
        w.ov.u32(g + 0x1C, rng.randrange(0, 10))
    w.ov.u32(w.lg + 0x2388, gaps[0] if gaps else 0)
    starts = put_starts(w, rng.randrange(0, 3))
    for s in starts:
        w.ov.u32(s + 0xC, rng.randrange(0, 40))
    w.ov.u32(w.lg + 0x23A4, starts[0] if starts else 0)
    w.ov.u32(w.lg + 0x23C4, 0)
    w.ov.u32(0x0056A920, rng.randrange(1 << 16))
    return [], w.ov.pokes()


GROUP_STUBS.update({"lgz": lg_stubs, "lgt": lgt_stubs})


# ---- lgr: the region builder ---------------------------------------------------------------------------------------------------------------------------

from gen_specs_model import (FALLOFF_PTR, LIGHT_A, LIGHT_B, LIGHT_C, LIGHT_COUNT, LIGHT_POS, SHADE_PTR, out_stubs)  # noqa: E402

LGR_TABLES = REG_BASE + 0x2000                    # the out-model's falloff (0x400) and shade (0x100) tables
LGR_ARENA_BASE = REG_BASE + 0x2600
LGR_ARENA = 0x3A000


def lgr_regions(slab):
    return (lg_regions(slab, (RING, 0x400), (PIECE_BASE, 0x2400), (BUILT_BASE, 0x400), (GAP_BASE, 0x600), (START_BASE, 0x100), (ITEM_BASE, 0x200), (Z_FAKE, Z_FAKE_SIZE),
                       (Z_POLY, 0x1000), (LGR_TABLES, 0x600), (LGR_ARENA_BASE, LGR_ARENA)) + REGION_STATICS + [ROTATIONS, (0x533940, 0x10)])


def lgr_seed(env, rng):
    env.alloc_base = LGR_ARENA_BASE
    env.alloc_size = LGR_ARENA
    return {}


SLAB_BYTES["lgr"] = LGR_ARENA_BASE + LGR_ARENA + 0x100
SEEDS["lgr"] = lgr_seed
EXTRA_REGIONS["lgr"] = lgr_regions
READ_ONLY["lgr"] = LG_READS


def lights(w):
    ov, rng = w.ov, w.rng
    count = rng.choice([0, 1, 2])
    ov.u32(LIGHT_COUNT, count)
    ov.u32(LIGHT_B, rng.randrange(1, 1 << 14))
    ov.u32(LIGHT_C, rng.randrange(0, 4000))
    ov.u32(LIGHT_A, rng.randrange(0, 200))
    for k in range(count):
        ov.u32(LIGHT_POS + 12 * k, rng.randrange(-300, 300) & 0xFFFFFFFF)
        ov.u32(LIGHT_POS + 4 + 12 * k, 0xFFFFFC18)
        ov.u32(LIGHT_POS + 8 + 12 * k, rng.randrange(-300, 300) & 0xFFFFFFFF)
    ov.u32(FALLOFF_PTR, w.a(LGR_TABLES - REG_BASE + REG_BASE) if False else w.slab + LGR_TABLES)
    ov.u32(SHADE_PTR, w.slab + LGR_TABLES + 0x400)
    ov.put(w.slab + LGR_TABLES, rng.randbytes(0x500))


@spec(0x0043D400, "LevelGen_FinalizeRuntimeRegion", "lgr", this=lambda env: env.slab + LG)
def gen_finalize(env, n):
    rng = env.rng
    game = rng.choice([0, 0, 1])
    w = World(env, width=rng.choice([4, 6, 8]), depth=rng.choice([4, 6, 8]))
    w.object(in_game=game)
    w.maps()
    w.source()
    ring(w)
    models = models_that_fit(w, rng.randrange(3, 6))
    from levelgen_specs_pieces import fitting_cells
    # the fill pieces of the floor are library slots 0 (one cell), 1 (two cells wide) and 2 (two by two): they fit every cell the floor puts them in
    for index, (ww, dd) in enumerate(((1, 1), (2, 1), (2, 2))):
        models[index].update(width=ww, depth=dd, layers=1)
        w.ov.u8(models[index]["at"] + 0xA, ww)
        w.ov.u8(models[index]["at"] + 0xC, dd)
        w.ov.u8(models[index]["at"] + 0xB, 1)
        w.ov.u8(models[index]["at"] + 9, rng.choice([0, 0x10]))
    w.ov.u16(0x00533944, rng.randrange(1 << 16))      # (the texture word the gap members' primitives carry)
    for k in range(5):
        w.ov.u32(0x005614A8 + 4 * k, 0)
        w.ov.u32(0x00561490 + 4 * k, 1)
        w.ov.u32(0x00561174 + 4 * k, 2)
    placed = fitting_cells(w, rng.choice([0.2, 0.4, 0.6]), models[3:] or models)
    w.items(active=rng.randrange(0, 3))
    # the items' ends on cells that hold pieces (so the gap members get their pieces)
    cells = [(x, z, layer) for (x, z), lst in placed.items() for (layer, m, t) in lst]
    base = w.a(ITEM_BASE)
    for i in range(10):
        if i < 3 and cells and rng.random() < 0.9:
            at = base + 0x2C * i
            for end in range(2):
                x, z, layer = rng.choice(cells)
                w.ov.u8(at + end, x)
                w.ov.u8(at + 2 + end, layer)
                w.ov.u8(at + 4 + end, z)
    n1 = len(models)
    source_region(w, n1)
    t1 = w.a(Z_FAKE + T1)
    trailing_table(w, t1, n1, rng.randrange(0, 4), which=0)
    n2 = rng.randrange(1, 4)
    second_region(w, n2)
    t2 = w.a(Z_FAKE + T2)
    trailing_table(w, t2, n2, rng.randrange(0, 4), which=1)
    env.lg = {"tables": {w.a(Z_FAKE + R1): t1, w.a(Z_FAKE + R2): t2}}
    w.ov.u32(w.lg + 0x14, SRC_SLOT)
    lights(w)
    pool_clean(w)
    # the region slots: 3 and 4 are the libraries; others in use at random, at least one free
    free = rng.choice([0, 1, 2, 5, 6, 7, 8, 12, 19])
    for i in range(20):
        used = i in (SRC_SLOT, SECOND_SLOT) or (i != free and rng.random() < 0.5)
        w.ov.u8(REGION_NAMES + 0x44 * i, rng.choice([0x65, 0x70]) if used else 0)
    slot = rng.choice([0xFFFFFFFF, 0xFFFFFFFF, free])
    w.ov.u32(SLOT, slot)
    if slot != 0xFFFFFFFF:
        w.ov.u32(slot_entry(slot) + 0x18, rng.choice([0, 0, w.slab + LGR_TABLES]))
    w.ov.u32(LIVE_COUNT, 0)
    put_rail_polygons(w)
    w.ov.put(w.lg + 0x23B8, bytes(0xC))
    w.ov.u32(w.lg + 0x2394, 0)
    w.ov.u32(0x0056E31C, 0) if False else None
    w.ov.put(w.a(Z_FAKE + 0xFE0), bytes(8))
    # start and horse types among the models
    for m in w.models:
        if rng.random() < 0.3:
            w.ov.u8(m["at"] + 8, rng.choice([w.ov_u32(0x005613B0), w.ov_u32(0x005613B4), w.ov_u32(0x005611A8), w.ov_u32(0x005611AC)]))
    return [w.a(Z_FAKE + 0xFE0), rng.choice([1, 1, 0])], w.ov.pokes()


def lgr_stubs(fc, env):
    lg_stubs(fc, env)
    out_stubs(fc, env)
    node_stubs(fc, env)


GROUP_STUBS.update({"lgr": lgr_stubs})
