"""Phase 4.13b2 probe: the network layer (groups mouse, nets, netp).

pc\\netgame.cpp's two-player layer and the mouse and text-entry code that shares its address range (0x004dbd90..0x004e1370). Same shape as audio_specs.py: the functions work on the game's
own globals (windows of the real address space, poked per case) and on a few fake objects in a scratch slab (two skaters, the cursor sprites, a text entry control, a slider row, a
file list, a message). DirectPlay, the message handlers, the font calls, the pad, the sound loops and every other engine routine are stubs; the nested functions of the range itself are
the real retail functions in every case.

netp.py-style note: the three groups share one set of windows, one seed and one stub installer, because the update function calls the state sender, the waiting loops call the
update, and the flags are read everywhere.

Slab layout (offsets): skaters 0 and 0x3100, the trick-config row 0x6200, the editor image cells 0x6300 / 0x6400, a message 0x8200, a custom-level message 0x8400, the DirectPlay
record 0xa500, the cursors 0xa600, the text entry control 0xa800, the slider / menu row object 0xa980 and its children, the file list 0xac80, strings 0xb000, the allocator arena 0xc000.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import LazyRanges, check_disjoint, ensure, merged, without
from frontend_specs_menu import Image
from platform_specs import mark_under_test, stop_clock

GROUPS = ("mouse", "nets", "netp")

KSTACK = 0x7FF00700            # what an argument that points at a stack local is recorded as (pc_platform.hpp kStackLocal)
KDEFAULT = 0x7FF00800          # the formatted default text of the options file (kScratchDefault)
KOUT = 0x7FF00900              # the buffer it answers into (kScratchOut)

SLAB = 0xE000
LOCAL = 0x0000                 # this machine's skater (0x30c0 bytes)
REMOTE = 0x3100                # the other machine's skater
CONFIG = 0x6200                # the trick configuration row TrickConfig_FindByCharacter answers
CELL = 0x6300                  # [0x005614cc] -> CELL, [CELL] -> IMAGE
IMAGE = 0x6400                 # the editor's park image (0x1e00 bytes)
MESSAGE = 0x8200               # a DirectPlay message (+0x10 -> BODY)
BODY = 0x8240
LEVELMSG = 0x8400              # a custom level message body (0x2028 bytes)
DPREC = 0xA500                 # a session record (+0x38 name, +0x3c password)
CURSOR_A = 0xA600
CURSOR_B = 0xA640
VT_CURSOR = 0xA700
TEXTEDIT = 0xA800
WIDGET = 0xA980
CHILD_A = 0xAAC0
CHILD_B = 0xAB20
SUBOBJ = 0xAB80
SPRITE_A = 0xABC0
SPRITE_B = 0xAC00
FILELIST = 0xAC80
IDS = 0xAD40
PICK_VT = 0xAD80               # a fake vtable: every slot goes to FAKE_PICK (a pick-by-mouse answer, scripted)
OBJ_B0 = 0xADA0                # the object behind +0xb0 of the arrow-children window (children at +0x3c, +0x40, +0x44)
PICK_CHILD = 0xADF0            # three children of 0x10 bytes (vtable first)
GRID_TABLE = 0xAE20            # the item table of the grid (12 pointers)
GRID_ITEMS = 0xAE60            # its items (12 of 0x10 bytes: vtable, key +4, enabled +0xc)
STRINGS = 0xB000               # 32 strings of 0x40 bytes
VT_EDIT = 0xAF40              # a fake vtable of the text entry control: every slot goes to FAKE_SETTEXT (slot 9 is the window method that draws the text)
ARENA = 0xC000                 # the allocator's arena (0x2000 bytes)
ARENA_SIZE = 0x2000

FAKE_DELETE = 0x004AD9B0       # unreached retail entry points the fake cursor table points at
FAKE_DRAW = 0x004AD9C0
FAKE_PICK2 = 0x004AD9E0        # (a jump, core_specs' FS_DONE) the fake vtable's slot 7: its own answers, so slots 4 and 7 can be told apart
FAKE_SETTEXT = 0x004AD9F0      # (an SFX function nothing here reaches) what the fake text entry vtable calls, stubbed
FAKE_PICK = 0x004AD9D0         # (SFX_Resume) nothing in this group reaches it: what the fake pick vtable calls, stubbed

CLOCK = 0x0056E31C

# the windows of the real address space the layer touches (mutable) ...
WINDOWS = [
    (0x005244CC, 4), (0x00533F38, 4), (0x00533FA0, 4), (0x0055F72C, 4), (0x0055FAF8, 4),
    (0x005614B8, 0x20), (0x00561BF8, 0x10), (0x00561C70, 8), (0x00561C90, 4), (0x00567C80, 4),
    (0x005685F0, 8), (0x00568618, 4), (0x00568638, 4), (0x00568658, 8), (0x00568818, 0x50),
    (0x0056A688, 2), (0x0056A850, 0x20), (0x0056A890, 0x20), (0x0056A8D0, 0x20), (0x0056A8F8, 0x10), (0x0056A948, 0x10),
    (0x0056AFF8, 8), (0x0056B010, 0x300), (0x0056B760, 4), (0x0056B798, 0x28), (0x0056B7DC, 8), (0x0056B7EC, 0x10),
    (0x0056DB5C, 0x10), (0x0056DB8C, 4), (CLOCK, 4),
    (0x006A05C0, 0x90), (0x006A1118, 8), (0x006A13E0, 0x60), (0x006A1540, 0x130), (0x006A16B0, 0x90),
    (0x006A1840, 0x2120), (0x006A3960, 0x180), (0x006A3B00, 0x400), (0x029DA394, 8), (0x006A696C, 4),
    (0x006A44F0, 8),      # the pointer the input layer reports (the pointer frame reads it)
]
# ... and the tables and strings it only reads
TABLES = [
    (0x005199BC, 0x20), (0x0052EAB8, 0x30), (0x0052EAE8, 4), (0x0052EB04, 0x28), (0x0053129C, 8), (0x005252F0, 4), (0x005256DC, 4), (0x0054A97C, 0x40),
    (0x0055FE10, 0x10), (0x0053D7B4, 0x40), (0x0053A510, 0x170 * 20 + 8), (0x0054A218, 8), (0x0055F6BC, 4), (0x0056865C, 4),
    (0x0052C740, 8),      # the text entry's cursor ("_")
    (0x0055D9D0, 0x308),  # the C runtime's character class table (the pointer at 0x0055dac8, characters -128..255) and MB_CUR_MAX, read by the ctype calls the text entry makes
    (0x0054A5F8, 0x20), (0x0054A5E4, 0x20), (0x0054A65C, 8), (0x0054AC78, 8),
]


def net_read_only():
    def build():
        from opentony.memory import mem

        strings = []
        for address in (0x0052EABC, 0x0052EAC0, 0x0052EAC4, 0x0052EAE8, 0x0052EB04, 0x0052EB08, 0x0052EB28):
            value = mem.u32(address)
            if 0x00520000 <= value < 0x00560000:
                strings.append((value, 0x80))
        # the key-name table: rows of 12 bytes {key, name, replacement} until the row whose name is null
        rows = 0
        while mem.u32(0x0054A3A8 + 12 * rows + 4) != 0:
            rows += 1
        table = [(0x0054A3A8, 12 * (rows + 1))]
        for k in range(rows):
            for field in (4, 8):
                value = mem.u32(0x0054A3A8 + 12 * k + field)
                if 0x00500000 <= value < 0x00560000:
                    strings.append((value, 0x40))
        return merged(without(TABLES + strings + table, WINDOWS), gap=0)

    return LazyRanges(build)


_RO = net_read_only()


def net_regions(slab):
    result = merged([(slab, SLAB)] + WINDOWS, gap=0)
    check_disjoint(result, _RO)
    return result


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = net_regions
    READ_ONLY[_group] = _RO


def seed_net(env, rng):
    """The fake cursor table, the editor image cells, the allocator's arena."""
    slab = env.slab
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    out = {}
    out[slab + CURSOR_A] = struct.pack("<I", slab + VT_CURSOR)
    out[slab + CURSOR_B] = struct.pack("<I", slab + VT_CURSOR)
    vtable = [0] * 8
    vtable[0] = FAKE_DELETE
    vtable[3] = FAKE_DRAW
    out[slab + VT_CURSOR] = struct.pack("<8I", *vtable)
    out[slab + CELL] = struct.pack("<I", slab + IMAGE)
    out[slab + PICK_VT] = struct.pack("<8I", *([FAKE_PICK] * 7 + [FAKE_PICK2]))
    out[slab + VT_EDIT] = struct.pack("<12I", *([FAKE_PICK] * 9 + [FAKE_SETTEXT] + [FAKE_PICK] * 2))      # (slot 9 draws the text; the other slots go elsewhere so a wrong slot shows)
    out[0x005614CC] = struct.pack("<I", slab + CELL)
    return out


