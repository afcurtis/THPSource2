"""Phase 4.15b probe: the camera modes and the camera's orientation code (group cmm): the follow, tripod, point and death modes, the competition intro, the viewport commit, the reset, the rigs' update and
blend, the debug pad and the smoothing at the end of the camera's frame.

A fake camera (0x700 bytes, all captured) and two fake skaters (the camera's target and a rig's owner; only the fields the code reads are captured) live in the slab, with a fake vtable whose slot 3 is an
unreached retail function (the camera's virtual update, stubbed). The GTE register window, the viewport records, the camera globals (distance, pitch, shake, tuning), the level records and the pad bytes are
captured too. Camera_ApplyEffects (the 4.3 model), the collision queries, the object update, the competition screens, the line drawer and sprintf are stubs; the quaternion, vector and GTE math runs as retail code.

Slab layout (offsets): camera 0x000, vtable 0x700, rigs 0x740 / 0x7e0 / 0x880, target skater 0x940, rig owner 0x3b40 (0x3200 bytes each, windows only).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("cmm", "cma")

CAM = 0x000
CAM_BYTES = 0x700
VTABLE = 0x700
RIG_A = 0x740
RIG_B = 0x7E0
RIG_OUT = 0x880
TRIPOD = 0x940
OWNER = 0x3B40
SLAB = 0x6D40

VIEWPORT = 0x6D40        # a fake viewport record (Camera_Update writes the field of view at +0xe)
SKATER_WINDOWS = [(0x08, 0x10), (0x4C, 0x0C), (0x118, 0x20), (0x2C68, 4), (0x2CCC, 4), (0x2DDC, 4), (0x2E58, 0x20), (0x2F64, 4), (0x3058, 4), (0x30B8, 4), (0x30F4, 0x30), (0x31EC, 4)]
GTE = 0x006A3E00
GLOBALS = [(GTE, 0x120), (0x0055F7CC, 0x148), (0x0055F940, 0x30), (0x0055FA30, 0x20), (0x0055FA40, 8), (0x00524A40, 0x68), (0x0056A858, 8), (0x0056A898, 4), (0x0056A954, 4), (0x0056A86C, 4),
           (0x00567C74, 4), (0x0056865C, 4), (0x0056B028, 4), (0x0056B160, 0xC0), (0x0053C1F8, 0x1AC * 3 + 0x80), (0x0055FF1C, 0x90), (0x0055FA00, 0x30),
           (0x0053757C, 0x10),       # (the axis table {1, 2, 0}: read-only)
           # Camera_Update: the shake words, the debug camera's saved values, the pad bytes, the frame clock, the freeze flag, the current viewport, the pending movie, the assertion location
           (0x0055FA50, 4), (0x0055F910, 4), (0x0055F978, 4), (0x0055F9A4, 4), (0x0056A8D4, 4), (0x0056A8E0, 4), (0x0056A900, 4), (0x00561C04, 4), (0x00563A38, 4), (0x0056B7F0, 4),
           (0x0056AFF8, 0x270), (0x00568638, 4), (0x0056881C, 0x40)]


def cmm_regions(slab, extra_skater=(), extra_globals=()):
    windows = [(slab + CAM, CAM_BYTES), (slab + VTABLE, 0x20), (slab + RIG_A, 0x140), (slab + RIG_OUT, 0xA0), (slab + VIEWPORT, 0x20)]
    for base in (TRIPOD, OWNER):
        windows += [(slab + base + off, size) for off, size in (*SKATER_WINDOWS, *extra_skater)]
    windows += [*GLOBALS, *extra_globals]
    return merged(windows, gap=0)


def cma_regions(slab):
    """Camera_ApplyEffects (group cma) also reads the tripod's lean word and two level words."""
    return cmm_regions(slab, extra_skater=[(0x30C4, 4)], extra_globals=[(0x00568658, 4), (0x005614C4, 4)])


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = cma_regions if _group == "cma" else cmm_regions
    READ_ONLY[_group] = [(0x0053757C, 0x10)]


