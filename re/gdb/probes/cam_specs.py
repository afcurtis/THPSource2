"""Phase 4.15b probe: the camera's quaternion and matrix math (0x004a9650..0x004a9bf0, 0x004103b0; group cmq).

Quaternions are five dwords {vtable, x, y, z, w} (Q12), matrices nine shorts (+ six more the conversion clears). They live in a scratch slab; the GTE register window (0x006a3e00, 0x120 bytes) is
captured too because the vector normalisation uses it, and the axis table at 0x0053757c is read-only data. Nothing is stubbed: the sine, cosine, square root and arc-cosine routines are retail code.

Slab layout (offsets): quaternions at 0x00 / 0x20 / 0x40 / 0x60, a vector at 0x80, matrices at 0xA0 / 0xC0, the scratch of the vector normalisation at 0x100.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import merged
from frontend_specs_menu import Image

GROUPS = ("cmq",)

SLAB = 0x200
Q_A = 0x00
Q_B = 0x20
Q_OUT = 0x40
Q_OUT2 = 0x60
VEC = 0x80
MAT_A = 0xA0
MAT_B = 0xC0
GTE = 0x006A3E00
GTE_BYTES = 0x120
AXIS_TABLE = 0x0053757C


def cmq_regions(slab):
    return merged([(slab, SLAB), (GTE, GTE_BYTES)], gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = cmq_regions
    READ_ONLY[_group] = [(AXIS_TABLE, 12)]


def cmq(address, name):
    return spec(address, name, "cmq")


def noise(img, rng):
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))


def short(rng):
    return rng.choice([0, 1, -1, 0x1000, -0x1000, 0x7FFF, -0x8000, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000)])


def component(rng):
    """A Q12 quaternion component: mostly inside [-1, 1], now and then wild."""
    return rng.choice([0, 0x1000, -0x1000, 0x800, -0x800, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x1000, 0x1001), rng.randrange(-0x1000, 0x1001), rng.randrange(-0x20000, 0x20000),
                       rng.getrandbits(32) - (1 << 31)]) & 0xFFFFFFFF


def put_quaternion(img, address, rng, unit=False):
    img.u32(address, rng.choice([0x5184D8, 0x5184D8, 0, rng.getrandbits(32)]))
    if unit:
        # a roughly unit quaternion: pick a direction and scale it to 0x1000
        import math
        v = [rng.uniform(-1, 1) for _ in range(4)]
        length = math.sqrt(sum(c * c for c in v)) or 1.0
        for k, c in enumerate(v):
            img.u32(address + 4 + 4 * k, int(c / length * 0x1000) & 0xFFFFFFFF)
    else:
        for k in range(4):
            img.u32(address + 4 + 4 * k, component(rng))


def put_matrix(img, address, rng):
    for k in range(9):
        img.u16(address + 2 * k, short(rng) & 0xFFFF)
    for k in range(9, 16):
        img.u16(address + 2 * k, short(rng) & 0xFFFF)


def rotation_matrix(img, address, rng):
    """A rotation matrix of three random angles, from the quaternion formula, with now and then a perturbation."""
    import math
    v = [rng.uniform(-1, 1) for _ in range(4)]
    length = math.sqrt(sum(c * c for c in v)) or 1.0
    x, y, z, w = (c / length for c in v)
    m = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
    for k, c in enumerate(m):
        value = int(round(c * 0x1000)) + (rng.randrange(-3, 4) if rng.random() < 0.3 else 0)
        img.u16(address + 2 * k, value & 0xFFFF)
    for k in range(9, 16):
        img.u16(address + 2 * k, short(rng) & 0xFFFF)


@cmq(0x004A9650, "Camera_ComposeTransformQ12")
def gen_compose(env, n):
    img, rng = Image(), env.rng
    put_quaternion(img, env.slab + Q_A, rng, unit=rng.random() < 0.5)
    put_quaternion(img, env.slab + Q_B, rng, unit=rng.random() < 0.5)
    put_quaternion(img, env.slab + Q_OUT, rng)
    out = rng.choice([Q_OUT, Q_OUT, Q_OUT, Q_A, Q_B])
    return [env.slab + out, env.slab + Q_A, env.slab + Q_B], img.pokes()


@cmq(0x004A9750, "Camera_QuaternionFromRotationVector")
def gen_from_vector(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    scale = rng.choice([0x10, 0x100, 0x400, 0x1000, 0x2000, 0x7FFF, 0x10000])
    vector = [rng.randrange(-scale, scale + 1) for _ in range(3)]
    if rng.random() < 0.15:
        vector = [0, 0, 0]
    for k, c in enumerate(vector):
        img.u32(env.slab + VEC + 4 * k, c & 0xFFFFFFFF)
    put_quaternion(img, env.slab + Q_OUT, rng)
    return [env.slab + Q_OUT, env.slab + VEC], img.pokes()


def gen_axis(env, n):
    img, rng = Image(), env.rng
    put_quaternion(img, env.slab + Q_OUT, rng)
    angle = rng.choice([0, 1, 0x200, 0x400, 0x800, 0xC00, 0xFFF, 0x1000, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(0, 0x1000), rng.getrandbits(32), rng.getrandbits(16)])
    return [env.slab + Q_OUT, angle], img.pokes()


for _address, _name in ((0x004A9820, "Camera_QuaternionAboutX"), (0x004A9870, "Camera_QuaternionAboutY"), (0x004A98C0, "Camera_QuaternionAboutZ")):
    cmq(_address, _name)(gen_axis)


@cmq(0x004A9910, "Camera_TransformToMatrixQ12")
def gen_to_matrix(env, n):
    img, rng = Image(), env.rng
    put_quaternion(img, env.slab + Q_A, rng, unit=rng.random() < 0.6)
    put_matrix(img, env.slab + MAT_A, rng)
    return [env.slab + Q_A, env.slab + MAT_A], img.pokes()


@cmq(0x004A9A00, "Camera_MatrixToTransformQ12")
def gen_from_matrix(env, n):
    img, rng = Image(), env.rng
    noise(img, rng)
    if rng.random() < 0.7:
        rotation_matrix(img, env.slab + MAT_A, rng)
    else:
        put_matrix(img, env.slab + MAT_A, rng)
    put_quaternion(img, env.slab + Q_OUT, rng)
    return [env.slab + MAT_A, env.slab + Q_OUT], img.pokes()


@cmq(0x004A9BF0, "Camera_SlerpTransformQ12")
def gen_slerp(env, n):
    img, rng = Image(), env.rng
    a = [rng.choice([0x5184D8, 0, rng.getrandbits(32)])]
    b = [rng.choice([0x5184D8, 0, rng.getrandbits(32)])]
    close = rng.random() < 0.3
    import math
    base = [rng.uniform(-1, 1) for _ in range(4)]
    length = math.sqrt(sum(c * c for c in base)) or 1.0
    unit_a = [c / length * 0x1000 for c in base]
    if close:
        unit_b = [c + rng.uniform(-8, 8) for c in unit_a]
    else:
        other = [rng.uniform(-1, 1) for _ in range(4)]
        length_b = math.sqrt(sum(c * c for c in other)) or 1.0
        unit_b = [c / length_b * 0x1000 for c in other]
    a += [int(c) & 0xFFFFFFFF for c in unit_a]
    b += [int(c) & 0xFFFFFFFF for c in unit_b]
    if rng.random() < 0.08:
        a[1:] = [component(rng) for _ in range(4)]
    if rng.random() < 0.08:
        b[1:] = [component(rng) for _ in range(4)]
    t = rng.choice([0, 0x1000, 0x800, 0x400, 0xC00, 1, 0xFFF, rng.randrange(0, 0x1001), rng.randrange(0, 0x1001), rng.randrange(-0x400, 0x1400)])
    put_quaternion(img, env.slab + Q_OUT, rng)
    return a + b + [t & 0xFFFFFFFF, env.slab + Q_OUT], img.pokes()


@cmq(0x004103B0, "Camera_AngleDelta12")
def gen_angle_delta(env, n):
    rng = env.rng
    v = lambda: rng.choice([0, 1, 0x7FF, 0x800, 0x801, 0xFFF, 0x1000, 0x1001, rng.randrange(0, 0x1000), rng.getrandbits(32), rng.getrandbits(16)])
    return [v(), v()], Image().pokes()
