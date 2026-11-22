"""Phase 4.15a probe: the bail, the vibration and the pad history of physics.cpp (group pht).

Like phys_specs.py: the live player-one skater (all of it captured), a few globals and a scratch slab; the crowd (0x0042f680 / 0x0042f700), the panel (0x0048d7b0, and the landed value 0x0048cd40 of its
data), the rumble (0x004896a0), the movie test (0x004cde70), the netgame queries (0x004de010 / 0x004de4d0) and the trick script interpreter (0x004be450) are stubs. `TrickScript_Start`, the
speed (`Fixed_DotProductQ12` and the integer root) and the callees inside physics.cpp (the state change, `Trick_Bail`, `Vibrate`, `Bail`) are the real code.

Slab layout (offsets): the grind trick offsets (shorts) at 0x000 (0xe0 entries), the state script offsets at 0x1c0 (16 entries).
"""

from __future__ import annotations

import phys_specs as ps
from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("pht",)

SLAB = 0x200
GRIND = 0x000
STATES = 0x1C0

SECOND_PLAYER_MASK = 0x0056A854
VIBRATION = 0x0056A3E0
BAIL_LOCKED = 0x0056A890
TRICKS_BINARY = 0x0056A894
GRIND_OFFSETS = 0x0056A840
STATE_OFFSETS = 0x005685F0

WINDOWS = ps.WINDOWS + [(SECOND_PLAYER_MASK, 4), (VIBRATION, 8), (BAIL_LOCKED, 4), (TRICKS_BINARY, 4), (GRIND_OFFSETS, 4), (STATE_OFFSETS, 4)]

STATE = {"landed": 0, "movie": 0, "remote": 0}


