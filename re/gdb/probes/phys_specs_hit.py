"""Phase 4.15a probe: the collision of two skaters, the trigger check and the speed damping of physics.cpp (group phq).

The other skater is a fake object in the scratch slab (0x3538 bytes at slab + 0x400: the functions read and the bail writes its fields); `this` is the live player-one skater. The services are those of
the bail (phys_specs_trick.py) plus the trigger layer (0x004c4b80, 0x004c4ba0, 0x004660b0), the collision query (0x004624d0), `atexit` (0x00500260), the stat machinery of `Skater_ProfileValue`
(phys_specs_stats.py) and the GTE register file as a window (the lengths go through it).

Slab layout (offsets): the script offset tables of the bail at 0x000 / 0x1c0 (phys_specs_trick.py), the fake skater at 0x400.
"""

from __future__ import annotations

import phys_specs as ps
import phys_specs_stats as pk
import phys_specs_trick as pt
from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("phq",)

FAKE = 0x400
SLAB = FAKE + ps.SKATER_BYTES + 0x40
INFO = 0x7FF00D00
TWO_PLAYERS = 0x0056A85C
STATIC_GUARD = 0x0056B7E4
LAST_MOVE = 0x0056B780
TRIGGER_OWNER = 0x0056A954
AUTO_KICK = 0x0056A3D8

WINDOWS = pt.WINDOWS + pk.WINDOWS + [(TWO_PLAYERS, 4), (STATIC_GUARD, 4), (LAST_MOVE, 0x10), (TRIGGER_OWNER, 4), (AUTO_KICK, 8)]


