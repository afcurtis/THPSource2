"""Phase 4.11c case generators, part 1: the world of the park generator (levelgen.cpp, 0x004397d0..0x0043f720) and the group `lgm` (construction, the maps, the list frees).

The probe (frontend_calls.py with the group name) forces calls of the retail functions on the live process.  The LevelGen object (0x3748 bytes), the cells of a small park,
the maps of pieces, the pieces themselves (out-models with nodes of the ring), the source models and the item records are made up in the scratch slab; the statics the code
reads (grid size, theme, cell sizes, the special piece ids, the region tables) are the game's own, poked per case.  Services (the allocator, Mem_Delete, the logger, the
assertion routine, the PSX region spooler and the trigger node classes) are stubbed and reported.

Slab layout (offsets from the scratch slab):
  0x0000 the LevelGen object (0x3800)       0x3800 rows of the editor's cells (0x100) and their cells (0x1000)
  0x4900 rows of FloorTab (0x100), cells (0x1000)   0x5a00 rows of OutModelMap (0x100), pointers (0x1400)   0x6f00 rows of FloorModelMap (0x100), pointers (0x1400)
  0x8400 the ten item records (0x200)       0x8600 source models (8 of 0x200)       0x9600 their elements (8 of 0x100)
  0x9e00 pieces (24 of 0x240)               0xd400 their built models (24 of 0x200)  0x10400 gap members (24 of 0x60)  0x10b00 start positions (8 of 0x20)
  0x10c00 the first 64 nodes of the ring    0x11000 the gap members' primitive buffers (2 of 0x900)    0x13000 the allocation arena
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from gen_specs import LIST_BASE, RECORD_BYTES, RECORD_COUNT, RECORDS, THEME
from gen_specs_model import (CELL_X, CELL_Z, FREE, GRID_D, GRID_W, LIVE, RING_CURSOR, RING_TABLE, RING_USED, ROW_SET, STATIC_READS, STATIC_WINDOWS, build_model)
from pspool_specs import Ov, ensure, under_test

# ---- the statics -------------------------------------------------------------------------------------------------------------
VERSION_TABLE = 0x005283A4
ROWS_SOURCE, ITEMS_SOURCE, ROWS, ITEMS = 0x005614CC, 0x005614D0, 0x005614D4, 0x005614D8
SLOT = 0x005613B8
START_TYPES = 0x005613B0          # two ids
HORSE_TYPE, START_MARKER = 0x005611A8, 0x005611AC
SPECIAL_A, SPECIAL_B, SPECIAL_C = 0x005614A8, 0x00561490, 0x00561174
FILLER_A, FILLER_B = 0x005614A4, 0x00561158
LIVE_COUNT = 0x00561198
TRAILING = 0x0056A1FC
OBJECT_TABLES = 0x0056D438
REGION_NAMES = 0x0056D428
LEVEL_STATE = 0x00533F38
CLOCK = 0x0056E31C
GAP_MARKER = 0x005286FC
GAP_TYPES = 0x005286BC
GAP_NAMES = 0x005283D0

# ---- the slab ------------------------------------------------------------------------------------------------------------------
LG = 0x0000
LG_SIZE = 0x3800
ROWS_SRC, CELLS_SRC = 0x3800, 0x3900
ROWS_FLR, CELLS_FLR = 0x4900, 0x4A00
ROWS_OM, BLOCK_OM = 0x5A00, 0x5B00
ROWS_FM, BLOCK_FM = 0x6F00, 0x7000
ITEM_BASE = 0x8400
MODEL_BASE, MODEL_STRIDE = 0x8600, 0x200
ELEM_BASE, ELEM_STRIDE = 0x9600, 0x100
PIECE_BASE, PIECE_STRIDE = 0x9E00, 0x240
BUILT_BASE, BUILT_STRIDE = 0xD400, 0x200
GAP_BASE, GAP_STRIDE = 0x10400, 0x60
START_BASE, START_STRIDE = 0x10B00, 0x20
RING = 0x10C00
GAPBUF_BASE, GAPBUF_STRIDE = 0x11000, 0x900
ARENA = 0x13000
SLAB_FULL = 0x13000

IN_BASE, IN_STRIDE = 0x18, 0x24          # the source models' in-models inside the LevelGen object
MAX_MODELS = 8
MAX_PIECES = 24


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


# ---- windows -----------------------------------------------------------------------------------------------------------------
# statics every group captures (mutable): the grid, the cell sizes, the theme, the ring and lights of the out-model, the special ids
LG_STATICS = STATIC_WINDOWS + [(LEVEL_STATE, 4), (0x528398, 0x1F0), (0x56114C, 4), (0x5611A0, 0x20), (TRAILING, 0x40), (0x563A58, 0x10), (0x56B700, 4), (0x56A920, 4), (0x56DB28, 4), (0x6A0548, 0x20)]
# statics the region code touches
REGION_STATICS = [(0x56D41C, 0x560), (0x537C80, 8), (0x56E1B0, 0x10), (0x56E204, 0x14), (0x5477EC, 4), (0x560FD0, 4), (0x568628, 4)]
LG_READS = STATIC_READS + [(0x5286BC, 0x44), (0x52A428, 0xC0), (CLOCK, 4), (0x5198A0, 0x48)]


def window(slab, *ranges):
    return [(slab + a, n) for a, n in ranges]


class World:
    """A legal state of the generator in the slab: statics, a small park, source models, pieces and the lists that link them (each part is made on request)."""

    def __init__(self, env, width=None, depth=None, theme=None, version=None):
        self.env = env
        self.rng = env.rng
        self.slab = env.slab
        self.ov = Ov()
        rng = self.rng
        self.W = rng.choice([2, 4, 4, 6, 8]) if width is None else width
        self.D = rng.choice([2, 4, 4, 6, 8]) if depth is None else depth
        self.theme = rng.randrange(4) if theme is None else theme
        self.version = rng.randrange(5) if version is None else version
        self.models = []
        self.pieces = []
        ov = self.ov
        ov.u32(GRID_W, self.W)
        ov.u32(GRID_D, self.D)
        ov.u32(ROW_SET, self.version)
        ov.u32(THEME, self.theme)
        ov.u32(CELL_X, rng.choice([0x154, 0x154, 0x154, 0x100, 0x180]))
        ov.u32(CELL_Z, rng.choice([0xAA, 0xAA, 0xAA, 0x80, 0x90]))
        # the special ids: the fill pieces of the floor (library slots) and the pieces handled specially
        for base in (SPECIAL_A, SPECIAL_B, SPECIAL_C):
            for k in range(5):
                ov.u32(base + 4 * k, rng.choice([0, 1, 2, 3, 4, 5, 6, 7]))
        ov.u32(START_TYPES, rng.choice([8, 9]))
        ov.u32(START_TYPES + 4, rng.choice([10, 11]))
        ov.u32(HORSE_TYPE, rng.choice([12, 13]))
        ov.u32(START_MARKER, rng.choice([14, 15, 9]))
        ov.u32(FILLER_A, rng.choice([1, 2]))
        ov.u32(FILLER_B, rng.choice([3, 4]))

    # ---- addresses --------------------------------------------------------------------------------------------------------------
    def a(self, offset):
        return self.slab + offset

    @property
    def lg(self):
        return self.slab + LG

    def in_model(self, m):
        return self.lg + IN_BASE + IN_STRIDE * m

    # ---- the object ----------------------------------------------------------------------------------------------------------------
    def object(self, in_game=None, model_count=None, scrub=True):
        """The LevelGen object: random bytes (a field the code must overwrite shows as a mismatch), the fixed state of a constructed object."""
        ov, rng = self.ov, self.rng
        if scrub:
            ov.put(self.lg, rng.randbytes(LG_SIZE))
        ov.u8(self.lg + 0x3744, rng.choice([0, 1]) if in_game is None else in_game)
        ov.u32(self.lg + 0x14, 0xFFFFFFFF)
        ov.u32(self.lg + 0x2380, 0xFFFFFFFF)
        ov.u32(self.lg + 0x236C, 0xFFFFFFFF)
        ov.u32(self.lg + 0x2340, 0 if model_count is None else model_count)
        for off in (0x2344, 0x234C, 0x2388, 0x23A4, 0x23B8, 0x23C4, 0x2390, 0x23A0, 0x23B4, 0x23B0):
            ov.u32(self.lg + off, 0)
        ov.u32(self.lg + 0x2394, 0)
        ov.u32(self.lg + 0x23A8, 0)
        ov.u32(self.lg + 0x238C, 0)
        ov.u32(self.lg + 0x2354, 0)
        ov.u32(self.lg + 0x2358, 0)
        ov.u32(self.lg + 0x2384, 0)
        ov.u32(self.lg + 0x3698, 0)
        ov.u8(self.lg + 0x369C, 0)
        ov.u8(self.lg + 0x36B0, 0)
        for i in range(6):
            ov.u8(self.lg + 0x36C4 + 0x14 * i, 0)
        ov.u32(self.lg + 0x239C, 0x10)
        ov.u32(self.lg + 0x2398, 0)
        ov.u32(self.lg + 0x2350, 0)
        ov.u32(self.lg + 0x2348, 0)
        ov.u32(self.lg, 0)
        # the in-model holders are empty (no polygons), as a fresh object leaves them (the destructor frees all 250)
        for m in range(250):
            ov.u32(self.in_model(m) + 0x20, 0)

    # ---- the maps ------------------------------------------------------------------------------------------------------------------
    def rows(self, rows_off, block_off, cell_bytes, count=None):
        """A table of D row pointers over a block of D rows of W cells; returns the table's address."""
        ov = self.ov
        table = self.a(rows_off)
        block = self.a(block_off)
        for z in range(self.D if count is None else count):
            ov.u32(table + 4 * z, block + z * self.W * cell_bytes)
        return table

    def maps(self, floor=True, out=True, flr_map=True, scrub=True):
        """FloorTab, OutModelMap and FloorModelMap allocated in the object (the row tables point into the slab)."""
        ov, rng = self.ov, self.rng
        if floor:
            ov.u32(self.lg, self.rows(ROWS_FLR, CELLS_FLR, 16))
            if scrub:
                ov.put(self.a(CELLS_FLR), rng.randbytes(self.W * self.D * 16))
        if out:
            ov.u32(self.lg + 0x2348, self.rows(ROWS_OM, BLOCK_OM, 20))
            if scrub:
                ov.put(self.a(BLOCK_OM), bytes(self.W * self.D * 20))
        if flr_map:
            ov.u32(self.lg + 0x2350, self.rows(ROWS_FM, BLOCK_FM, 20))
            if scrub:
                ov.put(self.a(BLOCK_FM), bytes(self.W * self.D * 20))

    def source(self):
        """The editor's cell rows (the globals 0x5614cc and 0x5614d4 point at them) and the item records (0x5614d0, 0x5614d8)."""
        ov = self.ov
        table = self.rows(ROWS_SRC, CELLS_SRC, 16)
        ov.u32(ROWS_SOURCE, table)
        ov.u32(ROWS, table)
        ov.u32(ITEMS_SOURCE, self.a(ITEM_BASE))
        ov.u32(ITEMS, self.a(ITEM_BASE))

    # ---- source models --------------------------------------------------------------------------------------------------------
    def make_models(self, count, with_elements=False, kinds=None, types=None):
        """`count` source models: model data, the in-model inside the object, no polygons.  Returns a list of dicts."""
        ov, rng = self.ov, self.rng
        cell_x = self.ov_u32(CELL_X)
        cell_z = self.ov_u32(CELL_Z)
        out = []
        for m in range(count):
            at = self.in_model(m)
            model = self.a(MODEL_BASE + MODEL_STRIDE * m)
            nfaces = rng.randrange(1, 4)
            build_model(ov, model, rng, nfaces)
            width, layers, depth = rng.randrange(1, 4), rng.randrange(1, 4), rng.randrange(1, 4)
            kind = rng.choice([0, 0, 0, 0x10, 0x10, 0x20, 8, 4, 2, 6, 0x14, 0x12]) if kinds is None else kinds[m]
            type_id = rng.choice([0, 1, 2, 3, 5, 7, 12, 13, 20, 40]) if types is None else types[m]
            ov.put(at, rng.randbytes(IN_STRIDE))
            ov.u32(at, 0)                       # (no elements: no nodes, nothing to hide)
            ov.u32(at + 4, model)
            ov.u8(at + 8, type_id)
            ov.u8(at + 9, kind)
            ov.u8(at + 0xA, width)
            ov.u8(at + 0xB, layers)
            ov.u8(at + 0xC, depth)
            ov.u8(at + 0xD, rng.choice([0, 1, 2]))
            ext = lambda: rng.randrange(-8, 9) * 0x20      # noqa: E731
            ov.u16(at + 0xE, ext() & 0xFFFF)
            ov.u16(at + 0x10, ext() & 0xFFFF)
            ov.u16(at + 0x12, ext() & 0xFFFF)
            ov.u16(at + 0x14, ext() & 0xFFFF)
            ov.u16(at + 0x16, ext() & 0xFFFF)
            ov.u32(at + 0x20, 0)
            out.append(dict(index=m, at=at, model=model, width=width, layers=layers, depth=depth, kind=kind, type=type_id))
        self.models = out
        ov.u32(self.lg + 0x2340, count)
        return out

    def ov_u32(self, address):
        data = bytes(self.ov.bytes.get(address + i, 0) for i in range(4))
        return struct.unpack("<I", data)[0]

    # ---- cells ---------------------------------------------------------------------------------------------------------------------
    def cells(self, density=0.5, rotations=True, models=None, layers_max=3):
        """The editor's cells: each cell has `count` layers (0..layers_max), each a library slot or empty (0xffff) and a turn.  Returns {(x, z): [(layer, model, turn)]}."""
        ov, rng = self.ov, self.rng
        models = self.models if models is None else models
        placed = {}
        base = self.a(CELLS_SRC)
        for z in range(self.D):
            for x in range(self.W):
                cell = base + (z * self.W + x) * 16
                count = 0
                refs = [0xFFFF] * 5
                turns = [rng.randrange(4) if rotations else 0 for _ in range(5)]
                if models and rng.random() < density:
                    count = rng.randrange(1, layers_max + 1)
                    for k in range(count):
                        if rng.random() < 0.8:
                            refs[k] = rng.choice(models)["index"]
                        placed.setdefault((x, z), []).append((k, refs[k], turns[k]))
                for k in range(5):
                    ov.u16(cell + 2 * k, refs[k])
                    ov.u8(cell + 0xA + k, turns[k] if rotations else 0)
                ov.u8(cell + 0xF, count)
        return placed

    def items(self, active=None):
        """The ten item records: free ones (marker 0x80), the disabled marker (0x81) and ends on cells."""
        ov, rng = self.ov, self.rng
        base = self.a(ITEM_BASE)
        active = rng.randrange(0, 4) if active is None else active
        for i in range(10):
            at = base + 0x2C * i
            ov.put(at, rng.randbytes(0x2C))
            if i < active:
                ov.u8(at + 0xC, rng.choice([0x81, 0x00, 0x01, 0x05]))
                for end in range(2):
                    ov.u8(at + end, rng.randrange(self.W))
                    ov.u8(at + 2 + end, rng.randrange(5))
                    ov.u8(at + 4 + end, rng.randrange(self.D))
                    ov.u8(at + 6 + end, rng.randrange(4))
                    ov.u8(at + 8 + end, rng.randrange(16))
                    ov.u8(at + 0xA + end, rng.randrange(16))
            else:
                ov.u8(at + 0xC, 0x80)
            ov.u32(at + 0x28, rng.randrange(0, 4))      # (the item's type indexes a table of 16-bit entries)
            name = ("Gap %d" % (i + 1)).encode() + b"\0"
            ov.put(at + 0xD, name)


