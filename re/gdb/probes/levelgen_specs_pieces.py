"""Phase 4.11c case generators, part 2: the floor table, the piece, floor, gap and start builders and the lists they leave (levelgen.cpp, 0x0043a3e0..0x0043bbf0, 0x0043f1b0,
0x0043f400, 0x0043f5a0): groups `lgf` (floor table), `lgb` (builders), `lgl` (lists and flags).

A park is a small grid (2..8 cells either way); the source models' sizes are chosen so that a piece placed in a cell fits in the grid, so the loops never read a row that is not there.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from gen_specs_model import CELL_X, GRID_D, GRID_W
from levelgen_specs import (ARENA, BLOCK_FM, BLOCK_OM, BUILT_BASE, BUILT_STRIDE, CELLS_FLR, CELLS_SRC, GAP_BASE, ITEM_BASE, LG, LG_READS, LG_SIZE, MODEL_BASE,
                            PIECE_BASE, PIECE_STRIDE, RING, ROWS_FLR, ROWS_FM, ROWS_OM, ROWS_SRC, START_BASE, World, lg_regions, lg_stubs, put_gaps, put_pieces, put_starts, ring,
                            s16, HORSE_TYPE, START_MARKER, START_TYPES)
from pspool_specs import Ov, ensure, under_test
from gen_specs_model import FREE, LIVE, RING_USED

BUILD_ARENA = 0x8000


def fitting_cells(world, density, models, fill=0.0, layers_max=4):
    """The editor's cells with pieces that fit in the grid (and, with `fill`, empty layers: cells with a count but no piece); returns {(x, z): [(layer, model, turn)]}."""
    ov, rng = world.ov, world.rng
    placed = {}
    base = world.a(CELLS_SRC)
    for z in range(world.D):
        for x in range(world.W):
            cell = base + (z * world.W + x) * 16
            refs = [0xFFFF] * 5
            turns = [rng.randrange(4) for _ in range(5)]
            count = 0
            if rng.random() < fill:
                count = rng.randrange(0, 6)
            if models and rng.random() < density:
                m = rng.choice(models)
                turn = turns[0]
                w, d = (m["width"], m["depth"]) if turn % 2 == 0 else (m["depth"], m["width"])
                if x + w <= world.W and z + d <= world.D:
                    layer = rng.randrange(0, 3)
                    refs[layer] = m["index"]
                    count = max(count, layer + 1)
                    turns[layer] = turn
                    placed.setdefault((x, z), []).append((layer, m["index"], turn))
            for k in range(5):
                ov.u16(cell + 2 * k, refs[k])
                ov.u8(cell + 0xA + k, turns[k])
            ov.u8(cell + 0xF, count)
    return placed


def models_that_fit(world, count):
    """Source models whose footprint (either way round) fits in the grid."""
    models = world.make_models(count)
    for m in models:
        for key, value in (("width", min(m["width"], world.W)), ("depth", min(m["depth"], world.D))):
            m[key] = value
        world.ov.u8(m["at"] + 0xA, m["width"])
        world.ov.u8(m["at"] + 0xC, m["depth"])
    return models


# ---- lgf: the floor table -------------------------------------------------------------------------------------------------------

def lgf_regions(slab):
    return lg_regions(slab, (LG, LG_SIZE), (ROWS_SRC, 0x1100), (ROWS_FLR, 0x1100))


SLAB_BYTES["lgf"] = ARENA + 0x100
SEEDS["lgf"] = lambda env, rng: {}
EXTRA_REGIONS["lgf"] = lgf_regions
READ_ONLY["lgf"] = LG_READS


@spec(0x0043A3E0, "LevelGen_BuildFloorTable", "lgf", this=lambda env: env.slab + LG)
def gen_floor_table(env, n):
    rng = env.rng
    w = World(env, width=rng.choice([2, 4, 6, 8]), depth=rng.choice([2, 4, 6, 8]))
    w.object()
    w.maps(floor=True, out=False, flr_map=False)
    w.source()
    models = models_that_fit(w, rng.randrange(1, 6))
    fitting_cells(w, rng.choice([0.1, 0.3, 0.6]), models, fill=rng.choice([0.0, 0.5, 0.9]))
    # the fill pieces: the same id on every layer makes the merges likely
    if rng.random() < 0.6:
        v = rng.randrange(1, 8)
        for k in range(5):
            w.ov.u32(0x005614A8 + 4 * k, v)
    return [], w.ov.pokes()