def phq_regions(slab):
    return merged([(slab, SLAB), (ps.skater(), ps.SKATER_BYTES)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = phq_regions
    READ_ONLY[_group] = []


def phq(address, name):
    return spec(address, name, "phq", this=lambda env: ps.skater())


def install_hit_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_phq_installed", False):
        return
    fc._phq_installed = True
    mark_under_test()
    pt.install_trick_stubs(fc, env)
    pk.install_stat_stubs(fc, env)
    ensure(fc, 0x00500260, 1, "engine_00500260")
    ensure(fc, 0x004C4B80, 1, "engine_004c4b80")
    ensure(fc, 0x004C4BA0, 1, "engine_004c4ba0")
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=INFO)
    ensure(fc, 0x004660B0, 2, "engine_004660b0", stack_to=INFO)


GROUP_STUBS = {group: install_hit_stubs for group in GROUPS}


def gte_noise(img, rng):
    img.put(pk.GTE, bytes(rng.getrandbits(8) for _ in range(pk.GTE_BYTES)))


def fake_skater(img, env, rng, base):
    """The fields of the other skater the collision reads and its bail writes: a state, a position, a velocity and the bail's own fields."""
    img.u32(base + 0x2F64, rng.choice([0, 0, 0, 1]))
    for k in range(3):
        img.u32(base + 8 + 4 * k, rng.randrange(-0x200000, 0x200000) & 0xFFFFFFFF)
        img.u32(base + 0x4C + 4 * k, rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF)
        img.u32(base + 0xBC + 4 * k, rng.randrange(-0x200000, 0x200000) & 0xFFFFFFFF)
    img.u32(base + 0x2CC4, rng.choice([0, 1]))
    img.u32(base + 0x30B8, rng.choice(ps.STATES))
    for field in (0x2C64, 0x2F60, 0x2C8C, 0x302C, 0x29DC, 0x29E0, 0x2BD8, 0x2EC8, 0x2E90, 0x29D4, 0x29D0, 0x29F0, 0x29F4, 0x2DD0, 0x2DD4, 0x29C8, 0x2F68, 0x2DE0, 0x2DE8, 0x2C68, 0x2E94, 0x2CC8, 0x2D88,
                  0x3064, 0x306C, 0x3068, 0x3014, 0x3018, 0x2C88, 0x2C84, 0x2C80, 0x2F6C, 0x2F70, 0x2C70, 0x2C74, 0x3044, 0xD8):
        img.u32(base + field, ps.word(rng))
    for k in range(9):
        img.u32(base + 0x30F4 + 4 * k, ps.basis_word(rng))
    for k in range(3):
        img.u32(base + 0x3118 + 4 * k, ps.word(rng))


@phq(0x0049D2F0, "Skater_CollideWithOtherPlayer")
def gen_collide(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    other = env.slab + FAKE
    pt.tables(img, env, rng)
    gte_noise(img, rng)
    fake_skater(img, env, rng, other)
    img.u32(TWO_PLAYERS, rng.choice([0, 1, 1, 1]))
    img.u32(sk + 0x29BC, rng.choice([other, other, other, 0]))
    img.u32(sk + 0x2F64, rng.choice([0, 0, 0, 1]))
    close = rng.random() < 0.8
    for k in range(3):
        base = rng.randrange(-0x200000, 0x200000)
        img.u32(sk + 8 + 4 * k, base & 0xFFFFFFFF)
        img.u32(other + 8 + 4 * k, (base + rng.randrange(-0x14000, 0x14000)) & 0xFFFFFFFF if close else rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF)
        img.u32(sk + 0xBC + 4 * k, (base + rng.randrange(-0x14000, 0x14000)) & 0xFFFFFFFF)
        img.u32(sk + 0x4C + 4 * k, rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF)
    for field in (0x2F60, 0x2C8C, 0x302C, 0x29DC, 0x29E0, 0x2BD8, 0x2EC8, 0x2E90, 0x29D4, 0x29D0, 0x29F0, 0x29F4, 0x2DD0, 0x2DD4, 0x29C8, 0x2F68, 0x2DE0, 0x2DE8, 0x2C68, 0x2E94, 0x2CC8, 0x2D88, 0x3064, 0x306C,
                  0x3068, 0x3014, 0x3018, 0x2C88, 0x2C84, 0x2C80, 0x2F6C, 0x2F70, 0x2C70, 0x2C74, 0x3044):
        img.u32(sk + field, ps.word(rng))
    img.u32(sk + 0x2CC4, rng.choice([0, 1]))
    img.u32(sk + 0x30B8, rng.choice(ps.STATES))
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    for k in range(3):
        img.u32(sk + 0x3118 + 4 * k, ps.word(rng))
    pt.STATE["landed"] = rng.choice([0, 0, 1])
    pt.STATE["movie"] = rng.choice([0, 1])
    pt.STATE["remote"] = rng.choice([0, 1])
    return [], img.pokes()


@phq(0x0049D740, "CBruce__CheckForTriggers")
def gen_triggers(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    gte_noise(img, rng)
    img.u8(STATIC_GUARD, rng.choice([0, 1, 0xFE, 0xFF]))
    for k in range(3):
        img.u32(LAST_MOVE + 4 * k, ps.word(rng))
    here = [rng.randrange(-0x400000, 0x400000) for _ in range(3)]
    still = rng.random() < 0.2
    for k in range(3):
        img.u32(sk + 8 + 4 * k, here[k] & 0xFFFFFFFF)
        img.u32(sk + 0xBC + 4 * k, (here[k] if still else here[k] + rng.choice([rng.randrange(-0x40000, 0x40000), rng.randrange(-0x2000, 0x2000)])) & 0xFFFFFFFF)
    img.u32(sk + 0x2CC4, rng.choice([0, 1, 2]))
    img.u32(TRIGGER_OWNER, ps.word(rng))
    return [], img.pokes()


@phq(0x0049D480, "Skater_DampResponseVector")
def gen_damp(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    pk.stats_world(img, env, rng, sk)
    gte_noise(img, rng)
    ps.clock_world(img, rng)
    scale = rng.choice([0, 0x100, 0x1000, 0x3000, 0x10000, 0x30000, 0x80000, 0x200000])
    for k in range(3):
        img.u32(sk + 0x4C + 4 * k, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF if scale else 0)
    img.u32(sk + 0x2CC4, rng.choice([0, 1]))
    img.u32(ps.GAME_MODE, rng.choice([0, 7, 9]))
    img.u32(pt.SECOND_PLAYER_MASK, rng.choice([0, 1]))
    img.u32(AUTO_KICK, rng.choice([0, 0, 1]))
    img.u32(AUTO_KICK + 4, rng.choice([0, 0, 1]))
    return [], img.pokes()