def lg_regions(slab, *ranges, extra=()):
    # (the object, its models and the tables World.maps / World.source poke are always captured)
    return window(slab, *ranges, (LG, LG_SIZE), (MODEL_BASE, MODEL_STRIDE * MAX_MODELS), (ROWS_SRC, 0x1100), (ROWS_FLR, 0x1100), (ROWS_OM, 0x1500), (ROWS_FM, 0x1500), (ITEM_BASE, 0x200)) + LG_STATICS + list(extra)


# ---- the ring --------------------------------------------------------------------------------------------------------------------
def ring(world, free=True, cursor=None, used=None):
    """The first 64 nodes of the out-model's ring free (or live), the table placed so that the scan stays in the window."""
    ov, rng = world.ov, world.rng
    table = world.a(RING)
    ov.u32(RING_TABLE, table)
    ov.u32(RING_CURSOR, rng.randrange(0, 8) if cursor is None else cursor)
    ov.u32(RING_USED, rng.randrange(8, 0x300) if used is None else used)
    for k in range(64):
        ov.u32(table + 16 * k, FREE if free else LIVE)
        ov.put(table + 16 * k + 4, rng.randbytes(12))
    return table


# ---- lgm: construction, the maps, the list frees ------------------------------------------------------------------------------------
LGM_ARENA = 0xA000       # (the out-model's ring of 0x8c0 nodes, the tables, the maps of a small park)


