"""Phase 4.15c probe: Powerup_Update and its tail block, the glow function (group wpg, 0x004a8ac0; 0x004a8b4c is entered by a jump from it with the power-up in ebp and ends in Powerup_Update's own
epilogue, so it is replayed through its entry point).

The glow sprite, its methods, the power-up's age check and physics, and the power-up's kill method (slot 1 of its vtable) are services.

Slab layout (offsets): power-up 0x000 (0x230 bytes), glow object 0x300, first player 0x400, second player 0x440.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import OBJ, SPRITE_B, fill_object, noise, word
from world_specs import install_stubs as install_base_stubs

GROUPS = ("wpg", "wpa", "wpd")

SLAB = 0x500
PLAYER_A = 0x400
PLAYER_B = 0x440
POWERUP_VTABLE = 0x00519684
CAMERA = 0x480
GLOBALS = [(0x0056A8D4, 4), (0x005244CC, 4), (0x00533F3C, 4), (0x00563A3C, 4), (0x0056A960, 4), (0x0056865C, 4), (0x005620E0, 4), (0x006A3E00, 0xD0)]


def regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = [(POWERUP_VTABLE, 0x10)]


def install_stubs(fc, env):
    from opentony.memory import mem
    from platform_specs import mark_under_test

    if getattr(fc, "_wpg_installed", False):
        return
    fc._wpg_installed = True
    install_base_stubs(fc, env)
    mark_under_test()
    child = lambda a: env.slab + SPRITE_B
    ensure(fc, 0x004A8620, 0, "engine_004a8620", convention="thiscall", this=True)                      # CPowerUp__CheckAge
    ensure(fc, 0x004A8300, 0, "engine_004a8300", convention="thiscall", this=True)                      # CPowerUp__DoPhysics
    kill = mem.u32(POWERUP_VTABLE + 4)
    ensure(fc, kill, 0, "engine_%08x" % kill, convention="thiscall", this=True, result=lambda a: env.rng.getrandbits(32))     # the kill method
    die = mem.u32(POWERUP_VTABLE + 8)
    ensure(fc, die, 0, "engine_%08x" % die, convention="thiscall", this=True, result=lambda a: env.rng.getrandbits(32))      # the die method (slot 2): CheckAge calls it when the timer runs out
    ensure(fc, 0x004CA140, 3, "engine_004ca140", result=lambda a: env.rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0x1000, 0xFFFFF000, env.rng.getrandbits(32)]))   # Utils_GetGroundHeight
    ensure(fc, 0x004056C0, 2, "engine_004056c0", convention="thiscall", this=True, result=child)        # the glow's constructor
    ensure(fc, 0x00405B20, 3, "engine_00405b20", convention="thiscall", this=True)
    ensure(fc, 0x00405B50, 3, "engine_00405b50", convention="thiscall", this=True)
    ensure(fc, 0x00405B90, 4, "engine_00405b90", convention="thiscall", this=True)
    ensure(fc, 0x00405A70, 1, "engine_00405a70", convention="thiscall", this=True)
    ensure(fc, 0x00405C40, 4, "engine_00405c40", convention="thiscall", this=True)
    ensure(fc, 0x00403EA0, 1, "engine_00403ea0", convention="thiscall", this=True)
    ensure(fc, 0x00405AA0, 2, "engine_00405aa0", convention="thiscall", this=True)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


@spec(0x004A8AC0, "Powerup_Update", "wpg", this=lambda env: env.slab + OBJ)
def gen_update(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x230)
    img.u32(env.slab + OBJ, POWERUP_VTABLE)
    img.u16(env.slab + OBJ + 0x3C, rng.choice([0, 3, 4, 5, 6, 7, 10, 15, 16, 17, 18, 19, 20, 12, rng.getrandbits(16)]))
    img.u8(env.slab + OBJ + 0x4, rng.choice([0, 1, 0x40, 0x41, 0x81, 0xFF, rng.getrandbits(8)]))
    img.u8(env.slab + OBJ + 0x6A, rng.choice([0, 0, 0, 0x04, rng.getrandbits(8) & 0xFB]))      # (bit 2 set would fail the glow's assertion: the tail reaches it only then)
    img.u8(env.slab + OBJ + 0xD1, rng.choice([0, 1]))
    img.u32(env.slab + OBJ + 0xC8, rng.choice([0, 0, env.slab + SPRITE_B]))
    img.put(env.slab + SPRITE_B, noise(rng, 0x80))
    img.u32(env.slab + SPRITE_B + 0x44, rng.choice([0, 1, 2, 3, 5, 8]))
    threshold = rng.choice([100, 0x1000, 0x100000, rng.randrange(1, 0x200000)])
    img.u32(0x00563A3C, threshold)
    img.u32(env.slab + OBJ + 0xB8, rng.choice([0, threshold - 1, threshold, threshold + 1, rng.randrange(0, 0x400000)]))
    img.u32(0x00533F3C, rng.choice([1, 2, 2]))
    # the player record's list: the second player is the first one's next object
    img.put(env.slab + PLAYER_A, noise(rng, 0x40))
    img.put(env.slab + PLAYER_B, noise(rng, 0x40))
    img.u32(env.slab + PLAYER_A + 0x20, env.slab + PLAYER_B)
    for k in range(3):
        img.u32(env.slab + PLAYER_B + 8 + 4 * k, rng.choice([0, 0x1000, 0xFFFFF000, rng.randrange(-0x8000, 0x8000) & 0xFFFFFFFF]))
        img.u32(env.slab + OBJ + 8 + 4 * k, rng.choice([0, 0x1000, 0xFFFFF000, rng.randrange(-0x8000, 0x8000) & 0xFFFFFFFF]))
    img.u32(0x0056A960, env.slab + PLAYER_A)
    img.u32(0x0056A8D4, rng.choice([0, 1, 7, 100, 1000, rng.getrandbits(32)]))
    img.u32(0x005244CC, word(rng))
    return [], img.pokes()


@spec(0x004A8620, "CPowerUp__CheckAge", "wpa", this=lambda env: env.slab + OBJ)
def gen_check_age(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x230)
    img.u32(env.slab + OBJ, POWERUP_VTABLE)
    img.u16(env.slab + OBJ + 0xF0, rng.choice([0xFFFF, 0xFFFF, 0, 1, 2, 0x1D, 0x1E, 0x1F, 0x20, 0x3B, 0x3C, 0x3D, 0x100, rng.randrange(0, 0x40), rng.getrandbits(16)]))
    img.u16(env.slab + OBJ + 0xEA, rng.choice([0, 1, 0x32, 0x3C, 0x7FFF, 0xFFFF, rng.getrandbits(16)]))
    img.u32(env.slab + OBJ + 0xFC, rng.choice([0, 1, 0x7FFFFFFF, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u16(env.slab + OBJ + 0x4, rng.choice([0, 1, 0x41, 0xFFFF, 0xFFFE, rng.getrandbits(16)]))
    img.u32(0x0056A8D4, rng.choice([0, 1, 2, 3, 4, 5, rng.getrandbits(32)]))
    return [], img.pokes()


@spec(0x004A8300, "CPowerUp__DoPhysics", "wpd", this=lambda env: env.slab + OBJ)
def gen_do_physics(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x230)
    img.u32(env.slab + OBJ, POWERUP_VTABLE)
    o = env.slab + OBJ
    small = lambda: rng.choice([0, 1, 0xFFFFFFFF, 0x1000, 0xFFFFF000, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF])
    img.u8(o + 0xD2, rng.choice([0, 0, 1, 1, 2]))
    img.u8(o + 0xD3, rng.choice([0, 0, 1]))
    img.u8(o + 0xD4, rng.choice([0, 1, 2, 0x10, rng.getrandbits(8)]))
    ground = rng.choice([0, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFD, 0xFFFFFFFC, 0xFFFFFFFB, 0xFFFFFFFA, 5, 0x100000, rng.randrange(-0x800000, 0x800000) & 0xFFFFFFFF])
    img.u32(o + 0xD8, ground)
    for k in range(3):
        img.u32(o + 0xDC + 4 * k, small())
        img.u32(o + 0x4C + 4 * k, small())
        img.u32(o + 0x8 + 4 * k, small())
    img.u32(o + 0x5C, small())
    img.u8(o + 0x6D, rng.choice([0, 1, 5, 31, 32, 33, rng.getrandbits(8)]))
    if rng.random() < 0.5:        # home y at the landing limit, one unit either side
        limit = (ground - (rng.choice([0, 1, 2]) << 12)) & 0xFFFFFFFF
        img.u32(o + 0xE0, (limit + rng.choice([-1, 0, 1, 2, 0x1000])) & 0xFFFFFFFF)
        img.u32(o + 0x50, rng.choice([0, 1, 0xFFFFFFFF, 0x7FFF0000, 0x10000]))
    img.u32(o + 0x0C, small())
    for off in (0x70, 0x72, 0x74):
        img.u16(o + off, rng.choice([0, 1, 100, 0xFFFF, 0x8000, 0x7FFF, rng.getrandbits(16)]))
    for off in (0xE8, 0xEC, 0xEE):
        img.u16(o + off, rng.choice([0, 1, 0x32, 0xFFF, 0x1000, 0xFFFF, rng.getrandbits(16)]))
    follow = rng.random() < 0.35
    img.u16(o + 0xF6, rng.choice([1, 0x100, 0x1000, 0xFFFF, rng.getrandbits(16) | 1]) if follow else 0)
    img.u16(o + 0xF2, rng.getrandbits(16))
    img.u16(o + 0xF4, rng.getrandbits(16))
    img.put(env.slab + CAMERA, noise(rng, 0x50))
    img.u32(0x005620E0, env.slab + CAMERA)
    for k in range(9):
        img.u16(env.slab + CAMERA + 0x34 + 2 * k, rng.choice([0, 0x1000, 0xF000, rng.getrandbits(16)]))
    img.put(0x006A3E00, noise(rng, 0xD0))
    img.u32(0x0056865C, rng.choice([0, 0x100, 0x80, 0x200, 0x1000, rng.randrange(0, 0x400), rng.getrandbits(32)]))
    img.u32(0x0056A8D4, rng.choice([0, 1, 2, 3, 4, 5, 7, rng.getrandbits(32)]))
    return [], img.pokes()


# ---- 10g s21: CPowerUp__TakeEffect 0x004a86f0 (groups wpt, wpu) -----------------------------------------------------------------------------------------------------------------------------------

TAKE_GROUPS = ("wpt", "wpu")
SKATER_AT = 0x300                       # the skater's +0x2f64 (the base is 0x2f64 below it: +0x3068, +0x306c and +0x30b8 are in the slab too)
LETTER_TABLE, MONEY_TABLE, LINE, FILE = 0x0052EA00, 0x0052E424, 0x00568638, 0x0056881C
TAKE_GLOBALS = [(0x00533F3C, 4), (0x0056A960, 4), (LETTER_TABLE, 0x14), (MONEY_TABLE, 0xC), (LINE, 4), (FILE, 0x40)]
for _group in TAKE_GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = lambda slab: merged([(slab, SLAB)] + TAKE_GLOBALS, gap=0)
    READ_ONLY[_group] = [(POWERUP_VTABLE, 0x10), (0x005373EC, 0x40)]


def install_take_stubs(fc, env):
    from opentony.memory import mem
    from platform_specs import mark_under_test

    if getattr(fc, "_wpt_installed", False):
        return
    fc._wpt_installed = True
    install_base_stubs(fc, env)
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x00473650, 0, "engine_00473650")                                                         # Mess_DeleteAll
    ensure(fc, 0x004AD620, 3, "engine_004ad620", result=lambda a: rng.choice([0, 1, 0x12, 0x7FFFFFFF, 0xFFFFFFFF, rng.getrandbits(32)]))     # SFX_PlayX
    ensure(fc, 0x0048D7C0, 1, "engine_0048d7c0", result=lambda a: rng.choice([0, 0, 0, 1, 0x100, 0xFFFFFF00, 0xFFFFFFFF, rng.getrandbits(32)]))   # Panel_SkateLetterCollected
    ensure(fc, 0x004736F0, 5, "engine_004736f0")                                                         # Mess_AddMessage
    ensure(fc, 0x00414570, 1, "engine_00414570")                                                         # Career_GiveGoalType
    ensure(fc, 0x0048F5F0, 0, "engine_0048f5f0", convention="thiscall", this=True)                       # Trick_TakeOff
    ensure(fc, 0x004AD890, 2, "engine_004ad890")                                                         # SFX_SetVoiceFrequency
    ensure(fc, 0x00415180, 2, "engine_00415180")                                                         # Career_GiveMoney
    ensure(fc, 0x004152F0, 0, "engine_004152f0")                                                         # Career_GetLevelPickup
    ensure(fc, 0x0048F8F0, 0, "engine_0048f8f0", convention="thiscall", this=True)                       # Trick_Land
    ensure(fc, 0x004C8550, 1, "engine_004c8550", result=lambda a: rng.choice([0, 1, 0x1000, rng.getrandbits(32)]))     # TRG_GetNodeLinks
    ensure(fc, 0x004C5B00, 1, "engine_004c5b00")                                                         # TRG_ExecuteCommandList
    die = mem.u32(POWERUP_VTABLE + 8)
    ensure(fc, die, 0, "engine_%08x" % die, convention="thiscall", this=True, result=lambda a: rng.getrandbits(32))      # the die method (slot 2)


GROUP_STUBS.update({group: install_take_stubs for group in TAKE_GROUPS})

TAKE_TYPES = [4, 5, 6, 10, 14, 15, 16, 24, 25, 26, 33]


@spec(0x004A86F0, "CPowerUp__TakeEffect", "wpt", this=lambda env: env.slab + OBJ)
def gen_take_effect(env, n):
    return take_case(env, False)


@spec(0x004A86F0, "CPowerUp__TakeEffect", "wpu", this=lambda env: env.slab + OBJ)
def gen_take_effect_aimed(env, n):
    """The second fixture: the types the dispatch accepts, the refusal bits at their edges, every state of the skater's gates."""
    return take_case(env, True)


