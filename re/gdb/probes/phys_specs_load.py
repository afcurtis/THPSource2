"""Phase 4.15a probe: the tricks.bin loader and the trick script table parser of physics.cpp (group phl).

They work on the game's globals (the pointers to the tricks.bin tables, the extra animation list and its cursor) and on a fake tricks.bin in the scratch slab; the file loader (0x00448dd0), Mem_Delete, the script
interpreter's word reader (0x004be3c0) and command size (0x004bf6c0), the skater manager (0x004b18a0) and the runtime table builder (0x004bd1e0) are stubs; the log line is the shared `printf_log` stub.
The animation list at 0x005365ac is static data the functions also write (the extra animations go behind the fixed list).

Slab layout (offsets): the fake file at 0x000: seven header shorts (offsets to the tables), then the tables; the streams follow at 0x200. A stream is command bytes; the size of a command comes
from the size table below (the stub answers with it), command 7 ends a stream, command 1 is followed by a word (the animation).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from opentony.memory import mem

GROUPS = ("phl",)

SLAB = 0x600
TABLE_ONE = 0x040
TABLE_TWO = 0x0A0
STREAMS = 0x200

TRICKS_BINARY = 0x0056A894
HEADER = [0x00568688, 0x0056A840, 0x005685F0, 0x00568A50, 0x00568A4C, 0x0056A844, 0x0056A850]
ANIMATION_LIST = 0x005365AC
EXTRA_CURSOR = 0x0056B7F8
EXTRA_COUNT = 0x0056B7BC
PLAYERS = 0x00533F3C
FIXED_END = 0x0053663A                 # the end mark of the fixed list; the extra list starts at 0x0053663c

WINDOWS = [(TRICKS_BINARY, 4)] + [(a, 4) for a in HEADER] + [(EXTRA_CURSOR, 4), (EXTRA_COUNT, 4), (PLAYERS, 4), (ANIMATION_LIST, 0x300)]

SIZES = {0: 1, 1: 3, 2: 2, 3: 1, 4: 4, 5: 2, 6: 3}
STATE = {"file": 0, "slot": [0, 0, 0]}


def phl_regions(slab):
    return merged([(slab, SLAB)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = phl_regions
    READ_ONLY[_group] = []


def phl(address, name):
    return spec(address, name, "phl")


def command_size(a):
    return a[0] + SIZES.get(mem.u8(a[0]), 1)


def read_word(a):
    return mem.u16(mem.u32(a[0]))


def install_load_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_phl_installed", False):
        return
    fc._phl_installed = True
    mark_under_test()
    ensure(fc, 0x00448DD0, 2, "engine_00448dd0", result=lambda a: STATE["file"])
    ensure(fc, 0x0046F4D0, 1, "engine_0046f4d0")
    ensure(fc, 0x004BE3C0, 1, "engine_004be3c0", deref=[0], result=read_word)      # (the argument is the address of a local that holds the stream pointer: the record is the pointer)
    ensure(fc, 0x004BF6C0, 1, "engine_004bf6c0", result=command_size)
    ensure(fc, 0x004B18A0, 1, "engine_004b18a0", result=lambda a: STATE["slot"].pop(0) if STATE["slot"] else 0)
    ensure(fc, 0x004BD1E0, 0, "engine_004bd1e0")


GROUP_STUBS = {group: install_load_stubs for group in GROUPS}


def short(rng):
    return rng.choice([0, 1, 5, 100, 499, 500, 501, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(0, 600), rng.getrandbits(16)])


def stream(img, rng, address):
    """Command bytes at `address` (at most 33 bytes): animation commands (1, with a word) and others, ended by a 7."""
    at = address
    for _ in range(rng.choice([0, 1, 2, 4, 6])):
        command = rng.choice([1, 1, 1, 2, 3, 4, 5, 6, 0])
        img.u8(at, command)
        if command == 1:
            img.u16(at + 1, rng.choice([0, 1, 12, 100, 250, 500, rng.randrange(0, 501), rng.randrange(0, 501), 501, 0xFFFF]))
        at += SIZES[command]
    img.u8(at, 7)


def put_table(img, env, rng, base, streams_from, records):
    """A table of `records` records at the slab offset `base`: [buttons, that many button words, the offset of a stream, one more word], then a count of 0."""
    at = env.slab + base
    for k in range(records):
        buttons = rng.choice([1, 2, 3, 4, 4, 5])
        img.u16(at, buttons)
        for j in range(buttons):
            img.u16(at + 2 + 2 * j, rng.randrange(0, 20))
        offset = streams_from + 0x20 * k
        img.u16(at + 2 + 2 * buttons, offset)
        img.u16(at + 4 + 2 * buttons, rng.randrange(0, 20))
        stream(img, rng, env.slab + offset)
        at += 6 + 2 * buttons
    img.u16(at, 0)


def file_world(img, env):
    """A fake tricks.bin: the header offsets (the file's start is the slab)."""
    for k, offset in enumerate([0x40, 0x60, 0x70, TABLE_ONE, TABLE_TWO, 0x180, 0x1A0]):
        img.u16(env.slab + 2 * k, offset)
    STATE["file"] = env.slab


@phl(0x00492A90, "Tricks_LoadBinary")
def gen_load(env, n):
    img, rng = Image(), env.rng
    file_world(img, env)
    img.u32(TRICKS_BINARY, rng.choice([0, 0, 0, 0x01000000, rng.getrandbits(32)]))
    for address in HEADER:
        img.u32(address, rng.getrandbits(32))
    return [], img.pokes()


@phl(0x00492B20, "UnloadGTricks")
def gen_unload(env, n):
    img, rng = Image(), env.rng
    img.u32(TRICKS_BINARY, rng.choice([0, 0x01000000, rng.getrandbits(32)]))
    img.u32(HEADER[5], rng.getrandbits(32))
    return [], img.pokes()


def put_extra_list(img, rng, count):
    """The extra animations behind the fixed list: `count` shorts then the end mark. Returns the address of the end mark (where the cursor is)."""
    start = FIXED_END + 2
    for k in range(count):
        img.u16(start + 2 * k, rng.randrange(0, 501) if rng.random() < 0.9 else rng.randrange(0, 0x10000))
    end = start + 2 * count
    img.u16(end, 0xFFFF)
    return end


@phl(0x00492B60, "TrickScript_ResetExtraAnimations")
def gen_reset_extra(env, n):
    img, rng = Image(), env.rng
    img.u32(EXTRA_CURSOR, rng.getrandbits(32))
    img.u32(EXTRA_COUNT, rng.getrandbits(32))
    if rng.random() < 0.3:
        put_extra_list(img, rng, rng.choice([1, 5, 20]))
    return [], img.pokes()


@phl(0x00492BA0, "TrickScript_RegisterAnimation")
def gen_register(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 5, 20, 100, 298, 299, 300, 301])
    end = put_extra_list(img, rng, min(count, 40))
    img.u32(EXTRA_CURSOR, end)
    img.u32(EXTRA_COUNT, count)
    return [short(rng) | (rng.getrandbits(16) << 16 if rng.random() < 0.2 else 0)], img.pokes()


