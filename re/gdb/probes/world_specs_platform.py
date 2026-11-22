"""Phase 5 / 10g s14 probe: the platform's per-frame methods (groups wcd, wcu, wcb; 0x004a03e0 CPlatform__DoPhysics, 0x0049fef0 CPlatform__Update, 0x0049f780 CPlatform__BouncyAI).

The platform (vtable 0x005194f8) is the fake object at the slab's start (0x230 bytes of random fields); its callees that are not pure vector arithmetic are services: DoPhysics (inside Update), BouncyAI, the script stream
runner, the positional sound update.

Slab layout (offsets): platform 0x000 (0x230 bytes), camera/player records above it.
"""

from __future__ import annotations

import math

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import OBJ, fill_object, noise, word
from world_specs import install_stubs as install_base_stubs

GROUPS = ("wcd", "wcu", "wcb", "wcs", "wce", "wcv", "wcf", "wcc", "wcr", "wcq", "wcp")

SLAB = 0x500
PLATFORM_VTABLE = 0x005194F8
CLOCK = 0x0056E320
GRAVITY = 0x0056B770
TICK = 0x0056A8D4
GLOBALS = [(CLOCK, 4), (GRAVITY, 12), (TICK, 4), (0x006A3E00, 0xD0), (0x0056D43C, 0xC8)]


def regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = [(PLATFORM_VTABLE, 0x30)]


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_wcd_installed", False):
        return
    fc._wcd_installed = True
    install_base_stubs(fc, env)
    mark_under_test()
    ensure(fc, 0x0049F780, 0, "engine_0049f780", convention="thiscall", this=True)                      # CPlatform__BouncyAI
    ensure(fc, 0x004A03E0, 0, "engine_004a03e0", convention="thiscall", this=True)                      # CPlatform__DoPhysics (its own group: wcd)
    ensure(fc, 0x00401520, 1, "engine_00401520", convention="thiscall", this=True)                      # TRG_RunScriptStream
    ensure(fc, 0x004AD8B0, 3, "engine_004ad8b0")                                                        # Audio_UpdatePositionalSound
    scratch = 0x7FF00800 + 0xC00 + 0x340                                                                # (worldkit::kScratch + 0x340: the collision query record the port keeps where the retail code has a local)
    rng = env.rng
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=scratch)                                      # M3D_InitCollisionQuery
    normal = lambda a: rng.choice([0, 0x1000, 0xF000, 0xFFFFF000 & 0xFFFF, rng.getrandbits(16), rng.randrange(0, 0x1000), 0xF000 + rng.randrange(0, 0x1000)])
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=scratch, blob=(0, 0x18),                      # M3D_QueryZoneCollision: the body hit, the hit point and the normal (as words)
           pokes=[(0, 4, lambda a: rng.choice([0, 0, 1, 0x1234]), 0x68), (0, 4, lambda a: near(rng), 0x6C), (0, 4, lambda a: near(rng), 0x70), (0, 4, lambda a: near(rng), 0x74),
                  (0, 2, normal, 0x78), (0, 2, normal, 0x7A), (0, 2, normal, 0x7C)])
    ensure(fc, 0x004013B0, 0, "engine_004013b0", convention="thiscall", this=True)                      # the platform's die method (vtable slot 2)
    ensure(fc, 0x004AD620, 3, "engine_004ad620", result=lambda a: rng.choice([0xFFFFFFFF, 0, 5, rng.getrandbits(32)]))   # SFX_PlayX (a voice handle)
    ensure(fc, 0x004A07E0, 1, "engine_004a07e0", convention="thiscall", this=True, masks={0: 0xFFFF}, result=lambda a: rng.choice([0, 1, 1, 1]))     # the platform's command handler (vtable slot 7; the command is a 16-bit word, its upper half the caller's ebx)
    ensure(fc, 0x004A1060, 1, "engine_004a1060", convention="thiscall", this=True, masks={0: 0xFFFF})  # the platform's field handler (vtable slot 8)
    ensure(fc, 0x004A1270, 1, "engine_004a1270", convention="thiscall", this=True, masks={0: 0xFFFF}, result=lambda a: rng.getrandbits(16) | rng.choice([0, 0, 0x10000]))   # the platform's variable handler (vtable slot 9)
    ensure(fc, 0x004015E0, 1, "engine_004015e0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1, 1]))           # the base class's script commands
    ensure(fc, 0x004AD9F0, 1, "engine_004ad9f0")                                                        # Audio_StopVoice
    ensure(fc, 0x004B1DE0, 2, "engine_004b1de0", result=lambda a: rng.choice([0, 1, 2, 3]) | rng.choice([0, 0, 0x10000]))   # Spool_FindModelIndex(checksum, region)
    ensure(fc, 0x004027F0, 1, "engine_004027f0", convention="thiscall", this=True)                      # the base class's script fields
    ensure(fc, 0x004AD7D0, 3, "engine_004ad7d0", result=lambda a: rng.choice([0xFFFFFFFF, 0, 5, rng.getrandbits(32)]))   # the positional sound start
    ensure(fc, 0x004AF0B0, 3, "engine_004af0b0")                                                        # Shatter_Item
    ensure(fc, 0x0046E1D0, 1, "engine_0046e1d0")                                                        # the skater's grunt (CPlatform__Bounce)
    ensure(fc, 0x0048FFC0, 3, "engine_0048ffc0", convention="thiscall", this=True)                      # CBruce__Vibrate (on the skater)
    vector =0x7FF00800 + 0xC00 + 0x240                                                                 # (worldkit::kScratch + 0x240)
    ensure(fc, 0x004C8650, 2, "engine_004c8650", stack_to=vector, pokes=[(0, 4, lambda a: near(rng), 0), (0, 4, lambda a: near(rng), 4), (0, 4, lambda a: near(rng), 8)])   # TRG_DecodeNodePosition


BOUNCE_GROUPS = ("wcr", "wcq", "wcp")
for _group in BOUNCE_GROUPS:     # the assertion's line and file words (0x00568638, 0x0056881c) are compared too
    EXTRA_REGIONS[_group] = lambda slab: merged([(slab, SLAB)] + GLOBALS + [(0x00568638, 4), (0x0056881C, 0x20)], gap=0)

GROUP_STUBS = {group: install_stubs for group in GROUPS}


def near(rng):
    """A vector component near the dead zone of +-0x800."""
    return rng.choice([0, 1, 0xFFFFFFFF, 0x7FF, 0x800, 0x801, 0xFFFFF801, 0xFFFFF800, 0xFFFFF7FF, 2, 0xFFFFFFFE, 0x400, 0xFFFFFC00, 0x1000, 0xFFFFF000,
                       rng.randrange(-0x2000, 0x2000) & 0xFFFFFFFF, rng.getrandbits(32)])


def small16(rng):
    return rng.choice([0, 1, 0xFFFF, 2, 0xFFFE, 0x7FFF, 0x8000, 0xFFF, 0x1000, 3, 0xFFFD, rng.getrandbits(16)])


def platform(img, env, rng):
    fill_object(img, env, rng, 0x230)
    img.u32(env.slab + OBJ, PLATFORM_VTABLE)
    return env.slab + OBJ


@spec(0x004A03E0, "CPlatform__DoPhysics", "wcd", this=lambda env: env.slab + OBJ)
def gen_do_physics(env, n):
    img, rng = Image(), env.rng
    o = platform(img, env, rng)
    img.u16(o + 0x1A4, rng.choice([0, 0, 0, 0, 0, 1, 1, 2, 2, 3, rng.getrandbits(16)]))
    for k in range(3):
        img.u32(o + 0x58 + 4 * k, near(rng))
        img.u32(o + 0x4C + 4 * k, near(rng))
        img.u16(o + 0x70 + 2 * k, small16(rng))
        img.u16(o + 0x76 + 2 * k, small16(rng))
        img.u8(o + 0x6C + k, rng.choice([0, 1, 2, 4, 8, 15, 16, 31, 32, 33, rng.getrandbits(8)]))
        img.u8(o + 0x7C + k, rng.choice([0, 1, 2, 4, 8, 15, 16, 31, 32, 33, rng.getrandbits(8)]))
    if rng.random() < 0.45:      # a recorded path: the step runs once per two ticks elapsed since the start
        index = rng.choice([0, 1, 2, 5])
        start = rng.choice([0, 1, 100, 0xFFFFFFF0, rng.getrandbits(32)])
        img.u32(o + 0x1C0, rng.choice([index + 1, index + 2, index + 3, index + 6, 0, index, rng.randrange(0, 12)]))
        img.u32(o + 0x1DC, index)
        img.u32(o + 0x1E0, start)
        img.u32(CLOCK, (start + 2 * index + rng.choice([-2, -1, 0, 1, 2, 3, 4, 7, 12])) & 0xFFFFFFFF)
    else:
        img.u32(o + 0x1C0, 0)
        img.u32(CLOCK, rng.getrandbits(32))
    return [], img.pokes()


def target_geometry(img, o, rng):
    """The position and the move-to target a whole number of units apart; the arrival radius around their distance."""
    scale = rng.choice([1, 3, 20, 100, 1000, 2500, 20000])
    delta = [rng.randrange(-scale, scale + 1) for _ in range(3)]
    position = [rng.getrandbits(32) for _ in range(3)]
    for k in range(3):
        img.u32(o + 0x8 + 4 * k, position[k])
        img.u32(o + 0x1C4 + 4 * k, (position[k] + (delta[k] << 12) + rng.choice([0, 0, 0, 1, 0xFFF, 0xFFFFF000])) & 0xFFFFFFFF)
    root = int(math.sqrt(sum(d * d for d in delta)))
    img.u16(o + 0x1A0, rng.choice([root, root, max(root - 1, 0), root + 1, root + 2, 0, 1, rng.getrandbits(16)]))


@spec(0x0049FEF0, "CPlatform__Update", "wcu", this=lambda env: env.slab + OBJ)
def gen_update(env, n):
    return update_case(env, False)


@spec(0x0049FEF0, "CPlatform__Update", "wcv", this=lambda env: env.slab + OBJ)
def gen_update_roll(env, n):
    """The roll levelling and the bank: kind 1 with the angles at the edges of the wrap (the second fixture, aimed at the first one's surviving mutants)."""
    return update_case(env, True)


def update_case(env, aimed):
    img, rng = Image(), env.rng
    o = platform(img, env, rng)
    for off in (0x1E4, 0x1E6, 0x1E8):
        img.u16(o + off, rng.choice([0, 0, 1, 2, 7, rng.getrandbits(16)]))
    img.u16(o + 0x68, rng.choice([0, 1, 2, 3, 0x100, rng.getrandbits(16)]))
    flags = rng.choice([0, 1, 1, 1, 0x20, 0x21, 0x21, 0x10, 0x11, 0x30, 0x4, 0x5, 0x25, 0xFFFFFFFF, rng.getrandbits(32)])
    img.u32(o + 0x178, flags)
    img.u8(o + 0x16C, rng.choice([0, 0, 1, 2, 5, rng.getrandbits(8)]))
    img.u8(o + 0x16E, rng.choice([0, 0, 1, 2, rng.getrandbits(8)]))
    img.u32(o + 0x17C, rng.getrandbits(32))
    ring = rng.choice([0, 0, 0, 3, 5, 0xFFFFFFFF])
    img.u32(o + 0x1C0, ring)
    img.u32(o + 0x1DC, rng.choice([0, 1, 2, 3, 4, 5, 6, 0xFFFFFFFF]))
    img.u16(o + 0x1A4, rng.choice([0, 0, 1, 1, 1, 2, 2, 2, 3, rng.getrandbits(16)]))
    target_geometry(img, o, rng)
    if aimed:
        img.u16(o + 0x1A4, rng.choice([1, 1, 1, 2]))
        img.u32(o + 0x178, rng.choice([0, 0x20, 0x21, 0x1, 0x20, 0, 0x4]))
        img.u8(o + 0x16C, 0)
        img.u16(o + 0x18, rng.choice([0, 0x800, 0xF800, 0x7FF, 0x801, 0xF801, 0xF7FF, 0x1000, 0xFFFF, 1, rng.getrandbits(16)]))
        img.u16(o + 0x74, rng.choice([0, 1, 5, 0xFFFF, rng.getrandbits(16)]))
        img.u16(o + 0x16, rng.choice([0, 0x400, 0x800, 0xC00, rng.getrandbits(16)]))
    for k in range(3):
        img.u32(GRAVITY + 4 * k, near(rng))
    img.u32(o + 0x204, rng.getrandbits(32))
    img.u32(o + 0x208, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 1, 2, 3, 5, 0x10, 0x7FFFFFFF, rng.randrange(1, 0x100)]))
    img.u32(TICK, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 10, 12, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32)]))
    img.put(0x006A3E00, noise(rng, 0xD0))
    return [], img.pokes()


