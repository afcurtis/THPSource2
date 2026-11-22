"""Phase 4.11b case generators, part 3: the model-info pool and the in-model (`gpool`), the out-model's statics and node ring (`gring`), the out-model (`gout`) and the gap
members (`ggap`) of genobjs.cpp (0x0043f730..0x004417b0, 0x004436a0, 0x004436d0).

A model is the PSX model format: a header (0x1c bytes: 16-bit counts at +2, +4, +6 and the extent at +0xc..+0x16), the vertices (8 bytes: x, y, z, a pad) and the
faces (a flags word, the face's size, four vertex indices, then the face's other data up to its size).  The probe builds walls (rectangles in a plane of constant x or z) the
way the editor's pieces are: the in-model then turns each into an element (flags, a point count, the next element, the face and up to five points), the out-model
gives each element a node of the ring.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from gen_specs import LIST_BASE, RECORD_BYTES, RECORD_COUNT, RECORDS, THEME, memo
from pspool_specs import Ov, ensure

# the statics
POOL_INDEX, POOL_TABLE = 0x00528710, 0x005611BC
RING_TABLE, RING_CURSOR, RING_USED = 0x00561454, 0x00561458, 0x005614E4
CELL_X, CELL_Z = 0x00561488, 0x0056148C
GRID_W, GRID_D, ROW_SET = 0x00528398, 0x0052839C, 0x005283A0
LIGHT_A, LIGHT_B, LIGHT_COUNT, LIGHT_C = 0x00561160, 0x0056119C, 0x00561168, 0x00561194
LIGHT_POS, LIGHT_DIR = 0x00561350, 0x005613F0
FALLOFF_PTR, SHADE_PTR = 0x005614DC, 0x005614E0
GTE_IN, GTE_SQ = 0x006A3EB0, 0x006A3EB8
LIGHT_ROWS = 0x0052A190
SHADE_ROWS = 0x0052A410
FREE, LIVE = 0xFFFFD8F1, 0xFFFFDD48

STATIC_WINDOWS = [(0x00528390, 0x20), (0x00528700, 0x20), (0x00561150, 0x3B0), (GTE_IN, 0x20)]
STATIC_READS = [(RECORDS - 0x40, RECORD_BYTES * (RECORD_COUNT + 1) + 0x40), (LIST_BASE, 0xD0), (LIGHT_ROWS, 0x2A0)]


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


# ---- models and in-models ---------------------------------------------------------------------------------------------------------

def build_model(ov: Ov, at: int, rng, nfaces: int, offsets=None):
    """A model of wall faces at `at`; returns (size, [face addresses], [per face: four (x, y, z) vertices])."""
    verts, faces = [], []
    for f in range(nfaces):
        y0 = -rng.randrange(1, 8) * 16
        y1 = y0 - rng.randrange(1, 8) * 16
        if rng.random() < 0.5:
            zc = rng.randrange(-8, 8) * 16
            x0 = rng.randrange(-8, 8) * 16
            x1 = x0 + rng.randrange(1, 8) * 16
            quad = [(x0, y0, zc), (x1, y0, zc), (x1, y1, zc), (x0, y1, zc)]
        else:
            xc = rng.randrange(-8, 8) * 16
            z0 = rng.randrange(-8, 8) * 16
            z1 = z0 + rng.randrange(1, 8) * 16
            quad = [(xc, y0, z0), (xc, y0, z1), (xc, y1, z1), (xc, y1, z0)]
        if rng.random() < 0.25:
            quad[3] = quad[2]
        verts += quad
        faces.append(quad)
    nva = 2 * nfaces
    nvb = len(verts) - nva
    ov.u16(at, rng.randrange(1 << 16))
    ov.u16(at + 2, nva)
    ov.u16(at + 4, nvb)
    ov.u16(at + 6, nfaces)
    ov.u32(at + 8, rng.randrange(1 << 32))
    xs, ys, zs = [v[0] for v in verts], [v[1] for v in verts], [v[2] for v in verts]
    for k, value in enumerate((max(xs), min(xs), max(ys), min(ys), max(zs), min(zs))):
        ov.u16(at + 0xC + 2 * k, value & 0xFFFF)
    ov.put(at + 0x18, rng.randbytes(4))
    for k, (x, y, z) in enumerate(verts):
        ov.u16(at + 0x1C + 8 * k, x & 0xFFFF)
        ov.u16(at + 0x1E + 8 * k, y & 0xFFFF)
        ov.u16(at + 0x20 + 8 * k, z & 0xFFFF)
        ov.u16(at + 0x22 + 8 * k, rng.randrange(1 << 16))
    cursor = at + 0x1C + 8 * len(verts)
    addrs = []
    for f, quad in enumerate(faces):
        stride = rng.choice([0x1C, 0x20, 0x24, 0x28])
        flags = 1 | (0x10 if quad[3] == quad[2] and rng.random() < 0.8 else 0) | rng.choice([0, 0x40, 0x80, 0xC0, 0x20 if rng.random() < 0.1 else 0])
        ov.put(cursor, rng.randbytes(stride))
        ov.u16(cursor, flags)
        ov.u16(cursor + 2, stride)
        for k in range(4):
            ov.u8(cursor + 4 + k, 4 * f + k)
        ov.u16(cursor + 0xE, (rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xE]) << 9) | rng.randrange(1 << 9))
        addrs.append(cursor)
        cursor += stride
    return cursor - at, addrs, faces


def put_in_model(ov: Ov, at: int, model: int, type_id: int, kind: int, rng, elements=0, holder=0):
    ov.put(at, rng.randbytes(0x24))
    ov.u32(at, elements)
    ov.u32(at + 4, model)
    ov.u8(at + 8, type_id)
    ov.u8(at + 9, kind)
    ov.u32(at + 0x20, holder)


def turn_of(flags: int, rotation: int) -> int:
    """The turn the pair routine derives for a face (its element's flags byte and the piece's rotation)."""
    f = flags - 256 if flags & 0x80 else flags
    t = f if f & 1 else 2 - f
    t = (rotation + t) & 3
    return t if t & 1 else 2 - t


def invert(turn: int, point, off_x: int, off_z: int, off_y: int):
    """The local (x, y, z) that the routine turns into the ring point (x, y, z) at this turn."""
    x, y, z = point
    if turn == 0:
        px, pz = off_x - x, off_z - z
    elif turn == 1:
        px, pz = off_z - x, z - off_x
    elif turn == 2:
        px, pz = x - off_x, z - off_z
    else:
        px, pz = x - off_z, off_x - z
    return px, y - off_y, pz


# ---- gpool --------------------------------------------------------------------------------------------------------------------------
# slab: 0x0100 the polygon world of gen_specs_poly (lists 0x100, polygons 0x200, points 0x800), 0x1400 in-model, 0x1500 model, 0x2000 model-info block (0x1200),
# 0x3400 arena (0x1800)

BLOCK, IN0, MODEL0, ARENA2, ARENA2_SIZE = 0x2000, 0x1400, 0x1500, 0x3400, 0x1800
POOL_SLAB = 0x4D00


def pool_regions(slab):
    return [(slab, 0x3300), (slab + ARENA2, ARENA2_SIZE)] + STATIC_WINDOWS


def pool_seed(env, rng):
    env.alloc_base = ARENA2
    env.alloc_size = ARENA2_SIZE
    return {}


SLAB_BYTES["gpool"] = POOL_SLAB
SEEDS["gpool"] = pool_seed
EXTRA_REGIONS["gpool"] = pool_regions
READ_ONLY["gpool"] = STATIC_READS


def pool_state(env, ov, rng, index=None, with_block=True, count=None):
    """The pool's statics: the index of the block being filled, the table (blocks before it are anything; the current one is the slab's block or none)."""
    index = rng.choice([0, 1, 5, 20, 99]) if index is None else index
    ov.u32(POOL_INDEX, index)
    for k in range(100):
        ov.u32(POOL_TABLE + 4 * k, rng.choice([0, 0, rng.randrange(1 << 32)]) if k != index else 0)
    if with_block and rng.random() < 0.6:
        ov.u32(POOL_TABLE + 4 * index, env.slab + BLOCK)
        ov.u32(env.slab + BLOCK, rng.choice([0, 1, 5, 50, 98, 99]) if count is None else count)
    return index


@spec(0x0043F730, "ModelInfo_ClearTable", "gpool")
def gen_clear_table(env, n):
    rng = env.rng
    ov = Ov()
    ov.u32(POOL_INDEX, rng.randrange(0, 101))
    for k in range(100):
        ov.u32(POOL_TABLE + 4 * k, rng.choice([0, 0, 0, rng.randrange(1, 1 << 32)]))
    return [rng.choice([0, 0, 1, 0x100, 0x101, 0xFF])], ov.pokes()


@spec(0x0043F7B0, "EditorInModel_KillModelInfo", "gpool")
def gen_kill_model_info(env, n):
    rng = env.rng
    ov = Ov()
    index = rng.choice([0, 1, 2, 7, 50, 99, 100])
    ov.u32(POOL_INDEX, index)
    for k in range(101):
        ov.u32(POOL_TABLE + 4 * k, rng.choice([0, rng.randrange(1, 1 << 32), rng.randrange(1, 1 << 32)]))
    return [], ov.pokes()


@spec(0x0043F820, "EditorInModel_NewModelInfo", "gpool")
def gen_new_model_info(env, n):
    rng = env.rng
    ov = Ov()
    index = pool_state(env, ov, rng, index=rng.choice([0, 1, 5, 20, 99, 99, 100]))
    return [], ov.pokes()


@spec(0x0043F8E0, "EditorInModel_Construct", "gpool", this=lambda env: env.slab + IN0)
def gen_in_construct(env, n):
    ov = Ov()
    ov.put(env.slab + IN0, env.rng.randbytes(0x24))
    return [], ov.pokes()


def holder_with_polys(env, ov, rng, at):
    """A face list holder (0xc bytes) with a few polygons of points from the polygon world of the slab (offsets of gen_specs_poly: POLYS 0x200, POINTS 0x800)."""
    from gen_specs_poly import World, some_polys
    w = World(env)
    polys = some_polys(rng, w, rng.randrange(0, 4))
    w.ov.u32(at, polys[0] if polys else 0)
    w.link_polys(polys)
    for a, d in w.ov.bytes.items():
        ov.bytes[a] = d
    return at


@spec(0x0043F8F0, "EditorInModel_DestructThunk", "gpool", this=lambda env: env.slab + IN0)
def gen_in_destruct_thunk(env, n):
    rng = env.rng
    ov = Ov()
    holder = env.slab + 0x100
    holder_with_polys(env, ov, rng, holder)
    put_in_model(ov, env.slab + IN0, 0, 0, 0, rng, holder=holder if rng.random() < 0.8 else 0)
    return [], ov.pokes()


@spec(0x0043F900, "EditorInModel_Destruct", "gpool", this=lambda env: env.slab + IN0)
def gen_in_destruct(env, n):
    return gen_in_destruct_thunk(env, n)


def struct_u32(ov, address):
    return int.from_bytes(bytes(ov.bytes[address + i] for i in range(4)), 'little')


def theme_and_type(env, ov, rng):
    m = memo(env)
    theme = rng.randrange(4)
    ov.u32(THEME, theme)
    roll = rng.random()
    type_id = rng.choice(m.ids[theme]) if roll < 0.9 else rng.randrange(256)
    return type_id


@spec(0x0043F930, "EditorInModel_SetData", "gpool", this=lambda env: env.slab + IN0)
def gen_in_set_data(env, n):
    rng = env.rng
    ov = Ov()
    pool_state(env, ov, rng, index=rng.choice([0, 1, 5, 98, 99]), count=rng.choice([0, 1, 40, 90, 95, 99]))
    ov.u32(CELL_X, rng.choice([0x40, 0x80, 0x100, 0x180, 0x200, 0x400]))
    ov.u32(CELL_Z, rng.choice([0x40, 0x80, 0x100, 0x180, 0x200, 0x400]))
    type_id = theme_and_type(env, ov, rng)
    size, faces, quads = build_model(ov, env.slab + MODEL0, rng, rng.randrange(1, 8))
    ov.put(env.slab + IN0, rng.randbytes(0x24))
    # the model's extent as the editor would store it
    cell_x = struct_u32(ov, CELL_X)
    # (extents whose remainder passes cell - 4 now and then, and a negative depth)
    near = lambda cell: cell * rng.randrange(0, 4) + rng.choice([0, 1, 2, 5, cell - 3, cell - 2, cell - 1])    # noqa: E731
    xmax = near(cell_x) // 2 + rng.randrange(0, 100)
    xmin = -(near(cell_x) - near(cell_x) // 2) - rng.randrange(0, 100)
    ymax = rng.randrange(0, 900) if rng.random() < 0.8 else -rng.randrange(0, 600)
    ymin = -rng.randrange(0, 200)
    zmax = near(cell_x) + rng.choice([0, 0, 1, rng.randrange(0, 50)])
    zmin = rng.choice([0, 0, -rng.randrange(0, 40), rng.randrange(-300, 300)])
    for k, value in enumerate((xmax, xmin, ymax, ymin, zmax, zmin)):
        ov.u16(env.slab + MODEL0 + 0xC + 2 * k, value & 0xFFFF)
    return [env.slab + MODEL0, type_id], ov.pokes()


@spec(0x0043FDA0, "EditorInModel_SetTransparency", "gpool", this=lambda env: env.slab + IN0)
def gen_in_set_transparency(env, n):
    rng = env.rng
    ov = Ov()
    size, faces, quads = build_model(ov, env.slab + MODEL0, rng, rng.randrange(0, 8))
    put_in_model(ov, env.slab + IN0, env.slab + MODEL0, 0, 0, rng)
    return [rng.choice([0, 1, 0x100, 0xFF])], ov.pokes()


@spec(0x004436A0, "GenObjs_NewZeroed", "gpool")
def gen_new_zeroed(env, n):
    rng = env.rng
    return [rng.choice([0, 1, 3, 4, 5, 0x18, 0x1C, 0x2C, 0xC, 0x100])], {}


@spec(0x004436D0, "GenObjs_Delete", "gpool")
def gen_delete(env, n):
    return [env.rng.randrange(1 << 32)], {}


# ---- gring and gnode: the out-model's statics and the ring of nodes -------------------------------------------------------------------
# gring: FaceRemoval-free, one big arena for the ring (0x8c00), the tables (0x400, 0x100); gnode: a ring table of which only the first 64 nodes are real

RING_ARENA = 0x0400
RING_ARENA_BYTES = 0x9400


def ring_regions(slab):
    return [(slab, 0x100), (slab + RING_ARENA, RING_ARENA_BYTES)] + STATIC_WINDOWS


def ring_seed(env, rng):
    env.alloc_base = RING_ARENA
    env.alloc_size = RING_ARENA_BYTES
    return {}


SLAB_BYTES["gring"] = RING_ARENA + RING_ARENA_BYTES + 0x100
SEEDS["gring"] = ring_seed
EXTRA_REGIONS["gring"] = ring_regions
READ_ONLY["gring"] = STATIC_READS


def lights_state(ov, rng, theme=None, rows=None):
    ov.u32(THEME, rng.randrange(4) if theme is None else theme)
    ov.u32(ROW_SET, rng.randrange(5) if rows is None else rows)
    ov.u32(GRID_W, rng.choice([16, 24, 30, 48]))
    ov.u32(GRID_D, rng.choice([16, 24, 30, 18]))
    ov.u32(CELL_X, rng.choice([0x40, 0x80, 0x100, 0x200, 0x400]))


@spec(0x0043FE00, "EditorOutModel_Init", "gring")
def gen_out_init(env, n):
    rng = env.rng
    ov = Ov()
    lights_state(ov, rng)
    ov.u32(FALLOFF_PTR, rng.choice([0, rng.randrange(1, 1 << 32)]))
    ov.u32(SHADE_PTR, rng.choice([0, rng.randrange(1, 1 << 32)]))
    ov.u32(LIGHT_C, rng.randrange(0, 4000))
    ov.u32(LIGHT_A, rng.randrange(0, 200))
    for k in range(12):
        ov.u32(LIGHT_POS + 12 * k, rng.randrange(1 << 32))
        ov.u32(LIGHT_POS + 4 + 12 * k, rng.randrange(1 << 32))
        ov.u32(LIGHT_POS + 8 + 12 * k, rng.randrange(1 << 32))
        ov.u32(LIGHT_DIR + 12 * k, rng.randrange(1 << 32))
        ov.u32(LIGHT_DIR + 4 + 12 * k, rng.randrange(1 << 32))
        ov.u32(LIGHT_DIR + 8 + 12 * k, rng.randrange(1 << 32))
    ov.u32(RING_CURSOR, rng.randrange(1 << 32))
    ov.u32(RING_USED, rng.randrange(1 << 32))
    return [], ov.pokes()


@spec(0x0043FF30, "EditorOutModel_BuildTables", "gring")
def gen_build_tables(env, n):
    rng = env.rng
    ov = Ov()
    ov.u32(FALLOFF_PTR, rng.choice([0, rng.randrange(1, 1 << 32)]))
    ov.u32(SHADE_PTR, rng.choice([0, rng.randrange(1, 1 << 32)]))
    ov.u32(LIGHT_C, rng.choice([0, 1, 10, 100, rng.randrange(0, 4000), rng.randrange(0, 1 << 32)]))
    ov.u32(LIGHT_A, rng.choice([0, 1, 50, rng.randrange(0, 200), rng.randrange(0, 1 << 32)]))
    return [], ov.pokes()


@spec(0x00440010, "EditorOutModel_Cleanup", "gring")
def gen_out_cleanup(env, n):
    rng = env.rng
    ov = Ov()
    for a in (FALLOFF_PTR, SHADE_PTR, RING_TABLE):
        ov.u32(a, rng.choice([0, rng.randrange(1, 1 << 32)]))
    return [], ov.pokes()


# gnode: the ring table is placed so that its first 64 nodes lie in the captured window (the scan of the cases never goes beyond)
NODE_RING = 0x0100


def node_regions(slab):
    return [(slab, 0x600), (slab + NODE_RING + 0x8800, 0x400)] + STATIC_WINDOWS


SLAB_BYTES["gnode"] = 0x9100
SEEDS["gnode"] = lambda env, rng: {}
EXTRA_REGIONS["gnode"] = node_regions
READ_ONLY["gnode"] = []


def ring_state(env, ov, rng, cursor=None, free_at=None, window=64):
    """Nodes 0..window-1 in use (an area or the live marker) except some free ones; returns the cursor."""
    table = env.slab + NODE_RING
    ov.u32(RING_TABLE, table)
    cursor = rng.randrange(0, 48) if cursor is None else cursor
    ov.u32(RING_CURSOR, cursor)
    ov.u32(RING_USED, rng.randrange(0, 0x900))
    free = set(free_at) if free_at is not None else set(rng.sample(range(window), rng.randrange(1, 6)))
    # (a free node within reach of the scan, so it ends inside the window)
    free.add((cursor + rng.randrange(0, 12)) % window)
    for k in range(window):
        ov.u32(table + 16 * k, FREE if k in free else rng.choice([LIVE, rng.randrange(0, 1 << 20), rng.randrange(1 << 32) & ~1 if rng.random() < 0.1 else 0x1000]))
        ov.put(table + 16 * k + 4, rng.randbytes(12))
    return cursor


@spec(0x00440060, "EditorOutModel_AllocNode", "gnode")
def gen_alloc_node(env, n):
    rng = env.rng
    ov = Ov()
    ring_state(env, ov, rng)
    if rng.random() < 0.25:
        # the cursor near the end of the ring: nodes up to the end are in use and the scan wraps to the free ones at the start
        table = env.slab + NODE_RING
        cursor = 0x8C0 - rng.randrange(1, 9)
        ov.u32(RING_CURSOR, cursor)
        for k in range(0x880, 0x8C0):
            ov.u32(table + 16 * k, rng.choice([LIVE, rng.randrange(0, 1 << 20)]))
        free = {rng.randrange(0, 10), rng.randrange(0, 10)}
        for k in range(64):
            ov.u32(table + 16 * k, FREE if k in free else LIVE)
    return [], ov.pokes()


@spec(0x00440170, "EditorOutModel_SpareNodes", "gnode")
def gen_spare_nodes(env, n):
    rng = env.rng
    ov = Ov()
    ov.u32(RING_USED, rng.choice([0, 1, 0x897, 0x898, 0x899, 0x8BF, 0x8C0, 0x8C1, 0x900, rng.randrange(0, 0x1000), 0xFFFFFFFF, 0x7FFFFFFF]))
    return [], ov.pokes()


# ---- ggap -----------------------------------------------------------------------------------------------------------------------
# slab: 0x0100 the gap member (0x58), 0x0200 its record, 0x0300 the target item, 0x0400 the buffer (0x900)

GAP, GAP_RECORD, GAP_TARGET, GAP_BUFFER = 0x0100, 0x0200, 0x0300, 0x0400


def gap_regions(slab):
    return [(slab, 0xD00), (0x00533940, 0x10)] + STATIC_WINDOWS


SLAB_BYTES["ggap"] = 0xD00
SEEDS["ggap"] = lambda env, rng: {}
EXTRA_REGIONS["ggap"] = gap_regions
READ_ONLY["ggap"] = []


def gap_member(env, ov, rng, rotation=None, built=False):
    """A gap member: the record it points to, the target item, the grid and cell sizes of the park."""
    g = env.slab + GAP
    ov.put(g, rng.randbytes(0x58))
    ov.u32(g + 4, env.slab + GAP_RECORD)
    ov.put(env.slab + GAP_RECORD, rng.randbytes(0x20))
    ov.u8(env.slab + GAP_RECORD + 5, rng.randrange(0, 40))
    ov.u8(env.slab + GAP_RECORD + 6, rng.randrange(0, 5))
    ov.u8(env.slab + GAP_RECORD + 7, rng.randrange(0, 40))
    ov.u8(env.slab + GAP_RECORD + 8, rng.randrange(0, 256))
    ov.u8(env.slab + GAP_RECORD + 10, rng.randrange(0, 256))
    ov.u32(GRID_W, rng.choice([16, 24, 30, 48]))
    ov.u32(GRID_D, rng.choice([16, 24, 30, 18]))
    ov.u32(CELL_X, rng.choice([0x40, 0x80, 0x100, 0x180, 0x200]))
    ov.u32(CELL_Z, rng.choice([0x40, 0x80, 0x100, 0x180, 0x200]))
    ov.u16(0x00533944, rng.randrange(1 << 16))
    ov.u32(g + 0x14, env.slab + GAP_TARGET)
    ov.put(env.slab + GAP_TARGET, rng.randbytes(0x20))
    ov.u32(g + 0x10, env.slab + GAP_BUFFER)
    ov.u8(g + 0x38, rng.choice([0, 1, 1]))
    ov.u8(g + 0x39, rng.choice([0, 1]))
    ov.u32(g + 0x40, 0x1C)
    ov.u32(g + 0x48, rng.choice([0x48, 0x48, 0x40, 0x10]))
    ov.u32(g + 0x4C, rng.choice([0x31, 0x31, 0x20]))
    ov.u32(g + 0x3C, 0x864)
    ov.u32(g + 0x50, rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, 9]) if rotation is None else rotation)
    ov.u32(g + 0x54, rng.randrange(1 << 16))
    # the edges: a box on the grid
    x0 = rng.randrange(-2000, 2000)
    z0 = rng.randrange(-2000, 2000)
    ov.u32(g + 0x20, x0 & 0xFFFFFFFF)
    ov.u32(g + 0x28, (x0 + rng.randrange(1, 2000)) & 0xFFFFFFFF)
    ov.u32(g + 0x24, z0 & 0xFFFFFFFF)
    ov.u32(g + 0x2C, (z0 + rng.randrange(1, 2000)) & 0xFFFFFFFF)
    return g


@spec(0x004410B0, "LevelGen_InitGapMember", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_init(env, n):
    ov = Ov()
    ov.put(env.slab + GAP, env.rng.randbytes(0x58))
    return [], ov.pokes()


@spec(0x004410D0, "LevelGen_GapMemberNothing", "ggap")
def gen_gap_nothing(env, n):
    return [], {}


@spec(0x004410E0, "LevelGen_ComputeGapMemberEdges", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_edges(env, n):
    rng = env.rng
    ov = Ov()
    gap_member(env, ov, rng)
    if rng.random() < 0.05:
        ov.u32(env.slab + GAP + 4, env.slab + GAP_RECORD)
    small = lambda: rng.choice([0, 1, 2, 3, 5, 10, 0xFFFFFFFF])    # noqa: E731
    return [rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, 7]), small(), small()], ov.pokes()


@spec(0x00441240, "LevelGen_SetGapMemberConstants", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_constants(env, n):
    ov = Ov()
    ov.put(env.slab + GAP, env.rng.randbytes(0x58))
    return [], ov.pokes()


@spec(0x00441270, "GapMember_BuildPrims", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_prims(env, n):
    rng = env.rng
    ov = Ov()
    gap_member(env, ov, rng)
    ov.put(env.slab + GAP_BUFFER, rng.randbytes(0x100))
    return [env.slab + GAP_BUFFER], ov.pokes()


@spec(0x00441400, "GapMember_BuildHeader", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_header(env, n):
    rng = env.rng
    ov = Ov()
    gap_member(env, ov, rng)
    return [], ov.pokes()


@spec(0x00441630, "LevelGen_SetGapMemberActive", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_active(env, n):
    rng = env.rng
    ov = Ov()
    gap_member(env, ov, rng)
    return [rng.choice([0, 1, 0x100, 0xFF, 2])], ov.pokes()


@spec(0x00441640, "GapMember_SetVisible", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_visible(env, n):
    rng = env.rng
    ov = Ov()
    gap_member(env, ov, rng)
    ov.put(env.slab + GAP_BUFFER, rng.randbytes(0x20))
    return [rng.choice([0, 1, 0x100, 0xFF, 2])], ov.pokes()


@spec(0x00441670, "GapMember_ReturnThis", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_return_this(env, n):
    return [], {}


@spec(0x00441680, "GapMember_NothingB", "ggap")
def gen_gap_nothing_b(env, n):
    return [], {}


@spec(0x00441690, "LevelGen_SetGapMemberDefaults", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_defaults(env, n):
    ov = Ov()
    ov.put(env.slab + GAP, env.rng.randbytes(0x58))
    return [], ov.pokes()


@spec(0x004416B0, "GapPiece_BuildQuad", "ggap", this=lambda env: env.slab + GAP)
def gen_gap_quad(env, n):
    rng = env.rng
    ov = Ov()
    gap_member(env, ov, rng)
    ov.u32(env.slab + GAP + 0x14, rng.choice([0x1C, 0x28, 0x40]))
    ov.put(env.slab + GAP_BUFFER, rng.randbytes(0x100))
    return [env.slab + GAP_BUFFER], ov.pokes()


# ---- gout: the out-model --------------------------------------------------------------------------------------------------------
# slab: 0x0100/0x0400/0x0700 out-models (0x238 each), 0x0A00.. in-models (0x40 apart), 0x0B00/0x0E00/0x1100 elements (0x2c each), 0x1400/0x1C00/0x2400 models, 0x2C00 the ring's
# first 64 nodes, 0x3000 the face-removal pool (0x480), 0x3500 falloff table, 0x3900 shade table, 0x3A00 the output buffer (0x1000), 0x4A00 the fog header

OUTS = [0x0100, 0x0400, 0x0700]
INS = [0x0A00, 0x0A40, 0x0A80]
ELEMS = [0x0B00, 0x0E00, 0x1100]
MODELS = [0x1400, 0x1C00, 0x2400]
RINGW, POOLB, FALLOFF, SHADE, OUTBUF, FOG = 0x2C00, 0x3000, 0x3500, 0x3900, 0x3A00, 0x4A00
SPECIAL_ARRAYS = [0x005614A8, 0x00561490, 0x00561174]
FACE_POOL = (0x005611B0, 0x00561164, 0x0056115C, 0x00561190)


def out_regions(slab):
    return [(slab, 0x4B00)] + STATIC_WINDOWS


def out_seed(env, rng):
    return {}


SLAB_BYTES["gout"] = 0x4B00
SEEDS["gout"] = out_seed
EXTRA_REGIONS["gout"] = out_regions
READ_ONLY["gout"] = STATIC_READS


class Scene:
    """Pieces (a model, its in-model with elements, an out-model with a node per element) laid out in the slab, and the statics they use."""

    def __init__(self, env):
        self.env = env
        self.rng = env.rng
        self.ov = Ov()
        self.slab = env.slab
        self.next_node = 1
        self.pieces = []
        self.statics()

    # -- statics --
    def statics(self):
        ov, rng, slab = self.ov, self.rng, self.slab
        ov.u32(THEME, rng.randrange(4))
        ov.u32(CELL_X, rng.choice([0x40, 0x80, 0x100, 0x200]))
        ov.u32(CELL_Z, rng.choice([0x40, 0x80, 0x100, 0x200]))
        ov.u32(FACE_POOL[0], slab + POOLB)
        ov.u32(FACE_POOL[1], slab + POOLB)
        ov.u8(FACE_POOL[2], 0)
        ov.u32(FACE_POOL[3], 0)
        ov.u32(RING_TABLE, slab + RINGW)
        ov.u32(RING_CURSOR, rng.randrange(0, 4))
        ov.u32(RING_USED, rng.randrange(8, 0x300))
        for k in range(64):
            ov.u32(slab + RINGW + 16 * k, FREE)
            ov.put(slab + RINGW + 16 * k + 4, rng.randbytes(12))
        # lights and the tables the shades come from
        count = rng.choice([0, 1, 2, 3, 4])
        ov.u32(LIGHT_COUNT, count)
        ov.u32(LIGHT_B, rng.randrange(1, 1 << 14))
        ov.u32(LIGHT_C, rng.randrange(0, 4000))
        ov.u32(LIGHT_A, rng.randrange(0, 200))
        reach = rng.choice([40, 300, 3000])
        for k in range(6):
            ov.u32(LIGHT_POS + 12 * k, rng.randrange(-reach, reach) & 0xFFFFFFFF)
            ov.u32(LIGHT_POS + 4 + 12 * k, 0xFFFFFC18)
            ov.u32(LIGHT_POS + 8 + 12 * k, rng.randrange(-reach, reach) & 0xFFFFFFFF)
        ov.u32(FALLOFF_PTR, slab + FALLOFF)
        ov.u32(SHADE_PTR, slab + SHADE)
        ov.put(slab + FALLOFF, rng.randbytes(0x400))
        ov.put(slab + SHADE, rng.randbytes(0x100))
        for base in SPECIAL_ARRAYS:
            for k in range(5):
                ov.u32(base + 4 * k, rng.choice([0, 0, rng.randrange(256)]))

    # -- nodes --
    def node(self, area=None, alive=None, orient=None, element=0):
        rng = self.rng
        at = self.slab + RINGW + 16 * self.next_node
        self.next_node += 1
        self.ov.u32(at, rng.choice([LIVE, rng.randrange(300, 20000)]) if area is None else area)
        self.ov.u8(at + 4, rng.choice([0, 1, 1, 1]) if alive is None else alive)
        self.ov.u8(at + 5, rng.randrange(256) if orient is None else orient)
        self.ov.u32(at + 8, element)
        self.ov.u32(at + 0xC, 0)
        return at

    # -- a piece --
    def piece(self, k, nelem, extra=0, rot=None, flags=None, ring_faces=None, kind=None, state=None, type_id=None, offsets=None, nodes_alive=None):
        """ring_faces: for element i, a list of ring-space points (x, y, z) to invert into the element's local points (or None for random ones)."""
        ov, rng, slab = self.ov, self.rng, self.slab
        rot = rng.randrange(4) if rot is None else rot
        nfaces = max(1, nelem + extra)
        size, faces, quads = build_model(ov, slab + MODELS[k], rng, nfaces)
        off_z = rng.randrange(-300, 300) if offsets is None else offsets[0]
        off_y = rng.randrange(-300, 300) if offsets is None else offsets[1]
        off_x = rng.randrange(-300, 300) if offsets is None else offsets[2]
        elements = []
        for i in range(nelem):
            at = slab + ELEMS[k] + 0x2C * i
            f = (rng.choice([0, 1, 2, 3, 0xFF, 0xFE, rng.randrange(256)]) if flags is None or flags[i] is None else flags[i])
            pts = ring_faces[i] if ring_faces and ring_faces[i] else None
            n = len(pts) if pts else rng.choice([3, 4])
            ov.u8(at, f)
            ov.u8(at + 1, rng.randrange(256))
            ov.u8(at + 2, n)
            ov.u8(at + 3, rng.randrange(256))
            ov.u32(at + 8, faces[i])
            turn = turn_of(f, rot)
            locals_ = []
            for j in range(n):
                if pts:
                    locals_.append(invert(turn, pts[j], off_x, off_z, off_y))
                else:
                    locals_.append((rng.randrange(-100, 100), rng.randrange(-100, 0), rng.randrange(-8, 8) * 8))
            locals_.append(locals_[0])
            for j, (px, py, pz) in enumerate(locals_):
                ov.u16(at + 0xC + 6 * j, px & 0xFFFF)
                ov.u16(at + 0xE + 6 * j, py & 0xFFFF)
                ov.u16(at + 0x10 + 6 * j, pz & 0xFFFF)
            for j in range(len(locals_), 5):
                ov.put(at + 0xC + 6 * j, rng.randbytes(6))
            elements.append(at)
        for i, at in enumerate(elements):
            ov.u32(at + 4, elements[i + 1] if i + 1 < len(elements) else 0)
        m = self.rng
        in_at = slab + INS[k]
        kind = rng.choice([0, 0, 0, 8, 0x10, 0x20, 0x28, 0x18, 1, 0x30]) if kind is None else kind
        type_id = rng.randrange(256) if type_id is None else type_id
        put_in_model(ov, in_at, slab + MODELS[k], type_id, kind, rng, elements=elements[0] if elements else 0)
        ov.u8(in_at + 0xD, rng.randrange(0, 4))
        out_at = slab + OUTS[k]
        ov.put(out_at, rng.randbytes(0x238))
        ov.u8(out_at, rng.randrange(256))
        ov.u8(out_at + 1, rng.choice([0, 1, 1, 2]) if state is None else state)
        ov.u8(out_at + 2, rng.randrange(256))
        ov.u8(out_at + 3, 0)
        ov.u8(out_at + 4, rng.randrange(0, 4))
        ov.u8(out_at + 0xB, rng.randrange(0, 128))
        ov.u8(out_at + 0xC, rot)
        ov.u8(out_at + 0xD, rng.choice([0, 1]))
        ov.u16(out_at + 0xE, rng.randrange(1 << 16))
        ov.u16(out_at + 0x10, 2 * nfaces)
        ov.u16(out_at + 0x12, 2 * nfaces)
        ov.u16(out_at + 0x14, nfaces)
        ov.u16(out_at + 0x21C, off_z & 0xFFFF)
        ov.u16(out_at + 0x21E, off_y & 0xFFFF)
        ov.u16(out_at + 0x220, off_x & 0xFFFF)
        ov.u32(out_at + 0x224, in_at)
        ov.u32(out_at + 0x228, slab + OUTBUF)
        ov.u32(out_at + 0x22C, slab + FOG)
        nodes = []
        for i, el in enumerate(elements):
            nodes.append(self.node(element=el, alive=None if nodes_alive is None else nodes_alive[i]))
        for i, nd in enumerate(nodes):
            ov.u32(nd + 0xC, nodes[i + 1] if i + 1 < len(nodes) else 0)
        ov.u32(out_at + 0x218, nodes[0] if nodes else 0)
        piece = dict(out=out_at, inm=in_at, model=slab + MODELS[k], elements=elements, nodes=nodes, faces=faces, rot=rot, offsets=(off_x, off_y, off_z), nfaces=nfaces)
        self.pieces.append(piece)
        return piece


