"""Phase 4.15a probe: the stats of a skater (physics.cpp, group phk): the deck record lookup, the stat initialisation and the stat value.

Like phys_specs.py: the live player-one skater (all of it captured), the stat tables and the deck records of the exe as mutable windows (a case pokes random values into the ones it reads), and a scratch
slab holding the character's trick configuration record. The configuration lookup (0x00416050), the cheat query (0x00416980) and the tag layer's stat modifier (0x004b8410) are stubs; the log line is the shared
`printf_log` stub.

Slab layout (offsets): the trick configuration record at 0x000 (0x60 bytes: the deck mask at +0x34, eleven signed stat bytes at +0x38).
"""

from __future__ import annotations

import phys_specs as ps
from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("phk",)

SLAB = 0x100
CONFIG = 0x000

STAT_TABLES = 0x00536884
SET_RECORDS = 0x0053A3FC
SET_STRIDE = 0x170
EXTRA_DECKS = 0x0053C078
PLAYER_CHOICE = 0x0056A2E0
PLAYER_BOOST = 0x0056A2F0
OVERRIDE = 0x0056B7F4
STAT_FLAGS = 0x0056B78C
BAIL_LOCKED = 0x0056A890
PLAYERS = 0x00533F3C
GAME_MODE = 0x00533F38

GTE = 0x006A3E00
GTE_BYTES = 0x120
WINDOWS = [(GTE, GTE_BYTES), (STAT_TABLES, 0xE0), (SET_RECORDS - 0x40, 0x1F40), (EXTRA_DECKS, 0x180), (PLAYER_CHOICE, 0x10), (PLAYER_BOOST, 0x10), (OVERRIDE, 4), (STAT_FLAGS - 4, 0x10), (BAIL_LOCKED, 4), (PLAYERS, 4), (GAME_MODE, 4)]

STATE = {"config": 0, "cheats": [], "tag": 0, "manual": None}


