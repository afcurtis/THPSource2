"""Phase 4.14c probe: the top of the call tree, `LaunchGame` and `main` (group top).

Both are sequences of calls into the rest of the game with a few reads and writes of globals; every callee is a stub that reports its call and answers a scripted value (the level loop and the front end
write the exit code the launcher tests). The font copy at the end of the failure branch and in `main` runs in the port.
"""

from __future__ import annotations

import struct

import core_specs_main as base
from core_specs_main import (ARENA, ARENA_SIZE, CAM0, CONFIG, FONT, GLOBALS, MATBLOCK, MATERIAL, P0, P1, PLAYER_WINDOWS, SCRATCH, SLAB, STATE, TEXT, mn_seed, u)
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("top",)

TEXTURE = 0x9C00           # the shadow texture block ("Shadow" resource -> +4 -> this): bytes at +4 / +9 / +10 / +11 are decremented
RESOURCE = 0x9C40          # the named resource (+4 points at the texture block)

WIDTH_AT = SCRATCH + 0x5E0
HEIGHT_AT = SCRATCH + 0x5E4


def top_regions(slab):
    windows = [(slab + FONT, 0x150), (slab + CONFIG, 0x100), (slab + MATERIAL, 0x40), (slab + TEXTURE, 0x20), (slab + RESOURCE, 0x10), (slab + TEXT, 0x100)]
    windows += [(slab + P0 + 0x2CC0, 4), (slab + P1 + 0x2CC0, 4)]
    windows += list(GLOBALS) + [(0x0056D450, 0x44 * 4 + 4), (0x00563A58, 4), (0x0056A386, 1), (0x0056A3A2, 1), (0x00533FA8, 0xD0), (0x0056B7F4, 4), (0x0056A8EC, 4), (0x0056A88C, 4), (0x00568618, 4)]
    return merged(windows, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = top_regions
    READ_ONLY[_group] = []
    SEEDS[_group] = mn_seed


def top(address, name):
    return spec(address, name, "top")


def cheat_value(args):
    return STATE["cheat"].get(args[0], 0)


def next_status(args):
    return STATE["levels"].pop(0) if STATE["levels"] else 8


TOP_SERVICES = [
    (0x00416980, 1, dict(result=cheat_value)),
    (0x004586F0, 2, {}),
    (0x00458730, 0, {}),
    (0x004F6E60, 1, {}),
    (0x004F6D00, 2, dict(stack_to={0: WIDTH_AT, 1: HEIGHT_AT}, pokes=[(0, 4, lambda a: STATE["dims"][0]), (1, 4, lambda a: STATE["dims"][1])])),
    (0x004524A0, 0, dict(writes=[(0x0056A8D0, 4, lambda a: STATE["load"])])),
    (0x0044ADA0, 1, {}),
    (0x0044B000, 1, dict(result=lambda a: STATE["slab"] + FONT)),
    (0x0046A600, 2, {}),
    (0x0044B0A0, 1, {}),
    (0x0045D680, 0, {}),
    (0x004B3680, 1, dict(result=lambda a: STATE["resource"])),
    (0x00416A00, 3, {}),
    (0x0048FD60, 3, dict(convention="thiscall", this=True)),
    (0x0044B470, 0, {}),
    (0x00416050, 1, dict(result=lambda a: STATE["slab"] + CONFIG)),
    (0x0046A3A0, 0, dict(writes=[(0x0056A8D0, 4, next_status)])),
    (0x00458500, 1, {}),
    (0x00448C60, 0, {}),
    (0x004691E0, 0, {}),
    (0x004C8130, 0, {}),
    (0x004AA5D0, 0, {}),
    (0x004C4E30, 0, {}),
    (0x0048B100, 0, {}),
    (0x004AC0B0, 0, {}),
    (0x00413F70, 0, {}),
    (0x00458760, 0, {}),
    (0x004302C0, 4, {}),
    (0x0046F460, 0, {}),
    (0x0046A500, 0, {}),
    (0x0046A580, 0, {}),
    (0x00452FF0, 0, {}),
    (0x0047A080, 0, {}),
    (0x00458840, 0, {}),
]


def install_top_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_top_installed", False):
        return
    fc._top_installed = True
    mark_under_test()
    stop_clock(fc)
    base.disable_movie_breakpoints()
    base.lock_scheduler()
    STATE["slab"] = env.slab
    for address, count, kw in TOP_SERVICES:
        ensure(fc, address, count, "engine_%08x" % address, **kw)


GROUP_STUBS = {group: install_top_stubs for group in GROUPS}


def prepare(env, rng):
    from core_specs_main_cases import begin

    begin(env, rng)
    STATE["levels"] = [rng.choice([7, 7, 8, 9]) for _ in range(rng.choice([0, 1, 2]))] + [rng.choice([8, 9, 0x16])]
    STATE["load"] = rng.choice([7, 7, 7, 9, 8])
    STATE["dims"] = (rng.choice([320, 640, 800, rng.randrange(1, 0x12000)]), rng.choice([240, 480, 600, rng.randrange(1, 0x12000)]))
    STATE["resource"] = env.slab + RESOURCE


def put_world(img, env, rng):
    slab = env.slab
    img.u32(0x0056A858, slab + P0)
    img.u32(0x0056A85C, rng.choice([0, slab + P1]))
    img.u32(slab + P0 + 0x2CC0, rng.randrange(0, 4))
    for address in (0x0056B7F4, 0x0056A8EC, 0x0056A88C, 0x0056DB8C, 0x00568618, 0x00561B9C):
        img.u32(address, rng.getrandbits(32))
    img.u8(0x0056A3A2, rng.getrandbits(8))
    img.u8(0x0056A386, rng.getrandbits(8))
    img.u32(0x00533F38, rng.choice([0, 1, 1, 7, 8, 9, 10]))
    img.u32(0x00533F3C, rng.choice([1, 2, 2]))
    img.u32(0x0056A89C, rng.choice([0, 0, 1]))
    img.u32(0x005618C8, rng.choice([0, 0, 0, 1]))
    img.u8(0x00561990, rng.choice([0, 1]))
    img.u8(0x005614C4, rng.choice([0, 0, 1]))
    img.u32(0x005613BC, rng.randrange(0, 4))
    for k in range(4):
        img.u32(0x0056D450 + 0x44 * k, rng.choice([0, slab + ARENA + 0x80 * k]))
    img.u32(0x00563A58, rng.getrandbits(32))
    img.u32(0x0056A898, rng.randrange(0, 15))
    img.put(slab + CONFIG, bytes(rng.getrandbits(8) for _ in range(0x100)))
    img.u16(0x0056B8D4, rng.choice([0, 1, 2, 0xFFFF, 0x8000, 0xFFFE]))
    img.u32(0x0052E9D0, rng.getrandbits(32))
    img.u32(0x0052E61C, rng.getrandbits(32))
    img.u32(0x0056A8D0, rng.getrandbits(32))
    img.put(slab + RESOURCE, bytes(rng.getrandbits(8) for _ in range(0x10)))
    img.u32(slab + RESOURCE + 4, slab + TEXTURE)          # (a missing resource or texture asserts and then writes through the null pointer)
    img.put(slab + TEXTURE, bytes(rng.getrandbits(8) for _ in range(0x20)))
    img.put(0x00533FA8, bytes(rng.getrandbits(8) for _ in range(0xD0)))
    for number in (8,):
        STATE["cheat"][number] = rng.choice([0x64, 0x64, 0, 0x32, 0xFF])


@top(0x0046A8D0, "LaunchGame")
def gen_launch_game(env, n):
    img, rng = Image(), env.rng
    prepare(env, rng)
    put_world(img, env, rng)
    return [rng.choice([0, 1, 2, 5, 7, 12, rng.randrange(0, 15)])], img.pokes()


@top(0x0046A770, "main")
def gen_main(env, n):
    img, rng = Image(), env.rng
    prepare(env, rng)
    slab = env.slab
    img.put(slab + FONT, bytes(rng.getrandbits(8) for _ in range(0x150)))
    img.put(0x005619E0, bytes(rng.getrandbits(8) for _ in range(0x14C)))
    img.u32(0x0052EB14, rng.getrandbits(32))
    return [], img.pokes()