for _group in GROUPS:
    SEEDS[_group] = seed_net


def u(value):
    return value & 0xFFFFFFFF


def mouse(address, name, this=None):
    return spec(address, name, "mouse", this=(lambda env: env.slab + this) if this is not None else None)


def nets(address, name, this=None):
    return spec(address, name, "nets", this=(lambda env: env.slab + this) if this is not None else None)


def netp(address, name, this=None):
    return spec(address, name, "netp", this=(lambda env: env.slab + this) if this is not None else None)


# per-case scripting of the stubs (reset by the generators)
STATE = {"keys": 0, "abort_after": 3, "polls": 0, "max_polls": 2, "found": 0, "slab": 0, "edit_key": 0, "edit_char": 0}


def reset_state(rng, env=None):
    STATE["keys"] = 0
    STATE["abort_after"] = rng.choice([1, 1, 2, 3, 5, 9])
    STATE["polls"] = 0
    STATE["max_polls"] = rng.choice([0, 0, 1, 2, 4])
    if env is not None:
        STATE["slab"] = env.slab


def string_at(env, index):
    return env.slab + STRINGS + 0x40 * index


def put_string(img, env, index, text):
    address = string_at(env, index)
    img.put(address, text.encode("latin-1") + b"\0")
    return address


def pick_clock(rng):
    return rng.choice([0, 1, 100, 5000, rng.randrange(1000, 100000), 0xFFFF0000, rng.getrandbits(32) & 0x7FFFFFFF])


# ---- the worlds ----------------------------------------------------------------------------------------------------------------------------------------------------------------

BITS = [0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000]


def flag_word(rng):
    word = 0
    for bit in BITS:
        if rng.random() < 0.3:
            word |= bit
    return word