def lgm_regions(slab):
    return lg_regions(slab, (LG, LG_SIZE), (ROWS_SRC, 0x1100), (ROWS_FLR, 0x1100), (ROWS_OM, 0x1500), (ROWS_FM, 0x1500), (ITEM_BASE, 0x200), (PIECE_BASE, 0x2400),
                      (BUILT_BASE, 0x400), (GAP_BASE, 0x600), (START_BASE, 0x100), (RING, 0x400), (ARENA, LGM_ARENA))


def lgm_seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = LGM_ARENA
    return {}


SLAB_BYTES["lgm"] = ARENA + LGM_ARENA + 0x100
SEEDS["lgm"] = lgm_seed
EXTRA_REGIONS["lgm"] = lgm_regions
READ_ONLY["lgm"] = LG_READS


def put_pieces(world, count, with_gap=False):
    """`count` linked pieces (out-models of 0x238 bytes) with a node or two each from the ring; returns their addresses."""
    ov, rng, slab = world.ov, world.rng, world.slab
    pieces = []
    for i in range(count):
        at = world.a(PIECE_BASE + PIECE_STRIDE * i)
        ov.put(at, rng.randbytes(0x238))
        pieces.append(at)
    nodes = 0
    for i, at in enumerate(pieces):
        ov.u32(at + 0x230, pieces[i + 1] if i + 1 < len(pieces) else 0)
        first = None
        previous = None
        for _ in range(rng.randrange(0, 3)):
            if nodes >= 60:
                break
            node = world.a(RING) + 16 * nodes
            nodes += 1
            ov.u32(node, LIVE)
            ov.u32(node + 0xC, 0)
            if previous is not None:
                ov.u32(previous + 0xC, node)
            else:
                first = node
            previous = node
        ov.u32(at + 0x218, first or 0)
        ov.u32(at + 0x234, 0)
    return pieces


