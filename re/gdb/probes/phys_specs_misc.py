"""Phase 4.15a probe: small handlers of physics.cpp (group phm): the movement score timer, the animation completion sound, the trick flags, the take-off and landing sounds, the orientation angles, the
blocked-skater reset and the nollie flip.

Like phys_specs.py: the live player-one skater (all of it captured), a few globals and a scratch slab; the sound (0x004ad620), the score timer (0x0048f720) and rollback (0x0048e8a0), the angle converter
(0x004657c0) and the animation player (0x00480730) are stubs. The GTE register file (0x006a3e00, 0x120 bytes) is a window poked with random words, because the length of a vector goes through it.

Slab layout (offsets): the pad record the nollie flip clears a byte in (the skater's +0x2ccc points at it) at 0x000 (0x60 bytes).
"""

from __future__ import annotations

import phys_specs as ps
from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("phm",)

SLAB = 0x100
PAD_RECORD = 0x000
GTE = 0x006A3E00
GTE_BYTES = 0x120
SCRATCH_ANGLES = 0x7FF00F00             # what the local that receives the angles is recorded as (pc_platform.hpp kScratchBase + 0x700)

WINDOWS = ps.WINDOWS + [(GTE, GTE_BYTES)]

STATE = {"angles": bytes(6)}


def phm_regions(slab):
    return merged([(slab, SLAB), (ps.skater(), ps.SKATER_BYTES)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = phm_regions
    READ_ONLY[_group] = []


def phm(address, name, via_this=True):
    return spec(address, name, "phm", this=(lambda env: ps.skater()) if via_this else None)


def install_misc_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_phm_installed", False):
        return
    fc._phm_installed = True
    mark_under_test()
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x0048F720, 1, "engine_0048f720", convention="thiscall", this=True)
    ensure(fc, 0x0048E8A0, 0, "engine_0048e8a0", convention="thiscall", this=True)
    ensure(fc, 0x004657C0, 2, "engine_004657c0", stack_to=SCRATCH_ANGLES, fill=(0, lambda a: STATE["angles"]))
    ensure(fc, 0x00480730, 4, "engine_00480730", convention="thiscall", this=True)


GROUP_STUBS = {group: install_misc_stubs for group in GROUPS}


def gte_noise(img, rng):
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))


