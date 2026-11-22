"""Phase 4.15c probe: the traffic objects' collision with the skaters (group wtc): the car's largest box and the skater's swept step against it (0x00412ef0, 0x00413000).

A car (0x180 bytes), a skater (0x3600 bytes, the same fake as world_specs_skater), the box of a model (twelve bytes) and the tables of the region sit in the slab; the GTE registers are
captured (the matrix times vector products run as the game's own code).

Slab layout (offsets): skater 0x0000, car 0x3700, model A 0x3900, model B 0x3980, model table 0x3a00, matrix 0x3a40, arena 0x3b00.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import SPRITE_B, word
from world_specs import install_stubs as install_base_stubs

GROUPS = ("wtc",)

SKATER = 0x0000
SKATER_BYTES = 0x3600
CAR = 0x3700
MODEL_A = 0x3900
MODEL_B = 0x3980
MODEL_TABLE = 0x3A00
MATRIX = 0x3A40
ARENA = 0x3B00
SLAB = 0x3C00
SCR = 0x7FF00800 + 0xC00
LOCAL_MATRIX = SCR + 0x300
LOCAL_SPREAD = SCR + 0x330

GLOBALS = [(0x006A3E00, 0xD0), (0x006A3B20, 4), (0x006A3A88, 4), (0x0056D43C, 68 * 3 + 0x28), (0x0056A858, 8), (0x0052E5A0, 0x10), (0x0052E2E0, 8), (0x0055F72C, 4), (0x005244CC, 4),
           (0x0056A920, 0x60), (0x0056B7E8, 4)]
READ = [(0x0053A514, 0x170 * 24), (0x00518DD8, 0x20)]


def regions(slab):
    return merged([(slab + SKATER, SKATER_BYTES), (slab + CAR, 0x180), (slab + MODEL_A, 0x100), (slab + MODEL_TABLE, 0x20), (slab + MATRIX, 0x20), (slab + ARENA, 0x100)] + GLOBALS, gap=0)


STATE = {"matrix": bytes(18)}


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = 0x100
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = READ
    SEEDS[_group] = seed


def wtc(address, name):
    return spec(address, name, "wtc")


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_wtc_installed", False):
        return
    fc._wtc_installed = True
    install_base_stubs(fc, env)
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x004CDE70, 0, "engine_004cde70", result=lambda a: rng.choice([0, 0, 0, 1]))                              # Video_Playing
    ensure(fc, 0x004F53E0, 2, "engine_004f53e0", stack_to={1: LOCAL_MATRIX}, pokes=[(1, None, lambda a: STATE["matrix"], 0)])   # Render_TransposeMatrixQ12
    ensure(fc, 0x0048FFC0, 3, "engine_0048ffc0", convention="thiscall", this=True)                                       # CBruce__Vibrate
    ensure(fc, 0x004AD620, 3, "engine_004ad620")                                                                         # SFX_PlayX
    ensure(fc, 0x0048F690, 4, "engine_0048f690", convention="thiscall", this=True)                                       # Gap_ApplyDefinitionState
    ensure(fc, 0x0049C8D0, 0, "engine_0049c8d0", convention="thiscall", this=True)                                       # Skater_UpdateOrientationAngles
    ensure(fc, 0x00406070, 1, "engine_00406070", blob=(0, 6))
    ensure(fc, 0x00406090, 1, "engine_00406090", stack_to={0: LOCAL_SPREAD}, blob=(0, 6))
    ensure(fc, 0x0040B060, 5, "engine_0040b060", convention="thiscall", this=True)                                       # CGlassBit__Move
    ensure(fc, 0x004904D0, 2, "engine_004904d0", convention="thiscall", this=True)                                       # CBruce__Bail


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def identity(rng):
    """The car's rotation (Q12): the identity most of the time, a quarter turn or a random one otherwise."""
    pick = rng.random()
    if pick < 0.6:
        rows = [0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000]
    elif pick < 0.8:
        rows = [0, 0, 0x1000, 0, 0x1000, 0, -0x1000, 0, 0]
    else:
        rows = [rng.randrange(-0x1000, 0x1001) for _ in range(9)]
    return struct.pack("<9h", *rows)


def car_world(img, env, rng):
    car = env.slab + CAR
    img.put(car, noise(rng, 0x180))
    img.u16(car + 0x3C, rng.choice([0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xD4, 0xDD]))
    img.u8(car + 0x1F, rng.choice([0, 1, 2]))
    img.put(car + 0x118, identity(rng))
    for k in range(3):
        img.u32(car + 0x4C + 4 * k, rng.choice([0, 0x1000, 0xFFFFF000, 0x8000, 0xFFFF8000, rng.randrange(-0x20000, 0x20000) & 0xFFFFFFFF]))
        img.u32(car + 8 + 4 * k, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF)
    return car


