"""Phase 4.11b case generators, part 2: the ring-of-points polygons of genobjs.cpp (0x004417c0..0x004436d0), group `gpoly`.

A point is 0x18 bytes (x, height, z as 16-bit values, +8 the next point, +0xc and +0x10 two words copies keep, +0x14 a mark); a polygon is 0x1c bytes (its box, +0xc the first
point, +0x10 the next polygon of a list, +0x14 whether it is a loop, +0x18 the number of points); a list holds the first polygon at +0 and a deadline at +8.  The probe builds
these in the scratch slab (rectilinear rail loops and strings, as the park editor's rails are, with the small jitters the "near" test is there for) and the allocator hands
out the arena at the end.

Slab layout:
  0x0000  scratch slots (the four out slots of the edge-run search, the out slot of the cut routines)
  0x0100  lists (0x10 apart)       0x0200  polygons (0x20 apart)       0x0800  points (0x18 apart)       0x4000  the allocation arena
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from pspool_specs import Ov, ensure

SLOTS, LISTS, POLYS, POINTS = 0x0000, 0x0100, 0x0200, 0x0800
ARENA, ARENA_SIZE = 0x4000, 0x3000
SLAB = 0x7100
CLOCK = 0x0056E31C


def regions(slab):
    return [(slab, 0x1400), (slab + ARENA, 0x1800)]


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    return {}


SLAB_BYTES["gpoly"] = SLAB
SEEDS["gpoly"] = seed
EXTRA_REGIONS["gpoly"] = regions
READ_ONLY["gpoly"] = [(CLOCK - 0xC, 0x20)]      # (the load clock ticks on its own while a call runs: the cases choose deadlines that make it irrelevant)


def s16(v: int) -> int:
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class World:
    """Points and polygons laid out in the slab for one case."""

    def __init__(self, env):
        self.env = env
        self.rng = env.rng
        self.ov = Ov()
        self.point_at = env.slab + POINTS
        self.poly_at = env.slab + POLYS
        self.list_at = env.slab + LISTS

    def point(self, x, y, z, mark=0, nxt=0):
        at = self.point_at
        self.point_at += 0x18
        self.ov.u16(at, x & 0xFFFF)
        self.ov.u16(at + 2, y & 0xFFFF)
        self.ov.u16(at + 4, z & 0xFFFF)
        self.ov.u32(at + 8, nxt)
        self.ov.u32(at + 0xC, self.rng.choice([0xFFFFFFFF, 0, self.rng.randrange(8)]))
        self.ov.u32(at + 0x10, self.rng.choice([0, 0, self.rng.randrange(1000)]))
        self.ov.u8(at + 0x14, mark)
        return at

    def string(self, pts, closed, marks=None):
        """A polygon of points (x, y, z); a loop's last point links back to the first.  Returns (polygon, [point addresses])."""
        addrs = [self.point(x, y, z, marks[i] if marks else 0) for i, (x, y, z) in enumerate(pts)]
        for i, at in enumerate(addrs):
            nxt = addrs[i + 1] if i + 1 < len(addrs) else (addrs[0] if closed else 0)
            self.ov.u32(at + 8, nxt)
        poly = self.poly_at
        self.poly_at += 0x20
        self.fill_poly(poly, pts, addrs[0], closed)
        return poly, addrs

    def fill_poly(self, poly, pts, head, closed, count=None):
        xs, ys, zs = [s16(p[0]) for p in pts], [s16(p[1]) for p in pts], [s16(p[2]) for p in pts]
        for k, vals in enumerate((xs, ys, zs)):
            self.ov.u16(poly + 2 * k, min(vals) & 0xFFFF)
            self.ov.u16(poly + 6 + 2 * k, max(vals) & 0xFFFF)
        self.ov.u32(poly + 0xC, head)
        self.ov.u32(poly + 0x10, 0)
        self.ov.u8(poly + 0x14, 1 if closed else 0)
        self.ov.u32(poly + 0x18, len(pts) if count is None else count)

    def link_polys(self, polys):
        for a, b in zip(polys, polys[1:]):
            self.ov.u32(a + 0x10, b)

    def list_object(self, polys, slot=0, deadline=None):
        at = self.list_at + 0x10 * slot
        self.ov.u32(at, polys[0] if polys else 0)
        self.ov.u32(at + 4, polys[-1] if polys else 0)
        self.ov.u32(at + 8, self.rng.randrange(1 << 32) if deadline is None else deadline)
        self.link_polys(polys)
        return at

    def blank_poly(self, junk=True):
        at = self.poly_at
        self.poly_at += 0x20
        if junk:
            self.ov.put(at, self.rng.randbytes(0x1C))
        return at