def facing_pair(scene, rng, quiet=False):
    """Two pieces whose first elements lie in one plane facing each other (so the pair routine builds both rings and takes their overlap)."""
    rot_a, rot_b = rng.randrange(4), rng.randrange(4)
    f_a = rng.choice([0, 1, 2, 3])
    t_a = turn_of(f_a, rot_a)
    f_b = rng.choice([0, 1, 2, 3])
    for _ in range(60):
        f_b = rng.choice([0, 1, 2, 3])
        if turn_of(f_b, rot_b) == t_a ^ 2:
            break
        rot_b = rng.randrange(4)
    zt = rng.randrange(-8, 8) * 8
    x0, y0 = rng.randrange(-100, 100) * 4, -rng.randrange(1, 20) * 16
    w, h = rng.randrange(2, 20) * 16, rng.randrange(2, 20) * 16
    ra = [(x0, y0, zt), (x0 + w, y0, zt), (x0 + w, y0 - h, zt), (x0, y0 - h, zt)]
    ox, oy = rng.randrange(-8, 12) * 16, rng.randrange(-8, 12) * 16
    wb, hb = rng.randrange(2, 20) * 16, rng.randrange(2, 20) * 16
    rb = [(x0 + ox, y0 + oy, zt + rng.choice([0, 0, 1, -1])), (x0 + ox + wb, y0 + oy, zt), (x0 + ox + wb, y0 + oy - hb, zt), (x0 + ox, y0 + oy - hb, zt)]
    if rng.random() < 0.3:
        ra = ra[:3]
    if rng.random() < 0.3:
        rb = rb[:3]
    nelem_a, nelem_b = rng.randrange(1, 4), rng.randrange(1, 4)
    # (the routine reverses a ring when its turn has bit 1 clear: the elements hold the ring the other way round for those, so both rings come out running the same way,
    # as the editor's faces do; a clipping of rings that run opposite ways loops for ever)
    pre = lambda ring, turn: ring if turn & 2 else [ring[0]] + ring[:0:-1]    # noqa: E731
    if rng.random() < 0.3:
        ra, rb = [ra[0]] + ra[:0:-1], [rb[0]] + rb[:0:-1]
    pa = scene.piece(0, nelem_a, rng.randrange(0, 2), rot=rot_a, flags=[f_a] + [None] * 3, ring_faces=[pre(ra, t_a)] + [None] * 3)
    pb = scene.piece(1, nelem_b, rng.randrange(0, 2), rot=rot_b, flags=[f_b] + [None] * 3, ring_faces=[pre(rb, turn_of(f_b, rot_b))] + [None] * 3)
    return pa, pb, t_a, turn_of(f_b, rot_b)


