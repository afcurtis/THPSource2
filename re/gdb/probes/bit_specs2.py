"""Phase 4.15d probe: the spark settings and the animated bit classes (group bit4: 0x00406070..0x00407050) on the slab and the services of bit_specs.py.

Fake objects: a bit is a record in the slab (vtable of the fake class, a random body), an animation is a fake array with its frame count in the dword before the first entry, a ribbon is an
object whose ring of points and array of bits are laid out by `fake_ribbon`.
"""

from __future__ import annotations

from bit_specs import (ANIM_STRIDE, ANIMS, ARENA, BIT_STRIDE, BITS, FAKE_VTABLE, GLOBALS, LISTS, SLAB, SPARE, STATIC, STRINGS, bit_at, chain_with, counters, ft4_body, install_stubs, link_chain,
                       noise, put_anim, put_bit, put_shorts, quick_anims, regions, seed, time_scale, word)
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_menu import Image
from bit_specs import bit

GROUPS = ("bit4",)

for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(STATIC)
    SEEDS[_group] = seed

GROUP_STUBS = {group: install_stubs for group in GROUPS}

SPARK_GLOBALS = 0x005244E0
RING = ARENA + 0x800          # the ring of points of a fake ribbon
BIT_ARRAY = ARENA + 0xC00     # its array of bit pointers


def b4(address, name, this=BITS):
    return bit(address, name, "bit4", this)


def scattered(rng, low=-0x200000, high=0x200000):
    return rng.choice([rng.randrange(low, high), 0, rng.getrandbits(32), rng.randrange(-0x2000, 0x2000)]) & 0xFFFFFFFF


def vector(img, rng, at, low=-0x200000, high=0x200000):
    for k in range(3):
        img.u32(at + 4 * k, scattered(rng, low, high))


def spark_settings(img, rng):
    img.u32(SPARK_GLOBALS, rng.choice([0x00010001, 0x00020002, 0x00030003, 0x000F000F, 0x00040004, 0x00000000, 0x00110011, rng.getrandbits(32)]))
    img.u8(SPARK_GLOBALS + 4, rng.choice([0, 0, 1, 2]))
    img.put(SPARK_GLOBALS + 8, bytes(rng.getrandbits(8) for _ in range(3)))
    img.put(SPARK_GLOBALS + 12, bytes(rng.getrandbits(8) for _ in range(3)))
    img.put(0x0055F6C4, noise(rng, 6))
    cone = rng.choice([[0, 0, 0], [0x100, 0x80, 0x40], [-0x100 & 0xFFFF, 0x7FFF, 1], [rng.getrandbits(16) for _ in range(3)], [0x400, 0, 0x400]])
    for k, v in enumerate(cone):
        img.u16(0x0055F718 + 2 * k, v)


# ---- the spark settings and the spark ----------------------------------------------------------------------------------------------------------------------------------------------------

@b4(0x00406070, "Bit_SetSparkTrajectory", None)
def gen_trajectory(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + SPARE, noise(rng, 8))
    return [env.slab + SPARE], img.pokes()


@b4(0x00406090, "Bit_SetSparkTrajectoryCone", None)
def gen_cone(env, n):
    return gen_trajectory(env, n)


@b4(0x004060B0, "Bit_CalculateSparkVelocity", None)
def gen_spark_velocity(env, n):
    img, rng = Image(), env.rng
    spark_settings(img, rng)
    img.put(env.slab + SPARE, noise(rng, 12))
    return [env.slab + SPARE, rng.choice([0, 1, 0x100, 0x1000, 0x4000, 0x10000, -0x1000 & 0xFFFFFFFF, rng.randrange(0, 0x8000)])], img.pokes()


@b4(0x00406160, "Bit_SetSparkSize", None)
def gen_spark_size(env, n):
    img, rng = Image(), env.rng
    return [rng.choice([0, 1, 2, 5, 0xF, 0x10, 0x11, 0xFF, 0xFFFFFFFF, rng.randrange(0, 0x14)])], img.pokes()


@b4(0x004061C0, "Bit_SetSparkRGB", None)
def gen_spark_rgb(env, n):
    img, rng = Image(), env.rng
    return [word(rng), word(rng), word(rng)], img.pokes()


@b4(0x004061E0, "Bit_SetSparkFadeRGB", None)
def gen_spark_fade_rgb(env, n):
    return gen_spark_rgb(env, n)