def cmm(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "cmm", this=resolve)


def cma(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "cma", this=resolve)


def install_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_cmm_installed", False):
        return
    fc._cmm_installed = True
    mark_under_test()
    stop_clock(fc)
    rng = env.rng
    scratch = 0x7FF00800 + 0xA00
    ensure(fc, 0x0040C370, 0, "engine_0040c370", convention="thiscall", this=True)
    ensure(fc, 0x004AD9B0, 0, "engine_004ad9b0", convention="thiscall", this=True)
    ensure(fc, 0x00480FF0, 1, "engine_00480ff0")
    ensure(fc, 0x0041B8B0, 0, "engine_0041b8b0", convention="thiscall", this=True)
    ensure(fc, 0x00419600, 0, "engine_00419600", convention="thiscall", this=True)
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=scratch + 0x340)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=scratch + 0x340, pokes=[(0, 4, lambda a: rng.choice([0, 0, 1, 0x1234]), 0x68)])
    ensure(fc, 0x0048B620, 8, "engine_0048b620")
    ensure(fc, 0x005002A1, 5, "engine_005002a1", fill=(0, lambda a: bytes(rng.choice(b"abcdefgh 0123-") for _ in range(15)) + b"\0"))
    # the camera constructor: the base object's constructor, the camera list's add and the log line for a pad that is neither of the two
    ensure(fc, 0x00480060, 0, "engine_00480060", convention="thiscall", this=True)
    ensure(fc, 0x004801D0, 1, "engine_004801d0", convention="thiscall", this=True)
    ensure(fc, 0x004CF850, 1, "printf_log")
    ensure(fc, 0x004CDE70, 0, "engine_004cde70", result=lambda a: rng.choice([0, 0, 1]))     # Video_Playing (Camera_Update)


def install_stubs_cma(fc, env):
    """Camera_ApplyEffects is the function under test (the group's own stub of it stays off): the collision query answers with a hit body, a line length and a hit distance."""
    install_stubs(fc, env)
    if getattr(fc, "_cma_installed", False):
        return
    fc._cma_installed = True
    rng = env.rng
    scratch = 0x7FF00800 + 0xA00
    last = [0]

    def body(a):
        return rng.choice([0, 1, 0x1234, 1, 0x1234, 0])

    def length(a):
        last[0] = rng.choice([0, 1, 0x40, 0x100, 0x400, 0x1000, rng.randrange(1, 0x4000), rng.randrange(1, 0x4000)])
        return last[0]

    def distance(a):
        n = last[0]
        return rng.choice([0, 1, n, n // 2, n + 1, rng.randrange(0, 2 * n + 2), rng.randrange(0, 0x400), 0x77, 0x78, 0x79, rng.randrange(-8, 8)]) & 0xFFFFFFFF

    # (the record's hit body at +0x68, the line length at +0x44 and the hit distance at +0x40; the length comes first so the distance can follow it)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=scratch + 0x340, blob=(0, 0x18), pokes=[(0, 4, body, 0x68), (0, 4, length, 0x44), (0, 4, distance, 0x40)])     # (the blob: the line the ray runs along, start and end)


GROUP_STUBS = {"cmm": install_stubs, "cma": install_stubs_cma}


def word(rng):
    return rng.choice([0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x10000), rng.randrange(0, 0x100)])


def q12(rng, limit=0x400000):
    return rng.choice([0, 0x1000, -0x1000, rng.randrange(-limit, limit), rng.randrange(-limit, limit), rng.randrange(-0x20000, 0x20000)]) & 0xFFFFFFFF


def put_vector(img, address, rng, limit=0x400000):
    for k in range(3):
        img.u32(address + 4 * k, q12(rng, limit))


def put_quaternion(img, address, rng):
    """A quaternion object (vtable, x, y, z, w): near unit length mostly."""
    import math

    img.u32(address, 0x5184D8)
    if rng.random() < 0.8:
        v = [rng.uniform(-1, 1) for _ in range(4)]
        length = math.sqrt(sum(c * c for c in v)) or 1.0
        for k, c in enumerate(v):
            img.u32(address + 4 + 4 * k, int(c / length * 0x1000) & 0xFFFFFFFF)
    else:
        for k in range(4):
            img.u32(address + 4 + 4 * k, q12(rng, 0x2000))


def put_matrix(img, address, rng):
    import math

    v = [rng.uniform(-1, 1) for _ in range(4)]
    length = math.sqrt(sum(c * c for c in v)) or 1.0
    x, y, z, w = (c / length for c in v)
    m = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 2 * (x * z + y * w), 2 * (y * z - x * w),
         1 - 2 * (x * x + y * y)]
    for k, c in enumerate(m):
        img.u16(address + 2 * k, int(round(c * 0x1000)) & 0xFFFF)
    for k in range(9, 16):
        img.u16(address + 2 * k, rng.getrandbits(16))


