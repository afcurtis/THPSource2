"""Phase 4.15a probe: the large state functions of physics.cpp (group phz): the continuous sounds (0x0049c970), the upright correction, the swept contact state, the pre-physics ollie and the rail physics as they
are added.

Like phys_specs.py: the live player-one skater (all of it captured), the globals the stat machinery (phys_specs_stats.py), the bail (phys_specs_trick.py) and the clock read, and a scratch slab. The services are
those of the bail and the stats plus the sound layer (0x004ad620 returns a voice, 0x004ad9f0 stops one, 0x004ad890 sets its frequency, 0x004ad950 its volume).
"""

from __future__ import annotations

import phys_specs as ps
import phys_specs_stats as pk
import phys_specs_trick as pt
from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from rail_specs import changed

GROUPS = ("phz",)

SLAB = 0x400

WORLD_UP = 0x0056B7C0
INFO = 0x7FF00D00                  # what the collision record (a stack local) is recorded as (collision_kit.hpp kInfoScratch)
FLAGS = [(0x0056B768, 4), (0x0056B7A8, 0x14), (0x0056B7EC, 4), (0x0056A93C, 4), (0x00568804, 4), (0x0056AF28, 0x0C)]

WINDOWS = pt.WINDOWS + pk.WINDOWS + [(WORLD_UP, 0x0C)] + FLAGS

STATE = {"voices": [], "history": [], "hits": []}


def phz_regions(slab):
    return merged([(slab, SLAB), (ps.skater(), ps.SKATER_BYTES)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = phz_regions
    READ_ONLY[_group] = []


def phz(address, name):
    return spec(address, name, "phz", this=lambda env: ps.skater())


def install_big_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_phz_installed", False):
        return
    fc._phz_installed = True
    mark_under_test()
    pt.install_trick_stubs(fc, env)
    pk.install_stat_stubs(fc, env)
    ensure(fc, 0x0048F720, 1, "engine_0048f720", convention="thiscall", this=True)
    ensure(fc, 0x004AD620, 3, "engine_004ad620", result=lambda a: STATE["voices"].pop(0) if STATE["voices"] else 0)
    ensure(fc, 0x004AD9F0, 1, "engine_004ad9f0")
    ensure(fc, 0x004AD890, 2, "engine_004ad890")
    ensure(fc, 0x004AD950, 3, "engine_004ad950")
    ensure(fc, 0x00492560, 5, "engine_00492560", convention="thiscall", this=True, result=lambda a: STATE["history"].pop(0) if STATE["history"] else 0xFFFFFFFF)
    ensure(fc, 0x00492030, 2, "engine_00492030", convention="thiscall", this=True)
    ensure(fc, 0x0049B500, 3, "engine_0049b500", convention="thiscall", this=True)
    ensure(fc, 0x0048F8F0, 0, "engine_0048f8f0", convention="thiscall", this=True)
    ensure(fc, 0x0048F5F0, 0, "engine_0048f5f0", convention="thiscall", this=True)
    ensure(fc, 0x00431260, 2, "engine_00431260", convention="thiscall", this=True)
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=INFO)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=INFO, pokes=[(0, None, lambda a: STATE["hits"].pop(0) if STATE["hits"] else bytes(0x50), 0x40)])
    ensure(fc, 0x0048EA80, 1, "engine_0048ea80", stack_to=INFO)
    ensure(fc, 0x00496060, 3, "engine_00496060", convention="thiscall", this=True)
    ensure(fc, 0x0048F800, 1, "engine_0048f800", convention="thiscall", this=True)
    ensure(fc, 0x0049D080, 0, "engine_0049d080", convention="thiscall", this=True)
    ensure(fc, 0x0049C060, 1, "engine_0049c060", convention="thiscall", this=True)
    ensure(fc, 0x0049BAD0, 3, "engine_0049bad0", convention="thiscall", this=True)
    ensure(fc, 0x0046E1D0, 1, "engine_0046e1d0")


GROUP_STUBS = {group: install_big_stubs for group in GROUPS}