def put_gaps(world, count):
    """`count` gap members with a linked piece each; returns their addresses (the pieces are put separately)."""
    ov, rng = world.ov, world.rng
    gaps = []
    for i in range(count):
        at = world.a(GAP_BASE + GAP_STRIDE * i)
        ov.put(at, rng.randbytes(0x58))
        gaps.append(at)
    for i, at in enumerate(gaps):
        ov.u32(at + 0xC, gaps[i + 1] if i + 1 < len(gaps) else 0)
    return gaps


def put_starts(world, count):
    ov, rng = world.ov, world.rng
    starts = []
    for i in range(count):
        at = world.a(START_BASE + START_STRIDE * i)
        ov.put(at, rng.randbytes(0x1C))
        starts.append(at)
    for i, at in enumerate(starts):
        ov.u32(at + 8, starts[i + 1] if i + 1 < len(starts) else 0)
    return starts


def lgm_object(world, with_maps=True, pieces=0, floor_pieces=0, gaps=0, starts=0, models=0):
    world.object()
    if with_maps:
        world.maps()
    ring(world)
    if pieces or floor_pieces:
        ps = put_pieces(world, pieces + floor_pieces)
        if pieces:
            world.ov.u32(world.lg + 0x2344, ps[0])
        if floor_pieces:
            if pieces:
                world.ov.u32(ps[pieces - 1] + 0x230, 0)
            world.ov.u32(world.lg + 0x234C, ps[pieces])
        # gap members point at pieces
    if gaps:
        gs = put_gaps(world, gaps)
        world.ov.u32(world.lg + 0x2388, gs[0])
        pcs = [world.a(PIECE_BASE + PIECE_STRIDE * i) for i in range(max(pieces, 1))]
        for g in gs:
            world.ov.u32(g + 4, world.rng.choice(pcs))
    if starts:
        ss = put_starts(world, starts)
        world.ov.u32(world.lg + 0x23A4, ss[0])
    if models:
        world.make_models(models)


