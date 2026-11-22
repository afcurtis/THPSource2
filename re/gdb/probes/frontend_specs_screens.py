"""Phase 4.10e1 case generators, part 1: what the create-a-skater screens share (create_ui.cpp, creedit.cpp, cresave.cpp, statedit.cpp and the stat
window's constructor).  Part 2 (frontend_specs_screens_cases.py) holds the generators of each group.

The screens build windows from the toolkit and the window base (native since 4.10a / 4.10c), the skater manager (4.8) and the font code (4.10a); every
other engine call is stubbed (a service).  The probe lays one scratch slab of 0x24000 bytes out:

  0x00000..0x18000  the 4.10d layout (frontend_specs_skwin.py: the menu group's fake vtables, fonts and strings below 0x8000, then pages, lists,
                    item records, window children, fake dialogs and a texture-set manager)
  0x18000  four main objects of 0x800 bytes (the object under test and its helpers)     0x1a000  the renderer object
  0x1a100  the fake vtable VT_ANY (every virtual method a reported service)    0x1a200  a pool of 48 fake objects of 0x40 bytes    0x1ae00  texts
  0x1ba00  an editor skater object (0x340)   0x1be00  an animated skater object (0x180)
  0x1c000  the allocation arena (0x8000): every block starts with VT_ANY, so an object a stubbed constructor "builds" has a vtable whose methods are
           reported services

Groups: crui (create_ui.cpp), crsave (cresave.cpp), crstat (statedit.cpp and the stat window constructor), credit (creedit.cpp) and crloop (the three
screen loops and their callbacks, whose per-frame calls are seams: see CreateScreens::set_loop_seams).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_menu import READ_ONLY as MENU_READ_ONLY
from frontend_specs_menu import (ARENA, ELEM_HOOK, CHILD_DRAW, CHILD_UPDATE, FIND_FONTS, FONT_SIZE, GLYPHS, ITEM_DRAW, ITEM_HIGHLIGHT, ITEM_NAVIGATE,
                                 ITEM_SET_POSITION, ITEM_SNAP, SPRITE_DRAW, STRING_POOL, VIRTUAL_DELETE, VT_FONT, install_menu_stubs)
from frontend_specs_skwin import (CAREER, CLOCK, CUSTOM, CURRENT_PAD, MANAGER, PADS, SCREEN, LIST_V3, READ_ONLY_RANGES as SKWIN_READ_ONLY, regions as skwin_regions,
                                  seed_skwin)

SLAB = 0x24000
OBJ = [0x18000, 0x18800, 0x19000, 0x19800]
RENDERER = 0x1A000
VT_ANY = 0x1A100
POOL = 0x1A200
POOL_N = 48
POOL_STRIDE = 0x40
TEXTS = 0x1AE00
TEXT_STRIDE = 0x100
SKATER = 0x1BA00
ANIM = 0x1BE00
ARENA2 = 0x1C000
ARENA2_SIZE = SLAB - ARENA2

SNAP_X = 0x004ADC50      # unreached engine entry points: the fake vtable's snap slots
SNAP_Y = 0x004ADC60

TEXT_POOL = [b"Short message", b"This is a longer message that has to wrap onto several lines of the help box when it is shown on the screen",
             b"First line&Second line", b"One&Two&Three&Four", b"x", b"Warning: Unsaved skaters will be lost if you quit the game without saving them first",
             b"Averyveryveryverylongwordthatdoesnotfitanywhereinthebox and a short tail", b"A B C D E F G H I J K L M N O P Q R S T U V W X Y Z",
             b"Two words", b"&Leading break", b"Trailing break&"]

# the globals the screens read and write besides the 4.10d set
FRONT_FLAG = 0x0056114C
LAST_TICK = 0x00568624
TICK = 0x0056E31C
RENDERER_POINTER = 0x00560FD4
PRE_MANAGER = 0x0056B834
CUSTOM_SKATER_GLOBAL = 0x00568648
DATABASE = 0x0056A87C
CACHED_DATABASE = 0x00561CB4
ANIM_SCALE = 0x0056865C
CAMERA = 0x0055F7C8
EDITOR_MODE = 0x0056A878
SPIN_LEFT = 0x0056B008
SPIN_RIGHT = 0x0056B018

# retail vtables of the classes the screens construct
VT_REAL = {"textbox": 0x0051877C, "confirm": 0x00518784, "editor": 0x005187A8, "save": 0x00518808, "stat": 0x0051977C, "statpage": 0x0051973C,
           "personal": 0x005187C8}

# the global pointer variables whose strings the screens read (static): the text table and the texts
STATIC_RANGES = [
    (0x00525400, 0x80), (0x005258D8, 0x40), (0x00525FFC, 0x10), (0x00526ACC, 0x10), (0x00527830, 0x1E0), (0x00527C10, 0x30), (0x0052C9F8, 0x10),
    (0x0052D84C, 0x10), (0x0053E9C0, 0x1200), (0x0052E340, 0x40), (0x00531A00, 0x1A00), (0x0053DC1C, 0x34), (0x0052544C, 0x10), (0x00525458, 0x10), (0x005254E0, 0x10),
]


def A(env, offset):
    return env.slab + offset


def regions(slab):
    return skwin_regions(slab) + [(slab + 0x18000, SLAB - 0x18000), (CAMERA, 0x40), (ANIM_SCALE, 4), (CUSTOM_SKATER_GLOBAL, 4), (FRONT_FLAG, 4),
                                  (LAST_TICK, 4), (TICK, 4), (PRE_MANAGER, 4), (RENDERER_POINTER, 4)]


def seed_screens(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_skwin(env, rng)
    env.alloc_base = ARENA2
    env.alloc_size = ARENA2_SIZE
    # the fake vtable every pool object and arena block shares: delete, update, draw, sprite draw, snaps, the per-frame hook
    vt = bytearray(0x40)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, CHILD_UPDATE), (2, CHILD_DRAW), (3, SPRITE_DRAW), (5, SNAP_X), (6, SNAP_Y), (7, ELEM_HOOK)):
        struct.pack_into("<I", vt, 4 * slot, entry)
    write_chunks(env.slab + VT_ANY, bytes(vt))
    block = struct.pack("<I", env.slab + VT_ANY)
    for offset in range(0, ARENA2_SIZE, 16):
        write_chunks(env.slab + ARENA2 + offset, block)
    for i in range(POOL_N):
        write_chunks(env.slab + POOL + POOL_STRIDE * i, block)
    for i, text in enumerate(TEXT_POOL):
        write_chunks(env.slab + TEXTS + TEXT_STRIDE * i, text + b"\0")
    write_chunks(env.slab + SKATER, block)
    write_chunks(env.slab + ANIM, block)
    return writes


READ_ONLY_COMMON = list(SKWIN_READ_ONLY) + list(MENU_READ_ONLY["menu"]) + STATIC_RANGES


def register(group):
    SLAB_BYTES[group] = SLAB
    EXTRA_REGIONS[group] = regions
    READ_ONLY[group] = READ_ONLY_COMMON
    SEEDS[group] = seed_screens


GROUPS = ("crui", "crsave", "crstat", "credit", "crloop")
for _group in GROUPS:
    register(_group)

# ---- stubs -------------------------------------------------------------------------------------------------------------------------------------
# (address, stack arguments, thiscall, options) of the engine functions the screens reach, named engine_XXXXXXXX
ENGINE = [
    (0x00423BD0, 0, False, {}), (0x004B2390, 0, False, {}), (0x0048A420, 0, False, {}), (0x00489490, 0, False, {}), (0x0042FC70, 0, False, {}),
    (0x004F5FF0, 0, False, {}), (0x004DAE00, 0, False, {}), (0x00418150, 0, False, {}), (0x004304B0, 0, False, {}), (0x00489A10, 0, False, {}),
    (0x00430510, 0, False, {}), (0x00466C00, 0, False, {}), (0x004C8F30, 0, False, {}), (0x0046F480, 0, False, {}), (0x004645F0, 0, False, {}),
    (0x0045F3F0, 0, False, {}), (0x00489670, 1, False, {}), (0x004E8740, 1, False, {}), (0x004C9300, 1, False, {}), (0x004F5430, 1, False, {}),
    (0x004F5390, 2, False, {}), (0x004E8460, 2, False, {}), (0x00464710, 3, False, {}), (0x0045E8E0, 3, False, {}), (0x00461BF0, 1, False, {}),
    (0x0045F530, 1, False, {}), (0x004B32F0, 3, False, {}), (0x00452390, 3, False, {}), (0x00423BE0, 2, False, {}), (0x00423B50, 2, False, {}),
    (0x00422EC0, 1, False, {}), (0x004171E0, 1, False, {}), (0x004170D0, 2, False, {}), (0x00418480, 3, False, {}),
    (0x0047A0B0, 3, False, {}), (0x0047AE10, 1, False, {}), (0x0047ACE0, 0, False, {}),
    # thiscalls on the objects the screens build or hold
    (0x004D78C0, 0, True, {}), (0x004D7990, 3, True, {}), (0x004A9330, 1, True, {}), (0x004A90F0, 2, True, {}),
    (0x00480FA0, 0, True, {}), (0x00422640, 1, True, {}), (0x00421410, 0, True, {}), (0x00421460, 1, True, {}), (0x00422E30, 1, True, {}), (0x00420C10, 1, True, {}),
    (0x004208A0, 3, True, {}), (0x00422D80, 4, True, {}), (0x00422690, 1, True, {}), (0x00480890, 2, True, {}), (0x0042F950, 3, True, {}),
    (0x00429CD0, 0, True, {}), (0x004B6BF0, 0, True, {}), (0x004C29D0, 0, True, {}), (0x0042B4D0, 0, True, {}), (0x0042B880, 0, True, {}),
    (0x0042AE30, 2, True, {}), (0x004B6C00, 1, True, {}), (0x004B6560, 0, True, {}),
]


def install_screen_stubs(fc, env):
    if getattr(fc, "_screens_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._screens_installed = True
    rng = env.rng
    # (the menu group's fake grid vtable slot is 0x0046F480, which is SuperMem_CheckForLeaks here: leave that stub out, one stub per address)
    plain = fc.stub

    def without_grid(address, *args, **kw):
        if address != 0x0046F480:
            plain(address, *args, **kw)

    fc.stub = without_grid
    install_menu_stubs(fc, env, font_keep=None, frame_masks={4: 0xFF, 5: 0xFF, 6: 0xFF})
    del fc.stub
    fc.stub(LIST_V3, 0, "list_v3", convention="thiscall", this=True)
    fc.stub(ELEM_HOOK, 0, "element_hook", convention="thiscall", this=True)
    fc.stub(CHILD_UPDATE, 0, "element_update", convention="thiscall", this=True)
    fc.stub(CHILD_DRAW, 2, "element_draw", convention="thiscall", this=True)
    fc.stub(SNAP_X, 0, "element_snap_x", convention="thiscall", this=True)
    fc.stub(SNAP_Y, 0, "element_snap_y", convention="thiscall", this=True)
    # allocation and files
    fc.stub(0x005002FE, 1, "crt_new", result=env.alloc)
    fc.stub(0x005002F3, 1, "crt_delete")
    fc.stub(0x00448DD0, 2, "file_open", text=True, result=lambda a: 1 + rng.randrange(0x1000))
    fc.stub(0x0044ADA0, 1, "font_load", text=True, result=lambda a: env.slab + FIND_FONTS + FONT_SIZE * rng.randrange(4))
    fc.stub(0x0044B0A0, 1, "font_unload", text=True)
    fc.stub(0x00457420, 4, "sprite_construct", convention="thiscall", this=True, text=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x00457120, 0, "sprite_init", convention="thiscall", this=True)
    fc.stub(0x004D74A0, 3, "engine_004d74a0", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    # the engine's functions
    skip = {0x004D74A0}
    for address, count, thiscall, options in ENGINE:
        if address in skip:
            continue
        fc.stub(address, count, "engine_%08x" % address, convention="thiscall" if thiscall else "cdecl", this=thiscall, **options)
    # scripted results and writes
    fc.stub(0x004B46A0, 2, "engine_004b46a0", text=True, result=lambda a: rng.randrange(0, 0x40))
    fc.stub(0x00452D30, 0, "engine_00452d30", result=lambda a: rng.choice([0, 0, 0, 1]))
    fc.stub(0x00422B10, 0, "engine_00422b10", result=lambda a: rng.choice([0, 1]))
    fc.stub(0x0047AC60, 0, "engine_0047ac60", result=lambda a: rng.choice([0, 0, 1, 2, 3]))
    fc.stub(0x0047FD30, 1, "engine_0047fd30", result=env.alloc)
    fc.stub(0x00480610, 0, "engine_00480610", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x0042F880, 0, "engine_0042f880", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x0041FF50, 3, "engine_0041ff50", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x004BB4F0, 0, "engine_004bb4f0", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x00429E00, 3, "engine_00429e00", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x004C0430, 2, "engine_004c0430", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x004B6D90, 6, "engine_004b6d90", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x00423800, 6, "engine_00423800", result=env.alloc)
    fc.stub(0x004203C0, 0, "engine_004203c0", convention="thiscall", this=True, result=lambda a: rng.randrange(0, 0x40))
    fc.stub(0x004214D0, 0, "engine_004214d0", convention="thiscall", this=True, result=lambda a: env.slab + TEXTS + TEXT_STRIDE * rng.randrange(len(TEXT_POOL)))
    fc.stub(0x0047FE30, 1, "engine_0047fe30", convention="thiscall", this=True, keep=slice(0, 0), text=True)
    fc.stub(0x00420420, 2, "engine_00420420", convention="thiscall", this=True, keep=slice(0, 0))
    fc.stub(0x00418360, 2, "engine_00418360", result=lambda a: rng.choice([0, env.slab + 0x0F00]))
    fc.stub(0x004B43E0, 2, "engine_004b43e0", text=True, result=lambda a: rng.randrange(0, 0x40))
    fc.stub(0x004B47A0, 5, "engine_004b47a0", keep=slice(0, 1), text=True, pokes=[(3, 4, lambda a: rng.randrange(0, 12))],
            result=lambda a: env.slab + 0x0F00)
    fc.stub(0x00480D90, 4, "engine_00480d90", convention="thiscall", this=True, keep=[1, 3])
    # the pages' own entry points a screen calls (these are 4.10d functions; here they are seams with a scripted answer)
    for address in (0x004293B0, 0x0042A9B0, 0x004B6180, 0x004C3D30):
        fc.stub(address, 0, "engine_%08x" % address, convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 0, 1, 1, 2]))
    fc.stub(0x00429CE0, 0, "engine_00429ce0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))
    fc.stub(0x0042BA00, 0, "engine_0042ba00", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))


def install_loop_stubs(fc, env):
    """The screen loops: the clock advances at every Game_FrameTick (so a loop that waits for it ends) and each window's four per-frame calls are
    seams with scripted answers."""
    from opentony.memory import mem

    rng = env.rng
    install_screen_stubs(fc, env)
    # Game_FrameTick moves the clock on
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    for step, update, draw, delete in ((0x0042C940, 0x0042C910, 0x0042C860, 0x0042C6F0), (0x004B8080, 0x004B8020, 0x004B7F40, 0x004B77B0),
                                       (0x004282D0, 0x00427E40, 0x004281E0, 0x004277E0)):
        fc.stub(step, 0, "engine_%08x" % step, convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 1, 2]))
        fc.stub(update, 0, "engine_%08x" % update, convention="thiscall", this=True)
        fc.stub(draw, 2, "engine_%08x" % draw, convention="thiscall", this=True)
        fc.stub(delete, 1, "engine_%08x" % delete, convention="thiscall", this=True)


def install_group_stubs(fc, env):
    install_screen_stubs(fc, env)


GROUP_STUBS = {"crui": install_group_stubs, "crsave": install_group_stubs, "crstat": install_group_stubs, "credit": install_group_stubs,
               "crloop": install_loop_stubs}
