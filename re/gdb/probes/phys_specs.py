"""Phase 4.15a probe: the small skater functions of physics.cpp (group phs).

They work on the live player-one skater of the probe's Warehouse run (the pointer at 0x0056a960, 0x3538 bytes, all of it captured), on a handful of globals and on small vectors in a scratch slab; the
animation player (0x00480730), the panel (0x0048d660), the sound (0x004ad620) and the netgame layer (0x004dece0, 0x00466c10) are stubs. A case pokes a random legal state into the fields the function reads;
what it does not poke stays what the live skater had.

Slab layout (offsets): vectors 0x000 / 0x010 / 0x020 (three dwords each), normals 0x040 (two dwords) / 0x050, the output vectors 0x060 / 0x070.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from opentony.memory import mem

GROUPS = ("phs",)

SKATER_POINTER = 0x0056A960
SKATER_BYTES = 0x3538
SLAB = 0x100
VEC_A = 0x000
VEC_B = 0x010
VEC_C = 0x020
NORMAL_A = 0x040
OUT_A = 0x060
OUT_B = 0x070

ANIMATION_CLOCK = 0x005685F4
ANIMATION_SCALE = 0x0056865C
GAME_MODE = 0x00533F38
CURRENT_LEVEL = 0x0056A898
TIMELESS = 0x0056A89C
COUNTS_UP = 0x029DA3A4
NET_FLAG = 0x006A3B21
NET_STATUS = 0x006A18BC
RUN_LOCKED = 0x00568658
END_CAUSE = 0x0056A8D0
PLAYER_ONE = 0x0056DB5C
LEVEL_RECORD = 0x0053C240
LEVEL_STRIDE = 0x1AC
LEVELS = (0, 1, 12, 13)               # the levels whose record flag the cases vary

WINDOWS = [(ANIMATION_CLOCK, 4), (ANIMATION_SCALE, 4), (GAME_MODE, 4), (CURRENT_LEVEL, 4), (TIMELESS, 4), (COUNTS_UP, 4), (NET_FLAG, 4), (NET_STATUS, 4), (RUN_LOCKED, 4), (END_CAUSE, 4),
           (PLAYER_ONE, 4)] + [(LEVEL_RECORD + LEVEL_STRIDE * level, 4) for level in LEVELS]


def skater():
    return mem.u32(SKATER_POINTER)


def phs_regions(slab):
    return merged([(slab, SLAB), (skater(), SKATER_BYTES)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = phs_regions
    READ_ONLY[_group] = []


def phs(address, name, via_this=True):
    return spec(address, name, "phs", this=(lambda env: skater()) if via_this else None)


# ---- the stubs ---------------------------------------------------------------------------------------------------------------------------------------------------------------

STATE = {"agree": 0}


def install_phys_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_phs_installed", False):
        return
    fc._phs_installed = True
    mark_under_test()
    ensure(fc, 0x0048D660, 2, "engine_0048d660")
    ensure(fc, 0x00480730, 4, "engine_00480730", convention="thiscall", this=True)
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x004DECE0, 0, "engine_004dece0", result=lambda a: STATE["agree"])
    ensure(fc, 0x00466C10, 0, "engine_00466c10")


GROUP_STUBS = {group: install_phys_stubs for group in GROUPS}


# ---- random words ---------------------------------------------------------------------------------------------------------------------------------------------------------------

def word(rng):
    return rng.choice([0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x10000), rng.randrange(0, 0x100)])


def short(rng):
    return rng.choice([0, 1, -1, 0x1000, -0x1000, 0x7FFF, -0x8000, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000)])


def put_shorts(img, address, values):
    for k, v in enumerate(values):
        img.u16(address + 2 * k, v & 0xFFFF)


def basis_word(rng):
    """An integer of the basis: mostly the sign extension of a short (what the game keeps there), sometimes any word."""
    if rng.random() < 0.8:
        v = short(rng)
        return v & 0xFFFFFFFF
    return rng.getrandbits(32)


def clock_world(img, rng):
    img.u32(ANIMATION_CLOCK, rng.choice([0, 1, 59, 60, 61, 1000, rng.randrange(0, 100000), rng.randrange(0, 1 << 31)]))
    img.u32(ANIMATION_SCALE, rng.choice([0, 0x100, 0x80, 0x200, 0x1000, rng.randrange(0, 0x2000), rng.getrandbits(32)]))


# ---- the functions -----------------------------------------------------------------------------------------------------------------------------------------------------------------

@phs(0x0048EB70, "Animation_PingPong", via_this=False)
def gen_ping_pong(env, n):
    img, rng = Image(), env.rng
    clock_world(img, rng)
    low = rng.choice([0, 1, -5, 10, rng.randrange(-100, 100)])
    high = low + rng.choice([1, 2, 3, 10, 29, rng.randrange(1, 200)])
    rate = rng.choice([1, 2, 3, 5, 6, 10, 12, 15, 20, 30, 60])
    return [low & 0xFFFFFFFF, high & 0xFFFFFFFF, rate], img.pokes()


@phs(0x0048F590, "Skater_RestoreSavedPose")
def gen_restore_pose(env, n):
    img, rng = Image(), env.rng
    sk = skater()
    img.put(sk + 0x30C8, bytes(rng.getrandbits(8) for _ in range(32)))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(32)))
    for k in range(3):
        img.u32(sk + 0x30E8 + 4 * k, word(rng))
        img.u32(sk + 8 + 4 * k, word(rng))
    return [], img.pokes()


@phs(0x0048F5D0, "Skater_SetPanelNumber")
def gen_panel_number(env, n):
    img, rng = Image(), env.rng
    return [rng.choice([0, 1, 2, rng.getrandbits(32)])], img.pokes()


STATES = [0, 1, 1, 2, 2, 3, 4, 5, 5, 6, 7, 8, 9]


@phs(0x004900B0, "CBruce__StateTransition")
def gen_state_transition(env, n):
    img, rng = Image(), env.rng
    sk = skater()
    old = rng.choice(STATES)
    new = rng.choice(STATES)
    if rng.random() < 0.3:
        old, new = 5, 1
    img.u32(sk + 0x30B8, old)
    img.u32(sk + 0x3044, rng.choice([0, 1, 5, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(sk + 0xD8, rng.getrandbits(32))
    img.u16(sk + 0xF6, rng.choice([0x23, 0x23, 0, 1, 0x82, rng.getrandbits(16)]))
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, basis_word(rng))
    for k in range(3):
        img.u32(sk + 0x3118 + 4 * k, word(rng))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    return [new, rng.getrandbits(32)], img.pokes()


@phs(0x004902E0, "CBruce__MaybeFlipAnimationTransition")
def gen_flip_transition(env, n):
    img, rng = Image(), env.rng
    sk = skater()
    img.u16(sk + 0xF6, rng.choice([0x82, 0x82, 0, rng.getrandbits(16)]))
    img.u16(sk + 0xF4, rng.choice([0, 0xF, 0x10, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)]))
    img.u32(sk + 0xD8, rng.getrandbits(32))
    img.u32(sk + 0x2DD0, word(rng))
    img.u32(sk + 0x2C78, rng.choice([0, 0, 1, rng.getrandbits(32)]))
    img.u32(sk + 0x2C7C, rng.choice([0, 0, 1, rng.getrandbits(32)]))
    img.u32(sk + 0x2C08, word(rng))
    for k in range(6):
        img.u32(sk + 0x30F4 + 4 * k, basis_word(rng))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    return [], img.pokes()


@phs(0x004904C0, "CBruce__ResetTrickBuffers")
def gen_reset_buffers(env, n):
    img, rng = Image(), env.rng
    img.u32(skater() + 0x2E80, word(rng))
    return [], img.pokes()


def put_normal(img, address, rng):
    nx, ny, nz = short(rng), short(rng), short(rng)
    return nx, ny, nz


@phs(0x00490610, "Skater_RemoveNormalComponent", via_this=False)
def gen_remove_normal(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    scale = rng.choice([0x100, 0x1000, 0x10000, 0x100000, 0x7FFFFFF])
    for k in range(3):
        img.u32(slab + VEC_A + 4 * k, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF)
    nx, ny, nz = short(rng), short(rng), short(rng)
    return [slab + VEC_A, ((ny & 0xFFFF) << 16) | (nx & 0xFFFF), (nz & 0xFFFF) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0)], img.pokes()


@phs(0x00490680, "Skater_ProjectOntoPlaneKeepLength", via_this=False)
def gen_damp_plane(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    scale = rng.choice([0x100, 0x1000, 0x10000, 0x40000, 0x100000])
    for k in range(3):
        img.u32(slab + VEC_A + 4 * k, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF)
    img.u32(slab + NORMAL_A, ((short(rng) & 0xFFFF) << 16) | (short(rng) & 0xFFFF))
    img.u32(slab + NORMAL_A + 4, (short(rng) & 0xFFFF) | (rng.getrandbits(16) << 16))
    return [slab + VEC_A, slab + NORMAL_A, slab + OUT_A], img.pokes()


@phs(0x0049C7D0, "CBruce__FRUVectorsFromRotMatrix")
def gen_fru(env, n):
    img, rng = Image(), env.rng
    sk = skater()
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, word(rng))
    return [], img.pokes()


@phs(0x0049C850, "Skater_PublishOrientationBasis")
def gen_publish(env, n):
    img, rng = Image(), env.rng
    sk = skater()
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, word(rng))
    return [], img.pokes()


@phs(0x0049E430, "CBruce__HandleFlips")
def gen_flips(env, n):
    img, rng = Image(), env.rng
    sk = skater()
    clock_world(img, rng)
    img.u32(sk + 0x2C88, rng.choice([0, 1, 1, 2, 3, 100, rng.getrandbits(32)]))
    img.u32(sk + 0x2C8C, rng.choice([0, 0xFFF, 0x1000, 0x7FF, 0xFFFFF000, rng.getrandbits(32)]))
    img.u32(sk + 0x2C90, rng.choice([0, 1, 0x100, 0x7FF, 0x800, 0x1000, 0x10000, rng.getrandbits(32)]))
    return [], img.pokes()


@phs(0x0049E480, "CBruce__HandleTimer")
def gen_timer(env, n):
    img, rng = Image(), env.rng
    sk = skater()
    clock_world(img, rng)
    img.u32(GAME_MODE, rng.choice([0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9]))
    level = rng.choice(LEVELS)
    img.u32(CURRENT_LEVEL, level)
    img.u32(LEVEL_RECORD + LEVEL_STRIDE * level, rng.choice([0, 1, 2, rng.getrandbits(8), rng.getrandbits(32)]))
    img.u32(TIMELESS, rng.choice([0, 0, 1, 5]))
    img.u8(COUNTS_UP, rng.choice([0, 0, 1]))
    img.u8(NET_FLAG, rng.choice([0, 0, 1]))
    img.u32(NET_STATUS, rng.choice([0, 0x80000, rng.getrandbits(32)]))
    img.u32(RUN_LOCKED, rng.choice([0, 0, 1]))
    img.u32(END_CAUSE, word(rng))
    img.u32(PLAYER_ONE, sk)
    img.u32(sk + 0x2CC8, rng.choice([0, 1, 100, 1000, 0x7FFFFFFF, 0x80000000, rng.randrange(0, 5000), rng.getrandbits(32)]))
    img.u32(sk + 0x30B8, rng.choice([0, 0, 1, 2, 5]))
    img.u32(sk + 0x2DD4, rng.choice([0, 0, 1]))
    STATE["agree"] = rng.choice([0, 1, 1, 0x100, 0x101])
    return [], img.pokes()


def animation_request(address, name, count):
    @phs(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        sk = skater()
        img.u16(sk + 0xF6, rng.choice([0x82, 0, rng.getrandbits(16)]))
        img.u16(sk + 0xF4, rng.choice([0, 0x10, 0x7FFF, 0x8000, rng.getrandbits(16)]))
        img.u32(sk + 0xD8, rng.getrandbits(32))
        img.u32(sk + 0x2C78, rng.choice([0, 1]))
        img.u32(sk + 0x2C7C, rng.choice([0, 1]))
        img.u32(sk + 0x108, word(rng))
        for k in range(6):
            img.u32(sk + 0x30F4 + 4 * k, basis_word(rng))
        return [rng.choice([0, 1, 5, 0x123, rng.getrandbits(32)]) for _ in range(count)], img.pokes()

    return generate


animation_request(0x004903F0, "Animation_RequestStart", 2)       # (each takes one argument more than it uses: ret 8 / 0xc / 0x10 / 0x14)
animation_request(0x00490420, "Animation_RequestStartDefaultEnd", 3)
animation_request(0x00490450, "Animation_RequestRange", 4)
animation_request(0x00490480, "Animation_RequestFull", 5)
