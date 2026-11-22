"""Phase 4.11b case generators, part 1: editorpieces2.cpp (group `gpiece`) and faceremoval.cpp (group `gface`).

The probe (frontend_calls.py with the group name) forces calls of the retail functions on the live process.  The statics the code reads and writes (the piece id tables,
the theme, the face-removal point pool) are the game's own; the objects they work on (rings of points) are made up in the scratch slab.  Services (the allocator,
Mem_Delete, the logger, the assertion routine) are stubbed and reported by frontend_specs_common.install_stubs.
"""

from __future__ import annotations

import struct

from career_state import u8, u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from pspool_specs import Ov, pick

# ---- the statics ------------------------------------------------------------------------------------------------------------
THEME = 0x005614C8
RECORDS = 0x00528820
RECORD_BYTES = 0x25
RECORD_COUNT = 151                  # (a 152nd, with 0xff in every theme column, ends the list)
LIST_BASE = 0x0052A0C0
ROTATIONS = 0x00529E18
STRIDES = 0x00529E38
SPECIAL_A, SPECIAL_B, SPECIAL_C = 0x005614A8, 0x00561490, 0x00561174
CLEARED = 0x00561170
RECORD_COUNT_WORD = 0x0056118C

POOL_BLOCK, POOL_CURSOR, POOL_OVERFLOW, POOL_USED = 0x005611B0, 0x00561164, 0x0056115C, 0x00561190

SLAB = 0x2000
ARENA = 0x1000
ARENA_SIZE = 0x800


class Memo:
    """What the generators know of the read-only tables (parsed once from the base snapshot)."""

    def __init__(self, env):
        data = env.bytes(RECORDS, RECORD_BYTES * RECORD_COUNT)
        self.records = [data[k * RECORD_BYTES:(k + 1) * RECORD_BYTES] for k in range(RECORD_COUNT)]
        self.ids = [[r[theme] for r in self.records] for theme in range(4)]
        self.list_bytes = env.bytes(LIST_BASE, 0xD0)


_memo: dict = {}


def memo(env) -> Memo:
    if "m" not in _memo or _memo.get("env") is not env:
        _memo["m"] = Memo(env)
        _memo["env"] = env
    return _memo["m"]


def poke_words(ov: Ov, addresses, rng, values):
    for a in addresses:
        ov.u32(a, rng.choice(values))


# ---- gpiece -------------------------------------------------------------------------------------------------------------------

def piece_regions(slab):
    return [(slab, 0x100), (0x00561150, 0x3B0), (ROTATIONS, 0x2A8)]


SLAB_BYTES["gpiece"] = 0x1000
SEEDS["gpiece"] = lambda env, rng: {}
EXTRA_REGIONS["gpiece"] = piece_regions
READ_ONLY["gpiece"] = [(RECORDS - 0x40, RECORD_BYTES * (RECORD_COUNT + 1) + 0x40), (LIST_BASE, 0xD0)]


def any_id(env, rng, theme=None):
    m = memo(env)
    roll = rng.random()
    if roll < 0.75:
        t = rng.randrange(4) if theme is None else theme
        return rng.choice(m.ids[t])
    if roll < 0.9:
        return rng.randrange(256)
    return rng.choice([0, 0xFF, 0x100, 0x1234])


def theme_poke(env, ov, rng, theme=None):
    t = rng.randrange(4) if theme is None else theme
    ov.u32(THEME, t)
    return t


@spec(0x00444010, "EditorPieces_ClearFlag", "gpiece")
def gen_clear_flag(env, n):
    ov = Ov()
    ov.u8(CLEARED, env.rng.choice([0, 1, 7]))
    return [], ov.pokes()


@spec(0x00444020, "EditorPieces_Setup", "gpiece")
def gen_setup(env, n):
    rng = env.rng
    ov = Ov()
    theme = rng.randrange(4)
    # garbage in every word the call fills, so a missed or misplaced store shows
    for a in [SPECIAL_A + 4 * k for k in range(5)] + [SPECIAL_B + 4 * k for k in range(5)] + [SPECIAL_C + 4 * k for k in range(5)]:
        ov.u32(a, rng.randrange(1 << 32))
    for a in (0x00561184, 0x005611A0, 0x005611B4, 0x005613B0, 0x005613B4, 0x0056116C, 0x005611A8, 0x005611AC, 0x005614A4, 0x00561158, 0x005614B8, RECORD_COUNT_WORD):
        ov.u32(a, rng.randrange(1 << 32))
    for row in range(18):
        ov.u8(STRIDES + 0x22 * row + 0x21, rng.randrange(256))
    ov.u32(THEME, rng.randrange(4))
    return [theme], ov.pokes()