def model_boxes(img, env, rng, region, count):
    """The region's models: each with a box (twelve shorts-worth) at +0xc, high bound before low, in whole units."""
    img.u32(0x0056D43C + 68 * region, env.slab + MODEL_TABLE)
    img.u16(0x0056D460 + 68 * region, count)
    for k in range(max(count, 1)):
        model = env.slab + (MODEL_A if k % 2 == 0 else MODEL_B)
        img.u32(env.slab + MODEL_TABLE + 4 * k, model)
    for model in (env.slab + MODEL_A, env.slab + MODEL_B):
        hi = [rng.randrange(0, 80) for _ in range(3)]
        lo = [-rng.randrange(0, 80) for _ in range(3)]
        words = [hi[0], lo[0], hi[1], lo[1], hi[2], lo[2]]
        img.put(model + 0xC, struct.pack("<6h", *words))


@wtc(0x00412EF0, "Traffic_Check")
def gen_check(env, n):
    img, rng = Image(), env.rng
    car = car_world(img, env, rng)
    region = img.b.get(car + 0x1F, 0)
    model_boxes(img, env, rng, region, rng.choice([1, 2, 3]))
    STATE["matrix"] = identity(rng)
    img.u8(0x006A3B20 + 1, rng.choice([0, 1]))
    img.u32(0x006A3A88, env.slab + SKATER)
    img.u32(0x0056A858, env.slab + SKATER)
    img.u32(0x0056A85C, rng.choice([0, env.slab + SKATER]))
    skater_state(img, env, rng)
    return [car], img.pokes()


def skater_state(img, env, rng):
    base = env.slab + SKATER
    img.put(base + 0x4, noise(rng, 0x20))
    img.put(base + 0x4C, noise(rng, 12))
    img.put(base + 0xBC, noise(rng, 12))
    img.u32(base + 0x29B8, rng.choice([0, 0, env.slab + CAR, rng.getrandbits(32)]))
    img.u32(base + 0x2F64, rng.choice([0, 0, 1]))
    img.u32(base + 0x30B8, rng.choice([0, 1, 1, 2, 4]))
    img.put(base + 0x2C54, noise(rng, 12))
    img.u32(base + 0x2CC0, rng.randrange(0, 20))
    for k in range(3):
        img.u32(base + 8 + 4 * k, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF)
        img.u32(base + 0xBC + 4 * k, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF)
        img.u32(base + 0x4C + 4 * k, rng.choice([0, 0x1000, 0xFFFFF000, 0x20000, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF]))
    img.put(base + 0x14, noise(rng, 6))
    img.u32(0x0055F72C, rng.choice([0, 100, 249, 250, 251]))
    img.u32(0x005244CC, word(rng))
    img.u32(0x0052E5A0, word(rng))
    img.put(0x0052E5A4, noise(rng, 4))
    img.u32(0x0052E2E0, word(rng))
    img.u32(0x0052E2E4, word(rng))
    img.put(0x0056A920, noise(rng, 8))


@wtc(0x00413000, "TRG_ProcessTrafficInteraction")
def gen_interaction(env, n):
    img, rng = Image(), env.rng
    car = car_world(img, env, rng)
    skater_state(img, env, rng)
    # a skater next to the car: positions within a few whole units of the car's, so the swept step often meets the box
    base = env.slab + SKATER
    center = [rng.randrange(-0x10000, 0x10000) for _ in range(3)]
    for k in range(3):
        img.u32(car + 8 + 4 * k, center[k] & 0xFFFFFFFF)
        img.u32(base + 8 + 4 * k, (center[k] + rng.randrange(-0x30000, 0x30000)) & 0xFFFFFFFF)
        img.u32(base + 0xBC + 4 * k, (center[k] + rng.randrange(-0x30000, 0x30000)) & 0xFFFFFFFF)
    if rng.random() < 0.3:
        img.u32(car + 0xC, (center[1] - rng.choice([0x70000, 0x100000, 0x64001])) & 0xFFFFFFFF)      # the car above the skater: the stomp
        img.u32(base + 0x30B8, 1)
        img.u32(base + 0x2F64, 0)
        img.u16(car + 0x3C, rng.choice([0xD7, 0xD8, 0xDB, 0xD5, 0xD9]))
    if rng.random() < 0.15:                                                                           # (a skater far away)
        img.u32(base + 8, (center[0] + 0x3000000) & 0xFFFFFFFF)
    img.put(env.slab + MATRIX, identity(rng))
    model_boxes(img, env, rng, 0, 1)
    img.put(env.slab + MODEL_A + 0xC, struct.pack("<6h", *[rng.choice([40, 80, 200]), -rng.choice([40, 80, 200]), rng.choice([40, 80, 200]), -rng.choice([40, 80, 200]), rng.choice([40, 80, 200]), -rng.choice([40, 80, 200])]))
    return [car, base, env.slab + MATRIX, env.slab + MODEL_A + 0xC], img.pokes()
