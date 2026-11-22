"""Phase 4.15a probe: the small matrix and GTE register helpers of the M3D transform code (0x004f4050..0x004f5f10; group m3d).

They work on 3x3 matrices of Q12 shorts and small vectors in a scratch slab and on the GTE register file (0x006a3e00, 0x120 bytes, a window poked with random words). Nothing is stubbed: the helpers
call each other and the GTE operations (`gte_rtir`, `gte_lcir`) and the rotation-from-angles routine as the real retail code.

Slab layout (offsets): matrices at 0x000 / 0x020 / 0x040, vectors at 0x080 / 0x090, a box (two vectors) at 0x0a0, its lower and upper corner at 0x0c0 / 0x0d0, the flags at 0x0e0, angles at 0x0f0.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import merged
from frontend_specs_menu import Image

GROUPS = ("m3d",)

SLAB = 0x600
MAT_A = 0x000
MAT_B = 0x020
MAT_C = 0x040
VEC_A = 0x080
VEC_B = 0x090
BOX = 0x0A0
LOW = 0x0C0
HIGH = 0x0D0
FLAGS = 0x0E0
ANGLES = 0x0F0
OBJECTS = 0x100                 # the null-terminated array of object pointers (8 dwords)
OBJ = 0x140                     # six objects of 0x40 bytes
MODELS = 0x300                  # four model records of 0x20 bytes
MODEL_TABLE = 0x380             # the array of model pointers (4 dwords)
CAMERA = 0x3A0
PROJECT_BOX = 0x3C0             # 0x30 bytes
CULL_BOX = 0x400                # six dwords
TRANSFORM = 0x440               # a transform of twelve shorts (0x18 bytes) ...
TRANSFORM_B = 0x460
TRANSFORM_C = 0x480
VERTICES = 0x4A0                # a model record: the count at +2, the vertices from +0x1c (four shorts each)
OUT = 0x540                     # the output records (8 bytes a vertex)
LIGHTS = 0x5C0                  # a lights record (0x20 bytes)
GTE = 0x006A3E00
GTE_BYTES = 0x120
MODEL_TABLES = 0x0056D43C       # PsxModelTablesByRegion: per region (0x44 bytes) the pointer to the model array


def m3d_regions(slab):
    return merged([(slab, SLAB), (GTE, GTE_BYTES), (MODEL_TABLES, 0x44 * 3 + 4)], gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = m3d_regions
    READ_ONLY[_group] = []


def m3d(address, name):
    return spec(address, name, "m3d")


def noise(img, rng):
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))


def short(rng):
    return rng.choice([0, 1, -1, 0x1000, -0x1000, 0x7FFF, -0x8000, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000)]) & 0xFFFF


def put_matrix(img, address, rng):
    for k in range(9):
        img.u16(address + 2 * k, short(rng))


def put_vector(img, address, rng, scale=None):
    scale = scale or rng.choice([0x1000, 0x100000, 0x7FFFFFF, 0x7FFFFFFF])
    for k in range(3):
        img.u32(address + 4 * k, rng.randrange(-scale, scale) & 0xFFFFFFFF)


@m3d(0x004F5390, "M3D_RotationMatrixFromAngles")
def gen_rotation(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    for k in range(3):
        img.u16(env.slab + ANGLES + 2 * k, rng.choice([0, 0x400, 0x800, 0xC00, 0xFFF, rng.randrange(0, 0x1000), rng.getrandbits(16)]))
    put_matrix(img, env.slab + MAT_A, rng)
    return [env.slab + ANGLES, env.slab + MAT_A], img.pokes()


@m3d(0x004F50D0, "GTE_SetBackgroundColour")
def gen_background(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    put_vector(img, env.slab + VEC_A, rng)
    return [env.slab + VEC_A], img.pokes()


@m3d(0x004F50F0, "GTE_SetTranslationQ12")
def gen_translation(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    put_vector(img, env.slab + VEC_A, rng)
    return [env.slab + VEC_A], img.pokes()


@m3d(0x004F5F10, "Math_MulDivTruncated")
def gen_mul_div(env, n):
    rng = env.rng
    pick = lambda: rng.choice([0, 1, -1, 7, 100, 4096, -4096, 0x7FFFFFFF, -0x80000000, rng.randrange(-100000, 100000), rng.getrandbits(32) - (1 << 31)])
    c = pick()
    while c == 0:
        c = pick()
    return [pick() & 0xFFFFFFFF, pick() & 0xFFFFFFFF, c & 0xFFFFFFFF], Image().pokes()


@m3d(0x004F5430, "Matrix_SetIdentityQ12")
def gen_identity(env, n):
    img, rng = Image(), env.rng
    put_matrix(img, env.slab + MAT_A, rng)
    return [env.slab + MAT_A], img.pokes()


@m3d(0x004F53E0, "Render_TransposeMatrixQ12")
def gen_transpose(env, n):
    img, rng = Image(), env.rng
    put_matrix(img, env.slab + MAT_A, rng)
    put_matrix(img, env.slab + MAT_B, rng)
    return [env.slab + MAT_A, env.slab + (MAT_A if rng.random() < 0.15 else MAT_B)], img.pokes()


@m3d(0x004F5730, "Matrix_CopyQ12")
def gen_copy(env, n):
    img, rng = Image(), env.rng
    put_matrix(img, env.slab + MAT_A, rng)
    put_matrix(img, env.slab + MAT_B, rng)
    return [env.slab + MAT_A, env.slab + MAT_B], img.pokes()


@m3d(0x004F56C0, "GTE_LoadRotationAndRotateIR")
def gen_load_rotate(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    put_matrix(img, env.slab + MAT_A, rng)
    return [env.slab + MAT_A], img.pokes()


@m3d(0x004F4FE0, "GTE_LoadRotationAndTranslation")
def gen_load_translation(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    for k in range(12):
        img.u16(env.slab + MAT_A + 2 * k, short(rng))
    return [env.slab + MAT_A], img.pokes()


@m3d(0x004F5460, "Matrix_MultiplyByRotationColumns")
def gen_by_rotation(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    put_matrix(img, env.slab + MAT_A, rng)
    put_matrix(img, env.slab + MAT_B, rng)
    return [env.slab + MAT_A, env.slab + (MAT_A if rng.random() < 0.2 else MAT_B)], img.pokes()


@m3d(0x004F5780, "Matrix_MultiplyByLightColumns")
def gen_by_light(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    put_matrix(img, env.slab + MAT_A, rng)
    put_matrix(img, env.slab + MAT_B, rng)
    return [env.slab + MAT_A, env.slab + (MAT_A if rng.random() < 0.2 else MAT_B)], img.pokes()


@m3d(0x004F4050, "Box_OrderCorners")
def gen_order(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    put_vector(img, env.slab + LOW, rng)
    put_vector(img, env.slab + HIGH, rng)
    img.u32(env.slab + FLAGS, rng.choice([0, 1, 2, 7, rng.getrandbits(32)]))
    return [env.slab + LOW, env.slab + HIGH, env.slab + FLAGS], img.pokes()


@m3d(0x004F40D0, "Box_CornersToUnits")
def gen_to_units(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    for k in range(6):
        img.u32(env.slab + BOX + 4 * k, rng.randrange(-0x7FFFFFFF, 0x7FFFFFFF) & 0xFFFFFFFF)
    img.u32(env.slab + FLAGS, rng.getrandbits(32))
    return [env.slab + BOX, env.slab + LOW, env.slab + HIGH, env.slab + FLAGS], img.pokes()


@m3d(0x004F3F30, "Camera_ProjectBoxCorners")
def gen_project(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    slab = env.slab
    for k in range(6):
        img.u32(slab + PROJECT_BOX + 4 * k, rng.randrange(-0x4000000, 0x4000000) & 0xFFFFFFFF)
    img.put(slab + PROJECT_BOX + 0x18, bytes(rng.getrandbits(8) for _ in range(0x18)))
    for k in range(4):
        img.u32(slab + CAMERA + 4 * k, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF)
    return [slab + PROJECT_BOX, slab + CAMERA], img.pokes()


@m3d(0x004F4130, "Box_ObjectCornersRelativeToBox")
def gen_object_corners(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    for k in range(6):
        img.u32(slab + MODELS + 0xC + 4 * (k % 3), rng.getrandbits(32))
    pos = lambda: rng.randrange(-0x4000, 0x4000) & 0xFFFFFFFF
    coord = lambda: rng.randrange(-0x4000, 0x4000) & 0xFFFFFFFF
    img.put(slab + LOW, bytes(rng.getrandbits(8) for _ in range(0x20)))
    img.put(slab + HIGH, bytes(rng.getrandbits(8) for _ in range(0x20)))
    low = [coord() for _ in range(3)]
    high = [coord() for _ in range(3)]
    obj = [pos() for _ in range(3)]
    return [slab + MODELS, *low, *high, *obj, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, rng.getrandbits(8), rng.getrandbits(32)]), slab + LOW, slab + HIGH], img.pokes()


@m3d(0x004F4240, "Box_ObjectOverlapsBox")
def gen_overlap(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    base = [rng.randrange(-0x400, 0x400) for _ in range(3)]
    size = [rng.randrange(0, 0x300) for _ in range(3)]
    high = [base[k] + size[k] for k in range(3)]
    other_low = [base[k] + rng.randrange(-0x300, 0x300) for k in range(3)]
    other_high = [other_low[k] + rng.randrange(-0x40, 0x300) for k in range(3)]
    extents = [high[k] - base[k] for k in range(3)] if rng.random() < 0.7 else [rng.randrange(-0x3000, 0x3000) for _ in range(3)]
    args = [*base, *high, *other_low, *other_high, *extents]
    return [v & 0xFFFFFFFF for v in args], img.pokes()


@m3d(0x004F4940, "Collision_CullObjectsAgainstBox")
def gen_cull(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    slab = env.slab
    region = rng.randrange(0, 3)
    img.u32(MODEL_TABLES + 0x44 * region, slab + MODEL_TABLE)
    for k in range(4):
        img.u32(slab + MODEL_TABLE + 4 * k, slab + MODELS + 0x20 * k)
        for j in range(3):
            img.u32(slab + MODELS + 0x20 * k + 0xC + 4 * j, ((rng.randrange(-0x40, 0x40) & 0xFFFF) << 16) | (rng.randrange(-0x40, 0x40) & 0xFFFF))
    count = rng.choice([0, 1, 2, 4, 6])
    for k in range(6):
        img.u32(slab + OBJECTS + 4 * k, slab + OBJ + 0x40 * k if k < count else 0)
        obj = slab + OBJ + 0x40 * k
        img.u16(obj + 4, rng.choice([0, 0x20, 0x41, 0x61, 0x01, 0x40, rng.getrandbits(16)]))
        img.u16(obj + 6, rng.choice([0, 1, 5, 0x7FFF, 0xFFFF, rng.getrandbits(16)]))
        for j in range(3):
            img.u32(obj + 8 + 4 * j, (rng.randrange(-0x80, 0x80) << 12) & 0xFFFFFFFF)
        img.u16(obj + 0x1A, rng.randrange(0, 4))
        img.u8(obj + 0x1F, region)
    img.u32(slab + OBJECTS + 4 * 6, 0)
    base = [rng.randrange(-0x40, 0x40) << 12 for _ in range(3)]
    for k in range(3):
        img.u32(slab + CULL_BOX + 4 * k, base[k] & 0xFFFFFFFF)
        img.u32(slab + CULL_BOX + 12 + 4 * k, (base[k] + rng.randrange(-0x40, 0x80) * 0x1000) & 0xFFFFFFFF)
    return [slab + OBJECTS, rng.getrandbits(32), slab + CULL_BOX, rng.choice([0, 1, 2, 5, 0x7FFF, 0xFFFF, rng.getrandbits(32)])], img.pokes()


def put_transform(img, address, rng):
    for k in range(12):
        img.u16(address + 2 * k, short(rng))


@m3d(0x004F4B00, "Collision_TransformLinkedModelVertices")
def gen_vertices(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    slab = env.slab
    count = rng.choice([0, 1, 2, 4, 8, 12])
    img.u16(slab + VERTICES + 2, count)
    for k in range(13):
        for j in range(4):
            img.u16(slab + VERTICES + 0x1C + 8 * k + 2 * j, rng.randrange(-0x400, 0x400) & 0xFFFF if rng.random() < 0.9 else short(rng))
    for k in range(3):
        img.u32(slab + CAMERA + 4 * k, rng.randrange(-0x400, 0x400) & 0xFFFFFFFF)
    img.put(slab + OUT, bytes(rng.getrandbits(8) for _ in range(0x60)))
    return [slab + VERTICES, slab + OUT, rng.choice([0, 0x100, 0x1000, 0x7FFF, -1 & 0xFFFFFFFF, rng.randrange(-0x2000, 0x2000) & 0xFFFFFFFF]), slab + CAMERA], img.pokes()


@m3d(0x004F5160, "M3D_ApplyHookTransform")
def gen_hook(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    slab = env.slab
    put_transform(img, slab + TRANSFORM, rng)
    for k in range(3):
        img.u16(slab + VEC_A + 2 * k, short(rng))
    img.put(slab + LIGHTS, bytes(rng.getrandbits(8) for _ in range(0x20)))
    return [slab + VEC_A, slab + TRANSFORM, slab + LIGHTS], img.pokes()


@m3d(0x004F5540, "Collision_ScaleLinkedObjectMatrix")
def gen_scale(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    slab = env.slab
    put_matrix(img, slab + MAT_A, rng)
    for k in range(3):
        img.u16(slab + MODELS + 0x28 + 2 * k, rng.choice([0, 0x1000, 0x800, 0x2000, short(rng)]))
    return [slab + MODELS, slab + MAT_A], img.pokes()


@m3d(0x004F5A70, "Transform_ComposeQ12")
def gen_compose(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    slab = env.slab
    put_transform(img, slab + TRANSFORM, rng)
    put_transform(img, slab + TRANSFORM_B, rng)
    put_transform(img, slab + TRANSFORM_C, rng)
    return [slab + TRANSFORM_C, slab + TRANSFORM, slab + TRANSFORM_B], img.pokes()


@m3d(0x004F5CA0, "Transform_ComposeWithTransposeQ12")
def gen_compose_transpose(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    slab = env.slab
    put_transform(img, slab + TRANSFORM, rng)
    put_transform(img, slab + TRANSFORM_B, rng)
    put_transform(img, slab + TRANSFORM_C, rng)
    return [slab + TRANSFORM_C, slab + TRANSFORM, slab + TRANSFORM_B], img.pokes()