# ---- lgb: the builders -------------------------------------------------------------------------------------------------------------

def lgb_regions(slab):
    return lg_regions(slab, (LG, LG_SIZE), (ROWS_SRC, 0x1100), (ROWS_FLR, 0x1100), (ROWS_OM, 0x1500), (ROWS_FM, 0x1500), (ITEM_BASE, 0x200), (RING, 0x400), (ARENA, BUILD_ARENA),
                      (PIECE_BASE, 0x1200), (GAP_BASE, 0x600), (START_BASE, 0x100), (BUILT_BASE, 0x400))


def lgb_seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = BUILD_ARENA
    return {}


SLAB_BYTES["lgb"] = ARENA + BUILD_ARENA + 0x100
SEEDS["lgb"] = lgb_seed
EXTRA_REGIONS["lgb"] = lgb_regions
READ_ONLY["lgb"] = LG_READS


def builder_world(env, floor_source=False):
    """A park the builders can run on: models without elements, cells that fit, empty maps, the ring free."""
    rng = env.rng
    w = World(env, width=rng.choice([2, 4, 4, 6, 8]), depth=rng.choice([2, 4, 4, 6, 8]))
    w.object()
    w.maps(floor=True, out=True, flr_map=True)
    w.source()
    ring(w)
    models = models_that_fit(w, rng.randrange(1, 5))
    placed = fitting_cells(w, rng.choice([0.1, 0.3, 0.5]), models)
    return w, models, placed


@spec(0x0043AB00, "LevelGen_BuildPieceObjects", "lgb", this=lambda env: env.slab + LG)
def gen_build_pieces(env, n):
    w, models, placed = builder_world(env)
    rng = w.rng
    # start and horse types among the models now and then
    for m in w.models:
        if rng.random() < 0.3:
            w.ov.u8(m["at"] + 8, rng.choice([w.ov_u32(START_TYPES), w.ov_u32(START_TYPES + 4), w.ov_u32(HORSE_TYPE)]))
    # the out-model map starts empty (release empties it; the pieces of an earlier build are none)
    w.ov.u32(w.lg + 0x3698, rng.choice([0, 0, 2, 5, 6]))
    return [], w.ov.pokes()


@spec(0x0043B410, "LevelGen_BuildGridPieceObjects", "lgb", this=lambda env: env.slab + LG)
def gen_build_floor(env, n):
    w, models, placed = builder_world(env)
    rng = w.rng
    # the floor table is the source: a random one made of the same models
    base = w.a(CELLS_FLR)
    for z in range(w.D):
        for x in range(w.W):
            cell = base + (z * w.W + x) * 16
            refs = [0xFFFF] * 5
            if rng.random() < 0.3:
                m = rng.choice(models)
                turn = rng.randrange(4)
                ww, dd = (m["width"], m["depth"]) if turn % 2 == 0 else (m["depth"], m["width"])
                if x + ww <= w.W and z + dd <= w.D:
                    refs[rng.randrange(0, 3)] = m["index"]
                    w.ov.u8(cell + 0xA + 0, turn)
            for k in range(5):
                w.ov.u16(cell + 2 * k, refs[k])
                w.ov.u8(cell + 0xA + k, rng.randrange(4))
            w.ov.u8(cell + 0xF, 5)
    return [], w.ov.pokes()