@spec(0x0049F780, "CPlatform__BouncyAI", "wcb", this=lambda env: env.slab + OBJ)
def gen_bouncy_ai(env, n):
    return bouncy_case(env, False)


@spec(0x0049F780, "CPlatform__BouncyAI", "wcc", this=lambda env: env.slab + OBJ)
def gen_bouncy_ai_first(env, n):
    """The first bounce of a spinning kind 2 or 4 platform at speed (the second fixture, aimed at the first one's surviving mutants: the re-rolled target angles and their special cases)."""
    return bouncy_case(env, True)


def bouncy_case(env, aimed):
    img, rng = Image(), env.rng
    o = platform(img, env, rng)
    img.u16(o + 0x1FA, rng.choice([0, 0, 0, 0, 1, 2, 5, rng.getrandbits(16)]))
    img.u32(o + 0x178, rng.choice([0x20, 0x20, 0x20, 0x30, 0x21, 0x10, 0, 0xFFFFFFFF, rng.getrandbits(32) | 0x20]))
    angles = [rng.getrandbits(16) for _ in range(3)]
    for k in range(3):
        img.u16(o + 0x14 + 2 * k, angles[k])
        delta = rng.choice([0, 0x27, 0x28, 0x29, 0xFFD8, 0xFFD9, 0xFFD7, 0x100, 0xF00, 0x800, rng.getrandbits(16)])
        img.u16(o + 0x1FC + 2 * k, (angles[k] + delta) & 0xFFFF if rng.random() < 0.8 else rng.choice([0, 0x800, 0x7FF, 0x801, 0x1800, rng.getrandbits(16)]))
        img.u16(o + 0x70 + 2 * k, rng.choice([0, 1, 0xFFFF, 0x27, 0x28, 0xFFD8, 0xFFD9, rng.getrandbits(16)]))
    for off in (0x1F4, 0x1F6, 0x1F8):
        img.u16(o + off, rng.choice([0, 1, 0x100, 0x800, 0x1000, rng.getrandbits(16)]))
    magnitude = rng.choice([0x400, 0x4000, 0x8000, 0x20000, 0x100000])
    for k in range(3):
        img.u32(o + 0x4C + 4 * k, rng.choice([0, near(rng), rng.randrange(-magnitude, magnitude + 1) & 0xFFFFFFFF, rng.randrange(-magnitude, magnitude + 1) & 0xFFFFFFFF]))
        img.u32(o + 0x8 + 4 * k, rng.getrandbits(32))
    img.u16(o + 0x1F0, rng.choice([0, 0, 0, 1, 2, 3, 4, 5, 6, 0x10, 0x11, 0x12, 0x13, rng.getrandbits(16)]))
    img.u16(o + 0x1F2, rng.choice([0, 1, 2, 3, 4, 5, 6, rng.getrandbits(16)]))
    if aimed:
        img.u16(o + 0x1FA, 0)
        img.u32(o + 0x178, rng.choice([0x20, 0x20, 0x21]))
        img.u16(o + 0x1F0, rng.choice([0, 0, 0, 1]))
        img.u16(o + 0x1F2, rng.choice([2, 4, 4, 2, 1, 3]))
        img.u16(o + 0x1FC, rng.choice([0x400, 0xC00, 0x800, 0, 0x1000, 0x1400, 0x1C00, 0xF400, rng.getrandbits(16)]))
        img.u16(o + 0x200, rng.choice([0x400, 0xC00, 0x800, 0, 0x1000, 0x1400, 0x1C00, 0xF400, rng.getrandbits(16)]))
        for k in range(3):
            img.u16(o + 0x14 + 2 * k, rng.choice([0, 0x100, 0x500, 0xFB00, 0x51F, 0x520, 0xFAE0, rng.getrandbits(16)]))
            img.u32(o + 0x4C + 4 * k, rng.choice([0, 0x200000, 0xFFE00000, 0x40000, near(rng), rng.randrange(-0x100000, 0x100000) & 0xFFFFFFFF]))
        img.u32(o + 0x50, rng.choice([0x100000, 0x180000, 0x200000, 0x300000, 0xFFF00000]))
        if rng.random() < 0.4:
            img.u32(o + 0x4C, rng.choice([0, 0x100, 0xFFFFFF00, 0x800]))
            img.u32(o + 0x54, rng.choice([0, 0x100, 0xFFFFFF00, 0x800]))
    img.put(0x006A3E00, noise(rng, 0xD0))
    return [], img.pokes()


