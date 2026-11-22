"""Phase 4.15c probe: the skater object's small methods (group wsk): the board bits, the destructor, the registry of reference-counted animation names, the light colour fade, the geometry helpers,
the flip to the velocity, the grunt sounds and the water droplet (0x0046c5f0..0x0046e4f0).

A fake skater (0x3600 bytes, all of it captured) stands for the CBruce; the sprite objects, a profile record, strings and the out values of the line intersection sit behind it.

Slab layout (offsets): skater 0x0000, sprite A 0x3700, sprite B 0x3780, profile 0x3800, strings 0x3980 (8 of 0x20), out values 0x3a80.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import SPRITE_VTABLE, word
from world_specs import install_stubs as install_base_stubs

GROUPS = ("wsk",)

SKATER = 0x0000
SKATER_BYTES = 0x3600
SPRITE_A = 0x3700
SPRITE_B = 0x3780
PROFILE = 0x3800
STRINGS = 0x3980
OUT = 0x3A80
CAMERA = 0x3B00
ARENA = 0x3C00
ARENA_SIZE = 0x100
SLAB = 0x3D00

GLOBALS = [(0x0056A95C, 4), (0x0056A88C, 4), (0x00533F3C, 4), (0x0056B7E8, 4), (0x0056A920, 0x60), (0x0056A898, 4), (0x00533F38, 4), (0x005685FC, 4), (0x0056A890, 4), (0x0056A86C, 4), (0x005614C4, 4), (0x0055FA38, 4), (0x005244CC, 4), (0x00568638, 4), (0x0056881C, 0x40)]   # (the last two: the assertion macro's line and file name, which Droplet_CheckedUpdate stamps)
READ = [(SPRITE_VTABLE, 8), (0x0053A514, 0x170 * 24), (0x0053C240, 0x1000), (0x0053DB10, 0x40), (0x0053DB80, 0x300), (0x005348F4, 0x40)]   # (the last: Droplet_CheckedUpdate's assertion text)


def regions(slab):
    return merged([(slab + SKATER, SKATER_BYTES), (slab + SPRITE_A, 0x100), (slab + PROFILE, 0x180), (slab + STRINGS, 0x100), (slab + OUT, 0x20), (slab + CAMERA + 0x504, 4), (slab + ARENA, ARENA_SIZE)] + GLOBALS, gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = READ
    SEEDS[_group] = seed


def wsk(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "wsk", this=resolve)


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_wsk_installed", False):
        return
    fc._wsk_installed = True
    install_base_stubs(fc, env)         # (the object base class's services)
    mark_under_test()
    rng = env.rng
    for address in (0x004B9770, 0x004BA060, 0x0044A060, 0x004311D0):                      # the panel's text members
        ensure(fc, address, 0, "engine_%08x" % address, convention="thiscall", this=True)
    ensure(fc, 0x004AD9F0, 1, "engine_004ad9f0")                                          # SFX_StopVoice
    ensure(fc, 0x004801F0, 1, "engine_004801f0", convention="thiscall", this=True)       # ObjectList_Remove
    ensure(fc, 0x0049C7D0, 0, "engine_0049c7d0", convention="thiscall", this=True)       # CBruce__FRUVectorsFromRotMatrix
    ensure(fc, 0x00464B90, 2, "engine_00464b90")                                          # M3D_ReadHooksPacket
    ensure(fc, 0x004AD620, 3, "engine_004ad620")                                          # SFX_PlayX
    ensure(fc, 0x0048D990, 1, "engine_0048d990", convention="thiscall", this=True)       # Panel_Construct
    ensure(fc, 0x00431190, 0, "engine_00431190", convention="thiscall", this=True)       # the panel member's constructor
    ensure(fc, 0x004801D0, 1, "engine_004801d0", convention="thiscall", this=True)       # ObjectList_PushHead


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def fake_sprites(img, env, rng):
    img.put(env.slab + SPRITE_A, noise(rng, 0x70))
    img.u32(env.slab + SPRITE_A, SPRITE_VTABLE)
    img.put(env.slab + SPRITE_B, noise(rng, 0x70))


def sprite_pointer(env, rng):
    return rng.choice([0, 0, env.slab + SPRITE_A])


def hash_of(text, seed):
    value = seed & 1
    for byte in text.encode():
        value = (((value << 1) | (value >> 31)) & 0xFFFFFFFF) ^ byte
    return value


NAMES = ["idle", "run", "ollie", "kickflip", "grind", "manual", "bail", "Idle"]


def put_names(img, env):
    for k, text in enumerate(NAMES):
        img.put(env.slab + STRINGS + 0x20 * k, text.encode() + b"\0")


# ---- the board bits and the orientation -----------------------------------------------------------------------------------------------------------------------------------------

def board_case(address, name):
    @wsk(address, name, SKATER)
    def gen(env, n):
        img, rng = Image(), env.rng
        for offset in (0x2CD0, 0x2CD4, 0x2CD8):
            img.u32(env.slab + offset, rng.choice([0, 1, 5, 17, 31, 32, 33, 0xFFFFFFFF, rng.getrandbits(32)]))
        img.u32(env.slab + 0x148, word(rng))
        return [], img.pokes()
    return gen


board_case(0x0046C5F0, "CBruce__BoardOff")
board_case(0x0046C630, "CBruce__BoardOn")


@wsk(0x0046C670, "Skater_ResetOrientationRows", SKATER)
def gen_reset_rows(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x2E38, noise(rng, 0x40))
    img.put(env.slab + 0x80, noise(rng, 6))
    img.put(env.slab + 0x3130, noise(rng, 0x10))
    return [], img.pokes()


# ---- destruction -----------------------------------------------------------------------------------------------------------------------------------------------------------------------

def skater_state(img, env, rng):
    fake_sprites(img, env, rng)
    img.u32(env.slab + 0xA4, sprite_pointer(env, rng))
    img.put(env.slab + 0x4, noise(rng, 0x80))
    img.u32(env.slab + 0xA4, sprite_pointer(env, rng))
    for offset in (0x3090, 0x3094):
        img.u32(env.slab + offset, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 5, rng.getrandbits(32)]))
    for offset in (0xE0, 0xE4, 0x138, 0x13C, 0xEC, 0xE8):
        img.u32(env.slab + offset, rng.choice([0, 0, env.slab + SPRITE_B, rng.getrandbits(32)]))
    img.u32(0x0056A95C, rng.choice([0, 1, 2, 3, rng.getrandbits(32)]))


@wsk(0x0046CBE0, "Skater_ScalarDelete", SKATER)
def gen_scalar_delete(env, n):
    img, rng = Image(), env.rng
    skater_state(img, env, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@wsk(0x0046CC00, "Skater_DestroyPanelMembers", SKATER)
def gen_panel_members(env, n):
    return [], Image().pokes()


@wsk(0x0046CCA0, "Skater_Destruct", SKATER)
def gen_destruct(env, n):
    img, rng = Image(), env.rng
    skater_state(img, env, rng)
    return [], img.pokes()


# ---- the registry of animation names ----------------------------------------------------------------------------------------------------------------------------------------

def put_registry(img, env, rng):
    put_names(img, env)
    count = rng.choice([0, 1, 2, 3, 5, 99, 100])
    img.u32(env.slab + 0x3214, count)
    full = count >= 99 and rng.random() < 0.6               # (a full registry with none of the names: the add runs out of room)
    for k in range(min(count, 100)):
        text = rng.choice(NAMES)
        img.u32(env.slab + 0x3218 + 8 * k, hash_of(text, rng.choice([0, 1])) if rng.random() < 0.7 and not full else rng.getrandbits(32))
        img.u16(env.slab + 0x321C + 8 * k, rng.choice([0, 1, 2, 3, 0xFFFF]))
        img.u16(env.slab + 0x321E + 8 * k, rng.getrandbits(16))


def name_args(env, rng):
    return env.slab + STRINGS + 0x20 * rng.randrange(len(NAMES)), rng.choice([0, 1, 2, 3, 0xFFFFFFFF, rng.getrandbits(32)])


@wsk(0x0046CDF0, "Skater_NameHash")
def gen_name_hash(env, n):
    img, rng = Image(), env.rng
    put_names(img, env)
    return list(name_args(env, rng)), img.pokes()


@wsk(0x0046CE20, "Skater_NameFind", SKATER)
def gen_name_find(env, n):
    img, rng = Image(), env.rng
    put_registry(img, env, rng)
    return list(name_args(env, rng)), img.pokes()


@wsk(0x0046CE60, "Skater_NameReset", SKATER)
def gen_name_reset(env, n):
    img, rng = Image(), env.rng
    img.u32(env.slab + 0x3214, word(rng))
    return [], img.pokes()


@wsk(0x0046CE70, "Skater_NameAdd", SKATER)
def gen_name_add(env, n):
    img, rng = Image(), env.rng
    put_registry(img, env, rng)
    return list(name_args(env, rng)), img.pokes()


@wsk(0x0046CF20, "Skater_NameRelease", SKATER)
def gen_name_release(env, n):
    img, rng = Image(), env.rng
    put_registry(img, env, rng)
    return list(name_args(env, rng)), img.pokes()


@wsk(0x0046CFE0, "Skater_NameCount", SKATER)
def gen_name_count(env, n):
    img, rng = Image(), env.rng
    put_registry(img, env, rng)
    return list(name_args(env, rng)), img.pokes()


# ---- the light colour and the geometry -------------------------------------------------------------------------------------------------------------------------------------------

@wsk(0x0046D010, "Skater_StepToward")
def gen_step_toward(env, n):
    rng = env.rng
    pick = [0, 1, 2, 5, 10, 100, 255, 0xFFFFFFFF, 0xFFFFFFF0, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32), rng.randrange(0, 300)]
    return [rng.choice(pick), rng.choice(pick), rng.choice(pick)], Image().pokes()


@wsk(0x0046D040, "Skater_FadeLightColour", SKATER)
def gen_fade(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x3174, noise(rng, 8))
    if rng.random() < 0.5:                    # (near the target, so the pending target takes over)
        for k in range(3):
            img.u8(env.slab + 0x3174 + k, rng.choice([0, 1, 2, 100]))
            img.u8(env.slab + 0x3177 + k, rng.choice([0, 1, 2, 100]))
        img.u8(env.slab + 0x317A, rng.choice([0, 1, 5, 255]))
    img.put(env.slab + 0x88, noise(rng, 8))
    return [], img.pokes()


@wsk(0x0046D1D0, "Skater_DistanceToLine")
def gen_distance(env, n):
    rng = env.rng
    while True:
        values = [rng.randrange(-3000, 3000) & 0xFFFFFFFF for _ in range(6)]
        if (values[4] - values[2]) & 0xFFFFFFFF or (values[5] - values[3]) & 0xFFFFFFFF:
            break
    return values, Image().pokes()


@wsk(0x0046D220, "Skater_LineIntersection")
def gen_intersection(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + OUT, noise(rng, 0x10))
    values = [rng.randrange(-2000, 2000) & 0xFFFFFFFF for _ in range(8)]
    if rng.random() < 0.2:                    # parallel lines
        values[6] = (values[4] + (values[2] - values[0])) & 0xFFFFFFFF
        values[7] = (values[5] + (values[3] - values[1])) & 0xFFFFFFFF
    return [*values, *[env.slab + OUT + 4 * k for k in range(4)]], img.pokes()


# ---- the rest ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@wsk(0x0046D6E0, "Skater_InputProfilePrep", SKATER)
def gen_input_profile(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + PROFILE + 0x140, noise(rng, 0x10))
    img.u32(env.slab + 0x2CCC, env.slab + PROFILE)
    img.u32(0x0056A88C, rng.choice([0, 1, 0xFF]))
    img.u32(0x00533F3C, rng.choice([1, 2, 1]))
    img.put(env.slab + 0x31A0, noise(rng, 4))
    return [], img.pokes()


@wsk(0x0046D750, "Skater_MarkDyingAndVisible", SKATER)
def gen_mark_dying(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x4, noise(rng, 0x80))
    return [], img.pokes()


@wsk(0x0046D940, "Player_BindHookPacket", SKATER)
def gen_bind_hooks(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x1A, noise(rng, 8))
    img.u32(env.slab + 0x2CC0, rng.choice([0, 1, 5, 20, 100]))
    return [rng.choice([0, env.slab + STRINGS, rng.getrandbits(32) | 1])], img.pokes()


@wsk(0x0046D970, "CBruce__FlipToVelocity", SKATER)
def gen_flip(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x4C, noise(rng, 12))
    img.u16(env.slab + 0xF6, rng.choice([0x22, 0x10, 0, rng.getrandbits(16)]))
    img.u32(env.slab + 0x2C7C, rng.choice([0, 0, 1]))
    img.u32(env.slab + 0x2C78, rng.choice([0, 0, 1]))
    img.u32(env.slab + 0xD8, word(rng))
    big = rng.random() < 0.6
    for k in range(3):
        img.u32(env.slab + 0x30F4 + 4 * k, rng.choice([0x1000, 0x0800, 0xFFFFF000, rng.getrandbits(16)]) & 0xFFFFFFFF)
        img.u32(env.slab + 0x4C + 4 * k, (rng.choice([0x10000, 0x8000, 0xFFFF8000, 0x3000]) if big else rng.getrandbits(14)) & 0xFFFFFFFF)
    img.put(env.slab + 0x3100, noise(rng, 12))
    img.put(env.slab + 0x2E58, noise(rng, 0x14))
    return [], img.pokes()


@wsk(0x0046DD20, "Skater_RequestBloodSplat", SKATER)
def gen_request_splat(env, n):
    img, rng = Image(), env.rng
    img.u32(env.slab + 0x2CC0, rng.randrange(0, 23))
    img.put(env.slab + 0x2C54, noise(rng, 12))
    return [word(rng), word(rng)], img.pokes()


@wsk(0x0046E1D0, "SFX_Grunt")
def gen_grunt(env, n):
    img, rng = Image(), env.rng
    img.u32(0x0056B7E8, rng.choice([0, 6, 7, 8, 9, 10, 11, 12, 13, rng.getrandbits(32)]))
    return [], img.pokes()


@wsk(0x0046E3F0, "Droplet_ScalarDelete", SKATER)
def gen_droplet_scalar_delete(env, n):
    img, rng = Image(), env.rng
    fake_sprites(img, env, rng)
    img.put(env.slab + 0x4, noise(rng, 0x80))
    img.u32(env.slab + 0xA4, sprite_pointer(env, rng))
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@wsk(0x0046E410, "Droplet_Destruct", SKATER)
def gen_droplet_destruct(env, n):
    img, rng = Image(), env.rng
    fake_sprites(img, env, rng)
    img.put(env.slab + 0x4, noise(rng, 0x80))
    img.u32(env.slab + 0xA4, sprite_pointer(env, rng))
    return [], img.pokes()


@wsk(0x0046E460, "Droplet_EmptyHook", SKATER)
def gen_droplet_hook(env, n):
    return [], Image().pokes()


@wsk(0x0046E470, "Droplet_Update", SKATER)
def gen_droplet_update(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x8, noise(rng, 0x10))
    img.put(env.slab + 0x14, noise(rng, 6))
    img.put(env.slab + 0x70, noise(rng, 6))
    img.put(env.slab + 0xD4, noise(rng, 0x18))
    return [], img.pokes()


@wsk(0x0046E4F0, "Droplet_CheckedUpdate", SKATER)
def gen_droplet_checked(env, n):
    """12 s10: the assertion on a flag byte (+0x6a, bit 2) and then Droplet_Update."""
    img, rng = Image(), env.rng
    img.put(env.slab + 0x8, noise(rng, 0x10))
    img.put(env.slab + 0x14, noise(rng, 6))
    img.put(env.slab + 0x70, noise(rng, 6))
    img.put(env.slab + 0xD4, noise(rng, 0x18))
    img.u8(env.slab + 0x6A, rng.choice([0, 4, 0xFB, 0xFF, rng.getrandbits(8)]))
    return [], img.pokes()


@wsk(0x0046D770, "Skater_EmptyHook", SKATER)
def gen_empty(env, n):
    return [], Image().pokes()


@wsk(0x0046D790, "Skater_EmptyHook1", SKATER)
def gen_empty1(env, n):
    return [word(env.rng)], Image().pokes()


@wsk(0x0046D780, "Skater_AlwaysTrue3", SKATER)
def gen_true3(env, n):
    return [word(env.rng), word(env.rng), word(env.rng)], Image().pokes()


@wsk(0x0046C720, "Skater_Construct", SKATER)
def gen_construct(env, n):
    img, rng = Image(), env.rng
    fake_sprites(img, env, rng)
    img.put(env.slab + 0x4, noise(rng, 0x100))
    img.put(env.slab + 0x2C00, noise(rng, 0x600))
    img.put(env.slab + 0x31A0, noise(rng, 0x50))
    img.u32(env.slab + 0xA4, 0)
    img.u32(0x0056A898, rng.choice([0, 1, 2, 3]))
    img.u32(0x00533F38, rng.choice([0, 1, 5, 7, 8, 9, 2]))
    img.u32(0x005685FC, word(rng))
    img.u32(0x0056A890, rng.choice([0, 1]))
    img.u32(0x0056A95C, rng.choice([0, 0, 1, 2, 3, 0xFFFFFFFF]))
    img.u32(0x0056A86C, rng.choice([0, 0, 1]))
    img.u32(0x00533F3C, rng.choice([1, 1, 2]))
    img.u32(0x005614C4, rng.choice([0, 1]))
    img.u32(0x0055FA38, rng.choice([0, env.slab + CAMERA]))
    return [], img.pokes()