def place_piece_records(world, count, in_models=None, on_map=True, floor_map=False, built=False):
    """`count` pieces placed (non-overlapping where the map is used) on the park; returns the list of dicts."""
    ov, rng = world.ov, world.rng
    models = in_models or world.models
    pieces = []
    occupied = set()
    ring_nodes = 0
    for i in range(count):
        m = rng.choice(models)
        turn = rng.randrange(4)
        ww, dd = (m["width"], m["depth"]) if turn % 2 == 0 else (m["depth"], m["width"])
        x, z = rng.randrange(0, max(1, world.W - ww + 1)), rng.randrange(0, max(1, world.D - dd + 1))
        y = rng.randrange(0, 3)
        layers = m["layers"]
        slots = {(xx, zz, y + kk) for xx in range(x, x + ww) for zz in range(z, z + dd) for kk in range(layers)}
        if any(s[2] > 4 for s in slots) or (slots & occupied):
            continue
        occupied |= slots
        at = world.a(PIECE_BASE + PIECE_STRIDE * len(pieces))
        ov.put(at, rng.randbytes(0x238))
        ov.u8(at, 0)
        ov.u8(at + 1, rng.choice([0, 1, 2]))
        ov.u8(at + 2, rng.choice([0, 1]))
        ov.u8(at + 3, 0)
        ov.u8(at + 4, 0)
        ov.u8(at + 5, x)
        ov.u8(at + 6, y)
        ov.u8(at + 7, z)
        ov.u8(at + 8, ww)
        ov.u8(at + 9, layers)
        ov.u8(at + 0xA, dd)
        ov.u8(at + 0xB, rng.randrange(0, 4))
        ov.u8(at + 0xC, turn)
        ov.u32(at + 0x224, m["at"])
        ov.u32(at + 0x234, 0)
        ov.u32(at + 0x230, 0)
        ov.u32(at + 0x218, 0)
        ov.u16(at + 0x14, 0)
        if built:
            b = world.a(BUILT_BASE + BUILT_STRIDE * len(pieces))
            ov.put(b, rng.randbytes(0x20))
            ov.u32(at + 0x228, b)
        pieces.append(dict(at=at, x=x, y=y, z=z, w=ww, d=dd, layers=layers, model=m, slots=slots))
    for a, b in zip(pieces, pieces[1:]):
        ov.u32(a["at"] + 0x230, b["at"])
    base_map = BLOCK_FM if floor_map else BLOCK_OM
    if on_map:
        for p in pieces:
            for xx, zz, yy in p["slots"]:
                ov.u32(world.a(base_map) + (zz * world.W + xx) * 20 + 4 * yy, p["at"])
    return pieces


# ---- lgl: the lists and the flags -----------------------------------------------------------------------------------------------------

def lgl_regions(slab):
    return lg_regions(slab, (LG, LG_SIZE), (ROWS_OM, 0x1500), (ROWS_FM, 0x1500), (ITEM_BASE, 0x200), (RING, 0x400), (PIECE_BASE, 0x2D00), (GAP_BASE, 0x600), (START_BASE, 0x100),
                      (BUILT_BASE, 0x2100))     # (the floor pieces of the flag cases follow the park's: slots 12 to 19)


SLAB_BYTES["lgl"] = ARENA + 0x100
SEEDS["lgl"] = lambda env, rng: {}
EXTRA_REGIONS["lgl"] = lgl_regions
READ_ONLY["lgl"] = LG_READS


def list_world(env, map_pieces=0):
    rng = env.rng
    w = World(env, width=rng.choice([2, 4, 6, 8]), depth=rng.choice([2, 4, 6, 8]))
    w.object()
    w.maps()
    ring(w)
    w.source()
    models = models_that_fit(w, rng.randrange(1, 5))
    return w, models


@spec(0x0043AA30, "LevelGen_ReleasePieces", "lgl", this=lambda env: env.slab + LG)
def gen_release(env, n):
    w, models = list_world(env)
    rng = w.rng
    pieces = put_pieces(w, rng.randrange(0, 6))
    w.ov.u32(w.lg + 0x2344, pieces[0] if pieces else 0)
    # the map is full of pointers
    w.ov.put(w.a(BLOCK_OM), rng.randbytes(w.W * w.D * 20))
    for off, size in ((0x369C, 1), (0x36B0, 1)):
        w.ov.u8(w.lg + off, rng.randrange(256))
    for i in range(6):
        w.ov.u8(w.lg + 0x36C4 + 0x14 * i, rng.randrange(256))
    w.ov.u32(w.lg + 0x3698, rng.randrange(7))
    if rng.random() < 0.2:
        w.ov.u32(w.lg + 0x2348, 0)
    return [], w.ov.pokes()


@spec(0x0043B3C0, "LevelGen_FreeFloorPieces", "lgl", this=lambda env: env.slab + LG)
def gen_free_floor_pieces(env, n):
    w, models = list_world(env)
    pieces = put_pieces(w, w.rng.randrange(0, 6))
    w.ov.u32(w.lg + 0x234C, pieces[0] if pieces else 0)
    return [], w.ov.pokes()


@spec(0x0043B880, "LevelGen_FreeGaps", "lgl", this=lambda env: env.slab + LG)
def gen_free_gaps(env, n):
    w, models = list_world(env)
    rng = w.rng
    pieces = put_pieces(w, rng.randrange(1, 5))
    gaps = put_gaps(w, rng.randrange(0, 6))
    w.ov.u32(w.lg + 0x2388, gaps[0] if gaps else 0)
    for g in gaps:
        w.ov.u32(g + 4, rng.choice(pieces))
    return [], w.ov.pokes()