def skater_fields(img, env, rng, base, index, *, tick=0):
    """The fields of a skater object the network layer reads."""
    img.u32(base + 0x2CC4, index)
    img.u32(base + 0x2CC8, rng.choice([0, 1, 0x1E, u(tick - 1), tick, u(tick + 30), rng.randrange(0, 0x40000), u(-5)]))
    img.u32(base + 0x2CDC, rng.randrange(0, 0x10000))
    img.u32(base + 0x2CE0, rng.randrange(0, 0x10000))
    img.u32(base + 0x16C, rng.randrange(0, 5000))
    img.u32(base + 0x2A8, rng.choice([0, 0, 1, rng.randrange(0, 5000)]))
    img.u32(base + 0x2858, rng.choice([0, 0, 1]))
    img.u32(base + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(base + 0x2DD4, rng.choice([0, 0, 0, 1]))
    img.u32(base + 0x30B8, rng.choice([0, 0, 7, 7, 1, 3]))
    img.u32(base + 0xD8, rng.getrandbits(32))
    for off in (0xF4, 0xF6, 0xFA, 0xFC, 0xFE, 0x164, 0x166):
        img.u16(base + off, rng.getrandbits(16))
    img.u8(base + 0xF8, rng.getrandbits(8))
    img.u8(base + 0x100, rng.getrandbits(8))
    img.put(base + 0x8, bytes(rng.getrandbits(8) for _ in range(12)))
    img.put(base + 0x14, bytes(rng.getrandbits(8) for _ in range(12)))
    img.put(base + 0x4C, bytes(rng.getrandbits(8) for _ in range(12)))
    img.put(base + 0x2E58, bytes(rng.getrandbits(8) for _ in range(20)))
    img.u16(base + 0x4, rng.getrandbits(16))
    img.u16(base + 0xB0, rng.getrandbits(16))
    img.u16(base + 0x3C, rng.getrandbits(16))
    img.u32(base + 0x1DC, rng.getrandbits(32))


def net_world(img, env, rng, *, flag=None, host=None, lost=None, state=None):
    """The network layer's statics: legal random values (the flag mostly set, the steps in 0..0x16)."""
    reset_state(rng, env)
    slab = env.slab
    clock = pick_clock(rng)
    img.u32(CLOCK, clock)
    img.u8(0x006A3B21, (rng.choice([1, 1, 1, 1, 0]) if flag is None else flag))
    img.u32(0x006A05D8, rng.choice([0, 1, 1, 0x1234]) if host is None else host)
    for address in (0x006A3B20, 0x006A3B22, 0x006A3B23, 0x006A3B25, 0x006A3B26, 0x006A3D18, 0x006A3D19, 0x006A3D24, 0x006A3D25, 0x006A3D26, 0x006A3D60, 0x006A3ABC, 0x006A3ABD,
                    0x006A3ABE, 0x006A18DC, 0x006A18DD, 0x006A18DE, 0x006A1417, 0x006A3C11, 0x006A3C12, 0x006A3C13, 0x006A3D34, 0x006A3D35, 0x006A3DE1, 0x006A3DE2):
        img.u8(address, rng.choice([0, 0, 1]))
    img.u8(0x006A3B30, rng.choice([0, 0, 0, 1]) if lost is None else lost)
    steps = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 0x30]
    img.u32(0x006A3A90, rng.choice(steps) if state is None else state)
    img.u32(0x006A18B0, rng.choice(steps))
    img.u32(0x006A3A98, rng.choice(steps))
    img.u32(0x006A18B8, rng.choice(steps))
    img.u32(0x006A3A9C, flag_word(rng))
    img.u32(0x006A18BC, flag_word(rng))
    for address in (0x006A3AA8, 0x006A18C8, 0x006A3AB0, 0x006A18D0, 0x006A3A8C, 0x006A18AC, 0x006A3D14, 0x006A3D54, 0x006A3D58, 0x006A3D44, 0x006A3DE4, 0x006A3DEC):
        img.u32(address, rng.choice([0, 1, 2, 5, rng.randrange(0, 70000)]))
    img.u32(0x006A3AAC, rng.choice([0, 0xA, 0xA, 0xB]))
    img.u32(0x006A18CC, rng.choice([0, 0xA, 0xA, 0xB]))
    img.u32(0x006A3D28, rng.choice([0, 1]))
    img.u32(0x006A3D2C, rng.choice([0, 1]))
    img.u32(0x006A3D1C, rng.choice([0, 1, 5, 0x3C, 0xC8, 0x40]))
    img.u32(0x006A3D20, rng.choice([0, 0x14, 0x1E, 0x3C]))
    img.u32(0x006A3D3C, rng.choice([0, 0, clock, u(clock - 5), u(clock - 5000), rng.getrandbits(32)]))
    img.u32(0x006A3D40, rng.choice([0, 1, 2, 3]))
    img.u32(0x006A3D4C, rng.getrandbits(32))
    img.u32(0x006A3D50, rng.choice([0, 1, 5, 8, 9]))
    img.u32(0x006A3B00, rng.choice([0, 1, 100, u(-100)]))
    img.u32(0x006A3B0C, rng.choice([clock, u(clock - 1000), u(clock + 1000), 0]))
    img.u32(0x006A3B28, rng.choice([clock, u(clock - 100), u(clock - 1000), u(clock - 5000), u(clock + 5)]))
    img.u32(0x006A3DF0, rng.choice([0, 1, 3, 19, 20, 40, 60, 199, 200, rng.randrange(0, 100000)]))
    img.u32(0x006A3DF4, rng.choice([0, clock, u(clock - 10), u(clock + 10)]))
    img.u32(0x006A3DF8, rng.choice([0, clock, u(clock - 10), u(clock + 10)]))
    img.u32(0x006A3DFC, rng.choice([0, u(clock - 10), clock, u(clock + 10)]))
    img.u32(0x006A395C, rng.choice([0, u(clock - 10), clock, u(clock + 10)]))
    img.u32(0x006A1438, rng.choice([0, u(clock - 10), clock, u(clock + 10)]))
    img.u32(0x006A1934, rng.choice([0x64, 0x64, 0, 1, 3, 7]))
    img.u32(0x006A18B4, rng.choice([0, 1, 1, 2, 0x33]))
    img.u32(0x006A1920, rng.choice([0, 1, u(-1), 100]))
    img.u32(0x006A1924, rng.getrandbits(16))
    img.u32(0x006A1928, rng.getrandbits(32))
    img.u32(0x006A1868, rng.getrandbits(32))
    img.u32(0x006A3AB4, rng.randrange(0, 20))
    img.u32(0x006A3AB8, rng.randrange(0, 8))
    img.u32(0x006A3A88, slab + LOCAL if rng.random() < 0.9 else 0)
    img.u32(0x006A18A8, slab + REMOTE if rng.random() < 0.9 else 0)
    img.u32(0x006A3DC8, 0x006A1938)
    # the game's own words
    tick = rng.choice([0, 0x40, 0x78, 0x100, 0x1000])
    img.u32(0x005685F4, tick)
    img.u32(0x00533F38, rng.choice([0, 1, 5, 5, 7, 8, 8, 9, 9, 12]))
    img.u32(0x00561C74, rng.choice([0, 1, 5, 5, 8, 9, 9, 6]))
    img.u32(0x00561C04, rng.choice([0, 0, 1, 0x33]))
    img.u32(0x00561BFC, rng.choice([0, 0, 0x33, 0xD, 5]))
    img.u32(0x00561C90, rng.randrange(0, 14) if rng.random() < 0.85 else 0xD)
    img.u32(0x0056DB64, rng.choice([0, 1, 2]))
    img.u32(0x0056A8E0, rng.choice([0, 1]))
    img.u32(0x0056A8D0, rng.getrandbits(32))
    img.u32(0x0056A858, rng.choice([slab + LOCAL, slab + REMOTE]))
    img.u32(0x0056A85C, rng.choice([slab + LOCAL, slab + REMOTE]))
    img.u32(0x0055F72C, rng.choice([0, 100, 0xFA, 0x200, u(-5)]))
    img.u32(0x005614BC, rng.choice([0, 1, 3]))
    img.u8(0x005614C4, rng.choice([0, 1]))
    for address in (0x0056B760, 0x0056DB8C, 0x0056A890, 0x0056A868, 0x0056B7F4, 0x0056A8F8, 0x0056A948, 0x00533FA0, 0x0056A904, 0x0056B7EC):
        img.u32(address, rng.choice([0, 1, rng.getrandbits(32)]))
    for address in (0x0056B798, 0x0056B79C, 0x0056B7A0, 0x0056B7A4, 0x0056B7B0, 0x0056B7B4, 0x0056B7DC, 0x0056B7E0):
        img.u32(address, rng.getrandbits(32))
    img.u8(0x0056A688, rng.getrandbits(8))
    img.u8(0x0056A689, rng.getrandbits(8))
    # the texts
    for address, size in ((0x006A3B71, 0x40), (0x006A3BB1, 0x40), (0x006A3B31, 0x40), (0x006A3C93, 0x40), (0x006A3BD1, 0x40), (0x006A18DF, 0x20), (0x006A3C13, 0x40)):
        text = rng.choice([b"", b"Host", b"Tony", b"192.168.0.1", b"a b", b"NETPARK", bytes(rng.randrange(33, 127) for _ in range(rng.randrange(1, 20)))])
        img.put(address, text + b"\0" + bytes(size - len(text) - 1))
    # the trick-config byte, the player flag table is read-only (0x0053a514 + 0x170 * slot)
    skater_fields(img, env, rng, slab + LOCAL, rng.choice([0, 1]), tick=tick)
    skater_fields(img, env, rng, slab + REMOTE, rng.choice([0, 1]), tick=tick)
    img.put(slab + CONFIG, bytes(rng.getrandbits(8) for _ in range(0x100)))
    img.put(slab + IMAGE, bytes(rng.getrandbits(8) for _ in range(0x40)))
    return clock