@spec(0x004397D0, "LevelGen_Initialize", "lgm", this=lambda env: env.slab + LG)
def gen_construct(env, n):
    w = World(env)
    w.object(scrub=True)
    # (the maps are allocated by the constructor: the pointers it checks are zero)
    ring(w)
    w.ov.u32(RING_USED, w.rng.randrange(0, 0x900))
    w.source()
    # statics the constructor's out-model init and pool reset use
    from gen_specs_model import POOL_INDEX, POOL_TABLE, LIGHT_ROWS
    w.ov.u32(POOL_INDEX, w.rng.choice([0, 0, 3]))
    for k in range(100):
        w.ov.u32(POOL_TABLE + 4 * k, 0)
    w.ov.u32(0x005614DC, w.rng.choice([0, 0, w.rng.randrange(1, 1 << 32)]))
    w.ov.u32(0x005614E0, w.rng.choice([0, 0, w.rng.randrange(1, 1 << 32)]))
    return [w.rng.choice([0, 1])], w.ov.pokes()


def lgm_pool_statics(w):
    from gen_specs_model import POOL_INDEX, POOL_TABLE
    rng = w.rng
    w.ov.u32(POOL_INDEX, rng.choice([0, 1, 4]))
    for k in range(100):
        w.ov.u32(POOL_TABLE + 4 * k, rng.choice([0, rng.randrange(1, 1 << 32)]))
    for a in (0x005614DC, 0x005614E0):
        w.ov.u32(a, rng.choice([0, rng.randrange(1, 1 << 32)]))


