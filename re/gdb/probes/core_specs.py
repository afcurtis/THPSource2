"""Phase 4.14c probe: the core utilities (group utl: utils.cpp and the vector helpers).

The vector, distance and angle functions of `utils.cpp`, the object-list sweeps and the 3-vector operators (0x004c9300..0x004cad70). They work on vectors, angle records and object lists in a scratch
slab (poked per case with edge-biased random words), on the GTE register file and on a few globals (the random generator, the viewport records, the list heads); the nested functions (the Q12
trigonometry, the arctangent, `gte_sqr0`, `gte_rtir`, `_ftol` and the functions of this group that call each other) are the real retail functions. The only stubs are the services outside the
group: the integer square root `0x004f53b0` (answered with the truncated root of its argument), the object skip test `0x004805f0` (scripted per object), the collision queries `0x004624d0` and
`0x00466090` (the second one fills the hit fields of the record the caller keeps in a stack local) and the two virtual methods the fake objects' vtable names.

Slab layout (offsets): vectors 0x000 / 0x010 / 0x020 / 0x030, angle records 0x040 / 0x050 / 0x060, shorts and the results 0x070, texts 0x100 / 0x140 (0x40 bytes each), two objects 0x200 / 0x280 (0x80
bytes), the list objects 0x400 + k * 0x80 (k < 16), the fake vtable 0xc00, the boxes 0xc20 / 0xc30, the type-2 objects 0xd00 + k * 0x80 (k < 4).
"""

from __future__ import annotations

import math
import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import check_disjoint, merged
from frontend_specs_menu import Image

GROUPS = ("utl",)

SLAB = 0x1000
VEC_A = 0x000
VEC_B = 0x010
VEC_C = 0x020
VEC_D = 0x030
ANG_A = 0x040
ANG_B = 0x050
ANG_C = 0x060
SHORTS = 0x070
TEXT_A = 0x100
TEXT_B = 0x140
OBJ_A = 0x200
OBJ_B = 0x280
LIST = 0x400
VTABLE = 0xC00
BOX_LOW = 0xC20
BOX_HIGH = 0xC30
RAILS = 0xD00

INFO = 0x7FF00C00              # what the query record's address is recorded as (pc_platform.hpp kScratchBase + 0x400)

GTE = 0x006A3E00
GTE_BYTES = 0x120

WINDOWS = [(GTE, GTE_BYTES), (0x0056E28C, 0x28), (0x0056E30C, 4), (0x00533F38, 8), (0x0056A954, 4), (0x0056A960, 4), (0x0055F6BC, 4), (0x0056AF40, 8), (0x0056B830, 4), (0x0056DB28, 4),
           (0x0055F704, 4), (0x0056D432, 0xD0), (0x0055F7C8, 0x148), (0x0055FA3C, 4), (0x00567C84, 4)]

SLOT0 = 0x004E87B0      # the fake vtable's slot 0 (a deleting destructor: thiscall, one argument) and slot 2 (a kill method: thiscall, none)
SLOT2 = 0x004E87C0

STATE = {"skip": [], "hit": 0, "slope": 0, "hity": 0}


def utl_regions(slab):
    result = merged([(slab, SLAB)] + WINDOWS, gap=0)
    return result


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = utl_regions
    READ_ONLY[_group] = []


def u(value):
    return value & 0xFFFFFFFF


def utl(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "utl", this=resolve)


# ---- random words ----------------------------------------------------------------------------------------------------------------------------------------------------------

def pick_coord(rng, scale="mixed"):
    """A world coordinate (Q12 units): small, around a few thousand units, and large."""
    kind = rng.choice(["small", "units", "units", "big", "edge", "zero"]) if scale == "mixed" else scale
    if kind == "zero":
        return 0
    if kind == "small":
        return rng.randrange(-0x2000, 0x2000)
    if kind == "units":
        return rng.randrange(-0x400000, 0x400000)
    if kind == "big":
        return rng.randrange(-0x10000000, 0x10000000)
    return rng.choice([0x7FFFFFFF, -0x80000000, 0x1000, -0x1000, 0xFFF, -0xFFF, 0x7FFFF000, -0x7FFFF000])


def pick_vec(rng, scale="mixed"):
    return [pick_coord(rng, scale) for _ in range(3)]