def take_case(env, aimed):
    img, rng = Image(), env.rng
    o = env.slab + OBJ
    fill_object(img, env, rng, 0x230)
    img.u32(o, POWERUP_VTABLE)
    skater = env.slab + SKATER_AT - 0x2F64
    other = env.slab + 0x440
    if aimed or rng.random() < 0.85:
        kind = rng.choice(TAKE_TYPES)
        if aimed and rng.random() < 0.15:
            kind |= rng.choice([0x100, 0x8000, 0x1000, 0xFF00])           # (a valid low byte under a set upper byte is no type)
    else:
        kind = rng.choice([0, 1, 3, 7, 8, 9, 11, 12, 13, 17, 18, 19, 20, 21, 22, 23, 27, 28, 29, 30, 31, 32, 34, 0x22, 0x100, 0x10004, 0xFFFF, 0xFFFE, rng.getrandbits(16)]) & 0xFFFF
    img.u16(o + 0x3C, kind)
    img.u8(o + 0x4, rng.choice([0, 0, 0, 1, 0x40, 0x41, 0x80, 0xFE, 0xBF, 0xFF, rng.getrandbits(8)]))
    img.u16(o + 0xB0, rng.choice([0, 1, 0x32, 0xFFFF, 0x8000, 0x7FFF, rng.getrandbits(16)]))
    img.u8(o + 0xD0, rng.choice([0, 1, 1, 0xFF, 0x80, rng.getrandbits(8)]))
    img.u16(o + 0xD6, rng.choice([0, 1, 0x7F, 0xFFFF, rng.getrandbits(16)]))
    img.u32(0x00533F3C, rng.choice([1, 1, 2, 2, 0, 3, 0x101, 0xFFFFFFFF] if not aimed else [1, 2, 2, 1, 0]))
    img.u32(0x0056A960, rng.choice([skater, skater, other, 0, rng.getrandbits(32)]))
    img.put(env.slab + SKATER_AT, noise(rng, 0x160))
    img.u32(skater + 0x2F64, rng.choice([0, 0, 0, 1, 0xFFFFFFFF, 0x100, rng.getrandbits(32)]))
    img.u32(skater + 0x30B8, rng.choice([0, 0, 0, 1, 0xFFFFFFFF, 0x10000, rng.getrandbits(32)]))
    img.u32(skater + 0x3068, rng.getrandbits(32))
    img.u32(skater + 0x306C, rng.getrandbits(32))
    for k in range(5):
        img.u32(LETTER_TABLE + 4 * k, rng.choice([0, 0, 0x534000 + rng.randrange(0x400), rng.getrandbits(32), 1]))
    for k in range(3):
        img.u32(MONEY_TABLE + 4 * k, rng.choice([0, 0x534000 + rng.randrange(0x400), rng.getrandbits(32), 1]))
    return [skater], img.pokes()