@spec(0x004441C0, "EditorPieces_Find", "gpiece")
def gen_find(env, n):
    rng = env.rng
    return [any_id(env, rng), rng.randrange(4)], {}


@spec(0x00444210, "EditorPieces_ThemeId", "gpiece")
def gen_theme_id(env, n):
    rng = env.rng
    m = memo(env)
    roll = rng.random()
    id_ = rng.choice(m.ids[0]) if roll < 0.85 else any_id(env, rng, 0)
    return [id_, rng.randrange(4)], {}


@spec(0x00444280, "EditorPieces_IsSpecial", "gpiece")
def gen_is_special(env, n):
    rng = env.rng
    ov = Ov()
    id_ = any_id(env, rng)
    slots = [SPECIAL_A + 4 * k for k in range(5)] + [SPECIAL_B + 4 * k for k in range(5)] + [SPECIAL_C + 4 * k for k in range(5)]
    for a in slots:
        ov.u32(a, rng.choice([0, rng.randrange(256), rng.randrange(256)]))
    if rng.random() < 0.6:
        ov.u32(rng.choice(slots), id_)
    return [id_], ov.pokes()


@spec(0x004442B0, "EditorPieces_ListIndex", "gpiece")
def gen_list_index(env, n):
    rng = env.rng
    ov = Ov()
    theme = theme_poke(env, ov, rng)
    m = memo(env)
    listed = [b for b in m.list_bytes if b not in (0xFE, 0xFF)]
    if rng.random() < 0.7:
        # a piece of the current theme whose theme-0 id is in the list
        k = rng.randrange(RECORD_COUNT)
        for _ in range(40):
            k = rng.randrange(RECORD_COUNT)
            if m.records[k][0] in listed:
                break
        id_ = m.records[k][theme]
    else:
        id_ = any_id(env, rng, theme)
    return [id_], ov.pokes()


@spec(0x00444380, "LevelGen_TranslatePieceReference", "gpiece")
def gen_translate(env, n):
    rng = env.rng
    ov = Ov()
    theme_poke(env, ov, rng)
    m = memo(env)
    end = m.list_bytes.index(0xFE) if 0xFE in m.list_bytes else len(m.list_bytes) - 1
    return [rng.choice([rng.randrange(end + 1), rng.randrange(end + 1), rng.randrange(end + 1), end])], ov.pokes()


@spec(0x00444410, "EditorPieces_Kind", "gpiece")
def gen_kind(env, n):
    rng = env.rng
    ov = Ov()
    theme = theme_poke(env, ov, rng)
    return [any_id(env, rng, theme)], ov.pokes()


@spec(0x00444450, "EditorPieces_TurnedId", "gpiece")
def gen_turned(env, n):
    rng = env.rng
    ov = Ov()
    theme = theme_poke(env, ov, rng)
    m = memo(env)
    rotating = [0x59, 0x5B, 0x5C, 0x5A, 0x54]
    candidates = [r[theme] for r in m.records if r[0] in rotating]
    id_ = rng.choice(candidates) if candidates and rng.random() < 0.7 else any_id(env, rng, theme)
    return [id_, rng.randrange(8), rng.randrange(8)], ov.pokes()


# ---- gface --------------------------------------------------------------------------------------------------------------------
# slab: 0x0100 ring A points, 0x0300 ring B points, 0x0500 extra points, 0x0800 the pool block (0x480 bytes), 0x1000 the arena for FaceRemoval_Setup

RING_A, RING_B, EXTRA = 0x0100, 0x0300, 0x0500
POOL = 0x0800
FACE_ARENA = 0x1000


def face_regions(slab):
    return [(slab, 0x1800), (0x00561150, 0xC0)]


def face_seed(env, rng):
    env.alloc_base = FACE_ARENA
    env.alloc_size = 0x600
    return {}


SLAB_BYTES["gface"] = 0x1800
SEEDS["gface"] = face_seed
EXTRA_REGIONS["gface"] = face_regions
READ_ONLY["gface"] = []


def put_point(ov: Ov, at: int, x: int, y: int, z: int, flag: int = 1, nxt: int = 0, aux: int = 0):
    ov.u32(at, x & 0xFFFFFFFF)
    ov.u32(at + 4, y & 0xFFFFFFFF)
    ov.u32(at + 8, z & 0xFFFFFFFF)
    ov.u8(at + 0xC, flag)
    ov.u32(at + 0x10, nxt)
    ov.u32(at + 0x14, aux)


