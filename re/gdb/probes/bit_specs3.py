"""Phase 4.15d probe: the sweeps, polylines, smoke generator, line bits, blood manager and small helpers (group bit5: 0x00407250..0x0040b570) and the dust (group bdst: 0x00431260..0x00432250).

The dust group's skater is a fake player at slab offset 0x4000 of which only the fields the dust reads are captured (position, region, velocity, terrain); the dust object is 0x40 bytes in bit slot 23.
"""

from __future__ import annotations

from bit_specs import (ANIM_STRIDE, ANIMS, ARENA, BIT_COUNTER, BIT_STRIDE, BITS, FAKE_VTABLE, GLOBALS, LISTS, SCRATCH, SLAB, SPARE, STATIC, STRINGS, bit, bit_at, chain_with, counters,
                       ft4_body, install_stubs as install_base_stubs, link_chain, merged, noise, put_anim, put_bit, quick_anims, regions as base_regions, seed, time_scale, word)
from bit_specs2 import position_arg, scattered, spark_settings, vector
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure
from frontend_specs_menu import Image

GROUPS = ("bit5", "bdst")

BRUCE = 0x4000
DUST = BITS + BIT_STRIDE * 22          # the dust object (bit slot 22)
MATERIALS = 0x0200                     # five fake scene materials of 0x10 bytes (the logo loader reads their texture coordinates)
SUBDIR = 0x02A0                        # the folder name FileIO_GetSubDir answers with
LOGO_POOL = 0x0300                     # the names and the pointer arrays of the logo lists
REGION_TABLE = 0x0056D460


def bit5_regions(slab):
    return merged(list(base_regions(slab)) + [(0x0056AF28, 0xC), (0x0055F7B8, 8), (0x0055F9A8, 4)], gap=0)


def bdst_regions(slab):
    return merged([(slab, SLAB), (slab + BRUCE, 0x60), (slab + BRUCE + 0x30B0, 4)] + [g for g in GLOBALS] + [(REGION_TABLE, 0x44 * 4), (0x00533F3C, 4), (0x0053A524, 0x170 * 4 + 4), (0x00539E1C, 0x28), (0x0052835C, 8)], gap=0)


for _group, _regions in (("bit5", bit5_regions), ("bdst", bdst_regions)):
    SLAB_BYTES[_group] = BRUCE + 0x3100 if _group == "bdst" else SLAB + 0x100
    EXTRA_REGIONS[_group] = _regions
    READ_ONLY[_group] = list(STATIC)
    SEEDS[_group] = seed
READ_ONLY["bdst"] = list(STATIC) + [(0x0052835C, 8)]       # (the "logos" folder name the loader hands to FileIO_SetSubDir)


def install_stubs(fc, env):
    if getattr(fc, "_bit3_installed", False):
        return
    fc._bit3_installed = True
    install_base_stubs(fc, env)
    ensure(fc, 0x00466BB0, 1, "engine_00466bb0")                                         # CClass_OpDelete
    ensure(fc, 0x0040E000, 4, "engine_0040e000", convention="thiscall", this=True)       # Camera_QuaternionConstruct
    ensure(fc, 0x00464E90, 3, "engine_00464e90", stack_to={2: SCRATCH}, blob=(2, 8))     # M3D_GetHookPosition(out, object, offset)
    rng = env.rng
    ensure(fc, 0x004B18A0, 1, "engine_004b18a0", result=lambda a: rng.randrange(0, 4))                   # Skater_SelectManagerSlot
    ensure(fc, 0x004B2030, 1, "engine_004b2030", result=lambda a: env.slab + MATERIALS + 0x10 * rng.randrange(0, 5) if rng.random() < 0.9 else 0)   # PSX_FindSceneMaterial
    ensure(fc, 0x004496F0, 0, "engine_004496f0", result=lambda a: env.slab + SUBDIR)                      # FileIO_GetSubDir
    ensure(fc, 0x00449700, 1, "engine_00449700", text=True, stack_to=SCRATCH + 0x300)                    # FileIO_SetSubDir
    ensure(fc, 0x004674D0, 2, "engine_004674d0")                                                          # Skater_LoadBitmap


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def b5(address, name, this=BITS):
    return bit(address, name, "bit5", this)