@spec(0x00439900, "LevelGen_Destruct", "lgm", this=lambda env: env.slab + LG)
def gen_destruct(env, n):
    w = World(env)
    rng = w.rng
    lgm_object(w, with_maps=True, pieces=rng.randrange(0, 4), floor_pieces=rng.randrange(0, 3), gaps=rng.randrange(0, 3), starts=rng.randrange(0, 3), models=rng.randrange(0, 4))
    lgm_pool_statics(w)
    w.ov.u32(RING_TABLE, rng.choice([0, w.a(RING)]))
    w.ov.u32(w.lg + 0x14, rng.choice([0xFFFFFFFF, 3, 5]))
    w.ov.u32(w.lg + 0x2380, rng.choice([0xFFFFFFFF, 4]))
    w.ov.u32(w.lg + 0x236C, rng.choice([0xFFFFFFFF, 6]))
    w.ov.u8(w.lg + 0x3744, rng.choice([0, 1]))
    return [], w.ov.pokes()


@spec(0x004399A0, "LevelGen_InitStatics", "lgm")
def gen_init_statics(env, n):
    w = World(env)
    rng = w.rng
    w.object()
    w.source()
    w.cells(density=0.5)
    w.items(active=rng.randrange(0, 5))
    for j in range(8):
        w.ov.put(0x0056A1FC + 8 * j, rng.randbytes(8))
    w.ov.u32(SLOT, rng.choice([0xFFFFFFFF, 3]))
    w.ov.u32(0x00561488, rng.randrange(1 << 32))
    w.ov.u32(0x0056148C, rng.randrange(1 << 32))
    if rng.random() < 0.1:
        w.ov.u32(ITEMS_SOURCE, 0)
    return [rng.choice([0, 1, 1, 1, 0x100, 2])], w.ov.pokes()