def put_ring(ov: Ov, base: int, pts, flag: int = 1, flags=None):
    """A circular ring of points at base, 0x18 apart; returns the address of the first."""
    n = len(pts)
    for i, (x, y, z) in enumerate(pts):
        at = base + 0x18 * i
        nxt = base + 0x18 * ((i + 1) % n)
        put_point(ov, at, x, y, z, flag if flags is None else flags[i], nxt, 0)
    return base


def pool_state(env, ov, used: int = 0, block: int | None = None, overflow: int = 0):
    block = env.slab + POOL if block is None else block
    ov.u32(POOL_BLOCK, block)
    ov.u32(POOL_CURSOR, block + 0x18 * used)
    ov.u32(POOL_USED, used)
    ov.u8(POOL_OVERFLOW, overflow)


def rect(x0, x1, y0, y1, z, ccw=True):
    pts = [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]
    return pts if ccw else pts[::-1]


def tri(rng, z):
    a = (rng.randrange(0, 64) * 8, rng.randrange(0, 64) * 8, z)
    b = (a[0] + rng.randrange(1, 32) * 8, a[1], z)
    c = (a[0] + rng.randrange(0, 32) * 8, a[1] + rng.randrange(1, 32) * 8, z)
    return [a, b, c]


def pair_of_faces(rng):
    """Two faces in (nearly) one plane: overlapping, touching, nested, apart or sharing an edge."""
    z = rng.randrange(-4, 5) * 16
    kind = rng.choice(["cross", "cross", "overlap", "overlap", "nested", "edge", "edge", "apart", "tri", "same", "corner"])
    x0, y0 = rng.randrange(0, 20) * 8, rng.randrange(0, 20) * 8
    w, h = rng.randrange(2, 24) * 8, rng.randrange(2, 24) * 8
    a = rect(x0, x0 + w, y0, y0 + h, z)
    if kind == "cross":
        # a wide, flat one over a tall, thin one
        b = rect(x0 - rng.randrange(1, 8) * 8, x0 + w + rng.randrange(1, 8) * 8, y0 + rng.randrange(1, max(2, h // 8 - 1)) * 8,
                 y0 + h - rng.randrange(0, max(1, h // 16)) * 8 + 8, z + rng.choice([0, 0, 1, -1]))
    elif kind == "overlap":
        b = rect(x0 + rng.randrange(1, max(2, w // 8)) * 8, x0 + w + rng.randrange(1, 12) * 8, y0 + rng.randrange(1, max(2, h // 8)) * 8, y0 + h + rng.randrange(1, 12) * 8, z)
    elif kind == "nested":
        b = rect(x0 + 8, x0 + w - 8 if w > 16 else x0 + w, y0 + 8, y0 + h - 8 if h > 16 else y0 + h, z)
    elif kind == "edge":
        b = rect(x0 + w, x0 + w + rng.randrange(2, 16) * 8, y0 + rng.randrange(0, max(1, h // 16)) * 8, y0 + h + rng.randrange(0, 8) * 8, z)
    elif kind == "apart":
        b = rect(x0 + w + 200, x0 + w + 200 + rng.randrange(2, 16) * 8, y0, y0 + h, z)
    elif kind == "tri":
        b = tri(rng, z)
        a = tri(rng, z) if rng.random() < 0.5 else a
    elif kind == "same":
        b = list(a)
    else:
        b = rect(x0 + w - rng.randrange(1, 4) * 8, x0 + w + rng.randrange(2, 12) * 8, y0 + h - rng.randrange(1, 4) * 8, y0 + h + rng.randrange(2, 12) * 8, z)
    # (the pair routine hands the clipping two rings that run the same way round: reversed for both now and then, for one only rarely)
    if rng.random() < 0.5:
        a, b = a[::-1], b[::-1]
    if rng.random() < 0.1:
        b = b[::-1]
    rot = rng.randrange(len(b))
    b = b[rot:] + b[:rot]
    rot = rng.randrange(len(a))
    a = a[rot:] + a[:rot]
    return a, b


@spec(0x004436E0, "FaceRemoval_Setup", "gface")
def gen_face_setup(env, n):
    ov = Ov()
    ov.u32(POOL_BLOCK, env.rng.choice([0, env.slab + POOL]))
    ov.u32(POOL_CURSOR, env.rng.randrange(1 << 32))
    ov.u8(POOL_OVERFLOW, env.rng.choice([0, 1]))
    return [], ov.pokes()


@spec(0x00443710, "FaceRemoval_Cleanup", "gface")
def gen_face_cleanup(env, n):
    ov = Ov()
    pool_state(env, ov, env.rng.randrange(0, 40))
    return [], ov.pokes()


@spec(0x00443730, "FaceRemoval_NewPoint", "gface")
def gen_new_point(env, n):
    rng = env.rng
    ov = Ov()
    used = rng.choice([0, 1, 5, 20, 38, 39, 40, 41, 44])
    pool_state(env, ov, used, overflow=rng.choice([0, 0, 1]))
    if rng.random() < 0.1:
        ov.u32(POOL_BLOCK, 0)
    ov.put(env.slab + POOL + 0x18 * used, rng.randbytes(0x18))
    return [], ov.pokes()


@spec(0x004437D0, "FaceRemoval_ResetPoints", "gface")
def gen_reset_points(env, n):
    rng = env.rng
    ov = Ov()
    pool_state(env, ov, rng.randrange(0, 45), overflow=rng.choice([0, 1]))
    ov.u32(POOL_BLOCK, env.slab + POOL + rng.choice([0, 0x18]))
    return [], ov.pokes()


@spec(0x004437F0, "FaceRemoval_PrintPoint", "gface")
def gen_print_point(env, n):
    rng = env.rng
    ov = Ov()
    put_point(ov, env.slab + EXTRA, rng.randrange(-5000, 5000), rng.randrange(-5000, 5000), rng.randrange(-100, 100), rng.choice([0, 1, 2]))
    return [env.slab + EXTRA], ov.pokes()


@spec(0x00443EB0, "FaceRemoval_PointsEqual", "gface")
def gen_points_equal(env, n):
    rng = env.rng
    ov = Ov()
    x, y, z = rng.randrange(-2000, 2000), rng.randrange(-2000, 2000), rng.randrange(-50, 50)
    d = lambda: rng.choice([0, 0, 1, -1, 3, -3, 4, -4, 100, -100])    # noqa: E731
    put_point(ov, env.slab + EXTRA, x, y, z)
    put_point(ov, env.slab + EXTRA + 0x18, x + d(), y + d() if rng.random() < 0.7 else y, z + d() if rng.random() < 0.7 else z)
    return [env.slab + EXTRA, env.slab + EXTRA + 0x18], ov.pokes()


@spec(0x00443F00, "FaceRemoval_RingArea", "gface")
def gen_ring_area(env, n):
    rng = env.rng
    ov = Ov()
    count = rng.randrange(3, 8)
    pts = [(rng.randrange(-300, 300), rng.randrange(-300, 300), 0) for _ in range(count)]
    put_ring(ov, env.slab + RING_A, pts)
    return [env.slab + RING_A + 0x18 * rng.randrange(count)], ov.pokes()


def four_points(env, rng, ov):
    addrs = []
    for k in range(4):
        at = env.slab + EXTRA + 0x18 * k
        put_point(ov, at, rng.randrange(-500, 500), rng.randrange(-500, 500), rng.randrange(-5, 5))
        addrs.append(at)
    if rng.random() < 0.3:
        addrs[3] = addrs[1]
    return addrs


@spec(0x00443F30, "FaceRemoval_Cross", "gface")
def gen_cross(env, n):
    ov = Ov()
    return four_points(env, env.rng, ov), ov.pokes()


@spec(0x00443F70, "FaceRemoval_SameSide", "gface")
def gen_side(env, n):
    ov = Ov()
    return four_points(env, env.rng, ov), ov.pokes()


@spec(0x00443FC0, "FaceRemoval_PrintRing", "gface")
def gen_print_ring(env, n):
    rng = env.rng
    ov = Ov()
    count = rng.randrange(1, 7)
    put_ring(ov, env.slab + RING_A, [(rng.randrange(-300, 300), rng.randrange(-300, 300), 0) for _ in range(count)])
    return [env.slab + RING_A], ov.pokes()


@spec(0x00443820, "FaceRemoval_Overlap", "gface")
def gen_overlap(env, n):
    rng = env.rng
    ov = Ov()
    a, b = pair_of_faces(rng)
    put_ring(ov, env.slab + RING_A, a)
    put_ring(ov, env.slab + RING_B, b)
    pool_state(env, ov, rng.choice([0, 0, 0, 0, 20, 36, 39]), overflow=rng.choice([0, 0, 0, 0, 0, 1]))
    return [env.slab + RING_A, env.slab + RING_B], ov.pokes()


GROUP_STUBS: dict = {}