def phk_regions(slab):
    return merged([(slab, SLAB), (ps.skater(), ps.SKATER_BYTES)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = phk_regions
    READ_ONLY[_group] = []


def phk(address, name, via_this=True):
    return spec(address, name, "phk", this=(lambda env: ps.skater()) if via_this else None)


def install_stat_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_phk_installed", False):
        return
    fc._phk_installed = True
    mark_under_test()
    ensure(fc, 0x00416050, 1, "engine_00416050", result=lambda a: STATE["config"])
    ensure(fc, 0x00416980, 1, "engine_00416980", result=lambda a: STATE["cheats"].pop(0) if STATE["cheats"] else 0)
    ensure(fc, 0x004B8410, 3, "engine_004b8410", result=lambda a: STATE["tag"])
    ensure(fc, 0x0048F690, 4, "engine_0048f690", convention="thiscall", this=True)
    ensure(fc, 0x00480730, 4, "engine_00480730", convention="thiscall", this=True)
    from opentony.memory import mem

    manual = ps.skater() + 0x2DD4

    def script_effect(a):
        # the trick script the manual starts may put the skater into the manual state (+0x2dd4); a case says what it does, else the field stays as it was
        value, STATE["manual"] = STATE["manual"], None
        return mem.u32(manual) if value is None else value

    ensure(fc, 0x004BE450, 0, "engine_004be450", convention="thiscall", this=True, writes=[(manual, 4, script_effect)])


GROUP_STUBS = {group: install_stat_stubs for group in GROUPS}


def small(rng):
    return rng.choice([0, 1, 2, 3, 4, 5, 10, 50, 100, 150, 250, -1, -5, rng.randrange(-20, 400), rng.randrange(-20, 400)])


def stat_tables(img, rng):
    for table in range(5):
        for k in range(11):
            img.u32(STAT_TABLES + 0x28 * table + 4 * k, small(rng) & 0xFFFFFFFF)


@phk(0x0048F090, "Skater_DeckRecord", via_this=False)
def gen_deck_record(env, n):
    img, rng = Image(), env.rng
    s = rng.choice([0, 1, 2, 5, 19, 19, 20, rng.randrange(0, 20)])
    deck = rng.choice([0, 1, 7, 8, 9, 19, 20, 22, -1 & 0xFFFFFFFF, rng.randrange(0, 20)])
    if 0 <= s < 20:
        img.u8(SET_RECORDS + s * SET_STRIDE + 0x118, rng.choice([0, 2, 3, rng.getrandbits(8)]))
    return [s, deck], img.pokes()


@phk(0x0048F1D0, "Skater_InitStats")
def gen_init_stats(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    slab = env.slab
    player = rng.choice([0, 1])
    img.u32(sk + 0x2CC4, player)
    img.u8(PLAYER_CHOICE + player, rng.choice([0, 1, 2, 3, rng.getrandbits(8)]))
    mask = rng.choice([0, 1, 0xFF, 0xFFFFF, 0xFFFFFFFF, rng.getrandbits(32), rng.getrandbits(32) & 0xFFFFF])
    img.u32(slab + CONFIG + 0x34, mask)
    STATE["config"] = slab + CONFIG
    decks = bin(mask & 0xFFFFF).count("1")
    flag = rng.choice([0, 2, 3])
    img.u8(SET_RECORDS + player * SET_STRIDE + 0x118, flag)
    deck = decks - 1
    if decks > 0:
        if deck < 8:
            record = SET_RECORDS + player * SET_STRIDE + (deck << 5)
        elif flag & 2:
            record = EXTRA_DECKS + ((deck - 8) << 5)
        else:
            record = SET_RECORDS + player * SET_STRIDE + (deck << 5)       # (an assertion fails; the record is read anyway)
        if SET_RECORDS <= record < SET_RECORDS + 0x1F00 - 0x20 or EXTRA_DECKS <= record < EXTRA_DECKS + 0x160:
            for offset in (0xC, 0x10, 0x14):
                img.u32(record + offset, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xFFFFFFFF, 0x80, 0x100, rng.getrandbits(32)]))
    for field in (0x29B4, 0x29B5, 0x29B6, 0x29B7):
        img.u8(sk + field, rng.getrandbits(8))
    for field in (0x29F8, 0x29FC, 0x2A00, 0x2A04, 0x2A08):
        img.u32(sk + field, ps.word(rng))
    img.u16(sk + 0x2A02, rng.getrandbits(16))
    return [], img.pokes()


@phk(0x0048F3A0, "Skater_ProfileValue")
def gen_profile_value(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    slab = env.slab
    stat_tables(img, rng)
    character = rng.randrange(0, 20)
    player = rng.choice([0, 1])
    img.u32(sk + 0x2CC0, character)
    img.u32(sk + 0x2CC4, player)
    img.u32(PLAYERS, rng.choice([1, 1, 2, 3]))
    img.u32(GAME_MODE, rng.choice([0, 1, 2, 9, 9]))
    img.u32(OVERRIDE, rng.choice([0, 0, 0, 1, 5, 10, rng.getrandbits(32)]))
    img.u32(BAIL_LOCKED, rng.choice([0, 0, 1]))
    img.u32(PLAYER_BOOST + 4 * player, rng.choice([0, 1, 2, 5, 10, 0xFFFFFFFF, rng.getrandbits(32)]))
    for stat in range(11):
        img.u8(0x0053A4FC + stat + character * 0x170, rng.choice([0, 1, 2, 3, 5, 8, rng.getrandbits(8)]))
        img.u16(sk + 0x29F8 + 2 * stat, small(rng) & 0xFFFF)
        img.u8(slab + CONFIG + 0x38 + stat, small(rng) & 0xFF)
    img.u32(sk + 0x2F70, rng.choice([0, 0, 0x1000, 0x80000000, rng.getrandbits(32)]))
    img.u32(sk + 0xD8, rng.getrandbits(32))
    img.u8(sk + 0x163, rng.choice([0, 1]))
    STATE["config"] = slab + CONFIG
    STATE["cheats"] = [rng.choice([0, 0, 1, 0x100]) for _ in range(8)]
    STATE["tag"] = rng.choice([0, 100, 250, rng.getrandbits(32)])
    return [rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 6, 6, 4, 4, 11])], img.pokes()


