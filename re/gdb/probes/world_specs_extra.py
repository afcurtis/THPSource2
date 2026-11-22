"""Phase 4.15c probe: the small methods of the platform, the power-up and the traffic object (group wpp): destructors and the setters between the big methods.

A fake object (0x230 bytes, all of it captured) stands for each; the power-up's kill method is slot 1 of its vtable (0x00519684), stubbed.

Slab layout (offsets): object 0x000, sprite A 0x280, sprite B 0x300, position 0x380.
"""

from __future__ import annotations

import os

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import OBJ, SPRITE_A, SPRITE_B, SPRITE_VTABLE, fake_sprite, fill_object, noise, sprite_pointer, word
from world_specs import install_stubs as install_base_stubs

GROUPS = ("wpp", "wpk")

POSITION = 0x380
SLAB = 0x400
GLOBALS = [(0x0056AF40, 8), (0x0056B830, 4), (0x0055F6B0, 0x10), (0x0055FAEC, 4), (0x0056E218, 4), (0x005244CC, 4), (0x0056A920, 4)]
POWERUP_VTABLE = 0x00519684


def regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = [(SPRITE_VTABLE, 8), (POWERUP_VTABLE, 8)]


def wpp(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "wpp", this=resolve)


def wpk(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "wpk", this=resolve)


def install_stubs(fc, env):
    from opentony.memory import mem
    from platform_specs import mark_under_test

    if getattr(fc, "_wpp_installed", False):
        return
    fc._wpp_installed = True
    install_base_stubs(fc, env)
    mark_under_test()
    ensure(fc, 0x004801F0, 1, "engine_004801f0", convention="thiscall", this=True)       # ObjectList_Remove
    ensure(fc, 0x004801D0, 1, "engine_004801d0", convention="thiscall", this=True)       # ObjectList_PushHead
    ensure(fc, 0x004AD9F0, 1, "engine_004ad9f0")                                          # SFX_StopVoice
    # the power-up's kill method (slot 1 of its class's vtable: Powerup_Release, a function under test in wpp, so these two functions have a group of their own where it is a stub)
    if "wpk" in os.environ.get("P410_GROUPS", "").split(","):
        kill = mem.u32(POWERUP_VTABLE + 4)
        ensure(fc, kill, 0, "engine_%08x" % kill, convention="thiscall", this=True, result=lambda a: env.rng.getrandbits(32))


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def state(img, env, rng, vtable=None):
    fill_object(img, env, rng, 0x230)
    fake_sprite(img, env, rng)
    img.u32(env.slab + OBJ + 0xA4, sprite_pointer(env, rng))
    for offset in (0x138, 0x13C, 0xEC, 0xE8):
        img.u32(env.slab + OBJ + offset, rng.choice([0, 0, env.slab + SPRITE_B, rng.getrandbits(32)]))
    img.u32(env.slab + OBJ + 0xC8, rng.choice([0, env.slab + SPRITE_A]))
    img.u32(env.slab + OBJ + 0x1D0, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 3, rng.getrandbits(32)]))
    img.u32(env.slab + OBJ + 0x204, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, rng.getrandbits(32)]))
    img.u32(0x0055F6B8, rng.choice([0, 1, 2, 5, rng.getrandbits(32)]))
    img.u32(0x0055FAEC, rng.choice([0, 1, 2, 5, rng.getrandbits(32)]))
    if vtable:
        img.u32(env.slab + OBJ, vtable)


@wpp(0x0049F340, "Platform_ScalarDelete", OBJ)
def gen_platform_scalar_delete(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@wpp(0x0049F360, "Platform_ConstructAt", OBJ)
def gen_platform_construct_at(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    img.put(env.slab + POSITION, noise(rng, 12))
    img.u32(0x0056E218, word(rng))
    return [env.slab + POSITION], img.pokes()


@wpp(0x0049F440, "Platform_Destruct", OBJ)
def gen_platform_destruct(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    return [], img.pokes()


@wpp(0x004A07D0, "Platform_EmptyHook", OBJ)
def gen_platform_hook(env, n):
    return [], Image().pokes()


@wpp(0x004A81D0, "Powerup_ScalarDelete", OBJ)
def gen_powerup_scalar_delete(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@wpp(0x004A81F0, "Powerup_Destruct", OBJ)
def gen_powerup_destruct(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    return [], img.pokes()


@wpk(0x004A8250, "maybe_destroy_model", OBJ)
def gen_powerup_kill(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng, vtable=POWERUP_VTABLE)
    return [], img.pokes()


@wpp(0x004A8260, "Powerup_SetFields", OBJ)
def gen_powerup_set_fields(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    return [word(rng), word(rng)], img.pokes()


@wpp(0x004A8280, "Powerup_AllowSprite")
def gen_powerup_allow(env, n):
    img, rng = Image(), env.rng
    img.u32(0x005244CC, word(rng))
    return [], img.pokes()


@wpp(0x004A8290, "Powerup_Release", OBJ)
def gen_powerup_release(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    return [], img.pokes()


@wpp(0x004A82C0, "CPowerUp__SetNode", OBJ)
def gen_powerup_set_node(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    return [word(rng)], img.pokes()


@wpk(0x004A82E0, "Powerup_MarkDying", OBJ)
def gen_powerup_mark_dying(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng, vtable=POWERUP_VTABLE)
    return [], img.pokes()


@wpp(0x004128A0, "TRG_DeleteTrafficObject", OBJ)
def gen_traffic_scalar_delete(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@wpp(0x004128C0, "TRG_DestroyTrafficObject", OBJ)
def gen_traffic_destruct(env, n):
    img, rng = Image(), env.rng
    state(img, env, rng)
    return [], img.pokes()