def skater(img, env, rng, base, state=None):
    sk = env.slab + base
    put_vector(img, sk + 0x08, rng)
    put_vector(img, sk + 0x4C, rng, 0x8000)
    put_matrix(img, sk + 0x118, rng)
    put_matrix(img, sk + 0x2E58, rng)
    img.u32(sk + 0x2C68, rng.choice([0, 0, 1, word(rng)]))
    img.u32(sk + 0x2DDC, rng.choice([0, 1, word(rng)]))
    img.u32(sk + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x3058, word(rng))
    img.u32(sk + 0x30B8, state if state is not None else rng.choice([0, 1, 2, 3, 4, 5, 8, 9, rng.randrange(0, 16)]))
    # the basis: forward / right / up as roughly unit vectors in Q12
    import math

    for base_offset in (0x30F4, 0x3100, 0x310C):
        v = [rng.uniform(-1, 1) for _ in range(3)]
        length = math.sqrt(sum(c * c for c in v)) or 1.0
        for k, c in enumerate(v):
            img.u32(sk + base_offset + 4 * k, int(c / length * 0x1000) & 0xFFFFFFFF)
    return sk


def camera(img, env, rng, tripod=True, mode=None):
    cam = env.slab + CAM
    img.put(cam, bytes(rng.getrandbits(8) for _ in range(CAM_BYTES)))
    img.u32(cam, env.slab + VTABLE)
    for slot in range(8):
        img.u32(env.slab + VTABLE + 4 * slot, 0x4AD9B0)
    img.u32(cam + 0x3A4, env.slab + TRIPOD if tripod else 0)
    for base in (0x3B0, 0x3C0, 0x3F0, 0x5B8, 0x5C4, 0x610, 0x574, 0x594, 0x564, 0x63C, 0x08):
        put_vector(img, cam + base, rng, rng.choice([0x1000, 0x100000, 0x400000]))
    for base in (0x444, 0x458, 0x46C, 0x480, 0x494, 0x580, 0x5A0, 0x64C):
        put_quaternion(img, cam + base, rng)
    for off in (0x3AC, 0x3BC, 0x418, 0x4A8, 0x4A9, 0x560, 0x5D4):
        img.u8(cam + off, rng.choice([0, 0, 1]))
    img.u32(cam + 0x504, mode if mode is not None else rng.choice([0x17, 0x19, 1, 2, 5, rng.randrange(0, 40)]))
    img.u32(cam + 0x510, rng.choice([0, 3, 0xB, 0xC, 0x10, rng.randrange(0, 40)]))
    img.u32(cam + 0x55C, rng.choice([0, 1, 5, 0x40, 0x81, 0x82, 0x83, rng.randrange(0, 0x90)]))
    img.u32(cam + 0x570, rng.choice([0, 1, 5, 0x1D, 0x1E, 0x1F, rng.randrange(0, 0x28)]))
    img.u32(cam + 0x5D0, rng.choice([0, 100, rng.randrange(-0x1000, 0x10000) & 0xFFFFFFFF]))
    img.u16(cam + 0x5B4, rng.getrandbits(16))
    img.u32(cam + 0x5D8, rng.choice([0, 0, 1, 5, 7, 8, 20, 0xFFFFFFFF, rng.randrange(0, 40)]))
    img.u32(cam + 0x5DC, rng.choice([0, 1, 5, rng.randrange(0, 12)]))
    img.u32(cam + 0x5E0, rng.choice([0, 1, 10, 0x1E, 0x1F, 100, rng.randrange(0, 0x40)]))
    img.u32(cam + 0x5E4, rng.randrange(0, 30))
    img.u32(cam + 0x5E8, rng.choice([0, 1, 2, 10, 0x13, 0x14, 50, rng.randrange(0, 100)]))
    img.u32(cam + 0x60C, rng.choice([0, 1, 3, 4, 5, rng.randrange(0, 10)]))
    img.u32(cam + 0x670, rng.choice([0, 0, 1, 5, 0x14, rng.randrange(0, 30)]))
    for k in range(6):
        img.u32(cam + 0x620 + 4 * k, rng.choice([0, 100, 5000, rng.randrange(0, 40000)]))
    img.u16(cam + 0xC8, rng.choice([0, 1]))