@spec(0x0043BBA0, "LevelGen_FreeStarts", "lgl", this=lambda env: env.slab + LG)
def gen_free_starts(env, n):
    w, models = list_world(env)
    starts = put_starts(w, w.rng.randrange(0, 5))
    w.ov.u32(w.lg + 0x23A4, starts[0] if starts else 0)
    return [], w.ov.pokes()


@spec(0x0043B080, "LevelGen_PrunePieces", "lgl", this=lambda env: env.slab + LG)
def gen_prune(env, n):
    w, models = list_world(env)
    rng = w.rng
    pieces = put_pieces(w, rng.randrange(0, 7))
    for p in pieces:
        w.ov.u8(p, rng.choice([0, 0, 1]))
        w.ov.u8(p + 2, rng.choice([0, 1]))
    w.ov.u32(w.lg + 0x2344, pieces[0] if pieces else 0)
    w.ov.u32(RING_USED, rng.randrange(0x40, 0x300))
    return [], w.ov.pokes()


def mapped_pieces(w, count, floor_map=False, built=False):
    ps = place_piece_records(w, count, floor_map=floor_map, built=built)
    # the ring nodes: each piece a node or two
    nodes = 0
    for p in ps:
        first = prev = None
        for _ in range(w.rng.randrange(0, 3)):
            if nodes >= 60:
                break
            node = w.a(RING) + 16 * nodes
            nodes += 1
            w.ov.u32(node, LIVE)
            w.ov.u32(node + 0xC, 0)
            if prev is not None:
                w.ov.u32(prev + 0xC, node)
            else:
                first = node
            prev = node
        w.ov.u32(p["at"] + 0x218, first or 0)
    return ps


@spec(0x0043B0F0, "LevelGen_ComputeNeighbours", "lgl", this=lambda env: env.slab + LG)
def gen_neighbours(env, n):
    w, models = list_world(env)
    rng = w.rng
    ps = mapped_pieces(w, rng.randrange(1, 12))
    w.ov.u32(w.lg + 0x2344, ps[0]["at"] if ps else 0)
    # the neighbour lists start as anything
    for p in ps:
        w.ov.put(p["at"] + 0x18, rng.randbytes(0x20))
    return [], w.ov.pokes()


@spec(0x0043B850, "LevelGen_FlagFloorPieces", "lgl", this=lambda env: env.slab + LG)
def gen_flag_floor(env, n):
    w, models = list_world(env)
    rng = w.rng
    w.ov.u8(w.lg + 0x3744, rng.choice([0, 0, 1]))
    ps = place_piece_records(w, rng.randrange(1, 6), on_map=False, built=True)
    floor_ps = place_piece_records_floor(w, rng.randrange(1, 8))
    w.ov.u32(w.lg + 0x2344, ps[0]["at"] if ps else 0)
    return [], w.ov.pokes()


def place_piece_records_floor(w, count):
    """Floor pieces (with built models) on FloorModelMap, in the piece area after the park's pieces."""
    ov, rng = w.ov, w.rng
    out = []
    for i in range(count):
        at = w.a(PIECE_BASE + PIECE_STRIDE * (12 + i))
        b = w.a(BUILT_BASE + BUILT_STRIDE * (12 + i))
        ov.put(at, rng.randbytes(0x238))
        ov.put(b, rng.randbytes(0x20))
        ov.u32(at + 0x228, b)
        out.append(at)
    for xx in range(w.W):
        for zz in range(w.D):
            for yy in range(5):
                ov.u32(w.a(BLOCK_FM) + (zz * w.W + xx) * 20 + 4 * yy, rng.choice([0, 0, 0] + out) if out else 0)
    return out


@spec(0x0043F400, "LevelGen_SetCellFlags", "lgl", this=lambda env: env.slab + LG)
def gen_cell_flags(env, n):
    w, models = list_world(env)
    rng = w.rng
    ps = place_piece_records(w, rng.randrange(1, 4), on_map=False, built=True)
    if not ps:
        w.ov.u32(w.lg + 0x2344, 0)
    place_piece_records_floor(w, rng.randrange(1, 8))
    # the piece to flag: kind bit 2 on its model, a layer above 0
    p = ps[0] if ps else None
    if p is None:
        raise_skip()
    w.ov.u8(p["model"]["at"] + 9, rng.choice([4, 4, 0, 5, 0x14]))
    w.ov.u8(p["at"] + 6, rng.choice([0, 1, 1, 2, 3]))
    return [p["at"], rng.choice([0, 1, 0x100, 0xFF])], w.ov.pokes()