@spec(0x00440190, "LevelGen_ConstructPieceObject", "gout", this=lambda env: env.slab + OUTS[0])
def gen_out_construct(env, n):
    rng = env.rng
    sc = Scene(env)
    p = sc.piece(0, rng.randrange(0, 5), rng.randrange(0, 2))
    # the ring: the nodes the constructor allocates (a run of free nodes from the cursor)
    sc.ov.u32(RING_CURSOR, rng.randrange(0, 10))
    for k in range(64):
        sc.ov.u32(env.slab + RINGW + 16 * k, FREE)
    sc.ov.u8(p["inm"] + 0xD, rng.choice([0, 1, 2, 0x7F, 0x80, 0xFF]))
    sc.ov.u8(p["inm"] + 8, rng.randrange(256))
    return [p["inm"], rng.randrange(1 << 32)], sc.ov.pokes()


@spec(0x00440290, "EditorOutModel_ReleaseNodes", "gout", this=lambda env: env.slab + OUTS[0])
def gen_release_nodes(env, n):
    rng = env.rng
    sc = Scene(env)
    p = sc.piece(0, rng.randrange(0, 5), 0)
    sc.ov.u32(RING_USED, rng.randrange(len(p["nodes"]), 0x900))
    return [], sc.ov.pokes()