def put_vec(img, address, values):
    for k, v in enumerate(values):
        img.u32(address + 4 * k, u(v))


def pick_angle(rng):
    return rng.choice([0, 1, 0x100, 0x400, 0x800, 0xC00, 0xFFF, 0x1000, 0x1001, -1, -0x400, 0x12345, rng.randrange(0, 0x1000), rng.randrange(0, 0x1000), rng.randrange(-0x8000, 0x8000)])


def pick16(rng):
    return rng.choice([0, 1, -1, 0x7FFF, -0x8000, 0x400, 0x800, 0xC00, 0x7FF, 0x801, -0x7FF, -0x801, rng.randrange(0, 0x1000), rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000)])


def isqrt(a):
    value = a[0] - (1 << 32) if a[0] & 0x80000000 else a[0]
    return math.isqrt(value) if value > 0 else 0


def seed_world(img, rng):
    """The register file and the random generator: random words, so a function that reads them without writing them first shows up."""
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))
    for off in (0x0, 0x4, 0x20):
        img.u32(0x0056E28C + off, rng.getrandbits(32))


# ---- the stubs ---------------------------------------------------------------------------------------------------------------------------------------------------------------

def install_core_stubs(fc, env):
    from frontend_specs_front import ensure
    from platform_specs import mark_under_test

    if getattr(fc, "_utl_installed", False):
        return
    fc._utl_installed = True
    mark_under_test()
    ensure(fc, 0x004F53B0, 1, "engine_004f53b0", result=isqrt)
    ensure(fc, 0x004805F0, 0, "engine_004805f0", convention="thiscall", this=True, result=lambda a: STATE["skip"].pop(0) if STATE["skip"] else 0)
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=INFO)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=INFO,
           pokes=[(0, 4, lambda a: STATE["hit"], 0x68), (0, 2, lambda a: STATE["slope"], 0x7A), (0, 4, lambda a: STATE["hity"], 0x70)])
    ensure(fc, SLOT0, 1, "engine_%08x" % SLOT0, convention="thiscall", this=True)
    ensure(fc, SLOT2, 0, "engine_%08x" % SLOT2, convention="thiscall", this=True)


GROUP_STUBS = {group: install_core_stubs for group in GROUPS}


# ---- the functions -----------------------------------------------------------------------------------------------------------------------------------------------------------