def jit(rng, tolerance=5, chance=0.6):
    """A small offset (within the near test's tolerance, now and then just outside it)."""
    if rng.random() > chance:
        return 0
    return rng.randrange(-tolerance, tolerance + 1) if rng.random() < 0.92 else rng.choice([-8, -7, 7, 8, 12])


def j3(rng, x, y, z, chance=0.6):
    return (x + jit(rng, 5, chance), y + jit(rng, 3, chance / 2), z + jit(rng, 5, chance))


def rect_loop(rng, x0, x1, z0, z1, y=0, ccw=None, start=None):
    pts = [(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)]
    if ccw if ccw is not None else rng.random() < 0.5:
        pts = pts[::-1]
    k = rng.randrange(4) if start is None else start
    return pts[k:] + pts[:k]


def unit(rng):
    return rng.choice([16, 32, 64, 128])


def string_points(rng, count=None, x=None, z=None, y=0, straight_bias=0.6):
    """A rectilinear rail string; its corners are far apart enough for the straightness test."""
    count = rng.randrange(2, 7) if count is None else count
    x = rng.randrange(-8, 8) * 128 if x is None else x
    z = rng.randrange(-8, 8) * 128 if z is None else z
    pts = [(x, y, z)]
    direction = rng.randrange(4)
    for _ in range(count - 1):
        if rng.random() > straight_bias:
            direction = (direction + rng.choice([1, 3])) % 4
        step = rng.randrange(2, 7) * 32
        dx, dz = [(step, 0), (0, step), (-step, 0), (0, -step)][direction]
        x, z = x + dx, z + dz
        pts.append((x, y, z))
    return pts


def bump(pts, rng, chance=0.5):
    return [j3(rng, *p, chance=chance) for p in pts]


# ---- single points and polygons ------------------------------------------------------------------------------------------------

@spec(0x004417C0, "GenPoint_Init", "gpoly", this=lambda env: env.slab + POINTS)
def gen_point_init(env, n):
    w = World(env)
    at = w.point_at
    w.ov.put(at, env.rng.randbytes(0x18))
    return [], w.ov.pokes()


@spec(0x004417E0, "GenPoint_Near", "gpoly", this=lambda env: env.slab + POINTS)
def gen_point_near(env, n):
    rng = env.rng
    w = World(env)
    x, y, z = rng.randrange(-300, 300), rng.randrange(-30, 30), rng.randrange(-300, 300)
    w.point(x, y, z)
    d = lambda: rng.choice([0, 0, 1, -1, 5, -5, 6, -6, 7, -7, 20])    # noqa: E731
    other = w.point(x + d(), y + d(), z + d())
    return [other], w.ov.pokes()


@spec(0x00441830, "GenPoly_Init", "gpoly", this=lambda env: env.slab + POLYS)
def gen_poly_init(env, n):
    w = World(env)
    w.ov.put(env.slab + POLYS, env.rng.randbytes(0x1C))
    return [], w.ov.pokes()


def a_string(rng, w, closed=None, count=None):
    closed = rng.random() < 0.5 if closed is None else closed
    if closed:
        x0, z0 = rng.randrange(-8, 8) * 128, rng.randrange(-8, 8) * 128
        pts = rect_loop(rng, x0, x0 + unit(rng) * rng.randrange(1, 5), z0, z0 + unit(rng) * rng.randrange(1, 5))
    else:
        pts = string_points(rng, count)
    return w.string(pts, closed), closed