@phm(0x00494020, "Skater_AdvancePointTimerWhenMoved")
def gen_moved(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    gte_noise(img, rng)
    scale = rng.choice([0, 0x100, 0x1000, 0x1F000, 0x20000, 0x40000, 0x100000, 0x7FFFFFF])
    base = [rng.randrange(-0x400000, 0x400000) for _ in range(3)]
    for k in range(3):
        img.u32(sk + 8 + 4 * k, base[k] & 0xFFFFFFFF)
        img.u32(sk + 0x2EE0 + 4 * k, (base[k] + rng.randrange(-scale, scale + 1)) & 0xFFFFFFFF if scale else base[k] & 0xFFFFFFFF)
    img.u32(sk + 0x2F00, ps.word(rng))
    return [], img.pokes()


@phm(0x00496280, "Skater_AnimationCompletionCheck")
def gen_completion(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    for field in (0x4C, 0x54):
        img.u32(sk + field, rng.choice([0, 0xFFF, 0x1000, 0x1FFF, 0x2000, 0xFFFFF000, 0xFFFFEFFF, 0xFFFFE000, rng.getrandbits(32)]))
    img.u32(sk + 0x30A4, rng.choice([0, 0, 1]))
    return [], img.pokes()


@phm(0x004962F0, "Skater_Nothing", via_this=False)
def gen_nothing(env, n):
    return [], Image().pokes()


@phm(0x00496300, "Skater_ClearTrickFlags")
def gen_clear_flags(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    for field in (0x29C8, 0x29DC, 0x2BD8, 0x29E0, 0x2C70, 0x2C74):
        img.u32(sk + field, ps.word(rng))
    img.u32(sk + 0x2C68, rng.choice([0, 1]))
    img.u32(sk + 0x2C6C, rng.choice([0, 0, 1]))
    img.u32(sk + 0x2858, rng.choice([0, 1, 5]))
    return [], img.pokes()


@phm(0x0049A1B0, "Skater_PlayTakeoffSound", via_this=False)
def gen_takeoff_sound(env, n):
    return [env.rng.choice([0, 1, 0xB, 0xC, 0xD, env.rng.getrandbits(32)])], Image().pokes()


@phm(0x0049A1E0, "Skater_PlayLandingSound", via_this=False)
def gen_landing_sound(env, n):
    return [env.rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0xFFFFFFFF, env.rng.getrandbits(32)])], Image().pokes()


@phm(0x0049C8D0, "Skater_UpdateOrientationAngles")
def gen_angles(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    img.put(sk + 0x14, bytes(rng.getrandbits(8) for _ in range(6)))
    STATE["angles"] = bytes(rng.getrandbits(8) for _ in range(6))
    return [], img.pokes()


@phm(0x0049D8A0, "Skater_BlockedPhysicsReset")
def gen_blocked(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    ps.clock_world(img, rng)
    img.u32(sk + 0x2F64, rng.choice([0, 1, 1, 1]))
    img.u32(sk + 0x2F60, rng.choice([0, 1, 2]))
    img.u32(sk + 0x30B8, rng.choice([0, 1, 2, 5]))
    for field in (0x4C, 0x50, 0x54, 0x58, 0x60, 0x2DE0, 0x2DDC):
        img.u32(sk + field, rng.randrange(-0x100000, 0x100000) & 0xFFFFFFFF if rng.random() < 0.9 else rng.choice([0, 0x7FFFFFFF]))
    img.u32(sk + 0x2C10, rng.choice([0, 1, 2, 3, 4, 8, 0xFFFFFFFF, 0xFFFFFFFE, 100, rng.randrange(1, 1000)]))
    clock = rng.choice([0, 5, 20, 21, 100, 1000, rng.randrange(0, 100000)])
    img.u32(ps.ANIMATION_CLOCK, clock)
    img.u32(sk + 0x2B14, rng.randrange(0, 32))
    for k in range(32):
        base = sk + 0x2A14 + 8 * k
        img.u8(base, rng.choice([0, 1, 1, 3]))
        img.u32(base + 4, (clock - rng.choice([0, 0, 1, 2, 5, 10, 19, 20, 21, 30, rng.randrange(0, 100)])) & 0xFFFFFFFF)
    img.u32(sk + 0x108, ps.word(rng))
    return [], img.pokes()


@phm(0x0049E570, "Skater_UpdateNollieFlip")
def gen_nollie(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    slab = env.slab
    state = rng.choice([0, 0, 7, 7, 1, 2, 5])
    img.u32(sk + 0x30B8, state)
    img.u32(sk + 0x30C0, rng.choice([state, state, 0, 7, 3]))
    for field in (0x31F8, 0x31FC, 0x31F0, 0x31F4, 0x2DD8):
        img.u32(sk + field, rng.choice([0, 1, ps.word(rng)]))
    img.u32(sk + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x2DD4, rng.choice([0, 0, 0, 1]))
    img.u16(sk + 0xF6, rng.choice([0x82, 0, 5, rng.getrandbits(16)]))
    img.u16(sk + 0xF4, rng.getrandbits(16))
    img.u8(sk + 0x100, rng.getrandbits(8))
    img.u8(sk + 0x101, rng.getrandbits(8))
    img.u32(sk + 0x2CCC, slab + PAD_RECORD)
    img.u8(slab + PAD_RECORD + 0x51, rng.choice([0, 1, 1, 2, rng.getrandbits(8)]))
    img.u16(sk + 0xF8, rng.getrandbits(16))
    for k in range(6):
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    img.u32(sk + 0xD8, rng.getrandbits(32))
    img.u32(sk + 0x2C78, rng.choice([0, 1]))
    img.u32(sk + 0x2C7C, rng.choice([0, 1]))
    return [], img.pokes()


@phm(0x00493E10, "Skater_RebuildSidewaysBasis")
def gen_sideways_basis(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    gte_noise(img, rng)
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    return [], img.pokes()


@phm(0x00494090, "CBruce__HandleGrindOffSound")
def gen_grind_off_sound(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    img.u32(sk + 0x2E8C, rng.choice([0, 1, 2, 3, 4, 5, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(sk + 0x2C0C, rng.choice([0, 0, 1, ps.word(rng)]))
    return [], img.pokes()


@phm(0x00494180, "CBruce__LeaveRail")
def gen_leave_rail(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    img.u32(sk + 0x30B8, rng.choice(ps.STATES))
    img.u32(sk + 0x3044, ps.word(rng))
    img.u32(sk + 0xD8, rng.getrandbits(32))
    img.u16(sk + 0xF6, rng.choice([0x23, 0x23, 0, 1, 0x82, rng.getrandbits(16)]))
    for field in (0x2EC0, 0x2EC4, 0x2E90, 0x2E94, 0x2DDC, 0x2C68, 0x306C, 0x3068, 0x29D4, 0x29C8, 0x2C0C):
        img.u32(sk + field, ps.word(rng))
    img.u32(sk + 0x29D0, rng.choice([0, 0, 0x01234560, ps.word(rng)]))
    img.u32(sk + 0x2E8C, rng.choice([0, 1, 2, 3, 4]))
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    for k in range(3):
        img.u32(sk + 0x3118 + 4 * k, ps.word(rng))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    img.u32(sk + 0x2C78, rng.choice([0, 1]))
    img.u32(sk + 0x2C7C, rng.choice([0, 1]))
    img.u16(sk + 0xF4, rng.getrandbits(16))
    return [], img.pokes()


@phm(0x004F53B0, "Math_SqrtTruncated", via_this=False)
def gen_sqrt(env, n):
    rng = env.rng
    root = rng.choice([0, 1, 2, 3, 100, 1000, 46340, 46341, rng.randrange(0, 46341)])
    value = rng.choice([0, 1, 2, 3, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, root * root, root * root - 1, root * root + 1, rng.getrandbits(32), rng.getrandbits(31), rng.getrandbits(20), rng.getrandbits(8)]) & 0xFFFFFFFF
    return [value], Image().pokes()