@spec(0x004402D0, "EditorOutModel_ShowNodes", "gout", this=lambda env: env.slab + OUTS[0])
def gen_show_nodes(env, n):
    rng = env.rng
    sc = Scene(env)
    sc.piece(0, rng.randrange(0, 5), 0)
    return [], sc.ov.pokes()


@spec(0x004402F0, "EditorOutModel_HideAgainst", "gout", this=lambda env: env.slab + OUTS[0])
def gen_hide_against(env, n):
    rng = env.rng
    sc = Scene(env)
    if rng.random() < 0.85:
        pa, pb, ta, tb = facing_pair(sc, rng)
        mode = tb if rng.random() < 0.8 else rng.randrange(4)
    else:
        pa = sc.piece(0, rng.randrange(0, 4), 0)
        pb = sc.piece(1, rng.randrange(0, 4), 0)
        mode = rng.randrange(4)
    sc.ov.u8(pa["out"] + 1, rng.choice([0, 1, 1, 1, 2]))
    sc.ov.u8(pb["out"] + 1, rng.choice([0, 1, 1, 1, 2]))
    return [pb["out"], mode], sc.ov.pokes()


@spec(0x004403C0, "EditorOutModel_HidePair", "gout")
def gen_hide_pair(env, n):
    rng = env.rng
    sc = Scene(env)
    pa, pb, ta, tb = facing_pair(sc, rng)
    mode = tb if rng.random() < 0.85 else rng.randrange(4)
    na, nb = pa["nodes"][0], pb["nodes"][0]
    sc.ov.u8(na + 4, 1)
    sc.ov.u8(nb + 4, 1)
    return [pa["out"], pb["out"], na, nb, mode], sc.ov.pokes()