@spec(0x00441850, "GenPoly_FreePoints", "gpoly", this=lambda env: env.slab + POLYS)
def gen_free_points(env, n):
    w = World(env)
    (poly, _), _ = a_string(env.rng, w)
    return [], w.ov.pokes()


@spec(0x00441880, "GenPoly_Append", "gpoly", this=lambda env: env.slab + POLYS)
def gen_append(env, n):
    rng = env.rng
    w = World(env)
    if rng.random() < 0.15:
        # an empty polygon
        poly = w.blank_poly(False)
        w.fill_poly(poly, [(0, 0, 0)], 0, False, count=0)
    else:
        poly, _ = w.string(string_points(rng, rng.randrange(1, 5)), False)
    point = w.point(rng.randrange(-500, 500), 0, rng.randrange(-500, 500))
    # (appending to a loop would walk round it for ever; a closing append to an empty polygon only logs)
    return [point, rng.choice([0, 0, 0, 1, 0x100, 0x101])], w.ov.pokes()


@spec(0x004419C0, "GenPoly_FixWinding", "gpoly", this=lambda env: env.slab + POLYS)
def gen_fix_winding(env, n):
    rng = env.rng
    w = World(env)
    kind = rng.choice(["rect", "rect", "tri", "poly", "open"])
    if kind == "open":
        w.string(string_points(rng), False)
    elif kind == "rect":
        x0, z0 = rng.randrange(-8, 8) * 64, rng.randrange(-8, 8) * 64
        w.string(rect_loop(rng, x0, x0 + unit(rng) * rng.randrange(1, 6), z0, z0 + unit(rng) * rng.randrange(1, 6)), True)
    elif kind == "tri":
        a = (rng.randrange(-200, 200), 0, rng.randrange(-200, 200))
        pts = [a, (a[0] + rng.randrange(40, 300), 0, a[2]), (a[0] + rng.randrange(-100, 100), 0, a[2] + rng.randrange(40, 300))]
        w.string(pts if rng.random() < 0.5 else pts[::-1], True)
    else:
        # an L shape (a corner that turns the other way at the start now and then)
        x0, z0 = rng.randrange(-4, 4) * 64, rng.randrange(-4, 4) * 64
        pts = [(x0, 0, z0), (x0 + 192, 0, z0), (x0 + 192, 0, z0 + 64), (x0 + 64, 0, z0 + 64), (x0 + 64, 0, z0 + 192), (x0, 0, z0 + 192)]
        k = rng.randrange(6)
        pts = pts[k:] + pts[:k]
        w.string(pts if rng.random() < 0.5 else pts[::-1], True)
    return [], w.ov.pokes()


@spec(0x00441AA0, "GenPoly_Reverse", "gpoly", this=lambda env: env.slab + POLYS)
def gen_reverse(env, n):
    w = World(env)
    a_string(env.rng, w)
    return [], w.ov.pokes()


@spec(0x00441AE0, "GenPoly_CopyTransformed", "gpoly", this=lambda env: env.slab + POLYS + 0x20)
def gen_copy_transformed(env, n):
    rng = env.rng
    w = World(env)
    (src, _), closed = a_string(rng, w)
    mine = w.blank_poly()
    w.ov.u8(mine + 0x14, rng.choice([0, 1]))
    rot = rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, 0xFFFFFFFF]) if rng.random() < 0.95 else rng.choice([4, 9])
    return [src, rng.randrange(-300, 300) & 0xFFFF, rng.randrange(-50, 50) & 0xFFFF, rng.randrange(-300, 300) & 0xFFFF, rot, rng.randrange(8)], w.ov.pokes()


# ---- joining strings and loops ---------------------------------------------------------------------------------------------------