def pht_regions(slab):
    return merged([(slab, SLAB), (ps.skater(), ps.SKATER_BYTES)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = pht_regions
    READ_ONLY[_group] = []


def pht(address, name):
    return spec(address, name, "pht", this=lambda env: ps.skater())


def install_trick_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_pht_installed", False):
        return
    fc._pht_installed = True
    mark_under_test()
    ensure(fc, 0x0042F700, 1, "engine_0042f700")
    ensure(fc, 0x0042F680, 1, "engine_0042f680")
    ensure(fc, 0x0048CD40, 0, "engine_0048cd40", convention="thiscall", this=True, result=lambda a: STATE["landed"])
    ensure(fc, 0x0048D7B0, 1, "engine_0048d7b0")
    ensure(fc, 0x004CDE70, 0, "engine_004cde70", result=lambda a: STATE["movie"])
    ensure(fc, 0x004896A0, 4, "engine_004896a0")
    ensure(fc, 0x004DE010, 1, "engine_004de010", result=lambda a: STATE["remote"])
    ensure(fc, 0x004DE4D0, 2, "engine_004de4d0")
    ensure(fc, 0x004BE450, 0, "engine_004be450", convention="thiscall", this=True)


GROUP_STUBS = {group: install_trick_stubs for group in GROUPS}


def tables(img, env, rng):
    """The script offset tables (shorts) the bail and the grind start read, and the pointers to them."""
    slab = env.slab
    for k in range(0xE0):
        img.u16(slab + GRIND + 2 * k, ps.short(rng) & 0xFFFF)
    for k in range(16):
        img.u16(slab + STATES + 2 * k, ps.short(rng) & 0xFFFF)
    img.u32(GRIND_OFFSETS, slab + GRIND)
    img.u32(STATE_OFFSETS, slab + STATES)
    img.u32(TRICKS_BINARY, rng.choice([0x01000000, 0x12345678, 0xFFFFF000, rng.getrandbits(32)]))


def movement(img, rng, sk):
    scale = rng.choice([0, 0x100, 0x1000, 0x10000, 0x20000, 0x40000, 0x100000])
    for k in range(3):
        img.u32(sk + 0x4C + 4 * k, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF if scale else 0)


@pht(0x0048FB20, "CBruce__Trick_Bail")
def gen_trick_bail(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    img.u32(ps.GAME_MODE, rng.choice([0, 1, 7, 7, 9]))
    img.u32(sk + 0x2CC8, ps.word(rng))
    for field in (0x2D88, 0x3064, 0x306C, 0x3068, 0x3014, 0x3018, 0x2C88, 0x2C84, 0x2C80, 0x2F6C, 0x2F70, 0x2C70, 0x2C74):
        img.u32(sk + field, ps.word(rng))
    movement(img, rng, sk)
    img.u32(sk + 0x2CC4, rng.choice([0, 1]))
    img.u32(SECOND_PLAYER_MASK, rng.choice([0, 1]))
    img.u32(VIBRATION, rng.choice([0, 1, 2]))
    img.u32(VIBRATION + 4, rng.choice([0, 1, 2]))
    STATE["landed"] = rng.choice([0, 0, 1, 500, rng.getrandbits(32)])
    STATE["movie"] = rng.choice([0, 0, 1])
    return [], img.pokes()


@pht(0x0048FFC0, "CBruce__Vibrate")
def gen_vibrate(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    img.u32(ps.GAME_MODE, rng.choice([0, 1, 7, 7, 9]))
    img.u32(sk + 0x2CC4, rng.choice([0, 1]))
    img.u32(SECOND_PLAYER_MASK, rng.choice([0, 1]))
    img.u32(VIBRATION, rng.choice([0, 1, 2]))
    img.u32(VIBRATION + 4, rng.choice([0, 1, 2]))
    STATE["movie"] = rng.choice([0, 0, 1, 0x100])
    return [rng.choice([0, 8, rng.getrandbits(32)]), rng.choice([0, 1, rng.getrandbits(32)]), rng.choice([0x96, rng.getrandbits(32)])], img.pokes()


@pht(0x004904D0, "CBruce__Bail")
def gen_bail(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    tables(img, env, rng)
    img.u32(ps.GAME_MODE, rng.choice([0, 1, 7, 9, 9]))
    for field in (0x2C64, 0x2F60, 0x2C8C, 0x302C, 0x29DC, 0x29E0, 0x2BD8, 0x2EC8, 0x2E90, 0x29D4, 0x29D0, 0x29F0, 0x29F4, 0x2DD0, 0x2DD4, 0x29C8, 0x2F68, 0x2F64, 0x2DE0, 0x2DE8, 0x2C68, 0x2E94,
                  0x2CC8, 0x2D88, 0x3064, 0x306C, 0x3068, 0x3014, 0x3018, 0x2C88, 0x2C84, 0x2C80, 0x2F6C, 0x2F70, 0x2C70, 0x2C74):
        img.u32(sk + field, ps.word(rng))
    img.u32(sk + 0x2CC4, rng.choice([0, 1]))
    img.u32(SECOND_PLAYER_MASK, rng.choice([0, 1]))
    img.u32(VIBRATION, rng.choice([0, 1, 2]))
    img.u32(VIBRATION + 4, rng.choice([0, 1, 2]))
    img.u32(sk + 0x30B8, rng.choice(ps.STATES))
    img.u32(sk + 0x3044, ps.word(rng))
    img.u32(sk + 0xD8, rng.getrandbits(32))
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    for k in range(3):
        img.u32(sk + 0x3118 + 4 * k, ps.word(rng))
    movement(img, rng, sk)
    STATE["landed"] = rng.choice([0, 0, 1, 500])
    STATE["movie"] = rng.choice([0, 0, 1])
    STATE["remote"] = rng.choice([0, 0, 1, 0x100, 0x101])
    return [rng.choice([0, 0, 1, 2, 5, 7, 15]), rng.choice([0, 1, 0x715, rng.getrandbits(32)])], img.pokes()


@pht(0x004916D0, "Skater_BailWhenFlipping")
def gen_bail_flipping(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    tables(img, env, rng)
    img.u32(ps.GAME_MODE, rng.choice([0, 1, 9]))
    img.u32(BAIL_LOCKED, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x2C68, rng.choice([0, 1]))
    img.u32(sk + 0x2C6C, rng.choice([0, 0, 1]))
    img.u32(sk + 0x2C88, rng.choice([0, 1, 2]))
    img.u32(sk + 0x2C90, rng.choice([0, 1, -1 & 0xFFFFFFFF, 0x100, 0xFFFFFF00, 0x7FFFFFFF]))
    img.u32(sk + 0x2C8C, rng.choice([0, 0x12C, 0x12D, 0xED3, 0xED4, 0xFFF, 0x1000, 0x1ED4, rng.getrandbits(32)]))
    img.u16(sk + 0x29F0, rng.choice([0, 1, 2, 5, 15]))
    img.u32(sk + 0x3034, ps.word(rng))
    for field in (0x2C64, 0x2F60, 0x302C, 0x29DC, 0x29E0, 0x2BD8, 0x2EC8, 0x2E90, 0x29D4, 0x29D0, 0x29F4, 0x2DD0, 0x2DD4, 0x29C8, 0x2F68, 0x2DE0, 0x2DE8, 0x2E94):
        img.u32(sk + field, ps.word(rng))
    img.u32(sk + 0x2CC4, rng.choice([0, 1]))
    img.u32(VIBRATION, rng.choice([0, 1]))
    img.u32(VIBRATION + 4, rng.choice([0, 1]))
    img.u32(sk + 0x30B8, rng.choice(ps.STATES))
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    movement(img, rng, sk)
    STATE["landed"] = rng.choice([0, 1])
    STATE["movie"] = rng.choice([0, 1])
    STATE["remote"] = rng.choice([0, 1])
    return [rng.getrandbits(32)], img.pokes()


@pht(0x00491C10, "Skater_StartGrindTrick")
def gen_grind_start(env, n):
    img, rng = Image(), env.rng
    tables(img, env, rng)
    sk = ps.skater()
    for field in (0x2C68, 0x29C8, 0x2C6C, 0x2F58, 0x2F54, 0x29D8, 0x29D0, 0x29D4, 0x29CC, 0x2E2C, 0x108, 0x29C4, 0x29E0, 0x3210):
        img.u32(sk + field, ps.word(rng))
    img.u16(sk + 0x29F2, rng.getrandbits(16))
    img.u16(sk + 0x29F0, rng.getrandbits(16))
    img.u16(sk + 0x29C0, rng.getrandbits(16))
    return [rng.choice([0, 1, 50, 0xCF, 0xD0, 0xDF, rng.randrange(0, 0xD0)])], img.pokes()


def history(img, rng, sk):
    """A pad history: 32 entries (button, pressed, two pad bytes, time) and the head, with times around the animation clock."""
    clock = rng.choice([0, 5, 100, 1000, rng.randrange(0, 100000)])
    img.u32(ps.ANIMATION_CLOCK, clock)
    img.u32(sk + 0x2B14, rng.randrange(0, 32))
    for k in range(32):
        base = sk + 0x2A14 + 8 * k
        img.u8(base, rng.choice([0, 1, 2, 3, 4, 5, rng.randrange(0, 0x12)]))
        img.u8(base + 1, rng.choice([0, 1, 1, 1]))
        img.u8(base + 2, rng.getrandbits(8))
        img.u8(base + 3, rng.getrandbits(8))
        img.u32(base + 4, (clock - rng.choice([0, 0, 1, 2, 3, 5, 10, 30, rng.randrange(0, 200)])) & 0xFFFFFFFF)


@pht(0x00491DA0, "TrickInput_PressTime")
def gen_press_time(env, n):
    img, rng = Image(), env.rng
    history(img, rng, ps.skater())
    return [rng.choice([0, 1, 2, 3, 4, 5, rng.randrange(0, 0x14)]) | (rng.getrandbits(24) << 8 if rng.random() < 0.1 else 0), rng.choice([0, 0, 1, 2, 5, 40])], img.pokes()


def pad_pair(address, name):
    @pht(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        sk = ps.skater()
        history(img, rng, sk)
        # make the newest press of the first button this very tick now and then
        if rng.random() < 0.4:
            head = rng.randrange(0, 32)
            slot = sk + 0x2A14 + 8 * ((head - 1) % 32)
            img.u32(sk + 0x2B14, head)
            img.u8(slot, rng.randrange(0, 5))
            img.u8(slot + 1, 1)
            img.u32(slot + 4, mem_clock(img))
        button = lambda: rng.choice([0, 1, 2, 3, 4, rng.randrange(0, 0x14)]) | (rng.getrandbits(24) << 8 if rng.random() < 0.1 else 0)
        return [button(), button(), rng.choice([0, 1, 2, 5, 10, 100, 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000])], img.pokes()

    return generate


def mem_clock(img):
    value = 0
    for k in range(4):
        value |= img.b.get(ps.ANIMATION_CLOCK + k, 0) << (8 * k)
    return value


pad_pair(0x00491E50, "TrickInput_PressedTogether")
pad_pair(0x00491F40, "TrickInput_BothRecent")