@spec(0x00439E10, "LevelGen_UnloadModels", "lgm", this=lambda env: env.slab + LG)
def gen_unload(env, n):
    w = World(env)
    rng = w.rng
    lgm_object(w, with_maps=True, pieces=rng.randrange(0, 4), floor_pieces=rng.randrange(0, 3), gaps=rng.randrange(0, 3), starts=rng.randrange(0, 3), models=rng.randrange(0, 5))
    lgm_pool_statics(w)
    w.ov.u32(w.lg + 0x14, rng.choice([0xFFFFFFFF, 3, 5]))
    w.ov.u32(w.lg + 0x2380, rng.choice([0xFFFFFFFF, 4]))
    w.ov.u32(w.lg + 0x236C, rng.choice([0xFFFFFFFF, 6]))
    w.ov.put(w.lg + 4, rng.randbytes(0x10))
    w.ov.u8(w.lg + 0x3744, rng.choice([0, 1]))
    return [], w.ov.pokes()


@spec(0x00439ED0, "LevelGen_ClearSource", "lgm", this=lambda env: env.slab + LG)
def gen_clear_source(env, n):
    w = World(env)
    w.object()
    w.ov.u32(w.lg + 0x14, w.rng.choice([0xFFFFFFFF, 0, 3, 7]))
    return [], w.ov.pokes()


@spec(0x00439EF0, "LevelGen_AllocMaps", "lgm", this=lambda env: env.slab + LG)
def gen_alloc_maps(env, n):
    w = World(env, width=env.rng.choice([2, 4, 8, 16]), depth=env.rng.choice([2, 4, 6, 16]))
    w.object()
    rng = w.rng
    # (an earlier map still allocated is a logged assertion; the pointer is overwritten)
    for off in (0, 0x2348, 0x2350):
        w.ov.u32(w.lg + off, rng.choice([0, 0, 0, 0x1234]))
    return [], w.ov.pokes()


@spec(0x0043A100, "LevelGen_FreeMaps", "lgm", this=lambda env: env.slab + LG)
def gen_free_maps(env, n):
    w = World(env)
    w.object()
    rng = w.rng
    w.maps(floor=rng.random() < 0.9, out=rng.random() < 0.9, flr_map=rng.random() < 0.9)
    return [], w.ov.pokes()


@spec(0x0043A240, "LevelGen_Resize", "lgm", this=lambda env: env.slab + LG)
def gen_resize(env, n):
    w = World(env, width=env.rng.choice([2, 4, 8]), depth=env.rng.choice([2, 4, 6]))
    w.object()
    w.maps()
    w.source()
    rng = w.rng
    w.cells(density=0.3)
    w.items(active=rng.randrange(0, 3))
    return [0], w.ov.pokes()