def adjacent_loops(rng, w):
    """Two loops that share an edge (their corners within the near tolerance)."""
    x0, z0 = rng.randrange(-4, 4) * 128, rng.randrange(-4, 4) * 128
    wa, ha = unit(rng) * rng.randrange(2, 5), unit(rng) * rng.randrange(2, 5)
    wb = unit(rng) * rng.randrange(2, 5)
    horizontal = rng.random() < 0.5
    if horizontal:
        a = rect_loop(rng, x0, x0 + wa, z0, z0 + ha)
        b = rect_loop(rng, x0 + wa, x0 + wa + wb, z0, z0 + ha)
    else:
        a = rect_loop(rng, x0, x0 + wa, z0, z0 + ha)
        b = rect_loop(rng, x0, x0 + wa, z0 + ha, z0 + ha + wb)
    if rng.random() < 0.4:
        a, b = b, a
    b = [j3(rng, *p, chance=0.5) for p in b]
    if rng.random() < 0.15:
        # a loop of the same size on top (all four corners shared)
        b = [j3(rng, *p, chance=0.5) for p in rect_loop(rng, x0, x0 + wa, z0, z0 + ha)]
    pa, _ = w.string(a, True)
    pb, _ = w.string(b, True)
    return pa, pb


def octagon_halves(rng, w):
    """Two strings that are the two halves of an octagon: their ends meet at both ends, and the turn there (45 degrees) passes the straightness test."""
    r = rng.choice([100, 200, 300])
    k = 0.7071
    pts = [(r, 0, 0), (int(r * k), 0, int(r * k)), (0, 0, r), (-int(r * k), 0, int(r * k)), (-r, 0, 0), (-int(r * k), 0, -int(r * k)), (0, 0, -r), (int(r * k), 0, -int(r * k))]
    cx, cz = rng.randrange(-4, 4) * 100, rng.randrange(-4, 4) * 100
    pts = [(x + cx, y, z + cz) for x, y, z in pts]
    first = pts[:5]
    second = [j3(rng, *p, chance=0.4) for p in pts[4:] + pts[:1]]
    if rng.random() < 0.3:
        second = second[::-1]
    pa, _ = w.string(first, False)
    pb, _ = w.string(second, False)
    return pa, pb


def meeting_strings(rng, w):
    """Two open strings whose ends meet (end to end, or head to head), straight on or turning."""
    if rng.random() < 0.12:
        return octagon_halves(rng, w)
    first = string_points(rng, rng.randrange(2, 5), straight_bias=0.7)
    last = first[-1]
    direction = (first[-1][0] - first[-2][0], first[-1][2] - first[-2][2])
    if direction[0] != 0:
        step = (1 if direction[0] > 0 else -1) * 32 * rng.randrange(2, 5)
        nxt = [(last[0] + step * (k + 1), last[1], last[2]) for k in range(rng.randrange(1, 4))]
    else:
        step = (1 if direction[1] > 0 else -1) * 32 * rng.randrange(2, 5)
        nxt = [(last[0], last[1], last[2] + step * (k + 1)) for k in range(rng.randrange(1, 4))]
    if rng.random() < 0.3:
        nxt = [(last[0], last[1], last[2] + 64 * (k + 1)) for k in range(rng.randrange(1, 4))] if direction[0] != 0 else [(last[0] + 64 * (k + 1), last[1], last[2]) for k in range(rng.randrange(1, 4))]
    second = [j3(rng, *last, chance=0.5)] + [j3(rng, *p, chance=0.3) for p in nxt]
    mode = rng.random()
    if mode < 0.2:
        second = second[::-1]
    elif mode < 0.35:
        first = first[::-1]
    if rng.random() < 0.15:
        # close the ring: the second string runs back to the first one's start
        second = second + [j3(rng, *first[0], chance=0.5)]
    pa, _ = w.string(first, False)
    pb, _ = w.string(second, False)
    return pa, pb