def bd(address, name, this=DUST):
    return bit(address, name, "bdst", this)


# ---- the sweeps -------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@b5(0x00407250, "MoveList", None)
def gen_move_list(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 3, 5, 8])
    return [link_chain(img, env, rng, 0, count) if count else 0], img.pokes()


@b5(0x00407270, "Bit_Move", None)
def gen_bit_move_all(env, n):
    img, rng = Image(), env.rng
    slot = 0
    for name, address in LISTS.items():
        count = rng.choice([0, 0, 1, 2])
        img.u32(address, link_chain(img, env, rng, slot, count) if count else 0)
        slot += count
    return [], img.pokes()


# ---- the polylines ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

def line_start(img, rng):
    img.put(0x0056AF28, noise(rng, 12))


def polyline_cases(address, name, list_name, size, this_fields):
    @b5(address, f"{name}_Construct")
    def gen_construct(env, n):
        img, rng = Image(), env.rng
        put_bit(img, env, rng, 0, size=size)
        counters(img, rng)
        line_start(img, rng)
        img.u32(LISTS[list_name], link_chain(img, env, rng, 1, rng.choice([0, 1])) if rng.random() < 0.5 else 0)
        return [rng.choice([1, 2, 5, 10, 50, 99, 0, 100, -1 & 0xFFFFFFFF, 120])], img.pokes()


polyline_cases(0x0040A740, "CPolyLine", "polyline", 0x60, None)
polyline_cases(0x0040AA20, "CGPolyLine", "gpolyline", 0x60, None)


def fake_line(img, env, rng, buffer_field, count_field, list_name):
    """A polyline in slot 0 in its list: `count` segments of 16 bytes in the arena."""
    chain_with(img, env, rng, list_name, 0x60)
    at = bit_at(env, 0)
    count = rng.choice([0, 1, 2, 3, 8])
    img.put(env.slab + ARENA + 0x400, noise(rng, 16 * 8))
    img.u32(at + buffer_field, env.slab + ARENA + 0x400)
    img.u32(at + count_field, count)
    return count


@b5(0x0040A860, "CPolyLine_ScalarDelete")
def gen_polyline_sdelete(env, n):
    img, rng = Image(), env.rng
    fake_line(img, env, rng, 0x44, 0x40, "polyline")
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b5(0x0040A880, "CPolyLine_Destruct")
def gen_polyline_destruct(env, n):
    img, rng = Image(), env.rng
    fake_line(img, env, rng, 0x44, 0x40, "polyline")
    counters(img, rng)
    return [], img.pokes()


@b5(0x0040A900, "CPolyLine_SetSemiTransparent")
def gen_polyline_semi(env, n):
    img, rng = Image(), env.rng
    fake_line(img, env, rng, 0x44, 0x40, "polyline")
    return [], img.pokes()


@b5(0x0040AB50, "CGPolyLine_ScalarDelete")
def gen_gpolyline_sdelete(env, n):
    img, rng = Image(), env.rng
    fake_line(img, env, rng, 0x40, 0x3C, "gpolyline")
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b5(0x0040AB70, "CGPolyLine_Destruct")
def gen_gpolyline_destruct(env, n):
    img, rng = Image(), env.rng
    fake_line(img, env, rng, 0x40, 0x3C, "gpolyline")
    counters(img, rng)
    return [], img.pokes()


# ---- the smoke generator, the line bits ----------------------------------------------------------------------------------------------------------------------------------------------

@b5(0x0040AD30, "CSmokeGenerator_Construct")
def gen_smoke_generator_construct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x60)
    counters(img, rng)
    img.u32(LISTS["nonrendered"], link_chain(img, env, rng, 1, rng.choice([0, 1])) if rng.random() < 0.5 else 0)
    return [position_arg(img, env, rng), rng.choice([0, 1, 2, 30, 0xFFFF, 0x10000, word(rng)])] + [word(rng) for _ in range(8)], img.pokes()