SCRATCH_NAME, SCRATCH_LIBRARY, SCRATCH_POSITION = 0x7FF00800, 0x7FF00840, 0x7FF00880


def safe(fn, default=0):
    """A stub answer that cannot raise (an exception inside a breakpoint's stop leaves the inferior stopped in the forced call)."""
    def run(a):
        try:
            return fn(a)
        except Exception:      # noqa: BLE001
            return default
    return run


def lg_stubs(fc, env):
    """The engine calls the generator reaches: the PSX region spooler (queueing, clearing, trailing tables), the fog, the zone blockmap, the trigger file and its rail
    nodes.  What they answer is scripted per case through `env.lg` (set by the generator; the defaults are a source in slot 3 and no rails)."""
    if getattr(fc, "lg_stubs", False):
        return
    fc.lg_stubs = True
    under_test()
    lg = lambda: getattr(env, "lg", {})      # noqa: E731
    ensure(fc, 0x004B3270, 1)         # PSX_ClearNamedRegion(name)
    ensure(fc, 0x004B2440, 0)         # PSX_NothingA
    ensure(fc, 0x004B2390, 0)         # PSX_FreeUnusedMaterials
    ensure(fc, 0x0048A420, 0)         # Palette_ClearSourceLookup
    ensure(fc, 0x00489490, 0)         # Options_NothingH (a jump from UnloadModels)
    ensure(fc, 0x004B32F0, 3)         # PSX_ClearRegion(slot, 0, 0)
    queue = lambda a: lg().get("queue", [3, 4, 5]).pop(0) if lg().get("queue", [3, 4, 5]) else 0       # noqa: E731
    ensure(fc, 0x004B37A0, 4, "engine_004b37a0", stack_to=SCRATCH_LIBRARY, result=queue)
    ensure(fc, 0x004B3DF0, 0)         # PSX_DrainSpooler
    ensure(fc, 0x004B20F0, 1, result=lambda a: lg().get("tables", {}).get(a[0], 0))     # PSX_FindTrailingTables(region)
    ensure(fc, 0x0042FC70, 0)
    ensure(fc, 0x0045D760, 1)         # the loading screen's progress call (GenPolyList_Merge)
    ensure(fc, 0x00464710, 3)         # M3D_SetFogRange(10, near, far)
    ensure(fc, 0x004667E0, 2)         # M3D_LoadZoneBlockmap(0, packet)
    # the trigger file and its rail nodes
    ensure(fc, 0x004C5130, 1, stack_to=SCRATCH_NAME, text=True)      # TRG_LoadFile(name)
    ensure(fc, 0x004C5100, 0)
    rails = lambda: lg().get("rails", {})      # noqa: E731
    ensure(fc, 0x004AA040, 1, result=safe(lambda a: rails().get("is_rail", {}).get(a[0] & 0xFFFF, 0)))
    ensure(fc, 0x004AA1C0, 1, result=safe(lambda a: len(rails().get("links", {}).get(a[0] & 0xFFFF, []))))
    ensure(fc, 0x004AA090, 2, result=safe(lambda a: rails().get("links", {}).get(a[0] & 0xFFFF, [0])[a[1] & 0xFFFF]))
    ensure(fc, 0x004AA220, 1, result=safe(lambda a: rails().get("next", {}).get(a[0] & 0xFFFF, 0)))
    ensure(fc, 0x004C8650, 2, stack_to=SCRATCH_POSITION,
           pokes=[(0, None, lambda a: struct.pack("<iii", *rails().get("pos", {}).get(a[1] & 0xFFFF, (0, 0, 0))))], result=safe(lambda a: rails().get("record", 0)))


GROUP_STUBS: dict = {"lgm": lg_stubs}