def raise_skip():
    from create_specs import Skip
    raise Skip()


@spec(0x0043F5A0, "LevelGen_MarkGaps", "lgl", this=lambda env: env.slab + LG)
def gen_mark_gaps(env, n):
    w, models = list_world(env)
    rng = w.rng
    ps = put_pieces(w, rng.randrange(1, 4))
    gaps = put_gaps(w, rng.randrange(0, 5))
    # each gap has a partner (the next, or the one before) and a piece
    for i, g in enumerate(gaps):
        w.ov.u32(g + 4, rng.choice(ps))
        w.ov.u32(g + 8, gaps[i ^ 1] if (i ^ 1) < len(gaps) else g)
        w.ov.u32(g + 0x10, w.a(BUILT_BASE) + 0x200 * i)
        w.ov.put(w.a(BUILT_BASE) + 0x200 * i, rng.randbytes(8))
    w.ov.u32(w.lg + 0x2388, gaps[0] if gaps else 0)
    return [rng.choice([0, 0, rng.choice(ps)])], w.ov.pokes()


@spec(0x0043B8D0, "LevelGen_BuildItemGapObjects", "lgb", this=lambda env: env.slab + LG)
def gen_build_gaps(env, n):
    w, models, placed = builder_world(env)
    rng = w.rng
    ps = place_piece_records(w, rng.randrange(2, 14))
    w.items(active=rng.randrange(0, 4))
    w.ov.u32(w.lg + 0x2344, ps[0]["at"] if ps else 0)
    # the items' ends name cells that hold pieces (mostly)
    cells = [(p["x"], p["z"], p["y"]) for p in ps]
    base = w.a(ITEM_BASE)
    for i in range(10):
        at = base + 0x2C * i
        if i < 4 and cells and rng.random() < 0.9:
            for end in range(2):
                x, z, y = rng.choice(cells)
                w.ov.u8(at + end, x)
                w.ov.u8(at + 2 + end, y)
                w.ov.u8(at + 4 + end, z)
    w.ov.u8(w.lg + 0x3744, rng.choice([0, 1]))
    return [], w.ov.pokes()


@spec(0x0043BBF0, "LevelGen_BuildStarts", "lgb", this=lambda env: env.slab + LG)
def gen_build_starts(env, n):
    w, models, placed = builder_world(env)
    rng = w.rng
    ps = place_piece_records(w, rng.randrange(0, 8))
    for p in ps:
        if rng.random() < 0.4:
            w.ov.u8(p["model"]["at"] + 8, w.ov_u32(START_MARKER))
    w.ov.u32(w.lg + 0x2344, ps[0]["at"] if ps else 0)
    starts = put_starts(w, rng.randrange(0, 3))
    w.ov.u32(w.lg + 0x23A4, starts[0] if starts else 0)
    return [], w.ov.pokes()


@spec(0x0043F1B0, "LevelGen_MarkEdges", "lgl", this=lambda env: env.slab + LG)
def gen_mark_edges(env, n):
    w, models = list_world(env)
    rng = w.rng
    ps = place_piece_records(w, rng.randrange(1, 10), built=True)
    if not ps:
        raise_skip()
    # neighbours that do the work are cheap: their model's kind has bit 8 (out_mark_faces returns at once), or they have no faces
    for p in ps:
        if rng.random() < 0.7:
            w.ov.u8(p["model"]["at"] + 9, w.rng.choice([8, 9, 0x18, 8]))
        else:
            w.ov.u8(p["model"]["at"] + 9, rng.choice([0, 0x10, 4]))
        # a built model with no faces
        b = w.a(BUILT_BASE + BUILT_STRIDE * ps.index(p))
        for off in (2, 4, 6):
            w.ov.u16(b + off, 0)
        w.ov.u16(p["at"] + 0x14, 0)
        w.ov.u32(p["model"]["at"] + 0, 0)
        w.ov.u16(p["model"]["model"] + 6, 0)       # (the source model has no faces: the marking has nothing to walk)
    target = rng.choice(ps)
    w.ov.u8(target["model"]["at"] + 9, rng.choice([0, 0, 0, 1, 0x10, 4]))
    return [target["at"], rng.choice([0, 1])], w.ov.pokes()


def lg_pieces_stubs(fc, env):
    lg_stubs(fc, env)


GROUP_STUBS: dict = {"lgf": lg_pieces_stubs, "lgb": lg_pieces_stubs, "lgl": lg_pieces_stubs}