@b5(0x0040AE20, "CSmokeGenerator_ScalarDelete")
def gen_smoke_generator_sdelete(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "nonrendered", 0x60)
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b5(0x0040AE40, "CSmokeGenerator_Destruct")
def gen_smoke_generator_destruct(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "nonrendered", 0x60)
    counters(img, rng)
    return [], img.pokes()


@b5(0x0040AE50, "CSmokeGenerator_Move")
def gen_smoke_generator_move(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x60)
    counters(img, rng)
    at = bit_at(env, 0)
    for k in range(4):
        put_anim(img, env, rng, k)
    vector(img, rng, at + 0x10, -0x100000, 0x100000)
    img.u32(at + 0x3C, rng.choice([0, 1, 2, 3, 4]))
    img.put(at + 0x40, noise(rng, 3))
    img.u32(at + 0x44, rng.choice([0, 1, 5, 10, -3 & 0xFFFFFFFF]))
    img.u32(at + 0x48, rng.choice([0, 1, 5, 10, 100]))
    img.u32(at + 0x4C, rng.choice([0, 100, 400, 0x7FFF]))
    img.u32(at + 0x50, rng.choice([0, 1, 5, 100]))
    img.u16(at + 0xC, rng.choice([0, 1, 5, 29, 30, 0xFFFE, 0xFFFF]))
    img.u16(at + 0xE, rng.choice([0, 1, 2, 30, 31, 0xFFFF]))
    img.u8(at + 0x3A, rng.choice([0, 0, 0, 1]))
    return [], img.pokes()


@b5(0x0040AF70, "CGLine_Construct")
def gen_gline_construct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x70)
    counters(img, rng)
    img.u32(LISTS["gline"], link_chain(img, env, rng, 1, rng.choice([0, 1, 2])) if rng.random() < 0.5 else 0)
    return [], img.pokes()


@b5(0x0040AFE0, "CGLine_ScalarDelete")
def gen_gline_sdelete(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "gline", 0x70)
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b5(0x0040B000, "CGLine_Destruct")
def gen_gline_destruct(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "gline", 0x70)
    counters(img, rng)
    return [], img.pokes()


@b5(0x0040B060, "CGLineBit_Construct")
def gen_gline_bit_construct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x70)
    counters(img, rng)
    spark_settings(img, rng)
    img.u32(LISTS["gline"], link_chain(img, env, rng, 1, rng.choice([0, 1])) if rng.random() < 0.5 else 0)
    obj = env.slab + SPARE + 0x40
    img.put(obj, noise(rng, 0x20))
    return [obj, position_arg(img, env, rng), rng.choice([0, 100, 0x1000, -100 & 0xFFFFFFFF, -0x1000 & 0xFFFFFFFF, rng.randrange(-0x2000, 0x2000) & 0xFFFFFFFF]), word(rng),
            rng.choice([0, 1, 5, 30, 100])], img.pokes()


@b5(0x0040B1A0, "CGLineBit_ScalarDelete")
def gen_gline_bit_sdelete(env, n):
    return gen_gline_sdelete(env, n)


@b5(0x0040B1C0, "CGLineBit_Destruct")
def gen_gline_bit_destruct(env, n):
    return gen_gline_destruct(env, n)


@b5(0x0040B1D0, "CGLineBit_Move")
def gen_gline_bit_move(env, n):
    """A line bit in slot 0 (0x70 bytes) flying from the object at +0x68 (position at +8): the age against the life on the edges (signed age, unsigned life, the age wrapping through 0x8000)."""
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x70)
    counters(img, rng)
    at = bit_at(env, 0)
    obj = env.slab + SPARE + 0x40
    img.put(obj, noise(rng, 0x20))
    img.u32(at + 0x68, obj)
    vector(img, rng, at + 0x10, -0x400000, 0x400000)
    vector(img, rng, at + 0x1C, -0x80000, 0x80000)
    vector(img, rng, at + 0x28, -0x2000, 0x2000)
    life = rng.choice([0, 1, 2, 5, 30, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)])
    age = rng.choice([(life - 2) & 0xFFFF, (life - 1) & 0xFFFF, life, (life - 3) & 0xFFFF, 0, 1, 0x7FFE, 0x7FFF, 0xFFFE, 0xFFFF, rng.getrandbits(16)])
    img.u16(at + 0xC, age)
    img.u16(at + 0xE, life)
    return [], img.pokes()