BOUNCE_SKATER = 0x300       # the skater record the bounce is handed (its velocity at +0x4c; the callees that take the skater are services)


@spec(0x0049F4C0, "CPlatform__Bounce", "wcr", this=lambda env: env.slab + OBJ)
def gen_bounce(env, n):
    return bounce_case(env, False)


@spec(0x0049F4C0, "CPlatform__Bounce", "wcq", this=lambda env: env.slab + OBJ)
def gen_bounce_aimed(env, n):
    """The second fixture, aimed at the first one's surviving mutants: the launch speed at the two thresholds (0x12, 0x28), the target angles at the +-0x800 special cases of kinds 2, 4 and 5."""
    return bounce_case(env, True)


@spec(0x0049F4C0, "CPlatform__Bounce", "wcp", this=lambda env: env.slab + OBJ)
def gen_bounce_speed(env, n):
    """The third fixture: kinds 1 and 3 (and the unknown kinds) with the launch speed exactly at 0x12 and 0x28 and one either side, along an axis."""
    return bounce_case(env, True, speed=True)


def bounce_case(env, aimed, speed=False):
    img, rng = Image(), env.rng
    o = platform(img, env, rng)
    skater = env.slab + BOUNCE_SKATER
    img.put(skater, noise(rng, 0x80))
    img.u32(o + 0x178, rng.choice([0x10, 0x10, 0x10, 0x10, 0x11, 0x30, 0x20, 0, 0xFFFFFFFF, rng.getrandbits(32) | 0x10, rng.getrandbits(32)]) if not aimed else rng.choice([0x10, 0x10, 0x11, 0x10]))
    img.u16(o + 0x1F2, rng.choice([1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 0, 6, 0xFFFF, rng.getrandbits(16)]) if not aimed else rng.choice([2, 4, 5, 2, 4, 5, 1, 3]))
    img.u16(o + 0x1F0, rng.getrandbits(16))
    img.u16(o + 0x4, rng.choice([0, 0x20, 0xFFDF, 0xFFFF, rng.getrandbits(16)]))
    angle = lambda: rng.choice([0, 0x400, 0x800, 0xC00, 0xF800, 0xF400, 0xFC00, 0x1000, 0x1400, 0x200, 0xE00, rng.getrandbits(16)])
    for k in range(3):
        img.u16(o + 0x14 + 2 * k, angle())
    if aimed:
        for k in (0, 2):
            img.u16(o + 0x14 + 2 * k, rng.choice([0x400, 0xC00, 0xFC00, 0xF400, 0x1400, 0, 0x800, 0xF800]))
    if speed:
        img.u16(o + 0x1F2, rng.choice([1, 3, 1, 3, 1, 3, 0, 6]))
        length = rng.choice([0x11, 0x12, 0x13, 0x27, 0x28, 0x29, 0x12, 0x28])
        x, z = rng.choice([(length, 0), (0, length), (-length, 0), (0, -length)])
        x, z = (x << 12) + rng.randrange(0x1000), (z << 12) + rng.randrange(0x1000)     # (the squared length takes the components >> 12)
    # the launch speed: the horizontal length of the velocity (the y component is not read)
    elif rng.random() < 0.7:
        length = rng.choice([0, 1, 0x11, 0x12, 0x13, 0x27, 0x28, 0x29, 0x50, 0x1000, rng.randrange(0, 0x60)])
        x = rng.randrange(-length, length + 1)
        z = int(round((length * length - x * x) ** 0.5)) * rng.choice([1, -1])
        x, z = (x, z) if rng.random() < 0.5 else (z, x)
        x += rng.choice([0, 0, 0, 1, -1])
        if rng.random() < 0.8:      # (the squared length takes the components >> 12: whole units in the upper bits)
            x, z = (x << 12) + rng.randrange(0x1000), (z << 12) + rng.randrange(0x1000)
    else:
        x, z = near(rng), near(rng)
        x, z = (x - (1 << 32) if x >= 1 << 31 else x), (z - (1 << 32) if z >= 1 << 31 else z)
    img.u32(skater + 0x4C, x & 0xFFFFFFFF)
    img.u32(skater + 0x50, rng.getrandbits(32))
    img.u32(skater + 0x54, z & 0xFFFFFFFF)
    for k in range(3):
        img.u32(o + 0x4C + 4 * k, rng.getrandbits(32))
    return [skater, skater + 0x4C], img.pokes()


