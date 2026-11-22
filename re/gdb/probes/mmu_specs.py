"""Phase 4.14b probe: the memory-card layer without its screen (group mmu; mmu.cpp 0x00473d20..0x0047ba00 except the window code, and the card backend 0x004e8d40..0x004e9130).

The card manager is an object of the game (0xdd0 bytes, a table of 15 file records and the state around it); the forced calls run it on a fake one in a scratch slab, with the records, display names,
buffers and flags poked per case. The functions of the group call each other for real (the backend helpers included); what is outside the group is a stub that reports its call and answers from a
per-case script: the C runtime's directory and stat calls, the PC file primitives (open, seek, read, write, close, create, remove), the card library's state call, `String_FindNoCase`, the career and
replay engine, the screen's constructor and `operator new`. The screen's own virtual calls go to a fake vtable whose slots are stubbed entry points.

Slab layout (offsets): the screen at 0x400 (0xdd0 bytes; the card manager is its sub-object at +0x1f8), four buffers at 0x1400 / 0x1900 / 0x1e00 / 0x2300 (0x500 each), text at 0x2800 (0x400), the fake vtable at 0x2c00, and the allocation arena
(what `alloc` and `operator new` hand out) at 0x3000 (0x2c00 bytes).
"""

from __future__ import annotations

import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Skip, spec
from frontend_specs_front import check_disjoint, ensure, merged
from frontend_specs_menu import Image
from platform_specs import mark_under_test, stop_clock

GROUPS = ("mmu",)

SLAB = 0x5C00
SCR = 0x0400                    # the screen object (the global MmuManager), 0xdd0 bytes
MGR = SCR + 0x1F8               # the card manager inside it (the `this` of the card methods)
BUF = (0x1400, 0x1900, 0x1E00, 0x2300)
BUF_BYTES = 0x500
TEXT = 0x2800
VT = 0x2C00
ARENA = 0x3000
ARENA_SIZE = 0x2C00

# the manager's fields
REC = 0x004
REC_STRIDE = 0x94
REC_SIZE = 0x88
TYPES = 0x8B0
NAMES = 0x928
NAME_STRIDE = 0x21
COUNT = 0xB18
STATE = 0xB1C
FREE = 0xB20
BUFCOUNT = 0xB28
BUFS = 0xB2C
BUFSIZES = 0xB3C

SB = 0x7FF00800        # pc_platform.hpp kScratchBase: what an argument that points into the caller's frame is recorded as
KA = SB + 0x400
KB = SB + 0x420
KFIND = SB + 0x440
KSTAT = SB + 0x560
KPATH = SB + 0x600
KHEADER = SB + 0x700
KLABEL = SB + 0x900
KDESC = SB + 0x980
KNAME = SB + 0xA00
KTOKEN = SB + 0xA80
TRANSFER_LIMIT = 0x400

# the globals
G_LEVEL_MODE = 0x00533F38
G_CAREER_LOAD_FLAG = 0x005350FC
G_RETURN_A = 0x00535100
G_RETURN_B = 0x00535104
G_CARD_NAME_FLAG = 0x0056ACF8
G_LOADED_FLAG = 0x0056ACF9
G_SKIP_FLAG = 0x0056ACFA
G_PARK_NAME_FLAG = 0x0056ACFB
G_SCREEN_MODE = 0x0056ACFC
G_MANAGER = 0x0056AD00
G_CAREER_BUFFER = 0x0056AD04
G_REPLAY_BUFFER = 0x0056AD08
G_PARK_NAME = 0x0056AB18
G_LOADED_NAME = 0x0056AB58
G_PARK_FILE = 0x0056ABD8
G_CAREER_FILE = 0x0056ABF8
G_PARK_BUFFER = 0x006A141C
G_PARK_BYTES = 0x006A1420
G_HANDLE = 0x0054B304
G_CARD_FLAG = 0x006A74BC
G_CARD_STATE = 0x006A74C0
G_ACTIVE_PLAYER = 0x0056A888
G_ACTIONS = 0x0056AFF9
G_REPLAY0 = 0x0056E5DC
G_REPLAY1 = 0x0056E454
G_REPLAY_FIELDS = 0x0056B8D4