# ---- the blood manager, the helpers ---------------------------------------------------------------------------------------------------------------------------------------------

def fake_blood(img, env, rng):
    """A blood manager in slot 0: up to twenty bits (the count at +0x54), the age at +0x58."""
    at = put_bit(img, env, rng, 0, size=0x5C)
    count = rng.choice([0, 1, 2, 3, 5])
    for k in range(5):
        img.u32(at + 4 + 4 * k, put_bit(img, env, rng, 1 + k, size=0x50) if (k < count and rng.random() < 0.85) else 0)
    img.u32(at + 0x54, rng.choice([count, count, count, rng.randrange(0, 6), -1 & 0xFFFFFFFF]) if rng.random() < 0.95 else count)
    img.u32(at + 0x58, word(rng))
    return at


@b5(0x0040B2E0, "Blood_ScalarDelete")
def gen_blood_sdelete(env, n):
    img, rng = Image(), env.rng
    fake_blood(img, env, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b5(0x0040B300, "Blood_Destruct")
def gen_blood_destruct(env, n):
    img, rng = Image(), env.rng
    fake_blood(img, env, rng)
    return [], img.pokes()


@b5(0x0040B350, "Blood_Reset")
def gen_blood_reset(env, n):
    return gen_blood_destruct(env, n)


@b5(0x0040B3E0, "CClass_Destruct")
def gen_class_destruct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x20)
    return [], img.pokes()


@b5(0x0040B3F0, "CClass_ScalarDelete")
def gen_class_sdelete(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x20)
    return [rng.choice([0, 1, 2, 3, 0x100, 0x101, word(rng)])], img.pokes()


@b5(0x0040B410, "Vector_Jitter", None)
def gen_jitter(env, n):
    img, rng = Image(), env.rng
    vector(img, rng, env.slab + SPARE)
    return [env.slab + SPARE, rng.choice([0, 1, 2, 3, 8, 16, 100, 1000, rng.randrange(1, 64)])], img.pokes()


@b5(0x0040B4E0, "Camera_RandSeed", None)
def gen_camera_seed(env, n):
    img, rng = Image(), env.rng
    img.put(0x0055F7B8, noise(rng, 8))
    return [word(rng)], img.pokes()


@b5(0x0040B500, "Camera_Rand", None)
def gen_camera_rand(env, n):
    img, rng = Image(), env.rng
    img.u32(0x0055F7B8, word(rng))
    img.u32(0x0055F7BC, word(rng))
    img.u32(0x0055F9A8, word(rng))
    return [rng.choice([0, 1, 10, 100, 0x10000, 0x7FFFFFFF, word(rng), rng.randrange(1, 1000)])], img.pokes()


@b5(0x0040B570, "Camera_QuaternionIdentity", None)
def gen_camera_identity(env, n):
    return [], Image().pokes()


# ---- the dust ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

TERRAINS = [0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 8, 0xD, 0xD, 0xE, 0xE, 0xF, 0x10]


def fake_bruce(img, env, rng):
    at = env.slab + BRUCE
    img.put(at, noise(rng, 0x60))
    img.u8(at + 0x1F, rng.randrange(0, 4))
    img.u32(at + 0x30B0, rng.choice(TERRAINS))
    for k in range(4):
        img.u16(REGION_TABLE + 0x44 * k, rng.choice([0, 1, 5, 100, 0x7FFF, 0xFFFF, rng.getrandbits(16)]))
    return at


