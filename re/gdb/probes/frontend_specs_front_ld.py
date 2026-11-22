"""Phase 4.10i probe, part 5: the level loader group (fld): Front_LoadGame, Skater_LoadPlayerResources, PSX_ShareAnimationRegion, the wait for a button, the unlock checks and
the little quit-combination functions.

The loader reaches dozens of engine services (the spooler, the sound banks, the trigger script, the panel's bitmaps); they are the stubs of frontend_specs_front.py with
scripted answers. The group's arena blocks start with a fake vtable (the loader deletes the definition object it built through its virtual destructor).
"""

from __future__ import annotations

import struct

from create_specs import SEEDS, spec
from frontend_specs_front import VT_SPRITE, seed_front
from frontend_specs_front_cases import CURRENT_LEVEL, GAME_MODE, PLAYER_COUNT, STATUS, new_image, string_at

LEVEL_FLAGS = 0x0053C240


def fld(address, name):
    return spec(address, name, "fld")


def seed_load(env, rng):
    """The front groups' seed, and every arena block starts with the fake delete vtable (VT_SPRITE's first slot is the virtual delete)."""
    from opentony.fcall import write_chunks
    from frontend_specs_front import ARENA, ARENA_SIZE

    writes = seed_front(env, rng)
    block = struct.pack("<I", env.slab + VT_SPRITE)
    for offset in range(0, ARENA_SIZE, 16):
        write_chunks(env.slab + ARENA + offset, block)
    return writes


SEEDS["fld"] = seed_load


def character_flags(img, rng):
    for character in range(20):
        img.u32(0x0053A514 + character * 0x170, rng.choice([0, 0, 0, 1, 2, 2, 0x80, 0x82, 0x100, 3]))


def load_world(env, img, rng):
    character_flags(img, rng)
    img.u32(GAME_MODE, rng.choice([1, 1, 2, 3, 4, 7, 9, 10]))
    img.u32(PLAYER_COUNT, rng.choice([1, 1, 2, 2]))
    img.u32(CURRENT_LEVEL, rng.randrange(0, 14))
    img.u32(0x0056A89C, rng.choice([0, 1]))
    img.u8(0x005614C4, rng.choice([0, 0, 1]))
    img.u32(0x005618C8, rng.choice([0, 0, 0, 1]))
    img.put(0x0056B8D4, struct.pack("<H", rng.choice([0, 0, 1])))
    img.u32(0x0056867C, rng.choice([0, 0, 1]))
    img.u32(0x0056E208, rng.choice([0, 1, 3]))
    for k in range(20):
        img.u32(0x0056E1B8 + 4 * k, string_at(env, rng.randrange(0, 20)))
    for address in (0x0056AFF9, 0x0056B165, 0x0056B0D8, 0x0056B0E8, 0x0056B244, 0x0056B254):
        img.u8(address, rng.choice([0, 0, 0, 1]))
    img.u8(0x00561CBC, rng.choice([0, 0, 1]))
    img.u8(0x00561CBD, rng.choice([0, 0, 1]))
    img.u32(0x00561CC0, rng.choice([0, 0, 1000, 5, 100]))
    img.u8(0x00560080, rng.choice([0, 1]))
    img.u32(0x00561C90, rng.randrange(0, 14))
    img.u8(0x006A3B21, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056A68C, rng.randrange(0, 0x40))
    img.u32(0x0055FD78, rng.randrange(0, 14))
    img.u32(0x0056B834, string_at(env, 0))
    img.u8(0x00561990, rng.choice([0, 0, 1]))


@fld(0x004521C0, "Skater_LoadPlayerResources")
def gen_load_player(env, n):
    img, rng = new_image(env)
    load_world(env, img, rng)
    return [rng.choice([0, 1])], img.pokes()


@fld(0x00452390, "PSX_ShareAnimationRegion")
def gen_share_region(env, n):
    img, rng = new_image(env)
    load_world(env, img, rng)
    for k in range(40):
        img.u32(0x0056D43C + 4 * k, rng.getrandbits(32))
    return [rng.choice([rng.randrange(0, 12), rng.randrange(0, 12), 0xFFFFFFFF]), rng.randrange(0, 12), rng.choice([0, 1, 2])], img.pokes()


@fld(0x00452430, "Front_WaitForButton")
def gen_wait_for_button(env, n):
    img, rng = new_image(env)
    load_world(env, img, rng)
    clock = rng.randrange(1, 100000)
    img.u32(0x0056E31C, clock)
    img.u32(0x00561CC0, (clock - rng.choice([0, 100, 0xDC, 0xDD, 0x200])) & 0xFFFFFFFF)
    return [rng.choice([0, 0, 1, 0x100])], img.pokes()


@fld(0x004524A0, "Front_LoadGame")
def gen_load_game(env, n):
    img, rng = new_image(env)
    load_world(env, img, rng)
    return [], img.pokes()


@fld(0x00452A80, "Front_CheckUnlocks")
def gen_check_unlocks(env, n):
    img, rng = new_image(env)
    load_world(env, img, rng)
    img.u32(0x0056A68C, rng.choice([0, 4, 8, 0x10, 0x20, 0x3C, 0x1C, 0x3F, rng.randrange(0, 0x40)]))
    for character in range(20):
        img.u8(0x00568A6C + character * 0x104 + 1, rng.choice([0, 0, 1]))
        img.u8(0x00568A6C + character * 0x104 + 3, rng.choice([0, 3, 3, 1]))
    return [rng.randrange(0, 0x38)], img.pokes()


@fld(0x00452D00, "Front_UnlockContinue")
def gen_unlock_continue(env, n):
    img, rng = new_image(env)
    load_world(env, img, rng)
    img.u32(0x00561B8C, rng.randrange(0, 0x38))
    return [], img.pokes()


@fld(0x00452D30, "Front_QuitCombo")
def gen_quit_combo(env, n):
    img, rng = new_image(env)
    load_world(env, img, rng)
    clock = rng.randrange(1, 100000)
    img.u32(0x0056E31C, clock)
    img.u32(0x00561CC0, rng.choice([0, (clock - 0xDD) & 0xFFFFFFFF, (clock - 5) & 0xFFFFFFFF, (clock - 0xDC) & 0xFFFFFFFF]))
    return [], img.pokes()


@fld(0x00452DC0, "Front_QuitComboReset")
def gen_quit_reset(env, n):
    img, rng = new_image(env)
    img.u8(0x00561CBC, rng.choice([0, 1]))
    return [], img.pokes()


@fld(0x00452DD0, "Front_QuitComboSet")
def gen_quit_set(env, n):
    img, rng = new_image(env)
    img.u8(0x00561CBC, rng.choice([0, 1]))
    return [], img.pokes()


@fld(0x00452DE0, "Front_ReplayPlaying")
def gen_replay_playing(env, n):
    img, rng = new_image(env)
    img.put(0x0056B8D4, struct.pack("<h", rng.choice([-2, 0, 1, 2, 5])))
    img.u8(0x00561990, rng.choice([0, 1]))
    return [], img.pokes()