@utl(0x004C9300, "Pause")
def gen_pause(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    return [rng.choice([0, 1, 10, 0x7FFFFFFF])], img.pokes()


@utl(0x004C9320, "Utils_InitialRand")
def gen_initial_rand(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    return [rng.getrandbits(32)], img.pokes()


def rotate_spec(address, name):
    @utl(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        seed_world(img, rng)
        slab = env.slab
        put_vec(img, slab + VEC_A, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
        put_vec(img, slab + VEC_D, pick_vec(rng))
        out = rng.choice([VEC_D, VEC_D, VEC_A])               # (the output may be the input)
        return [slab + out, slab + VEC_A, u(pick_angle(rng))], img.pokes()

    return generate


rotate_spec(0x004C9390, "Utils_RotateY")
rotate_spec(0x004C9400, "Utils_RotateYQ12")


@utl(0x004C9470, "Utils_CalcUnit")
def gen_calc_unit(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    kind = rng.choice(["small", "units", "mixed", "zero"])
    put_vec(img, slab + VEC_A, [0, 0, 0] if kind == "zero" else pick_vec(rng, kind))
    put_vec(img, slab + VEC_D, pick_vec(rng))
    return [slab + rng.choice([VEC_D, VEC_D, VEC_A]), slab + VEC_A], img.pokes()


@utl(0x004C9500, "Camera_DirectionFromAnglesRaw")
def gen_direction(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    img.u16(slab + ANG_A, u(pick16(rng)))
    img.u16(slab + ANG_A + 2, u(pick16(rng)))
    put_vec(img, slab + VEC_D, pick_vec(rng))
    return [slab + VEC_D, u(rng.choice([0, 1, 0x1000, -0x1000, rng.randrange(-0x20000, 0x20000), rng.getrandbits(32)])), slab + ANG_A], img.pokes()


def two_points(img, env, rng):
    slab = env.slab
    scale = rng.choice(["small", "units", "mixed"])
    put_vec(img, slab + VEC_A, pick_vec(rng, scale))
    put_vec(img, slab + VEC_B, pick_vec(rng, scale))
    return slab


@utl(0x004C9590, "Utils_Dist")
def gen_dist(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = two_points(img, env, rng)
    return [slab + VEC_A, slab + VEC_B], img.pokes()


@utl(0x004C95E0, "Utils_XZDist")
def gen_xz_dist(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = two_points(img, env, rng)
    return [slab + VEC_A, slab + VEC_B], img.pokes()


@utl(0x004C96D0, "Utils_VectorCosineQ12")
def gen_cosine(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    centre = pick_vec(rng, "units")
    reach = rng.choice([0x3000, 0x40000, 0x400000, 0x4000000, 0x9C40000, 0xA000000])
    put_vec(img, slab + VEC_B, centre)
    for addr in (VEC_A, VEC_C):
        v = [u(centre[k] + rng.randrange(-reach, reach)) for k in range(3)]
        v[rng.randrange(3)] = u(centre[0] + rng.choice([-1, 1]) * rng.randrange(0x2000, max(0x2001, reach)))     # (some axis is far enough for a distance of at least one)
        put_vec(img, slab + addr, v)
    return [slab + VEC_A, slab + VEC_B, slab + VEC_C], img.pokes()


@utl(0x004C9770, "Camera_BuildLookAngles")
def gen_look_angles(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    centre = pick_vec(rng, "units")
    put_vec(img, slab + VEC_A, centre)
    mode = rng.choice(["free", "free", "same_z", "same_xz", "tiny"])
    offset = [rng.randrange(-0x800000, 0x800000) for _ in range(3)]
    if mode == "same_z":
        offset[2] = rng.randrange(-0xFFF, 0x1000)
    elif mode == "same_xz":
        offset[0] = rng.randrange(-0xFFF, 0x1000)
        offset[2] = rng.randrange(-0xFFF, 0x1000)
    elif mode == "tiny":
        offset = [rng.randrange(-0x3000, 0x3000) for _ in range(3)]
    put_vec(img, slab + VEC_B, [u(centre[k] + offset[k]) for k in range(3)])
    img.put(slab + ANG_A, bytes(rng.getrandbits(8) for _ in range(8)))
    return [slab + ANG_A, slab + VEC_A, slab + VEC_B], img.pokes()


@utl(0x004C9880, "Utils_TurnTowards")
def gen_turn_towards(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    img.put(slab + SHORTS, bytes(rng.getrandbits(8) for _ in range(8)))
    same = rng.random() < 0.2
    a0, a1 = u(pick16(rng)) & 0xFFFF, u(pick16(rng)) & 0xFFFF
    b0, b1 = (a0, a1) if same else (u(pick16(rng)) & 0xFFFF, u(pick16(rng)) & 0xFFFF)
    return [a0 | (a1 << 16), rng.getrandbits(32), slab + SHORTS, slab + SHORTS + 4, b0 | (b1 << 16), rng.getrandbits(32),
            u(rng.choice([0, 1, 0x100, 0x80, -0x100, rng.randrange(-0x1000, 0x1000)]))], img.pokes()


@utl(0x004C9CB0, "Utils_ClearExplosionStack")
def gen_clear_explosion(env, n):
    img, rng = Image(), env.rng
    img.u32(0x0056E30C, rng.getrandbits(32))
    return [], img.pokes()


@utl(0x004C9F50, "CaseInsensitiveBoundedNameEqual")
def gen_name_equal(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    seed_world(img, rng)
    stem = "".join(rng.choice("abcdeXYZ_09@[`{") for _ in range(rng.randrange(0, 12)))
    other = stem
    kind = rng.choice(["same", "case", "differ", "tail", "short"])
    if kind == "case":
        other = stem.swapcase()
    elif kind == "differ" and stem:
        k = rng.randrange(len(stem))
        other = stem[:k] + rng.choice("abcQ@[") + stem[k + 1:]
    elif kind == "tail":
        other = stem + "zz"
    elif kind == "short":
        other = stem[:-1]
    a = slab + TEXT_A
    b = slab + TEXT_B
    img.put(a, stem.encode("latin-1") + b"\0")
    img.put(b, other.encode("latin-1") + b"\0")
    roll = rng.random()
    args = [0 if roll < 0.06 else a, 0 if 0.03 < roll < 0.09 else b, rng.choice([0, 1, 2, 3, 5, 8, 100, 0x7FFFFFFF, 0xFFFFFFFF])]
    if args[2] == 0 and args[0] and args[1]:
        args[2] = 1          # (a count of 0 reads on to the terminator: allowed, kept rare)
    return args, img.pokes()


def put_object(img, env, address, rng, pos=None):
    slab = env.slab
    img.put(address, bytes(rng.getrandbits(8) for _ in range(0x80)))
    img.u32(address, slab + VTABLE)
    if pos is not None:
        put_vec(img, address + 8, pos)


@utl(0x004C9FD0, "CPlatform__AI")
def gen_platform_ai(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    centre = pick_vec(rng, "units")
    put_object(img, env, slab + OBJ_A, rng, centre)
    near = rng.choice([True, True, False])
    spread = 0x80000 if near else 0x3000000
    put_object(img, env, slab + OBJ_B, rng, [u(centre[k] + rng.randrange(-spread, spread)) for k in range(3)])
    img.u16(slab + OBJ_A + 0x14, u(pick16(rng)))
    img.u16(slab + OBJ_A + 0x16, u(pick16(rng)))
    return [slab + OBJ_A, slab + OBJ_B, rng.choice([0x100, 0x400, 0x800, 0x7FFF, 0, rng.randrange(0, 0x1000)]), rng.choice([0x100, 0x400, 0x800, 0x7FFF, 0, rng.randrange(0, 0x1000)]),
            rng.choice([0, 1, 100, 1000, 0x7FFF, rng.randrange(0, 4000)])], img.pokes()


@utl(0x004CA0A0, "Camera_AcosRatioToAngle12")
def gen_acos(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    value = rng.choice([4096, -4096, 0, 1, -1, 4095, -4095, 2048, -2048, rng.randrange(-4096, 4097), rng.randrange(-4096, 4097), rng.randrange(-4096, 4097)])
    return [u(value)], img.pokes()


def query_world(img, env, rng):
    seed_world(img, rng)
    slab = env.slab
    put_vec(img, slab + VEC_A, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
    put_vec(img, slab + VEC_B, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
    img.u32(0x00567C84, rng.choice([0, 0, 1, 7]))
    STATE["hit"] = rng.choice([0, 0, 0, slab + OBJ_A, 0x00DB0010, rng.getrandbits(32)])
    STATE["slope"] = rng.choice([-0x1000, -0xC00, -0xBFF, -0xBFE, 0, 0x100, 0x7FFF, -0x8000, rng.randrange(-0x1000, 0x1000)]) & 0xFFFF
    STATE["hity"] = rng.getrandbits(32)
    return slab


@utl(0x004CA0C0, "Utils_LineOfSight")
def gen_line_of_sight(env, n):
    img, rng = Image(), env.rng
    slab = query_world(img, env, rng)
    return [slab + VEC_A, slab + VEC_B], img.pokes()


@utl(0x004CA140, "Utils_GetGroundHeight")
def gen_ground_height(env, n):
    img, rng = Image(), env.rng
    slab = query_world(img, env, rng)
    return [slab + VEC_A, u(rng.choice([0, 1, 5, 100, -3, rng.randrange(-300, 300)])), u(rng.choice([0, 1, 5, 100, -3, rng.randrange(-300, 300)]))], img.pokes()


# ---- the object lists -------------------------------------------------------------------------------------------------------------------------------------------------------

def make_list(img, env, rng, count, first_slot, box, spread=1.0):
    """`count` objects linked through +0x20, at positions around the box: inside, outside and on the boundary. Returns the address of the first (0 for an empty list)."""
    slab = env.slab
    low, high = box
    objects = []
    for k in range(count):
        address = slab + LIST + (first_slot + k) * 0x80
        pos = []
        for axis in range(3):
            roll = rng.random()
            if roll < 0.45:
                pos.append(rng.randrange(low[axis], high[axis] + 1))
            elif roll < 0.6:
                pos.append(rng.choice([low[axis], high[axis], low[axis] - 1, high[axis] + 1]))
            else:
                pos.append(rng.randrange(low[axis] - 0x100000, high[axis] + 0x100000))
        put_object(img, env, address, rng, pos)
        objects.append(address)
    for a, b in zip(objects, objects[1:] + [0]):
        img.u32(a + 0x20, b)
    return objects[0] if objects else 0, objects


def make_box(rng):
    low = [rng.randrange(-0x400000, 0x100000) for _ in range(3)]
    high = [low[k] + rng.randrange(0, 0x600000) for k in range(3)]
    return low, high


@utl(0x004CA1E0, "Utils_KillObjectsInBox")
def gen_kill_in_box(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    box = make_box(rng)
    put_vec(img, slab + BOX_LOW, box[0])
    put_vec(img, slab + BOX_HIGH, box[1])
    img.put(slab + VTABLE, struct.pack("<III", SLOT0, 0x004E87D0, SLOT2))
    count = rng.choice([0, 1, 2, 3, 5, 8])
    head, objects = make_list(img, env, rng, count, 0, box)
    STATE["skip"] = [rng.choice([0, 0, 0, 1]) for _ in objects]
    return [slab + BOX_LOW, slab + BOX_HIGH, head, rng.choice([0, 1, 1, 0x100, 0x1FF])], img.pokes()


@utl(0x004CA270, "RuntimeObject_ApplyBoundsToLists")
def gen_apply_bounds(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    box = make_box(rng)
    put_vec(img, slab + BOX_LOW, box[0])
    put_vec(img, slab + BOX_HIGH, box[1])
    img.put(slab + VTABLE, struct.pack("<III", SLOT0, 0x004E87D0, SLOT2))
    slot = 0
    skips = []
    for head_address in (0x0055F6BC, 0x0056AF40, 0x0056B830, 0x0056AF44):
        count = rng.choice([0, 1, 2, 3])
        head, objects = make_list(img, env, rng, count, slot, box)
        slot += count
        img.u32(head_address, head)
        skips += [rng.choice([0, 0, 0, 1]) for _ in objects]
    STATE["skip"] = skips
    return [slab + BOX_LOW, slab + BOX_HIGH], img.pokes()


@utl(0x004CA2D0, "RuntimeObject_UpdateVisibilityBounds")
def gen_visibility(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    box = make_box(rng)
    put_vec(img, slab + BOX_LOW, box[0])
    put_vec(img, slab + BOX_HIGH, box[1])
    img.u32(0x0056A954, rng.choice([0, slab + OBJ_A, slab + OBJ_B]))
    img.u32(0x0056A960, rng.choice([0, slab + OBJ_A, slab + OBJ_B]))
    img.u32(0x00533F3C, rng.choice([1, 2, 2]))
    img.u32(0x00533F38, rng.choice([0, 1, 7, 7, 9, 10]))
    for region in range(4):
        img.u8(0x0056D432 + 0x44 * region, rng.choice([0, 1, 1, 7]))
    slot = 0
    for head_address in (0x0056DB28, 0x0056B830, 0x0056AF44, 0x0056AF40):
        count = rng.choice([0, 1, 2, 3])
        head, objects = make_list(img, env, rng, count, slot, box)
        slot += count
        img.u32(head_address, head)
        if head_address == 0x0056DB28:
            for o in objects:
                img.u8(o + 0x1F, rng.randrange(0, 4))
    # the type-2 objects keep their position at +0x10 (x), +0x14 (y), +0x18 (z) and link through +8
    rails = []
    for k in range(rng.choice([0, 1, 2, 3])):
        address = slab + RAILS + 0x80 * k
        img.put(address, bytes(rng.getrandbits(8) for _ in range(0x80)))
        img.u8(address + 0x3B, rng.choice([2, 2, 2, 1, 0]))
        for axis, off in enumerate((0x10, 0x14, 0x18)):
            img.u32(address + off, u(rng.randrange(box[0][axis] - 0x1000, box[1][axis] + 0x1000)))
        rails.append(address)
    for a, b in zip(rails, rails[1:] + [0]):
        img.u32(a + 8, b)
    img.u32(0x0055F704, rails[0] if rails else 0)
    return [slab + BOX_LOW, slab + BOX_HIGH, rng.choice([0, 1, 0x101]), rng.choice([0, 1, 1, 0x180])], img.pokes()


@utl(0x004CA690, "Utils_CalculateSpatialAttenuation")
def gen_attenuation(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    index = rng.choice([0, 1])
    img.u32(0x0055FA3C, index)
    view = 0x0055F7C8 + 0xA4 * index
    img.put(view, bytes(rng.getrandbits(8) for _ in range(0xA4)))
    listener = [rng.randrange(-0x400, 0x400) for _ in range(3)]
    for k in range(3):
        img.u32(view + 4 + 4 * k, u(listener[k]))
    for k in range(9):
        img.u16(view + 0x74 + 2 * k, u(rng.choice([0, 0x1000, -0x1000, 0x800, -0x800, rng.randrange(-0x1000, 0x1001)])))
    mode = rng.choice(["near", "mid", "mid", "far", "same"])
    spread = {"near": 0x800000, "mid": 0x4000000, "far": 0x20000000, "same": 0}[mode]
    pos = [u((listener[k] << 12) + (rng.randrange(-spread, spread) if spread else 0)) for k in range(3)]
    put_vec(img, slab + VEC_A, pos)
    near_d = rng.choice([0, 1, 5, 10, rng.randrange(0, 200)])
    far_d = rng.choice([near_d + 1, near_d + 100, 1000, 5000, 0x7FFF, rng.randrange(near_d + 1, 20000)])
    return [slab + VEC_A, near_d, far_d], img.pokes()


@utl(0x004CA890, "Utils_Pulse")
def gen_pulse(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    period = rng.choice([0, 2, 3, 4, 100, 101, 1000, rng.randrange(2, 5000), -5])
    value = rng.choice([-1, 0, 1, period // 2 if period > 0 else 0, period - 1, period, period + 1, rng.randrange(-10, 5100)])
    return [u(value), u(period), u(rng.choice([0, 1, 100, 1000, -50, rng.randrange(-100000, 100000)]))], img.pokes()


@utl(0x004CA8D0, "Utils_LimitRange")
def gen_limit_range(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    img.u32(slab + VEC_A, u(pick_coord(rng, rng.choice(["small", "mixed"]))))
    low = rng.choice([-100, 0, -0x1000, pick_coord(rng)])
    high = rng.choice([100, 0x1000, 0, pick_coord(rng)])
    return [slab + VEC_A, u(low), u(high)], img.pokes()


@utl(0x004CA940, "Math_Vector3SquaredLength", this=VEC_A)
def gen_squared_length(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    put_vec(img, env.slab + VEC_A, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
    return [], img.pokes()


@utl(0x004CA980, "Math_Vector3Normalize", this=VEC_A)
def gen_normalize(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    put_vec(img, env.slab + VEC_A, pick_vec(rng, rng.choice(["small", "units", "mixed", "zero"])))
    return [], img.pokes()


@utl(0x004CA9B0, "Math_Vector3DeadZone", this=VEC_A)
def gen_dead_zone(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    put_vec(img, env.slab + VEC_A, [rng.choice([0, 1, -1, 0x800, 0x801, -0x800, -0x801, 0x7FF, rng.randrange(-0x1000, 0x1000), pick_coord(rng)]) for _ in range(3)])
    return [], img.pokes()


def out_spec(address, name):
    @utl(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        seed_world(img, rng)
        slab = env.slab
        put_vec(img, slab + VEC_A, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
        put_vec(img, slab + VEC_B, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
        put_vec(img, slab + VEC_D, pick_vec(rng))
        out = rng.choice([VEC_D, VEC_D, VEC_A])
        return [slab + out, slab + VEC_A, slab + VEC_B], img.pokes()

    return generate


out_spec(0x004CABB0, "Math_Vector3AddOut")
out_spec(0x004CABF0, "Vector3_Subtract")


@utl(0x004CAC30, "Math_Vector3MulScalarOut")
def gen_mul_out(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    put_vec(img, slab + VEC_A, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
    img.u32(slab + VEC_B, u(pick_coord(rng, rng.choice(["small", "units", "mixed"]))))
    put_vec(img, slab + VEC_D, pick_vec(rng))
    return [slab + rng.choice([VEC_D, VEC_A]), slab + VEC_A, slab + VEC_B], img.pokes()


@utl(0x004CAC60, "Math_ScalarMulVector3Out")
def gen_scalar_mul_out(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    put_vec(img, slab + VEC_B, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
    img.u32(slab + VEC_A, u(pick_coord(rng, rng.choice(["small", "units", "mixed"]))))
    put_vec(img, slab + VEC_D, pick_vec(rng))
    return [slab + rng.choice([VEC_D, VEC_B]), slab + VEC_A, slab + VEC_B], img.pokes()


@utl(0x004CAC90, "Math_Vector3DivScalarOut")
def gen_div_out(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    put_vec(img, slab + VEC_A, [rng.randrange(-0x7FFFFFFF, 0x7FFFFFFF) if rng.random() < 0.5 else max(pick_coord(rng), -0x7FFFFFFF) for _ in range(3)])      # (INT_MIN / -1 faults)
    divisor = rng.choice([1, 2, 3, -1, -2, 7, 4096, -4096, 0x7FFFFFFF, rng.randrange(1, 100000), -rng.randrange(1, 100000)])
    img.u32(slab + VEC_B, u(divisor))
    put_vec(img, slab + VEC_D, pick_vec(rng))
    return [slab + rng.choice([VEC_D, VEC_A]), slab + VEC_A, slab + VEC_B], img.pokes()


def shift_spec(address, name):
    @utl(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        seed_world(img, rng)
        slab = env.slab
        put_vec(img, slab + VEC_A, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
        img.u32(slab + VEC_B, rng.choice([0, 1, 4, 12, 31, 32, 33, 0xFF, rng.getrandbits(32)]))
        put_vec(img, slab + VEC_D, pick_vec(rng))
        return [slab + rng.choice([VEC_D, VEC_A]), slab + VEC_A, slab + VEC_B], img.pokes()

    return generate


shift_spec(0x004CACD0, "Math_Vector3ShiftRightOut")
shift_spec(0x004CAD00, "Math_Vector3ShiftLeftOut")


@utl(0x004CAD30, "Math_Vector3NegateOut", this=VEC_A)
def gen_negate_out(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    put_vec(img, slab + VEC_A, pick_vec(rng, rng.choice(["small", "units", "mixed"])))
    put_vec(img, slab + VEC_D, pick_vec(rng))
    return [slab + rng.choice([VEC_D, VEC_A])], img.pokes()


@utl(0x004CAD60, "Math_Vector3sMask12", this=SHORTS)
def gen_mask12(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    img.put(env.slab + SHORTS, bytes(rng.getrandbits(8) for _ in range(8)))
    return [], img.pokes()


# ---- unlisted functions of utils.cpp --------------------------------------------------------------------------------------------------------------------------------------------

@utl(0x004CA620, "Utils_IsEnvironmentObject")
def gen_is_environment_object(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    objects = [slab + LIST + 0x80 * k for k in range(rng.choice([0, 1, 2, 3]))]
    for k, address in enumerate(objects):
        put_object(img, env, address, rng, pick_vec(rng))
        img.u32(address + 0x20, objects[k + 1] if k + 1 < len(objects) else 0)
    img.u32(0x0056DB28, objects[0] if objects else 0)
    return [rng.choice(objects + [0, slab + OBJ_A, slab + LIST + 0x300])], img.pokes()


@utl(0x004CA650, "Utils_ShuffleShorts")
def gen_shuffle_shorts(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    count = rng.choice([0, 1, 2, 3, 5, 8, -1])
    img.put(env.slab + VEC_A, bytes(rng.getrandbits(8) for _ in range(16)))
    return [env.slab + VEC_A, u(count)], img.pokes()


@utl(0x004C9C90, "Utils_FlagListObjects")
def gen_flag_list_objects(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    box = make_box(rng)
    head, objects = make_list(img, env, rng, rng.choice([0, 1, 2, 4]), 0, box)
    return [head], img.pokes()


@utl(0x004C9FB0, "Utils_StringCopy")
def gen_string_copy(env, n):
    img, rng = Image(), env.rng
    seed_world(img, rng)
    slab = env.slab
    img.put(slab + TEXT_A, bytes(rng.choice(b"abcXYZ_09 ") for _ in range(rng.randrange(0, 20))) + b"\0")
    img.put(slab + TEXT_B, bytes(rng.getrandbits(8) for _ in range(0x30)))
    return [slab + TEXT_A, slab + TEXT_B], img.pokes()