def fake_dust(img, env, rng, count=None, kind=None, clouds=None, fading=None):
    """The dust object in slot 22: `count` clouds (flat bits, slots 1..12), the kind (0 landing, 1 bailing, 2 braking) and the clouds still to make."""
    count = rng.choice([0, 1, 2, 5, 11, 12, 12]) if count is None else count
    at = put_bit(img, env, rng, 22, size=0x40)
    for k in range(12):
        if k < count:
            cloud = put_bit(img, env, rng, 1 + k, size=0x70)
            img.u16(cloud + 0x52, rng.choice([0, 0x100, 0x1FF, 0x200, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)]))
            img.u16(cloud + 0x3E, rng.choice([0, 1, 8, 0x40, rng.getrandbits(16)]))
            img.u16(cloud + 0x3C, rng.choice([0, 1, 2, 8, 0x80, rng.getrandbits(16)]))
            img.u32(cloud + 0x40, rng.choice([0, 0x2C808080, 0x00000001, 0x01000000, 0x00404040, 0x00010000, 0x00000100, rng.getrandbits(32)]) if fading is None else fading)
            img.u8(cloud + 0x3A, rng.choice([0, 0, 1]))
            img.u32(at + 4 + 4 * k, cloud)
        else:
            img.u32(at + 4 + 4 * k, 0)
    img.u32(at + 0x34, count)
    img.u32(at + 0x38, rng.choice([0, 1, 2]) if kind is None else kind)
    img.u32(at + 0x3C, rng.choice([0, 0, 1, 2, 5, 12, -1 & 0xFFFFFFFF]) if clouds is None else clouds)
    return at


@bd(0x00431260, "Dust_Reset")
def gen_dust_reset(env, n):
    img, rng = Image(), env.rng
    fake_dust(img, env, rng)
    return [rng.choice(TERRAINS), rng.choice([0, 1, 2, 0, 1, 2, 3, 0xFFFFFFFF])], img.pokes()


@bd(0x00431320, "Dust_Update")
def gen_dust_update(env, n):
    img, rng = Image(), env.rng
    counters(img, rng)
    for k in range(4):
        put_anim(img, env, rng, k)
    bruce = fake_bruce(img, env, rng)
    fake_dust(img, env, rng, clouds=rng.choice([0, 0, 1, 2, 3, 12, 13]))
    # a few cases end the clouds: all of them faded
    if rng.random() < 0.3:
        count = img.b[env.slab + DUST + 0x34]
        for k in range(count):
            cloud = img.b[env.slab + DUST + 4 + 4 * k] | (img.b[env.slab + DUST + 5 + 4 * k] << 8) | (img.b[env.slab + DUST + 6 + 4 * k] << 16) | (img.b[env.slab + DUST + 7 + 4 * k] << 24)
            img.u32(cloud + 0x40, rng.choice([0, 0, 0, 0x2C000000]))
    return [bruce if rng.random() < 0.97 else 0], img.pokes()


@bd(0x00431650, "Dust_InsertDust")
def gen_dust_insert(env, n):
    img, rng = Image(), env.rng
    fake_dust(img, env, rng)
    cloud = put_bit(img, env, rng, 20, size=0x70)
    at = env.slab + DUST
    if rng.random() < 0.15:
        img.u32(at + 4 + 4 * 11, rng.choice([0, cloud]))
    return [cloud if rng.random() < 0.97 else 0], img.pokes()


def terrain_outputs(img, env, rng):
    """Where the get_terrain_parameters functions write: scale (short), tint (byte), fade (short), scale decay (short), clouds (dword), offset (dword)."""
    base = env.slab + SPARE
    img.put(base, noise(rng, 0x30))
    return [base, base + 0x08, base + 0x10, base + 0x18, base + 0x20, base + 0x28]


def terrain_case(address, name, kind):
    @bd(address, name)
    def gen(env, n):
        img, rng = Image(), env.rng
        fake_dust(img, env, rng, kind=kind if rng.random() < 0.9 else rng.choice([0, 1, 2, 3]))
        outputs = terrain_outputs(img, env, rng)
        if rng.random() < 0.05:
            outputs[rng.randrange(0, 6)] = 0
            # (a null pointer is asserted and then written through: retail faults)
            raise_skip()
        return [rng.choice(TERRAINS)] + outputs, img.pokes()


def raise_skip():
    from create_specs import Skip
    raise Skip()


terrain_case(0x004317C0, "Dust_GetTerrainParameters", None)
terrain_case(0x004318A0, "Dust_GetLandTerrainParameters", 0)
terrain_case(0x00431B80, "Dust_GetBailTerrainParameters", 1)
terrain_case(0x00431E80, "Dust_GetBrakeTerrainParameters", 2)