def world(img, env, rng):
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(0x120)))
    img.u32(0x0056A858, env.slab + TRIPOD if rng.random() < 0.85 else env.slab + OWNER)
    img.u32(0x0056A85C, env.slab + OWNER if rng.random() < 0.6 else 0)
    img.u32(0x0056A898, rng.choice([0, 1, 2]))
    img.u32(0x0056A954, env.slab + TRIPOD if rng.random() < 0.5 else 0)
    img.u32(0x0056A86C, rng.choice([0, 0, 1]))
    img.u32(0x0055FA3C, rng.choice([0, 1]))
    img.u32(0x0055FA30, rng.choice([0, 0, 0, 1]))
    img.u32(0x00524A98, rng.choice([0x100, 0x200, rng.randrange(-0x400, 0x400) & 0xFFFFFFFF]))
    img.u32(0x0056865C, rng.choice([0x100, 0x80, 0x200, rng.randrange(0, 0x400)]))
    img.u32(0x0056B028, rng.choice([0, 1, 0x100]))
    img.put(0x0055F940, bytes(rng.getrandbits(8) for _ in range(0x30)))
    for k in range(3):
        img.u32(0x0055F948 + 4 * k, q12(rng, 0x10000))
    img.put(0x0053C1F8, bytes(rng.getrandbits(8) for _ in range(0x1AC * 3)))
    for level in range(3):
        for k in range(3):
            img.u32(0x0053C240 + level * 0x1AC + 0x54 + 4 * k, rng.randrange(-300, 300) & 0xFFFFFFFF)
        for k in range(3):
            img.u16(0x0053C240 + level * 0x1AC + 0x60 + 2 * k, rng.getrandbits(16))
        img.u32(0x0053C238 + level * 0x1AC, 0x0053C1F8)       # a pointer to a string in the window
    img.u16(0x0055FA42, rng.randrange(0, 12))
    img.u32(0x0055FA44, rng.choice([0, 1, 0, 1, 2, rng.getrandbits(32)]))
    img.u8(0x0055FA40, rng.choice([0, 1]))
    img.u32(0x0055FF1C, rng.choice([0, 0, 0x100, 0x1, 0x205, rng.getrandbits(16)]))
    img.u32(0x0055FF8C, word(rng))
    img.u32(0x0055FF3C, word(rng))


@cmm(0x00410610, "Camera_FollowTarget", CAM)
def gen_follow(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng, tripod=rng.random() < 0.93)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    return [], img.pokes()