@phl(0x00492C70, "TrickScript_IsKnownAnimation")
def gen_in_lists(env, n):
    img, rng = Image(), env.rng
    put_extra_list(img, rng, rng.choice([0, 1, 3, 10, 40]))
    pick = rng.choice([0, 1, 12, 175, 216, 217, 300, 500, rng.randrange(0, 501), rng.randrange(0, 600)])
    return [pick | (rng.getrandbits(16) << 16 if rng.random() < 0.2 else 0)], img.pokes()


@phl(0x00492D10, "TrickScript_ParseStream")
def gen_parse_stream(env, n):
    img, rng = Image(), env.rng
    stream(img, rng, env.slab + STREAMS)
    end = put_extra_list(img, rng, rng.choice([0, 3, 20]))
    img.u32(EXTRA_CURSOR, end)
    img.u32(EXTRA_COUNT, rng.choice([0, 3, 20, 290, 298]))
    return [env.slab + STREAMS], img.pokes()


@phl(0x00492D50, "TrickScript_ParseTable")
def gen_parse_table(env, n):
    img, rng = Image(), env.rng
    img.u32(TRICKS_BINARY, env.slab)
    end = put_extra_list(img, rng, rng.choice([0, 3, 20]))
    img.u32(EXTRA_CURSOR, end)
    img.u32(EXTRA_COUNT, rng.choice([0, 3, 20, 290, 298]))
    put_table(img, env, rng, TABLE_ONE, STREAMS, rng.choice([0, 1, 2, 3, 4]))
    return [env.slab + TABLE_ONE], img.pokes()


@phl(0x00492DE0, "TrickScript_Initialize")
def gen_initialize(env, n):
    img, rng = Image(), env.rng
    file_world(img, env)
    img.u32(TRICKS_BINARY, rng.choice([0, 0, env.slab]))
    img.u32(PLAYERS, rng.choice([1, 2, 2, 3]))
    for address in HEADER:
        img.u32(address, rng.getrandbits(32))
    img.u32(HEADER[3], env.slab + TABLE_ONE)
    img.u32(HEADER[4], env.slab + TABLE_TWO)
    end = put_extra_list(img, rng, rng.choice([0, 3, 20]))
    img.u32(EXTRA_CURSOR, end)
    img.u32(EXTRA_COUNT, rng.choice([0, 3, 20]))
    STATE["slot"] = [rng.choice([-1, 0, 5, 0x14, 0x15, 100]) & 0xFFFFFFFF, rng.choice([0, 5, 0x14, 0x15, 100]), rng.choice([0, 5, 99])]
    put_table(img, env, rng, TABLE_ONE, STREAMS, rng.choice([0, 1, 2, 3]))
    put_table(img, env, rng, TABLE_TWO, STREAMS + 0x100, rng.choice([0, 1, 2, 3]))
    return [], img.pokes()
