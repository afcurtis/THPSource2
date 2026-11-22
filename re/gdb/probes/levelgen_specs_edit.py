"""Phase 4.11c case generators, part 3: the editing operations (levelgen.cpp 0x0043c770..0x0043cf20, 0x0043ce60): group `lge`.

Placing a piece, taking the top piece of a cell away, the item records (add, update, remove), the gap members of an item and the flash of the pieces with a gap.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Skip, spec
from gen_specs_model import FREE, LIVE, RING_USED
from levelgen_specs import (ARENA, BLOCK_OM, BUILT_BASE, BUILT_STRIDE, CELLS_SRC, GAP_BASE, GAP_STRIDE, GAPBUF_BASE, GAPBUF_STRIDE, HORSE_TYPE, ITEM_BASE, LG, LG_READS, LG_SIZE,
                            PIECE_BASE, PIECE_STRIDE, RING, ROWS_OM, ROWS_SRC, START_TYPES, World, lg_regions, lg_stubs, put_gaps, ring)
from levelgen_specs_pieces import fitting_cells, models_that_fit, place_piece_records
from pspool_specs import Ov

EDIT_ARENA = 0x1000


def lge_regions(slab):
    return lg_regions(slab, (LG, LG_SIZE), (ROWS_SRC, 0x1100), (ROWS_OM, 0x1500), (ITEM_BASE, 0x200), (RING, 0x400), (PIECE_BASE, 0x2400), (BUILT_BASE, 0x1800),
                      (GAP_BASE, 0x600), (GAPBUF_BASE, 0x1200), (ARENA, EDIT_ARENA), extra=[(0x533940, 0x10)])


def lge_seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = EDIT_ARENA
    return {}


SLAB_BYTES["lge"] = ARENA + EDIT_ARENA + 0x100
SEEDS["lge"] = lge_seed
EXTRA_REGIONS["lge"] = lge_regions
READ_ONLY["lge"] = LG_READS


def edit_world(env, pieces=(1, 8)):
    """A park under edit: the cells, the models, some pieces placed on the map (and the cells that hold them), the ring."""
    rng = env.rng
    w = World(env, width=rng.choice([4, 4, 6, 8]), depth=rng.choice([4, 4, 6, 8]))
    w.object()
    w.maps()
    w.source()
    ring(w)
    models = models_that_fit(w, rng.randrange(2, 7))
    # start and horse ids that are among the library slots
    w.ov.u32(START_TYPES, rng.randrange(len(models)))
    w.ov.u32(START_TYPES + 4, rng.randrange(len(models)))
    w.ov.u32(HORSE_TYPE, rng.randrange(len(models)))
    w.items(active=rng.randrange(0, 4))
    ps = place_piece_records(w, rng.randrange(*pieces), built=True)
    if ps:
        w.ov.u32(w.lg + 0x2344, ps[0]["at"])
    # the cells of the park: the pieces' layers
    base = w.a(CELLS_SRC)
    for z in range(w.D):
        for x in range(w.W):
            cell = base + (z * w.W + x) * 16
            for k in range(5):
                w.ov.u16(cell + 2 * k, 0xFFFF)
                w.ov.u8(cell + 0xA + k, rng.randrange(4))
            w.ov.u8(cell + 0xF, rng.randrange(0, 6))
    for p in ps:
        cell = base + (p["z"] * w.W + p["x"]) * 16
        w.ov.u16(cell + 2 * p["y"], p["model"]["index"])
        w.ov.u8(cell + 0xA + p["y"], rng.randrange(4))
        w.ov.u8(cell + 0xF, max(1, p["y"] + 1))
        # the out-model's fog header (the flash writes it)
        w.ov.u32(p["at"] + 0x22C, w.a(BUILT_BASE + BUILT_STRIDE * ps.index(p)) + 0x100)
    # pieces for gap members and fog headers
    return w, models, ps


@spec(0x0043C770, "LevelGen_PlacePiece", "lge", this=lambda env: env.slab + LG)
def gen_place(env, n):
    w, models, ps = edit_world(env, pieces=(0, 6))
    rng = w.rng
    m = rng.choice(models)
    rot = rng.randrange(4)
    ww, dd = (m["width"], m["depth"]) if rot % 2 == 0 else (m["depth"], m["width"])
    x, z = rng.randrange(0, max(1, w.W - ww + 1)), rng.randrange(0, max(1, w.D - dd + 1))
    layer = rng.randrange(0, 3)
    # a start or horse record may be active (its piece is on the map or not)
    for j in range(2):
        rec = w.lg + 0x369C + 0x14 * j
        w.ov.u8(rec, rng.choice([0, 1]))
        if ps and rng.random() < 0.7:
            p = rng.choice(ps)
            w.ov.u32(rec + 4, p["x"])
            w.ov.u32(rec + 8, p["z"])
        else:
            w.ov.u32(rec + 4, rng.randrange(w.W))
            w.ov.u32(rec + 8, rng.randrange(w.D))
    for i in range(6):
        w.ov.u8(w.lg + 0x36C4 + 0x14 * i, rng.choice([0, 0, 1]))
    w.ov.u32(w.lg + 0x3698, rng.randrange(0, 7))
    # (the model's type may be a start or horse type)
    index = m["index"]
    if rng.random() < 0.3:
        w.ov.u32(START_TYPES + 4 * rng.randrange(2), index)
    elif rng.random() < 0.2:
        w.ov.u32(HORSE_TYPE, index)
    # the footprint is mostly free (an overlap is a logged assertion, the map is overwritten)
    return [x, z, layer, rot, index], w.ov.pokes()


@spec(0x0043CC30, "LevelGen_AddItem", "lge")
def gen_add_item(env, n):
    w, models, ps = edit_world(env, pieces=(0, 2))
    rng = w.rng
    w.items(active=rng.randrange(0, 11))
    w.ov.u8(0x005286FC, rng.randrange(256))
    index = rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.randrange(10), rng.randrange(10)])
    return [rng.randrange(w.W), rng.randrange(5), rng.randrange(w.D), index], w.ov.pokes()


@spec(0x0043CE00, "LevelGen_RemoveItemsAt", "lge")
def gen_remove_items(env, n):
    w, models, ps = edit_world(env, pieces=(0, 2))
    rng = w.rng
    w.items(active=rng.randrange(1, 8))
    base = w.a(ITEM_BASE)
    # make the ends of some items small numbers so the search hits
    x, y, z = rng.randrange(0, 3), rng.randrange(0, 3), rng.randrange(0, 3)
    for i in range(10):
        if rng.random() < 0.6:
            at = base + 0x2C * i
            end = rng.randrange(2)
            w.ov.u8(at + end, x)
            w.ov.u8(at + end + 2, y)
            w.ov.u8(at + end + 4, z)
            w.ov.u8(at + 0xC, rng.choice([0x80, 0x81, 1, 0]))
    return [x, y, z], w.ov.pokes()


@spec(0x0043CD90, "LevelGen_RefreshItemGaps", "lge", this=lambda env: env.slab + LG)
def gen_refresh_gaps(env, n):
    w, models, ps = edit_world(env, pieces=(2, 8))
    rng = w.rng
    if not ps:
        raise Skip()
    w.items(active=4)
    count = rng.randrange(0, 5)
    gaps = put_gaps(w, count)
    for i, g in enumerate(gaps):
        w.ov.u32(g + 4, rng.choice(ps)["at"])
        w.ov.u32(g + 0x10, w.a(GAPBUF_BASE + GAPBUF_STRIDE * (i % 2)))
        w.ov.u32(g + 0x14, w.a(GAPBUF_BASE) + 0x1000)
        w.ov.put(g + 0x1C, bytes([rng.randrange(0, 4), 0, 0, 0]))
        w.ov.u8(g, rng.choice([0, 1]))
        # (a made gap member: the layout the constructor sets, a turn of 0..3 and a box on the grid)
        for offset, value in ((0x3C, 0x864), (0x40, 0x1C), (0x44, 0x3C8), (0x48, 0x48), (0x4C, 0x31)):
            w.ov.u32(g + offset, value)
        w.ov.u32(g + 0x50, rng.randrange(0, 4))
        for offset in (0x20, 0x24, 0x28, 0x2C):
            w.ov.u32(g + offset, rng.randrange(-0x3000, 0x3000) & 0xFFFFFFFF)
    w.ov.u32(w.lg + 0x2388, gaps[0] if gaps else 0)
    for i in range(2):
        w.ov.put(w.a(GAPBUF_BASE + GAPBUF_STRIDE * i), rng.randbytes(0x900))
    w.ov.put(w.a(GAPBUF_BASE) + 0x1000, rng.randbytes(0x20))
    w.ov.u16(0x00533944, rng.randrange(1 << 16))
    if gaps and rng.random() < 0.7:        # (mostly an item and side one of the gap members belongs to)
        g = rng.choice(gaps)
        return [w.ov.bytes[g + 0x1C], 1 if w.ov.bytes[g] == 0 else 0], w.ov.pokes()
    return [rng.randrange(0, 4), rng.randrange(2)], w.ov.pokes()


@spec(0x0043CE60, "LevelGen_SetFlash", "lge", this=lambda env: env.slab + LG)
def gen_flash(env, n):
    w, models, ps = edit_world(env, pieces=(1, 6))
    rng = w.rng
    if not ps:
        raise Skip()
    for p in ps:
        w.ov.u32(p["at"] + 0x234, rng.choice([0, 0, w.a(GAP_BASE)]))
    # the disabled item's member points at a piece
    gap = w.a(GAP_BASE)
    w.ov.put(gap, rng.randbytes(0x58))
    w.ov.u32(gap + 4, ps[0]["at"])
    w.ov.u32(w.lg + 0x2390, rng.choice([0, gap]))
    w.ov.u32(w.lg + 0x2398, rng.choice([0, 0x10, 0x70, 0x7F, 0x7E, 0x6F]))
    w.ov.u32(w.lg + 0x239C, rng.choice([0x10, 0xFFFFFFF0, 0x10, 0xFFFFFFF0, 0x20, 0xFFFFFFE0]))
    for i in range(0, 1):
        w.ov.put(w.a(BUILT_BASE) + 0x100, rng.randbytes(0x40))
    return [rng.choice([0, 1, 1, 0x100])], w.ov.pokes()


@spec(0x0043CF20, "LevelGen_RemoveTop", "lge", this=lambda env: env.slab + LG)
def gen_remove_top(env, n):
    w, models, ps = edit_world(env, pieces=(1, 12))
    rng = w.rng
    if not ps:
        raise Skip()
    p = rng.choice(ps)
    x = p["x"] + rng.randrange(p["w"]) if rng.random() < 0.9 else rng.randrange(w.W)
    z = p["z"] + rng.randrange(p["d"]) if rng.random() < 0.9 else rng.randrange(w.D)
    # the start and horse records (so removing the piece hits them)
    for j in range(2):
        rec = w.lg + 0x369C + 0x14 * j
        w.ov.u8(rec, rng.choice([0, 1]))
    for i in range(6):
        rec = w.lg + 0x36C4 + 0x14 * i
        w.ov.u8(rec, rng.choice([0, 1]))
        w.ov.u32(rec + 4, rng.choice([p["x"], rng.randrange(w.W)]))
        w.ov.u32(rec + 8, rng.choice([p["z"], rng.randrange(w.D)]))
    w.ov.u32(w.lg + 0x3698, rng.randrange(0, 7))
    # a solid block (kind bit 4) and a type that is a start or horse piece
    if rng.random() < 0.4:
        w.ov.u8(p["model"]["at"] + 9, rng.choice([0x10, 0x14, 0x18]))
    if rng.random() < 0.3:
        w.ov.u8(p["model"]["at"] + 8, rng.choice([w.ov_u32(START_TYPES), w.ov_u32(START_TYPES + 4), w.ov_u32(HORSE_TYPE)]))
    limit_at = 0
    if rng.random() < 0.5:
        limit_at = w.a(ARENA)
        w.ov.u32(limit_at, rng.choice([0, 1, 2, p["y"], p["y"] + 1, 0xFFFFFFFF]))
    return [x, z, limit_at, rng.choice([0, 1, 1])], w.ov.pokes()


GROUP_STUBS: dict = {"lge": lg_stubs}