# ---- the stubs --------------------------------------------------------------------------------------------------------------------------------------------------------------

def ecx():
    import gdb

    return int(gdb.parse_and_eval("$ecx")) & 0xFFFFFFFF


def install_net_stubs(fc, env):
    """DirectPlay, the message handlers, the font calls, the pad, the sound loops and the engine routines outside the range."""
    if getattr(fc, "_net_installed", False):
        return
    fc._net_installed = True
    mark_under_test()
    stop_clock(fc)
    from opentony.memory import mem

    rng = env.rng
    slab = env.slab
    chance = lambda p: (lambda a: 1 if rng.random() < p else 0)
    # the mouse side
    ensure(fc, 0x00449460, 1, "engine_00449460", result=chance(0.65))
    ensure(fc, 0x00466B70, 1, "engine_00466b70", result=lambda a: 0 if rng.random() < 0.1 else env.alloc([max(a[0], 8)]))
    ensure(fc, 0x00457420, 4, "engine_00457420", convention="thiscall", this=True, result=lambda a: ecx())
    ensure(fc, 0x00466BB0, 1, "engine_00466bb0")
    ensure(fc, 0x0046FC50, 6, "engine_0046fc50", convention="thiscall", this=True)
    ensure(fc, 0x0046FE80, 0, "engine_0046fe80", convention="thiscall", this=True)
    ensure(fc, 0x004734B0, 1, "engine_004734b0")
    ensure(fc, 0x004734F0, 1, "engine_004734f0")
    ensure(fc, 0x00473500, 0, "engine_00473500", result=lambda a: rng.choice([0, 0x10, 0x30, 0xC0, 0xC8]))
    ensure(fc, 0x00473580, 4, "engine_00473580")
    ensure(fc, 0x004739E0, 4, "engine_004739e0")
    ensure(fc, 0x004DBF90, 4, "engine_004dbf90", result=chance(0.5))
    ensure(fc, 0x004E4190, 1, "engine_004e4190", masks={0: 0xFFFF})
    ensure(fc, 0x004E4C10, 2, "engine_004e4c10")
    ensure(fc, 0x004E41D0, 1, "engine_004e41d0", result=lambda a: STATE["edit_key"])
    ensure(fc, 0x004E4230, 1, "engine_004e4230", result=lambda a: STATE["edit_char"])
    ensure(fc, FAKE_SETTEXT, 4, f"engine_{FAKE_SETTEXT:08x}", convention="thiscall", this=True)
    ensure(fc, 0x0044B770, 1, "engine_0044b770", convention="thiscall", this=True)
    ensure(fc, 0x004F6560, 5, "engine_004f6560", result=lambda a: STATE["found"])
    ensure(fc, FAKE_DELETE, 1, f"engine_{FAKE_DELETE:08x}", convention="thiscall", this=True)
    ensure(fc, FAKE_DRAW, 3, f"engine_{FAKE_DRAW:08x}", convention="thiscall", this=True)
    ensure(fc, FAKE_PICK, 0, f"engine_{FAKE_PICK:08x}", convention="thiscall", this=True, result=chance(0.6))
    ensure(fc, FAKE_PICK2, 0, f"engine_{FAKE_PICK2:08x}", convention="thiscall", this=True, result=chance(0.5))
    # DirectPlay and the netgame routines outside the range's functions
    ensure(fc, 0x004DB1A0, 1, "engine_004db1a0")
    ensure(fc, 0x004DBA20, 1, "engine_004dba20", result=lambda a: rng.choice([0, slab + DPREC, slab + DPREC]))
    ensure(fc, 0x004DBC50, 3, "engine_004dbc50", result=lambda a: rng.choice([0xFFFFFFFF, 0, 5]))
    ensure(fc, 0x004DBCC0, 0, "engine_004dbcc0")
    ensure(fc, 0x004DDBF0, 0, "engine_004ddbf0")
    ensure(fc, 0x004DD980, 0, "engine_004dd980")
    ensure(fc, 0x004DEA30, 0, "engine_004dea30")
    ensure(fc, 0x004DEC50, 0, "engine_004dec50")
    ensure(fc, 0x004DEFF0, 0, "engine_004deff0")
    ensure(fc, 0x004DF420, 0, "engine_004df420")
    ensure(fc, 0x004DF170, 2, "engine_004df170")
    ensure(fc, 0x004E00C0, 2, "engine_004e00c0")
    ensure(fc, 0x004E0120, 2, "engine_004e0120")
    ensure(fc, 0x004E0180, 0, "engine_004e0180")
    ensure(fc, 0x004E0210, 0, "engine_004e0210")
    # the message handler's effects the update reads back: a state message raises the "remote changed" byte; handling takes a few ticks now and then
    ensure(fc, 0x004E0DD0, 2, "engine_004e0dd0",
           writes=[(0x006A3D24, 1, lambda a: rng.choice([0, 1])), (CLOCK, 4, lambda a: (mem.u32(CLOCK) + rng.choice([0, 0, 0, 1, 0x20, 0x50])) & 0xFFFFFFFF),
                   (0x006A18B0, 4, lambda a: rng.choice([mem.u32(0x006A18B0)] * 3 + [0xB, 0xC, 0xF, 0x10, 0x12, 0x14, 0x15, 5]))])
    ensure(fc, 0x004DB2D0, 1, "engine_004db2d0", result=lambda a: rng.choice([0, 3, 0x28, 0x29, 100]))
    ensure(fc, 0x004DB1C0, 1, "engine_004db1c0", result=poll_result)
    ensure(fc, 0x004DB200, 1, "engine_004db200", result=lambda a: slab + MESSAGE)
    # the two senders: stubbed on their call sites (the descriptor is a stack local; the size follows the text for the chat packet)
    fc.stub_site(0x004DF40C, 5, 4, "engine_004db350", stack_to=KSTACK, blob=(1, lambda v: 25 + len(mem.cstring(0x006A3C93, 0x100))), blank=lambda: [(0, 12), (16, 4)])
    fc.stub_site(0x004DF9E6, 5, 4, "engine_004db350", stack_to=KSTACK, blob=(1, 20), blank=lambda: [(0, 12)])
    # the engine's other routines
    ensure(fc, 0x004E4A90, 0, "engine_004e4a90")
    ensure(fc, 0x004E41B0, 1, "engine_004e41b0", result=key_result)
    ensure(fc, 0x00432980, 2, "engine_00432980")
    ensure(fc, 0x004326A0, 0, "engine_004326a0", writes=[(0x005614CC, 4, lambda a: slab + CELL)])
    ensure(fc, 0x004F62C0, 4, "engine_004f62c0")
    ensure(fc, 0x004F6140, 4, "engine_004f6140", result=lambda a: rng.getrandbits(32))
    ensure(fc, 0x0047FC70, 0, "engine_0047fc70")
    ensure(fc, 0x004F6CA0, 4, "engine_004f6ca0", text=2, stack_to={2: KDEFAULT, 3: KOUT}, fill=(3, setting_answer))
    ensure(fc, 0x004F6CD0, 3, "engine_004f6cd0", text=2, stack_to={2: KDEFAULT})
    ensure(fc, 0x004B0C60, 1, "engine_004b0c60", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1, 2, 5, 10, 15, 19]))
    ensure(fc, 0x004B0CD0, 1, "engine_004b0cd0", convention="thiscall", this=True, result=lambda a: rng.randrange(0, 8))
    ensure(fc, 0x004B0E60, 2, "engine_004b0e60", convention="thiscall", this=True)
    ensure(fc, 0x004B0FE0, 3, "engine_004b0fe0", convention="thiscall", this=True)
    ensure(fc, 0x00416050, 1, "engine_00416050", result=lambda a: slab + CONFIG)
    ensure(fc, 0x0049C8D0, 0, "engine_0049c8d0", convention="thiscall", this=True)
    ensure(fc, 0x00406070, 1, "engine_00406070")
    ensure(fc, 0x00406090, 1, "engine_00406090", stack_to=KSTACK, blob=(0, 6))
    ensure(fc, 0x00403D30, 1, "engine_00403d30", result=lambda a: 0 if rng.random() < 0.15 else env.alloc([max(a[0], 8)]))
    ensure(fc, 0x0040B060, 5, "engine_0040b060", convention="thiscall", this=True, stack_to=KSTACK, blob=(1, 12))
    ensure(fc, 0x004895C0, 1, "engine_004895c0")
    ensure(fc, 0x004F3900, 0, "engine_004f3900")
    ensure(fc, 0x004F3940, 0, "engine_004f3940")
    ensure(fc, 0x00466C10, 0, "engine_00466c10")
    ensure(fc, 0x0044E0F0, 0, "engine_0044e0f0")
    ensure(fc, 0x004F7CE0, 0, "engine_004f7ce0", writes=[(CLOCK, 4, lambda a: (mem.u32(CLOCK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])


def poll_result(a):
    STATE["polls"] += 1
    return 0 if STATE["polls"] > STATE["max_polls"] else 1


def key_result(a):
    STATE["keys"] += 1
    return 1 if STATE["keys"] >= STATE["abort_after"] else 0


def setting_answer(a):
    """What the options file answers into the out buffer: a short text (or the default back), padded with zeros."""
    import random

    text = random.Random(STATE["keys"] * 7 + STATE["polls"] + 1).choice([b"", b"Joe", b"Tony", b"Player One", b"192.168.1.5", b"X" * 40])
    return text + bytes(0x100 - len(text))


GROUP_STUBS = {group: install_net_stubs for group in GROUPS}


# ---- the mouse group --------------------------------------------------------------------------------------------------------------------------------------------------------

def mouse_world(img, env, rng):
    reset_state(rng, env)
    slab = env.slab
    img.u32(0x006A13F0, rng.choice([0, 10, 160, 319, rng.randrange(0, 400)]))
    img.u32(0x006A13F4, rng.choice([0, 10, 120, 239, rng.randrange(0, 300)]))
    img.u32(0x006A13F8, rng.choice([0, 0, 1, 2, 0x100]))
    for address in (0x006A13FC, 0x006A13FD, 0x006A13FE, 0x006A13FF, 0x006A1400, 0x006A1414, 0x006A1415, 0x006A1416, 0x006A1417):
        img.u8(address, rng.choice([0, 0, 1]))
    img.u32(0x006A1404, rng.choice([0, 1, 5]))
    img.u32(0x006A1408, rng.getrandbits(32))
    img.u32(0x006A1410, rng.getrandbits(32))
    img.u32(0x006A1424, rng.choice([0, slab + CURSOR_A]))
    img.u32(0x006A1428, rng.choice([0, slab + CURSOR_B]))
    img.u32(0x006A142C, rng.getrandbits(32))
    img.u32(0x006A1430, rng.getrandbits(32))
    img.u32(0x029DA394, rng.choice([640, 800, 1024, 320, 512, 1280]))
    img.u32(0x029DA398, rng.choice([480, 600, 768, 240, 400, 1024]))


@mouse(0x004DBD90, "Mouse_LoadCursors")
def gen_load_cursors(env, n):
    img, rng = Image(), env.rng
    mouse_world(img, env, rng)
    return [], img.pokes()


@mouse(0x004DBE60, "Mouse_FreeCursors")
def gen_free_cursors(env, n):
    img, rng = Image(), env.rng
    mouse_world(img, env, rng)
    return [], img.pokes()


@mouse(0x004DBEA0, "MENUPC_DrawMouseCursor")
def gen_draw_cursor(env, n):
    img, rng = Image(), env.rng
    mouse_world(img, env, rng)
    if rng.random() < 0.7:
        img.u8(0x006A1400, 1)
        img.u8(0x006A1414, 0)
        img.u32(0x006A1424, env.slab + CURSOR_A)
    return [], img.pokes()


@mouse(0x004DBEE0, "Mouse_Track")
def gen_track(env, n):
    img, rng = Image(), env.rng
    mouse_world(img, env, rng)
    x, y = rng.randrange(0, 1300), rng.randrange(0, 1100)
    b = rng.choice([0, 0, 1, 2, 3, 0x100, rng.getrandbits(8)])
    w = rng.choice([640, 800, 1024, 320, 512, 1280])
    h = rng.choice([480, 600, 768, 240, 400, 1024])
    img.u32(0x029DA394, w)
    img.u32(0x029DA398, h)
    if rng.random() < 0.3:
        # the pointer has not moved: the old position is where the arguments scale to
        img.u32(0x006A13F0, ((x << 9) & 0xFFFFFFFF) // w)
        img.u32(0x006A13F4, (y * 240 & 0xFFFFFFFF) // h)
        if rng.random() < 0.6:
            img.u32(0x006A13F8, b)
    return [x, y, b], img.pokes()


@mouse(0x004DC020, "Mouse_EnableCursors")
def gen_enable(env, n):
    img, rng = Image(), env.rng
    mouse_world(img, env, rng)
    return [rng.choice([0, 1, 1, 0xFF, 0x100, 0x101, rng.getrandbits(32)])], img.pokes()


def key_strings(rng, column):
    """A name from the key table (in a random case) or an unknown one."""
    from opentony.memory import mem

    rows = 0
    while mem.u32(0x0054A3A8 + 12 * rows + 4) != 0:
        rows += 1
    if rng.random() < 0.2:
        return rng.choice(["", "ZZZZ", "Nothing here", "up"])
    text = mem.cstring(mem.u32(0x0054A3A8 + 12 * rng.randrange(0, rows) + column), 64)
    return rng.choice([text, text.lower(), text.upper(), text.swapcase()])


@mouse(0x004DC050, "NetKeys_TranslateName")
def gen_translate(env, n):
    img, rng = Image(), env.rng
    reset_state(rng, env)
    return [put_string(img, env, 0, key_strings(rng, 4))], img.pokes()


@mouse(0x004DC090, "Text_DotsToSpaces")
def gen_dots(env, n):
    img, rng = Image(), env.rng
    reset_state(rng, env)
    text = "".join(rng.choice("ab.. .X.9") for _ in range(rng.randrange(0, 30)))
    return [put_string(img, env, 1, text)], img.pokes()


@mouse(0x004DC0B0, "NetGame_BlankText")
def gen_blank(env, n):
    return [], {}


@mouse(0x004DC0C0, "NetMenu_BannerStyle")
def gen_banner(env, n):
    img, rng = Image(), env.rng
    reset_state(rng, env)
    return [], img.pokes()


@mouse(0x004DC100, "TextEdit_Construct", this=TEXTEDIT)
def gen_edit_construct(env, n):
    img, rng = Image(), env.rng
    reset_state(rng, env)
    img.put(env.slab + TEXTEDIT, bytes(rng.getrandbits(8) for _ in range(0x140)))
    return [rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], img.pokes()


@mouse(0x004DC160, "TextEdit_DeletingDestructor", this=TEXTEDIT)
def gen_edit_delete(env, n):
    img, rng = Image(), env.rng
    reset_state(rng, env)
    img.put(env.slab + TEXTEDIT, bytes(rng.getrandbits(8) for _ in range(0x140)))
    return [rng.choice([0, 1, 2, 3, rng.getrandbits(32)])], img.pokes()


@mouse(0x004DC180, "TextEdit_Destruct", this=TEXTEDIT)
def gen_edit_destruct(env, n):
    img, rng = Image(), env.rng
    reset_state(rng, env)
    img.put(env.slab + TEXTEDIT, bytes(rng.getrandbits(8) for _ in range(0x140)))
    return [], img.pokes()


@mouse(0x004DC370, "TextEdit_SetBuffer", this=TEXTEDIT)
def gen_edit_buffer(env, n):
    img, rng = Image(), env.rng
    reset_state(rng, env)
    img.put(env.slab + TEXTEDIT, bytes(rng.getrandbits(8) for _ in range(0x140)))
    return [rng.getrandbits(32), rng.choice([0x10, 0x20, 0x1F, 5]), rng.getrandbits(32)], img.pokes()


@mouse(0x004DC190, "TextEdit_Update", this=TEXTEDIT)
def gen_edit_update(env, n):
    """The control with a buffer of random text, a capacity and the two filters; the key the stubs report (a plain one, backspace, the arrows, the one held last frame) and the character it types."""
    img, rng = Image(), env.rng
    reset_state(rng, env)
    slab = env.slab
    obj = bytearray(rng.getrandbits(8) for _ in range(0x140))
    img.put(slab + TEXTEDIT, bytes(obj))
    img.u32(slab + TEXTEDIT, slab + VT_EDIT)
    buffer = string_at(env, rng.randrange(0, 8))
    alphabet = "abcXYZ019 ..-_"
    length = rng.choice([0, 1, 3, 10, 20, 29, 30, 31, 38])
    img.put(buffer, "".join(rng.choice(alphabet) for _ in range(length)).encode("latin-1") + b"\0" + bytes(rng.randrange(1, 256) for _ in range(0x3F - length)))   # (garbage after the terminator, so a missing terminator shows)
    img.u32(slab + TEXTEDIT + 0x100, rng.choice([buffer] * 19 + [0]))
    img.u32(slab + TEXTEDIT + 0x12C, u(rng.choice([-1, 0, 1, 5, 10, 29, 30, 30, 31, 45, 200, length, length + 1, max(length - 1, 0)])))
    img.u8(slab + TEXTEDIT + 0xE, rng.choice([0, 1, 1, 1, 1, 1, 1]))
    img.u8(slab + TEXTEDIT + 0x130, rng.choice([0, 1]))
    img.u8(slab + TEXTEDIT + 0x131, rng.choice([0, 0, 1]))
    key = rng.choice([0, 0x1E, 0x30, 0x02, 0x39, 0x34, 0xCB, 0xC8, 0xD0, 0x1C, rng.randrange(1, 0xEE), rng.randrange(1, 0xEE)])
    STATE["edit_key"] = key
    STATE["edit_char"] = rng.choice([0, 8, 0x41, 0x7A, 0x30, 0x39, 0x2E, 0x20, 0x20, 0x20, 0x0D, 0x09, 0x2D, 0x5F, 0x21, 0x80, 0xFF, 0x7F, rng.randrange(0, 256)])
    if STATE["edit_char"] >= 0x80:
        STATE["edit_char"] = u(STATE["edit_char"] - 0x100)   # (movsx: a byte of 0x80 or more arrives sign-extended)
    img.u32(0x006A13E8, rng.choice([key, 0, rng.randrange(0, 0xEE), rng.randrange(0, 0xEE)]))
    img.u32(CLOCK, rng.choice([0, 0xF, 0x10, 0x1F, 0x20, 0x2F, rng.getrandbits(32)]))
    return [], img.pokes()


@mouse(0x004DD390, "Mouse_PointerFrame")
def gen_pointer_frame(env, n):
    """A drag in progress or not, the buttons, the pad mode, the pointer on, near and away from the middle of the box, the mouse on and off the two bars and the big box."""
    img, rng = Image(), env.rng
    mouse_world(img, env, rng)
    img.u8(0x006A1414, rng.choice([0, 1, 1]))
    img.u32(0x006A13F8, rng.choice([0, 1, 2, 0x100]))
    img.u8(0x006A13FC, rng.choice([0, 0, 1]))
    middle_x = rng.choice([0x140, 0x140, 100, 160, rng.randrange(0, 400)])
    middle_y = rng.choice([0xF0, 0xF0, 100, 120, rng.randrange(0, 300)])
    img.u32(0x006A111C, middle_x)
    img.u32(0x006A13E0, middle_y)
    img.u32(0x006A44F0, u(rng.choice([middle_x, middle_x - 1, middle_x + 1, middle_x - 2, middle_x + 2, rng.randrange(0, 700), -5])))
    img.u32(0x006A44F4, u(rng.choice([middle_y, middle_y - 1, middle_y + 1, middle_y - 2, middle_y + 2, rng.randrange(0, 500), -5])))
    img.u32(0x006A13F4, u(rng.choice([0, 0x1E, 0x1F, 0x20, 100, -1])))
    img.u32(0x006A13E4, rng.choice([0, 1, rng.getrandbits(32)]))      # (the word a drag clears: nonzero, or the clear cannot show)
    img.u32(0x006A13EC, rng.choice([0, rng.getrandbits(32)]))
    img.u32(0x006A1118, rng.choice([0, rng.getrandbits(32)]))
    return [], img.pokes()


FILE_NAMES = ["PARK1.PRK", "my.park.PRK", "A.B.C.PRK", "NETPARK.PRK", "X.PRK", "a" * 24 + ".PRK", "b" * 31 + ".PRK", "c" * 40 + ".PRK", "noext", ".hidden", "d" * 30 + ".e"]


@mouse(0x004DC3A0, "NetMenu_ParkListLoad", this=FILELIST)
def gen_park_list(env, n):
    img, rng = Image(), env.rng
    reset_state(rng, env)
    count = rng.choice([1, 2, 3, 4, 8, 16])
    buffer = env.slab + ARENA        # the allocator's first block is the buffer FindFiles fills
    img.put(env.slab + FILELIST, bytes(rng.getrandbits(8) for _ in range(0xC0)))
    for k in range(count):
        if rng.random() < 0.85:
            img.u32(env.slab + FILELIST + 0x7C + 4 * k, buffer + 260 * k)
            img.put(buffer + 260 * k, rng.choice(FILE_NAMES).encode("latin-1") + b"\0")
        else:
            img.u32(env.slab + FILELIST + 0x7C + 4 * k, 0)
    img.put(env.slab + IDS, bytes(rng.getrandbits(8) for _ in range(0x40)))
    STATE["found"] = rng.randrange(0, count + 2)
    return [env.slab + IDS, count], img.pokes()


@mouse(0x004DC620, "NetMenu_Nothing")
def gen_menu_nothing(env, n):
    return [], {}


def widget_world(img, env, rng):
    reset_state(rng, env)
    slab = env.slab
    img.put(slab + WIDGET, bytes(rng.getrandbits(8) for _ in range(0x140)))
    for child in (CHILD_A, CHILD_B):
        img.put(slab + child, bytes(rng.getrandbits(8) for _ in range(0x60)))
        img.u32(slab + child + 0x44, rng.choice([0, 10, 100, 300, rng.randrange(0, 400)]))
    img.u32(slab + WIDGET + 0x6C, rng.choice([slab + CHILD_A, slab + CHILD_A, 0]))
    img.u32(slab + WIDGET + 0x70, slab + CHILD_B)
    img.u32(slab + WIDGET + 0x28, slab + SUBOBJ)
    img.u32(slab + WIDGET + 0x2C, slab + SPRITE_A)
    img.u32(slab + WIDGET + 0x30, rng.choice([0, slab + SPRITE_B, slab + SPRITE_B]))
    img.put(slab + SUBOBJ, bytes(rng.getrandbits(8) for _ in range(0x10)))
    img.put(slab + SPRITE_A, bytes(rng.getrandbits(8) for _ in range(0x30)))
    img.put(slab + SPRITE_B, bytes(rng.getrandbits(8) for _ in range(0x30)))
    mouse_world(img, env, rng)


@mouse(0x004DC7B0, "Slider_PickByMouse", this=WIDGET)
def gen_slider(env, n):
    img, rng = Image(), env.rng
    widget_world(img, env, rng)
    slab = env.slab
    img.u32(slab + WIDGET + 0x30, rng.choice([0, 12, 100, 240]))
    img.u32(slab + WIDGET + 0x34, rng.choice([0, 50, 160, 300]))
    img.u32(slab + WIDGET + 0x38, rng.choice([0, 50, 120, 200]))
    img.u32(slab + WIDGET + 0x30, rng.choice([0, 12, 100, 240]))
    img.u16(slab + WIDGET + 0x1C, rng.choice([0, 0, 0x40, 0x1000, 0xFFFF]))
    img.u32(slab + WIDGET + 0x20, rng.getrandbits(32))
    img.u32(slab + WIDGET + 0x70, slab + CHILD_B)
    return [], img.pokes()


@mouse(0x004DC9E0, "NetMenu_MinusOne")
def gen_minus_one(env, n):
    return [], {}


@mouse(0x004DCB00, "MenuWidget_SetColour", this=WIDGET)
def gen_set_colour(env, n):
    img, rng = Image(), env.rng
    widget_world(img, env, rng)
    return [rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], img.pokes()


@mouse(0x004DCBC0, "NetKeys_CodeByName", this=WIDGET)
def gen_code_by_name(env, n):
    img, rng = Image(), env.rng
    widget_world(img, env, rng)
    return [put_string(img, env, 2, key_strings(rng, 8))], img.pokes()


@mouse(0x004DD520, "MenuRows_PickByMouse", this=WIDGET)
def gen_pick_row(env, n):
    img, rng = Image(), env.rng
    widget_world(img, env, rng)
    slab = env.slab
    count = rng.choice([0, 1, 2, 3, 5, 8])
    img.u8(slab + WIDGET + 9, count)
    img.u32(slab + WIDGET + 0x10, rng.choice([100, 120, 200, 40]))
    img.u32(slab + WIDGET + 0x14, rng.choice([0, 20, 30, 41, u(-10)]))
    for k in range(8):
        row = slab + WIDGET + 0x24 + 0x18 * k
        img.u8(row, rng.randrange(0, 12))
        img.u8(row + 1, rng.choice([0, 1, 1]))
        img.u8(row + 2, rng.choice([0, 1, 1]))
    return [], img.pokes()


# ---- 12 s7: the pick-by-mouse methods the career fuzz reached (and their neighbours) --------------------------------------------------------------------------------------------------

def pick_world(img, env, rng):
    """The widget world plus the mouse flags tilted towards the branches (a changed pointer, a release, the pointer driving the menus) and the pending-key word."""
    widget_world(img, env, rng)
    img.u8(0x006A13FC, rng.choice([0, 1, 1]))
    img.u8(0x006A13FD, rng.choice([0, 1, 1, 1]))
    img.u8(0x006A13FE, rng.choice([0, 1]))
    img.u32(0x006A13F0, rng.choice([0, 10, 100, 150, 160, 250, 319, rng.randrange(0, 400)]))
    img.u16(0x006A696C, rng.choice([0, 0, 0, 0x40, 0x1000]))


def rect_fields(img, env, rng, widget, *, x=0x1C, y=0x20, w=None, h=0x18):
    slab = env.slab
    img.u32(slab + widget + x, rng.choice([0, 10, 50, 100, 150, u(-5)]))
    img.u32(slab + widget + y, rng.choice([0, 10, 60, 120, u(-3)]))
    if w is not None:
        img.u32(slab + widget + w, rng.choice([0, 20, 100, 200, 0x200]))
    img.u32(slab + widget + h, rng.choice([0, 12, 20, 60, 120]))


@mouse(0x004DC530, "MenuGrid_PickByMouse", this=WIDGET)
def gen_grid_pick(env, n):
    img, rng = Image(), env.rng
    pick_world(img, env, rng)
    slab = env.slab
    count = rng.choice([0, 1, 2, 3, 4, 6, 9, 12])
    columns = rng.choice([1, 2, 3, 4, 5, 8])
    img.u32(slab + WIDGET + 4, columns)
    img.u32(slab + WIDGET + 0x0C, slab + GRID_TABLE)
    img.u32(slab + WIDGET + 0x10, rng.getrandbits(32))
    img.u32(slab + WIDGET + 0x14, rng.getrandbits(32))
    img.u32(slab + WIDGET + 0x18, count)
    for k in range(12):
        item = slab + GRID_ITEMS + 0x10 * k
        img.u32(slab + GRID_TABLE + 4 * k, 0 if rng.random() < 0.1 else item)
        img.u32(item, slab + PICK_VT)
        img.u16(item + 4, rng.choice([0, 0x40, 0x8000, rng.getrandbits(16)]))
        img.u8(item + 0xC, rng.choice([0, 1, 1]))
    return [], img.pokes()


def button_fields(img, env, rng):
    slab = env.slab
    rect_fields(img, env, rng, WIDGET, w=0x14)
    img.u16(slab + WIDGET + 4, rng.choice([0, 0x40, 0x8000, 0x2000, rng.getrandbits(16)]))
    img.u32(slab + WIDGET + 8, rng.getrandbits(32))
    img.u8(slab + WIDGET + 0xC, rng.choice([0, 1, 1]))


@mouse(0x004DC5C0, "Button_PickByMouse", this=WIDGET)
def gen_button_pick(env, n):
    img, rng = Image(), env.rng
    pick_world(img, env, rng)
    button_fields(img, env, rng)
    return [], img.pokes()


@mouse(0x004DC630, "Button_PickByMouseJump", this=WIDGET)
def gen_button_pick_jump(env, n):
    return gen_button_pick(env, n)


@mouse(0x004DC8A0, "RightButton_PickByMouse", this=WIDGET)
def gen_right_button_pick(env, n):
    return gen_button_pick(env, n)


def img_u32(img, address):
    """The word a generator poked at `address` (the Image keeps bytes)."""
    return struct.unpack("<I", bytes(img.b[address + k] for k in range(4)))[0]


def img_s32(img, address):
    value = img_u32(img, address)
    return value - (1 << 32) if value >= 1 << 31 else value


def arrow_children(img, env, rng, left_at, right_at):
    """The two arrow objects of a bar: +0x44 is the x they are centred on."""
    slab = env.slab
    img.u32(slab + WIDGET + left_at, rng.choice([slab + CHILD_A, slab + CHILD_A, 0]))
    img.u32(slab + WIDGET + right_at, slab + CHILD_B)
    for child in (CHILD_A, CHILD_B):
        img.u32(slab + child + 0x44, rng.choice([0, 10, 100, 150, 300, rng.randrange(0, 400)]))


@mouse(0x004DC640, "ColumnList_PickByMouse", this=WIDGET)
def gen_column_list_pick(env, n):
    img, rng = Image(), env.rng
    pick_world(img, env, rng)
    slab = env.slab
    rect_fields(img, env, rng, WIDGET)
    img.u16(slab + WIDGET + 4, rng.choice([0, 0x40, 0x8000, rng.getrandbits(16)]))
    img.u32(slab + WIDGET + 8, rng.getrandbits(32))
    img.u8(slab + WIDGET + 0xE, rng.choice([0, 1, 1, 1]))
    count = rng.choice([0, 1, 2, 3, 4, 5])
    img.u32(slab + WIDGET + 0xE4, count)
    for k in range(4):
        img.u32(slab + WIDGET + 0xD4 + 4 * k, rng.choice([0, 20, 50, 100, 200, u(-20), 500]))
    arrow_children(img, env, rng, 0xF8, 0xFC)
    img.u32(slab + WIDGET + 0xFC, rng.choice([slab + CHILD_B, slab + CHILD_B, 0]))
    if rng.random() < 0.6:   # the pointer on or beside the end of the widest column + 0x14 (the widths are the first four words; a fifth is the count itself)
        x = img_u32(img, slab + WIDGET + 0x1C)
        widths = [img_s32(img, slab + WIDGET + 0xD4 + 4 * k) for k in range(4)] + [count]
        widest = max([0] + widths[:count])
        img.u32(0x006A13F0, u(x + widest + 0x14 + rng.choice([-1, 0, 1, 2])))
    return [], img.pokes()


@mouse(0x004DC8D0, "ArrowBar_PickByMouse", this=WIDGET)
def gen_arrow_pair_pick(env, n):
    img, rng = Image(), env.rng
    pick_world(img, env, rng)
    slab = env.slab
    rect_fields(img, env, rng, WIDGET)
    img.u32(slab + WIDGET + 8, rng.getrandbits(32))
    img.u8(slab + WIDGET + 0xE, rng.choice([0, 1, 1, 1]))
    arrow_children(img, env, rng, 0x70, 0x74)
    if rng.random() < 0.6:   # the pointer on or beside the right arrow's x + 0x14
        edge = img_s32(img, slab + CHILD_B + 0x44) + 0x14
        img.u32(0x006A13F0, u(edge + rng.choice([-1, 0, 1, 2])))
    return [], img.pokes()


@mouse(0x004DD180, "ArrowChildren_PickByMouse", this=WIDGET)
def gen_arrow_children_pick(env, n):
    img, rng = Image(), env.rng
    pick_world(img, env, rng)
    slab = env.slab
    img.u32(slab + WIDGET + 0xB0, slab + OBJ_B0)
    img.put(slab + OBJ_B0, bytes(rng.getrandbits(8) for _ in range(0x50)))
    for k, at in enumerate((0x3C, 0x40, 0x44)):
        child = slab + PICK_CHILD + 0x10 * k
        img.u32(slab + OBJ_B0 + at, child)
        img.u32(child, slab + PICK_VT)
    return [], img.pokes()


@mouse(0x004DC9D0, "NetMenu_FalseA")
def gen_false_a(env, n):
    return [], {}


@mouse(0x004DCEA0, "NetMenu_NothingArg")
def gen_nothing_arg(env, n):
    return [env.rng.getrandbits(32)], {}


@mouse(0x004DCEB0, "NetMenu_FalseB")
def gen_false_b(env, n):
    return [], {}


@mouse(0x004DD170, "NetMenu_FalseC")
def gen_false_c(env, n):
    return [], {}


@mouse(0x004DD6D0, "MenuWidget_SetTint", this=WIDGET)
def gen_set_tint(env, n):
    img, rng = Image(), env.rng
    widget_world(img, env, rng)
    value = lambda: struct.unpack("<I", struct.pack("<f", rng.choice([0.0, 1.0, 0.5, -1.0, 8.0, 15.9999, 0.25, 3.3e5, 3.0e9, 1.0e19, rng.uniform(-3.0, 20.0)])))[0]
    return [value(), value(), value()], img.pokes()