@spec(0x004407C0, "EditorOutModel_RemoveAllHiddenFaces", "gout", this=lambda env: env.slab + OUTS[0])
def gen_remove_hidden(env, n):
    rng = env.rng
    sc = Scene(env)
    pa, pb, ta, tb = facing_pair(sc, rng)
    pc = sc.piece(2, rng.randrange(0, 3), 0)
    others = [pb, pc]
    count = rng.randrange(0, 3)
    sc.ov.u8(pa["out"] + 3, count)
    sc.ov.u8(pa["out"] + 4, rng.randrange(0, 3))
    for i in range(count):
        sc.ov.u32(pa["out"] + 0x18 + 4 * i, others[i]["out"])
    sc.ov.u8(pa["out"] + 1, rng.choice([0, 1, 2]))
    sc.ov.u8(pb["out"] + 1, rng.choice([0, 1, 2]))
    return [], sc.ov.pokes()


@spec(0x00440920, "EditorOutModel_SetModelDataSize", "gout", this=lambda env: env.slab + OUTS[0])
def gen_data_size(env, n):
    rng = env.rng
    sc = Scene(env)
    p = sc.piece(0, rng.randrange(0, 5), rng.randrange(0, 3))
    return [rng.choice([0, 1, 1, 0x100])], sc.ov.pokes()