@bd(0x00432180, "Dust_ApplyRandomOffset", None)
def gen_dust_random_offset(env, n):
    img, rng = Image(), env.rng
    vector(img, rng, env.slab + SPARE)
    return [env.slab + SPARE, rng.choice([0, 1, 2, 8, 0x10, 0x20, 100, -4 & 0xFFFFFFFF, rng.randrange(1, 50)]), rng.choice([0, 0, 0xC, 1, 4, 31, 32, 33])], img.pokes()


def logo_names(img, env, rng, count):
    """`count` file names in the strings area, '<base><digit>.bmp' with digits 1..4, and an array of pointers to them (null terminated)."""
    pool = env.slab + SPARE
    array = env.slab + SPARE + 0x80
    for k in range(count):
        digit = rng.choice(["1", "2", "3", "4"])
        text = (rng.choice(["logo", "ab", "x"]) + digit + rng.choice([".bmp", ".tga", ".x"])).encode() + b"\0"
        img.put(pool + 0x10 * k, text.ljust(0x10, b"\0"))
        img.u32(array + 4 * k, pool + 0x10 * k)
    img.u32(array + 4 * count, 0)
    return array


@bd(0x004321D0, "DynamicLogo_NameMatches", None)
def gen_logo_matches(env, n):
    img, rng = Image(), env.rng
    pool = env.slab + SPARE
    text = rng.choice([b"logo1.bmp", b"logo4.tga", b"a2.x", b"logo.bmp", b"x.", b".bmp", b"zz3.q.r"]) if rng.random() < 0.9 else b"noext"
    img.put(pool, text + b"\0")
    return [pool, rng.choice([0, 1, 2, 3, 0, 1, 2, 3, -1 & 0xFFFFFFFF])], img.pokes()


@bd(0x00432250, "DynamicLogo_FindNext", None)
def gen_logo_next(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([1, 2, 3, 4, 6])
    array = logo_names(img, env, rng, count)
    start = array + 4 * rng.randrange(0, count)
    return [rng.choice([start, start, start, 0]), array, rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, -1 & 0xFFFFFFFF])], img.pokes()


@bd(0x00432330, "DynamicLogo_LoadAll", None)
def gen_logo_load_all(env, n):
    img, rng = Image(), env.rng
    img.u32(0x00533F3C, rng.choice([1, 1, 2, 3, 0]))
    # four name lists (pointer arrays, null terminated) of the pool; the character records point at them
    pool = env.slab + LOGO_POOL
    cursor = 0
    arrays = []
    for k in range(4):
        count = rng.choice([0, 1, 2, 3, 5])
        array = pool + 0x400 + 0x40 * k
        for j in range(count):
            digit = rng.choice(["1", "2", "3", "4"])
            text = (rng.choice(["logo", "ab", "x"]) + digit + ".bmp").encode() + b"\x00"
            at = pool + 0x10 * cursor
            cursor += 1
            img.put(at, text.ljust(0x10, b"\x00"))
            img.u32(array + 4 * j, at)
        img.u32(array + 4 * count, 0)
        arrays.append(array)
    for k in range(4):
        img.u32(0x0053A524 + 0x170 * k, arrays[k])
    img.put(env.slab + SUBDIR, rng.choice([b"levels\\", b"", b"a\b\\", b"x"]) + b"\x00")
    checksums = [rng.getrandbits(32) | 1 for _ in range(rng.choice([0, 1, 2, 4, 6]))]
    for k in range(7):
        img.u32(0x00539E1C + 4 * k, checksums[k] if k < len(checksums) else 0)
    for k in range(5):
        width = rng.choice([0x20, 0x40, 0x60, 0x80, 0x20, 0x40, 0x60, 0x80, rng.randrange(1, 0xFF)])
        low = rng.randrange(0, 0x100 - min(width, 0xFF))
        high = min(0xFF, low + width - 1)
        values = [low, high, rng.randrange(low, high + 1), rng.randrange(low, high + 1)]
        rng.shuffle(values)
        at = env.slab + MATERIALS + 0x10 * k
        img.put(at, noise(rng, 0x10))
        for offset, value in zip((0, 4, 8, 0xA), values):
            img.u8(at + offset, value)
    return [], img.pokes()