def class_group4(ctor, sdel, dtor, name, list_name, size, ctor_args):
    @b4(ctor, f"{name}_Construct")
    def gen_construct(env, n):
        img, rng = Image(), env.rng
        put_bit(img, env, rng, 0, size=size)
        counters(img, rng)
        spark_settings(img, rng)
        if list_name:
            img.u32(LISTS[list_name], link_chain(img, env, rng, 1, rng.choice([0, 0, 1, 2])) if rng.random() < 0.5 else 0)
        return ctor_args(env, img, rng), img.pokes()

    if sdel:
        @b4(sdel, f"{name}_ScalarDelete")
        def gen_sdelete(env, n):
            img, rng = Image(), env.rng
            chain_with(img, env, rng, list_name, size) if list_name else put_bit(img, env, rng, 0, size=size)
            counters(img, rng)
            return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()

    if dtor:
        @b4(dtor, f"{name}_Destruct")
        def gen_destruct(env, n):
            img, rng = Image(), env.rng
            chain_with(img, env, rng, list_name, size) if list_name else put_bit(img, env, rng, 0, size=size)
            counters(img, rng)
            return [], img.pokes()


def position_arg(img, env, rng, slot=0):
    at = env.slab + SPARE + 0x20 * slot
    vector(img, rng, at)
    return at


def spark_args(env, img, rng):
    return [position_arg(img, env, rng), rng.choice([0, 0x100, 0x1000, 0x4000, rng.randrange(0, 0x8000)]), word(rng), rng.choice([0, 1, 5, 30, 60, 100, 0xFFFF, rng.randrange(0, 200)])]


class_group4(0x00406200, 0x00406300, 0x00406320, "CSpark", "pixel", 0x60, spark_args)


@b4(0x00406330, "CSpark_Move")
def gen_spark_move(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x60)
    at = bit_at(env, 0)
    vector(img, rng, at + 0x10, -0x100000, 0x100000)
    vector(img, rng, at + 0x1C, -0x100000, 0x100000)
    vector(img, rng, at + 0x28, -0x10000, 0x10000)
    img.u32(at + 0x4C, rng.choice([0, 0, 0x80000, scattered(rng), 0x7FFFFFFF]))
    img.u16(at + 0xC, rng.choice([0, 1, 5, 0x7FFF, 0xFFFF, rng.getrandbits(16)]))
    img.u16(at + 0xE, rng.choice([0, 1, 2, 5, 6, 100, 0xFFFF, rng.getrandbits(16)]))
    img.u8(at + 0x3A, rng.choice([0, 0, 0, 1]))
    return [], img.pokes()


# ---- the animated flat bits ---------------------------------------------------------------------------------------------------------------------------------------------------------------

def simple_anim_args(by_index):
    def args(env, img, rng):
        quick_anims(img, env, rng)
        for k in range(4):
            put_anim(img, env, rng, k)
        animation = rng.choice([0, 1, 2, 3]) if by_index else env.slab + STRINGS
        return [position_arg(img, env, rng), animation, rng.choice([0, 100, 400, 0x7FFF, 0xFFFF, word(rng)]), rng.choice([0, 0, 1, 2]), rng.choice([0, 1, 2, word(rng)]),
                rng.choice([-1 & 0xFFFFFFFF, -2 & 0xFFFFFFFF, 0, 1, 3, 5, word(rng)])]
    return args


class_group4(0x004063E0, 0x00406490, 0x00406560, "CSimpleAnim", "flat", 0x70, simple_anim_args(False))


@b4(0x004064B0, "CSimpleAnim_ConstructIndexed")
def gen_simple_anim_indexed(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x70)
    counters(img, rng)
    img.u32(LISTS["flat"], link_chain(img, env, rng, 1, rng.choice([0, 1])) if rng.random() < 0.5 else 0)
    return simple_anim_args(True)(env, img, rng), img.pokes()


@b4(0x00406570, "CSimpleAnim_Move")
def gen_simple_anim_move(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, True)
    at = bit_at(env, 0)
    count = img.b[at + 0x4D]
    img.u16(at + 0x50, rng.choice([0x80, 0x100, 0x200, 0x400, 0x1000, rng.getrandbits(16)]))
    time_scale(img, rng)
    img.u32(at + 0x64, rng.choice([0, 0, 1, 2]))
    img.u32(at + 0x68, rng.choice([-2, -1, 0, 1, count - 1, count, 3, 100]) & 0xFFFFFFFF)
    img.u8(at + 0x3A, rng.choice([0, 0, 0, 1]))
    return [], img.pokes()


def motion_blur_args(env, img, rng):
    for k in range(4):
        put_anim(img, env, rng, k)
    return [position_arg(img, env, rng, 0), position_arg(img, env, rng, 1), env.slab + STRINGS, rng.choice([0, 100, 400, 0x7FFF, 0xFFFF]), rng.choice([0, 1, 8, 50, 0x7FFF]),
            rng.choice([0, 1, 8, 50, 0x7FFF])]


class_group4(0x004065C0, 0x00406660, 0x00406680, "CMotionBlur", "flat", 0x70, motion_blur_args)


