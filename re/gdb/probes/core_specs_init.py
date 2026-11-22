"""Phase 4.14c probe: init.cpp (group ini): the clean-up, restart and start-up sequences (0x00458270..0x004588b7).

Each function is a fixed sequence of calls into the rest of the game plus a few resets of global state. Every callee is a stub that reports its call (`engine_<address>`, cdecl, with the arguments the
code pushes); the virtual destructors the list sweeps call are two fake slots of a vtable in the scratch slab; the one function that is really called is `Utils_InitialRand` (it belongs to the
utils group but is a plain global write, so it runs for real). `Init_AtStart` allocates and clears a 6 MB block: the stub for `operator new` hands back a real block taken from the C runtime once, and
the replay (which does not hold that block) skips the clearing of it.

Slab layout (offsets): the fake vtable 0x300, the fake objects 0x400 + k * 0x80 (k < 16), the bits-list nodes 0xc00 + k * 0x10, the environment objects 0xd00 + k * 0x40.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import merged
from frontend_specs_menu import Image

GROUPS = ("ini",)

SLAB = 0x1000
VTABLE = 0x300
OBJECTS = 0x400
NODES = 0xC00
ENVIRONMENT = 0xD00

SLOT0 = 0x004E87B0                  # the fake vtable's slot 0 (deleting destructor: thiscall, one argument)
LISTS = [0x0056A960, 0x0055FA38, 0x0055F6BC, 0x0056AF40, 0x0055F6B0, 0x0056B830, 0x0056AF44]

WINDOWS = [(address, 4) for address in LISTS] + [
    (0x00563A38, 4), (0x00533FB6, 2), (0x0052CB38, 4), (0x00568648, 8), (0x0056B70C, 4), (0x0056B710, 4), (0x0056B73C, 4), (0x0056DB38, 4), (0x0056DB28, 4), (0x0056A8D4, 4), (0x00568684, 4),
    (0x00535AF8, 4), (0x00533F90, 8), (0x0056E28C, 0x28), (0x0052BE4C, 4), (0x00562018, 4), (0x0056A97C, 16), (0x0056A920, 4), (0x0056E224, 4), (0x0056B704, 4), (0x0056AF6C, 4),
    (0x0056DB44, 4), (0x0056C4DC, 4), (0x0055F728, 4), (0x005481A8, 4)]

STATE = {"big": 0}


def ini_regions(slab):
    return merged([(slab, SLAB)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = ini_regions
    READ_ONLY[_group] = []


def u(value):
    return value & 0xFFFFFFFF


def ini(address, name):
    return spec(address, name, "ini")


# (address, number of arguments, name) of the services: all cdecl, reported as engine_<address>
SERVICES = [
    (0x004DE000, 0), (0x004BD720, 0), (0x004AA8A0, 0), (0x00403760, 0), (0x00473650, 0), (0x004AD9B0, 0), (0x004AD9E0, 0), (0x00454E60, 0), (0x004ABC80, 0), (0x004C4B30, 0),
    (0x00411F00, 0), (0x004ADB90, 0), (0x00422EC0, 1), (0x004B32B0, 0), (0x004C5100, 0), (0x004CBCA0, 0), (0x00492B20, 0), (0x004ADA10, 0), (0x004AD5B0, 0), (0x0044C670, 0),
    (0x0048A320, 0), (0x00489420, 0), (0x004B34B0, 0), (0x004C4B10, 0), (0x004497E0, 0), (0x004BD700, 0), (0x00466080, 0), (0x004AA870, 0), (0x00489610, 0), (0x004301B0, 0),
    (0x004B1AC0, 0), (0x00489410, 0), (0x004ACFE0, 0), (0x0048A0B0, 2), (0x0044BE50, 0), (0x004CBA30, 0), (0x00411EF0, 0), (0x0046E8C0, 0), (0x0047A060, 0), (0x004894A0, 0),
    (0x004301A0, 0), (0x0040B4E0, 1), (0x00448F50, 0), (0x00454D00, 0), (0x004645F0, 0), (0x004ABA90, 0), (0x00417300, 0), (0x004138F0, 0), (0x004AC3F0, 0), (0x004B5190, 0),
    (0x004B3DF0, 0), (0x005002F3, 1)]


def install_ini_stubs(fc, env):
    from frontend_specs_front import ensure
    from opentony.fcall import ForcedCalls
    from platform_specs import mark_under_test

    if getattr(fc, "_ini_installed", False):
        return
    fc._ini_installed = True
    mark_under_test()
    for address, count in SERVICES:
        ensure(fc, address, count, "engine_%08x" % address)
    ensure(fc, SLOT0, 1, "engine_%08x" % SLOT0, convention="thiscall", this=True)
    STATE["big"] = ForcedCalls.call(0x00501BB4, (0x005DC000,))
    if STATE["big"] == 0:
        raise RuntimeError("no 6 MB block for Init_AtStart")
    ensure(fc, 0x005002FE, 1, "engine_005002fe", result=lambda a: STATE["big"])


GROUP_STUBS = {group: install_ini_stubs for group in GROUPS}


def make_object(img, env, rng, address, next_address=0, flag_clear=None):
    img.put(address, bytes(rng.getrandbits(8) for _ in range(0x80)))
    img.u32(address, env.slab + VTABLE)
    img.u32(address + 0x20, next_address)
    flag = rng.getrandbits(8) & 0xFF
    if flag_clear is True:
        flag &= ~0x20
    elif flag_clear is False:
        flag |= 0x20
    img.u8(address + 0x6A, flag)


def make_list(img, env, rng, count, slot):
    """`count` fake objects linked through +0x20; returns the head (0 for none) and the next free object slot."""
    addresses = [env.slab + OBJECTS + 0x80 * (slot + k) for k in range(count)]
    for k, address in enumerate(addresses):
        make_object(img, env, rng, address, addresses[k + 1] if k + 1 < count else 0)
    return (addresses[0] if addresses else 0), slot + count


def seed_vtable(img, env):
    img.put(env.slab + VTABLE, struct.pack("<II", SLOT0, 0x004E87D0))


def lists_world(img, env, rng):
    seed_vtable(img, env)
    slot = 0
    for address in LISTS:
        head, slot = make_list(img, env, rng, rng.choice([0, 0, 1, 2]), slot)
        img.u32(address, head)


@ini(0x004584D0, "DeleteList")
def gen_delete_list(env, n):
    img, rng = Image(), env.rng
    seed_vtable(img, env)
    head, _ = make_list(img, env, rng, rng.choice([0, 1, 2, 3, 6]), 0)
    return [head], img.pokes()


@ini(0x00458270, "Init_KillAll")
def gen_kill_all(env, n):
    img, rng = Image(), env.rng
    lists_world(img, env, rng)
    return [], img.pokes()


def cleanup_world(img, env, rng):
    lists_world(img, env, rng)
    img.u32(0x00563A38, rng.getrandbits(32))
    img.u16(0x00533FB6, rng.getrandbits(16))
    img.u32(0x0052CB38, rng.getrandbits(32))
    for k in range(2):
        img.u32(0x00568648 + 4 * k, rng.choice([0, 0, 0x00DB0010 + k, rng.getrandbits(32)]))
    slot = 8
    for address in (0x0056B73C, 0x0056B710, 0x0056B70C):
        if rng.random() < 0.6 or address == 0x0056B73C and rng.random() < 0.5:
            head, slot = make_list(img, env, rng, 1, slot)
            img.u32(address, head)
        else:
            img.u32(address, 0)
    nodes = []
    for k in range(rng.choice([0, 0, 1, 3])):
        nodes.append(env.slab + NODES + 0x10 * k)
    for k, node in enumerate(nodes):
        img.put(node, bytes(rng.getrandbits(8) for _ in range(0x10)))
        img.u32(node + 4, nodes[k + 1] if k + 1 < len(nodes) else 0)
    img.u32(0x0056DB38, nodes[0] if nodes else 0)
    environment = []
    for k in range(rng.choice([0, 1, 2, 3])):
        environment.append(env.slab + ENVIRONMENT + 0x40 * k)
    for k, address in enumerate(environment):
        img.put(address, bytes(rng.getrandbits(8) for _ in range(0x40)))
        img.u32(address + 0x20, environment[k + 1] if k + 1 < len(environment) else 0)
    img.u32(0x0056DB28, environment[0] if environment else 0)
    for address in (0x0056A8D4, 0x00568684, 0x00535AF8, 0x00533F90, 0x00533F94, 0x0052BE4C):
        img.u32(address, rng.getrandbits(32))
    img.put(0x0056E28C, bytes(rng.getrandbits(8) for _ in range(0x28)))


@ini(0x00458500, "Init_Cleanup")
def gen_cleanup(env, n):
    img, rng = Image(), env.rng
    cleanup_world(img, env, rng)
    return [u(rng.choice([0, 0, 0, 1, 1, 2, 2, 3, -1, 5]))], img.pokes()


@ini(0x00458690, "Init_ResetMemory")
def gen_reset_memory(env, n):
    img, rng = Image(), env.rng
    img.u32(0x00562018, rng.choice([0x00100000, 0x02000000, rng.getrandbits(32) & 0x7FFFFFF0]))
    img.put(0x0056A97C, bytes(rng.getrandbits(8) for _ in range(16)))
    for address in (0x0056A920, 0x0056E224, 0x0056B704, 0x0056AF6C, 0x0056DB44, 0x0056C4DC, 0x0055F728):
        img.u32(address, rng.getrandbits(32))
    return [rng.choice([0, 0x10, 0x100, 0x5DC000, rng.getrandbits(32) & 0xFFFFFF0])], img.pokes()


@ini(0x004586F0, "Init_Restart")
def gen_restart(env, n):
    img, rng = Image(), env.rng
    cleanup_world(img, env, rng)
    return [], img.pokes()


@ini(0x00458730, "Init_ForGame")
def gen_for_game(env, n):
    img, rng = Image(), env.rng
    img.u32(0x00563A38, rng.getrandbits(32))
    return [], img.pokes()


@ini(0x00458750, "Init_DebugStuff")
def gen_debug_stuff(env, n):
    return [], Image().pokes()


@ini(0x00458760, "Init_AtStart")
def gen_at_start(env, n):
    img, rng = Image(), env.rng
    img.u32(0x00562018, rng.getrandbits(32))
    img.u32(0x00563A38, rng.getrandbits(32))
    img.put(0x0056A97C, bytes(rng.getrandbits(8) for _ in range(16)))
    for address in (0x0056A920, 0x0056E224, 0x0056B704, 0x0056AF6C, 0x0056DB44, 0x0056C4DC, 0x0055F728):
        img.u32(address, rng.getrandbits(32))
    return [], img.pokes()


@ini(0x00458840, "Init_AtEnd")
def gen_at_end(env, n):
    img, rng = Image(), env.rng
    img.u32(0x00562018, rng.choice([0, 0, 0x01234560, rng.getrandbits(32)]))
    img.u32(0x005481A8, rng.getrandbits(32))
    return [], img.pokes()