@phz(0x0049C970, "CBruce__HandleContinuousSounds")
def gen_sounds(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    pk.stats_world(img, env, rng, sk)
    img.u32(sk + 0x30B8, rng.choice([0, 1, 2, 3, 4, 4, 4, 5, 7, 8, 8, 9, 0xFFFFFFFF]))
    for field in (0x308C, 0x3090, 0x3094, 0x3098):
        img.u32(sk + field, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 5, 7, 0x12345]))
    img.u32(sk + 0x2E8C, rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, 7]))
    img.u32(sk + 0x2C0C, rng.choice([0, 1, 0xFF]))
    img.u32(sk + 0x30B0, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xE, 0xF, 0x10, rng.getrandbits(32)]))
    img.u32(sk + 0x3090, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0, 5]))
    img.u32(sk + 0x30B4, rng.choice([0, 1, 2, 5, rng.getrandbits(32)]))
    img.u32(sk + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x3084, rng.choice([100, 0x28, 0, rng.randrange(0, 200), rng.getrandbits(32)]))
    img.u32(sk + 0x3088, rng.choice([100, 0x96, 0, rng.randrange(0, 200), rng.getrandbits(32)]))
    img.u32(sk + 0x30A0, rng.choice([0, 1, 31, 32, 1000, 0xFFFF, rng.getrandbits(32)]))
    img.u32(sk + 0x309C, rng.getrandbits(32))
    scale = rng.choice([0x100, 0x1000, 0x8000, 0x10000, 0x40000, 0x100000])
    for k in range(3):
        img.u32(sk + 0x4C + 4 * k, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF)
    STATE["voices"] = [rng.choice([0, 1, 3, 9, 0x40, rng.getrandbits(8)]) for _ in range(4)]
    return [], changed(env, img)