@spec(0x00441CD0, "GenPoly_TryMerge", "gpoly", this=lambda env: env.slab + POLYS)
def gen_try_merge(env, n):
    rng = env.rng
    w = World(env)
    kind = rng.choice(["loops", "loops", "strings", "strings", "mixed", "far"])
    if kind == "loops":
        pa, pb = adjacent_loops(rng, w)
        mode = rng.choice([0, 0, 0, 1, 2])
    elif kind == "strings":
        pa, pb = meeting_strings(rng, w)
        mode = rng.choice([1, 1, 1, 0, 2])
    elif kind == "mixed":
        (pa, _), _ = a_string(rng, w)
        (pb, _), _ = a_string(rng, w)
        mode = rng.choice([0, 1])
    else:
        pa, pb = adjacent_loops(rng, w)
        for k in range(3):
            w.ov.u16(pb + 2 * k, 0x2000)
        mode = 0
    if rng.random() < 0.05:
        w.ov.u32(pb + 0xC, 0)
    return [pb, mode], w.ov.pokes()


@spec(0x00441DA0, "GenPoly_JoinEnds", "gpoly", this=lambda env: env.slab + POLYS)
def gen_join_ends(env, n):
    rng = env.rng
    w = World(env)
    if rng.random() < 0.9:
        pa, pb = meeting_strings(rng, w)
    else:
        (pa, _), _ = a_string(rng, w, closed=False)
        (pb, _), _ = a_string(rng, w, closed=False)
    return [pb], w.ov.pokes()


@spec(0x00442210, "GenPoly_MergeLoops", "gpoly", this=lambda env: env.slab + POLYS)
def gen_merge_loops(env, n):
    rng = env.rng
    w = World(env)
    if rng.random() < 0.9:
        pa, pb = adjacent_loops(rng, w)
    else:
        (pa, _), _ = a_string(rng, w, closed=True)
        (pb, _), _ = a_string(rng, w, closed=rng.random() < 0.7)
    return [pb], w.ov.pokes()


def runs_of(marks, count):
    """The search of GenPoly_EdgeRuns on the marks of a ring: (index of the first point of the run, index of its last point) or None."""
    state, first, last = 0, None, None
    for i in range(2 * count):
        k = i % count
        if state == 0:
            if not marks[k]:
                state = 1
        elif state == 1:
            if marks[k] and marks[(k + 1) % count]:
                first, state = k, 2
        elif state == 2:
            if not marks[k]:
                return first, last
            last = k
    return None


@spec(0x00442100, "GenPoly_EdgeRuns", "gpoly")
def gen_edge_runs(env, n):
    rng = env.rng
    w = World(env)
    na = rng.randrange(3, 8)
    start, run = rng.randrange(na), rng.randrange(0, na + 1)
    marks_a = [1 if (i - start) % na < run else 0 for i in range(na)]
    if rng.random() < 0.3:
        marks_a = [rng.choice([0, 1]) for _ in range(na)]
    pts_a = [(i * 64, 0, 0) for i in range(na)]
    _, addrs_a = w.string(pts_a, True, marks_a)
    found = runs_of(marks_a, na)
    nb = rng.randrange(3, 8)
    pts_b = [(1000 + i * 64, 0, 100) for i in range(nb)]
    marks_b = [rng.choice([0, 1, 1]) for _ in range(nb)]
    if found and rng.random() < 0.75 and nb >= 3:
        # B runs from a point near the run's last point, over marked points, to a point near the run's first point
        first, last = found
        mids = rng.randrange(0, nb - 1)
        order = [j3(rng, *pts_a[last], chance=0.5)] + [(1000 + 64 * k, 0, 100) for k in range(mids)] + [j3(rng, *pts_a[first], chance=0.5)]
        marks_o = [rng.choice([0, 1])] + [1] * mids + [1]
        while len(order) < nb:
            order.append((2000 + 64 * len(order), 0, 100))
            marks_o.append(rng.choice([0, 1]))
        pts_b, marks_b = order[:nb], marks_o[:nb]
        r = rng.randrange(nb)
        pts_b, marks_b = pts_b[r:] + pts_b[:r], marks_b[r:] + marks_b[:r]
    _, addrs_b = w.string(pts_b, True, marks_b)
    slots = env.slab + SLOTS + 0x20
    for k in range(4):
        w.ov.u32(slots + 4 * k, rng.choice([0, rng.randrange(1 << 32)]))
    count_a = na if rng.random() < 0.9 else na + 1
    return [addrs_a[0], addrs_b[0], count_a, nb, slots, slots + 4, slots + 8, slots + 12], w.ov.pokes()