def stats_world(img, env, rng, sk):
    """The stat machinery of Skater_ProfileValue for the functions that read stats: tables, the configuration record, the cheats."""
    slab = env.slab
    stat_tables(img, rng)
    character = rng.randrange(0, 20)
    player = rng.choice([0, 1])
    img.u32(sk + 0x2CC0, character)
    img.u32(sk + 0x2CC4, player)
    img.u32(PLAYERS, rng.choice([1, 1, 2]))
    img.u32(OVERRIDE, rng.choice([0, 0, 1, 5, 10, 14]))
    img.u32(BAIL_LOCKED, rng.choice([0, 0, 1]))
    img.u32(PLAYER_BOOST + 4 * player, rng.choice([0, 1, 2, 5, 10]))
    for stat in range(11):
        img.u8(0x0053A4FC + stat + character * 0x170, rng.choice([0, 1, 2, 3, 5, 8]))
        img.u16(sk + 0x29F8 + 2 * stat, rng.randrange(0, 60))
        img.u8(slab + CONFIG + 0x38 + stat, rng.randrange(0, 11))
    img.u32(sk + 0x2F70, rng.choice([0, 0, 0x1000]))
    img.u32(sk + 0xD8, rng.getrandbits(32))
    img.u8(sk + 0x163, rng.choice([0, 1]))
    img.u32(GAME_MODE, rng.choice([0, 1, 2, 9]))
    STATE["config"] = slab + CONFIG
    STATE["cheats"] = [rng.choice([0, 0, 1]) for _ in range(24)]
    STATE["tag"] = rng.choice([0, 100, 250])


@phk(0x004993F0, "Skater_EnterFootplantState")
def gen_footplant(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    stats_world(img, env, rng, sk)
    gte_noise(img, rng)
    img.u32(sk + 0x30B8, rng.choice(ps.STATES))
    img.u32(sk + 0x3044, ps.word(rng))
    img.u16(sk + 0xF6, rng.choice([0x23, 0, 1]))
    for field in (0x2DE8, 0x2DDC, 0x50):
        img.u32(sk + field, rng.randrange(-0x100000, 0x100000) & 0xFFFFFFFF)
    for k in range(3):
        img.u32(sk + 0x4C + 4 * k, rng.randrange(-0x200000, 0x200000) & 0xFFFFFFFF)
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    for k in range(3):
        img.u32(sk + 0x3118 + 4 * k, ps.word(rng))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    return [], img.pokes()


def gte_noise(img, rng):
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))


@phk(0x004995D0, "Skater_UpdateSpecialBasisState")
def gen_special_basis(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    gte_noise(img, rng)
    img.u32(sk + 0x2EC0, ps.word(rng))
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, rng.choice([ps.basis_word(rng), rng.randrange(0xD00, 0x1000), 0xDA7, 0xDA8, 0xDA9]) if k == 1 else ps.basis_word(rng))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    return [], img.pokes()


@phk(0x00497BB0, "CBruce__MaybeManual")
def gen_manual(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    stats_world(img, env, rng, sk)
    for field in (0x2F64, 0x2DD4, 0x30B8, 0x29E0, 0x306C, 0x3068, 0x29E8, 0x2DFC, 0x2BD8, 0x2F10, 0x2F18, 0x2F14, 0x2F20, 0x2F24):
        img.u32(sk + field, ps.word(rng))
    img.u32(sk + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x2DD4, rng.choice([0, 0, 1, 2]))
    STATE["manual"] = rng.choice([None, 0, 1, 1, 2, 2, 5])
    img.u32(sk + 0x30B8, rng.choice([0, 1, 2, 5]))
    img.u16(sk + 0xF6, rng.choice([0x82, 0, 5]))
    img.u32(sk + 0x29E0, rng.choice([0, 0, 0x01234560]))
    for field in (0x2F18, 0x2F14, 0x2F24):
        img.u32(sk + field, rng.randrange(-0x4000, 0x4000) & 0xFFFFFFFF)
    img.u32(sk + 0x2F20, rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF)
    for field in (0x2C68, 0x29C8, 0x2C6C, 0x2F58, 0x2F54, 0x29D8, 0x29D0, 0x29D4, 0x29CC, 0x2E2C, 0x108, 0x29C4, 0x3210):
        img.u32(sk + field, ps.word(rng))
    img.u16(sk + 0x29F2, rng.getrandbits(16))
    img.u16(sk + 0x29F0, rng.getrandbits(16))
    img.u16(sk + 0x29C0, rng.getrandbits(16))
    return [rng.getrandbits(32), rng.choice([0, 1, 2, 0xFFFFFFFF])], img.pokes()