# mutable windows (address, size): the globals above, the input actions of the two players, the replay fields
WINDOWS = [
    (0x0056AB18, 0x1F4),              # the names and flags 0x0056ab18..0x0056ad0c
    (0x005350FC, 0x0C),
    (0x006A141C, 0x08),
    (G_HANDLE, 4),
    (G_CARD_FLAG, 8),
    (G_LEVEL_MODE, 4),
    (G_ACTIVE_PLAYER, 4),
    (0x0056AFF8, 0x170),
    (G_REPLAY_FIELDS, 0x254),          # 0x0056b8d4..0x0056bb28: the replay fields and the two skater records
    (G_REPLAY1, 4),
    (G_REPLAY0, 0x78),                 # 0x0056e5dc..0x0056e654: stream buffer 0, frame count and highlight ranges
]
STATICS = [
    (0x0052E350, 0x40),                # the pointer tables (prefixes, labels)
    (0x00532B10, 0x28),                # the type labels
    (0x00532C20, 0x40),                # the file name prefixes
    (0x005351E0, 0x20),                # ".SAV", the format, "THPS2_"
    (0x00535270, 0x10),                # "*.SAV"
    (0x00518F80, 0x364),               # the three header templates
    (0x006A71BC, 0x100),               # the card directory
    (0x00540C34, 0x10),                # the path separator
]


def mmu_regions(slab):
    result = merged([(slab, SLAB)] + WINDOWS, gap=0)
    check_disjoint(result, STATICS)
    return result


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = mmu_regions
    READ_ONLY[_group] = STATICS