@spec(0x00442670, "GenPoly_CutBy", "gpoly", this=lambda env: env.slab + POLYS)
def gen_cut_by(env, n):
    rng = env.rng
    w = World(env)
    pa, pb = cutting_pair(rng, w)
    w.ov.u32(env.slab + SLOTS + 0x40, rng.choice([0, rng.randrange(1 << 32)]))
    return [pb, env.slab + SLOTS + 0x40], w.ov.pokes()


def cutting_pair(rng, w, other_closed=0.1, overshoot=0.6):
    """A string and a second string that runs along part of it or on past one of its ends (so the trim and the cut have something to do)."""
    base = string_points(rng, rng.randrange(3, 7), straight_bias=0.8)
    if rng.random() < overshoot:
        # the second string leaves from one end of the first and goes on in the same direction (and now and then bends at its last point)
        at_head = rng.random() < 0.5
        end, before = (base[0], base[1]) if at_head else (base[-1], base[-2])
        dx, dz = end[0] - before[0], end[2] - before[2]
        along = [j3(rng, *end, chance=0.5)]
        cur = end
        for _ in range(rng.randrange(1, 4)):
            cur = (cur[0] + dx, cur[1], cur[2] + dz)
            along.append(j3(rng, *cur, chance=0.3))
        if rng.random() < 0.4:
            last = along[-1]
            along[-1] = (last[0] - dz, last[1], last[2] + dx)
        if rng.random() < 0.3:
            along = along[::-1]
    else:
        along = [j3(rng, *p, chance=0.4) for p in base[rng.randrange(0, 2):rng.randrange(2, len(base) + 1)]]
        if len(along) < 2:
            along = [j3(rng, *p, chance=0.4) for p in base[:2]]
        if rng.random() < 0.4:
            last = along[-1]
            along = along + [(last[0] + 64, last[1], last[2])]
        if rng.random() < 0.3:
            along = along[::-1]
    pa, _ = w.string(base, False)
    pb, _ = w.string(along, rng.random() < other_closed)
    return pa, pb


@spec(0x00442F40, "GenPoly_Trim", "gpoly", this=lambda env: env.slab + POLYS)
def gen_trim(env, n):
    rng = env.rng
    w = World(env)
    pa, pb = cutting_pair(rng, w)
    return [pb], w.ov.pokes()


@spec(0x00442AE0, "GenPoly_CutOut", "gpoly", this=lambda env: env.slab + POLYS)
def gen_cut_out(env, n):
    rng = env.rng
    w = World(env)
    if rng.random() < 0.3:
        # a closed string to cut
        x0, z0 = rng.randrange(-4, 4) * 128, rng.randrange(-4, 4) * 128
        loop = rect_loop(rng, x0, x0 + 256, z0, z0 + 128)
        w.string(loop, True)
        # (a closed string with a single marked point, or none after one, is walked for ever by retail: the kill string touches two or three corners, well within the near tolerance)
        pb, _ = w.string([(x + rng.randrange(-4, 5), y, z + rng.randrange(-4, 5)) for x, y, z in loop[:rng.randrange(2, 4)]], False)
    else:
        # (the kill string is an open one: walking a loop as a string never ends; it touches two or more consecutive points of the first, well within the near
        # tolerance, or retail finds no run to cut and faults)
        base = string_points(rng, rng.randrange(4, 8), straight_bias=0.85)
        i = rng.randrange(0, 2)
        j = rng.randrange(i + 2, len(base) + 1)
        pa, _ = w.string(base, False)
        pb, _ = w.string([(x + rng.randrange(-4, 5), y, z + rng.randrange(-4, 5)) for x, y, z in base[i:j]], False)
    w.ov.u32(env.slab + SLOTS + 0x40, rng.choice([0, rng.randrange(1 << 32)]))
    return [pb, env.slab + SLOTS + 0x40], w.ov.pokes()