def motion_blur_state(env, rng):
    img = Image()
    ft4_body(img, env, rng, True)
    at = bit_at(env, 0)
    vector(img, rng, at + 0x10, -0x100000, 0x100000)
    vector(img, rng, at + 0x1C, -0x10000, 0x10000)
    img.u16(at + 0x52, rng.choice([0, 1, 5, 400, 0x7FFF, 0x8000, 0xFFFF]))
    img.u16(at + 0x3E, rng.choice([0, 1, 5, 100, 0x7FFF]))
    img.u32(at + 0x40, rng.choice([0, 0x2C808080, 0x01000000, 0x00000010, rng.getrandbits(32)]))
    img.u16(at + 0x3C, rng.choice([0, 1, 2, 8, 0x80, 0xFF, rng.getrandbits(16)]))
    img.u16(at + 0x50, rng.choice([0x80, 0x100, 0x200, rng.getrandbits(16)]))
    time_scale(img, rng)
    img.u8(at + 0x3A, rng.choice([0, 0, 0, 1]))
    return img


@b4(0x00406690, "CMotionBlur_Move")
def gen_motion_blur_move(env, n):
    return [], motion_blur_state(env, env.rng).pokes()


@b4(0x00406740, "CMotionBlurImmortal_Move")
def gen_immortal_move(env, n):
    return [], motion_blur_state(env, env.rng).pokes()


@b4(0x00406710, "CMotionBlurImmortal_ScalarDelete")
def gen_immortal_sdelete(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "flat", 0x70)
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b4(0x00406730, "CMotionBlurImmortal_Destruct")
def gen_immortal_destruct(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "flat", 0x70)
    counters(img, rng)
    return [], img.pokes()


def frag_args(env, img, rng):
    quick_anims(img, env, rng)
    return [position_arg(img, env, rng), word(rng), word(rng), word(rng), rng.choice([0, 1, 2, 3]), rng.choice([0, 100, 400, 0x7FFF, 0xFFFF]), rng.choice([0, 0, 1, 2]),
            rng.choice([0, 1, 2, 5, 10, 50]), word(rng), rng.choice([0, 1, 5, 30, 100, rng.randrange(0, 200)])]


class_group4(0x00406780, 0x00406870, 0x00406890, "CFrag", "flat", 0x70, frag_args)
class_group4(0x00406940, 0x00406960, 0x00406980, "CRibbonBit", "linked", 0x70, lambda env, img, rng: [])


# ---- the ribbon, the smoke trail and the glass ---------------------------------------------------------------------------------------------------------------------------------------

def ribbon_args(env, img, rng):
    for k in range(4):
        put_anim(img, env, rng, k)
    return [position_arg(img, env, rng), rng.choice([1, 2, 3, 4, 5]), rng.choice([1, 2, 3, 4]), env.slab + STRINGS, env.slab + STRINGS + 0x20, rng.choice([100, 400, 0x7FFF, word(rng)]),
            rng.choice([0, 1, 2])]


@b4(0x004069A0, "CRibbon_Construct")
def gen_ribbon_construct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x60)
    counters(img, rng)
    img.u32(LISTS["nonrendered"], link_chain(img, env, rng, 1, rng.choice([0, 1, 2])) if rng.random() < 0.5 else 0)
    return ribbon_args(env, img, rng), img.pokes()


def fake_ribbon(img, env, rng, bits=None, points=None, released=None):
    """A ribbon in bit slot 0: `bits` bits, `points` points each (the ring holds (points - 1) * bits + 1), the bits in slots 1..bits."""
    bits = rng.choice([1, 2, 3, 4]) if bits is None else bits
    points = rng.choice([2, 2, 3, 4]) if points is None else points
    total = (points - 1) * bits + 1
    at = put_bit(img, env, rng, 0, size=0x60)
    img.u32(at + 4, 0)
    img.u32(at + 8, 0)
    img.u32(LISTS["nonrendered"], rng.choice([0, at]))
    ring = env.slab + RING
    array = env.slab + BIT_ARRAY
    img.put(ring, noise(rng, total * 12))
    img.u32(at + 0x3C, bits)
    img.u32(at + 0x40, points - 1)
    img.u32(at + 0x44, total)
    img.u32(at + 0x48, rng.randrange(0, total))
    img.u32(at + 0x4C, ring)
    img.u32(at + 0x50, array)
    img.u32(at + 0x54, rng.choice([0, 1]) if released is None else released)
    for k in range(bits):
        piece = put_bit(img, env, rng, 1 + k, size=0x70)
        img.u32(array + 4 * k, piece)
        img.u8(piece + 0x3A, 1)
    return at, bits, total