@spec(0x00401520, "TRG_RunScriptStream", "wcs", this=lambda env: env.slab + OBJ)
def gen_run_script_stream(env, n):
    img, rng = Image(), env.rng
    o = platform(img, env, rng)
    stream = env.slab + 0x300
    words = []
    for _ in range(rng.choice([0, 0, 1, 2, 3, 5, 8])):
        words.append(rng.choice([0x4200, 0x4309, 0x4A00, 0x2123, 0x2A00, 0x6000, 0x6123, 0x0000, 0x1234, 0x0100, 0x8000, 0xA123]) | rng.choice([0, 1, 5, 0x7F]))
        if (words[-1] & 0xFF00) == 0x4100:
            words[-1] ^= 0x0100
    words.append(0x4100)
    words.extend(rng.getrandbits(16) for _ in range(4))
    for k, w in enumerate(words):
        img.u16(stream + 2 * k, w)
    img.u8(o + 0x16C, rng.choice([0, 1, 2, 5, rng.getrandbits(8)]))
    img.u32(o + 0x17C, rng.getrandbits(32))
    return [stream], img.pokes()


@spec(0x004A07E0, "CPlatform__ExecuteScriptCommand", "wce", this=lambda env: env.slab + OBJ)
def gen_script_command(env, n):
    img, rng = Image(), env.rng
    o = platform(img, env, rng)
    stream = env.slab + 0x300
    img.put(stream, noise(rng, 0x40))
    command = rng.choice([0x4205, 0x4220, 0x4221, 0x4222, 0x4300, 0x4301, 0x4302, 0x4303, 0x4304, 0x4304, 0x4305, 0x4306, 0x4307, 0x4308, 0x4309, 0x4507, 0x4508, 0x4509,
                          0x4206, 0x4100, 0x4310, 0x4506, 0x450A, 0x0000, 0xFFFF, rng.getrandbits(16)])
    if rng.random() < 0.15:
        command |= rng.getrandbits(16) << 16     # (the high half of the argument is the caller's)
    cursor = stream + rng.choice([0, 0, 1, 2, 3, 4, 6])
    img.u32(o + 0x17C, cursor)
    for k in range(4):
        img.u16(cursor + 2 * k, rng.choice([0, 1, 2, 5, 0x10, 0x64, 0xFFFF, 0x8000, 0x7FFF, 0x2000, 0x2001, 0x21FF, 0x1234, rng.getrandbits(16)]))
    if command & 0xFFFF in (0x4220, 0x4222) and rng.random() < 0.7:
        # the stream's three dwords (aligned) become the move-to target; the target already held is the same but for one component (or all of them)
        at = (cursor + 3) & ~3
        pending = [rng.getrandbits(32) for _ in range(3)]
        for k in range(3):
            img.u32(at + 4 * k, pending[k] & 0xFFFFF)
        if command & 0xFFFF == 0x4220:
            held = [(w & 0xFFFFF) << 12 & 0xFFFFFFFF for w in pending]
            held[rng.randrange(3)] ^= rng.choice([0, 0, 1, 0x1000])
            for k in range(3):
                img.u32(o + 0x1C4 + 4 * k, held[k])
    img.u32(o + 0x204, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 7, rng.getrandbits(32)]))
    img.u32(o + 0x178, rng.choice([0, 0x10, 0x20, 0x04, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u16(o + 0x4, rng.choice([0, 0x200, 0x8, 0xFFFF, rng.getrandbits(16)]))
    img.u8(o + 0x1F, rng.choice([0, 1, 2]))
    img.u16(o + 0x1A, rng.choice([0, 1, 2, 3]))
    for region in range(3):
        img.u32(0x0056D43C + region * 0x44, env.slab + 0x400 + region * 0x10)
        for index in range(4):
            img.u32(env.slab + 0x400 + region * 0x10 + 4 * index, env.slab + 0x440 + ((region * 4 + index) % 8) * 0x10)
    for model in range(8):
        img.put(env.slab + 0x440 + model * 0x10, noise(rng, 0x10))
    img.put(stream + 0x40, noise(rng, 0x10))
    return [command], img.pokes()


@spec(0x004A1060, "CPlatform__ApplyScriptField", "wcf", this=lambda env: env.slab + OBJ)
def gen_script_field(env, n):
    img, rng = Image(), env.rng
    o = platform(img, env, rng)
    stream = env.slab + 0x300
    img.put(stream, noise(rng, 0x40))
    field = rng.choice([0x2123, 0x2124, 0x2127, 0x2128, 0x212F, 0x2134, 0x2137, 0x2123, 0x2127, 0x2134, 0x2122, 0x2125, 0x2126, 0x2138, 0x2100, 0x2A00, 0xFFFF, rng.getrandbits(16)])
    if rng.random() < 0.15:
        field |= rng.getrandbits(16) << 16
    cursor = stream + rng.choice([0, 0, 1, 2, 3, 4, 6])
    img.u32(o + 0x17C, cursor)
    for k in range(8):
        img.u16(cursor + 2 * k, rng.choice([0, 1, 2, 5, 0x10, 0x64, 0xFFFF, 0x8000, 0x7FFF, 0x2000, 0x2001, 0x21FF, 0xA000, 0x1234, rng.getrandbits(16)]))
    img.u8(o + 0x1F, rng.choice([0, 1, 2]))
    img.u16(o + 0x1A, rng.choice([0, 1, 2, 3]))
    img.u16(o + 0x4, rng.choice([0, 0x20, 0x1, 0xFFFF, rng.getrandbits(16)]))
    if field & 0xFFFF == 0x2124:
        img.u16(cursor, rng.randrange(4))
    for region in range(3):
        img.u32(0x0056D43C + region * 0x44, env.slab + 0x400 + region * 0x10)
        for index in range(4):
            img.u32(env.slab + 0x400 + region * 0x10 + 4 * index, env.slab + 0x440 + ((region * 4 + index) % 8) * 0x10)
    for model in range(8):
        img.put(env.slab + 0x440 + model * 0x10, noise(rng, 0x10))
    return [field], img.pokes()