@spec(0x00442780, "GenPoly_Split", "gpoly", this=lambda env: env.slab + POLYS)
def gen_split(env, n):
    rng = env.rng
    w = World(env)
    kind = rng.choice(["pinch", "pinch", "plain", "open"])
    if kind == "open":
        w.string(string_points(rng), False)
    elif kind == "plain":
        x0, z0 = rng.randrange(-4, 4) * 128, rng.randrange(-4, 4) * 128
        w.string(rect_loop(rng, x0, x0 + 256, z0, z0 + 128), True)
    else:
        x0, z0 = rng.randrange(-4, 4) * 128, rng.randrange(-4, 4) * 128
        s = rng.choice([64, 128])        # (points of a pinched loop closer than the near tolerance make retail walk for ever)
        arm = rng.choice([0, 0, 1, 2])
        pts = [(x0, 0, z0), (x0 + s, 0, z0), (x0 + s, 0, z0 + s), (x0 + 2 * s, 0, z0 + s)]
        pts += [(x0 + 2 * s, 0, z0 + s + (k + 1) * (s // (arm + 1))) for k in range(arm + 1)]
        pts += [(x0 + s, 0, z0 + 2 * s), (x0 + s, 0, z0 + s), (x0, 0, z0 + s)]
        pts[-2] = j3(rng, *pts[-2], chance=0.7)
        k = rng.randrange(len(pts))
        pts = pts[k:] + pts[:k]
        w.string(pts if rng.random() < 0.5 else pts[::-1], True)
    return [], w.ov.pokes()


@spec(0x00442970, "GenPoly_UpdateBounds", "gpoly", this=lambda env: env.slab + POLYS)
def gen_update_bounds(env, n):
    rng = env.rng
    w = World(env)
    kind = rng.random()
    if kind < 0.2:
        (poly, addrs), _ = a_string(rng, w, count=2)
        w.ov.u32(addrs[0] + 8, 0)
    else:
        (poly, _), _ = a_string(rng, w)
    w.ov.put(poly, rng.randbytes(12))
    return [], w.ov.pokes()


@spec(0x00442E90, "GenPoint_EdgeStraight", "gpoly")
def gen_edge_straight(env, n):
    rng = env.rng
    w = World(env)
    p = (rng.randrange(-300, 300), 0, rng.randrange(-300, 300))
    q = (p[0] + rng.choice([-1, 1]) * rng.randrange(8, 200), 0, p[2] + rng.choice([0, 0, rng.randrange(-100, 100)]))
    r = (q[0] + rng.randrange(-200, 200), 0, q[2] + rng.randrange(-200, 200))
    if r == q:
        r = (q[0] + 16, 0, q[2])
    if q == p:
        q = (p[0] + 16, 0, p[2])
    a, b, c = w.point(*p), w.point(*q), w.point(*r)
    return [a, b, c, rng.choice([0x800, 0x800, 0xB50, 0, -0x800])], w.ov.pokes()


# ---- lists of polygons ------------------------------------------------------------------------------------------------------------

def some_polys(rng, w, count, loops=None):
    polys = []
    for _ in range(count):
        (p, _), _ = a_string(rng, w, closed=loops)
        polys.append(p)
    return polys


@spec(0x004431F0, "GenPolyList_Init", "gpoly", this=lambda env: env.slab + LISTS)
def gen_plist_init(env, n):
    w = World(env)
    w.ov.put(env.slab + LISTS, env.rng.randbytes(12))
    return [], w.ov.pokes()


@spec(0x00443200, "GenPolyList_FreeAllThunk", "gpoly", this=lambda env: env.slab + LISTS)
def gen_plist_thunk(env, n):
    rng = env.rng
    w = World(env)
    w.list_object(some_polys(rng, w, rng.randrange(0, 4)))
    return [], w.ov.pokes()


@spec(0x00443210, "GenPolyList_NewPoly", "gpoly", this=lambda env: env.slab + LISTS)
def gen_plist_new(env, n):
    rng = env.rng
    w = World(env)
    w.list_object(some_polys(rng, w, rng.randrange(0, 4)))
    return [], w.ov.pokes()


@spec(0x00443250, "GenPolyList_FreeAll", "gpoly", this=lambda env: env.slab + LISTS)
def gen_plist_free_all(env, n):
    rng = env.rng
    w = World(env)
    w.list_object(some_polys(rng, w, rng.randrange(0, 5)))
    return [], w.ov.pokes()


@spec(0x00443290, "GenPolyList_Count", "gpoly", this=lambda env: env.slab + LISTS)
def gen_plist_count(env, n):
    rng = env.rng
    w = World(env)
    w.list_object(some_polys(rng, w, rng.randrange(0, 6)))
    return [], w.ov.pokes()


@spec(0x004432B0, "GenPolyList_CountPoints", "gpoly", this=lambda env: env.slab + LISTS)
def gen_plist_points(env, n):
    rng = env.rng
    w = World(env)
    w.list_object(some_polys(rng, w, rng.randrange(0, 4)))
    return [], w.ov.pokes()


@spec(0x00443580, "GenPolyList_Pop", "gpoly", this=lambda env: env.slab + LISTS)
def gen_plist_pop(env, n):
    rng = env.rng
    w = World(env)
    w.list_object(some_polys(rng, w, rng.randrange(1, 4)))
    return [], w.ov.pokes()


@spec(0x004435E0, "GenPolyList_Append", "gpoly", this=lambda env: env.slab + LISTS)
def gen_plist_append(env, n):
    rng = env.rng
    w = World(env)
    w.list_object(some_polys(rng, w, rng.randrange(0, 4)))
    extra = some_polys(rng, w, 1)[0]
    if rng.random() < 0.1:
        w.ov.u32(extra + 0xC, 0)
    return [extra], w.ov.pokes()


@spec(0x004434E0, "GenPolyList_Pair", "gpoly", this=lambda env: env.slab + LISTS + 0x10)
def gen_plist_pair(env, n):
    rng = env.rng
    w = World(env)
    kind = rng.choice(["loops", "strings", "cut"])
    if kind == "loops":
        pa, pb = adjacent_loops(rng, w)
        mode = rng.choice([0, 0, 1])
    elif kind == "strings":
        pa, pb = meeting_strings(rng, w)
        mode = rng.choice([1, 1, 0])
    else:
        pa, pb = cutting_pair(rng, w)
        mode = 1
    w.list_object([pa], slot=1)
    w.list_object([], slot=0)
    return [pb, mode], w.ov.pokes()


@spec(0x004432D0, "GenPolyList_Merge", "gpoly", this=lambda env: env.slab + LISTS)
def gen_plist_merge(env, n):
    rng = env.rng
    w = World(env)
    mode = rng.choice([0, 0, 1])
    if mode == 0:
        existing = [adjacent_loops(rng, w)[0] for _ in range(rng.randrange(0, 3))]
        pa, pb = adjacent_loops(rng, w)
        incoming = [pa, pb]
    else:
        existing = [meeting_strings(rng, w)[0] for _ in range(rng.randrange(0, 2))]
        pa, pb = meeting_strings(rng, w)
        incoming = [pa, pb]
    deadline = rng.choice([0xFFFFFFC4, 0x7FFFFF00])       # +0x3c wraps to 0 (the clock is later: late) or stays far ahead of it (not late)
    w.list_object(existing, slot=0, deadline=deadline)
    w.list_object(incoming, slot=1)
    return [env.slab + LISTS + 0x10, mode], w.ov.pokes()


def poly_stubs(fc, env):
    ensure(fc, 0x0045D760, 1, masks={0: 0xFF})


GROUP_STUBS = {"gpoly": poly_stubs}