@phz(0x0049C330, "Skater_UprightCorrection")
def gen_upright(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    pk.gte_noise(img, rng)
    img.u32(sk + 0x30B8, rng.choice([1, 1, 3, 4, 5, 7, 8, 0, 2]))
    small = rng.random() < 0.4
    for k in range(3):
        img.u32(sk + 0x30F4 + 4 * k, (rng.randrange(-60, 60) if small else ps.short(rng)) & 0xFFFFFFFF)
        img.u32(sk + 0x310C + 4 * k, (rng.randrange(-0x1000, 0x1001) if rng.random() < 0.8 else ps.short(rng)) & 0xFFFFFFFF)
    if rng.random() < 0.7:
        img.u32(WORLD_UP, 0)
        img.u32(WORLD_UP + 4, 0xFFFFF000)
        img.u32(WORLD_UP + 8, 0)
    else:
        for k in range(3):
            img.u32(WORLD_UP + 4 * k, ps.basis_word(rng))
    img.put(sk + 0x2E58, b"".join((ps.short(rng) & 0xFFFF).to_bytes(2, "little") for _ in range(9)))
    img.put(sk + 0x2E6A, bytes(rng.getrandbits(8) for _ in range(0x10)))
    return [], changed(env, img)


@phz(0x0049A280, "Skater_PrePhysicsOllie")
def gen_ollie(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    slab = env.slab
    pk.stats_world(img, env, rng, sk)
    pad = slab + 0x200
    pt.tables(img, env, rng)
    img.u32(sk + 0x2CCC, pad)
    img.u8(pad + 0x30, rng.choice([0, 1, 1]))
    img.u8(pad + 0x80, rng.choice([0, 1]))
    img.u8(pad + 0x90, rng.choice([0, 1]))
    img.u32(sk + 0x2F64, rng.choice([0, 0, 0, 0, 1]))
    img.u32(sk + 0x2DD8, rng.choice([0, 0, 1]))
    img.u16(sk + 0xF6, rng.choice([0, 4, 5, 8, 0xE, 0x19, 0x22, 0x82, 0xA2, 0xA3, 0xAF, 0xB5, 1, 3, 0x40, 0x100, rng.getrandbits(16)]))
    img.u16(sk + 0xF4, rng.choice([0, 0, 1, rng.getrandbits(16)]))
    img.u8(sk + 0x102, rng.choice([0xFF, 0xFF, 0, 3]))
    img.u8(sk + 0x107, rng.choice([0, 1, 1, 1]))
    img.u32(sk + 0x30B8, rng.choice([0, 0, 1, 2, 4, 5, 7, 3]))
    img.u32(sk + 0x30C0, rng.choice([0, 1, 5]))
    img.u32(sk + 0x30B0, rng.randrange(0, 17))
    for field in (0x2DDC, 0x2DD4, 0x29C8, 0x2E30, 0x2DE0, 0x2C68, 0x2E90, 0x31F4, 0x31FC, 0x29D4, 0x2D88):
        img.u32(sk + field, rng.choice([0, 0, 1, 1, ps.word(rng)]))
    img.u32(sk + 0x29D4, rng.choice([0, 0, 0x01234560]))
    img.u32(sk + 0x2DE8, rng.choice([0, 1, 5, 10, 14, 15, 16, 20, rng.randrange(0, 30)]))
    img.u32(sk + 0x2DE4, rng.choice([0, 0, -1 & 0xFFFFFFFF, 5, rng.randrange(1, 100000)]))
    img.u32(sk + 0x2DB4, rng.choice([0, 1]))
    img.u32(sk + 0x2DB8, rng.choice([0, 1]))
    img.u32(sk + 0x3144, rng.choice([0, 100, 0x9FFF, 0xA000, -0x9000 & 0xFFFFFFFF, ps.word(rng)]))
    img.u32(sk + 0x3110, rng.choice([0, 0x100, 0x9C3, 0x9C4, -0x9C3 & 0xFFFFFFFF, -0x1000 & 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(sk + 0x2F48, rng.randrange(0, 0x1000000))
    img.u32(sk + 0x2F4C, rng.randrange(0, 0x100000))
    img.u32(sk + 0x2B14, rng.randrange(0, 32))
    img.u32(sk + 0x3128 - 0, ps.word(rng))
    img.u32(sk + 0x312A, ps.word(rng))
    for k in range(32):
        img.u8(sk + 0x2A14 + 8 * k, rng.choice([0, 1, 1, 3]))
    img.u32(ps.CLOCK_ADDRESS if hasattr(ps, "CLOCK_ADDRESS") else ps.ANIMATION_CLOCK, rng.choice([0, 100, 1000, rng.randrange(0, 100000)]))
    img.u32(0x00568658, rng.choice([0, 1, 1]))
    img.u32(0x0056A890, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0xD8, rng.getrandbits(32))
    img.u8(sk + 0x163, rng.choice([0, 1]))
    scale = rng.choice([0x1000, 0x20000, 0x80000])
    for k in range(3):
        img.u32(sk + 0x4C + 4 * k, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF)
    for k in range(3):
        img.u32(sk + 0x2EB4 + 4 * k, ps.basis_word(rng))
    ps.clock_world(img, rng)
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    STATE["landed"] = 0
    pt.STATE["landed"] = rng.choice([0, 100, 500, 3000, rng.getrandbits(16)])
    history = [rng.choice([-1, rng.randrange(0, 32)]) for _ in range(rng.choice([1, 2, 3]))]
    if rng.random() < 0.5:
        # a release that makes the jump: the button up, the pad history with one or two presses
        img.u8(pad + 0x30, 0)
        for field, value in ((0x2DE0, 1), (0x2F64, 0), (0x2DD4, 0), (0x29C8, 0), (0x2E30, 0), (0x30B8, rng.choice([0, 1, 2, 7])), (0x31F4, rng.choice([0, 0, 1, 1]))):
            img.u32(sk + field, value)
        first, second = rng.randrange(0, 32), rng.randrange(0, 32)
        history = [first, second if rng.random() < 0.5 else -1, -1]
        img.u8(sk + 0x2A14 + 8 * first, 1)
        img.u8(sk + 0x2A14 + 8 * second, 1)
    STATE["history"] = [h & 0xFFFFFFFF for h in history]
    return [], changed(env, img)


FACE = 0x300          # a face record in the slab: the flags word is read at +0xc
BODY = 0x340


def hit_record(env, rng, hit):
    """The answer of a zone query: 0x50 bytes from +0x40 of the record (the distance, the length, the basis, the body, the position, the normal, the face)."""
    slab = env.slab
    data = bytearray(rng.getrandbits(8) for _ in range(0x50))

    def put(offset, value, size=4):
        data[offset - 0x40:offset - 0x40 + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")

    put(0x40, rng.choice([0, 0x10, 0x73, 0x74, 0x75, 0x100, rng.randrange(0, 0x400)]))
    put(0x68, (slab + BODY) if hit else 0)
    for k in range(3):
        put(0x6C + 4 * k, rng.randrange(-0x400000, 0x400000))
    nx, ny, nz = ps.short(rng), rng.choice([0xCCC, 0xCCD, 0xCCE, 0xE00, 0x1000, -0x1000, ps.short(rng)]), ps.short(rng)
    put(0x78, nx & 0xFFFF, 2)
    put(0x7A, ny & 0xFFFF, 2)
    put(0x7C, nz & 0xFFFF, 2)
    put(0x80, slab + FACE)
    return bytes(data)


@phz(0x00499710, "Skater_UpdateSweptContactState")
def gen_swept(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    slab = env.slab
    pk.stats_world(img, env, rng, sk)
    img.u32(sk + 0x30B8, rng.choice([0, 1, 2, 2, 3, 5, 7]))
    img.u8(sk + 0x107, rng.choice([0, 1]))
    img.u16(sk + 0xF6, rng.choice([0x23, 0x24, 0, 5, rng.getrandbits(16)]))
    position = [rng.randrange(-0x400000, 0x400000) for _ in range(3)]
    for k in range(3):
        img.u32(sk + 8 + 4 * k, position[k] & 0xFFFFFFFF)
        img.u32(sk + 0xBC + 4 * k, (position[k] if rng.random() < 0.92 else position[k] + rng.randrange(-100, 100)) & 0xFFFFFFFF)
    scale = rng.choice([0x1000, 0x8000, 0x20000, 0x80000])
    for k in range(3):
        img.u32(sk + 0x4C + 4 * k, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF)
        img.u32(sk + 0x58 + 4 * k, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF)
    img.u32(0x00568804, rng.choice([0x100, 0x1000, 0x80, rng.randrange(0, 0x2000)]))
    img.u32(ps.ANIMATION_SCALE, rng.choice([0x100, 0x80, 0x200, 0x1000, rng.randrange(0, 0x2000)]))
    for k in range(3):
        img.u32(0x0056AF28 + 4 * k, rng.choice([0, 0, 0, 1, rng.getrandbits(32)]))
        img.u32(sk + 0x310C + 4 * k, (rng.randrange(-0x1000, 0x1001)) & 0xFFFFFFFF)
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    img.u32(sk + 0x2C88, rng.choice([0, 0, 1]))
    img.u32(sk + 0x2E80, rng.choice([0, 0, 1]))
    img.u32(sk + 0x2F64, rng.choice([0, 0, 1]))
    img.u32(sk + 0x2DD8, rng.choice([0, 1]))
    img.u32(sk + 0x3124, ps.word(rng))
    img.u32(0x0056B7EC, rng.choice([0, 0, 1]))
    img.u32(0x0056A93C, rng.choice([1, 2, 5, 100]))
    for flag in (0x0056B7AC, 0x0056B768, 0x0056B7B8, 0x0056B7A8):
        img.u32(flag, rng.choice([0, 0, 1, 0xFF]))
    img.put(slab + FACE, bytes(rng.getrandbits(8) for _ in range(0x10)))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    pk.gte_noise(img, rng)
    STATE["hits"] = [hit_record(env, rng, rng.random() < 0.7), hit_record(env, rng, rng.random() < 0.75)]
    return [], changed(env, img)