def seed_mmu(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    # the screen's virtual calls (delete, update, draw) go to entry points the stubs intercept
    return {env.slab + VT: struct.pack("<IIII", 0x00477910, 0x004780D0, 0x00479F60, 0)}


for _group in GROUPS:
    SEEDS[_group] = seed_mmu


def u(value):
    return value & 0xFFFFFFFF


def mmu(address, name, this=None, edx=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "mmu", this=resolve)


# ---- the per-case script of the stubs ---------------------------------------------------------------------------------------------------------------------------------------------

SCRIPT: dict = {}


def fresh(rng=None):
    """Forget the previous case's script: every queue starts empty (a stub answers its default when its queue is empty)."""
    keep = rng if rng is not None else SCRIPT.get("rng")
    SCRIPT.clear()
    SCRIPT.update(state=[], stat=[], names=[], handle=None, open=[], seek=[], read=[], write=[], create=[], remove=[], find=[], park=[], resource=[], reads=[], cur=(3, 0, 1), last_find=None, size=None, count=0, rng=keep)


fresh()

PREFIX = {1: "THPS2 CAREER- ", 2: "THPS2 REPLAY- ", 3: "THPS2 PARK- ", 4: "OTHER-FILE-- "}
USER_NAMES = ["A", "ALPHA", "Hawk", "BIRDMAN", "x", "NO NAME", "Tony H", "PARK 1", "My Park", "HAWK2", "zz9"]


def pop(queue, default):
    return queue.pop(0) if queue else default


def getstate_state(values):
    SCRIPT["cur"] = pop(SCRIPT["state"], (3, 0, 1))
    return SCRIPT["cur"][0]


def getstate_flag(values):
    return SCRIPT["cur"][1]


def getstate_result(values):
    return SCRIPT["cur"][2]


def find_no_case(values):
    from opentony.memory import mem

    haystack, needle = mem.cstring(values[0], 256), mem.cstring(values[1], 256)
    at = haystack.lower().find(needle.lower())
    if SCRIPT["find"]:
        return SCRIPT["find"].pop(0)(values[0], at)
    return values[0] + at if at >= 0 else 0


def stat_fill(values):
    mtime = pop(SCRIPT["stat"], (0, 0))
    SCRIPT["stat_result"] = mtime[1]
    return b"\0" * 0x1C + struct.pack("<i", mtime[0]) + b"\0" * 4


def stat_result(values):
    return u(SCRIPT.get("stat_result", 0))


def find_fill(values, first=True):
    name = SCRIPT["names"].pop(0) if SCRIPT["names"] else None
    SCRIPT["last_find"] = name
    if name is None:
        return b"\0" * 0x118
    return b"\0" * 0x14 + name.encode("latin-1")[:0x103] + b"\0" * (0x118 - 0x14 - len(name.encode("latin-1")[:0x103]))


def findfirst_result(values):
    if SCRIPT["last_find"] is None:
        return 0xFFFFFFFF
    return SCRIPT["handle"] if SCRIPT["handle"] is not None else 0x1234


def findnext_result(values):
    return 0 if SCRIPT["last_find"] is not None else 0xFFFFFFFF


def open_result(values):
    return u(pop(SCRIPT["open"], 7))


def create_result(values):
    return u(pop(SCRIPT["create"], 9))


def remove_result(values):
    return u(pop(SCRIPT["remove"], 0))


def seek_result(values):
    answer = pop(SCRIPT["seek"], None)
    return values[1] if answer is None else u(answer)


def read_fill(values):
    n = min(values[2], TRANSFER_LIMIT)
    data = pop(SCRIPT["reads"], b"")
    return (data + bytes(n))[:n]


def read_result(values):
    answer = pop(SCRIPT["read"], None)
    return values[2] if answer is None else u(answer)


def write_result(values):
    answer = pop(SCRIPT["write"], None)
    return values[2] if answer is None else u(answer)


def park_result(values):
    return u(pop(SCRIPT["park"], 0))


def resource_result(values):
    return u(pop(SCRIPT["resource"], 0x4000))


def install_mmu_stubs(fc, env):
    """Everything outside the group: the C runtime, the file primitives, the card library's state call, the career and replay engine and the screen."""
    if getattr(fc, "_mmu_installed", False):
        return
    fc._mmu_installed = True
    mark_under_test()
    stop_clock(fc)
    import gdb

    SCRIPT["rng"] = env.rng
    # void services
    for address, count in ((0x00452DC0, 0), (0x004165D0, 0), (0x004166F0, 0), (0x004F62C0, 4), (0x004CB810, 2), (0x004E73F0, 0), (0x004E7BC0, 1), (0x00449230, 1), (0x00449660, 0),
                           (0x004E8C90, 1), (0x004E8CA0, 0), (0x004E8CB0, 0), (0x004E8CC0, 0), (0x004E8CD0, 1), (0x004E8CF0, 1)):
        ensure(fc, address, count)
    ensure(fc, 0x004AD620, 3, "sfx")
    ensure(fc, 0x0046F2E0, 3, "mem_copy")
    ensure(fc, 0x00466BB0, 1, "operator_delete")
    ensure(fc, 0x00502604, 1, "crt_findclose")
    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    # the screen's constructor returns the object
    ensure(fc, 0x00476B00, 3, "engine_00476b00", convention="thiscall", this=True, result=lambda a: int(gdb.parse_and_eval("$ecx")) & 0xFFFFFFFF)
    # the screen's virtual methods (thiscall; the object comes first)
    ensure(fc, 0x00477910, 1, "engine_00477910", convention="thiscall", this=True)
    ensure(fc, 0x004780D0, 0, "engine_004780d0", convention="thiscall", this=True)
    ensure(fc, 0x00479F60, 2, "engine_00479f60", convention="thiscall", this=True)
    # services with answers
    ensure(fc, 0x004E8D10, 3, "engine_004e8d10", pokes=[(1, 4, getstate_state), (2, 4, getstate_flag)], result=getstate_result, stack_to={1: KA, 2: KB})
    ensure(fc, 0x004B82B0, 2, "engine_004b82b0", result=find_no_case)
    ensure(fc, 0x004F60D0, 1, "engine_004f60d0", result=park_result)
    ensure(fc, 0x00449030, 1, "engine_00449030", result=resource_result)
    ensure(fc, 0x005005D3, 2, "crt_stat", text=True, fill=(1, stat_fill), result=stat_result, stack_to={1: KSTAT})
    ensure(fc, 0x0050246F, 2, "crt_findfirst", text=True, fill=(1, find_fill), result=findfirst_result, stack_to={0: KPATH, 1: KFIND})
    ensure(fc, 0x0050253C, 2, "crt_findnext", fill=(1, lambda v: find_fill(v, False)), result=findnext_result, stack_to={1: KFIND})
    ensure(fc, 0x004E7500, 1, "engine_004e7500", text=True, result=remove_result)
    ensure(fc, 0x004E75B0, 3, "engine_004e75b0", text=True, result=open_result, stack_to={0: KNAME})
    ensure(fc, 0x004E7900, 2, "engine_004e7900", text=True, result=create_result, stack_to={0: KNAME})
    ensure(fc, 0x004E79F0, 3, "engine_004e79f0", result=seek_result)
    ensure(fc, 0x004E7AD0, 3, "engine_004e7ad0", fill=(1, read_fill), result=read_result, stack_to={1: KHEADER})
    ensure(fc, 0x004E7B60, 3, "engine_004e7b60", blob=(1, lambda a: min(a[2], TRANSFER_LIMIT)), result=write_result, stack_to={1: KHEADER})


GROUP_STUBS = {group: install_mmu_stubs for group in GROUPS}


# ---- the world --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def cstr(text, size=None):
    data = text.encode("latin-1") + b"\0"
    return data if size is None else (data + bytes(size))[:size]


def mgr_at(env):
    return env.slab + MGR


def rec_at(env, index):
    return mgr_at(env) + REC + REC_STRIDE * index


def user_name(rng):
    return rng.choice(USER_NAMES + ["".join(rng.choice("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ") for _ in range(rng.randrange(1, 12)))])


def file_name(rng, kind):
    return PREFIX[kind] + user_name(rng) + ".SAV"


def manager_world(img, env, rng, count=None, kinds=None, buffers=None, aligned=True):
    """A manager with `count` file records (names with the type's prefix, sizes in 8 KB blocks), their types and display names, the card state and free blocks, and `buffers` registered buffers."""
    fresh(rng)
    mgr = mgr_at(env)
    n = count if count is not None else rng.choice([0, 1, 2, 3, 4, 5, 8, 12, 14, 15, rng.randrange(0, 16)])
    SCRIPT["count"] = n
    scr = env.slab + SCR
    img.u32(scr, env.slab + VT)
    img.u32(mgr, rng.choice([0, 0x005184B4, 0x005192E4]))
    img.u32(mgr + COUNT, n)
    img.u32(mgr + STATE, rng.choice([0, 0, 1, 2, 3, 4, rng.getrandbits(32)]))
    img.u32(mgr + FREE, rng.choice([0, 3, 7, 12, 15, rng.getrandbits(32)]))
    for i in range(n):
        kind = kinds[i] if kinds else rng.choice([1, 2, 3, 4, 1, 2, 3])
        name = file_name(rng, kind)
        img.put(mgr + REC + REC_STRIDE * i, cstr(name, 0x84))
        blocks = {1: 1, 2: rng.choice([3, 6, 3, 6, 4]), 3: rng.choice([1, 2]), 4: rng.choice([1, 2, 3])}[kind]
        size = 0x2000 * blocks if aligned or rng.random() < 0.8 else 0x2000 * blocks + rng.choice([4, 0x80, 0x1FFF])
        img.u32(mgr + REC + REC_STRIDE * i + 0x84, size)
        img.u32(mgr + REC + REC_STRIDE * i + 0x88, rng.getrandbits(32))
        img.u32(mgr + TYPES + 4 * i, kind)
        img.put(mgr + NAMES + NAME_STRIDE * i, cstr(user_name(rng), NAME_STRIDE))
    count_buffers = buffers if buffers is not None else rng.choice([0, 1, 1, 2, 3, 4])
    img.u32(mgr + BUFCOUNT, count_buffers)
    for k in range(count_buffers):
        img.u32(mgr + BUFS + 4 * k, env.slab + BUF[k])
        img.u32(mgr + BUFSIZES + 4 * k, rng.choice([0x80, 0x100, 0x200, 0x280, 0x400, 0x500, 0x180]) if aligned or rng.random() < 0.8 else rng.choice([4, 0x7F, 0x81, 0x120]))
    # the screen's own words (the update and flag functions read them)
    img.u32(scr + 0xDA4, rng.choice([0, 1, 2, 3, 4, 5, 6]))
    img.u32(scr + 0xDC4, rng.choice([0, 1, 2, 7, rng.getrandbits(32)]))
    img.u32(scr + 0xDCC, rng.getrandbits(32))
    # the globals the functions read
    for address in (G_CARD_NAME_FLAG, G_LOADED_FLAG, G_SKIP_FLAG, G_PARK_NAME_FLAG):
        img.u8(address, rng.choice([0, 0, 1, rng.getrandbits(8)]))
    img.u32(G_SCREEN_MODE, rng.choice([0, 1, 2]))
    img.u32(G_PARK_BUFFER, rng.choice([0, env.slab + BUF[3]]))
    img.u32(G_PARK_BYTES, rng.choice([0, 0, 0, 0x100, 0x180]))
    img.u32(G_LEVEL_MODE, rng.choice([0, 1, 2, 5, 7, 7, 8]))
    img.u32(G_HANDLE, rng.choice([0, 7, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u8(G_CARD_FLAG, rng.choice([0, 1, 0x80]))
    img.u32(G_CARD_STATE, rng.choice([0, 1, 2, 3, 5]))
    for address in (G_PARK_NAME, G_LOADED_NAME, G_PARK_FILE, G_CAREER_FILE):
        img.put(address, cstr(rng.choice(["", "OLD", "THPS2 CAREER- OLD.SAV"]), 0x20))
    return mgr


def put_text(img, env, offset, text, size=None):
    address = env.slab + TEXT + offset
    img.put(address, cstr(text, size))
    return address


def pick_index(rng, count, wild=True):
    if count == 0 or (wild and rng.random() < 0.1):
        return rng.choice([-3, -1, 0, count, count + 1, 15, 17])
    return rng.randrange(0, count)