@b4(0x00406B10, "CRibbon_ScalarDelete")
def gen_ribbon_sdelete(env, n):
    img, rng = Image(), env.rng
    fake_ribbon(img, env, rng)
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b4(0x00406B30, "CRibbon_Destruct")
def gen_ribbon_destruct(env, n):
    img, rng = Image(), env.rng
    at, bits, _ = fake_ribbon(img, env, rng)
    for k in range(bits):
        if rng.random() < 0.2:
            img.u32(env.slab + BIT_ARRAY + 4 * k, 0)
    counters(img, rng)
    return [], img.pokes()


@b4(0x00406BF0, "CRibbon_SetTint")
def gen_ribbon_tint(env, n):
    img, rng = Image(), env.rng
    fake_ribbon(img, env, rng)
    return [word(rng), word(rng), word(rng)], img.pokes()


@b4(0x00406C30, "CRibbon_SetScale")
def gen_ribbon_scale(env, n):
    img, rng = Image(), env.rng
    fake_ribbon(img, env, rng)
    return [rng.choice([0, 100, 400, 0x7FFF, 0xFFFF, word(rng)])], img.pokes()


@b4(0x00406C60, "CRibbon_SetPos")
def gen_ribbon_pos(env, n):
    img, rng = Image(), env.rng
    at, bits, total = fake_ribbon(img, env, rng)
    img.u32(at + 0x48, rng.choice([0, total - 1, rng.randrange(0, total)]))
    return [position_arg(img, env, rng)], img.pokes()


def smoke_trail_args(env, img, rng):
    for k in range(4):
        put_anim(img, env, rng, k)
    return [position_arg(img, env, rng), rng.choice([1, 2, 3, 4, 5]), word(rng), word(rng), word(rng)]


@b4(0x00406D00, "CSmokeTrail_Construct")
def gen_smoke_trail_construct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x60)
    counters(img, rng)
    img.u32(LISTS["nonrendered"], link_chain(img, env, rng, 1, rng.choice([0, 1, 2])) if rng.random() < 0.5 else 0)
    return smoke_trail_args(env, img, rng), img.pokes()


@b4(0x00406DB0, "CSmokeTrail_ScalarDelete")
def gen_smoke_trail_sdelete(env, n):
    return gen_ribbon_sdelete(env, n)


@b4(0x00406DD0, "CSmokeTrail_Destruct")
def gen_smoke_trail_destruct(env, n):
    return gen_ribbon_destruct(env, n)


@b4(0x00406DE0, "CSmokeTrail_Move")
def gen_smoke_trail_move(env, n):
    img, rng = Image(), env.rng
    at, bits, _ = fake_ribbon(img, env, rng, released=rng.choice([0, 1, 1, 1]))
    array = env.slab + BIT_ARRAY
    for k in range(bits):
        piece = img.b[array + 4 * k] | (img.b[array + 4 * k + 1] << 8) | (img.b[array + 4 * k + 2] << 16) | (img.b[array + 4 * k + 3] << 24)
        img.u32(piece + 0x40, rng.choice([0, 0, 0x2C808080, 0x01000000, 0x00000005, rng.getrandbits(32)]))
        img.u16(piece + 0x3C, rng.choice([0, 1, 8, 0x80, rng.getrandbits(16)]))
    img.u8(at + 0x3A, rng.choice([0, 0, 0, 1]))
    return [], img.pokes()


def glass_args(env, img, rng):
    return [position_arg(img, env, rng, 0), position_arg(img, env, rng, 1), word(rng), word(rng), word(rng), word(rng), rng.choice([0, 1, 2, 5, 10]), rng.choice([0, 1, 2, 5, 10]),
            rng.choice([0, 1, 2, 5, 10])]


class_group4(0x00406E30, 0x00407010, 0x00407030, "CGlassBit", "glass", 0x70, glass_args)


@b4(0x00407050, "CGlassBit_Move")
def gen_glass_move(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x70)
    at = bit_at(env, 0)
    vector(img, rng, at + 0x1C, -0x80000, 0x80000)
    for k in range(9):
        img.u32(at + 0x3C + 4 * k, scattered(rng, -0x400000, 0x400000))
    img.u32(at + 0x60, rng.choice([scattered(rng, -0x400000, 0x400000), 0, 0x100000, -0x100000 & 0xFFFFFFFF]))
    img.u16(at + 0xC, rng.choice([0, 1, 2, 3, 0x7FFF, 0xFFFF]))
    img.u16(at + 0xE, rng.choice([0, 0, 1, 2, 30, 0xFFFF]))
    img.put(at + 0x64, bytes(rng.choice([0, 0, 1, 3, 4, 5, 0x20, 0xFF, rng.getrandbits(8)]) for _ in range(6)))
    img.u8(at + 0x6A, rng.choice([0, 1, 2, 4, 4, 4, 5, 0x10, 0xFF]))
    img.u8(at + 0x3A, rng.choice([0, 0, 0, 1]))
    return [], img.pokes()
