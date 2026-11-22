"""Phase 4.10f2 case generators, part 1: what the equipment-select screen's groups share (equipsel.cpp and the classes it builds).
Part 2 (frontend_specs_eqsel_cases.py) holds the generators of each group.

The slab is the 4.10f one (frontend_specs_playsel.py: the 4.10e1 slab with six object slots of 0x1400 bytes at 0x1c000, sixteen real element records, the
allocation arena at 0x29000 whose blocks start with VT_ANY, the zeroed object slots) with an extension behind the arena:

  0x1a140  VT_RACK, the fake vtable of a rack (delete, update, draw, move)      0x1a170  VT_DIALOG, the fake vtable of the confirm box (delete, update, draw)
  0x38000  the deck entries Player_GetTextureSetEntry hands back (20 sets x 20 decks x 0x20 bytes)     0x3b400  their file stems (16 bytes each)
  0x3cd00  the four stat dots (0x20 bytes each)     0x3cd80  the view rectangle     0x3cda0  the spool (0x18)     0x3cdc0  spool nodes (64 x 8)
  0x3d000  deck items (96 x 0x28)     0x3dc00  deck logos (20 x 0x40)     0x3e100  the dialog     0x3e180  the placeholder pictures

Groups: eqh (the deck helpers and the small functions), eqwin (the window), eqpan (the stat dots, the cash window and the name window), eqview (the view),
eqrack (the racks, the deck pictures and the spool queue), eqloop (the run loop).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_menu import CHILD_DRAW, CHILD_UPDATE, VIRTUAL_DELETE, string_at
from frontend_specs_playsel import (PA, PS_ARENA, PS_OBJ, PSLAB, ROWS, ROWS_SIZE, disable, install_play_stubs, pregions, seed_play)
from frontend_specs_screens import READ_ONLY_COMMON, VT_ANY, install_screen_stubs
from frontend_specs_skwin import CAREER

EQ_SLAB = 0x3E400

VT_RACK = 0x1A140
VT_DIALOG = 0x1A170
ENT = 0x38000
STEMS = 0x3B400
DOTS = 0x3CD00
RECT = 0x3CD80
SPOOL = 0x3CDA0
NODES = 0x3CDC0
ITEMS = 0x3D000
LOGOS = 0x3DC00
DIALOG = 0x3E100
PICTURES = 0x3E180
ITEM_STRIDE = 0x28
ITEM_COUNT = 96
NODE_COUNT = 64
SET_COUNT = 20
DECK_COUNT = 20

# unreached engine entry points: one stub each
RACK_V3 = 0x004ADC80       # the rack's move (a rack's vtable slot 3)
DIALOG_V2 = 0x004ADC40     # the confirm box's slot 2

# the retail globals this family reads or writes
EQ_CAREER = 0x00561C64
EQ_CHAR = 0x00561C68
EQ_PLAYER = 0x00561C7C
EQ_SET = 0x00561CC4
EQ_IDX = 0x00561CC8
EQ_TRUCK = 0x00561CCC
TRUCK_TEXTS = 0x00561CD0
PLAYER_SET = 0x0056A688
PLAYER_IDX = 0x0056A2DC
PLAYER_TRUCK = 0x0056A2E0
SAVED_BASE = 0x0055FAF0
LOADING = 0x005615D0
REGION_NAMES = 0x0056D428
VIEW_RECT = 0x0052CB54
SLOT_TABLE = 0x0052CB58
HIGH_RES = 0x029DA3A0
PADS = 0x0056AFF8
PAD_STRIDE = 0x16C
PAD_LEFT, PAD_RIGHT, PAD_UP, PAD_DOWN = 0x0056B079, 0x0056B089, 0x0056B099, 0x0056B0A9
PAD_TOGGLE, PAD_CONFIRM, PAD_ALT, PAD_BACK = 0x0056B019, 0x0056B029, 0x0056B0D9, 0x0056AFF9
PAD_SCROLL_UP, PAD_SCROLL_DOWN = 0x0056B204, 0x0056B214

# the two-letter tag each character's deck stems carry (the real table: sets with the same tag share one rack)
TAGS = [b"Bi", b"Fi", b"Po", b"Ci", b"Fl", b"Gi", b"Bi", b"At", b"Sh", b"Bi", b"Fl", b"To", b"Ze", b"Cr", b"Cr", b"Cr", b"Cr", b"Di", b"pv", b"SP"]
CUSTOM_ROWS = (13, 14, 15, 16)

EQ_STATIC = [(0x0052B6A0, 0x360), (SLOT_TABLE, 0x30), (0x005317C0, 0x60), (0x005252F0, 0x10)]


def eq_regions(slab):
    return pregions(slab) + [(slab + PSLAB, EQ_SLAB - PSLAB), (ROWS, ROWS_SIZE), (0x00561C60, 0x54), (0x00561CB8, 0x28), (SAVED_BASE, 0x60), (PLAYER_IDX, 0x10),
                             (LOADING, 4), (REGION_NAMES, 0x240), (VIEW_RECT, 4), (HIGH_RES, 4)]


def seed_eq(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_play(env, rng)
    write_chunks(env.slab + VT_RACK, struct.pack("<IIII", VIRTUAL_DELETE, CHILD_UPDATE, CHILD_DRAW, RACK_V3))
    write_chunks(env.slab + VT_DIALOG, struct.pack("<III", VIRTUAL_DELETE, 0x004AD180, DIALOG_V2))
    # the deck entries and the file stems of every (set, deck); the character rows point at the first stem of their set
    for s in range(SET_COUNT):
        entries = bytearray()
        stems = bytearray()
        for d in range(DECK_COUNT):
            stem = b"s2d" + TAGS[s] + (b"%02d" % (d + 1)) + bytes([ord("a") + d % 26]) + b".bmp"
            stems += stem.ljust(16, b"\0")
            entries += struct.pack("<IIIiiiii", env.slab + STEMS + (s * DECK_COUNT + d) * 16, string_at(env, rng.randrange(0, 16)), 0, rng.randrange(0, 11),
                                   rng.randrange(0, 11), rng.randrange(0, 11), 0, rng.randrange(0, 11))
        write_chunks(env.slab + STEMS + s * DECK_COUNT * 16, bytes(stems))
        write_chunks(env.slab + ENT + s * DECK_COUNT * 0x20, bytes(entries))
        write_chunks(ROWS + 0x170 * s + 0x44, struct.pack("<I", env.slab + STEMS + s * DECK_COUNT * 16))
    # the rectangle the view draws into
    write_chunks(VIEW_RECT, struct.pack("<I", env.slab + RECT))
    return writes


def register(group):
    SLAB_BYTES[group] = EQ_SLAB
    EXTRA_REGIONS[group] = eq_regions
    READ_ONLY[group] = [r for r in READ_ONLY_COMMON if r[0] != ROWS] + EQ_STATIC
    SEEDS[group] = seed_eq


for _group in ("eqh", "eqwin", "eqpan", "eqview", "eqrack", "eqloop"):
    register(_group)


# ---- stubs -----------------------------------------------------------------------------------------------------------------------------------------------

SCRATCH_LINE = 0x7FF00400      # where the native code builds the strings retail keeps in locals (ScreenKit's kScratchLine)


def install_eq_base(fc, env):
    if getattr(fc, "_eq_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._eq_installed = True
    rng = env.rng
    install_screen_stubs(fc, env)
    # the sprite constructor is handed the file name of a logo built in a local (a stack address): record it as the native code's scratch line
    disable(fc, "sprite_construct")
    fc.stub(0x00457420, 4, "sprite_construct", convention="thiscall", this=True, text=True, stack_to=SCRATCH_LINE, result=lambda a: env.alloc_log[-1][0])
    # the per-frame hook of the real window-base classes (slot 7 of a real vtable) is an engine function: a reported service like the fake vtables' slot 7
    for hook in (0x004DC9F0, 0x004DCA60, 0x004DCB30):
        fc.stub(hook, 0, "element_hook", convention="thiscall", this=True)
    # the engine objects every group reaches
    fc.stub(0x004547F0, 2, "engine_004547f0", result=lambda a: env.slab + ENT + ((a[0] % SET_COUNT) * DECK_COUNT + (a[1] % DECK_COUNT)) * 0x20)
    fc.stub(0x004E8730, 4, "engine_004e8730", stack_to=SCRATCH_LINE)
    fc.stub(0x00466BE0, 0, "engine_00466be0")
    fc.stub(0x004B37A0, 4, "engine_004b37a0", text=True, result=lambda a: rng.randrange(0, 8))
    fc.stub(0x004B3DF0, 0, "engine_004b3df0")
    fc.stub(0x004ACF20, 5, "engine_004acf20")
    fc.stub(0x00457E10, 3, "engine_00457e10")
    fc.stub(0x00449030, 1, "engine_00449030", text=True, result=lambda a: rng.choice([0, 0x400, 0x800, 0x1000, 0x1000, 0x2000]))
    fc.stub(0x00449230, 1, "engine_00449230")
    fc.stub(0x00449660, 0, "engine_00449660")
    fc.stub(0x004494B0, 0, "engine_004494b0")
    fc.stub(0x00500400, 5, "engine_00500400", convention="stdcall")
    fc.stub(0x0050030C, 4, "engine_0050030c", convention="stdcall")
    # the fake vtable slots of the racks and the dialog, the dialog's answer
    fc.stub(RACK_V3, 1, "rack_v3", convention="thiscall", this=True)
    fc.stub(DIALOG_V2, 0, "dialog_v2", convention="thiscall", this=True)
    fc.stub(0x004AD180, 0, "dialog_v1", convention="thiscall", this=True)
    fc.stub(0x00426310, 2, "engine_00426310", convention="thiscall", this=True, result=lambda a: env.dialog_result)
    fc.stub(0x00425FA0, 4, "engine_00425fa0", convention="thiscall", this=True, text=1, stack_to=SCRATCH_LINE, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x004A1440, 4, "engine_004a1440", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x004A1540, 1, "engine_004a1540", convention="thiscall", this=True)
    fc.stub(0x004A1ED0, 7, "engine_004a1ed0", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])


def seam(fc, env, address, count):
    """A constructor another class calls (a seam): it hands back the block the caller just allocated."""
    fc.stub(address, count, "engine_%08x" % address, convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])


def install_eqh(fc, env):
    install_eq_base(fc, env)


def install_eqwin(fc, env):
    install_eq_base(fc, env)
    seam(fc, env, 0x004455C0, 4)      # the dots, the name window, the cash window and the view are built by their own constructors
    seam(fc, env, 0x00446490, 4)
    seam(fc, env, 0x00445920, 4)
    seam(fc, env, 0x00445EA0, 4)


def install_eqpan(fc, env):
    install_eq_base(fc, env)


def install_eqview(fc, env):
    install_eq_base(fc, env)
    seam(fc, env, 0x004469F0, 0)      # the racks
    seam(fc, env, 0x00448230, 0)


def install_eqrack(fc, env):
    install_eq_base(fc, env)


def install_eqloop(fc, env):
    from opentony.memory import mem
    from frontend_specs_screens import TICK

    rng = env.rng
    install_eq_base(fc, env)
    seam(fc, env, 0x00444980, 0)      # the window's constructor
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])


GROUP_STUBS = {"eqh": install_eqh, "eqwin": install_eqwin, "eqpan": install_eqpan, "eqview": install_eqview, "eqrack": install_eqrack, "eqloop": install_eqloop}