@cmm(0x004113F0, "Camera_Mode2", CAM)
def gen_mode2(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng, tripod=rng.random() < 0.9)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    if rng.random() < 0.25:
        # a camera whose look-at point has not moved keeps its direction (the else branch of the direction update)
        for k in range(3):
            value = q12(rng)
            img.u32(env.slab + CAM + 0x3C0 + 4 * k, value)
            img.u32(env.slab + CAM + 0x3B0 + 4 * k, value)
    return [], img.pokes()


@cmm(0x004103E0, "Camera_BuildTripodRotation", CAM)
def gen_tripod_rotation(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    return [], img.pokes()


@cmm(0x0040B650, "SkaterCamera_Construct", CAM)
def gen_construct(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng)                    # (a random block: the constructor sets everything the other functions read)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    img.u32(env.slab + TRIPOD + 0x2CCC, rng.choice([0x56AFF8, 0x56AFF8, 0x56B164, 0x56B164, rng.getrandbits(32)]))     # the pad the skater listens to
    return [env.slab + TRIPOD, rng.choice([0, 1, 2, rng.getrandbits(32)])], img.pokes()


@cmm(0x0040C1A0, "CCamera__ResetOrientation", CAM)
def gen_reset_orientation(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng, tripod=rng.random() < 0.8)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    img.u32(0x0056A954, env.slab + TRIPOD)      # (a camera with no target and no query owner copies an uninitialised stack vector into itself: retail leftovers, not replayable)
    return [], img.pokes()


@cmm(0x0040BE70, "Camera_CommitViewportEffects", CAM)
def gen_commit_viewport(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng, tripod=rng.random() < 0.85)
    skater(img, env, rng, TRIPOD, state=rng.choice([5, 5, 0, 1, 2]))
    world(img, env, rng)
    return [], img.pokes()


@cmm(0x00410C40, "Camera_ResetAndSettle", CAM)
def gen_reset_settle(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng, tripod=rng.random() < 0.85)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    img.u32(0x0056A954, env.slab + TRIPOD)      # (as for the orientation reset it starts with)
    return [], img.pokes()


@cmm(0x00410C90, "Camera_DeathMode", CAM)
def gen_death(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    return [], img.pokes()


@cmm(0x00410F70, "Camera_PointMode", CAM)
def gen_point(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    img.u32(0x0056A858, env.slab + TRIPOD)
    return [], img.pokes()


@cmm(0x00410BB0, "Camera_StartCompetitionIntro", CAM)
def gen_competition_intro(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng)
    world(img, env, rng)
    return [], img.pokes()


@cmm(0x0040F200, "Camera_DrawDebugBars")
def gen_debug_bars(env, n):
    img, rng = Image(), env.rng
    world(img, env, rng)
    return [], img.pokes()


def fill_rig(img, env, rng, base, owner=OWNER):
    rig = env.slab + base
    img.put(rig, bytes(rng.getrandbits(8) for _ in range(0xA0)))
    img.u32(rig, 0x5184DC)
    for q in (0x1C, 0x40, 0x74):
        put_quaternion(img, rig + q, rng)
    img.u8(rig + 4, rng.choice([0, 1]))
    img.u8(rig + 0x18, rng.choice([0, 1]))
    img.u32(rig + 8, env.slab + owner)
    img.u32(rig + 0x64, env.slab + owner if rng.random() < 0.5 else 0)
    put_vector(img, rig + 0x0C, rng)
    put_vector(img, rig + 0x30, rng, 0x100000)
    img.u16(rig + 0x3C, rng.choice([0, 100, 0x7FFF, 0x8000, rng.getrandbits(16)]))
    return rig


@cmm(0x00411A00, "CameraRig_Update", RIG_A)
def gen_rig_update(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    skater(img, env, rng, OWNER)
    world(img, env, rng)
    return [], img.pokes()


@cmm(0x00411C00, "CameraRig_BuildMatrix", RIG_A)
def gen_rig_matrix(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    world(img, env, rng)
    return [], img.pokes()


@cmm(0x00411CE0, "CameraRig_Blend")
def gen_rig_blend(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    fill_rig(img, env, rng, RIG_B)
    img.put(env.slab + RIG_OUT, bytes(rng.getrandbits(8) for _ in range(0xA0)))
    skater(img, env, rng, OWNER)
    world(img, env, rng)
    t = rng.choice([0, 1, 0x800, 0xFFF, 0x400, 0xC00, rng.randrange(0, 0x1000), rng.randrange(0, 0x1000), 0x1000, 0x2000])
    return [env.slab + RIG_A, env.slab + RIG_B, t, env.slab + RIG_OUT], img.pokes()


@cmm(0x0040E090, "Camera_SmoothAndValidate", CAM)
def gen_smooth(env, n):
    img, rng = Image(), env.rng
    has_target = rng.random() < 0.9
    camera(img, env, rng, tripod=has_target)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    cam = env.slab + CAM
    if has_target:
        if rng.random() < 0.3:
            # a fast target (over 100 units a frame): the speed is clamped
            put_vector(img, env.slab + TRIPOD + 0x4C, rng, 0x400000)
    else:
        # no target: the camera settles on the line to its look target; straight up or down takes the other basis, and a look target on top of the look-at point has no direction at all
        roll = rng.random()
        if roll < 0.4:
            for k, delta in enumerate((rng.randrange(-0x40, 0x40), rng.choice([-1, 1]) * rng.randrange(0x10000, 0x100000), rng.randrange(-0x40, 0x40))):
                base = rng.getrandbits(32)
                img.u32(cam + 0x3C0 + 4 * k, base)
                img.u32(cam + 0x3F0 + 4 * k, (base + delta) & 0xFFFFFFFF)
        elif roll < 0.55:
            for k in range(3):
                value = q12(rng)
                img.u32(cam + 0x3C0 + 4 * k, value)
                img.u32(cam + 0x3F0 + 4 * k, value)
    return [rng.choice([0, 1, 0x100, 0x400, 0x800, 0xFFF, rng.randrange(0, 0x1000), rng.getrandbits(32)])], img.pokes()


@cmm(0x0040F270, "Camera_HandlePad", CAM)
def gen_pad(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng)
    world(img, env, rng)
    for address in (0x56B164, 0x56B184, 0x56B194, 0x56B174, 0x56B1C4, 0x56B1D4, 0x56B1E5, 0x56B1F5, 0x56B205, 0x56B215):
        img.u8(address, rng.choice([0, 0, 1]))
    view = rng.choice([0, 1, 0, 1, 0, 1, 2, 3])
    img.u32(0x0055FA44, view)
    img.u16(0x0055FA42, rng.choice([0, 1, 2, 3, 4, 5, 5, 5, 6, 6, 7, 8, 9, 10, 11]))
    if view >= 2:
        # a view number that picks no rig leaves retail with a stale register: the buttons that would use it must be off
        for address in (0x56B164, 0x56B184, 0x56B194, 0x56B174, 0x56B1C4, 0x56B1D4):
            img.u8(address, 0)
    return [], img.pokes()


UPDATE_KEYS = (0x56AFF8, 0x56B008, 0x56B018, 0x56B174, 0x56B184, 0x56B1E4, 0x56B1F4, 0x56B204, 0x56B214, 0x56B1A4, 0x56B1B4, 0x56B244, 0x56B254)


def small16(rng):
    return rng.choice([0, 0, 1, 0xFFFF, 5, 0xFFFB, 0x7FFF, 0x8000, 0x40, 0xFFC0, rng.getrandbits(16)])


@cmm(0x0040F850, "Camera_Update", CAM)
def gen_update(env, n):
    img, rng = Image(), env.rng
    has_target = rng.random() < 0.92
    mode = rng.choice([1, 1, 1, 2, 0x17, 0x18, 0x19, 0x19, 5, 0, 0x1A, rng.randrange(0, 40)])
    if not has_target and mode in (0x17, 0x18, 0x19):
        mode = rng.choice([1, 2, 5])        # (these modes read the target without a check: retail faults)
    camera(img, env, rng, tripod=has_target, mode=mode)
    skater(img, env, rng, TRIPOD)
    skater(img, env, rng, OWNER)
    world(img, env, rng)
    cam = env.slab + CAM
    img.u32(0x0056A858, env.slab + TRIPOD)
    img.u32(cam + 0x3DC, env.slab + rng.choice([TRIPOD, OWNER]))
    img.u8(cam + 0x3E0, rng.choice([0, 1]))
    if not has_target:
        img.u8(cam + 0x3AC, 0)
        img.u32(0x0056B7F0, 0)          # (a movie pending with no target copies the position of a null skater: retail faults)
    else:
        img.u32(0x0056B7F0, rng.choice([0, 0, 1]))
    img.u32(cam + 0x40C, rng.choice([0x100, 0x80, rng.getrandbits(32), rng.randrange(0, 0x400)]))
    img.u16(cam + 0x410, rng.choice([0, 0, 1, 5, small16(rng)]))
    img.u32(cam + 0x414, rng.choice([0, 1, 0xFFFFFFFF, rng.randrange(0, 0x40)]))
    img.u16(cam + 0xC8, rng.choice([0, 1]))
    img.u16(cam + 0x434, rng.choice([0, 0, 5, 0x40, 0xFFF0, small16(rng)]))
    img.u16(cam + 0x436, rng.choice([0, 0, 3, 0x40, 0xFFF0, small16(rng)]))
    for k in range(3):
        img.u16(cam + 0x4F2 + 2 * k, rng.choice([0, 0, 0, small16(rng), rng.randrange(0, 0x40), (0x10000 - rng.randrange(1, 0x40)) & 0xFFFF]))
        img.u8(cam + 0x4F8 + k, rng.choice([0, 1, 4, 0x20, rng.getrandbits(8)]))
        img.u16(cam + 0x4FC + 2 * k, rng.getrandbits(16))
    img.u32(0x0056A8D4, rng.choice([0, 1, 2, 7, rng.getrandbits(32), rng.randrange(0, 0x100)]))
    img.u32(cam + 0x61C, rng.choice([0, 1, 3, 4, 5, 9]))
    img.u32(cam + 0x5EC, rng.choice([0, 100, rng.getrandbits(32)]))
    img.u32(cam + 0x5F0, rng.choice([0, 1, 0xFFFFFFFF, 5, rng.randrange(-9, 9) & 0xFFFFFFFF]))
    img.u16(env.slab + TRIPOD + 0x31EC, rng.choice([0, 1, 3]))
    # a look-at point that has not moved (the smoothing step 0x97)
    if rng.random() < 0.25:
        img.u8(cam + 0x3AC, 0)
        for k in range(3):
            value = q12(rng)
            img.u32(cam + 0x3C0 + 4 * k, value)
            img.u32(cam + 0x3B0 + 4 * k, value)
    # the debug camera: its enable flag, the chord, the free-fly state and the pad bytes
    img.u32(0x0056A900, rng.choice([0, 1, 1]))
    img.u8(0x0056B0E8, rng.choice([0, 1, 1]))
    img.u32(0x0055FA48, rng.choice([0, 0, 1]))
    img.u32(0x0055FA4C, rng.choice([0, 1]))
    img.u32(0x0056A8E0, rng.choice([0, 0, 0, 0, 1]))
    img.u32(0x00561C04, rng.choice([0, 0, 0, 0, 0, 0, 1]))
    for address in UPDATE_KEYS:
        img.u8(address, rng.choice([0, 0, 0, 1]))
    for address in (0x56B1C5, 0x56B1D5, 0x56B049, 0x56B0E9, 0x56B0D9):
        img.u8(address, rng.choice([0, 1]))
    for address in (0x55F978, 0x55F9A4, 0x55F910, 0x524AA4, 0x524AA0):
        img.u32(address, rng.choice([0, 0x100, rng.getrandbits(32), rng.randrange(0, 0x1000)]))
    for address in (0x524A40, 0x524A44, 0x524A48):
        img.u32(address, rng.choice([0, 0x100, 0xFFFFFF00, rng.getrandbits(32), rng.randrange(0, 0x2000)]))
    img.put(env.slab + VIEWPORT, bytes(rng.getrandbits(8) for _ in range(0x20)))
    img.u32(0x00563A38, env.slab + VIEWPORT)
    return [], img.pokes()


@cma(0x0040C370, "Camera_ApplyEffects", CAM)
def gen_apply_effects(env, n):
    img, rng = Image(), env.rng
    camera(img, env, rng, tripod=True)
    skater(img, env, rng, TRIPOD)
    world(img, env, rng)
    cam = env.slab + CAM
    sk = env.slab + TRIPOD
    # the gates: frozen, debug and query-owner words are mostly clear, the tick mostly past 12
    img.u32(0x00561C04, rng.choice([0] * 40 + [1]))
    img.u32(0x0056A8E0, rng.choice([0] * 40 + [1]))
    img.u32(0x0056A86C, rng.choice([0] * 40 + [1]))
    img.u32(cam + 0x510, rng.choice([0xD, 0x10, 0x20, rng.randrange(0xD, 0x100), rng.randrange(0xD, 0x100), rng.randrange(0xD, 0x100), rng.randrange(0xD, 0x100), rng.randrange(0xD, 0x100), rng.randrange(0xD, 0x100), 0xC, 0]))
    # the phase byte (stored value; the function adds one and wraps past two to zero), the cycle count whose low bit picks the ray layout
    img.u8(cam + 0x550, rng.choice([0, 1, 2, 0, 1, 2, 0, 1, 2, 3, 0xFF, 0xFE, rng.getrandbits(8)]))
    img.u32(cam + 0x554, rng.choice([0, 1, 2, 3, rng.getrandbits(32)]))
    img.u32(cam + 0x4D4, rng.choice([0, 1, 1, 1, rng.getrandbits(32)]))
    img.u16(cam + 0x4DC, rng.choice([0, 0x100, 0x400, 0x1000, 0xFFF0, small16(rng)]))
    img.u16(cam + 0x4E0, rng.choice([0, 1, 1, 0x40, small16(rng)]))
    img.u32(cam + 0x5D8, rng.choice([0, 0, 0, 0, 1, 5, 0xFFFFFFFF, 0xFFFFFFFE, rng.getrandbits(32)]))
    img.u32(cam + 0x61C, rng.choice([0, 1, 3, 4, 5, 9, 0x20, rng.getrandbits(32)]))
    img.u32(cam + 0x558, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 5, 0x100, 0x77, rng.getrandbits(32)]))
    for off in (0x4A8, 0x4A9):
        img.u8(cam + off, rng.choice([0, 1, 1]))
    # the lean shorts (and their values of the last call): mostly small, so the half sums are zero now and then
    for off in range(0x530, 0x54A, 2):
        img.u16(cam + off, rng.choice([0, 0, 0, small16(rng), small16(rng), rng.randrange(0, 0x40), (0x10000 - rng.randrange(1, 0x40)) & 0xFFFF]))
    if rng.random() < 0.3:
        for k in range(3):
            value = q12(rng)
            img.u32(cam + 0x3C0 + 4 * k, value)
            img.u32(cam + 0x3B0 + 4 * k, value)
    img.u32(sk + 0x30C4, rng.choice([0, 0, 0, 0, 0, 1, 2, rng.getrandbits(32)]))
    img.u32(0x00568658, rng.choice([0, 0, 0, 1]))
    img.u8(0x005614C4, rng.choice([0, 0, 1]))
    img.u32(0x00567C74, rng.choice([0, 0, 1]))      # (the lean's ray flag: the first ray leaves it alone, the others set and clear it)
    return [], img.pokes()