@spec(0x004409E0, "LevelGen_LoadModelDescriptor", "gout", this=lambda env: env.slab + OUTS[0])
def gen_create_data(env, n):
    rng = env.rng
    sc = Scene(env)
    p = sc.piece(0, rng.randrange(0, 5), rng.randrange(0, 3))
    sc.ov.put(env.slab + OUTBUF, rng.randbytes(0x40))
    sc.ov.u16(p["out"] + 0x10, 2 * p["nfaces"])
    return [env.slab + OUTBUF, rng.choice([0, 1, 1, 0x100])], sc.ov.pokes()


@spec(0x00440E80, "EditorOutModel_MarkHiddenFaces", "gout", this=lambda env: env.slab + OUTS[0])
def gen_mark_faces(env, n):
    rng = env.rng
    sc = Scene(env)
    p = sc.piece(0, rng.randrange(0, 5), rng.randrange(0, 3), kind=rng.choice([0, 1, 0x10, 0x20, 8, 0, 0x30]))
    # the built model: header and vertices, then faces of the model's size
    size = 0x1C + 8 * 4 * p["nfaces"]
    sc.ov.put(env.slab + OUTBUF, rng.randbytes(0x1000))
    sc.ov.u16(env.slab + OUTBUF + 2, 2 * p["nfaces"])
    sc.ov.u16(env.slab + OUTBUF + 4, 2 * p["nfaces"])
    cursor = env.slab + OUTBUF + size
    mod = p["model"]
    face = mod + 0x1C + 8 * 4 * p["nfaces"]
    for f in range(p["nfaces"]):
        stride = env_stride(env, sc, face)
        sc.ov.u16(cursor + 2, stride)
        cursor += stride
        face += stride
    wire = rng.choice([0, 1, 1])
    return [wire, rng.choice([0, env.slab + OUTBUF]), rng.randrange(4)], sc.ov.pokes()


def env_stride(env, sc, face):
    """The size the scene gave a model face (read back from the overlay)."""
    low = sc.ov.bytes.get(face + 2, 0)
    high = sc.ov.bytes.get(face + 3, 0)
    return low | (high << 8)


@spec(0x00441050, "EditorOutModel_SetFog", "gout", this=lambda env: env.slab + OUTS[0])
def gen_set_fog(env, n):
    rng = env.rng
    sc = Scene(env)
    sc.piece(0, 1, 0)
    sc.ov.put(env.slab + FOG, rng.randbytes(0x40))
    return [rng.choice([0, 1, 0x100, 0xFF]), rng.choice([0, rng.randrange(1 << 32), rng.randrange(1 << 24), 0x80000000, 0xFFFFFFFF])], sc.ov.pokes()


def gte_square(address):
    from opentony.memory import mem

    def make(index):
        def value(args):
            a = mem.u16(GTE_IN + 2 * index)
            a = a - 0x10000 if a & 0x8000 else a
            return (a * a) & 0xFFFFFFFF
        return value
    return make


def out_stubs(fc, env):
    make = gte_square(GTE_IN)
    # the GTE's square of the vector register (three halves): x, y and z squared into three words
    ensure(fc, GTE_SQRT, 0, writes=[(GTE_SQ, 4, make(0)), (GTE_SQ + 4, 4, make(1)), (GTE_SQ + 8, 4, make(2))])


GTE_SQRT = 0x004E3CE0


GROUP_STUBS: dict = {"gout": out_stubs}
